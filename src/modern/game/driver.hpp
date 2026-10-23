// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/driver.hpp"
// The matchday driver after the kick-off (FinishEvent + BuildSideRecords, BMH.exe V2.24).
//
// The driver plays the phases one after another with EndPhase(first, last,
// stoppage, phase):
//      1..45 (no stoppage), 46..90 (+ up to 4), then 91..105 and 106..120 (+ up to 1,
//      phase 1 = extra time; indoor tournaments play 1..20 and 21..40 instead).
// A phase ends when every match has ended; a phase in which no match is running draws
// nothing. At the end of a phase (end_phase):
//   - if any match played in the phase, the result screen of every group is called
//     unless switched off at the desk, including groups already settled in regular
//     time; leaving a league screen stirs the random numbers (stir_random),
//   - at full time (90, 120, and 40 indoors) every match gets FinishMatch and is marked
//     done, except cup ties decided on the day (descriptor flag 8) that are still level
//     on aggregate (cup_tie): they go into extra time, after 120 minutes into the
//     penalty shoot-out (CupTie2).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "game/match_engine.hpp"
#include "game/random.hpp"
#include "modern/game/modern_state.hpp"

namespace bmh::modern {

// CupTie: state of a cup tie after match m of a group: 0 the home side is through,
// 1 the away side, 2 still level. Two-legged ties add the other leg (its home team is
// this match's away team); with descriptor flag 0x10 away goals count double while the
// match is in regular time.
int cup_tie(const MatchGroup& grp, int m);

// Whether the result screen of a cup (CupGroupOverviewEffects with a phase end minute) is shown.
bool cup_screen_shown(const std::vector<uint8_t>& buttons, int cup_id, int minute);

// One phase: minutes first..last+stoppage until every match has ended. `after_minute`
// is called after each simulated minute (for checks). Stops early if the engine reports
// an unsupported path.
void play_phase(MatchEngine& engine, std::vector<MatchGroup>& groups, int first, int last, int stoppage, int phase,
                MinuteReport& report, const std::function<void(int minute)>& after_minute = {});

// The end of EndPhase for a phase that ended at minute `last`.
void end_phase(ModernState& g, std::vector<MatchGroup>& groups, int last, const std::vector<uint8_t>& buttons,
               GameRandom& game, WatcomRandom& rand, MinuteReport& report,
               const std::function<void(const PhaseResultRequest&, const std::vector<MatchGroup>&,
                                        const ModernState&)>& presenter = {});

// Nonblocking end-of-phase result barrier. The cursor borrows the original mutable
// objects for its whole lifetime; do not copy/move it or independently mutate those
// objects while pending. Native viewer actions use the owning synchronous App dispatcher;
// it must preserve group identity and never resume simulation. Cup entry effects precede
// presentation; league exit RNG and match settlement belong to the single ACK.
// Only presentation metadata and a bounded last-ACK record are owned.

// First call starts/resumes the phase; repeating begin with the same phase while waiting
// polls the identical request. A completed cursor may be reused for a later phase.
PhaseEndOutput begin_phase_end(PhaseEndCursor& cursor, uint64_t phase_id, ModernState& g,
                               std::vector<MatchGroup>& groups, int last,
                               const std::vector<uint8_t>& buttons, GameRandom& game,
                               WatcomRandom& rand, MinuteReport& report);
// An empty reply polls without effects. Only an exact current (phase, group, revision)
// reply resumes the group. Duplicate last ACK returns its cached continuation; older or
// mismatched replies are rejected without state/RNG/cursor advancement.
PhaseEndOutput continue_phase_end(PhaseEndCursor& cursor,
                                  const std::optional<PhaseResultReply>& reply = std::nullopt);

// All phases of a matchday (not indoor): regular time, extra time where needed.
// `stop_at` (45, 90, 105 or 120) stops at the whistle of that phase, before its result
// screens and match bookkeeping (end_phase); 0 plays everything.
void play_matchday(MatchEngine& engine, ModernState& g, std::vector<MatchGroup>& groups,
                   const std::vector<uint8_t>& buttons, GameRandom& game, WatcomRandom& rand, MinuteReport& report,
                   const std::function<void(int minute)>& after_minute = {}, int stop_at = 0);

}  // namespace bmh::modern
