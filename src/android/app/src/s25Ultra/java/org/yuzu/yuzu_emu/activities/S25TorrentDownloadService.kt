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
import android.os.Environment
import android.os.IBinder
import androidx.core.app.NotificationCompat
import com.frostwire.jlibtorrent.AlertListener
import com.frostwire.jlibtorrent.SessionManager
import com.frostwire.jlibtorrent.TorrentHandle
import com.frostwire.jlibtorrent.TorrentInfo
import com.frostwire.jlibtorrent.alerts.AddTorrentAlert
import com.frostwire.jlibtorrent.alerts.Alert
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean

/**
 * One foreground torrent download at a time, triggered by explicit user consent.
 * Android 15+ imposes a time budget on dataSync foreground services. When the
 * service is stopped, users can explicitly restart a saved .torrent from the UI;
 * libtorrent will attempt to validate existing pieces on disk.
 */
class S25TorrentDownloadService : Service() {
    companion object {
        const val PREFS = "s25_torrent_download_state"
        const val ACTION_START = "dev.eden.s25torrent.START"
        const val ACTION_STOP = "dev.eden.s25torrent.STOP"
        const val EXTRA_TORRENT = "torrent_path"
        @Volatile var isDownloadServiceActive: Boolean = false
            private set
        private const val CHANNEL = "s25_torrent_download"
        private const val NOTICE_ID = 9250
    }

