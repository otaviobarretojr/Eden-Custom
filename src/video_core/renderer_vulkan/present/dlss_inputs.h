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
    DlssImageInput color_in{};
    DlssImageInput depth{};
    VkImage motion_candidate{};
    VkFormat motion_candidate_format{VK_FORMAT_UNDEFINED};
    VkExtent2D render_extent{};
    u32 motion_candidate_slot{};
    u32 motion_candidate_persistence{};
    u64 frame_index{};
    DlssMotionConfidence motion_confidence{DlssMotionConfidence::None};

    [[nodiscard]] bool HasGuestTemporalPair() const {
        return color_in.IsValid() && depth.IsValid() &&
               render_extent.width != 0 && render_extent.height != 0;
    }
};

enum class DlssTagReadiness {
    MissingResources,
    MissingVulkanMetadata,
    MissingTemporalValidation,
    Ready,
};

struct DlssTagPlan {
    DlssTagReadiness readiness{DlssTagReadiness::MissingResources};
    bool tag_color_in{};
    bool tag_color_out{};
    bool tag_depth{};
    bool tag_motion_vectors{};

    [[nodiscard]] bool IsReady() const {
        return readiness == DlssTagReadiness::Ready && tag_color_in && tag_color_out &&
               tag_depth && tag_motion_vectors;
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

    [[nodiscard]] DlssTagPlan BuildTagPlan() const {
        DlssTagPlan plan{};
        if (color_in.image == VK_NULL_HANDLE || color_out.image == VK_NULL_HANDLE ||
            depth.image == VK_NULL_HANDLE || motion_vectors.image == VK_NULL_HANDLE) {
            return plan;
        }
        if (!color_in.IsValid() || !color_out.IsValid() || !depth.IsValid() ||
            !motion_vectors.IsValid() || color_in.layout == VK_IMAGE_LAYOUT_UNDEFINED ||
            color_out.layout == VK_IMAGE_LAYOUT_UNDEFINED ||
            depth.layout == VK_IMAGE_LAYOUT_UNDEFINED ||
            motion_vectors.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
            plan.readiness = DlssTagReadiness::MissingVulkanMetadata;
            return plan;
        }
        if (!camera_constants_valid || !jitter_valid ||
            motion_confidence != DlssMotionConfidence::Verified) {
            plan.readiness = DlssTagReadiness::MissingTemporalValidation;
            return plan;
        }
        plan.readiness = DlssTagReadiness::Ready;
        plan.tag_color_in = true;
        plan.tag_color_out = true;
        plan.tag_depth = true;
        plan.tag_motion_vectors = true;
        return plan;
    }
};

} // namespace Vulkan
