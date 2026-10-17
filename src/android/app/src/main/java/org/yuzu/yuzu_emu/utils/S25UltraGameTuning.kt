// SPDX-FileCopyrightText: Copyright 2026 Eden S25 Ultra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import android.content.Context
import android.widget.Toast
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import java.io.File
import java.security.MessageDigest
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.features.settings.model.BooleanSetting
import org.yuzu.yuzu_emu.features.settings.model.IntSetting
import org.yuzu.yuzu_emu.features.settings.utils.SettingsFile
import org.yuzu.yuzu_emu.model.Game

/**
 * Opt-in, reversible PER-GAME experiments. No global settings, game content,
 * drivers, shaders or saves are deleted or modified by this helper.
 *
 * GPU accuracy enum in src/common/settings_enums.h:
 * Low=0, High=1. Resolution indices from rendererResolutionValues.
 */
object S25UltraGameTuning {
    private const val PREF_FILE = "s25_game_tuning_v1"

    private fun key(game: Game) = "original:" + game.settingsName
    private fun hashKey(game: Game) = "managed_hash:" + game.settingsName
    private fun backup(config: File) = File(config.parentFile, config.name + ".s25-original.bak")

    private fun sha256(config: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        config.inputStream().buffered().use { input ->
            val buffer = ByteArray(16 * 1024)
            while (true) {
                val read = input.read(buffer)
                if (read < 0) break
                digest.update(buffer, 0, read)
            }
        }
        return digest.digest().joinToString("") { "%02x".format(it) }
    }

    fun showDialog(context: Context, game: Game) {
        if (!S25UltraProfile.isSupportedDevice()) return
        val prefs = context.getSharedPreferences(PREF_FILE, Context.MODE_PRIVATE)
        val config = SettingsFile.getCustomSettingsFile(game)
        val labels = arrayOf(
            context.getString(R.string.s25_game_compatibility),
            context.getString(R.string.s25_game_performance),
            context.getString(R.string.s25_game_quality)
        )
        var selected = 0

        MaterialAlertDialogBuilder(context)
            .setTitle(context.getString(R.string.s25_game_title, game.title))
            .setMessage(R.string.s25_game_warning)
            .setSingleChoiceItems(labels, selected) { _, index -> selected = index }
            .setPositiveButton(R.string.s25_apply_profile) { _, _ ->
                if (NativeConfig.isPerGameConfigLoaded()) {
                    Toast.makeText(context, R.string.s25_close_game_warning, Toast.LENGTH_LONG).show()
                    return@setPositiveButton
                }
                try {
                    val previouslyManaged = prefs.contains(key(game))
                    if (!previouslyManaged) {
                        val existed = config.exists()
                        if (existed) {
                            val original = backup(config)
                            check(!original.exists()) { "Há um backup anterior. Preserve-o antes de continuar." }
                            config.copyTo(original, overwrite = false)
                        }
                        // Durable marker even when there was no original INI.
                        check(prefs.edit().putBoolean(key(game), existed).commit()) {
                            "Não foi possível registrar o backup."
                        }
                    } else {
                        val lastHash = prefs.getString(hashKey(game), null)
                        if (lastHash != null && (!config.exists() || sha256(config) != lastHash)) {
                            error("As configurações foram modificadas fora dos perfis. Restaure ou faça backup antes de aplicar novamente.")
                        }
                    }

                    // Match SettingsActivity's lifecycle, but never alter global config.
                    SettingsFile.loadCustomConfig(game)
                    try {
                        IntSetting.RENDERER_RESOLUTION.setInt(
                            when (selected) {
                                1 -> 2 // performance, 0.75x
                                2 -> 5 // quality, 1.5x
                                else -> 3 // compatibility, 1x
                            }
                        )
                        // Low accuracy boosts speed, High fixes some render/memory ordering defects.
                        IntSetting.RENDERER_ACCURACY.setInt(if (selected == 1) 0 else 1)
                        BooleanSetting.RENDERER_USE_DISK_SHADER_CACHE.setBoolean(true)
                        // Compatibility prioritizes reproducible graphics over concurrency.
                        BooleanSetting.RENDERER_ASYNCHRONOUS_GPU_EMULATION.setBoolean(selected != 0)
                        // Async shaders can reduce stutter but temporarily hide effects.
                        BooleanSetting.RENDERER_ASYNCHRONOUS_SHADERS.setBoolean(selected == 1)
                        NativeConfig.savePerGameConfig()
                    } finally {
                        NativeConfig.unloadPerGameConfig()
                    }
                    check(prefs.edit().putString(hashKey(game), sha256(config)).commit()) {
                        "Perfil aplicado, mas a verificação da cópia falhou."
                    }
                    Toast.makeText(context, R.string.s25_game_applied, Toast.LENGTH_LONG).show()
                } catch (error: Exception) {
                    android.util.Log.e("S25UltraGameTuning", "Unable to tune game", error)
                    MaterialAlertDialogBuilder(context)
                        .setTitle(R.string.s25_profile_error)
                        .setMessage(error.message ?: context.getString(R.string.s25_profile_error))
                        .setPositiveButton(android.R.string.ok, null)
                        .show()
                }
            }
            .setNeutralButton(R.string.s25_restore_settings) { _, _ ->
                if (NativeConfig.isPerGameConfigLoaded()) {
                    Toast.makeText(context, R.string.s25_close_game_warning, Toast.LENGTH_LONG).show()
                    return@setNeutralButton
                }
                if (!prefs.contains(key(game))) {
                    Toast.makeText(context, R.string.s25_nothing_to_restore, Toast.LENGTH_LONG).show()
                    return@setNeutralButton
                }
                try {
                    val lastHash = prefs.getString(hashKey(game), null)
                    if (lastHash != null && (!config.exists() || sha256(config) != lastHash)) {
                        error("Configurações alteradas após o perfil S25. Exporte o INI antes de restaurar para evitar perda de mudanças.")
                    }
                    if (prefs.getBoolean(key(game), false)) {
                        val original = backup(config)
                        check(original.isFile) { "Backup original ausente; nenhuma configuração foi apagada." }
                        original.copyTo(config, overwrite = true)
                        check(original.delete()) { "Restaurado, mas não foi possível excluir a cópia auxiliar." }
                    } else if (config.exists()) {
                        check(config.delete()) { "Não foi possível remover o perfil novo." }
                    }
                    prefs.edit().remove(key(game)).remove(hashKey(game)).apply()
                    Toast.makeText(context, R.string.s25_profile_restored, Toast.LENGTH_LONG).show()
                } catch (error: Exception) {
                    android.util.Log.e("S25UltraGameTuning", "Unable to restore game", error)
                    MaterialAlertDialogBuilder(context)
                        .setTitle(R.string.s25_profile_error)
                        .setMessage(error.message ?: context.getString(R.string.s25_profile_error))
                        .setPositiveButton(android.R.string.ok, null)
                        .show()
                }
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }
}
