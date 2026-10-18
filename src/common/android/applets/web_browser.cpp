// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <string_view>
#include <thread>

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
    s_open_local_web_page = env->GetStaticMethodID(
        s_native_library_class, "openLocalWebPage", "(Ljava/lang/String;)Ljava/lang/String;");
}

void CleanupJNI(JNIEnv* env) {
    if (s_native_library_class != nullptr) {
        env->DeleteGlobalRef(s_native_library_class);
        s_native_library_class = nullptr;
    }
    s_open_external_url = nullptr;
    s_open_local_web_page = nullptr;
}

void AndroidWebBrowser::OpenLocalWebPage(const std::string& local_url, ExtractROMFSCallback extract_romfs_callback, OpenWebPageCallback callback) const {
    using Service::AM::Frontend::WebExitReason;

    // Deliberately restricted to this title's offline privacy/document page.
    // Other titles retain the current Android behavior until a general
    // sandboxed web applet implementation is reviewed.
    if (local_url.find("0100E5600D446000") == std::string::npos) {
        LOG_WARNING(Frontend, "Unsupported Android offline web applet: {}", local_url);
        callback(WebExitReason::WindowClosed, "");
        return;
    }

    const auto query = local_url.find('?');
    const auto document = std::filesystem::path(local_url.substr(0, query));
    std::error_code ec;
    if (!std::filesystem::is_regular_file(document, ec)) {
        LOG_INFO(Frontend, "Extracting Ni no Kuni offline HTML document");
        extract_romfs_callback();
    }

    ec.clear();
    if (!std::filesystem::is_regular_file(document, ec)) {
        LOG_ERROR(Frontend, "Ni no Kuni offline document unavailable after extraction: {}",
                  document.string());
        callback(WebExitReason::WindowClosed, "");
        return;
    }

    // JNI cannot be called directly from a CPU fiber. The keyboard backend
    // uses the same short-lived worker-thread pattern for modal UI requests.
    std::string result;
    std::thread worker([&] {
        JNIEnv* env = GetEnvForThread();
        if (env == nullptr || s_native_library_class == nullptr ||
            s_open_local_web_page == nullptr) {
            LOG_ERROR(Frontend, "Ni no Kuni WebView JNI is not initialized");
            return;
        }

        const jstring url = ToJString(env, local_url);
        auto response = static_cast<jstring>(
            env->CallStaticObjectMethod(s_native_library_class, s_open_local_web_page, url));
        env->DeleteLocalRef(url);
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            LOG_ERROR(Frontend, "Ni no Kuni WebView invocation failed");
            return;
        }
        if (response != nullptr) {
            result = GetJString(env, response);
            env->DeleteLocalRef(response);
        }
    });
    worker.join();

    if (result.starts_with("callback:")) {
        callback(WebExitReason::CallbackURL, result.substr(sizeof("callback:") - 1));
    } else if (!result.empty()) {
        callback(WebExitReason::ExitRequested, result);
    } else {
        callback(WebExitReason::WindowClosed, "");
    }
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
