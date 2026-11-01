# OpenBMH 1.0.3

This Classic maintenance release fixes startup, dialog and match presentation,
transfer handling and several long-career limits. It uses the player's own
supported game installation. Extended development lives separately in
[OpenBMH-Extended](https://github.com/Weyoune/OpenBMH-extended).

## Changes

- Football Limited startup reads its own calendar epoch and prepared-image
  tables. Original CD assets are located without assuming filename case.
- Original Diamond 3.7 saves with the verified `V3.7` container marker load in
  the career, browser information and preparation paths. Unknown versions and
  checksum failures are reported separately; layout checks remain strict.
- The original title remains visible until a complete left or right mouse click,
  without loading balls or automatic advancement.
- Career dialogs retain their scene background instead of inheriting the
  executable's black boot-dialog mode.
- Goal scenes retain the previous visible score until the result is revealed.
- Closing a table or pairings overview owns the complete right-button gesture.
- Pitch placement again uses the edition's preferred-position colors and
  original colored cursor.
- Select an owned transfer listing and use the sale action or Enter to inspect
  existing offers before expiry. Cancel preserves the listing. Cleared listings
  can be offered again; refusals receive visible feedback.
- A fresh squad arrival clears negotiation flags left by the previous occupant
  of its row. This prevents inherited refusal of contract renewal.
- Optional saved archives preserve cumulative player counters and observed
  title winners beyond the original byte/word and fifty-season windows.
- Daily interest and property/fund dividend products use wider intermediate
  arithmetic; Bank portfolio totals use wide display values.
- The high-cash bank robbery remains, at most once per manager and calendar
  month. Saving and loading preserves the limit. An older save with unknown
  history receives a first qualifying month of grace.
- Standard stadium blocks now allow 144,000 places in total. Unit prices come
  from the loaded edition; build periods increase with expanded block size.

## Validation

The release workflow requires successful builds, configured synthetic tests and
package checks for Windows x64, macOS ARM64 and Linux x64 at the tagged revision.
Reports and checksums accompany the downloads. Missing external-data tests are
reported as skipped, rather than counted as successful game tests.

Additional private Linux checks use Original CD, Diamond and Football Limited
assets for startup and setup, scene-backed dialogs, calendar/rescheduling,
transfer decisions, preferred-position feedback and score reveal. A 144,000-place
stadium was rendered after encrypted native save/load. These checks do not
constitute manual gameplay validation on Windows or macOS, or a full
200–300-season simulation.

## Known limits

- Individual native cash, deposit and financial-history fields remain 32-bit and
  can still overflow. Wider calculations and aggregate display do not solve
  multi-billion single-account storage; that issue remains open.
- Calendar and rescheduling opened in the checked Classic datasets; the reported
  failure remains open because it could not be reproduced.
- Football Limited remains experimental. Its original V2.20 save layout is
  different and is not enabled by the Diamond 3.7 import correction.
- Already lost counters and earlier title winners cannot be reconstructed. The
  separate manager-achievements graph retains its original fifty-season window.
  Original programs and older OpenBMH writers can discard optional archive and
  monthly-robbery extensions; keep backups before exchanging saves.
- The row-initialization correction does not clear genuine retirement/refusal
  flags in an existing career or establish a fixed 200–300-season threshold.
- Early acceptance of a human-buyer loan still needs its separate consent flow
  and is unavailable from the early offer preview.

The public downloads contain implementation, corresponding source and notices;
original game content is not included.
