# OpenBMH v0.1.0

OpenBMH reconstructs the Bundesliga Manager Hattrick engine in C++20. The graphical client uses SDL2; the console provides command batches and local manager-client transports over the same career session. This release needs a supported game installation supplied by the player. No original executable, artwork, music, text database, manual or saved game is included.

## Scope

The engine includes career management, leagues and cups, transfers and contracts, finances, training and camps, stadium management, live match interventions, audio playback and normal saves. Supported actions remain subject to mode, manager and phase restrictions; an unimplemented path reports an explicit error rather than an invented result. The project aims for recognisable behaviour and plausible results, not a promise that every original version produces identical events.

German game editions are the primary compatibility target. **Football Limited support is experimental.** English installation loading and edition-specific text/layout data are supported paths, while original V2.20 save import, English audio and remaining competition/interface differences have limitations. Check current issues before relying on a particular English workflow.

## Compatibility decisions

OpenBMH uses shared rule values across supported German editions, with installation-specific content read from the selected data. It includes safety handling around invalid input, calendar boundaries, capacity checks and suspended operations. See [compatibility decisions](../../COMPATIBILITY.md) and [architecture](../../architecture.md). This is an independent reconstruction, not an official update to the original game.

## Running

Extract the package to a separate directory or alongside your installation. `openbmh_play` can select a game directory, executable or a ZIP/7z containing one supported installation. Use an explicit writable save directory when the original directory is read-only, and keep backups.

```sh
openbmh_play --data "path/to/game" --save-directory "path/to/saves"
openbmh_cli --console --data "path/to/game" --saves "path/to/saves"
```

Packages and their native platform results are listed with the release assets. The [build instructions](../../BUILDING.md) cover Windows, Linux and macOS source builds; an instruction is not a claim of native acceptance. Linux's default file dialog backend requires GTK3 system libraries. A macOS terminal executable is distinct from a signed application bundle.

## License

The matching application source tag is [v0.1.0](https://github.com/rezmer/openbmh/tree/v0.1.0). Source/build and relinking materials for linked components must also accompany or be offered with binary releases under their licenses.

OpenBMH-owned code and documentation use GPL-3.0-only. Third-party components retain their licenses; the full texts accompany the package. Read [LICENSE](../../../LICENSE) and [THIRD_PARTY_NOTICES](../../../THIRD_PARTY_NOTICES.md). These licenses do not grant rights to original game content.

For reports, include the OpenBMH version, data edition, independently written steps and the observed result. Do not upload original files, game screenshots, recordings, text collections or saved games to public issues.
