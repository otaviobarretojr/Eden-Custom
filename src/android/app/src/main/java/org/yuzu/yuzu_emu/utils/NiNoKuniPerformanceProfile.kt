// SPDX-FileCopyrightText: Copyright 2026 ASTRYX contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import android.content.Context
import org.yuzu.yuzu_emu.features.settings.model.BooleanSetting
import org.yuzu.yuzu_emu.features.settings.model.IntSetting
import org.yuzu.yuzu_emu.features.settings.utils.SettingsFile
import org.yuzu.yuzu_emu.model.Game

/**
 * Explicit, reversible Ni no Kuni rendering profiles on Android.
 * This does not unlock the original Switch game's 30 FPS cap.
 * ResolutionSetup::Res3_4X is 2; ResolutionSetup::Res1X is 3.
 */
object NiNoKuniPerformanceProfile {
    private const val TITLE = "0100E5600D446000"
    private const val PREFS = "astryx_ni_no_kuni_native_fps"
    private const val MODE = "profile_mode"
    private const val BACKUP = "backup_valid"
    private const val RESOLUTION = "prior_resolution"
    private const val DOCKED = "prior_docked"
    private const val FRAME_GEN = "prior_frame_gen"
    private const val SHADER_CACHE = "prior_shader_cache"

    const val ORIGINAL = 0
    const val STABLE_1X = 1
    const val PERFORMANCE_075X = 2

    fun supports(game: Game?): Boolean = game?.programIdHex == TITLE

    fun current(context: Context): Int =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getInt(MODE, ORIGINAL)

    /**
     * Save only this title's configuration and snapshot the original settings.
     * A game restart is necessary for the renderer to pick up the new profile.
     */
    fun apply(context: Context, game: Game, requested: Int): Boolean {
        if (!supports(game) || requested !in ORIGINAL..PERFORMANCE_075X) {
            return false
        }

        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        if (requested == ORIGINAL && !prefs.getBoolean(BACKUP, false)) {
            return true
        }

        val owned = !NativeConfig.isPerGameConfigLoaded()
        try {
            if (owned) {
                SettingsFile.loadCustomConfig(game)
            }
            if (requested != ORIGINAL && !prefs.getBoolean(BACKUP, false)) {
                val saved = prefs.edit()
                    .putBoolean(BACKUP, true)
                    .putInt(RESOLUTION, IntSetting.RENDERER_RESOLUTION.getInt())
                    .putBoolean(DOCKED, BooleanSetting.USE_DOCKED_MODE.getBoolean())
                    .putBoolean(FRAME_GEN, BooleanSetting.RENDERER_FRAME_GEN.getBoolean())
                    .putBoolean(
                        SHADER_CACHE,
                        BooleanSetting.RENDERER_USE_DISK_SHADER_CACHE.getBoolean()
                    )
                    .commit()
                if (!saved) {
                    Log.error("[ASTRYX FPS Profile] Could not save configuration backup")
                    return false
                }
            }

            if (requested == ORIGINAL) {
                IntSetting.RENDERER_RESOLUTION.setInt(prefs.getInt(RESOLUTION, 3))
                BooleanSetting.USE_DOCKED_MODE.setBoolean(prefs.getBoolean(DOCKED, false))
                BooleanSetting.RENDERER_FRAME_GEN.setBoolean(
                    prefs.getBoolean(FRAME_GEN, false)
                )
                BooleanSetting.RENDERER_USE_DISK_SHADER_CACHE.setBoolean(
                    prefs.getBoolean(SHADER_CACHE, true)
                )
            } else {
                // Reduce genuine GPU work, not merely display interpolated frames.
                IntSetting.RENDERER_RESOLUTION.setInt(
                    if (requested == PERFORMANCE_075X) 2 else 3
                )
                BooleanSetting.USE_DOCKED_MODE.setBoolean(false)
                BooleanSetting.RENDERER_FRAME_GEN.setBoolean(false)
                BooleanSetting.RENDERER_USE_DISK_SHADER_CACHE.setBoolean(true)
            }
            NativeConfig.savePerGameConfig()

            val editor = prefs.edit()
            if (requested == ORIGINAL) {
                editor.remove(MODE)
                    .remove(BACKUP)
                    .remove(RESOLUTION)
                    .remove(DOCKED)
                    .remove(FRAME_GEN)
                    .remove(SHADER_CACHE)
            } else {
                editor.putInt(MODE, requested)
            }
            editor.apply()
            Log.info("[ASTRYX FPS Profile] profile=$requested applied=true " +
                     "game=0100E5600D446000 requires_restart=true " +
                     "frame_generation=disabled_or_restored")
            return true
        } catch (e: Exception) {
            Log.error("[ASTRYX FPS Profile] failed: " + e.message)
            return false
        } finally {
            if (owned && NativeConfig.isPerGameConfigLoaded()) {
                NativeConfig.unloadPerGameConfig()
            }
        }
    }
}
