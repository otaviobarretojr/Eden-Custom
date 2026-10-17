// SPDX-FileCopyrightText: Copyright 2026 Eden S25 Ultra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.activities

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.os.StatFs
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import android.provider.Settings
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.frostwire.jlibtorrent.TorrentInfo
import com.google.android.material.button.MaterialButton
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import java.io.File
import java.util.Locale
import org.yuzu.yuzu_emu.model.GameDir
import org.yuzu.yuzu_emu.utils.NativeConfig

/**
 * A user-supplied .torrent downloader, not a torrent index or game marketplace.
 * Downloads go into public Downloads/EdenS25 so the normal Eden game scanner can use them.
 * No decryption, DRM bypass, or conversion of unknown archives is attempted.
 */
class S25UltraTorrentActivity : AppCompatActivity() {
    private lateinit var details: TextView
    private lateinit var status: TextView
    private lateinit var downloadButton: MaterialButton
    private lateinit var selectButton: MaterialButton
    private var selectedTorrent: File? = null
    private val ui = Handler(Looper.getMainLooper())

    private val selectTorrent = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) readTorrent(uri)
    }
    private val selectGameFolder = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri == null) return@registerForActivityResult
        try {
            contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
            if (NativeConfig.getGameDirs().none { it.uriString == uri.toString() }) {
                NativeConfig.addGameDir(GameDir(uri.toString(), false))
                NativeConfig.saveGlobalConfig()
            }
            notifyUser("Pasta registrada! Volte à biblioteca e atualize a lista de jogos.")
        } catch (ex: Exception) {
            showError("Não foi possível adicionar a pasta: ${ex.message}")
        }
    }
    private val tick = object : Runnable {
        override fun run() {
            refreshStatus()
            ui.postDelayed(this, 1200L)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        title = "Downloads · Eden S25 Ultra"
        val density = resources.displayMetrics.density
        fun dip(px: Int) = (px * density).toInt()
        val column = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dip(20), dip(20), dip(20), dip(28))
        }
        fun text(value: String, sp: Float): TextView =
            TextView(this).apply { text = value; textSize = sp; setPadding(0, 0, 0, dip(14)) }
        fun button(value: String, click: () -> Unit): MaterialButton =
            MaterialButton(this).apply {
                text = value
                setOnClickListener { click() }
                column.addView(this, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
            }

        column.addView(text("Downloads por torrent", 23f))
        column.addView(text("Importe um arquivo .torrent do seu próprio acervo ou de um distribuidor autorizado. O Eden não procura nem fornece jogos.", 14f))
        details = text("Nenhum .torrent selecionado.", 14f)
        column.addView(details)
        selectButton = button("1 · Selecionar arquivo .torrent") {
            selectTorrent.launch(arrayOf("application/x-bittorrent", "application/octet-stream", "*/*"))
        }
        downloadButton = button("2 · Iniciar download") { startTorrent() }
        downloadButton.isEnabled = false
        status = text("Aguardando arquivo.", 14f)
        column.addView(status)
        button("Interromper download") {
            startService(Intent(this, S25TorrentDownloadService::class.java).apply {
                action = S25TorrentDownloadService.ACTION_STOP
            })
        }
        button("3 · Adicionar pasta à biblioteca Eden") {
            MaterialAlertDialogBuilder(this)
                .setTitle("Adicionar jogos baixados")
                .setMessage("Na janela seguinte, selecione Download > EdenS25. O Eden lerá os jogos NSP/XCI compatíveis diretamente da pasta. Não use 'instalar APK'.")
                .setPositiveButton("Selecionar pasta") { _, _ -> selectGameFolder.launch(null) }
                .setNegativeButton("Cancelar", null)
                .show()
        }
        button("Abrir configurações de acesso aos arquivos") { requestFileAccess() }
        column.addView(text("Formatos de jogos reconhecidos: .nsp e .xci. O formato .nro é homebrew, sujeito a compatibilidade. Arquivos .zip/.7z/.rar/.nsz/.xcz não são convertidos automaticamente; extraia ou converta somente com ferramentas apropriadas e autorização.", 13f))
        setContentView(ScrollView(this).apply { addView(column) })
    }

    override fun onResume() {
        super.onResume()
        ui.post(tick)
    }

    override fun onPause() {
        ui.removeCallbacks(tick)
        super.onPause()
    }

    private fun readTorrent(uri: Uri) {
        val name = contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { cursor -> if (cursor.moveToFirst()) cursor.getString(0) else null }
            ?: uri.lastPathSegment.orEmpty()
        if (!name.lowercase(Locale.ROOT).endsWith(".torrent")) {
            showError("Selecione um arquivo com extensão .torrent. Não renomeie ZIP/HTML para .torrent.")
            return
        }
        val prefs = getSharedPreferences(S25TorrentDownloadService.PREFS, MODE_PRIVATE)
        if (prefs.getString("state", "") == "running") {
            showError("Interrompa o download atual antes de selecionar outro torrent.")
            return
        }
        downloadButton.isEnabled = false
        details.text = "Inspecionando metadados…"
        Thread {
            try {
                val target = File(filesDir, "s25torrent/selected.torrent")
                target.parentFile?.mkdirs()
                var count = 0L
                contentResolver.openInputStream(uri)?.use { input ->
                    target.outputStream().use { output ->
                        val buf = ByteArray(32 * 1024)
                        while (true) {
                            val read = input.read(buf)
                            if (read < 0) break
                            count += read
                            if (count > 5L * 1024L * 1024L) error("Metadados .torrent maiores que 5 MiB.")
                            output.write(buf, 0, read)
                        }
                    }
                } ?: error("Não foi possível abrir o arquivo.")
                if (count == 0L) error("Arquivo vazio.")
                val info = TorrentInfo(target)
                val storage = info.files()
                if (storage.numFiles() < 1 || storage.numFiles() > 20000) error("Quantidade de arquivos inválida.")
                for (i in 0 until storage.numFiles()) {
                    val entry = storage.filePath(i).replace('\\', '/')
                    if (entry.startsWith("/") || entry.split('/').any { it == ".." || it == "." }) {
                        error("Torrent contém caminho de arquivo inseguro.")
                    }
                }
                val gameCount = (0 until storage.numFiles()).count {
                    storage.filePath(it).lowercase(Locale.ROOT).let { p ->
                        p.endsWith(".nsp") || p.endsWith(".xci") || p.endsWith(".nro")
                    }
                }
                runOnUiThread {
                    selectedTorrent = target
                    downloadButton.isEnabled = true
                    details.text = "${info.name()}\n${storage.numFiles()} arquivo(s) · ${formatBytes(info.totalSize())}\n${gameCount} arquivo(s) com extensão de jogo/homebrew reconhecível."
                }
            } catch (ex: Throwable) {
                runOnUiThread {
                    selectedTorrent = null
                    downloadButton.isEnabled = false
                    showError("Torrent inválido ou não suportado: ${ex.message}")
                }
            }
        }.start()
    }

    private fun startTorrent() {
        val torrent = selectedTorrent ?: return
        if (!Environment.isExternalStorageManager()) {
            MaterialAlertDialogBuilder(this)
                .setTitle("Permissão de armazenamento")
                .setMessage("Para baixar arquivos grandes diretamente em Download/EdenS25, precisamos de acesso aos arquivos. O Android permite conceder ou negar essa permissão nas configurações.")
                .setPositiveButton("Configurar") { _, _ -> requestFileAccess() }
                .setNegativeButton("Cancelar", null)
                .show()
            return
        }
        val destination = File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS), "EdenS25")
        val free = StatFs(destination.parentFile!!.absolutePath).availableBytes
        val totalSize = runCatching { TorrentInfo(torrent).totalSize() }.getOrDefault(-1L)
        if (totalSize <= 0L || free < totalSize + 200L * 1024L * 1024L) {
            showError("Espaço insuficiente. Necessário: ${formatBytes(totalSize)} e 200 MiB de reserva.")
            return
        }
        MaterialAlertDialogBuilder(this)
            .setTitle("Confirmar download BitTorrent")
            .setMessage("Arquivos .torrent podem compartilhar dados com outros participantes durante o download. Iniciaremos somente após sua confirmação e encerraremos a sessão ao concluir. Confirme que possui autorização para obter e compartilhar este conteúdo. Destino: Download/EdenS25.")
            .setPositiveButton("Tenho autorização · Baixar") { _, _ ->
                val intent = Intent(this, S25TorrentDownloadService::class.java).apply {
                    action = S25TorrentDownloadService.ACTION_START
                    putExtra(S25TorrentDownloadService.EXTRA_TORRENT, torrent.absolutePath)
                }
                ContextCompat.startForegroundService(this, intent)
                notifyUser("Download solicitado; acompanhe pelo progresso.")
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    private fun requestFileAccess() {
        val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION).apply {
            data = Uri.parse("package:$packageName")
        }
        runCatching { startActivity(intent) }.onFailure {
            showError("Abra Configurações > Apps > Eden S25 Ultra > Acesso a todos os arquivos.")
        }
    }

    private fun refreshStatus() {
        val prefs = getSharedPreferences(S25TorrentDownloadService.PREFS, MODE_PRIVATE)
        val state = prefs.getString("state", "idle")
        val name = prefs.getString("name", "").orEmpty()
        val progress = prefs.getInt("progress", 0)
        val message = prefs.getString("message", "").orEmpty()
        status.text = when (state) {
            "running" -> "Baixando: $name\n$progress% · $message"
            "completed" -> "Download concluído: $name\nVerifique Download/EdenS25 e adicione essa pasta ao Eden."
            "stopped" -> "Download interrompido. $message"
            "failed" -> "Falha no download: $message"
            else -> "Nenhum download em execução."
        }
        selectButton.isEnabled = state != "running"
        downloadButton.isEnabled = selectedTorrent != null && state != "running"
    }

    private fun formatBytes(bytes: Long): String = if (bytes < 0) "desconhecido" else
        String.format(Locale.ROOT, "%.2f GiB", bytes.toDouble() / (1024.0 * 1024 * 1024))

    private fun notifyUser(text: String) = Toast.makeText(this, text, Toast.LENGTH_LONG).show()
    private fun showError(text: String) = MaterialAlertDialogBuilder(this)
        .setTitle("Eden S25 Ultra").setMessage(text).setPositiveButton("OK", null).show()
}
