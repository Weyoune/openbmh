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

Loaded executable images are not rewritten to the former shared rule profile. Career strength ranges are read from all nine pairs of the original table. Normalized simulation values from 0.2/NextGen are not backported.

The six-budget monthly environment implementation has been checked against the provided CD and Diamond 3.6/3.7 program code. Its budget increments are read from that program's own table. Capability recognition uses verified code fingerprints, never directory names. Unrecognized environment implementations cannot be edited or simulated through a guessed model; they require separate compatibility verification. Older Diamond academy patches are not silently treated as the monthly model. The old floppy executable remains unsupported, and Football Limited remains experimental.

Release acceptance of individual editions is recorded separately from their recognition. Source compatibility and automated tests do not establish full original-game or platform validation.

## Diamond 3.7 foreign crests

The supplied edition controls whether foreign league clubs use individual
original crests or the original neutral crest. OpenBMH recognizes the verified
Diamond 3.7 drawing gate and uses its database/save selectors and W atlases.
Older drawing gates retain their own behaviour; no external crest pack is needed.
