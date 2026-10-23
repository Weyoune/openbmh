# Dropdown chevron alignment — Editor 0.5.5

The old chevron was too low and too far inside the button. RmlUI positions its
native `selectarrow` from the select's content box, which already excludes the
reserved right padding. The correction moves the existing glyph 28dp right and
8dp up, into that padding and onto the control's vertical center. Size, colour,
selected values and native select events stay the same.

These are actual SDL2/RmlUI captures with software OpenGL. All data comes from
`OpenBMHEditor --demo --page clubs`: generated clubs and project-authored
geometric crests. No original installation or savegame is involved. Temporary
MCP profiles use an automatically assigned localhost port; no token is visible
or included. Images are captured directly, without image generation or retouching.

| View | Native capture |
|---|---|
| Before: 0.5.4, Midnight / German | [Before](before-0.5.4.png) |
| After: 0.5.5, Midnight / German | [After](after-midnight-1440x900.png) |
| After: Silver / English | [Silver](after-silver-1440x900.png) |
| After: Matchday / German | [Matchday](after-matchday-1440x900.png) |
| Toolbar and inspector controls | [Main view](after-toolbar.png) |
| Click directly on the repositioned theme arrow | [Native open list](after-dropdown.png) |
| Small window, 1080×720, editor scale 0.78× | [Small](after-midnight-1080x720.png) |
| Large window, 2560×1600, editor scale 1.78× | [Large](after-midnight-2560x1600.png) |

[Capture parameters](native-captures.json) record the themes, languages, native
viewports and editor scales. The arrow remains inside the button at each scale.
Clicking its new position opens the native option list; Escape dismisses it.
The existing native GUI smoke verifies selection changes, all themes, both
languages, draft protection and navigation. This layout-only correction adds
no editor data or engine behaviour.
