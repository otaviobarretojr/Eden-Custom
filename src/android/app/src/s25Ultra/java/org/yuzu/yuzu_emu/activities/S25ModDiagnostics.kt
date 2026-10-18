// SPDX-FileCopyrightText: Copyright 2026 ASTRYX contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.activities

import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.LinkOption
import java.util.Locale

/**
 * Read-only diagnostics for existing LayeredFS mods.
 * Cheat filenames MAY indicate a target Build ID, but never prove the game's
 * runtime Build ID or compatibility with its current update.
 */
internal object S25ModDiagnostics {
    data class Issue(val kind: String, val detail: String)
    data class BuildIdReference(val buildId: String, val modName: String)
    data class Report(
        val activeMods: Int,
        val disabledMods: Int,
        val activeFiles: Int,
        val conflicts: List<S25ModConflictScanner.Conflict>,
        val buildIdReferences: List<BuildIdReference>,
        val issues: List<Issue>
    )

    private val TITLE_ID = Regex("(?i)^[a-f0-9]{16}$")
    private val BUILD_ID = Regex("(?i)^[a-f0-9]{16}$")

    fun inspect(userDirectory: File, titleId: String): Report {
        require(TITLE_ID.matches(titleId)) { "Title ID inválido." }
        val activeRoot = File(userDirectory, "load/$titleId")
        val disabledRoot = File(userDirectory, "disabled_mods/$titleId")
        val active = safeFolders(activeRoot)
        val disabled = safeFolders(disabledRoot)
        val owners = mutableMapOf<String, String>()
        val conflicts = mutableListOf<S25ModConflictScanner.Conflict>()
        val references = mutableListOf<BuildIdReference>()
        val issues = mutableListOf<Issue>()
        var fileCount = 0

        active.forEach { mod ->
            val inventory = S25ModConflictScanner.inventory(mod)
            fileCount += inventory.size
            if (inventory.isEmpty()) {
                issues.add(Issue("EMPTY_MOD", "Mod '${mod.name}' sem arquivos LayeredFS reconhecidos."))
            }
            for (file in inventory) {
                val path = file.relativePath.lowercase(Locale.ROOT)
                val previous = owners.putIfAbsent(path, mod.name)
                if (previous != null && previous != mod.name) {
                    conflicts.add(S25ModConflictScanner.Conflict(file.relativePath, previous, mod.name))
                }
                val parts = file.relativePath.split('/')
                if (parts.size == 2 && parts[0].equals("cheats", ignoreCase = true) &&
                    parts[1].endsWith(".txt", ignoreCase = true)) {
                    val candidate = parts[1].substringBeforeLast('.')
                    if (BUILD_ID.matches(candidate)) {
                        references.add(BuildIdReference(candidate.uppercase(Locale.ROOT), mod.name))
                    }
                }
            }
        }
        if (conflicts.isNotEmpty()) {
            issues.add(Issue("FILE_CONFLICT", "${conflicts.size} colisão(ões) entre arquivos de mods ativos."))
        }
        val builds = references.map { it.buildId }.distinct()
        if (builds.size > 1) {
            issues.add(Issue("MULTIPLE_CHEAT_BUILD_IDS",
                "Cheats apontam para ${builds.size} Build IDs diferentes; nenhum foi confirmado como ativo."))
        }
        if (references.isNotEmpty()) {
            issues.add(Issue("BUILD_ID_UNVERIFIED",
                "Build IDs extraídos de nomes de cheats, NÃO do executável carregado pelo jogo."))
        }
        return Report(
            activeMods = active.size,
            disabledMods = disabled.size,
            activeFiles = fileCount,
            conflicts = conflicts,
            buildIdReferences = references.distinct(),
            issues = issues
        )
    }

    private fun safeFolders(root: File): List<File> {
        val path = root.toPath()
        if (!Files.exists(path, LinkOption.NOFOLLOW_LINKS)) return emptyList()
        if (!Files.isDirectory(path, LinkOption.NOFOLLOW_LINKS)) {
            throw IOException("Pasta de mods inválida: ${root.name}.")
        }
        val files = root.listFiles() ?: throw IOException("Não foi possível ler ${root.name}.")
        for (file in files) {
            if (Files.isSymbolicLink(file.toPath())) {
                throw IOException("Link simbólico detectado em ${root.name}: ${file.name}.")
            }
        }
        return files.filter { Files.isDirectory(it.toPath(), LinkOption.NOFOLLOW_LINKS) }
            .sortedBy { it.name.lowercase(Locale.ROOT) }
    }
}
