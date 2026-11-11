// SPDX-License-Identifier: GPL-3.0-only
// Implementation of CareerSession (career_session.hpp): the engine side of the career. It
// owns the GameState, the two random generators, the turn driver and every "owner" of an
// open modal window (transfer editor, league table, bank, contracts, ...), and it is the
// only code the UI flow reaches into for game logic, through the controller.
// Request/acknowledge model: the UI sends a typed request that carries the revision it
// observed; the session checks that revision, applies the change (or rejects it with an
// error text), bumps the revision only when something changed and returns a projection or
// dispatch result. The front end then acknowledges that result (controller.cpp and the
// *Actions facades roll the session back if the acknowledgement fails). Live match events and
// notices flow the other way: the session calls presenter hooks synchronously and waits.
// This file also contains the checkpoint/restore code used by those rollbacks, the career
// start (options, manager join), save/load through the .FBM container and autosave.
// The member functions are defined by topic:
//   career_session.cpp  construction, career start and manager join
//   career_session_club.cpp  the club information dialogs (club, player list, player detail) and their services
//   career_session_league.cpp  the league family: tables, forecasts, friendly opponent table, phase results, running overview
//   career_session_statistics.cpp  strengths, top scorers, opportunities, other games, balance plan and league pairings
//   career_session_transfer.cpp  transfers: offer editor, poaching, transfer market
//   career_session_desk.cpp  the desk: mailbox, settings, advertising, contracts, desk settings, search profile, widgets, layout
//   career_session_match.cpp  match preparation and the live match: tactics, line-up, live overview, interventions, pitch
//   career_session_management.cpp  club management: training, training camp, bank, stadium, toto
// (here also: the turn - finish and kick-off -, the desk projection, autosave and save, the presenter hooks)
#include "app/career_session_internal.hpp"

