#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

"""Build a normalized Eden Custom mod catalog from Switch-Emulator-Mod-Database.

The generator intentionally emits metadata only. It never copies mod payloads into Eden.
Compatibility is keyed by Title ID and, whenever available, NSO Build ID.
"""

from __future__ import annotations

import argparse
import io
import json
import os
import re
import zipfile
from collections import defaultdict
from pathlib import PurePosixPath

SOURCE_REPOSITORY = "ADEMOLA200/Switch-Emulator-Mod-Database"
SOURCE_BRANCH = "develop"
SOURCE_COMMIT = "abd55774c369b9c3a4df960e7afdb0391cb52056"

TITLE_ID_RE = re.compile(r"(?i)(?<![0-9a-f])(010[0-9a-f]{13})(?![0-9a-f])")
BUILD_ID_RE = re.compile(r"(?im)^\\s*@nsobid[-\\s]+([0-9a-f]{16,64})\\s*$")
VERSION_RE = re.compile(r"(?i)\\bv(?:ersion)?\\.?\\s*([0-9]+(?:\\.[0-9]+){1,4})")
CHEAT_FILE_RE = re.compile(r"(?i)^([0-9a-f]{16})\\.txt$")
ARCHIVE_TITLE_RE = re.compile(
    r"^(.*?)\\s*\\[([0-9A-Fa-f]{16})\\](?:\\[[^]]+\\])*\\s*(?:\\[mods\\])?\\.zip$",
    re.I,
)


def normalize_path(path: str) -> str:
    return path.replace("\\\\", "/").strip("/")


def repository_relative_path(path: str) -> str:
    normalized = normalize_path(path)
    parts = normalized.split("/")
    if parts and parts[0].lower().startswith("switch-emulator-mod-database-"):
        return "/".join(parts[1:])
    return normalized


def infer_category(text: str) -> tuple[str, str | None]:
    value = text.lower()

    if "dynamic fps" in value:
        return "dynamic_fps", "framerate"

    fps_match = re.search(r"(?<![0-9])(30|40|45|60|90|120|144|165|240|360)\\s*fps(?![0-9])", value)
    if fps_match:
        fps = fps_match.group(1)
        if fps in {"30", "60", "120"}:
            return f"{fps}_fps", "framerate"
        return "fps", "framerate"

    if re.search(r"(?<![a-z])fps(?![a-z])", value):
        return "fps", "framerate"

    if (
        "ultrawide" in value
        or "21.9" in value
        or "21:9" in value
        or "16.10" in value
        or "16:10" in value
    ):
        return "aspect_ratio", "aspect_ratio"

    if (
        "4k" in value
        or "2k" in value
        or "resolution" in value
        or re.search(r"(?<![0-9])(480|540|720|800|900|1008|1080|1200|1440|1600|1800|2160)p(?![0-9])", value)
    ):
        return "resolution", "resolution"

    if any(token in value for token in ("graphic", "quality", "shadow", "lod", "draw distance")):
        return "graphics", None

    if "cheat" in value:
        return "cheat", None

    return "other", None


def title_from_path(path: str, title_id: str) -> str:
    ignored = {
        "switch-emulator-mod-database-develop",
        "nx-60fps-res-gfx-cheats",
        "titles",
        "mods",
    }

    for component in normalize_path(path).split("/")[1:]:
        lower = component.lower()
        if (
            lower in ignored
            or title_id.lower() in lower
            or lower in {"exefs", "exefs_patches", "romfs", "cheats"}
        ):
            continue
        if lower.endswith((".pchtxt", ".txt", ".ips", ".zip", ".rar", ".7z")):
            continue

        cleaned = re.sub(r"\\s*\\[[0-9A-Fa-f]{16}\\].*$", "", component).strip()
        if cleaned:
            return cleaned

    return ""


def mod_name_from_path(path: str, title_id: str) -> str:
    ignored = {
        "switch-emulator-mod-database-develop",
        "nx-60fps-res-gfx-cheats",
        "titles",
        title_id.lower(),
        "exefs",
        "exefs_patches",
        "romfs",
        "cheats",
    }

    candidates: list[str] = []
    for component in normalize_path(path).split("/")[:-1]:
        lower = component.lower()
        if lower in ignored:
            continue
        if lower.startswith("010") and len(component) == 16:
            continue
        candidates.append(component)

    return candidates[-1] if candidates else PurePosixPath(path).stem


def parse_pchtxt(data: bytes) -> tuple[str | None, str | None]:
    text = data.decode("utf-8", "replace")
    build_match = BUILD_ID_RE.search(text)
    version_match = VERSION_RE.search(text)
    return (
        build_match.group(1).upper() if build_match else None,
        version_match.group(1) if version_match else None,
    )


def make_record(
    title_id: str,
    title: str,
    name: str,
    kind: str,
    build_id: str | None,
    version: str | None,
    source_path: str,
    container: str | None,
) -> dict:
    category, conflict_group = infer_category(name + " " + source_path)

    return {
        "title_id": title_id.upper(),
        "title": title,
        "name": name,
        "kind": kind,
        "category": category,
        "conflict_group": conflict_group,
        "build_id": build_id.upper() if build_id else None,
        "version": version,
        "compatibility": (
            "exact_build" if build_id else ("version_only" if version else "title_only")
        ),
        "source_repository": SOURCE_REPOSITORY,
        "source_branch": SOURCE_BRANCH,
        "source_commit": SOURCE_COMMIT,
        "source_path": repository_relative_path(source_path) if container is None else normalize_path(source_path),
        "container": repository_relative_path(container) if container else None,
    }


