# OpenBMH v0.1.2

OpenBMH reconstructs the Bundesliga Manager Hattrick engine in C++20. The graphical client uses SDL2; the console provides command batches and local manager-client transports over the same career session. This release needs a supported game installation supplied by the player. No original executable, artwork, music, text database, manual or saved game is included.

## Unreleased corrections

- Application exits ask for confirmation in a native German/English message
  box, with Cancel selected by default, including window close events during
  matches and dialogs ([#11](https://github.com/Weyoune/openbmh/issues/11)).

- Football Limited uses the correct enabled/disabled setup sprites, positive
  balance colour and outside controls of the desktop shortcut bar ([#7](https://github.com/Weyoune/openbmh/issues/7)).
  Its cached data is regenerated automatically for the corrected mapping.
- Filename dialogs reacquire the native SDL text editor on entry and window
  focus, including the live settings dialog. macOS acceptance remains human QA
  ([#8](https://github.com/Weyoune/openbmh/issues/8)).
- Save-directory labels use portable `/` separators, avoiding the German bitmap
  font's `Ö` glyph at the backslash position on Windows ([#9](https://github.com/Weyoune/openbmh/issues/9)).
- Setup ignores mouse releases without a matching press on that screen, so
  window activation cannot toggle the initial league/national-team selection
  through that input path ([#10](https://github.com/Weyoune/openbmh/issues/10)).
- Source builds include the standalone [OBW installation converter](https://github.com/Weyoune/openbmh/blob/v0.2.1/docs/formats/obw/import.md).
  It produces intermediate database conversions; game integration remains
  planned for 0.2.

These changes require a build from the updated source. The tagged 0.1.2 package
does not contain these subsequent corrections.

## Changes in 0.1.2

- The environment menu shows all six original artwork panels: three youth teams,
  assistant coach, doctor and physio. Youth panels include their league and rank.
- All six monthly budgets can be set directly to zero through six stars. Youth
  development, season promotion/relegation, finance previews and monthly accounts
  share those values. Existing academy lump-sum saves retain their paid status.
- Match-minute pacing follows the loaded original delay setting; A9 in Settings
  allows faster or slower progression. No fixed 250-ms pause is added. Retrace
  animations use the original reference rate on different host monitors.
- External test fixtures are now recognised correctly by CMake instead of being
  reported as unavailable despite configured paths.

See [compatibility decisions](../../COMPATIBILITY.md) for youth recruitment and
legacy contract handling. The previous UI corrections remain included.

## Changes in 0.1.1

- Live lineup navigation preserves the saved pitch view and allows repeated switches between the team list and pitch. Right-click can leave the dialog even when the pointer moves between press and release ([#1](https://github.com/Weyoune/openbmh/issues/1)).
- The instruction panel restores original control symbols, substitute information, red coverage on player hover and pitch-position hints ([#2](https://github.com/Weyoune/openbmh/issues/2)).
- Right-click cancels load/save dialogs, including settings and desktop-layout files ([#3](https://github.com/Weyoune/openbmh/issues/3)).
- Save filenames accept typing immediately, replace a selected existing name and support Ctrl+A/Cmd+A. This also applies to desktop layouts saved as MEN files ([#4](https://github.com/Weyoune/openbmh/issues/4)).

These corrections passed targeted UI/controller checks and Linux SDL input replays, including normal and Reality matches, substitutions, file writing and a fresh-process reload. Physical macOS window verification remains human QA. Use packages from 0.1.1 or later to test these fixes; older 0.1.0 builds do not include them.

## Scope

The engine includes career management, leagues and cups, transfers and contracts, finances, training and camps, stadium management, live match interventions, audio playback and normal saves. Supported actions remain subject to mode, manager and phase restrictions; an unimplemented path reports an explicit error rather than an invented result. The project aims for recognisable behaviour and plausible results, not a promise that every original version produces identical events.

German game editions are the primary compatibility target. **Football Limited support is experimental.** English installation loading and edition-specific text/layout data are supported paths, while original V2.20 save import, English audio and remaining competition/interface differences have limitations. A known limitation can reject an English save made after the first calendar
advance: the saved turn cursor may no longer match the active calendar. Initial
save/reload works; a complete English career is not yet verified. Check current
issues before relying on a particular English workflow.

## Compatibility decisions

OpenBMH uses shared rule values across supported German editions, with installation-specific content read from the selected data. It includes safety handling around invalid input, calendar boundaries, capacity checks and suspended operations. See [compatibility decisions](../../COMPATIBILITY.md) and [architecture](../../architecture.md). This is an independent reconstruction, not an official update to the original game.

## Running

Extract the package to a separate directory or alongside your installation. `openbmh_play` can select a game directory, executable or a ZIP/7z containing one supported installation. Use an explicit writable save directory when the original directory is read-only, and keep backups.

```sh
openbmh_play --data "path/to/game" --save-directory "path/to/saves"
openbmh_cli --console --data "path/to/game" --saves "path/to/saves"
```

Version 0.1 ships binary packages for Windows x64, macOS on Apple silicon (arm64, macOS 11 or later) and Linux x64. The Linux package passes the automated build and tests but has not yet been played natively; reports are welcome. The macOS package is not signed or notarized, so Gatekeeper asks for confirmation on first start: open it once via right-click → Open, or allow it under System Settings → Privacy & Security. Packages and their native platform results are listed with the release assets. The [build instructions](../../BUILDING.md) cover Windows, Linux and macOS source builds; an instruction is not a claim of native acceptance. Linux's default file dialog backend requires GTK3 system libraries. A macOS terminal executable is distinct from a signed application bundle.

## License

The matching application source tag is [v0.1.2](https://github.com/Weyoune/openbmh/tree/v0.1.2). Source/build and relinking materials for linked components must also accompany or be offered with binary releases under their licenses.

OpenBMH-owned code and documentation use GPL-3.0-only. Third-party components retain their licenses; the full texts accompany the package. Read [LICENSE](../../../LICENSE) and [THIRD_PARTY_NOTICES](../../../THIRD_PARTY_NOTICES.md). These licenses do not grant rights to original game content.

For reports, include the OpenBMH version, data edition, independently written steps and the observed result. Do not upload original files, game screenshots, recordings, text collections or saved games to public issues.
