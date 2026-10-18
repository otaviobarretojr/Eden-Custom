// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <system_error>

#include "common/android/android_common.h"
#include "common/android/id_cache.h"
#include "common/android/applets/web_browser.h"
#include "common/logging.h"

static jclass s_native_library_class = nullptr;
static jmethodID s_open_external_url = nullptr;
static jmethodID s_open_local_web_page = nullptr;

namespace Common::Android::WebBrowser {

void InitJNI(JNIEnv* env) {
    const jclass local = env->FindClass("org/yuzu/yuzu_emu/NativeLibrary");
    s_native_library_class = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    s_open_external_url = env->GetStaticMethodID(s_native_library_class, "openExternalUrl", "(Ljava/lang/String;)V");
    s_open_local_web_page = env->GetStaticMethodID(s_native_library_class, "openLocalWebPage",
                                                  "(Ljava/lang/String;)Ljava/lang/String;");
}

void CleanupJNI(JNIEnv* env) {
    if (s_native_library_class != nullptr) {
        env->DeleteGlobalRef(s_native_library_class);
        s_native_library_class = nullptr;
    }
    s_open_external_url = nullptr;
    s_open_local_web_page = nullptr;
}

void AndroidWebBrowser::OpenLocalWebPage(const std::string& local_url,
                                         ExtractROMFSCallback extract_romfs_callback,
                                         OpenWebPageCallback callback) const {
    // The URI may include a query string (e.g. index.html?1); strip it for
    // filesystem checks but pass it intact to the Android WebView.
    const std::string document_path = local_url.substr(0, local_url.find('?'));
    std::error_code ec;
    if (!std::filesystem::is_regular_file(document_path, ec)) {
        // Previously never called on Android: the game's HTML RomFS had no
        // chance to be extracted before its local web page was requested.
        extract_romfs_callback();
    }

    ec.clear();
    if (!std::filesystem::is_regular_file(document_path, ec)) {
        LOG_ERROR(Frontend, "Offline applet HTML not found after extraction: {}", document_path);
        callback(Service::AM::Frontend::WebExitReason::WindowClosed, "");
        return;
    }

    // Waiting happens on a dedicated JNI thread, not Android's UI thread.
    const auto last_url = Common::Android::RunJNIOnFiber<std::string>([&](JNIEnv* env) {
        if (env == nullptr || s_native_library_class == nullptr ||
            s_open_local_web_page == nullptr) {
            LOG_ERROR(Frontend, "Offline applet JNI bridge unavailable");
            return std::string{};
        }
        const jstring url = Common::Android::ToJString(env, local_url);
        const auto result = static_cast<jstring>(
            env->CallStaticObjectMethod(s_native_library_class, s_open_local_web_page, url));
        env->DeleteLocalRef(url);
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            LOG_ERROR(Frontend, "Offline applet Java bridge failed");
            return std::string{};
        }
        const std::string resolved_url = Common::Android::GetJString(env, result);
        if (result != nullptr) {
            env->DeleteLocalRef(result);
        }
        return resolved_url;
    });

    callback(last_url.empty() ? Service::AM::Frontend::WebExitReason::WindowClosed
                              : Service::AM::Frontend::WebExitReason::EndButtonPressed,
             last_url);
}

void AndroidWebBrowser::OpenExternalWebPage(const std::string& external_url, OpenWebPageCallback callback) const {
    // do a dedicated thread, calling from the this thread crashed CPU fiber.
    Common::Android::RunJNIOnFiber<void>([&](JNIEnv* env) {
        if (env != nullptr && s_native_library_class != nullptr && s_open_external_url != nullptr) {
            const jstring j_url = Common::Android::ToJString(env, external_url);
            env->CallStaticVoidMethod(s_native_library_class, s_open_external_url, j_url);
            env->DeleteLocalRef(j_url);
        } else {
            LOG_ERROR(Frontend, "JNI not initialized, cannot open {}", external_url);
        }
        return;
    });

    callback(Service::AM::Frontend::WebExitReason::WindowClosed, external_url);
}

} // namespace Common::Android::WebBrowser
