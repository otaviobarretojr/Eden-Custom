// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>

#include "common/assert.h"
#include "common/hex_util.h"
#include "common/logging.h"
#include "common/settings.h"
#ifndef _WIN32
#include "common/string_util.h"
#endif

#include "core/core.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/ips_layer.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/vfs/vfs_cached.h"
#include "core/file_sys/vfs/vfs_layered.h"
#include "core/file_sys/vfs/vfs3_ptbr_overlay.h"
#include "core/file_sys/vfs/vfs_vector.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/ns/language.h"
#include "core/hle/service/set/settings_server.h"
#include "core/loader/loader.h"
#include "core/loader/nso.h"
#include "core/memory/cheat_engine.h"

namespace FileSys {
namespace {

// Builds a read-only view of the game's original data.vfs: only one 24-byte
// index record is virtually replaced and one translated resource is appended.
// No on-disk archive content is changed or copied into an output NSP.
class ASTRYXVfs3MenuOverlay final : public VfsFile {
public:
    ASTRYXVfs3MenuOverlay(VirtualFile base_, std::size_t recordPos_,
                         std::array<u8, 24> record_, std::vector<u8> payload_)
        : base(std::move(base_)), recordPos(recordPos_), record(record_),
          payload(std::move(payload_)) {}

    std::string GetName() const override { return base->GetName(); }
    std::size_t GetSize() const override { return base->GetSize() + payload.size(); }
    bool Resize(std::size_t) override { return false; }
    VirtualDir GetContainingDirectory() const override {
        return base->GetContainingDirectory();
    }
    bool IsWritable() const override { return false; }
    bool IsReadable() const override { return true; }
    std::size_t Write(const u8*, std::size_t, std::size_t) override { return 0; }
    bool Rename(std::string_view) override { return false; }

