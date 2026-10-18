<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Modern UI POC (0.2 development)

The RmlUI Modern frontend is a functional **preparation-only** prototype. It uses
the same application session, imported rules and native careers as Classic.
Calendar progression, match presentation and the other management screens follow
in later milestones. Modern does not currently mean a new league or rules engine.

## Using it

Start OpenBMH normally and choose **Add**. Select an existing Classic import to
create a Modern variant without needing the original installation again. Choose
a new directory name; its original world and career directory are retained.
Alternatively select an installation, choose **Modern** in the import form and
use the normal converter, including optional legacy-career migration.
The launcher displays the variant as **Modern**. Start it to enter the new frontend.
`OpenBMH --mod mods/<name>` uses the same presentation detection.

The prototype includes:

- New club career: original default mode (3), unlimited duration, difficulty 1–10,
  one to four managers with sequential club selection, normal medium-strength
  setup and default portrait. Imported single-byte name limits still apply.
- Load and save existing native careers; overwrite confirmation and an unsaved
  changes dialog. Failed loading retains the current session.
- Office with actual club, manager, date, balance, next-event and strength fields.
- Squad, original attribute scales, frozen player portraits and a tactical overview.
  Select two players and confirm **Tauschen**. Opening this page performs the same
  preparation action as Classic, once per loaded/new session.
- League tables and league selection from the imported runtime projections.
- Keyboard focus, modal Tab trapping, Escape, Ctrl/Command+S and Ctrl/Command+L,
  resizing from 1024×640 and HiDPI. The initial game window is 1280×800.

The interface is German for both German BMH and Football Limited imports. Existing
multiplayer careers expose the engine's active manager; there is no free manager
switch outside the shared turn driver. Native careers are shared between variants
with identical world identity. Classic can resume the saved career for full gameplay.

## Presentation and runtime contracts

An optional `mods/<name>/mod.json` identifies presentation independently of the
world's original `manifest.mod` edition and `runtime:classic-v1` requirement:

```json
{"schema":"openbmh-mod@1.0","type":"Modern"}
```

Missing descriptors mean Classic. Unknown types/versions and linked descriptors
are rejected. The descriptor is not part of `content.obw`, its content identity,
or a career. Renaming a mod changes only its directory. Deleting a variant retains
shared careers and settings.

Classic requires its SHA-256 seal and CRC importer inventory as before. Modern
ignores these historical import signatures, including missing/stale inventories;
it does not silently repair or reseal edited input. ZIP integrity, container size,
paths, data schemas, references, edition, current rules and supported dimensions
remain checked. Both GUI and CLI mount through the same policy selection.

The fingerprint is calculated from actual world documents. Themes, frontend choice
and directory names do not change it. Gameplay/world edits change the fingerprint
and cannot silently attach an unrelated career. The native save-state checksums,
full embedded world and both random-generator checkpoints remain intact. With an
unchanged Classic import, saves written by either frontend remain readable by the
other. An intentionally unsealed/modified world remains a Modern world; Classic
still requires a matching, sealed world when loading a career.

## Implementation and next steps

The shared RmlUI window owns SDL/OpenGL, FreeType, layout loading, text input,
per-event keyboard modifiers, HiDPI pointer conversion and rendering lifetime.
Launcher and Modern documents share the existing visual styles, fonts and stadium.
The game still runs as a child process, returning to its launcher on exit.

Modern view code reads copyable projections, stable club identities and player IDs.
All lineup writes pass through shared application actions with observed ownership
and revision, including rollback if acknowledgement/projection fails. Rendering,
image composition and local selection do not draw random numbers or advance time.

The image provider returns RGBA and actual dimensions through logical `asset:` keys.
It currently uses the imported crest resources and frozen portrait recipes; missing
pictures get a neutral silhouette. It does not regenerate faces from player names.
Future high-resolution images and optional packs extend this resolver, with
presentation overlays separate from gameplay identity. World metadata/layouts stay
separate; no original assets are shipped in the source or release package.

The frontend builds lists from model vectors, rather than assuming 18 or 20 clubs,
nine navigation buttons, a fixed roster length or a particular national league name.
The current Classic adapter supplies its existing leagues and tactical coordinates.
Replacing that adapter for new league structures is later work; view code does not
access executable offsets to construct leagues or simulate gameplay.

Verification consists of data-free RML/layout/package checks, data-dependent tests
of import/derivation, shared Classic/Modern careers, ownership/revision rejection,
failed-load retention and editable-content validation, plus GUI milestone evidence.
Original assets and actual careers/screenshots remain in the private reference repo.
