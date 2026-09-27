// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#include "common/common_types.h"

namespace Common {
class DynamicLibrary;
}

namespace Vulkan {

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

    [[nodiscard]] u64 CurrentEdenFrameId() const noexcept {
        return current_eden_frame_id;
    }

private:
    std::unique_ptr<Common::DynamicLibrary> interposer;
    bool initialized{};
    u64 current_eden_frame_id{};
};

[[nodiscard]] bool IsStreamlineBootstrapReady() noexcept;

} // namespace Vulkan