namespace bmh::app {
namespace {

// Process-wide counter that hands every CareerSession a unique id (see session_id_ in the
// constructor); the turn driver stores that id to recognise which session it belongs to.
std::atomic<uint64_t> next_session_id{1};

constexpr uint32_t kSavedCalendar = ::bmh::layout::CompetitionCalendar;

constexpr uint32_t kSavedCompetitionBits = ::bmh::layout::SavedCompetition;

// Little-endian 32-bit read from a saved match-group descriptor (throws via at() when short).
uint32_t saved_group_u32(const Bytes& bytes, size_t offset) {
    return uint32_t(bytes.at(offset)) | (uint32_t(bytes.at(offset + 1)) << 8) |
           (uint32_t(bytes.at(offset + 2)) << 16) | (uint32_t(bytes.at(offset + 3)) << 24);
}

// Turn-cursor restore helpers (this and the next three functions plus restore_saved_turn_cursor).
// They rebuild the in-memory MatchGroup of a day that was saved in the middle of the desk loop,
// from the saved descriptors, fixtures and results. They never draw random numbers and never
// replay event preparation; any inconsistency throws std::runtime_error, which the caller turns
// into an "unsupported continuation" message or a load failure.
// League group: validates the descriptor (size, type 0, league id, fixture count) and copies
// the round's fixtures and results; per-match runtime arrays start empty.
void initialize_saved_match_group(MatchGroup& group, const GameState& state, size_t index,
                                  int league) {
    group.desc = group_descriptor(state, index);
    if (group.desc.size() != 0xd0 || group.desc[0] != 0 ||
        saved_group_u32(group.desc, 0xcc) != uint32_t(league))
        throw std::runtime_error("saved turn cursor: league descriptor does not match the active calendar");
    const int round = state.league_rounds.at(size_t(league));
    const auto fixtures = league_fixtures(league, round + 1);
    if (group.desc[2] != fixtures.size())
        throw std::runtime_error("saved turn cursor: league fixture count is incomplete");
    for (const Fixture& fixture : fixtures) {
        group.fixtures.push_back(fixture.home);
        group.fixtures.push_back(fixture.away);
    }
    const size_t result_at = size_t(league) * kLeagueResultsStride + size_t(round) * 10;
    if (result_at + fixtures.size() > state.results.size())
        throw std::runtime_error("saved turn cursor: league result block is incomplete");
    group.results.assign(state.results.begin() + long(result_at),
                         state.results.begin() + long(result_at + fixtures.size()));
    // Assign2 and the match loop read exactly one mask bit per descriptor
    // match. Before kickoff, a saved desk can retain high bits from the previous
    // larger group; those padding bits are not fixture identities and are ignored
    // by the original. Keep the saved descriptor intact and validate actual
    // fixture count, result range, and active league above.
    // These per-match arrays are transient runtime data. kick_off rebuilds them from
    // the authoritative saved lineups; no setup/event replay belongs in the loader.
    group.records.assign(fixtures.size() * 18, 0);
    group.end_minute.assign(fixtures.size(), 0);
    group.spectators.assign(fixtures.size(), 0);
}

// Cup group: checks the saved descriptor against the cup stage entry (stage, team count,
// match count), then rebuilds the fixture list (group stage: verified against regenerated
// fixtures; knockout: taken from the stage team list) and binds the result arrays of both legs.
void initialize_saved_cup_group(MatchGroup& group, const GameState& state, size_t index,
                                int cup_id, int entry_index) {
    const StaticMemory& mem = state.memory;
    group.desc = group_descriptor(state, index);
    const uint32_t guest_entry = saved_group_u32(group.desc, 0xcc);
    if (group.desc.size() != 0xd0 || group.desc[0] != cup_id + 1 ||
        guest_entry < kGuestDataBase || (guest_entry - kGuestDataBase) != cup_entry_address(entry_index))
        throw std::runtime_error("cup descriptor mismatch group=" + std::to_string(index) +
                                 " type=" + std::to_string(group.desc[0]) +
                                 " guest=" + std::to_string(guest_entry) +
                                 " stage=" + std::to_string(entry_index));
    const uint32_t entry = cup_entry_address(entry_index);
    // In memory the descriptor stores a guest pointer; MatchGroup carries the data-object
    // address, as setup_cup_groups does before store_group_descriptors adds the guest base.
    for (int i = 0; i < 4; ++i) group.desc.at(0xcc + size_t(i)) = uint8_t(entry >> (i * 8));
    const uint8_t flags = mem.u8(entry + 0x14);
    const bool knockout = (flags & 4) != 0;
    const int teams = mem.u8(entry + 0x1a);
    if (int16_t(mem.u16(entry + 0x18)) != cup_id || teams < 2 || (teams & 1) ||
        group.desc[2] == 0 || group.desc[2] > (knockout ? 32 : 18))
        throw std::runtime_error("saved turn cursor: cup descriptor has invalid stage or match count");
    const int round = cup_round(mem, entry_index);
    int first = 0;
    int expected_matches = 0;
    if (knockout) {
        if (round < 0 || round >= 8 || (1 << (round + 1)) > teams)
            throw std::runtime_error("saved turn cursor: cup knockout round is outside the stage");
        expected_matches = teams / (1 << (round + 1));
        first = uint8_t(cup_round_offset(mem, entry_index, round));
    } else {
        if (teams / 2 > 18) throw std::runtime_error("saved turn cursor: cup group is too large");
        expected_matches = teams / 2;
        // Group-stage fixtures are stored in descriptor +0x88; KO descriptors do not
        // contain fixture IDs there. Their IDs come from the stage's round-indexed list.
        first = uint8_t(expected_matches * mem.u8(entry + 0x1d) * 2);
    }
    if (expected_matches != group.desc[2])
        throw std::runtime_error("saved turn cursor: cup round match count disagrees with its descriptor");
    const size_t fixture_count = size_t(group.desc[2]) * 2;
    // As in Assign2, only bits [0, group.desc[2]) are read. The descriptor
    // can carry stale high padding bits from the preceding event; preserve them
    // in the copy rather than treating them as corrupt fixture references.
    const uint32_t list = mem.pointer(entry);
    if (list == 0 || (knockout
        ? !mem.has(list + uint32_t(first + expected_matches * 2) * 2 - 1)
        : !mem.has(list + uint32_t(teams) * 2 - 1)))
        throw std::runtime_error("saved turn cursor: cup team list is missing");
    auto valid_team = [&](int team) {
        return team >= 0 && size_t(team) < state.team_flags.size();
    };
    std::vector<int> cup_teams;
    cup_teams.reserve(size_t(teams));
    for (int i = 0; !knockout && i < teams; ++i) {
        const int team = int16_t(mem.u16(list + uint32_t(i) * 2));
        if (!valid_team(team) ||
            std::find(cup_teams.begin(), cup_teams.end(), team) != cup_teams.end())
            throw std::runtime_error("saved turn cursor: cup team list contains an invalid or duplicate team");
        cup_teams.push_back(team);
    }
    if (!knockout) {
        int table_teams = 0;
        switch (mem.pointer(entry + 8)) {
            case ::bmh::layout::CareerSession_RuntimeError_RuntimeField: table_teams = 4; break;
            case ::bmh::layout::CareerSession_RuntimeError_RuntimeField2: table_teams = 6; break;
            case ::bmh::layout::CareerSession_RuntimeError_RuntimeField3: table_teams = 12; break;
            case ::bmh::layout::CareerSession_RuntimeError_ExecutableField3: table_teams = 16; break;
            case ::bmh::layout::CareerSession_RuntimeError_ExecutableField: table_teams = 18; break;
            case ::bmh::layout::CareerSession_RuntimeError_ExecutableField2: table_teams = 20; break;
            default: throw std::runtime_error("saved turn cursor: cup group has unknown round strings");
        }
        const int rounds = mem.u8(entry + 0x1b) / ((flags & 1) + 1);
        const std::vector<Fixture> expected = generate_fixtures(table_teams, round + 1, rounds, 0);
        if (expected.size() < fixture_count / 2)
            throw std::runtime_error("saved turn cursor: cup group round is incomplete");
        std::vector<int> expected_fixture_ids;
        expected_fixture_ids.reserve(fixture_count);
        for (size_t i = 0; i < fixture_count / 2; ++i) {
            if (expected[i].home < 0 || expected[i].home >= teams ||
                expected[i].away < 0 || expected[i].away >= teams)
                throw std::runtime_error("saved turn cursor: cup round exceeds the saved team list");
            const int home = int16_t(mem.u16(list + uint32_t(expected[i].home) * 2));
            const int away = int16_t(mem.u16(list + uint32_t(expected[i].away) * 2));
            if (!valid_team(home) || !valid_team(away) || home == away ||
                std::find(cup_teams.begin(), cup_teams.end(), home) == cup_teams.end() ||
                std::find(cup_teams.begin(), cup_teams.end(), away) == cup_teams.end())
                throw std::runtime_error("saved turn cursor: generated cup fixture references an invalid team");
            expected_fixture_ids.push_back(home);
            expected_fixture_ids.push_back(away);
        }
        for (size_t i = 0; i < fixture_count; ++i) {
            const size_t at = 0x88 + i * 2;
            const int team = int16_t(uint16_t(group.desc.at(at)) | (uint16_t(group.desc.at(at + 1)) << 8));
            if (team != expected_fixture_ids[i])
                throw std::runtime_error("saved turn cursor: cup fixture IDs disagree with the saved round");
            group.fixtures.push_back(team);
        }
    } else {
        // SetupCupGroups derives KO fixtures from the stage team list and
        // cup_round_offset; descriptor +0x88 is not populated for KO stages.
        std::vector<int> active_teams;
        for (int i = 0; i < expected_matches * 2; ++i) {
            const int team = int16_t(mem.u16(list + uint32_t(first + i) * 2));
            if (!valid_team(team) || std::find(active_teams.begin(), active_teams.end(), team) != active_teams.end())
                throw std::runtime_error("saved turn cursor: knockout fixture references an invalid team");
            active_teams.push_back(team);
            group.fixtures.push_back(team);
        }
        for (int m = 0; m < expected_matches; ++m)
            if (group.fixtures[size_t(m) * 2] == group.fixtures[size_t(m) * 2 + 1])
                throw std::runtime_error("saved turn cursor: knockout fixture contains the same team twice");
    }

    for (int leg = 0; leg < 2; ++leg) {
        if (leg == 1 && !(flags & 1)) break;
        const uint32_t address = mem.pointer(entry + 0x0c + uint32_t(leg) * 4);
        // setup_cup_groups leaves the corresponding result vector empty when this stage
        // has no result-array pointer; the match engine initializes its runtime scores.
        if (address == 0) continue;
        const uint32_t result_at = address + uint32_t(first / 2) * 2;
        if (!mem.has(result_at + uint32_t(group.matches()) * 2 - 1))
            throw std::runtime_error("saved turn cursor: cup result array is missing");
        if (leg == 0) group.cup_result_address[0] = result_at;
        else group.cup_result_address[1] = result_at;
        std::vector<uint16_t>& results = leg == 0 ? group.cup_results : group.cup_other_leg;
        for (int m = 0; m < group.matches(); ++m)
            results.push_back(mem.u16(result_at + uint32_t(m) * 2));
    }
    group.records.assign(fixture_count * 9, 0);
    group.end_minute.assign(size_t(group.matches()), 0);
    group.spectators.assign(size_t(group.matches()), 0);
}

// Follows the chain of stage entries of a cup and returns the first stage whose round counter
// has not reached its round count; -1 when the cup is unknown or finished.
int current_cup_stage(const StaticMemory& mem, int cup_id) {
    int entry = find_cup(mem, cup_id);
    if (entry < 0) return -1;
    for (int guard = 0; guard < kCupEntries; ++guard) {
        const uint32_t address = cup_entry_address(entry);
        if (cup_round(mem, entry) < mem.u8(address + 0x1b)) return entry;
        const int next = int8_t(mem.u8(address + 0x16));
        if (next < 0 || next >= kCupEntries) return -1;
        entry = next;
    }
    return -1;
}

// Replay group (cup type 9): rebuilds the replay day from the postponed-match records of 'day',
// checks each against the league schedule and result marker, and recreates the two transient
// replay buffers that are not part of a save.
void initialize_saved_replay_group(MatchGroup& group, GameState& state, size_t index,
                                   int entry_index, int day) {
    constexpr uint32_t kPostponed = ::bmh::layout::Postponed;
    constexpr uint32_t kReplayFixtures = ::bmh::layout::ReplayFixtures;
    constexpr uint32_t kReplayResults = ::bmh::layout::ReplayResults;
    constexpr uint8_t kPostponedMark = 0xcd;
    constexpr int kMaxReplayMatches = 9;  // AddBlock clears 0x12 result bytes.
    StaticMemory& mem = state.memory;
    const uint32_t entry = cup_entry_address(entry_index);
    group.desc = group_descriptor(state, index);
    const uint32_t saved_entry = saved_group_u32(group.desc, 0xcc);
    const uint8_t flags = mem.u8(entry + 0x14);
    if (group.desc.size() != 0xd0 || group.desc[0] != 10 ||
        saved_entry != kGuestDataBase + entry || group.desc[1] != flags ||
        int16_t(mem.u16(entry + 0x18)) != 9 || flags != 6 ||
        mem.u8(entry + 0x1d) != 0 || (mem.u8(entry + 0x1e) & 0x0f) != 1 ||
        mem.pointer(entry) != kReplayFixtures || mem.pointer(entry + 0x0c) != kReplayResults ||
        mem.pointer(entry + 0x10) != 0)
        throw std::runtime_error("saved replay descriptor or stage is inconsistent");

    std::vector<Fixture> replay_fixtures;
    std::vector<uint32_t> replay_keys;
    replay_fixtures.reserve(kMaxReplayMatches);
    for (int i = 0; i < 0x28; ++i) {
        const uint32_t record = kPostponed + uint32_t(i) * 6;
        if (mem.u16(record) != day) continue;
        const int round = mem.u8(record + 2);
        const int league = mem.u8(record + 3);
        const int match = mem.u8(record + 4);
        if (league < 0 || league >= kLeagueCount || round < 0 ||
            round >= league_matchdays(league) ||
            match >= int(league_system().sizes[league] / 2))
            throw std::runtime_error("saved replay record is outside the league schedule");
        const std::vector<Fixture> fixtures = generate_fixtures(
            league_table_teams(league), round + 1, league_matchdays(league) / 2,
            league_first_team(league));
        if (match >= int(fixtures.size()))
            throw std::runtime_error("saved replay record has no corresponding fixture");
        const size_t result = size_t(league) * kLeagueResultsStride + size_t(round) * 10 + size_t(match);
        if (result >= state.results.size() || state.results[result] != kPostponedMark)
            throw std::runtime_error("saved replay record has no postponed result marker");
        const uint32_t key = (uint32_t(league) << 16) | (uint32_t(round) << 8) | uint32_t(match);
        if (std::find(replay_keys.begin(), replay_keys.end(), key) != replay_keys.end())
            throw std::runtime_error("saved replay records contain a duplicate schedule key");
        const Fixture fixture = fixtures[size_t(match)];
        replay_keys.push_back(key);
        replay_fixtures.push_back(fixture);
        if (replay_fixtures.size() > kMaxReplayMatches)
            throw std::runtime_error("saved replay day exceeds its result buffer");
    }
    if (replay_fixtures.empty() || group.desc[2] != replay_fixtures.size() ||
        mem.u8(entry + 0x1a) != replay_fixtures.size() * 2)
        throw std::runtime_error("saved replay match count disagrees with its records");
    // Neither transient address is in SaveState's serialized block layout; a loaded
    // GameState therefore has no valid prior runtime buffer to reuse.
    if (mem.has(kReplayFixtures) || mem.has(kReplayResults))
        throw std::runtime_error("saved replay transient blocks are unexpectedly present");

    // The original rebuilds these two non-savegame buffers in AddBlock. Recreate
    // only their current values, leaving the saved replay records/stage counters intact.
    mem.add_block(kReplayFixtures, 0x50);
    mem.add_block(kReplayResults, 0x12);
    for (int i = 0; i < kMaxReplayMatches; ++i)
        mem.set16(kReplayResults + uint32_t(i) * 2, 0xcccc);
    for (size_t i = 0; i < replay_fixtures.size(); ++i) {
        mem.set16(kReplayFixtures + uint32_t(i) * 4, uint16_t(replay_fixtures[i].home));
        mem.set16(kReplayFixtures + uint32_t(i) * 4 + 2, uint16_t(replay_fixtures[i].away));
        group.fixtures.push_back(replay_fixtures[i].home);
        group.fixtures.push_back(replay_fixtures[i].away);
        group.cup_results.push_back(0xcccc);
    }
    group.cup_result_address[0] = kReplayResults;
    for (int i = 0; i < 4; ++i) group.desc.at(0xcc + size_t(i)) = uint8_t(entry >> (i * 8));
    group.records.assign(replay_fixtures.size() * 18, 0);
    group.end_minute.assign(replay_fixtures.size(), 0);
    group.spectators.assign(replay_fixtures.size(), 0);
}

// Re-establishes the turn driver after loading a save that was written mid-day: validates the
// saved desk position / active manager / manager order, handles the season-end desk, compares
// the calendar entry with the saved competition flags, rebuilds the league and cup groups and
// starts the driver. Competitions that cannot be resumed safely set 'unsupported_continuation'
// (a player-visible text) instead of throwing; broken save data throws.
void restore_saved_turn_cursor(TurnDriver& driver, GameState& state, const Database& db,
                               uint64_t session_id, uint64_t revision,
                               std::string& unsupported_continuation) {
    // Original DailyInterest's resume path skips new-event preparation. Rebind its saved
    // desk position and event data directly; replaying prepare_event here would draw RNG,
    // reshuffle managers, and rewrite saved team state.
    if (revision == 0 || session_id == 0 || state.manager_teams.empty())
        throw std::runtime_error("saved turn cursor: invalid session identity or manager roster");
    const int manager_count = int(state.manager_teams.size());
    if (manager_count > 4 || !state.memory.has(::bmh::layout::ActiveManager) || !state.memory.has(::bmh::layout::DeskPosition))
        throw std::runtime_error("saved turn cursor: manager continuation fields are missing");
    int position = int(state.memory.u8(::bmh::layout::DeskPosition));
    const int active = int(state.memory.u8(::bmh::layout::ActiveManager));
    // A completed manager loop leaves DeskPosition == ManagerCount (the day's desks are done). A desk
    // that was saved in that state is the first desk again for a single manager: nothing else can
    // follow it. Reading the byte as 0 lets the day go on instead of refusing the whole save.
    if (manager_count == 1 && position == 1 && active == 0) {
        position = 0;
        state.memory.set8(::bmh::layout::DeskPosition, 0);
    }
    if (position < 0 || position >= manager_count)
        throw std::runtime_error("saved turn cursor: manager position is outside the saved roster");
    if (manager_count == 1) {
        if (position != 0 || active != 0)
            throw std::runtime_error("saved turn cursor: single-manager slot does not match the saved desk");
    } else {
        if (!state.memory.has(::bmh::layout::NextDeskManager + uint32_t(manager_count) * 2 - 1))
            throw std::runtime_error("saved turn cursor: manager order is missing");
        std::vector<bool> seen(size_t(manager_count), false);
        for (int i = 0; i < manager_count; ++i) {
            const int slot = int(state.memory.u16(::bmh::layout::NextDeskManager + uint32_t(i) * 2));
            if (slot < 0 || slot >= manager_count || seen[size_t(slot)])
                throw std::runtime_error("saved turn cursor: manager order is sparse or duplicated");
            seen[size_t(slot)] = true;
        }
        if (active != int(state.memory.u16(::bmh::layout::NextDeskManager + uint32_t(position) * 2)))
            throw std::runtime_error("saved turn cursor: active manager disagrees with saved order");
    }

    if (state.memory.has(::bmh::layout::CareerSession_RuntimeError_RuntimeField4) && state.memory.u8(::bmh::layout::CareerSession_RuntimeError_RuntimeField4)) {
        driver.stage = TurnDriver::Stage::SeasonEndDesk;
        driver.loaded_matchday_resume = true;
        driver.manager_position = position;
        driver.revision = revision;
        driver.session_id = session_id;
        driver.referee_source = db.raw_referee_properties;
        driver.referees = driver.referee_source;
        return;
    }
    const int day = state.season_day();
    if (day < 0 || day >= 0x16d || !state.memory.has(kSavedCalendar + uint32_t(day) * 2 + 1) ||
        !state.memory.has(kSavedCompetitionBits + 3))
        throw std::runtime_error("saved turn cursor: current calendar entry is missing");
    const uint16_t entry = state.memory.u16(kSavedCalendar + uint32_t(day) * 2);
    const uint32_t calendar_competitions = day_competitions(state, entry);
    const uint32_t saved_competitions = state.memory.u32(kSavedCompetitionBits);
    // AdvanceTurn maps the national-team-only marker national-only flag to saved
    // SavedCompetition=0 before it creates the no-fixture desk. Resume must compare
    // against that canonical saved value, not treat the calendar marker as a cup.
    const uint32_t competitions = calendar_competitions == 0x800000 && saved_competitions == 0
        ? 0 : calendar_competitions;
    if (competitions != saved_competitions)
        throw std::runtime_error("saved turn cursor: current calendar and saved competition flags disagree");
    if (competitions & ~0x7ffffu)
        unsupported_continuation = "Dieser Spieltag kann mit den verfügbaren Wettbewerbsdaten derzeit nicht fortgesetzt werden.";
    if (!unsupported_continuation.empty()) return;

    std::vector<int> active_leagues;
    // DayCompetitions2's competition bits expand to group leagues in this original order;
    // bit 2 owns both league 2 and 3. The stale descriptors after the active group count
    // are intentionally ignored.
    static constexpr int first_league[8] = {0, 1, 2, 4, 5, 6, 7, 8};
    static constexpr int league_count[8] = {1, 1, 2, 1, 1, 1, 1, 1};
    for (int bit = 0; bit < 5; ++bit) {
        if (!((competitions >> bit) & 1u)) continue;
        const int first = bit, last = bit < 4 ? bit + 1 : 8;
        for (int k = first; k < last; ++k)
            for (int league = first_league[k]; league < first_league[k] + league_count[k]; ++league)
                active_leagues.push_back(league);
    }
    if (active_leagues.size() > 6)
        throw std::runtime_error("saved turn cursor: too many league fixtures are active");

    std::vector<MatchGroup> groups;
    groups.reserve(active_leagues.size());
    for (size_t i = 0; i < active_leagues.size(); ++i) {
        MatchGroup group;
        initialize_saved_match_group(group, state, i, active_leagues[i]);
        groups.push_back(std::move(group));
    }
    // Rebind saved cup descriptors and their current stage fixtures/results. Group-stage
    // IDs live at descriptor +0x88; KO IDs live in the stage list. Replays reconstruct
    // only missing transient buffers. Calling setup_cup_groups would replay event work.
    bool has_empty_cup = false;
    try {
        for (int bit = 5; bit < 0x13; ++bit) {
            if (!((competitions >> bit) & 1u)) continue;
            const int cup_id = bit - 5;
            int entry = current_cup_stage(state.memory, cup_id);
            if (entry < 0) throw std::runtime_error("cup stage is missing");
            const uint32_t first_entry = cup_entry_address(entry);
            const int type = int16_t(state.memory.u16(first_entry + 0x18));
            // Type 9's daily fixtures can be reconstructed from the saved postponed-match
            // records. Other special competitions need preparation hooks and stay guarded.
            if (type < 0 || (type > 9 && type != 10 && type != 11 && type != 13))
                throw std::runtime_error("this cup type requires event preparation");
            // SetupCupGroups creates a group only inside its per-stage `teams != 0`
            // branch. A validated type-8 stage with zero teams is therefore a valid
            // no-group cup day. On FBM load, competition_pointers() rebinds the native
            // null list pointer to its runtime address; the team-count gate is the
            // stable Source behavior across both representations.
            if (type == 8 && state.memory.u8(first_entry + 0x1a) == 0) {
                has_empty_cup = true;
                continue;
            }
            // SetupCupGroups sets CareerSessionRuntimeField while projecting competition 11.
            // This runtime-only mode is absent from the saved byte stream, so infer
            // it from the authoritative active cup while rebuilding its MatchGroup.
            if (type == 11) state.indoor = true;
            const int parallel = state.memory.u8(first_entry + 0x1e) & 0x0f;
            if (parallel == 0) throw std::runtime_error("cup stage has no saved parallel groups");
            const size_t groups_before_cup = groups.size();
            for (int k = 0; k < parallel; ++k, ++entry) {
                if (entry < 0 || entry >= kCupEntries)
                    throw std::runtime_error("cup stage points outside the saved cup table");
                if (state.memory.u8(cup_entry_address(entry) + 0x1a) == 0) continue;
                if (groups.size() >= 6)
                    throw std::runtime_error("too many saved match groups");
                MatchGroup group;
                if (type == 9)
                    initialize_saved_replay_group(group, state, groups.size(), entry, day);
                else
                    initialize_saved_cup_group(group, state, groups.size(), cup_id, entry);
                groups.push_back(std::move(group));
            }
            if (groups.size() == groups_before_cup)
                throw std::runtime_error("cup stage has no saved fixture group");
        }
        if (competitions != 0 && groups.empty() && !has_empty_cup)
            throw std::runtime_error("active matchday has no saved fixture groups");
    } catch (const std::exception& e) {
        (void)e;  // Do not expose internal parser/descriptor details to the player.
        unsupported_continuation = "Der Pokalspieltag kann mit den gespeicherten Paarungen nicht sicher fortgesetzt werden.";
        return;
    }
    if (!groups.empty() && db.raw_referee_properties.empty())
        throw std::runtime_error("saved turn cursor: referee source is missing");
    std::vector<uint8_t> referees = db.raw_referee_properties;
    for (uint8_t& ref : referees) ref &= 0x7f;
    for (const MatchGroup& group : groups)
        for (int m = 0; m < group.matches(); ++m) {
            const uint8_t ref = group.desc.at(0x10 + size_t(m));
            if (ref > 0 && ref < referees.size()) referees[ref] |= 0x80;
        }
    std::string error;
    if (!begin_turn_driver(driver, state, std::move(groups), entry, db.raw_referee_properties,
                           session_id, revision, error))
        throw std::runtime_error("saved turn cursor: " + error);
    driver.referees = std::move(referees);
    // Runtime provenance belongs to this successfully restored Native desk loop.
    // It is not inferred from a nonzero cursor, and does not select a policy.
    driver.loaded_matchday_resume = true;
}

}  // namespace

// The league structure of the loaded game (schedule.hpp): sizes LeagueSizes (German
// 18 18 18 18 18 12 12 12 12, English 20 20 20 20 10 12 12 12 12) and calendar groups.
// Installs the league structure and text/rule tables of the loaded game version from the
// executable image into the global schedule, message, career, environment, badge and calendar
// tables, so the same engine runs German and English data.
static void install_league_sizes(const LeImage& exe) {
    uint8_t sizes[kLeagueCount];
    for (int l = 0; l < kLeagueCount; ++l) sizes[l] = exe.u8(::bmh::layout::LeagueSizes + uint32_t(l));
    set_league_sizes(sizes);
    install_fixture_tables(exe);
    installed_league_system().english_competitions = sizes[0] == 20;
    // the calendar groups (CalendarFirstLeague first league, CalendarLeagueCounts league count per bit)
    uint8_t first[8], count[8];
    for (uint32_t b = 0; b < 8; ++b) first[b] = exe.u8(::bmh::layout::CalendarFirstLeague + b), count[b] = exe.u8(::bmh::layout::CalendarLeagueCounts + b);
    set_league_groups(first, count);
    // the promotion ladder (schedule.hpp)
    for (uint32_t l = 0; l < uint32_t(kLeagueCount); ++l) {
        installed_league_system().relegation[l] = exe.u8(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField2 + l);
        installed_league_system().europe[l] = exe.u8(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField + l);
        installed_league_system().suffix[l] = char(l < 5 ? exe.u8(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField3 + l * 2) : 0);
    }
    for (uint32_t g = 0; g < 8; ++g) installed_league_system().below_first[g] = exe.u8(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField5 + g), installed_league_system().below_count[g] = exe.u8(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField4 + g);
    installed_league_system().newcomer_first = exe.u16(::bmh::layout::CareerSession_DispatchLineup_RuntimeField);
    installed_league_system().newcomer_count = exe.u16(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField6);
    // EXE strings the engine posts (messages.hpp)
    install_message_texts(exe);
    install_career_rules(exe);
    install_environment_rules(exe);
    install_original_badge_policy(exe);
    install_calendar_rules(exe);
}

// Constructor for a new career: loads the executable, database, texts and start data from the
// data directory, installs the league tables and rule data, sets up the turn driver flags from
// the executable, builds the start state and runs the career start up to the first dialog.
// Throws std::runtime_error when the start state is rejected.
CareerSession::CareerSession(const std::filesystem::path& data, uint32_t watcom_seed)
    : data_directory_(data), exe_(load_le(ui::asset_path(data, "BMH.exe"))),
      db_(load_database(ui::asset_path(data, "M3D.MAN"))),
      texts_(load_textd(ui::asset_path(data, "TEXTD.BLK"))),
      rand_(watcom_seed) {
    install_league_sizes(exe_);
    turn_driver_.alternate_match_event_roll = exe_.u8(::bmh::layout::AlternateEventRoll) != 0;
    turn_driver_.disable_match_events = exe_.u8(::bmh::layout::MatchEventsDisabled) != 0;
    turn_driver_.transfer_allocator_context = exe_.u8(::bmh::layout::MatchEventsDisabled) == 0
        ? TransferAllocatorContext::zero : TransferAllocatorContext::blocked;
    session_id_ = next_session_id.fetch_add(1, std::memory_order_relaxed);
    initialize_club_modal_runtime();
    g_ = startup_state(exe_, db_, read_file(ui::asset_path(data, "M3.MEN")), read_file(ui::asset_path(data, "M3.DAT")));
    g_.texts = &texts_;
    g_.currency_suffix = exe_.cstring(::bmh::layout::CurrencySuffixText);  // CurrencySuffixText
    configure_training_camp_data(g_, exe_);
    for (size_t i = 0; i < g_.credit_rate_table.size(); ++i) g_.credit_rate_table[i] = exe_.u8(::bmh::layout::BankCredit_BankCreditContext_RuntimeField3 + uint32_t(i));
    g_.credit_divisor = std::bit_cast<double>(uint64_t(exe_.u32(::bmh::layout::BankCredit_BankCreditContext_RuntimeField)) | uint64_t(exe_.u32(::bmh::layout::BankCredit_BankCreditContext_RuntimeField2)) << 32);
    g_.credit_rate_table_available = true;
    install_default_hooks();
    startup_players(g_, game_, rand_);  // CareerFirstSteps2
    MinuteReport report;
    if (!new_career_before_dialog(g_, game_, rand_, report))  // InitializeCareer up to the dialog
        throw std::runtime_error("career start: " + report.reason);
}

// Constructor for a loaded career: same data/engine setup as the new-career constructor, then
// reads the native .FBM save, rebuilds the GameState, rebinds the competition pointers, lets a
// national coach take the national team over again, restores the random seed and the saved turn
// cursor (a mid-day save). A non-native save or inconsistent data throws.
CareerSession::CareerSession(const std::filesystem::path& data, uint32_t watcom_seed,
                             const std::filesystem::path& save_path)
    : data_directory_(data), exe_(load_le(ui::asset_path(data, "BMH.exe"))), db_(load_database(ui::asset_path(data, "M3D.MAN"))),
      texts_(load_textd(ui::asset_path(data, "TEXTD.BLK"))), rand_(watcom_seed) {
    install_league_sizes(exe_);
    turn_driver_.alternate_match_event_roll = exe_.u8(::bmh::layout::AlternateEventRoll) != 0;
    turn_driver_.disable_match_events = exe_.u8(::bmh::layout::MatchEventsDisabled) != 0;
    turn_driver_.transfer_allocator_context = exe_.u8(::bmh::layout::MatchEventsDisabled) == 0
        ? TransferAllocatorContext::zero : TransferAllocatorContext::blocked;
    session_id_ = next_session_id.fetch_add(1, std::memory_order_relaxed);
    initialize_club_modal_runtime();
    const Savegame file = load_savegame(save_path);
    require_native_savegame(file);
    const DbCounts counts{exe_.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField), exe_.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3), exe_.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2), exe_.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
    const SaveState save(file.stream, counts);
    g_ = GameState::from_save(save, counts);
    // CompetitionPointers2, after DeskDate2: these are process statics, not FBM fields.
    // Resuming within this month must not count it as a newly elapsed month.
    g_.autosave_months = 0;
    g_.autosave_month = month_code(g_.date);
    // Original CompetitionPointers2 calls CompetitionPointers after loading the stream. This
    // rebinds pointer fields in the saved competition table before any resume
    // path dereferences them (the serialized values may point into the old process).
    competition_pointers(g_.memory, exe_);
    g_.texts = &texts_;
    g_.currency_suffix = exe_.cstring(::bmh::layout::CurrencySuffixText);  // CurrencySuffixText
    configure_training_camp_data(g_, exe_);
    for (size_t i = 0; i < g_.credit_rate_table.size(); ++i) g_.credit_rate_table[i] = exe_.u8(::bmh::layout::BankCredit_BankCreditContext_RuntimeField3 + uint32_t(i));
    g_.credit_divisor = std::bit_cast<double>(uint64_t(exe_.u32(::bmh::layout::BankCredit_BankCreditContext_RuntimeField)) | uint64_t(exe_.u32(::bmh::layout::BankCredit_BankCreditContext_RuntimeField2)) << 32);
    g_.credit_rate_table_available = true;
    install_default_hooks();
    national_coach_after_load(g_);  // (NationalCoachAfterLoad: a Bundestrainer takes the national team over again)
    if (g_.memory.has(::bmh::layout::CareerSession_Save_RuntimeField)) game_.set_seed(int32_t(g_.memory.u32(::bmh::layout::CareerSession_Save_RuntimeField)));
    restore_saved_turn_cursor(turn_driver_, g_, db_, session_id_, 1, saved_turn_error_);
    capture_source_desk_callargs();
}

