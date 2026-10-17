// SPDX-FileCopyrightText: Copyright 2026 Eden S25 Ultra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.activities

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.google.android.material.button.MaterialButton
import java.util.Locale
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.yuzu.yuzu_emu.model.Game
import org.yuzu.yuzu_emu.utils.FileUtil
import org.yuzu.yuzu_emu.utils.NativeConfig

/**
 * Read-only SAF inventory for the S25 Ultra flavor.
 * Reports what the library scanner can see; it does not decrypt or validate games.
 */
class S25LibraryDiagnosticsActivity : AppCompatActivity() {
    private lateinit var reportView: TextView
    private lateinit var scanButton: MaterialButton
    private var report: ScanReport? = null
    private var scanning = false

    private class ScanReport {
        var folders = 0
        var readableFolders = 0
        var inaccessibleFolders = 0
        var scannedEntries = 0
        var visibleCandidates = 0
        var deepCandidates = 0
        var compressed = 0
        var archives = 0
        var limited = false
        var depthLimit = false
        val examples = mutableListOf<String>()

        fun addExample(message: String) {
            if (examples.size < 12) examples.add(message)
        }

        fun safeSummary(): String = buildString {
            appendLine("Eden S25 Ultra - diagnostico da biblioteca")
            appendLine("Pastas configuradas: " + folders)
            appendLine("Pastas acessiveis: " + readableFolders)
            appendLine("Pastas sem acesso: " + inaccessibleFolders)
            appendLine("Entradas examinadas: " + scannedEntries)
            appendLine("Arquivos com extensao aceita no alcance: " + visibleCandidates)
            appendLine("Arquivos aceitos fora do alcance: " + deepCandidates)
            appendLine("NSZ/XCZ para converter: " + compressed)
            appendLine("Arquivos ZIP/RAR/7Z: " + archives)
            appendLine("Limite de entradas atingido: " + limited)
            appendLine("Subpastas alem da profundidade de auditoria: " + depthLimit)
            appendLine("Nao inclui nomes, caminhos, conteudo ou chaves.")
        }

