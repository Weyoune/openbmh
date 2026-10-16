#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "formats/binary.hpp"
#include "formats/fbm.hpp"
#include "formats/le.hpp"
#include "formats/m3d.hpp"
#include "formats/savestate.hpp"
#include "formats/textd.hpp"
#include "game/career.hpp"
#include "game/desk_actions.hpp"
#include "game/matchday.hpp"
#include "game/matchday_setup.hpp"
#include "game/save.hpp"
#include "game/schedule.hpp"
#include "game/turn.hpp"
#include "game/turn_driver.hpp"
#include "game/week.hpp"

using namespace bmh;
namespace {
void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
uint32_t record(const GameState& g, size_t offset) { return le32(g.manager_records, offset); }
void set_record(GameState& g, size_t offset, uint32_t value) {
    for (int i = 0; i < 4; ++i) g.manager_records.at(offset + size_t(i)) = uint8_t(value >> (i * 8));
}
uint16_t record16(const GameState& g, size_t offset) {
    return uint16_t(g.manager_records.at(offset) | uint16_t(g.manager_records.at(offset + 1)) << 8);
}
void set_record16(GameState& g, size_t offset, uint16_t value) {
    g.manager_records.at(offset) = uint8_t(value);
    g.manager_records.at(offset + 1) = uint8_t(value >> 8);
}
struct Run {
    GameState state;
    TurnDriver driver;
    GameRandom game{665367};
    WatcomRandom rand{589388519};
};
Run load_run(const std::filesystem::path& input, const LeImage& exe, const TextDb& texts,
             const Database& database) {
    const auto file = load_savegame(input);
    check(file.checksum_ok && file.version == "V2.24", "Native INPUT checksum/version");
    const DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
    Run run;
    auto& g = run.state;
    g = GameState::from_save(SaveState(file.stream, counts), counts);
    competition_pointers(g.memory, exe);
    g.texts = &texts;
    check(g.manager_teams.size() == 1 && g.memory.u8(::bmh::layout::DeskPosition) == 0 &&
          g.memory.u8(::bmh::layout::ActiveManager) == 0, "Native INPUT owns the sole manager's prepared desk");
    check(g.memory.u32(::bmh::layout::SavedCompetition) == 7 && g.memory.u8(::bmh::layout::CareerSession_DispatchToto_RuntimeField) == 11,
          "unchanged Native INPUT contains four league groups and eleven coupon matches");
    std::vector<MatchGroup> groups;
    // Reconstruct only saved league descriptors and transient arrays, as the Native
    // App loader does. prepare_event would replay setup and change the saved RNG path.
    for (int league = 0; league < 4; ++league) {
        MatchGroup group;
        group.desc = group_descriptor(g, size_t(league));
        const auto fixtures = league_fixtures(league, g.league_rounds.at(size_t(league)) + 1);
        check(group.desc.size() == 0xd0 && group.desc[0] == 0 &&
              le32(group.desc, 0xcc) == uint32_t(league) && group.desc[2] == fixtures.size(),
              "Native saved league descriptor agrees with schedule");
        for (const auto& fixture : fixtures) {
            group.fixtures.push_back(fixture.home);
            group.fixtures.push_back(fixture.away);
        }
        const size_t at = size_t(league) * 0x17c + size_t(g.league_rounds.at(size_t(league))) * 10;
        group.results.assign(g.results.begin() + long(at), g.results.begin() + long(at + fixtures.size()));
        group.records.assign(fixtures.size() * 18, 0);
        group.end_minute.assign(fixtures.size(), 0);
        group.spectators.assign(fixtures.size(), 0);
        groups.push_back(std::move(group));
    }
    std::string error;
    const uint16_t entry = g.memory.u16(::bmh::layout::CompetitionCalendar + uint32_t(g.season_day()) * 2);
    check(begin_turn_driver(run.driver, g, std::move(groups), entry, database.raw_referee_properties,
                            13, 1, error), "bind genuine Native turn cursor: " + error);
    for (const auto& group : run.driver.groups)
        for (int m = 0; m < group.matches(); ++m) {
            const uint8_t ref = group.desc.at(0x10 + size_t(m));
            if (ref > 0 && ref < run.driver.referees.size()) run.driver.referees[ref] |= 0x80;
        }
    return run;
}
TurnAdvanceResult advance(Run& run, TurnAdvanceRequest request = {0, 1}) {
    MinuteReport report;
    const auto result = advance_to_next_desk(run.driver, run.state, request, run.game, run.rand, report);
    check(!report.unsupported && result.kind == TurnDeskKind::NextDesk,
          "actual turn reaches next desk: " + result.reason + "; " + report.reason);
    check(run.driver.last_results.valid && !run.driver.last_results.groups.empty(),
          "actual turn completed a real matchday");
    return result;
}
void reject_stale(Run& run, const TurnAdvanceRequest& request) {
    const auto before = save_stream(run.state);
    const auto game = run.game;
    const auto rand = run.rand;
    const auto stage = run.driver.stage;
    const auto revision = run.driver.revision;
    MinuteReport report;
    const auto result = advance_to_next_desk(run.driver, run.state, request, run.game, run.rand, report);
    check(result.kind == TurnDeskKind::Failed && result.reason == "stale_manager_or_revision" &&
          !report.unsupported && save_stream(run.state) == before && run.driver.stage == stage &&
          run.driver.revision == revision && run.game.seed() == game.seed() &&
          run.game.calls() == game.calls() && run.rand.seed() == rand.seed() &&
          run.rand.calls() == rand.calls(), "stale request preserves Native bytes, driver and both RNGs");
}
struct SaveEvidence {
    std::filesystem::path directory;
    bool temporary = true;
    SaveEvidence() {
        if (const char* output = std::getenv("E13_TEST_OUTPUT")) {
            directory = output;
            temporary = false;
        } else directory = std::filesystem::temp_directory_path() /
            ("openbmh-e13-toto-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
    }
    ~SaveEvidence() {
        if (temporary) { std::error_code ignored; std::filesystem::remove_all(directory, ignored); }
    }
};
void round_trip(const Run& run, const LeImage& exe, const std::filesystem::path& path) {
    auto save_rand = run.rand;
    const uint64_t calls = save_rand.calls();
    write_savegame(path, make_savegame(run.state, save_rand));
    check(save_rand.calls() == calls + 4, "Native envelope uses its four Source Watcom save draws on a copy");
    const auto file = load_savegame(path);
    check(file.checksum_ok && file.version == "V2.24", "written Native save freshly reloads with valid checksum/version");
    const DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
    auto reloaded = GameState::from_save(SaveState(file.stream, counts), counts);
    competition_pointers(reloaded.memory, exe);
    reloaded.texts = run.state.texts;
    check(save_stream(reloaded) == save_stream(run.state), "fresh Native reload preserves entire decrypted stream");
    check(reloaded.memory.u32(::bmh::layout::Coupon) == run.state.memory.u32(::bmh::layout::Coupon) &&
          reloaded.memory.u32(::bmh::layout::FixtureDriverSavedValueWord) == run.state.memory.u32(::bmh::layout::FixtureDriverSavedValueWord) &&
          reloaded.memory.u8(::bmh::layout::FixtureDriverSavedFlag) == run.state.memory.u8(::bmh::layout::FixtureDriverSavedFlag) &&
          record(reloaded, 0x314) == record(run.state, 0x314) &&
          record(reloaded, 0x874) == record(run.state, 0x874) &&
          record(reloaded, 0x8a0) == record(run.state, 0x8a0),
          "fresh reload preserves Toto stake, win, hits, balance and income/expense statistics");
    check(run.rand.calls() == calls, "save/reload evidence leaves live Watcom generator untouched");
}
void coupon_test(Run initial, const LeImage& exe) {
    auto& g = initial.state;
    g.level = 1;
    set_record(g, 0x314, 10000000);
    set_record(g, 0x31c, 0);
    g.memory.set32(::bmh::layout::Coupon, 0);
    g.memory.set8(::bmh::layout::LiveDeskDisabled, 1);
    auto control = initial;
    advance(control);
    // Read the actual completed fixture scores from a no-stake control with identical
    // input and RNGs, then submit the winning coupon through the real desk action.
    std::array<uint8_t, 11> tips{};
    int found = 0;
    for (const auto& group : control.driver.last_results.groups)
        for (int m = 0; m < group.matches() && found < 11; ++m) {
            if (group.fixtures.at(size_t(m) * 2) !=
                g.memory.u16(::bmh::layout::DeskActions_SubmitPoolsCoupon_RuntimeField + uint32_t(found) * 4)) continue;
            int home = 0, away = 0;
            check(match_score(group, m, home, away), "control has completed coupon result");
            tips[size_t(found++)] = uint8_t(home > away ? 1 : home == away ? 0 : 2);
        }
    check(found == 11, "all eleven saved coupon fixtures actually played");
    check(submit_pools_coupon(g, 0, 50000, tips), "real desk action submits valid Native coupon");
    SaveEvidence evidence;
    round_trip(initial, exe, evidence.directory / "TOTO_BEFORE.FBM");
    reject_stale(initial, {0, 999});
    reject_stale(initial, {1, 1});
    const auto result = advance(initial);
    check(g.memory.u32(::bmh::layout::Coupon) == 0, "Toto regression: actual advance_to_next_desk must consume the stake");
    check(g.memory.u32(::bmh::layout::FixtureDriverSavedValueWord) == 2500000 && g.memory.u8(::bmh::layout::FixtureDriverSavedFlag) == 11,
          "eleven correct tips pay Native quota 50 and persist eleven hits");
    check(record(g, 0x874) - record(control.state, 0x874) == 2500000 &&
          record(g, 0x8a0) - record(control.state, 0x8a0) == 50000,
          "actual turn books Toto payout and stake once in Native income/expense statistics");
    check(record(g, 0x314) > record(control.state, 0x314) + 2449999,
          "payout reaches account before subsequent daily interest");
    check(initial.game.seed() == control.game.seed() && initial.game.calls() == control.game.calls() &&
          initial.rand.seed() == control.rand.seed() && initial.rand.calls() == control.rand.calls(),
          "Toto production settlement adds no RNG draws to identical actual turns");
    check(result.revision > 1, "actual successful turn consumes request revision");
    reject_stale(initial, {0, 1});
    reject_stale(initial, {0, 1});
    round_trip(initial, exe, evidence.directory / "TOTO_AFTER.FBM");
    std::cout << "coupon: real Native INPUT advance, eleven-hit quota, accounting, RNG, duplicate rejection and fresh Native reload passed\n";
}
void flag_test(const Run& initial, const std::string& mode) {
    // live_desk_disabled is Native saved configuration. alternate_event_roll/match_events_disabled are unsaved EXE
    // configuration owned by TurnDriver; an unrelated memory block is ignored.
    const std::vector<uint32_t> addresses = mode == "runtime_flags"
        ? std::vector<uint32_t>{::bmh::layout::AlternateEventRoll, ::bmh::layout::MatchEventsDisabled} : std::vector<uint32_t>{::bmh::layout::LiveDeskDisabled};
    for (const uint32_t address : addresses)
        for (const uint8_t value : {uint8_t(0), uint8_t(1)}) {
            auto run = initial;
            run.state.memory.add_block(address, 1);
            if (address == ::bmh::layout::LiveDeskDisabled) run.state.memory.set8(address, value);
            else {
                run.state.memory.set8(address, uint8_t(!value));
                if (address == ::bmh::layout::AlternateEventRoll) run.driver.alternate_match_event_roll = value != 0;
                else run.driver.disable_match_events = value != 0;
            }
            int observed = 0;
            run.driver.live_minute = [&](int, MatchEngine& engine, std::vector<MatchGroup>&, GameState& g) {
                ++observed;
                const bool actual = address == ::bmh::layout::LiveDeskDisabled ? engine.live_desk_disabled :
                                    address == ::bmh::layout::AlternateEventRoll ? engine.alternate_event_roll : engine.match_events_disabled;
                check(actual == (value != 0),
                      "engine flag does not reflect its configuration owner at " + std::to_string(address));
                if (address == ::bmh::layout::LiveDeskDisabled)
                    check(actual == (g.memory.u8(address) != 0), "live_desk_disabled reflects current Native memory");
                if (!g.memory.has(::bmh::layout::AlternateEventRoll)) check(!engine.alternate_event_roll, "absent alternate_event_roll keeps false fallback");
                if (!g.memory.has(::bmh::layout::MatchEventsDisabled)) check(!engine.match_events_disabled, "absent match_events_disabled keeps false fallback");
            };
            advance(run);
            check(observed > 0, "real MatchEngine minutes observed");
        }
    std::cout << mode << ": actual live_minute observed Native/EXE configuration and absent-memory defaults\n";
}
void incomplete_test(const Run& initial) {
    for (const bool failed_before_entry : {true, false}) {
        auto run = initial;
        std::array<uint8_t, 11> tips{};
        check(submit_pools_coupon(run.state, 0, 50000, tips), "guard control submits real coupon");
        const auto before = save_stream(run.state);
        const auto game = run.game;
        const auto rand = run.rand;
        MinuteReport report;
        if (failed_before_entry) report.fail("pre-existing incomplete matchday");
        else {
            // A scoreless first group fails inside process_group before group
            // finalization. No synthetic settlement is performed by this test.
            run.driver.groups.front().results.clear();
            run.driver.groups.front().cup_results.clear();
        }
        const auto records = run.state.manager_records;
        const auto options = run.state.manager_options;
        finish_event(run.state, run.driver.groups, run.driver.calendar_entry, {}, records,
                     options, run.game, run.rand, report);
        check(report.unsupported && run.state.memory.u32(::bmh::layout::Coupon) == 50000 &&
              record(run.state, 0x314) == record(initial.state, 0x314) &&
              record(run.state, 0x874) == record(initial.state, 0x874) &&
              record(run.state, 0x8a0) == record(initial.state, 0x8a0),
              "incomplete group processing must preserve pending stake and account statistics");
        check(run.game.seed() == game.seed() && run.game.calls() == game.calls() &&
              run.rand.seed() == rand.seed() && run.rand.calls() == rand.calls(),
              "failed matchday control performs no RNG work");
        if (failed_before_entry)
            check(save_stream(run.state) == before, "already unsupported finish_event is entirely inert");
    }
    std::cout << "incomplete: pre-existing and scoreless-group failure preserve pending Toto stake\n";
}

Run watched_event_run(const Run& initial, uint32_t game_seed, uint32_t watcom_seed) {
    Run run = initial;
    const int manager = int(run.state.memory.u8(::bmh::layout::ActiveManager));
    check(manager == 0 && !run.state.manager_teams.empty(), "event fixture has its real manager owner");
    const int team = run.state.manager_teams.at(size_t(manager));
    check(team >= 0 && team < 90 && size_t(team) < run.state.team_morale.size(),
          "event fixture manager owns a regular Source club with a morale byte");
    // This is an in-memory watched-match test configuration only; the FBM fixture is never
    // rewritten. It makes the real MatchEngine event path eligible for legacy routine.
    run.state.team_morale.at(size_t(team)) |= 0x80;
    if (size_t(manager) * 5 + 1 < run.state.manager_options.size())
        run.state.manager_options[size_t(manager) * 5 + 1] = 0;
    if (run.state.memory.has(::bmh::layout::LiveDeskDisabled)) run.state.memory.set8(::bmh::layout::LiveDeskDisabled, 0);
    run.driver.alternate_match_event_roll = true;
    run.game.set_seed(int32_t(game_seed));
    run.rand.srand(watcom_seed);
    return run;
}

void source_event_hook_test(const Run& initial) {
    auto validate_event = [](const LiveMatchEvent& event, MatchEngine& engine,
                             std::vector<MatchGroup>& groups, GameState& state) {
        check((event.kind == 1 || event.kind == 2) && event.minute >= 1 &&
              event.manager_slot == 0 && event.side >= 0 && event.side <= 1 && event.player >= 0,
              "real Source callback carries red/injury kind, 1-based minute and manager identity");
        check(&groups == &engine.groups && &state == &engine.state &&
              event.group_index < groups.size(), "event group/state references are the active engine owners");
        const auto& group = groups.at(event.group_index);
        check(event.match_index >= 0 && event.match_index < group.matches() &&
              group.fixtures.at(size_t(event.match_index) * 2 + size_t(event.side)) == event.team &&
              group.fixtures.at(size_t(event.match_index) * 2 + size_t(1 - event.side)) == event.opponent,
              "event index resolves to its exact real fixture and side");
        check(state.manager_of(event.team) == event.manager_slot &&
              (state.team_morale.at(size_t(event.team)) & 0x80) && !engine.live_desk_disabled &&
              !state.manager_option(event.manager_slot, 1),
              "event is watched by the owning manager with assistant substitution disabled");
        check(size_t(event.player) < state.players.size(), "event player belongs to loaded Source roster data");
    };
    uint32_t found_game_seed = 0, found_watcom_seed = 0;
    bool found = false;
    // Search only deterministic RNG seeds over the same unmodified loaded career and
    // its real current matchday. No player, fixture, event byte, or saved FBM is edited.
    for (uint32_t seed = 1; seed <= 64 && !found; ++seed) {
        auto probe = watched_event_run(initial, seed, 0x13579bdu ^ seed);
        int callbacks = 0;
        probe.driver.live_event = [&](const LiveMatchEvent& event, MatchEngine& engine,
                                      std::vector<MatchGroup>& groups, GameState& state) {
            validate_event(event, engine, groups, state);
            ++callbacks;
        };
        (void)advance(probe);
        if (callbacks) {
            found = true;
            found_game_seed = seed;
            found_watcom_seed = 0x13579bdu ^ seed;
        }
    }
    check(found, "bounded deterministic seeds produce a real watched red-card/injury event");

    auto legacy = watched_event_run(initial, found_game_seed, found_watcom_seed);
    auto observed = watched_event_run(initial, found_game_seed, found_watcom_seed);
    int callbacks = 0;
    observed.driver.live_event = [&](const LiveMatchEvent& event, MatchEngine& engine,
                                     std::vector<MatchGroup>& groups, GameState& state) {
        validate_event(event, engine, groups, state);
        ++callbacks;
    };
    const auto legacy_result = advance(legacy);
    const auto observed_result = advance(observed);
    check(legacy_result.kind == observed_result.kind && legacy_result.manager_slot == observed_result.manager_slot &&
          legacy_result.date == observed_result.date && legacy_result.revision == observed_result.revision &&
          legacy_result.offer_pending == observed_result.offer_pending &&
          legacy_result.audio_events.size() == observed_result.audio_events.size(),
          "legacy and typed no-op observers return the same completed real turn result");
    for (size_t i = 0; i < legacy_result.audio_events.size(); ++i) {
        const auto& a = legacy_result.audio_events[i];
        const auto& b = observed_result.audio_events[i];
        check(a.session_id == b.session_id && a.event_id == b.event_id && a.kind == b.kind &&
              a.original_id == b.original_id, "turn audio event sequence is unchanged by no-op hook");
    }
    check(callbacks > 0, "typed Core/TurnDriver hook was called by a generated Source event");
    check(save_stream(legacy.state) == save_stream(observed.state),
          "no-op event hook leaves complete Native Save bytes identical to legacy path");
    check(legacy.game.seed() == observed.game.seed() && legacy.game.calls() == observed.game.calls() &&
          legacy.rand.seed() == observed.rand.seed() && legacy.rand.calls() == observed.rand.calls(),
          "no-op event hook consumes no additional Game or Watcom RNG draws");
    auto same_groups = [](const std::vector<MatchGroup>& left, const std::vector<MatchGroup>& right) {
        if (left.size() != right.size()) return false;
        for (size_t i = 0; i < left.size(); ++i) {
            const auto& a = left[i];
            const auto& b = right[i];
            if (a.desc != b.desc || a.fixtures != b.fixtures || a.results != b.results ||
                a.cup_results != b.cup_results || a.cup_other_leg != b.cup_other_leg ||
                a.cup_result_address[0] != b.cup_result_address[0] ||
                a.cup_result_address[1] != b.cup_result_address[1] ||
                a.phase != b.phase || a.records != b.records || a.end_minute != b.end_minute ||
                a.spectators != b.spectators || a.log != b.log || a.ended != b.ended ||
                a.kickoff_void_mask != b.kickoff_void_mask) return false;
        }
        return true;
    };
    check(same_groups(legacy.driver.groups, observed.driver.groups),
          "no-op event hook preserves all current driver group data");
    check(legacy.driver.last_results.valid && observed.driver.last_results.valid &&
          legacy.driver.last_results.date == observed.driver.last_results.date &&
          legacy.driver.last_results.calendar_entry == observed.driver.last_results.calendar_entry &&
          legacy.driver.last_results.matchday == observed.driver.last_results.matchday &&
          legacy.driver.last_results.tournament_name == observed.driver.last_results.tournament_name &&
          same_groups(legacy.driver.last_results.groups, observed.driver.last_results.groups),
          "no-op event hook preserves retained completed results, descriptors, records and logs");
    std::cout << "source_event_hook: real event observed at deterministic seed " << found_game_seed
              << "; legacy and typed no-op full-matchday outputs/RNG match\n";
}

bool managed_league_home_fixture(const Run& run, int* opponent = nullptr) {
    if (run.state.manager_teams.empty()) return false;
    for (const MatchGroup& group : run.driver.groups) {
        if (group.desc.empty() || group.desc[0] != 0) continue;
        for (int m = 0; m < group.matches(); ++m) {
            if (group.void_mask() & (1u << m)) continue;
            const int home = group.fixtures.at(size_t(m) * 2);
            const int away = group.fixtures.at(size_t(m) * 2 + 1);
            if (run.state.manager_of(home) != 0) continue;
            if (opponent) *opponent = away;
            return true;
        }
    }
    return false;
}

TurnAdvanceResult advance_current_desk(Run& run) {
    MinuteReport report;
    const int manager = run.state.memory.has(::bmh::layout::ActiveManager) ? int(run.state.memory.u8(::bmh::layout::ActiveManager)) : -1;
    const auto result = advance_to_next_desk(run.driver, run.state,
        {manager, run.driver.revision}, run.game, run.rand, report);
    check(!report.unsupported && (result.kind == TurnDeskKind::NextDesk ||
          result.kind == TurnDeskKind::SeasonEndDesk || result.kind == TurnDeskKind::CareerComplete),
          "advance current prepared Source event: " + result.reason + "; " + report.reason);
    return result;
}

void income_record_cache_test(const Run& loaded) {
    // Advance from a real loaded manager desk until an actual later league fixture
    // places that manager at home. The calendar and match groups remain production
    // TurnDriver state; no fixture row or opponent is fabricated.
    Run prepared = loaded;
    int events = 0;
    const uint16_t loaded_cache = prepared.state.memory.u16(::bmh::layout::DaysIntoSeason);
    while (events < 48 && (events == 0 || !managed_league_home_fixture(prepared))) {
        const auto result = advance_current_desk(prepared);
        check(result.kind != TurnDeskKind::SeasonEndDesk && result.kind != TurnDeskKind::CareerComplete,
              "genuine income fixture is reached before a season boundary or career end");
        ++events;
    }
    int opponent = -1;
    check(events > 0 && managed_league_home_fixture(prepared, &opponent),
          "real loaded career reaches an advanced league home fixture for its manager");
    const int live_day = prepared.state.season_day();
    check(live_day != int(loaded_cache), "prepared fixture has advanced beyond the loaded cached season day");

    Run cached_a = prepared;
    Run cached_b = prepared;
    auto clear_income_record = [](GameState& g) {
        set_record(g, 0x730, 0);
        set_record(g, 0x734, 0xdeadbeefu);
        set_record16(g, 0x8c6, 0xffff);
        g.manager_records.at(0x26e) = 12;  // a valid ticket price, with real table attendance
    };
    clear_income_record(cached_a.state);
    clear_income_record(cached_b.state);

    uint16_t cache_b = uint16_t(loaded_cache + 1);
    while (cache_b == uint16_t(live_day)) ++cache_b;
    cached_a.state.memory.set16(::bmh::layout::DaysIntoSeason, loaded_cache);
    cached_b.state.memory.set16(::bmh::layout::DaysIntoSeason, cache_b);
    check(int(cached_a.state.memory.u16(::bmh::layout::DaysIntoSeason)) != live_day &&
          int(cached_b.state.memory.u16(::bmh::layout::DaysIntoSeason)) != live_day &&
          cached_a.state.memory.u16(::bmh::layout::DaysIntoSeason) != cached_b.state.memory.u16(::bmh::layout::DaysIntoSeason),
          "two stale loaded-cache values differ from the live advanced day");
    const auto native_before_a = save_stream(cached_a.state);
    const auto native_before_b = save_stream(cached_b.state);
    check(native_before_a == native_before_b,
          "pre-call serialized Native inputs are identical despite different cached bb468 values");

    const uint32_t expected_record_day = uint32_t(cached_a.state.season_day()) +
                                         uint32_t(cached_a.state.memory.u16(::bmh::layout::CalendarState_FormatError_RuntimeField3));
    const int event_day = cached_a.state.season_day();
    const auto result_a = advance_current_desk(cached_a);
    const auto result_b = advance_current_desk(cached_b);
    check(result_a.kind == TurnDeskKind::NextDesk && result_b.kind == result_a.kind,
          "both stale-cache variants complete the same genuine manager-home league matchday");
    check(record(cached_a.state, 0x730) > 0 && record(cached_b.state, 0x730) > 0 &&
          record16(cached_a.state, 0x8c6) == cached_a.state.club_ids.at(size_t(opponent)) &&
          record16(cached_b.state, 0x8c6) == cached_b.state.club_ids.at(size_t(opponent)),
          "real gate income and stable opponent record both changed");
    const auto after_a = save_stream(cached_a.state);
    const auto after_b = save_stream(cached_b.state);
    if (after_a != after_b) {
        const uint32_t stamp_a = record(cached_a.state, 0x734);
        const uint32_t stamp_b = record(cached_b.state, 0x734);
        std::cerr << "income-record cache discrepancy: live day " << event_day
                  << ", origin " << cached_a.state.memory.u16(::bmh::layout::CalendarState_FormatError_RuntimeField3)
                  << ", cached values " << loaded_cache << "/" << cache_b
                  << ", written dates " << stamp_a << "/" << stamp_b << '\n';
        check(stamp_a != stamp_b && record(cached_a.state, 0x730) == record(cached_b.state, 0x730) &&
              record16(cached_a.state, 0x8c6) == record16(cached_b.state, 0x8c6),
              "cache-dependent serialized difference is specifically the income-record date");
        check(false, "income-record date depends on stale cached bb468 rather than live season_day");
    }
    check(record(cached_a.state, 0x734) == expected_record_day && after_a == after_b,
          "income-record date is live season day plus the saved Original origin; full Native outputs match");
    check(cached_a.game.seed() == cached_b.game.seed() && cached_a.game.calls() == cached_b.game.calls() &&
          cached_a.rand.seed() == cached_b.rand.seed() && cached_a.rand.calls() == cached_b.rand.calls(),
          "cache-independence run consumes identical Game and Watcom RNG streams");

    // Exercise strict record replacement and retention on the same genuine prepared
    // match with isolated copies. This keeps the control deterministic while changing
    // only valid ticket/record fields, not teams, fixtures or random input.
    auto kickoff_income = [](Run& run) {
        MinuteReport report;
        MatchdayContext context;
        context.month = month_code(run.state.date);
        context.season_day = run.state.season_day();
        context.weekday = weekday_code(run.state.date);
        context.referees = &run.driver.referees;
        kick_off(run.state, run.driver.groups, context, run.rand, run.game, report);
        check(!report.unsupported, "genuine home-league kick_off supports attendance record test: " + report.reason);
        return record(run.state, 0x730);
    };
    Run first_record = prepared;
    clear_income_record(first_record.state);
    first_record.state.manager_records.at(0x26e) = 8;
    const uint32_t first_income = kickoff_income(first_record);
    const uint16_t first_opponent = record16(first_record.state, 0x8c6);
    check(first_income > 0 && first_opponent == first_record.state.club_ids.at(size_t(opponent)),
          "initial genuine attendance sets a positive record and actual opponent");

    uint16_t prior_opponent = 0xffff;
    for (uint16_t stable_id : prepared.state.club_ids) {
        if (stable_id != first_opponent) { prior_opponent = stable_id; break; }
    }
    check(prior_opponent != 0xffff && prior_opponent != first_opponent,
          "genuine fixture has a distinct valid prior stable opponent ID");
    const uint32_t expected_larger_stamp = uint32_t(prepared.state.season_day()) +
        uint32_t(prepared.state.memory.u16(::bmh::layout::CalendarState_FormatError_RuntimeField3));
    check(expected_larger_stamp >= 7, "live record date can seed a valid older control date");
    const uint32_t older_control_stamp = expected_larger_stamp - 7;

    Run larger_record = prepared;
    clear_income_record(larger_record.state);
    set_record(larger_record.state, 0x730, first_income - 1);
    set_record(larger_record.state, 0x734, older_control_stamp);
    set_record16(larger_record.state, 0x8c6, prior_opponent);
    larger_record.state.manager_records.at(0x26e) = 25;
    const uint32_t larger_income = kickoff_income(larger_record);
    check(larger_income > first_income && record(larger_record.state, 0x734) == expected_larger_stamp &&
          record16(larger_record.state, 0x8c6) == larger_record.state.club_ids.at(size_t(opponent)),
          "larger gate-income record replaces an older date and distinct prior opponent");

    Run nonrecord = prepared;
    clear_income_record(nonrecord.state);
    set_record(nonrecord.state, 0x730, larger_income);
    set_record(nonrecord.state, 0x734, older_control_stamp);
    set_record16(nonrecord.state, 0x8c6, prior_opponent);
    nonrecord.state.manager_records.at(0x26e) = 25;
    const uint32_t nonrecord_income = kickoff_income(nonrecord);
    check(nonrecord_income == larger_income && record(nonrecord.state, 0x734) == older_control_stamp &&
          record16(nonrecord.state, 0x8c6) == prior_opponent,
          "equal non-record attendance retains its older date and distinct prior opponent");
    std::cout << "income_record: after " << events << " initial event advances, stale bb468 variants match full Native/RNG at day "
              << event_day << "; income=" << first_income << " then larger=" << larger_income
              << ", equal non-record retains its date\n";
}
}
int main(int argc, char** argv) {
    try {
        check(argc == 3 || argc == 4, "usage: e13_turn_driver_tests ORIGINAL_DATA Native_INPUT [coupon|flags|runtime_flags|incomplete|income_record|source_event_hook]");
        if (!*argv[1]) {
            std::cout << "E13 turn driver tests: skipped (Original data directory not configured)\n";
            return 0;
        }
        const auto data = std::filesystem::path(argv[1]);
        const auto exe = bmh::test::load_executable(data / "BMH.exe");
        const auto texts = load_textd(data / "TEXTD.BLK");
        const auto database = load_database(data / "M3D.MAN");
        const auto initial = load_run(argv[2], exe, texts, database);
        const std::string mode = argc == 4 ? argv[3] : "all";
        if (mode == "all" || mode == "coupon") coupon_test(initial, exe);
        if (mode == "all" || mode == "flags") flag_test(initial, "flags");
        if (mode == "all" || mode == "runtime_flags") flag_test(initial, "runtime_flags");
        if (mode == "all" || mode == "incomplete") incomplete_test(initial);
        if (mode == "all" || mode == "income_record") income_record_cache_test(initial);
        if (mode == "all" || mode == "source_event_hook") source_event_hook_test(initial);
        check(mode == "all" || mode == "coupon" || mode == "flags" || mode == "runtime_flags" ||
              mode == "incomplete" || mode == "income_record" || mode == "source_event_hook", "unknown test mode");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
