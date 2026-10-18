// SPDX-FileCopyrightText: Copyright 2026 ASTRYX contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.activities

import java.io.File
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.util.Locale
import java.util.Properties

/**
 * Holds only the last observed *main NSO* identity from a successful load.
 * A saved observation is historical, never proof of a currently running game.
 * Does not read or write game saves, game data, updates, or mod files.
 */
internal object S25BuildIdStore {
    data class Observed(
        val titleId: String,
        val fullBuildId: String,
        val capturedAtEpochMs: Long
    ) {
        val cheatBuildId: String get() = fullBuildId.take(16)
    }

    private val identityFormat = Regex("(?i)^([0-9a-f]{16}):([0-9a-f]{64})$")
    private val titleFormat = Regex("(?i)^[0-9a-f]{16}$")

    @JvmStatic
    fun capture(directoryPath: String, identity: String): Boolean {
        val match = identityFormat.matchEntire(identity) ?: return false
        val title = match.groupValues[1].uppercase(Locale.ROOT)
        val full = match.groupValues[2].uppercase(Locale.ROOT)
        if (full.all { it == '0' }) return false
        val destinationRoot = File(directoryPath, "astryx/observed-build-ids")
        if (!destinationRoot.isDirectory && !destinationRoot.mkdirs()) return false
        val destination = File(destinationRoot, "$title.properties")
        val temporary = File.createTempFile(".build-id-", ".tmp", destinationRoot)
        try {
            val properties = Properties().apply {
                setProperty("schema", "1")
                setProperty("titleId", title)
                setProperty("fullBuildId", full)
                setProperty("capturedAtEpochMs", System.currentTimeMillis().toString())
                setProperty("source", "LOADED_MAIN_NSO_HEADER")
            }
            temporary.outputStream().buffered().use { properties.store(it, "ASTRYX observed main NSO Build ID") }
            try {
                Files.move(temporary.toPath(), destination.toPath(),
                    StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
            } catch (_: java.nio.file.AtomicMoveNotSupportedException) {
                Files.move(temporary.toPath(), destination.toPath(), StandardCopyOption.REPLACE_EXISTING)
            }
            return true
        } finally {
            temporary.delete()
        }
    }

    fun read(directoryPath: String, titleId: String): Observed? {
        if (!titleFormat.matches(titleId)) return null
        val file = File(directoryPath, "astryx/observed-build-ids/${titleId.uppercase(Locale.ROOT)}.properties")
        if (!file.isFile) return null
        return runCatching {
            val values = Properties().apply {
                file.inputStream().buffered().use { input -> load(input) }
            }
            val id = values.getProperty("titleId").orEmpty()
            val full = values.getProperty("fullBuildId").orEmpty()
            val timestamp = values.getProperty("capturedAtEpochMs").toLongOrNull() ?: return null
            if (!titleFormat.matches(id) || !id.equals(titleId, ignoreCase = true) ||
                !Regex("(?i)^[a-f0-9]{64}$").matches(full) ||
                values.getProperty("source") != "LOADED_MAIN_NSO_HEADER" || timestamp <= 0
            ) return null
            Observed(id.uppercase(Locale.ROOT), full.uppercase(Locale.ROOT), timestamp)
        }.getOrNull()
    }
}
