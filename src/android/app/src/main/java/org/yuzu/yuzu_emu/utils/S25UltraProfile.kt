// SPDX-FileCopyrightText: Copyright 2026 Eden S25 Ultra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import android.app.ActivityManager
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.PowerManager
import android.widget.Toast
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import java.util.Locale
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.features.settings.model.BooleanSetting
import org.yuzu.yuzu_emu.features.settings.model.IntSetting

/**
 * Explicit, reversible GLOBAL configuration presets for the Galaxy S25 Ultra.
 *
 * Resolution values follow rendererResolutionValues in res/values/arrays.xml:
 * 2 = 0.75x, 3 = 1x, 5 = 1.5x. No thermal overclock, driver replacement,
 * frame-generation spoofing or per-game configuration is performed.
 */
object S25UltraProfile {
    private const val PREF_FILE = "s25_ultra_presets_v1"
    private const val BACKUP_VALID = "backup_valid"
    private const val CHOSEN_PROFILE = "chosen_profile"

    private val backedUpBooleanSettings = listOf(
        BooleanSetting.RENDERER_USE_DISK_SHADER_CACHE,
        BooleanSetting.RENDERER_ASYNCHRONOUS_GPU_EMULATION,
        BooleanSetting.SHOW_PERFORMANCE_OVERLAY,
        BooleanSetting.SHOW_FPS,
        BooleanSetting.SHOW_FRAMETIME
    )

    // These options were not managed by v1 presets. Back them up only when the
    // user explicitly selects the new shader-fluidity preset, so upgrades do
    // not overwrite shaders configured manually on earlier Eden versions.
    private val shaderFluiditySettings = listOf(
        BooleanSetting.RENDERER_ASYNCHRONOUS_SHADERS,
        BooleanSetting.SHOW_SHADERS_BUILDING
    )

    private fun backupKey(setting: BooleanSetting) = "backup_" + setting.key

    fun isSupportedDevice(): Boolean =
        Build.MANUFACTURER.equals("samsung", ignoreCase = true) &&
            Build.MODEL.uppercase(Locale.ROOT).startsWith("SM-S938")

