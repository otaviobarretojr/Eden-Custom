// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#include "common/common_types.h"

namespace Common {
class DynamicLibrary;
}

#if defined(_WIN32) && defined(HAS_NVIDIA_STREAMLINE)
#include <sl.h>
#endif

namespace Vulkan {

struct Dlss5VulkanResourceDescription;

class StreamlineBootstrap final {
public:
    StreamlineBootstrap();
    ~StreamlineBootstrap();

    StreamlineBootstrap(const StreamlineBootstrap&) = delete;
    StreamlineBootstrap& operator=(const StreamlineBootstrap&) = delete;

    [[nodiscard]] bool IsInitialized() const noexcept {
        return initialized;
    }

    // Reserves the Streamline token corresponding to Eden's presentation frame. This is
    // intentionally feature-agnostic; it does not tag resources or evaluate any plugin.
    [[nodiscard]] bool BeginFrame(u64 eden_frame_id) noexcept;

    // Tags the submitted depth resource for the current presentation frame. The caller must
    // submit the GPU work that produces the resource before calling this method.
    [[nodiscard]] bool TagDepthResource(const Dlss5VulkanResourceDescription& resource) noexcept;

    [[nodiscard]] u64 CurrentEdenFrameId() const noexcept {
        return current_eden_frame_id;
    }

    [[nodiscard]] bool IsFrameTaggingReady() const noexcept {
#if defined(_WIN32) && defined(HAS_NVIDIA_STREAMLINE)
        return initialized && current_eden_frame_id != 0 && current_frame_token != nullptr;
#else
        return false;
#endif
    }

#if defined(_WIN32) && defined(HAS_NVIDIA_STREAMLINE)
    [[nodiscard]] sl::FrameToken* CurrentFrameToken() const noexcept {
        return current_frame_token;
    }
#endif

private:
    std::unique_ptr<Common::DynamicLibrary> interposer;
    bool initialized{};
    u64 current_eden_frame_id{};
#if defined(_WIN32) && defined(HAS_NVIDIA_STREAMLINE)
    sl::FrameToken* current_frame_token{};
#endif
};

[[nodiscard]] bool IsStreamlineBootstrapReady() noexcept;

} // namespace Vulkan
