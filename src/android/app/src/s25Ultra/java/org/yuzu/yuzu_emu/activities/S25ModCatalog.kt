// SPDX-FileCopyrightText: Copyright 2026 Eden S25 Ultra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.activities

import android.net.Uri
import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.text.Normalizer
import java.util.Locale

/** Read-only unofficial sources. Entries do not establish version compatibility. */
internal data class S25ModOffer(
    val title: String,
    val source: String,
    val sourceUrl: String,
    val downloadUrl: String?,
    val description: String,
    val translation: Boolean,
    val gameBananaId: Long = 0L
)

internal object S25ModCatalog {
    private const val MODS_REPO = "Fl4sh9174/Switch-Emulator-Ultrawide-FPS-Mods"
    private const val LEGACY_TRANSLATIONS =
        "https://raw.githubusercontent.com/gamemoddesignbr/nx-links/main/nx-links-v220.json"
    private const val MAX_CATALOG_BYTES = 3 * 1024 * 1024

    fun githubPacks(titleId: String): List<S25ModOffer> {
        val uri = "https://api.github.com/repos/$MODS_REPO/contents?ref=main"
        val entries = JSONArray(readText(uri, MAX_CATALOG_BYTES))
        val result = mutableListOf<S25ModOffer>()
        for (i in 0 until entries.length()) {
            val item = entries.optJSONObject(i) ?: continue
            val name = item.optString("name")
            if (item.optString("type") != "file" || !name.endsWith(".zip", true)) continue
            val extractedId = Regex("\\[([0-9a-fA-F]{16})\\]").find(name)?.groupValues?.get(1)
                ?: continue
            if (!extractedId.equals(titleId, true)) continue
            val download = item.optString("download_url")
            val origin = item.optString("html_url")
            if (!isApprovedUrl(download) || !isApprovedUrl(origin)) continue
            result.add(S25ModOffer(
                name.substringBefore(" [").ifBlank { name.removeSuffix(".zip") },
                "GitHub · Fl4sh9174", origin, download,
                "Pacote comunitário de FPS/gráficos. Confira versões e compatibilidade.",
                false
            ))
        }
        return result
    }

    /** Last updated 2023. User must accept that downloads can be broken or outdated. */
    fun portugueseTranslations(titleId: String): List<S25ModOffer> {
        val doc = JSONObject(readText(LEGACY_TRANSLATIONS, MAX_CATALOG_BYTES))
        val translations = doc.optJSONObject("translations") ?: return emptyList()
        val result = mutableListOf<S25ModOffer>()
        val keys = translations.keys()
        while (keys.hasNext()) {
            val name = keys.next()
            val item = translations.optJSONObject(name) ?: continue
            if (!item.optBoolean("enabled", false)) continue
            val url = item.optString("link")
            if (!url.startsWith("https://github.com/", true) ||
                !url.substringBefore('?').endsWith(".zip", true)) continue
            val hash = item.optJSONObject("installed")?.optString("hash_file").orEmpty()
            val fromHash = Regex("(?i)^([0-9a-f]{16})/").find(hash)?.groupValues?.get(1)
            val fromLink = Regex("(?i)([0-9a-f]{16})\\.zip").find(url)?.groupValues?.get(1)
            if (fromHash?.equals(titleId, true) != true &&
                fromLink?.equals(titleId, true) != true) continue
            result.add(S25ModOffer(
                name, "CNX · catálogo histórico (2023)", url, url,
                "Tradução PT-BR antiga: disponibilidade e versão NÃO verificadas.",
                true
            ))
        }
        return result
    }

