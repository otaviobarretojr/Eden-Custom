// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "video_core/vulkan_common/streamline_runtime.h"

#ifdef HAS_NVIDIA_STREAMLINE
#include <sl.h>
#include <sl_consts.h>

#include "common/logging.h"
#endif

namespace Vulkan {

StreamlineRuntime::StreamlineRuntime() {
#ifdef HAS_NVIDIA_STREAMLINE
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
        return;
    }

    initialized = true;
    LOG_INFO(Render_Vulkan, "Streamline initialized before Vulkan startup");
#endif
}

StreamlineRuntime::~StreamlineRuntime() {
#ifdef HAS_NVIDIA_STREAMLINE
    if (!initialized) {
        return;
    }

    const sl::Result result = slShutdown();
    if (result != sl::Result::eOk) {
        LOG_WARNING(Render_Vulkan, "Streamline shutdown failed: {}", static_cast<int>(result));
    }
#endif
}

} // namespace Vulkan
