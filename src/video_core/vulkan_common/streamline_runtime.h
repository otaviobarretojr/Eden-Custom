// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;

class StreamlineRuntime final {
public:
    StreamlineRuntime();
    ~StreamlineRuntime();

    StreamlineRuntime(const StreamlineRuntime&) = delete;
    StreamlineRuntime& operator=(const StreamlineRuntime&) = delete;

    void BindVulkanDevice(const vk::Instance& instance, const Device& device);
    [[nodiscard]] bool IsDlssSupported() const {
        return dlss_supported;
    }

    [[nodiscard]] bool IsInitialized() const {
        return initialized;
    }

private:
    bool initialized{};
    bool dlss_supported{};
};

} // namespace Vulkan
