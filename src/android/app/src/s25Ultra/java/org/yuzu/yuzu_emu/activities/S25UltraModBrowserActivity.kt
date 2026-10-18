// SPDX-FileCopyrightText: Copyright 2026 Eden S25 Ultra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.activities

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import androidx.activity.result.contract.ActivityResultContracts
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.button.MaterialButton
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import java.util.Locale
import java.util.concurrent.Executors

/**
 * Opt-in per-game ZIP/RAR mod manager. Only uses existing native load/<TitleID>
 * folders and does not install game updates or firmware.
 */
class S25UltraModBrowserActivity : AppCompatActivity() {
    private val executor = Executors.newSingleThreadExecutor()
    private lateinit var body: LinearLayout
    private lateinit var status: TextView
    private lateinit var gameId: String
    private lateinit var gameTitle: String
    private var busy = false

    private val chooseTranslationArchive =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null && !busy) confirmLocalArchive(uri)
        }


    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        gameId = intent.getStringExtra(EXTRA_TITLE_ID).orEmpty().uppercase(Locale.ROOT)
        gameTitle = intent.getStringExtra(EXTRA_GAME_TITLE).orEmpty()
        if (!Regex("^[A-F0-9]{16}$").matches(gameId)) {
            MaterialAlertDialogBuilder(this).setTitle("Mods e traduções")
                .setMessage("O jogo não possui um Title ID válido. Abra um jogo instalado e tente novamente.")
                .setPositiveButton("Fechar") { _, _ -> finish() }.setCancelable(false).show()
            return
        }
        title = "Mods e traduções"
        val dp = resources.displayMetrics.density
        fun px(v: Int) = (v * dp).toInt()
        body = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(px(18), px(18), px(18), px(32))
        }
        fun caption(text: String, size: Float) = TextView(this).apply {
            this.text = text
            textSize = size
            setPadding(0, px(3), 0, px(10))
        }
        body.addView(caption(gameTitle, 22f))
        body.addView(caption("Title ID: $gameId", 13f))
        body.addView(caption(
            "Downloads de patches da comunidade. Não inclui jogos, DLCs, firmware ou chaves. " +
                "Sempre confira a versão do jogo e a autoria do mod.", 14f))
        action("Pesquisar mods") { load(false) }
        action("Pesquisar traduções PT-BR") { load(true) }
        action("Instalar tradução baixada (ZIP/RAR)") { chooseTranslationArchive.launch(arrayOf("*/*")) }
        action("Gerenciar mods instalados") { showInstalled() }
        status = caption("Escolha uma categoria para pesquisar.", 14f)
        body.addView(status)
        setContentView(ScrollView(this).apply { addView(body) })
        load(false)
    }

    override fun onDestroy() {
        executor.shutdownNow()
        super.onDestroy()
    }

    private fun action(label: String, handler: () -> Unit) {
        val button = MaterialButton(this).apply {
            text = label
            setOnClickListener { if (!busy) handler() }
        }
        body.addView(button, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
        ))
    }

    private fun label(s: String, size: Float = 14f) {
        val t = TextView(this).apply {
            text = s; textSize = size
            setPadding(0, (8 * resources.displayMetrics.density).toInt(), 0,
                (8 * resources.displayMetrics.density).toInt())
        }
        body.addView(t)
    }

    private fun clearResults() {
        val starting = body.indexOfChild(status)
        if (starting >= 0 && body.childCount > starting + 1) {
            body.removeViews(starting + 1, body.childCount - starting - 1)
        }
    }

    private fun inform(value: String) = runOnUiThread {
        if (!isFinishing && !isDestroyed) status.text = value
    }

    private fun load(translations: Boolean) {
        if (busy) return
        clearResults()
        busy = true
        status.text = if (translations) "Buscando traduções por Title ID…" else "Buscando mods por Title ID…"
        executor.execute {
            val offers = mutableListOf<S25ModOffer>()
            val warnings = mutableListOf<String>()
            if (translations) {
                offers.addAll(S25ModCatalog.authorTranslationPages(gameId))
                runCatching { S25ModCatalog.githubReleaseTranslations(gameId) }
                    .onSuccess { offers.addAll(0, it) }
                    .onFailure { warnings.add("GitHub Releases: ${it.message}") }
                // The CNX 2022 catalog is available on GitHub/main but its
                // archived download endpoints have been removed. Discover
                // exact-ID historical references without pretending to install.
                runCatching { S25ModCatalog.cnxArchiveReferences(gameId) }
                    .onSuccess { offers.addAll(it) }
                    .onFailure { warnings.add("CNX histórico: ${it.message}") }
            } else {
                runCatching { S25ModCatalog.githubPacks(gameId) }
                    .onSuccess { offers.addAll(it) }
                    .onFailure { warnings.add("GitHub: ${it.message}") }
            }
            runCatching { S25ModCatalog.gameBanana(gameTitle, translations) }
                .onSuccess { offers.addAll(it) }
                .onFailure { warnings.add("GameBanana: ${it.message}") }
            runOnUiThread {
                if (isFinishing || isDestroyed) return@runOnUiThread
                busy = false
                // All CNX historical records share one catalog URL; preserve distinct
                // entries for the same Title ID instead of dropping all but the first.
                val uniqueOffers = offers.distinctBy { offer ->
                    if (offer.source.startsWith("CNX histórico"))
                        "${offer.sourceUrl}|${offer.title}"
                    else offer.sourceUrl
                }
                renderOffers(uniqueOffers, warnings, translations)
            }
        }
    }

    private fun renderOffers(offers: List<S25ModOffer>, warnings: List<String>, translations: Boolean) {
        clearResults()
        val archiveCount = offers.count { it.source.startsWith("CNX histórico") }
        val currentListings = offers.size - archiveCount
        val directDownloads = offers.count { it.downloadUrl != null }
        status.text = if (translations) {
            "Traduções para $gameTitle: $currentListings publicação(ões) consultável(is)" +
                " · $directDownloads link(s) de arquivo listado(s)" +
                (if (archiveCount > 0) " · $archiveCount registro(s) histórico(s)" else "")
        } else {
            "Resultados para $gameTitle: ${offers.size}"
        }
        if (translations) label(
            "Fontes: JumpmanClub Brasil (site ativo), GitHub Releases de autores " +
                "e GameBanana. Os registros CNX são apenas referências históricas: " +
                "os arquivos antigos podem ter sido excluídos. " +
                "Só baixe patches com versão do jogo e autoria conferidas.")
        if (offers.isEmpty()) label(
            if (translations) "Nenhuma tradução verificada encontrada para este Title ID. " +
                "A ausência de resultados não significa que a tradução não exista. " +
                "Use as páginas dos tradutores abaixo ou importe um ZIP/RAR autorizado."
            else "Nenhum pacote confirmado para este Title ID nesta busca. " +
                "Isso não significa que não existam mods para o jogo.")
        if (warnings.isNotEmpty()) label("Avisos de rede: " + warnings.joinToString(" | "), 12f)
        offers.take(40).forEach { offer ->
            label("• ${offer.title}\n${offer.source}\n${offer.description}")
            if (offer.downloadUrl != null || offer.gameBananaId > 0) {
                action("Baixar e instalar · ${offer.title.take(28)}") { confirmInstall(offer) }
            }
            action(
                when {
                    offer.source.startsWith("CNX histórico") -> "Ver referência histórica CNX"
                    offer.translation && offer.downloadUrl == null &&
                        offer.gameBananaId == 0L -> "Abrir página da tradução"
                    else -> "Ver página do autor"
                }
            ) { openWeb(offer.sourceUrl) }
        }
        action("Pesquisar manualmente no GameBanana") {
            openWeb("https://gamebanana.com/search?_sSearchString=" + Uri.encode(gameTitle))
        }
        if (translations) {
            action("Pesquisar traduções PT-BR no GitHub") {
                openWeb("https://github.com/search?q=" +
                    Uri.encode(gameTitle + " tradução PT-BR Switch") + "&type=repositories")
            }
        }
        if (translations) {
            action("Ver catálogo JumpmanClub Brasil") {
                openWeb("https://www.jumpmanclubbrasil.com.br/category/nintendo-switch")
            }
            action("Ver índice histórico CNX (downloads não verificados)") {
                openWeb("https://github.com/gamemoddesignbr/nx-links/blob/main/nx-links-v220.json")
            }
            action("Ver projetos de tradução PT-BR") { openWeb("https://patcheados.org/") }
        }
    }

    private fun confirmLocalArchive(uri: Uri) {
        val displayName = runCatching {
            contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
                ?.use { if (it.moveToFirst()) it.getString(0) else null }
        }.getOrNull().orEmpty()
        val isRar = displayName.endsWith(".rar", true)
        if (!isRar && !displayName.endsWith(".zip", true)) {
            MaterialAlertDialogBuilder(this).setTitle("Formato não suportado")
                .setMessage("Selecione um arquivo ZIP ou RAR de tradução. 7z, EXE e ROMs não são aceitos.")
                .setPositiveButton("OK", null).show()
            return
        }
        MaterialAlertDialogBuilder(this).setTitle("Importar tradução PT-BR?")
            .setMessage("Arquivo: $displayName\n\nO conteúdo será validado para o Title ID " +
                "$gameId. O Eden não substituirá mods instalados. " +
                "Feche o jogo e confirme que o patch corresponde à versão correta.")
            .setPositiveButton("Validar e instalar") { _, _ -> installLocalArchive(uri, isRar) }
            .setNegativeButton("Cancelar", null).show()
    }

    private fun installLocalArchive(uri: Uri, isRar: Boolean) {
        if (busy) return
        busy = true
        inform(if (isRar) "Preparando RAR selecionado…" else "Preparando ZIP selecionado…")
        executor.execute {
            var temp: java.io.File? = null
            var repacked: java.io.File? = null
            val result = runCatching {
                val file = java.io.File.createTempFile(
                    "eden-ptbr-", if (isRar) ".rar" else ".zip", cacheDir
                )
                temp = file
                val expectedSize = runCatching {
                    contentResolver.query(uri, arrayOf(OpenableColumns.SIZE), null, null, null)
                        ?.use { cursor ->
                            val index = cursor.getColumnIndex(OpenableColumns.SIZE)
                            if (index >= 0 && cursor.moveToFirst() && !cursor.isNull(index)) {
                                cursor.getLong(index)
                            } else {
                                -1L
                            }
                        }
                }.getOrNull() ?: -1L
                if (expectedSize > S25ArchiveLimits.MAX_ARCHIVE_BYTES) {
                    error(S25ArchiveLimits.archiveSizeError())
                }
                S25ArchiveLimits.ensureAvailableSpace(cacheDir, expectedSize.coerceAtLeast(0L))
                val stream = contentResolver.openInputStream(uri) ?: error("Arquivo não acessível.")
                stream.use { input ->
                    file.outputStream().buffered().use { output ->
                        var total = 0L
                        var lastSpaceCheck = 0L
                        val buffer = ByteArray(32768)
                        while (true) {
                            val read = input.read(buffer)
                            if (read < 0) break
                            total += read
                            if (total > S25ArchiveLimits.MAX_ARCHIVE_BYTES) {
                                error(S25ArchiveLimits.archiveSizeError())
                            }
                            if (S25ArchiveLimits.shouldCheckSpace(total, lastSpaceCheck)) {
                                S25ArchiveLimits.ensureAvailableSpace(cacheDir, read.toLong())
                                lastSpaceCheck = total
                                inform("Importando arquivo: ${total / (1024 * 1024)} MiB…")
                            }
                            output.write(buffer, 0, read)
                        }
                        if (total < 8L) error("Arquivo vazio ou incompleto.")
                    }
                }
                val packageToInstall = if (isRar) {
                    inform("Descompactando RAR 4/5 e verificando os arquivos…")
                    S25RarBridge.convert(applicationContext, file, gameId, ::inform)
                        .also { repacked = it }
                } else {
                    file
                }
                inform("Validando a estrutura do patch e instalando…")
                S25ModInstaller.install(
                    applicationContext, packageToInstall, gameId,
                    "Tradução PT-BR", ::inform
                )
            }
            temp?.delete()
            repacked?.delete()
            runOnUiThread {
                if (isFinishing || isDestroyed) return@runOnUiThread
                busy = false
                result.onSuccess { names ->
                    status.text = "Tradução importada: ${names.joinToString(", ")}. Reinicie o jogo."
                    MaterialAlertDialogBuilder(this).setTitle("Tradução instalada")
                        .setMessage("Pasta(s): ${names.joinToString(", ")}. " +
                            "O idioma só mudará se o patch for compatível com a versão do jogo.")
                        .setPositiveButton("Gerenciar") { _, _ -> showInstalled() }
                        .setNegativeButton("OK", null).show()
                }.onFailure { problem ->
                    status.text = "Falha ao importar: ${problem.message}"
                    MaterialAlertDialogBuilder(this).setTitle("ZIP não instalado")
                        .setMessage(problem.message ?: "Falha ao validar o arquivo.")
                        .setPositiveButton("OK", null).show()
                }
            }
        }
    }

    private fun confirmInstall(offer: S25ModOffer) {
        MaterialAlertDialogBuilder(this).setTitle("Instalar: ${offer.title}?")
            .setMessage(
                "${offer.source}\n\n${offer.description}\n\n" +
                    "A versão do jogo e a licença do patch precisam ser verificadas por você. " +
                    "Arquivos serão validados antes de instalar e mods existentes não serão substituídos. " +
                    "ZIP/RAR de até 2 GiB são aceitos, com validação de conteúdo e espaço livre."
            )
            .setPositiveButton("Baixar e instalar") { _, _ -> install(offer) }
            .setNeutralButton("Fonte") { _, _ -> openWeb(offer.sourceUrl) }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    private fun install(offer: S25ModOffer) {
        if (busy) return
        busy = true
        inform("Preparando download autorizado…")
        executor.execute {
            var zip: java.io.File? = null
            val outcome = runCatching {
                val url = offer.downloadUrl ?: S25ModCatalog.gameBananaArchive(offer.gameBananaId)
                    ?: error("Sem ZIP direto válido. Abra a página do autor para instalar manualmente.")
                inform("Conectando à fonte…")
                zip = S25ModInstaller.download(applicationContext, url, ::inform)
                inform("Validando estrutura do pacote…")
                S25ModInstaller.install(applicationContext, zip!!, gameId, offer.title, ::inform)
            }
            zip?.delete()
            runOnUiThread {
                if (isFinishing || isDestroyed) return@runOnUiThread
                busy = false
                outcome.onSuccess { names ->
                    status.text = "Instalado: ${names.joinToString(", ")}. Reinicie o jogo."
                    MaterialAlertDialogBuilder(this)
                        .setTitle("Mods instalados")
                        .setMessage("Pasta(s): ${names.joinToString(", ")}.\nO Eden detectará os mods ao reiniciar o jogo.")
                        .setPositiveButton("Gerenciar") { _, _ -> showInstalled() }
                        .setNegativeButton("OK", null).show()
                }.onFailure { ex ->
                    status.text = "Falha: ${ex.message}"
                    MaterialAlertDialogBuilder(this).setTitle("Download ou instalação não concluída")
                        .setMessage(ex.message ?: "Erro desconhecido.")
                        .setPositiveButton("OK", null).show()
                }
            }
        }
    }

    private fun showInstalled() {
        if (busy) return
        clearResults()
        val result = runCatching { S25ModInstaller.installed(gameId) }
        result.onFailure { status.text = "Não foi possível ler os mods: ${it.message}" }
        val (active, disabled) = result.getOrNull() ?: return
        status.text = "Ativos: ${active.size} · Desativados: ${disabled.size}"
        active.forEach { name ->
            label("Ativo: $name")
            action("Desativar · $name") { confirmToggle(name, false) }
        }
        disabled.forEach { name ->
            label("Desativado: $name")
            action("Reativar · $name") { confirmToggle(name, true) }
        }
        if (active.isEmpty() && disabled.isEmpty()) label("Nenhum mod do tipo romfs/exefs/cheats encontrado.")
        label("Desativar move apenas a pasta do mod para o armazenamento interno do Eden; " +
            "não modifica os saves nem remove os arquivos do jogo.", 12f)
    }

    private fun confirmToggle(name: String, enable: Boolean) {
        MaterialAlertDialogBuilder(this)
            .setTitle((if (enable) "Reativar" else "Desativar") + " $name?")
            .setMessage("Feche o jogo antes de alterar os mods. Nenhum save será apagado.")
            .setPositiveButton("Confirmar") { _, _ ->
                runCatching { S25ModInstaller.toggle(gameId, name, enable) }
                    .onSuccess { showInstalled() }
                    .onFailure {
                        MaterialAlertDialogBuilder(this).setTitle("Erro no mod")
                            .setMessage(it.message).setPositiveButton("OK", null).show()
                    }
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    private fun openWeb(url: String) {
        if (!url.startsWith("https://", true)) return
        runCatching { startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url))) }
            .onFailure { inform("Não foi possível abrir o navegador.") }
    }

    companion object {
        const val EXTRA_TITLE_ID = "s25_mod_title_id"
        const val EXTRA_GAME_TITLE = "s25_mod_game_title"
    }
}