// Maps a master-database club index to the runtime team slot, or -1 when it is not found.
// The overload below does the lookup in a given state: clubs travel with their name offset when
// the league tables reshuffle, so the slot is found by matching that offset.
int CareerSession::runtime_club(int master) const { return runtime_club(g_, master); }

// Same lookup inside the given state (see above); -1 when no club carries the master entry name offset.
int CareerSession::runtime_club(const GameState& state, int master) const {
    // ClubsChangePlaces moves a club together with its name: the club whose name offset is the
    // master entry's is the one the manager chose
    const uint16_t offset = uint16_t(db_.raw_club_name_offsets.at(size_t(master) * 2) | db_.raw_club_name_offsets.at(size_t(master) * 2 + 1) << 8);
    for (size_t t = 0; t < state.club_ids.size(); t++)
        if (state.club_ids[t] == offset) return int(t);
    return -1;
}

// Club-selection context (runtime slot to master club order) of the live state at 'revision'.
ui::SelectionContext CareerSession::selection_context(uint64_t revision) const { return selection_context_for(g_, revision); }

// Same for an arbitrary state (e.g. the join draft); a club whose name offset matches no master
// entry keeps its own index.
ui::SelectionContext CareerSession::selection_context_for(const GameState& state, uint64_t revision) const {
    ui::SelectionContext c;
    c.revision = revision;
    c.source = "engine";
    for (size_t t = 0; t < state.club_ids.size(); t++) {
        int master = int(t);
        for (size_t m = 0; m * 2 + 1 < db_.raw_club_name_offsets.size(); m++)
            if (uint16_t(db_.raw_club_name_offsets[m * 2] | db_.raw_club_name_offsets[m * 2 + 1] << 8) == state.club_ids[t]) {
                master = int(m);
                break;
            }
        c.club_order.push_back(master);
    }
    return c;
}

