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
    // Streamline's Vulkan integration is opt-in. If a signed/packaged interposer is shipped
    // next to Eden, prefer it as the Vulkan loader. If it is absent or incomplete, fall back
    // to the system Vulkan loader without preventing the emulator from starting.
    {
        wchar_t full_path[MAX_PATH]{};
        const DWORD path_size = GetFullPathNameW(L"sl.interposer.dll", MAX_PATH, full_path, nullptr);
        bool signature_valid = false;
        if (path_size > 0 && path_size < MAX_PATH) {
            WINTRUST_FILE_INFO file_info{};
            file_info.cbStruct = sizeof(file_info);
            file_info.pcwszFilePath = full_path;

            WINTRUST_DATA trust_data{};
            trust_data.cbStruct = sizeof(trust_data);
            trust_data.dwUIChoice = WTD_UI_NONE;
            trust_data.fdwRevocationChecks = WTD_REVOKE_NONE;
            trust_data.dwUnionChoice = WTD_CHOICE_FILE;
            trust_data.pFile = &file_info;
            trust_data.dwStateAction = WTD_STATEACTION_VERIFY;
            trust_data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;

            GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
            signature_valid = WinVerifyTrust(nullptr, &policy, &trust_data) == ERROR_SUCCESS;
            trust_data.dwStateAction = WTD_STATEACTION_CLOSE;
            WinVerifyTrust(nullptr, &policy, &trust_data);
        }

        auto streamline = std::make_shared<Common::DynamicLibrary>();
        if (signature_valid && streamline->Open("sl.interposer.dll")) {
            PFN_vkGetInstanceProcAddr sl_get_instance_proc_addr{};
            PFN_vkGetDeviceProcAddr sl_get_device_proc_addr{};
            const bool has_instance =
                streamline->GetSymbol("vkGetInstanceProcAddr", &sl_get_instance_proc_addr);
            const bool has_device =
                streamline->GetSymbol("vkGetDeviceProcAddr", &sl_get_device_proc_addr);
            if (has_instance && has_device) {
                LOG_INFO(Render_Vulkan, "Using application-local Streamline Vulkan interposer");
                return streamline;
            }
            LOG_WARNING(Render_Vulkan,
                        "Streamline interposer is present but missing Vulkan exports; falling back");
        } else if (path_size > 0 && path_size < MAX_PATH) {
            LOG_WARNING(Render_Vulkan,
                        "Streamline interposer failed Windows signature verification; falling back");
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
