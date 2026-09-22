# Eden Custom Mod Catalog

The Eden Custom mod catalog is a metadata layer for matching game mods to the exact game installed by the user.

## Safety model

The catalog uses these identifiers in priority order:

1. Title ID
2. NSO Build ID
3. game version

A mod with an exact Build ID match may be shown as **Compatible**.

A record with only a version match is informational until the Build ID is verified.

A record with only a Title ID match must never be installed automatically.

Comments inside third-party mod files are not trusted as the primary identity source. The database contains examples where a human-readable comment has the wrong game Title ID even though the archive/path identity is correct.

## Source

Initial source:

- repository: `ADEMOLA200/Switch-Emulator-Mod-Database`
- branch: `develop`
- pinned commit: `abd55774c369b9c3a4df960e7afdb0391cb52056`

The source repository aggregates work from multiple mod authors and projects. Eden Custom must preserve source/credit metadata and must not assume that every mod payload shares one common license.

## Generator

`tools/mod_catalog/generate_catalog.py` reads a database ZIP and emits metadata only.

The generator currently recognizes:

- `.pchtxt` patches and their `@nsobid`
- Atmosphere-style `cheats/<BuildID>.txt`
- `.ips` patches
- game-specific nested ZIP packs

It deliberately ignores Nintendo 3DS Title IDs and does not unpack RAR or 7z archives.

## Catalog record

Each record contains:

- `title_id`
- `title`
- `name`
- `kind`
- `category`
- `conflict_group`
- `build_id`
- `version`
- `compatibility`
- source repository / branch / commit / path
- optional containing archive

## Conflict groups

Initial conservative groups:

- `framerate`: static FPS, dynamic FPS and other FPS patches
- `aspect_ratio`: ultrawide/aspect-ratio alternatives
- `resolution`: mutually exclusive resolution presets

Only one enabled mod from the same conflict group should be selected by default. The user may override this later only when a curated rule explicitly marks the combination safe.

## Runtime direction

The Eden frontend should:

1. read the selected game's Title ID;
2. read the actual NSO Build ID from the loaded game/update;
3. filter catalog records to that exact pair;
4. show incompatible records separately instead of installing them;
5. use the existing Eden mod installer for the final installation;
6. preserve the existing enable/disable/delete flow in per-game Add-ons.

The catalog is not a replacement for Eden's existing `FrontendCommon::InstallMod` path. It is a compatibility and discovery layer in front of it.
