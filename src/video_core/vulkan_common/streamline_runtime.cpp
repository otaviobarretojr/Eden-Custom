// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#include "video_core/vulkan_common/streamline_runtime.h"
#ifdef HAS_NVIDIA_STREAMLINE
#include <sl.h>
#include <sl_consts.h>
#include "common/logging.h"
namespace Vulkan {
namespace {
bool initialized{};
}
bool InitializeStreamline() {
    if (initialized) return true;
    sl::Preferences preferences{};
    const sl::Feature features[] = {sl::kFeatureDLSS};
    preferences.featuresToLoad = features;
    preferences.numFeaturesToLoad = 1;
    preferences.engine = sl::EngineType::eCustom;
    preferences.engineVersion = "Eden-Custom";
    preferences.renderAPI = sl::RenderAPI::eVulkan;
    const sl::Result result = slInit(preferences);
    if (result != sl::Result::eOk) {
        LOG_WARNING(Render_Vulkan, "Streamline initialization failed: {}", static_cast<int>(result));
        return false;
    }
    initialized = true;
    LOG_INFO(Render_Vulkan, "Streamline initialized for Vulkan/DLSS");
    return true;
}
void ShutdownStreamline() {
    if (!initialized) return;
    const sl::Result result = slShutdown();
    if (result != sl::Result::eOk) {
        LOG_WARNING(Render_Vulkan, "Streamline shutdown failed: {}", static_cast<int>(result));
    }
    initialized = false;
}
bool IsStreamlineInitialized() { return initialized; }
} // namespace Vulkan
#endif