    /** GameBanana public game-name lookup, then mod listing scoped to its game ID. */
    fun gameBanana(gameName: String, translationsOnly: Boolean): List<S25ModOffer> {
        val query = gameName.trim().replace(Regex("\\s+"), " ")
        if (query.length < 3) return emptyList()
        val prefix = query.split(" ").take(3).joinToString(" ")
        val endpoint = "https://api.gamebanana.com/Core/List/Like?" +
            "itemtype=Game&field=name&match=" + Uri.encode(prefix)
        val games = JSONArray(readText(endpoint, 512 * 1024))
        var gameId = 0L
        var score = -1
        fun normalize(v: String): String = Normalizer.normalize(v, Normalizer.Form.NFD)
            .replace(Regex("\\p{Mn}+"), "").lowercase(Locale.ROOT)
            .replace(Regex("[^a-z0-9]"), "")
        val needle = normalize(query)
        for (i in 0 until games.length()) {
            val game = games.optJSONObject(i) ?: continue
            val name = normalize(game.optString("name"))
            val points = when {
                name == needle -> 100
                needle.length >= 5 && (name.contains(needle) || needle.contains(name)) -> 60
                else -> 0
            }
            if (points > score && points > 0) {
                gameId = game.optLong("id")
                score = points
            }
        }
        if (gameId <= 0) return emptyList()
        val listUrl = "https://gamebanana.com/apiv11/Mod/Index?" +
            "_nPage=1&_nPerpage=35&_aFilters%5BGeneric_Game%5D=$gameId"
        val mods = JSONObject(readText(listUrl, MAX_CATALOG_BYTES))
            .optJSONArray("_aRecords") ?: return emptyList()
        val result = mutableListOf<S25ModOffer>()
        val translations = Regex("(?i)tradu[cç][aã]o|translation|portugu[eê]s|brazilian|pt[-_ ]?br")
        for (i in 0 until mods.length()) {
            val item = mods.optJSONObject(i) ?: continue
            val name = item.optString("_sName")
            val id = item.optLong("_idRow")
            if (id <= 0 || name.isBlank() || translationsOnly != translations.containsMatchIn(name)) continue
            result.add(S25ModOffer(
                name, "GameBanana", "https://gamebanana.com/mods/$id", null,
                "Publicação comunitária; verifique compatibilidade e licença antes de instalar.",
                translationsOnly, id
            ))
        }
        return result
    }

    fun gameBananaArchive(modId: Long): String? {
        if (modId <= 0) return null
        val url = "https://gamebanana.com/apiv11/Mod/$modId?" +
            "_csvProperties=_sName,_aFiles"
        val files = JSONObject(readText(url, 1024 * 1024)).optJSONArray("_aFiles")
            ?: return null
        for (i in 0 until files.length()) {
            val file = files.optJSONObject(i) ?: continue
            val href = file.optString("_sDownloadUrl")
            val filename = file.optString("_sFile")
            if (filename.endsWith(".zip", true) && isApprovedUrl(href)) return href
        }
        return null
    }

    fun isApprovedUrl(url: String): Boolean {
        val parsed = runCatching { URL(url) }.getOrNull() ?: return false
        if (parsed.protocol != "https" || parsed.userInfo != null ||
            parsed.port !in listOf(-1, 443)) return false
        val host = parsed.host.lowercase(Locale.ROOT)
        return host in setOf(
            "github.com", "api.github.com", "raw.githubusercontent.com",
            "objects.githubusercontent.com", "release-assets.githubusercontent.com",
            "gamebanana.com", "api.gamebanana.com"
        ) || host.endsWith(".gamebanana.com") || host.endsWith(".githubusercontent.com")
    }

    fun connect(url: String): HttpURLConnection {
        var current = url
        repeat(6) {
            require(isApprovedUrl(current)) { "Origem ou redirecionamento não autorizado." }
            val connection = (URL(current).openConnection() as HttpURLConnection).apply {
                connectTimeout = 12000
                readTimeout = 22000
                instanceFollowRedirects = false
                setRequestProperty("Accept", "application/json, application/octet-stream, */*")
                setRequestProperty("User-Agent", "Eden-S25-ModBrowser/1.0")
            }
            val status = connection.responseCode
            if (status in 300..399) {
                val redirect = connection.getHeaderField("Location")
                connection.disconnect()
                require(!redirect.isNullOrBlank()) { "Redirecionamento inválido." }
                current = URL(URL(current), redirect).toString()
            } else {
                if (status !in 200..299) {
                    connection.disconnect()
                    error("Servidor retornou HTTP $status.")
                }
                return connection
            }
        }
        error("Redirecionamentos excessivos.")
    }

    private fun readText(url: String, max: Int): String {
        val conn = connect(url)
        try {
            if (conn.contentLengthLong > max) error("Catálogo acima do limite permitido.")
            conn.inputStream.use { input ->
                val out = java.io.ByteArrayOutputStream()
                val bytes = ByteArray(8192)
                while (true) {
                    val n = input.read(bytes)
                    if (n < 0) break
                    if (out.size() + n > max) error("Catálogo acima do limite permitido.")
                    out.write(bytes, 0, n)
                }
                return out.toString(Charsets.UTF_8.name())
            }
        } finally { conn.disconnect() }
    }
}
