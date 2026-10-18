// Pure JVM smoke tests; run with kotlinc on the two production files.
package org.yuzu.yuzu_emu.activities

import java.nio.file.Files
import java.util.Properties

fun main() {
    val home = Files.createTempDirectory("astryx-mod-tests").toFile()
    try {
        val active = home.resolve("load/0100F43008C44000").also { it.mkdirs() }
        val old = active.resolve("English").also { it.mkdirs() }
        old.resolve("romfs/ui/phrase.txt").apply { parentFile.mkdirs(); writeText("hello") }
        val newOne = home.resolve("stage/Portuguese").also { it.mkdirs() }
        newOne.resolve("romfs/ui/phrase.txt").apply { parentFile.mkdirs(); writeText("oi") }
        val list = S25ModConflictScanner.findConflicts(active, listOf(newOne))
        check(list.size == 1 && list[0].installedMod == "English")
        check(list[0].relativePath == "romfs/ui/phrase.txt")
        check(runCatching { S25ModConflictScanner.requireNoConflicts(active, listOf(newOne)) }.isFailure)

        newOne.resolve("romfs/ui/phrase.txt").delete()
        newOne.resolve("romfs/arc/item.bin").apply { parentFile.mkdirs(); writeText("mod") }
        S25ModConflictScanner.requireNoConflicts(active, listOf(newOne))
        val sibling = home.resolve("stage2/Other").also { it.mkdirs() }
        sibling.resolve("romfs/ARC/item.bin").apply { parentFile.mkdirs(); writeText("other") }
        check(S25ModConflictScanner.findConflicts(active, listOf(newOne, sibling)).size == 1)

        val manifests = S25ModRegistry.record(home, "0100F43008C44000", listOf(newOne))
        check(manifests.size == 1 && manifests[0].isFile)
        val properties = Properties()
        manifests[0].inputStream().use { properties.load(it) }
        check(properties.getProperty("fileCount") == "1")
        check(properties.getProperty("file.0.path") == "romfs/arc/item.bin")
        check(properties.getProperty("modName") == "Portuguese")

        val disabled = home.resolve("disabled_mods/0100F43008C44000/English").also { it.mkdirs() }
        disabled.resolve("romfs/arc/item.bin").apply { parentFile.mkdirs(); writeText("other") }
        check(S25ModConflictScanner.findConflicts(active, listOf(newOne)).isEmpty())
        check(S25ModConflictScanner.findConflicts(active, listOf(disabled, newOne)).size == 1)

        val symlink = newOne.resolve("romfs/traversal")
        val linkSupported = runCatching { Files.createSymbolicLink(symlink.toPath(), home.toPath()) }.isSuccess
        if (linkSupported) check(runCatching { S25ModConflictScanner.inventory(newOne) }.isFailure)

        // Catalog version metadata is advisory, never proof of runtime compatibility.
        val compat = S25ModCompatibility
        check(compat.assess("v1.0.3", "1.0.3").status == compat.Status.VERSION_MATCHES)
        check(compat.assess("1.0.2", "1.0.3").status == compat.Status.VERSION_MISMATCH)
        check(compat.assess("", "1.0.3").status == compat.Status.UNKNOWN_VERSION)
        check(compat.assess("1.0.3", null).status == compat.Status.NO_REQUIREMENT)
        check(compat.assess("v196608", "1.0.3").status == compat.Status.VERSION_MISMATCH)
        val expectedDigest = "sha256:" + "ab".repeat(32)
        check(compat.parseGitHubDigest(expectedDigest) == "ab".repeat(32))
        check(compat.parseGitHubDigest("sha1:" + "ab".repeat(20)) == null)

        val download = "This is an intentionally small ZIP-like download payload.".toByteArray()
        val sha = java.security.MessageDigest.getInstance("SHA-256")
            .digest(download).joinToString("") { (it.toInt() and 0xff).toString(16).padStart(2, '0') }
        val valid = S25DownloadVerifier(download.size.toLong(), sha.uppercase())
        valid.accept(download, download.size)
        check(valid.totalBytes == download.size.toLong() && valid.verify())

        val unknownHash = S25DownloadVerifier(download.size.toLong(), null)
        unknownHash.accept(download, download.size)
        check(!unknownHash.verify())

        val wrongSize = S25DownloadVerifier(download.size.toLong() + 1L, sha)
        wrongSize.accept(download, download.size)
        check(runCatching { wrongSize.verify() }.isFailure)

        val wrongHash = S25DownloadVerifier(download.size.toLong(), "0".repeat(64))
        wrongHash.accept(download, download.size)
        check(runCatching { wrongHash.verify() }.isFailure)

        check(runCatching { S25DownloadVerifier(-1L, null) }.isFailure)
        check(runCatching { S25DownloadVerifier(null, "not a sha") }.isFailure)
        println("PASS: mod safety, version requirements, release digest and streaming integrity")
    } finally {
        home.deleteRecursively()
    }
}
