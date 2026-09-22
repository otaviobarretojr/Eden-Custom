// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace Vulkan {
#ifdef HAS_NVIDIA_STREAMLINE
[[nodiscard]] bool InitializeStreamline();
void ShutdownStreamline();
[[nodiscard]] bool IsStreamlineInitialized();
#else
inline bool InitializeStreamline() { return false; }
inline void ShutdownStreamline() {}
inline bool IsStreamlineInitialized() { return false; }
#endif
} // namespace Vulkan
