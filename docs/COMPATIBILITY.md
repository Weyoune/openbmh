# Compatibility decisions

OpenBMH aims to preserve the familiar game while handling malformed input and unsafe edge cases explicitly. A reconstruction can remain faithful without reproducing a crash or pretending to implement an unknown path.

- Content such as names, text, graphics and edition-specific tables is loaded from the user's installation. The loaded program retains its own rule-table values; loading no longer applies the former shared replacement profile.
- Safety checks reject transactions that would exhaust required return capacity, use stale identities or lose a valid state. Unsupported operations report their limitation rather than inventing an outcome.
- Calendar progression handles boundary days and seasonal transitions without relying on unsafe fixed-day assumptions.
- Saves are written from the committed session state. Saving does not deliberately replay unrelated game actions.
- The reconstruction uses two deterministic random generators with explicit integer semantics. This supports reproducible runs, but is not a promise of complete bit-for-bit equivalence to every original edition or across changed engine versions.

Intentional changes in code should use the marker `OpenBMH deviates from the original:` and explain their purpose. Adding a new behaviour or balance change requires a concrete description and relevant tests; this page is not permission to substitute guessed rules.

The [generated differences catalogue](DIFFERENCES.md) lists these markers with links to their source files.

## Application exit confirmation

Closing the window, exiting with Escape or confirming the desktop's exit control
opens a native confirmation dialog. BMH data uses German text and Football
Limited data uses English. Enter, Escape and closing the confirmation cancel the
exit; only the explicit Quit/Beenden button permits normal shutdown. Cancelling
keeps the current dialog or match owner alive without a turn or implicit save.
An unavailable native message box also cancels the request. This applies to
voluntary application exits, including quit events received during animations;
normal completed-career handling remains separate.

## Original edition rules in 1.0

Loaded executable images retain their own rule tables. Career strength ranges are read from all nine pairs of the supplied original table.

The six-budget monthly environment implementation has been checked against the provided CD and Diamond 3.6/3.7 program code. Its budget increments are read from that program's own table. Capability recognition uses verified code fingerprints, never directory names. Unrecognized environment implementations cannot be edited or simulated through a guessed model; they require separate compatibility verification. Older Diamond academy patches are not silently treated as the monthly model. The old floppy executable remains unsupported, and Football Limited remains experimental.

Release acceptance of individual editions is recorded separately from their recognition. Source compatibility and automated tests do not establish full original-game or platform validation.

OpenBMH deviates from the original: graphical startup shows the supplied static
137.VGA title with its embedded palette, without the DOS loading animation.
It waits for a complete left or right mouse click before the game options.
The title does not advance on a timer; its confirming press and release are
consumed so they cannot also change a setup option.

Setup restores ordinary scene-backed manager dialogs before a career is created
or loaded. The executable's boot-only black dialog mode is process state and is
not restored from native save files. Football Limited's translated data includes
its own verified calendar year and weekday operands; German epochs are not used
as a substitute for the English edition.

## Diamond 3.7 foreign crests

The supplied edition controls whether foreign league clubs use individual
original crests or the original neutral crest. OpenBMH recognizes the verified
Diamond 3.7 drawing gate and uses its database/save selectors and W atlases.
Older drawing gates retain their own behaviour; no external crest pack is needed.

The automatic lineup installs the supplied foreign-player rule switch alongside the career tables. Where enabled, it restores the original three/two-player limit, reserve swaps with board-coordinate preservation and the short-lineup outcome when domestic reserves are unavailable. This does not consume additional random draws.

New careers correct the original all-zero EM qualification placeholders to the original national-team placeholders. Supplied nonzero seeds and restored savegame state are not rewritten by this initialization.

## Stadium extension in1.0.3

The user-requested stadium extension raises standard A–H maxima to14/24/14/20/
14/24/14/20thousand places (144,000total), keeping larger supplied custom limits
where the saved byte fields can represent them. Original unit prices remain
edition-owned. Each stand's base build period is at least two weeks per1,000
places of its expanded maximum and never shorter than the edition's original
period. Contractor shortening remains unchanged.

The original Diamond3.6/3.7 table's14k stands really used25weeks versus30weeks
for8k and37weeks for18k. That was original data, not a mistaken timer. Expanded
limits/time policy is an intentional improvement. The UI now validates quotes
against the actual edition and this shared policy rather than hardcoded Diamond
prices and sizes. Existing eight capacity/construction bytes and saves are retained.
## Daily interest at large native balances

OpenBMH deviates from the original: daily debt and deposit interest uses 64-bit
intermediate arithmetic before the original integer divisions. The balance-plan
preview uses the same calculation. Original rates, division order and random
consumption remain unchanged. Account balances and accumulated statistics retain
the native 32-bit save representation; this change does not add unlimited account
balances or change the original high-cash deduction policy.
OpenBMH deviates from the original: the legacy transfer market allows an owned
listing to open incoming offers before its weekly expiry. Select the player and
use the sale action (or Enter). Cancel keeps the listing and offers unchanged;
acceptance uses the existing buyer eligibility, player salary qualification and
transaction rules. Human-buyer loans still require their separate consent and
allocator context and are unavailable in this early review. Viewing/canceling
consumes no RNG. The weekly auction remains unchanged.

The transfer overview processes one selected player per action and renders action
feedback in its footer. A cleared unsuccessful listing can be listed again
immediately; there is no additional cooldown. The manager listing action refuses a full ordinary market
without allocating a reserved special-offer row. These are explicit
application outcomes instead of the Original's destructive overflow/dialog paths.

OpenBMH deviates from the original: a newly initialized squad board entry clears
retirement, refused-negotiation and month-processing flags left by its previous
occupant. The Original leaves that byte intact when reusing the row, so purchases
and academy arrivals can inherit a refusal to renew. Existing players' genuine
retirement/refusal flags remain effective. The initial academy contract remains
two years; a young arrival can immediately negotiate any supported renewal term.
This corrects the reused-row mechanism; it does not establish a fixed 200–300
season threshold or repair ambiguous refusal flags in existing saved careers.
