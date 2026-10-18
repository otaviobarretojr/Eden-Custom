// SPDX-FileCopyrightText: Copyright 2026 Eden S25 Ultra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.activities

import java.io.File
import java.io.IOException

/**
 * Shared hard limits for untrusted community archives. Downloads, local imports,
 * RAR-to-ZIP conversion and ZIP extraction all use bounded streaming IO, not a
 * giant ByteArray in memory. The limits prevent zip bombs and filling userdata.
 */
internal object S25ArchiveLimits {
    const val MAX_ARCHIVE_BYTES = 2L * 1024 * 1024 * 1024 // 2 GiB
    const val MAX_EXPANDED_BYTES = 6L * 1024 * 1024 * 1024 // 6 GiB
    const val MAX_ENTRIES = 50_000
    private const val RESERVED_BYTES = 512L * 1024 * 1024 // leave 512 MiB free
    private const val CHECK_INTERVAL_BYTES = 8L * 1024 * 1024

    fun ensureAvailableSpace(directory: File, upcomingBytes: Long = 0L) {
        val available = directory.usableSpace
        if (available < RESERVED_BYTES || upcomingBytes > available - RESERVED_BYTES) {
            throw IOException(
                "Espaço insuficiente para o pacote. Libere armazenamento no celular " +
                    "e mantenha pelo menos 512 MiB livres após a instalação."
            )
        }
    }

    fun shouldCheckSpace(bytesWritten: Long, previousCheck: Long): Boolean =
        bytesWritten - previousCheck >= CHECK_INTERVAL_BYTES

    fun archiveSizeError(): String = "Arquivo excede o limite de 2 GiB compactados."
    fun extractedSizeError(): String = "Pacote excede o limite de 6 GiB extraídos."
}
