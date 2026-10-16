// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/turn.hpp"
#include "game/trace.hpp"

#include "game/month.hpp"
#include "game/national.hpp"
#include "game/week.hpp"
#include "game/weekly.hpp"

namespace bmh {

bool full_game(const GameState& g) { return g.memory.has(::bmh::layout::CompetitionSelection) && (g.memory.u8(::bmh::layout::CompetitionSelection) & 1) != 0; }

void event_end(GameState& g) {
    if (g.memory.has(::bmh::layout::Savestate_Layout_ExecutableField8)) g.memory.set8(::bmh::layout::Turn_EventEnd_RuntimeField, 1);
}

void day_stock_market(GameState& g, GameRandom& game) {
    if (full_game(g)) stock_market(g, game);
}

bool month_step(GameState& g, GameRandom& game, WatcomRandom& rand, MinuteReport& report) {
    if (!full_game(g) || g.date == 0 || month_code(g.date - 1) == month_code(g.date)) return true;
    month_change(g, rand, report, &game);  // DayStepRest2
    return !report.unsupported;
}

bool day_step_rest(GameState& g, GameRandom& game, WatcomRandom& rand, MinuteReport& report,
                   uint16_t source_season_day, TransferAllocatorContext allocator) {
    // DayStepRest tests CareerSession_PairingsRuntimeDraw_ExecutableField6 after DeskDate2(date - 1): the weekday of the
    // previous day decides whether it is a training day
    const int wd = weekday_code(g.date - 1);
    const bool training = wd == 2 || wd == 6;
    const bool month = full_game(g) && g.date > 0 && month_code(g.date - 1) != month_code(g.date);
    // the Bundestrainer leaves the national team for the day's processing: on a training day
    // the national squad trains first (FamiliarityDecay), then the slots are swapped back
    // (NationalCoachHandBack); at the end he takes it over again (SwapManagerSlots2, ActiveManager left at
    // the number of managers by the loop below)
    const bool coach = (training || month) && national_coach_active(g);
    if (coach) {
        if (training) familiarity_decay(g, game, rand);
        national_coach_hand_back(g);
    }
    if (training) {
        if (full_game(g) && weekday_code(g.date) == 3) {
            turn_step = "weekly_processing";
            weekly_processing(g, game, rand, report, allocator);  // DayStepRest3
            if (report.unsupported) return false;
        }
        turn_step = "training";
        familiarity_decay(g, game, rand);
        for (size_t m = 0; m < g.manager_teams.size(); m++) train_squad(g, int(m), game, rand);
    }
    turn_step = "month_step";
    const bool ok = month_step(g, game, rand, report);
    if (coach) national_coach_take_over(g, int(g.manager_teams.size()), source_season_day);
    return ok;
}

}  // namespace bmh

#include "game/matchday_setup.hpp"
#include "game/season.hpp"

namespace bmh {

void plain_day(GameState& g, GameRandom& game, WatcomRandom& rand) {
    day_stock_market(g, game);
    g.date++;
    daily_interest(g, rand);
}

bool empty_event_day(GameState& g, const MatchdayContext& ctx, GameRandom& game, WatcomRandom& rand, MinuteReport& report) {
    setup_matchday(g, 0, ctx, game, rand, report);
    turn_step = "prepare_event: matchday number";
    draw_matchday_number(g, rand);  // CurrentMatchday
    shuffle_manager_order(g, game);
    event_end(g);
    g.date++;
    daily_interest(g, rand);
    return !report.unsupported;
}

bool is_season_end(const GameState& g) { return g.date > 0 && month_code(g.date) == 7 && month_code(g.date - 1) != 7; }

void season_end_day(GameState& g) {
    if (national_coach_active(g)) national_coach_hand_back(g);  // DailyInterest: NationalCoachHandBack first
    if (g.memory.has(::bmh::layout::CareerSession_RuntimeError_RuntimeField4)) g.memory.set8(::bmh::layout::CareerSession_RuntimeError_RuntimeField4, 1);
    season_awards(g);
    if (g.memory.has(::bmh::layout::EventPasses)) g.memory.set8(::bmh::layout::EventPasses, 0);
}

}  // namespace bmh

