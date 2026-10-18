// SPDX-FileCopyrightText: Copyright 2026 Eden S25 Ultra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.activities

import android.content.Context
import com.github.junrar.Archive
import java.io.File
import java.io.FileInputStream
import java.util.Locale
import java.util.zip.Deflater
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/**
 * Safely repack RAR4/RAR5 translation archives to a temporary ZIP accepted by
 * the existing mod installer. This is decompression ONLY: no RAR creation.
 *
 * Junrar / UnRAR licensing: RAR decompression code must not be used to develop
 * any RAR-compatible compression implementation.
 *
 * No archive contents are written to their user-supplied paths. All data goes
 * to a temporary ZIP, and S25ModInstaller then enforces destination safety,
 * atomic staging, extension rules and collision/rollback protections.
 */
internal object S25RarBridge {
    private const val MAX_COMPRESSED_BYTES = S25ArchiveLimits.MAX_ARCHIVE_BYTES
    private const val MAX_EXPANDED_BYTES = S25ArchiveLimits.MAX_EXPANDED_BYTES
    private const val MAX_ENTRIES = S25ArchiveLimits.MAX_ENTRIES
    private val MOD_ROOTS = setOf("romfs", "exefs", "cheats", "romfslite", "romfs_ext")
    private val TITLE_ID = Regex("(?i)^[0-9a-f]{16}$")
    private val SIGNATURE_RAR4 = byteArrayOf(
        0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x00
    )
    private val SIGNATURE_RAR5 = byteArrayOf(
        0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x01, 0x00
    )

    /**
     * @return newly generated temporary ZIP; caller must delete it in finally.
     * Files such as readme/tutorias outside mod folders are intentionally
     * discarded. RAR5 symbolic links/redirections and multi-part archives
     * are rejected to avoid unexpected reads.
     */
    fun convert(
        context: Context,
        rarFile: File,
        titleId: String,
        progress: (String) -> Unit
    ): File {
        require(TITLE_ID.matches(titleId)) { "Title ID do jogo inválido." }
        require(rarFile.length() in 8..MAX_COMPRESSED_BYTES) {
            "RAR inválido ou acima do limite de 2 GiB."
        }
        S25ArchiveLimits.ensureAvailableSpace(context.cacheDir)
        verifySignature(rarFile)
        val converted = File.createTempFile("eden-rar-to-zip-", ".zip", context.cacheDir)
        try {
            var entryCount = 0
            var selectedFiles = 0
            var uncompressedTotal = 0L
            var lastSpaceCheck = 0L
            val names = hashSetOf<String>()
            Archive(rarFile).use { archive ->
                ZipOutputStream(converted.outputStream().buffered()).use { zip ->
                    zip.setLevel(Deflater.BEST_SPEED)
                    for (header in archive.fileHeaders) {
                        entryCount++
                        if (entryCount > MAX_ENTRIES) {
                            error("RAR contém entradas demais (máximo $MAX_ENTRIES).")
                        }
                        val raw = header.fileName.replace('\\', '/')
                        validateEntryName(raw, titleId)
                        if (header.redirection != null) {
                            error("Links e redirecionamentos no RAR não são permitidos.")
                        }
                        if (header.isEncrypted) {
                            error("RAR protegido por senha não é suportado.")
                        }
                        if (header.isSplitAfter || header.isSplitBefore) {
                            error("RAR multipartes não é suportado nesta versão.")
                        }
                        if (header.isDirectory) continue

                        val parts = raw.split('/').filter { it.isNotBlank() }
                        val pivot = parts.indexOfFirst {
                            it.lowercase(Locale.ROOT) in MOD_ROOTS
                        }
                        // Ignore documentation/readme outside valid mod folders.
                        if (pivot < 0 || pivot >= parts.lastIndex) continue

                        val claimed = header.fullUnpackSize
                        if (claimed < 0 || claimed > MAX_EXPANDED_BYTES ||
                            uncompressedTotal > MAX_EXPANDED_BYTES - claimed) {
                            error(S25ArchiveLimits.extractedSizeError())
                        }
                        if (!names.add(raw.lowercase(Locale.ROOT))) {
                            error("RAR contém caminhos duplicados.")
                        }

                        zip.putNextEntry(ZipEntry(raw))
                        archive.getInputStream(header).use { source ->
                            val buffer = ByteArray(32768)
                            while (true) {
                                val read = source.read(buffer)
                                if (read < 0) break
                                uncompressedTotal += read
                                if (uncompressedTotal > MAX_EXPANDED_BYTES) {
                                    error(S25ArchiveLimits.extractedSizeError())
                                }
                                if (S25ArchiveLimits.shouldCheckSpace(uncompressedTotal, lastSpaceCheck)) {
                                    S25ArchiveLimits.ensureAvailableSpace(context.cacheDir, read.toLong())
                                    lastSpaceCheck = uncompressedTotal
                                }
                                zip.write(buffer, 0, read)
                            }
                        }
                        zip.closeEntry()
                        selectedFiles++
                        if (selectedFiles % 10 == 0) {
                            progress("Extraindo RAR: $selectedFiles arquivos validados…")
                        }
                    }
                }
            }
            if (selectedFiles == 0) {
                error("RAR não contém pastas romfs, exefs ou cheats para este jogo.")
            }
            progress("RAR extraído com segurança: $selectedFiles arquivos. Validando mods…")
            return converted
        } catch (ex: Exception) {
            converted.delete()
            throw ex
        }
    }

    private fun validateEntryName(raw: String, titleId: String) {
        if (raw.isEmpty() || raw.startsWith('/') ||
            raw.contains('\u0000') || raw.contains('\n') ||
            raw.any { it.code in 1..31 }) {
            error("Caminho inválido no RAR.")
        }
        val components = raw.split('/').filter { it.isNotBlank() }
        if (components.any { it == "." || it == ".." || it.contains(':') }) {
            error("RAR contém caminho inseguro.")
        }
        if (components.any { TITLE_ID.matches(it) && !it.equals(titleId, true) }) {
            error("A tradução RAR pertence a outro jogo (Title ID divergente).")
        }
    }

    private fun verifySignature(file: File) {
        val bytes = ByteArray(8)
        val read = FileInputStream(file).use { it.read(bytes) }
        val rar4 = read >= 7 && bytes.copyOf(7).contentEquals(SIGNATURE_RAR4)
        val rar5 = read == 8 && bytes.contentEquals(SIGNATURE_RAR5)
        if (!rar4 && !rar5) error("O arquivo selecionado não é um RAR 4/5 válido.")
    }
}
