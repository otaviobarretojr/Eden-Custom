// SPDX-FileCopyrightText: Copyright 2026 Eden S25 Ultra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.activities

import android.app.Activity
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
    private val launchedFromInstall get() =
        intent.getBooleanExtra(EXTRA_OPEN_PICKER, false)

    private val chooseLocalArchive =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null && !busy) {
                confirmLocalArchive(uri)
            } else if (uri == null && launchedFromInstall && !busy) {
                finish()
            }
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
        title = "Importar mod ou tradução"
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
            "Escolha um mod ou tradução ZIP/RAR salvo no aparelho. " +
                "Nenhum servidor ou catálogo é consultado. " +
                "Confira a versão do jogo antes de instalar.", 14f))
        action("Importar arquivo ZIP ou RAR") { chooseLocalArchive.launch(arrayOf("*/*")) }
        status = caption("Nenhum arquivo selecionado.", 14f)
        body.addView(status)
        setContentView(ScrollView(this).apply { addView(body) })
        if (savedInstanceState == null && launchedFromInstall) {
            // Reuse the native content-type dialog. Go directly to Android's
            // document picker; do not require another import-button tap.
            body.post {
                if (!isFinishing && !isDestroyed && !busy) {
                    chooseLocalArchive.launch(arrayOf("*/*"))
                }
            }
        }
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

    private fun inform(value: String) = runOnUiThread {
        if (!isFinishing && !isDestroyed) status.text = value
    }

    private fun confirmLocalArchive(uri: Uri) {
        val displayName = runCatching {
            contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
                ?.use { if (it.moveToFirst()) it.getString(0) else null }
        }.getOrNull().orEmpty()
        val isRar = displayName.endsWith(".rar", true)
        if (!isRar && !displayName.endsWith(".zip", true)) {
            MaterialAlertDialogBuilder(this).setTitle("Formato não suportado")
                .setMessage("Selecione um mod ou tradução ZIP/RAR. 7z, EXE e ROMs não são aceitos.")
                .setPositiveButton("OK", null).show()
            return
        }
        MaterialAlertDialogBuilder(this).setTitle("Importar mod ou tradução?")
            .setMessage("Arquivo: $displayName\n\nO conteúdo será validado para o Title ID " +
                "$gameId. O ASTRYX não substituirá mods instalados. " +
                "Feche o jogo e confirme que o patch corresponde à versão correta.")
            .setPositiveButton("Validar e instalar") { _, _ ->
                val importName = displayName.substringBeforeLast('.').ifBlank { "Mod ou tradução" }
                installLocalArchive(uri, isRar, importName)
            }
            .setNegativeButton("Cancelar", null).show()
    }

    private fun installLocalArchive(uri: Uri, isRar: Boolean, importName: String) {
        if (busy) return
        busy = true
        inform(if (isRar) "Preparando RAR selecionado…" else "Preparando ZIP selecionado…")
        executor.execute {
            var temp: java.io.File? = null
            var repacked: java.io.File? = null
            val result = runCatching {
                val file = java.io.File.createTempFile(
                    "astryx-archive-", if (isRar) ".rar" else ".zip", cacheDir
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
                    importName, ::inform
                )
            }
            temp?.delete()
            repacked?.delete()
            runOnUiThread {
                if (isFinishing || isDestroyed) return@runOnUiThread
                busy = false
                result.onSuccess { names ->
                    status.text = "Arquivo importado: ${names.joinToString(", ")}. Reinicie o jogo."
                    MaterialAlertDialogBuilder(this).setTitle("Mod ou tradução instalado")
                        .setMessage("Pasta(s): ${names.joinToString(", ")}. " +
                            "O efeito do mod ou tradução depende da versão do jogo.")
                        .setPositiveButton("OK") { _, _ ->
                            if (launchedFromInstall) {
                                setResult(Activity.RESULT_OK)
                                finish()
                            }
                        }.show()
                }.onFailure { problem ->
                    status.text = "Falha ao importar: ${problem.message}"
                    MaterialAlertDialogBuilder(this).setTitle("Arquivo não instalado")
                        .setMessage(problem.message ?: "Falha ao validar o arquivo.")
                        .setPositiveButton("OK", null).show()
                }
            }
        }
    }

    companion object {
        const val EXTRA_TITLE_ID = "s25_mod_title_id"
        const val EXTRA_GAME_TITLE = "s25_mod_game_title"
        const val EXTRA_OPEN_PICKER = "s25_mod_open_picker"
    }
}
