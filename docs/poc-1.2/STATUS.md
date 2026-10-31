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

- Country-qualified import of both supplied full-season calendars; Germany and England keep separate national-cup/supercup entries, while European/national entries have one shared owner. The English 10-team group receives 18 evenly distributed league dates. Common weekday-epoch alignment remains a separate application integration step.
- A shared daily cursor prepares deterministic fixture batches for all leagues on that date. Complete validated score batches commit atomically; partial/stale results and double advancement are rejected before mutation. This coordinator accepts scores from the match layer and does not generate outcomes or consume RNG itself.
- The development O1.2 snapshot schema now retains the complete calendar, next-day cursor and revision. Save validation rejects round/results/calendar inconsistencies. Unresolved cup or national appointments explicitly block league-only advancement.
- Synthetic tests completed all 365 daily transactions and 4,364 league fixtures, saved/reloaded midseason and at season end, and checked mixed same-day country rounds and rollback. These are coordinator tests with test scores, not simulated career matches. Both real supplied calendars imported with all 14 expected league round counts and 75 non-league appointments.

Remaining: shared application progression, cup handling and promotion settlement, human manager/tactics projection and match-event translation, application save ownership, German country picker and all dependent UI views, complete editor integration, three-season/multiplayer/native release QA. The old loader intentionally rejects the new profile until these gates are complete. No 1.2 binaries are released at this stage.
