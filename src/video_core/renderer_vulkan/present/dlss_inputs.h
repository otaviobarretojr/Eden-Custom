// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vulkan/vulkan_core.h>

namespace Vulkan {

enum class DlssMotionConfidence {
    None,
    Candidate,
    Verified,
};

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

struct DlssTemporalSnapshot {
    VkImage color{};
    VkImage depth{};
    VkImage motion_candidate{};
    VkFormat motion_candidate_format{VK_FORMAT_UNDEFINED};
    VkExtent2D render_extent{};
    u32 motion_candidate_slot{};
    u32 motion_candidate_persistence{};
    u64 frame_index{};
    DlssMotionConfidence motion_confidence{DlssMotionConfidence::None};

    [[nodiscard]] bool HasGuestTemporalPair() const {
        return color != VK_NULL_HANDLE && depth != VK_NULL_HANDLE &&
               render_extent.width != 0 && render_extent.height != 0;
    }
};

struct DlssTemporalInputs {
    DlssImageInput color_in{};
    DlssImageInput color_out{};
    DlssImageInput depth{};
    DlssImageInput motion_vectors{};
    bool camera_constants_valid{};
    bool jitter_valid{};
    DlssMotionConfidence motion_confidence{DlssMotionConfidence::None};

    [[nodiscard]] bool IsReady() const {
        return color_in.IsValid() && color_out.IsValid() && depth.IsValid() &&
               motion_vectors.IsValid() && camera_constants_valid && jitter_valid &&
               motion_confidence == DlssMotionConfidence::Verified;
    }
};

} // namespace Vulkan