// Lists the managers already in the game (slot, club, name, start league, portrait) as the
// choices a joining player has to avoid; throws FormatError when a manager team is invalid.
std::vector<ui::ManagerChoice> CareerSession::manager_choices(uint64_t revision) const {
    const auto context = selection_context(revision);
    std::vector<ui::ManagerChoice> choices;
    for (size_t slot = 0; slot < g_.manager_teams.size(); ++slot) {
        const int team = g_.manager_teams[slot];
        if (team < 0 || size_t(team) >= context.club_order.size()) throw FormatError("join: invalid existing club");
        ui::ManagerChoice c;
        c.slot = int(slot); c.selection_club = team; c.master_club = context.club_order[size_t(team)];
        c.roster_revision = revision;
        const size_t at = slot * 0xb0c;
        for (size_t i = 0; i < 25 && g_.manager_records.at(at + 0xa8 + i); ++i)
            c.name.push_back(char(g_.manager_records[at + 0xa8 + i]));
        c.start_league = g_.manager_records.at(at + 0x210);
        c.portrait = g_.manager_records.at(at + 0x221);
        choices.push_back(std::move(c));
    }
    return choices;
}

// Manager join (add a human manager to a running career) follows a draft pattern: begin copies
// the state/random generators into manager_join_, confirm_manager_join adds one manager to the
// draft (restoring the draft on failure) and commit_manager_join swaps the draft into the live
// state only if the live career is byte-identical to what the draft started from.
// The members of the setup/club-information family below refuse to run while another modal
// owner is open (balance draft, strengths, top scorers, stadium block expansion, other games)
// and then report the error text "strengths_active".
// Begin: needs the menu-29 desk request of the current revision on the current manager's desk
// (not the national team), options that only grow the manager count (max 4) and keep all other
// options; returns false with 'error' set on any mismatch.
bool CareerSession::begin_manager_join(const ui::DeskRequest& origin, const ui::SetupOptions& options,
                                      uint64_t revision, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    try {
        const auto desk = desktop_projection(revision);
        const auto& current = desk.options;
        if (manager_join_ || origin.kind != ui::DeskRequestKind::open_menu || origin.menu_kind != 29 ||
            origin.revision != revision || origin.manager_slot != desk.manager_slot || desk.national_team ||
            revision > std::numeric_limits<uint64_t>::max() - 4 ||
            options.managers < current.managers || options.managers > 4 ||
            options.difficulty < 0 || options.difficulty > 9 ||
            (current.difficulty == 0 && options.difficulty != 0) ||
            options.duration != current.duration || options.mode != current.mode ||
            options.league != current.league || options.world != current.world)
            throw FormatError("join: options or origin no longer match the running career");
        ManagerJoinDraft draft;
        draft.state = g_; draft.rand = rand_; draft.game = game_;
        draft.options = options; draft.origin = origin; draft.choices = manager_choices(revision);
        draft.original_stream = save_stream(g_); draft.original_rand_seed = rand_.seed();
        draft.original_game_seed = game_.seed(); draft.original_rand_calls = rand_.calls();
        draft.original_game_calls = game_.calls(); draft.context_revision = revision;
        auto& mem = draft.state.memory;
        mem.set8(::bmh::layout::Difficulty, uint8_t(options.difficulty)); draft.state.level = options.difficulty;
        mem.set8(::bmh::layout::ManagerCount, uint8_t(options.managers));
        const uint16_t signature = uint16_t(options.mode + options.duration * 4 + options.difficulty * 0x40);
        mem.add_block(::bmh::layout::CareerSession_FormatError_RuntimeField7, 2);
        mem.set16(::bmh::layout::CareerSession_FormatError_RuntimeField7, mem.u16(::bmh::layout::CareerSession_FormatError_RuntimeField7) == signature ? signature : 0);
        mem.add_block(::bmh::layout::CareerSession_FormatError_RuntimeField6, 1); mem.set8(::bmh::layout::CareerSession_FormatError_RuntimeField6, 1);
        if (mem.u32(::bmh::layout::CompetitionSelection) == 1)
            for (int slot = current.managers; slot < options.managers; ++slot)
                draft.state.manager_options.at(size_t(slot) * 5 + 4) = 1;
        career_managers_begin(draft.state, true);
        manager_join_ = std::move(draft);
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

// Selection context of the join draft; throws FormatError when no join is in progress.
ui::SelectionContext CareerSession::manager_join_context() const {
    if (!manager_join_) throw FormatError("join: no draft");
    return selection_context_for(manager_join_->state, manager_join_->context_revision);
}

// True when the draft holds one choice per requested manager.
bool CareerSession::manager_join_complete() const {
    return manager_join_ && manager_join_->choices.size() == size_t(manager_join_->options.managers);
}

// Validates one choice (next slot, current roster revision, name, league, strength, portrait,
// club known and not already taken) and adds it to the draft. On failure the draft is rolled
// back to its copy from before the call and false is returned with 'error'.
bool CareerSession::confirm_manager_join(const ui::ManagerChoice& choice, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    if (!manager_join_ || manager_join_complete()) { error = "join: no pending manager"; return false; }
    auto before = *manager_join_;
    try {
        auto& draft = *manager_join_;
        const auto context = manager_join_context();
        if (choice.slot != int(draft.choices.size()) || choice.roster_revision != context.revision ||
            choice.name.empty() || choice.name.size() > 24 || choice.name.find('\0') != std::string::npos ||
            choice.start_league < 0 || choice.start_league > 4 || choice.strength < 0 || choice.strength > 2 ||
            choice.portrait < 0 || choice.portrait > 11 || choice.selection_club < 0 ||
            size_t(choice.selection_club) >= context.club_order.size() ||
            context.club_order[size_t(choice.selection_club)] != choice.master_club ||
            std::any_of(draft.choices.begin(), draft.choices.end(), [&](const auto& old) {
                return old.master_club == choice.master_club || old.portrait == choice.portrait;
            })) throw FormatError("join: invalid or stale manager choice");
        CareerManager selected;
        selected.name = choice.name; selected.team = runtime_club(draft.state, choice.master_club);
        selected.league = choice.start_league; selected.strength = choice.strength; selected.portrait = choice.portrait;
        if (selected.team < 0) throw FormatError("join: club not found");
        const auto visible=player_league_count(exe_);
        const auto club_league=team_league(selected.team);
        if(selected.league>=visible||(club_league>=visible&&club_league<5))throw FormatError("join: league is not available for human management");
        MinuteReport report;
        if (!career_add_manager(draft.state, exe_, choice.slot, selected, true, draft.game, draft.rand, report))
            throw FormatError(report.reason);
        draft.choices.push_back(choice); ++draft.context_revision;
        return true;
    } catch (const std::exception& e) { manager_join_ = std::move(before); error = e.what(); return false; }
}

// Commit: requires a complete draft whose origin revision is the current one and whose live
// state and random counters are unchanged since begin. On success the draft replaces the live
// state, the revision advances by one and the new desktop/preparation projections are returned;
// on any failure the checkpoint is restored, the revision is left unchanged and false is returned.
bool CareerSession::commit_manager_join(ui::CareerInitializationResult& result, uint64_t& revision,
                                       std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    if (!manager_join_complete()) { error = "join: selection incomplete"; return false; }
    auto before = manager_join_checkpoint();
    const auto previous_revision = revision;
    try {
        const auto& draft = *manager_join_;
        if (draft.origin.revision != revision || save_stream(g_) != draft.original_stream ||
            rand_.seed() != draft.original_rand_seed || game_.seed() != draft.original_game_seed ||
            rand_.calls() != draft.original_rand_calls || game_.calls() != draft.original_game_calls)
            throw FormatError("join: career changed during selection");
        g_ = draft.state; rand_ = draft.rand; game_ = draft.game;
        career_managers_end(g_, true);
        managers_.clear();
        for (const auto& c : draft.choices)
            managers_.push_back({c.slot, c.master_club, g_.manager_teams.at(size_t(c.slot))});
        // ConfirmManager retains the current desk/cursor. Weiter performs the later handoff.
        const auto next_revision = revision + 1;
        turn_driver_.revision = next_revision;
        // The next opponent of every manager is captured per day for the manager count of that day;
        // the joined manager has only the default ("no game") until it is captured again.
        capture_source_desk_callargs();
        ui::CareerInitializationResult next;
        next.desktop = desktop_projection(next_revision); next.managers = managers_;
        next.preparation = preparation_projection(next_revision);
        ui::validate_preparation_context(next.preparation);
        result = std::move(next); revision = next_revision; manager_join_.reset();
        return true;
    } catch (const std::exception& e) {
        restore_manager_join_checkpoint(std::move(before)); revision = previous_revision;
        error = e.what(); return false;
    }
}

// Writes the setup-dialog options into the game memory and prunes the calendar (no league game /
// no World Cup game) as the original setup screen does at the start of a new game.
void CareerSession::apply_options(const ui::SetupOptions& o) {
    // SetupScreen2 takes the four numbers over when the options dialog is left
    StaticMemory& mem = g_.memory;
    mem.set8(::bmh::layout::CareerDuration, uint8_t(o.duration));
    mem.set8(::bmh::layout::Difficulty, uint8_t(o.difficulty));
    g_.level = o.difficulty;  // (the typed copy)
    mem.set8(::bmh::layout::ManagerCount, uint8_t(o.managers));
    mem.set8(::bmh::layout::GameMode, uint8_t(o.mode));
    mem.set32(::bmh::layout::CompetitionSelection, (o.league ? 1u : 0u) | (o.world ? 2u : 0u));
    // The end of SetupScreen2 at a new game (param_1 = 0, the first manager):
    // - a one-manager game flags its manager as Bundestrainer (record +0x21c, CareerSessionRuntimeField2);
    // - without the league game every calendar day (CompetitionCalendar, 0x16e words) except the
    //   World Cup finals (0x4006) loses its low byte and keeps only the international bits
    //   0x1e of its high byte;
    // - without the World Cup game the international bits go: the high byte keeps 0x61 only.
    //   So the national team plays (and its manager takes over, SwapManagerSlots2 on 0x1e00
    //   days) only when the World Cup mode is on, as the manual says (Handbuch 2, p. 11);
    // - the league-only game sets option byte 4 (CareerSessionRuntimeField3) of every manager.
    if (managers_.empty() && o.managers == 1 && g_.manager_records.size() >= 0xb0c)
        g_.manager_records.at(0x21c) = 1;
    if (mem.has(kSavedCalendar + 0x16e * 2 - 1))
        for (uint32_t day = 0; day < 0x16e; ++day) {
            const uint32_t at = kSavedCalendar + day * 2;
            uint16_t word = mem.u16(at);
            if (!o.league && word != 0x4006) word = uint16_t(word & 0x1e00);
            if (!o.world) word = uint16_t(word & 0x61ff);
            mem.set16(at, word);
        }
    if (o.league && !o.world)
        for (int m = 0; m < o.managers && size_t(m) * 5 + 4 < g_.manager_options.size(); ++m)
            g_.manager_options.at(size_t(m) * 5 + 4) = 1;
    career_managers_begin(g_);
    options_applied_ = true;
}

// New-career manager entry: refuses while a modal owner is open, checks the chosen league is
// visible for human management, applies the options on the first call and adds the manager in
// the next free slot. Returns false with 'error' on failure.
bool CareerSession::confirm_manager(const ui::SetupOptions& options, const ui::ManagerChoice& choice, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    const auto visible=player_league_count(exe_);
    const auto selected_team=runtime_club(choice.master_club);
    const auto selected_league=selected_team>=0?team_league(selected_team):-1;
    if(choice.start_league<0||choice.start_league>=visible||(selected_league>=visible&&selected_league<5)) {
        error="league is not available for human management";return false;
    }
    if (!options_applied_) apply_options(options);
    const int m = int(managers_.size());
    if (m >= options.managers) {
        error = "more managers than chosen in the options";
        return false;
    }
    CareerManager c;
    c.name = choice.name;
    c.team = runtime_club(choice.master_club);
    c.league = choice.start_league;
    c.strength = choice.strength;
    c.portrait = choice.portrait;
    if (c.team < 0) {
        error = "club not found";
        return false;
    }
    MinuteReport report;
    if (!career_add_manager(g_, exe_, m, c, false, game_, rand_, report)) {
        error = report.reason;
        return false;
    }
    managers_.push_back({choice.slot, choice.master_club, g_.manager_teams.at(size_t(m))});
    return true;
}

// Finishes the career setup after all managers are chosen: refuses while a modal owner is open
// and runs the season setup and first steps with calendar_advance_active_ set (restored by
// AdvanceReset). Returns false with 'error' (the engine's reason) on failure.
bool CareerSession::finish(ui::CareerInitializationResult& result, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    // calendar_advance_active_ marks the span in which the calendar is being advanced; it is set for
    // the whole setup/turn and restored by the scope guard below, so nested entries are safe.
    const bool previous_advance = calendar_advance_active_;
    calendar_advance_active_ = true;
    struct AdvanceReset { bool& active; bool previous; ~AdvanceReset() { active = previous; } }
        advance_reset{calendar_advance_active_, previous_advance};
    MinuteReport report;
    career_managers_end(g_, false);
    const auto season_pause = [this](const char* checkpoint) {
        if (turn_driver_.season_pause) turn_driver_.season_pause(checkpoint, g_);
    };
    if (!career_season_setup(g_, game_, rand_, report, season_pause, turn_driver_.season_cup_draw) ||
        !career_first_steps(g_, game_, rand_, report)) {
        error = report.reason;
        return false;
    }
    // DailyInterest's loop: the event of season day 0 (AdvanceTurn) and the first desk
    std::vector<uint8_t> referees = db_.raw_referee_properties;
    MatchdayContext ctx;
    ctx.month = month_code(g_.date);
    ctx.season_day = g_.season_day();
    ctx.weekday = weekday_code(g_.date);
    ctx.referees = &referees;
    const uint16_t entry = g_.memory.u16(::bmh::layout::CompetitionCalendar + uint32_t(g_.season_day()) * 2);
    std::vector<MatchGroup> groups = prepare_event(g_, entry, ctx, game_, rand_, report);
    enter_event_desk(g_, 0, groups, game_, rand_, report, true, turn_driver_.desk_autosave);
    if (report.unsupported) {
        error = report.reason;
        return false;
    }
    if (!begin_turn_driver(turn_driver_, g_, std::move(groups), entry, db_.raw_referee_properties,
                           session_id_, 1, error))
        return false;
    capture_source_desk_callargs();
    // Both UI projections must reflect the freshly initialized driver cursor.
    // Reuse its prepared groups; these projections do not prepare an event or draw RNG.
    result.desktop = desktop_projection(1);
    result.preparation = preparation_projection(1);
    for (size_t m = 0; m < managers_.size(); ++m)
        managers_[m].runtime_club = g_.manager_teams.at(m);
    result.managers = managers_;
    return true;
}

// Turn driver entry (advance_turn): the player presses "Weiter" at a desk. Stages of the driver
// that count as a desk are MatchdayDesks and SeasonEndDesk; while the day is played the claimed
// turn is marked Failed until it completes. Every refusal below (open modal owner, completed
// career, saved turn that cannot be continued, stale revision or manager slot, exhausted
// cursor) returns a Failed result with the reason and leaves the state untouched.
// Order of work: resolve the manager's pending offers on staged copies (all or nothing), then
// advance the driver to the next desk, then publish the new revision and the results view.
TurnAdvanceResult CareerSession::dispatch_kickoff(const ui::DeskRequest& request, uint64_t& revision,
                                                   std::string& error) {
    error.clear();
    if (strengths_owner_ || strengths_dispatch_active_ || balance_draft_ || balance_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_ || league_pairings_draft_ || league_pairings_dispatch_active_ || transfer_offer_editor_ || pending_offer_dispatch_active_ || market_dispatch_active_) {
        error = "pending_offer_decision_active";
        TurnAdvanceResult result;
        result.kind = TurnDeskKind::Failed;
        result.manager_slot = int(g_.memory.u8(::bmh::layout::ActiveManager));
        result.date = g_.date;
        result.revision = revision;
        result.reason = error;
        return result;
    }
    if (career_complete()) {
        error = "career_complete";
        TurnAdvanceResult result;
        result.kind = TurnDeskKind::Failed;
        result.manager_slot = -1;
        result.date = g_.date;
        result.revision = revision;
        result.reason = error;
        return result;
    }
    if (!saved_turn_error_.empty()) {
        error = saved_turn_error_;
        TurnAdvanceResult result;
        result.kind = TurnDeskKind::Failed;
        result.manager_slot = int(g_.memory.u8(::bmh::layout::ActiveManager));
        result.date = g_.date;
        result.revision = revision;
        result.reason = error;
        return result;
    }
    if (request.kind != ui::DeskRequestKind::advance_turn || request.revision != revision) {
        error = "stale_manager_or_revision";
        TurnAdvanceResult result;
        result.kind = TurnDeskKind::Failed;
        result.manager_slot = int(g_.memory.u8(::bmh::layout::ActiveManager));
        result.date = g_.date;
        result.revision = revision;
        result.reason = error;
        return result;
    }
    if (request.manager_slot != int(g_.memory.u8(::bmh::layout::ActiveManager))) {
        error = "stale_manager_or_revision";
        TurnAdvanceResult result;
        result.kind = TurnDeskKind::Failed;
        result.manager_slot = int(g_.memory.u8(::bmh::layout::ActiveManager));
        result.date = g_.date;
        result.revision = revision;
        result.reason = error;
        return result;
    }
    // Builds the result that keeps the player at the current desk (revision unchanged) with 'reason'.
    const auto remain_at_desk = [&](TurnDeskKind kind, std::string reason) {
        if (reason.empty()) reason = "post-desk pending presenter stopped";
        error = std::move(reason);
        TurnAdvanceResult out;
        out.kind = kind;
        out.manager_slot = request.manager_slot;
        out.date = g_.date;
        out.revision = revision;
        out.reason = error;
        return out;
    };
    if (revision == std::numeric_limits<uint64_t>::max() ||
        (turn_driver_.stage != TurnDriver::Stage::MatchdayDesks &&
         turn_driver_.stage != TurnDriver::Stage::SeasonEndDesk))
        return remain_at_desk(TurnDeskKind::Failed, "stale_or_exhausted_turn_cursor");

    // Pending decisions and their cursor checks must complete together or
    // restore the previous desk. Once simulation starts, its terminal failure
    // cursor must stay claimed: external presentation cannot be rolled back.
    // Transaction around the pending-offer decisions: if the function returns before the driver
    // claims the turn, the destructor restores the checkpoint taken at entry (state, revision).
    struct KickoffTransaction {
        CareerSession& session;
        uint64_t& revision;
        std::optional<AdvertisingSessionCheckpoint> before;
        void restore() {
            if (!before) return;
            auto checkpoint = std::move(*before);
            before.reset();
            revision = checkpoint.revision;
            session.restore_advertising_session_checkpoint(std::move(checkpoint));
        }
        ~KickoffTransaction() { restore(); }
    } transaction{*this, revision, advertising_session_checkpoint(revision)};
    // Rolls the transaction back, then stays at the desk with 'reason'.
    const auto stop_at_desk = [&](TurnDeskKind kind, std::string reason) {
        transaction.restore();
        return remain_at_desk(kind, std::move(reason));
    };
    // calendar_advance_active_ marks the span in which the turn driver plays the day; live overviews
    // and live settings need it, desk modals refuse to open while it is set. The scope guard
    // (AdvanceReset) restores the previous value on every exit.
    const bool previous_advance = calendar_advance_active_;
    calendar_advance_active_ = true;
    struct AdvanceReset { bool& active; bool previous; ~AdvanceReset() { active = previous; } }
        advance_reset{calendar_advance_active_, previous_advance};
    if ((turn_driver_.stage == TurnDriver::Stage::MatchdayDesks ||
         turn_driver_.stage == TurnDriver::Stage::SeasonEndDesk) &&
        pending_offer_entry_policy_ == PendingOfferEntryPolicy::normal_interactive) {
        // AdvanceTurn resolves this manager before the next desk or simulation.
        // An unsupported decision must leave the driver unclaimed and retryable.
        pending_offer_dispatch_active_ = true;
        struct PendingReset { bool& active; ~PendingReset() { active = false; } }
            pending_reset{pending_offer_dispatch_active_};
        try {
            const auto modal_baseline = advertising_session_checkpoint(revision);
            auto next_state = g_;
            auto next_game = game_;
            auto next_rand = rand_;
            const auto hooks = pending_hooks_for_dispatch(pending_offer_hooks_);
            MinuteReport pending_report;
            // Explicit supported interactive runtime policy, not reconstructed
            // Original transient bytes or an answer inferred from missing data.
            const PendingOfferDeskRequest pending_request{
                PendingOfferDeskRuntimeMode::normal_interactive,
                int(g_.manager_teams.size()), turn_driver_.manager_position};
            // A nomination desk has one temporary national manager but retains
            // the original order prefix. Its Source post-return owner can differ
            // from the runtime desk owner, so ordinary permutation checks do not apply.
            const auto pending = turn_driver_.national_nomination_desk
                ? process_national_nomination_pending_after_desk(
                    next_state, pending_request, next_game, next_rand, hooks, pending_report)
                : process_natural_pending_offers_after_desk(
                    next_state, pending_request, next_game, next_rand, hooks, pending_report);
            // This existing fingerprint covers Native and its qualified
            // runtime/owners, RNG and revision, not arbitrary memory writes.
            if (!advertising_session_unchanged(modal_baseline, revision) ||
                turn_driver_.manager_position != modal_baseline.driver.manager_position ||
                turn_driver_.revision != modal_baseline.driver.revision ||
                turn_driver_.calendar_entry != modal_baseline.driver.calendar_entry)
                return stop_at_desk(TurnDeskKind::Failed, "pending_offer_reentrant_state");
            if (pending_report.unsupported || !pending.completed)
                return stop_at_desk(TurnDeskKind::WaitingAtDesk,
                    pending_report.reason.empty() ? "post-desk pending decision incomplete" : pending_report.reason);
            // Even a batch without owned offers performs Source's last-manager
            // cleanup and completed-loop cursor write. No intermediate revision.
            g_ = std::move(next_state);
            game_ = next_game;
            rand_ = next_rand;
        } catch (const std::exception& e) {
            return stop_at_desk(TurnDeskKind::WaitingAtDesk, e.what());
        }
    }
    // Resolve names before advancing: next_event can reorder clubs at a season
    // transition. The result view must keep the identities of the played event.
    std::vector<std::string> names;
    names.reserve(g_.club_ids.size());
    for (size_t team = 0; team < g_.club_ids.size(); ++team)
        names.push_back(ui::original_to_utf8(g_.club_name(int(team))));
    const bool had_results = turn_driver_.last_results.valid;
    const uint32_t previous_result_date = turn_driver_.last_results.date;
    const uint32_t previous_day = g_.date;
    const uint16_t previous_calendar_entry = turn_driver_.calendar_entry;
    // Desk actions legitimately advance the app's projection revision between turns.
    // Adopt that already-validated current revision before handing off the kickoff.
    // AdvanceTurn(param1=1) skips only later pre-match offer generation in this
    // already prepared loop. Legacy/headless callers retain their explicit path.
    // Select it after a complete staged batch so Waiting cannot alter the cursor.
    turn_driver_.suppress_remaining_pre_match_offers =
        pending_offer_entry_policy_ == PendingOfferEntryPolicy::normal_interactive &&
        turn_driver_.loaded_matchday_resume;
    // Select the complete nomination-desk lifecycle only for the interactive
    // controller, after the staged outgoing batch has completed successfully.
    turn_driver_.enable_national_nomination_desks =
        pending_offer_entry_policy_ == PendingOfferEntryPolicy::normal_interactive;
    turn_driver_.revision = revision;
    MinuteReport report;
    // Contract expiry can also be reached by a headless/unpresented session.
    // Keep the checkpoint from before the complete pending decision batch so a
    // missing decision owner cannot commit its Native cursor cleanup or leave
    // an expired player partially advanced.
    // Kept so a contract-expiry or loan stop during the month change can still undo the already
    // resolved pending decisions; the transaction itself is released just below.
    std::optional<AdvertisingSessionCheckpoint> contract_expiry_checkpoint = transaction.before;
    transaction.before.reset(); // claim the completed decision batch before simulation callbacks
    turn_driver_.reality_rules = &exe_;
    turn_driver_.sportschau_source = sportschau_source_options();
    TurnAdvanceResult result = advance_to_next_desk(turn_driver_, g_,
        TurnAdvanceRequest{request.manager_slot, request.revision}, game_, rand_, report);
    if (result.kind == TurnDeskKind::Failed || result.kind == TurnDeskKind::WaitingAtDesk) {
        if (contract_expiry_checkpoint && (result.reason.find("month change: a contract ends") != std::string::npos ||
            result.reason.find("month change: human-owner loan record is incomplete") != std::string::npos ||
            result.reason.find("month change: human-owner loan cannot be restored safely") != std::string::npos ||
            result.reason.find("month change: expiring human-borrower loan") != std::string::npos ||
            result.reason.find("month change: human-borrower loan return dialog") != std::string::npos ||
            result.reason.find("month change: OpenTransferEditor return-offer editor") != std::string::npos)) {
            const uint64_t checkpoint_revision = contract_expiry_checkpoint->revision;
            restore_advertising_session_checkpoint(std::move(*contract_expiry_checkpoint));
            revision = checkpoint_revision;
            return remain_at_desk(result.kind, result.reason);
        }
        return stop_at_desk(result.kind, result.reason);
    }
    revision = result.revision;
    if (g_.date != previous_day || turn_driver_.calendar_entry != previous_calendar_entry)
        capture_source_desk_callargs();
    lineup_entry_manager_ = -1;
    lineup_entry_club_ = -1;
    completed_results_.reset();
    const auto& snapshot = turn_driver_.last_results;
    if (snapshot.valid && !snapshot.groups.empty() &&
        (!had_results || snapshot.date != previous_result_date)) {
        completed_results_ = ui::matchday_results_from_snapshot(snapshot, [&](int team) {
            const size_t id = size_t(team & 0x0fff);
            return id < names.size() ? names[id] : std::string();
        }, exe_, g_);
    }
    return result;
}

// Captures, for every manager slot, the competition id and opponent shown at the desk today
// (defaults 33/34, or 32 at season end; no opponent). Only done while the driver is at a desk
// stage; the values are kept in source_desk_callargs_ and later used to authenticate lineup
// and pitch requests.
void CareerSession::capture_source_desk_callargs() {
    source_desk_callargs_.clear();
    const auto stage = turn_driver_.stage;
    if (stage != TurnDriver::Stage::MatchdayDesks &&
        stage != TurnDriver::Stage::SeasonEndDesk)
        return;
    uint32_t competitions = 0;
    int default_competition = 32;
    const std::vector<MatchGroup>* groups = nullptr;
    if (stage == TurnDriver::Stage::MatchdayDesks) {
        competitions = day_competitions(g_, turn_driver_.calendar_entry);
        if (competitions == 0x800000) competitions = 0;
        default_competition = competitions != 0 ? 34 : 33;
        groups = &turn_driver_.groups;
    }
    // DayCompetitions2 initializes the full four-slot callarg arrays before joining
    // any manager desk: FF opponents, competition 33/34 (or 32 at season end).
    source_desk_callargs_.assign(4, ui::DeskMatchCallargs{default_competition, -1});
    for (size_t slot = 0; slot < g_.manager_teams.size(); ++slot) {
        if (slot >= source_desk_callargs_.size())
            throw FormatError("desk: too many manager slots while capturing Source callargs");
        if (!groups) continue;
        GameState manager_view = g_;
        if (!manager_view.memory.has(::bmh::layout::ActiveManager))
            throw FormatError("desk: active manager field is missing while capturing Source callargs");
        manager_view.memory.set8(::bmh::layout::ActiveManager, uint8_t(slot));
        const ui::DeskState source = ui::desk_from_game(
            manager_view, exe_, turn_driver_.revision, groups, default_competition);
        source_desk_callargs_[slot] = {source.next_competition.value_or(default_competition),
                                       source.next_opponent.value_or(-1)};
    }
}

// Desk projection of the live state at 'revision'; read-only.
ui::DeskState CareerSession::desktop_projection(uint64_t revision) const {
    return desktop_projection(g_, revision);
}

// Desk projection of 'state'. Only at a desk stage (matchday or season end) are the next
// competition and opponent filled in from the captured call arguments; otherwise they stay
// unknown.
ui::DeskState CareerSession::desktop_projection(const GameState& state, uint64_t revision) const {
    std::optional<int> source_default;
    if (turn_driver_.stage == TurnDriver::Stage::SeasonEndDesk) {
        source_default = 32;
    } else if (turn_driver_.stage == TurnDriver::Stage::MatchdayDesks) {
        uint32_t competitions = day_competitions(state, turn_driver_.calendar_entry);
        if (competitions == 0x800000) competitions = 0;
        source_default = competitions != 0 ? 34 : 33;
    } else {
        // An unsupported/empty saved cursor is not evidence that the Source
        // dispatcher produced its no-fixture default. Keep those fields unknown.
        return ui::desk_from_game(state, exe_, revision);
    }
    const int slot = int(state.memory.u8(::bmh::layout::ActiveManager));
    const ui::DeskMatchCallargs defaults{source_default.value_or(34), -1};
    const ui::DeskMatchCallargs* callargs = &defaults;
    if (slot >= 0 && size_t(slot) < source_desk_callargs_.size())
        callargs = &source_desk_callargs_[size_t(slot)];
    return ui::desk_from_game(state, exe_, revision, &turn_driver_.groups, source_default, callargs);
}

// Saving group. Autosave: set once the directory is known; the driver then calls
// write_desk_autosave at each desk. Manual save (save_game) validates the target and writes on a
// staged copy of the random generator. Both go through make_savegame / write_savegame (.FBM
// container). Changing the autosave directory during an active turn throws FormatError.
void CareerSession::configure_autosave_directory(const std::filesystem::path& directory) {
    if (autosave_directory_ && *autosave_directory_ == directory) return;
    if (calendar_advance_active_) throw FormatError("autosave directory change during active turn");
    autosave_directory_ = directory;
    turn_driver_.desk_autosave = [this](const GameState& state, WatcomRandom& random) {
        write_desk_autosave(state, random);
    };
}

// Returns and clears the last autosave error text.
std::string CareerSession::take_autosave_error() {
    return std::exchange(autosave_error_, {});
}

// Writes the desk autosave under the original file name to a temporary file in the autosave
// directory and renames it over the target; the random generator advances only on success.
// Failures are stored in autosave_error_ (never thrown) and the temporary file is removed.
void CareerSession::write_desk_autosave(const GameState& state, WatcomRandom& random) {
    std::filesystem::path temporary;
    try {
        if (!autosave_directory_ || !std::filesystem::is_directory(*autosave_directory_))
            throw FormatError("autosave directory unavailable");
        // CareerSession_Name_TextPointer is the Original TEXTD filename, not the manual browser name.
        const std::filesystem::path name(texts_.strings.at((::bmh::layout::CareerSession_Name_TextPointer - ::bmh::layout::TextPointerTable) / 4));
        if (name.empty() || name != name.filename() || name.extension() != ".FBM")
            throw FormatError("invalid Original autosave filename");
        const auto target = *autosave_directory_ / name;
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(target)) ||
            std::filesystem::is_directory(target)) throw FormatError("autosave target unavailable");
        WatcomRandom staged = random;
        const auto saved = make_savegame(state, staged);
        temporary = target;
        temporary += ".tmp-" + std::to_string(saved.id2);
        if (std::filesystem::exists(std::filesystem::symlink_status(temporary))) {
            temporary.clear();
            throw FormatError("autosave temporary target already exists");
        }
        write_savegame(temporary, saved);
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(target)) ||
            std::filesystem::is_directory(target)) throw FormatError("autosave target changed");
        std::filesystem::rename(temporary, target);
        temporary.clear();
        random = staged;
    } catch (const std::exception& e) {
        autosave_error_ = e.what();
        if (!temporary.empty()) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        }
    }
}

