// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
namespace Vulkan {
class Device;
enum class StreamlineBootstrapState {
    Unavailable,
    RuntimePresent,
    ApiResolved,
};
struct DlssProbeResult {
    bool nvidia{};
    bool vulkan_compatible{};
    bool streamline_runtime_present{};
    bool temporal_inputs_available{};
    StreamlineBootstrapState bootstrap_state{StreamlineBootstrapState::Unavailable};
    std::string reason;
};
[[nodiscard]] DlssProbeResult ProbeDlssSupport(const Device& device);
} // namespace Vulkan
