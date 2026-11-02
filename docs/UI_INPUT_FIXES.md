# Desktop and advertising mouse input fixes

The optional desktop frame bit controls decoration, not whether the visible
widget contents accept input. The corrections reuse the Original renderer,
coordinates, layout records and action dispatchers.

- The date widget activates the same owned advance action as Anstoß, with or
  without a frame. In a multiplayer turn this first hands control to the next
  manager. The separate calendar/rescheduling menu keeps its own behavior.
- Stored icons remain clickable and draggable from unframed folders. Extraction
  restores the actual icon record to the visible chain; the folder stays put.
  Hidden or unlinked owners remain invalid. Removal corners keep their separate
  transaction and mouse-release barrier. The same frame-origin correction also
  covers the existing admission scale and five automation controls.
- A true double-click on an existing save opens the shared Original-style
  overwrite question. A separate single click on Ja confirms, Nein returns to
  the picker, and repeated double-click releases cannot write. Enter and cancel
  paths remain. FBM, EIN and MEN use the same confirmation boundary.
- Advertising follows all nineteen original hit rectangles with palette0x27
  outlines and the installed edition's detail/help text. Motion alone does not
  change the career, revision or either random stream. Offers/calculators retain
  their input owner; returning from them reevaluates the actual pointer, avoiding
  a stale highlight without requiring another mouse movement.

## Validation

The focused configured-data tests are `app_desktop_input_tests`,
`app_advertising_input_tests`, `ui_desktop_input_tests`, `ui_save_load_tests` and
`ui_advertising_tests`. They cover frame variants, exact field boundaries,
canonical actions, stale/hidden-owner refusal, cancellation, input release,
overwrite confirmation and unchanged game/random state for presentation-only
input. The desktop input test isolates these checks from unrelated private
text/badge inventories; the existing full test retains its additional checks.

Private Linux SDL before/after captures and actual saved games are maintained in
`bmh_4k_ui/docs/qa/classic-extended-ui-input`. The fixtures use actual club/player
data with a controlled desktop layout. No results are fabricated. A successful
compile or CI run alone is not native Windows/macOS UI verification.

Tracked reports: [#51](https://github.com/Weyoune/openbmh/issues/51), [#52](https://github.com/Weyoune/openbmh/issues/52), [#53](https://github.com/Weyoune/openbmh/issues/53), [#54](https://github.com/Weyoune/openbmh/issues/54).

The primary regression run uses the supplied Diamond 3.7 installation. Unrelated
historical rendering fixtures retain their declared reference requirements.
Two existing Classic test fixtures were aligned with the already implemented
stadium-capacity policy and verified edition-specific crest gate; product
validation was not relaxed to satisfy those tests.

The overwrite prompt also maps the actual Football Limited English pointer.
The translation copies exactly that four-byte pointer and preserves adjacent
empty slots. The launcher translation-cache revision is advanced, so an existing
cached English installation receives the newly supported prompt as well.
