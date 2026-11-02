# OpenBMH Classic 1.0.4

This Classic patch release includes the desktop, save and advertising fixes
added after 1.0.3. It requires the player's own supported game installation.
Extended development remains separate in
[OpenBMH-Extended](https://github.com/Weyoune/OpenBMH-extended).

## Changes since 1.0.3

- The supplied Classic start artwork is embedded at its native resolution and
  displayed with correct proportions, including high-DPI output. A complete
  left or right mouse click continues into the game.
- An unframed desktop calendar/date control now starts the next turn correctly.
  Unframed admission-price and automation controls use their correct origins;
  hidden controls and national-team restrictions remain enforced.
- Dragging an icon out of a stored desktop folder moves the selected icon while
  keeping the folder in place. Folder-corner removal uses the original input
  geometry.
- Double-clicking an occupied save slot opens the original overwrite question.
  Its Yes/No buttons work entirely with the mouse. No preserves the existing
  file; Yes writes the selected slot.
- Advertising choices use the original hover outlines and help areas. Returning
  from a dialog refreshes the pointer state and removes stale highlighting.
- Malformed saved formation automation is rejected before desktop projection,
  without changing the career state or random streams.
- Football Limited retains its original overwrite question during English-data
  translation. The launcher rebuilds older translated caches automatically.

## Validation

The release workflow requires successful builds, configured tests, version and
package checks for Windows x64, macOS ARM64 and Linux x64 at the exact tagged
revision. Downloads include checksums, a platform test report and corresponding
source with pinned open-source dependencies.

Additional private Linux validation of the fixes used Diamond 3.7 data and real
mouse input. The 211-test configured suite executed 116 passing tests, skipped
95 checks needing other private fixtures, and reported no failures. Focused
save, desktop and advertising input checks executed. Native before/after checks
verified mouse-only overwrite refusal/acceptance, folder extraction and a real
two-manager calendar handoff, with saved-state verification. An original
Football Limited data probe verified the translated overwrite question.

These checks do not establish manual gameplay acceptance on Windows or macOS.
Issues [#51](https://github.com/Weyoune/openbmh/issues/51),
[#52](https://github.com/Weyoune/openbmh/issues/52),
[#53](https://github.com/Weyoune/openbmh/issues/53) and
[#54](https://github.com/Weyoune/openbmh/issues/54) remain open for human QA.

## Known limits

The compatibility and long-career limits documented for
[1.0.3](../1.0.3/RELEASE_NOTES.md) remain. Football Limited is experimental;
its original V2.20 save layout remains unsupported. Keep backups of saves.
macOS packages are unsigned terminal executables, not notarised app bundles.

No original executables, databases, saves, graphics or audio are included.
