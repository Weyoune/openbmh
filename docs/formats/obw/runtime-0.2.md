<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Classic runtime and portable launcher (0.2 development)

This contract is for the 0.2 development branch. It is not a release validation claim.

## Portable layout and lifecycle

`OpenBMH.exe` (Windows) / `OpenBMH` (Linux and macOS) starts the operating system's
RmlUI launcher: one selected row, **Name | Art**, and **Start, Add, Delete, Rename, End**.
No installation is auto-selected on first launch. Add converts an externally owned
installation; original assets never belong in a distribution.

```
OpenBMH[.exe]
assets/launcher/*
mods/<name>/content.obw
userdata/<content-id>/*.obs
userdata/<content-id>/*.obsettings
userdata/<content-id>/*.oblayout
userdata/<content-id>/highscore-<signature>.obscore
```

The directory name is the launcher name, separate from `manifest.mod` (`BMH-classic`
or `FLTD-classic`). `FTDL-classic` is the requested English directory-name default.
Names must be portable single path components; duplicates are case-insensitive.
Classic files are sealed, not editable world overlays. Modern gameplay is not implemented.

Rename changes the directory only. Delete removes mod content after confirmation and
retains user data. Reimporting the same content and start year reconnects it. The content
identity excludes display title, generator version and the CRC inventory, but includes
runtime content and the starting-year configuration. It deliberately changes when content
actually changes. User data cannot be silently associated with a different database.

The launcher starts a child process of the same binary, with `--run-mod` and a separate
save directory, and remains available after the child exits. `--mod <content.obw>` bypasses
the launcher. A normal GUI/console invocation cannot fall back to original game files.

RML/RCSS define the launcher and its import, rename, delete and progress dialogs.
Only folder selection uses an OS dialog. The SDL2/OpenGL view keeps UI resources
relative to the executable, supports resizing and HiDPI input, and sleeps between
input or scheduled UI updates. The running child's match engine does not depend on
RmlUI. OpenGL 3.3 is required for the launcher (4.1 on macOS).

## Integrity and content access

The existing `classic-installation-v1` payload is complemented by
`x-classic/seal.json` (`classic-seal@1.0`): SHA-256 for every other archive entry plus
`content_id`. The earlier CRC inventory excludes the seal to avoid a circular digest.
A missing seal, changed entry or unexpected file in a Classic mod directory prevents start.
SHA-256 detects ordinary manual changes/corruption; it is not a signature or a promise
against a local owner modifying the validator.

The runtime decodes indexed images, fonts and palettes into an immutable store. Existing
UI aliases such as `KB/29.VGA` select the already decoded object; they are not files.
Embedded image palettes are imported separately, preserving the Sportschau/title colours.
Original single-byte readme content is preserved in the local package where available.
No original executable is reconstructed or executed.

## Native careers and personal files

A `.obs` ZIP contains the complete sealed starting world, with its original manifest at
`state/world-manifest.json`. The top manifest has `kind: save`,
`save_profile: classic-career-v1` and the content identity.

| Entry | Native contract |
|---|---|
| `state/career.json` | `classic-state@1.0`: named typed arrays/scalars, name bytes and explicitly addressed compatibility blocks |
| `state/rng.json` | `classic-rng@1.0`: both seeds and decimal-string draw counters |
| `state/session.json` | `classic-session@1.0`: runtime option bytes, stable desk arguments and manager mappings |
| `state/driver.json` | `classic-driver@1.0`: desk stage/order, match groups, referees, event counter and previous results |
| `state/information.json` | `classic-bytes@1.0`: Classic browser information fields |
| `state/integrity.json` | `save-integrity@1.0`: SHA-256 of every other state entry |

Only existing manager-desk save boundaries are supported. Runtime callbacks and host
pointers are reconstructed. Native saving is RNG-neutral; unlike the old FBM envelope it
needs no random encryption fields. Both generator seeds and draw counters resume exactly.
Names and compatibility bytes use hexadecimal encoding to retain single-byte values
without pretending they are UTF-8. Integer fields have explicit bounds.

`.obsettings`, `.oblayout` and `.obscore` are JSON documents with kind-specific schema
names (`classic-settings@1.0`, `classic-layout@1.0`, `classic-highscore@1.0`) and a
`classic-bytes@1.0` payload. They preserve existing availability bits, layout chains and
high-score fields. The existing application validators still govern settings and layouts.
Writes publish a complete temporary file atomically; overwrites require the existing
application confirmation.

## Legacy migration and Classic dates

Add defaults to transferring games/settings, with a separately selectable source directory.
The source is read-only. Existing personal files are retained rather than overwritten.
Individual failures appear in the import report.

German/OpenBMH V2.24 files use their documented block contract. Original Football Limited
V2.20 has its own block translator, including FA Cup, League Cup and additional-cup memory.
It is never accepted by merely changing a version string. Like the V2.20 loader, migration
rebuilds its unsaved current match groups without replaying daily finances or shuffling
saved manager order. Unknown/damaged versions fail explicitly.

Original FBM files do not contain the clock-seeded Watcom random state. Migration therefore
seeds that stream reproducibly from SHA-256 of the source file. A converted career cannot
promise the original machine's unknown random future; subsequent native checkpoints retain
both streams in full.

The selected start year changes only displayed years for new careers. Weekdays and the
original four-year calendar remain unchanged. Imported careers retain the year from their
saved information header when it is valid. This cosmetic origin is independent of the
immutable world's start configuration and is carried in the native career.
