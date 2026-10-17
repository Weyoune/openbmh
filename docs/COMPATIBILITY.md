# Compatibility decisions

OpenBMH aims to preserve the familiar game while handling malformed input and unsafe edge cases explicitly. A reconstruction can remain faithful without reproducing a crash or pretending to implement an unknown path.

- Content such as names, text, graphics and edition-specific tables is loaded from the user's installation. OpenBMH's shared rule base supplies simulation values; selecting another data edition does not select a separate engine.
- Safety checks reject transactions that would exhaust required return capacity, use stale identities or lose a valid state. Unsupported operations report their limitation rather than inventing an outcome.
- Calendar progression handles boundary days and seasonal transitions without relying on unsafe fixed-day assumptions.
- Saves are written from the committed session state. Saving does not deliberately replay unrelated game actions.
- The reconstruction uses two deterministic random generators with explicit integer semantics. This supports reproducible runs, but is not a promise of complete bit-for-bit equivalence to every original edition or across changed engine versions.

Intentional changes in code should use the marker `OpenBMH deviates from the original:` and explain their purpose. Adding a new behaviour or balance change requires a concrete description and relevant tests; this page is not permission to substitute guessed rules.

The [generated differences catalogue](DIFFERENCES.md) lists these markers with links to their source files.

## Environment and presentation in 0.1.2

The desktop environment provides three youth teams and three staff
budgets, each set to zero through six stars. Youth budgets are monthly, can be
changed throughout the season, and contribute to the finance preview, balance
plan and booked expenses. Youth tables develop on first-division matchdays;
promotion, relegation and age-group handover occur at the season change.
The two previously hidden youth groups are available across data editions.
Staff prices and the shared academy player-generation strength bands stay in
effect. Recruitment needs an active middle youth budget and an oldest youth team
within 25 strength points of the senior squad (the full-build mode bypasses the
strength gate). This deliberately
combines the six-budget environment with OpenBMH's existing player-generation rules.

Old saved academy lump sums remain payable once and retain their paid status.
They are not multiplied into twelve monthly payments. Editing that tile replaces
the old amount with the selected monthly budget. There is no new save field.

Live match minutes use the loaded original delay setting (A9), including zero,
instead of an added fixed pause. A9 and the existing scene delays can be adjusted
in Settings; loaded saves and EIN files retain their chosen speeds. VGA retrace
animations use a 70-Hz reference instead of the host monitor's refresh rate.
`--live-frame-ms` and `BMH_LIVE_FRAME_MS` remain explicit diagnostic overrides.
