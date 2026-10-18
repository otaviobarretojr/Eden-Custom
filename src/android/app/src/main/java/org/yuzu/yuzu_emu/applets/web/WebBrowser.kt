// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.applets.web

import android.content.Intent
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.webkit.WebResourceError
import androidx.webkit.WebViewCompat
import androidx.webkit.WebViewFeature
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.FrameLayout
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import org.yuzu.yuzu_emu.utils.Log
import androidx.annotation.Keep
import org.yuzu.yuzu_emu.NativeLibrary

/**
    Should run WebBrowser as a new intent.
*/

@Keep
object WebBrowser {
    // Adapted from Eden's Qt WINDOW_NX_SCRIPT: provides the Switch-side
    // JavaScript API before inline page scripts execute. No Java/Kotlin
    // JavascriptInterface is exposed to untrusted HTML.
    private val nxBootstrap = """
        (function () {
            if (window.nx) return;
            window.end_applet = false;
            window.yuzu_key_callbacks = [];
            var codes = {A:0, B:1, X:2, Y:3, L:6, R:7};
            window.nx = {
                endApplet: function () { window.end_applet = true; },
                addEventListener: function (t, f, o) { window.addEventListener(t, f, o); },
                playSystemSe: function () {},
                sendMessage: function () {},
                footer: {
                    setAssign: function (key, label, action) {
                        var n = codes[key];
                        if (n !== undefined && typeof action === 'function') {
                            window.yuzu_key_callbacks[n] = action;
                        }
                    },
                    unsetAssign: function (key) {
                        var n = codes[key];
                        if (n !== undefined) window.yuzu_key_callbacks[n] = function () {};
                    },
                    setFixed: function () {}
                },
                playReport: {
                    incrementCounter: function () {},
                    setCounterSetIdentifier: function () {}
                }
            };
            window.yuzu_key_callbacks[1] = function () { window.history.back(); };
            window.yuzu_key_callbacks[2] = function () { window.nx.endApplet(); };
        })();
    """.trimIndent()

