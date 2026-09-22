// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#include "video_core/renderer_vulkan/present/dlss_probe.h"
#include "video_core/vulkan_common/vulkan_device.h"
#ifdef _WIN32
#include <windows.h>
#endif
namespace Vulkan {
DlssProbeResult ProbeDlssSupport(const Device& device) {
    DlssProbeResult result{};
    result.nvidia = device.GetDriverID() == VK_DRIVER_ID_NVIDIA_PROPRIETARY;
    // Streamline 2.14.x requires Vulkan 1.2+ for Vulkan integrations.
    result.vulkan_compatible = device.ApiVersion() >= VK_API_VERSION_1_2;
    if (!result.nvidia) {
        result.reason = "DLSS probe disabled: selected Vulkan device is not using the NVIDIA proprietary driver.";
        return result;
    }
    if (!result.vulkan_compatible) {
        result.reason = "DLSS probe disabled: Streamline requires Vulkan 1.2 or newer.";
        return result;
    }
#ifdef _WIN32
    HMODULE module = LoadLibraryExW(L"sl.interposer.dll", nullptr,
                                    LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
    if (module) {
        result.streamline_runtime_present = true;
        FreeLibrary(module);
        result.reason = "NVIDIA Vulkan device and Streamline runtime detected; DLSS resource integration is not enabled yet.";
    } else {
        result.reason = "NVIDIA Vulkan device detected; Streamline runtime was not found next to the executable.";
    }
#else
    result.reason = "NVIDIA Vulkan device detected; initial Streamline experiment is Windows-only.";
#endif
    return result;
}
