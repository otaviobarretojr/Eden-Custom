// SPDX-FileCopyrightText: Copyright 2026 Eden S25 Ultra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.activities

import java.util.Locale

/**
 * The regular installer accepts packaged LayeredFS paths (romfs/exefs/cheats).
 * Some Pokémon Legends: Z-A translation archives instead contain the files
 * INSIDE romfs: arc/, audio/, ik_message/, ui/. Recognize that specific,
 * unambiguous structure and wrap it in romfs/ while staging. Never interpret
 * an arbitrary archive as a mod.
 */
internal class S25ModLayout private constructor(
    private val bareRoot: List<String>?
) {
    companion object {
        private const val ZA_TITLE_ID = "0100F43008C44000"
        private val MOD_ROOTS = setOf("romfs", "exefs", "cheats", "romfslite", "romfs_ext")
        private val ZA_ROMFS_DIRS = setOf("arc", "audio", "ik_message", "ui")

        fun parts(raw: String): List<String> =
            raw.replace('\\', '/').split('/').filter { it.isNotBlank() }

        fun explicitPivot(parts: List<String>): Int =
            parts.indexOfFirst { it.lowercase(Locale.ROOT) in MOD_ROOTS }

        fun detect(titleId: String, filenames: List<String>): S25ModLayout {
            if (filenames.any { name ->
                    val path = parts(name)
                    explicitPivot(path) in 0 until path.lastIndex
                }) {
                return S25ModLayout(null)
            }

            // The recognizable bare-folder layout is scoped to the correct
            // game's Title ID. Other titles still need a standard LayeredFS ZIP.
            if (!titleId.equals(ZA_TITLE_ID, ignoreCase = true)) {
                return S25ModLayout(null)
            }

            val roots = mutableMapOf<List<String>, MutableSet<String>>()
            for (name in filenames) {
                val path = parts(name).map { it.lowercase(Locale.ROOT) }
                // Only count actual files, not empty top-level directories.
                for (i in 0 until path.lastIndex) {
                    if (path[i] in ZA_ROMFS_DIRS) {
                        roots.getOrPut(path.take(i)) { mutableSetOf() }.add(path[i])
                        break
                    }
                }
            }
            val valid = roots.filter { (_, dirs) ->
                dirs.size >= 3 && "arc" in dirs && "ik_message" in dirs
            }.keys.toList()
            if (valid.size > 1) {
                error(
                    "Este pacote contém mais de uma variante RomFS do Pokémon Z-A. " +
                        "Compacte somente a pasta da versão desejada em ZIP e tente novamente."
                )
            }
            return S25ModLayout(valid.singleOrNull())
        }
    }

    val isBareRomfs: Boolean get() = bareRoot != null

    /**
     * Returns a LayeredFS-relative path. Null means documentation or other
     * non-mod material, which the installer must skip without writing.
     */
    fun relativePath(raw: String): String? {
        val path = parts(raw)
        val root = bareRoot
        if (root == null) {
            val pivot = explicitPivot(path)
            if (pivot < 0 || pivot >= path.lastIndex) return null
            return path.drop(pivot).joinToString("/")
        }
        if (path.size <= root.size + 1 ||
            path.take(root.size).map { it.lowercase(Locale.ROOT) } != root ||
            path[root.size].lowercase(Locale.ROOT) !in setOf("arc", "audio", "ik_message", "ui")
        ) {
            return null
        }
        return "romfs/" + path.drop(root.size).joinToString("/")
    }
}
