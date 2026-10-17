// SPDX-FileCopyrightText: Copyright 2026 Eden S25 Ultra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.activities

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.OpenableColumns
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import android.view.ViewGroup
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.google.android.material.button.MaterialButton
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.yuzu.yuzu_emu.model.GameDir
import org.yuzu.yuzu_emu.utils.NativeConfig
import java.util.Locale

/** Offline conversion manager. File access through Storage Access Framework only. */
class S25UltraConverterActivity : AppCompatActivity() {
    private lateinit var sourceLabel: TextView
    private lateinit var destLabel: TextView
    private lateinit var stateLabel: TextView
    private lateinit var startButton: MaterialButton
    private lateinit var cancelButton: MaterialButton
    private var source: Uri? = null
    private var destination: Uri? = null
    private var sourceName = ""
    private var format = ""
    private val handler = Handler(Looper.getMainLooper())

    private val selectSource = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) inspectSource(uri)
    }
    private val chooseOutput = registerForActivityResult(
        ActivityResultContracts.CreateDocument("application/octet-stream")
    ) { uri ->
        if (uri != null) {
            if (uri == source) { errorMessage("Origem e destino não podem ser iguais."); return@registerForActivityResult }
            grant(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
            destination = uri
            getSharedPreferences(S25ConversionService.PREFS, MODE_PRIVATE).edit()
                .putString("dest_uri", uri.toString()).apply()
            destLabel.text = "Destino: ${fileName(uri)}"
            controls()
        }
    }
    private val chooseLibrary = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) try {
            grant(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
            if (NativeConfig.getGameDirs().none { it.uriString == uri.toString() }) {
                NativeConfig.addGameDir(GameDir(uri.toString(), false))
                NativeConfig.saveGlobalConfig()
            }
            toast("Pasta adicionada. Atualize a biblioteca do Eden.")
        } catch (e: Exception) { errorMessage(e.message ?: "Não foi possível registrar a pasta.") }
    }
    private val updater = object : Runnable {
        override fun run() { refresh(); handler.postDelayed(this, 1100) }
    }
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        title = "Conversor · Eden S25 Ultra"
        val dp = resources.displayMetrics.density
        fun p(n: Int) = (n * dp).toInt()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(p(20),p(20),p(20),p(28))
        }
        fun label(s: String, size: Float) = TextView(this).apply {
            text = s; textSize = size; setPadding(0,0,0,p(14))
        }
        fun button(s: String, action: () -> Unit) = MaterialButton(this).apply {
            text = s
            setOnClickListener { action() }
            col.addView(this, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        }
        col.addView(label("Conversor de jogos",23f))
        col.addView(label("NSZ → NSP | XCZ → XCI. Tudo no celular, sem torrent ou downloads de jogos.",14f))
        sourceLabel=label("Origem: nenhum arquivo selecionado.",14f)
        col.addView(sourceLabel)
        button("1 · Selecionar NSZ ou XCZ") { selectSource.launch(arrayOf("*/*")) }
        destLabel=label("Destino: escolha onde salvar o arquivo descompactado.",14f)
        col.addView(destLabel)
        button("2 · Escolher arquivo de destino") {
            if (source == null) errorMessage("Selecione primeiro a origem.")
            else chooseOutput.launch(sourceName.substringBeforeLast('.') + if(format=="nsz") ".nsp" else ".xci")
        }
        startButton=button("3 · Converter agora") { confirm() }
        cancelButton=button("Cancelar conversão") {
            startService(Intent(this,S25ConversionService::class.java).setAction(S25ConversionService.ACTION_CANCEL))
            toast("Cancelamento solicitado.")
        }
        stateLabel=label("Aguardando seleção.",14f)
        col.addView(stateLabel)
        button("Adicionar pasta de saída à biblioteca do Eden") { chooseLibrary.launch(null) }
        button("Copiar diagnóstico da conversão") {
            val prefs=getSharedPreferences(S25ConversionService.PREFS,MODE_PRIVATE)
            val diagnostic="Estado: ${prefs.getString("state","idle")}\n" +
                "Motor ativo: ${S25ConversionService.isActive}\n" +
                "Formato: ${prefs.getString("format","")}\n" +
                "Progresso: ${prefs.getInt("progress",0)}%\n" +
                "Mensagem: ${prefs.getString("message","")}"
            (getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager)
                .setPrimaryClip(ClipData.newPlainText("Eden Converter",diagnostic))
            toast("Diagnóstico copiado, sem nomes, chaves ou caminhos pessoais.")
        }
        col.addView(label("O resultado pode ocupar muito mais espaço que o NSZ/XCZ. O original nunca será apagado. Use somente backups legítimos.",13f))
        setContentView(ScrollView(this).apply { addView(col) })
        controls()
    }
    override fun onResume() {
        super.onResume()
        val prefs=getSharedPreferences(S25ConversionService.PREFS,MODE_PRIVATE)
        if(source==null) prefs.getString("source_uri",null)?.let {
            source=Uri.parse(it); sourceName=prefs.getString("source_name","").orEmpty()
            format=prefs.getString("format","").orEmpty()
            sourceLabel.text="Origem: $sourceName"
        }
        if(destination==null) prefs.getString("dest_uri",null)?.let {
            destination=Uri.parse(it); destLabel.text="Destino: ${fileName(destination!!)}"
        }
        handler.removeCallbacks(updater); handler.post(updater)
    }
    override fun onPause() { handler.removeCallbacks(updater); super.onPause() }

    private fun inspectSource(uri: Uri) {
        if(S25ConversionService.isActive) { errorMessage("Finalize a conversão atual."); return }
        val name=fileName(uri)
        val ext=name.substringAfterLast('.',"").lowercase(Locale.ROOT)
        if(ext!="nsz"&&ext!="xcz") { errorMessage("Selecione um arquivo .nsz ou .xcz."); return }
        val magic=runCatching { contentResolver.openInputStream(uri)?.use { input ->
            if(ext=="nsz") input.readNBytes(4).contentEquals(byteArrayOf(80,70,83,48))
            else input.read()>=0
        } == true }.getOrDefault(false)
        if(!magic) { errorMessage("Arquivo inacessível ou não reconhecido. NSZ exige cabeçalho PFS0.");return }
        grant(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        source=uri; sourceName=name; format=ext; destination=null
        sourceLabel.text="Origem: $name"
        destLabel.text="Destino: selecione arquivo .${if(ext=="nsz") "nsp" else "xci"}"
        getSharedPreferences(S25ConversionService.PREFS,MODE_PRIVATE).edit()
            .putString("source_uri",uri.toString()).putString("source_name",name)
            .putString("format",ext).remove("dest_uri").apply()
        controls()
    }
    private fun confirm() {
        val input=source ?: return
        val output=destination ?: return
        if(S25ConversionService.isActive || input==output) { errorMessage("Conversão em andamento ou destino igual à origem.");return }
        MaterialAlertDialogBuilder(this).setTitle("Iniciar conversão?")
            .setMessage("O arquivo de saída pode ficar muito maior que o original. Confira o espaço disponível e o direito de processar esse backup. A origem será preservada.")
            .setPositiveButton("Converter") { _, _ ->
                val prefs=getSharedPreferences(S25ConversionService.PREFS,MODE_PRIVATE)
                prefs.edit().putString("state","starting")
                    .putString("message","Preparando motor nativo…")
                    .putLong("heartbeat",System.currentTimeMillis()).apply()
                val intent=Intent(this,S25ConversionService::class.java).apply {
                    action=S25ConversionService.ACTION_START
                    putExtra(S25ConversionService.EXTRA_SOURCE,input.toString())
                    putExtra(S25ConversionService.EXTRA_DEST,output.toString())
                    putExtra(S25ConversionService.EXTRA_FORMAT,format)
                }
                runCatching { ContextCompat.startForegroundService(this,intent) }.onFailure {
                    prefs.edit().putString("state","failed").putString("message",it.message).apply()
                    errorMessage("Não foi possível iniciar: ${it.message}")
                }
                controls()
            }.setNegativeButton("Cancelar",null).show()
    }
    private fun refresh() {
        val prefs=getSharedPreferences(S25ConversionService.PREFS,MODE_PRIVATE)
        var state=prefs.getString("state","idle").orEmpty()
        val hb=prefs.getLong("heartbeat",0L)
        if(state in listOf("starting","running","cancelling") && !S25ConversionService.isActive &&
            (hb==0L || System.currentTimeMillis()-hb>15000)) {
            state="interrupted"
            prefs.edit().putString("state",state).putString("message","Serviço interrompido pelo Android.").apply()
        }
        val pct=prefs.getInt("progress",0)
        val msg=prefs.getString("message","").orEmpty()
        stateLabel.text=when(state) {
            "starting" -> "Preparando conversão…\n$msg"
            "running" -> "Convertendo: $pct%\n$msg"
            "cancelling" -> "Cancelando…"
            "completed" -> "Concluído: 100%. Atualize a biblioteca do Eden."
            "cancelled" -> "Cancelado. O original foi preservado."
            "failed","interrupted" -> "Falha ou interrupção: $msg"
            else -> "Aguardando seleção."
        }
        controls()
    }
    private fun controls() {
        val state=getSharedPreferences(S25ConversionService.PREFS,MODE_PRIVATE).getString("state","idle")
        val busy=S25ConversionService.isActive || state in listOf("starting","running","cancelling")
        startButton.isEnabled=!busy && source!=null && destination!=null
        cancelButton.isEnabled=busy
    }
    private fun fileName(uri: Uri): String = runCatching {
        contentResolver.query(uri,arrayOf(OpenableColumns.DISPLAY_NAME),null,null,null)
            ?.use { if(it.moveToFirst()) it.getString(0) else null }
    }.getOrNull() ?: uri.lastPathSegment.orEmpty().substringAfterLast('/')
    private fun grant(uri: Uri,flags: Int) {
        runCatching { contentResolver.takePersistableUriPermission(uri,flags) }
    }
    private fun toast(s:String)=Toast.makeText(this,s,Toast.LENGTH_LONG).show()
    private fun errorMessage(s:String) {
        MaterialAlertDialogBuilder(this).setTitle("Conversor Eden")
            .setMessage(s).setPositiveButton("OK",null).show()
    }
}
