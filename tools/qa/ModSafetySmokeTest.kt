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
        println("PASS: 7 ASTRYX mod safety checks")
    } finally {
        home.deleteRecursively()
    }
}