    std::size_t Read(u8* output, std::size_t length,
                     std::size_t offset = 0) const override {
        const auto fullSize = GetSize();
        if (offset >= fullSize || length == 0) {
            return 0;
        }
        const auto actual = std::min(length, fullSize - offset);
        const auto baseSize = base->GetSize();
        const auto baseLen =
            offset < baseSize ? std::min(actual, baseSize - offset) : 0;
        if (baseLen && base->Read(output, baseLen, offset) != baseLen) {
            return 0;
        }
        const auto appendedLen = actual - baseLen;
        if (appendedLen) {
            const auto appendOffset = offset + baseLen - baseSize;
            std::memcpy(output + baseLen, payload.data() + appendOffset, appendedLen);
        }

        // Reapply the one patched record for reads of arbitrary boundaries.
        if (baseLen && offset < recordPos + record.size() &&
            offset + baseLen > recordPos) {
            const auto begin = std::max(offset, recordPos);
            const auto end = std::min(offset + baseLen, recordPos + record.size());
            std::memcpy(output + (begin - offset),
                        record.data() + (begin - recordPos), end - begin);
        }
        return actual;
    }

private:
    VirtualFile base;
    std::size_t recordPos;
    std::array<u8, 24> record;
    std::vector<u8> payload;
};

constexpr u32 SINGLE_BYTE_MODULUS = 0x100;

constexpr std::array<const char*, 14> EXEFS_FILE_NAMES{
    "main",    "main.npdm", "rtld",    "sdk",     "subsdk0", "subsdk1", "subsdk2",
    "subsdk3", "subsdk4",   "subsdk5", "subsdk6", "subsdk7", "subsdk8", "subsdk9",
};

enum class TitleVersionFormat : u8 {
    ThreeElements, ///< vX.Y.Z
    FourElements,  ///< vX.Y.Z.W
};

std::string FormatTitleVersion(u32 version,
                               TitleVersionFormat format = TitleVersionFormat::ThreeElements) {
    std::array<u8, sizeof(u32)> bytes{};
    bytes[0] = static_cast<u8>(version % SINGLE_BYTE_MODULUS);
    for (std::size_t i = 1; i < bytes.size(); ++i) {
        version /= SINGLE_BYTE_MODULUS;
        bytes[i] = static_cast<u8>(version % SINGLE_BYTE_MODULUS);
    }

    if (format == TitleVersionFormat::FourElements) {
        return fmt::format("v{}.{}.{}.{}", bytes[3], bytes[2], bytes[1], bytes[0]);
    }
    return fmt::format("v{}.{}.{}", bytes[3], bytes[2], bytes[1]);
}

// Returns a directory with name matching name case-insensitive. Returns nullptr if directory
// doesn't have a directory with name.
VirtualDir FindSubdirectoryCaseless(const VirtualDir dir, std::string_view name) {
#ifdef _WIN32
    return dir->GetSubdirectory(name);
#else
    const auto subdirs = dir->GetSubdirectories();
    for (const auto& subdir : subdirs) {
        std::string dir_name = Common::ToLower(subdir->GetName());
        if (dir_name == name) {
            return subdir;
        }
    }

    return nullptr;
#endif
}

std::optional<std::vector<Core::Memory::CheatEntry>> ReadCheatFileFromFolder(
    u64 title_id, const PatchManager::BuildID& build_id_, const VirtualDir& base_path, bool upper) {
    const auto build_id_raw = Common::HexToString(build_id_, upper);
    const auto build_id = build_id_raw.substr(0, sizeof(u64) * 2);
    const auto file = base_path->GetFile(fmt::format("{}.txt", build_id));

    if (file == nullptr) {
        LOG_INFO(Common_Filesystem, "No cheats file found for title_id={:016X}, build_id={}",
                 title_id, build_id);
        return std::nullopt;
    }

    std::vector<u8> data(file->GetSize());
    if (file->Read(data.data(), data.size()) != data.size()) {
        LOG_INFO(Common_Filesystem, "Failed to read cheats file for title_id={:016X}, build_id={}",
                 title_id, build_id);
        return std::nullopt;
    }

    const Core::Memory::TextCheatParser parser;
    return parser.Parse(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
}

void AppendCommaIfNotEmpty(std::string& to, std::string_view with) {
    if (to.empty()) {
        to += with;
    } else {
        to += ", ";
        to += with;
    }
}

bool IsDirValidAndNonEmpty(const VirtualDir& dir) {
    return dir != nullptr && (!dir->GetFiles().empty() || !dir->GetSubdirectories().empty());
}

bool IsVersionedExternalUpdateDisabled(const std::vector<std::string>& disabled, u32 version) {
    const std::string disabled_key = fmt::format("Update@{}", version);
    return std::find(disabled.cbegin(), disabled.cend(), disabled_key) != disabled.cend() ||
           std::find(disabled.cbegin(), disabled.cend(), "Update") != disabled.cend();
}

std::string GetUpdateVersionStringFromSlot(const ContentProvider* provider, u64 update_tid) {
    if (provider == nullptr) {
        return {};
    }

    auto control_nca = provider->GetEntry(update_tid, ContentRecordType::Control);
    if (control_nca == nullptr ||
        control_nca->GetStatus() != Loader::ResultStatus::Success) {
        return {};
    }

    const auto romfs = control_nca->GetRomFS();
    if (romfs == nullptr) {
        return {};
    }

    const auto extracted = ExtractRomFS(romfs);
    if (extracted == nullptr) {
        return {};
    }

    auto nacp_file = extracted->GetFile("control.nacp");
    if (nacp_file == nullptr) {
        nacp_file = extracted->GetFile("Control.nacp");
    }
    if (nacp_file == nullptr) {
        return {};
    }

    NACP nacp{nacp_file};
    return nacp.GetVersionString();
}
YUZU_NO_INLINE std::optional<u64> GetParentApplicationId(const ContentProvider* provider, u64 title_id) {
    return provider == nullptr ? std::nullopt : provider->GetParentApplicationId(title_id);
}
} // Anonymous namespace

PatchManager::PatchManager(u64 title_id_,
                           const Service::FileSystem::FileSystemController& fs_controller_,
                           const ContentProvider& content_provider_)
    : title_id{title_id_}, parent_title_id{GetParentApplicationId(std::addressof(content_provider_), title_id_)}, fs_controller{fs_controller_}, content_provider{content_provider_} {}

PatchManager::~PatchManager() = default;

u64 PatchManager::GetTitleID() const {
    return title_id;
}

VirtualDir PatchManager::GetModificationLoadRoot(bool sdmc) const {
    const auto get_root = [&](u64 id) {
        return sdmc ? fs_controller.GetSDMCModificationLoadRoot(id) : fs_controller.GetModificationLoadRoot(id);
    };
    auto root = get_root(title_id);
    if (!parent_title_id) {
        return root;
    }
    std::vector<VirtualDir> roots{std::move(root), get_root(*parent_title_id)};
    std::erase(roots, nullptr);
    return LayeredVfsDirectory::MakeLayeredDirectory(std::move(roots));
}

std::vector<std::string> PatchManager::GetDisabledAddons() const {
    auto disabled = Settings::values.disabled_addons[title_id];
    if (parent_title_id) {
        const auto& shared = Settings::values.disabled_addons[*parent_title_id];
        disabled.insert(disabled.end(), shared.begin(), shared.end());
    }
    return disabled;
}

VirtualDir PatchManager::PatchExeFS(VirtualDir exefs) const {
    LOG_INFO(Loader, "Patching ExeFS for title_id={:016X}", title_id);

    if (exefs == nullptr)
        return exefs;

    const auto disabled = GetDisabledAddons();

    bool update_disabled = true;
    std::optional<u32> enabled_version;
    bool checked_external = false;
    bool checked_manual = false;

    const auto* content_union = static_cast<const ContentProviderUnion*>(&content_provider);
    const auto update_tid = GetUpdateTitleID(title_id);

    if (content_union) {
        // First, check ExternalContentProvider
        const auto* external_provider = content_union->GetExternalProvider();
        if (external_provider) {
            const auto update_versions = external_provider->ListUpdateVersions(update_tid);

            if (!update_versions.empty()) {
                checked_external = true;
                for (const auto& update_entry : update_versions) {
                    if (!IsVersionedExternalUpdateDisabled(disabled, update_entry.version)) {
                        update_disabled = false;
                        enabled_version = update_entry.version;
                        break;
                    }
                }
            }
        }

        // Also check ManualContentProvider (for Android)
        if (!checked_external) {
            const auto* manual_provider = static_cast<const ManualContentProvider*>(
                content_union->GetSlotProvider(ContentProviderUnionSlot::FrontendManual));
            if (manual_provider) {
                const auto manual_update_versions = manual_provider->ListUpdateVersions(update_tid);

                if (!manual_update_versions.empty()) {
                    checked_manual = true;
                    for (const auto& update_entry : manual_update_versions) {
                        if (!IsVersionedExternalUpdateDisabled(disabled, update_entry.version)) {
                            update_disabled = false;
                            enabled_version = update_entry.version;
                            break;
                        }
                    }
                }
            }
        }
    }

    // check for original NAND style
    // Check NAND if: no external updates exist, OR all external updates are disabled
    if (!checked_external && !checked_manual) {
        // Only enable NAND update if it exists AND is not disabled
        // We need to check if an update actually exists in the content provider
        const bool has_nand_update = content_provider.HasEntry(update_tid, ContentRecordType::Program);

        if (has_nand_update) {
            const bool nand_disabled = std::find(disabled.cbegin(), disabled.cend(), "Update (NAND)") != disabled.cend();
            const bool sdmc_disabled = std::find(disabled.cbegin(), disabled.cend(), "Update (SDMC)") != disabled.cend();
            const bool generic_disabled = std::find(disabled.cbegin(), disabled.cend(), "Update") != disabled.cend();

            if (!nand_disabled && !sdmc_disabled && !generic_disabled) {
                update_disabled = false;
            }
        }
    } else if (update_disabled && content_union) {
        const bool nand_disabled = std::find(disabled.cbegin(), disabled.cend(), "Update (NAND)") != disabled.cend();
        const bool sdmc_disabled = std::find(disabled.cbegin(), disabled.cend(), "Update (SDMC)") != disabled.cend();

        if (!nand_disabled || !sdmc_disabled) {
            const auto nand_sdmc_entries = content_union->ListEntriesFilterOrigin(
                std::nullopt, TitleType::Update, ContentRecordType::Program, update_tid);

            for (const auto& [slot, entry] : nand_sdmc_entries) {
                if (slot == ContentProviderUnionSlot::UserNAND ||
                    slot == ContentProviderUnionSlot::SysNAND) {
                    if (!nand_disabled) {
                        update_disabled = false;
                        break;
                    }
                } else if (slot == ContentProviderUnionSlot::SDMC) {
                    if (!sdmc_disabled) {
                        update_disabled = false;
                        break;
                    }
                }
            }
        }
    }

    // Game Updates
    std::unique_ptr<NCA> update = nullptr;

    // If we have a specific enabled version from external provider, use it
    if (enabled_version.has_value() && content_union) {
        const auto* external_provider = content_union->GetExternalProvider();
        if (external_provider) {
            auto file = external_provider->GetEntryForVersion(update_tid, ContentRecordType::Program, *enabled_version);
            if (file != nullptr) {
                update = std::make_unique<NCA>(file);
            }
        }

        // Also try ManualContentProvider
        if (update == nullptr) {
            const auto* manual_provider = static_cast<const ManualContentProvider*>(
                content_union->GetSlotProvider(ContentProviderUnionSlot::FrontendManual));
            if (manual_provider) {
                auto file = manual_provider->GetEntryForVersion(update_tid, ContentRecordType::Program, *enabled_version);
                if (file != nullptr) {
                    update = std::make_unique<NCA>(file);
                }
            }
        }
    }

    // Fallback to regular content provider if no external update was loaded
    if (update == nullptr && !update_disabled) {
        update = content_provider.GetEntry(update_tid, ContentRecordType::Program);
    }

    if (!update_disabled && update != nullptr && update->GetExeFS() != nullptr) {
        LOG_INFO(Loader, "    ExeFS: Update ({}) applied successfully",
                 FormatTitleVersion(content_provider.GetEntryVersion(update_tid).value_or(0)));
        exefs = update->GetExeFS();
    }

    // LayeredExeFS
    const auto load_dir = GetModificationLoadRoot();
    const auto sdmc_load_dir = GetModificationLoadRoot(true);

    std::vector<VirtualDir> patch_dirs = {sdmc_load_dir};
    if (load_dir != nullptr) {
        const auto load_patch_dirs = load_dir->GetSubdirectories();
        patch_dirs.insert(patch_dirs.end(), load_patch_dirs.begin(), load_patch_dirs.end());
    }

    std::sort(patch_dirs.begin(), patch_dirs.end(),
              [](const VirtualDir& l, const VirtualDir& r) { return l->GetName() < r->GetName(); });

    std::vector<VirtualDir> layers;
    layers.reserve(patch_dirs.size() + 1);
    for (const auto& subdir : patch_dirs) {
        if (std::find(disabled.begin(), disabled.end(), subdir->GetName()) != disabled.end())
            continue;

        auto exefs_dir = FindSubdirectoryCaseless(subdir, "exefs");
        if (exefs_dir != nullptr)
            layers.push_back(std::move(exefs_dir));
    }
    layers.push_back(exefs);

    auto layered = LayeredVfsDirectory::MakeLayeredDirectory(std::move(layers));
    if (layered != nullptr) {
        LOG_INFO(Loader, "    ExeFS: LayeredExeFS patches applied successfully");
        exefs = std::move(layered);
    }

    if (Settings::values.dump_exefs) {
        LOG_INFO(Loader, "Dumping ExeFS for title_id={:016X}", title_id);
        const auto dump_dir = fs_controller.GetModificationDumpRoot(title_id);
        if (dump_dir != nullptr) {
            const auto exefs_dir = GetOrCreateDirectoryRelative(dump_dir, "/exefs");
            VfsRawCopyD(exefs, exefs_dir);
        }
    }

    return exefs;
}

std::vector<VirtualFile> PatchManager::CollectPatches(const std::vector<VirtualDir>& patch_dirs, const std::string& build_id) const {
    const auto disabled = GetDisabledAddons();
    const auto nso_build_id = fmt::format("{:0<64}", build_id);

    std::vector<VirtualFile> out;
    out.reserve(patch_dirs.size());
    for (const auto& subdir : patch_dirs) {
        if (std::find(disabled.cbegin(), disabled.cend(), subdir->GetName()) != disabled.cend())
            continue;

        auto exefs_dir = FindSubdirectoryCaseless(subdir, "exefs");
        if (exefs_dir != nullptr) {
            for (const auto& file : exefs_dir->GetFiles()) {
                if (file->GetExtension() == "ips") {
                    auto name = file->GetName();
                    const auto this_build_id = fmt::format("{:0<64}", name.substr(0, name.find('.')));
                    if (nso_build_id == this_build_id)
                        out.push_back(file);
                } else if (file->GetExtension() == "pchtxt") {
                    IPSwitchCompiler compiler{file};
                    const auto this_build_id = Common::HexToString(compiler.GetBuildID());
                    if (nso_build_id == this_build_id)
                        out.push_back(file);
                }
            }
        }
    }
    return out;
}

std::vector<u8> PatchManager::PatchNSO(const std::vector<u8>& nso, const std::string& name) const {
    if (nso.size() < sizeof(Loader::NSOHeader)) {
        return nso;
    }

    Loader::NSOHeader header;
    std::memcpy(&header, nso.data(), sizeof(header));

    if (header.magic != Common::MakeMagic('N', 'S', 'O', '0')) {
        return nso;
    }

    const auto build_id_raw = Common::HexToString(header.build_id);
    const auto build_id = build_id_raw.substr(0, build_id_raw.find_last_not_of('0') + 1);

    if (Settings::values.dump_nso) {
        LOG_INFO(Loader, "Dumping NSO for name={}, build_id={}, title_id={:016X}", name, build_id,
                 title_id);
        const auto dump_dir = fs_controller.GetModificationDumpRoot(title_id);
        if (dump_dir != nullptr) {
            const auto nso_dir = GetOrCreateDirectoryRelative(dump_dir, "/nso");
            const auto file = nso_dir->CreateFile(fmt::format("{}-{}.nso", name, build_id));

            file->Resize(nso.size());
            file->WriteBytes(nso);
        }
    }

    LOG_INFO(Loader, "Patching NSO for name={}, build_id={}", name, build_id);

    const auto load_dir = GetModificationLoadRoot();
    if (load_dir == nullptr) {
        LOG_ERROR(Loader, "Cannot load mods for invalid title_id={:016X}", title_id);
        return nso;
    }

    auto patch_dirs = load_dir->GetSubdirectories();
    std::sort(patch_dirs.begin(), patch_dirs.end(),
              [](const VirtualDir& l, const VirtualDir& r) { return l->GetName() < r->GetName(); });
    const auto patches = CollectPatches(patch_dirs, build_id);

    auto out = nso;
    for (const auto& patch_file : patches) {
        if (patch_file->GetExtension() == "ips") {
            LOG_INFO(Loader, "    - Applying IPS patch from mod \"{}\"",
                     patch_file->GetContainingDirectory()->GetParentDirectory()->GetName());
            const auto patched = PatchIPS(std::make_shared<VectorVfsFile>(out), patch_file);
            if (patched != nullptr)
                out = patched->ReadAllBytes();
        } else if (patch_file->GetExtension() == "pchtxt") {
            LOG_INFO(Loader, "    - Applying IPSwitch patch from mod \"{}\"",
                     patch_file->GetContainingDirectory()->GetParentDirectory()->GetName());
            const IPSwitchCompiler compiler{patch_file};
            const auto patched = compiler.Apply(std::make_shared<VectorVfsFile>(out));
            if (patched != nullptr)
                out = patched->ReadAllBytes();
        }
    }

    if (out.size() < sizeof(Loader::NSOHeader)) {
        return nso;
    }

    std::memcpy(out.data(), &header, sizeof(header));
    return out;
}

bool PatchManager::HasNSOPatch(const BuildID& build_id_, std::string_view name) const {
    const auto build_id_raw = Common::HexToString(build_id_);
    const auto build_id = build_id_raw.substr(0, build_id_raw.find_last_not_of('0') + 1);

    LOG_INFO(Loader, "Querying NSO patch existence for build_id={}, name={}", build_id, name);

    const auto load_dir = GetModificationLoadRoot();
    if (load_dir == nullptr) {
        LOG_ERROR(Loader, "Cannot load mods for invalid title_id={:016X}", title_id);
        return false;
    }

    auto patch_dirs = load_dir->GetSubdirectories();
    std::sort(patch_dirs.begin(), patch_dirs.end(),
              [](const VirtualDir& l, const VirtualDir& r) { return l->GetName() < r->GetName(); });

    return !CollectPatches(patch_dirs, build_id).empty();
}

std::vector<Core::Memory::CheatEntry> PatchManager::CreateCheatList(const BuildID& build_id_) const {
    const auto load_dir = GetModificationLoadRoot();
    if (load_dir == nullptr) {
        LOG_ERROR(Loader, "Cannot load mods for invalid title_id={:016X}", title_id);
        return {};
    }

    const auto disabled = GetDisabledAddons();
    auto patch_dirs = load_dir->GetSubdirectories();
    std::sort(patch_dirs.begin(), patch_dirs.end(), [](auto const& l, auto const& r) { return l->GetName() < r->GetName(); });

    // <mod dir> / <folder> / cheats / <build id>.txt
    std::vector<Core::Memory::CheatEntry> out;
    for (const auto& subdir : patch_dirs) {
        if (std::find(disabled.cbegin(), disabled.cend(), subdir->GetName()) == disabled.cend()) {
            if (auto cheats_dir = FindSubdirectoryCaseless(subdir, "cheats"); cheats_dir != nullptr) {
                if (auto const res = ReadCheatFileFromFolder(title_id, build_id_, cheats_dir, true))
                    std::copy(res->begin(), res->end(), std::back_inserter(out));
                if (auto const res = ReadCheatFileFromFolder(title_id, build_id_, cheats_dir, false))
                    std::copy(res->begin(), res->end(), std::back_inserter(out));
            }
        }
    }
    // Uncareless user-friendly loading of patches (must start with 'cheat_')
    // <mod dir> / <cheat file>.txt
    for (auto const& f : load_dir->GetFiles()) {
        auto const name = f->GetName();
        if (name.starts_with("cheat_") && std::find(disabled.cbegin(), disabled.cend(), name) == disabled.cend()) {
            std::vector<u8> data(f->GetSize());
            if (f->Read(data.data(), data.size()) == data.size()) {
                const Core::Memory::TextCheatParser parser;
                auto const res = parser.Parse(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
                std::copy(res.begin(), res.end(), std::back_inserter(out));
            } else {
                LOG_INFO(Common_Filesystem, "Failed to read cheats file for title_id={:016X}", title_id);
            }
        }
    }
    return out;
}

void PatchManager::ApplyLayeredFS(VirtualFile& romfs, ContentRecordType type) const {
    const auto load_dir = GetModificationLoadRoot();
    const auto sdmc_load_dir = GetModificationLoadRoot(true);
    if ((type != ContentRecordType::Program && type != ContentRecordType::Data &&
         type != ContentRecordType::HtmlDocument) ||
        (load_dir == nullptr && sdmc_load_dir == nullptr)) {
        return;
    }

    const auto disabled = GetDisabledAddons();
    std::vector<VirtualDir> patch_dirs = load_dir->GetSubdirectories();
    if (std::find(disabled.cbegin(), disabled.cend(), "SDMC") == disabled.cend()) {
        patch_dirs.push_back(sdmc_load_dir);
    }
    std::sort(patch_dirs.begin(), patch_dirs.end(),
              [](const VirtualDir& l, const VirtualDir& r) { return l->GetName() < r->GetName(); });

    std::vector<VirtualDir> layers;
    std::vector<VirtualDir> layers_ext;
    layers.reserve(patch_dirs.size() + 1);
    layers_ext.reserve(patch_dirs.size() + 1);
    // Diagnostic only: Ni no Kuni (0100E5600D446000) official base + PKG PT-BR.
    // Record whether its difficulty-menu resource exists in each source layer,
    // in the original RomFS, and in the final merged view. Never dump game data.
    constexpr u64 kNiNoKuniId = 0x0100E5600D446000ULL;
    constexpr std::string_view kTitleMenuAsset =
        "data/menu/gui/titleMenu02/titleMenu02_en.mnupak";
    const bool auditNiNoKuni = title_id == kNiNoKuniId &&
                              type == ContentRecordType::Program;
    VirtualFile vfsOriginalForMenuPatch;
    std::optional<std::size_t> menuRecordPosition;
    std::size_t vfsPayloadBase = 0;

    const auto auditTitleMenu = [&](std::string_view origin, const VirtualDir& directory) {
        if (!auditNiNoKuni || !directory) {
            return;
        }
        const auto file = directory->GetFileRelative(kTitleMenuAsset);
        if (!file) {
            LOG_INFO(Loader, "[ASTRYX PTBR Audit] {}: titleMenu02_en.mnupak missing", origin);
            return;
        }
        const auto size = file->GetSize();
        bool containsPtEasy = false;
        bool containsEnEasy = false;
        if (size > 0 && size <= 4 * 1024 * 1024) {
            const auto bytes = file->ReadAllBytes();
            constexpr std::array<u8, 6> portugueseEasy{
                'F', 0xC3, 0xA1, 'c', 'i', 'l'};
            constexpr std::array<u8, 4> englishEasy{'E', 'a', 's', 'y'};
            containsPtEasy =
                std::search(bytes.begin(), bytes.end(), portugueseEasy.begin(),
                            portugueseEasy.end()) != bytes.end();
            containsEnEasy =
                std::search(bytes.begin(), bytes.end(), englishEasy.begin(),
                            englishEasy.end()) != bytes.end();
        }
        LOG_INFO(Loader,
                 "[ASTRYX PTBR Audit] {}: titleMenu02_en.mnupak size={} "
                 "contains_pt_easy={} contains_en_easy={}",
                 origin, size, containsPtEasy, containsEnEasy);
    };
    for (const auto& subdir : patch_dirs) {
        if (std::find(disabled.cbegin(), disabled.cend(), subdir->GetName()) != disabled.cend()) {
            continue;
        }

        auto romfs_dir = FindSubdirectoryCaseless(subdir, "romfs");
        if (romfs_dir != nullptr) {
            if (auditNiNoKuni) {
                auditTitleMenu(subdir->GetName(), romfs_dir);
            }
            layers.emplace_back(std::make_shared<CachedVfsDirectory>(std::move(romfs_dir)));
        }

        // Support for romfslite introduced in Atmosphere 1.9.5
        auto romfslite_dir = FindSubdirectoryCaseless(subdir, "romfslite");
        if (romfslite_dir != nullptr)
            layers.emplace_back(std::make_shared<CachedVfsDirectory>(std::move(romfslite_dir)));

        auto ext_dir = FindSubdirectoryCaseless(subdir, "romfs_ext");
        if (ext_dir != nullptr)
            layers_ext.emplace_back(std::make_shared<CachedVfsDirectory>(std::move(ext_dir)));

        if (type == ContentRecordType::HtmlDocument) {
            auto manual_dir = FindSubdirectoryCaseless(subdir, "manual_html");
            if (manual_dir != nullptr)
                layers.emplace_back(std::make_shared<CachedVfsDirectory>(std::move(manual_dir)));
        }
    }

    // When there are no layers to apply, return early as there is no need to rebuild the RomFS
    if (layers.empty() && layers_ext.empty()) {
        return;
    }

    auto extracted = ExtractRomFS(romfs);
    if (extracted == nullptr) {
        return;
    }

    if (auditNiNoKuni) {
        auditTitleMenu("original RomFS", extracted);

        // Ni no Kuni VFS3 format probe: read ONLY the first 48 bytes.
        // No archive data is extracted, modified, or exported. Layout fields
        // remain unlabeled because VFS3 has incompatible variants across games.
        const auto originalDataVfs = extracted->GetFile("data.vfs");
        if (!originalDataVfs) {
            LOG_INFO(Loader, "[ASTRYX VFS3 Audit] original data.vfs missing");
        } else {
            const auto archiveSize = originalDataVfs->GetSize();
            std::array<u8, 48> header{};
            const auto wantedBytes = std::min<std::size_t>(header.size(), archiveSize);
            const auto receivedBytes = originalDataVfs->Read(header.data(), wantedBytes, 0);
            const bool hasVfs3Magic =
                receivedBytes >= 4 && std::memcmp(header.data(), "VFS3", 4) == 0;
            LOG_INFO(Loader, "[ASTRYX VFS3 Audit] data.vfs size={} "
                     "header_bytes={} magic_vfs3={}",
                     archiveSize, receivedBytes, hasVfs3Magic);
            if (hasVfs3Magic && receivedBytes >= 36) {
                // Uninterpreted little-endian header words: mapping/semantics
                // must be verified against the FIRST game's specific format.
                const auto headerWord32 = [&](std::size_t pos) -> u32 {
                    return static_cast<u32>(header[pos]) |
                           (static_cast<u32>(header[pos + 1]) << 8) |
                           (static_cast<u32>(header[pos + 2]) << 16) |
                           (static_cast<u32>(header[pos + 3]) << 24);
                };
                LOG_INFO(Loader,
                         "[ASTRYX VFS3 Audit] le32_at_04={} le32_at_08={} "
                         "le32_at_0c={} le32_at_10={} le32_at_14={} "
                         "le32_at_18={} le32_at_1c={} le32_at_20={}",
                         headerWord32(4), headerWord32(8), headerWord32(12),
                         headerWord32(16), headerWord32(20), headerWord32(24),
                         headerWord32(28), headerWord32(32));

                // The VFS3 v1 format is documented by killer7_vfs.bms (QuickBMS
                // script 0.2). Version 1 uses 20-byte folder entries,
                // 24-byte file entries and a 32-bit names pointer, unlike
                // version 16+ VFS3. Read metadata only and validate bounds.
                const auto archiveHas = [&](u64 pos, u64 len) {
                    return pos <= archiveSize && len <= archiveSize - pos;
                };
                const auto readVfsU32 = [&](u64 pos, u32& result) {
                    if (!archiveHas(pos, 4)) {
                        return false;
                    }
                    std::array<u8, 4> bytes{};
                    if (originalDataVfs->Read(bytes.data(), bytes.size(), pos) !=
                        bytes.size()) {
                        return false;
                    }
                    result = static_cast<u32>(bytes[0]) |
                             (static_cast<u32>(bytes[1]) << 8) |
                             (static_cast<u32>(bytes[2]) << 16) |
                             (static_cast<u32>(bytes[3]) << 24);
                    return true;
                };

                const u32 vfsVersion = headerWord32(4);
                const u32 folderCount = headerWord32(8);
                if (vfsVersion == 1 && folderCount > 0 && folderCount <= 100000) {
                    constexpr u64 kFolderTableStart = 12;
                    constexpr u64 kFolderEntryBytes = 20;
                    constexpr u64 kFileEntryBytes = 24;
                    const u64 fileCountOffset =
                        kFolderTableStart + static_cast<u64>(folderCount) * kFolderEntryBytes;
                    u32 fileCount = 0;
                    if (readVfsU32(fileCountOffset, fileCount) &&
                        fileCount > 0 && fileCount <= 1000000) {
                        const u64 fileTableStart = fileCountOffset + 4;
                        const u64 fileTableBytes = static_cast<u64>(fileCount) * kFileEntryBytes;
                        const u64 namesPointerPos = fileTableStart + fileTableBytes;
                        u32 namesOffset = 0;
                        if (archiveHas(fileTableStart, fileTableBytes) &&
                            readVfsU32(namesPointerPos, namesOffset)) {
                            const u64 dataBaseOffset = namesPointerPos + 4;
                            const bool validNamesPointer =
                                namesOffset >= dataBaseOffset &&
                                archiveHas(namesOffset, 4);
                            LOG_INFO(Loader,
                                     "[ASTRYX VFS3 V1] folders={} files={} "
                                     "file_table_start={} names_ptr_at={} "
                                     "data_base={} names_offset={} "
                                     "names_pointer_valid={}",
                                     folderCount, fileCount, fileTableStart,
                                     namesPointerPos, dataBaseOffset, namesOffset,
                                     validNamesPointer);
                            if (validNamesPointer) {
                                u32 nameCount = 0;
                                if (readVfsU32(namesOffset, nameCount)) {
                                    LOG_INFO(Loader,
                                             "[ASTRYX VFS3 V1] file_names_count={} "
                                             "matches_file_count={}",
                                             nameCount, nameCount == fileCount);
                                }
                            }
                            // Targeted Ni no Kuni archive path resolution. The
                            // QuickBMS VFS3 v1 names table uses an LE32 count
                            // and LE32 byte-length-prefixed strings. Read at
                            // most 4 MiB of metadata, never resource payloads.
                            if (validNamesPointer) {
                                constexpr u64 kMaxNameTableBytes = 4 * 1024 * 1024;
                                const u64 nameTableLen = archiveSize - namesOffset;
                                if (nameTableLen <= kMaxNameTableBytes) {
                                    std::vector<u8> table(static_cast<std::size_t>(nameTableLen));
                                    if (originalDataVfs->Read(table.data(), table.size(),
                                                              namesOffset) == table.size()) {
                                        std::size_t cursor = 0;
                                        const auto readTableU32 = [&](u32& val) -> bool {
                                            if (cursor > table.size() ||
                                                table.size() - cursor < 4) {
                                                return false;
                                            }
                                            val = static_cast<u32>(table[cursor]) |
                                                  (static_cast<u32>(table[cursor + 1]) << 8) |
                                                  (static_cast<u32>(table[cursor + 2]) << 16) |
                                                  (static_cast<u32>(table[cursor + 3]) << 24);
                                            cursor += 4;
                                            return true;
                                        };
                                        const auto readTableName = [&](std::string& val) -> bool {
                                            u32 nameLen = 0;
                                            if (!readTableU32(nameLen) || nameLen > 4096 ||
                                                cursor > table.size() ||
                                                nameLen > table.size() - cursor) {
                                                return false;
                                            }
                                            val.assign(reinterpret_cast<const char*>(
                                                           table.data() + cursor), nameLen);
                                            while (!val.empty() && val.back() == '\0') {
                                                val.pop_back();
                                            }
                                            cursor += nameLen;
                                            return true;
                                        };
                                        u32 tableFileCount = 0;
                                        bool namesValid = readTableU32(tableFileCount) &&
                                                          tableFileCount == fileCount;
                                        std::vector<u32> targetFileIds;
                                        if (namesValid) {
                                            for (u32 id = 0; id < tableFileCount; ++id) {
                                                std::string fileName;
                                                if (!readTableName(fileName)) {
                                                    namesValid = false;
                                                    break;
                                                }
                                                if (fileName.find("titleMenu02_en.mnupak") !=
                                                        std::string::npos &&
                                                    targetFileIds.size() < 8) {
                                                    targetFileIds.push_back(id);
                                                }
                                            }
                                        }
                                        u32 tableFolderCount = 0;
                                        if (namesValid) {
                                            namesValid = readTableU32(tableFolderCount) &&
                                                         tableFolderCount <= 100000;
                                        }
                                        std::vector<std::string> folderNames;
                                        if (namesValid) {
                                            folderNames.reserve(tableFolderCount);
                                            for (u32 id = 0; id < tableFolderCount; ++id) {
                                                std::string name;
                                                if (!readTableName(name)) {
                                                    namesValid = false;
                                                    break;
                                                }
                                                folderNames.emplace_back(std::move(name));
                                            }
                                        }
                                        LOG_INFO(Loader,
                                                 "[ASTRYX VFS3 Names] parsed={} files={} "
                                                 "folders={} menu_name_matches={}",
                                                 namesValid, tableFileCount, tableFolderCount,
                                                 targetFileIds.size());
                                        if (namesValid && !targetFileIds.empty()) {
                                            constexpr std::size_t kBatchEntries = 256;
                                            std::array<u8, kBatchEntries * 24> records{};
                                            u32 matchesLogged = 0;
                                            for (u32 start = 0; start < fileCount &&
                                                              matchesLogged < targetFileIds.size();
                                                 start += kBatchEntries) {
                                                const std::size_t count =
                                                    std::min<std::size_t>(kBatchEntries,
                                                                          fileCount - start);
                                                const u64 pos = fileTableStart +
                                                                static_cast<u64>(start) * 24;
                                                const std::size_t bytes = count * 24;
                                                if (originalDataVfs->Read(records.data(), bytes,
                                                                          pos) != bytes) {
                                                    break;
                                                }
                                                const auto recordU32 = [&](std::size_t off) -> u32 {
                                                    return static_cast<u32>(records[off]) |
                                                           (static_cast<u32>(records[off + 1]) << 8) |
                                                           (static_cast<u32>(records[off + 2]) << 16) |
                                                           (static_cast<u32>(records[off + 3]) << 24);
                                                };
                                                for (std::size_t j = 0; j < count; ++j) {
                                                    const std::size_t off = j * 24;
                                                    const u32 entryFileId = recordU32(off + 4);
                                                    if (std::find(targetFileIds.begin(),
                                                                  targetFileIds.end(), entryFileId) ==
                                                        targetFileIds.end()) {
                                                        continue;
                                                    }
                                                    const u32 flags = recordU32(off + 8);
                                                    const u32 folderId = recordU32(off + 12);
                                                    const u32 offset = recordU32(off + 16);
                                                    const u32 size = recordU32(off + 20);
                                                    std::string folderPath;
                                                    u32 current = folderId;
                                                    u32 depth = 0;
                                                    while (current != 0xffffffff &&
                                                           current < folderCount && depth < 64) {
                                                        std::array<u8, 20> folder{};
                                                        const u64 folderPos =
                                                            kFolderTableStart +
                                                            static_cast<u64>(current) * 20;
                                                        if (originalDataVfs->Read(folder.data(),
                                                                                  folder.size(),
                                                                                  folderPos) !=
                                                            folder.size()) {
                                                            break;
                                                        }
                                                        const auto folderU32 = [&](std::size_t o) {
                                                            return static_cast<u32>(folder[o]) |
                                                                (static_cast<u32>(folder[o + 1]) << 8) |
                                                                (static_cast<u32>(folder[o + 2]) << 16) |
                                                                (static_cast<u32>(folder[o + 3]) << 24);
                                                        };
                                                        const u32 folderNameId = folderU32(4);
                                                        if (folderNameId >= folderNames.size()) {
                                                            break;
                                                        }
                                                        const auto& folderName =
                                                            folderNames[folderNameId];
                                                        if (!folderName.empty()) {
                                                            folderPath = folderName +
                                                                         (folderPath.empty() ? "" : "/") +
                                                                         folderPath;
                                                        }
                                                        current = folderU32(8);
                                                        ++depth;
                                                    }
                                                    if (folderPath.size() > 256) {
                                                        folderPath.resize(256);
                                                    }
                                                    const u64 absoluteOffset =
                                                        dataBaseOffset + static_cast<u64>(offset);
                                                    if (folderPath ==
                                                            "data/menu/gui/titleMenu02" &&
                                                        entryFileId == 47314 && flags == 3 &&
                                                        archiveHas(absoluteOffset, size) &&
                                                        absoluteOffset + size <= namesOffset &&
                                                        !menuRecordPosition) {
                                                        menuRecordPosition =
                                                            static_cast<std::size_t>(pos +
                                                                static_cast<u64>(j) * 24);
                                                        vfsOriginalForMenuPatch =
                                                            originalDataVfs;
                                                        vfsPayloadBase =
                                                            static_cast<std::size_t>(dataBaseOffset);
                                                        LOG_INFO(Loader,
                                                                 "[ASTRYX PTBR Virtual] "
                                                                 "target_verified=true "
                                                                 "record_offset={}",
                                                                 *menuRecordPosition);
                                                    }
                                                    LOG_INFO(Loader,
                                                             "[ASTRYX VFS3 Names] menu_file_id={} "
                                                             "table_index={} dir={} flags={} "
                                                             "rel_offset={} size={} data_valid={}",
                                                             entryFileId, start + j, folderPath,
                                                             flags, offset, size,
                                                             archiveHas(absoluteOffset, size) &&
                                                                 absoluteOffset + size <= namesOffset);
                                                    ++matchesLogged;
                                                }
                                            }
                                        }
                                    } else {
                                        LOG_WARNING(Loader,
                                                    "[ASTRYX VFS3 Names] metadata read failed");
                                    }
                                } else {
                                    LOG_INFO(Loader,
                                             "[ASTRYX VFS3 Names] name table too large: {} bytes",
                                             nameTableLen);
                                }
                            }

                            for (u32 idx = 0; idx < std::min<u32>(fileCount, 3); ++idx) {
                                const u64 pos =
                                    fileTableStart + static_cast<u64>(idx) * kFileEntryBytes;
                                std::array<u8, 24> entry{};
                                if (originalDataVfs->Read(entry.data(), entry.size(), pos) !=
                                    entry.size()) {
                                    break;
                                }
                                const auto entryU32 = [&](std::size_t off) -> u32 {
                                    return static_cast<u32>(entry[off]) |
                                           (static_cast<u32>(entry[off + 1]) << 8) |
                                           (static_cast<u32>(entry[off + 2]) << 16) |
                                           (static_cast<u32>(entry[off + 3]) << 24);
                                };
                                const auto fileId = entryU32(4);
                                const auto flags = entryU32(8);
                                const auto folderId = entryU32(12);
                                const auto dataRelative = entryU32(16);
                                const auto dataSize = entryU32(20);
                                const u64 absoluteData =
                                    dataBaseOffset + static_cast<u64>(dataRelative);
                                const bool validBounds = archiveHas(absoluteData, dataSize) &&
                                    (!validNamesPointer ||
                                     (absoluteData <= namesOffset &&
                                      dataSize <= namesOffset - absoluteData));
                                LOG_INFO(Loader,
                                         "[ASTRYX VFS3 V1] sample={} file_id={} "
                                         "flags={} folder_id={} rel_offset={} size={} "
                                         "within_payload={}",
                                         idx, fileId, flags, folderId, dataRelative,
                                         dataSize, validBounds);
                            }
                        } else {
                            LOG_WARNING(Loader,
                                        "[ASTRYX VFS3 V1] file table or names pointer "
                                        "out of archive bounds");
                        }
                    } else {
                        LOG_WARNING(Loader,
                                    "[ASTRYX VFS3 V1] file count unavailable or implausible "
                                    "at offset={}",
                                    fileCountOffset);
                    }
                } else {
                    LOG_INFO(Loader, "[ASTRYX VFS3 V1] unsupported version={} "
                             "or folder count={}",
                             vfsVersion, folderCount);
                }
            } else if (!hasVfs3Magic) {
                LOG_WARNING(Loader, "[ASTRYX VFS3 Audit] original archive is not VFS3 "
                            "or the header could not be read");
            }
        }

        // A modified NSP is explicitly required by the translation's own README.
        // List ONLY bounded names from the original game's packaged directory tree,
        // never file contents, to identify the missing resource layout safely.
        constexpr std::array<std::string_view, 4> kProbeDirs{
            "data", "data/menu", "data/menu/gui", "data/menu/gui/titleMenu02"};
        for (const auto relative : kProbeDirs) {
            const auto folder = extracted->GetDirectoryRelative(relative);
            LOG_INFO(Loader, "[ASTRYX PTBR Layout] original directory {}: {}",
                     relative, folder ? "present" : "missing");
            if (folder && relative == "data/menu/gui/titleMenu02") {
                const auto files = folder->GetFiles();
                LOG_INFO(Loader, "[ASTRYX PTBR Layout] original titleMenu02 file count={}",
                         files.size());
                for (std::size_t i = 0; i < std::min<std::size_t>(files.size(), 12); ++i) {
                    LOG_INFO(Loader, "[ASTRYX PTBR Layout] original menu file {}",
                             files[i]->GetName());
                }
            }
        }
        const auto rootDirs = extracted->GetSubdirectories();
        LOG_INFO(Loader, "[ASTRYX PTBR Layout] original root directory count={}",
                 rootDirs.size());
        for (std::size_t i = 0; i < std::min<std::size_t>(rootDirs.size(), 16); ++i) {
            LOG_INFO(Loader, "[ASTRYX PTBR Layout] original root directory {}",
                     rootDirs[i]->GetName());
        }
        const auto rootFiles = extracted->GetFiles();
        LOG_INFO(Loader, "[ASTRYX PTBR Layout] original root file count={}",
                 rootFiles.size());
        for (std::size_t i = 0; i < std::min<std::size_t>(rootFiles.size(), 16); ++i) {
            LOG_INFO(Loader, "[ASTRYX PTBR Layout] original root file {}",
                     rootFiles[i]->GetName());
        }
    }
    // Capture active mod layers only, so the batch reader never enumerates
    // files originating in the official RomFS.
    const auto ptbrModLayers = auditNiNoKuni ? layers : std::vector<VirtualDir>{};
    layers.emplace_back(std::move(extracted));

    auto layered = LayeredVfsDirectory::MakeLayeredDirectory(std::move(layers));
    auto layered_ext = LayeredVfsDirectory::MakeLayeredDirectory(std::move(layers_ext));
    if (layered == nullptr && layered_ext == nullptr) {
        return;
    }

    if (auditNiNoKuni) {
        auditTitleMenu("merged LayeredFS", layered);
        const auto batch = ASTRYX::TryMakeNiNoKuniPtbrBatchView(
            vfsOriginalForMenuPatch, ptbrModLayers);
        bool batchInstalled = false;
        if (batch.view) {
            auto batchRoot = std::make_shared<VectorVfsDirectory>(
                std::vector<VirtualFile>{batch.view}, std::vector<VirtualDir>{}, "");
            auto batchLayered = LayeredVfsDirectory::MakeLayeredDirectory(
                std::vector<VirtualDir>{batchRoot, layered});
            if (batchLayered) {
                layered = std::move(batchLayered);
                batchInstalled = true;
                LOG_INFO(Loader,
                         "[ASTRYX PTBR Batch] applied=true files={} bytes={} "
                         "source_size={} virtual_size={}",
                         batch.matched, batch.appendedBytes,
                         vfsOriginalForMenuPatch->GetSize(), batch.view->GetSize());
            }
        }
        // Preserve the already-proven v14 one-file translation if a batch
        // was not eligible or an active full pack could not be validated.
        if (!batchInstalled) {
            const auto translated = layered->GetFileRelative(kTitleMenuAsset);
        if (translated && vfsOriginalForMenuPatch && menuRecordPosition &&
            vfsPayloadBase < vfsOriginalForMenuPatch->GetSize()) {
            const auto translatedSize = translated->GetSize();
            const auto originalSize = vfsOriginalForMenuPatch->GetSize();
            const auto relativeNewOffset = originalSize - vfsPayloadBase;
            constexpr std::size_t kMaxMenuSize = 4 * 1024 * 1024;
            if (translatedSize > 0 && translatedSize <= kMaxMenuSize &&
                translatedSize <= std::numeric_limits<u32>::max() &&
                relativeNewOffset <= std::numeric_limits<u32>::max() &&
                originalSize <= std::numeric_limits<std::size_t>::max() -
                                    translatedSize) {
                auto translatedBytes = translated->ReadAllBytes();
                constexpr std::array<u8, 6> kPtEasy{'F', 0xC3, 0xA1, 'c', 'i', 'l'};
                const bool verifiedTranslation =
                    translatedBytes.size() == translatedSize &&
                    std::search(translatedBytes.begin(), translatedBytes.end(),
                                kPtEasy.begin(), kPtEasy.end()) != translatedBytes.end();
                std::array<u8, 24> record{};
                const bool gotRecord =
                    vfsOriginalForMenuPatch->Read(record.data(), record.size(),
                                                  *menuRecordPosition) == record.size();
                const auto loadLe32 = [&](std::size_t off) -> u32 {
                    return static_cast<u32>(record[off]) |
                           (static_cast<u32>(record[off + 1]) << 8) |
                           (static_cast<u32>(record[off + 2]) << 16) |
                           (static_cast<u32>(record[off + 3]) << 24);
                };
                if (verifiedTranslation && gotRecord &&
                    loadLe32(4) == 47314 && loadLe32(8) == 3) {
                    const auto writeLe32 = [&](std::size_t off, u32 val) {
                        for (std::size_t i = 0; i < 4; ++i) {
                            record[off + i] = static_cast<u8>(val >> (i * 8));
                        }
                    };
                    // VFS3 v1 flags & 2 means compressed. Override only this
                    // file as raw data (flags=0); all other 60,159 records and
                    // all original archive bytes stay unchanged.
                    writeLe32(8, 0);
                    writeLe32(16, static_cast<u32>(relativeNewOffset));
                    writeLe32(20, static_cast<u32>(translatedSize));

                    auto view = std::make_shared<ASTRYXVfs3MenuOverlay>(
                        vfsOriginalForMenuPatch, *menuRecordPosition,
                        record, std::move(translatedBytes));
                    auto rootOverride = std::make_shared<VectorVfsDirectory>(
                        std::vector<VirtualFile>{view}, std::vector<VirtualDir>{}, "");
                    auto patchedLayered = LayeredVfsDirectory::MakeLayeredDirectory(
                        std::vector<VirtualDir>{rootOverride, layered});
                    if (patchedLayered) {
                        layered = std::move(patchedLayered);
                        LOG_INFO(Loader,
                                 "[ASTRYX PTBR Virtual] applied=true source_size={} "
                                 "virtual_size={} menu_size={} raw_flags=0 "
                                 "target_file_id=47314",
                                 originalSize, view->GetSize(), translatedSize);
                    }
                } else {
                    LOG_WARNING(Loader,
                                "[ASTRYX PTBR Virtual] applied=false "
                                "reason=invalid_translation_or_record");
                }
            } else {
                LOG_INFO(Loader,
                         "[ASTRYX PTBR Virtual] applied=false "
                         "reason=translation_size_or_offset_bounds");
            }
        } else {
            LOG_INFO(Loader,
                     "[ASTRYX PTBR Virtual] applied=false reason=missing_verified_target");
        }
        }
    }
    auto packed = CreateRomFS(std::move(layered), std::move(layered_ext));
    if (packed == nullptr) {
        return;
    }

    LOG_INFO(Loader, "    RomFS: LayeredFS patches applied successfully");
    romfs = std::move(packed);
}

VirtualFile PatchManager::PatchRomFS(const NCA* base_nca, VirtualFile base_romfs,
                                     ContentRecordType type, VirtualFile packed_update_raw,
                                     bool apply_layeredfs) const {
    const auto log_string = fmt::format("Patching RomFS for title_id={:016X}, type={:02X}",
                                        title_id, static_cast<u8>(type));
    if (type == ContentRecordType::Program || type == ContentRecordType::Data) {
        LOG_INFO(Loader, "{}", log_string);
    } else {
        LOG_DEBUG(Loader, "{}", log_string);
    }

    auto romfs = base_romfs;

    // Game Updates
    const auto update_tid = GetUpdateTitleID(title_id);
    const auto disabled = GetDisabledAddons();

    bool update_disabled = true;
    std::optional<u32> enabled_version;
    VirtualFile update_raw = nullptr;
    bool checked_external = false;
    bool checked_manual = false;

    const auto* content_union = static_cast<const ContentProviderUnion*>(&content_provider);
    if (content_union) {
        // First, check ExternalContentProvider
        const auto* external_provider = content_union->GetExternalProvider();
        if (external_provider) {
            const auto update_versions = external_provider->ListUpdateVersions(update_tid);

            if (!update_versions.empty()) {
                checked_external = true;
                for (const auto& update_entry : update_versions) {
                    if (!IsVersionedExternalUpdateDisabled(disabled, update_entry.version)) {
                        update_disabled = false;
                        enabled_version = update_entry.version;
                        update_raw = external_provider->GetEntryForVersion(update_tid, type, update_entry.version);
                        break;
                    }
                }
            }
        }

        if (!checked_external) {
            const auto* manual_provider = static_cast<const ManualContentProvider*>(
                content_union->GetSlotProvider(ContentProviderUnionSlot::FrontendManual));
            if (manual_provider) {
                const auto manual_update_versions = manual_provider->ListUpdateVersions(update_tid);

                if (!manual_update_versions.empty()) {
                    checked_manual = true;
                    for (const auto& update_entry : manual_update_versions) {
                        if (!IsVersionedExternalUpdateDisabled(disabled, update_entry.version)) {
                            update_disabled = false;
                            enabled_version = update_entry.version;
                            update_raw = manual_provider->GetEntryForVersion(update_tid, type, update_entry.version);
                            break;
                        }
                    }
                }
            }
        }
    }

    if (!checked_external && !checked_manual) {
        const bool nand_disabled = std::find(disabled.cbegin(), disabled.cend(), "Update (NAND)") != disabled.cend();
        const bool sdmc_disabled = std::find(disabled.cbegin(), disabled.cend(), "Update (SDMC)") != disabled.cend();
        const bool generic_disabled = std::find(disabled.cbegin(), disabled.cend(), "Update") != disabled.cend();

        if (!nand_disabled && !sdmc_disabled && !generic_disabled) {
            update_disabled = false;
        }
        if (!update_disabled) {
            update_raw = content_provider.GetEntryRaw(update_tid, type);
        }
    } else if (update_disabled && content_union) {
        const bool nand_disabled = std::find(disabled.cbegin(), disabled.cend(), "Update (NAND)") != disabled.cend();
        const bool sdmc_disabled = std::find(disabled.cbegin(), disabled.cend(), "Update (SDMC)") != disabled.cend();

        if (!nand_disabled || !sdmc_disabled) {
            const auto nand_sdmc_entries = content_union->ListEntriesFilterOrigin(
                std::nullopt, TitleType::Update, type, update_tid);

            for (const auto& [slot, entry] : nand_sdmc_entries) {
                if (slot == ContentProviderUnionSlot::UserNAND ||
                    slot == ContentProviderUnionSlot::SysNAND) {
                    if (!nand_disabled) {
                        update_disabled = false;
                        update_raw = content_provider.GetEntryRaw(update_tid, type);
                        break;
                    }
                } else if (slot == ContentProviderUnionSlot::SDMC) {
                    if (!sdmc_disabled) {
                        update_disabled = false;
                        update_raw = content_provider.GetEntryRaw(update_tid, type);
                        break;
                    }
                }
            }
        }
    }

    if (!update_disabled && update_raw != nullptr && base_nca != nullptr) {
        const auto new_nca = std::make_shared<NCA>(update_raw, base_nca);
        if (new_nca->GetStatus() == Loader::ResultStatus::Success &&
            new_nca->GetRomFS() != nullptr) {
            LOG_INFO(Loader, "    RomFS: Update ({}) applied successfully",
                     enabled_version.has_value() ? FormatTitleVersion(*enabled_version) :
                     FormatTitleVersion(content_provider.GetEntryVersion(update_tid).value_or(0)));
            romfs = new_nca->GetRomFS();
        }
    } else if (!update_disabled && packed_update_raw != nullptr && base_nca != nullptr) {
        const auto new_nca = std::make_shared<NCA>(packed_update_raw, base_nca);
        if (new_nca->GetStatus() == Loader::ResultStatus::Success &&
            new_nca->GetRomFS() != nullptr) {
            LOG_INFO(Loader, "    RomFS: Update (PACKED) applied successfully");
            romfs = new_nca->GetRomFS();
        }
    }

    // LayeredFS
    if (apply_layeredfs) {
        ApplyLayeredFS(romfs, type);
    }

    return romfs;
}

std::vector<Patch> PatchManager::GetPatches(VirtualFile update_raw) const {
    if (title_id == 0) {
        return {};
    }

    std::vector<Patch> out;
    const auto& disabled = Settings::values.disabled_addons[title_id];

    // Game Updates
    const auto update_tid = GetUpdateTitleID(title_id);

    std::vector<Patch> external_update_patches;

    const auto* content_union = static_cast<const ContentProviderUnion*>(&content_provider);

    if (content_union) {
        // First, check ExternalContentProvider for updates
        const auto* external_provider = content_union->GetExternalProvider();
        if (external_provider) {
            const auto update_versions = external_provider->ListUpdateVersions(update_tid);

            for (const auto& update_entry : update_versions) {
                std::string version_str = update_entry.version_string;
                if (version_str.empty()) {
                    version_str = FormatTitleVersion(update_entry.version);
                }

                const auto update_disabled =
                    IsVersionedExternalUpdateDisabled(disabled, update_entry.version);

                Patch update_patch = {.enabled = !update_disabled,
                                      .name = "Update",
                                      .version = version_str,
                                      .type = PatchType::Update,
                                      .program_id = title_id,
                                      .title_id = update_tid,
                                      .source = PatchSource::External,
                                      .numeric_version = update_entry.version};

                external_update_patches.push_back(update_patch);
            }
        }

        const auto* manual_provider = static_cast<const ManualContentProvider*>(
            content_union->GetSlotProvider(ContentProviderUnionSlot::FrontendManual));
        if (manual_provider && external_update_patches.empty()) {
            const auto manual_update_versions = manual_provider->ListUpdateVersions(update_tid);

            for (const auto& update_entry : manual_update_versions) {
                std::string version_str = update_entry.version_string;
                if (version_str.empty()) {
                    version_str = FormatTitleVersion(update_entry.version);
                }

                const auto update_disabled =
                    IsVersionedExternalUpdateDisabled(disabled, update_entry.version);


                Patch update_patch = {.enabled = !update_disabled,
                                      .name = "Update",
                                      .version = version_str,
                                      .type = PatchType::Update,
                                      .program_id = title_id,
                                      .title_id = update_tid,
                                      .source = PatchSource::External,
                                      .numeric_version = update_entry.version};

                external_update_patches.push_back(update_patch);
            }
        }

        if (external_update_patches.size() > 1) {
            bool found_enabled = false;
            for (auto& patch : external_update_patches) {
                if (patch.enabled) {
                    if (found_enabled) {
                        patch.enabled = false;
                    } else {
                        found_enabled = true;
                    }
                }
            }
        }

        for (auto& patch : external_update_patches) {
            out.push_back(std::move(patch));
        }

        const auto all_updates = content_union->ListEntriesFilterOrigin(
            std::nullopt, std::nullopt, ContentRecordType::Program, update_tid);

        for (const auto& [slot, entry] : all_updates) {
            (void)entry;
            if (slot == ContentProviderUnionSlot::External ||
                slot == ContentProviderUnionSlot::FrontendManual) {
                continue;
            }

            PatchSource source_type = PatchSource::Unknown;
            std::string source_suffix;

            switch (slot) {
                case ContentProviderUnionSlot::UserNAND:
                case ContentProviderUnionSlot::SysNAND:
                    source_type = PatchSource::NAND;
                    source_suffix = " (NAND)";
                    break;
                case ContentProviderUnionSlot::SDMC:
                    source_type = PatchSource::SDMC;
                    source_suffix = " (SDMC)";
                    break;
                default:
                    break;
            }

            std::string version_str;
            u32 numeric_ver = 0;
            const auto* slot_provider = content_union->GetSlotProvider(slot);
            version_str = GetUpdateVersionStringFromSlot(slot_provider, update_tid);

            if (slot_provider != nullptr) {
                const auto slot_ver = slot_provider->GetEntryVersion(update_tid);
                if (slot_ver.has_value()) {
                    numeric_ver = *slot_ver;
                    if (version_str.empty() && numeric_ver != 0) {
                        version_str = FormatTitleVersion(numeric_ver);
                    }
                }
            }

            std::string patch_name = "Update" + source_suffix;

            bool update_disabled =
                std::find(disabled.cbegin(), disabled.cend(), patch_name) != disabled.cend();

            Patch update_patch = {.enabled = !update_disabled,
                                  .name = patch_name,
                                  .version = version_str,
                                  .type = PatchType::Update,
                                  .program_id = title_id,
                                  .title_id = update_tid,
                                  .source = source_type,
                                  .numeric_version = numeric_ver};

            out.push_back(update_patch);
        }
    } else {
        PatchManager update{update_tid, fs_controller, content_provider};
        const auto metadata = update.GetControlMetadata();
        const auto& nacp = metadata.first;

        bool update_disabled =
            std::find(disabled.cbegin(), disabled.cend(), "Update") != disabled.cend();
        Patch update_patch = {.enabled = !update_disabled,
                              .name = "Update",
                              .version = "",
                              .type = PatchType::Update,
                              .program_id = title_id,
                              .title_id = title_id,
                              .source = PatchSource::Unknown,
                              .numeric_version = 0};

        if (nacp != nullptr) {
            update_patch.version = nacp->GetVersionString();
            out.push_back(update_patch);
        } else {
            if (content_provider.HasEntry(update_tid, ContentRecordType::Program)) {
                const auto meta_ver = content_provider.GetEntryVersion(update_tid);
                if (meta_ver.value_or(0) == 0) {
                    out.push_back(update_patch);
                } else {
                    update_patch.version = FormatTitleVersion(*meta_ver);
                    update_patch.numeric_version = *meta_ver;
                    out.push_back(update_patch);
                }
            } else if (update_raw != nullptr) {
                update_patch.version = "PACKED";
                update_patch.source = PatchSource::Packed;
                out.push_back(update_patch);
            }
        }
    }

    // General Mods (LayeredFS and IPS)
    const auto mod_dir = fs_controller.GetModificationLoadRoot(title_id);
    if (mod_dir != nullptr) {
        for (auto const& f : mod_dir->GetFiles())
            if (auto const name = f->GetName(); name.starts_with("cheat_")) {
                auto const mod_disabled = std::find(disabled.begin(), disabled.end(), name) != disabled.end();
                out.push_back({
                    .enabled = !mod_disabled,
                    .name = name,
                    .version = "Cheats",
                    .type = PatchType::Mod,
                    .program_id = title_id,
                    .title_id = title_id,
                    .source = PatchSource::Unknown,
                    .location = f->GetFullPath(),
                });
            }

        for (const auto& mod : mod_dir->GetSubdirectories()) {
            std::string types;

            const auto exefs_dir = FindSubdirectoryCaseless(mod, "exefs");
            if (IsDirValidAndNonEmpty(exefs_dir)) {
                bool ips = false;
                bool ipswitch = false;
                bool layeredfs = false;

                for (const auto& file : exefs_dir->GetFiles()) {
                    if (file->GetExtension() == "ips") {
                        ips = true;
                    } else if (file->GetExtension() == "pchtxt") {
                        ipswitch = true;
                    } else if (std::find(EXEFS_FILE_NAMES.begin(), EXEFS_FILE_NAMES.end(),
                                         file->GetName()) != EXEFS_FILE_NAMES.end()) {
                        layeredfs = true;
                                         }
                }

                if (ips)
                    AppendCommaIfNotEmpty(types, "IPS");
                if (ipswitch)
                    AppendCommaIfNotEmpty(types, "IPSwitch");
                if (layeredfs)
                    AppendCommaIfNotEmpty(types, "LayeredExeFS");
            }
            if (IsDirValidAndNonEmpty(FindSubdirectoryCaseless(mod, "romfs")) ||
                IsDirValidAndNonEmpty(FindSubdirectoryCaseless(mod, "romfslite")))
                AppendCommaIfNotEmpty(types, "LayeredFS");
            if (IsDirValidAndNonEmpty(FindSubdirectoryCaseless(mod, "romfs_ext")))
                AppendCommaIfNotEmpty(types, "ExtLayeredFS");
            if (IsDirValidAndNonEmpty(FindSubdirectoryCaseless(mod, "cheats")))
                AppendCommaIfNotEmpty(types, "Cheats");

            if (types.empty())
                continue;

            const auto mod_disabled = std::find(disabled.begin(), disabled.end(), mod->GetName()) != disabled.end();
            out.push_back({.enabled = !mod_disabled,
                           .name = mod->GetName(),
                           .version = types,
                           .type = PatchType::Mod,
                           .program_id = title_id,
                           .title_id = title_id,
                           .source = PatchSource::Unknown,
                           .location = mod->GetFullPath()});
        }
    }

    // SDMC mod directory (RomFS LayeredFS)
    const auto sdmc_mod_dir = fs_controller.GetSDMCModificationLoadRoot(title_id);
    if (sdmc_mod_dir != nullptr) {
        std::string types;
        if (IsDirValidAndNonEmpty(FindSubdirectoryCaseless(sdmc_mod_dir, "exefs")))
            AppendCommaIfNotEmpty(types, "LayeredExeFS");
        if (IsDirValidAndNonEmpty(FindSubdirectoryCaseless(sdmc_mod_dir, "romfs")) ||
            IsDirValidAndNonEmpty(FindSubdirectoryCaseless(sdmc_mod_dir, "romfslite")))
            AppendCommaIfNotEmpty(types, "LayeredFS");
        if (IsDirValidAndNonEmpty(FindSubdirectoryCaseless(sdmc_mod_dir, "romfs_ext")))
            AppendCommaIfNotEmpty(types, "ExtLayeredFS");

        if (!types.empty()) {
            const auto mod_disabled =
                std::find(disabled.begin(), disabled.end(), "SDMC") != disabled.end();
            out.push_back({.enabled = !mod_disabled,
                           .name = "SDMC",
                           .version = types,
                           .type = PatchType::Mod,
                           .program_id = title_id,
                           .title_id = title_id,
                           .source = PatchSource::Unknown});
        }
    }

    // DLC
    std::vector<ContentProviderEntry> dlc_match;
    bool has_external_dlc = false;
    bool has_nand_dlc = false;
    bool has_sdmc_dlc = false;
    bool has_other_dlc = false;
    const auto dlc_entries_with_origin =
        content_union->ListEntriesFilterOrigin(std::nullopt, TitleType::AOC, ContentRecordType::Data);

    dlc_match.reserve(dlc_entries_with_origin.size());
    for (const auto& [slot, entry] : dlc_entries_with_origin) {
        const auto base_tid = GetBaseTitleID(entry.title_id);
        const bool matches_base = base_tid == title_id;
        if (!matches_base) {
            LOG_DEBUG(Loader, "DLC {:016X} base {:016X} doesn't match title {:016X}",
                      entry.title_id, base_tid, title_id);
            continue;
        }

        const auto* slot_provider = content_union->GetSlotProvider(slot);
        if (slot_provider == nullptr) {
            continue;
        }

        auto nca = slot_provider->GetEntry(entry);
        if (!nca) {
            LOG_DEBUG(Loader, "Failed to get NCA for DLC {:016X}", entry.title_id);
            continue;
        }

        const auto status = nca->GetStatus();
        if (status != Loader::ResultStatus::Success) {
            LOG_DEBUG(Loader, "DLC {:016X} NCA has status {}", entry.title_id,
                      static_cast<int>(status));
            continue;
        }

        switch (slot) {
            case ContentProviderUnionSlot::External:
            case ContentProviderUnionSlot::FrontendManual:
                has_external_dlc = true;
                break;
            case ContentProviderUnionSlot::UserNAND:
            case ContentProviderUnionSlot::SysNAND:
                has_nand_dlc = true;
                break;
            case ContentProviderUnionSlot::SDMC:
                has_sdmc_dlc = true;
                break;
            default:
                has_other_dlc = true;
                break;
        }
        dlc_match.push_back(entry);
    }

    if (!dlc_match.empty()) {
        // Ensure sorted so DLC IDs show in order.
        std::sort(dlc_match.begin(), dlc_match.end());

        std::string list;
        for (size_t i = 0; i < dlc_match.size() - 1; ++i)
            list += fmt::format("{}, ", dlc_match[i].title_id & 0x7FF);

        list += fmt::format("{}", dlc_match.back().title_id & 0x7FF);

        const auto dlc_disabled =
            std::find(disabled.begin(), disabled.end(), "DLC") != disabled.end();
        PatchSource dlc_source = PatchSource::Unknown;
        if (has_external_dlc && !has_nand_dlc && !has_sdmc_dlc && !has_other_dlc) {
            dlc_source = PatchSource::External;
        } else if (has_nand_dlc && !has_external_dlc && !has_sdmc_dlc && !has_other_dlc) {
            dlc_source = PatchSource::NAND;
        } else if (has_sdmc_dlc && !has_external_dlc && !has_nand_dlc && !has_other_dlc) {
            dlc_source = PatchSource::SDMC;
        }

        out.push_back({.enabled = !dlc_disabled,
                       .name = "DLC",
                       .version = std::move(list),
                       .type = PatchType::DLC,
                       .program_id = title_id,
                       .title_id = dlc_match.back().title_id,
                       .source = dlc_source});
    }

    return out;
}

std::optional<u32> PatchManager::GetGameVersion() const {
    const auto update_tid = GetUpdateTitleID(title_id);
    if (content_provider.HasEntry(update_tid, ContentRecordType::Program)) {
        const auto version = content_provider.GetEntryVersion(update_tid);
        return version || !parent_title_id ? version : content_provider.GetEntryVersion(GetUpdateTitleID(*parent_title_id));
    }

    const auto version = content_provider.GetEntryVersion(title_id);
    return version || !parent_title_id ? version : content_provider.GetEntryVersion(*parent_title_id);
}

PatchManager::Metadata PatchManager::GetControlMetadata() const {
    const auto base_control_nca = content_provider.GetEntry(title_id, ContentRecordType::Control);
    if (base_control_nca == nullptr) {
        if (parent_title_id) {
            const auto control_id = content_provider.HasEntry(*parent_title_id, ContentRecordType::Control) ? *parent_title_id : GetUpdateTitleID(*parent_title_id);
            const PatchManager parent{control_id, fs_controller, content_provider};
            return parent.GetControlMetadata();
        }
        return {};
    }

    return ParseControlNCA(*base_control_nca);
}

PatchManager::Metadata PatchManager::ParseControlNCA(const NCA& nca) const {
    const auto base_romfs = nca.GetRomFS();
    if (base_romfs == nullptr) {
        return {};
    }

    const auto romfs = PatchRomFS(&nca, base_romfs, ContentRecordType::Control);
    if (romfs == nullptr) {
        return {};
    }

    const auto extracted = ExtractRomFS(romfs);
    if (extracted == nullptr) {
        return {};
    }

    auto nacp_file = extracted->GetFile("control.nacp");
    if (nacp_file == nullptr) {
        nacp_file = extracted->GetFile("Control.nacp");
    }

    auto nacp = nacp_file == nullptr ? nullptr : std::make_unique<NACP>(nacp_file);

    // Get language code from settings
    const auto language_code = Service::Set::GetLanguageCodeFromIndex(
        static_cast<u32>(Settings::values.language_index.GetValue()));

    // Convert to application language and get priority list
    const auto application_language =
        Service::NS::ConvertToApplicationLanguage(language_code)
            .value_or(Service::NS::ApplicationLanguage::AmericanEnglish);
    const auto language_priority_list =
        Service::NS::GetApplicationLanguagePriorityList(application_language);

    // Convert to language names
    auto priority_language_names = FileSys::LANGUAGE_NAMES; // Copy
    if (language_priority_list) {
        for (size_t i = 0; i < priority_language_names.size(); ++i) {
            // Relies on FileSys::LANGUAGE_NAMES being in the same order as
            // Service::NS::ApplicationLanguage
            const auto language_index = static_cast<u8>(language_priority_list->at(i));

            if (language_index < FileSys::LANGUAGE_NAMES.size()) {
                priority_language_names[i] = FileSys::LANGUAGE_NAMES[language_index];
            } else {
                // Not a catastrophe, unlikely to happen
                LOG_WARNING(Loader, "Invalid language index {}", language_index);
            }
        }
    }

    // Get first matching icon
    VirtualFile icon_file;
    for (const auto& language : priority_language_names) {
        icon_file = extracted->GetFile(std::string("icon_").append(language).append(".dat"));
        if (icon_file != nullptr) {
            break;
        }
    }

    return {std::move(nacp), icon_file};
}

[[nodiscard]] PatchManager::Metadata PatchManager::GetMetadataFromBaseOrUpdate(Core::System& system, u64 application_id) noexcept {
    const FileSys::PatchManager pm{application_id, system.GetFileSystemController(), system.GetContentProvider()};
    auto metadata = pm.GetControlMetadata();
    if (metadata.first != nullptr)
        return metadata;
    const FileSys::PatchManager pm_update{FileSys::GetUpdateTitleID(application_id), system.GetFileSystemController(), system.GetContentProvider()};
    return pm_update.GetControlMetadata();
}

} // namespace FileSys
