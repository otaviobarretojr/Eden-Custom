// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "video_core/renderer_vulkan/present/dlss5_compatibility.h"

#include "common/logging.h"
#include "video_core/renderer_vulkan/vk_present_manager.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

Dlss5CompatibilityFilter::Dlss5CompatibilityFilter(const Device& device) {
    if (device.GetDriverID() != VK_DRIVER_ID_NVIDIA_PROPRIETARY) {
        state = Dlss5CompatibilityState::UnsupportedGpu;
        LOG_INFO(Render_Vulkan,
                 "DLSS 5 compatibility filter: disabled for this adapter (NVIDIA proprietary "
                 "Vulkan driver not detected)");
        return;
    }

    if (device.ApiVersion() < VK_API_VERSION_1_2) {
        state = Dlss5CompatibilityState::UnsupportedVulkan;
        LOG_WARNING(Render_Vulkan,
                    "DLSS 5 compatibility filter: NVIDIA adapter detected, but Vulkan 1.2+ is "
                    "required for the Streamline integration path");
        return;
    }

    state = Dlss5CompatibilityState::FrameTapReady;
    LOG_INFO(Render_Vulkan,
             "DLSS 5 compatibility filter: final-frame tap ready. Streamline / Neural Rendering "
             "evaluation remains disabled until the runtime stage is wired.");
}

void Dlss5CompatibilityFilter::ObservePresentFrame(const Frame& frame) {
    if (!IsFrameTapReady()) {
        return;
    }

    ++frames_observed;

    if (frame.width == last_width && frame.height == last_height) {
        return;
    }

    last_width = frame.width;
    last_height = frame.height;

    LOG_INFO(Render_Vulkan,
             "DLSS 5 compatibility filter: observing final presentation frame {}x{} (frame #{})",
             last_width, last_height, frames_observed);
}

} // namespace Vulkan
