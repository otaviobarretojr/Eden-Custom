// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace Vulkan {

class StreamlineRuntime final {
public:
    StreamlineRuntime();
    ~StreamlineRuntime();

    StreamlineRuntime(const StreamlineRuntime&) = delete;
    StreamlineRuntime& operator=(const StreamlineRuntime&) = delete;

    [[nodiscard]] bool IsInitialized() const {
        return initialized;
    }

private:
    bool initialized{};
};

} // namespace Vulkan
