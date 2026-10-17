// SPDX-FileCopyrightText: Copyright 2026 Eden S25 Ultra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.activities

import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.util.Locale

/**
 * Read-only online catalog for explicitly configured HTTPS servers.
 *
 * No default third-party game-ROM sources are included. The built-in homebrew
 * directory lists independently distributed community projects; many .nro
 * packages require Switch HBL and will NOT launch in the Android emulator.
 *
 * See https://tinfoil.io/install/network (HTTP/HTTPS JSON "files/directories").
 */
internal object S25OnlineCatalog {
    const val HOMEBREW_INDEX = "https://switch.cdn.fortheusers.org/repo.json"
    private const val MAX_INDEX_BYTES = 7 * 1024 * 1024
    private const val MAX_ITEMS = 3500
    private val supportedSuffixes = setOf("nsp", "nsz", "xci", "xcz", "zip")
    data class Entry(
        val title: String,
        val url: String,
        val size: Long,
        val kind: String,
        val description: String = ""
    )
    data class Result(
        val items: List<Entry>,
        val directories: List<Entry>,
        val message: String = ""
    )

    fun isValidHttps(raw: String): Boolean {
        val parsed = runCatching { URL(raw) }.getOrNull() ?: return false
        if (!parsed.protocol.equals("https", true) ||
            parsed.userInfo != null || parsed.port !in listOf(-1, 443)) return false
        val h = parsed.host.lowercase(Locale.ROOT).removePrefix("[").removeSuffix("]")
        if (h.isEmpty() || h == "localhost" || h.endsWith(".localhost") ||
            h.endsWith(".local") || h.endsWith(".internal") || h.endsWith(".lan") ||
            h.endsWith(".onion") || h.contains(':') || h == "0.0.0.0" ||
            h.endsWith(".test") || h.endsWith(".invalid")) return false
        val parts = h.split('.')
        if (parts.size == 4 && parts.all { part -> part.toIntOrNull()?.let { n -> n in 0..255 } == true }) {
            val a = parts[0].toInt()
            val b = parts[1].toInt()
            // Block private, loopback, link-local, carrier-grade NAT, multicast,
            // documentation and reserved IPv4 ranges.
            if (a == 0 || a == 10 || a == 127 || a >= 224 ||
                a == 169 && b == 254 || a == 172 && b in 16..31 ||
                a == 192 && b == 168 || a == 100 && b in 64..127 ||
                a == 192 && b == 0 || a == 198 && b in 18..19 ||
                a == 203 && b == 0) return false
        }
        return true
    }

    private fun fullUrl(base: String, child: String): String? {
        val resolved = runCatching { URL(URL(base), child).toString() }.getOrNull()
            ?: return null
        return resolved.takeIf(::isValidHttps)
    }

    fun tinfoil(indexUrl: String): Result {
        require(isValidHttps(indexUrl)) { "Use uma URL HTTPS pública sem credenciais." }
        val root = JSONObject(readText(indexUrl))
        val files = root.optJSONArray("files")
        val dirs = root.optJSONArray("directories")
        if (files == null && dirs == null) {
            error("Este endereço não retornou o índice JSON 'files/directories' do Tinfoil.")
        }
        val out = mutableListOf<Entry>()
        val nested = mutableListOf<Entry>()
        if (files != null) {
            for (i in 0 until minOf(files.length(), MAX_ITEMS)) {
                val obj = files.optJSONObject(i)
                val raw = obj?.optString("url") ?: files.optString(i)
                val uri = fullUrl(indexUrl, raw) ?: continue
                val name = obj?.optString("name")?.takeIf(String::isNotBlank)
                    ?: UriName.title(uri)
                val ext = UriName.extension(uri)
                if (ext !in supportedSuffixes) continue
                out.add(Entry(
                    title = name.take(180),
                    url = uri,
                    size = obj?.optLong("size")?.takeIf { it > 0 } ?: -1L,
                    kind = ext.uppercase(Locale.ROOT),
                    description = "Arquivo listado pelo servidor configurado."
                ))
            }
        }
        if (dirs != null) {
            for (i in 0 until minOf(dirs.length(), 250)) {
                val obj = dirs.optJSONObject(i)
                val raw = obj?.optString("url") ?: dirs.optString(i)
                val uri = fullUrl(indexUrl, raw) ?: continue
                val label = obj?.optString("name")?.takeIf(String::isNotBlank)
                    ?: UriName.title(uri)
                nested.add(Entry(label.take(120), uri, -1L, "PASTA"))
            }
        }
        return Result(out.distinctBy { it.url }, nested.distinctBy { it.url },
            root.optString("success").take(350))
    }