// Manual save of the active manager's game to a .fbm path. Refused while a modal owner, editor
// or pending decision is open or the career is complete; rejects symlink targets, a missing
// directory and an existing file without 'overwrite'. The random generator advances only if the
// write succeeded. Returns false with a short error code in 'error' otherwise.
bool CareerSession::save_game(const std::filesystem::path& path, int manager_slot, bool overwrite, std::string& error) {
    if (advertising_renewal_dispatch_active_) { error="save_blocked_during_advertising_renewal"; return false; }
    if(balance_draft_||balance_dispatch_active_||strengths_owner_||strengths_dispatch_active_||
       top_scorers_owner_||top_scorers_dispatch_active_ || stadium_.block_expansion||other_games_owner_||other_games_dispatch_active_){error="strengths_active";return false;}
    if(balance_draft_||balance_dispatch_active_||league_pairings_draft_||league_pairings_dispatch_active_){error="league_pairings_active";return false;}
    if (transfer_offer_editor_) { error = "transfer_offer_editor_active"; return false; }
    if (pending_offer_dispatch_active_) { error = "pending_offer_decision_active"; return false; }
    if (market_dispatch_active_) { error = "transfer_market_decision_active"; return false; }
    if (career_complete()) { error = "career_complete"; return false; }
    const int active = int(g_.memory.u8(::bmh::layout::ActiveManager));
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (manager_slot != active || manager_slot < 0 || size_t(manager_slot) >= g_.manager_teams.size() ||
        extension != ".fbm" || path.filename().empty()) {
        error = "invalid_save_request"; return false;
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(path.parent_path(), ec) || ec) {
        error = "save_directory_unavailable"; return false;
    }
    const auto status = std::filesystem::symlink_status(path, ec);
    if (!ec && std::filesystem::is_symlink(status)) { error = "save_target_is_symlink"; return false; }
    ec.clear();
    const bool exists = std::filesystem::exists(path, ec);
    if (ec || (exists && !overwrite)) { error = exists ? "overwrite_confirmation_required" : "save_target_unavailable"; return false; }
    try {
        WatcomRandom staged_rand = rand_;
        const Savegame save = make_savegame(g_, staged_rand);
        write_savegame(path, save);
        rand_ = staged_rand;
        return true;
    } catch (const std::exception&) {
        error = "save_write_failed"; return false;
    }
}

