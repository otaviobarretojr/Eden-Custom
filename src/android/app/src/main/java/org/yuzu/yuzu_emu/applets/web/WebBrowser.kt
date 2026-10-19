// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.applets.web

import android.content.Intent
import android.content.DialogInterface
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.view.KeyEvent
import android.webkit.ConsoleMessage
import android.webkit.WebChromeClient
import android.webkit.WebResourceError
import android.webkit.WebResourceResponse
import android.webkit.MimeTypeMap
import androidx.webkit.WebViewAssetLoader
import androidx.webkit.WebViewCompat
import androidx.webkit.WebViewFeature
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.FrameLayout
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import java.io.File
import java.io.ByteArrayInputStream
import java.util.Locale
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
    private const val offlineHost = "appassets.androidplatform.net"
    private const val offlinePrefix = "/astryx-ni-no-kuni/"
    // Adapted from Eden's Qt WINDOW_NX_SCRIPT: provides the Switch-side
    // JavaScript API before inline page scripts execute. No Java/Kotlin
    // JavascriptInterface is exposed to untrusted HTML.
    private val nxBootstrap = """
        (function () {
            if (window.nx) return;
            window.end_applet = false;
            window.yuzu_key_callbacks = [];
            window.yuzu_footer_labels = {};
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
                            window.yuzu_footer_labels[key] = String(label || '');
                        }
                    },
                    unsetAssign: function (key) {
                        var n = codes[key];
                        if (n !== undefined) {
                            window.yuzu_key_callbacks[n] = function () {};
                            delete window.yuzu_footer_labels[key];
                        }
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
                // Same-origin HTTPS routing for all assets under this game's extracted RomFS.
                // Unlike file://, this permits page-local XHR without widening file URL access.
                val offlineLoader = WebViewAssetLoader.Builder()
                    .setDomain(offlineHost)
                    .addPathHandler(
                        offlinePrefix,
                        WebViewAssetLoader.PathHandler { assetPath ->
                            val requestedFile = runCatching {
                                File(allowed, assetPath).canonicalFile
                            }.getOrNull()
                            if (requestedFile == null ||
                                !requestedFile.path.startsWith(allowed.path + File.separator) ||
                                !requestedFile.isFile) {
                                Log.warning("[ASTRYX WebView] Offline asset missing or rejected")
                                WebResourceResponse(
                                    "text/plain", "UTF-8", 404, "Not Found",
                                    emptyMap(), ByteArrayInputStream(ByteArray(0))
                                )
                            } else {
                                try {
                                    val mime = when (requestedFile.extension.lowercase(Locale.ROOT)) {
                                        "html", "htm" -> "text/html"
                                        "js", "mjs" -> "text/javascript"
                                        "css" -> "text/css"
                                        else -> MimeTypeMap.getSingleton()
                                            .getMimeTypeFromExtension(
                                                requestedFile.extension.lowercase(Locale.ROOT)
                                            ) ?: "application/octet-stream"
                                    }
                                    WebResourceResponse(
                                        mime, if (mime.startsWith("text/")) "UTF-8" else null,
                                        requestedFile.inputStream()
                                    )
                                } catch (error: Exception) {
                                    Log.error(
                                        "[ASTRYX WebView] Offline asset could not be read: " +
                                            "${requestedFile.name}: ${error.message}"
                                    )
                                    WebResourceResponse(
                                        "text/plain", "UTF-8", 404, "Not Found",
                                        emptyMap(), ByteArrayInputStream(ByteArray(0))
                                    )
                                }
                            }
                        }
                    )
                    .build()
                browser.settings.apply {
                    javaScriptEnabled = true
                    domStorageEnabled = true
                    // Offline game HTML may need to autoplay an embedded introduction.
                    mediaPlaybackRequiresUserGesture = false
                    allowFileAccess = false
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
                    WebViewCompat.addDocumentStartJavaScript(browser, nxBootstrap, setOf("https://$offlineHost"))
                    Log.info("[ASTRYX WebView] window.nx bootstrap registered at document start")
                } else {
                    Log.error("[ASTRYX WebView] document-start JS not supported; using late shim")
                }
                // Log script failures: a page load alone does not mean HTML completion.
                browser.webChromeClient = object : WebChromeClient() {
                    override fun onConsoleMessage(message: ConsoleMessage?): Boolean {
                        if (message != null &&
                            (message.messageLevel() == ConsoleMessage.MessageLevel.ERROR ||
                             message.messageLevel() == ConsoleMessage.MessageLevel.WARNING)) {
                            val source = message.sourceId().substringAfterLast('/')
                            Log.warning(
                                "[ASTRYX WebView] JS ${message.messageLevel()} " +
                                    "${message.message().take(240)} at $source:${message.lineNumber()}"
                            )
                        }
                        return false
                    }
                }
                val dialog = MaterialAlertDialogBuilder(activity)
                    .setTitle("ASTRYX · Ni no Kuni")
                    .setView(wrapper)
                    // Never report acceptance simply because a dialog button was pressed.
                    // The game's page must complete via nx.endApplet() or its callback URL.
                    .setPositiveButton("A · Selecionar", null)
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

                var loggedResourceErrors = 0
                browser.webViewClient = object : WebViewClient() {
                    override fun shouldInterceptRequest(
                        view: WebView?, request: WebResourceRequest?
                    ): WebResourceResponse? {
                        val target = request?.url ?: return null
                        return if (target.scheme == "https" &&
                            target.host == offlineHost &&
                            target.path?.startsWith(offlinePrefix) == true
                        ) {
                            offlineLoader.shouldInterceptRequest(target)
                        } else {
                            null // Networking and file:// remain disabled in WebSettings.
                        }
                    }

                    override fun onPageFinished(view: WebView?, finishedUrl: String?) {
                        Log.info("[ASTRYX WebView] HTML main page loaded")
                        if (dialog.isShowing) {
                            dialog.getButton(DialogInterface.BUTTON_POSITIVE).isEnabled = true
                        }
                        if (!hasDocumentStart) view?.evaluateJavascript(nxBootstrap, null)
                        view?.evaluateJavascript(
                            "(function(){return JSON.stringify({nx:!!window.nx, " +
                                "end:!!window.end_applet, ready:document.readyState, " +
                                "scripts:document.scripts.length, " +
                                "aAssigned:typeof window.yuzu_key_callbacks[0]==='function'," +
                                "footerA:(window.yuzu_footer_labels||{}).A||''});})()"
                        ) { state ->
                            Log.info("[ASTRYX WebView] HTML script state: ${state?.take(200)}")
                        }
                    }

                    override fun onReceivedError(
                        view: WebView?,
                        request: WebResourceRequest?,
                        error: WebResourceError?
                    ) {
                        if (loggedResourceErrors++ < 12) {
                            val frame = if (request?.isForMainFrame == true) "main" else "subresource"
                            val resource = request?.url?.lastPathSegment.orEmpty().take(100)
                            Log.error(
                                "[ASTRYX WebView] $frame load failure: " +
                                    "code=${error?.errorCode}, resource=$resource"
                            )
                        }
                    }

                    override fun shouldOverrideUrlLoading(
                        view: WebView?, request: WebResourceRequest?
                    ): Boolean {
                        val target = request?.url ?: return true
                        if (target.scheme == "https" && target.host == offlineHost &&
                            target.path?.startsWith(offlinePrefix) == true) {
                            return false
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
                val aButton = dialog.getButton(DialogInterface.BUTTON_POSITIVE)
                aButton.isEnabled = false // Enable only once the HTML page is ready.
                aButton.setOnClickListener {
                    if (!dialog.isShowing) return@setOnClickListener
                    browser.requestFocus()
                    browser.evaluateJavascript(
                        "(function(){var f=window.yuzu_key_callbacks && " +
                            "window.yuzu_key_callbacks[0];" +
                            "if(typeof f==='function'){f();return 'footer-A';}" +
                            "return 'native-enter';})()"
                    ) { result ->
                        if (!dialog.isShowing) return@evaluateJavascript
                        if (result == "\"footer-A\"") {
                            Log.info("[ASTRYX WebView] User invoked game's A footer action")
                        } else {
                            // Qt's Web applet sends Space/Return when a footer callback
                            // is absent. Android WebView needs native input for focused links.
                            browser.dispatchKeyEvent(
                                KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_ENTER)
                            )
                            browser.dispatchKeyEvent(
                                KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_ENTER)
                            )
                            Log.info("[ASTRYX WebView] User pressed A: native Enter fallback")
                        }
                    }
                }
                pollHandler.postDelayed(pollEnd, 500)
                val suffix = url.substringAfter('?', "").let {
                    if (it.isEmpty()) "" else "?$it"
                }
                val relativeUrl = document.relativeTo(allowed).path
                    .replace(File.separatorChar, '/')
                    .split('/')
                    .joinToString("/") { Uri.encode(it) }
                browser.loadUrl("https://$offlineHost$offlinePrefix$relativeUrl$suffix")
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
