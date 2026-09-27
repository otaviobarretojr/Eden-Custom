// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "video_core/vulkan_common/streamline_bootstrap.h"

#include <atomic>
#include <string>
#include <string_view>

#include "common/dynamic_library.h"
#include "common/logging.h"

#if defined(_WIN32) && defined(HAS_NVIDIA_STREAMLINE)
#include <windows.h>
#include <sl.h>
#include <sl_security.h>
#endif

namespace Vulkan {
namespace {

std::atomic_bool g_streamline_initialized{false};

#if defined(_WIN32) && defined(HAS_NVIDIA_STREAMLINE)
[[nodiscard]] std::wstring ApplicationLocalInterposerPath() {
    std::wstring path(32768, L'\0');
    const DWORD size =
        GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (size == 0 || size >= path.size()) {
        return {};
    }

    path.resize(size);
    const auto separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
        return {};
    }

    path.resize(separator + 1);
    path += L"sl.interposer.dll";
    return path;
}
#endif

} // namespace

StreamlineBootstrap::StreamlineBootstrap() {
#if defined(_WIN32) && defined(HAS_NVIDIA_STREAMLINE)
    const std::wstring interposer_path = ApplicationLocalInterposerPath();
    if (interposer_path.empty()) {
        LOG_WARNING(Render_Vulkan,
                    "Streamline bootstrap: could not resolve application-local interposer path");
        return;
    }

    if (!sl::security::verifyEmbeddedSignature(interposer_path.c_str())) {
        LOG_WARNING(Render_Vulkan,
                    "Streamline bootstrap: sl.interposer.dll failed NVIDIA signature "
                    "verification; Streamline remains disabled");
        return;
    }

    interposer = std::make_unique<Common::DynamicLibrary>();
    if (!interposer->Open(std::wstring_view{interposer_path})) {
        LOG_WARNING(Render_Vulkan,
                    "Streamline bootstrap: verified sl.interposer.dll could not be loaded");
        interposer.reset();
        return;
    }

    PFun_slInit* sl_init{};
    if (!interposer->GetSymbol("slInit", &sl_init) || sl_init == nullptr) {
        LOG_WARNING(Render_Vulkan, "Streamline bootstrap: slInit export was not found");
        interposer.reset();
        return;
    }

    sl::Preferences preferences{};
    preferences.engine = sl::EngineType::eCustom;
    preferences.engineVersion = "Eden-Custom-DLSS5-Compatibility";
    preferences.renderAPI = sl::RenderAPI::eVulkan;
    preferences.flags =
        preferences.flags | sl::PreferenceFlags::eUseFrameBasedResourceTagging;

    // Deliberately request no feature here. The public Streamline 2.14.1 source announces
    // sl.dlss_nr, but does not expose a public feature-specific header/contract for it.
    // Eden initializes only the core lifecycle rather than guessing private ABI values.
    const sl::Result result = sl_init(preferences, sl::kSDKVersion);
    if (result != sl::Result::eOk) {
        LOG_WARNING(Render_Vulkan, "Streamline bootstrap: slInit failed with result {}",
                    static_cast<int>(result));
        interposer.reset();
        return;
    }

    initialized = true;
    g_streamline_initialized.store(true, std::memory_order_release);
    LOG_INFO(Render_Vulkan,
             "Streamline core initialized before Vulkan startup; DLSS 5 Neural Rendering "
             "remains intentionally unloaded");
#endif
}

bool StreamlineBootstrap::BeginFrame(u64 eden_frame_id) noexcept {
#if defined(_WIN32) && defined(HAS_NVIDIA_STREAMLINE)
    if (!initialized || !interposer || eden_frame_id == 0) {
        return false;
    }

    PFun_slGetNewFrameToken* sl_get_new_frame_token{};
    if (!interposer->GetSymbol("slGetNewFrameToken", &sl_get_new_frame_token) ||
        sl_get_new_frame_token == nullptr) {
        LOG_WARNING(Render_Vulkan,
                    "Streamline frame lifecycle: slGetNewFrameToken export was not found");
        return false;
    }

    // Eden already owns a monotonic frame id. Streamline accepts a host-provided uint32 frame
    // index, which keeps its token correlated with Eden without relying on SL's internal counter.
    const u32 frame_index = static_cast<u32>(eden_frame_id);
    sl::FrameToken* token{};
    const sl::Result result = sl_get_new_frame_token(token, &frame_index);
    if (result != sl::Result::eOk || token == nullptr) {
        LOG_WARNING(Render_Vulkan,
                    "Streamline frame lifecycle: slGetNewFrameToken failed for Eden frame {} "
                    "with result {}",
                    eden_frame_id, static_cast<int>(result));
        return false;
    }

    current_eden_frame_id = eden_frame_id;
    return true;
#else
    (void)eden_frame_id;
    return false;
#endif
}

StreamlineBootstrap::~StreamlineBootstrap() {
#if defined(_WIN32) && defined(HAS_NVIDIA_STREAMLINE)
    if (!initialized || !interposer) {
        return;
    }

    PFun_slShutdown* sl_shutdown{};
    if (interposer->GetSymbol("slShutdown", &sl_shutdown) && sl_shutdown != nullptr) {
        const sl::Result result = sl_shutdown();
        if (result != sl::Result::eOk) {
            LOG_WARNING(Render_Vulkan, "Streamline shutdown failed with result {}",
                        static_cast<int>(result));
        }
    }

    g_streamline_initialized.store(false, std::memory_order_release);
    initialized = false;
#endif
}

bool IsStreamlineBootstrapReady() noexcept {
    return g_streamline_initialized.load(std::memory_order_acquire);
}

} // namespace Vulkan
