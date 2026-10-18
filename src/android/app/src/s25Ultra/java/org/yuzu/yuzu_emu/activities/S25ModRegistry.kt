// SPDX-FileCopyrightText: Copyright 2026 ASTRYX contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.activities

import java.io.File
import java.io.IOException
import java.nio.charset.StandardCharsets
import java.util.Properties
import java.util.UUID

/** Append-only installation inventory, stored outside the emulator's load and save trees. */
internal object S25ModRegistry {
    fun record(userDirectory: File, titleId: String, installedMods: List<File>): List<File> {
        require(Regex("(?i)^[a-f0-9]{16}$").matches(titleId))
        val registry = File(userDirectory, "astryx/mod-registry/$titleId")
        if (!registry.isDirectory && !registry.mkdirs()) throw IOException("Registro de mods inacessível.")

        val staged = mutableListOf<Pair<File, File>>()
        val committed = mutableListOf<File>()
        try {
            installedMods.forEach { mod ->
                val inventory = S25ModConflictScanner.inventory(mod)
                val properties = Properties().apply {
                    setProperty("schema", "1")
                    setProperty("titleId", titleId.uppercase())
                    setProperty("modName", mod.name)
                    setProperty("installedAtEpochMs", System.currentTimeMillis().toString())
                    setProperty("fileCount", inventory.size.toString())
                    inventory.forEachIndexed { index, file ->
                        setProperty("file.$index.path", file.relativePath)
                        setProperty("file.$index.bytes", file.size.toString())
                    }
                }
                val temp = File.createTempFile(".astryx-mod-", ".tmp", registry)
                staged.add(temp to File(registry, "${mod.name}-${UUID.randomUUID()}.properties"))
                temp.outputStream().bufferedWriter(StandardCharsets.UTF_8).use { writer ->
                    properties.store(writer, "ASTRYX mod installation inventory")
                }
            }
            staged.forEach { (temp, final) ->
                if (final.exists() || !temp.renameTo(final)) throw IOException("Falha ao registrar instalação de mod.")
                committed.add(final)
            }
            return committed.toList()
        } catch (error: Exception) {
            committed.forEach { it.delete() }
            throw error
        } finally {
            staged.forEach { (temp, _) -> temp.delete() }
        }
    }
}
