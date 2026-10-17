// SPDX-FileCopyrightText: Copyright 2026 Eden S25 Ultra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.os.SystemClock
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.R

/**
 * Low-overhead, advisory thermal monitor, active only while a game activity
 * is visible on the supported S25 Ultra. Never bypasses thermal throttling,
 * changes device clocks, or mutates live renderer settings.
 *
 * Thermal headroom is a normalized prediction (not a temperature in °C).
 * The OEM supplies thresholds, so do not interpret it as a fixed Celsius value.
 */
class S25ThermalGuardian(
    context: Context,
    private val warning: (String) -> Unit
) {
    private val appContext = context.applicationContext
    private val power = appContext.getSystemService(Context.POWER_SERVICE) as? PowerManager
    private val handler = Handler(Looper.getMainLooper())
    private val minWarningIntervalMs = 120_000L
    private var lastWarningMs = -minWarningIntervalMs
    private var running = false
    private var previousSeverity = 0

    private val statusListener = PowerManager.OnThermalStatusChangedListener { status ->
        if (running) assess(status, null)
    }

    private val sampler = object : Runnable {
        override fun run() {
            if (!running) return
            sample()
            handler.postDelayed(this, 15_000L)
        }
    }

    fun start() {
        if (running || !S25UltraProfile.isSupportedDevice() || power == null) return
        running = true
        // A broadcast-style listener responds promptly to system thermal changes.
        runCatching { power.addThermalStatusListener(appContext.mainExecutor, statusListener) }
            .onFailure { android.util.Log.w("S25ThermalGuardian", "Thermal listener unavailable", it) }
        handler.post(sampler)
    }

    fun stop() {
        if (!running) return
        running = false
        handler.removeCallbacks(sampler)
        runCatching { power?.removeThermalStatusListener(statusListener) }
        previousSeverity = 0
    }

    private fun sample() {
        val service = power ?: return
        val currentStatus = runCatching { service.currentThermalStatus }.getOrNull() ?: return

        // The official Android API recommends sampling no faster than once
        // every ten seconds. NaN means the device did not provide a prediction.
        val predicted = runCatching { service.getThermalHeadroom(10) }
            .getOrNull()?.takeIf { !it.isNaN() && it.isFinite() }

        val moderateThreshold = runCatching {
            service.thermalHeadroomThresholds[PowerManager.THERMAL_STATUS_MODERATE]
        }.getOrNull()

        // Only infer a warning from the forecast if the device declares the
        // matching OEM threshold. No speculative universal threshold.
        val nearModerate = predicted != null &&
            moderateThreshold != null &&
            predicted >= moderateThreshold
        assess(currentStatus, if (nearModerate) predicted else null)
    }

    private fun assess(status: Int, predictedModerate: Float?) {
        if (!running) return
        if (!runCatching { NativeLibrary.isRunning() }.getOrDefault(false)) return

        val severity = when {
            status >= PowerManager.THERMAL_STATUS_CRITICAL -> 3
            status >= PowerManager.THERMAL_STATUS_SEVERE -> 2
            status >= PowerManager.THERMAL_STATUS_MODERATE || predictedModerate != null -> 1
            else -> 0
        }

        if (severity == 0) {
            previousSeverity = 0
            return
        }

        val now = SystemClock.elapsedRealtime()
        if (severity <= previousSeverity && now - lastWarningMs < minWarningIntervalMs) return
        if (severity > previousSeverity || now - lastWarningMs >= minWarningIntervalMs) {
            previousSeverity = severity
            lastWarningMs = now
            val messageId = if (severity >= 2) {
                R.string.s25_thermal_high_warning
            } else {
                R.string.s25_thermal_early_warning
            }
            android.util.Log.w(
                "S25ThermalGuardian",
                "Thermal pressure severity=$severity, platformStatus=$status; " +
                    "advisory only, no live setting change"
            )
            warning(appContext.getString(messageId))
        }
    }
}
