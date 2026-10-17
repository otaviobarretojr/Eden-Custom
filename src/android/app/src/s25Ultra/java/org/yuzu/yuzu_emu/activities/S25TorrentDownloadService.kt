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
 * Android 15+ imposes a time budget on dataSync foreground services. If stopped, the
 * already-downloaded pieces remain on disk but the user must reimport to restart.
 */
class S25TorrentDownloadService : Service() {
    companion object {
        const val PREFS = "s25_torrent_download_state"
        const val ACTION_START = "dev.eden.s25torrent.START"
        const val ACTION_STOP = "dev.eden.s25torrent.STOP"
        const val EXTRA_TORRENT = "torrent_path"
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
            if (!running.get()) stopSelf()
            return START_NOT_STICKY
        }
        if (intent?.action != ACTION_START || !running.compareAndSet(false, true)) {
            return START_NOT_STICKY
        }
        stopped = false
        createChannel()
        startForeground(
            NOTICE_ID,
            notification("Preparando download…", 0),
            ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC
        )

        val path = intent.getStringExtra(EXTRA_TORRENT)
        Thread({ download(path) }, "S25TorrentDownload").start()
        return START_NOT_STICKY
    }

    private fun download(path: String?) {
        var session: SessionManager? = null
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
            save("running", name, 0, "Buscando pares…")
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
                val kib = torrentStatus.downloadRate() / 1024
                save("running", name, progress, "${kib} KiB/s · ${torrentStatus.numPeers()} pares")
                if (progress != lastProgress) {
                    (getSystemService(NOTIFICATION_SERVICE) as NotificationManager)
                        .notify(NOTICE_ID, notification(name, progress))
                    lastProgress = progress
                }
                if (torrentStatus.isFinished() || torrentStatus.isSeeding()) {
                    save("completed", name, 100, "Arquivos verificados pelo cliente BitTorrent.")
                    (getSystemService(NOTIFICATION_SERVICE) as NotificationManager)
                        .notify(NOTICE_ID, notification("Download concluído: $name", 100))
                    break
                }
            }
            if (stopped) save("stopped", name, 0, "Interrompido pelo usuário.")
        } catch (ex: Throwable) {
            val message = ex.message ?: ex.javaClass.simpleName
            android.util.Log.e("S25Torrent", "Download failed", ex)
            save(if (stopped) "stopped" else "failed", "", 0, message.take(200))
        } finally {
            runCatching { session?.stop() }
            handle = null
            running.set(false)
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf()
        }
    }

    private fun save(state: String, name: String, progress: Int, message: String) {
        getSharedPreferences(PREFS, MODE_PRIVATE).edit()
            .putString("state", state)
            .putString("name", name)
            .putInt("progress", progress)
            .putString("message", message)
            .apply()
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
