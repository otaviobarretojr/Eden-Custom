// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"
#include <vulkan/vulkan_core.h>

namespace Vulkan {

class Device;
struct Frame;

struct Dlss5PresentationSource {
    u32 width{};
    u32 height{};
    VkImage image{VK_NULL_HANDLE};
    bool processed{};

    [[nodiscard]] bool IsValid() const noexcept {
        return width != 0 && height != 0 && image != VK_NULL_HANDLE;
    }
};

// Borrowed candidate produced by a future compatibility-processing stage. Ownership and GPU
// completion remain with that stage; presentation may consume it only after it is explicitly
// marked ready and passes the structural checks below.
// Vulkan metadata required by Streamline's public resource-tagging contract. Keeping this
// separate from feature-specific tags lets Eden describe its final color accurately without
// claiming unavailable depth, motion-vector or exposure inputs.
struct Dlss5VulkanResourceDescription {
    VkImage image{VK_NULL_HANDLE};
    VkImageView image_view{VK_NULL_HANDLE};
    VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
    VkFormat format{VK_FORMAT_UNDEFINED};
    VkImageUsageFlags usage{};
    u32 width{};
    u32 height{};

    [[nodiscard]] bool IsValid() const noexcept {
        return image != VK_NULL_HANDLE && image_view != VK_NULL_HANDLE &&
               layout != VK_IMAGE_LAYOUT_UNDEFINED && format != VK_FORMAT_UNDEFINED &&
               usage != 0 && width != 0 && height != 0;
    }
};

[[nodiscard]] bool ValidateStreamlineVulkanResource(
    const Dlss5VulkanResourceDescription& resource) noexcept;

struct Dlss5ProcessedOutput {
    u32 width{};
    u32 height{};
    VkImage image{VK_NULL_HANDLE};
    // Monotonic token of the exact presentation frame this output was produced from.
    u64 frame_id{};
    bool ready{};
    // True only after the producer's GPU completion dependency has been folded into the owning
    // Frame synchronization. A CPU-side ready flag alone must never authorize presentation.
    bool synchronized_with_frame{};

    [[nodiscard]] bool IsValid() const noexcept {
        return ready && synchronized_with_frame && frame_id != 0 && width != 0 && height != 0 &&
               image != VK_NULL_HANDLE;
    }
};

enum class Dlss5CompatibilityState {
    UnsupportedGpu,
    UnsupportedVulkan,
    FrameTapReady,
};

// Inputs associated with the final presentation frame only. Auxiliary resources must remain
// unavailable unless they can be proven to describe the same presented frame. In particular,
// rasterizer depth attachments are transient per-pass resources and must not be forwarded here.
struct Dlss5CompatibilityInputs {
    u32 width{};
    u32 height{};
    bool final_color_available{};
    bool depth_available{};
    bool motion_vectors_available{};
    bool exposure_available{};

    [[nodiscard]] bool HasNativeAuxiliaryInputs() const noexcept {
        return depth_available || motion_vectors_available || exposure_available;
    }
};

class Dlss5CompatibilityFilter final {
public:
    explicit Dlss5CompatibilityFilter(const Device& device);

    void SetFinalColorFormat(VkFormat format) noexcept {
        final_color_format = format;
    }

    void ObservePresentFrame(const Frame& frame);

    // Records a borrowed output candidate. This does not make the image presentable by itself;
    // readiness, synchronization, dimensions and handle are revalidated when presentation selects
    // its source. The producer must keep the image alive through the owning Frame's present_done.
    void SetProcessedOutput(Dlss5ProcessedOutput output) noexcept {
        processed_output = output;
    }

    void ClearProcessedOutput() noexcept {
        processed_output = {};
    }

    // Returns the safest image to present. Any missing, incomplete or stale processing result
    // falls back to Eden's untouched final frame.
    [[nodiscard]] Dlss5PresentationSource SelectPresentationSource(const Frame& frame) const noexcept;

    [[nodiscard]] Dlss5CompatibilityState State() const noexcept {
        return state;
    }

    [[nodiscard]] bool IsFrameTapReady() const noexcept {
        return state == Dlss5CompatibilityState::FrameTapReady;
    }

    [[nodiscard]] u64 FramesObserved() const noexcept {
        return frames_observed;
    }

    [[nodiscard]] u64 CurrentFrameId() const noexcept {
        return current_frame_id;
    }

    [[nodiscard]] const Dlss5CompatibilityInputs& Inputs() const noexcept {
        return inputs;
    }

    [[nodiscard]] const Dlss5VulkanResourceDescription& FinalColorResource() const noexcept {
        return final_color_resource;
    }

private:
    Dlss5CompatibilityState state{Dlss5CompatibilityState::UnsupportedGpu};
    Dlss5CompatibilityInputs inputs{};
    Dlss5VulkanResourceDescription final_color_resource{};
    Dlss5ProcessedOutput processed_output{};
    u64 frames_observed{};
    u64 current_frame_id{};
    VkFormat final_color_format{VK_FORMAT_UNDEFINED};
    u32 last_width{};
    u32 last_height{};
};

} // namespace Vulkan
