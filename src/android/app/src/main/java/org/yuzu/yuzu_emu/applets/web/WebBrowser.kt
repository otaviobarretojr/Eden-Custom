// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.applets.web

import android.app.AlertDialog
import android.content.Intent
import android.net.Uri
import android.os.Looper
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.annotation.Keep
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.utils.Log
import java.io.ByteArrayInputStream
import java.io.File
import java.io.FileInputStream
import java.net.URLConnection
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/**
 * Android frontend for browser applets. Offline documents are displayed in an
 * isolated WebView origin, without granting file:// access to game-supplied JS.
 */
@Keep
object WebBrowser {
    private const val OFFLINE_HOST = "astryx-offline.invalid"

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

    /**
     * Called on a worker thread by native OpenLocalWebPage. Returns only after
     * the player closes the dialog. An empty result means the page could not open.
     */
    @JvmStatic
    fun openLocal(url: String): String {
        val activity = NativeLibrary.sEmulationActivity.get() ?: return ""
        if (Looper.myLooper() == Looper.getMainLooper()) {
            Log.error("Offline applet must not block Android UI thread")
            return ""
        }

        val completed = CountDownLatch(1)
        val lastUrl = AtomicReference("")
        val dialogRef = AtomicReference<AlertDialog?>(null)

        activity.runOnUiThread {
            try {
                val rawPath = url.substringBefore('?')
                val document = File(rawPath).canonicalFile
                val root = document.parentFile
                if (!document.isFile || root == null) {
                    Log.error("Offline applet document is missing: $rawPath")
                    completed.countDown()
                    return@runOnUiThread
                }

                val webView = WebView(activity)
                webView.settings.javaScriptEnabled = true
                webView.settings.domStorageEnabled = true
                webView.settings.allowFileAccess = false
                webView.settings.allowContentAccess = false
                webView.settings.setSupportMultipleWindows(false)
                webView.minimumHeight = (360 * activity.resources.displayMetrics.density).toInt()

                webView.webViewClient = object : WebViewClient() {
                    override fun shouldInterceptRequest(
                        view: WebView?,
                        request: WebResourceRequest
                    ): WebResourceResponse {
                        val requestUri = request.url
                        if (requestUri.scheme != "https" || requestUri.host != OFFLINE_HOST) {
                            return blockedResponse()
                        }
                        return try {
                            val relative = Uri.decode(requestUri.encodedPath ?: "")
                                .removePrefix("/")
                            val resource = File(root, relative).canonicalFile
                            // Keep all reads inside this title's extracted HTML directory.
                            if (!resource.path.startsWith(root.path + File.separator) ||
                                !resource.isFile
                            ) {
                                blockedResponse()
                            } else {
                                WebResourceResponse(
                                    mimeType(resource),
                                    null,
                                    FileInputStream(resource)
                                )
                            }
                        } catch (e: Exception) {
                            Log.error("Offline applet resource failed: ${e.message}")
                            blockedResponse()
                        }
                    }

                    override fun shouldOverrideUrlLoading(
                        view: WebView?,
                        request: WebResourceRequest
                    ): Boolean {
                        val next = request.url
                        if (next.scheme != "https" || next.host != OFFLINE_HOST) {
                            Log.warning("Offline applet blocked external navigation: $next")
                            return true
                        }
                        lastUrl.set(next.toString())
                        return false
                    }
                }

                val baseUrl = "https://$OFFLINE_HOST/" + Uri.encode(document.name)
                val querySuffix = if (url.contains('?')) "?" + url.substringAfter('?') else ""
                lastUrl.set(baseUrl + querySuffix)

                val dialog = AlertDialog.Builder(activity)
                    .setTitle("ASTRYX • Conteúdo do jogo")
                    .setView(webView)
                    .setPositiveButton("Fechar") { window, _ -> window.dismiss() }
                    .create()
                dialogRef.set(dialog)
                dialog.setOnDismissListener {
                    try {
                        webView.stopLoading()
                        webView.destroy()
                    } finally {
                        completed.countDown()
                    }
                }
                dialog.show()
                webView.loadUrl(baseUrl + querySuffix)
                Log.info("Offline applet WebView opened")
            } catch (e: Exception) {
                Log.error("Offline applet failed to open: ${e.message}")
                dialogRef.get()?.dismiss()
                completed.countDown()
            }
        }

        // Do not leave an emulation worker blocked forever if the Activity is lost.
        return try {
            if (!completed.await(30, TimeUnit.MINUTES)) {
                activity.runOnUiThread { dialogRef.get()?.dismiss() }
                ""
            } else {
                lastUrl.get()
            }
        } catch (_: InterruptedException) {
            Thread.currentThread().interrupt()
            activity.runOnUiThread { dialogRef.get()?.dismiss() }
            ""
        }
    }

    private fun blockedResponse(): WebResourceResponse =
        WebResourceResponse(
            "text/plain",
            "UTF-8",
            ByteArrayInputStream("Blocked by ASTRYX offline applet".toByteArray())
        )

    private fun mimeType(file: File): String =
        when (file.extension.lowercase()) {
            "html", "htm" -> "text/html"
            "js", "mjs" -> "application/javascript"
            "css" -> "text/css"
            "json" -> "application/json"
            "svg" -> "image/svg+xml"
            "wasm" -> "application/wasm"
            "woff" -> "font/woff"
            "woff2" -> "font/woff2"
            "ttf" -> "font/ttf"
            "otf" -> "font/otf"
            else -> URLConnection.guessContentTypeFromName(file.name)
                ?: "application/octet-stream"
        }
}