def scan_member(
    path: str,
    data: bytes,
    *,
    title_hint: str | None = None,
    title_id_hint: str | None = None,
    title_lookup: dict[str, str] | None = None,
    container: str | None = None,
) -> list[dict]:
    path = normalize_path(path)
    title_match = TITLE_ID_RE.search(path)
    title_id = title_match.group(1) if title_match else title_id_hint

    if not title_id or not title_id.upper().startswith("010"):
        return []

    title_id = title_id.upper()
    title = title_hint or (title_lookup or {}).get(title_id) or title_from_path(path, title_id)
    lower = path.lower()
    basename = PurePosixPath(path).name
    name = mod_name_from_path(path, title_id)

    if lower.endswith(".pchtxt"):
        build_id, version = parse_pchtxt(data)
        return [
            make_record(
                title_id,
                title,
                name,
                "pchtxt",
                build_id,
                version,
                path,
                container,
            )
        ]

    if "/cheats/" in lower and lower.endswith(".txt"):
        build_match = CHEAT_FILE_RE.match(basename)
        build_id = build_match.group(1).upper() if build_match else None
        record = make_record(
            title_id,
            title,
            "Cheat pack",
            "cheat",
            build_id,
            None,
            path,
            container,
        )
        record["category"] = "cheat"
        record["conflict_group"] = None
        return [record]

    if lower.endswith(".ips"):
        return [
            make_record(
                title_id,
                title,
                name,
                "ips",
                None,
                None,
                path,
                container,
            )
        ]

    return []


def scan_nested_zip(
    blob: bytes,
    container_name: str,
    *,
    title_hint: str | None = None,
    title_id_hint: str | None = None,
) -> list[dict]:
    records: list[dict] = []

    try:
        archive = zipfile.ZipFile(io.BytesIO(blob))
    except zipfile.BadZipFile:
        return records

    with archive:
        for path in archive.namelist():
            if path.endswith("/"):
                continue
            try:
                data = archive.read(path)
            except Exception:
                continue
            records.extend(
                scan_member(
                    path,
                    data,
                    title_hint=title_hint,
                    title_id_hint=title_id_hint,
                    container=container_name,
                )
            )

    return records


def build_catalog(zip_path: str) -> dict:
    records: list[dict] = []
    nested_zip_packs_scanned = 0

    with zipfile.ZipFile(zip_path) as archive:
        names = archive.namelist()
        title_lookup: dict[str, str] = {}

        # Prefer the human-readable metadata filename located beside each Title ID.
        # This avoids using aggregator folder names such as NX-60FPS-RES-GFX-Cheats as game names.
        for candidate in names:
            normalized = normalize_path(candidate)
            if "/cheats/" in normalized.lower() or not normalized.lower().endswith(".txt"):
                continue
            title_match = TITLE_ID_RE.search(normalized)
            if not title_match:
                continue
            title_id = title_match.group(1).upper()
            parent = PurePosixPath(normalized).parent.name.upper()
            if parent != title_id:
                continue
            title_name = PurePosixPath(normalized).stem.strip()
            if title_name and title_name.lower() not in {"readme", "info", "credits"}:
                title_lookup.setdefault(title_id, title_name)

        for path in names:
            if path.endswith("/"):
                continue

            data = archive.read(path)
            records.extend(scan_member(path, data, title_lookup=title_lookup))

            if path.lower().endswith(".zip"):
                archive_match = ARCHIVE_TITLE_RE.match(PurePosixPath(path).name)
                title_hint = archive_match.group(1).strip() if archive_match else None
                title_id_hint = archive_match.group(2).upper() if archive_match else None

                nested = scan_nested_zip(
                    data,
                    path,
                    title_hint=title_hint,
                    title_id_hint=title_id_hint,
                )
                if nested:
                    nested_zip_packs_scanned += 1
                    records.extend(nested)

    unique: dict[tuple, dict] = {}
    for record in records:
        key = (
            record["title_id"],
            record["build_id"],
            record["version"],
            record["kind"],
            record["name"],
            record["source_path"],
            record["container"],
        )
        unique[key] = record

    normalized = list(unique.values())
    normalized.sort(
        key=lambda record: (
            record["title_id"],
            record["name"].lower(),
            record["build_id"] or "",
            record["version"] or "",
            record["source_path"],
        )
    )

    by_title: defaultdict[str, list[dict]] = defaultdict(list)
    for record in normalized:
        by_title[record["title_id"]].append(record)

    return {
        "schema_version": 1,
        "source": {
            "repository": SOURCE_REPOSITORY,
            "branch": SOURCE_BRANCH,
            "commit": SOURCE_COMMIT,
            "input_file": os.path.basename(zip_path),
        },
        "stats": {
            "records": len(normalized),
            "titles": len(by_title),
            "exact_build_records": sum(
                record["compatibility"] == "exact_build" for record in normalized
            ),
            "nested_zip_packs_scanned": nested_zip_packs_scanned,
        },
        "titles": dict(sorted(by_title.items())),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("database_zip", help="Switch-Emulator-Mod-Database ZIP")
    parser.add_argument("-o", "--output", required=True, help="Output catalog JSON")
    args = parser.parse_args()

    catalog = build_catalog(args.database_zip)

    with open(args.output, "w", encoding="utf-8") as output:
        json.dump(catalog, output, ensure_ascii=False, separators=(",", ":"))

    print(json.dumps(catalog["stats"], indent=2))


if __name__ == "__main__":
    main()
