// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vulkan/vulkan_core.h>

namespace Vulkan {

struct DlssImageInput {
    VkImage image{};
    VkImageView view{};
    VkFormat format{VK_FORMAT_UNDEFINED};
    VkExtent2D extent{};
    VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};

    [[nodiscard]] bool IsValid() const {
        return image != VK_NULL_HANDLE && view != VK_NULL_HANDLE &&
               extent.width != 0 && extent.height != 0;
    }
};

struct DlssTemporalInputs {
    DlssImageInput color_in{};
    DlssImageInput color_out{};
    DlssImageInput depth{};
    DlssImageInput motion_vectors{};
    bool camera_constants_valid{};
    bool jitter_valid{};

    [[nodiscard]] bool IsReady() const {
        return color_in.IsValid() && color_out.IsValid() && depth.IsValid() &&
               motion_vectors.IsValid() && camera_constants_valid && jitter_valid;
    }
};

} // namespace Vulkan
