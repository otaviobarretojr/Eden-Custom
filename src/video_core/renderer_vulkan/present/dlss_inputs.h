// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

#include <vulkan/vulkan_core.h>

#include "common/common_types.h"

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
               format != VK_FORMAT_UNDEFINED && extent.width != 0 && extent.height != 0;
    }
};

struct DlssTemporalSnapshot {
    DlssImageInput color_in{};
    DlssImageInput depth{};
    VkImage motion_candidate{};
    VkImageView motion_candidate_view{};
    VkFormat motion_candidate_format{VK_FORMAT_UNDEFINED};
    VkImageLayout motion_candidate_layout{VK_IMAGE_LAYOUT_UNDEFINED};
    VkExtent2D render_extent{};
    u32 motion_candidate_slot{};
    u32 motion_candidate_persistence{};
    u64 motion_fragment_shader_hash{};
    u32 motion_producer_persistence{};
    bool motion_fragment_shader_writes_slot{};
    bool motion_producer_stable{};
    bool motion_semantic_evidence{};
    u64 title_id{};
    DlssTemporalConstants temporal_constants{};
    bool temporal_profile_valid{};
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

[[nodiscard]] inline DlssImageInput MakeDlssPresentationOutput(
    VkImage image, VkImageView view, VkFormat format, VkExtent2D extent, VkImageLayout layout) {
    return DlssImageInput{
        .image = image,
        .view = view,
        .format = format,
        .extent = extent,
        .layout = layout,
    };
}

enum class DlssTemporalConstantsSource {
    None,
    ValidatedGameProfile,
    ValidatedGuestMetadata,
};

struct DlssTemporalConstants {
    float jitter_x{};
    float jitter_y{};
    float camera_near{};
    float camera_far{};
    float camera_fov_vertical{};
    float camera_aspect_ratio{};
    bool jitter_valid{};
    bool camera_valid{};
    DlssTemporalConstantsSource source{DlssTemporalConstantsSource::None};

    [[nodiscard]] bool IsReady() const noexcept {
        return source != DlssTemporalConstantsSource::None && jitter_valid && camera_valid &&
               camera_near > 0.0f &&
               camera_far > camera_near && camera_fov_vertical > 0.0f &&
               camera_aspect_ratio > 0.0f;
    }
};

struct DlssTemporalGameProfile {
    u64 title_id{};
    u64 motion_fragment_shader_hash{};
    u32 motion_slot{};
    VkFormat motion_format{VK_FORMAT_UNDEFINED};
    DlssTemporalConstants constants{};

    [[nodiscard]] bool Matches(u64 current_title_id, u64 fragment_shader_hash, u32 slot,
                               VkFormat format) const noexcept {
        return title_id != 0 && title_id == current_title_id &&
               motion_fragment_shader_hash != 0 &&
               motion_fragment_shader_hash == fragment_shader_hash &&
               motion_slot == slot && motion_format == format &&
               constants.source == DlssTemporalConstantsSource::ValidatedGameProfile &&
               constants.IsReady();
    }
};

[[nodiscard]] inline const DlssTemporalGameProfile* FindValidatedDlssTemporalGameProfile(
    u64 title_id, u64 fragment_shader_hash, u32 slot, VkFormat format) noexcept {
    // Intentionally empty until a game's temporal data has been independently validated.
    // Do not add heuristic or guessed profiles here.
    static constexpr std::array<DlssTemporalGameProfile, 0> validated_profiles{};
    for (const auto& profile : validated_profiles) {
        if (profile.Matches(title_id, fragment_shader_hash, slot, format)) {
            return &profile;
        }
    }
    return nullptr;
}

struct DlssTemporalInputs {
    DlssImageInput color_in{};
    DlssImageInput color_out{};
    DlssImageInput depth{};
    DlssImageInput motion_vectors{};
    DlssTemporalConstants constants{};
    DlssMotionConfidence motion_confidence{DlssMotionConfidence::None};

    [[nodiscard]] bool IsReady() const {
        return color_in.IsValid() && color_out.IsValid() && depth.IsValid() &&
               motion_vectors.IsValid() && constants.IsReady() &&
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
        if (!constants.IsReady() ||
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

[[nodiscard]] inline DlssTemporalInputs MakeDlssTemporalInputs(
    const DlssTemporalSnapshot& snapshot, const DlssImageInput& color_out) {
    DlssTemporalInputs inputs{};
    inputs.color_in = snapshot.color_in;
    inputs.color_out = color_out;
    inputs.depth = snapshot.depth;
    inputs.motion_vectors = {
        .image = snapshot.motion_candidate,
        .view = snapshot.motion_candidate_view,
        .format = snapshot.motion_candidate_format,
        .extent = snapshot.render_extent,
        .layout = snapshot.motion_candidate_layout,
    };
    if (snapshot.temporal_profile_valid) {
        inputs.constants = snapshot.temporal_constants;
    }
    inputs.motion_confidence = snapshot.motion_confidence;
    return inputs;
}

} // namespace Vulkan