    fun homebrews(): Result {
        val root = JSONObject(readText(HOMEBREW_INDEX))
        val packages = root.optJSONArray("packages")
            ?: error("O catálogo Homebrew App Store não retornou a lista de pacotes.")
        val result = mutableListOf<Entry>()
        for (i in 0 until minOf(packages.length(), MAX_ITEMS)) {
            val obj = packages.optJSONObject(i) ?: continue
            val name = obj.optString("name")
            if (!Regex("[A-Za-z0-9_.-]{1,90}").matches(name)) continue
            val title = obj.optString("title").ifBlank { name }
            val license = obj.optString("license").take(55)
            val category = obj.optString("category").take(40)
            val description = obj.optString("description").take(240)
            val url = "https://switch.cdn.fortheusers.org/zips/$name.zip"
            result.add(Entry(
                title = title.take(140),
                url = url,
                size = obj.optLong("size").takeIf { it > 0 } ?: -1L,
                kind = "HOMEBREW",
                description = "Categoria: $category · Licença: $license\n" +
                    description + "\nPacote para Nintendo Switch/HBL; pode NÃO abrir no Eden Android."
            ))
        }
        return Result(
            result.sortedWith(compareByDescending<Entry> {
                it.description.startsWith("Categoria: game", true)
            }.thenBy { it.title.lowercase(Locale.ROOT) }),
            emptyList(),
            "Fonte: Homebrew App Store (ForTheUsers). Pacotes comunitários."
        )
    }

    fun downloadable(entry: Entry): Boolean =
        entry.kind in setOf("NSP", "NSZ", "XCI", "XCZ", "ZIP", "HOMEBREW") &&
            isValidHttps(entry.url)

    private fun readText(index: String): String {
        var next = index
        repeat(5) {
            if (!isValidHttps(next)) error("Redirecionamento para destino não autorizado.")
            val conn = (URL(next).openConnection() as HttpURLConnection).apply {
                connectTimeout = 12000
                readTimeout = 25000
                instanceFollowRedirects = false
                setRequestProperty("Accept", "application/json")
                setRequestProperty("User-Agent", "Eden-S25-OnlineLibrary/1.0")
            }
            try {
                val code = conn.responseCode
                if (code in 300..399) {
                    val location = conn.getHeaderField("Location")
                    if (location.isNullOrEmpty()) error("Redirecionamento sem destino.")
                    next = URL(URL(next), location).toString()
                } else {
                    if (code !in 200..299) error("Servidor retornou HTTP $code.")
                    if (conn.contentLengthLong > MAX_INDEX_BYTES) {
                        error("Catálogo excede o tamanho permitido.")
                    }
                    val out = ByteArrayOutputStream()
                    conn.inputStream.use { stream ->
                        val buffer = ByteArray(8192)
                        while (true) {
                            val count = stream.read(buffer)
                            if (count < 0) break
                            if (out.size() + count > MAX_INDEX_BYTES) {
                                error("Catálogo excede 7 MiB.")
                            }
                            out.write(buffer, 0, count)
                        }
                    }
                    return out.toString(Charsets.UTF_8.name())
                }
            } finally { conn.disconnect() }
        }
        error("Redirecionamentos excessivos no servidor.")
    }

    private object UriName {
        fun title(url: String): String {
            val path = runCatching { URL(url).path }.getOrDefault("")
            val last = path.trimEnd('/').substringAfterLast('/')
            return java.net.URLDecoder.decode(last, "UTF-8").takeIf(String::isNotBlank)
                ?: "Arquivo"
        }
        fun extension(url: String): String {
            val path = runCatching { URL(url).path }.getOrDefault("")
            return path.substringAfterLast('.', "").lowercase(Locale.ROOT)
        }
    }
}
