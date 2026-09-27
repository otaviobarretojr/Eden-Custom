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
struct Dlss5ProcessedOutput {
    u32 width{};
    u32 height{};
    VkImage image{VK_NULL_HANDLE};
    bool ready{};

    [[nodiscard]] bool IsValid() const noexcept {
        return ready && width != 0 && height != 0 && image != VK_NULL_HANDLE;
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

    void ObservePresentFrame(const Frame& frame);

    // Records a borrowed output candidate. This does not make the image presentable by itself;
    // readiness, dimensions and handle are revalidated when presentation selects its source.
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

    [[nodiscard]] const Dlss5CompatibilityInputs& Inputs() const noexcept {
        return inputs;
    }

private:
    Dlss5CompatibilityState state{Dlss5CompatibilityState::UnsupportedGpu};
    Dlss5CompatibilityInputs inputs{};
    Dlss5ProcessedOutput processed_output{};
    u64 frames_observed{};
    u32 last_width{};
    u32 last_height{};
};

} // namespace Vulkan
