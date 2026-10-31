# Other games: mixed BMH/FLTD layout

The 1.2 overview keeps the Original 320×240 surface and two columns of four competition controls:

| Left | Right |
| --- | --- |
| DFB-Pokal | WM/EM-Qualifikation |
| UEFA-Cup | Super-Cup |
| Champions League | FA Cup |
| Pokalsieger-Cup | League Cup / Koka Kola Cup |

Germany/England controls follow the selected club systems. National qualification follows International. European pages require club play. Unavailable competition data is shown explicitly as not initialized; no European/national fixtures are fabricated by the UI adapter.

## Shared Super-Cup page

The two competitions remain separate engine competitions and separate saved results. Only their menu page is combined. With both countries enabled, the German Super-Cup and English Charity Shield have individual country/competition headings and panels, separated by a 60-native-pixel row distance. The whitespace is not a match hitbox. This avoids a shared semifinal-like bracket or aggregate score.

With one country disabled, only that country's final remains and uses the original centered final position. A missing draw is marked within its own section instead of inventing teams or suppressing the other final. No second round or complete-view switch is introduced for the combined finals page.

## Existing rendering and safe identities

`world12_other_games_state` projects the typed snapshot into the existing `OtherGamesScreen`. Original assets, sprite buttons, arrows, row text, crown markers, score phase/return-leg formatting and club-detail labels are reused. Club averages call the same Core helper as the legacy overview; rendering does not draw randomness or mutate the world.

UI-local row tokens are mapped separately to global 32-bit club IDs. Requests carry a typed manager/club/revision/scope/sequence identity. Team-info requests carry the global club ID and never pass the local token as an engine ID. Legacy dispatch explicitly refuses typed requests. Read-only typed navigation supports the eight selectors, Original detail windows, complete view and stored domestic cup stages/rounds; the window owner must advance its sequence after accepted navigation.

The existing single-edition projection/rendering path remains under its source layout. Its geometry/action tests still run against the supplied reference installation.

## QA and integration boundary

Data-free tests cover the selector order, scope removal, two independent scores, missing draws, wide club IDs, detail pagination and stale typed navigation. Additional supplied-asset tests render all eight pages and exercise both final hitboxes, the non-clickable gap and FA Cup's selector.

`world12_other_games_demo` exports seven Original-asset views from supplied private data. Its supercup scores are explicit **synthetic UI examples**, not played career results; generated images/Original content are kept only in the private QA repository.

This is the typed overview and navigation adapter, not a completed GUI career hookup. Initial supercup participant selection and European/national competition owners remain pending. Their absence is displayed explicitly. No RNG, scores, cup winners or engine stage links are changed by opening or navigating this screen.
