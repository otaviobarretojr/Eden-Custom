// SPDX-FileCopyrightText: Copyright 2026 ASTRYX contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.activities

import java.io.File
import java.io.IOException
import java.nio.file.FileVisitResult
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.Path
import java.nio.file.SimpleFileVisitor
import java.nio.file.attribute.BasicFileAttributes
import java.util.Locale

/** Read-only LayeredFS inventory and fail-closed conflict detection. */
internal object S25ModConflictScanner {
    private val roots = setOf("romfs", "exefs", "cheats", "romfslite", "romfs_ext")
    private const val MAX_SCANNED_FILES = 100_000

    data class ModFile(val relativePath: String, val size: Long)
    data class Conflict(val relativePath: String, val installedMod: String, val incomingMod: String)

    /** Uses NOFOLLOW_LINKS so a malicious or manually added symlink cannot escape the mod tree. */
    fun inventory(modDir: File): List<ModFile> {
        val root = modDir.toPath()
        if (!Files.isDirectory(root, LinkOption.NOFOLLOW_LINKS)) {
            throw IOException("Pasta de mod inválida: ${modDir.name}")
        }
        val files = mutableListOf<ModFile>()
        Files.walkFileTree(root, object : SimpleFileVisitor<Path>() {
            override fun visitFile(file: Path, attrs: BasicFileAttributes): FileVisitResult {
                if (!attrs.isRegularFile || attrs.isSymbolicLink) {
                    throw IOException("Arquivo especial ou link simbólico em ${modDir.name}")
                }
                val relative = root.relativize(file).toString().replace('\\', '/')
                if (relative.substringBefore('/').lowercase(Locale.ROOT) !in roots) {
                    return FileVisitResult.CONTINUE
                }
                if (files.size >= MAX_SCANNED_FILES) {
                    throw IOException("Mod excede o limite de arquivos para verificação.")
                }
                files.add(ModFile(relative, attrs.size()))
                return FileVisitResult.CONTINUE
            }

            override fun visitFileFailed(file: Path, exc: IOException): FileVisitResult = throw exc
        })
        return files.sortedBy { it.relativePath.lowercase(Locale.ROOT) }
    }

    /**
     * Compare new mods with already active mods and with each other.
     * Old, pre-existing conflicts are not modified. Case-only path differences
     * are treated conservatively as conflicts across filesystems.
     */
    fun findConflicts(activeRoot: File, incoming: List<File>): List<Conflict> {
        val owners = HashMap<String, String>()
        if (activeRoot.exists() && !activeRoot.isDirectory) {
            throw IOException("Pasta de mods ativos inválida.")
        }
        val activeMods = if (activeRoot.exists()) {
            activeRoot.listFiles() ?: throw IOException("Não foi possível ler mods ativos.")
        } else emptyArray()
        activeMods.filter { it.isDirectory }.sortedBy { it.name }.forEach { mod ->
            inventory(mod).forEach { file ->
                owners.putIfAbsent(file.relativePath.lowercase(Locale.ROOT), mod.name)
            }
        }
        val conflicts = mutableListOf<Conflict>()
        incoming.forEach { mod ->
            inventory(mod).forEach { file ->
                val key = file.relativePath.lowercase(Locale.ROOT)
                val owner = owners[key]
                if (owner != null && owner != mod.name) {
                    conflicts.add(Conflict(file.relativePath, owner, mod.name))
                } else if (owner == null) {
                    owners[key] = mod.name
                }
            }
        }
        return conflicts.distinctBy { Triple(it.relativePath.lowercase(Locale.ROOT), it.installedMod, it.incomingMod) }
    }

    fun requireNoConflicts(activeRoot: File, incoming: List<File>) {
        val collisions = findConflicts(activeRoot, incoming)
        if (collisions.isEmpty()) return
        val sample = collisions.take(3).joinToString("; ") {
            "${it.relativePath} (${it.installedMod} x ${it.incomingMod})"
        }
        throw IOException(
            "Mods conflitantes: ${collisions.size} arquivo(s) com o mesmo destino. " +
                "$sample. Desative o mod existente ou use uma versão compatível antes de continuar."
        )
    }
}