#include "game/desk.hpp"

namespace bmh {

std::vector<MatchGroup> prepare_event(GameState& g, uint16_t calendar_entry, const MatchdayContext& ctx, GameRandom& game,
                                      WatcomRandom& rand, MinuteReport& report) {
    std::vector<MatchGroup> groups = setup_matchday(g, day_competitions(g, calendar_entry), ctx, game, rand, report);
    draw_matchday_number(g, rand);  // CurrentMatchday
    turn_step = "prepare_event: manager order";
    const std::vector<int> order = shuffle_manager_order(g, game);
    for (size_t k = 0; k < order.size() && g.memory.has(::bmh::layout::NextDeskManager + uint32_t(k) * 2); k++)
        g.memory.set16(::bmh::layout::NextDeskManager + uint32_t(k) * 2, uint16_t(order[k]));
    return groups;
}

bool enter_event_desk(GameState& g, int position, const std::vector<MatchGroup>& groups, GameRandom& game,
                      WatcomRandom& rand, MinuteReport& report, bool prepare_pre_match_offer,
                      const DeskAutosaveHook& autosave) {
    const int manager = g.manager_teams.size() > 1 ? g.memory.u16(::bmh::layout::NextDeskManager + uint32_t(position) * 2) : position;
    if (g.memory.has(::bmh::layout::DeskPosition)) g.memory.set8(::bmh::layout::DeskPosition, uint8_t(position));
    if (g.memory.has(::bmh::layout::ActiveManager)) g.memory.set8(::bmh::layout::ActiveManager, uint8_t(manager));
    // AdvanceTurn(param1 != 0) suppresses only PreMatchOffer on the remaining
    // desks of a loaded manager loop. Coupon preparation and desk entry still run.
    // (an out-of-range access below names its step: the error reaches the player as text)
    const char* step = "pre_match_offer";
    try {
        const bool offer = prepare_pre_match_offer
            ? pre_match_offer(g, manager, game, rand, report) : false;
        step = "build_pools_coupon";
        build_pools_coupon(g, groups, &rand);                                      // BuildPoolsCoupon
        step = "enter_desk";
        enter_desk(g, manager, game, rand, autosave);                          // DeskDate
        return offer;
    } catch (const std::out_of_range& e) {
        throw std::out_of_range(std::string(e.what()) + " (" + step + ", manager " + std::to_string(manager) + ")");
    }
}

}  // namespace bmh

#include "formats/binary.hpp"
#include "game/cup.hpp"
#include "game/matchday.hpp"

