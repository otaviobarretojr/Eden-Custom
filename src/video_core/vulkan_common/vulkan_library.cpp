// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <string>

#include "common/dynamic_library.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#if defined(_WIN32) && defined(HAS_NVIDIA_STREAMLINE)
#include <windows.h>
#include <softpub.h>
#include <wintrust.h>
#include <sl_security.h>
#endif
#include "video_core/vulkan_common/vulkan_library.h"

namespace Vulkan {

std::shared_ptr<Common::DynamicLibrary> OpenLibrary(
    [[maybe_unused]] Core::Frontend::GraphicsContext* context) {
    LOG_DEBUG(Render_Vulkan, "Looking for a Vulkan library");
#if defined(__ANDROID__) && defined(ARCHITECTURE_arm64)
    // Android manages its Vulkan driver from the frontend.
    return context->GetDriverLibrary();
#else
    auto library = std::make_shared<Common::DynamicLibrary>();
#ifdef __APPLE__
    const auto libvulkan_filename =
        Common::FS::GetBundleDirectory() / "Contents/Frameworks/libvulkan.1.dylib";
    const auto libmoltenvk_filename =
        Common::FS::GetBundleDirectory() / "Contents/Frameworks/libMoltenVK.dylib";
    const char* library_paths[] = {std::getenv("LIBVULKAN_PATH"), libvulkan_filename.c_str(),
                                   libmoltenvk_filename.c_str()};
    // Check if a path to a specific Vulkan library has been specified.
    for (const auto& library_path : library_paths) {
        if (library_path && library->Open(library_path)) {
            break;
        }
    }
#else
#if defined(_WIN32) && defined(HAS_NVIDIA_STREAMLINE)
    // Streamline's Vulkan integration is opt-in. Only load the NVIDIA-signed
    // interposer from the executable directory using its absolute path.
    {
        wchar_t executable_path[MAX_PATH]{};
        const DWORD executable_size =
            GetModuleFileNameW(nullptr, executable_path, static_cast<DWORD>(std::size(executable_path)));
        if (executable_size > 0 && executable_size < std::size(executable_path)) {
            std::wstring interposer_path{executable_path, executable_size};
            const auto separator = interposer_path.find_last_of(L"\\/");
            if (separator != std::wstring::npos) {
                interposer_path.resize(separator + 1);
                interposer_path += L"sl.interposer.dll";

                const bool signature_valid =
                    sl::security::verifyEmbeddedSignature(interposer_path.c_str());
                if (signature_valid) {
                    const int utf8_size =
                        WideCharToMultiByte(CP_UTF8, 0, interposer_path.c_str(), -1, nullptr, 0,
                                            nullptr, nullptr);
                    std::string interposer_utf8(static_cast<size_t>(utf8_size), '\0');
                    if (utf8_size > 0) {
                        WideCharToMultiByte(CP_UTF8, 0, interposer_path.c_str(), -1,
                                            interposer_utf8.data(), utf8_size, nullptr, nullptr);
                        auto streamline = std::make_shared<Common::DynamicLibrary>();
                        if (streamline->Open(interposer_utf8.c_str())) {
                            PFN_vkGetInstanceProcAddr sl_get_instance_proc_addr{};
                            PFN_vkGetDeviceProcAddr sl_get_device_proc_addr{};
                            const bool has_instance = streamline->GetSymbol(
                                "vkGetInstanceProcAddr", &sl_get_instance_proc_addr);
                            const bool has_device = streamline->GetSymbol(
                                "vkGetDeviceProcAddr", &sl_get_device_proc_addr);
                            if (has_instance && has_device) {
                                LOG_INFO(Render_Vulkan,
                                         "Using verified application-local Streamline Vulkan interposer");
                                return streamline;
                            }
                            LOG_WARNING(Render_Vulkan,
                                        "Verified Streamline interposer is missing Vulkan exports; falling back");
                        }
                    }
                } else {
                    LOG_WARNING(Render_Vulkan,
                                "Streamline interposer is absent or failed NVIDIA signature verification; falling back");
                }
            }
        }
    }
#endif
    std::string filename = Common::DynamicLibrary::GetVersionedFilename("vulkan", 1);
    LOG_DEBUG(Render_Vulkan, "Trying Vulkan library: {}", filename);
    if (!library->Open(filename.c_str())) {
        // Android devices may not have libvulkan.so.1, only libvulkan.so.
        filename = Common::DynamicLibrary::GetVersionedFilename("vulkan");
        LOG_DEBUG(Render_Vulkan, "Trying Vulkan library (second attempt): {}", filename);
        void(library->Open(filename.c_str()));
    }
#endif
    return library;
#endif
}

} // namespace Vulkan
