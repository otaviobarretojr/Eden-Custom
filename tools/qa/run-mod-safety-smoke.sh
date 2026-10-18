#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEMP="$(mktemp -d)"
trap 'rm -rf "$TEMP"' EXIT
ACTIVITIES="$ROOT/src/android/app/src/s25Ultra/java/org/yuzu/yuzu_emu/activities"
kotlinc "$ACTIVITIES/S25ModConflictScanner.kt" \
    "$ACTIVITIES/S25ModDiagnostics.kt" \
    "$ACTIVITIES/S25ModRegistry.kt" \
    "$ACTIVITIES/S25ArchiveLimits.kt" \
    "$ACTIVITIES/S25ModCompatibility.kt" \
    "$ACTIVITIES/S25DownloadVerifier.kt" \
    "$ROOT/tools/qa/ModSafetySmokeTest.kt" \
    -include-runtime -d "$TEMP/astryx-mod-tests.jar"
java -jar "$TEMP/astryx-mod-tests.jar"
