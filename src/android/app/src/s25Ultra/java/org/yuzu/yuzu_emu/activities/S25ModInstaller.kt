// SPDX-FileCopyrightText: Copyright 2026 Eden S25 Ultra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.activities

import android.content.Context
import org.yuzu.yuzu_emu.utils.DirectoryInitialization
import java.io.File
import java.util.Locale
import java.util.zip.ZipFile
import java.util.zip.ZipInputStream

/** Download ZIP only; stage/validate before moving into the game's mod folder. */
internal object S25ModInstaller {
    private const val MAX_DOWNLOAD = S25ArchiveLimits.MAX_ARCHIVE_BYTES
    private const val MAX_UNPACKED = S25ArchiveLimits.MAX_EXPANDED_BYTES
    private const val MAX_ENTRIES = S25ArchiveLimits.MAX_ENTRIES
    private val MOD_ROOTS = setOf("romfs", "exefs", "cheats", "romfslite", "romfs_ext")
    private val FORBIDDEN_EXT = Regex("(?i)\\.(apk|exe|dll|so|dex|nsp|nsz|xci|xcz|nca|nro|bat|cmd|ps1|sh)$")
    private val TITLE_ID = Regex("(?i)^[a-f0-9]{16}$")

    fun download(
        context: Context, url: String, progress: (String) -> Unit,
        expectedSizeBytes: Long? = null, expectedSha256: String? = null
    ): File {
        require(S25ModCatalog.isApprovedUrl(url)) { "Host de download não autorizado." }
        val verifier = S25DownloadVerifier(expectedSizeBytes, expectedSha256)
        val target = File.createTempFile("eden-mod-", ".zip", context.cacheDir)
        val connection = S25ModCatalog.connect(url)
        try {
            if (connection.contentLengthLong > MAX_DOWNLOAD) error(S25ArchiveLimits.archiveSizeError())
            if (expectedSizeBytes != null && connection.contentLengthLong >= 0 &&
                connection.contentLengthLong != expectedSizeBytes) {
                error("Tamanho informado pelo servidor não corresponde ao release do autor.")
            }
            S25ArchiveLimits.ensureAvailableSpace(
                context.cacheDir, connection.contentLengthLong.coerceAtLeast(0L)
            )
            connection.inputStream.use { input ->
                target.outputStream().buffered().use { output ->
                    var total = 0L
                    var milestone = 0L
                    var lastSpaceCheck = 0L
                    val buffer = ByteArray(32768)
                    while (true) {
                        val n = input.read(buffer)
                        if (n < 0) break
                        verifier.accept(buffer, n)
                        total += n
                        if (S25ArchiveLimits.shouldCheckSpace(total, lastSpaceCheck)) {
                            S25ArchiveLimits.ensureAvailableSpace(context.cacheDir, n.toLong())
                            lastSpaceCheck = total
                        }
                        output.write(buffer, 0, n)
                        if (total - milestone >= 2L * 1024 * 1024) {
                            progress("Baixando: ${total / (1024 * 1024)} MiB")
                            milestone = total
                        }
                    }
                    if (verifier.verify()) {
                        progress("SHA-256 conferido com o release do autor.")
                    }
                }
            }
            target.inputStream().use { input ->
                val magic = ByteArray(4)
                if (input.read(magic) != 4 ||
                    magic[0] != 0x50.toByte() || magic[1] != 0x4b.toByte() ||
                    magic[2] != 0x03.toByte() || magic[3] != 0x04.toByte()) {
                    error("O servidor não enviou um ZIP válido.")
                }
            }
            return target
        } catch (ex: Exception) {
            target.delete()
            throw ex
        } finally {
            connection.disconnect()
        }
    }