        fun onScreen(): String = buildString {
            appendLine("DIAGNÓSTICO DA BIBLIOTECA")
            appendLine()
            appendLine("Pastas de jogos: " + folders)
            appendLine("Com acesso: " + readableFolders + "  |  Sem acesso: " + inaccessibleFolders)
            appendLine("Entradas verificadas: " + scannedEntries)
            appendLine()
            appendLine("Arquivos de formato aceito no alcance da busca: " + visibleCandidates)
            appendLine("Arquivos aceitos, mas em subpastas profundas: " + deepCandidates)
            appendLine("NSZ/XCZ (exigem conversão): " + compressed)
            appendLine("ZIP/RAR/7Z (não são jogos executáveis diretamente): " + archives)
            appendLine()
            if (folders == 0) appendLine("AÇÃO: adicione uma pasta de jogos nas configurações do Eden.")
            if (inaccessibleFolders > 0) appendLine("AÇÃO: conceda novamente acesso às pastas sem permissão.")
            if (deepCandidates > 0) appendLine(
                "AÇÃO: ative a busca em subpastas ou mova os jogos para mais perto da raiz da pasta."
            )
            if (compressed > 0) appendLine(
                "AÇÃO: use o Conversor NSZ/XCZ do S25 Ultra para gerar NSP/XCI."
            )
            if (archives > 0) appendLine(
                "AÇÃO: extraia somente arquivos legítimos e compatíveis antes de adicioná-los."
            )
            if (visibleCandidates > 0) appendLine(
                "ATENÇÃO: a extensão aceita não garante que o jogo seja válido. Chaves, firmware, integridade e suporte do núcleo ainda podem impedir a leitura."
            )
            if (limited || depthLimit) appendLine(
                "ATENÇÃO: auditoria parcial por limite de profundidade ou quantidade."
            )
            if (examples.isNotEmpty()) {
                appendLine()
                appendLine("EXEMPLOS LOCAIS (não incluídos ao copiar):")
                examples.forEach { appendLine("• " + it) }
            }
            appendLine()
            appendLine("Análise somente de leitura. Nenhum jogo, save, mod, chave ou pasta foi alterado.")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        title = "Biblioteca · Eden S25 Ultra"
        val dp = resources.displayMetrics.density
        fun px(value: Int) = (value * dp).toInt()
        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(px(18), px(18), px(18), px(28))
        }
        fun addButton(label: String, click: () -> Unit): MaterialButton {
            return MaterialButton(this).also { button ->
                button.text = label
                button.setOnClickListener { click() }
                content.addView(
                    button, LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT
                    )
                )
            }
        }
        content.addView(TextView(this).apply {
            text = "Por que meu jogo não aparece?"
            textSize = 22f
            setPadding(0, 0, 0, px(12))
        })
        content.addView(TextView(this).apply {
            text = "Verifica as pastas já configuradas e mostra formatos incompatíveis, permissões e arquivos fora da profundidade de busca. Não valida chaves ou ROMs."
            textSize = 14f
            setPadding(0, 0, 0, px(12))
        })
        scanButton = addButton("Analisar pastas de jogos") { analyze() }
        addButton("Abrir conversor NSZ/XCZ") {
            startActivity(Intent(this, S25UltraConverterActivity::class.java))
        }
        addButton("Copiar resumo anônimo") {
            val value = report?.safeSummary()
            if (value == null) {
                Toast.makeText(this, "Faça a análise primeiro.", Toast.LENGTH_SHORT).show()
            } else {
                val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                clipboard.setPrimaryClip(ClipData.newPlainText("Eden library scan", value))
                Toast.makeText(this, "Resumo copiado sem nomes de arquivos.", Toast.LENGTH_SHORT).show()
            }
        }
        reportView = TextView(this).apply {
            text = "Analisando…"
            textSize = 14f
            setTextIsSelectable(true)
            setPadding(0, px(16), 0, 0)
        }
        content.addView(reportView)
        setContentView(ScrollView(this).apply { addView(content) })
        analyze()
    }

    private fun analyze() {
        if (scanning) return
        scanning = true
        scanButton.isEnabled = false
        reportView.text = "Verificando permissões, formatos e profundidade das pastas…"
        lifecycleScope.launch {
            try {
                val result = withContext(Dispatchers.IO) { scanLibrary() }
                report = result
                reportView.text = result.onScreen()
            } catch (error: Exception) {
                reportView.text = "A auditoria não pôde ser concluída: " +
                    error.javaClass.simpleName +
                    ". Reabra o Eden e confira as permissões das pastas."
            } finally {
                scanning = false
                scanButton.isEnabled = true
            }
        }
    }

    private fun scanLibrary(): ScanReport {
        val result = ScanReport()
        val directories = NativeConfig.getGameDirs().toList()
        result.folders = directories.size
        val maxEntries = 4000
        val visitedDirectories = mutableSetOf<String>()
        val supported = Game.extensions

        fun walk(uri: Uri, level: Int, maxVisibleLevel: Int) {
            if (result.scannedEntries >= maxEntries) {
                result.limited = true
                return
            }
            if (!visitedDirectories.add(uri.toString())) return
            val entries = FileUtil.listFiles(uri)
            for (entry in entries) {
                if (result.scannedEntries >= maxEntries) {
                    result.limited = true
                    break
                }
                result.scannedEntries++
                if (entry.isDirectory) {
                    if (level < 6) walk(entry.uri, level + 1, maxVisibleLevel)
                    else result.depthLimit = true
                    continue
                }
                val extension = entry.filename.substringAfterLast('.', "").lowercase(Locale.ROOT)
                val label = entry.filename.replace(Regex("[\\r\\n\\t]"), " ").take(80)
                when {
                    extension in supported -> {
                        if (level <= maxVisibleLevel) {
                            result.visibleCandidates++
                        } else {
                            result.deepCandidates++
                            result.addExample("Fora do alcance: " + label)
                        }
                    }
                    extension == "nsz" || extension == "xcz" -> {
                        result.compressed++
                        result.addExample("Precisa converter: " + label)
                    }
                    extension == "zip" || extension == "rar" || extension == "7z" -> {
                        result.archives++
                        result.addExample("Arquivo compactado: " + label)
                    }
                }
            }
        }

        for (folder in directories) {
            val uri = Uri.parse(folder.uriString)
            if (!FileUtil.isTreeUriValid(uri)) {
                result.inaccessibleFolders++
                result.addExample("Pasta sem permissão: verifique o acesso no seletor Android.")
                continue
            }
            result.readableFolders++
            // GameHelper examines the root with depth 1 and at most two more levels
            // if deepScan is enabled. Continue farther only to diagnose hidden games.
            walk(uri, 1, if (folder.deepScan) 3 else 1)
            if (result.limited) break
        }
        return result
    }
}
