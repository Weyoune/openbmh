<!-- SPDX-License-Identifier: GPL-3.0-only -->
# OpenBMH v0.2.0

Version 0.2 makes the original-style game independent of the original installation
after import. It supports Bundesliga Manager Hattrick and Football Limited Classic
content. Modern game screens are deferred; the stadium-themed launcher is included.

## Start playing

1. Extract the package into a writable folder and start `OpenBMH.exe` on Windows
   or `OpenBMH` on Linux/macOS. Keep the supplied `assets` folder beside it.
2. Choose **Add**, select an installation containing `BMH.EXE` or `FLTD.EXE`,
   and choose a unique mod name. Defaults are `BMH-classic` and `FTDL-classic`.
3. Choose the displayed starting year. Optionally migrate original saved games,
   settings and desktop layouts; another source folder can be selected.
4. Select the imported mod and choose **Start**. The game reads its converted
   `content.obw`, so the original installation is no longer required for play.

The list is empty on the first launch. Rename changes the mod folder; Delete asks
for confirmation and retains personal files. Failed individual save conversions
appear in the import report. Keep the original files as backups.

## Content and saves

- Converted content lives in `mods/<name>/content.obw`. Classic content preserves
  the original edition's tables, rules, artwork and competition structure. A seal
  detects modified or damaged contents; reimport to repair a rejected package.
- New saved games use `.obs`, including the starting world, complete career state,
  turn driver and both random streams. Settings, layouts and high scores use
  `.obsettings`, `.oblayout` and `.obscore`.
- Personal files live in `userdata/<content-id>/`. Renaming or deleting a mod
  retains them; reimporting identical content reconnects them.
- Use Add to migrate older `.FBM`, `.EIN` and `.MEN` files. Original Football
  Limited V2.20 saves have an edition-specific translator. Unsupported or damaged
  saves are reported rather than guessed. OpenBMH 0.1.2 cannot load native 0.2 saves.
- Original saves did not record every random seed. Migration establishes a
  reproducible continuation; subsequent native saves preserve both streams exactly.
- Starting-year selection changes displayed years, retaining Classic scheduling
  and weekday rules. Existing careers keep their saved year.

The GUI and console both run from native content. For direct startup:

```sh
./OpenBMH --mod "mods/BMH-classic/content.obw"
./openbmh_cli --console --mod "mods/BMH-classic/content.obw"
```

See the [runtime and migration contract](../../formats/obw/runtime-0.2.md).

## Platforms and distribution

The build pipeline produces Windows x64, Linux x64 and macOS ARM64 ZIPs and a
matching source ZIP with pinned dependency sources and build information.
Builds and automated tests do not replace native interactive acceptance.
The launcher requires OpenGL 3.3 (macOS: 4.1). Linux requires the platform libraries
described in [BUILDING](../../BUILDING.md), including GTK3 for its folder picker.
The macOS executable is unsigned and is not a notarised application bundle.

No original executables, graphics, sound, databases or saved games are distributed.
Supply a supported CD/later German or Football Limited installation; the original
German floppy edition is unsupported. OpenBMH is GPL-3.0-only; bundled components
retain their licenses. Keep all supplied notices and corresponding source material.
