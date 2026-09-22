// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "video_core/vulkan_common/streamline_runtime.h"

#ifdef HAS_NVIDIA_STREAMLINE
#include <sl.h>
#include <sl_consts.h>

#include "common/logging.h"
#include "video_core/vulkan_common/vulkan_device.h"
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


void StreamlineRuntime::BindVulkanDevice(const vk::Instance& instance, const Device& device) {
#ifdef HAS_NVIDIA_STREAMLINE
    if (!initialized) {
        return;
    }

    sl::VulkanInfo info{};
    info.instance = *instance;
    info.physicalDevice = *device.GetPhysical();
    info.device = *device.GetLogical();
    info.graphicsQueueFamily = device.GetGraphicsFamily();
    info.graphicsQueueIndex = 0;

    const sl::Result result = slSetVulkanInfo(info);
    if (result != sl::Result::eOk) {
        LOG_WARNING(Render_Vulkan, "Streamline Vulkan device binding failed: {}",
                    static_cast<int>(result));
        return;
    }

    LOG_INFO(Render_Vulkan, "Streamline bound to Eden Vulkan device");
    sl::AdapterInfo adapter_info{};
    adapter_info.vkPhysicalDevice = *device.GetPhysical();
    const sl::Result support = slIsFeatureSupported(sl::kFeatureDLSS, adapter_info);
    dlss_supported = support == sl::Result::eOk;
    if (dlss_supported) {
        LOG_INFO(Render_Vulkan, "DLSS is supported on the selected Vulkan adapter");
    } else {
        LOG_WARNING(Render_Vulkan, "DLSS is unavailable on the selected Vulkan adapter: {}",
                    static_cast<int>(support));
    }
#else
    (void)instance;
    (void)device;
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
