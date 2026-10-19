// SPDX-FileCopyrightText: Copyright 2026 Eden S25 Ultra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/logging.h"
#include "core/file_sys/vfs/vfs.h"

namespace FileSys::ASTRYX {

// A read-only view consisting of the original archive, localized data appended
// virtually, and exactly the changed 24-byte file index records.
// The original game file and save storage are never written or rebuilt.
class Vfs3BatchTranslationView final : public VfsFile {
public:
    struct RecordPatch {
        std::size_t offset{};
        std::array<u8, 24> bytes{};
    };

    Vfs3BatchTranslationView(VirtualFile source_, std::vector<RecordPatch> patches_,
                             std::vector<u8> appendix_)
        : source(std::move(source_)), patches(std::move(patches_)),
          appendix(std::move(appendix_)) {
        std::sort(patches.begin(), patches.end(),
                  [](const RecordPatch& a, const RecordPatch& b) {
                      return a.offset < b.offset;
                  });
    }

    std::string GetName() const override { return source->GetName(); }
    std::size_t GetSize() const override { return source->GetSize() + appendix.size(); }
    bool Resize(std::size_t) override { return false; }
    VirtualDir GetContainingDirectory() const override {
        return source->GetContainingDirectory();
    }
    bool IsWritable() const override { return false; }
    bool IsReadable() const override { return true; }
    std::size_t Write(const u8*, std::size_t, std::size_t) override { return 0; }
    bool Rename(std::string_view) override { return false; }

