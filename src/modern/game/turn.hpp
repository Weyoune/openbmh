// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/turn.hpp"
// The "Weiter" loop of the original (DailyInterest) - everything between two desks of the
// human managers: the day steps, the weekly and monthly processing, the matchdays with
// their preparation, the cups, the season end and the season change.
//
// Built up step by step from the parts verified with tests/week_check (which uses these
// functions, so every regression run checks them against the recordings).

#include "game/match_engine.hpp"
#include "game/random.hpp"
#include "modern/game/season.hpp"
#include "game/sportschau_presentation.hpp"
#include "modern/game/modern_state.hpp"
#include "game/transfer_allocator.hpp"

namespace bmh::modern {

// Whether the career plays the leagues (CompetitionSelection bit 0, "full game"); without them
// there is no weekly or monthly processing.
bool full_game(const ModernState& g);
// DailyInterest: the daily share price run (DailyInterest3) only with the leagues on
// (CompetitionSelection bit 0) - the world game without leagues keeps its prices.
void day_stock_market(ModernState& g, GameRandom& game);
// The end of AdvanceTurn: EventEnd(TurnRuntimeField), with the value layout field the program
// start leaves there, sets Turn_EventEnd_RuntimeField = 1.
void event_end(ModernState& g);

// The rest of the day step DayStepRest after the date moved on: on a training day (the
// previous day's weekday code 2 or 6) the weekly processing (DayStepRest3, full game,
// when the new day has weekday code 3), the decay of the familiarity and every human
// manager's training; then the month change (DayStepRest2) when the month moved on.
// Returns false on a reported state/context failure; see MinuteReport::reason.
bool day_step_rest(ModernState& g, GameRandom& game, WatcomRandom& rand, MinuteReport& report,
                   uint16_t source_season_day,
                   TransferAllocatorContext allocator = TransferAllocatorContext::unsupported);

// The month change part alone (full game only, when the new day is in another month).
bool month_step(ModernState& g, GameRandom& game, WatcomRandom& rand, MinuteReport& report);

}  // namespace bmh::modern

namespace bmh::modern {


// A day without an event (the "Weiter" loop): the shares move (DailyInterest3), then the
// date counts on and the daily interest is booked (the start of DayStepRest). The rest
// of the day step (day_step_rest) follows.
void plain_day(ModernState& g, GameRandom& game, WatcomRandom& rand);

// A calendar entry whose competitions are all switched off (e.g. 0x4006 without the
// option CompetitionSelection bit 1) is still an event for DailyInterest: AdvanceTurn prepares an
// empty matchday (DayCompetitions2, the matchday number, the manager order) without a desk;
// then the date counts on (with the daily interest) as after any event.
bool empty_event_day(ModernState& g, const MatchdayContext& ctx, GameRandom& game, WatcomRandom& rand, MinuteReport& report);

// 1 July (DailyInterest: CareerSession_PairingsRuntimeDraw_ExecutableField4 == 1, CareerSession_PairingsRuntimeDraw_ExecutableField5 == 7): the season is over -
// CareerSession_RuntimeError_RuntimeField4 = 1, the prizes for top scorers and fair play (SeasonAwards); no event is
// due any more (EventPasses = 0). The managers' "Saisonende" desk follows.
bool is_season_end(const ModernState& g);
void season_end_day(ModernState& g);

}  // namespace bmh::modern

#include <vector>
#include "modern/game/desk.hpp"

namespace bmh::modern {

// AdvanceTurn up to its desk loop for the day's calendar entry: the match groups
// (DayCompetitions2), the matchday number (CurrentMatchday) and the shuffled order of the
// managers' desks (PrepareEvent, kept in NextDeskManager).
std::vector<MatchGroup> prepare_event(ModernState& g, uint16_t calendar_entry, const MatchdayContext& ctx, GameRandom& game,
                                      WatcomRandom& rand, MinuteReport& report);

// One pass of that desk loop for the manager at `position` of the order: ActiveManager /
// DeskPosition, a pre-match offer (PreMatchOffer; true = its message box comes first), the
// pools coupon (BuildPoolsCoupon) and entering the desk (DeskDate).
bool enter_event_desk(ModernState& g, int position, const std::vector<MatchGroup>& groups, GameRandom& game,
                      WatcomRandom& rand, MinuteReport& report, bool prepare_pre_match_offer = true,
                      const DeskAutosaveHook& autosave = {});

}  // namespace bmh::modern

namespace bmh::modern {

// The end of a matchday after its last phase (the part of FinishEvent / AdvanceTurn after
// the matches): the human boards' substitution marks are cleared, every group is processed
// (process_group; a league's result screen - when shown - stirs the numbers, its results go
// into the season table, a cup's into its rounds), the managers' match settings changed
// during the matches (record +0xd2..+0xd9, option byte 2) return to `saved_records` /
// `saved_options` (taken at the kick-off), the leagues count their round and the cups of the
// day move on.
void finish_event(ModernState& g, std::vector<MatchGroup>& groups, uint16_t calendar_entry, const std::vector<uint8_t>& buttons,
                  const std::vector<uint8_t>& saved_records, const std::vector<uint8_t>& saved_options, GameRandom& game,
                  WatcomRandom& rand, MinuteReport& report, const SeasonCupDrawHook& cup_draw = {},
                  const SportschauPresentationHooks* sportschau = nullptr,
                  const std::vector<ShowEvent>* show_events = nullptr,
                  bool source_sportschau_replay = false,
                  const SportschauRatingTables* ratings = nullptr);

}  // namespace bmh::modern
