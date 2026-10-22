<!-- SPDX-License-Identifier: GPL-3.0-only -->
# OpenBMH Editor 0.5.0

The first standalone editor release uses SDL2, RmlUI and OpenGL on Windows x64,
Linux x64 and macOS ARM64. Its version is independent of OpenBMH 0.2.0.

## Opening and editing worlds

Open an OBW archive, an unpacked world, or import a supported Bundesliga Manager
Hattrick / Football Limited installation. Edit countries, competitions, clubs,
players, squads, stadiums, referees and indexed artwork. Save exclusively as
**OBW 1.1 `editor-world-v1`**. Legacy installations are not overwritten.

The interface offers German and English, Midnight Glass, Silver Glass and
Matchday themes, shared undo/redo, validation and source-conflict review. Image
import supports cropping, arbitrary rotation and mirroring while retaining the
original. Shared placeholders remain intact when an individual image is edited.

The integrated authenticated MCP server is enabled by default on loopback.
Configure its port and copy the connection in Options. GUI and MCP use the same
transactions and revision checks; credentials remain in the local profile.

## Compatibility and limits

- Existing sealed Classic BMH/FLTD worlds can be opened and saved as editable
  worlds. Edited worlds cannot be played by the Classic 0.2 runtime and are
  never resealed as unmodified Classic content.
- Complete source rosters, unknown extensions, image originals and source
  evidence are preserved. Missing facts remain missing; unresolved source
  conflicts remain visible. This release does not supply game data.
- Modern season simulation, complete rule/calendar authoring and saved-career
  editing are outside this release. An editor-valid world does not establish
  that a season engine can play it.
- The ZIP contains the executable, `assets/editor`, fonts and notices. Retain
  them together. Windows includes its app-local runtime. Linux needs the
  documented system libraries. macOS is unsigned and not notarised.

The release has matching platform archives and a corresponding source archive
with pinned dependencies. Build/core/MCP checks run on all three platforms;
native mouse/keyboard GUI checks run on Linux. Target-platform human acceptance
remains separate. See the [editor guide](../../EDITOR.md) and
[format contract](../../formats/obw/editor-v1.md).