    /**
     * Local HTML applet for Ni no Kuni only. Runs on a JNI worker thread and
     * waits for the user to dismiss the dialog. No external URLs are loaded.
     * Returning null means cancellation or unavailable local HTML.
     */
    @JvmStatic
    fun openLocal(url: String): String? {
        val activity = NativeLibrary.sEmulationActivity.get() ?: return null
        val root = activity.getExternalFilesDir(null) ?: return null
        val allowed = File(
            root, "cache/offline_web_applet_manual/0100E5600D446000"
        ).canonicalFile
        val document = runCatching { File(url.substringBefore('?')).canonicalFile }
            .getOrNull() ?: return null
        if (!document.path.startsWith(allowed.path + File.separator) ||
            !document.isFile || !document.extension.equals("html", true)) {
            Log.error("[ASTRYX WebView] Rejected missing or out-of-scope offline document")
            return null
        }

        val outcome = AtomicReference<String?>(null)
        val completion = CountDownLatch(1)
        val dialogRef = AtomicReference<androidx.appcompat.app.AlertDialog?>()
        activity.runOnUiThread {
            if (activity.isFinishing || activity.isDestroyed) {
                completion.countDown()
                return@runOnUiThread
            }
            try {
                val browser = WebView(activity)
                val wrapper = FrameLayout(activity).apply {
                    val height = (resources.displayMetrics.heightPixels * 0.72f).toInt()
                    addView(browser, FrameLayout.LayoutParams(
                        FrameLayout.LayoutParams.MATCH_PARENT, height
                    ))
                }
                browser.settings.apply {
                    javaScriptEnabled = true
                    domStorageEnabled = true
                    allowFileAccess = true
                    allowContentAccess = false
                    blockNetworkLoads = true
                    @Suppress("DEPRECATION")
                    allowFileAccessFromFileURLs = false
                    @Suppress("DEPRECATION")
                    allowUniversalAccessFromFileURLs = false
                }

                val hasDocumentStart = WebViewFeature.isFeatureSupported(
                    WebViewFeature.DOCUMENT_START_SCRIPT
                )
                if (hasDocumentStart) {
                    WebViewCompat.addDocumentStartJavaScript(browser, nxBootstrap, setOf("*"))
                    Log.info("[ASTRYX WebView] window.nx bootstrap registered at document start")
                } else {
                    Log.error("[ASTRYX WebView] document-start JS not supported; using late shim")
                }
                val dialog = MaterialAlertDialogBuilder(activity)
                    .setTitle("ASTRYX · Ni no Kuni")
                    .setView(wrapper)
                    .setPositiveButton("Voltar ao jogo") { _, _ ->
                        outcome.set("end")
                        Log.info("[ASTRYX WebView] User pressed return-to-game button")
                    }
                    .setNegativeButton("Cancelar", null)
                    .create()
                dialogRef.set(dialog)
                val pollHandler = Handler(Looper.getMainLooper())
                val pollEnd = object : Runnable {
                    override fun run() {
                        if (!dialog.isShowing) return
                        browser.evaluateJavascript(
                            "(function(){return !!window.end_applet;})()"
                        ) { result ->
                            if (!dialog.isShowing) return@evaluateJavascript
                            if (result == "true") {
                                Log.info("[ASTRYX WebView] window.nx.endApplet completed")
                                outcome.set("end")
                                dialog.dismiss()
                            } else {
                                pollHandler.postDelayed(this, 250)
                            }
                        }
                    }
                }

                browser.webViewClient = object : WebViewClient() {
                    override fun onPageFinished(view: WebView?, finishedUrl: String?) {
                        Log.info("[ASTRYX WebView] HTML main page loaded")
                        if (!hasDocumentStart) view?.evaluateJavascript(nxBootstrap, null)
                    }

                    override fun onReceivedError(
                        view: WebView?,
                        request: WebResourceRequest?,
                        error: WebResourceError?
                    ) {
                        if (request?.isForMainFrame == true) {
                            Log.error("[ASTRYX WebView] Main frame load failure: ${error?.errorCode}")
                        }
                    }

                    override fun shouldOverrideUrlLoading(
                        view: WebView?, request: WebResourceRequest?
                    ): Boolean {
                        val target = request?.url ?: return true
                        if (target.scheme == "file") {
                            val targetFile = runCatching {
                                File(target.path.orEmpty()).canonicalFile
                            }.getOrNull() ?: return true
                            return !targetFile.path.startsWith(
                                allowed.path + File.separator
                            )
                        }
                        if (target.scheme in listOf("applet", "nx") ||
                            (target.scheme in listOf("http", "https") &&
                                target.host == "localhost")) {
                            Log.info("[ASTRYX WebView] Callback URL requested")
                            outcome.set("callback:${target}")
                            dialog.dismiss()
                        }
                        // Block all other destinations; never access network.
                        return true
                    }
                }
                dialog.setOnDismissListener {
                    pollHandler.removeCallbacks(pollEnd)
                    browser.stopLoading()
                    browser.destroy()
                    if (outcome.get() == null) Log.info("[ASTRYX WebView] Page dismissed without completion")
                    completion.countDown()
                }
                dialog.show()
                pollHandler.postDelayed(pollEnd, 500)
                val suffix = url.substringAfter('?', "").let {
                    if (it.isEmpty()) "" else "?$it"
                }
                browser.loadUrl(Uri.fromFile(document).toString() + suffix)
                Log.info("[ASTRYX WebView] Opened local Ni no Kuni HTML")
            } catch (error: Exception) {
                Log.error("[ASTRYX WebView] Failed: ${error.message}")
                dialogRef.getAndSet(null)?.dismiss()
                completion.countDown()
            }
        }
        if (!completion.await(180, TimeUnit.SECONDS)) {
            activity.runOnUiThread { dialogRef.getAndSet(null)?.dismiss() }
            Log.error("[ASTRYX WebView] Timed out waiting for local page dialog")
            return null
        }
        return outcome.get()
    }

    @JvmStatic
    fun openExternal(url: String) {
        val activity = NativeLibrary.sEmulationActivity.get() ?: run {
            return
        }

        activity.runOnUiThread {
            try {
                val intent = Intent(Intent.ACTION_VIEW, Uri.parse(url)).apply {
                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                }
                activity.startActivity(intent)
            } catch (e: Exception) {
                Log.error("WebBrowser failed to launch $url: ${e.message}")
            }
        }
    }
}