    private val running = AtomicBoolean(false)
    @Volatile private var stopped = false
    @Volatile private var handle: TorrentHandle? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            stopped = true
            val prefs = getSharedPreferences(PREFS, MODE_PRIVATE)
            if (running.get()) {
                save("stopping", prefs.getString("name", "").orEmpty(),
                    prefs.getInt("progress", 0), "Encerrando conexão e preservando os arquivos…")
            } else {
                save("stopped", prefs.getString("name", "").orEmpty(),
                    prefs.getInt("progress", 0), "Download interrompido. Você pode retomá-lo.")
                stopSelf()
            }
            return START_NOT_STICKY
        }
        if (intent?.action != ACTION_START || !running.compareAndSet(false, true)) {
            return START_NOT_STICKY
        }
        stopped = false
        isDownloadServiceActive = true
        createChannel()
        startForeground(
            NOTICE_ID,
            notification("Preparando download…", 0),
            ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC
        )

        val path = intent.getStringExtra(EXTRA_TORRENT)
        getSharedPreferences(PREFS, MODE_PRIVATE).edit()
            .putString("torrent_path", path)
            .putLong("heartbeat_at", System.currentTimeMillis())
            .apply()
        Thread({ download(path) }, "S25TorrentDownload").start()
        return START_NOT_STICKY
    }

    private fun download(path: String?) {
        var session: SessionManager? = null
        var resultState = "failed"
        var resultMessage = "O download foi encerrado antes de terminar."
        var resultName = ""
        var resultProgress = 0
        try {
            val torrent = File(path ?: error("Arquivo .torrent não informado."))
            require(torrent.isFile && torrent.length() in 1..(5L * 1024 * 1024)) {
                "Arquivo .torrent não encontrado ou inválido."
            }
            val info = TorrentInfo(torrent)
            val dest = File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS), "EdenS25")
            require(Environment.isExternalStorageManager()) { "Permissão de acesso aos arquivos revogada." }
            require(dest.isDirectory || dest.mkdirs()) { "Impossível criar Download/EdenS25." }
            val name = info.name().take(120)
            resultName = name
            save("running", name, 0, "Buscando pares…", 0L, info.totalSize())
            session = SessionManager()
            session.addListener(object : AlertListener {
                override fun types(): IntArray? = null
                override fun alert(alert: Alert<*>) {
                    if (alert is AddTorrentAlert) {
                        handle = alert.handle()
                        handle?.resume()
                    }
                }
            })
            session.start()
            session.download(info, dest)
            var lastProgress = -1
            var startupChecks = 0
            while (!stopped) {
                Thread.sleep(1300L)
                val th = handle ?: session.find(info.infoHashV1())
                if (th == null) {
                    startupChecks++
                    if (startupChecks > 50) error("Não foi possível iniciar o torrent (tracker ou metadados).")
                    continue
                }
                val torrentStatus = th.status()
                val nativeError = torrentStatus.errorCode()
                if (nativeError.value() != 0) error(nativeError.message())
                val progress = (torrentStatus.progress() * 100f).toInt().coerceIn(0, 100)
                resultProgress = progress
                val kib = torrentStatus.downloadRate() / 1024
                val downloadedBytes = torrentStatus.totalWantedDone().coerceAtLeast(0L)
                val totalBytes = torrentStatus.totalWanted().coerceAtLeast(0L)
                save("running", name, progress,
                    "${kib} KiB/s · ${torrentStatus.numPeers()} pares",
                    downloadedBytes, totalBytes)
                if (progress != lastProgress) {
                    (getSystemService(NOTIFICATION_SERVICE) as NotificationManager)
                        .notify(NOTICE_ID, notification(name, progress))
                    lastProgress = progress
                }
                if (torrentStatus.isFinished() || torrentStatus.isSeeding()) {
                    resultState = "completed"
                    resultProgress = 100
                    resultMessage = "Download concluído e peças verificadas pelo BitTorrent."
                    break
                }
            }
            if (stopped) {
                resultState = "stopped"
                resultMessage = "Conexão encerrada; arquivos parciais preservados."
            }
        } catch (ex: Throwable) {
            android.util.Log.e("S25Torrent", "Download failed", ex)
            resultState = if (stopped) "stopped" else "failed"
            resultMessage = (ex.message ?: ex.javaClass.simpleName).take(200)
        } finally {
            // Keep UI in 'stopping' until native torrent resources are released.
            runCatching { session?.stop() }.onFailure {
                android.util.Log.w("S25Torrent", "Native session cleanup failed", it)
            }
            handle = null
            running.set(false)
            isDownloadServiceActive = false
            val prefs = getSharedPreferences(PREFS, MODE_PRIVATE)
            save(resultState, resultName.ifBlank { prefs.getString("name", "").orEmpty() },
                if (resultProgress > 0) resultProgress else prefs.getInt("progress", 0),
                resultMessage)
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf()
        }
    }

    private fun save(
        state: String,
        name: String,
        progress: Int,
        message: String,
        downloadedBytes: Long? = null,
        totalBytes: Long? = null
    ) {
        getSharedPreferences(PREFS, MODE_PRIVATE).edit().apply {
            putString("state", state)
            putString("name", name)
            putInt("progress", progress)
            putString("message", message)
            putLong("heartbeat_at", System.currentTimeMillis())
            if (downloadedBytes != null) putLong("downloaded_bytes", downloadedBytes)
            if (totalBytes != null) putLong("total_bytes", totalBytes)
            apply()
        }
    }

    private fun createChannel() {
        val manager = getSystemService(NOTIFICATION_SERVICE) as NotificationManager
        manager.createNotificationChannel(
            NotificationChannel(CHANNEL, "Downloads Eden S25 Ultra", NotificationManager.IMPORTANCE_LOW)
        )
    }

    private fun notification(label: String, percent: Int): Notification {
        val stopIntent = PendingIntent.getService(
            this,
            101,
            Intent(this, S25TorrentDownloadService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        return NotificationCompat.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.stat_sys_download)
            .setContentTitle("Eden S25 Ultra · Torrent")
            .setContentText(label)
            .setOngoing(percent < 100)
            .setOnlyAlertOnce(true)
            .setProgress(100, percent, percent == 0)
            .addAction(android.R.drawable.ic_delete, "Parar", stopIntent)
            .build()
    }

    override fun onDestroy() {
        stopped = true
        super.onDestroy()
    }
}