    fun showPresetDialog(context: Context) {
        if (!isSupportedDevice()) {
            MaterialAlertDialogBuilder(context)
                .setTitle(R.string.s25_device_only_title)
                .setMessage(R.string.s25_device_only_message)
                .setPositiveButton(android.R.string.ok, null)
                .show()
            return
        }

        val preferences = context.getSharedPreferences(PREF_FILE, Context.MODE_PRIVATE)
        val labels = arrayOf(
            context.getString(R.string.s25_performance_profile),
            context.getString(R.string.s25_balanced_profile),
            context.getString(R.string.s25_quality_profile),
            context.getString(R.string.s25_shader_fluidity_profile)
        )
        var choice = preferences.getInt(CHOSEN_PROFILE, 1).coerceIn(0, 3)

        MaterialAlertDialogBuilder(context)
            .setTitle(R.string.s25_profile_title)
            .setSingleChoiceItems(labels, choice) { _, selected -> choice = selected }
            .setPositiveButton(R.string.s25_apply_profile) { _, _ ->
                if (NativeConfig.isPerGameConfigLoaded()) {
                    showToast(context, R.string.s25_close_game_warning)
                    return@setPositiveButton
                }
                try {
                    if (!preferences.getBoolean(BACKUP_VALID, false)) {
                        // Capture the user's actual settings once, before any preset is applied.
                        preferences.edit().apply {
                            putInt("backup_resolution", IntSetting.RENDERER_RESOLUTION.getInt(true))
                            backedUpBooleanSettings.forEach { setting ->
                                putBoolean(backupKey(setting), setting.getBoolean(true))
                            }
                            putBoolean(BACKUP_VALID, true)
                            apply()
                        }
                    }

                    val previouslySelected = preferences.getInt(CHOSEN_PROFILE, -1)

                    if (choice == 3) {
                        // Add only missing backup keys, including when migrating
                        // an existing v1 backup from an earlier installed APK.
                        preferences.edit().apply {
                            shaderFluiditySettings.forEach { setting ->
                                if (!preferences.contains(backupKey(setting))) {
                                    putBoolean(backupKey(setting), setting.getBoolean(true))
                                }
                            }
                            apply()
                        }
                    } else if (previouslySelected == 3) {
                        // Leaving the opt-in profile must undo its experimental
                        // shader options without touching the user's driver/cache.
                        shaderFluiditySettings.forEach { setting ->
                            if (preferences.contains(backupKey(setting))) {
                                setting.setBoolean(
                                    preferences.getBoolean(backupKey(setting), setting.getBoolean(true))
                                )
                            }
                        }
                    }

                    val resolution = when (choice) {
                        0 -> 2 // 0.75x
                        2 -> 5 // 1.50x
                        else -> 3 // 1.00x (balanced or shader-fluidity)
                    }
                    IntSetting.RENDERER_RESOLUTION.setInt(resolution)
                    BooleanSetting.RENDERER_USE_DISK_SHADER_CACHE.setBoolean(true)
                    BooleanSetting.RENDERER_ASYNCHRONOUS_GPU_EMULATION.setBoolean(true)

                    if (choice == 3) {
                        // Best-effort asynchronous compile. New shaders can still
                        // briefly stall and some games can exhibit missing effects.
                        BooleanSetting.RENDERER_ASYNCHRONOUS_SHADERS.setBoolean(true)
                        BooleanSetting.SHOW_SHADERS_BUILDING.setBoolean(true)
                    }

                    // Performance or shader-fluidity modes show diagnostic overlays.
                    // Never touch the Vulkan driver, clocks or pipeline worker count.
                    val monitoring = choice == 0 || choice == 3
                    BooleanSetting.SHOW_PERFORMANCE_OVERLAY.setBoolean(monitoring)
                    BooleanSetting.SHOW_FPS.setBoolean(monitoring)
                    BooleanSetting.SHOW_FRAMETIME.setBoolean(monitoring)

                    NativeConfig.saveGlobalConfig()
                    preferences.edit().putInt(CHOSEN_PROFILE, choice).apply()
                    showToast(
                        context,
                        if (choice == 3) R.string.s25_shader_fluidity_applied
                        else R.string.s25_profile_applied
                    )
                } catch (error: Exception) {
                    android.util.Log.e("S25UltraProfile", "Unable to apply preset", error)
                    showToast(context, R.string.s25_profile_error)
                }
            }
            .setNeutralButton(R.string.s25_restore_settings) { _, _ ->
                if (NativeConfig.isPerGameConfigLoaded()) {
                    showToast(context, R.string.s25_close_game_warning)
                    return@setNeutralButton
                }
                if (!preferences.getBoolean(BACKUP_VALID, false)) {
                    showToast(context, R.string.s25_nothing_to_restore)
                    return@setNeutralButton
                }
                try {
                    IntSetting.RENDERER_RESOLUTION.setInt(
                        preferences.getInt("backup_resolution", 3)
                    )
                    backedUpBooleanSettings.forEach { setting ->
                        setting.setBoolean(
                            preferences.getBoolean(backupKey(setting), setting.getBoolean(true))
                        )
                    }
                    // v1 backups lack these keys. Leave the user's shader
                    // preferences alone unless the opt-in preset saved them.
                    shaderFluiditySettings.forEach { setting ->
                        if (preferences.contains(backupKey(setting))) {
                            setting.setBoolean(
                                preferences.getBoolean(backupKey(setting), setting.getBoolean(true))
                            )
                        }
                    }
                    NativeConfig.saveGlobalConfig()
                    preferences.edit().clear().apply()
                    showToast(context, R.string.s25_profile_restored)
                } catch (error: Exception) {
                    android.util.Log.e("S25UltraProfile", "Unable to restore settings", error)
                    showToast(context, R.string.s25_profile_error)
                }
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    fun showDiagnosticsDialog(context: Context) {
        val details = buildString {
            appendLine("Modelo: " + Build.MANUFACTURER + " " + Build.MODEL)
            appendLine("Android: " + Build.VERSION.RELEASE + " (API " +
                Build.VERSION.SDK_INT + ")")
            appendLine("Arquitetura: " + Build.SUPPORTED_ABIS.joinToString())
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                appendLine("SoC: " + Build.SOC_MODEL)
            }

            val ram = (context.getSystemService(Context.ACTIVITY_SERVICE) as? ActivityManager)
                ?.let { manager ->
                    val info = ActivityManager.MemoryInfo()
                    manager.getMemoryInfo(info)
                    info.totalMem / (1024 * 1024)
                }
            if (ram != null) appendLine("RAM total: " + ram + " MiB")

            val thermalStatus = (context.getSystemService(Context.POWER_SERVICE) as? PowerManager)
                ?.currentThermalStatus
            if (thermalStatus != null) {
                appendLine("Estado termico Android: " + thermalStatus +
                    " (0 = sem limitacao informada)")
            }
            appendLine("GPU: " + runCatching {
                NativeLibrary.getGpuModel()
            }.getOrDefault("Indisponivel"))
            appendLine("Vulkan: " + runCatching {
                NativeLibrary.getVulkanApiVersion()
            }.getOrDefault("Indisponivel"))
            appendLine("Driver Vulkan: " + runCatching {
                NativeLibrary.getVulkanDriverVersion()
            }.getOrDefault("Indisponivel"))

            appendLine()
            appendLine("Diagnóstico de fluidez (configuração global):")
            fun enabled(setting: BooleanSetting): String =
                if (setting.getBoolean(true)) "Ativado" else "Desativado"
            appendLine("Cache de shaders: " +
                enabled(BooleanSetting.RENDERER_USE_DISK_SHADER_CACHE))
            appendLine("Shaders assíncronos: " +
                enabled(BooleanSetting.RENDERER_ASYNCHRONOUS_SHADERS))
            appendLine("GPU assíncrona: " +
                enabled(BooleanSetting.RENDERER_ASYNCHRONOUS_GPU_EMULATION))
            appendLine("Apresentação assíncrona: " +
                enabled(BooleanSetting.RENDERER_ASYNC_PRESENTATION))
            appendLine("Mostrar shaders compilando: " +
                enabled(BooleanSetting.SHOW_SHADERS_BUILDING))
            appendLine("Threads de pipelines: " +
                IntSetting.ANDROID_PIPELINE_WORKERS.getInt(true))
            appendLine("Escala da resolução (índice): " +
                IntSetting.RENDERER_RESOLUTION.getInt(true))
            appendLine()
            appendLine(context.getString(R.string.s25_shader_fluidity_diagnostics_tip))
            appendLine()
            appendLine(context.getString(R.string.s25_diagnostics_disclaimer))
        }

        MaterialAlertDialogBuilder(context)
            .setTitle(R.string.s25_diagnostics_title)
            .setMessage(details)
            .setPositiveButton(android.R.string.ok, null)
            .setNeutralButton(R.string.s25_share_diagnostics) { _, _ ->
                val send = Intent(Intent.ACTION_SEND).apply {
                    type = "text/plain"
                    putExtra(Intent.EXTRA_TEXT, details)
                }
                context.startActivity(Intent.createChooser(send, null))
            }
            .show()
    }

    private fun showToast(context: Context, messageId: Int) {
        Toast.makeText(context, messageId, Toast.LENGTH_LONG).show()
    }
}
