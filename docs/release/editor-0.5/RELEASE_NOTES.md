<!-- SPDX-License-Identifier: GPL-3.0-only -->
# OpenBMH Editor 0.5.3

Fixes the Windows graphical startup on machines without an OpenGL 3.3 driver,
including Windows Server / Remote Desktop. The editor and game launcher use the
pinned RmlUI Direct3D 11 renderer with automatic WARP software fallback. No
additional graphics runtime download is needed on the supported Windows systems.
Linux/macOS retain their existing OpenGL rendering.

Windows also builds WebP without an AVX requirement, avoiding illegal instructions
when displaying imported club images on older CPUs or virtual machines.

Double-click a country to see its competitions, a league to see its clubs, and
a club to see its players. Destination lists inherit the corresponding filters;
unapplied inspector entries are protected by the existing confirmation dialog.

Windows startup failures now produce a native dialog and a log in
`%LOCALAPPDATA%/OpenBMH/OpenBMHEditor-startup.log`. Console redirection is
retained and headless/MCP errors remain nonmodal. Native GUI startup, screenshots,
Unicode paths and the actual launcher are now tested on Windows Server 2022
with WARP forced, in addition to the existing core/MCP tests.

## Editor 0.5.2

Adds **Regulations / Regelwerk**, a read-only view of game and league rules.
Inspect promotion/relegation places and destinations, playoff conditions,
international qualification selectors and replacement rules. Search competitions
and filter by country; inspect preserved game-rule records and stage details.
Both interface languages and all three themes are supported. The read-only MCP
tool `editor_regulations` returns the same projection.

Explicit OBW declarations take precedence. Imported worlds show the named
Classic ladder and qualification policy, clearly labelled. The parallel third
divisions retain champion-plus-playoff promotion; the bottom tier uses its
newcomer pool. Historical display/colouring quotas are not reinterpreted as
European qualification rules. Undeclared rules stay unspecified.
This adds no season simulation or rule editing.

## Editor 0.5.1

Adds **Ratings & salaries / Wertevergleich**: sortable player, team and league
overviews, searchable and filterable by country/league/club and attribute system.
Choose the attribute columns and scroll horizontally for additional metrics.
Click a row to open its existing editor. All three themes and both interface
languages are supported.

Player values retain their native scales. Group ratings are player-weighted
means; salaries are per-season amounts from contracts active at the world start
date. Partial wage bills are marked, unknown values stay unknown, explicit zero
salaries remain zero. All reserves are included; source-only duplicate evidence
is excluded. Different rating systems are not combined. The read-only MCP tool
`editor_compare` exposes the same results and pagination. Comparison caches
refresh after edits and undo/redo without changing the world.

## Initial editor 0.5.0

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
