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
import org.yuzu.yuzu_emu.utils.DirectoryInitialization
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
    private var gameLibraryVersion = ""
    private var activeUpdateDescription = ""
    private var busy = false

    private val chooseTranslationArchive =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null && !busy) confirmLocalArchive(uri)
        }


    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        gameId = intent.getStringExtra(EXTRA_TITLE_ID).orEmpty().uppercase(Locale.ROOT)
        gameTitle = intent.getStringExtra(EXTRA_GAME_TITLE).orEmpty()
        gameLibraryVersion = intent.getStringExtra(EXTRA_GAME_VERSION).orEmpty()
        activeUpdateDescription = intent.getStringExtra(EXTRA_ACTIVE_UPDATE).orEmpty()
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
            "Versão na biblioteca: " + gameLibraryVersion.ifBlank { "não informada" } +
                " (pode diferir da atualização efetivamente ativa).", 12f))
        body.addView(caption(
            "Atualização habilitada (gerenciador): " +
                activeUpdateDescription.ifBlank { "não identificada" } +
                ". Build ID do executável: não confirmado.", 12f))
        body.addView(caption(
            "Downloads de patches da comunidade. Não inclui jogos, DLCs, firmware ou chaves. " +
                "Sempre confira a versão do jogo e a autoria do mod.", 14f))
        action("Pesquisar mods") { load(false) }
        action("Pesquisar traduções PT-BR") { load(true) }
        action("Instalar tradução baixada (ZIP/RAR)") { chooseTranslationArchive.launch(arrayOf("*/*")) }
        action("Gerenciar mods instalados") { showInstalled() }
        action("Diagnosticar mods e Build IDs") { diagnoseMods() }
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
                renderOffers(offers.distinctBy { it.sourceUrl }, warnings, translations)
            }
        }
    }

    private fun renderOffers(offers: List<S25ModOffer>, warnings: List<String>, translations: Boolean) {
        clearResults()
        status.text = "Resultados para $gameTitle: ${offers.size}"
        if (translations) label(
            "Fontes PT-BR: publicações dos tradutores e GitHub Releases. " +
                "O catálogo CNX antigo, com links removidos, não é mais usado para download. " +
                "A versão exigida por cada patch deve ser conferida antes da instalação.")
        if (offers.isEmpty()) label(
            if (translations) "Nenhuma tradução conhecida encontrada para este Title ID nas fontes consultadas. " +
                "Você também pode baixar um patch do autor e usar 'Instalar tradução baixada (ZIP/RAR)'."
            else "Nenhum pacote confirmado para este Title ID nesta busca. " +
                "Isso não significa que não existam mods para o jogo.")
        if (warnings.isNotEmpty()) label("Avisos de rede: " + warnings.joinToString(" | "), 12f)
        offers.take(40).forEach { offer ->
            val compatibility = S25ModCompatibility.assess(gameLibraryVersion, offer.requiredGameVersion)
            val integrity = when {
                offer.expectedSha256 != null -> "SHA-256 do autor disponível; será verificado no download."
                offer.expectedSizeBytes != null -> "Tamanho declarado disponível; SHA-256 não publicado."
                else -> "Sem checksum oficial disponível; não é possível autenticar o arquivo por hash."
            }
            label("• ${offer.title}\n${offer.source}\n${offer.description}\n" +
                compatibility.description + "\n" + integrity)
            if (offer.downloadUrl != null || offer.gameBananaId > 0) {
                action("Baixar e instalar · ${offer.title.take(28)}") { confirmInstall(offer) }
            }
            action(
                if (offer.translation && offer.downloadUrl == null && offer.gameBananaId == 0L)
                    "Abrir página da tradução" else "Ver página do autor"
            ) { openWeb(offer.sourceUrl) }
        }
        action("Pesquisar manualmente no GameBanana") {
            openWeb("https://gamebanana.com/search?_sSearchString=" + Uri.encode(gameTitle))
        }
        if (translations) {
            action("Ver catálogo JumpmanClub Brasil") {
                openWeb("https://www.jumpmanclubbrasil.com.br/category/nintendo-switch")
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
        val assessment = S25ModCompatibility.assess(gameLibraryVersion, offer.requiredGameVersion)
        if (assessment.status == S25ModCompatibility.Status.VERSION_MISMATCH) {
            MaterialAlertDialogBuilder(this).setTitle("Possível incompatibilidade de versão")
                .setMessage(assessment.description + "\n\nNão haverá instalação automática. " +
                    "Confira a atualização realmente ativa e as instruções do autor. " +
                    "Se a biblioteca estiver desatualizada, você poderá instalar o ZIP manualmente.")
                .setPositiveButton("Ver fonte") { _, _ -> openWeb(offer.sourceUrl) }
                .setNegativeButton("Cancelar", null).show()
            return
        }
        MaterialAlertDialogBuilder(this).setTitle("Instalar: ${offer.title}?")
            .setMessage(
                "${offer.source}\n\n${offer.description}\n\n" +
                    assessment.description + "\n\n" +
                    "A versão efetiva do jogo e a licença do patch precisam ser verificadas por você. " +
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
                zip = S25ModInstaller.download(
                    applicationContext, url, ::inform,
                    offer.expectedSizeBytes, offer.expectedSha256
                )
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

    private fun diagnoseMods() {
        if (busy) return
        busy = true
        clearResults()
        status.text = "Verificando arquivos dos mods ativos…"
        executor.execute {
            val result = runCatching {
                val root = DirectoryInitialization.userDirectory
                    ?: error("Pasta de dados do emulador indisponível.")
                S25ModDiagnostics.inspect(java.io.File(root), gameId)
            }
            runOnUiThread {
                if (isFinishing || isDestroyed) return@runOnUiThread
                busy = false
                clearResults()
                result.onSuccess { report ->
                    status.text = "Diagnóstico concluído: ${report.activeMods} mods ativos, " +
                        "${report.disabledMods} desativados, ${report.activeFiles} arquivos."
                    if (report.conflicts.isEmpty()) label("Nenhuma colisão entre caminhos LayeredFS ativos detectada.")
                    report.conflicts.take(12).forEach {
                        label("Conflito: ${it.relativePath} · ${it.installedMod} / ${it.incomingMod}")
                    }
                    report.buildIdReferences.take(15).forEach {
                        label("Build ID indicado por cheat: ${it.buildId} · ${it.modName}")
                    }
                    report.issues.take(15).forEach { label("Aviso: ${it.detail}") }
                    label("Atenção: Build IDs encontrados em nomes de cheats NÃO são uma " +
                        "leitura do executável ativo. A compatibilidade precisa ser validada com o jogo e o patch.", 12f)
                }.onFailure { problem ->
                    status.text = "Falha na auditoria: ${problem.message}"
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
        const val EXTRA_GAME_VERSION = "s25_mod_game_version"
        const val EXTRA_ACTIVE_UPDATE = "s25_mod_active_update"
    }
}
