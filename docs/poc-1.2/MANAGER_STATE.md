# Typed human-manager state

The first persistent human-manager state is the verified financial slice. It is separate from the still-pending tactical/live-match/complete-career adapter. Adding a financial state does not cause human clubs to be simulated as AI.

## Financial fields and Original behavior

World schema version 5 stores optional manager finances: signed cash balance and unsigned credit, cumulative income/expense counters and paid-player-bonus total. Older schemas leave this state uninitialized rather than inventing starting cash. The imported master remains manager-free.

`manager_finance_from_legacy` extracts only the verified financial words of an explicitly supplied initialized manager record. No club, player, name-pool or pointer reference is copied. It is a narrow financial extractor, not a legacy career/save converter.

`calculate_manager_booking` projects those words into a temporary single-account workspace and calls the existing `book_money` routine. It preserves insufficient-funds checks against balance plus credit, forced payments, income/expense selection, no-money difficulty behavior and Original 32-bit wrapping. Unrelated record bytes are not interpreted or copied back. Difficulty/force/statistics are trusted engine policy from the career owner; clients cannot choose them to bypass rules.

The core supports both initialized zero balances and missing state. Missing state is an explicit error. Proper starting values must come from the actual career initialization owner, which is not yet integrated into the typed GUI flow.

## Publication and ownership

The application service in `src/app/world12_manager.*` checks manager seat, stable club ID, session scope and revision before calculating. Financial state must already exist. Changed words and revision publish together through non-throwing assignments. Rejected, zero-effect and no-money operations leave state/revision unchanged. Replaying an applied payment with the old revision is rejected.

The owner scope/revision is runtime request ownership and is not source/mod data. It must be tied to the future career session and reset with a fresh scope on load. Financial values themselves survive world and O1.2 snapshot roundtrips. Existing match projection's manager-ownership comparison includes this new state, so concurrent financial changes cannot silently be overwritten by an old match projection.

## Evidence and remaining work

- 288 cases compare typed booking against direct Original `book_money`, including no-money mode, credit-funded negative cash, denied/forced payments, signed extremes and unsigned cumulative-counter wrapping.
- Ownership tests cover sparse manager seats, missing state, wrong clubs/scopes, rejected payments, replay protection and codec/snapshot persistence.
- Additional private checks extract a manager from the supplied Original save and match direct bookings for three signed amounts. The reference save is read-only and is not converted or overwritten.

Human lineup/full tactics (see the newer [tactical scalar adapter](MANAGER_TACTICS.md)), contracts/bonus inputs, source-compatible initial career setup, bank/loan policies, daily/weekly/monthly finances and the GUI/CLI request binding remain pending. This financial slice is not a completed playable human career or a 1.2 release.
