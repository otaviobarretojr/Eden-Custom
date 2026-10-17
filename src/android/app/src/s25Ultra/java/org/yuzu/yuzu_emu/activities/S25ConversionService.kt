// SPDX-FileCopyrightText: Copyright 2026 Eden S25 Ultra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.activities

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.Uri
import android.os.IBinder
import android.provider.DocumentsContract
import android.util.Log
import androidx.core.app.NotificationCompat
import com.androNSZ.NszConverter
import java.util.concurrent.atomic.AtomicBoolean

/** Offline NSZ/XCZ conversion, one active foreground worker. No network use. */
class S25ConversionService: Service() {
    companion object {
        const val PREFS="eden_s25_conversion"
        const val ACTION_START="dev.eden.s25conv.START"
        const val ACTION_CANCEL="dev.eden.s25conv.CANCEL"
        const val EXTRA_SOURCE="source_uri"
        const val EXTRA_DEST="dest_uri"
        const val EXTRA_FORMAT="format"
        private const val CHANNEL="eden_s25_conversion"
        private const val NOTICE=9251
        @Volatile var isActive=false
            private set
    }
    private val running=AtomicBoolean(false)
    @Volatile private var cancelled=false
    override fun onBind(intent:Intent?):IBinder?=null
    override fun onStartCommand(intent:Intent?,flags:Int,startId:Int):Int {
        if(intent?.action==ACTION_CANCEL) {
            cancelled=true
            if(running.get()) {
                save("cancelling","Solicitando cancelamento…")
                runCatching { NszConverter.nativeCancel() }
            } else stopSelf()
            return START_NOT_STICKY
        }
        if(intent?.action!=ACTION_START || !running.compareAndSet(false,true)) return START_NOT_STICKY
        cancelled=false
        isActive=true
        try {
            (getSystemService(NOTIFICATION_SERVICE) as NotificationManager)
                .createNotificationChannel(NotificationChannel(CHANNEL,
                    "Conversão NSZ/XCZ Eden",NotificationManager.IMPORTANCE_LOW))
            startForeground(NOTICE,notification("Preparando conversão",0),
                ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
        } catch(e:Exception) {
            save("failed","Serviço bloqueado: ${e.message}")
            running.set(false);isActive=false;stopSelf(startId)
            return START_NOT_STICKY
        }
        val src=intent.getStringExtra(EXTRA_SOURCE)
        val dst=intent.getStringExtra(EXTRA_DEST)
        val fmt=intent.getStringExtra(EXTRA_FORMAT)
        Thread({ convert(src,dst,fmt) },"EdenNszConvert").start()
        return START_NOT_STICKY
    }
    private fun convert(src:String?,dst:String?,fmt:String?) {
        val target=dst?.let(Uri::parse)
        var success=false
        try {
            require(fmt=="nsz" || fmt=="xcz") { "Formato não suportado." }
            require(!src.isNullOrBlank() && !dst.isNullOrBlank() && src!=dst) {
                "Origem e destino inválidos."
            }
            val inputUri=Uri.parse(src)
            val outputUri=target ?: error("Destino não informado.")
            val selectedFormat=fmt ?: error("Formato não informado.")
            require(inputUri.scheme=="content" && outputUri.scheme=="content") {
                "Escolha os arquivos pelo seletor de documentos Android."
            }
            save("running","Abrindo origem e destino…",0)
            val inputFd=contentResolver.openFileDescriptor(inputUri,"r") ?: error("Falha ao abrir origem")
            inputFd.use { input ->
                require(input.statSize>0L) {
                    "O provedor não disponibilizou leitura aleatória. Mova o arquivo para a memória interna."
                }
                val outputFd=contentResolver.openFileDescriptor(outputUri,"rw")
                    ?: error("Falha ao abrir destino")
                outputFd.use { output ->
                    NszConverter.nativeSetVerification(false,null,null)
                    var last=0L
                    val progress=object:NszConverter.ProgressCallback {
                        override fun onProgress(done:Long,total:Long) {
                            val now=System.currentTimeMillis()
                            if(now-last<650L) return
                            last=now
                            val pct=if(total>0) (done*100.0/total).toInt().coerceIn(0,99) else 0
                            save("running","${selectedFormat.uppercase()}: ${pretty(done)} / ${pretty(total)}",pct)
                            (getSystemService(NOTIFICATION_SERVICE) as NotificationManager)
                                .notify(NOTICE,notification("Convertendo $pct%",pct))
                        }
                    }
                    val status=object:NszConverter.StatusCallback {
                        override fun onStatus(tag:String,msg:String) {
                            if(tag=="ERROR") save("running","Erro durante descompressão; aguardando motor…")
                        }
                    }
                    val code=if(fmt=="nsz") NszConverter.nativeConvert("fd:${input.fd}",
                        "/proc/self/fd/${output.fd}",progress,status)
                    else NszConverter.nativeConvertXcz("fd:${input.fd}",
                        "/proc/self/fd/${output.fd}",progress,status)
                    if(cancelled || code==-8) { save("cancelled","Conversão cancelada");return }
                    require(code==0) { "Motor nativo: ${NszConverter.nativeErrorString(code)} ($code)" }
                }
            }
            if(cancelled) { save("cancelled","Conversão cancelada");return }
            success=true
            save("completed","Arquivo criado no destino escolhido.",100)
        } catch(t:Throwable) {
            Log.e("EdenNsz","Falha no conversor local",t)
            save(if(cancelled) "cancelled" else "failed",
                if(cancelled) "Cancelado" else (t.message?:"Erro desconhecido").take(220))
        } finally {
            if(!success && target!=null) {
                runCatching { DocumentsContract.deleteDocument(contentResolver,target) }
            }
            running.set(false);isActive=false
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf()
        }
    }
    private fun save(state:String,msg:String,percent:Int?=null) {
        getSharedPreferences(PREFS,MODE_PRIVATE).edit().apply {
            putString("state",state);putString("message",msg)
            putLong("heartbeat",System.currentTimeMillis())
            if(percent!=null) putInt("progress",percent)
            apply()
        }
    }
    private fun pretty(bytes:Long):String {
        if(bytes<0) return "?"
        val mib=bytes.toDouble()/(1024.0*1024.0)
        return if(mib<1024) "%.1f MiB".format(mib) else "%.2f GiB".format(mib/1024)
    }
    private fun notification(msg:String,pct:Int):Notification {
        val open=PendingIntent.getActivity(this,301,
            Intent(this,S25UltraConverterActivity::class.java).apply {
                flags=Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP
            },PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
        val cancel=PendingIntent.getService(this,302,
            Intent(this,S25ConversionService::class.java).setAction(ACTION_CANCEL),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
        return NotificationCompat.Builder(this,CHANNEL)
            .setSmallIcon(android.R.drawable.stat_sys_download_done)
            .setContentTitle("Eden S25 Ultra · Conversão")
            .setContentText(msg).setContentIntent(open)
            .setOnlyAlertOnce(true).setOngoing(pct<100)
            .setProgress(100,pct,pct==0)
            .addAction(android.R.drawable.ic_delete,"Cancelar",cancel).build()
    }
    override fun onTimeout(startId:Int,fgsType:Int) {
        cancelled=true
        runCatching { NszConverter.nativeCancel() }
        save("interrupted","Tempo máximo de conversão em segundo plano atingido.")
        stopSelf(startId)
    }
}