// Loads or saves the 0x360-byte menu layout (.men file) of the active manager. Needs the desk
// item handle (record 53), no active flow and a payload whose link chain is valid. A load is
// checked by projecting the desktop on a staged copy before it is committed (revision plus one);
// a save refuses symlinks and existing files without overwrite. Short error codes in 'error'.
bool CareerSession::dispatch_menu_layout(const ui::SaveLoadRequest& request, uint64_t& revision,
                                         std::string& error) {
    constexpr uint32_t kLayouts = ::bmh::layout::Desktops;
    constexpr size_t kLayoutBytes = 0x360;
    constexpr int kDeskItemCount = 54;
    if (request.file_kind != ui::SaveLoadFileKind::menu_layout ||
        request.origin_item_id < 0 || request.origin_item_id >= kDeskItemCount) {
        error = "invalid_menu_layout_origin"; return false;
    }
    if (request.revision != revision || revision == UINT64_MAX || request.path.filename().empty() ||
        request.manager_slot < 0 || request.manager_slot > 3 ||
        !g_.memory.has(::bmh::layout::ActiveManager) || int(g_.memory.u8(::bmh::layout::ActiveManager)) != request.manager_slot) {
        error = "stale_menu_layout_owner"; return false;
    }
    if (calendar_advance_active_ || live_intervention_ || career_complete() ||
        (turn_driver_.stage != TurnDriver::Stage::Empty &&
         turn_driver_.stage != TurnDriver::Stage::MatchdayDesks &&
         turn_driver_.stage != TurnDriver::Stage::SeasonEndDesk) ||
        pending_offer_dispatch_active_ || market_dispatch_active_ || transfer_offer_editor_ ||
        manager_join_ || contract_quote_ || advertising_renewal_dispatch_active_ ||
        balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || opportunities_owner_ ||
        opportunities_dispatch_active_ || league_pairings_draft_ || league_pairings_dispatch_active_ ||
        league_family_owner_ || league_family_teaminfo_active_ || other_games_owner_ ||
        other_games_dispatch_active_ || stadium_.block_expansion || stadium_.investment ||
        stadium_.tip || stadium_.periphery_quote) {
        error = "menu_layout_unavailable_during_active_flow"; return false;
    }
    std::string extension = request.path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (extension != ".men") { error = "invalid_menu_layout_extension"; return false; }
    const size_t active_base = size_t(request.manager_slot) * 0xb0c;
    if (active_base + 0xb0c > g_.manager_records.size() ||
        size_t(request.manager_slot) >= g_.manager_teams.size() ||
        (int(g_.manager_records[active_base + 0x290]) |
         int(g_.manager_records[active_base + 0x291]) << 8) != g_.manager_teams[size_t(request.manager_slot)]) {
        error = "menu_layout_manager_record_mismatch"; return false;
    }
    if (!g_.memory.has(kLayouts + uint32_t(request.manager_slot) * uint32_t(kLayoutBytes) +
                       uint32_t(kLayoutBytes - 1))) {
        error = "menu_layout_memory_unavailable"; return false;
    }
    // CopyN returns the matching record index. In the native MEN file,
    // the final bar cell (source value 72) resolves to record 53; DeskDate
    // dispatches that index to the menu-layout chooser.
    const uint32_t base = kLayouts + uint32_t(request.manager_slot) * uint32_t(kLayoutBytes);
    const int source_item = g_.memory.u8(base + 53u * 16u + 12u) == 72 ? 53 : -1;
    if (source_item != request.origin_item_id) { error = "menu_layout_source_handle_unavailable"; return false; }

    const auto validate_payload = [&](const Bytes& bytes) -> bool {
        if (bytes.size() != kLayoutBytes) { error = "menu_layout_wrong_size"; return false; }
        // Bound the forward traversal before asking the desk projector to
        // validate geometry, kinds, and remaining source fields. Native layouts
        // may contain valid custom back-links; Source load does not normalize them.
        std::array<bool, kDeskItemCount> seen{};
        const int head = bytes[5];
        // CopyN permits an empty linked drawer (head == 0).
        if (head == 0) return true;
        if (head < 1 || head >= kDeskItemCount) { error = "menu_layout_invalid_head"; return false; }
        int item = head;
        for (int steps = 0; steps < kDeskItemCount; ++steps) {
            if (item <= 0 || item >= kDeskItemCount || seen[size_t(item)]) {
                error = "menu_layout_invalid_chain"; return false;
            }
            seen[size_t(item)] = true;
            const size_t at = size_t(item) * 16;
            item = bytes[at + 5];
            if (item == 0) return true;
            if (item == 0xff) { error = "menu_layout_invalid_chain_terminator"; return false; }
        }
        error = "menu_layout_chain_cycle"; return false;
    };

    if (request.action == ui::SaveLoadAction::load) {
        Bytes payload;
        try { payload = read_file(request.path); }
        catch (const std::exception&) { error = "menu_layout_read_failed"; return false; }
        if (!validate_payload(payload)) return false;
        GameState staged = g_;
        for (size_t i = 0; i < kLayoutBytes; ++i)
            staged.memory.set8(base + uint32_t(i), payload[i]);
        try {
            (void)desktop_projection(staged, revision + 1);
        } catch (const std::exception&) { error = "menu_layout_projection_invalid"; return false; }
        g_ = std::move(staged);
        ++revision;
        error.clear();
        return true;
    }
    if (request.action != ui::SaveLoadAction::save) {
        error = "invalid_menu_layout_action";
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(request.path.parent_path(), ec) || ec) {
        error = "menu_layout_directory_unavailable"; return false;
    }
    ec.clear();
    const auto status = std::filesystem::symlink_status(request.path, ec);
    if (!ec && std::filesystem::is_symlink(status)) { error = "menu_layout_target_is_symlink"; return false; }
    ec.clear();
    const bool exists = std::filesystem::exists(request.path, ec);
    if (ec || (exists && !request.overwrite)) {
        error = exists ? "menu_layout_overwrite_confirmation_required" : "menu_layout_target_unavailable";
        return false;
    }
    Bytes payload(kLayoutBytes);
    for (size_t i = 0; i < kLayoutBytes; ++i) payload[i] = g_.memory.u8(base + uint32_t(i));
    if (!validate_payload(payload)) return false;
    try {
        std::ofstream file(request.path, std::ios::binary | std::ios::trunc);
        if (!file) { error = "menu_layout_write_failed"; return false; }
        file.write(reinterpret_cast<const char*>(payload.data()), std::streamsize(payload.size()));
        file.flush();
        if (!file) { error = "menu_layout_write_failed"; return false; }
    } catch (const std::exception&) { error = "menu_layout_write_failed"; return false; }
    ++revision;
    error.clear();
    return true;
}