    std::size_t Read(u8* output, std::size_t len, std::size_t offset = 0) const override {
        if (offset >= GetSize() || len == 0) {
            return 0;
        }
        const auto count = std::min(len, GetSize() - offset);
        const auto sourceSize = source->GetSize();
        const auto originalCount = offset < sourceSize ?
            std::min(count, sourceSize - offset) : 0;
        if (originalCount &&
            source->Read(output, originalCount, offset) != originalCount) {
            return 0;
        }
        if (originalCount != count) {
            const auto appOffset = offset + originalCount - sourceSize;
            std::memcpy(output + originalCount, appendix.data() + appOffset,
                        count - originalCount);
        }
        if (originalCount) {
            auto it = std::lower_bound(
                patches.begin(), patches.end(), offset,
                [](const RecordPatch& rec, std::size_t start) {
                    return rec.offset + rec.bytes.size() <= start;
                });
            while (it != patches.end() && it->offset < offset + originalCount) {
                const auto begin = std::max(offset, it->offset);
                const auto end = std::min(offset + originalCount,
                                          it->offset + it->bytes.size());
                if (begin < end) {
                    std::memcpy(output + (begin - offset),
                                it->bytes.data() + (begin - it->offset), end - begin);
                }
                ++it;
            }
        }
        return count;
    }

private:
    VirtualFile source;
    std::vector<RecordPatch> patches;
    std::vector<u8> appendix;
};

struct Vfs3BatchTranslationResult {
    VirtualFile view;
    u32 candidates{};
    u32 matched{};
    std::size_t appendedBytes{};
};

// This helper MUST be called only for Ni no Kuni's program Title ID.
// modLayers must contain active RomFS mod directories, not the base game.
inline Vfs3BatchTranslationResult TryMakeNiNoKuniPtbrBatchView(
    const VirtualFile& original, const std::vector<VirtualDir>& modLayers) {
    Vfs3BatchTranslationResult result{};
    if (!original || modLayers.empty()) {
        return result;
    }
    constexpr std::size_t kMaxModFiles = 2000;
    constexpr std::size_t kMaxFileBytes = 8 * 1024 * 1024;
    constexpr std::size_t kMaxAppendBytes = 128 * 1024 * 1024;
    constexpr std::size_t kMaxNameTableBytes = 4 * 1024 * 1024;
    constexpr u32 kMenuId = 47314;
    constexpr u32 kExpectedFolderCount = 4800;
    constexpr u32 kExpectedFileCount = 60160;
    const auto baseSize = original->GetSize();
    const auto within = [baseSize](u64 start, u64 size) {
        return start <= baseSize && size <= baseSize - start;
    };
    const auto read32 = [](const u8* p) -> u32 {
        return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
               (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
    };
    const auto write32 = [](u8* p, u32 value) {
        for (std::size_t k = 0; k < 4; ++k) {
            p[k] = static_cast<u8>(value >> (8 * k));
        }
    };

    std::array<u8, 12> header{};
    if (!within(0, header.size()) ||
        original->Read(header.data(), header.size(), 0) != header.size() ||
        std::memcmp(header.data(), "VFS3", 4) != 0 || read32(header.data() + 4) != 1 ||
        read32(header.data() + 8) != kExpectedFolderCount) {
        return result;
    }
    constexpr u64 folderStart = 12;
    const u32 folders = read32(header.data() + 8);
    const u64 folderBytes = static_cast<u64>(folders) * 20;
    if (!within(folderStart, folderBytes)) {
        return result;
    }
    std::vector<u8> folderRecords(static_cast<std::size_t>(folderBytes));
    if (original->Read(folderRecords.data(), folderRecords.size(), folderStart) !=
        folderRecords.size()) {
        return result;
    }
    const u64 fileCountOffset = folderStart + folderBytes;
    std::array<u8, 4> word{};
    if (!within(fileCountOffset, 4) ||
        original->Read(word.data(), 4, fileCountOffset) != 4 ||
        read32(word.data()) != kExpectedFileCount) {
        return result;
    }
    const u32 files = read32(word.data());
    const u64 fileStart = fileCountOffset + 4;
    const u64 fileBytes = static_cast<u64>(files) * 24;
    const u64 namePtrPos = fileStart + fileBytes;
    if (!within(fileStart, fileBytes) || !within(namePtrPos, 4) ||
        original->Read(word.data(), 4, namePtrPos) != 4) {
        return result;
    }
    const u64 nameStart = read32(word.data());
    const u64 baseDataOffset = namePtrPos + 4;
    if (nameStart < baseDataOffset || !within(nameStart, 4) ||
        baseSize - nameStart > kMaxNameTableBytes) {
        return result;
    }
    std::vector<u8> fileRecords(static_cast<std::size_t>(fileBytes));
    std::vector<u8> names(static_cast<std::size_t>(baseSize - nameStart));
    if (original->Read(fileRecords.data(), fileRecords.size(), fileStart) !=
            fileRecords.size() ||
        original->Read(names.data(), names.size(), nameStart) != names.size()) {
        return result;
    }
    std::size_t cursor = 0;
    const auto pop32 = [&]() -> u32 {
        if (cursor > names.size() || names.size() - cursor < 4) {
            cursor = names.size() + 1;
            return 0;
        }
        const auto value = read32(names.data() + cursor);
        cursor += 4;
        return value;
    };
    const auto popName = [&](std::string& value) -> bool {
        const auto length = pop32();
        if (cursor > names.size() || length > 4096 ||
            length > names.size() - cursor) {
            return false;
        }
        value.assign(reinterpret_cast<const char*>(names.data() + cursor), length);
        cursor += length;
        while (!value.empty() && value.back() == '\0') {
            value.pop_back();
        }
        return true;
    };
    if (pop32() != files) {
        return result;
    }
    std::vector<std::string> fileNames(files);
    for (auto& file : fileNames) {
        if (!popName(file)) {
            return result;
        }
    }
    if (pop32() != folders) {
        return result;
    }
    std::vector<std::string> folderNames(folders);
    for (auto& folder : folderNames) {
        if (!popName(folder)) {
            return result;
        }
    }

    std::vector<std::string> folderPaths(folders);
    std::vector<bool> folderValid(folders);
    for (u32 id = 0; id < folders; ++id) {
        std::vector<std::string> parts;
        u32 current = id;
        bool okay = false;
        for (u32 depth = 0; depth < 64; ++depth) {
            if (current == 0xffffffffU) {
                okay = true;
                break;
            }
            if (current >= folders) {
                break;
            }
            const auto* folder = folderRecords.data() +
                                 static_cast<std::size_t>(current) * 20;
            const auto nameId = read32(folder + 4);
            if (nameId >= folders) {
                break;
            }
            if (!folderNames[nameId].empty()) {
                parts.push_back(folderNames[nameId]);
            }
            const auto next = read32(folder + 8);
            if (next == current) {
                break;
            }
            current = next;
        }
        if (!okay) {
            continue;
        }
        for (auto i = parts.rbegin(); i != parts.rend(); ++i) {
            if (!folderPaths[id].empty()) {
                folderPaths[id] += '/';
            }
            folderPaths[id] += *i;
        }
        folderValid[id] = true;
    }

    // Only collect active mod file trees, never the base RomFS.
    std::unordered_map<std::string, VirtualFile> modFiles;
    const auto gather = [&](const auto& recurse, const VirtualDir& dir,
                            std::string path, u32 depth) -> void {
        if (!dir || depth > 16 || modFiles.size() > kMaxModFiles) {
            return;
        }
        for (const auto& file : dir->GetFiles()) {
            if (!file) {
                continue;
            }
            const auto key = path.empty() ? file->GetName() : path + '/' + file->GetName();
            if (key.rfind("data/", 0) == 0 && file->GetSize() > 0 &&
                file->GetSize() <= kMaxFileBytes && modFiles.size() < kMaxModFiles) {
                modFiles.emplace(key, file);
            }
        }
        for (const auto& sub : dir->GetSubdirectories()) {
            if (!sub) {
                continue;
            }
            const auto next = path.empty() ? sub->GetName() : path + '/' + sub->GetName();
            if (next == "data" || next.rfind("data/", 0) == 0) {
                recurse(recurse, sub, next, depth + 1);
            }
        }
    };
    for (const auto& root : modLayers) {
        gather(gather, root, "", 0);
    }
    result.candidates = static_cast<u32>(modFiles.size());
    if (modFiles.size() < 2 || modFiles.size() >= kMaxModFiles) {
        return result;
    }

    std::vector<u8> appendix;
    std::vector<Vfs3BatchTranslationView::RecordPatch> patches;
    bool matchedMenu = false;
    for (u32 index = 0; index < files; ++index) {
        const auto* entry = fileRecords.data() + static_cast<std::size_t>(index) * 24;
        const auto id = read32(entry + 4);
        const auto dirId = read32(entry + 12);
        if (id >= files || dirId >= folders || !folderValid[dirId]) {
            continue;
        }
        std::string name = folderPaths[dirId];
        if (!name.empty()) {
            name += '/';
        }
        name += fileNames[id];
        const auto match = modFiles.find(name);
        if (match == modFiles.end()) {
            continue;
        }
        const auto compressed = read32(entry + 20);
        const u64 physical = baseDataOffset + read32(entry + 16);
        if (physical > nameStart || compressed > nameStart - physical) {
            continue;
        }
        const auto translated = match->second;
        const auto payloadSize = translated->GetSize();
        if (payloadSize == 0 || payloadSize > kMaxFileBytes ||
            appendix.size() > kMaxAppendBytes - payloadSize ||
            baseSize > std::numeric_limits<std::size_t>::max() -
                            appendix.size() - payloadSize) {
            continue;
        }
        const u64 relative = static_cast<u64>(baseSize - baseDataOffset) +
                             appendix.size();
        if (relative > std::numeric_limits<u32>::max() ||
            payloadSize > std::numeric_limits<u32>::max()) {
            continue;
        }
        const auto bytes = translated->ReadAllBytes();
        if (bytes.size() != payloadSize) {
            continue;
        }
        Vfs3BatchTranslationView::RecordPatch patch;
        patch.offset = static_cast<std::size_t>(fileStart) +
                       static_cast<std::size_t>(index) * 24;
        std::memcpy(patch.bytes.data(), entry, patch.bytes.size());
        write32(patch.bytes.data() + 8, 0); // uncompressed translated resource
        write32(patch.bytes.data() + 16, static_cast<u32>(relative));
        write32(patch.bytes.data() + 20, static_cast<u32>(payloadSize));
        patches.emplace_back(std::move(patch));
        appendix.insert(appendix.end(), bytes.begin(), bytes.end());
        if (id == kMenuId &&
            name == "data/menu/gui/titleMenu02/titleMenu02_en.mnupak") {
            constexpr std::array<u8, 6> kPtEasy{'F', 0xc3, 0xa1, 'c', 'i', 'l'};
            matchedMenu = std::search(bytes.begin(), bytes.end(),
                                      kPtEasy.begin(), kPtEasy.end()) != bytes.end();
        }
    }
    result.matched = static_cast<u32>(patches.size());
    result.appendedBytes = appendix.size();
    LOG_INFO(Loader,
             "[ASTRYX PTBR Batch] candidates={} matched={} bytes={} menu_verified={}",
             result.candidates, result.matched, result.appendedBytes, matchedMenu);
    // Activate only with the already-proven Portuguese menu asset.
    // The v14 single-menu implementation remains as a fallback.
    if (patches.size() < 2 || !matchedMenu) {
        return result;
    }
    result.view = std::make_shared<Vfs3BatchTranslationView>(
        original, std::move(patches), std::move(appendix));
    return result;
}
} // namespace FileSys::ASTRYX
