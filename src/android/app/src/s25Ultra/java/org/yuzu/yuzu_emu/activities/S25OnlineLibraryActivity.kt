// SPDX-FileCopyrightText: Copyright 2026 Eden S25 Ultra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.activities

import android.app.DownloadManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Environment
import android.text.Editable
import android.text.TextWatcher
import android.view.ViewGroup
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.button.MaterialButton
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import java.net.URL
import java.util.Locale
import java.util.concurrent.Executors

/** Public homebrew directory and user-configured HTTPS Tinfoil JSON catalogs. */
class S25OnlineLibraryActivity : AppCompatActivity() {
    private val executor = Executors.newSingleThreadExecutor()
    private val prefs by lazy { getSharedPreferences("s25_online_library_v1", MODE_PRIVATE) }
    private lateinit var page: LinearLayout
    private lateinit var list: LinearLayout
    private lateinit var status: TextView
    private lateinit var address: EditText
    private lateinit var search: EditText
    private val back = ArrayDeque<String>()
    private var current: String? = null
    private var busy = false
    private var items: List<S25OnlineCatalog.Entry> = emptyList()
    private var folders: List<S25OnlineCatalog.Entry> = emptyList()
    private var mode = "homebrew"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        title = "Biblioteca Online · Eden"
        val dp = resources.displayMetrics.density
        val p = (18 * dp).toInt()
        page = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(p,p,p,p)
        }
        label(page, "Biblioteca Online", 24f)
        label(page,
            "Catálogos HTTPS de homebrew autorizado ou servidores próprios/licenciados. " +
                "Não inclui ROMs comerciais. Pacotes .nro do Switch normalmente não rodam no Eden Android.", 13f)
        button(page, "Explorar Homebrew App Store") { homebrew() }
        label(page, "Catálogo JSON do Tinfoil (HTTPS):", 15f)
        address = EditText(this).apply {
            hint = "https://exemplo.com/index.json"
            setSingleLine(true)
            inputType = android.text.InputType.TYPE_CLASS_TEXT or
                android.text.InputType.TYPE_TEXT_VARIATION_URI
            setText(prefs.getString("catalog_url", ""))
        }
        page.addView(address)
        button(page, "Conectar catálogo HTTPS") {
            val target = address.text.toString().trim()
            if (!S25OnlineCatalog.isValidHttps(target)) {
                dialog("URL inválida", "Use HTTPS público sem senha embutida. " +
                    "FTP, HTTP e lojas com catálogo criptografado ainda não são suportados.")
            } else {
                back.clear()
                openCatalog(target, false)
            }
        }
        button(page, "Voltar pasta") {
            if (back.isNotEmpty()) openCatalog(back.removeLast(), false)
            else status.text = "Não há pasta anterior."
        }
        button(page, "Downloads do Android") {
            runCatching { startActivity(Intent(DownloadManager.ACTION_VIEW_DOWNLOADS)) }
                .onFailure { dialog("Meus Arquivos", "Veja Downloads/EdenS25Ultra no celular.") }
        }
        status = TextView(this).apply {
            text = "Escolha um catálogo acima."
            textSize = 14f
            setPadding(0,p,0,p)
        }
        page.addView(status)
        search = EditText(this).apply {
            hint = "Pesquisar por nome ou formato"
            setSingleLine(true)
            addTextChangedListener(object: TextWatcher {
                override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
                override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) { render() }
                override fun afterTextChanged(s: Editable?) {}
            })
        }
        page.addView(search)
        list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        page.addView(list)
        setContentView(ScrollView(this).apply { addView(page) })
    }

    override fun onDestroy() { executor.shutdownNow(); super.onDestroy() }

    private fun label(root: LinearLayout, value: String, size: Float = 14f) {
        root.addView(TextView(this).apply {
            text = value
            textSize = size
            val spacing = (8 * resources.displayMetrics.density).toInt()
            setPadding(0,spacing,0,spacing)
        })
    }
    private fun button(root: LinearLayout, name: String, onClick: () -> Unit) {
        root.addView(MaterialButton(this).apply {
            text = name
            setOnClickListener { if (!busy) onClick() }
        }, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT
        ))
    }
    private fun homebrew() {
        if (busy) return
        busy = true
        mode = "homebrew"
        back.clear()
        current = null
        list.removeAllViews()
        status.text = "Consultando a Homebrew App Store…"
        executor.execute {
            val result = runCatching { S25OnlineCatalog.homebrews() }
            runOnUiThread {
                if (isFinishing || isDestroyed) return@runOnUiThread
                busy = false
                result.onSuccess { applyResult(it) }
                    .onFailure { status.text = "Fonte indisponível: ${it.message}" }
            }
        }
    }
    private fun openCatalog(url: String, push: Boolean) {
        if (busy) return
        busy = true
        mode = "tinfoil"
        val previous = current
        status.text = "Consultando catálogo HTTPS…"
        list.removeAllViews()
        executor.execute {
            val result = runCatching { S25OnlineCatalog.tinfoil(url) }
            runOnUiThread {
                if (isFinishing || isDestroyed) return@runOnUiThread
                busy = false
                result.onSuccess {
                    if (push && previous != null) back.addLast(previous)
                    current = url
                    if (!push && back.isEmpty()) {
                        prefs.edit().putString("catalog_url", url).apply()
                        address.setText(url)
                    }
                    applyResult(it)
                }.onFailure {
                    status.text = "Servidor indisponível: ${it.message}"
                }
            }
        }
    }
    private fun applyResult(result: S25OnlineCatalog.Result) {
        items = result.items
        folders = result.directories
        status.text = "Arquivos: ${items.size} | Pastas: ${folders.size}.\n" + result.message
        render()
    }
    private fun render() {
        if (!::list.isInitialized) return
        list.removeAllViews()
        val q = if (::search.isInitialized) search.text.toString().trim() else ""
        if (mode == "tinfoil") folders.filter { q.isBlank() ||
            it.title.contains(q, ignoreCase = true) }.take(30).forEach { folder ->
            label(list, "📁 ${folder.title}")
            button(list, "Abrir pasta") { openCatalog(folder.url, true) }
        }
        val matches = items.filter { q.isBlank() ||
            it.title.contains(q, ignoreCase = true) ||
            it.kind.contains(q, ignoreCase = true) }
        label(list, "Exibindo ${minOf(matches.size, 65)} de ${matches.size} resultado(s). " +
            if (matches.size > 65) "Use a busca para filtrar." else "")
        matches.take(65).forEach { entry ->
            val size = if (entry.size > 0) " · ${entry.size / (1024 * 1024)} MiB (informado)" else ""
            label(list, "• ${entry.title}\n${entry.kind}$size\n${entry.description.take(230)}")
            if (S25OnlineCatalog.downloadable(entry)) {
                button(list, "Baixar · ${entry.title.take(40)}") { confirm(entry) }
            }
        }
    }
    private fun confirm(entry: S25OnlineCatalog.Entry) {
        val caution = if (entry.kind == "HOMEBREW") {
            "Pacote feito para Switch desbloqueado. Arquivos .nro não executam diretamente no Eden Android."
        } else {
            "Confirme a origem e a autorização para baixar este conteúdo."
        }
        MaterialAlertDialogBuilder(this).setTitle("Baixar ${entry.title}?")
            .setMessage("$caution\n\nSerá salvo em Downloads/EdenS25Ultra pelo " +
                "gerenciador do Android, sem alterar seus jogos ou saves.")
            .setPositiveButton("Baixar") { _, _ -> download(entry) }
            .setNegativeButton("Cancelar", null).show()
    }
    private fun download(entry: S25OnlineCatalog.Entry) {
        if (!S25OnlineCatalog.downloadable(entry)) return
        val suffix = if (entry.kind == "HOMEBREW") "zip"
            else entry.kind.lowercase(Locale.ROOT)
        val basename = if (entry.kind == "HOMEBREW") {
            URL(entry.url).path.substringAfterLast('/').substringBeforeLast('.')
        } else entry.title.substringBeforeLast('.', entry.title)
        val safe = basename.replace(Regex("[^\\p{L}\\p{N} ._()\\[\\]-]"), "_")
            .trim().trim('.').take(65).ifBlank { "arquivo" }
        val filename = "${System.currentTimeMillis()}_$safe.$suffix"
        try {
            val request = DownloadManager.Request(Uri.parse(entry.url)).apply {
                setTitle(entry.title.take(75))
                setDescription("Eden S25 Ultra · Biblioteca Online")
                setMimeType("application/octet-stream")
                setAllowedOverMetered(true)
                setAllowedOverRoaming(false)
                setNotificationVisibility(
                    DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED)
                setDestinationInExternalPublicDir(
                    Environment.DIRECTORY_DOWNLOADS, "EdenS25Ultra/$filename")
            }
            (getSystemService(Context.DOWNLOAD_SERVICE) as DownloadManager).enqueue(request)
            status.text = "Download iniciado. Abra Downloads do Android para acompanhar."
            dialog("Download iniciado", "Confira Downloads/EdenS25Ultra. " +
                "Para jogos compatíveis, mova arquivos NSP/NSZ/XCI/XCZ " +
                "para uma pasta de jogos configurada no Eden.")
        } catch (e: Exception) {
            dialog("Falha no download", e.message ?: "Não foi possível iniciar a transferência.")
        }
    }
    private fun dialog(title: String, message: String) {
        MaterialAlertDialogBuilder(this).setTitle(title).setMessage(message)
            .setPositiveButton("OK", null).show()
    }
}
