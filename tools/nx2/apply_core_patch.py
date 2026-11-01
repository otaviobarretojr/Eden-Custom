#!/usr/bin/env python3
"""Patch the Eglantine NX2 emulation core, not user configuration.

Fails closed if the upstream source no longer matches the audited version.
GPL-3.0-or-later, consistent with the upstream project.
"""
from __future__ import annotations

import argparse
from pathlib import Path


def change_once(source: str, before: str, after: str, description: str) -> str:
    matches = source.count(before)
    if matches != 1:
        raise SystemExit(
            f"ABORTED: {description}: expected one original site, found {matches}. "
            "Review upstream before changing the emulator."
        )
    return source.replace(before, after, 1)


def apply(root: Path) -> None:
    gpu_split = root / "src/video_core/gpu_split.cpp"
    if not gpu_split.is_file():
        raise SystemExit(f"Missing emulation core: {gpu_split}")
    original = gpu_split.read_text(encoding="utf-8")
    text = original

    # Runtime diagnostics are opt-in. Skip ownership checks entirely during
    # normal gameplay; the expensive debug logic remains available on request.
    text = change_once(
        text,
        "void GpuSplit::CheckOwner(const char* where) {\n#ifdef _WIN32\n",
        "void GpuSplit::CheckOwner(const char* where) {\n#ifdef _WIN32\n"
        "    if (!check_owner) {\n        return;\n    }\n",
        "debug ownership fast-path",
    )

    # Avoid the atomic bookkeeping for debug-only owner tracking on normal
    # runs. This is independent of emulator compatibility behavior.
    text = change_once(
        text,
        "    g_back_busy.store(true, std::memory_order_relaxed);",
        "    if (check_owner) {\n"
        "        g_back_busy.store(true, std::memory_order_relaxed);\n"
        "    }",
        "start debug-busy bookkeeping",
    )
    text = change_once(
        text,
        "    g_back_busy.store(false, std::memory_order_release);",
        "    if (check_owner) {\n"
        "        g_back_busy.store(false, std::memory_order_release);\n"
        "    }",
        "finish debug-busy bookkeeping",
    )

    # Experimental producer/consumer latency optimization: return GPU ring
    # capacity in smaller chunks. Can reduce CPU-side stalls when the ring is
    # near full, but may increase atomic traffic; benchmark before claiming FPS.
    text = change_once(
        text,
        "if (read_position - last_store >= 4096 && read_position != available) {",
        "// Eden Custom: publish ring space earlier on the Ryzen 5600X.\n"
        "        if (read_position - last_store >= 2048 && read_position != available) {",
        "GPU ring consumption batching",
    )
    if text == original:
        raise SystemExit("Patch had no effect")
    gpu_split.write_text(text, encoding="utf-8", newline="\n")
    print("PATCHED C++ emulation core: src/video_core/gpu_split.cpp")
    print("CHANGES: owner-check fast path; debug atomic gating; 2048-word ring batching")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=Path)
    args = parser.parse_args()
    apply(args.root.resolve())