    fun install(
        context: Context, archive: File, titleId: String, offerName: String,
        progress: (String) -> Unit
    ): List<String> {
        require(TITLE_ID.matches(titleId)) { "Title ID do jogo inválido." }
        check(DirectoryInitialization.areDirectoriesReady) { "Pasta de dados ainda não inicializada." }
        val userPath = DirectoryInitialization.userDirectory ?: error("Pasta do Eden indisponível.")
        val installRoot = File(userPath, "load/$titleId")
        val staging = File(userPath, ".eden-mod-stage-${System.nanoTime()}")
        require(staging.mkdirs()) { "Não foi possível preparar a extração." }
        val stagedDirs = linkedSetOf<String>()
        val installed = mutableListOf<File>()
        val recorded = mutableListOf<File>()
        try {
            S25ArchiveLimits.ensureAvailableSpace(staging)
            // ZIP central-directory inspection does not decompress large files.
            // It lets us detect nested/bare RomFS once, before extraction.
            val layout = ZipFile(archive).use { zipFile ->
                val names = mutableListOf<String>()
                val items = zipFile.entries()
                while (items.hasMoreElements()) {
                    val item = items.nextElement()
                    if (names.size >= MAX_ENTRIES) error("ZIP contém arquivos demais.")
                    val raw = item.name.replace('\\', '/')
                    if (raw.startsWith('/') || raw.contains('\u0000') ||
                        raw.any { it.code in 1..31 }) {
                        error("Caminho ZIP inválido.")
                    }
                    val parts = S25ModLayout.parts(raw)
                    if (parts.any { it == "." || it == ".." || it.contains(':') }) {
                        error("Caminho ZIP inseguro.")
                    }
                    if (parts.any { TITLE_ID.matches(it) && !it.equals(titleId, true) }) {
                        error("O pacote contém um Title ID diferente do jogo.")
                    }
                    names.add(raw)
                }
                S25ModLayout.detect(titleId, names)
            }
            if (layout.isBareRomfs) {
                progress("Estrutura RomFS do Pokémon Z-A detectada. Preparando pastas…")
            }
            var total = 0L
            var lastSpaceCheck = 0L
            var entries = 0
            var files = 0
            ZipInputStream(archive.inputStream().buffered()).use { zip ->
                while (true) {
                    val entry = zip.nextEntry ?: break
                    entries++
                    if (entries > MAX_ENTRIES) error("ZIP contém arquivos demais.")
                    val raw = entry.name.replace('\\', '/')
                    if (raw.startsWith('/') || raw.contains('\u0000')) error("Caminho ZIP inválido.")
                    val pieces = raw.split('/').filter { it.isNotBlank() }
                    if (pieces.any { it == "." || it == ".." || it.contains(':') }) error("Caminho ZIP inseguro.")
                    if (pieces.any { TITLE_ID.matches(it) && !it.equals(titleId, true) }) {
                        error("O pacote contém um Title ID diferente do jogo.")
                    }
                    val suffix = layout.relativePath(raw)
                    if (!entry.isDirectory && suffix != null) {
                        val pivot = S25ModLayout.explicitPivot(pieces)
                        val parent = pieces.getOrNull(pivot - 1).orEmpty()
                        val candidate = if (layout.isBareRomfs || pivot == 0 ||
                            TITLE_ID.matches(parent) || parent in setOf("mods", "contents", "load")
                        ) offerName else parent
                        val modName = safeName(candidate)
                        val out = File(staging, "$modName/$suffix")
                        val rootCanonical = staging.canonicalPath + File.separator
                        if (!out.canonicalPath.startsWith(rootCanonical)) error("Caminho fora da pasta temporária.")
                        if (FORBIDDEN_EXT.containsMatchIn(out.name)) error("Arquivo não permitido em mods: ${out.name}")
                        if (entry.size > MAX_UNPACKED) error(S25ArchiveLimits.extractedSizeError())
                        if (stagedDirs.size >= 30 && modName !in stagedDirs) error("Pacote com mods demais.")
                        stagedDirs.add(modName)
                        require(out.parentFile?.isDirectory == true || out.parentFile?.mkdirs() == true)
                        if (out.exists()) error("Entradas duplicadas no pacote.")
                        out.outputStream().buffered().use { output ->
                            val buffer = ByteArray(32768)
                            while (true) {
                                val n = zip.read(buffer)
                                if (n < 0) break
                                total += n
                                if (total > MAX_UNPACKED) error(S25ArchiveLimits.extractedSizeError())
                                if (S25ArchiveLimits.shouldCheckSpace(total, lastSpaceCheck)) {
                                    S25ArchiveLimits.ensureAvailableSpace(staging, n.toLong())
                                    lastSpaceCheck = total
                                }
                                output.write(buffer, 0, n)
                            }
                        }
                        files++
                        if (files % 25 == 0) progress("Validando e extraindo $files arquivos…")
                    }
                    zip.closeEntry()
                }
            }
            if (stagedDirs.isEmpty() || files == 0) {
                error(
                    "ZIP sem estrutura LayeredFS reconhecida. Pokémon Z-A aceita romfs/ " +
                        "ou as pastas arc/, audio/, ik_message/ e ui/ juntas em uma variante. " +
                        "Confira se compactou a pasta correta."
                )
            }
            if (!installRoot.isDirectory && !installRoot.mkdirs()) error("Pasta de mods inacessível.")
            val plans = stagedDirs.map { name -> File(staging, name) to File(installRoot, name) }
            plans.forEach { (_, destination) ->
                if (destination.exists()) error("Mod '${destination.name}' já existe; não sobrescrito.")
            }
            progress("Verificando conflitos entre mods ativos e novos…")
            S25ModConflictScanner.requireNoConflicts(installRoot, plans.map { it.first })
            for ((source, destination) in plans) {
                if (!source.renameTo(destination)) error("Não foi possível instalar '${destination.name}'.")
                installed.add(destination)
            }
            recorded.addAll(S25ModRegistry.record(userPath, titleId, installed))
            progress("Instalação concluída: ${installed.size} mod(s). Reinicie o jogo.")
            return installed.map { it.name }
        } catch (ex: Exception) {
            recorded.forEach { it.delete() }
            // Only newly placed mod folders are removed, never existing content.
            installed.forEach { it.deleteRecursively() }
            throw ex
        } finally {
            staging.deleteRecursively()
        }
    }

