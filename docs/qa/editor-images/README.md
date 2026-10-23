# Native read-only image preview

Editor 0.5.3, captured from the native RmlUI application at 1440×900.
All pictures and records are authored synthetic demo data. No game installation,
real crest, portrait, savegame or personal profile is included.

| View | Native screenshot |
|---|---|
| Before: small image in the club inspector | [Club inspector](before-club-image.png) |
| After: large read-only preview, Midnight / German | [Midnight](preview-midnight-de.png) |
| Silver / English | [Silver](preview-silver-en.png) |
| Matchday / German | [Matchday](preview-matchday-de.png) |
| Jump to the exact linked image record | [Linked asset](linked-asset.png) |

Clicking the inspector image or a list thumbnail opens the same viewer. Images
retain their aspect ratio; there are no editing controls. Close, the window's
close button and Escape return to the current form. **View asset** opens the
linked asset, clearing unrelated country/league/team filters. Composed Classic
portraits without a single backing asset offer the general image library.

`tests/editor_gui_smoke.py` exercises the actual mouse and keyboard path. MCP
observes that opening/closing the preview leaves the world and history unchanged.
The test also preserves a pending property draft, cancels a guarded asset jump,
checks the selected club's exact asset, and opens the different asset from an
unselected row. These checks complement the core, comparison and regulations
tests. The synthetic demo starts unsaved; the asterisk in these captures reflects
that initial state rather than a modification caused by the preview.
