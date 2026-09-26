// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <string>

#include "common/dynamic_library.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "video_core/vulkan_common/streamline_bootstrap.h"
#if defined(_WIN32) && defined(HAS_NVIDIA_STREAMLINE)
#include <array>
#include <string_view>
#include <windows.h>
#endif
#include "video_core/vulkan_common/vulkan_library.h"

namespace Vulkan {

std::shared_ptr<Common::DynamicLibrary> OpenLibrary(
    [[maybe_unused]] Core::Frontend::GraphicsContext* context) {
    LOG_DEBUG(Render_Vulkan, "Looking for a Vulkan library");
#if defined(ANDROID) && defined(ARCHITECTURE_arm64)
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
    if (IsStreamlineBootstrapReady()) {
        std::array<wchar_t, 32768> executable_path{};
        const DWORD executable_size =
            GetModuleFileNameW(nullptr, executable_path.data(),
                               static_cast<DWORD>(executable_path.size()));
        if (executable_size > 0 && executable_size < executable_path.size()) {
            std::wstring interposer_path{executable_path.data(), executable_size};
            const auto separator = interposer_path.find_last_of(L"\\/");
            if (separator != std::wstring::npos) {
                interposer_path.resize(separator + 1);
                interposer_path += L"sl.interposer.dll";

                auto streamline = std::make_shared<Common::DynamicLibrary>();
                if (streamline->Open(std::wstring_view{interposer_path})) {
                    PFN_vkGetInstanceProcAddr get_instance_proc_addr{};
                    PFN_vkGetDeviceProcAddr get_device_proc_addr{};
                    const bool has_instance =
                        streamline->GetSymbol("vkGetInstanceProcAddr",
                                              &get_instance_proc_addr);
                    const bool has_device =
                        streamline->GetSymbol("vkGetDeviceProcAddr",
                                              &get_device_proc_addr);
                    if (has_instance && has_device) {
                        LOG_INFO(Render_Vulkan,
                                 "Using application-local Streamline Vulkan interposer verified "
                                 "during bootstrap");
                        return streamline;
                    }
                }
            }
        }

        LOG_WARNING(Render_Vulkan,
                    "Streamline core initialized but its verified Vulkan interposer could not be "
                    "used; falling back to the system Vulkan loader");
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
