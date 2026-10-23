# Native navigation, pickers and idle rendering

Editor 0.5.4, native SDL2/RmlUI at 1440×900. The captures contain only generated
demo records and project-authored geometric crests. No installation, real photo,
savegame, personal profile or MCP token is included. Reproduce the initial
database with `OpenBMHEditor --demo --page clubs`.

| View | Screenshot |
|---|---|
| Before: 0.5.3 controls and MCP button spacing | [Options before](before-options-0.5.3.png) |
| After: Modern Desktop controls and separated Copy connection | [Options](options-midnight-de.png) |
| Native dropdown, Midnight / German | [Midnight](dropdown-midnight-de.png) |
| Native dropdown, Silver / English | [Silver](dropdown-silver-en.png) |
| Native dropdown, Matchday / German | [Matchday](dropdown-matchday-de.png) |
| Browser context menu, both directions available | [Back / Forward](history-midnight-de.png) |

The controls port the Modern Desktop submenu palettes, gradient buttons, rounded
popup, selection checkmark, matching text size and hover treatment to RCSS. They
retain RmlUI's native select values/events and scale with the editor. Escape
cancels the open list before closing its dialog. The connection action now has
20dp of space above it. Correct frame initialization preserves transparent
offscreen layers for the popup shadows.

`tests/editor_gui_smoke.py` uses actual mouse/keyboard input and an MCP observer:

- Drill from the second of two leagues to its club and players. Back twice,
  then Forward twice; edit/undo the selected destinations to verify their IDs.
- Cancel a draft when going Back, then apply it to the original player. The
  subsequent Back still visits the club, proving the cursor did not advance.
- Navigate elsewhere after Back, verify that Forward stays unavailable, and
  exercise both mouse side buttons.
- Follow a preview's exact image-asset link and return in both directions.
- Reopen the same OBW through MCP. Its unchanged filename/IDs must not preserve
  history or unapplied fields from the previous world.
- Change native dropdowns by keyboard, cancel one with Escape, apply both
  languages and all three themes. Options alone leaves the world untouched.
- Leave the window idle for three seconds. Its CPU budget is 20% of one logical
  core, intentionally generous for software-rendered CI. Continuous repainting
  fails this check. The observed local idle use was below 1% of one core.

The core tests also verify that edits, undo and save retain `world_generation`,
while opening another world increments it. Navigation does not alter data or
the editing undo cursor. Scroll positions are restored after rebuilding a view;
history is limited to the last 200 visited views in the current world.

## Idle measurement

The former loop updated/rendered/presented continuously, with only a 10ms delay
after each frame. The new loop waits for input or the next requested RmlUI timer.
MCP state is checked every 400ms without repainting an unchanged screen. Async
work completion, resize/expose, screenshot and automated-exit deadlines still
wake the loop. Minimized windows do not render.

[Measured CPU time](idle-cpu-linux.json) uses six-second idle samples after the
window settles, software OpenGL and two renderer threads. The synthetic demo
changed from 95.3% to below the 0.01s sampling resolution; a private supplied world
changed from 136.0% to 0.17% of **one logical core**. Multithreaded rendering can
exceed 100% on this scale; these figures are not Task Manager's whole-machine
percentage. This measurement is separate from import or image-processing work.

Native startup, screenshot deadlines, Unicode paths and failure reporting are
covered by `tests/editor_startup_smoke.py`. Windows/WARP runtime measurements
with private data are kept in the private repository. Build success on macOS
does not establish native human acceptance there.
