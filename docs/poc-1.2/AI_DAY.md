# Atomic AI league-day owner

`src/app/world12_ai_day.*` connects actual Original-derived AI scoring and after-match settlement to the typed calendar/results coordinator. It is a bounded league-only transaction, not the application's complete daily career transaction.

## Inputs and publication

The caller supplies a current `DayTicket`, a nonzero session scope/revision, an initialized rule workspace and authoritative per-fixture conditions. Conditions identify each scheduled league appointment and use the snapshot's absolute career date, not the season-local ticket day. Weather, referee and attendance generation still belong to the future career owner; clients must not submit arbitrary match-policy inputs.

The service validates the complete snapshot, owner, ticket, group sizes and dates. Every human-owned participant is rejected before any match runs. An unrelated human manager does not prevent an empty league date; this does not perform that manager's daily economy. Every non-league appointment is rejected, including European/national, indoor and domestic-cup dates: those require their separate competition owners.

A staged copy runs each scheduled league in calendar order on its existing shared Original minute clock. All groups share one deterministic pair of RNG streams and the persistent development balance/rotation. After all groups finish, their results, post-match player/club changes, both seeds and one calendar/date transition are committed together. Empty league dates advance once without consuming match RNG.

Before publication the service rechecks the live snapshot and session owner. Failure in a later league, calendar commit or final validation publishes none of the candidate's earlier changes. External changes made while scoring are preserved and cause rejection. Publication uses a non-throwing snapshot move and increments the session revision; replaying an applied identity is rejected. Loaded sessions must use a fresh runtime scope.

## Verification

Synthetic tests compare 261 actual first-date fixtures across full German/English, German/Premier-League fallback and English-only worlds against the direct Core league-group APIs plus the calendar coordinator. Save/reload repeats must reproduce the complete snapshot, including development, cards, records, scores, cursor and RNG. Additional tests cover empty dates, sparse human seats, preflight human blocking, stale ownership/tickets, incomplete or wrong-date conditions, failure in the final league, exhausted calendar revision after scoring, non-league guards and externally changed state/owners.

`world12_day_validate <WORLD12.JSON> <German rules directory> <supplied FLTD.EXE>` is a private-data probe. It imports the supplied fixture tables and weekday-aligned league calendars and runs/repeats the first three league dates for each of those scopes. It explicitly removes and reports non-league appointments for this isolated verification. Its fixed weather/attendance inputs and excluded cup/national events are not evidence of a complete career or Original-save equivalence.

Human tactical state and match ownership, cup/national/indoor simulation, daily/weekly/monthly finances, promotion settlement and GUI progression remain pending. This packet does not enable the 1.2 profile in the legacy GUI.
