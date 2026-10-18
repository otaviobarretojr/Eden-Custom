// SPDX-FileCopyrightText: Copyright 2026 ASTRYX contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.activities

import java.util.Locale

/**
 * A game library's display version is not necessarily the installed update's
 * effective version. Never claim tested compatibility from metadata alone.
 */
internal object S25ModCompatibility {
    enum class Status { NO_REQUIREMENT, UNKNOWN_VERSION, VERSION_MATCHES, VERSION_MISMATCH }

    data class Assessment(val status: Status, val description: String)

    private val dotted = Regex("^\\d{1,6}(?:\\.\\d{1,6}){1,3}$")
    private val contentVersion = Regex("^v\\d{1,10}$")

    /** Compare normalized dotted versions, or content versions only within their own notation. */
    fun assess(reportedVersion: String?, requiredVersion: String?): Assessment {
        if (requiredVersion.isNullOrBlank()) {
            return Assessment(Status.NO_REQUIREMENT, "Versão exigida não informada pelo autor; compatibilidade não verificada.")
        }
        val required = normalize(requiredVersion)
        val reported = normalize(reportedVersion)
        if (required == null) {
            return Assessment(Status.UNKNOWN_VERSION, "Requisito de versão não reconhecido: $requiredVersion.")
        }
        if (reported == null) {
            return Assessment(Status.UNKNOWN_VERSION,
                "O patch exige $requiredVersion, mas a versão do jogo não está confirmada. Confira a atualização no emulador.")
        }
        if (reported == required) {
            return Assessment(Status.VERSION_MATCHES,
                "A versão informada pela biblioteca ($reportedVersion) coincide com o requisito $requiredVersion; ainda não é um teste de compatibilidade.")
        }
        return Assessment(Status.VERSION_MISMATCH,
            "Possível incompatibilidade: biblioteca mostra $reportedVersion; o patch exige $requiredVersion. Confirme a versão efetiva do jogo.")
    }

    private fun normalize(raw: String?): String? {
        val value = raw?.trim()?.lowercase(Locale.ROOT)?.takeIf { it.isNotEmpty() } ?: return null
        if (contentVersion.matches(value)) return value
        val dottedText = value.removePrefix("v")
        if (!dotted.matches(dottedText)) return null
        return dottedText.split('.').joinToString(".") { it.toLong().toString() }
    }

    /** Accept only GitHub's optional sha256 digest, never a git blob SHA or an arbitrary label. */
    fun parseGitHubDigest(raw: String?): String? {
        val value = raw?.trim() ?: return null
        val match = Regex("^sha256:([a-fA-F0-9]{64})$").matchEntire(value) ?: return null
        return match.groupValues[1].lowercase(Locale.ROOT)
    }
}