// Group: presenter hooks. Returns the hooks with the allocator gate taken from the turn driver,
// so presenters can never decide it.
PendingOfferHooks CareerSession::pending_hooks_for_dispatch(PendingOfferHooks hooks) const {
    // Presenters own answers and notice delivery, never allocator provenance.
    hooks.human_buyer_allocator_gate = turn_driver_.transfer_allocator_context;
    return hooks;
}

// Resets the three screen hooks (contract expiry, loan return, loan return offer) to empty. An
// expiring contract without an installed presenter then stops the month change instead of being
// decided automatically.
void CareerSession::install_default_hooks() {
    // InstallDefaultHooks pauses at the Original contracts screen when a term expires.
    // Never synthesize a two-year renewal here: callers must install the real
    // interactive ContractExpiryPresenter or month_change stops without a decision.
    g_.contract_screen = {};
    g_.loan_return_screen = {};
    g_.loan_return_offer_screen = {};
}

// Installs (or with an empty presenter removes) the contract expiry hook, keeping the other two
// screen hooks.
void CareerSession::set_contract_expiry_presenter(ContractExpiryPresenter presenter) {
    // Clear any previously installed presenter before replacing it. The only
    // fallback is an explicit unsupported expiry, never a synthesized decision.
    auto loan_return_hook = std::move(g_.loan_return_screen);
    auto loan_offer_hook = std::move(g_.loan_return_offer_screen);
    install_default_hooks();
    g_.loan_return_screen = std::move(loan_return_hook);
    g_.loan_return_offer_screen = std::move(loan_offer_hook);
    contract_expiry_presenter_installed_ = bool(presenter);
    if (!presenter) return;
    g_.contract_screen = [this, presenter = std::move(presenter)](GameState& g, int manager, int row) {
        return presenter(g, rand_, manager, row);
    };
}

