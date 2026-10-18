// SPDX-FileCopyrightText: Copyright 2026 ASTRYX contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.activities

import java.security.MessageDigest

/**
 * Streaming verification, independent of Android and network code.
 * Only a publisher-supplied SHA-256 can authenticate downloaded bytes.
 */
internal class S25DownloadVerifier(
    private val expectedSize: Long?,
    expectedSha256: String?
) {
    private val expectedDigest = expectedSha256?.lowercase()
    private val digest = if (expectedDigest != null) MessageDigest.getInstance("SHA-256") else null

    var totalBytes: Long = 0L
        private set

    init {
        require(expectedSize == null || expectedSize in 1..S25ArchiveLimits.MAX_ARCHIVE_BYTES) {
            "Tamanho esperado do download inválido."
        }
        require(expectedDigest == null || Regex("^[a-f0-9]{64}$").matches(expectedDigest)) {
            "SHA-256 esperado inválido."
        }
    }

    fun accept(bytes: ByteArray, count: Int) {
        require(count >= 0 && count <= bytes.size) { "Quantidade de bytes inválida." }
        if (count.toLong() > S25ArchiveLimits.MAX_ARCHIVE_BYTES - totalBytes) {
            error(S25ArchiveLimits.archiveSizeError())
        }
        totalBytes += count
        digest?.update(bytes, 0, count)
    }

    /** Returns true only if the exact publisher-provided SHA-256 was checked. */
    fun verify(): Boolean {
        if (totalBytes < 22L) error("Arquivo ZIP vazio ou incompleto.")
        if (expectedSize != null && totalBytes != expectedSize) {
            error("Download incompleto: recebido $totalBytes bytes; esperado $expectedSize.")
        }
        if (digest == null) return false
        val actual = digest.digest().joinToString("") {
            (it.toInt() and 0xff).toString(16).padStart(2, '0')
        }
        if (actual != expectedDigest) error("SHA-256 divergente: arquivo diferente do release.")
        return true
    }
}
