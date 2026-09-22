// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#include "video_core/renderer_vulkan/present/dlss_probe.h"
#include "video_core/vulkan_common/vulkan_device.h"
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
#ifdef HAS_NVIDIA_STREAMLINE
    result.streamline_runtime_present = true;
    result.bootstrap_state = StreamlineBootstrapState::ApiResolved;
    result.reason =
        "NVIDIA Vulkan device detected; Streamline bootstrap is compiled in. "
        "Runtime capability and temporal inputs are validated separately.";
#else
    result.reason =
        "NVIDIA Vulkan device detected; this build does not include experimental Streamline support.";
#endif
    return result;
}
