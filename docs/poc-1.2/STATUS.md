# 1.2 development status

The application is **not yet a playable 1.2 release**. Branch `1.2` remains isolated from 1.1 and stable main.

Completed foundation:

- Fourteen country-aware league descriptors, 252 league clubs, German/English ladders and national cup ownership. Table group selectors expose Germany/England/Europe; only Germany and England are selectable manager countries.
- Strong 32-bit player/club/league/crest identities and separate roster flags. A checked JSON world codec preserves IDs beyond 4,095/65,535 and crest IDs beyond 511 without packed truncation.
- Full 10-team home/away fixture generation; result stores scale with each league's actual round/match count. Original 18/20-team schedules remain supplied content, not guessed replacements.
- Version-separated O1.2 foundational snapshots retain world data, round cursors/results and both RNG seeds. These snapshots are not yet the complete application save path: finances, active cups and suspended transaction ownership still need integration.
- An explicit bounded AI roster projection maps global identities into a legacy match workspace and maps player changes back with stale-input checks. Human projection is rejected until its tactical/financial ownership is implemented; it is not silently treated as AI.
- Private development OBD: 252 league clubs, 538 total club/country records, 5,094 players. Existing 1.1 records/IDs are retained; new source membership is restricted to supplied men's country leagues. Portuguese identities are reconciled instead of duplicated. English tier 2–4 retain source-tier candidates before the reduced fifth group; fictional roster attributes, membership adaptations, amateur fillers and temporary crests are documented.

Validation: synthetic typed-codec, wide-ID, mixed-manager, cross-country loan, fixture uniqueness, save separation/truncation and mapping/ownership tests passed on Linux. The private real-world database also passed decode/encode and every referenced crest atlas was readable. New crests are temporary, explicitly fictional preparation assets.

Added calendar/progression foundation:

- Country-qualified import of both supplied full-season calendars; Germany and England keep separate national-cup/supercup entries, while European/national entries have one shared owner. The English 10-team group receives 18 evenly distributed league dates.
- A shared daily cursor prepares deterministic fixture batches for all leagues on that date. Complete validated score batches commit atomically; partial/stale results and double advancement are rejected before mutation. This coordinator accepts scores from the match layer and does not generate outcomes or consume RNG itself.
- The development O1.2 snapshot schema now retains the complete calendar, next-day cursor and revision. Save validation rejects round/results/calendar inconsistencies. Unresolved cup or national appointments explicitly block league-only advancement.
- Synthetic tests completed all 365 daily transactions and 4,364 league fixtures, saved/reloaded midseason and at season end, and checked mixed same-day country rounds and rollback. These are coordinator tests with test scores, not simulated career matches. Both real supplied calendars imported with all 14 expected league round counts and 75 non-league appointments.

Added weekday alignment and league-table projection:

- A checked reader extracts the English DOS date-setup operands directly, instead of reading German instruction addresses from a translated data image. Unsupported layouts, lengths and invalid operands are rejected. The supplied English source starts on Saturday, 1 July 1995; the supplied German source starts on Wednesday, 1 July 2026.
- Calendar sources can now be aligned to the common weekday epoch using the nearest whole-source shift. The supplied English calendar moves by +3 days, preserving every appointment's weekday and all 75 retained non-league appointments. An appointment crossing the season boundary causes an explicit error rather than wrapping or disappearing. Tests cover all 49 weekday combinations and both 365/366-day boundaries. The application still needs to install this prepared calendar when the new career path is connected.
- A read-only table adapter handles all 14 leagues with global club IDs, home/away and round-range filters, two/three-point modes and explicit point deductions. It reuses Classic's bounded per-league calculation and tie ordering, including its original eight-bit goal totals and form/rank-history behavior. No alphabetical replacement of tied clubs or global-ID truncation is introduced.
- Synthetic comparisons checked 1,170 table configurations directly against the existing Classic implementation for 18/20-team leagues. The newly scheduled ten-team English group separately completed a balanced 18-match-per-club table. Result validation now rejects scores outside the Classic 0–15 goal range at the model/save boundary as well as at day commit.