// Installs (or removes) the loan return question hook, keeping the other two screen hooks.
void CareerSession::set_loan_return_presenter(LoanReturnPresenter presenter) {
    auto contract_hook = std::move(g_.contract_screen);
    auto loan_offer_hook = std::move(g_.loan_return_offer_screen);
    install_default_hooks();
    g_.contract_screen = std::move(contract_hook);
    g_.loan_return_offer_screen = std::move(loan_offer_hook);
    loan_return_presenter_installed_ = bool(presenter);
    if (!presenter) return;
    g_.loan_return_screen = [this, presenter = std::move(presenter)](
        GameState& g, int manager, const std::string& question) {
        return presenter(g, rand_, manager, question);
    };
}

// Installs (or removes) the loan return offer hook; the hook runs the presenter against staged
// club modal services with the allocator gate taken from the turn driver, and the staged runtime
// bytes are kept only when the presenter's result is not negative.
void CareerSession::set_loan_return_offer_presenter(LoanReturnOfferPresenter presenter,
                                                    HumanBuyerSaleHooks sale_hooks) {
    auto contract_hook = std::move(g_.contract_screen);
    auto loan_question_hook = std::move(g_.loan_return_screen);
    install_default_hooks();
    g_.contract_screen = std::move(contract_hook);
    g_.loan_return_screen = std::move(loan_question_hook);
    loan_return_offer_presenter_installed_ = bool(presenter);
    if (!presenter) return;
    g_.loan_return_offer_screen = [this, presenter = std::move(presenter),
                                   sale_hooks = std::move(sale_hooks)](
        GameState& g, GameRandom& game, WatcomRandom& rand, int manager,
        int source_slot, int list_entry, const HumanBuyerSaleHooks&) mutable {
        auto staged_runtime = club_modal_runtime_;
        auto services = staged_club_modal_services(g, game, rand, staged_runtime);
        auto hooks = sale_hooks;
        switch (turn_driver_.transfer_allocator_context) {
        case PendingOfferAllocatorGate::zero: hooks.allocator_gate = HumanBuyerSaleAllocatorGate::zero; break;
        case PendingOfferAllocatorGate::blocked: hooks.allocator_gate = HumanBuyerSaleAllocatorGate::blocked; break;
        default: hooks.allocator_gate = HumanBuyerSaleAllocatorGate::unsupported; break;
        }
        hooks.direct_mode = HumanBuyerSaleDirectMode::ordinary_ai_purchase;
        const int result = presenter(g, game, rand, services, manager, source_slot, list_entry, hooks);
        if (result >= 0) club_modal_runtime_ = std::move(staged_runtime);
        return result;
    };
}

}  // namespace bmh::app