namespace bmh {

void finish_event(GameState& g, std::vector<MatchGroup>& groups, uint16_t calendar_entry, const std::vector<uint8_t>& buttons,
                  const std::vector<uint8_t>& saved_records, const std::vector<uint8_t>& saved_options, GameRandom& game,
                  WatcomRandom& rand, MinuteReport& report, const SeasonCupDrawHook& cup_draw,
                  const SportschauPresentationHooks* sportschau, const std::vector<ShowEvent>* show_events,
                  bool source_sportschau_replay, const SportschauRatingTables* ratings) {
    if (report.unsupported) return;
    for (size_t m = 0; m < g.manager_teams.size(); m++)
        for (int s = 0; s < 24; s++)
            for (int k = 0; k < 2; k++) {
                const size_t at = m * 0x840 + size_t(s) * 0x58 + 0x44 + size_t(k) * 2;
                g.tactics.at(at) = 0xFF, g.tactics.at(at + 1) = 0x0F;
            }
    auto show_intro = [&](int group_index) {
        if (!sportschau || !sportschau->intro) return;
        SportschauIntroView view;
        view.group_index = group_index;
        for (size_t i = 0; i < groups.size() && i <= 0xff; ++i) {
            const MatchGroup& group = groups[i];
            view.groups.push_back({uint8_t(i), group.desc.at(0), le32(group.desc, 0xcc),
                                   int16_t(sportschau_source_setting_index(g, group))});
        }
        sportschau->intro(g, view);
    };
    show_intro(-1);
    for (size_t group_index = 0; group_index < groups.size(); ++group_index) {
        MatchGroup& grp = groups[group_index];
        // FinishEvent sets DeskPosition to the current match-group index. Its final nonempty
        // iteration leaves the last index visible to the following cup presentation.
        if (g.memory.has(::bmh::layout::DeskPosition) && group_index <= 0xff)
            g.memory.set8(::bmh::layout::DeskPosition, uint8_t(group_index));
        if (group_index <= 0xff) show_intro(int(group_index));
        if (ratings && !source_sportschau_replay) {
            // FinishEvent/BuildSideRecords clear eleven scores outside the CareerSession_SportschauSourceOptions_ExecutableField gate.
            // The loaded EXE owner still performs that reset when it skips
            // MatchdayIntro entirely; old slot words remain untouched.
            for (size_t i = 0; i < 11; ++i) {
                grp.desc.at(0x70 + i) = 0;
                const uint32_t address = ::bmh::layout::Turn_FinishEvent_ExecutableField + uint32_t(group_index) * 0xd0 + uint32_t(i);
                if (group_index < 6 && g.memory.has(address)) g.memory.set8(address, 0);
            }
        }
        process_group(g, grp, game, rand, report, int(group_index),
                      sportschau ? sportschau->fixture : SportschauFixtureHook{}, show_events,
                      source_sportschau_replay && sportschau ? &sportschau->replay : nullptr,
                      source_sportschau_replay ? ratings : nullptr);
        if (report.unsupported) return;
        if (source_sportschau_replay && ratings) {
            const auto awards = finish_sportschau_awards(g, grp, int(group_index));
            if (awards && sportschau && sportschau->awards) sportschau->awards(g, *awards);
            if (sportschau && sportschau->team_info) {
                const auto info = sportschau_team_info_view(g, grp, int(group_index));
                if (info) sportschau->team_info(g, *info);
            }
        }
        const uint32_t id = le32(grp.desc, 0xcc);
        if (id < 0x15) {
            if (league_screen_shown(buttons, 1, 90, int(id))) stir_random(game, rand);
            const int round = g.league_rounds.at(id);
            for (int m = 0; m < grp.matches(); m++) g.results.at(size_t(id) * 0x17C + size_t(round) * 10 + size_t(m)) = grp.results.at(size_t(m));
        } else {
            store_cup_results(g, grp);
        }
    }
    // FinishEvent/BuildSideRecords call EvaluatePoolsCoupon(1) after every group's processing,
    // before returning to AdvanceTurn's calendar/round advancement. Result evaluation
    // at the second half is separate; pools_open does not gate this settlement.
    settle_pools_coupon(g, groups);
    for (size_t m = 0; m < g.manager_teams.size() && !saved_records.empty(); m++) {
        for (size_t k = 0; k < 8; k++) g.manager_records.at(m * 0xB0C + 0xd2 + k) = saved_records.at(m * 0xB0C + 0xd2 + k);
        g.manager_options.at(m * 5 + 2) = saved_options.at(m * 5 + 2);
    }
    const uint32_t competitions = day_competitions(g, calendar_entry);
    advance_league_rounds(g, competitions & 0x1F, rand, report,
                          sportschau ? sportschau->winner : CompetitionWinnerHook{});
    for (int bit = 5; bit < 0x13; bit++)
        if ((competitions >> bit) & 1) advance_cup(g, bit - 5, rand, report, &game, cup_draw,
                                                sportschau ? sportschau->winner : CompetitionWinnerHook{});
    event_end(g);
}

}  // namespace bmh