    fun installed(titleId: String): Pair<List<String>, List<String>> {
        require(TITLE_ID.matches(titleId))
        val userPath = DirectoryInitialization.userDirectory ?: return emptyList<String>() to emptyList()
        val active = File(userPath, "load/$titleId").listFiles()
            ?.filter { it.isDirectory && hasAddonContent(it) }
            ?.map { it.name }?.sorted() ?: emptyList()
        val disabled = File(userPath, "disabled_mods/$titleId").listFiles()
            ?.filter { it.isDirectory && hasAddonContent(it) }
            ?.map { it.name }?.sorted() ?: emptyList()
        return active to disabled
    }

    fun toggle(titleId: String, name: String, enable: Boolean) {
        require(TITLE_ID.matches(titleId))
        require(safeName(name) == name) { "Nome do mod inválido." }
        val root = DirectoryInitialization.userDirectory ?: error("Diretório não inicializado.")
        val source = File(root, (if (enable) "disabled_mods/" else "load/") + "$titleId/$name")
        val destination = File(root, (if (enable) "load/" else "disabled_mods/") + "$titleId/$name")
        if (!source.isDirectory || !hasAddonContent(source)) error("Mod não encontrado.")
        if (destination.exists()) error("Já existe um mod de mesmo nome no destino.")
        if (enable) {
            S25ModConflictScanner.requireNoConflicts(File(root, "load/$titleId"), listOf(source))
        }
        require(destination.parentFile?.isDirectory == true || destination.parentFile?.mkdirs() == true)
        if (!source.renameTo(destination)) error("Não foi possível mover o mod.")
    }

    private fun hasAddonContent(dir: File): Boolean =
        dir.listFiles()?.any { it.isDirectory && it.name.lowercase(Locale.ROOT) in MOD_ROOTS } == true

    private fun safeName(original: String): String {
        val safe = original.replace(Regex("[^\\p{L}\\p{N} ._+()-]"), "_")
            .trim().trim('.').take(75)
        require(safe.isNotBlank() && safe != "." && safe != "..") { "Nome inválido no ZIP." }
        return safe
    }
}
