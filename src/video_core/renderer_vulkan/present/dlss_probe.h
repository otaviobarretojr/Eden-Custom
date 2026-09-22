// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
namespace Vulkan {
class Device;
struct DlssProbeResult {
    bool nvidia{};
    bool vulkan_compatible{};
    bool streamline_runtime_present{};
    std::string reason;
};
[[nodiscard]] DlssProbeResult ProbeDlssSupport(const Device& device);
} // namespace Vulkan
