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
    // CNX-Updater upstream still references /master/nx-links-v204.json; that
    // branch no longer exists. The index below is readable under /main but its
    // 2022 download links must NEVER be considered live downloads.
    private const val CNX_ARCHIVE_INDEX =
        "https://raw.githubusercontent.com/gamemoddesignbr/nx-links/main/nx-links-v220.json"
    private const val CNX_ARCHIVE_PAGE =
        "https://github.com/gamemoddesignbr/nx-links/blob/main/nx-links-v220.json"
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

    /**
     * Curated source pages of original translators. These pages are live,
     * but an external link is NEVER reported as an automated, tested download.
     *
     * IDs are taken from the game's installed program ID. Do not use fuzzy
     * matching to install a translation for a different region/title.
     */
    fun authorTranslationPages(titleId: String): List<S25ModOffer> {
        val originalPages = mapOf(
            "0100000000010000" to ("Super Mario Odyssey · PT-BR v1.1" to "mario-odyssey"),
            "010048701995E000" to ("Luigi's Mansion 2 HD · PT-BR v1.0" to "luigi-mansions-2-hd"),
            "0100152000022000" to ("Mario Kart 8 Deluxe · PT-BR v2.5" to "mario-kart8"),
            "010012101468C000" to ("Metroid Prime Remastered · PT-BR v1.0" to "metroid-prime"),
            "010093801237C000" to ("Metroid Dread · PT-BR v2.1" to "metroid-dread"),
            "0100DCA0064A6800" to ("Luigi's Mansion 3 · PT-BR v1.0" to "luigi-mansions-3"),
            "010028600EBDA000" to ("Super Mario 3D World + Bowser's Fury · PT-BR v1.1" to "mario-3d-bowser"),
            "01009BF0072D4000" to ("Captain Toad: Treasure Tracker · PT-BR" to "captain-toad-switch"),
            "0100C1F0051B6000" to ("Donkey Kong Country: Tropical Freeze · PT-BR" to "donkey-kong-freeze"),
            "01000A10041EA000" to ("The Elder Scrolls V: Skyrim · PT-BR" to "elder-scrolls-skyrim-switch"),
            "010095300B6A4800" to ("South Park: The Stick of Truth · PT-BR" to "south-park-stick-of-truth"),
            "010028C003FD6000" to ("Ultra Street Fighter II · PT-BR" to "ultra-street-fighter-switch")
        )
        val page = originalPages[titleId.uppercase(Locale.ROOT)] ?: return emptyList()
        return listOf(S25ModOffer(
            title = page.first,
            source = "JumpmanClub Brasil · página oficial do autor",
            sourceUrl = "https://www.jumpmanclubbrasil.com.br/post/" + page.second,
            downloadUrl = null,
            description = "Página do tradutor com instruções de instalação e alternativas " +
                "para emuladores. Abra a página e baixe o patch do autor; " +
                "depois use 'Instalar ZIP baixado'. Compatibilidade de versão não confirmada.",
            translation = true
        ))
    }

    /**
     * Only original author repositories with known Switch Title IDs.
     * Resolve actual GitHub Releases assets at request time (not dead ZIP links
     * copied from the unsupported 2023 CNX index).
     */
    fun githubReleaseTranslations(titleId: String): List<S25ModOffer> {
        data class AuthorRepo(val repo: String, val label: String, val compatibility: String)
        val authorRepos = mapOf(
            "01001B700BA7C000" to AuthorRepo(
                "JUNIORGBJ/The_Escapists_Complete_Edition_PT-BR",
                "The Escapists: Complete Edition · Tradução PT-BR",
                "Port para Nintendo Switch de Traduções GBJ; confira a versão do jogo."),
            "010031200E044000" to AuthorRepo(
                "JUNIORGBJ/Two_Point_Hospital_PT-BR",
                "Two Point Hospital · Tradução PT-BR",
                "Traduções GBJ: exige atualização 1.0.3 (v196608)."),
            "0100582010AE0000" to AuthorRepo(
                "JUNIORGBJ/Crysis_2_Remastered_PT-BR",
                "Crysis 2 Remastered · Tradução PT-BR",
                "Port para Nintendo Switch por Traduções GBJ.")
        )
        val author = authorRepos[titleId.uppercase(Locale.ROOT)] ?: return emptyList()
        val url = "https://api.github.com/repos/" + author.repo + "/releases?per_page=8"
        val releases = JSONArray(readText(url, MAX_CATALOG_BYTES))
        val found = mutableListOf<S25ModOffer>()
        for (i in 0 until releases.length()) {
            val release = releases.optJSONObject(i) ?: continue
            if (release.optBoolean("draft", false) || release.optBoolean("prerelease", false)) continue
            val files = release.optJSONArray("assets") ?: continue
            for (j in 0 until files.length()) {
                val asset = files.optJSONObject(j) ?: continue
                val name = asset.optString("name")
                val download = asset.optString("browser_download_url")
                val size = asset.optLong("size")
                if (!name.endsWith(".zip", true) || size <= 0 ||
                    size > S25ArchiveLimits.MAX_ARCHIVE_BYTES ||
                    !download.startsWith("https://github.com/" + author.repo + "/releases/download/") ||
                    !isApprovedUrl(download)) continue
                val page = release.optString("html_url")
                if (!isApprovedUrl(page)) continue
                found.add(S25ModOffer(
                    title = author.label + " · " + release.optString("tag_name"),
                    source = "GitHub Releases · Traduções GBJ",
                    sourceUrl = page,
                    downloadUrl = download,
                    description = author.compatibility +
                        " Arquivo do autor publicado no GitHub (" + (size / 1024) +
                        " KiB). A existência do ZIP não garante compatibilidade no Eden.",
                    translation = true
                ))
            }
            // Most recent compatible ZIP release only; avoid stale alternatives.
            if (found.isNotEmpty()) break
        }
        return found.take(1)
    }

    /**
     * Discovery-only lookup in the archived CNX index, *not* a download server.
     * The entries may point to deleted repos, outdated builds, wrong regions or
     * unlicensed redistributed files. Never follow them or offer auto-install.
     * Match the complete game's exact Title ID to prevent wrong-game installs.
     */
    fun cnxArchiveReferences(titleId: String): List<S25ModOffer> {
        val expected = titleId.uppercase(Locale.ROOT)
        if (!Regex("^[0-9A-F]{16}$").matches(expected)) return emptyList()
        val root = JSONObject(readText(CNX_ARCHIVE_INDEX, MAX_CATALOG_BYTES))
        val translations = root.optJSONObject("translations") ?: return emptyList()
        val offers = mutableListOf<S25ModOffer>()
        val idPattern = Regex("(?i)(?<![a-f0-9])([a-f0-9]{16})(?![a-f0-9])")
        val keys = translations.keys()
        var scanned = 0
        while (keys.hasNext() && scanned++ < 500) {
            val name = keys.next()
            val entry = translations.optJSONObject(name) ?: continue
            if (!entry.optBoolean("enabled", false)) continue
            val installedPath = entry.optJSONObject("installed")?.optString("hash_file").orEmpty()
            val archiveLink = entry.optString("link")
            // Some old archives have ZIP names unrelated to Title IDs. The
            // installed path is an additional exact-ID reference, not a URL.
            val indexedId = idPattern.find(installedPath)?.groupValues?.get(1)
                ?: idPattern.find(archiveLink)?.groupValues?.get(1)
                ?: continue
            if (!indexedId.equals(expected, ignoreCase = true)) continue
            offers.add(S25ModOffer(
                title = name.take(110) + " · referência CNX antiga",
                source = "CNX histórico · catálogo 2022 (somente pesquisa)",
                sourceUrl = CNX_ARCHIVE_PAGE,
                downloadUrl = null,
                description = "Tradução listada num índice antigo do CNX. " +
                    "O download original NÃO foi validado e pode estar removido. " +
                    "Use a busca dos autores ou a importação manual de ZIP/RAR autorizado.",
                translation = true
            ))
        }
        return offers.take(3)
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
                    error(when (status) {
                        404, 410 -> "Arquivo removido da origem (HTTP $status). " +
                            "Abra a página do autor e procure outra versão."
                        403, 429 -> "Servidor bloqueou ou limitou a solicitação (HTTP $status). " +
                            "Tente novamente mais tarde."
                        else -> "Servidor retornou HTTP $status."
                    })
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