Remaining: shared application progression, cup handling and promotion settlement, human manager/tactics projection and match-event translation, application save ownership, German country picker and all dependent UI views, complete editor integration, three-season/multiplayer/native release QA. The old loader intentionally rejects the new profile until these gates are complete. No 1.2 binaries are released at this stage.

Added domestic cup score/progression adapter ([details](CUPS.md)):

- Country-owned original stage metadata, wide club identities, separate legs/results/revisions and original aggregate/away-goal resolution. FA Cup and League Cup qualifying stages retain their seeded clubs and linked main/final stages.
- Shared daily transactions can now publish complete league and supported domestic-cup result batches atomically. Missing draws, unresolved ties, incomplete successor stages or foreign/stale results leave every competition and the date unchanged. Indoor/shared/national events still block this domestic-only adapter.
- The development snapshot schema now retains complete cup stage histories and pending return legs. Synthetic tests completed all three domestic cup structures with 203 test-score matches and daily save/reload. All eight supported stage descriptors imported successfully from the private supplied source rules; those metadata probes use synthetic draws.
- Actual original entrant selection/draw calls and their RNG/effects, match simulation, premiums and human/title statistics remain application integration work. This is not a completed playable cup career or a 1.2 release.

Added draw audience and startup symbol:

- Initial and subsequent domestic draw notices are emitted only for countries with a human manager in their current league ladder. Lower divisions count; cup elimination does not hide the remaining country draws. AI-only countries continue processing the same results and stage transitions, with unchanged dates and RNG. The country-independent European/national audience policy remains part of its still-pending adapter.
- The startup screen on the `1.2` working branch now has separate country controls (see the newer scope contract below). Each control retains its native 51×40 Original frame. The source-only three-control layout remains available for Classic comparisons. The preview accepts `--league-symbol source|countries`.
- `tools/world12/prepare_setup_symbol.py` copies Germany, England, globe and load controls from the supplied Original atlases at 1:1 resolution, preserving the Original crossed-out states. Private `KB/SETUP12.VGA` is now a 204×80 four-column/off-on atlas. The world builder takes `--english-data` and includes it. Legacy packages without it keep their source layout. Source hashes and method details remain private; Original sprite bytes are not added to the public repository.

Added automatic domestic entrant selection and draws:

- Initial German/FA/League Cup fields now follow the original division quotas and explicit supplied rankings. English seeds are shuffled separately and retain the reversed reserved-prefix order. The existing legacy code and the typed adapter share the initial/subsequent shuffle and national tie-ordering kernels.
- A new application owner stages complete country result batches, automatic next-round/successor draws and their RNG seeds, publishing them together. First legs and terminal finals make no next-field draws. Hiding an AI-country draw changes only presentation notices. Unsupported/mismatched source rules, bad rankings and late result errors roll back all staged state and randomness.
- The automatic full domestic programme completed 203 test-score matches with daily save/reload, exactly 176 initial GameRandom draws and 420 subsequent Watcom draws. The private real typed world and both supplied source rule images also generated their initial fields and passed the save roundtrip.
- Existing seeded-cup synthetic regression and private original-save season/follow-up draw regressions passed, including original participants, group traversal, overview effects and exact RNG assertions. These are Linux engine tests; the combined 1.2 application remains incomplete and no actual match-score simulation or playable career is claimed.

Added independent country/national selection ([contract](GAME_SCOPE.md)):

- The setup now provides Germany, England, International and Load as four complete Original controls, each with its original off/on state. International retains the national-team meaning confirmed by the user; European club competitions belong to club play.
- A fresh-world scope filter removes excluded German/English club entities, including amateur/foreign-fallback entries, rather than retaining them as AI clubs. IDs are preserved. Disabled leagues have no memberships, rounds, results, cup initialization or calendar events. National-only scope retains no club leagues.
- Scoped world schema version 2 and the existing development snapshot preserve the selected world; full version-1 master data remains readable. All seven scope combinations have synthetic year, conditional-cup, absent-entity and save/codec checks.
- The country-selection request is reserved for the typed manager/GUI-career adapter. Legacy session confirmation explicitly refuses it, preventing a country-selection screen from starting the old wrong-country game while 1.2 integration remains incomplete.
