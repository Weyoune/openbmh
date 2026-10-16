// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/turn_driver.hpp"

#include <algorithm>
#include <array>
#include <exception>

#include "game/desk.hpp"
#include "game/driver.hpp"
#include "game/matchday.hpp"
#include "game/matchday_setup.hpp"
#include "game/trace.hpp"
#include "game/national.hpp"
#include "game/season.hpp"
#include "game/turn.hpp"
#include "game/week.hpp"

namespace bmh {
namespace {

constexpr uint32_t kCalendar = ::bmh::layout::CompetitionCalendar;
// The calendar entry of a season day. The table has 366 days (0-365); a season that starts
// on 1 July and spans 29 February ends on day 366, where the original reads the next bytes
// of its data (the fixture pointer table at CareerSession_RuntimeError_ExecutableField) - no competition. OpenBMH deviates from
// the original here: such a day has no calendar entry (0) instead of whatever lies behind.
uint16_t calendar_day(const GameState& g, int day) {
    const uint32_t at = kCalendar + uint32_t(day) * 2;
    return day >= 0 && g.memory.has(at + 1) && at + 1 < ::bmh::layout::CareerSession_RuntimeError_ExecutableField ? g.memory.u16(at) : uint16_t(0);
}
constexpr uint32_t kDeskButtons = ::bmh::layout::LeaguePresentationOptions;
constexpr uint32_t kSavedCompetition = ::bmh::layout::SavedCompetition;
constexpr uint32_t kCoachActive = ::bmh::layout::CoachActive;
constexpr uint32_t kOriginalManagerCount = ::bmh::layout::OriginalManagerCount;
constexpr uint32_t kManagerCount = ::bmh::layout::ManagerCount;
constexpr uint32_t kManagerOrder = ::bmh::layout::NextDeskManager;
constexpr uint32_t kDeskPosition = ::bmh::layout::DeskPosition;
constexpr uint32_t kActiveManager = ::bmh::layout::ActiveManager;
constexpr uint32_t kEventPasses = ::bmh::layout::EventPasses;

// DailyInterest sets the saved loop count once per calendar date: three Hall
// rounds on the first of two consecutive Hall days, two on the last day.
// Completing a round does not run another date/training/interest step.
void start_event_passes(GameState& g, uint16_t entry) {
    if (!g.memory.has(kEventPasses)) return;
    uint8_t passes = 1;
    if (entry == 0x4002) {
        const uint32_t next = kCalendar + uint32_t(g.season_day() + 1) * 2;
        passes = g.memory.has(next + 1) && g.memory.u16(next) == 0x4002 ? 3 : 2;
    }
    g.memory.set8(kEventPasses, passes);
}

bool consume_event_pass(TurnDriver& driver, GameState& g) {
    if (!g.memory.has(kEventPasses)) return false;
    // Only the Hall calendar cell has multiple passes. Old OpenBMH saves
    // carried a stale Hall count into ordinary dates; do not repeat leagues.
    const bool hall = driver.calendar_entry == 0x4002 && g.indoor;
    const uint8_t count = hall ? g.memory.u8(kEventPasses) : 1;
    const uint8_t remaining = count == 0 ? 0 : uint8_t(count - 1);
    g.memory.set8(kEventPasses, remaining);
    return hall && remaining != 0;
}

std::vector<uint8_t> read_buttons(const GameState& g) {
    std::vector<uint8_t> result;
    if (!g.memory.has(kDeskButtons + 0xb3)) return result;
    result.reserve(0xb4);
    for (uint32_t i = 0; i < 0xb4; ++i) result.push_back(g.memory.u8(kDeskButtons + i));
    return result;
}

MatchdayContext matchday_context(const GameState& g, std::vector<uint8_t>& referees) {
    MatchdayContext context;
    context.month = month_code(g.date);
    context.season_day = g.season_day();
    context.weekday = weekday_code(g.date);
    context.referees = &referees;
    return context;
}

bool saved_national_nomination_desk(const GameState& g, const std::vector<MatchGroup>& groups,
                                   uint16_t calendar_entry) {
    const auto& mem = g.memory;
    if (!groups.empty() || g.manager_teams.size() != 1 || g.manager_teams[0] != kNoTeam ||
        !mem.has(kCalendar + uint32_t(g.season_day()) * 2 + 1) ||
        !mem.has(kSavedCompetition + 3) || !mem.has(kCoachActive) ||
        !mem.has(kOriginalManagerCount) || !mem.has(kManagerCount) ||
        !mem.has(kManagerOrder + 1) || !mem.has(kDeskPosition) || !mem.has(kActiveManager))
        return false;
    const uint16_t saved_entry = mem.u16(kCalendar + uint32_t(g.season_day()) * 2);
    if (saved_entry != calendar_entry || day_competitions(g, saved_entry) != 0x800000 ||
        mem.u32(kSavedCompetition) != 0 || mem.u8(kCoachActive) == 0 ||
        mem.u8(kManagerCount) != 1 || mem.u8(kOriginalManagerCount) < 1 ||
        mem.u8(kOriginalManagerCount) > 4 || mem.u16(kManagerOrder) >= mem.u8(kOriginalManagerCount) ||
        mem.u8(kDeskPosition) != 0 || mem.u8(kActiveManager) != 0)
        return false;
    return true;
}

TurnAdvanceResult fail(TurnDriver& driver, const GameState& g, const MinuteReport& report,
                       const char* fallback) {
    driver.stage = TurnDriver::Stage::Failed;
    TurnAdvanceResult out;
    out.kind = TurnDeskKind::Failed;
    out.manager_slot = g.memory.has(::bmh::layout::ActiveManager) ? int(g.memory.u8(::bmh::layout::ActiveManager)) : -1;
    out.date = g.date;
    out.revision = driver.revision;
    out.reason = report.reason.empty() ? fallback : report.reason;
    return out;
}

TurnAudioEvent audio_event(TurnDriver& driver, TurnAudioKind kind) {
    // Cue mapping is deliberately absent until a concrete original call site is proven.
    return {driver.session_id, driver.next_event_id++, kind, std::nullopt};
}

TurnAdvanceResult desk_result(TurnDriver& driver, const GameState& g, TurnDeskKind kind,
                              bool offer_pending = false) {
    TurnAdvanceResult out;
    out.kind = kind;
    out.manager_slot = g.memory.has(::bmh::layout::ActiveManager) ? int(g.memory.u8(::bmh::layout::ActiveManager)) : -1;
    out.date = g.date;
    out.revision = driver.revision;
    out.offer_pending = offer_pending;
    return out;
}

bool run_current_matchday(TurnDriver& driver, GameState& g, GameRandom& game,
                          WatcomRandom& rand, MinuteReport& report,
                          std::vector<TurnAudioEvent>& events) {
    const std::vector<uint8_t> saved_records = g.manager_records;
    const std::vector<uint8_t> saved_options = g.manager_options;
    MatchdayContext context = matchday_context(g, driver.referees);

    turn_step = "kick_off";
    kick_off(g, driver.groups, context, rand, game, report,
             driver.sportschau_source.intro_after_matchday != 0,
             driver.sportschau_source.rated_club_threshold);
    if (report.unsupported) return false;
    if (driver.sportschau_source.intro_after_matchday != 0 && driver.sportschau.kickoff) {
        const auto intro = matchday_intro_view(g, driver.groups);
        if (intro) driver.sportschau.kickoff(g, *intro);
    }
    bool manager_match_played = false;
    for (const MatchGroup& group : driver.groups)
        for (int m = 0; m < group.matches(); ++m) {
            if (group.void_mask() >> m & 1) continue;
            const size_t fixture = size_t(m * 2);
            if (fixture + 1 < group.fixtures.size() &&
                (g.manager_of(group.fixtures[fixture]) >= 0 ||
                 g.manager_of(group.fixtures[fixture + 1]) >= 0))
                manager_match_played = true;
        }
    // Matchday resolution is synchronous. Emit cues only for fixtures involving a
    // human-managed club so background AI schedules do not flood the audio queue.
    if (manager_match_played) events.push_back(audio_event(driver, TurnAudioKind::kickoff_whistle));

    MatchEngine engine{g, driver.groups, game, rand, driver.referees};
    if (driver.original_sound) {
        engine.original_sound = [&driver, &g](uint8_t id) { driver.original_sound(id, g); };
        if (manager_match_played) engine.original_sound(3); // EndPhase / ReadButtons, crowd.
    }
    std::vector<ShowEvent> sportschau_show_events;
    if (driver.sportschau.fixture) engine.show = &sportschau_show_events;
    engine.month = context.month;
    engine.buttons = read_buttons(g);
    engine.reality_rules = driver.reality_rules;
    engine.live_scene_visible = g.live_scene && driver.reality_rules;
    engine.phase_results = driver.phase_results;
    if (driver.live_reality) {
        engine.live_reality = [&driver, &g](const LiveRealityEvent& event) {
            return driver.live_reality(event, driver.groups, g);
        };
    }
    // LiveDeskDisabled is a saved settings byte. It also changes attitude, assistant
    // substitutions and RNG consumption, so a background driver must preserve it.
    engine.live_desk_disabled = g.memory.has(::bmh::layout::LiveDeskDisabled) && g.memory.u8(::bmh::layout::LiveDeskDisabled) != 0;
    engine.alternate_event_roll = driver.alternate_match_event_roll;
    engine.match_events_disabled = driver.disable_match_events;
    engine.leave_screens = true;
    if (driver.live_shootout) {
        engine.live_shootout = [&driver, &g](const LiveShootoutEvent& event) {
            driver.live_shootout(event, driver.groups, g);
        };
    }
    if (driver.live_booking) {
        engine.live_booking = [&driver, &g](const LiveBookingEvent& event) {
            driver.live_booking(event, driver.groups, g);
        };
    }
    if (driver.live_set_piece) {
        engine.live_set_piece = [&driver, &g](const LiveSetPieceEvent& event) {
            driver.live_set_piece(event, driver.groups, g);
        };
    }
    if (driver.live_shot) {
        engine.live_shot = [&driver, &g](const LiveShotEvent& event) {
            driver.live_shot(event, driver.groups, g);
        };
    }
    if (driver.live_chance) {
        engine.live_chance = [&driver, &g](const LiveChanceEvent& event) {
            driver.live_chance(event, driver.groups, g);
        };
    }
    if (driver.live_ball) {
        engine.live_ball = [&driver, &g](const LiveBallEvent& event) {
            driver.live_ball(event, driver.groups, g);
        };
    }
    if (driver.live_event) {
        engine.live_event = [&driver, &engine](const LiveMatchEvent& event) {
            driver.live_event(event, engine, driver.groups, engine.state);
        };
    }
    std::vector<std::vector<std::array<int, 2>>> previous(driver.groups.size());
    for (size_t gi = 0; gi < driver.groups.size(); ++gi) {
        const MatchGroup& group = driver.groups[gi];
        previous[gi].resize(size_t(group.matches()));
        for (int m = 0; m < group.matches(); ++m) {
            int home = 0, away = 0;
            match_score(group, m, home, away);
            previous[gi][size_t(m)] = {home, away};
        }
    }
    bool manager_match_scored = false;
    auto collect_goals = [&](int) {
        for (size_t gi = 0; gi < driver.groups.size(); ++gi) {
            const MatchGroup& group = driver.groups[gi];
            for (int m = 0; m < group.matches(); ++m) {
                const size_t fixture = size_t(m * 2);
                if ((group.void_mask() >> m & 1) || fixture + 1 >= group.fixtures.size() ||
                    (g.manager_of(group.fixtures[fixture]) < 0 &&
                     g.manager_of(group.fixtures[fixture + 1]) < 0))
                    continue;
                int home = 0, away = 0;
                if (!match_score(group, m, home, away)) continue;
                auto& old = previous[gi][size_t(m)];
                const int goals = std::max(0, home - old[0]) + std::max(0, away - old[1]);
                manager_match_scored = manager_match_scored || goals > 0;
                old = {home, away};
            }
        }
    };
    auto after_minute = [&](int minute) {
        collect_goals(minute);
        if (driver.live_minute) driver.live_minute(minute, engine, driver.groups, g);
    };
    turn_step = "play_matchday";
    play_matchday(engine, g, driver.groups, engine.buttons, game, rand, report, after_minute);
    if (report.unsupported) return false;
    // The app receives the whole synchronous turn at once. One summary goal cue
    // per manager matchday is bounded and avoids replaying a stream of missed goals.
    if (manager_match_scored) events.push_back(audio_event(driver, TurnAudioKind::goal));
    if (manager_match_played) events.push_back(audio_event(driver, TurnAudioKind::full_time_whistle));

    turn_step = "finish_event";
    store_group_descriptors(g, driver.groups, true);
    finish_event(g, driver.groups, driver.calendar_entry, engine.buttons,
                 saved_records, saved_options, game, rand, report, driver.season_cup_draw,
                 &driver.sportschau, &sportschau_show_events,
                 driver.sportschau_source.intro_after_matchday != 0,
                 driver.sportschau_source.ratings ? &*driver.sportschau_source.ratings : nullptr);
    return !report.unsupported;
}

bool next_event(TurnDriver& driver, GameState& g, GameRandom& game, WatcomRandom& rand,
                MinuteReport& report, bool& offer_pending, bool after_event,
                bool first_day_preprocessed = false, bool repeat_event = false) {
    for (int day = 0; day < 366 * 5; ++day) {
        // DailyInterest enters AdvanceTurn on season day zero even when its
        // calendar cell is empty. The prepared first Wednesday is a manager
        // desk, not a day to skip while seeking the first scheduled fixture.
        const bool opening_desk = first_day_preprocessed && g.season_day() == 0;
        // DailyInterest advances DaysIntoSeason before DayStepRest on a quiet day,
        // but after DayStepRest when completing an event. Preserve that Source-visible
        // context for a nested Bundestrainer re-takeover; the typed date is already one
        // day ahead in the latter case.
        const uint16_t source_rest_day = uint16_t(
            g.date - g.season_start - uint32_t(after_event ? 1 : 0));
        turn_step = "day_step_rest";
        if (!first_day_preprocessed &&
            !day_step_rest(g, game, rand, report, source_rest_day,
                           driver.transfer_allocator_context)) return false;
        // DailyInterest tests for 1 July only after DayStepRest has advanced a day. The
        // prepared first day of a new season is not such a step: when 1 July is itself the
        // first Wednesday (2037), the season change leaves the date there, and testing it
        // again started the season change over and over (endless loop after ten seasons).
        const bool day_stepped = !first_day_preprocessed;
        first_day_preprocessed = false;
        if (day_stepped && is_season_end(g)) {
            turn_step = "season_end_day";
            season_end_day(g);
            // DailyInterest calls AdvanceTurn after the annual awards. DayCompetitions2's
            // closing branch supplies FF opponents / competition32 without
            // preparing teams, fixtures or cycling manager options. Its old
            // descriptors remain available to BuildPoolsCoupon's coupon scan.
            if (full_game(g)) { // DailyInterest calls this desk loop only with leagues.
                driver.calendar_entry = calendar_day(g, g.season_day());
                const uint32_t closing_competitions = day_competitions(g, driver.calendar_entry);
                g.memory.set32(kSavedCompetition, closing_competitions == 0x800000 ? 0 : closing_competitions);
                draw_matchday_number(g, rand);
                shuffle_manager_order(g, game);
                driver.manager_position = 0;
                driver.national_nomination_desk = false;
                turn_step = "enter_closing_desk";
                offer_pending = enter_event_desk(g, 0, driver.groups, game, rand, report, true, driver.desk_autosave);
                if (report.unsupported) return false;
            }
            driver.stage = TurnDriver::Stage::SeasonEndDesk;
            ++driver.revision;
            return true;
        }

        turn_step = "stock market";
        if (after_event) day_stock_market(g, game);
        after_event = false;
        const uint16_t entry = calendar_day(g, g.season_day());
        if (!(entry & 0x7fff) && !opening_desk) {
            turn_step = "plain_day";
            plain_day(g, game, rand);
            continue;
        }
        turn_step = "calendar day";
        const uint32_t competitions = day_competitions(g, entry);
        driver.referees = driver.referee_source;  // clear the per-day chosen flags
        MatchdayContext context = matchday_context(g, driver.referees);
        if (competitions == 0 && !opening_desk) {
            turn_step = "empty_event_day";
            if (!empty_event_day(g, context, game, rand, report)) return false;
            after_event = true;
            continue;
        }
        if (competitions == 0x800000 && !opening_desk) {
            // The return means that a human national-coach desk follows, not
            // that the calendar step succeeded. The ordinary no-coach case
            // still performs the Source national-team preparation and continues.
            const bool has_national_coach = national_team_day(g, rand, report);
            if (report.unsupported) return false;
            const bool enter_national_desk = has_national_coach && driver.enable_national_nomination_desks;
            // AdvanceTurn clears the transient competition result before its empty
            // setup. Keep the legacy/no-coach behavior byte-identical; the opted-in
            // coach path clears it before preparing the real national desk.
            if (enter_national_desk) {
                if (g.manager_teams.size() != 1 || g.manager_teams[0] != kNoTeam) {
                    report.fail("national nomination returned a coach without the one-manager national context");
                    return false;
                }
                if (g.memory.has(kSavedCompetition + 3)) g.memory.set32(kSavedCompetition, 0);
                context.national_team_day = true;
            }
            turn_step = "setup_matchday (national)";
            const std::vector<MatchGroup> none = setup_matchday(g, 0, context, game, rand, report);
            if (report.unsupported) return false;
            if (!none.empty()) {
                report.fail("national team day unexpectedly prepared match groups");
                return false;
            }
            draw_matchday_number(g, rand);
            shuffle_manager_order(g, game);
            if (enter_national_desk) {
                driver.calendar_entry = entry;
                driver.groups = none;
                driver.manager_position = 0;
                turn_step = "enter_event_desk";
                offer_pending = enter_event_desk(g, 0, driver.groups, game, rand, report, true, driver.desk_autosave);
                if (report.unsupported) return false;
                driver.national_nomination_desk = true;
                driver.stage = TurnDriver::Stage::MatchdayDesks;
                ++driver.revision;
                return true;
            }
            event_end(g);
            ++g.date;
            daily_interest(g, rand);
            after_event = true;
            continue;
        }

        driver.calendar_entry = entry;
        if (!repeat_event) start_event_passes(g, entry);
        repeat_event = false;
        turn_step = "prepare_event";
        driver.groups = prepare_event(g, entry, context, game, rand, report);
        if (report.unsupported) return false;
        if (!matchday_needs_manager_desks(g, entry, driver.groups)) {
            std::vector<TurnAudioEvent> background_events;
            if (!run_current_matchday(driver, g, game, rand, report, background_events)) return false;
            if (consume_event_pass(driver, g)) {
                first_day_preprocessed = true;
                repeat_event = true;
                continue;
            }
            ++g.date;
            daily_interest(g, rand);
            after_event = true;
            continue;
        }
        driver.manager_position = 0;
        turn_step = "enter_event_desk";
        offer_pending = enter_event_desk(g, 0, driver.groups, game, rand, report, true, driver.desk_autosave);
        if (report.unsupported) return false;
        driver.stage = TurnDriver::Stage::MatchdayDesks;
        ++driver.revision;
        return true;
    }
    report.fail("turn driver exceeded five seasons without a manager desk");
    return false;
}

}  // namespace

bool begin_turn_driver(TurnDriver& driver, const GameState& g, std::vector<MatchGroup> groups,
                       uint16_t calendar_entry, std::vector<uint8_t> referees,
                       uint64_t session_id, uint64_t revision, std::string& error) {
    if (driver.stage != TurnDriver::Stage::Empty || revision == 0 || session_id == 0) {
        error = "turn driver is already active or has invalid identity";
        return false;
    }
    if (g.manager_teams.empty()) {
        error = "turn driver needs at least one human manager";
        return false;
    }
    driver.stage = TurnDriver::Stage::MatchdayDesks;
    driver.calendar_entry = calendar_entry;
    driver.manager_position = g.memory.has(::bmh::layout::DeskPosition) ? int(g.memory.u8(::bmh::layout::DeskPosition)) : 0;
    driver.revision = revision;
    driver.session_id = session_id;
    driver.groups = std::move(groups);
    driver.referee_source = std::move(referees);
    driver.referees = driver.referee_source;
    driver.national_nomination_desk = saved_national_nomination_desk(
        g, driver.groups, calendar_entry);
    return true;
}

TurnAdvanceResult advance_to_next_desk(TurnDriver& driver, GameState& g,
                                       const TurnAdvanceRequest& request,
                                       GameRandom& game, WatcomRandom& rand,
                                       MinuteReport& report) {
    const int active_manager = g.memory.has(::bmh::layout::ActiveManager) ? int(g.memory.u8(::bmh::layout::ActiveManager)) : -1;
    // AdvanceTurn restores the retained club-order owner after every temporary
    // national desk, including desks with real fixtures. That owner can be1–3
    // although the visible/runtime national manager is0. Qualify the completed
    // pending batch below without turning an actual match into a nomination day.
    const bool national_handoff = driver.enable_national_nomination_desks &&
        (driver.national_nomination_desk ||
         (g.manager_teams.size() == 1 && g.manager_teams[0] == kNoTeam &&
          g.memory.has(kCoachActive) && g.memory.u8(kCoachActive) != 0));
    if (national_handoff) {
        const int original_count = g.memory.has(kOriginalManagerCount)
            ? int(g.memory.u8(kOriginalManagerCount)) : 0;
        const int source_owner = g.memory.has(kManagerOrder + 1)
            ? int(g.memory.u16(kManagerOrder)) : -1;
        const bool completed_pending = g.memory.has(kDeskPosition) &&
            g.memory.u8(kDeskPosition) == 1 && active_manager == source_owner &&
            original_count >= 1 && original_count <= 4 && source_owner >= 0 &&
            source_owner < original_count;
        if (!completed_pending) {
            TurnAdvanceResult out;
            out.kind = TurnDeskKind::Failed;
            out.manager_slot = active_manager;
            out.date = g.date;
            out.revision = driver.revision;
            out.reason = "national_nomination_pending_not_completed";
            return out;
        }
        if (driver.stage != TurnDriver::Stage::MatchdayDesks ||
            request.manager_slot != 0 || driver.manager_position != 0 ||
            request.revision != driver.revision || g.manager_teams.size() != 1 ||
            g.manager_teams[0] != kNoTeam || !g.memory.has(kCoachActive) ||
            g.memory.u8(kCoachActive) == 0 || !g.memory.has(kManagerCount) ||
            g.memory.u8(kManagerCount) != 1) {
            TurnAdvanceResult out;
            out.kind = TurnDeskKind::Failed;
            out.manager_slot = active_manager;
            out.date = g.date;
            out.revision = driver.revision;
            out.reason = "stale_national_coach_manager_or_revision";
            return out;
        }
    } else if ((driver.stage != TurnDriver::Stage::MatchdayDesks && driver.stage != TurnDriver::Stage::SeasonEndDesk) ||
               request.manager_slot != active_manager || request.revision != driver.revision) {
        TurnAdvanceResult out;
        out.kind = TurnDeskKind::Failed;
        out.manager_slot = active_manager;
        out.date = g.date;
        out.revision = driver.revision;
        out.reason = "stale_manager_or_revision";
        return out;
    }

    // Claim the request before any RNG or state mutation. A failure is terminal for this
    // cursor, so replaying the same request cannot repeat a partially completed turn.
    const auto previous_stage = driver.stage;
    driver.stage = TurnDriver::Stage::Failed;
    std::vector<TurnAudioEvent> events;
    bool offer_pending = false;
    try {
        if (driver.manager_position + 1 < int(g.manager_teams.size())) {
            ++driver.manager_position;
            const bool prepare_pre_match_offer =
                !(driver.loaded_matchday_resume && driver.suppress_remaining_pre_match_offers);
            turn_step = "enter_event_desk";
            offer_pending = enter_event_desk(g, driver.manager_position, driver.groups,
                                             game, rand, report, prepare_pre_match_offer, driver.desk_autosave);
            if (report.unsupported) return fail(driver, g, report, "manager pre-match desk unsupported");
            driver.stage = previous_stage;
            ++driver.revision;
            return desk_result(driver, g, previous_stage == TurnDriver::Stage::SeasonEndDesk
                ? TurnDeskKind::SeasonEndDesk : TurnDeskKind::NextDesk, offer_pending);
        }
        if (previous_stage == TurnDriver::Stage::SeasonEndDesk) {
            // Every manager has now left the closing desk. Do not carry the
            // loaded desk-resume policy into the annual transition or a new event.
            driver.complete_loaded_manager_loop();
            // AdvanceTurn's completed manager-desk loop leaves DeskPosition == ManagerCount
            // before DailyInterest enters the annual transition. This native byte is
            // serialized at modal checkpoints; the next real desk resets it.
            if (g.memory.has(::bmh::layout::DeskPosition))
                g.memory.set8(::bmh::layout::DeskPosition, g.memory.has(::bmh::layout::ManagerCount)
                    ? g.memory.u8(::bmh::layout::ManagerCount) : uint8_t(g.manager_teams.size()));
            const auto outcome = season_transition(g, game, rand, report, driver.season_pause
                ? std::function<void(const char*)>([&](const char* screen) { driver.season_pause(screen, g); })
                : std::function<void(const char*)>{}, driver.season_highscore, driver.season_tax,
                driver.season_promotion, driver.season_cup_draw, driver.transfer_allocator_context);
            if (report.unsupported || outcome == SeasonTransitionOutcome::Failed)
                return fail(driver, g, report, "season transition unsupported");
            if (outcome == SeasonTransitionOutcome::CareerComplete) {
                driver.stage = TurnDriver::Stage::CareerComplete;
                ++driver.revision;
                return desk_result(driver, g, TurnDeskKind::CareerComplete);
            }
            driver.stage = TurnDriver::Stage::MatchdayDesks;
            // season_end_steps has already performed the calendar days up to the new
            // season's first Wednesday; the next loop starts from that prepared date.
            if (!next_event(driver, g, game, rand, report, offer_pending, false, true))
                return fail(driver, g, report, "season transition could not reach a desk");
            auto out = desk_result(driver, g, driver.stage == TurnDriver::Stage::SeasonEndDesk
                ? TurnDeskKind::SeasonEndDesk : TurnDeskKind::NextDesk, offer_pending);
            out.audio_events = std::move(events);
            return out;
        }

        // The current manager desk loop is complete, so these flags have no further
        // desk to govern. Consume them before match simulation and future event setup.
        driver.complete_loaded_manager_loop();
        if (driver.enable_national_nomination_desks && driver.national_nomination_desk) {
            // AdvanceTurn's national nomination event has an empty desk, not a
            // matchday: Source skips kick_off/play/finish and advances the event
            // calendar directly after the pending cleanup performed by App.
            driver.national_nomination_desk = false;
            event_end(g);
            ++g.date;
            daily_interest(g, rand);
            if (!next_event(driver, g, game, rand, report, offer_pending, true))
                return fail(driver, g, report, "national nomination could not advance to next desk");
            auto out = desk_result(driver, g, driver.stage == TurnDriver::Stage::SeasonEndDesk
                ? TurnDeskKind::SeasonEndDesk : TurnDeskKind::NextDesk, offer_pending);
            out.audio_events = std::move(events);
            return out;
        }
        // A legacy caller can bind a genuine saved national desk, but it keeps the
        // historic matchday path unless the App explicitly opted into this lifecycle.
        driver.national_nomination_desk = false;
        const auto result_tournament_name = g.tournament_name;
        if (!run_current_matchday(driver, g, game, rand, report, events))
            return fail(driver, g, report, "matchday unsupported");
        driver.last_results.valid = true;
        driver.last_results.date = g.date;
        driver.last_results.calendar_entry = driver.calendar_entry;
        driver.last_results.matchday = g.memory.has(::bmh::layout::CurrentMatchday) ? g.memory.u16(::bmh::layout::CurrentMatchday) : 0;
        driver.last_results.groups = driver.groups;
        driver.last_results.tournament_name = result_tournament_name;
        const bool repeat = consume_event_pass(driver, g);
        if (!repeat) {
            ++g.date;
            daily_interest(g, rand);
        }
        if (!next_event(driver, g, game, rand, report, offer_pending, !repeat, repeat, repeat))
            return fail(driver, g, report, "could not advance to next desk");
        auto out = desk_result(driver, g, driver.stage == TurnDriver::Stage::SeasonEndDesk
            ? TurnDeskKind::SeasonEndDesk : TurnDeskKind::NextDesk, offer_pending);
        out.audio_events = std::move(events);
        return out;
    } catch (const std::exception& ex) {
        report.fail(std::string("turn driver exception: ") + ex.what() + " (in " + std::string(turn_step.load()) + ")");
        return fail(driver, g, report, "turn driver exception");
    }
}

}  // namespace bmh
