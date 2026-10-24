// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "modern/game/modern_state.hpp"
#include "modern/app/modern_session.hpp"
#include "modern/content/world_expand.hpp"
#include "modern/content/world_package.hpp"
#include "modern/content/world_save.hpp"
#include "modern/game/balance_plan.hpp"
#include "modern/game/bank_credit.hpp"
#include "modern/game/career.hpp"
#include "modern/game/club_info.hpp"
#include "modern/game/club_player_detail.hpp"
#include "modern/game/cup.hpp"
#include "modern/game/desk.hpp"
#include "modern/game/desk_actions.hpp"
#include "modern/game/driver.hpp"
#include "modern/game/environment.hpp"
#include "modern/game/highscore.hpp"
#include "modern/game/league_pairings.hpp"
#include "modern/game/matchday.hpp"
#include "modern/game/matchday_setup.hpp"
#include "modern/game/month.hpp"
#include "modern/game/national.hpp"
#include "modern/game/other_games.hpp"
#include "modern/game/poach.hpp"
#include "modern/game/prediction.hpp"
#include "modern/game/reschedule.hpp"
#include "modern/game/save.hpp"
#include "modern/game/season.hpp"
#include "modern/game/source_tip.hpp"
#include "modern/game/stadium_blocks.hpp"
#include "modern/game/table_forecast.hpp"
#include "modern/game/turn.hpp"
#include "modern/game/turn_driver.hpp"
#include "modern/game/week.hpp"
#include "modern/game/weekly.hpp"
#include "content/save.hpp"
#include "import/save.hpp"

#include <stdexcept>
#include <system_error>
#include <algorithm>
#include <set>
#include <array>
#include <atomic>
#include <cctype>
#include <bit>
#include <fstream>
#include <limits>
#include <utility>

#include "formats/binary.hpp"
#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "modern/game/career.hpp"
#include "modern/game/cup.hpp"
#include "modern/game/driver.hpp"
#include "modern/game/desk.hpp"
#include "modern/game/desk_actions.hpp"
#include "game/match.hpp"
#include "game/messages.hpp"
#include "modern/game/prediction.hpp"
#include "game/schedule.hpp"
#include "modern/game/bank_credit.hpp"
#include "modern/game/matchday_setup.hpp"
#include "modern/game/matchday.hpp"
#include "game/lineup.hpp"
#include "modern/game/month.hpp"
#include "modern/game/national.hpp"
#include "game/schedule.hpp"
#include "game/table.hpp"
#include "modern/game/table_forecast.hpp"
#include "modern/game/save.hpp"
#include "modern/game/season.hpp"
#include "modern/game/turn.hpp"
#include "modern/game/week.hpp"
#include "modern/game/weekly.hpp"
#include "ui/desk_state.hpp"
#include "ui/league_table.hpp"
#include "ui/other_games_state.hpp"
#include "ui/manager.hpp"
#include "ui/pending_offer.hpp"
#include "ui/training.hpp"

namespace bmh::modern::app {
namespace {
std::atomic<uint64_t> next_session_id{1};

constexpr uint32_t kSavedCalendar = ::bmh::layout::CompetitionCalendar;
constexpr uint32_t kSavedCompetitionBits = ::bmh::layout::SavedCompetition;
constexpr uint8_t kBlockSeat = 1;
constexpr uint8_t kBlockRoof = 2;
constexpr uint8_t kBlockFeatures = kBlockSeat | kBlockRoof;

bool is_block_expansion_action(ui::StadiumRequestKind kind) {
    return kind == ui::StadiumRequestKind::expansion_select ||
        kind == ui::StadiumRequestKind::expansion_set_total ||
        kind == ui::StadiumRequestKind::expansion_toggle_feature ||
        kind == ui::StadiumRequestKind::expansion_cancel ||
        kind == ui::StadiumRequestKind::expansion_confirm ||
        kind == ui::StadiumRequestKind::expansion_answer_tip;
}

int national_opportunities_rank(const ModernState& game,int team,std::array<uint8_t,24>& ranks) {
    ranks.fill(1);
    const int rounds=game.league_rounds.empty()?0:int8_t(game.league_rounds.at(0));
    if(rounds<2||rounds>=league_matchdays(0))return 0;
    return team>=modern::league_clubs()?9:0;
}

uint8_t source_runtime_byte(const ModernState& state,const std::map<uint32_t,uint8_t>& runtime,
                            const LeImage& exe,uint32_t address) {
    if(state.memory.has(address))return state.memory.u8(address);
    const auto found=runtime.find(address);return found==runtime.end()?exe.u8(address):found->second;
}
void source_runtime_set_byte(ModernState& state,std::map<uint32_t,uint8_t>& runtime,
                             uint32_t address,uint8_t value) {
    if(state.memory.has(address))state.memory.set8(address,value);
    else runtime[address]=value;
}

uint32_t source_runtime_u32(const ModernState& state,const std::map<uint32_t,uint8_t>& runtime,
                             const LeImage& exe,uint32_t address) {
    return uint32_t(source_runtime_byte(state,runtime,exe,address)) |
        uint32_t(source_runtime_byte(state,runtime,exe,address+1))<<8 |
        uint32_t(source_runtime_byte(state,runtime,exe,address+2))<<16 |
        uint32_t(source_runtime_byte(state,runtime,exe,address+3))<<24;
}

void apply_balance_source_style(ui::BalanceState& page,const ModernState& state,
                                const std::map<uint32_t,uint8_t>& runtime,const LeImage& exe) {
    page.decorated=source_runtime_byte(state,runtime,exe,::bmh::layout::PlainBackground)==0;
    page.original_panel=source_runtime_byte(state,runtime,exe,::bmh::layout::SolidPanelBackground)==0;
    page.gradient=source_runtime_byte(state,runtime,exe,::bmh::layout::TextGradientDisabled)==0 &&
        source_runtime_byte(state,runtime,exe,::bmh::layout::TextGradientMode)==2;
    page.cached_backdrop=source_runtime_u32(state,runtime,exe,::bmh::layout::CareerSession_ApplyBalanceSourceStyle_ExecutableField)!=0;
}

uint32_t saved_group_u32(const Bytes& bytes, size_t offset) {
    return uint32_t(bytes.at(offset)) | (uint32_t(bytes.at(offset + 1)) << 8) |
           (uint32_t(bytes.at(offset + 2)) << 16) | (uint32_t(bytes.at(offset + 3)) << 24);
}

void initialize_saved_match_group(MatchGroup& group, const ModernState& state, size_t index,
                                  int league) {
    group.desc = modern::group_descriptor(state, index);
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
    const size_t result_at = size_t(league) * 0x17c + size_t(round) * 10;
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

void initialize_saved_cup_group(MatchGroup& group, const ModernState& state, size_t index,
                                int cup_id, int entry_index) {
    const StaticMemory& mem = state.memory;
    group.desc = modern::group_descriptor(state, index);
    const uint32_t guest_entry = saved_group_u32(group.desc, 0xcc);
    if (group.desc.size() != 0xd0 || group.desc[0] != cup_id + 1 ||
        guest_entry < kGuestDataBase || (guest_entry - kGuestDataBase) != modern::cup_entry_address(entry_index))
        throw std::runtime_error("cup descriptor mismatch group=" + std::to_string(index) +
                                 " type=" + std::to_string(group.desc[0]) +
                                 " guest=" + std::to_string(guest_entry) +
                                 " stage=" + std::to_string(entry_index));
    const uint32_t entry = modern::cup_entry_address(entry_index);
    // In memory the descriptor stores a guest pointer; MatchGroup carries the data-object
    // address, as setup_cup_groups does before store_group_descriptors adds the guest base.
    for (int i = 0; i < 4; ++i) group.desc.at(0xcc + size_t(i)) = uint8_t(entry >> (i * 8));
    const uint8_t flags = mem.u8(entry + 0x14);
    const bool knockout = (flags & 4) != 0;
    const int teams = mem.u8(entry + 0x1a);
    if (int16_t(mem.u16(entry + 0x18)) != cup_id || teams < 2 || (teams & 1) ||
        group.desc[2] == 0 || group.desc[2] > (knockout ? 32 : 18))
        throw std::runtime_error("saved turn cursor: cup descriptor has invalid stage or match count");
    const int round = modern::cup_round(mem, entry_index);
    int first = 0;
    int expected_matches = 0;
    if (knockout) {
        if (round < 0 || round >= 8 || (1 << (round + 1)) > teams)
            throw std::runtime_error("saved turn cursor: cup knockout round is outside the stage");
        expected_matches = teams / (1 << (round + 1));
        first = uint8_t(modern::cup_round_offset(mem, entry_index, round));
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

int current_cup_stage(const StaticMemory& mem, int cup_id) {
    int entry = modern::find_cup(mem, cup_id);
    if (entry < 0) return -1;
    for (int guard = 0; guard < kCupEntries; ++guard) {
        const uint32_t address = modern::cup_entry_address(entry);
        if (modern::cup_round(mem, entry) < mem.u8(address + 0x1b)) return entry;
        const int next = int8_t(mem.u8(address + 0x16));
        if (next < 0 || next >= kCupEntries) return -1;
        entry = next;
    }
    return -1;
}

void initialize_saved_replay_group(MatchGroup& group, ModernState& state, size_t index,
                                   int entry_index, int day) {
    constexpr uint32_t kPostponed = ::bmh::layout::Postponed;
    constexpr uint32_t kReplayFixtures = ::bmh::layout::ReplayFixtures;
    constexpr uint32_t kReplayResults = ::bmh::layout::ReplayResults;
    constexpr uint8_t kPostponedMark = 0xcd;
    constexpr int kMaxReplayMatches = 9;  // AddBlock clears 0x12 result bytes.
    StaticMemory& mem = state.memory;
    const uint32_t entry = modern::cup_entry_address(entry_index);
    group.desc = modern::group_descriptor(state, index);
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
            match >= int(kLeagueSizes[league] / 2))
            throw std::runtime_error("saved replay record is outside the league schedule");
        const std::vector<Fixture> fixtures = generate_fixtures(
            league_table_teams(league), round + 1, league_matchdays(league) / 2,
            league_first_team(league));
        if (match >= int(fixtures.size()))
            throw std::runtime_error("saved replay record has no corresponding fixture");
        const size_t result = size_t(league) * 0x17c + size_t(round) * 10 + size_t(match);
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
    // ModernState therefore has no valid prior runtime buffer to reuse.
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

void restore_saved_turn_cursor(TurnDriver& driver, ModernState& state, const Database& db,
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
    const int position = int(state.memory.u8(::bmh::layout::DeskPosition));
    const int active = int(state.memory.u8(::bmh::layout::ActiveManager));
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
    const uint32_t calendar_competitions = modern::day_competitions(state, entry);
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
    // Expand the selected edition's groups. Only the German third divisions
    // share bit 2; the English divisions each have their own calendar group.
    // Stale descriptors beyond the active group count are ignored.
    for (int bit = 0; bit < 5; ++bit) {
        if (!((competitions >> bit) & 1u)) continue;
        const int first = bit, last = bit < 4 ? bit + 1 : 8;
        for (int k = first; k < last; ++k)
            for (int league = kGroupLeague[k]; league < kGroupLeague[k] + kGroupLeagues[k]; ++league)
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
            const uint32_t first_entry = modern::cup_entry_address(entry);
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
                if (state.memory.u8(modern::cup_entry_address(entry) + 0x1a) == 0) continue;
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
    if (!modern::begin_turn_driver(driver, state, std::move(groups), entry, db.raw_referee_properties,
                           session_id, revision, error))
        throw std::runtime_error("saved turn cursor: " + error);
    driver.referees = std::move(referees);
    // Runtime provenance belongs to this successfully restored Native desk loop.
    // It is not inferred from a nonzero cursor, and does not select a policy.
    driver.loaded_matchday_resume = true;
}
}

// The league structure of the loaded game (schedule.hpp): sizes LeagueSizes (German
// 18 18 18 18 18 12 12 12 12, English 20 20 20 20 10 12 12 12 12) and calendar groups.
static void install_league_sizes(const LeImage& exe) {
    uint8_t sizes[kLeagueCount];
    for (int l = 0; l < kLeagueCount; ++l) sizes[l] = exe.u8(::bmh::layout::LeagueSizes + uint32_t(l));
    set_league_sizes(sizes);
    install_fixture_tables(exe);
    kEnglishCompetitions = sizes[0] == 20;
    // the calendar groups (CalendarFirstLeague first league, CalendarLeagueCounts league count per bit)
    uint8_t first[8], count[8];
    for (uint32_t b = 0; b < 8; ++b) first[b] = exe.u8(::bmh::layout::CalendarFirstLeague + b), count[b] = exe.u8(::bmh::layout::CalendarLeagueCounts + b);
    set_league_groups(first, count);
    // the promotion ladder (schedule.hpp)
    for (uint32_t l = 0; l < uint32_t(kLeagueCount); ++l) {
        kLeagueRelegation[l] = exe.u8(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField2 + l);
        kLeagueEurope[l] = exe.u8(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField + l);
        kLeagueSuffix[l] = char(l < 5 ? exe.u8(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField3 + l * 2) : 0);
    }
    for (uint32_t g = 0; g < 8; ++g) kGroupBelow[g] = exe.u8(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField5 + g), kGroupBelowCount[g] = exe.u8(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField4 + g);
    kNewcomerFirst = exe.u16(::bmh::layout::CareerSession_DispatchLineup_RuntimeField);
    kNewcomerCount = exe.u16(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField6);
    // EXE strings the engine posts (messages.hpp)
    install_message_texts(exe);
}

ModernSession::ModernSession(const std::filesystem::path& data, uint32_t watcom_seed)
    : data_directory_(data), exe_(load_le(data / "BMH.exe")),
      db_(load_database(data / "M3D.MAN")),
      texts_(load_textd(data / "TEXTD.BLK")),
      rand_(watcom_seed) {
    set_world_layout(classic_world_layout());  // a previous game in this process may have left a world
    kLeagueTablesPerLeague = false;
    install_league_sizes(exe_);
    turn_driver_.alternate_match_event_roll = exe_.u8(::bmh::layout::AlternateEventRoll) != 0;
    turn_driver_.disable_match_events = exe_.u8(::bmh::layout::MatchEventsDisabled) != 0;
    turn_driver_.transfer_allocator_context = exe_.u8(::bmh::layout::MatchEventsDisabled) == 0
        ? TransferAllocatorContext::zero : TransferAllocatorContext::blocked;
    session_id_ = next_session_id.fetch_add(1, std::memory_order_relaxed);
    initialize_club_modal_runtime();
    g_ = modern::startup_state(exe_, db_, read_file(data / "M3.MEN"), read_file(data / "M3.DAT"));
    // A Modern world: give the Classic start the dimensions and data of the world (M3).
    if (std::getenv("MODERN_DEBUG")) std::fprintf(stderr, "session: classic start built\n");
    if (const World* world = current_world()) {
        world_report_ = expand_to_world(g_, *world);
        install_world_referees(db_.raw_referee_properties, *world);
    }
    if (std::getenv("MODERN_DEBUG")) std::fprintf(stderr, "session: world expanded\n");
    if (const auto resource = content::find(data)) {
        g_.display_start_year = resource->content->start_year;
        content::set_display_start_year(data, g_.display_start_year);
        exe_.set_display_start_year(g_.display_start_year);
    }
    g_.texts = &texts_;
    g_.currency_suffix = exe_.cstring(::bmh::layout::CurrencySuffixText);  // CurrencySuffixText
    modern::configure_training_camp_data(g_, exe_);
    if (const World* world = current_world()) install_training_camps(g_, *world);
    for (size_t i = 0; i < g_.credit_rate_table.size(); ++i) g_.credit_rate_table[i] = exe_.u8(::bmh::layout::BankCredit_BankCreditContext_RuntimeField3 + uint32_t(i));
    g_.credit_divisor = std::bit_cast<double>(uint64_t(exe_.u32(::bmh::layout::BankCredit_BankCreditContext_RuntimeField)) | uint64_t(exe_.u32(::bmh::layout::BankCredit_BankCreditContext_RuntimeField2)) << 32);
    g_.credit_rate_table_available = true;
    install_default_hooks();
    if (content::find(data))
        turn_driver_.desk_autosave = [this](const ModernState &state, WatcomRandom &random) {
            write_desk_autosave(state, random);
        };
    const bool trace = std::getenv("MODERN_DEBUG") != nullptr;
    if (trace) std::fprintf(stderr, "session: startup players\n");
    modern::startup_players(g_, game_, rand_);  // CareerFirstSteps2
    MinuteReport report;
    if (trace) std::fprintf(stderr, "session: new career before dialog\n");
    if (!modern::new_career_before_dialog(g_, game_, rand_, report))  // InitializeCareer up to the dialog
        throw std::runtime_error("career start: " + report.reason);
    if (trace) std::fprintf(stderr, "session: career prepared\n");
}

ModernSession::ModernSession(const std::filesystem::path &data, uint32_t watcom_seed,
                             const std::filesystem::path &save_path, bool importing_legacy)
    : data_directory_(data), exe_(load_le(data / "BMH.exe")), db_(load_database(data / "M3D.MAN")),
      texts_(load_textd(data / "TEXTD.BLK")), rand_(watcom_seed) {
    set_world_layout(classic_world_layout());  // a previous game in this process may have left a world
    kLeagueTablesPerLeague = false;
    install_league_sizes(exe_);
    turn_driver_.alternate_match_event_roll = exe_.u8(::bmh::layout::AlternateEventRoll) != 0;
    turn_driver_.disable_match_events = exe_.u8(::bmh::layout::MatchEventsDisabled) != 0;
    turn_driver_.transfer_allocator_context = exe_.u8(::bmh::layout::MatchEventsDisabled) == 0
        ? TransferAllocatorContext::zero : TransferAllocatorContext::blocked;
    session_id_ = next_session_id.fetch_add(1, std::memory_order_relaxed);
    initialize_club_modal_runtime();
    bool original_english_save = false;
    std::optional<content::SavedCareer> native;
    if (const auto resource = content::find(data); resource && !importing_legacy) {
        if (content::key(save_path.extension()) != ".obs")
            throw FormatError("0.2 loads native OBS saves; use Add to migrate legacy games");
        if (const World* world = current_world()) {
            // A Modern career: the save names its world; the dimensions come from the world.
            auto saved = load_world_career(save_path, *world);
            install_world_dimensions(*world);
            install_world_referees(db_.raw_referee_properties, *world);
            if (saved.state.master_database_metadata.size() != db_.raw_referee_properties.size() * 2)
                throw FormatError("the save and its world disagree on the referees");
            g_ = std::move(saved.state);
            rand_ = saved.random;
            game_ = saved.game;
            native.emplace();
            native->driver = std::move(saved.driver);
            native->session = std::move(saved.session);
            native->header = std::move(saved.header);
        } else {
            native = content::load_career(save_path, *resource->content);
            g_ = std::move(native->state);
            rand_ = native->random;
            game_ = native->game;
        }
    } else {
        const Savegame file = load_savegame(save_path);
        original_english_save = file.version == "V2.20";
        if (!file.checksum_ok ||
            (file.version != "V2.24" && !(importing_legacy && kEnglishCompetitions && original_english_save)))
            throw FormatError("savegame: unsupported or damaged FBM");
        const DbCounts counts{exe_.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField),
                              exe_.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3),
                              exe_.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2),
                              exe_.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
        const auto stream = original_english_save ? obw::normalize_english_save(file, counts) : file.stream;
        const SaveState save(stream, counts);
        const auto manager_count = save.data(::bmh::layout::ManagerCount)[0];
        if (manager_count < 1 || manager_count > 4)
            throw FormatError("Invalid legacy manager count");
        if (importing_legacy) {
            size_t english_blocks = 0;
            for (const auto &[address, size] : kEnglishCompetitionBlocks)
                for (const auto &block : save.extension())
                    if (block.address == address && block.bytes.size() == size)
                        ++english_blocks;
            if ((kEnglishCompetitions && english_blocks != std::size(kEnglishCompetitionBlocks)) ||
                (!kEnglishCompetitions && english_blocks != 0))
                throw FormatError(
                    "Spielstand und Mod-Ausgabe passen nicht zusammen oder englische Pokaldaten fehlen.");
        }
        g_ = GameState::from_save(save, counts);
        if (importing_legacy && file.plain_header.size() >= 11) {
            const std::string year(file.plain_header.begin() + 7, file.plain_header.begin() + 11);
            if (std::all_of(year.begin(), year.end(), [](unsigned char c) { return std::isdigit(c); })) {
                const int elapsed = ui::desk_date(g_.date, exe_).year - exe_.display_start_year();
                const int origin = std::stoi(year) - elapsed;
                if (origin >= 1900 && origin <= 9998)
                    g_.display_start_year = origin;
            }
        }
    }
    if (content::find(data)) {
        content::set_display_start_year(data, g_.display_start_year);
        exe_.set_display_start_year(g_.display_start_year);
    }
    // CompetitionPointers2, after DeskDate2: these are process statics, not FBM fields.
    // Resuming within this month must not count it as a newly elapsed month.
    if (!native) {
        g_.autosave_months = 0;
        g_.autosave_month = modern::month_code(g_.date);
    }
    // Original CompetitionPointers2 calls CompetitionPointers after loading the stream. This
    // rebinds pointer fields in the saved competition table before any resume
    // path dereferences them (the serialized values may point into the old process).
    modern::competition_pointers(g_.memory, exe_);
    g_.texts = &texts_;
    g_.currency_suffix = exe_.cstring(::bmh::layout::CurrencySuffixText);  // CurrencySuffixText
    modern::configure_training_camp_data(g_, exe_);
    if (const World* world = current_world()) install_training_camps(g_, *world);
    for (size_t i = 0; i < g_.credit_rate_table.size(); ++i) g_.credit_rate_table[i] = exe_.u8(::bmh::layout::BankCredit_BankCreditContext_RuntimeField3 + uint32_t(i));
    g_.credit_divisor = std::bit_cast<double>(uint64_t(exe_.u32(::bmh::layout::BankCredit_BankCreditContext_RuntimeField)) | uint64_t(exe_.u32(::bmh::layout::BankCredit_BankCreditContext_RuntimeField2)) << 32);
    g_.credit_rate_table_available = true;
    install_default_hooks();
    if (content::find(data))
        turn_driver_.desk_autosave = [this](const ModernState &state, WatcomRandom &random) {
            write_desk_autosave(state, random);
        };
    if (!native)
        modern::national_coach_after_load(
            g_); // (NationalCoachAfterLoad: a Bundestrainer takes the national team over again)
    if (!native && g_.memory.has(::bmh::layout::CareerSession_Save_RuntimeField))
        game_.set_seed(int32_t(g_.memory.u32(::bmh::layout::CareerSession_Save_RuntimeField)));
    if (native) {
        content::restore_driver(turn_driver_, native->driver);
        restore_native_session_context(native->session);
        turn_driver_.session_id = session_id_;
        turn_driver_.revision = 1;
        exe_.set_display_start_year(g_.display_start_year);
    } else if (original_english_save &&
               !g_.memory.u8(::bmh::layout::CareerSession_RuntimeError_RuntimeField4)) {
        // V2.20 never stored transient match descriptors. Like its original loader,
        // rebuild the current groups without replaying daily/financial progression
        // or shuffling the saved manager order.
        const int day = g_.season_day();
        if (day < 0 || day >= 365)
            throw FormatError("V2.20 calendar day is invalid");
        const auto entry = g_.memory.u16(::bmh::layout::CompetitionCalendar + uint32_t(day) * 2);
        const auto competitions = g_.memory.u32(::bmh::layout::SavedCompetition);
        std::vector<uint8_t> referees = db_.raw_referee_properties;
        for (auto &referee : referees)
            referee &= 0x7f;
        MatchdayContext context;
        context.month = modern::month_code(g_.date);
        context.season_day = day;
        context.weekday = ui::desk_date(g_.date, exe_).weekday;
        context.referees = &referees;
        MinuteReport report;
        auto groups = modern::setup_matchday(g_, competitions, context, game_, rand_, report);
        if (report.unsupported)
            throw FormatError("V2.20 continuation: " + report.reason);
        std::string error;
        if (!modern::begin_turn_driver(turn_driver_, g_, std::move(groups), entry, db_.raw_referee_properties,
                               session_id_, 1, error))
            throw FormatError(error);
        turn_driver_.referees = std::move(referees);
        turn_driver_.loaded_matchday_resume = true;
    } else
        restore_saved_turn_cursor(turn_driver_, g_, db_, session_id_, 1, saved_turn_error_);
    if (importing_legacy && !saved_turn_error_.empty())
        throw FormatError(saved_turn_error_);
    if (!native)
        capture_source_desk_callargs();
}

int ModernSession::runtime_club(int master) const { return runtime_club(g_, master); }

int ModernSession::runtime_club(const ModernState& state, int master) const {
    // ClubsChangePlaces moves a club together with its name: the club whose name offset is the
    // master entry's is the one the manager chose
    const uint16_t offset = uint16_t(db_.raw_club_name_offsets.at(size_t(master) * 2) | db_.raw_club_name_offsets.at(size_t(master) * 2 + 1) << 8);
    for (size_t t = 0; t < state.club_ids.size(); t++)
        if (state.club_ids[t] == offset) return int(t);
    return -1;
}

ui::SelectionContext ModernSession::selection_context(uint64_t revision) const { return selection_context_for(g_, revision); }

ui::SelectionContext ModernSession::selection_context_for(const ModernState& state, uint64_t revision) const {
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
    // A Modern world has more clubs than the original's master ids, and the clubs it added have
    // none. The human chooses German clubs only (docs/new_engine); those keep their master ids.
    // The picker needs the order up to its extra-team tabs (the amateur clubs behind all league
    // clubs). A club without a valid unique master id there (one the world added) gets an unused
    // one, so the order stays unique; only German clubs can be confirmed (confirm_manager).
    if (current_world()) {
        const size_t needed = size_t(world_layout().league_teams) +
                              exe_.u16(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField6);
        const int masters = int(db_.club_names.size());
        c.club_order.resize(std::min(c.club_order.size(), needed));
        std::set<int> used;
        std::vector<size_t> invalid;
        for (size_t t = 0; t < c.club_order.size(); ++t)
            if (c.club_order[t] < 0 || c.club_order[t] >= masters || !used.insert(c.club_order[t]).second) invalid.push_back(t);
        int spare = 0;
        for (size_t t : invalid) {
            while (spare < masters && used.count(spare)) ++spare;
            if (spare >= masters) throw FormatError("manager selection: not enough master ids for the world's clubs");
            c.club_order[t] = spare;
            used.insert(spare);
        }
    }
    return c;
}

std::vector<ui::ManagerChoice> ModernSession::manager_choices(uint64_t revision) const {
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

bool ModernSession::begin_manager_join(const ui::DeskRequest& origin, const ui::SetupOptions& options,
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
        draft.original_stream = modern::save_stream(g_); draft.original_rand_seed = rand_.seed();
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
        modern::career_managers_begin(draft.state, true);
        manager_join_ = std::move(draft);
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

ui::SelectionContext ModernSession::manager_join_context() const {
    if (!manager_join_) throw FormatError("join: no draft");
    return selection_context_for(manager_join_->state, manager_join_->context_revision);
}

bool ModernSession::manager_join_complete() const {
    return manager_join_ && manager_join_->choices.size() == size_t(manager_join_->options.managers);
}

bool ModernSession::confirm_manager_join(const ui::ManagerChoice& choice, std::string& error) {
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
        MinuteReport report;
        if (!modern::career_add_manager(draft.state, exe_, choice.slot, selected, true, draft.game, draft.rand, report))
            throw FormatError(report.reason);
        draft.choices.push_back(choice); ++draft.context_revision;
        return true;
    } catch (const std::exception& e) { manager_join_ = std::move(before); error = e.what(); return false; }
}

bool ModernSession::commit_manager_join(ui::CareerInitializationResult& result, uint64_t& revision,
                                       std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    if (!manager_join_complete()) { error = "join: selection incomplete"; return false; }
    auto before = manager_join_checkpoint();
    const auto previous_revision = revision;
    try {
        const auto& draft = *manager_join_;
        if (draft.origin.revision != revision || modern::save_stream(g_) != draft.original_stream ||
            rand_.seed() != draft.original_rand_seed || game_.seed() != draft.original_game_seed ||
            rand_.calls() != draft.original_rand_calls || game_.calls() != draft.original_game_calls)
            throw FormatError("join: career changed during selection");
        g_ = draft.state; rand_ = draft.rand; game_ = draft.game;
        modern::career_managers_end(g_, true);
        managers_.clear();
        for (const auto& c : draft.choices)
            managers_.push_back({c.slot, c.master_club, g_.manager_teams.at(size_t(c.slot))});
        // ConfirmManager retains the current desk/cursor. Weiter performs the later handoff.
        const auto next_revision = revision + 1;
        turn_driver_.revision = next_revision;
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

void ModernSession::apply_options(const ui::SetupOptions& o) {
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
    modern::career_managers_begin(g_);
    options_applied_ = true;
}

bool ModernSession::confirm_manager(const ui::SetupOptions& options, const ui::ManagerChoice& choice, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    if (!options_applied_) apply_options(options);
    const int m = int(managers_.size());
    if (m >= options.managers) {
        error = "more managers than chosen in the options";
        return false;
    }
    CareerManager c;
    c.name = choice.name;
    c.team = runtime_club(choice.master_club);
    // Modern 0.3: a human manages a German club only (docs/new_engine)
    if (current_world() && (c.team < 0 || c.team >= modern::german_clubs())) {
        error = "only German clubs can be managed";
        return false;
    }
    c.league = choice.start_league;
    c.strength = choice.strength;
    c.portrait = choice.portrait;
    if (c.team < 0) {
        error = "club not found";
        return false;
    }
    MinuteReport report;
    if (!modern::career_add_manager(g_, exe_, m, c, false, game_, rand_, report)) {
        error = report.reason;
        return false;
    }
    managers_.push_back({choice.slot, choice.master_club, g_.manager_teams.at(size_t(m))});
    return true;
}

std::optional<ClubInfoEntry> ModernSession::enter_club_information(int team, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return std::nullopt; }
    error.clear();
    try { return modern::prepare_club_info_entry(g_, team); }
    catch (const std::invalid_argument& e) { error = e.what(); return std::nullopt; }
}

std::optional<uint8_t> ModernSession::change_club_information_category(int team, int requested,
                                                                    std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return std::nullopt; }
    error.clear();
    try { return modern::set_club_info_category(g_, team, requested); }
    catch (const std::invalid_argument& e) { error = e.what(); return std::nullopt; }
}

std::optional<ClubPlayerDetailRedraw> ModernSession::redraw_club_player_detail(
    const ClubPlayerDetailSelection& selection, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return std::nullopt; }
    error.clear();
    auto next_game = game_;
    auto next_rand = rand_;
    try {
        auto redraw = modern::club_player_detail_redraw(g_, selection, exe_, next_game, next_rand);
        game_ = next_game;
        rand_ = next_rand;
        return redraw;
    } catch (const std::invalid_argument& e) {
        error = e.what();
        return std::nullopt;
    }
}

std::optional<bool> ModernSession::dispatch_club_player_national_action(
    const ClubPlayerDetailSelection& selection, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return std::nullopt; }
    error.clear();
    auto next_state = g_;
    auto next_rand = rand_;
    MinuteReport report;
    try {
        const bool applied = modern::club_player_detail_national_action(next_state, selection, next_rand, report);
        if (!report.reason.empty()) { error = report.reason; return std::nullopt; }
        g_ = std::move(next_state);
        rand_ = next_rand;
        return applied; // Source still refreshes the lineup when an add finds a full squad.
    } catch (const std::invalid_argument& e) {
        error = e.what();
        return std::nullopt;
    }
}

std::optional<PendingOfferResult> ModernSession::dispatch_club_player_pending_offers(
    const ClubPlayerDetailSelection& selection, uint64_t expected_revision,
    const PendingOfferHooks& hooks, uint64_t& revision, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return std::nullopt; }
    error.clear();
    if (expected_revision != revision || pending_offer_dispatch_active_ || market_dispatch_active_ ||
        revision == std::numeric_limits<uint64_t>::max()) {
        error = "pending offer: stale or nested player-card action";
        return std::nullopt;
    }
    auto next_state = g_;
    auto next_game = game_;
    auto next_rand = rand_;
    pending_offer_dispatch_active_ = true;
    struct Reset { bool& active; ~Reset() { active = false; } } reset{pending_offer_dispatch_active_};
    MinuteReport report;
    try {
        const auto result = modern::process_player_card_pending_offers(
            next_state, selection, next_game, next_rand, pending_hooks_for_dispatch(hooks), report);
        if (report.unsupported || !result.completed) {
            error = report.reason.empty() ? "pending offer: manual decision incomplete" : report.reason;
            return std::nullopt;
        }
        if (result.processed != 0) {
            g_ = std::move(next_state);
            game_ = next_game;
            rand_ = next_rand;
            // A nested cup modal belongs to the current calendar transaction;
            // its outer driver publishes that revision when advancement returns.
            if (!calendar_advance_active_) ++revision;
        }
        return result;
    } catch (const std::exception& e) {
        error = e.what();
        return std::nullopt;
    }
}

void ModernSession::initialize_club_modal_runtime() {
    // LE object4 has bb cdd/de/e1 in its zero-initialized virtual tail;
    // CareerSession_InitializeClubModalRuntime_ExecutableField is file-backed zero. These are explicit process-start values,
    // carried by this runtime owner across modal entry/return, not FBM bytes.
    for(uint32_t address:{::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableFieldUnsigned,::bmh::layout::CareerSession_DispatchPoachUi_ExecutableFieldUnsigned,::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableField2Unsigned,::bmh::layout::CareerSession_InitializeClubModalRuntime_ExecutableFieldUnsigned})
        club_modal_runtime_.emplace(address,exe_.u8(address));
}

SportschauSourceOptions ModernSession::sportschau_source_options() const {
    return {source_runtime_byte(g_, club_modal_runtime_, exe_, ::bmh::layout::CareerSession_SportschauSourceOptions_ExecutableField),
            source_runtime_byte(g_, club_modal_runtime_, exe_, ::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableField2), exe_.u16(::bmh::layout::TeamsWithSquads),
            sportschau_rating_tables(exe_)};
}

ClubModalServices ModernSession::staged_club_modal_services(ModernState& state,
    GameRandom& game,WatcomRandom& rand,std::map<uint32_t,uint8_t>& runtime) {
    ClubModalServices services;
    auto* g=&state; auto* gr=&game; auto* wr=&rand; auto* bytes=&runtime;
    services.state=[g]() -> ModernState& { return *g; };
    services.enter_club=[g](int team) { return modern::prepare_club_info_entry(*g,team); };
    services.change_category=[g](int team,int category) { return modern::set_club_info_category(*g,team,category); };
    services.redraw_player=[this,g,gr,wr](const ClubPlayerDetailSelection& selection) {
        auto next_game=*gr; auto next_rand=*wr;
        auto result=modern::club_player_detail_redraw(*g,selection,exe_,next_game,next_rand);
        *gr=next_game; *wr=next_rand; return result;
    };
    services.national_action=[g,wr](const ClubPlayerDetailSelection& selection) {
        auto next=*g; auto next_rand=*wr; MinuteReport report;
        const bool applied=modern::club_player_detail_national_action(next,selection,next_rand,report);
        if(!report.reason.empty())throw FormatError(report.reason);
        *g=std::move(next); *wr=next_rand; return applied;
    };
    services.pending_offers=[this,g,gr,wr](const ClubPlayerDetailSelection& selection,const PendingOfferHooks& hooks) {
        auto next=*g; auto next_game=*gr; auto next_rand=*wr; MinuteReport report;
        auto result=modern::process_player_card_pending_offers(next,selection,next_game,next_rand,
            pending_hooks_for_dispatch(hooks),report);
        if(report.unsupported||!result.completed)
            throw FormatError(report.reason.empty()?"TeamInfo: nested pending decision incomplete":report.reason);
        *g=std::move(next); *gr=next_game; *wr=next_rand; return result;
    };
    services.runtime_byte=[this,g,bytes](uint32_t address) {
        if(g->memory.has(address))return g->memory.u8(address);
        if(const auto at=bytes->find(address);at!=bytes->end())return at->second;
        // Explicit original initialization supplies the transient input owner.
        // Never serialize an absent BSS byte or infer Native absence as zero.
        return exe_.u8(address);
    };
    services.set_runtime_byte=[g,bytes](uint32_t address,uint8_t value) {
        if(g->memory.has(address))g->memory.set8(address,value);
        else (*bytes)[address]=value;
    };
    return services;
}

ClubModalServices ModernSession::live_club_modal_services(uint64_t& revision) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) throw FormatError("strengths_active");
    auto services=staged_club_modal_services(g_,game_,rand_,club_modal_runtime_);
    services.transactional=false;
    // Existing cup/club caller keeps its separately owned pending transaction.
    services.pending_offers=[this,&revision](const ClubPlayerDetailSelection& selection,const PendingOfferHooks& hooks) {
        std::string error;
        auto result=dispatch_club_player_pending_offers(selection,revision,hooks,revision,error);
        if(!result)throw FormatError(error);
        return *result;
    };
    return services;
}

namespace {
void pairings_runtime_draw(ModernState& g,std::map<uint32_t,uint8_t>& runtime,
    const ui::LeaguePairingsState& page,const LeImage& exe) {
    const auto put=[&](uint32_t at,uint8_t v) {
        if(g.memory.has(at))g.memory.set8(at,v);else runtime[at]=v;
    };
    const uint32_t tip_preference=::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(page.identity.manager);
    const bool computer_tip=page.source.owner.computer_tip_active;
    if(computer_tip)put(tip_preference,uint8_t(g.memory.u8(tip_preference)|0x10));
    // Actual5147b entry/input globals; these are process bytes, not invented FBM blocks.
    put(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField2,uint8_t(page.identity.league));put(::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField,page.source.owner.league_group);
    put(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField,0);put(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2,0);
    const auto date=[&](uint32_t counter) {
        if(counter==0xffff)return; //21258's Wettbewerb-gesperrt branch changes no date scratch.
        const auto d=ui::desk_date(counter,exe);
        int cycle=3;uint32_t n=counter;
        while(n>uint32_t(364+(cycle==4))) {if(cycle==4){cycle=0;--n;}n-=365;++cycle;}
        put(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField,uint8_t(28+(cycle==4)));
        put(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField4,uint8_t(d.day));put(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField5,uint8_t(d.month));put(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField6,uint8_t(d.weekday));
        put(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField2,uint8_t(d.year));put(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField3,uint8_t(d.year>>8));
    };
    const bool selected_filter=page.source.owner.filter_active&&page.source.owner.filter_team>=0;
    if(!selected_filter&&!page.source.owner.postponed_active)
        date(page.source.match_date); //333f3 is absent in selected Mode4 and Mode8.
    for(const auto& row:page.source.rows) {
        if(row.scheduled_date)date(*row.scheduled_date);
        else if(row.postponed_date)date(*row.postponed_date);
        const uint8_t displayed=row.computer_tip_result.value_or(row.raw_result);
        // In local28's referee phase, exact CC rows skip both 68164 calls.
        // Preserve prior scratch when every row takes that branch; a mixed
        // page leaves the last ordinary away-row value, in Source draw order.
        if(page.source.owner.active_phase&&displayed==0xcc)continue;
        //68164's final17295 call on the away team leaves Source CareerSession_FormatError_ExecutableField3.
        const auto strength=team_average(g,row.away.runtime_team,0x1ff);
        put(::bmh::layout::CareerSession_FormatError_ExecutableField3,uint8_t(strength.best_half_sum));put(::bmh::layout::CareerSession_FormatError_ExecutableField4,uint8_t(strength.best_half_sum>>8));
    }
    // Move clears its one-redraw local38 bit after the forecast has
    // been projected. The returned page retains the pressed Original button.
    if(computer_tip)put(tip_preference,uint8_t(g.memory.u8(tip_preference)&0xef));
}

bool other_games_source_manager_owned(const ModernState& g,int team) {
    if(team<0||!g.memory.has(::bmh::layout::ManagerCount))
        throw FormatError("other games: missing Source manager count for club detail redraw");
    const size_t count=g.memory.u8(::bmh::layout::ManagerCount);
    if(count>4||count>g.manager_teams.size())
        throw FormatError("other games: invalid Source manager count for club detail redraw");
    for(size_t manager=0;manager<count;++manager)
        if(g.manager_teams[manager]==team)return true;
    constexpr size_t national_record=4*0xb0c+0x290;
    if(!g.manager_teams.empty()&&g.manager_teams.front()==no_team()&&
       g.manager_records.size()>=national_record+2)
        return int(uint16_t(g.manager_records[national_record]|
               uint16_t(g.manager_records[national_record+1])<<8))==team;
    return false;
}

void other_games_source_redraw_effects(ModernState& g,WatcomRandom& rand,
    std::map<uint32_t,uint8_t>& runtime,const OtherGamesProjection& projection,
    const LeImage& exe) {
    const auto put=[&](uint32_t address,uint8_t value) {
        if(g.memory.has(address))g.memory.set8(address,value);
        else runtime[address]=value;
    };
    constexpr std::array<uint16_t,5> source_flags{0x0f1,0x0f2,0x0f4,0x0f8,0x0ff};
    for(const auto& row:projection.rows) {
        for(const int team:{row.home_team,row.away_team}) {
            // Void6 emits a fixed foreign-strength label without calling QuoteNationalFriendlyMatch
            // for a non-manager international club. All other displayed rows make
            // five ordered Source average calls; overseas manager clubs use W there.
            if(team>=exe.u16(::bmh::layout::NoTeamSquadStart)&&
               !other_games_source_manager_owned(g,team))continue;
            for(const uint16_t flags:source_flags) {
                const auto value=team_average(g,team,flags,&rand);
                // QuoteNationalFriendlyMatch resets/updates CareerSession_FormatError_ExecutableField3 only on the squad-backed branch;
                // TeamRating's foreign-rating branch leaves the previous scratch intact.
                if(team<exe.u16(::bmh::layout::NoTeamSquadStart)||team==no_team()) {
                    put(::bmh::layout::CareerSession_FormatError_ExecutableField3,uint8_t(value.best_half_sum));
                    put(::bmh::layout::CareerSession_FormatError_ExecutableField4,uint8_t(value.best_half_sum>>8));
                }
            }
        }
    }
}

bool same_other_games_live_baseline(const ModernSession::OtherGamesCheckpoint& before,
                                    const ModernSession::OtherGamesCheckpoint& after) {
    if(modern::save_stream(before.state)!=modern::save_stream(after.state)||
       before.game.seed()!=after.game.seed()||before.game.calls()!=after.game.calls()||
       before.rand.seed()!=after.rand.seed()||before.rand.calls()!=after.rand.calls()||
       before.runtime!=after.runtime||
       before.selection.competition!=after.selection.competition||
       before.selection.linked_page!=after.selection.linked_page||
       before.next_scope!=after.next_scope||before.owner.has_value()!=after.owner.has_value())
        return false;
    if(!before.owner)return true;
    const auto& a=*before.owner;const auto& b=*after.owner;
    return a.page.identity==b.page.identity&&a.desk_owner==b.desk_owner&&
        a.round==b.round&&a.participant_offset==b.participant_offset&&
        a.display_mode==b.display_mode&&a.native==b.native&&a.runtime==b.runtime&&
        a.rand_seed==b.rand_seed&&a.game_seed==b.game_seed&&
        a.rand_calls==b.rand_calls&&a.game_calls==b.game_calls&&
        a.page.source_participant_offset==b.page.source_participant_offset&&
        a.page.projection.competition==b.page.projection.competition&&
        a.page.projection.entry_index==b.page.projection.entry_index&&
        a.page.projection.linked_page==b.page.projection.linked_page&&
        a.page.projection.round==b.page.projection.round&&
        a.page.projection.display_mode==b.page.projection.display_mode;
}

bool same_strengths_runtime_state(const ModernState& a,const ModernState& b) {
    const auto same_callback=[](const auto& lhs,const auto& rhs) {
        return bool(lhs)==bool(rhs)&&(!lhs||lhs.target_type()==rhs.target_type());
    };
    return a.indoor==b.indoor&&a.currency_suffix==b.currency_suffix&&a.level==b.level&&
        a.chance_rain==b.chance_rain&&a.chance_temperature==b.chance_temperature&&
        a.last_shooter==b.last_shooter&&a.last_pick_quality==b.last_pick_quality&&
        a.live_scene==b.live_scene&&a.reality==b.reality&&a.live_matches==b.live_matches&&a.list_toggle==b.list_toggle&&
        a.lineups_frozen==b.lineups_frozen&&a.texts==b.texts&&a.date==b.date&&
        a.season_start==b.season_start&&a.development_balance==b.development_balance&&
        a.trend_rotation==b.trend_rotation&&a.season==b.season&&a.transfer_notice_suppressed==b.transfer_notice_suppressed&&
        a.offer_answer==b.offer_answer&&a.pools_open==b.pools_open&&a.autosave_months==b.autosave_months&&
        a.autosave_month==b.autosave_month&&same_callback(a.contract_screen,b.contract_screen)&&
        same_callback(a.loan_return_screen,b.loan_return_screen)&&
        same_callback(a.loan_return_offer_screen,b.loan_return_offer_screen)&&
        same_callback(a.transfer_decision,b.transfer_decision)&&
        same_callback(a.transfer_offer_selector,b.transfer_offer_selector)&&
        same_callback(a.transfer_decision_observer,b.transfer_decision_observer);
}

int8_t source_byte(int value) { return static_cast<int8_t>(uint8_t(value)); }

std::string source_table_range_label(const TextDb& texts,int first,int end) {
    // DE50860 patches offsets 4/7 of its nine-byte TEXTD82 buffer. EN1e83f
    // patches 0/3 of the full " 0-34 only" string instead (the final y is live).
    const auto fixed=texts.raw(82,9);
    auto raw=kEnglishCompetitions ? texts[82] : std::string(fixed.begin(),fixed.end());
    const size_t start=kEnglishCompetitions ? 0 : 4;
    const size_t finish=kEnglishCompetitions ? 3 : 7;
    if(raw.size()<finish+2)throw FormatError("league table: incomplete Source range label");
    auto patch_number=[&](size_t at,int value) {
        const int tens=value/10,ones=value%10;
        raw[at]=uint8_t(char(tens+'0'));raw[at+1]=uint8_t(char(ones+'0'));
    };
    patch_number(start,first+1);patch_number(finish,end);
    return raw;
}

void source_table_normalize_range(ModernSession::LeagueFamilyOwner::TableOwner& table,
                                 const ModernState& game,int league,const TextDb& texts) {
    if(league<0||league>=int(game.league_rounds.size()))
        throw FormatError("league table: missing Source round byte");
    const int8_t raw_end=source_byte(table.end_round);
    const int8_t raw_first=source_byte(table.first_round);
    const uint8_t raw_round=game.league_rounds[size_t(league)];
    const int max_round=bmh::league_matchdays(league);
    if(max_round<int(raw_end)||raw_end<=raw_first||int(raw_round)<int(raw_end))
        table.end_round=int(game.league_rounds[size_t(league)]);
    if(source_byte(table.end_round)<=source_byte(table.first_round))
        table.end_round=table.first_round+1;
    table.retained_range_label_source_bytes=source_table_range_label(
        texts,source_byte(table.first_round),source_byte(table.end_round));
}

bool source_table_identity_is_live(const ModernState& game,const ui::StrengthsIdentity& id) {
    if(id.manager<0||id.manager>3||id.league<0||id.league>=bmh::kLeagueCount||
       !game.memory.has(::bmh::layout::ActiveManager)||game.memory.u8(::bmh::layout::ActiveManager)!=id.manager||
       size_t(id.manager)>=game.manager_teams.size()||game.manager_teams[size_t(id.manager)]!=id.runtime_club||
       game.date!=id.day_counter||id.runtime_club<0||size_t(id.runtime_club)>=game.club_ids.size()||
       int(game.club_ids[size_t(id.runtime_club)])!=id.club_identity||
       id.league_group>1) return false;
    const size_t record=size_t(id.manager)*0xb0c;
    if(record+0x212>game.manager_records.size()||
       id.manager_default_league!=int(game.manager_records[record+0x210])) return false;
    const uint32_t settings=::bmh::layout::ManagerSettings+uint32_t(id.manager)*2;
    const uint32_t family=::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2;
    return game.memory.has(settings+1)&&game.memory.has(family+1)&&
        game.memory.u16(settings)==id.expected_settings_word&&
        game.memory.u16(family)==id.expected_family_word;
}

bool canonical_table_envelope(const ui::LeagueFamilyRequest& request,int control_index=-1,
                              bool allow_selector_league=false) {
    return request.target_child==0&&request.runtime_team==-1&&request.expected_club_identity==-1&&
        request.table_row_index==-1&&(allow_selector_league||request.league_id==-1)&&
        request.control_index==control_index&&
        !request.table_dialog_reply;
}

void source_table_apply_entry_flags(ModernSession::LeagueFamilyOwner::TableOwner& table,uint16_t word) {
    if((word&4)==0)table.aggregation_flags=uint8_t(table.aggregation_flags|1);
    if((word&2)==0)table.aggregation_flags=uint8_t(table.aggregation_flags|2);
}

ui::LeagueTableProjectionContext source_table_context(
    const ModernSession::LeagueFamilyOwner::TableOwner& table,uint16_t display_word,
    std::optional<bmh::LeagueTable> accepted={},bool forecast=false) {
    ui::LeagueTableProjectionContext context;
    context.first_round=table.first_round;context.end_round=table.end_round;
    context.aggregation_flags=table.aggregation_flags;context.normal_mode=table.normal_mode;
    context.display_word_override=display_word;
    context.retained_range_label_source_bytes=table.retained_range_label_source_bytes;
    if(accepted) {context.accepted_engine_league=accepted->league;context.accepted_engine_table=std::move(accepted);}
    context.forecast=forecast;return context;
}
}

std::optional<ui::StrengthsRequest> ModernSession::strengths_open_request(uint64_t revision) const {
    if(!g_.memory.has(::bmh::layout::ActiveManager))return {};
    const int manager=g_.memory.u8(::bmh::layout::ActiveManager);
    if(manager<0||manager>3||size_t(manager)>=g_.manager_teams.size())return {};
    const int club=g_.manager_teams[size_t(manager)];
    const uint32_t setting=::bmh::layout::ManagerSettings+uint32_t(manager)*2,family=::bmh::layout::LeagueTableDisplayModes+uint32_t(manager)*2;
    if(club<0||size_t(club)>=g_.club_ids.size()||!g_.memory.has(setting+1)||!g_.memory.has(family+1))return {};
    ui::StrengthsRequest request;
    request.identity={manager,club,int(g_.club_ids[size_t(club)]),club>356?0:team_league(club),g_.date,
        revision,0,0,g_.memory.u16(setting),g_.memory.u16(family)};
    request.identity.league_group=uint8_t(request.identity.league>=5);
    const size_t record=size_t(manager)*0xb0c;
    if(record+0x212<=g_.manager_records.size())
        request.identity.manager_default_league=int(g_.manager_records[record+0x210]);
    else return {};
    return request;
}

std::optional<ui::StrengthsIdentity> ModernSession::begin_league_family(
    ui::StrengthsIdentity identity,uint8_t child,uint64_t revision,std::string& error) {
    error.clear();
    if(league_family_teaminfo_active_||league_family_owner_||child<1||child>3||revision==UINT64_MAX||identity.revision!=revision||
       (identity.source_param2!=0&&!live_source_identity(identity))||identity.family_id||identity.manager<0||identity.manager>3||
       identity.league<0||identity.league>8||!g_.memory.has(::bmh::layout::ActiveManager)||g_.memory.u8(::bmh::layout::ActiveManager)!=identity.manager||
       size_t(identity.manager)>=g_.manager_teams.size()||g_.manager_teams[size_t(identity.manager)]!=identity.runtime_club||
       identity.day_counter!=g_.date||identity.runtime_club<0||size_t(identity.runtime_club)>=g_.club_ids.size()||
       int(g_.club_ids[size_t(identity.runtime_club)])!=identity.club_identity||next_league_family_id_==UINT64_MAX) {
        error="league family: invalid or nested family entry";return {};
    }
    if(child==1&&(strengths_owner_||league_pairings_draft_)) {
        error="league family: Table entry has another child owner";return {};
    }
    if(child==2&&(!league_pairings_draft_||strengths_owner_||
       league_pairings_draft_->owner.manager!=identity.manager||
       league_pairings_draft_->owner.runtime_club!=identity.runtime_club||
       league_pairings_draft_->owner.club_identity!=identity.club_identity)) {
        error="league family: Pairings entry has no matching child owner";return {};
    }
    if(child==3&&(!strengths_owner_||strengths_owner_->page.identity.manager!=identity.manager||
       strengths_owner_->page.identity.runtime_club!=identity.runtime_club||
       strengths_owner_->page.identity.revision!=revision)) {
        error="league family: Strengths entry has no matching child owner";return {};
    }
    identity.family_id=next_league_family_id_++;
    if(child==3) {
        identity=strengths_owner_->page.identity;
        identity.family_id=next_league_family_id_-1;
        strengths_owner_->page.identity=identity;
        strengths_owner_->native=modern::save_stream(g_);
    }
    LeagueFamilyOwner owner;owner.identity=identity;owner.child=child;owner.native=modern::save_stream(g_);
    owner.rand_seed=rand_.seed();owner.game_seed=game_.seed();owner.rand_calls=rand_.calls();
    owner.game_calls=game_.calls();owner.runtime=club_modal_runtime_;
    if(child==1) {
        LeagueFamilyOwner::TableOwner table;table.end_round=int(g_.league_rounds.at(size_t(identity.league)));
        uint16_t word=uint16_t(g_.memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(identity.manager)*2)&0xff7f);
        g_.memory.set16(::bmh::layout::LeagueTableDisplayModes+uint32_t(identity.manager)*2,word);
        identity.expected_family_word=word;owner.identity=identity;
        table.display_word_override=word;
        source_table_apply_entry_flags(table,word);
        table.normal_mode=source_runtime_byte(g_,club_modal_runtime_,exe_,::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField)&1;
        table.runtime_font_id=exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + 1);
        source_table_normalize_range(table,g_,identity.league,texts_);
        table.page=ui::league_table_from_game(g_,identity.manager,identity.league,
            source_table_context(table,table.display_word_override));
        owner.table=std::move(table);
    }
    owner.identity=identity;owner.native=modern::save_stream(g_);owner.runtime=club_modal_runtime_;
    owner.rand_seed=rand_.seed();owner.game_seed=game_.seed();owner.rand_calls=rand_.calls();owner.game_calls=game_.calls();
    league_family_owner_=std::move(owner);
    return identity;
}

std::optional<ui::StrengthsIdentity> ModernSession::begin_friendly_opponent_table(
        uint64_t revision,std::string& error) {
    error.clear();
    const auto request=strengths_open_request(revision);
    if(!request||request->identity.runtime_club>=modern::first_foreign_cup()||request->identity.source_param2||
       league_family_owner_||strengths_owner_||league_pairings_draft_) {
        error="friendly table: unavailable club entry";return {};
    }
    const auto checkpoint=league_family_checkpoint();
    try {
        auto identity=request->identity;
        // Desktop case6 initializes these before its direct FriendlyMatchScreen(2,0).
        const uint32_t word_at=::bmh::layout::LeagueTableDisplayModes+uint32_t(identity.manager)*2;
        const auto mode=exe_.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField+uint32_t(identity.league));
        uint16_t word=uint16_t((g_.memory.u16(word_at)&0xfc0f)|0x0170);
        if(mode)word|=0x80;
        g_.memory.set16(word_at,word);
        club_modal_runtime_[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField2]=uint8_t(identity.league);
        club_modal_runtime_[::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField]=0;identity.league_group=0;
        club_modal_runtime_[::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField]=mode;club_modal_runtime_[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField]=0;
        club_modal_runtime_[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2]=0;
        identity.expected_family_word=word;
        const auto bound=begin_league_family(identity,1,revision,error);
        if(!bound)throw FormatError(error.empty()?"friendly table: owner entry rejected":error);
        league_family_owner_->friendly_opponent=true;
        return bound;
    } catch(const std::exception& e) {
        restore_league_family_checkpoint(checkpoint);error=e.what();return {};
    }
}

std::optional<int> ModernSession::resolve_friendly_table_opponent(
        const ui::LeagueFamilyRequest& request,uint64_t revision,std::string& error) const {
    error.clear();
    if(!league_family_owner_||!league_family_owner_->friendly_opponent||
       league_family_owner_->identity!=request.identity||request.identity.revision!=revision||
       request.kind!=ui::LeagueFamilyRequestKind::select_opponent||request.from_child!=1||
       request.target_child||request.control_index!=-1||request.league_id!=-1||request.table_dialog_reply) {
        error="friendly table: stale opponent selection";return {};
    }
    const auto dispatch=current_league_table_dispatch(error);
    if(!dispatch||dispatch->phase!=TableDispatchPhase::ready)return {};
    const auto& table=*league_family_owner_->table;
    const int row=request.table_row_index;
    if(row<0||size_t(row)>=dispatch->page.rows.size()||size_t(row)>=table.page.rows.size()) {
        error="friendly table: opponent row outside the owned page";return {};
    }
    const auto& visible=dispatch->page.rows[size_t(row)];
    const auto& normal=table.page.rows[size_t(row)];
    if(visible.runtime_team_id!=request.runtime_team||visible.club_identity_key!=request.expected_club_identity||
       visible.runtime_team_id<0||size_t(visible.runtime_team_id)>=g_.club_ids.size()||
       visible.club_identity_key!=int(g_.club_ids[size_t(visible.runtime_team_id)])||
       normal.rank!=row+1||normal.runtime_team_id<0||normal.runtime_team_id>=modern::first_foreign_cup()||
       size_t(normal.runtime_team_id)>=g_.club_ids.size()||
       normal.club_identity_key!=int(g_.club_ids[size_t(normal.runtime_team_id)])) {
        error="friendly table: opponent row identity changed";return {};
    }
    // After a forecast Present FriendlyMatchScreen resolves TableRuntimeField6's restored normal ranks.
    return normal.runtime_team_id;
}

std::optional<ui::StrengthsIdentity> ModernSession::acknowledge_friendly_own_club(
        const ui::LeagueFamilyRequest& request,uint64_t revision,std::string& error) {
    const auto team=resolve_friendly_table_opponent(request,revision,error);
    if(!team||*team!=request.identity.runtime_club||request.identity.family_sequence==UINT64_MAX) {
        if(error.empty())error="friendly table: invalid own-club return";
        return {};
    }
    auto& owner=*league_family_owner_;auto next=owner.identity;
    uint16_t word=uint16_t((next.expected_family_word&0x0f7f)|(owner.table->normal_mode?0x80:0));
    auto state=g_;state.memory.set16(::bmh::layout::LeagueTableDisplayModes+uint32_t(next.manager)*2,word);
    auto native=modern::save_stream(state);
    ++next.family_sequence;next.expected_family_word=word;
    g_=std::move(state);owner.native=std::move(native);owner.identity=next;
    // Keep the visible forecast/body and normal ranks: this is the inner loop.
    return next;
}

std::optional<ui::PreparationState> ModernSession::finish_friendly_opponent_table(
        const ui::LeagueFamilyRequest& request,uint64_t& revision,std::string& error) {
    error.clear();
    if(!league_family_owner_||!league_family_owner_->friendly_opponent||
       request.identity!=league_family_owner_->identity||request.identity.revision!=revision||
       revision==UINT64_MAX||request.from_child!=1||request.target_child) {
        error="friendly table: stale return";return {};
    }
    if(request.kind==ui::LeagueFamilyRequestKind::select_opponent) {
        const auto team=resolve_friendly_table_opponent(request,revision,error);
        if(!team||*team==request.identity.runtime_club) {
            if(error.empty())error="friendly table: own club keeps the picker open";
            return {};
        }
    } else if(request.kind!=ui::LeagueFamilyRequestKind::close||!canonical_table_envelope(request,-1)) {
        error="friendly table: invalid return envelope";return {};
    }
    const auto dispatch=current_league_table_dispatch(error);
    if(!dispatch||dispatch->phase!=TableDispatchPhase::ready)return {};
    const auto checkpoint=league_family_checkpoint();
    try {
        const uint32_t word_at=::bmh::layout::LeagueTableDisplayModes+uint32_t(request.identity.manager)*2;
        uint16_t word=uint16_t(g_.memory.u16(word_at)&0x0f7f);
        if(league_family_owner_->table->normal_mode)word|=0x80;
        g_.memory.set16(word_at,word);
        auto preparation=preparation_projection(revision+1);
        ui::validate_preparation_context(preparation);
        ui::DesktopScreen validate(data_directory_,preparation.desktop);
        // Direct table return: no LeagueScreenShown family teardown / stir_random.
        league_family_owner_.reset();++revision;
        return preparation;
    } catch(const std::exception& e) {
        restore_league_family_checkpoint(checkpoint);error=e.what();return {};
    }
}

bool ModernSession::advance_league_family(const ui::LeagueFamilyRequest& request,
    ui::StrengthsIdentity next,uint8_t target,uint64_t revision,std::string& error) {
    error.clear();
    if(league_family_teaminfo_active_||!league_family_owner_||league_family_owner_->friendly_opponent||league_family_owner_->child!=request.from_child||
       league_family_owner_->identity!=request.identity||request.identity.family_id==0||
       request.kind!=ui::LeagueFamilyRequestKind::navigate||target<1||target>3||target!=request.target_child||
       revision<request.identity.revision||revision-request.identity.revision>1u||
       next.revision!=revision||next.family_id!=request.identity.family_id||
       next.manager!=request.identity.manager||next.runtime_club!=request.identity.runtime_club||
       next.club_identity!=request.identity.club_identity||next.day_counter!=request.identity.day_counter||
       next.source_param2!=request.identity.source_param2||next.manager_default_league!=request.identity.manager_default_league||
       request.identity.family_sequence==UINT64_MAX||next.family_sequence!=request.identity.family_sequence+1||
       next.league<0||next.league>8||
       (request.from_child==1&&league_family_owner_->table&&
        league_family_owner_->table->phase!=TableDispatchPhase::ready)) {
        error="league family: stale or mismatched child transition";return false;
    }
    auto owner=*league_family_owner_;owner.identity=next;owner.child=target;owner.native=modern::save_stream(g_);
    owner.rand_seed=rand_.seed();owner.game_seed=game_.seed();owner.rand_calls=rand_.calls();
    owner.game_calls=game_.calls();owner.runtime=club_modal_runtime_;
    owner.table.reset();
    if(target==1) {
        LeagueFamilyOwner::TableOwner table;table.end_round=int(g_.league_rounds.at(size_t(next.league)));
        uint16_t word=uint16_t(g_.memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(next.manager)*2)&0xff7f);
        g_.memory.set16(::bmh::layout::LeagueTableDisplayModes+uint32_t(next.manager)*2,word);
        next.expected_family_word=word;owner.identity=next;
        table.display_word_override=word;
        source_table_apply_entry_flags(table,word);
        table.normal_mode=source_runtime_byte(g_,club_modal_runtime_,exe_,::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField)&1;
        table.runtime_font_id=exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + 1);
        source_table_normalize_range(table,g_,next.league,texts_);
        table.page=ui::league_table_from_game(g_,next.manager,next.league,
            source_table_context(table,table.display_word_override));
        owner.table=std::move(table);
    }
    owner.identity=next;owner.native=modern::save_stream(g_);owner.runtime=club_modal_runtime_;
    owner.rand_seed=rand_.seed();owner.game_seed=game_.seed();owner.rand_calls=rand_.calls();owner.game_calls=game_.calls();
    league_family_owner_=std::move(owner);return true;
}

bool ModernSession::live_source_identity(const ui::StrengthsIdentity& id) const {
    return live_overview_owner_ &&
        live_overview_owner_->kind != LiveOverviewKind::cup_other_games &&
        live_overview_owner_->request.manager_slot == id.manager &&
        live_overview_owner_->request.runtime_team == id.runtime_club &&
        live_overview_owner_->request.minute >= 0 && live_overview_owner_->request.minute <= 255 &&
        id.source_param2 == uint8_t(live_overview_owner_->request.minute) &&
        id.day_counter == uint32_t(g_.date) &&
        (live_overview_owner_->request.group_index == 0 ||
         (live_overview_owner_->phase_result && phase_result_request_ &&
          live_overview_owner_->request.group_index == int(phase_result_request_->group_index)));
}

std::optional<ui::PreparationState> ModernSession::close_league_family(
    const ui::LeagueFamilyRequest& request,uint64_t& revision,std::string& error) {
    error.clear();const auto& id=request.identity;
    if(league_family_teaminfo_active_||request.kind!=ui::LeagueFamilyRequestKind::close||request.target_child!=0||
       request.from_child<1||request.from_child>3||revision==UINT64_MAX||id.revision!=revision||
       id.manager<0||id.manager>3||!g_.memory.has(::bmh::layout::ActiveManager)||g_.memory.u8(::bmh::layout::ActiveManager)!=id.manager||
       size_t(id.manager)>=g_.manager_teams.size()||id.runtime_club!=g_.manager_teams[size_t(id.manager)]||
       id.day_counter!=g_.date||id.runtime_club<0||size_t(id.runtime_club)>=g_.club_ids.size()||
       id.club_identity!=int(g_.club_ids[size_t(id.runtime_club)])||
       (id.source_param2!=0&&!live_source_identity(id))||
       !league_family_owner_||league_family_owner_->friendly_opponent||league_family_owner_->child!=request.from_child||
       league_family_owner_->identity!=id||
       strengths_dispatch_active_||league_pairings_dispatch_active_||
       (request.from_child==1&&(strengths_owner_||league_pairings_draft_))||
       (request.from_child==2&&(!league_pairings_draft_||strengths_owner_||
          modern::save_stream(g_)!=league_pairings_draft_->original_stream||
          rand_.seed()!=league_pairings_draft_->original_rand_seed||game_.seed()!=league_pairings_draft_->original_game_seed||
          rand_.calls()!=league_pairings_draft_->original_rand_calls||game_.calls()!=league_pairings_draft_->original_game_calls||
          club_modal_runtime_!=league_pairings_draft_->original_runtime))||
       (request.from_child==3&&(!strengths_owner_||league_pairings_draft_||
          strengths_owner_->page.identity!=id||modern::save_stream(g_)!=strengths_owner_->native||
          rand_.seed()!=strengths_owner_->rand_seed||game_.seed()!=strengths_owner_->game_seed||
          rand_.calls()!=strengths_owner_->rand_calls||game_.calls()!=strengths_owner_->game_calls||
          club_modal_runtime_!=strengths_owner_->runtime))||
       (request.from_child==1&&(modern::save_stream(g_)!=league_family_owner_->native||
          rand_.seed()!=league_family_owner_->rand_seed||game_.seed()!=league_family_owner_->game_seed||
          rand_.calls()!=league_family_owner_->rand_calls||game_.calls()!=league_family_owner_->game_calls||
          club_modal_runtime_!=league_family_owner_->runtime||!league_family_owner_->table||
          league_family_owner_->table->phase!=TableDispatchPhase::ready))) {
        error="league family: stale or unavailable final close";return {};
    }
    try {
        ModernState next_state=g_;GameRandom next_game=game_;WatcomRandom next_rand=rand_;
        auto next_runtime=club_modal_runtime_;
        auto next_pairings=league_pairings_draft_;
        if(request.from_child==2) {
            next_state=std::move(next_pairings->state);next_game=next_pairings->game;
            next_rand=next_pairings->rand;next_runtime=std::move(next_pairings->runtime);
            const uint32_t pairings_word=::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(id.manager);
            if(!next_state.memory.has(pairings_word))throw FormatError("league family: missing Pairings return word");
            next_state.memory.set8(pairings_word,uint8_t((next_state.memory.u8(pairings_word)&0xf0)|1));
        }
        // Phase viewers return to the pending engine ACK, which owns this draw.
        // Keyboard/desk family callers still leave through their existing draw.
        if(!live_overview_owner_ || !live_overview_owner_->phase_result)
            modern::stir_random(next_game,next_rand);
        auto preparation=preparation_projection(next_state,revision+1);
        ui::validate_preparation_context(preparation);
        ui::DesktopScreen validate(data_directory_,preparation.desktop);
        g_=std::move(next_state);game_=next_game;rand_=next_rand;
        club_modal_runtime_=std::move(next_runtime);
        if(request.from_child==2)league_pairings_draft_.reset();
        if(request.from_child==3)strengths_owner_.reset();
        league_family_owner_.reset();
        ++revision;return preparation;
    } catch(const std::exception& e) {error=e.what();return {};}
}

bool ModernSession::suspend_strengths_for_family(const ui::LeagueFamilyRequest& request,
    uint64_t revision,std::string& error) {
    error.clear();const auto& id=request.identity;
    if(league_family_teaminfo_active_||request.kind!=ui::LeagueFamilyRequestKind::navigate||request.from_child!=3||
       (request.target_child!=1&&request.target_child!=2)||!strengths_owner_||strengths_dispatch_active_||
       id.revision!=revision||strengths_owner_->page.identity!=id||modern::save_stream(g_)!=strengths_owner_->native||
       rand_.seed()!=strengths_owner_->rand_seed||rand_.calls()!=strengths_owner_->rand_calls||
       game_.seed()!=strengths_owner_->game_seed||game_.calls()!=strengths_owner_->game_calls||
       !league_family_owner_||league_family_owner_->child!=3||league_family_owner_->identity!=id) {
        error="league family: stale Strengths child transition";return false;
    }
    // A nonzero Reset2 selector returns to LeagueScreenShown without a child close.
    strengths_owner_.reset();return true;
}

bool ModernSession::suspend_pairings_for_family(const ui::LeagueFamilyRequest& request,
    uint64_t revision,std::string& error) {
    error.clear();const auto& id=request.identity;
    if(league_family_teaminfo_active_||request.kind!=ui::LeagueFamilyRequestKind::navigate||request.from_child!=2||
       (request.target_child!=1&&request.target_child!=3)||!league_pairings_draft_||
       league_pairings_dispatch_active_||strengths_owner_||id.revision!=revision||
       id.manager!=league_pairings_draft_->owner.manager||
       id.runtime_club!=league_pairings_draft_->owner.runtime_club||
       id.club_identity!=league_pairings_draft_->owner.club_identity||
       (id.source_param2!=0&&!live_source_identity(id))||
       modern::save_stream(g_)!=league_pairings_draft_->original_stream||
       rand_.seed()!=league_pairings_draft_->original_rand_seed||game_.seed()!=league_pairings_draft_->original_game_seed||
       rand_.calls()!=league_pairings_draft_->original_rand_calls||game_.calls()!=league_pairings_draft_->original_game_calls||
       club_modal_runtime_!=league_pairings_draft_->original_runtime||!league_family_owner_||
       league_family_owner_->child!=2||league_family_owner_->identity!=id) {
        error="league family: stale Pairings child transition";return false;
    }
    // A nonzero Move selector returns to LeagueScreenShown. Commit only its staged
    // page/redraw changes; never invoke edit_league_pairings_source(close).
    ModernState next_state=std::move(league_pairings_draft_->state);
    const uint32_t pairings_word=::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(id.manager);
    if(!next_state.memory.has(pairings_word)) {error="league family: missing Pairings return word";return false;}
    next_state.memory.set8(pairings_word,uint8_t((next_state.memory.u8(pairings_word)&0xf0)|1));
    g_=std::move(next_state);game_=league_pairings_draft_->game;
    rand_=league_pairings_draft_->rand;club_modal_runtime_=std::move(league_pairings_draft_->runtime);
    league_pairings_draft_.reset();return true;
}

bool ModernSession::prepare_league_family_child(const ui::StrengthsIdentity& id,uint8_t child,
    uint64_t revision,std::string& error) {
    error.clear();
    if(league_family_teaminfo_active_||(league_family_owner_&&league_family_owner_->friendly_opponent)||child<1||child>3||revision==UINT64_MAX||id.revision!=revision||
       (id.source_param2!=0&&!live_source_identity(id))||
       id.manager<0||id.manager>3||id.league<0||id.league>8||!g_.memory.has(::bmh::layout::ActiveManager)||
       g_.memory.u8(::bmh::layout::ActiveManager)!=id.manager||size_t(id.manager)>=g_.manager_teams.size()||
       g_.manager_teams[size_t(id.manager)]!=id.runtime_club||g_.date!=id.day_counter||
       id.runtime_club<0||size_t(id.runtime_club)>=g_.club_ids.size()||
       int(g_.club_ids[size_t(id.runtime_club)])!=id.club_identity||strengths_dispatch_active_||
       league_pairings_dispatch_active_||strengths_owner_||league_pairings_draft_) {
        error="league family: stale child entry";return false;
    }
    if((id.family_id==0&&league_family_owner_)||
       (id.family_id!=0&&(!league_family_owner_||league_family_owner_->identity.family_id!=id.family_id||
          league_family_owner_->identity.manager!=id.manager||league_family_owner_->identity.runtime_club!=id.runtime_club||
          league_family_owner_->identity.club_identity!=id.club_identity||league_family_owner_->identity.day_counter!=id.day_counter||
          league_family_owner_->identity.source_param2!=id.source_param2||
          league_family_owner_->identity.family_sequence==UINT64_MAX||
          league_family_owner_->identity.family_sequence+1!=id.family_sequence))) {
        error="league family: missing owner binding for child entry";return false;
    }
    const size_t record=size_t(id.manager)*0xb0c;
    if(record+0x212>g_.manager_records.size()||id.manager_default_league!=int(g_.manager_records[record+0x210])) {
        error="league family: stale manager default league";return false;
    }
    const uint32_t word=::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2;
    if(!g_.memory.has(word+1)) {error="league family: missing manager mode word";return false;}
    g_.memory.set16(word,ui::strengths_family_word_source(g_.memory.u16(word),exe_.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField+uint32_t(id.league))));
    club_modal_runtime_[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField2]=uint8_t(id.league);
    club_modal_runtime_[::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField]=id.league_group;
    club_modal_runtime_[::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField]=exe_.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField+uint32_t(id.league));
    // Uint8T2's selector is child-specific: Table/Pairings use 0, Strengths 1.
    // This is presentation context only; it must not be inferred from the prior page.
    club_modal_runtime_[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField]=uint8_t(child==3);club_modal_runtime_[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2]=0;
    return true;
}

void ModernSession::set_phase_results(decltype(TurnDriver::phase_results) hook) {
    if (!hook) { turn_driver_.phase_results = {}; return; }
    turn_driver_.phase_results = [this, hook=std::move(hook)](const PhaseResultRequest& request,
            const std::vector<MatchGroup>& groups, const ModernState& state) {
        if (phase_result_request_ || &groups != &turn_driver_.groups || &state != &g_ ||
            request.group_index >= groups.size())
            throw FormatError("phase results: invalid synchronous Source boundary");
        phase_result_request_ = &request;
        struct Reset { const PhaseResultRequest*& request; ~Reset() { request = nullptr; } } reset{phase_result_request_};
        hook(request, groups, state);
        if (live_overview_owner_ && live_overview_owner_->phase_result)
            throw FormatError("phase results: viewer must close before the engine ACK");
    };
}

std::optional<ModernSession::LiveOverviewProjection> ModernSession::open_phase_results(
    uint64_t& revision, const Image& underlay, std::string& error) {
    error.clear();
    if (!phase_result_request_ || !calendar_advance_active_ || live_overview_owner_ ||
        phase_result_request_->group_index >= turn_driver_.groups.size()) {
        error = "phase results: no current Source request or viewer already active"; return {};
    }
    try {
        // Native result arrays and process descriptors share pointers in the
        // Original. The Port owns separate vectors; publish their actual scores
        // at this viewer boundary, without simulation or random draws. This also
        // makes Source search/filter/forecast/TeamInfo read the current results.
        ModernState entered = g_;
        for (const auto& group : turn_driver_.groups) {
            const uint32_t id = le32(group.desc, 0xcc);
            if (id < 0x15) {
                const size_t at = size_t(id)*0x17c + size_t(entered.league_rounds.at(id))*10;
                for (size_t m = 0; m < group.results.size(); ++m)
                    entered.results.at(at+m) = group.results[m];
            } else modern::store_cup_results(entered, group);
        }
        const int manager = entered.memory.u8(::bmh::layout::ActiveManager);
        if (manager < 0 || size_t(manager) >= entered.manager_teams.size())
            throw FormatError("phase results: unavailable active Source manager");
        LiveOverviewRequest request;
        request.key_slot = 1; request.minute = phase_result_request_->phase_end_minute;
        request.group_index = int(phase_result_request_->group_index);
        request.manager_slot = manager; request.runtime_team = entered.manager_teams[size_t(manager)];
        request.revision = revision; request.sequence = next_live_overview_scope_;
        const auto checkpoint = g_;
        g_ = std::move(entered);
        MatchEngine context{g_, turn_driver_.groups, game_, rand_, turn_driver_.referees};
        auto page = open_running_overview(request, context, revision, underlay, error, true);
        if (!page) g_ = checkpoint;
        return page;
    } catch (const std::exception& e) { error = e.what(); return {}; }
}

std::optional<ModernSession::LiveOverviewDispatch> ModernSession::dispatch_phase_results(
    const LiveOverviewAction& action, uint64_t& revision, std::string& error,
    const TransferOfferTeamInfoPresenter& team_info) {
    if (!phase_result_request_ || !live_overview_owner_ || !live_overview_owner_->phase_result ||
        live_overview_owner_->request.group_index != int(phase_result_request_->group_index)) {
        error = "phase results: stale or unavailable Source viewer"; return {};
    }
    MatchEngine context{g_, turn_driver_.groups, game_, rand_, turn_driver_.referees};
    return dispatch_live_overview(action, context, revision, error, team_info);
}

std::optional<ModernSession::LiveOverviewProjection> ModernSession::open_live_overview(
    const LiveOverviewRequest& request, MatchEngine& engine, uint64_t& revision,
    const Image& underlay, std::string& error) {
    return open_running_overview(request, engine, revision, underlay, error, false);
}

std::optional<ModernSession::LiveOverviewProjection> ModernSession::open_running_overview(
    const LiveOverviewRequest& request, MatchEngine& engine, uint64_t& revision,
    const Image& underlay, std::string& error, bool phase_result) {
    error.clear();
    if (!calendar_advance_active_ || live_overview_owner_ || request.key_slot < 0 || request.key_slot > 1 ||
        request.minute < 0 || request.minute > 255 || request.group_index < 0 ||
        size_t(request.group_index) >= engine.groups.size() ||
        (phase_result ? (!phase_result_request_ || request.group_index != int(phase_result_request_->group_index) ||
                        request.minute != phase_result_request_->phase_end_minute || request.key_slot != 1)
                      : request.group_index != 0) || engine.groups.empty() ||
        engine.groups.size() != turn_driver_.groups.size() || turn_driver_.groups.empty() ||
        request.manager_slot < 0 || request.manager_slot >= int(g_.manager_teams.size()) ||
        request.revision != revision || revision == UINT64_MAX ||
        next_live_overview_scope_ == UINT64_MAX || &engine.state != &g_ || &engine.game != &game_ ||
        &engine.rand != &rand_ || !g_.memory.has(::bmh::layout::ActiveManager) ||
        g_.memory.u8(::bmh::layout::ActiveManager) != request.manager_slot || request.runtime_team < 0 ||
        size_t(request.runtime_team) >= g_.team_flags.size() ||
        g_.manager_teams[size_t(request.manager_slot)] != request.runtime_team ||
        g_.manager_of(request.runtime_team) != request.manager_slot ||
        engine.groups.front().desc != turn_driver_.groups.front().desc ||
        engine.groups.front().fixtures != turn_driver_.groups.front().fixtures ||
        strengths_owner_ || strengths_dispatch_active_ || league_family_owner_ || league_pairings_draft_ ||
        league_pairings_dispatch_active_ || other_games_owner_ || other_games_dispatch_active_ ||
        live_intervention_ || balance_draft_ || balance_dispatch_active_ || top_scorers_owner_ ||
        top_scorers_dispatch_active_ || pending_offer_dispatch_active_ || market_dispatch_active_ ||
        transfer_offer_editor_ || manager_join_ || contract_quote_ ||
        stadium_.investment || stadium_.tip || stadium_.periphery_quote || stadium_.block_expansion) {
        error = "live overview: stale, nested or unauthenticated Source key request";
        return {};
    }
    const MatchGroup& active = engine.groups[size_t(request.group_index)];
    if (active.desc.size() < 0xd0 || active.matches() <= 0 ||
        (!phase_result && active.void_mask() == ((uint32_t(1) << active.matches()) - 1))) {
        error = "live overview: missing active Source match descriptor";
        return {};
    }


    struct LiveBoundaryFamilyScope {
        bool& active;
        bool previous;
        explicit LiveBoundaryFamilyScope(bool& value) : active(value), previous(value) { active = false; }
        ~LiveBoundaryFamilyScope() { active = previous; }
    } family_scope(calendar_advance_active_);
    const uint64_t next_revision = revision + 1;
    const auto family_checkpoint = league_family_checkpoint();
    const auto pairings_checkpoint = league_pairings_checkpoint();
    const auto other_checkpoint = other_games_checkpoint();
    const auto scope_checkpoint = next_live_overview_scope_;
    try {
        LiveOverviewProjection result;
        source_runtime_set_byte(g_,club_modal_runtime_,::bmh::layout::CareerSession_LiveBoundaryFamilyScope_ExecutableField,0);
        result.owner = request;
        result.owner.revision = next_revision;
        result.owner.sequence = next_live_overview_scope_;
        const uint32_t competition = le32(active.desc, 0xcc);
        LiveOverviewOwner live;
        live.phase_result = phase_result;
        live.request = result.owner;
        live.source_competition = int(competition);
        live.original_native = modern::save_stream(g_);
        live.original_rand_seed = rand_.seed(); live.original_game_seed = game_.seed();
        live.original_rand_calls = rand_.calls(); live.original_game_calls = game_.calls();
        for (const auto& group : engine.groups) live.active_groups.emplace_back(group.desc, group.fixtures);
        live.referees = engine.referees;
        live_overview_owner_ = live;

        if (active.desc[0] == 0) {
            if (competition > 8)
                throw FormatError("live overview: active Source descriptor is not a league");
            result.kind = request.key_slot == 0 ? LiveOverviewKind::league_table : LiveOverviewKind::league_pairings;
            auto strengths = strengths_open_request(next_revision);
            if (!strengths) throw FormatError("live overview: active manager has no Source league identity");
            strengths->identity.league = int(competition);
            strengths->identity.league_group = uint8_t(competition >= 5);
            strengths->identity.sequence = next_live_overview_scope_;
            strengths->identity.source_param2 = uint8_t(request.minute);
            if (request.key_slot == 0) {
                auto family = begin_league_family(strengths->identity, 1, next_revision, error);
                if (!family) throw FormatError(error.empty() ? "live overview: Source Table family entry rejected" : error);
                result.family_identity = *family;
                live.family_identity = *family;
                auto table = current_league_table_dispatch(error);
                if (!table) throw FormatError(error.empty() ? "live overview: Source Table projection unavailable" : error);
                result.table = table->page;
            } else {
                LeaguePairingsDraft draft{g_,game_,rand_,club_modal_runtime_,club_modal_runtime_,{}, {},
                    modern::save_stream(g_),rand_.seed(),game_.seed(),rand_.calls(),game_.calls()};
                for (const auto& group : engine.groups) draft.original_active_groups.emplace_back(group.desc,group.fixtures);
                draft.original_referee_profiles = engine.referees;
                draft.owner = modern::open_league_pairings_source(draft.state,request.manager_slot,ui::league_pairings_constants(exe_));
                if (draft.owner.league != int(competition)) {
                    draft.owner.league=int(competition);
                    draft.owner.round=std::min<int>(draft.state.league_rounds.at(size_t(competition)),
                        league_matchdays(int(competition))-1);
                    draft.owner.title_round=draft.state.league_rounds.at(size_t(competition));
                    if(draft.owner.round>=league_matchdays(int(competition)))
                        throw FormatError("live overview: Source Pairings round is outside its league schedule");
                }
                draft.active_packet = validate_normal_pairings_group(draft.state,draft.owner,draft.runtime);
                const uint64_t child_scope = ++league_pairings_scope_;
                auto page = ui::league_pairings_from_game(draft.state,draft.owner,exe_,next_revision,child_scope,next_live_overview_scope_,draft.active_packet);
                draft.identity = page.identity;
                draft.runtime[::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField] = exe_.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField + uint32_t(draft.owner.league));
                pairings_runtime_draw(draft.state,draft.runtime,page,exe_);
                league_pairings_draft_ = std::move(draft);
                auto family = begin_league_family(strengths->identity,2,next_revision,error);
                if (!family) throw FormatError(error.empty() ? "live overview: Source Pairings family entry rejected" : error);
                result.family_identity = *family;
                live.family_identity = *family;
                result.pairings = std::move(page);
            }
        } else {
            if (request.key_slot != 0 && request.key_slot != 1)
                throw FormatError("live overview: invalid cup overview key slot");
            const int entry_index = modern::cup_entry_index(competition);
            // CupGroupOverviewEffects owns this unsaved traversal cursor even in a freshly
            // loaded career. Its absence in an FBM is not an invalid cup entry.
            if (phase_result && !g_.memory.has(::bmh::layout::CareerSession_FormatError_RuntimeField8)) g_.memory.add_block(::bmh::layout::CareerSession_FormatError_RuntimeField8, 1);
            if (entry_index < 0 || entry_index >= kCupEntries || !g_.memory.has(modern::cup_entry_address(entry_index)+0x1e) ||
                !g_.memory.has(::bmh::layout::CareerSession_FormatError_RuntimeField8) || !g_.memory.has(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(request.manager_slot)))
                throw FormatError("live overview: active cup descriptor is outside the Source linked table");
            // CupGroupOverviewEffects receives the cup-entry ID at entry+18. Match descriptor
            // +6 is part of its end-minute pointer, not a competition selector.
            const int selector = int16_t(g_.memory.u16(competition + 0x18));
            const uint8_t old_view = g_.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(request.manager_slot));
            if (!phase_result) {
                g_.memory.set8(::bmh::layout::CareerSession_FormatError_RuntimeField3,uint8_t(7-(uint16_t(g_.season)%4)));
                g_.memory.set8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(request.manager_slot),uint8_t(old_view|0x80));
            }
            // CupGroupOverviewEffects resolves this exact active descriptor through FindCup(selector, ordinal).
            const int competition_id = int16_t(g_.memory.u16(modern::cup_entry_address(entry_index)+0x18));
            int stable_team = request.runtime_team;
            if (stable_team > 0x164) {
                const size_t backup = 4 * 0xb0c + 0x290;
                if (request.manager_slot != 0 || backup + 2 > g_.manager_records.size())
                    throw FormatError("live overview: unsupported national cup owner");
                stable_team = int(uint16_t(g_.manager_records[backup] | uint16_t(g_.manager_records[backup+1])<<8));
            }
            if (stable_team < 0 || stable_team >= modern::league_clubs() || size_t(stable_team) >= g_.club_ids.size())
                throw FormatError("live overview: missing stable cup owner identity");
            ui::OtherGamesIdentity identity;
            identity.manager_slot=request.manager_slot; identity.runtime_team=request.runtime_team;
            identity.club_identity=int(g_.club_ids[size_t(stable_team)]); identity.day_counter=uint32_t(g_.date);
            identity.revision=next_revision; identity.scope=next_live_overview_scope_; identity.sequence=next_live_overview_scope_;
            identity.expected_view_byte=g_.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(request.manager_slot));
            std::optional<ui::OtherGamesState> page;
            int linked_page=-1;
            for(int ordinal=0;ordinal<kCupEntries;++ordinal) {
                try {
                    auto candidate=ui::other_games_state_from_game(g_,identity,texts_,exe_,selector,ordinal,-1,
                        OtherGamesDisplayMode::detail,int(g_.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(request.manager_slot))&0x7f),
                        false,underlay,club_modal_runtime_);
                    if(candidate.projection.entry_index==entry_index&&
                       candidate.projection.descriptor_competition==competition_id) {
                        linked_page=ordinal;page=std::move(candidate);break;
                    }
                } catch(const std::exception&) {}
            }
            if (!page || linked_page<0)
                throw FormatError("live overview: Source cup pointer resolved a different active descriptor");
            g_.memory.set8(::bmh::layout::CareerSession_FormatError_RuntimeField8,uint8_t(linked_page));
            page->source_participant_offset=int(g_.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(request.manager_slot))&0x7f);
            OtherGamesOwner owner;
            owner.page=*page; owner.round=page->projection.round; owner.participant_offset=page->source_participant_offset;
            owner.display_mode=page->projection.display_mode; owner.native=modern::save_stream(g_); owner.runtime=club_modal_runtime_;
            owner.rand_seed=rand_.seed(); owner.game_seed=game_.seed(); owner.rand_calls=rand_.calls(); owner.game_calls=game_.calls();
            owner.desk_owner.kind=ui::DeskRequestKind::open_menu; owner.desk_owner.menu_kind=5;
            owner.desk_owner.manager_slot=request.manager_slot; owner.desk_owner.revision=next_revision;
            other_games_owner_=std::move(owner);
            other_games_selection_={selector,linked_page};
            result.kind=LiveOverviewKind::cup_other_games; result.cup=std::move(*page);
        }
        live.kind=result.kind;
        live.original_native=modern::save_stream(g_); live.original_rand_seed=rand_.seed(); live.original_game_seed=game_.seed();
        live.original_rand_calls=rand_.calls(); live.original_game_calls=game_.calls();
        live_overview_owner_->kind=live.kind;
        live_overview_owner_->family_identity=live.family_identity;
        live_overview_owner_->request.revision=next_revision;
        ++next_live_overview_scope_;
        revision=next_revision;
        return result;
    } catch(const std::exception& e) {
        restore_league_family_checkpoint(family_checkpoint);
        restore_league_pairings_checkpoint(pairings_checkpoint);
        restore_other_games_checkpoint(other_checkpoint);
        live_overview_owner_.reset();
        next_live_overview_scope_=scope_checkpoint;
        error=e.what();
        return {};
    }
}

std::optional<ModernSession::LeagueTableDispatch> ModernSession::current_league_table_dispatch(
    std::string& error) const {
    error.clear();
    if(!league_family_owner_||league_family_owner_->child!=1||!league_family_owner_->table||
       !source_table_identity_is_live(g_,league_family_owner_->identity)||
       strengths_owner_||league_pairings_draft_) {
        error="league table: no active Source family table owner";return {};
    }
    try {
        const auto& owner=*league_family_owner_;const auto& table=*owner.table;
        const uint32_t settings_at=::bmh::layout::ManagerSettings+uint32_t(owner.identity.manager)*2;
        const uint32_t family_at=::bmh::layout::LeagueTableDisplayModes+uint32_t(owner.identity.manager)*2;
        if(modern::save_stream(g_)!=owner.native||rand_.seed()!=owner.rand_seed||rand_.calls()!=owner.rand_calls||
           game_.seed()!=owner.game_seed||game_.calls()!=owner.game_calls||club_modal_runtime_!=owner.runtime||
           !g_.memory.has(settings_at+1)||!g_.memory.has(family_at+1)||
           g_.memory.u16(settings_at)!=owner.identity.expected_settings_word||
           g_.memory.u16(family_at)!=owner.identity.expected_family_word) {
            error="league table: owned Source state changed before projection";return {};
        }
        const uint16_t word=table.display_word_override;
        LeagueTableDispatch result;
        result.identity=owner.identity;result.phase=table.phase;
        if(table.phase==TableDispatchPhase::range_start||table.phase==TableDispatchPhase::range_end) {
            const int league=owner.identity.league,maximum=bmh::league_matchdays(league);
            ui::TableRangeDialogProfile profile;
            profile.title_textd_address=table.phase==TableDispatchPhase::range_start?::bmh::layout::CareerSession_CurrentLeagueTableDispatch_ExecutableField: ::bmh::layout::CareerSession_CurrentLeagueTableDispatch_ExecutableField2;
            profile.minimum=table.phase==TableDispatchPhase::range_start?1:uint32_t(source_byte(table.pending_start_round)+1);
            profile.maximum=uint32_t(maximum);
            profile.default_value=table.phase==TableDispatchPhase::range_start?1:
                std::max<uint32_t>(1,std::min<uint32_t>(uint32_t(g_.league_rounds.at(size_t(league))),profile.maximum));
            const bool gradient=source_runtime_byte(g_,club_modal_runtime_,exe_,::bmh::layout::TextGradientDisabled)==0&&
                table.runtime_font_id==3;
            profile.text_mode=gradient?ui::TableRangeTextMode::source_gradient:ui::TableRangeTextMode::flat;
            profile.x=table.dialog_x;profile.y=table.dialog_y;result.dialog=profile;
        }
        result.page=table.forecast_page?*table.forecast_page:table.page;
        if(result.page.manager_slot!=owner.identity.manager||result.page.league_id!=owner.identity.league||
           result.page.rows.empty())
            throw FormatError("league table: owned active page is missing or mismatched");
        return result;
    } catch(const std::exception& e) {error=e.what();return {};}
}

std::optional<ModernSession::LeagueTableDispatch> ModernSession::dispatch_league_table_control(
    const ui::LeagueFamilyRequest& request,uint64_t& revision,std::string& error) {
    error.clear();const auto& id=request.identity;
    if(league_family_teaminfo_active_||request.kind!=ui::LeagueFamilyRequestKind::control||
       request.from_child!=1||!canonical_table_envelope(request,request.control_index)||
       id.revision!=revision||request.control_index<0||request.control_index>9||
       !source_table_identity_is_live(g_,id)||
       !league_family_owner_||league_family_owner_->child!=1||league_family_owner_->identity!=id||
       !league_family_owner_->table||league_family_owner_->table->phase!=TableDispatchPhase::ready||
       strengths_owner_||league_pairings_draft_||
       (id.source_param2!=0&&!live_source_identity(id))||id.family_sequence==UINT64_MAX||revision==UINT64_MAX||
       modern::save_stream(g_)!=league_family_owner_->native||rand_.seed()!=league_family_owner_->rand_seed||
       rand_.calls()!=league_family_owner_->rand_calls||game_.seed()!=league_family_owner_->game_seed||
       game_.calls()!=league_family_owner_->game_calls||club_modal_runtime_!=league_family_owner_->runtime||
       strengths_owner_||league_pairings_draft_) {
        error="league table: stale or unavailable control request";return {};
    }
    const auto checkpoint=league_family_checkpoint();
    const uint64_t old_revision=revision;
    try {
        auto& family=*league_family_owner_;auto& table=*family.table;
        const uint32_t word_at=::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2;
        if(!g_.memory.has(word_at+1))throw FormatError("league table: manager display word is unavailable");
        uint16_t word=g_.memory.u16(word_at);
        const int league=id.league,matchdays=bmh::league_matchdays(league);
        const auto update_page=[&](std::optional<bmh::LeagueTable> engine={},bool forecast=false) {
            table.display_word_override=word;
            const auto context=source_table_context(table,word,std::move(engine),forecast);
            table.page=ui::league_table_from_game(g_,id.manager,league,context);
        };
        switch(request.control_index) {
        case 0: table.aggregation_flags=7;word=uint16_t((word&0x0ff0)|1);break;
        case 1: table.aggregation_flags=1;word=uint16_t((word&0x0ff0)|2);break;
        case 2: table.aggregation_flags=2;word=uint16_t((word&0x0ff0)|4);break;
        case 3: {
            if((word&8)==0) {
                const int sort_extra=0x0e - int((word>>3)&0x0e);
                std::array<uint8_t,10> gap{};
                for(size_t i=0;i<gap.size();++i)gap[i]=exe_.u8(::bmh::layout::CareerSession_DispatchLeagueTableControl_RuntimeField+uint32_t(i));
                const auto forecast=modern::compute_league_table_forecast(g_,league,0,matchdays,
                    table.aggregation_flags,table.normal_mode,sort_extra,gap,game_);
                table.forecast_game_after=forecast.game_after;
                word=uint16_t((word|8)&0x0f7f);
                table.display_word_override=uint16_t((word&0x0cf7)+0x0108);
                table.forecast_present_pending=true;
                table.phase=TableDispatchPhase::forecast_pending_present;
                const auto context=source_table_context(table,table.display_word_override,forecast.table,true);
                table.forecast_page=ui::league_table_from_game(g_,id.manager,league,context);
            }
            break;
        }
        case 4: case 5: case 6: {
            const uint16_t bit=uint16_t(0x10u<<(request.control_index-4));word^=bit;
            if((word&0x70)==0)word=uint16_t(word|0x70);
            break;
        }
        case 7:
            table.normal_mode^=1;
            if(club_modal_runtime_.count(::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField))club_modal_runtime_[::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField]=uint8_t(table.normal_mode);
            else if(g_.memory.has(::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField))g_.memory.set8(::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField,uint8_t(table.normal_mode));
            break;
        case 8:
            table.first_round=0;table.end_round=matchdays;
            word=uint16_t((word&0x0cff)|0x0100);break;
        case 9: {
            table.previous_start_round=table.first_round;table.previous_end_round=table.end_round;
            table.pending_start_round=table.first_round;
            table.runtime_font_id=exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + 1);
            if(g_.memory.has(::bmh::layout::CareerSession_DispatchLeagueTableControl_RuntimeField2))g_.memory.set8(::bmh::layout::CareerSession_DispatchLeagueTableControl_RuntimeField2,table.runtime_font_id);
            else club_modal_runtime_[::bmh::layout::CareerSession_DispatchLeagueTableControl_RuntimeField2]=table.runtime_font_id;
            table.phase=TableDispatchPhase::range_start;
            break;
        }
        default: throw FormatError("league table: unsupported control");
        }
        if(request.control_index!=3&&request.control_index!=9) {
            table.forecast_page.reset();table.forecast_game_after.reset();table.forecast_present_pending=false;
            word=uint16_t(word&0x0f7f);
            g_.memory.set16(word_at,word);
            update_page();
        }
        if(request.control_index==3&&table.phase!=TableDispatchPhase::forecast_pending_present) {
            word=uint16_t(word&0x0f7f);
            g_.memory.set16(word_at,word);update_page();
        }
        auto next=id;next.revision=revision+1;next.family_sequence=id.family_sequence+1;
        next.expected_settings_word=g_.memory.u16(::bmh::layout::ManagerSettings+uint32_t(id.manager)*2);
        next.expected_family_word=g_.memory.u16(word_at);
        family.identity=next;family.native=modern::save_stream(g_);family.rand_seed=rand_.seed();
        family.game_seed=game_.seed();family.rand_calls=rand_.calls();family.game_calls=game_.calls();
        family.runtime=club_modal_runtime_;revision++;
        auto result=current_league_table_dispatch(error);
        if(!result)throw FormatError(error.empty()?"league table: dispatch projection failed":error);
        return result;
    } catch(const std::exception& e) {
        restore_league_family_checkpoint(checkpoint);revision=old_revision;error=e.what();return {};
    }
}


std::optional<ModernSession::LeagueTableDispatch> ModernSession::dispatch_league_table_range_reply(
    const ui::LeagueFamilyRequest& request,uint64_t& revision,std::string& error) {
    error.clear();const auto& id=request.identity;
    if(league_family_teaminfo_active_||request.kind!=ui::LeagueFamilyRequestKind::table_range_result||
       request.from_child!=1||request.target_child!=0||request.control_index!=9||
       request.runtime_team!=-1||request.expected_club_identity!=-1||request.table_row_index!=-1||
       request.league_id!=-1||id.revision!=revision||id.family_sequence==UINT64_MAX||revision==UINT64_MAX||
       !source_table_identity_is_live(g_,id)||
       !request.table_dialog_reply||!league_family_owner_||league_family_owner_->child!=1||
       league_family_owner_->identity!=id||!league_family_owner_->table||
       (league_family_owner_->table->phase!=TableDispatchPhase::range_start&&
        league_family_owner_->table->phase!=TableDispatchPhase::range_end)||
       modern::save_stream(g_)!=league_family_owner_->native||rand_.seed()!=league_family_owner_->rand_seed||
       rand_.calls()!=league_family_owner_->rand_calls||game_.seed()!=league_family_owner_->game_seed||
       game_.calls()!=league_family_owner_->game_calls||club_modal_runtime_!=league_family_owner_->runtime) {
        error="league table: stale or unavailable range reply";return {};
    }
    const auto checkpoint=league_family_checkpoint();
    const uint64_t old_revision=revision;
    try {
        auto& family=*league_family_owner_;auto& table=*family.table;
        const auto& reply=*request.table_dialog_reply;
        const auto phase=table.phase;
        const auto expected=phase==TableDispatchPhase::range_start?ui::TableDialogStage::start:ui::TableDialogStage::end;
        if((reply.outcome!=ui::TableDialogOutcome::accept&&reply.outcome!=ui::TableDialogOutcome::cancel)||
           reply.stage!=expected||reply.x<0||reply.x>207||reply.y<0||reply.y>104)
            throw FormatError("league table: wrong dialog stage or Source position");
        const int maximum=bmh::league_matchdays(id.league);
        if(reply.outcome==ui::TableDialogOutcome::accept) {
            const int minimum=phase==TableDispatchPhase::range_start?1:int(source_byte(table.pending_start_round))+1;
            if(reply.value<minimum||reply.value>maximum)
                throw FormatError("league table: accepted range value is outside Source bounds");
        }
        table.dialog_x=reply.x;table.dialog_y=reply.y;
        uint16_t word=g_.memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2);
        if(phase==TableDispatchPhase::range_start) {
            if(reply.outcome==ui::TableDialogOutcome::accept) {
                table.pending_start_round=reply.value-1;table.phase=TableDispatchPhase::range_end;
            } else table.phase=TableDispatchPhase::ready;
        } else {
            if(reply.outcome==ui::TableDialogOutcome::accept) {
                table.first_round=table.pending_start_round;table.end_round=reply.value;
            } else {
                table.first_round=table.previous_start_round;table.end_round=table.previous_end_round;
            }
            word=uint16_t((word&0x0cff)|0x0200);
            source_table_apply_entry_flags(table,word);
            source_table_normalize_range(table,g_,id.league,texts_);
            table.phase=TableDispatchPhase::ready;
        }
        if(table.phase==TableDispatchPhase::ready) {
            word=uint16_t(word&0x0f7f);
            g_.memory.set16(::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2,word);
            table.display_word_override=word;table.forecast_page.reset();table.forecast_game_after.reset();
            table.page=ui::league_table_from_game(g_,id.manager,id.league,source_table_context(table,word));
        }
        auto next=id;next.revision=revision+1;next.family_sequence=id.family_sequence+1;
        next.expected_settings_word=g_.memory.u16(::bmh::layout::ManagerSettings+uint32_t(id.manager)*2);
        next.expected_family_word=g_.memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2);
        family.identity=next;family.native=modern::save_stream(g_);family.rand_seed=rand_.seed();
        family.game_seed=game_.seed();family.rand_calls=rand_.calls();family.game_calls=game_.calls();
        family.runtime=club_modal_runtime_;revision++;
        auto result=current_league_table_dispatch(error);
        if(!result)throw FormatError(error.empty()?"league table: range projection failed":error);
        return result;
    } catch(const std::exception& e) {restore_league_family_checkpoint(checkpoint);revision=old_revision;error=e.what();return {};}
}

std::optional<ModernSession::LeagueTableDispatch> ModernSession::acknowledge_league_forecast_present(
    const ui::LeagueFamilyRequest& request,uint64_t& revision,std::string& error) {
    error.clear();const auto& id=request.identity;
    if(league_family_teaminfo_active_||request.kind!=ui::LeagueFamilyRequestKind::table_forecast_presented||
       request.from_child!=1||request.target_child!=0||request.control_index!=3||
       request.runtime_team!=-1||request.expected_club_identity!=-1||request.table_row_index!=-1||
       request.league_id!=-1||request.table_dialog_reply||
       id.revision!=revision||id.family_sequence==UINT64_MAX||revision==UINT64_MAX||
       !source_table_identity_is_live(g_,id)||
       !league_family_owner_||league_family_owner_->child!=1||league_family_owner_->identity!=id||
       !league_family_owner_->table||league_family_owner_->table->phase!=TableDispatchPhase::forecast_pending_present||
       !league_family_owner_->table->forecast_present_pending||!league_family_owner_->table->forecast_game_after||
       modern::save_stream(g_)!=league_family_owner_->native||rand_.seed()!=league_family_owner_->rand_seed||
       rand_.calls()!=league_family_owner_->rand_calls||game_.seed()!=league_family_owner_->game_seed||
       game_.calls()!=league_family_owner_->game_calls||club_modal_runtime_!=league_family_owner_->runtime) {
        error="league table: stale or unavailable forecast-present acknowledgement";return {};
    }
    const auto checkpoint=league_family_checkpoint();
    const uint64_t old_revision=revision;
    try {
        auto& family=*league_family_owner_;auto& table=*family.table;
        game_=*table.forecast_game_after;
        const uint32_t word_at=::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2;
        uint16_t word=uint16_t(table.display_word_override&0x0ff7);
        g_.memory.set16(word_at,word);
        table.display_word_override=word;table.forecast_present_pending=false;
        table.forecast_game_after.reset();table.phase=TableDispatchPhase::ready;
        const int sort_extra=0x0e - int((word>>3)&0x0e);
        const auto normal=bmh::compute_league_table(g_,id.league,0,table.end_round,
            table.aggregation_flags,table.normal_mode+sort_extra);
        table.page=ui::league_table_from_game(g_,id.manager,id.league,
            source_table_context(table,word,normal,false));
        auto next=id;next.revision=revision+1;next.family_sequence=id.family_sequence+1;
        next.expected_settings_word=g_.memory.u16(::bmh::layout::ManagerSettings+uint32_t(id.manager)*2);
        next.expected_family_word=word;
        family.identity=next;family.native=modern::save_stream(g_);family.rand_seed=rand_.seed();
        family.game_seed=game_.seed();family.rand_calls=rand_.calls();family.game_calls=game_.calls();
        family.runtime=club_modal_runtime_;revision++;
        auto result=current_league_table_dispatch(error);
        if(!result)throw FormatError(error.empty()?"league table: forecast ACK projection failed":error);
        return result;
    } catch(const std::exception& e) {restore_league_family_checkpoint(checkpoint);revision=old_revision;error=e.what();return {};}
}

bool ModernSession::select_league_family_page(const ui::LeagueFamilyRequest& request,
    uint64_t revision,std::string& error) {
    error.clear();const auto& id=request.identity;
    const bool map_select=request.kind==ui::LeagueFamilyRequestKind::select_league;
    const bool toggle=request.kind==ui::LeagueFamilyRequestKind::toggle_league_group;
    if(league_family_teaminfo_active_||(!map_select&&!toggle)||request.from_child!=1||
       !canonical_table_envelope(request,-1,true)||id.revision!=revision||request.league_id<0||request.league_id>8||
       !source_table_identity_is_live(g_,id)||strengths_owner_||league_pairings_draft_||
       !league_family_owner_||league_family_owner_->child!=1||league_family_owner_->identity!=id||
       !league_family_owner_->table||league_family_owner_->table->phase!=TableDispatchPhase::ready||
       modern::save_stream(g_)!=league_family_owner_->native||rand_.seed()!=league_family_owner_->rand_seed||
       rand_.calls()!=league_family_owner_->rand_calls||game_.seed()!=league_family_owner_->game_seed||
       game_.calls()!=league_family_owner_->game_calls||club_modal_runtime_!=league_family_owner_->runtime||
       id.family_sequence==UINT64_MAX) {
        error="league family: stale Table league selector";return false;
    }
    const uint8_t old_group=id.league_group;
    const uint8_t next_group=toggle?uint8_t(old_group^1):old_group;
    const size_t record=size_t(id.manager)*0xb0c;
    const auto checkpoint=league_family_checkpoint();
    try {
        if(toggle) {
            if(!g_.memory.has(::bmh::layout::CalendarState_FormatError_RuntimeField2)||g_.memory.u8(::bmh::layout::CalendarState_FormatError_RuntimeField2)!=0||
               request.league_id!=(next_group?int(exe_.u8(::bmh::layout::CareerSession_FormatError_RuntimeField4)):int(g_.manager_records[record+0x210])))
                throw FormatError("league family: group toggle target or Source availability is invalid");
        } else {
            bool listed=false;
            for(int row=0;row<4;++row) {
                const int count=exe_.u8(::bmh::layout::CalendarLeagueCounts+uint32_t(old_group*4+row));
                const int first=old_group?int(exe_.u8(::bmh::layout::CareerSession_FormatError_RuntimeField4))+row:
                    int(exe_.u8(::bmh::layout::CalendarFirstLeague+uint32_t(row)));
                if(first+count>int(bmh::kLeagueCount))
                    throw FormatError("league family: Source map row exceeds the league table");
                for(int cell=0;cell<count;++cell)
                    if(first+cell==request.league_id)listed=true;
            }
            if(!listed)throw FormatError("league family: selected league is not in the bound Source map");
        }
        ModernState next=g_;auto next_runtime=club_modal_runtime_;auto next_owner=*league_family_owner_;
        const uint32_t word_at=::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2;
        if(!next.memory.has(word_at+1))throw FormatError("league family: missing manager mode word");
        const bool league_changed=request.league_id!=id.league;
        next_runtime[::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField]=next_group;
        if(league_changed) {
            next_runtime[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField2]=uint8_t(request.league_id);
            next_runtime[::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField]=exe_.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField+uint32_t(request.league_id));
        }
        uint16_t word=uint16_t(next.memory.u16(word_at)&0x0f7f);
        if(next_runtime[::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField]!=0)word=uint16_t(word|0x0080);
        if(league_changed)word=uint16_t(word&0xff7f); // FriendlyMatchScreen outer redraw only.
        next.memory.set16(word_at,word);
        auto& identity=next_owner.identity;identity.league=request.league_id;
        identity.league_group=next_group;
        identity.expected_family_word=word;identity.family_sequence=id.family_sequence+1;
        auto& table=*next_owner.table;
        if(league_changed) {
            table.normal_mode=next_runtime[::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField]&1;
            table.display_word_override=word;table.forecast_page.reset();table.forecast_game_after.reset();
            table.forecast_present_pending=false;
            table.page=ui::league_table_from_game(next,id.manager,request.league_id,
                source_table_context(table,word));
        }
        // Same-league map/toggle stay inside FriendlyMatchScreen's event loop. Keep
        // the current body, forecast, range locals and display override intact.
        next_owner.native=modern::save_stream(next);next_owner.runtime=next_runtime;
        next_owner.rand_seed=rand_.seed();next_owner.rand_calls=rand_.calls();
        next_owner.game_seed=game_.seed();next_owner.game_calls=game_.calls();
        g_=std::move(next);club_modal_runtime_=std::move(next_runtime);
        league_family_owner_=std::move(next_owner);
        return true;
    } catch(const std::exception& e) {
        restore_league_family_checkpoint(checkpoint);error=e.what();return false;
    }
}

std::optional<ui::StrengthsIdentity> ModernSession::dispatch_family_table_team_info(
    const ui::LeagueFamilyRequest& request,uint64_t& revision,std::string& error,
    const TransferOfferTeamInfoPresenter& presenter,
    const std::optional<ClubPlayerDetailSelection>* poach_selection) {
    error.clear();const auto& id=request.identity;
    if((poach_selection&&!poach_table_selection_)||(league_family_owner_&&league_family_owner_->friendly_opponent)||league_family_teaminfo_active_||!presenter||request.kind!=ui::LeagueFamilyRequestKind::team_info||request.from_child!=1||
       request.target_child!=0||request.control_index!=-1||request.league_id!=-1||request.table_dialog_reply||
       id.revision!=revision||id.family_sequence==UINT64_MAX||!source_table_identity_is_live(g_,id)||
       !league_family_owner_||league_family_owner_->child!=1||league_family_owner_->identity!=id||
       !league_family_owner_->table||league_family_owner_->table->phase!=TableDispatchPhase::ready||
       strengths_owner_||league_pairings_draft_||
       modern::save_stream(g_)!=league_family_owner_->native||rand_.seed()!=league_family_owner_->rand_seed||
       game_.seed()!=league_family_owner_->game_seed||rand_.calls()!=league_family_owner_->rand_calls||
       game_.calls()!=league_family_owner_->game_calls||club_modal_runtime_!=league_family_owner_->runtime||
       revision==UINT64_MAX) {
        error="league family: stale Table TeamInfo request";return {};
    }
    const auto checkpoint=league_family_checkpoint();
    const uint64_t old_revision=revision;
    try {
        const auto& table_owner=*league_family_owner_->table;
        const ui::LeagueTableState& visible=table_owner.forecast_page?*table_owner.forecast_page:table_owner.page;
        if(request.table_row_index<0||size_t(request.table_row_index)>=visible.rows.size())
            throw FormatError("league family: Table TeamInfo row index is outside the visible page");
        const auto& visible_row=visible.rows[size_t(request.table_row_index)];
        if(visible_row.runtime_team_id!=request.runtime_team||
           visible_row.club_identity_key!=request.expected_club_identity||
           request.runtime_team<0||size_t(request.runtime_team)>=g_.club_ids.size()||
           int(g_.club_ids[size_t(request.runtime_team)])!=request.expected_club_identity)
            throw FormatError("league family: Table TeamInfo visible row identity is stale");
        if(size_t(request.table_row_index)>=table_owner.page.rows.size()||
           table_owner.page.rows[size_t(request.table_row_index)].rank!=request.table_row_index+1)
            throw FormatError("league family: normal working rank is unavailable for Table TeamInfo");
        const auto& selected_row=table_owner.page.rows[size_t(request.table_row_index)];
        const int selected_team=selected_row.runtime_team_id;
        if(selected_team<0||size_t(selected_team)>=g_.club_ids.size()||
           selected_row.club_identity_key!=int(g_.club_ids[size_t(selected_team)]))
            throw FormatError("league family: normal working rank has a stale club identity");
        const auto& before=checkpoint;
        ModernState next=g_;GameRandom next_game=game_;WatcomRandom next_rand=rand_;
        auto next_runtime=club_modal_runtime_;
        auto tables=ui::league_tables_from_game(g_,id.manager);
        if(size_t(id.league)>=tables.size()||visible.manager_slot!=id.manager||visible.league_id!=id.league)
            throw FormatError("league family: visible Table backdrop does not match the active owner");
        tables[size_t(id.league)]=visible;
        const Image backdrop=ui::LeagueTableScreen(data_directory_,tables,id.league,
            ui::LeagueTableScreen::FamilyMode{id,uint8_t(poach_selection!=nullptr)}).render();
        if(poach_selection) {
            const uint32_t word_at=::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2;
            uint16_t word=uint16_t(next.memory.u16(word_at)&0x0f7f);
            if(source_runtime_byte(next,next_runtime,exe_,::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField)!=0)word|=0x80;
            next.memory.set16(word_at,word);
        }
        auto services=staged_club_modal_services(next,next_game,next_rand,next_runtime);
        const auto enter=services.enter_club;int entry_calls=0;
        services.enter_club=[enter,&entry_calls,selected=selected_team](int team) {
            if(team!=selected)throw FormatError("league family: TeamInfo entered a different club");
            ++entry_calls;return enter(team);
        };
        league_family_teaminfo_active_=true;
        struct Reset { bool& active;~Reset(){active=false;} } reset{league_family_teaminfo_active_};
        const bool accepted=presenter(services,selected_team,backdrop);
        const auto after=league_family_checkpoint();
        const bool live_unchanged=same_strengths_runtime_state(before.state,after.state)&&
            modern::save_stream(before.state)==modern::save_stream(after.state)&&before.runtime==after.runtime&&
            before.game.seed()==after.game.seed()&&before.game.calls()==after.game.calls()&&
            before.rand.seed()==after.rand.seed()&&before.rand.calls()==after.rand.calls()&&
            before.family==after.family&&before.next_family_id==after.next_family_id&&
            before.next_strengths_scope==after.next_strengths_scope&&
            !before.strengths&&!after.strengths&&!before.pairings&&!after.pairings;
        if(!live_unchanged||!accepted||(poach_selection?entry_calls<1:entry_calls!=1))
            throw FormatError("league family: TeamInfo presenter changed owner or did not enter the selected club");
        const size_t record=size_t(id.manager)*0xb0c;
        if(next.date!=g_.date||next.manager_teams!=g_.manager_teams||
           !next.memory.has(::bmh::layout::ActiveManager)||next.memory.u8(::bmh::layout::ActiveManager)!=id.manager||record+0x292>next.manager_records.size()||
           (int(next.manager_records[record+0x290])|int(next.manager_records[record+0x291])<<8)!=id.runtime_club||
           size_t(id.runtime_club)>=next.club_ids.size()||int(next.club_ids[size_t(id.runtime_club)])!=id.club_identity||
           record+0x212>next.manager_records.size()||
           int(next.manager_records[record+0x210])!=id.manager_default_league)
            throw FormatError("league family: TeamInfo changed active manager or date");
        auto refreshed=id;refreshed.revision=revision+1;refreshed.family_sequence=id.family_sequence+1;
        refreshed.expected_settings_word=next.memory.u16(::bmh::layout::ManagerSettings+uint32_t(id.manager)*2);
        const uint32_t family_word_at=::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2;
        if(poach_selection && *poach_selection) {
            const auto chosen=modern::club_player_detail_context(next,**poach_selection);
            if(chosen.selection.runtime_team!=selected_team || chosen.source_slot<0 ||
               chosen.source_slot>=int(modern::transfer_list_slot()) || chosen.human_loan)
                throw FormatError("poach: card returned a different ordinary squad player");
            refreshed.expected_family_word=next.memory.u16(family_word_at);
            // A successful param1=1 selection leaves50775 here. Its common
            // low80 write survives; there is no next outer Table redraw.
            g_=std::move(next);game_=next_game;rand_=next_rand;club_modal_runtime_=std::move(next_runtime);
            league_family_owner_->identity=refreshed;league_family_owner_->native=modern::save_stream(g_);
            league_family_owner_->rand_seed=rand_.seed();league_family_owner_->game_seed=game_.seed();
            league_family_owner_->rand_calls=rand_.calls();league_family_owner_->game_calls=game_.calls();
            league_family_owner_->runtime=club_modal_runtime_;++revision;return refreshed;
        }
        const uint16_t drawn_word=uint16_t(next.memory.u16(family_word_at)&0xff7f);
        next.memory.set16(family_word_at,drawn_word);
        refreshed.expected_family_word=drawn_word;
        auto next_table_owner=*league_family_owner_->table;
        source_table_apply_entry_flags(next_table_owner,next.memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2));
        source_table_normalize_range(next_table_owner,next,refreshed.league,texts_);
        next_table_owner.forecast_page.reset();next_table_owner.forecast_game_after.reset();
        next_table_owner.forecast_present_pending=false;next_table_owner.phase=TableDispatchPhase::ready;
        next_table_owner.display_word_override=refreshed.expected_family_word;
        auto next_tables=ui::league_tables_from_game(next,id.manager);
        next_tables[size_t(refreshed.league)]=ui::league_table_from_game(next,id.manager,refreshed.league,
            source_table_context(next_table_owner,next_table_owner.display_word_override));
        ui::LeagueTableScreen validate(data_directory_,std::move(next_tables),refreshed.league,
            ui::LeagueTableScreen::FamilyMode{refreshed});
        next_table_owner.page=validate.state();
        auto preparation=preparation_projection(next,revision+1);
        ui::validate_preparation_context(preparation);ui::DesktopScreen desk(data_directory_,preparation.desktop);
        g_=std::move(next);game_=next_game;rand_=next_rand;club_modal_runtime_=std::move(next_runtime);
        league_family_owner_->identity=refreshed;league_family_owner_->native=modern::save_stream(g_);
        league_family_owner_->table=std::move(next_table_owner);
        league_family_owner_->rand_seed=rand_.seed();league_family_owner_->game_seed=game_.seed();
        league_family_owner_->rand_calls=rand_.calls();league_family_owner_->game_calls=game_.calls();
        league_family_owner_->runtime=club_modal_runtime_;++revision;return refreshed;
    } catch(const std::exception& e) {restore_league_family_checkpoint(checkpoint);revision=old_revision;error=e.what();return {};}
}

bool ModernSession::refresh_family_after_child(const ui::StrengthsIdentity& prior,uint8_t child,
    ui::StrengthsIdentity next,uint64_t revision,std::string& error) {
    error.clear();
    if(league_family_teaminfo_active_||child<1||child>3||!league_family_owner_||league_family_owner_->child!=child||
       league_family_owner_->identity!=prior||prior.family_id==0||next.family_id!=prior.family_id||
       next.family_sequence!=prior.family_sequence||next.revision!=revision||revision<prior.revision||
       revision-prior.revision>1||
       next.manager!=prior.manager||next.runtime_club!=prior.runtime_club||next.club_identity!=prior.club_identity||
       next.day_counter!=prior.day_counter||next.source_param2!=prior.source_param2||
       next.manager_default_league!=prior.manager_default_league||next.league<0||next.league>8||
       !g_.memory.has(::bmh::layout::ActiveManager)||g_.memory.u8(::bmh::layout::ActiveManager)!=next.manager||g_.date!=next.day_counter||
       size_t(next.manager)>=g_.manager_teams.size()||g_.manager_teams[size_t(next.manager)]!=next.runtime_club||
       next.runtime_club<0||size_t(next.runtime_club)>=g_.club_ids.size()||
       int(g_.club_ids[size_t(next.runtime_club)])!=next.club_identity) {
        error="league family: child acknowledgement identity is stale";return false;
    }
    next.expected_settings_word=g_.memory.u16(::bmh::layout::ManagerSettings+uint32_t(next.manager)*2);
    next.expected_family_word=g_.memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(next.manager)*2);
    if(child==3&&(!strengths_owner_||strengths_owner_->page.identity!=next)) {
        error="league family: Strengths child acknowledgement lost its owner";return false;
    }
    if(child==2&&(!league_pairings_draft_||league_pairings_draft_->identity.manager!=next.manager||
       league_pairings_draft_->identity.runtime_club!=next.runtime_club||league_pairings_draft_->identity.league!=next.league)) {
        error="league family: Pairings child acknowledgement lost its owner";return false;
    }
    league_family_owner_->identity=next;league_family_owner_->native=modern::save_stream(g_);
    league_family_owner_->rand_seed=rand_.seed();league_family_owner_->game_seed=game_.seed();
    league_family_owner_->rand_calls=rand_.calls();league_family_owner_->game_calls=game_.calls();
    league_family_owner_->runtime=club_modal_runtime_;return true;
}

std::optional<ModernSession::StrengthsDispatch> ModernSession::dispatch_strengths(
    const ui::StrengthsRequest& request,uint64_t& revision,std::string& error,
    const TransferOfferTeamInfoPresenter& team_info,bool family_transition) {
    error.clear();const auto& id=request.identity;const bool opening=request.kind==ui::StrengthsAction::open;
    if(league_family_teaminfo_active_||strengths_dispatch_active_||revision==UINT64_MAX||id.revision!=revision||id.sequence==UINT64_MAX||
       id.manager<0||id.manager>3||!g_.memory.has(::bmh::layout::ActiveManager)||g_.memory.u8(::bmh::layout::ActiveManager)!=id.manager||
       size_t(id.manager)>=g_.manager_teams.size()||id.runtime_club!=g_.manager_teams[size_t(id.manager)]||
       id.runtime_club<0||size_t(id.runtime_club)>=g_.club_ids.size()||
       id.club_identity!=int(g_.club_ids[size_t(id.runtime_club)])||id.day_counter!=g_.date||
       id.league<0||id.league>8||career_complete()||
       (turn_driver_.stage!=TurnDriver::Stage::MatchdayDesks&&turn_driver_.stage!=TurnDriver::Stage::SeasonEndDesk)||
       pending_offer_dispatch_active_||market_dispatch_active_||calendar_advance_active_||league_pairings_draft_||
       league_pairings_dispatch_active_||transfer_offer_editor_||manager_join_||contract_quote_||
       stadium_.investment||stadium_.tip||stadium_.periphery_quote||top_scorers_owner_||top_scorers_dispatch_active_ || stadium_.block_expansion||other_games_owner_||other_games_dispatch_active_) {
        error="strengths: stale or unavailable owner";return {};
    }
    const size_t rec=size_t(id.manager)*0xb0c;
    const uint32_t setting=::bmh::layout::ManagerSettings+uint32_t(id.manager)*2,family=::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2;
    if(rec+0xb0c>g_.manager_records.size()||
       (int(g_.manager_records[rec+0x290])|int(g_.manager_records[rec+0x291])<<8)!=id.runtime_club||
       !g_.memory.has(setting+1)||!g_.memory.has(family+1)||
       id.expected_settings_word!=g_.memory.u16(setting)||id.expected_family_word!=g_.memory.u16(family)) {
        error="strengths: inconsistent manager or saved settings";return {};
    }
    if(opening) {
        const int initial=id.runtime_club>356?0:team_league(id.runtime_club);
        if(strengths_owner_||id.scope||id.sequence||(!family_transition&&id.league!=initial)||
           (family_transition&&((id.source_param2!=0&&!live_source_identity(id))||id.league_group>1||id.manager_default_league<0||id.manager_default_league>8))||
           next_strengths_scope_==UINT64_MAX||
           request.value!=-1||request.control!=ui::StrengthsControl::condition) {
            error="strengths: invalid entry";return {};
        }
    } else if(!strengths_owner_||id!=strengths_owner_->page.identity||
              modern::save_stream(g_)!=strengths_owner_->native||
              rand_.seed()!=strengths_owner_->rand_seed||rand_.calls()!=strengths_owner_->rand_calls||
              game_.seed()!=strengths_owner_->game_seed||game_.calls()!=strengths_owner_->game_calls) {
        error="strengths: stale modal snapshot";return {};
    }
    // The process timer's automatic Source event8 belongs to an unported modal
    // context. Ordinary desk entry initializes this runtime timer to zero.
    const auto byte=[&](uint32_t at) {if(g_.memory.has(at))return g_.memory.u8(at);
        const auto found=club_modal_runtime_.find(at);return found==club_modal_runtime_.end()?uint8_t(0):found->second;};
    const uint32_t start=uint32_t(byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField2))|uint32_t(byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField3))<<8|
        uint32_t(byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField4))<<16|uint32_t(byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField5))<<24;
    if(opening&&start!=0&&byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField)<200) {
        error="strengths: active process-timer context unsupported";return {};
    }
    if(request.kind==ui::StrengthsAction::pairings||request.kind==ui::StrengthsAction::table) {
        error="strengths: navigation target not implemented";return {};
    }
    if(request.kind==ui::StrengthsAction::team_info&&!team_info) {
        error="strengths: TeamInfo presenter unavailable";return {};
    }
    strengths_dispatch_active_=true;
    struct Reset { bool& active; ~Reset(){active=false;} } reset{strengths_dispatch_active_};
    try {
        StrengthsDispatch result;
        if(request.kind==ui::StrengthsAction::team_info) {
            if(request.control!=ui::StrengthsControl::condition||request.value<0)
                throw FormatError("strengths: malformed TeamInfo target");
            // Keep an owned value copy across the external callback; callbacks
            // can re-enter the public API and replace/reset modal ownership.
            const auto current=strengths_owner_->page;
            const auto target=std::find_if(current.projection.rows.begin(),current.projection.rows.end(),
                [&](const auto& row){return row.runtime_team==request.value;});
            if(target==current.projection.rows.end())
                throw FormatError("strengths: TeamInfo target is not a visible team");

            // Reset2 opens ClubInfoFromGame from the selected runtime row and
            // resumes this same Strengths loop after the nested modal returns.
            // Stage the presenter against the live page, then commit only if it
            // did not re-enter or mutate the retained Strengths owner.
            ModernState next=g_;GameRandom next_game=game_;WatcomRandom next_rand=rand_;
            auto next_runtime=club_modal_runtime_;
            const auto live_before=strengths_checkpoint();const auto old_revision=revision;
            const auto owner_identity=current.identity;
            const auto same_strengths_checkpoint=[&](const StrengthsCheckpoint& before,
                                                      const StrengthsCheckpoint& after) {
                const bool owner_matches=before.owner.has_value()==after.owner.has_value()&&
                    (!before.owner||(before.owner->page==after.owner->page&&
                        before.owner->native==after.owner->native&&
                        before.owner->rand_seed==after.owner->rand_seed&&
                        before.owner->game_seed==after.owner->game_seed&&
                        before.owner->rand_calls==after.owner->rand_calls&&
                        before.owner->game_calls==after.owner->game_calls));
                return modern::save_stream(before.state)==modern::save_stream(after.state)&&
                    same_strengths_runtime_state(before.state,after.state)&&
                    before.game.seed()==after.game.seed()&&before.game.calls()==after.game.calls()&&
                    before.rand.seed()==after.rand.seed()&&before.rand.calls()==after.rand.calls()&&
                    before.runtime==after.runtime&&before.next_scope==after.next_scope&&owner_matches&&
                    before.family==after.family&&before.next_family_id==after.next_family_id;
            };
            const Image backdrop=ui::StrengthsScreen(data_directory_,current).render();
            auto services=staged_club_modal_services(next,next_game,next_rand,next_runtime);
            const bool accepted=team_info(services,request.value,backdrop);
            const auto live_after=strengths_checkpoint();
            if(revision!=old_revision||!same_strengths_checkpoint(live_before,live_after)||
               !live_after.owner||live_after.owner->page.identity!=owner_identity)
                throw FormatError("strengths: live owner changed during TeamInfo presenter");
            if(!accepted)throw FormatError("strengths: TeamInfo presenter stopped");
            if(next.date!=g_.date||next.manager_teams!=g_.manager_teams||
               !next.memory.has(::bmh::layout::ActiveManager)||next.memory.u8(::bmh::layout::ActiveManager)!=id.manager||
               next.memory.u16(setting)!=g_.memory.u16(setting)||next.memory.u16(family)!=g_.memory.u16(family)||
               rec+0x292>next.manager_records.size()||
               (int(next.manager_records[rec+0x290])|int(next.manager_records[rec+0x291])<<8)!=id.runtime_club)
                throw FormatError("strengths: TeamInfo changed the retained desk or family owner");
            // Reset2 retains local_68 across a nested ClubInfoFromGame and
            // redraws the same league. The accepted club-entry injury cleanup
            // can change lineup bits, so do not recompute this same-instance cache.
            ui::StrengthsProjectionContext context;
            context.cached_age=current.projection.age_scratch;
            context.retain_validated_owner_age=true;
            auto projection=ui::strengths_from_game(next,id.manager,id.league,context);
            if(projection.rows.empty())throw FormatError("strengths: empty TeamInfo return projection");
            if(current.projection.age_scratch.league!=id.league||
               current.projection.age_scratch.tenths.size()!=projection.rows.size())
                throw FormatError("strengths: retained TeamInfo age scratch does not match its owner");
            next.memory.set16(setting,projection.words.post_draw_settings_word);
            result.page=current;
            result.page.identity.revision=revision+1;
            result.page.identity.sequence=id.sequence+1;
            result.page.identity.expected_settings_word=next.memory.u16(setting);
            result.page.identity.expected_family_word=next.memory.u16(family);
            result.page.projection=std::move(projection);
            ui::StrengthsScreen validate(data_directory_,result.page);
            result.preparation=preparation_projection(next,revision+1);
            next_runtime[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField2]=uint8_t(id.league);
            next_runtime[::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField]=uint8_t(current.league_group);
            next_runtime[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField]=1;next_runtime[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2]=0;
            next_runtime[::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField]=exe_.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField+uint32_t(id.league));
            const auto last=team_average(next,result.page.projection.rows.back().runtime_team,
                result.page.projection.words.effective_average_flags);
            next_runtime[::bmh::layout::CareerSession_FormatError_ExecutableField3]=uint8_t(last.best_half_sum);
            next_runtime[::bmh::layout::CareerSession_FormatError_ExecutableField4]=uint8_t(last.best_half_sum>>8);
            StrengthsOwner owner{result.page,modern::save_stream(next),next_rand.seed(),next_game.seed(),
                next_rand.calls(),next_game.calls(),next_runtime};
            g_=std::move(next);game_=next_game;rand_=next_rand;
            club_modal_runtime_=std::move(next_runtime);strengths_owner_=std::move(owner);
            ++revision;return result;
        }
        if(request.kind==ui::StrengthsAction::close) {
            if(request.value!=-1||request.control!=ui::StrengthsControl::condition)
                throw FormatError("strengths: malformed close");
            result.closed=true;result.page=strengths_owner_->page;
            ++result.page.identity.revision;++result.page.identity.sequence;
            result.preparation=preparation_projection(g_,revision+1);
            strengths_owner_.reset();++revision;return result;
        }
        ModernState next=g_;auto runtime=club_modal_runtime_;ui::StrengthsProjectionContext context;
        int league=id.league,group=opening?(family_transition?id.league_group:0):strengths_owner_->page.league_group;
        if(opening)next.memory.set16(family,ui::strengths_family_word_source(next.memory.u16(family),exe_.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField+uint32_t(league))));
        else if(request.kind==ui::StrengthsAction::control) {
            if(request.value!=-1)throw FormatError("strengths: malformed control");
            const auto word=ui::strengths_control_word_source(id.expected_settings_word,request.control);
            if(!word||*word==id.expected_settings_word)throw FormatError("strengths: unavailable or unchanged control");
            next.memory.set16(setting,*word);
            if(request.control==ui::StrengthsControl::lineup)context.recompute_age_as=ui::StrengthsAgeBasis::header_toggle_bit15;
            else context.cached_age=strengths_owner_->page.projection.age_scratch;
        } else if(request.kind==ui::StrengthsAction::select_league) {
            if(request.control!=ui::StrengthsControl::condition||request.value<0||request.value>8||request.value==league)
                throw FormatError("strengths: invalid league selection");
            league=request.value;
        } else if(request.kind==ui::StrengthsAction::toggle_group) {
            if(request.value!=-1||request.control!=ui::StrengthsControl::condition||g_.memory.u8(::bmh::layout::CalendarState_FormatError_RuntimeField2)!=0)
                throw FormatError("strengths: unavailable group selection");
            group^=1;league=group?int(exe_.u8(::bmh::layout::CareerSession_FormatError_RuntimeField4)):int(g_.manager_records[rec+0x210]);
            if(league<0||league>8)throw FormatError("strengths: invalid manager league");
            if(league==id.league)context.cached_age=strengths_owner_->page.projection.age_scratch;
        } else if(!opening)throw FormatError("strengths: unknown action");
        auto projection=ui::strengths_from_game(next,id.manager,league,context);
        next.memory.set16(setting,projection.words.post_draw_settings_word);
        result.page.identity={id.manager,id.runtime_club,id.club_identity,league,id.day_counter,revision+1,
            opening?next_strengths_scope_:id.scope,opening?0:id.sequence+1,
            next.memory.u16(setting),next.memory.u16(family)};
        result.page.identity.source_param2=id.source_param2;
        result.page.identity.league_group=uint8_t(group);
        result.page.identity.manager_default_league=id.manager_default_league;
        result.page.identity.family_id=id.family_id;
        result.page.identity.family_sequence=id.family_sequence;
        result.page.projection=std::move(projection);result.page.league_group=group;
        result.page.decorated=g_.memory.u8(::bmh::layout::PlainBackground)==0;result.page.original_panel=g_.memory.u8(::bmh::layout::SolidPanelBackground)==0;
        result.page.gradient=g_.memory.u8(::bmh::layout::TextGradientDisabled)==0&&exe_.u8(::bmh::layout::TextGradientMode)==2;
        result.page.secondary_leagues=g_.memory.u8(::bmh::layout::CalendarState_FormatError_RuntimeField2)==0;
        result.page.title=message_text(next,::bmh::layout::CareerSession_FormatError_TextPointer5)+ui::utf8_to_original(result.page.projection.league_name);
        const uint8_t raw_round=next.league_rounds.at(size_t(league));const int round=(raw_round&0x7f)+1;
        result.page.subtitle=round>league_matchdays(league)?message_text(next,::bmh::layout::CareerSession_FormatError_TextPointer3):
            message_text(next,::bmh::layout::CareerSession_FormatError_TextPointer2+uint32_t((raw_round&0x80)!=0)*4)+format_number(next,round,1)+
            exe_.cstring(::bmh::layout::CareerSession_FormatError_TextPointer)+message_text(next,::bmh::layout::CareerSession_FormatError_TextPointer4);
        ui::StrengthsScreen validate(data_directory_,result.page);
        result.preparation=preparation_projection(next,revision+1);
        // Runtime drawing scratch belongs to the session process, not the FBM.
        runtime[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField2]=uint8_t(league);runtime[::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField]=uint8_t(group);runtime[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField]=1;runtime[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2]=0;
        runtime[::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField]=exe_.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField+uint32_t(league));
        const auto last=team_average(next,result.page.projection.rows.back().runtime_team,result.page.projection.words.effective_average_flags);
        runtime[::bmh::layout::CareerSession_FormatError_ExecutableField3]=uint8_t(last.best_half_sum);runtime[::bmh::layout::CareerSession_FormatError_ExecutableField4]=uint8_t(last.best_half_sum>>8);
        StrengthsOwner owner{result.page,modern::save_stream(next),rand_.seed(),game_.seed(),rand_.calls(),game_.calls(),runtime};
        g_=std::move(next);club_modal_runtime_=std::move(runtime);strengths_owner_=std::move(owner);
        if(opening)++next_strengths_scope_;
        ++revision;return result;
    } catch(const std::exception& e) {error=e.what();return {};}
}

std::optional<ui::TopScorersState> ModernSession::open_top_scorers(
    const ui::DeskRequest& request, uint64_t revision, const Image& underlay, std::string& error) {
    error.clear();
    if (top_scorers_dispatch_active_ || top_scorers_owner_ || revision == UINT64_MAX ||
        next_top_scorers_scope_ == UINT64_MAX || request.kind != ui::DeskRequestKind::open_menu ||
        request.menu_kind != 20 || request.revision != revision || request.manager_slot < 0 ||
        request.manager_slot > 3 || !g_.memory.has(::bmh::layout::ActiveManager) ||
        g_.memory.u8(::bmh::layout::ActiveManager) != request.manager_slot ||
        size_t(request.manager_slot) >= g_.manager_teams.size() || career_complete() ||
        (turn_driver_.stage != TurnDriver::Stage::MatchdayDesks &&
         turn_driver_.stage != TurnDriver::Stage::SeasonEndDesk) ||
        pending_offer_dispatch_active_ || market_dispatch_active_ || calendar_advance_active_ ||
        league_pairings_draft_ || league_pairings_dispatch_active_ || strengths_owner_ ||
        strengths_dispatch_active_ || balance_draft_ || balance_dispatch_active_ || transfer_offer_editor_ ||
        manager_join_ || contract_quote_ || stadium_.investment || stadium_.tip || stadium_.periphery_quote || stadium_.block_expansion) {
        error = "top scorers: stale or unavailable menu-20 owner";
        return {};
    }
    const auto source_byte = [&](uint32_t address) {
        if (g_.memory.has(address)) return g_.memory.u8(address);
        const auto it = club_modal_runtime_.find(address);
        return it == club_modal_runtime_.end() ? exe_.u8(address) : it->second;
    };
    const uint32_t timer_start=uint32_t(source_byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField2))|uint32_t(source_byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField3))<<8|
        uint32_t(source_byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField4))<<16|uint32_t(source_byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField5))<<24;
    const unsigned timer_duration=source_byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField);
    if (timer_start != 0 && timer_duration < 200) {
        error = "top scorers: active process-timer context unsupported";
        return {};
    }
    try {
        const int manager = request.manager_slot;
        const int team = g_.manager_teams[size_t(manager)];
        const size_t manager_record = size_t(manager) * 0xb0c;
        if (manager_record + 0x292 > g_.manager_records.size() ||
            uint16_t(g_.manager_records[manager_record + 0x290] |
                     uint16_t(g_.manager_records[manager_record + 0x291]) << 8) != uint16_t(team))
            throw FormatError("top scorers: manager record does not own the active team");
        int stable_team = team;
        if (team == no_team()) {
            // The live CupDraw_FormatError_RuntimeField3 owner is mirrored by the typed spare manager record 4.
            const size_t backup = 4 * 0xb0c + 0x290;
            if (manager != 0 || backup + 2 > g_.manager_records.size())
                throw FormatError("top scorers: unsupported national-coach owner");
            stable_team = int(uint16_t(g_.manager_records[backup] |
                                       uint16_t(g_.manager_records[backup + 1]) << 8));
        } else if (team < 0 || team >= modern::league_clubs()) {
            throw FormatError("top scorers: manager team is outside Source league scope");
        }
        if (stable_team < 0 || stable_team >= modern::league_clubs() || size_t(stable_team) >= g_.club_ids.size())
            throw FormatError("top scorers: missing stable club identity");
        const int league = team > 0x164 ? 0 : team_league(team);
        if (league < 0 || league > 8)
            throw FormatError("top scorers: manager has no supported Source league");

        ui::TopScorersIdentity identity;
        identity.manager_slot = manager;
        identity.runtime_team = team;
        identity.club_identity = int(g_.club_ids[size_t(stable_team)]);
        identity.league = league;
        identity.day_counter = uint32_t(g_.date);
        identity.revision = revision + 1;
        identity.scope = next_top_scorers_scope_;
        identity.sequence = 0;
        auto page = ui::top_scorers_state_from_game(g_, identity,
            ui::TopScorersMetric::goals, ui::TopScorersScope::league, underlay);
        page.decorated_background = source_byte(::bmh::layout::PlainBackground) == 0;
        page.gradient_text = source_byte(::bmh::layout::TextGradientDisabled) == 0 && source_byte(::bmh::layout::TextGradientMode) == 2;
        page.cached_backdrop = source_byte(::bmh::layout::CareerSession_ApplyBalanceSourceStyle_ExecutableField) != 0 || source_byte(::bmh::layout::CareerSession_FormatError_ExecutableField5) != 0 ||
                               source_byte(::bmh::layout::CareerSession_FormatError_ExecutableField6) != 0 || source_byte(::bmh::layout::CareerSession_FormatError_ExecutableField7) != 0;
        TopScorersOwner owner{page, modern::save_stream(g_), rand_.seed(), game_.seed(), rand_.calls(), game_.calls()};
        top_scorers_owner_ = std::move(owner);
        ++next_top_scorers_scope_;
        ++revision;
        return page;
    } catch (const std::exception& e) {
        error = e.what();
        return {};
    }
}

std::optional<ModernSession::TopScorersDispatch> ModernSession::dispatch_top_scorers(
    const ui::TopScorersRequest& request, uint64_t& revision, std::string& error) {
    error.clear();
    if (stadium_.block_expansion || top_scorers_dispatch_active_ || !top_scorers_owner_ || revision == UINT64_MAX ||
        request.identity != top_scorers_owner_->page.identity || request.identity.revision != revision ||
        request.identity.sequence == UINT64_MAX || request.identity.expected_settings_word != 0 ||
        request.identity.expected_family_word != 0 || modern::save_stream(g_) != top_scorers_owner_->native ||
        rand_.seed() != top_scorers_owner_->rand_seed || rand_.calls() != top_scorers_owner_->rand_calls ||
        game_.seed() != top_scorers_owner_->game_seed || game_.calls() != top_scorers_owner_->game_calls) {
        error = "top scorers: stale, replayed or modified owner";
        return {};
    }
    top_scorers_dispatch_active_ = true;
    struct Reset { bool& active; ~Reset() { active = false; } } reset{top_scorers_dispatch_active_};
    try {
        TopScorersDispatch result;
        const auto& current = top_scorers_owner_->page;
        const auto& identity = request.identity;
        if (identity.manager_slot < 0 || identity.manager_slot > 3 ||
            size_t(identity.manager_slot) >= g_.manager_teams.size() ||
            g_.manager_teams[size_t(identity.manager_slot)] != identity.runtime_team ||
            !g_.memory.has(::bmh::layout::ActiveManager) || g_.memory.u8(::bmh::layout::ActiveManager) != identity.manager_slot ||
            uint32_t(g_.date) != identity.day_counter || identity.league != current.projection.league)
            throw FormatError("top scorers: desk owner changed");
        const size_t manager_record = size_t(identity.manager_slot) * 0xb0c;
        if (manager_record + 0x292 > g_.manager_records.size() ||
            uint16_t(g_.manager_records[manager_record + 0x290] |
                     uint16_t(g_.manager_records[manager_record + 0x291]) << 8) !=
                uint16_t(identity.runtime_team))
            throw FormatError("top scorers: manager record changed");
        const size_t backup = 4 * 0xb0c + 0x290;
        const int identity_team = identity.runtime_team == no_team() ?
            (backup + 2 <= g_.manager_records.size() ?
                int(uint16_t(g_.manager_records[backup] |
                             uint16_t(g_.manager_records[backup + 1]) << 8)) : -1) : identity.runtime_team;
        if (identity_team < 0 || identity_team >= modern::league_clubs() ||
            size_t(identity_team) >= g_.club_ids.size() ||
            int(g_.club_ids[size_t(identity_team)]) != identity.club_identity)
            throw FormatError("top scorers: stable club identity changed");

        if (request.action == ui::TopScorersAction::open)
            throw FormatError("top scorers: open is not a modal action");
        if (request.action == ui::TopScorersAction::close) {
            if (request.metric != current.metric || request.scope != current.scope)
                throw FormatError("top scorers: malformed close request");
            result.closed = true;
            result.page = current;
        } else {
            ui::TopScorersMetric metric = current.metric;
            ui::TopScorersScope scope = current.scope;
            if (request.action == ui::TopScorersAction::set_metric) {
                const auto next_metric = current.metric == ui::TopScorersMetric::goals ?
                    ui::TopScorersMetric::assists : current.metric == ui::TopScorersMetric::assists ?
                    ui::TopScorersMetric::scorer : ui::TopScorersMetric::goals;
                if (request.scope != current.scope || request.metric != next_metric)
                    throw FormatError("top scorers: metric request is not the Source cycle successor");
                metric = request.metric;
            } else if (request.action == ui::TopScorersAction::toggle_scope) {
                if (request.metric != current.metric || request.scope == current.scope)
                    throw FormatError("top scorers: invalid scope request");
                scope = request.scope;
            } else {
                throw FormatError("top scorers: unknown action");
            }
            result.page = ui::top_scorers_state_from_game(g_, identity, metric, scope, current.underlay);
            result.page.decorated_background = current.decorated_background;
            result.page.gradient_text = current.gradient_text;
            result.page.cached_backdrop = current.cached_backdrop;
        }
        result.page.identity.revision = revision + 1;
        result.page.identity.sequence = identity.sequence + 1;
        result.preparation = preparation_projection(g_, revision + 1);
        if (result.closed) top_scorers_owner_.reset();
        else {
            top_scorers_owner_->page = result.page;
            top_scorers_owner_->native = modern::save_stream(g_);
            top_scorers_owner_->rand_seed = rand_.seed();
            top_scorers_owner_->game_seed = game_.seed();
            top_scorers_owner_->rand_calls = rand_.calls();
            top_scorers_owner_->game_calls = game_.calls();
        }
        ++revision;
        return result;
    } catch (const std::exception& e) {
        error = e.what();
        return {};
    }
}

bool ModernSession::apply_statistics_preference(const ui::DeskRequest& owner, int manager_slot,
                                                uint8_t expected_flags, uint8_t target_flags,
                                                uint64_t& revision, std::string& error) {
    constexpr uint32_t flags_base = ::bmh::layout::Flags;
    if (owner.kind != ui::DeskRequestKind::open_menu || owner.menu_kind != 23 ||
        owner.manager_slot != manager_slot || manager_slot < 0 || manager_slot >= 4 ||
        owner.revision != revision || !g_.memory.has(::bmh::layout::ActiveManager) ||
        g_.memory.u8(::bmh::layout::ActiveManager) != manager_slot || size_t(manager_slot) >= g_.manager_teams.size() ||
        career_complete() || opportunities_active() ||
        (turn_driver_.stage != TurnDriver::Stage::MatchdayDesks &&
         turn_driver_.stage != TurnDriver::Stage::SeasonEndDesk)) {
        error = "statistics: stale menu owner";
        return false;
    }
    const uint32_t address = flags_base + uint32_t(manager_slot) * 3;
    if (!g_.memory.has(address) || g_.memory.u8(address) != expected_flags) {
        error = "statistics: stale Source preference";
        return false;
    }
    const uint8_t delta = uint8_t(expected_flags ^ target_flags);
    const bool one_metric = delta != 0 && (delta & uint8_t(delta - 1)) == 0 && delta < 0x40;
    const bool absolute_pair = delta == 0xc0;
    if ((!one_metric && !absolute_pair) || target_flags != uint8_t(expected_flags ^ delta)) {
        error = "statistics: invalid Source preference operation";
        return false;
    }
    g_.memory.set8(address, target_flags);
    error.clear();
    return true;
}

std::optional<ui::OpportunitiesState> ModernSession::open_opportunities(
    const ui::DeskRequest& request,uint64_t revision,const Image& underlay,std::string& error,
    bool national_source_case,int stable_identity) {
    error.clear();
    if(opportunities_dispatch_active_||opportunities_owner_||revision==UINT64_MAX||next_opportunities_scope_==UINT64_MAX||
       request.kind!=ui::DeskRequestKind::open_menu||request.menu_kind!=23||request.revision!=revision||
       request.manager_slot<0||request.manager_slot>3||!g_.memory.has(::bmh::layout::ActiveManager)||g_.memory.u8(::bmh::layout::ActiveManager)!=request.manager_slot||
       size_t(request.manager_slot)>=g_.manager_teams.size()||career_complete()||
       (turn_driver_.stage!=TurnDriver::Stage::MatchdayDesks&&turn_driver_.stage!=TurnDriver::Stage::SeasonEndDesk)||
       pending_offer_dispatch_active_||market_dispatch_active_||calendar_advance_active_||league_pairings_draft_||
       league_pairings_dispatch_active_||strengths_owner_||strengths_dispatch_active_||top_scorers_owner_||
        top_scorers_dispatch_active_||balance_draft_||balance_dispatch_active_||other_games_owner_||other_games_dispatch_active_||
       transfer_offer_editor_||manager_join_||contract_quote_||stadium_.investment||stadium_.tip||stadium_.periphery_quote||
       stadium_.block_expansion) { error="opportunities: stale or unavailable menu-23 owner"; return {}; }
    try {
        const int manager=request.manager_slot,team=g_.manager_teams[size_t(manager)];
        const int league=national_source_case?0:team_league(team);
        if(national_source_case) {
            if(team!=no_team()||league!=0)throw FormatError("opportunities: invalid national menu-6 owner");
            if(stable_identity<0)throw FormatError("opportunities: missing national Desk identity");
        } else if(team<0||team>=modern::league_clubs()||size_t(team)>=g_.club_ids.size()||league<0||league>=9)
            throw FormatError("opportunities: unsupported team/league");
        if(underlay.width!=320||underlay.height!=240)throw FormatError("opportunities: expected captured 320x240 desktop");
        const uint32_t choice_at=::bmh::layout::CareerSession_FormatError_ExecutableField+uint32_t(manager)*2;
        if(!g_.memory.has(choice_at+1))throw FormatError("opportunities: Source choice word missing");
        uint16_t mode=g_.memory.u16(choice_at);if(mode==0){mode=1;g_.memory.set16(choice_at,mode);}
        if(mode!=1&&mode!=2&&mode!=4&&mode!=8)throw FormatError("opportunities: invalid stored Source mode");
        ui::OpportunitiesState page;page.identity={manager,team,national_source_case?stable_identity:int(g_.club_ids[size_t(team)]),uint32_t(g_.date),revision+1,next_opportunities_scope_,0};
        page.mode=mode;page.league_size=kLeagueSizes[league];page.underlay=underlay;
        page.gradient_text=g_.memory.has(::bmh::layout::TextGradientDisabled)&&g_.memory.u8(::bmh::layout::TextGradientDisabled)==0&&g_.memory.has(::bmh::layout::TextGradientMode)&&g_.memory.u8(::bmh::layout::TextGradientMode)==2;
        page.current_rank=national_source_case?national_opportunities_rank(g_,team,page.ranks):table_prediction(g_,team,mode,page.ranks,nullptr);
        opportunities_owner_=OpportunitiesOwner{page,modern::save_stream(g_),game_,rand_,national_source_case};++next_opportunities_scope_;
        return page;
    } catch(const std::exception& e){error=e.what();return {};}
}

std::optional<ModernSession::OpportunitiesDispatch> ModernSession::dispatch_opportunities(
    const ui::OpportunitiesRequest& request,uint64_t& revision,std::string& error) {
    error.clear();
    if(opportunities_dispatch_active_||!opportunities_owner_||revision==UINT64_MAX||
       request.identity!=opportunities_owner_->page.identity||request.identity.revision!=revision||
       request.identity.sequence==UINT64_MAX||modern::save_stream(g_)!=opportunities_owner_->native||
       game_.seed()!=opportunities_owner_->game.seed()||game_.calls()!=opportunities_owner_->game.calls()||
       rand_.seed()!=opportunities_owner_->rand.seed()||rand_.calls()!=opportunities_owner_->rand.calls()) {
        error="opportunities: stale, replayed or modified owner";return {};
    }
    opportunities_dispatch_active_=true;struct Reset{bool& active;~Reset(){active=false;}} reset{opportunities_dispatch_active_};
    try {
        const auto current=opportunities_owner_->page; // Value snapshot across all validation.
        const bool national_source_case=opportunities_owner_->national_source_case;
        if(current.identity.manager_slot<0||current.identity.manager_slot>3||
           size_t(current.identity.manager_slot)>=g_.manager_teams.size()||
           g_.manager_teams[size_t(current.identity.manager_slot)]!=current.identity.runtime_team||
           !g_.memory.has(::bmh::layout::ActiveManager)||g_.memory.u8(::bmh::layout::ActiveManager)!=current.identity.manager_slot||
           uint32_t(g_.date)!=current.identity.day_counter||
           size_t(current.identity.runtime_team)>=g_.club_ids.size()||
           (!national_source_case&&int(g_.club_ids[size_t(current.identity.runtime_team)])!=current.identity.stable_club_identity)||
           (national_source_case&&(current.identity.runtime_team!=no_team()||
                                  current.identity.stable_club_identity<0)))
            throw FormatError("opportunities: desk owner changed");
        OpportunitiesDispatch result;result.page=current;
        if(request.action==ui::OpportunitiesAction::close){
            if(request.mode!=current.mode)throw FormatError("opportunities: malformed close");
            result.closed=true;
        } else if(request.action==ui::OpportunitiesAction::select_mode){
            if(request.mode!=1&&request.mode!=2&&request.mode!=4&&request.mode!=8)
                throw FormatError("opportunities: unsupported Source mode");
            const uint32_t choice_at=::bmh::layout::CareerSession_FormatError_ExecutableField+uint32_t(current.identity.manager_slot)*2;
            if(!g_.memory.has(choice_at+1))throw FormatError("opportunities: Source choice word missing");
            g_.memory.set16(choice_at,uint16_t(request.mode));
            result.page.mode=request.mode;
            result.page.current_rank=national_source_case?
                national_opportunities_rank(g_,current.identity.runtime_team,result.page.ranks):
                modern::table_prediction(g_,current.identity.runtime_team,request.mode,result.page.ranks,nullptr);
        } else throw FormatError("opportunities: unknown action");
        result.page.identity.revision=revision+1;result.page.identity.sequence=request.identity.sequence+1;
        result.preparation=preparation_projection(g_,revision+1);
        if(result.closed)opportunities_owner_.reset();
        else opportunities_owner_=OpportunitiesOwner{result.page,modern::save_stream(g_),game_,rand_,national_source_case};
        ++revision;return result;
    } catch(const std::exception& e){error=e.what();return {};}
}

std::optional<ui::OtherGamesState> ModernSession::open_other_games(
    const ui::DeskRequest& request, uint64_t& revision, const Image& underlay, std::string& error) {
    error.clear();
    if (other_games_dispatch_active_ || other_games_owner_ || revision == UINT64_MAX ||
        next_other_games_scope_ == UINT64_MAX || request.kind != ui::DeskRequestKind::open_menu ||
        request.menu_kind != 5 || request.revision != revision || request.manager_slot < 0 ||
        request.manager_slot > 3 || !g_.memory.has(::bmh::layout::ActiveManager) ||
        g_.memory.u8(::bmh::layout::ActiveManager) != request.manager_slot ||
        size_t(request.manager_slot) >= g_.manager_teams.size() || career_complete() ||
        (turn_driver_.stage != TurnDriver::Stage::MatchdayDesks &&
         turn_driver_.stage != TurnDriver::Stage::SeasonEndDesk) ||
        pending_offer_dispatch_active_ || market_dispatch_active_ || calendar_advance_active_ ||
        league_pairings_draft_ || league_pairings_dispatch_active_ || strengths_owner_ ||
        strengths_dispatch_active_ || top_scorers_owner_ || top_scorers_dispatch_active_ || other_games_owner_ || other_games_dispatch_active_ ||
        balance_draft_ || balance_dispatch_active_ || transfer_offer_editor_ || manager_join_ ||
        contract_quote_ || stadium_.investment || stadium_.tip || stadium_.periphery_quote || stadium_.block_expansion) {
        error = "other games: stale or unavailable menu-5 owner";
        return {};
    }
    try {
        if (!g_.memory.has(::bmh::layout::ActiveSelectors + 7) || !g_.memory.has(::bmh::layout::ActiveManager) ||
            !g_.memory.has(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField + uint32_t(request.manager_slot)) || !g_.memory.has(::bmh::layout::FriendlyPlanCount))
            throw FormatError("other games: missing Source overview state");

        ModernState next = g_;
        // CupGroupOverviewEffects's exact entry transaction. CareerSession_FormatError_RuntimeField3 aliases selector slot four, so its
        // season write precedes the first-hit selector-12 insertion/removal.
        next.memory.set8(::bmh::layout::CareerSession_FormatError_RuntimeField3, uint8_t(7 - (uint16_t(next.season) % 4)));
        int active = next.memory.u8(::bmh::layout::ActiveManager);
        if (active > 3) {
            active = 0;
            next.memory.set8(::bmh::layout::ActiveManager, 0);
        }
        next.memory.set8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField + uint32_t(active), 0x80);
        if (next.memory.u8(::bmh::layout::FriendlyPlanCount) == 0) {
            modern::end_competition(next.memory, 12);
        } else {
            for (uint32_t i = 0; i < 8; ++i) {
                const uint8_t id = next.memory.u8(::bmh::layout::ActiveSelectors + i);
                if (id == 12) break;
                if (id == 0x14) { next.memory.set8(::bmh::layout::ActiveSelectors + i, 12); break; }
            }
        }

        const int manager = active;
        if (size_t(manager) >= next.manager_teams.size())
            throw FormatError("other games: active manager slot is outside typed manager list");
        const int team = next.manager_teams[size_t(manager)];
        const size_t record = size_t(manager) * 0xb0c;
        if (record + 0x292 > next.manager_records.size() || team < 0 || team >= 0x190 ||
            uint16_t(next.manager_records[record + 0x290] |
                     uint16_t(next.manager_records[record + 0x291]) << 8) != uint16_t(team))
            throw FormatError("other games: manager record does not own the active team");
        int stable_team = team;
        if (team > 0x164) {
            const size_t backup = 4 * 0xb0c + 0x290;
            if (manager != 0 || backup + 2 > next.manager_records.size())
                throw FormatError("other games: unsupported national-coach owner");
            stable_team = int(uint16_t(next.manager_records[backup] |
                                       uint16_t(next.manager_records[backup + 1]) << 8));
        }
        if (stable_team < 0 || stable_team >= modern::league_clubs() || size_t(stable_team) >= next.club_ids.size())
            throw FormatError("other games: missing stable manager club identity");

        const uint8_t source_view = next.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField + uint32_t(manager));
        const OtherGamesDisplayMode mode = (source_view & 0x80)
            ? OtherGamesDisplayMode::detail : OtherGamesDisplayMode::compact;
        const int local_cursor = source_view & 0x7f;
        if (local_cursor % 16 != 0)
            throw FormatError("other games: malformed Source local page cursor");
        ui::OtherGamesIdentity identity;
        identity.manager_slot = manager;
        identity.runtime_team = team;
        identity.club_identity = int(next.club_ids[size_t(stable_team)]);
        identity.day_counter = uint32_t(next.date);
        identity.revision = revision + 1;
        identity.scope = next_other_games_scope_;
        identity.sequence = 0;
        identity.expected_view_byte = source_view;

        auto page = ui::other_games_state_from_game(next,identity,texts_,exe_,
            other_games_selection_.competition,other_games_selection_.linked_page,-1,mode,
            local_cursor,true,underlay,club_modal_runtime_);
        if (page.projection.display_mode != mode) {
            // Source forces local20=detail on redraw if this round cannot use the toggle.
            if (page.projection.source_participant_count <= 16 && mode != OtherGamesDisplayMode::detail) {
                next.memory.set8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField + uint32_t(manager),uint8_t(source_view | 0x80));
                identity.expected_view_byte = uint8_t(source_view | 0x80);
                page.identity = identity;
            } else {
                throw FormatError("other games: malformed Source local page cursor");
            }
        }
        page.source_participant_offset = local_cursor;
        OtherGamesOwner owner;
        owner.page = page;
        owner.desk_owner = request;
        owner.round = page.projection.round;
        owner.participant_offset = local_cursor;
        owner.display_mode = page.projection.display_mode;
        owner.native = modern::save_stream(next);
        owner.runtime = club_modal_runtime_;
        owner.rand_seed = rand_.seed(); owner.game_seed = game_.seed();
        owner.rand_calls = rand_.calls(); owner.game_calls = game_.calls();
        g_ = std::move(next);
        other_games_owner_ = std::move(owner);
        ++next_other_games_scope_;
        ++revision;
        return page;
    } catch (const std::exception& e) {
        error = e.what();
        return {};
    }
}

std::optional<ModernSession::OtherGamesDispatch> ModernSession::dispatch_other_games(
    const ui::OtherGamesRequest& request, uint64_t& revision, std::string& error,
    const TransferOfferTeamInfoPresenter& team_info) {
    error.clear();
    if (stadium_.block_expansion || other_games_dispatch_active_ || !other_games_owner_ || revision == UINT64_MAX ||
        request.identity != other_games_owner_->page.identity || request.identity.revision != revision ||
        request.identity.sequence == UINT64_MAX || modern::save_stream(g_) != other_games_owner_->native ||
        club_modal_runtime_ != other_games_owner_->runtime || rand_.seed() != other_games_owner_->rand_seed ||
        rand_.calls() != other_games_owner_->rand_calls || game_.seed() != other_games_owner_->game_seed ||
        game_.calls() != other_games_owner_->game_calls) {
        error = "other games: stale, replayed or modified owner";
        return {};
    }
    other_games_dispatch_active_ = true;
    struct Reset { bool& active; ~Reset() { active = false; } } reset{other_games_dispatch_active_};
    try {
        // Keep value snapshots across any external presenter call. The presenter
        // may capture this ModernSession and invoke another public action while
        // the modal dispatch is active.
        const auto current = other_games_owner_->page;
        const auto id = request.identity;
        const auto desk = other_games_owner_->desk_owner;
        const int owned_round=other_games_owner_->round;
        const int owned_participant_offset=other_games_owner_->participant_offset;
        const auto owned_display_mode=other_games_owner_->display_mode;
        if (id.manager_slot != desk.manager_slot || id.manager_slot < 0 || id.manager_slot > 3 ||
            size_t(id.manager_slot) >= g_.manager_teams.size() ||
            g_.manager_teams[size_t(id.manager_slot)] != id.runtime_team ||
            g_.memory.u8(::bmh::layout::ActiveManager) != id.manager_slot || uint32_t(g_.date) != id.day_counter ||
            g_.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField + uint32_t(id.manager_slot)) != id.expected_view_byte)
            throw FormatError("other games: desk owner or view byte changed");
        const size_t rec = size_t(id.manager_slot) * 0xb0c;
        if (rec + 0x292 > g_.manager_records.size() ||
            uint16_t(g_.manager_records[rec + 0x290] | uint16_t(g_.manager_records[rec + 0x291]) << 8) !=
                uint16_t(id.runtime_team))
            throw FormatError("other games: manager record changed");
        int stable_team = id.runtime_team;
        if (id.runtime_team > 0x164) {
            const size_t backup = 4 * 0xb0c + 0x290;
            if (id.manager_slot != 0 || backup + 2 > g_.manager_records.size())
                throw FormatError("other games: unsupported national owner");
            stable_team = int(uint16_t(g_.manager_records[backup] | uint16_t(g_.manager_records[backup + 1]) << 8));
        }
        if (stable_team < 0 || stable_team >= modern::league_clubs() || size_t(stable_team) >= g_.club_ids.size() ||
            int(g_.club_ids[size_t(stable_team)]) != id.club_identity)
            throw FormatError("other games: stable club identity changed");
        if (request.action == ui::OtherGamesAction::team_info) {
            if (request.value < 0 || current.projection.unfinished_predecessor ||
                !current.projection.fixtures_available)
                throw FormatError("other games: invalid team-information target");
            bool target_visible = false;
            for (const auto& row:current.projection.rows) {
                target_visible |= row.home_team==request.value||row.away_team==request.value;
            }
            if (!target_visible) throw FormatError("other games: team-information target is not visible");
            if (!team_info) throw FormatError("other games: TeamInfo presenter unavailable");

            ModernState next=g_;
            GameRandom next_game=game_;
            WatcomRandom next_rand=rand_;
            auto next_runtime=club_modal_runtime_;
            // Source CupGroupOverviewEffects passes the accepted Other Games drawing as the
            // immediate parent's backdrop. The presenter's mutations are staged
            // with the same Native, W and process-runtime owners.
            const Image backdrop=ui::OtherGamesScreen(data_directory_,current).render();
            auto services=staged_club_modal_services(next,next_game,next_rand,next_runtime);
            const auto live_baseline=other_games_checkpoint();
            const auto revision_baseline=revision;
            const bool presenter_accepted=team_info(services,request.value,backdrop);

            // Validate the live session before consulting the owner again or
            // committing staged presenter changes. A captured callback that
            // mutated Native, either RNG, runtime, selection, scope or owner is
            // rejected; Controller restores its pre-dispatch checkpoint.
            if(revision!=revision_baseline||
               !same_other_games_live_baseline(live_baseline,other_games_checkpoint()))
                throw FormatError("other games: live session changed during TeamInfo presenter");
            if(!presenter_accepted)
                throw FormatError("other games: TeamInfo presenter stopped");
            if(!other_games_owner_||other_games_owner_->page.identity!=id||
               ui::OtherGamesScreen(data_directory_,other_games_owner_->page).render().pixels!=backdrop.pixels)
                throw FormatError("other games: live page changed during TeamInfo presenter");

            if(next.date!=g_.date||next.manager_teams!=g_.manager_teams||
               next.memory.u8(::bmh::layout::ActiveManager)!=id.manager_slot||
               next.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(id.manager_slot))!=id.expected_view_byte)
                throw FormatError("other games: TeamInfo changed its outer desk or saved page owner");
            const size_t active_record=size_t(id.manager_slot)*0xb0c;
            if(active_record+0x292>next.manager_records.size()||
               uint16_t(next.manager_records[active_record+0x290]|
                   uint16_t(next.manager_records[active_record+0x291])<<8)!=uint16_t(id.runtime_team))
                throw FormatError("other games: TeamInfo changed the active manager record");
            const int retained_team=id.runtime_team>0x164
                ? int(uint16_t(next.manager_records[4*0xb0c+0x290]|
                    uint16_t(next.manager_records[4*0xb0c+0x291])<<8)) : id.runtime_team;
            if(retained_team<0||retained_team>=modern::league_clubs()||size_t(retained_team)>=next.club_ids.size()||
               int(next.club_ids[size_t(retained_team)])!=id.club_identity)
                throw FormatError("other games: TeamInfo changed the stable desk club identity");

            // LAB_000226dc clears the input event, then local_50 skips the
            // entry-only stage/cursor/round setup and redraws the retained view.
            const auto put_runtime=[&](uint32_t address,uint8_t value) {
                if(next.memory.has(address))next.memory.set8(address,value);
                else next_runtime[address]=value;
            };
            put_runtime(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2,0);
            ui::OtherGamesIdentity returned_id=id;
            returned_id.revision=revision+1;
            returned_id.sequence=id.sequence+1;
            auto page=ui::other_games_state_from_game(next,returned_id,texts_,exe_,
                current.projection.competition,current.projection.linked_page,
                owned_round,owned_display_mode,
                owned_participant_offset,false,current.underlay,next_runtime);
            if(page.projection.competition!=current.projection.competition||
               page.projection.entry_index!=current.projection.entry_index||
               page.projection.descriptor_competition!=current.projection.descriptor_competition||
               page.projection.linked_page!=current.projection.linked_page||
               page.projection.round!=current.projection.round)
                throw FormatError("other games: TeamInfo return changed the retained Source selection");
            if(page.projection.source_participant_count<=16) {
                const uint32_t view_at=::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(id.manager_slot);
                const uint8_t before_view=next.memory.has(view_at)
                    ?next.memory.u8(view_at):id.expected_view_byte;
                const uint8_t forced=uint8_t(before_view|0x80);
                put_runtime(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(id.manager_slot),forced);
                returned_id.expected_view_byte=forced;
                page=ui::other_games_state_from_game(next,returned_id,texts_,exe_,
                    current.projection.competition,current.projection.linked_page,
                    owned_round,OtherGamesDisplayMode::detail,
                    owned_participant_offset,false,current.underlay,next_runtime);
            }
            page.source_participant_offset=owned_participant_offset;
            other_games_source_redraw_effects(next,next_rand,next_runtime,page.projection,exe_);
            OtherGamesDispatch result;
            result.page=std::move(page);
            result.preparation=preparation_projection(next,revision+1);
            ui::OtherGamesScreen validate_page(data_directory_,result.page);
            ui::DesktopScreen validate_desktop(data_directory_,result.preparation.desktop);

            // Commit only after the nested presenter and both return projections
            // have succeeded. The modal scope stays owned; only its action token
            // advances, matching StartupFlow's full-identity ACK.
            g_=std::move(next);game_=next_game;rand_=next_rand;
            club_modal_runtime_=std::move(next_runtime);
            other_games_owner_->page=result.page;
            other_games_owner_->round=result.page.projection.round;
            other_games_owner_->participant_offset=result.page.source_participant_offset;
            other_games_owner_->display_mode=result.page.projection.display_mode;
            other_games_owner_->native=modern::save_stream(g_);
            other_games_owner_->runtime=club_modal_runtime_;
            other_games_owner_->rand_seed=rand_.seed();other_games_owner_->game_seed=game_.seed();
            other_games_owner_->rand_calls=rand_.calls();other_games_owner_->game_calls=game_.calls();
            ++revision;
            return result;
        }

        OtherGamesDispatch result;
        if (request.action == ui::OtherGamesAction::close) {
            if (request.value != -1) throw FormatError("other games: malformed close request");
            result.closed = true;
            result.page = current;
        } else {
            ModernState next = g_;
            const int manager = id.manager_slot;
            const uint32_t view_at = ::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField + uint32_t(manager);
            uint8_t view = next.memory.u8(view_at);
            int competition = other_games_selection_.competition;
            int linked_page = other_games_selection_.linked_page;
            int round = other_games_owner_->round;
            int local_cursor = other_games_owner_->participant_offset;
            auto mode = other_games_owner_->display_mode;
            bool source_new_selection = false;
            switch (request.action) {
            case ui::OtherGamesAction::select_competition: {
                if (current.projection.display_mode != OtherGamesDisplayMode::detail || request.value < 0 ||
                    request.value >= int(current.projection.selectors.size()))
                    throw FormatError("other games: invalid selector slot");
                const auto& selector = current.projection.selectors[size_t(request.value)];
                if (!selector.active) throw FormatError("other games: inactive selector slot");
                const int old_competition=competition;
                if (old_competition == selector.competition &&
                    current.projection.source_participant_count > 16) {
                    local_cursor += 16;
                    if (local_cursor >= current.projection.source_participant_count) local_cursor = 0;
                    const uint8_t updated = uint8_t((view & 0x80) | (local_cursor & 0x7f));
                    next.memory.set8(view_at,updated);
                    view = updated;
                } else {
                    // CupGroupOverviewEffects compares the selected ID with the clicked SLOT before it
                    // stores the selector ID. These are intentionally different values.
                    if (old_competition != request.value) {
                        local_cursor = 0;
                        linked_page = 0;
                        const uint8_t updated = uint8_t(view & 0x80);
                        next.memory.set8(view_at,updated);
                        view = updated;
                    }
                    competition = selector.competition;
                    local_cursor = int(view & 0x7f);
                    round = -1;
                    source_new_selection = old_competition != competition;
                }
                break;
            }
            case ui::OtherGamesAction::select_linked_page:
                if (current.projection.display_mode != OtherGamesDisplayMode::detail || request.value < 0 ||
                    request.value >= int(current.projection.linked_pages.size()) ||
                    current.projection.linked_pages[size_t(request.value)].ordinal != request.value)
                    throw FormatError("other games: invalid linked-page ordinal");
                linked_page = request.value;
                round = -1;
                local_cursor = int(view & 0x7f);
                break;
            case ui::OtherGamesAction::previous_round:
            case ui::OtherGamesAction::next_round:
                if (request.value != -1 || !current.projection.round_arrows_visible)
                    throw FormatError("other games: invalid round action");
                round = request.action == ui::OtherGamesAction::previous_round
                    ? (round + current.projection.rounds - 1) % current.projection.rounds
                    : (round + 1) % current.projection.rounds;
                local_cursor = 0;
                break;
            case ui::OtherGamesAction::toggle_display_mode:
                if (request.value != -1 || !current.projection.display_mode_toggle_available ||
                    current.projection.unfinished_predecessor || !current.projection.fixtures_available)
                    throw FormatError("other games: display toggle is unavailable");
                view ^= 0x80;
                next.memory.set8(view_at,view);
                mode = (view & 0x80) ? OtherGamesDisplayMode::detail : OtherGamesDisplayMode::compact;
                break;
            case ui::OtherGamesAction::team_info:
            case ui::OtherGamesAction::close:
                throw FormatError("other games: malformed action");
            default:
                throw FormatError("other games: unknown action");
            }
            if (current.projection.source_participant_count <= 16) {
                const uint8_t updated = uint8_t(view | 0x80);
                if (updated != view) next.memory.set8(view_at,updated);
                view = updated; mode = OtherGamesDisplayMode::detail;
            }
            ui::OtherGamesIdentity next_id = id;
            next_id.revision = revision + 1;
            next_id.sequence = id.sequence + 1;
            next_id.expected_view_byte = view;
            auto page = ui::other_games_state_from_game(next,next_id,texts_,exe_,competition,linked_page,
                round,mode,local_cursor,source_new_selection,current.underlay,club_modal_runtime_);
            if (page.projection.source_participant_count <= 16 && mode != OtherGamesDisplayMode::detail) {
                const uint8_t updated = uint8_t(view | 0x80);
                next.memory.set8(view_at,updated);
                next_id.expected_view_byte = updated;
                page.identity = next_id;
                mode = OtherGamesDisplayMode::detail;
            }
            page.source_participant_offset = local_cursor;
            result.page = std::move(page);
            result.preparation = preparation_projection(next,revision+1);
            g_ = std::move(next);
            other_games_selection_ = {competition,linked_page};
            other_games_owner_->page = result.page;
            other_games_owner_->round = result.page.projection.round;
            other_games_owner_->participant_offset = local_cursor;
            other_games_owner_->display_mode = mode;
            other_games_owner_->native = modern::save_stream(g_);
            other_games_owner_->runtime = club_modal_runtime_;
            other_games_owner_->rand_seed = rand_.seed(); other_games_owner_->game_seed = game_.seed();
            other_games_owner_->rand_calls = rand_.calls(); other_games_owner_->game_calls = game_.calls();
        }
        if (result.closed) {
            result.page.identity.revision = revision + 1;
            result.page.identity.sequence = id.sequence + 1;
            result.preparation = preparation_projection(g_,revision+1);
            other_games_owner_.reset();
        }
        ++revision;
        return result;
    } catch (const std::exception& e) {
        error = e.what();
        return {};
    }
}

std::optional<ui::BalanceState> ModernSession::open_balance_plan(
    const ui::DeskRequest& request,uint64_t revision,const Image& underlay,std::string& error) {
    error.clear();
    if(request.kind!=ui::DeskRequestKind::open_menu||request.menu_kind!=28||request.revision!=revision||
       revision==UINT64_MAX||balance_draft_||balance_dispatch_active_||strengths_owner_||strengths_dispatch_active_||
       top_scorers_owner_||top_scorers_dispatch_active_ || stadium_.block_expansion||other_games_owner_||other_games_dispatch_active_||
       league_pairings_draft_||league_pairings_dispatch_active_||market_dispatch_active_||pending_offer_dispatch_active_||
       calendar_advance_active_||transfer_offer_editor_||manager_join_||request.manager_slot<0||request.manager_slot>=4||
       size_t(request.manager_slot)>=g_.manager_teams.size()||!g_.memory.has(::bmh::layout::ActiveManager)||
       g_.memory.u8(::bmh::layout::ActiveManager)!=request.manager_slot) {
        error="balance: stale, nested or unsupported opening";return {};
    }
    try {
        const int slot=request.manager_slot, runtime_club=g_.manager_teams[size_t(slot)];
        if(runtime_club<0||size_t(runtime_club)>=g_.club_ids.size()||g_.date>UINT32_MAX||next_balance_scope_==UINT64_MAX)
            throw FormatError("balance: invalid manager identity");
        BalanceDraft next{g_,game_,rand_,club_modal_runtime_,club_modal_runtime_,{},modern::save_stream(g_),
            rand_.seed(),game_.seed(),rand_.calls(),game_.calls(),false};
        next.notice_gate={source_runtime_byte(g_,club_modal_runtime_,exe_,::bmh::layout::CompactDialogFrame),
            source_runtime_byte(g_,club_modal_runtime_,exe_,::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField2),
            source_runtime_byte(g_,club_modal_runtime_,exe_,::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField)};
        ModernState opening=g_;
        if(!opening.memory.has(::bmh::layout::PlanTicketPrice))opening.memory.add_block(::bmh::layout::PlanTicketPrice,4);
        for(int i=0;i<4;++i)opening.memory.set8(::bmh::layout::PlanTicketPrice+uint32_t(i),
            source_runtime_byte(g_,club_modal_runtime_,exe_,::bmh::layout::PlanTicketPrice+uint32_t(i)));
        auto draft=modern::open_balance_plan(opening,slot);
        if(!g_.memory.has(::bmh::layout::PlanTicketPrice+uint32_t(slot)))
            next.runtime[::bmh::layout::PlanTicketPrice+uint32_t(slot)]=uint8_t(draft.rows[kBalanceTicketPrice]);
        ui::BalanceIdentity id{slot,runtime_club,int(g_.club_ids[size_t(runtime_club)]),uint32_t(g_.date),revision,next_balance_scope_,0};
        next.page=ui::balance_from_game(g_,draft,exe_,id,underlay);
        apply_balance_source_style(next.page,g_,club_modal_runtime_,exe_);
        next.page.read_only_notice_enabled=next.notice_gate.enabled();
        next.page.read_only_notice.compact_dialog_frame=next.notice_gate.compact_dialog_frame!=0;
        next.page.close_notice.compact_dialog_frame=next.notice_gate.compact_dialog_frame!=0;
        ui::BalanceScreen validate(data_directory_,next.page);
        auto accepted_page=next.page;
        auto accepted_runtime=next.runtime;
        next.original_runtime=accepted_runtime;
        balance_draft_=std::move(next);
        club_modal_runtime_=std::move(accepted_runtime);
        ++next_balance_scope_;
        return accepted_page;
    } catch(const std::exception& e) {error=e.what();return {};}
}

std::optional<ModernSession::BalanceDispatch> ModernSession::dispatch_balance_plan(
    const ui::BalanceRequest& request,uint64_t& revision,std::string& error) {
    error.clear();
    if(stadium_.block_expansion||balance_dispatch_active_||!balance_draft_||request.identity!=balance_draft_->page.identity||
       request.identity.revision!=revision||request.identity.sequence==UINT64_MAX||revision==UINT64_MAX||
       modern::save_stream(g_)!=balance_draft_->original_stream||rand_.seed()!=balance_draft_->original_rand_seed||
       game_.seed()!=balance_draft_->original_game_seed||rand_.calls()!=balance_draft_->original_rand_calls||
       game_.calls()!=balance_draft_->original_game_calls||club_modal_runtime_!=balance_draft_->original_runtime) {
        error="balance: stale or nested owner action";return {};
    }
    auto next=*balance_draft_;
    balance_dispatch_active_=true;struct Reset {bool& x;~Reset(){x=false;}} reset{balance_dispatch_active_};
    try {
        auto& identity=next.page.identity;
        const int manager=identity.manager_slot;
        if(manager<0||manager>=4||size_t(manager)>=g_.manager_teams.size()||
           g_.manager_teams[size_t(manager)]!=identity.runtime_club||
           int(g_.club_ids[size_t(identity.runtime_club)])!=identity.club_identity||g_.date!=identity.day_counter||
           !g_.memory.has(::bmh::layout::ActiveManager)||g_.memory.u8(::bmh::layout::ActiveManager)!=manager)
            throw FormatError("balance: manager identity changed");
        BalanceDispatch result;
        if(request.kind==ui::BalanceAction::confirm_close) {
            if(!next.page.pending_close||!next.warning)throw FormatError("balance: no staged warning close");
            if(request.row!=-1||request.value)throw FormatError("balance: malformed confirmation");
            const auto before=identity;identity.revision=revision+1;identity.sequence=before.sequence+1;
            next.page.pending_close=false;next.page.close_notice={};
            result.closed=true;result.page=next.page;
            result.preparation=preparation_projection(next.state,revision+1);
            ui::validate_preparation_context(result.preparation);
            ui::DesktopScreen validate(data_directory_,result.preparation.desktop);
            g_=std::move(next.state);game_=next.game;rand_=next.rand;club_modal_runtime_=std::move(next.runtime);
            balance_draft_.reset();++revision;
            return result;
        }
        if(next.page.pending_close)throw FormatError("balance: warning confirmation is pending");
        auto draft=next.page.draft;
        switch(request.kind) {
        case ui::BalanceAction::edit_row: {
            if(request.row<0||request.row>=kBalanceRows)throw FormatError("balance: invalid row");
            const auto status=modern::edit_balance_plan(next.state,draft,request.row,request.value);
            if(status!=BalanceEditStatus::changed)throw FormatError("balance: edit rejected");
            break;
        }
        case ui::BalanceAction::cycle_view:
            if(request.row!=-1||request.value)throw FormatError("balance: malformed view action");
            modern::cycle_balance_view(next.state,draft);break;
        case ui::BalanceAction::take_actuals:
            if(request.row!=-1||request.value)throw FormatError("balance: malformed reset action");
            modern::reset_balance_plan(next.state,draft);break;
        case ui::BalanceAction::close: {
            if(request.row!=-1||request.value)throw FormatError("balance: malformed close action");
            const auto original_identity=identity;
            ModernState closing=next.state;
            if(!closing.memory.has(::bmh::layout::PlanTicketPrice))closing.memory.add_block(::bmh::layout::PlanTicketPrice,4);
            for(int i=0;i<4;++i)closing.memory.set8(::bmh::layout::PlanTicketPrice+uint32_t(i),
                source_runtime_byte(next.state,next.runtime,exe_,::bmh::layout::PlanTicketPrice+uint32_t(i)));
            const bool dfb_warning=modern::close_balance_plan(closing,draft);
            next.state.manager_records=std::move(closing.manager_records);
            const uint32_t ticket_address=::bmh::layout::PlanTicketPrice+uint32_t(manager);
            if(next.state.memory.has(ticket_address))next.state.memory.set8(ticket_address,closing.memory.u8(ticket_address));
            else next.runtime[ticket_address]=closing.memory.u8(ticket_address);
            next.notice_gate={source_runtime_byte(next.state,next.runtime,exe_,::bmh::layout::CompactDialogFrame),
                source_runtime_byte(next.state,next.runtime,exe_,::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField2),
                source_runtime_byte(next.state,next.runtime,exe_,::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField)};
            next.warning=dfb_warning&&next.notice_gate.enabled();
            next.page.pending_close=next.warning;
            next.page.close_notice=next.warning?next.page.close_notice:ui::TaxDialogProjection{};
            next.page.draft=draft;
            next.page.identity.sequence=original_identity.sequence+1;
            if(next.warning) {
                auto& notice=next.page.close_notice;
                notice.kind=ui::TaxDialogKind::acknowledgement;
                notice.source_manager_flags=uint8_t(manager)|0x40;
                notice.compact_dialog_frame=next.notice_gate.compact_dialog_frame!=0;
                notice.body=message_text(next.state,::bmh::layout::BalancePlan_BalanceFromGame_TextPointer2);
                const size_t record=size_t(manager)*0xb0c;
                if(record+0x223>next.state.manager_records.size())throw FormatError("balance: missing manager display identity");
                const size_t name_at=record+0xa8;size_t name_end=name_at;
                while(name_end<name_at+30&&next.state.manager_records[name_end])++name_end;
                notice.manager_name.assign(reinterpret_cast<const char*>(next.state.manager_records.data()+name_at),name_end-name_at);
                notice.portrait=next.state.manager_records[record+0x221];
                const int club=next.state.manager_teams[size_t(manager)];
                notice.badge_fallback=club==0x0fff||(club>=modern::german_clubs()&&club<modern::league_clubs())||(club>=modern::first_foreign_cup()&&club<no_team());
                if(!notice.badge_fallback&&club>=0&&size_t(club)*2+2<=next.state.club_heap_78.size())
                    notice.badge_sprite=(int(next.state.club_heap_78[size_t(club)*2])|
                        int(next.state.club_heap_78[size_t(club)*2+1])<<8)&0x1ff;
                else notice.badge_fallback=true;
                result.warning=true;result.page=next.page;balance_draft_=std::move(next);return result;
            }
            next.page.pending_close=false;
            next.page.identity.revision=revision+1;
            next.page.identity.sequence=original_identity.sequence+1;
            result.closed=true;result.page=next.page;
            result.preparation=preparation_projection(next.state,revision+1);
            ui::validate_preparation_context(result.preparation);
            ui::DesktopScreen validate(data_directory_,result.preparation.desktop);
            g_=std::move(next.state);game_=next.game;rand_=next.rand;club_modal_runtime_=std::move(next.runtime);
            balance_draft_.reset();++revision;return result;
        }
        default: throw FormatError("balance: unsupported action");
        }
        const int calculator_x=next.page.calculator_x,calculator_y=next.page.calculator_y;
        next.page.draft=draft;next.page.identity.sequence=identity.sequence+1;
        next.page=ui::balance_from_game(next.state,draft,exe_,next.page.identity,next.page.underlay);
        next.page.calculator_x=calculator_x;next.page.calculator_y=calculator_y;
        apply_balance_source_style(next.page,next.state,next.runtime,exe_);
        next.notice_gate={source_runtime_byte(next.state,next.runtime,exe_,::bmh::layout::CompactDialogFrame),
            source_runtime_byte(next.state,next.runtime,exe_,::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField2),
            source_runtime_byte(next.state,next.runtime,exe_,::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField)};
        next.page.read_only_notice_enabled=next.notice_gate.enabled();
        next.page.read_only_notice.compact_dialog_frame=next.notice_gate.compact_dialog_frame!=0;
        next.page.close_notice.compact_dialog_frame=next.notice_gate.compact_dialog_frame!=0;
        ui::BalanceScreen validate(data_directory_,next.page);
        result.page=next.page;balance_draft_=std::move(next);return result;
    } catch(const std::exception& e) {error=e.what();return {};}
}

std::optional<ui::LeaguePairingsState> ModernSession::open_league_pairings(
    const ui::DeskRequest& request,uint64_t revision,std::string& error) {
    error.clear();
    if(league_family_teaminfo_active_||request.kind!=ui::DeskRequestKind::open_menu||request.menu_kind!=1||request.revision!=revision||
        revision==std::numeric_limits<uint64_t>::max()||strengths_owner_||strengths_dispatch_active_||balance_draft_||balance_dispatch_active_||
        top_scorers_owner_||top_scorers_dispatch_active_ || stadium_.block_expansion||other_games_owner_||other_games_dispatch_active_||league_pairings_draft_||league_pairings_dispatch_active_||
        market_dispatch_active_||pending_offer_dispatch_active_||calendar_advance_active_||transfer_offer_editor_||
        manager_join_||request.manager_slot<0||request.manager_slot>=4) {
        error="pairings: stale, nested or unsupported opening";return std::nullopt;
    }
    try {
        LeaguePairingsDraft next{g_,game_,rand_,club_modal_runtime_,club_modal_runtime_,{}, {},
            modern::save_stream(g_),rand_.seed(),game_.seed(),rand_.calls(),game_.calls()};
        for(const auto& group:turn_driver_.groups)
            next.original_active_groups.emplace_back(group.desc,group.fixtures);
        next.original_referee_profiles=turn_driver_.referees;
        next.owner=modern::open_league_pairings_source(next.state,request.manager_slot,ui::league_pairings_constants(exe_));
        next.active_packet=validate_normal_pairings_group(next.state,next.owner,next.runtime);
        const uint64_t scope=++league_pairings_scope_;
        auto page=ui::league_pairings_from_game(next.state,next.owner,exe_,revision,scope,0,next.active_packet);
        ui::LeaguePairingsScreen validate(data_directory_,page);
        next.identity=page.identity;
        next.runtime[::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField]=exe_.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField+uint32_t(next.owner.league));
        pairings_runtime_draw(next.state,next.runtime,page,exe_);
        league_pairings_draft_=std::move(next);return page;
    } catch(const std::exception& e) {error=e.what();return std::nullopt;}
}

std::optional<LeaguePairingsActivePacket> ModernSession::validate_normal_pairings_group(
    const ModernState& state,LeaguePairingsOwner& owner,
    const std::map<uint32_t,uint8_t>& runtime) const {
    //Sample synthesizes event8 only for a running process timer. These globals
    //are not serialized Native pointers. The normal port starts them at zero.
    const auto byte=[&](uint32_t address) {if(state.memory.has(address))return state.memory.u8(address);
        const auto at=runtime.find(address);return at==runtime.end()?uint8_t(0):at->second;};
    const unsigned duration=byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField);
    const uint32_t started=uint32_t(byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField2))|(uint32_t(byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField3))<<8)|
        (uint32_t(byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField4))<<16)|(uint32_t(byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField5))<<24);
    if(duration<200&&started!=0)
        throw FormatError("pairings: active process timer presentation is not implemented");
    auto packet=modern::league_pairings_active_packet_source(state,owner,turn_driver_.groups,turn_driver_.referees);
    owner.active_group=packet?packet->group_index:-1;
    if(!packet)owner.active_phase=false;
    if(packet)modern::validate_league_pairings_active_packet_source(state,owner,*packet);
    return packet;
}

std::optional<ModernSession::LeaguePairingsDispatch> ModernSession::dispatch_league_pairings(
    const ui::LeaguePairingsRequest& request,uint64_t& revision,std::string& error,
    const TransferOfferTeamInfoPresenter& team_info) {
    if (league_family_teaminfo_active_ || balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return std::nullopt; }
    error.clear();
    const auto live_matches=[&] {
        if(!league_pairings_draft_)return false;
        const auto& d=*league_pairings_draft_;
        if(d.original_active_groups.size()!=turn_driver_.groups.size()||
            d.original_referee_profiles!=turn_driver_.referees)return false;
        for(size_t n=0;n<turn_driver_.groups.size();++n)
            if(d.original_active_groups[n].first!=turn_driver_.groups[n].desc||
                d.original_active_groups[n].second!=turn_driver_.groups[n].fixtures)return false;
        return revision==d.identity.revision&&modern::save_stream(g_)==d.original_stream&&
            rand_.seed()==d.original_rand_seed&&game_.seed()==d.original_game_seed&&
            rand_.calls()==d.original_rand_calls&&game_.calls()==d.original_game_calls&&
            club_modal_runtime_==d.original_runtime;
    };
    if(balance_draft_||balance_dispatch_active_||!league_pairings_draft_||league_pairings_dispatch_active_||top_scorers_owner_||top_scorers_dispatch_active_||other_games_owner_||other_games_dispatch_active_||market_dispatch_active_||
        pending_offer_dispatch_active_||calendar_advance_active_||
        request.identity!=league_pairings_draft_->identity||!live_matches()||
        request.identity.sequence==std::numeric_limits<uint64_t>::max()||
        revision==std::numeric_limits<uint64_t>::max()) {
        error="pairings: stale or nested family action";return std::nullopt;
    }
    if(request.event.kind==LeaguePairingsAction::team_info&&!team_info) {
        error="pairings: TeamInfo presenter unavailable";return std::nullopt;
    }
    auto next=*league_pairings_draft_;
    league_pairings_dispatch_active_=true;
    struct Reset {bool& value;~Reset(){value=false;}} reset{league_pairings_dispatch_active_};
    try {
        auto bound_owner=next.owner;
        const auto bound_packet=validate_normal_pairings_group(next.state,bound_owner,next.runtime);
        if(bound_packet!=next.active_packet||bound_owner.active_phase!=next.owner.active_phase||
            bound_owner.active_group!=next.owner.active_group)
            throw FormatError("pairings: stale staged active presentation packet");
        next.owner=modern::edit_league_pairings_source(next.state,next.owner,request.event,
            ui::league_pairings_constants(exe_),next.game,next.rand);
        if(request.event.kind==LeaguePairingsAction::computer_tip) {
            const auto put_scratch=[&](uint32_t address,uint8_t value) {
                if(next.state.memory.has(address))next.state.memory.set8(address,value);
                else next.runtime[address]=value;
            };
            // ComputeLeagueTableForecast's final 17295(0x3fc) clears the strength scratch before
            // the ordinary list redraw repopulates it from visible away rows.
            put_scratch(::bmh::layout::CareerSession_FormatError_ExecutableField3,0);put_scratch(::bmh::layout::CareerSession_FormatError_ExecutableField4,0);
        }
        if(!next.owner.closed)next.active_packet=validate_normal_pairings_group(next.state,next.owner,next.runtime);
        if(request.event.kind==LeaguePairingsAction::team_info) {
            const auto parent=ui::league_pairings_from_game(next.state,next.owner,exe_,revision,
                next.identity.scope,next.identity.sequence,next.active_packet);
            auto services=staged_club_modal_services(next.state,next.game,next.rand,next.runtime);
            if(!team_info(services,request.event.value,ui::LeaguePairingsScreen(data_directory_,parent).render())) {
                error="pairings: TeamInfo presenter stopped";return std::nullopt;
            }
            const auto returned_packet=validate_normal_pairings_group(next.state,next.owner,next.runtime);
            if(returned_packet!=next.active_packet)
                throw FormatError("pairings: active presentation packet changed during TeamInfo");
        }
        if(!live_matches()) {error="pairings: live state changed during modal";return std::nullopt;}
        LeaguePairingsDispatch result;result.closed=next.owner.closed;
        if(result.closed) {
            result.preparation=preparation_projection(next.state,revision+1);
            ui::validate_preparation_context(result.preparation);
            ui::DesktopScreen validate(data_directory_,result.preparation.desktop);
            // Move returns with event8;53435 stirs once and leaves.
            next.runtime[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2]=8;
            g_=std::move(next.state);game_=next.game;rand_=next.rand;club_modal_runtime_=std::move(next.runtime);
            league_pairings_draft_.reset();++revision;
        } else {
            result.page=ui::league_pairings_from_game(next.state,next.owner,exe_,revision,
                next.identity.scope,next.identity.sequence+1,next.active_packet);
            ui::LeaguePairingsScreen validate(data_directory_,result.page);
            pairings_runtime_draw(next.state,next.runtime,result.page,exe_);
            next.identity=result.page.identity;league_pairings_draft_=std::move(next);
        }
        return result;
    } catch(const std::exception& e) {error=e.what();return std::nullopt;}
}

namespace {
ui::OutgoingTransferEditorState transfer_offer_projection(const ModernState& game,
    const TransferOfferEditorState& owner, uint64_t revision, bool gradient) {
    ui::OutgoingTransferEditorState out;
    const auto& request = owner.request;
    out.manager_slot = request.manager; out.runtime_club = request.runtime_club;
    out.revision = revision; out.list_entry = request.list_entry; out.source_slot = request.source_slot;
    out.player = request.expected_player;
    out.seller = request.expected_seller_word == 0xffff ? 0x0fff : request.expected_seller_word & 0x7fff;
    out.bid_slot = owner.bid_slot; out.source_mode = owner.mode;
    out.editable = owner.editable; out.closed = owner.closed; out.quote_loan = owner.quote_loan;
    out.initial_action_loan = request.loan;
    out.gradient_text = gradient;
    out.fee = owner.fee; out.monthly_salary = owner.salary; out.duration_years = owner.years;
    out.bonuses = owner.bonuses;
    const auto copy_profile = [](const TransferOfferInputProfile& p) {
        return ui::OutgoingTransferCalculatorProfile{p.supported, p.currency, p.minimum, p.maximum,
            p.initial, p.source_text_address, p.presets};
    };
    for (size_t n = 0; n < 2; ++n) out.money_calculators[n] = copy_profile(owner.money_profiles[n]);
    for (size_t n = 0; n < 8; ++n) out.mode3_calculators[n] = copy_profile(owner.mode3_profiles[n]);
    if (owner.closed) return out; // No modal is constructed after Source cleanup.
    ui::IncomingTransferDecisionView view;
    view.source_outgoing_editor_context = true;
    view.valid = true; view.list_entry = request.list_entry; view.player = out.player;
    view.seller = out.seller; view.seller_manager = out.seller == 0x0fff ? -1 : game.manager_of(out.seller);
    view.viewer_manager = request.manager;
    view.asking_fee = owner.asking_fee; view.asking_monthly_salary = owner.asking_salary;
    view.quoted_fee = owner.quoted_fee; view.quoted_monthly_salary = owner.quoted_salary;
    view.quoted_years = owner.years;
    const uint32_t list = ::bmh::layout::EditorList + uint32_t(request.list_entry) * 0x58;
    for (uint32_t n = 0; n < 4; ++n) {
        auto& row = view.offers[n];
        const auto buyer = game.memory.u16(list + n * 2);
        row.bid_index = int(n); row.present = buyer != 0xffff;
        row.buyer = row.present ? int(buyer) : -1;
        row.buyer_manager = row.present ? game.manager_of(row.buyer) : -1;
        row.fee = game.memory.u32(list + 0x1c + n * 4);
        row.monthly_salary = game.memory.u32(list + 0x0c + n * 4);
        const auto raw_term = game.memory.u8(list + 0x34 + n);
        row.loan = (raw_term & 0x80) != 0; row.contract_months = raw_term & 0x7f;
        row.eligible = row.present;
        for (uint32_t k = 0; k < 7; ++k) row.bonuses[k] = game.memory.u8(list + 0x38 + n * 7 + k);
    }
    out.panel = ui::incoming_transfer_decision_from_game(game, view);
    return out;
}
}

ui::OutgoingTransferEditorState loan_return_offer_editor_projection(
    const ModernState& game, const TransferOfferEditorState& owner, uint64_t revision, bool gradient) {
    return transfer_offer_projection(game, owner, revision, gradient);
}

TransferOfferEditorState edit_transfer_offer_from_ui(ModernState& game,
    const TransferOfferEditorState& owner, const ui::OutgoingTransferRequest& request,
    GameRandom& game_random, WatcomRandom& watcom_random) {
    return modern::edit_transfer_offer_editor_source(game, owner,
        {TransferOfferEditorField(uint8_t(request.field)), request.value, request.accepted},
        game_random, watcom_random);
}

std::optional<ModernSession::TransferOfferEditorDispatch> ModernSession::open_transfer_offer(
    const ui::TransferMarketRequest& request, uint64_t& revision, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return std::nullopt; }
    error.clear();
    if (request.kind != ui::TransferMarketRequestKind::open_offer || transfer_offer_editor_ ||
        market_dispatch_active_ || pending_offer_dispatch_active_ || calendar_advance_active_ ||
        request.revision != revision || revision == std::numeric_limits<uint64_t>::max() ||
        request.manager_slot < 0 || request.manager_slot >= 4 ||
        size_t(request.manager_slot) >= g_.manager_teams.size() ||
        !g_.memory.has(::bmh::layout::ActiveManager) || g_.memory.u8(::bmh::layout::ActiveManager) != request.manager_slot ||
        request.runtime_club != g_.manager_teams[size_t(request.manager_slot)] ||
        request.bid.manager != request.manager_slot || request.bid.list_entry < 0 || request.bid.list_entry >= 40 ||
        request.bid.source_slot != int(modern::transfer_list_slot()) + request.bid.list_entry || request.selected_players.size() > 1) {
        error = "transfer offer: stale, nested or unsupported marked opening";
        return std::nullopt;
    }
    const uint32_t list = ::bmh::layout::EditorList + uint32_t(request.bid.list_entry) * 0x58;
    if (!g_.memory.has(list + 0x57)) { error = "transfer offer: missing Native list row"; return std::nullopt; }
    const uint16_t seller_word = g_.memory.u16(list + 8);
    if (!request.selected_players.empty()) {
        const auto& selected = request.selected_players.front();
        const int seller = seller_word == 0xffff ? -1 : int(seller_word & 0x7fff);
        if (selected.squad_row != -1 || selected.source_slot != request.bid.source_slot ||
            selected.list_entry != request.bid.list_entry || selected.player != request.bid.player ||
            selected.seller != seller) {
            error = "transfer offer: selected row does not match the Native market identity";
            return std::nullopt;
        }
    }
    int bid_slot = -1;
    for (int n = 0; n < 4; ++n) {
        const auto buyer = g_.memory.u16(list + uint32_t(n) * 2);
        if (buyer == 0xffff || buyer == request.runtime_club) { bid_slot = n; break; }
    }
    const TransferOfferEditorRequest input{request.manager_slot, request.runtime_club, request.bid.list_entry,
        request.bid.source_slot, request.bid.player, seller_word, bid_slot, request.offer_is_loan};
    auto next_state = g_; auto next_game = game_; auto next_rand = rand_;
    market_dispatch_active_ = true;
    struct Reset { bool& active; ~Reset() { active = false; } } reset{market_dispatch_active_};
    try {
        auto owner = modern::open_transfer_offer_editor_source(next_state, input, next_game, next_rand);
        if (!owner.supported) { error = owner.reason; return std::nullopt; }
        // Opening has Source G-seeding/G92..108 even for a full read-only list.
        const auto next_revision = revision + 1;
        TransferOfferEditorDispatch result;
        result.preparation = preparation_projection(next_state, next_revision);
        result.editor = transfer_offer_projection(next_state, owner, next_revision, result.preparation.desktop.gradient_text);
        ui::OutgoingTransferScreen validate(data_directory_, result.editor);
        g_ = std::move(next_state); game_ = next_game; rand_ = next_rand;
        transfer_offer_editor_ = std::move(owner); transfer_offer_revision_ = next_revision; revision = next_revision;
        return result;
    } catch (const std::exception& e) { error = e.what(); return std::nullopt; }
}

std::optional<ModernSession::PoachDispatch> ModernSession::open_poach(
    int manager_slot, int squad_slot, uint64_t& revision, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return std::nullopt; }
    error.clear();
    if (transfer_offer_editor_ || market_dispatch_active_ || pending_offer_dispatch_active_ || calendar_advance_active_ ||
        revision == std::numeric_limits<uint64_t>::max() || manager_slot < 0 || manager_slot >= 4 ||
        size_t(manager_slot) >= g_.manager_teams.size() || !g_.memory.has(::bmh::layout::ActiveManager) ||
        g_.memory.u8(::bmh::layout::ActiveManager) != manager_slot) {
        error = "poach: stale, nested or unsupported opening";
        return std::nullopt;
    }
    auto next_state = g_; auto next_game = game_; auto next_rand = rand_;
    market_dispatch_active_ = true;
    struct Reset { bool& active; ~Reset() { active = false; } } reset{market_dispatch_active_};
    try {
        PoachDispatch result;
        result.start = modern::start_poach(next_state, manager_slot, squad_slot, next_rand);
        if (result.start.status == PoachStatus::ready) {
            const int row = 0x28 + result.start.offer_row;
            const TransferOfferEditorRequest input{manager_slot, g_.manager_teams[size_t(manager_slot)], row, int(modern::transfer_list_slot()) + row,
                result.start.player, next_state.memory.u16(::bmh::layout::EditorList + uint32_t(row) * 0x58 + 8), 0, false, true};
            auto owner = modern::open_transfer_offer_editor_source(next_state, input, next_game, next_rand);
            if (!owner.supported) { error = owner.reason; return std::nullopt; }
            const auto next_revision = revision + 1;
            TransferOfferEditorDispatch editor;
            editor.preparation = preparation_projection(next_state, next_revision);
            editor.editor = transfer_offer_projection(next_state, owner, next_revision, editor.preparation.desktop.gradient_text);
            result.editor = std::move(editor);
            transfer_offer_editor_ = std::move(owner);
            transfer_offer_revision_ = next_revision;
            poach_ = result.start;
            revision = next_revision;
        } else if (result.start.status != PoachStatus::invalid) {
            ++revision;  // draws, and "Transfermarkt überlastet" counts the attempt
        }
        g_ = std::move(next_state); game_ = next_game; rand_ = next_rand;
        return result;
    } catch (const std::exception& e) { error = e.what(); return std::nullopt; }
}

bool ModernSession::close_poach_table_selection(const ui::LeagueFamilyRequest& request,std::string& error) {
    error.clear();
    if(!poach_table_selection_ || request.kind!=ui::LeagueFamilyRequestKind::close || request.from_child!=1 ||
       !canonical_table_envelope(request) || !league_family_owner_ || league_family_owner_->child!=1 ||
       league_family_owner_->identity!=request.identity || !league_family_owner_->table ||
       league_family_owner_->table->phase!=TableDispatchPhase::ready ||
       modern::save_stream(g_)!=league_family_owner_->native || club_modal_runtime_!=league_family_owner_->runtime ||
       game_.seed()!=league_family_owner_->game_seed || game_.calls()!=league_family_owner_->game_calls ||
       rand_.seed()!=league_family_owner_->rand_seed || rand_.calls()!=league_family_owner_->rand_calls) {
        error="poach: stale Table selection close";return false;
    }
    const uint32_t at=::bmh::layout::LeagueTableDisplayModes+uint32_t(request.identity.manager)*2;
    uint16_t word=uint16_t(g_.memory.u16(at)&0x0f7f);
    if(source_runtime_byte(g_,club_modal_runtime_,exe_,::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField)!=0)word|=0x80;
    g_.memory.set16(at,word);
    if(g_.memory.has(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2))g_.memory.set8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2,0);else club_modal_runtime_[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2]=0;
    // FriendlyMatchScreen returns directly to19c1c. No21213 family close.
    league_family_owner_.reset();return true;
}

bool ModernSession::dispatch_poach_ui(const ui::TransferMarketRequest& request,
    const Image& backdrop, const PoachPlayerSelector& select, const PoachUiAccept& accept,
    uint64_t& revision, std::string& error) {
    error.clear();
    if (!accept || request.kind!=ui::TransferMarketRequestKind::open_poach ||
        !g_.memory.has(::bmh::layout::GameMode) || g_.memory.u8(::bmh::layout::GameMode)!=3 ||
        request.revision!=revision || revision==UINT64_MAX || request.manager_slot<0 || request.manager_slot>3 ||
        !g_.memory.has(::bmh::layout::ActiveManager) || g_.memory.u8(::bmh::layout::ActiveManager)!=request.manager_slot ||
        size_t(request.manager_slot)>=g_.manager_teams.size() ||
        request.runtime_club!=g_.manager_teams[size_t(request.manager_slot)] ||
        request.bid.manager!=request.manager_slot || request.bid.list_entry!=-1 || request.bid.player!=-1 ||
        request.bid.source_slot!=-1 || request.bid.fee!=0 || request.bid.annual_salary!=0 ||
        request.bid.contract_years!=1 || request.bid.poach || std::any_of(request.bid.bonuses.begin(),request.bid.bonuses.end(),
            [](uint32_t n){return n!=0;}) || request.squad_row!=-1 || request.expected_player!=-1 ||
        request.preview_source_slot!=-1 || request.preview_seller!=-1 || request.preview_is_purchase ||
        request.preview_is_loan || !request.selected_players.empty() ||
        backdrop.width!=320 || backdrop.height!=240 || advertising_renewal_dispatch_active_ || pending_offer_dispatch_active_ || market_dispatch_active_ ||
        calendar_advance_active_ || transfer_offer_editor_ || poach_ || manager_join_ || contract_quote_ ||
        league_family_owner_ || league_pairings_draft_ || strengths_owner_ || top_scorers_owner_ ||
        other_games_owner_ || balance_draft_ || stadium_.opened ||
        (turn_driver_.stage!=TurnDriver::Stage::MatchdayDesks && turn_driver_.stage!=TurnDriver::Stage::SeasonEndDesk)) {
        error="poach: stale or competing selection";return false;
    }
    const auto checkpoint=advertising_session_checkpoint(revision);
    try {
        const int manager=request.manager_slot;
        const size_t record=size_t(manager)*0xb0c;
        PoachDispatch result;
        result.start.manager=manager;
        // These three notices precede the Table modal in Reset.
        if (record+0x8c0>g_.manager_records.size()) throw FormatError("poach: missing manager record");
        const bool attempts_used=(g_.manager_records[record+0x8be] |
            uint16_t(g_.manager_records[record+0x8bf])<<8)!=0xffff;
        if(attempts_used) {
            result.start.status=PoachStatus::attempts_used;
            result.start.messages.push_back(message_text(g_,::bmh::layout::CareerSession_DispatchPoachUi_TextPointer2));
        } else if(g_.manager_records[record+0x220]>>5==1) {
            result.start.status=PoachStatus::transfer_ban;
            result.start.messages.push_back(message_text(g_,::bmh::layout::CareerSession_DispatchPoachUi_TextPointer));
        } else if(request.offer_is_loan) {
            auto services=staged_club_modal_services(g_,game_,rand_,club_modal_runtime_);
            services.set_runtime_byte(::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField,0);
            services.set_runtime_byte(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField2,g_.manager_records[record+0x210]);
            services.set_runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField,2);
            result.start.messages.push_back(message_text(g_,::bmh::layout::CareerSession_DispatchPoachUi_TextPointer3));
        } else {
            if(!select) throw FormatError("poach: Source player selection presenter unavailable");
            // The cloned Session owns the existing50775 controls implementation.
            // begin_league_family(child1) itself does not call53435; only the
            // host Table owner is reused, never prepare/close-family effects.
            ModernSession staged=*this;
            staged.poach_table_selection_=true;
            uint64_t selection_revision=revision;
            auto services=staged.staged_club_modal_services(staged.g_,staged.game_,staged.rand_,staged.club_modal_runtime_);
            services.set_runtime_byte(::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField,0);
            services.set_runtime_byte(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField2,g_.manager_records[record+0x210]);
            services.set_runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField,2);
            services.set_runtime_byte(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField,0);services.set_runtime_byte(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2,0);
            //50775 control7 toggles this existing BSS byte. Materialize its
            // actual current value in the staged runtime, including when it
            // was read from the executable rather than persisted in Native.
            services.set_runtime_byte(::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField,services.runtime_byte(::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField));
            auto entry=staged.strengths_open_request(selection_revision);
            if(!entry)throw FormatError("poach: missing Table selection identity");
            entry->identity.league=g_.manager_records[record+0x210];entry->identity.league_group=0;
            if(!staged.begin_league_family(entry->identity,1,selection_revision,error))throw FormatError(error);
            market_dispatch_active_=true;
            const auto live_baseline=advertising_session_checkpoint(revision);
            const auto selection=select(staged,services,manager,selection_revision,backdrop);
            if(!advertising_session_unchanged(live_baseline,revision))
                throw FormatError("poach: selection changed the live session");
            auto& next=staged.g_;
            if(next.date!=g_.date || next.manager_teams!=g_.manager_teams ||
               !next.memory.has(::bmh::layout::ActiveManager) || next.memory.u8(::bmh::layout::ActiveManager)!=manager)
                throw FormatError("poach: selection changed its manager/calendar context");
            market_dispatch_active_=false;
            int slot=-1;
            if(selection) {
                if(selection->origin!=ClubPlayerDetailOrigin::squad || selection->human_loan_index!=-1)
                    throw FormatError("poach: selection is not an ordinary squad row");
                const auto player=modern::club_player_detail_context(next,*selection);
                slot=player.source_slot;
                if(slot<0 || slot>=int(modern::transfer_list_slot()) || modern::slot_team(slot)!=selection->runtime_team ||
                   (next.squad_slots.at(size_t(slot))&0xfff)!=selection->player_id)
                    throw FormatError("poach: stale player selection");
            }
            // Preserve actual Table/ClubInfo/card effects, including a cancelled
            // selection. No LeagueScreenShown entry and no family-close RNG stir.
            g_=std::move(next);game_=staged.game_;rand_=staged.rand_;club_modal_runtime_=std::move(staged.club_modal_runtime_);
            if(selection) {
                auto opened=open_poach(manager,slot,revision,error);
                if(!opened) throw FormatError(error);
                result=std::move(*opened);
            }
            if(revision==checkpoint.revision) ++revision;
        }
        const auto accepted_baseline=advertising_session_checkpoint(revision);
        accept(result,preparation_projection(revision));
        if(!advertising_session_unchanged(accepted_baseline,revision))
            throw FormatError("poach: UI acknowledgement changed the session");
        return true;
    } catch(const std::exception& e) {
        restore_advertising_session_checkpoint(checkpoint);revision=checkpoint.revision;
        error=e.what();return false;
    }
}

std::optional<ModernSession::TransferOfferEditorDispatch> ModernSession::dispatch_transfer_offer(
    const ui::OutgoingTransferRequest& request, const HumanBuyerSaleHooks& presenters,
    uint64_t& revision, std::string& error,
    const TransferOfferTeamInfoPresenter& team_info,
    const TransferOfferPlayerInfoPresenter& player_info) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return std::nullopt; }
    error.clear();
    if (!transfer_offer_editor_ || market_dispatch_active_ || pending_offer_dispatch_active_ || calendar_advance_active_ ||
        request.identity.revision != revision || transfer_offer_revision_ != revision ||
        revision == std::numeric_limits<uint64_t>::max()) {
        error = "transfer offer: stale or nested editor event"; return std::nullopt;
    }
    const auto& current = *transfer_offer_editor_;
    const auto& id = request.identity;
    const int seller = current.request.expected_seller_word == 0xffff ? 0x0fff : current.request.expected_seller_word & 0x7fff;
    if (id.manager_slot != current.request.manager || id.runtime_club != current.request.runtime_club ||
        id.list_entry != current.request.list_entry || id.source_slot != current.request.source_slot ||
        id.player != current.request.expected_player || id.seller != seller || id.bid_slot != current.bid_slot ||
        request.quoted_fee != current.quoted_fee || request.quoted_monthly_salary != current.quoted_salary ||
        request.quoted_years != current.years) {
        error = "transfer offer: mismatched editor identity or quote"; return std::nullopt;
    }
    uint32_t fee = current.fee, salary = current.salary;
    uint8_t years = current.years; auto bonuses = current.bonuses;
    bool quote_loan = current.quote_loan;
    const unsigned field = unsigned(request.field);
    if (request.kind == ui::OutgoingTransferRequestKind::quote_change) {
        if (field > unsigned(ui::OutgoingTransferField::quote_loan)) {
            error = "transfer offer: invalid editor field"; return std::nullopt;
        }
        if (request.accepted) {
            if (field == 0) fee = request.value;
            else if (field == 1) salary = request.value;
            else if (field == 2) years = uint8_t(request.value);
            else if (field < 10) bonuses[field - 3] = uint8_t(field < 6 ? request.value : request.value / 5);
            else quote_loan = !quote_loan;
        }
    } else if (request.kind != ui::OutgoingTransferRequestKind::submit && request.kind != ui::OutgoingTransferRequestKind::cancel &&
               request.kind != ui::OutgoingTransferRequestKind::team_info &&
               request.kind != ui::OutgoingTransferRequestKind::player_info) {
        error = "transfer offer: unsupported editor event"; return std::nullopt;
    }
    if (request.fee != fee || request.monthly_salary != salary || request.duration_years != years ||
        request.bonuses != bonuses || request.quote_loan != quote_loan) {
        error = "transfer offer: posted row differs from the single Source field event"; return std::nullopt;
    }
    if(request.kind==ui::OutgoingTransferRequestKind::team_info&&!team_info) {
        error="transfer offer: TeamInfo presenter unavailable"; return std::nullopt;
    }
    if(request.kind==ui::OutgoingTransferRequestKind::player_info&&!player_info) {
        error="transfer offer: PlayerInfo presenter unavailable"; return std::nullopt;
    }
    auto next_state = g_; auto next_game = game_; auto next_rand = rand_;
    auto next_runtime=club_modal_runtime_;
    auto owner = current; TransferActionResult action;
    auto hooks = presenters;
    switch (turn_driver_.transfer_allocator_context) {
    case PendingOfferAllocatorGate::zero: hooks.allocator_gate = HumanBuyerSaleAllocatorGate::zero; break;
    case PendingOfferAllocatorGate::blocked: hooks.allocator_gate = HumanBuyerSaleAllocatorGate::blocked; break;
    default: hooks.allocator_gate = HumanBuyerSaleAllocatorGate::unsupported; break;
    }
    hooks.direct_mode = HumanBuyerSaleDirectMode::ordinary_ai_purchase;
    hooks.before_accounts = {}; hooks.after_paid_accounts = {};
    market_dispatch_active_ = true;
    struct Reset { bool& active; ~Reset() { active = false; } } reset{market_dispatch_active_};
    try {
        if (request.kind == ui::OutgoingTransferRequestKind::quote_change) {
            owner = edit_transfer_offer_from_ui(next_state, current, request, next_game, next_rand);
        } else if (request.kind == ui::OutgoingTransferRequestKind::cancel) {
            owner = modern::cancel_transfer_offer_editor_source(next_state, current);
        } else if(request.kind==ui::OutgoingTransferRequestKind::team_info) {
            // Validate the complete old Native row before any modal effect.
            const auto validation=modern::cancel_transfer_offer_editor_source(next_state,current);
            if(!validation.supported) {error=validation.reason;return std::nullopt;}
            auto services=staged_club_modal_services(next_state,next_game,next_rand,next_runtime);
            const auto parent=transfer_offer_projection(next_state,current,revision,
                next_state.memory.u8(::bmh::layout::TextGradientDisabled)==0);
            const Image backdrop=ui::OutgoingTransferScreen(data_directory_,parent).render();
            if(!team_info(services,current.request.runtime_club,backdrop)) {
                error="transfer offer: TeamInfo presenter stopped";return std::nullopt;
            }
            owner=modern::resume_transfer_offer_team_info_source(next_state,current,next_rand);
        } else if(request.kind==ui::OutgoingTransferRequestKind::player_info) {
            const auto selection=modern::transfer_offer_player_info_selection_source(next_state,current);
            auto services=staged_club_modal_services(next_state,next_game,next_rand,next_runtime);
            const auto parent=transfer_offer_projection(next_state,current,revision,
                next_state.memory.u8(::bmh::layout::TextGradientDisabled)==0);
            const Image backdrop=ui::OutgoingTransferScreen(data_directory_,parent).render();
            services.set_runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField,1); // Source direct entry persists, unlike4f548.
            if(!player_info(services,selection,backdrop)) {
                error="transfer offer: PlayerInfo presenter stopped";return std::nullopt;
            }
            owner=modern::resume_transfer_offer_player_info_source(next_state,current,
                services.runtime_byte(::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableField),services.runtime_byte(::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableField2),next_rand);
        } else {
            MinuteReport report;
            auto submitted = modern::submit_transfer_offer_editor_source(next_state, current, next_rand, hooks, report);
            owner = std::move(submitted.editor); action = submitted.action;
            if (report.unsupported) { error = report.reason; return std::nullopt; }
        }
        if (!owner.supported) { error = owner.reason; return std::nullopt; }
        // a poaching editor that closes frees its special row and counts the attempt (Reset)
        if (owner.closed && poach_) modern::finish_poach(next_state, *poach_);
        const bool changed = modern::save_stream(next_state) != modern::save_stream(g_) || next_runtime != club_modal_runtime_ ||
            next_game.seed() != game_.seed() || next_game.calls() != game_.calls() ||
            next_rand.seed() != rand_.seed() || next_rand.calls() != rand_.calls();
        const auto next_revision = revision + uint64_t(changed);
        TransferOfferEditorDispatch result;
        result.preparation = preparation_projection(next_state, next_revision);
        result.editor = transfer_offer_projection(next_state, owner, next_revision, result.preparation.desktop.gradient_text);
        result.action = action;
        if (!owner.closed) { ui::OutgoingTransferScreen validate(data_directory_, result.editor); }
        g_ = std::move(next_state); game_ = next_game; rand_ = next_rand;
        club_modal_runtime_=std::move(next_runtime);
        if (owner.closed) { transfer_offer_editor_.reset(); poach_.reset(); } else transfer_offer_editor_ = std::move(owner);
        transfer_offer_revision_ = next_revision; revision = next_revision;
        return result;
    } catch (const std::exception& e) { error = e.what(); return std::nullopt; }
}

std::optional<ModernSession::TransferMarketPreviewDispatch> ModernSession::preview_transfer_market(
    const ui::TransferMarketRequest& request, uint64_t& revision, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return std::nullopt; }
    error.clear();
    if (request.kind != ui::TransferMarketRequestKind::preview_quote ||
        transfer_offer_editor_ || market_dispatch_active_ || pending_offer_dispatch_active_ || calendar_advance_active_ ||
        request.revision != revision || revision == std::numeric_limits<uint64_t>::max() ||
        request.manager_slot < 0 || request.manager_slot >= 4 ||
        size_t(request.manager_slot) >= g_.manager_teams.size() ||
        !g_.memory.has(::bmh::layout::ActiveManager) || g_.memory.u8(::bmh::layout::ActiveManager) != request.manager_slot ||
        request.runtime_club != g_.manager_teams[size_t(request.manager_slot)]) {
        error = "transfer market: stale or nested preview";
        return std::nullopt;
    }
    if (request.preview_is_purchase) {
        if (request.preview_snapshot_index != -1 || request.squad_row != -1 || request.bid.manager != request.manager_slot ||
            request.bid.list_entry < 0 || request.bid.list_entry >= 40 ||
            request.bid.source_slot != request.preview_source_slot ||
            request.preview_source_slot != int(modern::transfer_list_slot()) + request.bid.list_entry ||
            request.bid.player != request.expected_player) {
            error = "transfer market: mismatched preview listing identity";
            return std::nullopt;
        }
    } else if (request.preview_snapshot_index >= 0) {
        const auto identity = modern::transfer_market_snapshot_preview(g_, request.manager_slot,
            request.runtime_club, request.preview_snapshot_index);
        if (!identity || identity->displayed_row != request.squad_row || identity->player != request.expected_player ||
            identity->source_slot != request.preview_source_slot || request.preview_seller != request.runtime_club) {
            error = "transfer market: mismatched lender snapshot identity";
            return std::nullopt;
        }
    } else {
        const auto range = squad_range(request.runtime_club);
        if (request.preview_snapshot_index != -1 || request.squad_row < 0 || size_t(request.squad_row) >= range.count ||
            request.preview_source_slot != int(range.first + size_t(request.squad_row)) ||
            request.preview_seller != request.runtime_club) {
            error = "transfer market: mismatched preview roster identity";
            return std::nullopt;
        }
    }
    auto next_rand = rand_;
    market_dispatch_active_ = true;
    struct Reset { bool& active; ~Reset() { active = false; } } reset{market_dispatch_active_};
    try {
        const TransferMarketPreviewRequest input{request.manager_slot, request.runtime_club,
            request.preview_source_slot, request.expected_player, request.preview_seller,
            request.preview_is_purchase, request.preview_is_loan, request.preview_snapshot_index};
        const auto result = modern::quote_transfer_market_preview_source(g_, input, next_rand);
        if (!result.supported) {
            error = result.reason.empty() ? "transfer market: unsupported Source preview" : result.reason;
            return std::nullopt;
        }
        const bool changed = next_rand.seed() != rand_.seed() || next_rand.calls() != rand_.calls();
        const auto next_revision = revision + uint64_t(changed);
        TransferMarketPreviewDispatch dispatch;
        dispatch.preparation = preparation_projection(next_revision);
        if (!dispatch.preparation.transfer_market)
            throw FormatError("transfer market: preview has no ordinary projection");
        dispatch.quote = {request.manager_slot, request.runtime_club, request.preview_source_slot,
            request.expected_player, request.preview_seller, next_revision,
            result.delta, result.projected_balance, request.preview_is_purchase, request.preview_is_loan, request.preview_snapshot_index};
        // Projection construction can throw. Keep the live W stream and revision
        // unchanged until both the Source result and the projection are valid.
        rand_ = next_rand;
        revision = next_revision;
        return dispatch;
    } catch (const std::exception& e) {
        error = e.what();
        return std::nullopt;
    }
}

std::optional<TransferActionResult> ModernSession::dispatch_transfer_market(
    const ui::TransferMarketRequest& request, const HumanBuyerSaleHooks& presenters,
    uint64_t& revision, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return std::nullopt; }
    error.clear();
    if (transfer_offer_editor_ || market_dispatch_active_ || pending_offer_dispatch_active_ || calendar_advance_active_ ||
        request.revision != revision || revision == std::numeric_limits<uint64_t>::max() ||
        request.manager_slot < 0 || request.manager_slot >= 4 ||
        size_t(request.manager_slot) >= g_.manager_teams.size() ||
        !g_.memory.has(::bmh::layout::ActiveManager) || g_.memory.u8(::bmh::layout::ActiveManager) != request.manager_slot ||
        request.runtime_club != g_.manager_teams[size_t(request.manager_slot)]) {
        error = "transfer market: stale or nested request";
        return std::nullopt;
    }
    if (request.kind == ui::TransferMarketRequestKind::submit_bid) {
        if (request.bid.manager != request.manager_slot || request.squad_row != -1 ||
            request.expected_player != -1 ||
            std::any_of(request.bid.bonuses.begin(), request.bid.bonuses.end(),
                        [](uint32_t value) { return value > 255; })) {
            error = "transfer market: invalid buyer request";
            return std::nullopt;
        }
    } else if (request.kind == ui::TransferMarketRequestKind::list_player) {
        const auto range = squad_range(request.runtime_club);
        if (request.squad_row < 0 || size_t(request.squad_row) >= range.count ||
            range.first + size_t(request.squad_row) >= g_.squad_slots.size() ||
            request.expected_player < 0 || request.expected_player >= 0xfff ||
            (g_.squad_slots[range.first + size_t(request.squad_row)] & 0xfff) != request.expected_player) {
            error = "transfer market: stale seller row";
            return std::nullopt;
        }
    } else {
        error = "transfer market: unsupported action";
        return std::nullopt;
    }
    // Reset sends an owned marked player through the same Size2
    // listing for both VER-KAUFEN and VER-LEIHEN. Only a buyer's OpenTransferEditor
    // offer owns the actual loan term; listing does not create a loan record.
    if ((request.offer_is_loan && request.kind != ui::TransferMarketRequestKind::list_player) ||
        request.selected_players.size() > 1) {
        error = "transfer market: this caller does not own buyer loan or marked batch processing";
        return std::nullopt;
    }
    if (!request.selected_players.empty()) {
        const auto& selected = request.selected_players.front();
        const bool matches = request.kind == ui::TransferMarketRequestKind::list_player
            ? selected.squad_row == request.squad_row && selected.player == request.expected_player &&
              selected.seller == request.runtime_club &&
              selected.source_slot == int(squad_range(request.runtime_club).first + size_t(request.squad_row))
            : selected.list_entry == request.bid.list_entry && selected.source_slot == request.bid.source_slot &&
              selected.player == request.bid.player && selected.squad_row == -1;
        if (!matches) {
            error = "transfer market: mismatched marked selection";
            return std::nullopt;
        }
    }

    auto next_state = g_;
    auto next_game = game_;
    auto next_rand = rand_;
    auto hooks = presenters;
    // A presenter cannot supply the allocator gate or insert the special
    // pending caller's severance/ACK/news sequence into a direct purchase.
    switch (turn_driver_.transfer_allocator_context) {
    case PendingOfferAllocatorGate::zero: hooks.allocator_gate = HumanBuyerSaleAllocatorGate::zero; break;
    case PendingOfferAllocatorGate::blocked: hooks.allocator_gate = HumanBuyerSaleAllocatorGate::blocked; break;
    default: hooks.allocator_gate = HumanBuyerSaleAllocatorGate::unsupported; break;
    }
    // Reset -> OpenTransferEditor establishes DeskActions_EditorFail_RuntimeField2=0 at LAB1b67e before
    // resolving the AI seller. This explicitly selected App entry supplies
    // that transient ownership; a missing FBM byte is never the observation.
    hooks.direct_mode = HumanBuyerSaleDirectMode::ordinary_ai_purchase;
    hooks.before_accounts = {};
    hooks.after_paid_accounts = {};
    market_dispatch_active_ = true;
    struct Reset { bool& active; ~Reset() { active = false; } } reset{market_dispatch_active_};
    MinuteReport report;
    try {
        const auto before = modern::save_stream(g_);
        TransferActionResult result;
        if (request.kind == ui::TransferMarketRequestKind::list_player)
            result = modern::list_manager_player(next_state, request.manager_slot, request.squad_row, next_rand, report);
        else
            result = modern::submit_transfer_bid_source_human_buyer(next_state, request.bid, next_rand, hooks, report);
        if (report.unsupported || result.status == TransferActionStatus::unsupported) {
            error = report.reason.empty() ? "transfer market: unsupported Source sale" : report.reason;
            return std::nullopt;
        }
        const bool changed = modern::save_stream(next_state) != before ||
            next_rand.seed() != rand_.seed() || next_rand.calls() != rand_.calls() ||
            next_game.seed() != game_.seed() || next_game.calls() != game_.calls();
        if (changed) {
            // Pure projections reject inconsistent squad/board/training state
            // before any staged notice effects become live.
            (void)ui::preparation_from_game(next_state, exe_, revision + 1);
            g_ = std::move(next_state);
            game_ = next_game;
            rand_ = next_rand;
            ++revision;
        }
        return result;
    } catch (const std::exception& e) {
        error = e.what();
        return std::nullopt;
    }
}

bool ModernSession::finish(ui::CareerInitializationResult& result, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    const bool previous_advance = calendar_advance_active_;
    calendar_advance_active_ = true;
    struct AdvanceReset { bool& active; bool previous; ~AdvanceReset() { active = previous; } }
        advance_reset{calendar_advance_active_, previous_advance};
    MinuteReport report;
    modern::career_managers_end(g_, false);
    const auto season_pause = [this](const char* checkpoint) {
        if (turn_driver_.season_pause) turn_driver_.season_pause(checkpoint, g_);
    };
    if (!modern::career_season_setup(g_, game_, rand_, report, season_pause, turn_driver_.season_cup_draw) ||
        !modern::career_first_steps(g_, game_, rand_, report)) {
        error = report.reason;
        return false;
    }
    // DailyInterest's loop: the event of season day 0 (AdvanceTurn) and the first desk
    std::vector<uint8_t> referees = db_.raw_referee_properties;
    MatchdayContext ctx;
    ctx.month = modern::month_code(g_.date);
    ctx.season_day = g_.season_day();
    ctx.weekday = modern::weekday_code(g_.date);
    ctx.referees = &referees;
    const uint16_t entry = g_.memory.u16(::bmh::layout::CompetitionCalendar + uint32_t(g_.season_day()) * 2);
    std::vector<MatchGroup> groups = modern::prepare_event(g_, entry, ctx, game_, rand_, report);
    modern::enter_event_desk(g_, 0, groups, game_, rand_, report, true, turn_driver_.desk_autosave);
    if (report.unsupported) {
        error = report.reason;
        return false;
    }
    if (!modern::begin_turn_driver(turn_driver_, g_, std::move(groups), entry, db_.raw_referee_properties,
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
    flush_native_autosave();
    return true;
}

TurnAdvanceResult ModernSession::dispatch_kickoff(const ui::DeskRequest& request, uint64_t& revision,
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
    struct KickoffTransaction {
        ModernSession& session;
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
    const auto stop_at_desk = [&](TurnDeskKind kind, std::string reason) {
        transaction.restore();
        return remain_at_desk(kind, std::move(reason));
    };
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
                ? modern::process_national_nomination_pending_after_desk(
                    next_state, pending_request, next_game, next_rand, hooks, pending_report)
                : modern::process_natural_pending_offers_after_desk(
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
    std::optional<AdvertisingSessionCheckpoint> contract_expiry_checkpoint = transaction.before;
    transaction.before.reset(); // claim the completed decision batch before simulation callbacks
    turn_driver_.reality_rules = &exe_;
    turn_driver_.sportschau_source = sportschau_source_options();
    TurnAdvanceResult result = modern::advance_to_next_desk(turn_driver_, g_,
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
    flush_native_autosave();
    return result;
}


bool ModernSession::dispatch_mailbox(const ui::MailboxActionRequest& request, uint64_t& revision,
                                     std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    const int active = int(g_.memory.u8(::bmh::layout::ActiveManager));
    if (active < 0 || active > 3 || size_t(active) >= g_.manager_teams.size() ||
        request.manager_slot != active || request.revision != revision ||
        revision == std::numeric_limits<uint64_t>::max()) {
        error = "Diese Nachricht gehört zu einem veralteten Postfach.";
        return false;
    }
    constexpr size_t entry_size = 0xa0, entries = 40;
    if (request.entry_index < 0 || size_t(request.entry_index) >= entries ||
        g_.mailbox.size() < entries * entry_size) {
        error = "Diese Nachricht ist nicht mehr vorhanden.";
        return false;
    }
    const size_t at = size_t(request.entry_index) * entry_size;
    if (!std::equal(request.expected_entry.begin(), request.expected_entry.end(), g_.mailbox.begin() + at)) {
        error = "Diese Nachricht wurde inzwischen geändert.";
        return false;
    }
    const auto word = [&](size_t p) {
        return uint32_t(g_.mailbox[p]) | uint32_t(g_.mailbox[p + 1]) << 8 |
               uint32_t(g_.mailbox[p + 2]) << 16 | uint32_t(g_.mailbox[p + 3]) << 24;
    };
    const uint32_t bit = uint32_t(1) << active;
    const uint32_t recipients = word(at), kept = word(at + 4);
    if (!(recipients & bit)) {
        error = "Diese Nachricht ist nicht an diesen Manager gerichtet.";
        return false;
    }
    size_t target = at;
    uint32_t value = recipients;
    if (request.kind == ui::MailboxActionKind::toggle_keep) {
        int count = 0;
        for (size_t i = 0; i < entries; ++i)
            if ((word(i * entry_size) & bit) && (word(i * entry_size + 4) & bit)) ++count;
        // MailboxStateFromGame tests (recipient word + 1), NOT the kept word. Preserve
        // its manager-specific five-entry exception, including blocked unkeep.
        if (!((recipients + 1u) & bit) && count >= 5) {
            error = "Die Originalgrenze für gemerkte Nachrichten ist erreicht.";
            return false;
        }
        target += 4; value = kept ^ bit;
    } else if (request.kind == ui::MailboxActionKind::delete_for_manager) {
        if (kept & bit) {
            error = "Diese Nachricht ist noch gemerkt.";
            return false;
        }
        value = recipients ^ bit;
        if (value == 0x80000000u) value = 0;
    } else {
        error = "Unbekannte Postfachaktion.";
        return false;
    }
    for (size_t i = 0; i < 4; ++i) g_.mailbox[target + i] = uint8_t(value >> (8 * i));
    ++revision;
    return true;
}

std::optional<ui::MailboxState> ModernSession::pending_mailbox_notification(uint64_t revision) const {
    if (!g_.memory.has(::bmh::layout::ActiveManager)) return std::nullopt;
    const int manager = int(g_.memory.u8(::bmh::layout::ActiveManager));
    if (manager < 0 || manager >= 4 || size_t(manager) >= g_.manager_teams.size() ||
        g_.manager_teams[size_t(manager)] == no_team() || !g_.memory.has(::bmh::layout::MailboxRecipientMask) ||
        (g_.memory.u32(::bmh::layout::MailboxRecipientMask) & (uint32_t(1) << manager)) == 0)
        return std::nullopt;
    return ui::mailbox_state_from_game(g_, manager, revision);
}

bool ModernSession::acknowledge_mailbox_close(const ui::MailboxCloseRequest& request,
                                               uint64_t revision, std::string& error) {
    if (!g_.memory.has(::bmh::layout::ActiveManager) || request.manager_slot < 0 || request.manager_slot >= 4 ||
        size_t(request.manager_slot) >= g_.manager_teams.size() ||
        request.manager_slot != int(g_.memory.u8(::bmh::layout::ActiveManager)) || request.revision != revision) {
        error = "Diese Nachrichtenansicht gehört zu einem veralteten Manager oder Spielstand.";
        return false;
    }
    // MailboxStateFromGame clears only this manager's MailboxRecipientMask bit on normal exit.
    // The latch lives outside the FBM data blocks and does not advance game revision.
    if (g_.memory.has(::bmh::layout::MailboxRecipientMask)) {
        const uint32_t notifications = g_.memory.u32(::bmh::layout::MailboxRecipientMask);
        const uint32_t bit = uint32_t(1) << request.manager_slot;
        if (notifications & bit) g_.memory.set32(::bmh::layout::MailboxRecipientMask, notifications & ~bit);
    }
    error.clear();
    return true;
}

bool ModernSession::dispatch_settings(const ui::SettingsWriteRequest& request, uint64_t& revision,
                                      std::string& error) {
    if ((live_settings_owner_ && !live_settings_dispatch_active_) ||
        (desk_settings_owner_ && !desk_settings_dispatch_active_)) {
        error = "Diese Einstellung gehört zum geöffneten Einstellungsmenü.";
        return false;
    }
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    auto& mem = g_.memory;
    if (request.manager_slot < 0 || request.manager_slot > 3 ||
        size_t(request.manager_slot) >= g_.manager_teams.size() || !mem.has(::bmh::layout::ActiveManager) ||
        int(mem.u8(::bmh::layout::ActiveManager)) != request.manager_slot || request.revision != revision ||
        revision == std::numeric_limits<uint64_t>::max()) {
        error = "Diese Einstellungsaktion gehört zu einem veralteten Manager oder Stand.";
        return false;
    }
    if (request.row >= 20 || request.column >= 9 || !mem.has(::bmh::layout::LeaguePresentationOptions + 179) ||
        !mem.has(::bmh::layout::GameMode) || !mem.has(::bmh::layout::CompetitionSelection)) {
        error = "Der Einstellungszustand ist unvollständig.";
        return false;
    }
    std::array<uint8_t, 180> raw{};
    for (size_t i = 0; i < raw.size(); ++i) {
        const uint32_t address = ::bmh::layout::LeaguePresentationOptions + uint32_t(i);
        if (!mem.has(address)) { error = "Der Einstellungszustand ist unvollständig."; return false; }
        raw[i] = mem.u8(address);
    }
    if (raw != request.expected_raw) {
        error = "Die Einstellungen wurden inzwischen geändert.";
        return false;
    }
    const size_t index = size_t(request.row) * 9 + request.column;
    const uint32_t address = ::bmh::layout::LeaguePresentationOptions + uint32_t(index);
    ui::SettingsAvailabilityInputs inputs{mem.u8(::bmh::layout::GameMode), mem.u32(::bmh::layout::CompetitionSelection),
        uint8_t(sound_capabilities_ && !sound_capabilities_->music),
        uint8_t(sound_capabilities_ && !sound_capabilities_->effects)};
    auto availability = ui::project_settings_availability(raw, inputs, exe_);
    // The host supplies the portable sound driver's capabilities. Headless
    // callers without that provider retain the unknown Source latch boundary.
    if (!sound_capabilities_) { availability[5] = raw[5]; availability[6] = raw[6]; }
    const bool sound_cell = request.row == 0 && (request.column == 5 || request.column == 6);
    if (sound_cell && !sound_capabilities_) {
        error = "Diese Einstellung ist noch nicht verfügbar.";
        return false;
    }
    const bool scoped_settings = (live_settings_owner_.has_value() && live_settings_dispatch_active_) ||
        (desk_settings_owner_.has_value() && desk_settings_dispatch_active_);
    if (((request.row == 8 && request.column == 7) && !scoped_settings) ||
        (request.column == 7 && request.row >= 14 && request.row <= 19)) {
        error = "Diese Einstellung ist noch nicht verfügbar.";
        return false;
    }
    if ((request.operation != ui::SettingsEdit::toggle_binary &&
         request.operation != ui::SettingsEdit::decrement && request.operation != ui::SettingsEdit::increment) ||
        (request.column < 7 && request.operation != ui::SettingsEdit::toggle_binary) ||
        (request.column >= 7 && request.operation == ui::SettingsEdit::toggle_binary)) {
        error = "Die Aktion passt nicht zum Einstellungsfeld.";
        return false;
    }
    const uint8_t shown = availability[index];
    const uint8_t raw_value = sound_cell && sound_capabilities_ ? uint8_t(raw[index] & 0x7f) : raw[index];
    if (request.column < 7 && ((shown & 0x80) || raw_value >= 2)) {
        error = "Dieses Einstellungsfeld ist im aktuellen Modus gesperrt.";
        return false;
    }
    const uint8_t old = raw_value;
    uint8_t next = old;
    if (request.operation == ui::SettingsEdit::toggle_binary) {
        // Inventory of actual current consumers: direct match-engine buttons,
        // matchday/season flags and the live-scene switch. Other cells are not
        // made writable merely because the Original UI displayed a cell.
        // Tie the write whitelist to the concrete consumer ranges. League
        // remapping reads rows 0..4, columns 0..3; matchday display kinds 1/2
        // map to columns 0..2 and rows 0..4 or 17..19.
        const bool consumed = (sound_cell && sound_capabilities_.has_value()) ||
            (request.row == 0 && request.column == 4) || // A5: match/Sportschau sound
            ((request.row <= 4 && request.column <= 3) ||
            ((request.row <= 4 || request.row >= 17) && request.column <= 2)) ||
            ((request.row == 5 || (request.row >= 9 && request.row <= 12)) && request.column == 0) ||
            (request.row == 1 && request.column == 4) || // B5 live-scene option
            (request.row == 2 && request.column == 4) || // C5: Reality player numbers (RealityMatch_FormatError_ExecutableField)
            (request.row == 5 && request.column == 5) || // F6: Source manager-match desk gate (MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField)
            (request.row == 6 && request.column == 5) || // G6: transfer confirmation wait (OutcomeDelayDisabled)
            (request.row == 16 && request.column == 6) || // Q7: own-club Sportschau preference (MatchdaySetup_ConsiderSportschauFixture_RuntimeField)
            (request.row == 13 && request.column == 6) || // N7: Source kickoff competition list (SportschauAwards_MatchdayIntroView_RuntimeField)
            (request.row == 1 && request.column == 6) || // B7: SportschauIntro intro gate (Controller_FormatError_RuntimeField)
            (request.row == 2 && request.column == 6) || // C7: SportschauIntro intro-animation gate (Controller_FormatError_RuntimeField2)
            (request.row == 3 && request.column == 6) || // D7: selected report progression (ReplayProgressionMode)
            (request.row == 4 && request.column == 6) || // E7: Source-owned Sportschau scenes (ReplayChanceMode)
            (request.row == 5 && request.column == 6) || // F7: SportschauIntro fixture-report gate (Controller_FormatError_RuntimeField3)
            (request.row == 6 && request.column == 6) || // G7: MatchdayIntro player/award rating (SportschauAwards_FinishSportschauAwards_RuntimeField)
            (request.row == 7 && request.column == 6) || // H7: SportschauTeamInfo competition totals (SportschauAwards_SportschauTeamInfoView_RuntimeField)
            (request.row == 2 && request.column == 5) || // C6 drawer-handle behavior (CareerSession_DispatchDesktopLayout_RuntimeField)
            (request.row == 3 && request.column == 5) || // D6 native desktop drag raster (CareerSession_DispatchDesktopLayout_RuntimeField2)
            (request.row == 4 && request.column == 5) || // E6 always-visible shortcut bar (DeskState_FormatError_RuntimeField3)
            (request.row == 7 && request.column == 5) || // H6 transfer option
            (request.row == 8 && (request.column == 5 || request.column == 6)) || // I6/I7
            (request.row >= 10 && request.row <= 13 && request.column == 4) || // K5:N5 match buttons
            (request.row >= 10 && request.row <= 12 && request.column == 6) || // K7:M7 presentation
            (request.row == 9 && request.column == 6) || // J7 season-entrant visibility (SeasonEntrants_SeasonEntrantsFromGame_TextPointer)
            (request.row == 15 && request.column == 4) || // P5
            (request.row == 16 && request.column == 4) || // Q5
            (request.row == 17 && request.column == 4);   // R5: secondary leagues (CalendarState_FormatError_RuntimeField2)
        if (!consumed || (old != 0 && old != 1)) {
            error = "Diese Einstellung ist noch nicht verfügbar.";
            return false;
        }
        if (request.row == 16 && request.column == 4 &&
            g_.season_day() >= 110 && g_.season_day() <= 230) {
            error = ui::original_to_utf8(message_text(g_, ::bmh::layout::CareerSession_DispatchSettings_TextPointer));
            return false;
        }
        // SettingsSourceRefusal only permits re-enabling R5 while CareerSessionRuntimeField4 is zero.
        // LeagueRoundCounters is the saved league_rounds block; CareerSessionRuntimeField4 is its sixth byte.
        // Turning the leagues off remains possible after they have started.
        if (request.row == 17 && request.column == 4 && old != 0 &&
            (g_.league_rounds.size() <= 5 || g_.league_rounds[5] != 0)) {
            error = ui::original_to_utf8(message_text(g_, ::bmh::layout::CareerSession_DispatchSettings_TextPointer2));
            return false;
        }
        next = old ^ 1;
    } else {
        // Column8: existing presentation speeds B8:F8, H8 process budget,
        // J8 postponed limit and K8 autosave. Column9 only exposes values
        // already read by Main's Source live presenters or CupDrawScreen.
        const bool consumed = (request.column == 7 &&
             ((request.row <= 6) || request.row == 7 || request.row == 9 || request.row == 10 ||
             (scoped_settings && request.row == 8))) ||
             // A9 match minutes; B9:C9 chance result; D9:E9 bookings; F9:G9 rolling ball;
             // I9 cup draw; K9 Reality report; M9 preview/shootout; P9 shot.
             (request.column == 8 && ((request.row >= 0 && request.row <= 6) ||
                 request.row == 7 || request.row == 8 || request.row == 10 || request.row == 12 || request.row == 15));
        if (!consumed || old >= 200) {
            error = "Diese Einstellung ist noch nicht verfügbar.";
            return false;
        }
        if (request.column == 8) {
            // SettingsSourceRefusal's ninth column is stored doubled for display. Its
            // Source edit step is two raw units; decrement is enabled only
            // above 1 and increment only below 0xc6 (198). Preserve odd raw
            // values exactly, as the original does.
            if (request.operation == ui::SettingsEdit::decrement) {
                if (old <= 1) { error = "Der Zahlenwert ist bereits am Originalminimum."; return false; }
                next = uint8_t(old - 2);
            } else {
                if (old >= 198) { error = "Der Zahlenwert ist bereits am Originalmaximum."; return false; }
                next = uint8_t(old + 2);
            }
        } else if (request.column == 7) {
            if (request.operation == ui::SettingsEdit::decrement) {
                if (old == 0) { error = "Der Zahlenwert ist bereits am Originalminimum."; return false; }
                next = uint8_t(old - 1);
            } else {
                const uint8_t limit = exe_.u8(::bmh::layout::CareerSession_DispatchSettings_RuntimeField + request.row);
                if (old >= limit) { error = "Der Zahlenwert ist bereits am Originalmaximum."; return false; }
                next = uint8_t(old + 1);
            }
        }
    }
    if (next == old) { error = "Die Einstellung hat sich nicht geändert."; return false; }
    mem.set8(address, next);
    ++revision;
    return true;
}

std::optional<ModernSession::LiveSettingsProjection> ModernSession::open_live_settings(
    const LiveSettingsRequest& request,MatchEngine& engine,uint64_t& revision,std::string& error) {
    error.clear();
    if(!calendar_advance_active_||live_settings_owner_||request.minute<0||request.minute>255||
       request.manager_slot<0||request.manager_slot>=int(g_.manager_teams.size())||
       request.revision!=revision||revision==UINT64_MAX||next_live_settings_scope_==UINT64_MAX||
       &engine.state!=&g_||&engine.game!=&game_||&engine.rand!=&rand_||
       engine.groups.empty()||engine.groups.size()!=turn_driver_.groups.size()||
       !g_.memory.has(::bmh::layout::ActiveManager)||g_.memory.u8(::bmh::layout::ActiveManager)!=request.manager_slot||
       size_t(request.manager_slot)>=g_.manager_teams.size()||
       g_.manager_teams[size_t(request.manager_slot)]!=request.runtime_team||
       request.runtime_team<0||size_t(request.runtime_team)>=g_.team_flags.size()||
       live_overview_owner_||live_intervention_||balance_draft_||balance_dispatch_active_||
       strengths_owner_||strengths_dispatch_active_||league_family_owner_||league_pairings_draft_||
       league_pairings_dispatch_active_||other_games_owner_||other_games_dispatch_active_||
       top_scorers_owner_||top_scorers_dispatch_active_||pending_offer_dispatch_active_||market_dispatch_active_||
       transfer_offer_editor_||manager_join_||contract_quote_||stadium_.investment||stadium_.tip||
       stadium_.periphery_quote||stadium_.block_expansion) {
        error="live settings: stale, nested or unauthenticated Source S request";return {};
    }
    for(size_t i=0;i<engine.groups.size();++i)
        if(engine.groups[i].desc!=turn_driver_.groups[i].desc||engine.groups[i].fixtures!=turn_driver_.groups[i].fixtures) {
            error="live settings: match descriptors changed outside the observer";return {};
        }
    if(!g_.memory.has(::bmh::layout::LeaguePresentationOptions+179)||!g_.memory.has(::bmh::layout::GameMode)||!g_.memory.has(::bmh::layout::CompetitionSelection)) {
        error="live settings: Source switch block is incomplete";return {};
    }
    ModernState staged=g_;
    auto runtime=club_modal_runtime_;
    try {
        std::array<uint8_t,180> raw{};
        for(size_t i=0;i<raw.size();++i) {
            const uint32_t address=::bmh::layout::LeaguePresentationOptions+uint32_t(i);
            if(!staged.memory.has(address))throw FormatError("live settings: Source switch block is incomplete");
            raw[i]=staged.memory.u8(address);
        }
        const auto entry=ui::project_settings_availability(raw,
            {staged.memory.u8(::bmh::layout::GameMode),staged.memory.u32(::bmh::layout::CompetitionSelection),0,0},exe_);
        for(size_t i=0;i<entry.size();++i) {
            // A6/A7 availability is projected from the host's runtime capability.
            // Keep their saved switches intact until an explicit user write.
            const uint8_t value=(i==5||i==6)?raw[i]:entry[i];
            staged.memory.set8(::bmh::layout::LeaguePresentationOptions+uint32_t(i),value);
        }
        source_runtime_set_byte(staged,runtime,::bmh::layout::CareerSession_LiveBoundaryFamilyScope_ExecutableField,0); // SavePathInDirectory, before SettingsSourceRefusal.
        auto state=ui::settings_from_game(staged,request.manager_slot,revision+1,exe_,sound_capabilities_);
        ui::SettingsScreen validate(data_directory_,state);
        std::vector<uint8_t> buttons(180);
        for(size_t i=0;i<buttons.size();++i)buttons[i]=staged.memory.u8(::bmh::layout::LeaguePresentationOptions+uint32_t(i));
        const bool live_desk_disabled=source_runtime_byte(staged,runtime,exe_,::bmh::layout::LiveDeskDisabled)!=0;
        LiveSettingsRequest owner=request;owner.sequence=next_live_settings_scope_;owner.revision=revision+1;
        LiveSettingsOwner next;next.request=owner;next.native=modern::save_stream(staged);
        next.expected_buttons=buttons;next.expected_live_desk_disabled=live_desk_disabled;next.expected_live_scene_visible=engine.live_scene_visible;
        next.expected_runtime=runtime;
        next.rand_seed=rand_.seed();next.game_seed=game_.seed();next.rand_calls=rand_.calls();next.game_calls=game_.calls();
        for(const auto& group:engine.groups)next.active_groups.emplace_back(group.desc,group.fixtures);
        next.referees=engine.referees;
        LiveSettingsProjection projection{owner,std::move(state)};
        g_=std::move(staged);club_modal_runtime_=std::move(runtime);
        engine.buttons=std::move(buttons);engine.live_desk_disabled=live_desk_disabled;
        live_settings_owner_=std::move(next);++next_live_settings_scope_;revision=owner.revision;
        return projection;
    } catch(const std::exception& e) {
        error=e.what();return {};
    }
}

std::optional<ModernSession::LiveSettingsDispatch> ModernSession::dispatch_live_settings(
    const LiveSettingsAction& action,MatchEngine& engine,uint64_t& revision,std::string& error) {
    error.clear();
    if(!calendar_advance_active_||!live_settings_owner_||revision==UINT64_MAX||
       live_settings_owner_->request.revision!=revision||&engine.state!=&g_||
       &engine.game!=&game_||&engine.rand!=&rand_||engine.groups.size()!=live_settings_owner_->active_groups.size()||
       engine.referees!=live_settings_owner_->referees||modern::save_stream(g_)!=live_settings_owner_->native||
       engine.buttons!=live_settings_owner_->expected_buttons||engine.live_desk_disabled!=live_settings_owner_->expected_live_desk_disabled||
       engine.live_scene_visible!=live_settings_owner_->expected_live_scene_visible||club_modal_runtime_!=live_settings_owner_->expected_runtime||
       rand_.seed()!=live_settings_owner_->rand_seed||rand_.calls()!=live_settings_owner_->rand_calls||
       game_.seed()!=live_settings_owner_->game_seed||game_.calls()!=live_settings_owner_->game_calls||
       !g_.memory.has(::bmh::layout::ActiveManager)||g_.memory.u8(::bmh::layout::ActiveManager)!=live_settings_owner_->request.manager_slot||
       size_t(live_settings_owner_->request.manager_slot)>=g_.manager_teams.size()||
       g_.manager_teams[size_t(live_settings_owner_->request.manager_slot)]!=live_settings_owner_->request.runtime_team) {
        error="live settings: stale owner or no longer at the Source observer";return {};
    }
    for(size_t i=0;i<engine.groups.size();++i)
        if(engine.groups[i].desc!=live_settings_owner_->active_groups[i].first||
           engine.groups[i].fixtures!=live_settings_owner_->active_groups[i].second||
           engine.groups[i].desc!=turn_driver_.groups[i].desc||engine.groups[i].fixtures!=turn_driver_.groups[i].fixtures) {
            error="live settings: active Source match list changed";return {};
        }
    const auto current=*live_settings_owner_;
    struct LiveSettingsBoundaryScope {
        bool& active;bool previous;
        explicit LiveSettingsBoundaryScope(bool& value):active(value),previous(value){active=false;}
        ~LiveSettingsBoundaryScope(){active=previous;}
    } boundary(calendar_advance_active_);
    std::optional<ModernState> write_rollback;
    const uint64_t revision_before_action=revision;
    try {
        if(const auto* write=std::get_if<LiveSettingsWrite>(&action)) {
            if(write->owner_sequence!=current.request.sequence||write->write.manager_slot!=current.request.manager_slot||
               write->write.revision!=revision)
                throw FormatError("live settings: write belongs to another Source owner");
            write_rollback=g_;
            live_settings_dispatch_active_=true;
            struct Reset { bool& active;~Reset(){active=false;} } reset{live_settings_dispatch_active_};
            uint64_t staged_revision=revision;
            if(!dispatch_settings(write->write,staged_revision,error))
                throw FormatError(error.empty()?"live settings: Source cell write rejected":error);
            ModernState staged=std::move(g_);
            g_=*write_rollback;
            std::array<uint8_t,180> raw{};
            for(size_t i=0;i<raw.size();++i)raw[i]=staged.memory.u8(::bmh::layout::LeaguePresentationOptions+uint32_t(i));
            const auto projected=ui::project_settings_availability(raw,
                {staged.memory.u8(::bmh::layout::GameMode),staged.memory.u32(::bmh::layout::CompetitionSelection),0,0},exe_);
            for(size_t i=0;i<projected.size();++i)
                if(i!=5&&i!=6)staged.memory.set8(::bmh::layout::LeaguePresentationOptions+uint32_t(i),projected[i]);
            std::vector<uint8_t> buttons(180);
            for(size_t i=0;i<buttons.size();++i)buttons[i]=staged.memory.u8(::bmh::layout::LeaguePresentationOptions+uint32_t(i));
            const bool live_desk_disabled=source_runtime_byte(staged,club_modal_runtime_,exe_,::bmh::layout::LiveDeskDisabled)!=0;
            auto state=ui::settings_from_game(staged,current.request.manager_slot,staged_revision,exe_,sound_capabilities_);
            ui::SettingsScreen validate(data_directory_,state);
            LiveSettingsOwner updated=current;
            updated.request.revision=staged_revision;updated.native=modern::save_stream(staged);
            updated.expected_buttons=buttons;updated.expected_live_desk_disabled=live_desk_disabled;
            updated.expected_live_scene_visible=engine.live_scene_visible;updated.expected_runtime=club_modal_runtime_;
            updated.rand_seed=rand_.seed();updated.game_seed=game_.seed();
            updated.rand_calls=rand_.calls();updated.game_calls=game_.calls();
            LiveSettingsDispatch result{updated.request,false,std::move(state)};
            g_=std::move(staged);revision=staged_revision;
            engine.buttons=std::move(buttons);engine.live_desk_disabled=live_desk_disabled;
            live_settings_owner_=std::move(updated);
            write_rollback.reset();
            return result;
        }
        if(const auto* file=std::get_if<LiveSettingsFile>(&action)) {
            if(file->owner_sequence!=current.request.sequence||file->request.manager_slot!=current.request.manager_slot||
               file->request.origin_item_id!=-1||file->request.revision!=revision||revision==UINT64_MAX)
                throw FormatError("live settings: file request belongs to another owner");
            ModernState staged=g_;bool loaded=false;
            if(!apply_settings_file(file->request,current.request.manager_slot,-1,staged,loaded,error))
                throw FormatError(error.empty()?"live settings: .EIN request rejected":error);
            auto state=ui::settings_from_game(staged,current.request.manager_slot,revision+1,exe_,sound_capabilities_);
            ui::SettingsScreen validate(data_directory_,state);
            std::vector<uint8_t> buttons(180);
            for(size_t i=0;i<buttons.size();++i)buttons[i]=staged.memory.u8(::bmh::layout::LeaguePresentationOptions+uint32_t(i));
            const bool live_desk_disabled=source_runtime_byte(staged,club_modal_runtime_,exe_,::bmh::layout::LiveDeskDisabled)!=0;
            LiveSettingsOwner updated=current;updated.request.revision=revision+1;
            updated.native=modern::save_stream(staged);updated.expected_buttons=buttons;
            updated.expected_live_desk_disabled=live_desk_disabled;updated.expected_live_scene_visible=engine.live_scene_visible;
            updated.expected_runtime=club_modal_runtime_;
            LiveSettingsDispatch result{updated.request,false,std::move(state)};
            g_=std::move(staged);engine.buttons=std::move(buttons);engine.live_desk_disabled=live_desk_disabled;
            live_settings_owner_=std::move(updated);++revision;
            return result;
        }
        const auto& close=std::get<LiveSettingsCloseRequest>(action);
        if(close.owner!=current.request||close.owner.sequence!=current.request.sequence||close.owner.revision!=revision)
            throw FormatError("live settings: stale or mismatched Source close");
        if(revision==UINT64_MAX)throw FormatError("live settings: revision exhausted");
        ModernState next=g_;auto runtime=club_modal_runtime_;
        std::array<uint8_t,180> raw{};
        for(size_t i=0;i<raw.size();++i)raw[i]=next.memory.u8(::bmh::layout::LeaguePresentationOptions+uint32_t(i));
        next.live_scene=uint8_t(1-(raw[13]&1))!=0; // SettingsSourceRefusal: LiveSceneVisible=1-(CareerSession_DispatchLiveTactic_RuntimeField3&1).
        source_runtime_set_byte(next,runtime,::bmh::layout::CareerSession_DispatchLiveSubstitution_ExecutableField,next.memory.u8(::bmh::layout::CareerSession_FormatError_RuntimeField2)); // Source max-substitution latch.
        uint32_t code=0;
        for(uint32_t i=0;i<6;++i)code=code*10+next.memory.u8(::bmh::layout::CareerSession_FormatError_RuntimeField + (i+14)*9);
        for(uint32_t i=0;i<4;++i)source_runtime_set_byte(next,runtime,::bmh::layout::CareerSession_FormatError_RuntimeField9+i,uint8_t(code>>(8*i)));
        source_runtime_set_byte(next,runtime,::bmh::layout::CareerSession_FormatError_ExecutableField8,1); // EndPhase requests its normal redraw on return.
        auto state=ui::settings_from_game(next,current.request.manager_slot,revision+1,exe_,sound_capabilities_);
        ui::SettingsScreen validate(data_directory_,state);
        std::vector<uint8_t> buttons(180);
        for(size_t i=0;i<buttons.size();++i)buttons[i]=next.memory.u8(::bmh::layout::LeaguePresentationOptions+uint32_t(i));
        const bool live_desk_disabled=source_runtime_byte(next,runtime,exe_,::bmh::layout::LiveDeskDisabled)!=0;
        LiveSettingsDispatch result;result.owner=current.request;result.owner.revision=revision+1;
        result.closed=true;result.state=std::move(state);
        g_=std::move(next);club_modal_runtime_=std::move(runtime);
        engine.buttons=std::move(buttons);engine.live_desk_disabled=live_desk_disabled;engine.live_scene_visible=g_.live_scene;
        live_settings_owner_.reset();++revision;return result;
    } catch(const std::exception& e) {
        if(write_rollback) {
            g_=std::move(*write_rollback);revision=revision_before_action;
            live_settings_owner_=current;
        }
        error=e.what();return {};
    }
}

ModernSession::DeskSettingsCheckpoint ModernSession::desk_settings_checkpoint(uint64_t revision) const {
    return {g_,club_modal_runtime_,desk_settings_owner_ ?
        std::optional<DeskSettingsOwner>(desk_settings_owner_->owner) : std::nullopt,
        desk_settings_owner_ ? desk_settings_owner_->initial_h8 : uint8_t(0),
        next_desk_settings_scope_,revision};
}

bool ModernSession::apply_settings_file(const ui::SaveLoadRequest& request,int manager_slot,int origin_item_id,
                                       ModernState& staged,bool& loaded,std::string& error) const {
    constexpr size_t kBytes=180;
    loaded=false;
    if(request.file_kind!=ui::SaveLoadFileKind::settings||request.manager_slot!=manager_slot||
       request.origin_item_id!=origin_item_id||request.path.filename().empty()) {
        error="invalid_settings_file_owner";return false;
    }
    std::string extension=request.path.extension().string();
    std::transform(extension.begin(),extension.end(),extension.begin(),
        [](unsigned char c){return char(std::tolower(c));});
    if (extension != (content::find(data_directory_) ? ".obsettings" : ".ein")) {
        error = "invalid_settings_file_extension";
        return false;
    }
    const uint32_t base=::bmh::layout::LeaguePresentationOptions;
    if(!staged.memory.has(base+uint32_t(kBytes-1))) {error="settings_file_memory_unavailable";return false;}
    std::error_code ec;
    const auto status=std::filesystem::symlink_status(request.path,ec);
    if(!ec&&std::filesystem::is_symlink(status)) {error="settings_file_target_is_symlink";return false;}
    if(request.action==ui::SaveLoadAction::load) {
        Bytes bytes;
        try {
            bytes = content::find(data_directory_)
                        ? content::load_preferences(request.path, "settings", kBytes)
                        : read_file(request.path);
        } catch (const std::exception &) {
            error = "settings_file_read_failed";
            return false;
        }
        if(bytes.size()!=kBytes) {error="settings_file_wrong_size";return false;}
        for(size_t i=0;i<kBytes;++i)staged.memory.set8(base+uint32_t(i),bytes[i]);
        std::array<uint8_t,kBytes> raw{};
        for(size_t i=0;i<kBytes;++i)raw[i]=bytes[i];
        const auto availability=ui::project_settings_availability(raw,
            {staged.memory.u8(::bmh::layout::GameMode),staged.memory.u32(::bmh::layout::CompetitionSelection),0,0},exe_);
        for(size_t i=0;i<kBytes;++i)
            if(i!=5&&i!=6)staged.memory.set8(base+uint32_t(i),availability[i]);
        loaded=true;error.clear();return true;
    }
    if(request.action!=ui::SaveLoadAction::save) {error="invalid_settings_file_action";return false;}
    if(!std::filesystem::is_directory(request.path.parent_path(),ec)||ec) {
        error="settings_file_directory_unavailable";return false;
    }
    ec.clear();const bool exists=std::filesystem::exists(request.path,ec);
    if(ec||(exists&&!request.overwrite)) {
        error=exists?"settings_file_overwrite_confirmation_required":"settings_file_target_unavailable";
        return false;
    }
    Bytes bytes(kBytes);
    for(size_t i=0;i<kBytes;++i)bytes[i]=staged.memory.u8(base+uint32_t(i));
    try {
        if (content::find(data_directory_)) {
            content::save_preferences(request.path, "settings", bytes, request.overwrite);
            error.clear();
            return true;
        }
        std::ofstream file(request.path,std::ios::binary|std::ios::trunc);
        if(!file) {error="settings_file_write_failed";return false;}
        file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));file.flush();
        if(!file) {error="settings_file_write_failed";return false;}
    } catch(const std::exception&) {error="settings_file_write_failed";return false;}
    error.clear();return true;
}

void ModernSession::restore_desk_settings_checkpoint(DeskSettingsCheckpoint checkpoint,uint64_t& revision) {
    g_=std::move(checkpoint.state);club_modal_runtime_=std::move(checkpoint.runtime);
    desk_settings_owner_.reset();
    if(checkpoint.owner) {
        DeskSettingsOwnerState restored;restored.owner=*checkpoint.owner;
        restored.initial_h8=checkpoint.initial_h8;restored.native=modern::save_stream(g_);
        restored.expected_runtime=club_modal_runtime_;restored.rand_seed=rand_.seed();
        restored.game_seed=game_.seed();restored.rand_calls=rand_.calls();restored.game_calls=game_.calls();
        desk_settings_owner_=std::move(restored);
    }
    next_desk_settings_scope_=checkpoint.next_scope;revision=checkpoint.revision;
}

std::optional<ModernSession::DeskSettingsProjection> ModernSession::open_desk_settings(
    const ui::DeskRequest& request,uint64_t& revision,std::string& error) {
    error.clear();
    if(request.kind!=ui::DeskRequestKind::open_menu||request.menu_kind!=19||request.value!=-1||
       request.item_id<0||request.item_id>=54||request.revision!=revision||revision==UINT64_MAX||
       next_desk_settings_scope_==UINT64_MAX||request.manager_slot<0||request.manager_slot>3||
       !g_.memory.has(::bmh::layout::ActiveManager)||g_.memory.u8(::bmh::layout::ActiveManager)!=request.manager_slot||
       size_t(request.manager_slot)>=g_.manager_teams.size()||
       live_settings_owner_||desk_settings_owner_||live_overview_owner_||live_intervention_||
       balance_draft_||balance_dispatch_active_||strengths_owner_||strengths_dispatch_active_||
       league_family_owner_||league_family_teaminfo_active_||league_pairings_draft_||
       league_pairings_dispatch_active_||other_games_owner_||other_games_dispatch_active_||
       top_scorers_owner_||top_scorers_dispatch_active_||opportunities_owner_||opportunities_dispatch_active_||
       pending_offer_dispatch_active_||market_dispatch_active_||transfer_offer_editor_||manager_join_||
       contract_quote_||advertising_renewal_dispatch_active_||stadium_.investment||stadium_.tip||
       stadium_.periphery_quote||stadium_.block_expansion||
       calendar_advance_active_||career_complete()) {
        error="desk settings: stale, nested or unauthenticated menu-19 request";return {};
    }
    try {
        const auto desk=desktop_projection(revision);
        const auto source=std::find_if(desk.items.begin(),desk.items.end(),[&](const ui::DeskItem& item) {
            return item.id==request.item_id;
        });
        if(source==desk.items.end()||source->kind!=19||source->disabled||desk.manager_slot!=request.manager_slot) {
            error="desk settings: menu-19 source item is not currently enabled";return {};
        }
        if(!g_.memory.has(::bmh::layout::LeaguePresentationOptions+179)||!g_.memory.has(::bmh::layout::GameMode)||!g_.memory.has(::bmh::layout::CompetitionSelection)) {
            error="desk settings: Source settings block is incomplete";return {};
        }
        ModernState staged=g_;auto runtime=club_modal_runtime_;
        std::array<uint8_t,180> raw{};
        for(size_t i=0;i<raw.size();++i) {
            const uint32_t address=::bmh::layout::LeaguePresentationOptions+uint32_t(i);
            if(!staged.memory.has(address))throw FormatError("desk settings: Source settings block is incomplete");
            raw[i]=staged.memory.u8(address);
        }
        const auto projected=ui::project_settings_availability(raw,
            {staged.memory.u8(::bmh::layout::GameMode),staged.memory.u32(::bmh::layout::CompetitionSelection),0,0},exe_);
        for(size_t i=0;i<projected.size();++i)
            staged.memory.set8(::bmh::layout::LeaguePresentationOptions+uint32_t(i),(i==5||i==6)?raw[i]:projected[i]);
        auto state=ui::settings_from_game(staged,request.manager_slot,revision+1,exe_,sound_capabilities_);
        ui::SettingsScreen validate(data_directory_,state);
        DeskSettingsOwner owner{next_desk_settings_scope_,revision+1,request.manager_slot,request.item_id};
        DeskSettingsOwnerState next;next.owner=owner;next.initial_h8=raw[7*9+7];next.native=modern::save_stream(staged);
        next.expected_runtime=runtime;next.rand_seed=rand_.seed();next.game_seed=game_.seed();
        next.rand_calls=rand_.calls();next.game_calls=game_.calls();
        DeskSettingsProjection result{owner,std::move(state)};
        g_=std::move(staged);club_modal_runtime_=std::move(runtime);
        desk_settings_owner_=std::move(next);++next_desk_settings_scope_;++revision;
        return result;
    } catch(const std::exception& e) {error=e.what();return {};}
}

std::optional<ModernSession::DeskSettingsDispatch> ModernSession::dispatch_desk_settings(
    const DeskSettingsAction& action,uint64_t& revision,std::string& error) {
    error.clear();
    if(!desk_settings_owner_||revision==UINT64_MAX||desk_settings_owner_->owner.revision!=revision||
       modern::save_stream(g_)!=desk_settings_owner_->native||club_modal_runtime_!=desk_settings_owner_->expected_runtime||
       rand_.seed()!=desk_settings_owner_->rand_seed||rand_.calls()!=desk_settings_owner_->rand_calls||
       game_.seed()!=desk_settings_owner_->game_seed||game_.calls()!=desk_settings_owner_->game_calls||
       !g_.memory.has(::bmh::layout::ActiveManager)||g_.memory.u8(::bmh::layout::ActiveManager)!=desk_settings_owner_->owner.manager_slot) {
        error="desk settings: stale owner or changed session";return {};
    }
    const auto current=*desk_settings_owner_;
    const uint64_t revision_before=revision;
    std::optional<ModernState> rollback;
    try {
        if(const auto* write=std::get_if<DeskSettingsWrite>(&action)) {
            if(write->owner_sequence!=current.owner.sequence||write->write.manager_slot!=current.owner.manager_slot||
               write->write.revision!=revision)throw FormatError("desk settings: write belongs to another owner");
            rollback=g_;desk_settings_dispatch_active_=true;
            struct Reset { bool& active;~Reset(){active=false;} } reset{desk_settings_dispatch_active_};
            uint64_t staged_revision=revision;
            if(!dispatch_settings(write->write,staged_revision,error))
                throw FormatError(error.empty()?"desk settings: Source write rejected":error);
            ModernState staged=std::move(g_);g_=*rollback;
            std::array<uint8_t,180> raw{};
            for(size_t i=0;i<raw.size();++i)raw[i]=staged.memory.u8(::bmh::layout::LeaguePresentationOptions+uint32_t(i));
            const auto projected=ui::project_settings_availability(raw,
                {staged.memory.u8(::bmh::layout::GameMode),staged.memory.u32(::bmh::layout::CompetitionSelection),0,0},exe_);
            for(size_t i=0;i<projected.size();++i)
                if(i!=5&&i!=6)staged.memory.set8(::bmh::layout::LeaguePresentationOptions+uint32_t(i),projected[i]);
            auto state=ui::settings_from_game(staged,current.owner.manager_slot,staged_revision,exe_,sound_capabilities_);
            ui::SettingsScreen validate(data_directory_,state);
            DeskSettingsOwnerState updated=current;updated.owner.revision=staged_revision;
            updated.native=modern::save_stream(staged);updated.expected_runtime=club_modal_runtime_;
            updated.rand_seed=rand_.seed();updated.game_seed=game_.seed();
            updated.rand_calls=rand_.calls();updated.game_calls=game_.calls();
            DeskSettingsDispatch result{updated.owner,false,std::move(state)};
            g_=std::move(staged);revision=staged_revision;desk_settings_owner_=std::move(updated);
            rollback.reset();return result;
        }
        if(const auto* file=std::get_if<DeskSettingsFile>(&action)) {
            if(file->owner_sequence!=current.owner.sequence||file->request.manager_slot!=current.owner.manager_slot||
               file->request.revision!=revision||file->request.origin_item_id!=current.owner.source_item_id||
               revision==UINT64_MAX)
                throw FormatError("desk settings: file request belongs to another owner");
            ModernState staged=g_;bool loaded=false;
            if(!apply_settings_file(file->request,current.owner.manager_slot,current.owner.source_item_id,
                                    staged,loaded,error))
                throw FormatError(error.empty()?"desk settings: .EIN request rejected":error);
            auto state=ui::settings_from_game(staged,current.owner.manager_slot,revision+1,exe_,sound_capabilities_);
            ui::SettingsScreen validate(data_directory_,state);
            DeskSettingsOwnerState updated=current;updated.owner.revision=revision+1;
            updated.native=modern::save_stream(staged);updated.expected_runtime=club_modal_runtime_;
            updated.rand_seed=rand_.seed();updated.game_seed=game_.seed();
            updated.rand_calls=rand_.calls();updated.game_calls=game_.calls();
            DeskSettingsDispatch result{updated.owner,false,std::move(state)};
            g_=std::move(staged);desk_settings_owner_=std::move(updated);++revision;
            return result;
        }
        const auto& close=std::get<DeskSettingsClose>(action);
        if(close.owner!=current.owner||close.owner.revision!=revision)
            throw FormatError("desk settings: stale close request");
        ModernState next=g_;auto runtime=club_modal_runtime_;
        std::array<uint8_t,180> raw{};
        for(size_t i=0;i<raw.size();++i)raw[i]=next.memory.u8(::bmh::layout::LeaguePresentationOptions+uint32_t(i));
        const uint8_t new_h8=raw[7*9+7];
        if(new_h8==0) {
            source_runtime_set_byte(next,runtime,::bmh::layout::CareerSession_FormatError_ExecutableField2,1);
            source_runtime_set_byte(next,runtime,::bmh::layout::CareerSession_FormatError_ExecutableField2+1,0);
            source_runtime_set_byte(next,runtime,::bmh::layout::CareerSession_FormatError_ExecutableField2+2,0);
            source_runtime_set_byte(next,runtime,::bmh::layout::CareerSession_FormatError_ExecutableField2+3,0);
        } else if(current.initial_h8!=0) {
            const int16_t delta=static_cast<int16_t>(int(new_h8)-int(current.initial_h8));
            const uint32_t remaining=source_runtime_u32(next,runtime,exe_,::bmh::layout::CareerSession_FormatError_ExecutableField2);
            const uint32_t adjusted=remaining+uint32_t(int32_t(delta)*60);
            for(uint32_t i=0;i<4;++i)
                source_runtime_set_byte(next,runtime,::bmh::layout::CareerSession_FormatError_ExecutableField2+i,uint8_t(adjusted>>(8*i)));
        }
        next.live_scene=uint8_t(1-(raw[13]&1))!=0;
        source_runtime_set_byte(next,runtime,::bmh::layout::CareerSession_DispatchLiveSubstitution_ExecutableField,next.memory.u8(::bmh::layout::CareerSession_FormatError_RuntimeField2));
        uint32_t code=0;
        for(uint32_t i=0;i<6;++i)code=code*10+next.memory.u8(::bmh::layout::CareerSession_FormatError_RuntimeField + (i+14)*9);
        for(uint32_t i=0;i<4;++i)source_runtime_set_byte(next,runtime,::bmh::layout::CareerSession_FormatError_RuntimeField9+i,uint8_t(code>>(8*i)));
        auto state=ui::settings_from_game(next,current.owner.manager_slot,revision+1,exe_,sound_capabilities_);
        ui::SettingsScreen validate(data_directory_,state);
        DeskSettingsDispatch result{current.owner,true,std::move(state)};result.owner.revision=revision+1;
        g_=std::move(next);club_modal_runtime_=std::move(runtime);desk_settings_owner_.reset();++revision;
        return result;
    } catch(const std::exception& e) {
        if(rollback) {g_=std::move(*rollback);revision=revision_before;desk_settings_owner_=current;}
        error=e.what();return {};
    }
}

std::optional<AdvertisingRenewal> ModernSession::check_advertising_renewal(int manager_slot, int display_slot,
                                                                         uint64_t& revision, std::string& error) {
    error.clear();
    if (advertising_renewal_dispatch_active_) {
        error = "advertising_renewal_active";
        return std::nullopt;
    }
    if (!g_.memory.has(::bmh::layout::ActiveManager) || manager_slot < 0 || size_t(manager_slot) >= g_.manager_teams.size() ||
        int(g_.memory.u8(::bmh::layout::ActiveManager)) != manager_slot || revision == std::numeric_limits<uint64_t>::max()) {
        error = "Diese Werbeaktion ist nicht mehr aktuell.";
        return std::nullopt;
    }
    AdvertisingRenewal renewal = modern::check_advertising_renewal(g_, manager_slot, display_slot, rand_);
    if (renewal.status == AdvertisingRenewal::Status::invalid) {
        error = "Diese Werbefläche gibt es nicht.";
        return std::nullopt;
    }
    if (renewal.status != AdvertisingRenewal::Status::not_expired) ++revision;
    return renewal;
}

bool ModernSession::answer_advertising_renewal(int manager_slot, int contract_slot, bool renew, uint64_t& revision,
                                               std::string& error) {
    error.clear();
    if (advertising_renewal_dispatch_active_) {
        error = "advertising_renewal_active";
        return false;
    }
    if (!g_.memory.has(::bmh::layout::ActiveManager) || manager_slot < 0 || size_t(manager_slot) >= g_.manager_teams.size() ||
        int(g_.memory.u8(::bmh::layout::ActiveManager)) != manager_slot || revision == std::numeric_limits<uint64_t>::max() ||
        !modern::answer_advertising_renewal(g_, manager_slot, contract_slot, renew)) {
        error = "Diese Vertragsverlängerung ist nicht mehr aktuell.";
        return false;
    }
    ++revision;
    return true;
}



ModernSession::AdvertisingSessionCheckpoint ModernSession::advertising_session_checkpoint(uint64_t revision) const {
    AdvertisingSessionCheckpoint c;
    c.state=g_; c.game=game_; c.rand=rand_; c.driver=turn_driver_;
    c.desk_callargs=source_desk_callargs_; c.saved_turn_error=saved_turn_error_;
    c.completed_results=completed_results_; c.lineup_manager=lineup_entry_manager_;
    c.lineup_club=lineup_entry_club_; c.managers=managers_; c.stock_tip=last_stock_tip_;
    c.property_notice=last_property_notice_; c.credit_notice=last_credit_notice_;
    c.stadium=stadium_; c.next_stadium_scope=next_stadium_scope_;
    c.contract_quote=contract_quote_; c.next_contract_quote_id=next_contract_quote_id_;
    c.last_contract_request_id=last_contract_request_id_; c.options_applied=options_applied_;
    c.pending_offer_active=pending_offer_dispatch_active_; c.market_active=market_dispatch_active_;
    c.pairings_active=league_pairings_dispatch_active_; c.family_teaminfo_active=league_family_teaminfo_active_;
    c.strengths_active=strengths_dispatch_active_; c.top_scorers_active=top_scorers_dispatch_active_;
    c.other_games_active=other_games_dispatch_active_; c.balance_active=balance_dispatch_active_;
    c.calendar_active=calendar_advance_active_; c.next_other_games_scope=next_other_games_scope_;
    c.other_games_owner=other_games_owner_; c.other_games_selection=other_games_selection_;
    c.next_balance_scope=next_balance_scope_; c.balance=balance_draft_;
    c.next_strengths_scope=next_strengths_scope_; c.strengths=strengths_owner_;
    c.family=league_family_owner_; c.next_family_id=next_league_family_id_;
    c.next_top_scorers_scope=next_top_scorers_scope_; c.top_scorers=top_scorers_owner_;
    c.pairings_scope=league_pairings_scope_; c.pairings=league_pairings_draft_;
    c.transfer_editor=transfer_offer_editor_; c.poach=poach_;
    c.transfer_offer_revision=transfer_offer_revision_; c.pending_hooks=pending_offer_hooks_;
    c.pending_policy=pending_offer_entry_policy_; c.runtime=club_modal_runtime_;
    c.last_request_id=last_advertising_request_id_; c.manager_join=manager_join_;
    c.revision=revision; c.session_id=session_id_;
    return c;
}

void ModernSession::restore_advertising_session_checkpoint(AdvertisingSessionCheckpoint c) {
    g_=std::move(c.state); game_=c.game; rand_=c.rand; turn_driver_=std::move(c.driver);
    source_desk_callargs_=std::move(c.desk_callargs); saved_turn_error_=std::move(c.saved_turn_error);
    completed_results_=std::move(c.completed_results); lineup_entry_manager_=c.lineup_manager;
    lineup_entry_club_=c.lineup_club; managers_=std::move(c.managers); last_stock_tip_=c.stock_tip;
    last_property_notice_=std::move(c.property_notice); last_credit_notice_=std::move(c.credit_notice);
    stadium_=std::move(c.stadium); next_stadium_scope_=c.next_stadium_scope;
    contract_quote_=std::move(c.contract_quote); next_contract_quote_id_=c.next_contract_quote_id;
    last_contract_request_id_=c.last_contract_request_id; options_applied_=c.options_applied;
    pending_offer_dispatch_active_=c.pending_offer_active; market_dispatch_active_=c.market_active;
    league_pairings_dispatch_active_=c.pairings_active; league_family_teaminfo_active_=c.family_teaminfo_active;
    strengths_dispatch_active_=c.strengths_active; top_scorers_dispatch_active_=c.top_scorers_active;
    other_games_dispatch_active_=c.other_games_active; balance_dispatch_active_=c.balance_active;
    calendar_advance_active_=c.calendar_active; next_other_games_scope_=c.next_other_games_scope;
    other_games_owner_=std::move(c.other_games_owner); other_games_selection_=c.other_games_selection;
    next_balance_scope_=c.next_balance_scope; balance_draft_=std::move(c.balance);
    next_strengths_scope_=c.next_strengths_scope; strengths_owner_=std::move(c.strengths);
    league_family_owner_=std::move(c.family); next_league_family_id_=c.next_family_id;
    next_top_scorers_scope_=c.next_top_scorers_scope; top_scorers_owner_=std::move(c.top_scorers);
    league_pairings_scope_=c.pairings_scope; league_pairings_draft_=std::move(c.pairings);
    transfer_offer_editor_=std::move(c.transfer_editor); poach_=std::move(c.poach);
    transfer_offer_revision_=c.transfer_offer_revision; pending_offer_hooks_=std::move(c.pending_hooks);
    pending_offer_entry_policy_=c.pending_policy; club_modal_runtime_=std::move(c.runtime);
    last_advertising_request_id_=c.last_request_id; manager_join_=std::move(c.manager_join);
}

bool ModernSession::advertising_session_unchanged(const AdvertisingSessionCheckpoint& c,
                                                  uint64_t revision) const {
    const auto byte_same=[&](uint32_t at) {
        const bool live_has=g_.memory.has(at), saved_has=c.state.memory.has(at);
        return live_has==saved_has && (!live_has || g_.memory.u8(at)==c.state.memory.u8(at));
    };
    const bool manager_ids_same = managers_.size()==c.managers.size() &&
        std::equal(managers_.begin(),managers_.end(),c.managers.begin(),[](const auto& a,const auto& b) {
            return a.slot==b.slot && a.master_club==b.master_club && a.runtime_club==b.runtime_club;
        });
    return modern::save_stream(g_)==modern::save_stream(c.state) && same_strengths_runtime_state(g_,c.state) &&
        g_.manager_teams==c.state.manager_teams && byte_same(::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField) && byte_same(::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField) &&
        rand_.seed()==c.rand.seed() && rand_.calls()==c.rand.calls() &&
        game_.seed()==c.game.seed() && game_.calls()==c.game.calls() &&
        turn_driver_.stage==c.driver.stage && source_desk_callargs_==c.desk_callargs &&
        lineup_entry_manager_==c.lineup_manager && lineup_entry_club_==c.lineup_club && manager_ids_same &&
        session_id_==c.session_id && last_advertising_request_id_==c.last_request_id && revision==c.revision &&
        club_modal_runtime_==c.runtime &&
        pending_offer_dispatch_active_==c.pending_offer_active && market_dispatch_active_==c.market_active &&
        league_pairings_dispatch_active_==c.pairings_active && league_family_teaminfo_active_==c.family_teaminfo_active &&
        strengths_dispatch_active_==c.strengths_active && top_scorers_dispatch_active_==c.top_scorers_active &&
        other_games_dispatch_active_==c.other_games_active && balance_dispatch_active_==c.balance_active &&
        calendar_advance_active_==c.calendar_active &&
        bool(other_games_owner_)==bool(c.other_games_owner) && bool(balance_draft_)==bool(c.balance) &&
        bool(strengths_owner_)==bool(c.strengths) && bool(league_family_owner_)==bool(c.family) &&
        bool(top_scorers_owner_)==bool(c.top_scorers) && bool(league_pairings_draft_)==bool(c.pairings) &&
        bool(transfer_offer_editor_)==bool(c.transfer_editor) && bool(poach_)==bool(c.poach) &&
        bool(manager_join_)==bool(c.manager_join) && bool(stadium_.opened)==bool(c.stadium.opened) &&
        bool(contract_quote_)==bool(c.contract_quote) &&
        next_other_games_scope_==c.next_other_games_scope && next_balance_scope_==c.next_balance_scope &&
        next_strengths_scope_==c.next_strengths_scope && next_league_family_id_==c.next_family_id &&
        next_top_scorers_scope_==c.next_top_scorers_scope && league_pairings_scope_==c.pairings_scope &&
        next_stadium_scope_==c.next_stadium_scope && next_contract_quote_id_==c.next_contract_quote_id &&
        last_contract_request_id_==c.last_contract_request_id && transfer_offer_revision_==c.transfer_offer_revision;
}

std::optional<AdvertisingRenewal> ModernSession::dispatch_advertising_renewal(
    const ui::AdvertisingRequest& request, uint64_t& revision, std::string& error,
    const AdvertisingRenewalPresenter& presenter) {
    error.clear();
    const auto competing_owner = [&] {
        return pending_offer_dispatch_active_ || market_dispatch_active_ || league_pairings_dispatch_active_ ||
            league_family_teaminfo_active_ || strengths_dispatch_active_ || top_scorers_dispatch_active_ ||
            other_games_dispatch_active_ || balance_dispatch_active_ || calendar_advance_active_ ||
            balance_draft_.has_value() || strengths_owner_.has_value() || top_scorers_owner_.has_value() ||
            other_games_owner_.has_value() || league_family_owner_.has_value() || league_pairings_draft_.has_value() ||
            manager_join_.has_value() || transfer_offer_editor_.has_value() || poach_.has_value() ||
            stadium_.opened || stadium_.investment.has_value() || stadium_.tip.has_value() ||
            stadium_.periphery_quote.has_value() || stadium_.selection_status.has_value() ||
            stadium_.block_expansion.has_value() || contract_quote_.has_value() ||
            (turn_driver_.stage!=TurnDriver::Stage::MatchdayDesks &&
             turn_driver_.stage!=TurnDriver::Stage::SeasonEndDesk);
    };
    if (advertising_renewal_dispatch_active_ || competing_owner()) {
        error = "Eine andere Session-Aktion oder ein konkurrierender Screen ist aktiv.";
        return std::nullopt;
    }
    if (!presenter || request.kind != ui::AdvertisingActionKind::check_renewal ||
        request.request_id == 0 || request.request_id <= last_advertising_request_id_ ||
        request.manager_slot < 0 || request.manager_slot > 3 ||
        !g_.memory.has(::bmh::layout::ActiveManager) || int(g_.memory.u8(::bmh::layout::ActiveManager)) != request.manager_slot ||
        size_t(request.manager_slot) >= g_.manager_teams.size() ||
        request.runtime_club != g_.manager_teams[size_t(request.manager_slot)] ||
        request.revision != revision || revision == std::numeric_limits<uint64_t>::max() ||
        request.renewal_display_slot < 0 || request.renewal_display_slot > 15 ||
        request.renewal_contract_slot < 0 || request.renewal_contract_slot > 15 ||
        request.offer != AdvertisingOfferRequest{} || request.spending != AdvertisingSpendingRequest{}) {
        error = "Diese Werbeaktion ist nicht mehr aktuell.";
        return std::nullopt;
    }
    const size_t record = size_t(request.manager_slot) * 0xb0c + 0xa34 +
                          size_t(request.renewal_contract_slot) * 0x0c;
    if (record + 12 > g_.manager_records.size() ||
        !std::equal(request.renewal_expected_contract.begin(), request.renewal_expected_contract.end(),
                    g_.manager_records.begin() + std::ptrdiff_t(record)) ||
        (request.renewal_expected_contract[6] & 0x7f) == 0 || request.renewal_expected_contract[5] != 0) {
        error = "Diese Vertragsverlängerung ist nicht mehr aktuell.";
        return std::nullopt;
    }

    const auto checkpoint = advertising_session_checkpoint(revision);
    const auto restore = [&] {
        restore_advertising_session_checkpoint(checkpoint);
        revision = checkpoint.revision;
    };
    advertising_renewal_dispatch_active_ = true;
    struct ActiveReset { bool& active; ~ActiveReset() { active = false; } } active_reset{advertising_renewal_dispatch_active_};
    try {
        ModernState staged = g_;
        WatcomRandom staged_rand = rand_;
        auto renewal = modern::check_advertising_renewal(staged, request.manager_slot,
                                                       request.renewal_display_slot, staged_rand);
        if (renewal.status == AdvertisingRenewal::Status::invalid ||
            renewal.contract_slot != request.renewal_contract_slot ||
            renewal.status == AdvertisingRenewal::Status::not_expired) {
            restore(); error = "Diese Vertragsverlängerung ist nicht mehr aktuell."; return std::nullopt;
        }
        const auto presentation = presenter(staged, renewal);
        if (!advertising_session_unchanged(checkpoint, revision)) {
            restore(); error = "Die Session wurde während des Werbedialogs verändert."; return std::nullopt;
        }
        // Always restore the snapshot after presentation. This makes the rollback
        // unconditional, including hidden owners, IDs and runtime maps; only the
        // staged Source result below is allowed to become live.
        restore();
        if (renewal.status == AdvertisingRenewal::Status::offered) {
            if (presentation != AdvertisingRenewalPresentation::renew &&
                presentation != AdvertisingRenewalPresentation::decline) {
                error = "Die Vertragsverlängerung wurde nicht bestätigt."; return std::nullopt;
            }
            const bool renew = presentation == AdvertisingRenewalPresentation::renew;
            if (!modern::answer_advertising_renewal(staged, request.manager_slot,
                                                  request.renewal_contract_slot, renew)) {
                error = "Diese Vertragsverlängerung ist nicht mehr aktuell."; return std::nullopt;
            }
        } else if (renewal.status == AdvertisingRenewal::Status::declined_by_sponsor) {
            if (presentation != AdvertisingRenewalPresentation::acknowledged) {
                error = "Die Sponsorabsage wurde nicht bestätigt."; return std::nullopt;
            }
        } else {
            error = "Unbekannter Source-Verlängerungsstatus."; return std::nullopt;
        }
        g_ = std::move(staged);
        rand_ = staged_rand;
        last_advertising_request_id_ = request.request_id;
        ++revision;
        error.clear();
        return renewal;
    } catch (const std::exception&) {
        restore(); error = "Die Vertragsverlängerung konnte nicht bestätigt werden."; return std::nullopt;
    } catch (...) {
        restore(); error = "Die Vertragsverlängerung konnte nicht bestätigt werden."; return std::nullopt;
    }
}

bool ModernSession::apply_advertising(const ui::AdvertisingRequest& request, uint64_t& revision,
    std::string& error, const AdvertisingAcknowledge& acknowledge,
    const AdvertisingRenewalPresenter& presenter) {
    auto before = advertising_checkpoint();
    const auto old_revision = revision;
    const auto restore = [&] {
        restore_advertising_checkpoint(std::move(before));
        revision = old_revision;
    };
    try {
        AdvertisingActionResult result;
        if (request.kind == ui::AdvertisingActionKind::check_renewal) {
            if (!presenter) {
                error = "Die Original-Verlängerungsfrage ist nicht verfügbar.";
                restore(); return false;
            }
            result.renewal = dispatch_advertising_renewal(request, revision, error, presenter);
            if (!result.renewal) { restore(); return false; }
        } else if (!dispatch_advertising(request, revision, error, &result.spending)) {
            restore(); return false;
        }
        if (acknowledge) acknowledge(result);
        return true;
    } catch (...) {
        restore();
        throw;
    }
}

bool ModernSession::accept_advertising_selection(const ui::AdvertisingState& observed,
    int display_slot, int raw_offer, uint64_t request_id, uint64_t& revision, std::string& error) {
    if (display_slot < 0 || display_slot >= int(observed.offers.size())) {
        error = "Dieses Werbeangebot kann nicht übernommen werden."; return false;
    }
    const auto& view = observed.offers[size_t(display_slot)];
    const auto candidate = std::find_if(view.offers.begin(), view.offers.end(),
        [&](const auto& offer) { return offer.raw_slot == raw_offer; });
    if (candidate == view.offers.end()) {
        error = "Dieses Werbeangebot kann nicht übernommen werden."; return false;
    }
    ui::AdvertisingRequest request;
    request.request_id = request_id;
    request.manager_slot = observed.manager_slot;
    request.runtime_club = observed.runtime_team_id;
    request.revision = observed.revision;
    request.kind = ui::AdvertisingActionKind::accept_offer;
    request.offer.manager = observed.manager_slot;
    request.offer.display_slot = display_slot;
    request.offer.contract_slot = view.contract_slot;
    request.offer.expected_contract = view.contract_bytes;
    request.offer.raw_slot = candidate->raw_slot;
    request.offer.expected_offer = candidate->bytes;
    return apply_advertising(request, revision, error);
}

bool ModernSession::set_advertising_budget(const ui::AdvertisingState& observed, int target_slot,
    uint32_t amount, uint64_t request_id, uint64_t& revision, std::string& error,
    AdvertisingSpendingResult& result) {
    if (target_slot < 16 || target_slot > 18) {
        error = "Dieser Werbeausgabenauftrag ist nicht mehr gültig."; return false;
    }
    const auto& view = observed.spending[size_t(target_slot - 16)];
    ui::AdvertisingRequest request;
    request.request_id = request_id;
    request.manager_slot = observed.manager_slot;
    request.runtime_club = observed.runtime_team_id;
    request.revision = observed.revision;
    request.kind = ui::AdvertisingActionKind::set_spending;
    request.spending.manager = observed.manager_slot;
    request.spending.target_slot = target_slot;
    request.spending.amount = amount;
    request.spending.expected_amount = view.current_amount;
    request.spending.expected_delivery_weeks = view.delivery_weeks;
    return apply_advertising(request, revision, error,
        [&](const AdvertisingActionResult& applied) { result = applied.spending; });
}

bool ModernSession::dispatch_advertising(const ui::AdvertisingRequest& request, uint64_t& revision,
                                          std::string& error, AdvertisingSpendingResult* spending_result) {
    if (advertising_renewal_dispatch_active_) { error = "advertising_renewal_active"; return false; }
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    error.clear();
    if (spending_result) *spending_result = {};
    const int manager = request.manager_slot;
    if (!g_.memory.has(::bmh::layout::ActiveManager) || manager < 0 || manager > 3 ||
        size_t(manager) >= g_.manager_teams.size() || int(g_.memory.u8(::bmh::layout::ActiveManager)) != manager ||
        request.runtime_club != g_.manager_teams[size_t(manager)] ||
        request.revision != revision || revision == std::numeric_limits<uint64_t>::max() ||
        request.request_id == 0 || request.request_id <= last_advertising_request_id_ ||
        request.kind == ui::AdvertisingActionKind::check_renewal ||
        request.renewal_display_slot != -1 || request.renewal_contract_slot != -1 ||
        request.renewal_expected_contract != std::array<uint8_t, 12>{}) {
        error = "Diese Werbeaktion ist nicht mehr aktuell.";
        return false;
    }
    if (request.kind == ui::AdvertisingActionKind::accept_offer) {
        if (request.offer.manager != manager || request.spending != AdvertisingSpendingRequest{}) {
            error = "Das Werbeangebot enthält fremde Ausgabedaten.";
            return false;
        }
        const auto result = modern::accept_advertising_offer(g_, request.offer);
        if (result.status != AdvertisingOfferStatus::accepted) {
            error = result.status == AdvertisingOfferStatus::unsupported_extension
                ? "Diese Vertragsverlängerung ist noch nicht verfügbar."
                : "Dieses Werbeangebot kann nicht übernommen werden.";
            return false;
        }
    } else if (request.kind == ui::AdvertisingActionKind::set_spending) {
        if (request.spending.manager != manager || request.offer != AdvertisingOfferRequest{}) {
            error = "Der Ausgabenauftrag enthält fremde Vertragsdaten.";
            return false;
        }
        const auto result = modern::set_advertising_spending(g_, request.spending, rand_);
        if (spending_result) *spending_result = result;
        if (result.status != AdvertisingSpendingStatus::accepted &&
            result.status != AdvertisingSpendingStatus::no_change &&
            result.status != AdvertisingSpendingStatus::insufficient_funds) {
            error = "Dieser Werbeausgabenauftrag ist nicht mehr gültig.";
            return false;
        }
        // Insufficient funds is an Original handled response: it clears the old
        // merchandise amount, but neither books cash nor draws a delivery delay.
    } else {
        error = "Unbekannte Werbeaktion.";
        return false;
    }
    last_advertising_request_id_ = request.request_id;
    ++revision;
    return true;
}

bool ModernSession::dispatch_contract(const ui::ContractRequest& request, uint64_t& revision,
                                      ui::ContractActionResult& result, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    const auto reject = [&](std::string message) { error = std::move(message); return false; };
    const auto original_text = [&](uint32_t address) {
        return ui::original_to_utf8(texts_[(address - ::bmh::layout::TextPointerTable) / 4]);
    };
    const int active = g_.memory.has(::bmh::layout::ActiveManager) ? int(g_.memory.u8(::bmh::layout::ActiveManager)) : -1;
    if (request.request_id == 0 || request.request_id <= last_contract_request_id_ ||
        request.revision != revision || revision == std::numeric_limits<uint64_t>::max() ||
        request.manager_slot != active || active < 0 || active > 3 ||
        size_t(active) >= g_.manager_teams.size() || request.runtime_club != g_.manager_teams[size_t(active)] ||
        request.runtime_club == no_team())
        return reject("Der Vertragsauftrag gehört zu einem veralteten Manager oder Stand.");
    const bool team_bonuses = request.kind == ui::ContractActionKind::submit_bonuses && request.row == -1;
    const SquadRange range = squad_range(request.runtime_club);
    int player = -1;
    size_t board = 0;
    if (team_bonuses) {
        if (request.player_id != -1 || request.quote_id != 0)
            return reject("Der Teamprämienauftrag enthält eine fremde Spieleridentität.");
    } else {
        if (request.row < 0 || size_t(request.row) >= range.count || request.row >= 24 ||
            range.first + size_t(request.row) >= g_.squad_slots.size())
            return reject("Der ausgewählte Kaderplatz ist nicht mehr gültig.");
        player = g_.squad_slots[range.first + size_t(request.row)] & 0x0fff;
        if (player == 0x0fff || player != request.player_id || size_t(player) >= g_.players.size())
            return reject("Der Spieler gehört nicht mehr zu diesem Kaderplatz.");
        board = size_t(active) * 0x840 + size_t(request.row) * 0x58;
        if (board + 0x58 > g_.tactics.size()) return reject("Der Vertragsdatensatz ist unvollständig.");
    }
    if (request.kind == ui::ContractActionKind::request_demands) {
        if (request.quote_id != 0 || request.term_column != 0 || request.demand != 0 || request.monthly_salary != 0 ||
            request.insurance_months != 0 || std::any_of(request.proposed_bonuses.begin(),
                request.proposed_bonuses.end(), [](uint8_t b) { return b != 0; }))
            return reject("Der Forderungsauftrag enthält ungültige Angebotsdaten.");
        ModernState staged_game = g_;
        WatcomRandom staged_rand = rand_;
        std::array<uint32_t, 4> demands{};
        try { demands = modern::contract_demands(staged_game, active, request.row, true, staged_rand); }
        catch (const std::exception&) { return reject("Die Gehaltsforderungen konnten nicht ermittelt werden."); }
        if (modern::save_stream(staged_game) != modern::save_stream(g_))
            return reject("Die Forderungsabfrage hat unerwartet den Spielstand verändert.");
        rand_ = staged_rand;
        ++revision;
        ContractQuoteRecord quote;
        quote.quote_id = next_contract_quote_id_++;
        quote.request_id = request.request_id;
        quote.revision = revision;
        quote.manager_slot = active;
        quote.runtime_club = request.runtime_club;
        quote.row = request.row;
        quote.player_id = player;
        quote.demands = demands;
        contract_quote_ = quote;
        last_contract_request_id_ = request.request_id;
        result.success = true;
        result.quote_id = quote.quote_id;
        result.demands = demands;
        return true;
    }
    if (!team_bonuses && (!contract_quote_ || request.quote_id != contract_quote_->quote_id ||
        request.request_id <= contract_quote_->request_id || request.revision != contract_quote_->revision ||
        request.manager_slot != contract_quote_->manager_slot || request.runtime_club != contract_quote_->runtime_club ||
        request.row != contract_quote_->row || player != contract_quote_->player_id))
        return reject("Dieser Auftrag ist nicht mehr an die aktuelle Verhandlung gebunden.");
    const bool full_mode = g_.memory.u8(::bmh::layout::GameMode) == 3;
    if (request.kind == ui::ContractActionKind::insure_player) {
        if (!full_mode || request.term_column >= 4 || request.insurance_months < 1 || request.insurance_months > 60 ||
            request.demand != 0 || request.monthly_salary != 0 ||
            std::any_of(request.proposed_bonuses.begin(), request.proposed_bonuses.end(), [](uint8_t b) { return b != 0; }))
            return reject("Der Versicherungsauftrag enthält ungültige Daten.");
        // ContractBonuses checks these in this order and uses the corresponding Original dialog.
        if (g_.tactics[board + 0x0b] & 0x20) return reject(original_text(::bmh::layout::CareerSession_Reject_TextPointer));
        if (g_.tactics[board + 0x0b] & 0x40) return reject(original_text(::bmh::layout::CareerSession_Reject_TextPointer2));
        if (g_.tactics[board + 0x43] != 0) return reject(original_text(::bmh::layout::CareerSession_Reject_TextPointer3));
        if (g_.players[size_t(player)].w35c >> 27) return reject(original_text(::bmh::layout::CareerSession_Reject_TextPointer4));
        ModernState staged_game = g_;
        if (!modern::contract_insurance(staged_game, active, request.row, request.term_column, request.insurance_months).accepted)
            return reject("Die Versicherung konnte nicht bestätigt werden.");
        g_ = std::move(staged_game);
        ++revision;
        last_contract_request_id_ = request.request_id;
        contract_quote_->revision = revision;
        result.success = result.accepted = true;
        result.quote_id = contract_quote_->quote_id;
        result.demands = contract_quote_->demands;
        return true;
    }
    for (size_t i = 0; i < request.proposed_bonuses.size(); ++i)
        if (request.proposed_bonuses[i] > (i < 3 ? 20 : 200))
            return reject("Die Prämie liegt über dem Originalmaximum.");
    if (request.kind == ui::ContractActionKind::submit_bonuses) {
        if (!full_mode || request.term_column != 0 || request.demand != 0 || request.monthly_salary != 0 ||
            request.insurance_months != 0) return reject("Der Prämienauftrag enthält ungültige Daten.");
        ModernState staged_game = g_;
        const bool accepted = modern::contract_bonuses(staged_game, active, request.row, request.proposed_bonuses);
        g_ = std::move(staged_game);
        ++revision;
        last_contract_request_id_ = request.request_id;
        contract_quote_.reset();
        result.success = true;
        result.accepted = accepted;
        result.feedback = accepted ? original_text(team_bonuses ? ::bmh::layout::CareerSession_Reject_TextPointer8 : ::bmh::layout::CareerSession_Reject_TextPointer9) :
            team_bonuses ? original_text(::bmh::layout::CareerSession_Reject_TextPointer7) : std::string{};
        return true;
    }
    if (request.kind != ui::ContractActionKind::submit_offer || request.term_column >= 4 || request.insurance_months != 0 ||
        request.demand != contract_quote_->demands[request.term_column] || request.demand == 0 ||
        request.demand == std::numeric_limits<uint32_t>::max() || request.monthly_salary > 200000 ||
        (g_.tactics[board + 0x4f] & 0x80) || (g_.tactics[board + 0x0b] & 0x60))
        return reject("Dieses Angebot ist im Original nicht mehr verhandelbar.");
    if (!full_mode && !std::equal(request.proposed_bonuses.begin(), request.proposed_bonuses.end(),
                                  g_.tactics.begin() + long(board + 0x12)))
        return reject("Prämienänderungen sind nur in Spielmodus III verfügbar.");
    ModernState staged_game = g_;
    WatcomRandom staged_rand = rand_;
    bool accepted = false;
    try {
        accepted = modern::contract_offer(staged_game, active, request.row, request.term_column,
                                 request.monthly_salary, request.demand, false, false, staged_rand,
                                 &request.proposed_bonuses);
    } catch (const std::exception&) { return reject("Der Vertrag konnte nicht verhandelt werden."); }
    g_ = std::move(staged_game);
    rand_ = staged_rand;
    ++revision;
    last_contract_request_id_ = request.request_id;
    contract_quote_.reset();
    result.success = true;
    result.accepted = accepted;
    result.feedback = original_text(accepted ? ::bmh::layout::CareerSession_Reject_TextPointer5 : ::bmh::layout::CareerSession_Reject_TextPointer6);
    return true;
}

bool ModernSession::validate_desk_setting_owner(const DeskSettingIdentity& identity, uint64_t revision,
                                                 std::string& error, bool require_desk_phase) const {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    if (balance_draft_ || balance_dispatch_active_ || league_pairings_draft_ || league_pairings_dispatch_active_) {
        error = "league_pairings_active"; return false;
    }
    if (career_complete()) { error = "career_complete"; return false; }
    if (require_desk_phase && ((turn_driver_.stage != TurnDriver::Stage::MatchdayDesks &&
         turn_driver_.stage != TurnDriver::Stage::SeasonEndDesk) ||
        pending_offer_dispatch_active_ || market_dispatch_active_ || calendar_advance_active_ ||
        transfer_offer_editor_ || manager_join_ || contract_quote_ || stadium_.investment ||
        stadium_.tip || stadium_.periphery_quote)) {
        error = "desk_setting_unavailable"; return false;
    }
    const int active = g_.memory.has(::bmh::layout::ActiveManager) ? int(g_.memory.u8(::bmh::layout::ActiveManager)) : -1;
    if (identity.manager_slot != active || active < 0 || active > 3 ||
        size_t(active) >= g_.manager_teams.size() || identity.revision != revision || revision == UINT64_MAX) {
        error = "stale_manager_or_revision"; return false;
    }
    const int club = g_.manager_teams[size_t(active)];
    const size_t rec = size_t(active) * 0xb0c;
    if (identity.runtime_club != club || club < 0 || club > no_team() || size_t(club) >= g_.club_ids.size() ||
        identity.stable_club_identity != std::optional<int>(int(g_.club_ids[size_t(club)])) ||
        rec + 0xb0c > g_.manager_records.size() || size_t(active + 1) * 5 > g_.manager_options.size() ||
        (int(g_.manager_records[rec + 0x290]) | int(g_.manager_records[rec + 0x291]) << 8) != club) {
        error = "invalid_desk_setting_owner"; return false;
    }
    return true;
}

bool ModernSession::apply_desk_setting(int manager, bool price, int index, int value, std::string& error) {
    const size_t rec = size_t(manager) * 0xb0c;
    const bool national = g_.manager_teams[size_t(manager)] == no_team();
    if (price) {
        // Uint8T4: local offsets35..86 map to admission values8..25.
        if (national || value < 8 || value > 25) { error = "invalid_ticket_price"; return false; }
        g_.manager_records[rec + 0x26e] = uint8_t(value);
    } else {
        if (index < 0 || index > 4) { error = "invalid_automation_index"; return false; }
        const size_t at = size_t(manager) * 5 + size_t(index);
        if (index == 0) {
            if (g_.manager_options[at] > 8 || value < 0 || value > 8) {
                error = "invalid_saved_formation_automation"; return false;
            }
            // SizeT6 uses the saved outdoor/indoor formation availability.
            const uint8_t mask = g_.manager_records[rec + 0x218 + size_t(g_.indoor)];
            if (value != 0 && !(mask & (1u << (value - 1)))) {
                error = "unavailable_saved_formation"; return false;
            }
            g_.manager_options[at] = uint8_t(value);
        } else {
            if (index == 4 && national) { error = "national_team_training_unavailable"; return false; }
            if (value != 0 && value != 1) { error = "invalid_automation_value"; return false; }
            // Explicit bool setters and SDL toggles preserve every other bit.
            g_.manager_options[at] = uint8_t((g_.manager_options[at] & ~uint8_t(1)) | uint8_t(value));
        }
    }
    error.clear();
    return true;
}

bool ModernSession::dispatch_ticket_price(const TicketPriceRequest& request, uint64_t& revision,
                                          std::string& error) {
    if (!validate_desk_setting_owner(request.identity, revision, error)) return false;
    if (request.price < 8 || request.price > 25) { error = "invalid_ticket_price"; return false; }
    if (!apply_desk_setting(request.identity.manager_slot, true, -1, request.price, error)) return false;
    ++revision;
    return true;
}

bool ModernSession::dispatch_automation(const AutomationRequest& request, uint64_t& revision,
                                       std::string& error) {
    if (!validate_desk_setting_owner(request.identity, revision, error)) return false;
    const int index = int(request.automation);
    if (index < 0 || index > 4) { error = "invalid_automation_index"; return false; }
    int value = int(request.enabled);
    if (index == 0) {
        if (request.enabled != request.formation.has_value() ||
            (request.formation && (*request.formation < 1 || *request.formation > 8))) {
            error = "invalid_saved_formation_automation"; return false;
        }
        value = request.formation.value_or(0);
    } else if (request.formation) { error = "unexpected_formation_selection"; return false; }
    if (!apply_desk_setting(request.identity.manager_slot, false, index, value, error)) return false;
    ++revision;
    return true;
}

bool ModernSession::dispatch_desk_widget(const ui::DeskRequest& request, uint64_t& revision,
                                         std::string& error) {
    const int active = g_.memory.has(::bmh::layout::ActiveManager) ? int(g_.memory.u8(::bmh::layout::ActiveManager)) : -1;
    DeskSettingIdentity identity{request.manager_slot, -1, std::nullopt, request.revision};
    if (active >= 0 && size_t(active) < g_.manager_teams.size()) {
        identity.runtime_club = g_.manager_teams[size_t(active)];
        if (identity.runtime_club >= 0 && size_t(identity.runtime_club) < g_.club_ids.size())
            identity.stable_club_identity = int(g_.club_ids[size_t(identity.runtime_club)]);
    }
    if (!validate_desk_setting_owner(identity, revision, error, false)) return false;
    const bool price = request.kind == ui::DeskRequestKind::change_ticket_price;
    if (!price && request.kind != ui::DeskRequestKind::change_option) {
        error = "unsupported_desk_widget"; return false;
    }
    // Keep SDL requests bound to a visible, open and enabled MEN drawer/item.
    const auto desk = desktop_projection(revision);
    const int expected_kind = price ? 55 : 64;
    const auto item = std::find_if(desk.items.begin(), desk.items.end(), [&](const ui::DeskItem& item) {
        return item.id == request.item_id && item.kind == expected_kind && item.open && !item.disabled;
    });
    if (request.menu_kind != expected_kind || item == desk.items.end()) {
        error = "stale_or_closed_desk_widget"; return false;
    }
    int value = request.value;
    if (!price) {
        if (request.value < 0 || request.value > 4) { error = "invalid_automation_index"; return false; }
        const size_t at = size_t(active) * 5 + size_t(request.value);
        if (request.value == 0) {
            if (g_.manager_options[at] > 8) { error = "invalid_saved_formation_automation"; return false; }
            value = g_.manager_options[at];
            const uint8_t mask = g_.manager_records[size_t(active) * 0xb0c + 0x218 + size_t(g_.indoor)];
            do { value = (value + 1) % 9; } while (value != 0 && !(mask & (1u << (value - 1))));
        } else value = (g_.manager_options[at] ^ 1) & 1;
    }
    if (!apply_desk_setting(active, price, request.value, value, error)) return false;
    ++revision;
    return true;
}

bool ModernSession::dispatch_desktop_layout(const ui::DeskRequest& request, uint64_t& revision,
                                           std::string& error) {
    constexpr size_t kItems=54, kStride=16, kBytes=kItems*kStride;
    if (request.kind != ui::DeskRequestKind::change_layout || request.revision != revision ||
        request.manager_slot < 0 || size_t(request.manager_slot) >= g_.manager_teams.size() ||
        !g_.memory.has(::bmh::layout::ActiveManager) || int(g_.memory.u8(::bmh::layout::ActiveManager)) != request.manager_slot ||
        turn_driver_.stage==TurnDriver::Stage::Failed || turn_driver_.stage==TurnDriver::Stage::CareerComplete ||
        career_complete()) {
        error="stale_or_invalid_desk_layout_owner"; return false;
    }
    if (transfer_offer_editor_ || pending_offer_dispatch_active_ || market_dispatch_active_ ||
        balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || other_games_owner_ || other_games_dispatch_active_ ||
        stadium_.block_expansion || advertising_renewal_dispatch_active_) {
        error="desk_layout_blocked_during_active_flow"; return false;
    }
    if (request.item_id < 1 || request.item_id >= int(kItems)) { error="invalid_desk_layout_item"; return false; }
    const uint32_t base=::bmh::layout::DesktopsUnsigned+uint32_t(request.manager_slot)*0x360u;
    if (!g_.memory.has(base+kBytes-1)) { error="desk_layout_memory_unavailable"; return false; }
    std::array<uint8_t,kBytes> actual{};
    for(size_t i=0;i<kBytes;++i) actual[i]=g_.memory.u8(base+uint32_t(i));
    if(actual!=request.layout_before) { error="stale_desk_layout_before_state"; return false; }
    auto expected=request.layout_before;
    const size_t at=size_t(request.item_id)*kStride;
    auto read16=[](const auto& bytes,size_t offset){return uint16_t(bytes[offset])|(uint16_t(bytes[offset+1])<<8);};
    auto write16=[](auto& bytes,size_t offset,uint16_t value){bytes[offset]=uint8_t(value);bytes[offset+1]=uint8_t(value>>8);};
    const auto moved_x=read16(request.layout_after,at);
    const uint8_t moved_y=request.layout_after[at+2];
    if(request.menu_kind!=expected[at+12]) { error="desk_layout_item_kind_mismatch"; return false; }
    const bool source_drag_enabled=g_.memory.has(::bmh::layout::CareerSession_DispatchDesktopLayout_RuntimeField)&&g_.memory.u8(::bmh::layout::CareerSession_DispatchDesktopLayout_RuntimeField)==0;
    if((request.layout_operation==ui::DeskLayoutOperation::move_item||
        request.layout_operation==ui::DeskLayoutOperation::set_folder||
        request.layout_operation==ui::DeskLayoutOperation::return_folder_child||
        request.layout_operation==ui::DeskLayoutOperation::send_to_folder||
        request.layout_operation==ui::DeskLayoutOperation::restore_folder_child)&&!source_drag_enabled) {
        error="source_desk_drag_disabled";return false;
    }
    switch(request.layout_operation) {
    case ui::DeskLayoutOperation::move_item: {
        if(request.layout_target_item!=-1 || expected[at+12]==72) { error="invalid_desk_layout_move_target"; return false; }
        for(size_t i=0;i<kBytes;++i) if(i!=at&&i!=at+1&&i!=at+2&&request.layout_after[i]!=expected[i]) {
            error="desk_layout_move_changed_unowned_bytes"; return false;
        }
        const int source_x=int(moved_x&0xefff), max_x=std::max(1,319-int(expected[at+3]));
        const int max_y=std::max(1,239-int(expected[at+4]));
        const bool snap=g_.memory.has(::bmh::layout::CareerSession_DispatchDesktopLayout_RuntimeField2) && g_.memory.u8(::bmh::layout::CareerSession_DispatchDesktopLayout_RuntimeField2)==0;
        if((moved_x&0x1000)!=(read16(expected,at)&0x1000) || source_x<1 || source_x>max_x ||
           moved_y<1 || moved_y>max_y ||
           (snap && ((source_x%5)!=0 && source_x!=max_x || (moved_y%5)!=0 && moved_y!=max_y))) {
            error="desk_layout_move_outside_source_bounds"; return false;
        }
        write16(expected,at,moved_x); expected[at+2]=moved_y;
        break;
    }
    case ui::DeskLayoutOperation::toggle_quickbar_drawer: {
        const int id=request.item_id;
        const int kind=expected[at+12];
        if(request.layout_target_item!=id || id<1 || id>=54 || kind<50 || kind>71 ||
           kind!=request.menu_kind || (request.value!=2 && request.value!=8)) {
            error="invalid_desk_quickbar_drawer"; return false;
        }
        if(expected[at+5]==0xff) {
            int tail=0;
            size_t guard=0;
            while(expected[size_t(tail)*kStride+5]!=0) {
                if(++guard>=kItems) { error="invalid_desk_layout_chain"; return false; }
                tail=expected[size_t(tail)*kStride+5];
                if(tail<0 || tail>=int(kItems)) { error="invalid_desk_layout_links"; return false; }
            }
            expected[at+5]=0; expected[at+6]=uint8_t(tail);
            expected[size_t(tail)*kStride+5]=uint8_t(id);
            const bool opened=!(request.value==8 && id>2);
            expected[at+13]=uint8_t((expected[at+13]&0x7f)|(opened?0x80:0));
        } else {
            const uint8_t previous=expected[at+6],next=expected[at+5];
            if(previous>=kItems || (next!=0 && next>=kItems)) { error="invalid_desk_layout_links"; return false; }
            expected[size_t(previous)*kStride+5]=next;
            if(next!=0) expected[size_t(next)*kStride+6]=previous;
            expected[at+5]=expected[at+6]=0xff;
        }
        break;
    }
    case ui::DeskLayoutOperation::return_folder_child: {
        const int folder=request.layout_target_item;
        if(request.item_id<1||request.item_id>30||folder<0||folder>=3||
           expected[at+5]!=0xff||expected[at+6]!=0xff||(expected[at+1]&0x10)!=0||
           (expected[at+13]&0x7f)!=folder||expected[at+12]>=50) {
            error="invalid_desk_folder_child_return"; return false;
        }
        bool owner_open=false;
        for(size_t item=1;item<kItems;++item) {
            if(expected[item*kStride+12]!=50+folder||(expected[item*kStride+13]&0x80)==0) continue;
            int cursor=0;size_t visited=0;
            while(++visited<kItems) {
                const int next=expected[size_t(cursor)*kStride+5];
                if(next==int(item)) { owner_open=true;break; }
                if(next<=0||next>=int(kItems)) break;
                cursor=next;
            }
            if(owner_open) break;
        }
        if(!owner_open) { error="desk_folder_child_owner_closed"; return false; }
        const int max_x=std::max(1,319-int(expected[at+3])),max_y=std::max(1,239-int(expected[at+4]));
        const int source_x=int(moved_x&0xefff);
        const bool snap=g_.memory.has(::bmh::layout::CareerSession_DispatchDesktopLayout_RuntimeField2)&&g_.memory.u8(::bmh::layout::CareerSession_DispatchDesktopLayout_RuntimeField2)==0;
        if((moved_x&0x1000)!=(read16(expected,at)&0x1000)||source_x<1||source_x>max_x||moved_y<1||moved_y>max_y||
           (snap&&(((source_x%5)!=0&&source_x!=max_x)||((moved_y%5)!=0&&moved_y!=max_y)))) {
            error="desk_folder_child_return_outside_source_bounds";return false;
        }
        int tail=0;size_t guard=0;
        while(expected[size_t(tail)*kStride+5]!=0) {
            if(++guard>=kItems) { error="invalid_desk_layout_chain";return false; }
            tail=expected[size_t(tail)*kStride+5];
            if(tail<0||tail>=int(kItems)) { error="invalid_desk_layout_links";return false; }
        }
        expected[at]=request.layout_after[at];expected[at+1]=request.layout_after[at+1];expected[at+2]=moved_y;
        expected[at+5]=0;expected[at+6]=uint8_t(tail);
        expected[size_t(tail)*kStride+5]=uint8_t(request.item_id);
        break;
    }
    case ui::DeskLayoutOperation::send_to_folder: {
        const int folder=request.layout_target_item;
        const uint8_t previous=expected[at+6],next=expected[at+5];
        if(request.item_id<1||request.item_id>30||folder<0||folder>=3||
           expected[at+5]==0xff||expected[at+12]>=50||expected[at+12]==12||
           (expected[at+13]&0x7f)!=folder||previous>=kItems||(next!=0&&next>=kItems)||
           expected[size_t(previous)*kStride+5]!=request.item_id||
           (next!=0&&expected[size_t(next)*kStride+6]!=request.item_id)) {
            error="invalid_desk_bin_send";return false;
        }
        expected[size_t(previous)*kStride+5]=next;
        if(next!=0)expected[size_t(next)*kStride+6]=previous;
        expected[at+5]=expected[at+6]=0xff;
        break;
    }
    case ui::DeskLayoutOperation::restore_folder_child: {
        const int folder=request.layout_target_item;
        if(request.item_id<1||request.item_id>30||folder<0||folder>=3||
           expected[at+5]!=0xff||expected[at+6]!=0xff||(expected[at+1]&0x10)!=0||
           (expected[at+13]&0x7f)!=folder||expected[at+12]>=50) {
            error="invalid_desk_bin_restore";return false;
        }
        bool owner_open=false;
        for(size_t item=1;item<kItems;++item) {
            if(expected[item*kStride+12]!=50+folder||(expected[item*kStride+13]&0x80)==0) continue;
            int cursor=0;size_t visited=0;
            while(++visited<kItems) {
                const int next=expected[size_t(cursor)*kStride+5];
                if(next==int(item)) { owner_open=true;break; }
                if(next<=0||next>=int(kItems)) break;
                cursor=next;
            }
            if(owner_open)break;
        }
        if(!owner_open) { error="desk_bin_restore_folder_closed";return false; }
        int tail=0;size_t guard=0;
        while(expected[size_t(tail)*kStride+5]!=0) {
            if(++guard>=kItems) { error="invalid_desk_layout_chain";return false; }
            tail=expected[size_t(tail)*kStride+5];
            if(tail<0||tail>=int(kItems)) { error="invalid_desk_layout_links";return false; }
        }
        expected[at+5]=0;expected[at+6]=uint8_t(tail);
        expected[size_t(tail)*kStride+5]=uint8_t(request.item_id);
        break;
    }
    case ui::DeskLayoutOperation::set_folder: {
        const int target=request.layout_target_item;
        const int kind=expected[at+12];
        if(kind>=50 || request.item_id>30 || (expected[at+1]&0x10)!=0 || target<1 || target>=int(kItems) || expected[size_t(target)*kStride+12]<50 ||
           expected[size_t(target)*kStride+12]>52) { error="invalid_desk_folder_drop"; return false; }
        const size_t target_at=size_t(target)*kStride;
        const uint8_t folder=uint8_t(expected[target_at+12]-50);
        int folder_count=0;
        for(int child=1;child<=30;++child) {
            const size_t child_at=size_t(child)*kStride;
            if(expected[child_at+5]==0xff && (expected[child_at+1]&0x10)==0 &&
               (expected[child_at+13]&0x7f)==folder) ++folder_count;
        }
        const bool already_in_folder=expected[at+5]==0xff&&(expected[at+13]&0x7f)==folder;
        if(folder_count-(already_in_folder?1:0)>=12) { error="desk_folder_source_capacity"; return false; }
        const int previous=expected[at+6], next=expected[at+5];
        if((previous!=0xff && previous>=int(kItems)) || (next!=0xff && next>=kItems)) { error="invalid_desk_layout_links"; return false; }
        if(previous!=0xff) expected[size_t(previous)*kStride+5]=uint8_t(next);
        if(next>0 && next<0xff) expected[size_t(next)*kStride+6]=uint8_t(previous);
        expected[at+13]=uint8_t((expected[at+13]&0x80)|folder);
        expected[at+5]=expected[at+6]=0xff;
        write16(expected,at,moved_x); expected[at+2]=moved_y;
        break;
    }
    case ui::DeskLayoutOperation::none:
        error="unsupported_desk_layout_operation"; return false;
    }
    if(expected!=request.layout_after) { error="desk_layout_after_state_not_source_derived"; return false; }
    ModernState staged=g_;
    for(size_t i=0;i<kBytes;++i) staged.memory.set8(base+uint32_t(i),expected[i]);
    try {
        const auto projected=desktop_projection(staged,revision+1);
        if(projected.manager_slot!=request.manager_slot) { error="desk_layout_projection_mismatch"; return false; }
        for(size_t item=0;item<kItems;++item)
            if(!std::equal(projected.layout_records[item].begin(),projected.layout_records[item].end(),expected.begin()+item*kStride)) {
                error="desk_layout_projection_mismatch"; return false;
            }
    } catch(const std::exception&) { error="desk_layout_projection_invalid"; return false; }
    g_=std::move(staged); ++revision; error.clear(); return true;
}

bool ModernSession::dispatch_tactics(const ui::TacticsRequest& request, uint64_t& revision,
                                    std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    error.clear();
    const int active = int(g_.memory.u8(::bmh::layout::ActiveManager));
    if (request.manager_slot != active || request.manager_slot < 0 || request.manager_slot > 3 ||
        size_t(request.manager_slot) >= g_.manager_teams.size() ||
        request.runtime_club != g_.manager_teams[size_t(request.manager_slot)] ||
        request.revision != revision) {
        error = "Die Taktikanfrage ist veraltet."; return false;
    }
    if (request.kind != ui::TacticsRequestKind::set_tactic || request.index < 0 || request.index >= 8 ||
        request.desired_value < 0 || request.desired_value > exe_.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField + request.index)) {
        error = "Ungültige Taktikeinstellung."; return false;
    }
    const bool live_option = (g_.memory.has(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3) ? g_.memory.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3) : exe_.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3)) != 0;
    if (live_option && exe_.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField2 + request.index) == 0) {
        error = "Diese Taktikeinstellung ist nicht verfügbar."; return false;
    }
    if ((request.index == 0 && request.desired_assistant_attitude != std::optional<uint8_t>(0)) ||
        (request.index != 0 && request.desired_assistant_attitude)) {
        error = "Ungültige Assistenteneinstellung."; return false;
    }
    // RerateTeam: only +d2..+d9 and (index 0) option byte +2.
    // The tactic board, player order and both RNG streams stay untouched.
    const size_t rec = size_t(request.manager_slot) * 0xb0c;
    const size_t option = size_t(request.manager_slot) * 5 + 2;
    if (g_.manager_records.size() < rec + 0xb0c || g_.manager_options.size() <= option) {
        error = "Die Taktikdaten sind unvollständig."; return false;
    }
    g_.manager_records[rec + 0xd2 + size_t(request.index)] = uint8_t(request.desired_value);
    if (request.index == 0) g_.manager_options[option] = 0;
    ++revision;
    return true;
}

bool ModernSession::dispatch_lineup(const ui::LineupRequest& request, uint64_t& revision,
                                    std::string& error) {
    if (calendar_advance_active_ || pending_offer_dispatch_active_) {
        error = "lineup_unavailable_during_calendar_advance";
        return false;
    }
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    error.clear();
    const int active = g_.memory.has(::bmh::layout::ActiveManager) ? int(g_.memory.u8(::bmh::layout::ActiveManager)) : -1;
    if (request.manager_slot != active || active < 0 || active > 3 ||
        size_t(active) >= g_.manager_teams.size() || request.runtime_club != g_.manager_teams[size_t(active)] ||
        request.revision != revision || revision == std::numeric_limits<uint64_t>::max() ||
        request.runtime_club < 0 || request.runtime_club >= int(g_.team_flags.size())) {
        error = "Die Aufstellungsaktion ist veraltet.";
        return false;
    }
    const SquadRange range = squad_range(request.runtime_club);
    if (range.count < 11 || range.first + range.count > g_.squad_slots.size() ||
        g_.tactics.size() < size_t(active + 1) * 0x840 ||
        g_.manager_options.size() <= size_t(active) * 5 || g_.player_cards.size() != g_.players.size()) {
        error = "Der Aufstellungszustand ist unvollständig.";
        return false;
    }
    if (request.kind == ui::LineupRequestKind::prepare_lineup) {
        ModernState staged = g_;
        try {
            // PitchScreen opens through the human Int2 path first.
            // Existing assistant scoring retains its documented port limitations.
            auto_lineup(staged, request.runtime_club, 0xe4);
            prepare_lineup_entry(staged, request.runtime_club); // PrepareLineupEntry.
            modern::open_lineup_screen(staged, active); // OpenBMH deviates from the original: supports designated set-piece record initialization and validation.
// supported rule profile NormalizePitchSetPieceSource entry initialization.
            if (!initialize_pitch_set_piece_source(staged, active, request.runtime_club))
                throw std::runtime_error("set-piece set-piece record did not validate");
            const uint32_t display=uint32_t(::bmh::layout::CareerSession_AnyOf_ExecutableField+active*2);
            if(staged.memory.has(display)&&(staged.memory.u16(display)&0x80)&&
               !normalize_pitch_set_piece_source(staged,active,request.runtime_club))
                throw std::runtime_error("set-piece reentry redraw did not validate");
        } catch (const std::exception& e) {
            error = std::string("Diese Aufstellung kann im aktuellen Spielmodus noch nicht vorbereitet werden. (") + e.what() + ")";
            return false;
        }
        // Accept only the valid authoritative eleven produced by the human path.
        std::array<bool, 0x1000> seen{};
        for (int row = 0; row < 11; ++row) {
            const uint16_t slot = staged.squad_slots.at(range.first + size_t(row));
            const int id = slot & 0x0fff;
            const auto* board = staged.tactics.data() + size_t(active) * 0x840 + size_t(row) * 0x58;
            if (id == 0x0fff || size_t(id) >= g_.players.size() || seen[size_t(id)] ||
                !(slot & 0x8000) || (slot & 0x4000) || int8_t(board[0x41]) < 0 ||
                int8_t(board[0x41]) > 19 || int8_t(board[0x42]) < 0 || int8_t(board[0x42]) > 8) {
                error = "Die Aufstellung ist noch nicht für den Anstoß vorbereitet.";
                return false;
            }
            seen[size_t(id)] = true;
        }
        g_ = std::move(staged);
        lineup_entry_manager_ = active;
        lineup_entry_club_ = request.runtime_club;
        ++revision;
        return true;
    }
    if (request.kind != ui::LineupRequestKind::swap_players ||
        lineup_entry_manager_ != active || lineup_entry_club_ != request.runtime_club ||
        request.first_row < 0 || request.second_row < 0 || request.first_row == request.second_row ||
        size_t(request.first_row) >= range.count || size_t(request.second_row) >= range.count) {
        error = "Der Spielerwechsel ist nicht freigegeben.";
        return false;
    }
    const size_t first = range.first + size_t(request.first_row);
    const size_t second = range.first + size_t(request.second_row);
    const uint16_t first_slot = g_.squad_slots.at(first), second_slot = g_.squad_slots.at(second);
    const int first_id = (first_slot & 0x0fff) == 0x0fff ? -1 : first_slot & 0x0fff;
    const int second_id = (second_slot & 0x0fff) == 0x0fff ? -1 : second_slot & 0x0fff;
    if (first_slot != request.first_slot || second_slot != request.second_slot ||
        first_id != request.first_player || second_id != request.second_player ||
        first_id < 0 || second_id < 0 || size_t(first_id) >= g_.players.size() || size_t(second_id) >= g_.players.size()) {
        error = "Die Kaderzeilen wurden inzwischen geändert.";
        return false;
    }
    // SizeT3 requires injury/foreign-rule confirmations for special
    // swaps. Until those dialogs exist, reject that path before any writes.
    if (((g_.players[size_t(first_id)].w364 >> 16) & 0x1f) != 0 ||
        ((g_.players[size_t(second_id)].w364 >> 16) & 0x1f) != 0) {
        error = "Ein verletzter Spieler kann hier noch nicht eingewechselt werden.";
        return false;
    }
    if ((request.first_row < 11) != (request.second_row < 11) && exe_.u8(::bmh::layout::ForeignPlayerLimitEnabled) != 0) {
        error = "Dieser Wechsel benötigt eine Bestätigung der Ausländerregel.";
        return false;
    }
    const size_t board_base = size_t(active) * 0x840;
    auto* a = g_.tactics.data() + board_base + size_t(request.first_row) * 0x58;
    auto* b = g_.tactics.data() + board_base + size_t(request.second_row) * 0x58;
    const uint8_t ax = a[0x41], ay = a[0x42], bx = b[0x41], by = b[0x42];
    // Original SquadSlotsBoardEntriesChangePlaces moves the player/tactics entry, then restores
    // the 0xc000 role bits to their roster rows. Preserve bit 12 and the
    // player id with the entry; keep the starter/bench flags with the slot.
    g_.squad_slots[first] = uint16_t((second_slot & 0x1fff) | (first_slot & 0xc000));
    g_.squad_slots[second] = uint16_t((first_slot & 0x1fff) | (second_slot & 0xc000));
    std::swap_ranges(a, a + 0x58, b);
    a[0x41] = ax; a[0x42] = ay; b[0x41] = bx; b[0x42] = by;
    // GatedByParamLive is applied to every marked row, not just the two swapped
    // players. Keep card-side row identity synchronized with the roster.
    for (size_t row = 0; row < range.count; ++row) {
        const uint16_t slot = g_.squad_slots[range.first + row];
        if ((slot & 0xc000) == 0) continue;
        const size_t id = slot & 0x0fff;
        if (id >= g_.players.size()) continue;
        g_.player_cards[id] = uint16_t((g_.player_cards[id] & 0xffe0) | uint16_t(row));
    }
    g_.manager_options.at(size_t(active) * 5) = 0; // source manual mode selects DispatchLiveOverview
    ++revision;
    return true;
}

std::optional<ModernSession::LiveOverviewDispatch> ModernSession::dispatch_live_overview(
    const LiveOverviewAction& action, MatchEngine& engine, uint64_t& revision, std::string& error,
    const TransferOfferTeamInfoPresenter& team_info) {
    error.clear();
    if (!calendar_advance_active_ || !live_overview_owner_ || revision==UINT64_MAX || live_overview_owner_->request.revision!=revision ||
        &engine.state!=&g_ || &engine.game!=&game_ || &engine.rand!=&rand_ ||
        (live_overview_owner_->phase_result
            ? (!phase_result_request_ || live_overview_owner_->request.group_index!=int(phase_result_request_->group_index))
            : live_overview_owner_->request.group_index!=0) || engine.groups.empty() ||
        engine.groups.size()!=live_overview_owner_->active_groups.size() ||
        engine.groups.size()!=turn_driver_.groups.size() ||
        engine.groups.front().desc!=turn_driver_.groups.front().desc ||
        engine.groups.front().fixtures!=turn_driver_.groups.front().fixtures ||
        engine.referees!=live_overview_owner_->referees) {
        error="live overview: stale owner or no longer at the Source minute boundary";
        return {};
    }
    for(size_t i=0;i<engine.groups.size();++i)
        if(engine.groups[i].desc!=live_overview_owner_->active_groups[i].first ||
           engine.groups[i].fixtures!=live_overview_owner_->active_groups[i].second) {
            error="live overview: active Source match group changed";return {};
        }
    const auto live= *live_overview_owner_;
    struct LiveBoundaryFamilyScope {
        bool& active; bool previous;
        explicit LiveBoundaryFamilyScope(bool& value) : active(value), previous(value) { active=false; }
        ~LiveBoundaryFamilyScope() { active=previous; }
    } family_scope(calendar_advance_active_);
    LiveOverviewDispatch result;result.kind=live.kind;
    try {
        if(const auto* family=std::get_if<ui::LeagueFamilyRequest>(&action)) {
            if((live.kind!=LiveOverviewKind::league_table&&live.kind!=LiveOverviewKind::league_pairings&&
                live.kind!=LiveOverviewKind::league_strengths) ||
               family->identity!=live.family_identity || family->identity.revision!=revision ||
               !league_family_owner_ || league_family_owner_->identity!=family->identity ||
               family->from_child!=league_family_owner_->child)
                throw FormatError("live overview: mismatched League family owner");
            if(family->kind==ui::LeagueFamilyRequestKind::close) {
                auto preparation=close_league_family(*family,revision,error);
                if(!preparation)throw FormatError(error.empty()?"live overview: Source family close rejected":error);
                result.closed=true;result.preparation=std::move(*preparation);live_overview_owner_.reset();return result;
            }
            if(family->kind==ui::LeagueFamilyRequestKind::control && family->from_child==1) {
                auto page=dispatch_league_table_control(*family,revision,error);
                if(!page)throw FormatError(error.empty()?"live overview: Source Table control rejected":error);
                result.table=page->page;result.family_identity=page->identity;
                result.table_phase=page->phase;result.table_dialog=page->dialog;
            } else if(family->kind==ui::LeagueFamilyRequestKind::table_range_result && family->from_child==1) {
                auto page=dispatch_league_table_range_reply(*family,revision,error);
                if(!page)throw FormatError(error.empty()?"live overview: Source Table range reply rejected":error);
                result.table=page->page;result.family_identity=page->identity;
                result.table_phase=page->phase;result.table_dialog=page->dialog;
            } else if(family->kind==ui::LeagueFamilyRequestKind::table_forecast_presented && family->from_child==1) {
                auto page=acknowledge_league_forecast_present(*family,revision,error);
                if(!page)throw FormatError(error.empty()?"live overview: Source Table forecast ACK rejected":error);
                result.table=page->page;result.family_identity=page->identity;
                result.table_phase=page->phase;result.table_dialog=page->dialog;
            } else if(family->kind==ui::LeagueFamilyRequestKind::team_info && family->from_child==1) {
                auto identity=dispatch_family_table_team_info(*family,revision,error,team_info);
                if(!identity)throw FormatError(error.empty()?"live overview: Source Table TeamInfo rejected":error);
                auto page=current_league_table_dispatch(error);
                if(!page)throw FormatError(error.empty()?"live overview: Source Table return is unavailable":error);
                result.table=page->page;result.family_identity=page->identity;
                result.table_phase=page->phase;result.table_dialog=page->dialog;
            } else if((family->kind==ui::LeagueFamilyRequestKind::select_league ||
                       family->kind==ui::LeagueFamilyRequestKind::toggle_league_group) && family->from_child==1) {
                if(!select_league_family_page(*family,revision,error))
                    throw FormatError(error.empty()?"live overview: Source Table selector rejected":error);
                auto page=current_league_table_dispatch(error);
                if(!page)throw FormatError(error.empty()?"live overview: Source Table selector projection missing":error);
                result.table=page->page;result.family_identity=page->identity;
                result.table_phase=page->phase;result.table_dialog=page->dialog;
            } else if(family->kind==ui::LeagueFamilyRequestKind::navigate &&
                      family->target_child>=1 && family->target_child<=3) {
                if(family->from_child==3) {
                    if(!suspend_strengths_for_family(*family,revision,error))
                        throw FormatError(error.empty()?"live overview: Source Strengths child return rejected":error);
                } else if(family->from_child==2) {
                    if(!suspend_pairings_for_family(*family,revision,error))
                        throw FormatError(error.empty()?"live overview: Source Pairings child return rejected":error);
                }
                auto fresh=strengths_open_request(revision);
                if(!fresh)throw FormatError("live overview: Source family has no active manager projection");
                auto identity=fresh->identity;
                identity.family_id=family->identity.family_id;
                identity.family_sequence=family->identity.family_sequence+1;
                identity.league=family->identity.league;
                identity.league_group=family->identity.league_group;
                identity.manager_default_league=family->identity.manager_default_league;
                identity.source_param2=family->identity.source_param2;
                if(!prepare_league_family_child(identity,family->target_child,revision,error))
                    throw FormatError(error.empty()?"live overview: Source family child preparation rejected":error);
                identity.expected_family_word=g_.memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(identity.manager)*2);
                if(family->target_child==1) {
                    if(!advance_league_family(*family,identity,1,revision,error))
                        throw FormatError(error.empty()?"live overview: Source Table handoff rejected":error);
                    auto page=current_league_table_dispatch(error);
                    if(!page)throw FormatError(error.empty()?"live overview: Source Table handoff projection missing":error);
                    result.kind=LiveOverviewKind::league_table;
                    result.table=page->page;result.family_identity=page->identity;
                    result.table_phase=page->phase;result.table_dialog=page->dialog;
                } else if(family->target_child==2) {
                    LeaguePairingsDraft draft{g_,game_,rand_,club_modal_runtime_,club_modal_runtime_,{}, {},
                        modern::save_stream(g_),rand_.seed(),game_.seed(),rand_.calls(),game_.calls()};
                    for(const auto& group:engine.groups)draft.original_active_groups.emplace_back(group.desc,group.fixtures);
                    draft.original_referee_profiles=engine.referees;
                    draft.owner=modern::open_league_pairings_source(draft.state,identity.manager,ui::league_pairings_constants(exe_));
                    if(draft.owner.league!=identity.league) {
                        // Source Move chooses the family league before its first visible page.
                        draft.owner.league=identity.league;
                        draft.owner.round=std::min<int>(draft.state.league_rounds.at(size_t(identity.league)),
                            league_matchdays(identity.league)-1);
                        draft.owner.title_round=draft.state.league_rounds.at(size_t(identity.league));
                        if(draft.owner.round>=league_matchdays(identity.league))
                            throw FormatError("live overview: selected Source Pairings league round is outside its schedule");
                    }
                    draft.active_packet=validate_normal_pairings_group(draft.state,draft.owner,draft.runtime);
                    const uint64_t child_scope=++league_pairings_scope_;
                    auto page=ui::league_pairings_from_game(draft.state,draft.owner,exe_,revision,
                        child_scope,family->identity.family_sequence+1,draft.active_packet);
                    draft.identity=page.identity;
                    draft.runtime[::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField]=exe_.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField+uint32_t(draft.owner.league));
                    pairings_runtime_draw(draft.state,draft.runtime,page,exe_);
                    league_pairings_draft_=std::move(draft);
                    if(!advance_league_family(*family,identity,2,revision,error))
                        throw FormatError(error.empty()?"live overview: Source Pairings handoff rejected":error);
                    result.kind=LiveOverviewKind::league_pairings;
                    result.pairings=std::move(page);
                    result.family_identity=league_family_owner_->identity;
                } else {
                    ui::StrengthsRequest open=*fresh;open.identity=identity;open.kind=ui::StrengthsAction::open;
                    auto page=dispatch_strengths(open,revision,error,{},true);
                    if(!page||page->closed)throw FormatError(error.empty()?"live overview: Source Strengths entry rejected":error);
                    if(!advance_league_family(*family,page->page.identity,3,revision,error))
                        throw FormatError(error.empty()?"live overview: Source Strengths handoff rejected":error);
                    result.kind=LiveOverviewKind::league_strengths;
                    result.strengths=page->page;result.family_identity=page->page.identity;
                }
            } else {
                throw FormatError("live overview: unsupported Source family transition");
            }
            if(league_family_owner_) {
                result.family_identity=league_family_owner_->identity;
                live_overview_owner_->kind=result.kind;
                live_overview_owner_->family_identity=result.family_identity;
            }
            live_overview_owner_->request.revision=revision;
            return result;
        }
        if(const auto* strengths=std::get_if<ui::StrengthsRequest>(&action)) {
            if(live.kind!=LiveOverviewKind::league_strengths || !strengths_owner_ ||
               strengths->identity!=strengths_owner_->page.identity ||
               strengths->identity.revision!=revision || strengths->identity!=live.family_identity)
                throw FormatError("live overview: mismatched Source Strengths owner");
            if(strengths->kind==ui::StrengthsAction::table || strengths->kind==ui::StrengthsAction::pairings) {
                ui::LeagueFamilyRequest navigation;
                navigation.identity=live.family_identity;
                navigation.kind=ui::LeagueFamilyRequestKind::navigate;
                navigation.from_child=3;
                navigation.target_child=strengths->kind==ui::StrengthsAction::table?1:2;
                struct RestoreObserverScope { bool& flag; bool previous; explicit RestoreObserverScope(bool& f):flag(f),previous(f){flag=true;} ~RestoreObserverScope(){flag=previous;} } observer_scope(calendar_advance_active_);
                return dispatch_live_overview(LiveOverviewAction{navigation},engine,revision,error,team_info);
            }
            if(strengths->kind==ui::StrengthsAction::close) {
                ui::LeagueFamilyRequest close;close.identity=live.family_identity;
                close.kind=ui::LeagueFamilyRequestKind::close;close.from_child=3;close.target_child=0;
                auto preparation=close_league_family(close,revision,error);
                if(!preparation)throw FormatError(error.empty()?"live overview: Source Strengths close rejected":error);
                result.closed=true;result.preparation=std::move(*preparation);live_overview_owner_.reset();return result;
            }
            const auto prior=live.family_identity;
            auto page=dispatch_strengths(*strengths,revision,error,team_info,false);
            if(!page || page->closed)throw FormatError(error.empty()?"live overview: Source Strengths action rejected":error);
            if(!refresh_family_after_child(prior,3,page->page.identity,revision,error))
                throw FormatError(error.empty()?"live overview: Source Strengths ACK rejected":error);
            result.strengths=page->page;result.preparation=page->preparation;
            result.family_identity=page->page.identity;
            live_overview_owner_->family_identity=result.family_identity;
            live_overview_owner_->request.revision=revision;
            return result;
        }
        if(const auto* pairings=std::get_if<ui::LeaguePairingsRequest>(&action)) {
            if(live.kind!=LiveOverviewKind::league_pairings || !league_pairings_draft_ ||
               pairings->identity!=league_pairings_draft_->identity || pairings->identity.revision!=revision)
                throw FormatError("live overview: mismatched Source Pairings owner");
            if(pairings->event.kind==LeaguePairingsAction::close) {
                ui::LeagueFamilyRequest close;close.identity=live.family_identity;
                close.kind=ui::LeagueFamilyRequestKind::close;close.from_child=2;close.target_child=0;
                auto preparation=close_league_family(close,revision,error);
                if(!preparation)throw FormatError(error.empty()?"live overview: Source Pairings close rejected":error);
                result.closed=true;result.preparation=std::move(*preparation);live_overview_owner_.reset();return result;
            }
            auto page=dispatch_league_pairings(*pairings,revision,error,team_info);
            if(!page)throw FormatError(error.empty()?"live overview: Source Pairings action rejected":error);
            result.closed=page->closed;result.pairings=page->page;result.preparation=page->preparation;
            if(result.closed) {
                // The Source Move return is completed by the outer G/T wrapper.
                live_overview_owner_.reset();league_family_owner_.reset();
            } else {
                live_overview_owner_->request.revision=revision;
                if(league_family_owner_) {
                    live_overview_owner_->family_identity=league_family_owner_->identity;
                    result.family_identity=league_family_owner_->identity;
                }
            }
            return result;
        }
        const auto* cup=std::get_if<ui::OtherGamesRequest>(&action);
        if(!cup || live.kind!=LiveOverviewKind::cup_other_games || !other_games_owner_ ||
           cup->identity!=other_games_owner_->page.identity || cup->identity.revision!=revision)
            throw FormatError("live overview: mismatched Source cup overview owner");
        auto page=dispatch_other_games(*cup,revision,error,team_info);
        if(!page)throw FormatError(error.empty()?"live overview: Source cup overview action rejected":error);
        result.closed=page->closed;result.cup=page->page;result.preparation=page->preparation;
        if(result.closed)live_overview_owner_.reset();
        else live_overview_owner_->request.revision=revision;
        return result;
    } catch(const std::exception& e) {
        error=e.what();return {};
    }
}

bool ModernSession::begin_live_intervention(int minute, int source_mode, int manager, int team,
        MatchEngine& engine, uint64_t& revision, std::string& error) {
    error.clear();
    if (live_intervention_ || minute <= 2 || (source_mode != 2 && source_mode != 8) ||
        revision == std::numeric_limits<uint64_t>::max() || manager < 0 || manager >= int(g_.manager_teams.size()) ||
        team < 0 || team >= int(g_.team_flags.size()) || g_.manager_teams[size_t(manager)] != team ||
        g_.manager_of(team) != manager || &engine.state != &g_ || &engine.game != &game_ || &engine.rand != &rand_) {
        error = "Der Live-Eingriff kann für diesen Manager oder Modus nicht geöffnet werden.";
        return false;
    }
    bool current_match = false;
    for (const MatchGroup& group : engine.groups) for (int match = 0; match < group.matches(); ++match) {
        const uint32_t bit = uint32_t(1) << match;
        if ((group.kickoff_void_mask & bit) || (group.void_mask() & bit)) continue;
        const int home = group.fixtures.at(size_t(match) * 2);
        const int away = group.fixtures.at(size_t(match) * 2 + 1);
        if (std::find(g_.live_matches.begin(), g_.live_matches.end(), home) != g_.live_matches.end() &&
            (team == home || team == away)) current_match = true;
    }
    if (!current_match) {
        error = "Die Mannschaft spielt in der aktuellen Partie nicht.";
        return false;
    }
    LiveInterventionCheckpoint checkpoint;
    checkpoint.state = g_;
    checkpoint.groups = engine.groups;
    checkpoint.game = engine.game;
    checkpoint.rand = engine.rand;
    checkpoint.minute = minute; checkpoint.source_mode = source_mode;
    checkpoint.manager = manager; checkpoint.team = team;
    checkpoint.initial_revision = revision; checkpoint.current_revision = revision;
    live_intervention_ = std::move(checkpoint);
    if (source_mode == 2) {
        try {
            ModernState entered = g_;
            prepare_lineup_entry(entered, team); // PitchScreen's unconditional PrepareLineupEntry entry pass.
            g_ = std::move(entered);
        } catch (const std::exception& e) {
            live_intervention_.reset();
            error = std::string("Die Live-Aufstellung kann nicht geöffnet werden: ") + e.what();
            return false;
        }
    }
    if (g_.memory.has(::bmh::layout::ActiveManager)) g_.memory.set8(::bmh::layout::ActiveManager, uint8_t(manager));
    return true;
}

bool ModernSession::begin_live_event_intervention(const LiveMatchEvent& event, MatchEngine& engine,
        uint64_t& revision, std::string& error) {
    error.clear();
    const int manager = event.manager_slot, team = event.team;
    if (live_intervention_ || event.minute < 1 || (event.kind != 1 && event.kind != 2) ||
        event.group_index >= engine.groups.size() || event.match_index < 0 || event.side < 0 || event.side > 1 ||
        manager < 0 || manager >= int(g_.manager_teams.size()) || team < 0 ||
        team >= int(g_.team_flags.size()) || event.player < 0 || event.player >= int(g_.players.size()) ||
        g_.manager_teams[size_t(manager)] != team || g_.manager_of(team) != manager ||
        g_.manager_option(manager, 1) || engine.live_desk_disabled || !(g_.team_morale.at(size_t(team)) & 0x80) ||
        &engine.state != &g_ || &engine.game != &game_ || &engine.rand != &rand_ ||
        revision == std::numeric_limits<uint64_t>::max()) {
        error = "Der automatische Live-Eingriff gehört nicht zu einem aktuellen Source-Ereignis.";
        return false;
    }
    const MatchGroup& group = engine.groups[event.group_index];
    if (event.match_index >= group.matches() ||
        group.fixtures.at(size_t(event.match_index) * 2 + size_t(event.side)) != team ||
        group.fixtures.at(size_t(event.match_index) * 2 + size_t(1 - event.side)) != event.opponent ||
        (group.kickoff_void_mask >> event.match_index & 1) || (group.void_mask() >> event.match_index & 1) ||
        std::find(g_.live_matches.begin(), g_.live_matches.end(), group.fixtures.at(size_t(event.match_index) * 2)) ==
            g_.live_matches.end()) {
        error = "Das Source-Ereignis gehört nicht zur aktuell gezeigten Partie.";
        return false;
    }
    // The Source event already changed this exact player/slot before opening PitchScreen.
    bool event_player_found = false;
    const auto range = squad_range(team);
    for (size_t offset = 0; offset < range.count; ++offset) {
        const size_t slot = size_t(range.first) + offset;
        const uint16_t entry = g_.squad_slots.at(slot);
        if ((entry & 0x0fff) != event.player) continue;
        if (event.kind == 2 && (g_.players.at(size_t(event.player)).w35c >> 27) != 0 && !(entry & 0x8000))
            event_player_found = true;
        if (event.kind == 1 && (g_.players.at(size_t(event.player)).w368 & 0x07000000u) != 0 && !(entry & 0x8000))
            event_player_found = true;
    }
    if (!event_player_found) {
        error = "Der Source-Spielerstatus passt nicht mehr zum automatischen Eingriff.";
        return false;
    }
    LiveInterventionCheckpoint checkpoint;
    checkpoint.state = g_; checkpoint.groups = engine.groups;
    checkpoint.game = engine.game; checkpoint.rand = engine.rand;
    checkpoint.minute = event.minute; checkpoint.source_mode = 2;
    checkpoint.manager = manager; checkpoint.team = team; checkpoint.source_event = true;
    checkpoint.event = event; checkpoint.initial_revision = revision; checkpoint.current_revision = revision;
    live_intervention_ = std::move(checkpoint);
    try {
        ModernState entered = g_;
        prepare_lineup_entry(entered, team);
        g_ = std::move(entered);
    } catch (const std::exception& e) {
        live_intervention_.reset();
        error = std::string("Die automatische Live-Aufstellung kann nicht geöffnet werden: ") + e.what();
        return false;
    }
    if (g_.memory.has(::bmh::layout::ActiveManager)) g_.memory.set8(::bmh::layout::ActiveManager, uint8_t(manager));
    return true;
}

bool ModernSession::finish_live_event_intervention(const LiveMatchEvent& event, MatchEngine& engine,
        uint64_t& revision, std::string& error) {
    error.clear();
    if (!live_intervention_ || !live_intervention_->source_event ||
        live_intervention_->event.group_index != event.group_index ||
        live_intervention_->event.match_index != event.match_index ||
        live_intervention_->event.side != event.side || live_intervention_->event.team != event.team ||
        live_intervention_->event.opponent != event.opponent ||
        live_intervention_->event.player != event.player || live_intervention_->event.kind != event.kind ||
        live_intervention_->minute != event.minute || live_intervention_->manager != event.manager_slot ||
        live_intervention_->current_revision != revision || &engine.state != &g_ ||
        &engine.game != &game_ || &engine.rand != &rand_ || revision == std::numeric_limits<uint64_t>::max()) {
        error = "Der automatische Live-Menüabschluss ist veraltet oder gehört zu einem anderen Ereignis.";
        return false;
    }
    if (event.group_index >= engine.groups.size() || event.match_index < 0 || event.side < 0 || event.side > 1) {
        error = "Das Source-Ereignis gehört nicht mehr zur aktiven Partie.";
        return false;
    }
    const MatchGroup& group = engine.groups[event.group_index];
    if (event.match_index >= group.matches() ||
        group.fixtures.at(size_t(event.match_index) * 2 + size_t(event.side)) != event.team ||
        group.fixtures.at(size_t(event.match_index) * 2 + size_t(1 - event.side)) != event.opponent ||
        (group.kickoff_void_mask >> event.match_index & 1) || (group.void_mask() >> event.match_index & 1)) {
        error = "Die aktive Partie hat sich während der automatischen Aufstellung geändert.";
        return false;
    }
    if (engine.groups.size() != live_intervention_->groups.size() ||
        group.desc != live_intervention_->groups[event.group_index].desc ||
        group.fixtures != live_intervention_->groups[event.group_index].fixtures) {
        error = "Die Matchgruppenstruktur hat sich während der Source-Aufstellung geändert.";
        return false;
    }
    // PitchScreen returns to EventImpl; its single Source rerate remains at the event's
    // existing tail, so this boundary only consumes the authenticated modal owner.
    ++revision;
    live_intervention_.reset();
    return true;
}

bool ModernSession::finish_live_intervention(int minute, int source_mode, int manager, int team,
        MatchEngine& engine, uint64_t& revision, std::string& error) {
    error.clear();
    if (!live_intervention_ || live_intervention_->minute != minute ||
        live_intervention_->source_mode != source_mode || live_intervention_->manager != manager ||
        live_intervention_->team != team || live_intervention_->current_revision != revision ||
        &engine.state != &g_ || &engine.game != &game_ || &engine.rand != &rand_) {
        error = "Der Live-Menüabschluss ist veraltet oder gehört zu einem anderen Eingriff.";
        return false;
    }
    const LiveInterventionCheckpoint& checkpoint = *live_intervention_;
    ModernState staged_state = g_;
    std::vector<MatchGroup> staged_groups = engine.groups;
    GameRandom staged_game = engine.game;
    WatcomRandom staged_rand = engine.rand;
    MatchEngine staged_engine(staged_state, staged_groups, staged_game, staged_rand, engine.referees);
    staged_engine.indoor = engine.indoor;
    staged_engine.alternate_event_roll = engine.alternate_event_roll;
    staged_engine.match_events_disabled = engine.match_events_disabled;
    staged_engine.live_scene_visible = engine.live_scene_visible;
    staged_engine.live_desk_disabled = engine.live_desk_disabled;
    staged_engine.month = engine.month;
    staged_engine.trace = engine.trace;
    staged_engine.show = engine.show;
    staged_engine.leave_screens = engine.leave_screens;
    staged_engine.buttons = engine.buttons;
    MinuteReport report;
    staged_engine.rerate_team(team, report);
    if (report.unsupported) {
        g_ = checkpoint.state;
        engine.groups = checkpoint.groups;
        engine.game = checkpoint.game;
        engine.rand = checkpoint.rand;
        revision = checkpoint.initial_revision;
        live_intervention_.reset();
        error = "Die Mannschaft kann nach dem Live-Eingriff nicht neu bewertet werden: " + report.reason;
        return false;
    }
    g_ = std::move(staged_state);
    engine.groups = std::move(staged_groups);
    engine.game = staged_game;
    engine.rand = staged_rand;
    if (revision == std::numeric_limits<uint64_t>::max()) {
        g_ = checkpoint.state;
        engine.groups = checkpoint.groups;
        engine.game = checkpoint.game;
        engine.rand = checkpoint.rand;
        revision = checkpoint.initial_revision;
        live_intervention_.reset();
        error = "Die Live-Menürevisionsnummer ist erschöpft.";
        return false;
    }
    ++revision;
    live_intervention_.reset();
    return true;
}

void ModernSession::abort_live_intervention(int minute, int source_mode, int manager, int team,
        MatchEngine& engine, uint64_t& revision) {
    if (!live_intervention_ || live_intervention_->minute != minute ||
        live_intervention_->source_mode != source_mode || live_intervention_->manager != manager ||
        live_intervention_->team != team || &engine.state != &g_ ||
        &engine.game != &game_ || &engine.rand != &rand_) return;
    g_ = std::move(live_intervention_->state);
    if (live_intervention_->source_event && engine.groups.size() == live_intervention_->groups.size()) {
        // Preserve MatchGroup element addresses held by MatchEngine::event/side;
        // side reacquires its record byte pointers immediately after this callback.
        for (size_t i = 0; i < engine.groups.size(); ++i)
            engine.groups[i] = std::move(live_intervention_->groups[i]);
    } else {
        engine.groups = std::move(live_intervention_->groups);
    }
    engine.game = live_intervention_->game;
    engine.rand = live_intervention_->rand;
    revision = live_intervention_->initial_revision;
    live_intervention_.reset();
}

bool ModernSession::dispatch_live_substitution(const ui::LineupRequest& request, int minute,
        int source_mode, MatchEngine& engine, uint64_t& revision, std::string& error) {
    error.clear();
    // PitchScreen stays open for repeated actions; EndPhase rerates once after
    // event 8 closes either live menu.
    if (source_mode != 2 || request.kind != ui::LineupRequestKind::swap_players ||
        (minute <= 2 && (!live_intervention_ || !live_intervention_->source_event))) {
        error = "Der Eingriff ist in dieser Spielminute nicht verfügbar.";
        return false;
    }
    const int active = request.manager_slot;
    const int team = request.runtime_club;
    if (!live_intervention_ || live_intervention_->minute != minute ||
        live_intervention_->source_mode != source_mode || live_intervention_->manager != active ||
        live_intervention_->team != team || live_intervention_->current_revision != revision ||
        request.revision != revision || revision == std::numeric_limits<uint64_t>::max() ||
        active < 0 || active > 3 || request.manager_slot != active || team < 0 ||
        size_t(active) >= g_.manager_teams.size() || g_.manager_teams[size_t(active)] != team ||
        g_.manager_of(team) != active || team >= int(g_.team_flags.size()) ||
        &engine.state != &g_ || &engine.game != &game_ || &engine.rand != &rand_) {
        error = "Der Eingriff ist veraltet oder gehört nicht zum aktiven Manager.";
        return false;
    }
    const bool current_match = std::any_of(engine.groups.begin(), engine.groups.end(), [this, team](const MatchGroup& group) {
            for (int match = 0; match < group.matches(); ++match) {
                const uint32_t bit = uint32_t(1) << match;
                if ((group.kickoff_void_mask & bit) || (group.void_mask() & bit)) continue;
                const int home = group.fixtures.at(size_t(match) * 2);
                const int away = group.fixtures.at(size_t(match) * 2 + 1);
                if (std::find(g_.live_matches.begin(), g_.live_matches.end(), home) != g_.live_matches.end() &&
                    (team == home || team == away)) return true;
            }
            return false;
        });
    if (!current_match) {
        error = "Die Mannschaft spielt in der aktuellen Partie nicht.";
        return false;
    }
    const auto range = squad_range(team);
    const bool first_field = request.first_row >= 0 && request.first_row < 11;
    const bool second_field = request.second_row >= 0 && request.second_row < 11;
    const bool same_zone_exchange = first_field == second_field;
    if (request.first_row < 0 || request.second_row < 0 ||
        size_t(request.first_row) >= range.count || size_t(request.second_row) >= range.count ||
        request.first_row == request.second_row ||
        range.first + range.count > g_.squad_slots.size()) {
        error = "Die Spielerzeilen sind für den Live-Tausch ungültig.";
        return false;
    }
    const uint16_t outgoing = g_.squad_slots[range.first + size_t(request.first_row)];
    const uint16_t incoming = g_.squad_slots[range.first + size_t(request.second_row)];
    const uint16_t same_zone_role = first_field ? 0x8000 : 0x4000;
    if (outgoing != request.first_slot || incoming != request.second_slot ||
        (outgoing & 0x0fff) != request.first_player ||
        (incoming & 0x0fff) != request.second_player ||
        (same_zone_exchange ? ((outgoing & 0xc000) != same_zone_role || (incoming & 0xc000) != same_zone_role) :
            (first_field ? (!(outgoing & 0x8000) || !(incoming & 0x4000)) :
                           (!(incoming & 0x8000) || !(outgoing & 0x4000)))) ||
        request.first_player < 0 || request.second_player < 0) {
        error = "Die Kaderauswahl hat sich inzwischen geändert.";
        return false;
    }
    if (size_t(request.first_player) >= g_.players.size() || size_t(request.second_player) >= g_.players.size()) {
        error = "Die Spielerkennung liegt außerhalb des Kaders.";
        return false;
    }
    const Player& first_player = g_.players.at(size_t(request.first_player));
    const Player& second_player = g_.players.at(size_t(request.second_player));
    const auto injury_status = [](const Player& p) { return int(p.w364 >> 16 & 0x1f); };
    // PitchScreen checks both selected players before it distinguishes same-zone
    // tactical exchanges from field/bench substitutions.
    if (((first_player.w35c >> 27) == 0 && injury_status(first_player) != 0) ||
        ((second_player.w35c >> 27) == 0 && injury_status(second_player) != 0)) {
        error = "source_notice:0xaf13c";
        return false;
    }
    if (!same_zone_exchange) {
        const int field_row = first_field ? request.first_row : request.second_row;
        const int bench_row = first_field ? request.second_row : request.first_row;
        const int field_player = first_field ? request.first_player : request.second_player;
        const int bench_player = first_field ? request.second_player : request.first_player;
        const Player& field = g_.players.at(size_t(field_player));
        const Player& bench = g_.players.at(size_t(bench_player));
        const int field_status = int(field.w364 >> 16 & 0x1f);
        const int bench_status = int(bench.w364 >> 16 & 0x1f);
        if (field_status != 0 && (field.w35c >> 27) == 0) {
            error = "source_notice:0xaf140";
            return false;
        }
        if (bench_status != 0) {
            error = "source_notice:0xaf144";
            return false;
        }
        const int used = int(g_.team_flags.at(size_t(team)) >> 5 & 7);
        const int max_substitutions = source_runtime_byte(g_,club_modal_runtime_,exe_,::bmh::layout::CareerSession_DispatchLiveSubstitution_ExecutableField);
        if (used >= max_substitutions) {
            error = "source_notice:0xaf150";
            return false;
        }
        const bool foreign_limit = g_.memory.has(::bmh::layout::ForeignPlayerLimitEnabled) ? g_.memory.u8(::bmh::layout::ForeignPlayerLimitEnabled) != 0 : exe_.u8(::bmh::layout::ForeignPlayerLimitEnabled) != 0;
        if (foreign_limit) {
            int foreign_on_field = 0;
            for (size_t row = 0; row < range.count; ++row) {
                const uint16_t slot = g_.squad_slots.at(range.first + row);
                if (!(slot & 0x8000) || (slot & 0x0fff) == 0x0fff) continue;
                if (modern::nationality(g_, slot & 0x0fff) != 0) ++foreign_on_field;
            }
            foreign_on_field -= modern::nationality(g_, field_player) != 0;
            foreign_on_field += modern::nationality(g_, bench_player) != 0;
            if (foreign_on_field >= 4) {
                error = "source_notice:0xaf148";
                return false;
            }
        }
        if (bench.w364 & 0x1000) {
            error = "source_notice:0xaf14c";
            return false;
        }
    }

    // Source updates the squad before leaving the child menu; BuildSideRecords3 belongs
    // to the single parent-menu completion callback.
    ModernState staged_state = g_;
    if (same_zone_exchange) {
        const size_t first = range.first + size_t(request.first_row);
        const size_t second = range.first + size_t(request.second_row);
        const size_t board = size_t(active) * 0x840;
        if (board + (size_t(std::max(request.first_row, request.second_row)) + 1) * 0x58 >
            staged_state.tactics.size()) {
            error = "Die taktischen Spielerdaten sind unvollständig.";
            return false;
        }
        auto* a = staged_state.tactics.data() + board + size_t(request.first_row) * 0x58;
        auto* b = staged_state.tactics.data() + board + size_t(request.second_row) * 0x58;
        const uint8_t ax = a[0x41], ay = a[0x42], bx = b[0x41], by = b[0x42];
        staged_state.squad_slots[first] = uint16_t((incoming & 0x1fff) | (outgoing & 0xc000));
        staged_state.squad_slots[second] = uint16_t((outgoing & 0x1fff) | (incoming & 0xc000));
        std::swap_ranges(a, a + 0x58, b);
        a[0x41] = ax; a[0x42] = ay; b[0x41] = bx; b[0x42] = by;
        // GatedByParamLive (the desk-card remap) is gated by param_2==0; a live
        // marked exchange changes the selected rows but leaves player_cards alone.
    } else {
        const int field_row = first_field ? request.first_row : request.second_row;
        const int bench_row = first_field ? request.second_row : request.first_row;
        const auto staged_range = squad_range(team);
        const size_t field_at = staged_range.first + size_t(field_row);
        const size_t bench_at = staged_range.first + size_t(bench_row);
        const uint16_t field_slot = staged_state.squad_slots.at(field_at);
        const uint16_t bench_slot = staged_state.squad_slots.at(bench_at);
        const int used = int(staged_state.team_flags.at(size_t(team)) >> 5 & 7);
        if (!(field_slot & 0x8000) || !(bench_slot & 0x4000) ||
            (bench_slot & 0x0fff) == 0x0fff || used >= source_runtime_byte(g_,club_modal_runtime_,exe_,::bmh::layout::CareerSession_DispatchLiveSubstitution_ExecutableField) ||
            (staged_state.players.at(bench_slot & 0x0fff).w364 & 0x1000)) {
            error = "Die Auswechslung erfüllt die Source-Bedingungen nicht.";
            return false;
        }
        const size_t first = std::min(field_at, bench_at), second = std::max(field_at, bench_at);
        const size_t board = size_t(active) * 0x840;
        if (board + (size_t(bench_row) + 1) * 0x58 > staged_state.tactics.size()) {
            error = "Die taktischen Spielerdaten sind unvollständig.";
            return false;
        }
        auto* a = staged_state.tactics.data() + board + size_t(field_row) * 0x58;
        auto* b = staged_state.tactics.data() + board + size_t(bench_row) * 0x58;
        const uint8_t fx = a[0x41], fy = a[0x42], bx = b[0x41], by = b[0x42];
        b[0] = uint8_t(minute); // EndPhase passes minute + 1 as PitchScreen param_2; source stores param_2 - 1.
        staged_state.squad_slots[first] = bench_slot;
        staged_state.squad_slots[second] = field_slot;
        std::swap_ranges(a, a + 0x58, b);
        a[0x41] = fx; a[0x42] = fy; b[0x41] = bx; b[0x42] = by;
        staged_state.squad_slots[bench_at] &= 0x1fff;
        staged_state.squad_slots[field_at] &= 0x1fff;
        staged_state.players.at(bench_slot & 0x0fff).w364 |= 0x1000;
        staged_state.squad_slots[field_at] |= 0x8000;
        staged_state.squad_slots[bench_at] |= 0x4000;
        staged_state.team_flags.at(size_t(team)) = uint16_t(
            (staged_state.team_flags.at(size_t(team)) & 0xff1f) | uint16_t(used + 1) << 5);
    }
    g_ = std::move(staged_state);
    ++revision;
    live_intervention_->current_revision = revision;
    return true;
}

LiveSubstitutionResult ModernSession::dispatch_live_substitution_result(
        const ui::LineupRequest& request, int minute, int source_mode,
        MatchEngine& engine, uint64_t& revision) {
    std::string error;
    if (dispatch_live_substitution(request, minute, source_mode, engine, revision, error))
        return {LiveSubstitutionStatus::applied, 0, request.manager_slot, {}};
    constexpr std::string_view prefix = "source_notice:";
    if (error.compare(0, prefix.size(), prefix) == 0) {
        uint32_t address = 0;
        try { address = uint32_t(std::stoul(error.substr(prefix.size()), nullptr, 16)); }
        catch (...) { return {LiveSubstitutionStatus::rejected, 0, request.manager_slot,
            "Invalid internal Source notice address."}; }
        return {LiveSubstitutionStatus::source_notice, address, request.manager_slot, {}};
    }
    return {LiveSubstitutionStatus::rejected, 0, request.manager_slot, std::move(error)};
}

bool ModernSession::dispatch_live_tactic(const ui::TacticsRequest& request, int minute,
        int source_mode, MatchEngine& engine, uint64_t& revision, std::string& error) {
    error.clear();
    if (minute <= 2 || source_mode != 8 || request.kind != ui::TacticsRequestKind::set_tactic) {
        error = "Die Taktikänderung ist in dieser Spielminute nicht verfügbar.";
        return false;
    }
    const int manager = request.manager_slot;
    const int team = request.runtime_club;
    if (!live_intervention_ || live_intervention_->minute != minute ||
        live_intervention_->source_mode != source_mode || live_intervention_->manager != manager ||
        live_intervention_->team != team || live_intervention_->current_revision != revision ||
        request.revision != revision || revision == std::numeric_limits<uint64_t>::max() ||
        manager < 0 || manager >= int(g_.manager_teams.size()) || team < 0 ||
        team >= int(g_.team_flags.size()) || team != g_.manager_teams[size_t(manager)] ||
        g_.manager_of(team) != manager || &engine.state != &g_ || &engine.game != &game_ || &engine.rand != &rand_) {
        error = "Die Taktikanfrage ist veraltet oder gehört nicht zum aktiven Manager.";
        return false;
    }
    bool selected_match = false;
    for (const MatchGroup& group : engine.groups) {
        for (int match = 0; match < group.matches(); ++match) {
            const uint32_t bit = uint32_t(1) << match;
            if ((group.kickoff_void_mask & bit) || (group.void_mask() & bit)) continue;
            const int home = group.fixtures.at(size_t(match) * 2);
            const int away = group.fixtures.at(size_t(match) * 2 + 1);
            if (std::find(g_.live_matches.begin(), g_.live_matches.end(), home) != g_.live_matches.end() &&
                (team == home || team == away)) selected_match = true;
        }
    }
    if (!selected_match) {
        error = "Die Mannschaft spielt nicht in der aktuell ausgewählten Partie.";
        return false;
    }
    if (request.index < 0 || request.index >= 8 || request.desired_value < 0 ||
        request.desired_value > exe_.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField + request.index)) {
        error = "Ungültige Taktikeinstellung.";
        return false;
    }
    const bool live_option = (g_.memory.has(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3) ? g_.memory.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3) : exe_.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3)) != 0;
    if (live_option && exe_.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField2 + request.index) == 0) {
        error = "Diese Taktikeinstellung ist nicht verfügbar.";
        return false;
    }
    if ((request.index == 0 && request.desired_assistant_attitude != std::optional<uint8_t>(0)) ||
        (request.index != 0 && request.desired_assistant_attitude)) {
        error = "Ungültige Assistenteneinstellung.";
        return false;
    }
    const size_t record = size_t(manager) * 0xb0c;
    const size_t option = size_t(manager) * 5 + 2;
    if (g_.manager_records.size() < record + 0xb0c || g_.manager_options.size() <= option) {
        error = "Die Taktikdaten sind unvollständig.";
        return false;
    }
    // RerateTeam commits each click to the manager record while the menu stays
    // open; EndPhase invokes BuildSideRecords3 exactly once after menu exit.
    g_.manager_records[record + 0xd2 + size_t(request.index)] = uint8_t(request.desired_value);
    if (request.index == 0) g_.manager_options[option] = 0;
    ++revision;
    live_intervention_->current_revision = revision;
    return true;
}

bool ModernSession::dispatch_pitch(const ui::PitchRequest& request, uint64_t& revision,
                                    std::string& error) {
    if (request.kind == ui::PitchRequestKind::swap_players) {
        if (request.runtime_club < 0 || request.runtime_club >= int(g_.team_flags.size()) ||
            size_t(request.runtime_club + 1) * 2 > g_.club_heap_78.size() ||
            request.indoor != g_.indoor) {
            error = "Der Hallen-/Außenmodus der Spielfeldanfrage ist veraltet.";
            return false;
        }
        ui::LineupRequest lineup;
        lineup.kind = ui::LineupRequestKind::swap_players;
        lineup.manager_slot = request.manager_slot; lineup.runtime_club = request.runtime_club;
        lineup.revision = request.revision;
        lineup.first_row = request.row; lineup.first_player = request.player_id; lineup.first_slot = request.slot;
        lineup.second_row = request.second_row; lineup.second_player = request.second_player;
        lineup.second_slot = request.second_slot;
        return dispatch_lineup(lineup, revision, error);
    }
    if (request.kind != ui::PitchRequestKind::move_player &&
        request.kind != ui::PitchRequestKind::set_automatic_weight &&
        request.kind != ui::PitchRequestKind::apply_formation &&
        request.kind != ui::PitchRequestKind::save_formation &&
        request.kind != ui::PitchRequestKind::open_set_piece_editor &&
        request.kind != ui::PitchRequestKind::close_set_piece_editor &&
        request.kind != ui::PitchRequestKind::set_piece_taker &&
        request.kind != ui::PitchRequestKind::auto_set_piece_takers &&
        request.kind != ui::PitchRequestKind::set_player_order &&
        request.kind != ui::PitchRequestKind::select_pitch_page &&
        request.kind != ui::PitchRequestKind::set_player_marking) {
        error = "Diese Spielfeldaktion ist noch nicht verfügbar.";
        return false;
    }
    // PitchRequest has runtime ownership and a revision, not a caller-supplied
    // stable-club token. Cross-check the live mapping and record just as for the
    // semantic desk setters, including their tracked phase/modal exclusions.
    const int count = g_.memory.has(::bmh::layout::ManagerCount) ? int(g_.memory.u8(::bmh::layout::ManagerCount)) : -1;
    if (count < 1 || count > 4 || g_.manager_teams.size() != size_t(count) ||
        request.manager_slot < 0 || request.manager_slot >= count) {
        error = "Die Managerdaten der Spielfeldaktion sind unvollständig.";
        return false;
    }
    DeskSettingIdentity identity{request.manager_slot, request.runtime_club, std::nullopt, request.revision};
    if (request.runtime_club >= 0 && size_t(request.runtime_club) < g_.club_ids.size())
        identity.stable_club_identity = int(g_.club_ids[size_t(request.runtime_club)]);
    // PitchScreen exposes the prepared Pitch view during initial setup as well as
    // during manager desks. A verified line-up owner in the still-empty turn loop
    // is sufficient authority there; ordinary automation/ticket settings remain
    // desk-only. Keep all nested desk mutations blocked while allowing this Source
    // line-up window before the first desk has been created.
    const bool live_context = live_intervention_ && live_intervention_->source_mode == 2 &&
        request.manager_slot == live_intervention_->manager &&
        request.runtime_club == live_intervention_->team &&
        request.revision == live_intervention_->current_revision && request.revision == revision;
    const bool desk_phase = turn_driver_.stage == TurnDriver::Stage::MatchdayDesks ||
        turn_driver_.stage == TurnDriver::Stage::SeasonEndDesk;
    const bool initial_lineup_phase = turn_driver_.stage == TurnDriver::Stage::Empty &&
        lineup_entry_manager_ == request.manager_slot && lineup_entry_club_ == request.runtime_club;
    if (!desk_phase && !initial_lineup_phase && !live_context) { error = "desk_setting_unavailable"; return false; }
    if (!live_context && (pending_offer_dispatch_active_ || market_dispatch_active_ || calendar_advance_active_ ||
        transfer_offer_editor_ || manager_join_ || contract_quote_ || stadium_.investment ||
        stadium_.tip || stadium_.periphery_quote)) {
        error = "desk_setting_unavailable"; return false;
    }
    if (live_context) {
        const int manager = live_intervention_->manager;
        const int team = live_intervention_->team;
        const size_t rec = size_t(manager) * 0xb0c;
        if (request.indoor != g_.indoor || manager < 0 || manager >= int(g_.manager_teams.size()) ||
            g_.manager_teams[size_t(manager)] != team || g_.manager_of(team) != manager ||
            size_t(team) >= g_.club_ids.size() || rec + 0xb0c > g_.manager_records.size() ||
            (int(g_.manager_records[rec + 0x290]) | int(g_.manager_records[rec + 0x291]) << 8) != team) {
            error = "Die Spielfeldaktion gehört nicht zum aktiven Live-Manager."; return false;
        }
    } else if (!validate_desk_setting_owner(identity, revision, error, false)) return false;
    const size_t badge = size_t(request.runtime_club) * 2;
    const int mode = g_.memory.has(::bmh::layout::GameMode) ? int(g_.memory.u8(::bmh::layout::GameMode)) : int(exe_.u8(::bmh::layout::GameMode));
    if (request.runtime_club >= int(exe_.u16(::bmh::layout::CareerSession_DispatchLineup_RuntimeField)) || g_.lineups_frozen ||
        mode < 1 || mode > 3 || g_.manager_options[size_t(request.manager_slot) * 5] > 8 ||
        request.indoor != g_.indoor ||
        (!live_context && (lineup_entry_manager_ != request.manager_slot ||
        lineup_entry_club_ != request.runtime_club)) || badge + 1 >= g_.club_heap_78.size()) {
        error = "Diese Spielfeldaktion ist nicht für die vorbereitete Aufstellung freigegeben.";
        return false;
    }
    if ((request.kind == ui::PitchRequestKind::apply_formation ||
         request.kind == ui::PitchRequestKind::save_formation) && request.new_weights != request.old_weights) {
        error = "Die Gewichtung der Aufstellungsanfrage wurde geändert.";
        return false;
    }
    const auto selected_live_opponent = [&](int opponent) {
        if (!live_context || opponent < 0 || opponent >= int(exe_.u16(::bmh::layout::CareerSession_DispatchLineup_RuntimeField))) return false;
        return std::any_of(live_intervention_->groups.begin(), live_intervention_->groups.end(),
            [&](const MatchGroup& group) {
                for (int match = 0; match < group.matches(); ++match) {
                    const uint32_t bit = uint32_t(1) << match;
                    if ((group.kickoff_void_mask & bit) || (group.void_mask() & bit)) continue;
                    const int home = group.fixtures.at(size_t(match) * 2);
                    const int away = group.fixtures.at(size_t(match) * 2 + 1);
                    if (std::find(g_.live_matches.begin(), g_.live_matches.end(), home) != g_.live_matches.end() &&
                        ((home == request.runtime_club && away == opponent) ||
                         (away == request.runtime_club && home == opponent))) return true;
                }
                return false;
            });
    };
    const auto selected_source_opponent = [&](int opponent) {
        if (live_context) return selected_live_opponent(opponent);
        const auto prepared = prepared_lineup_opponent(request.manager_slot,
            request.runtime_club, request.revision);
        return prepared && *prepared == opponent;
    };
    if (request.kind == ui::PitchRequestKind::select_pitch_page && request.page_control == 4 &&
        (request.indoor || g_.indoor || !selected_source_opponent(request.opponent_team))) {
        error = "Die Manndeckungsseite ist für diese Partie nicht verfügbar.";
        return false;
    }
    if (request.kind == ui::PitchRequestKind::set_player_marking &&
        (request.indoor || g_.indoor || !selected_source_opponent(request.opponent_team))) {
        error = "Die Manndeckung gehört nicht zur aktuellen Außenspielpartie.";
        return false;
    }
    try {
        ModernState staged = g_;
        bool accepted = false;
        const size_t manager_rec = size_t(request.manager_slot) * 0xb0c;
        static constexpr size_t set_piece_offsets[] = {0xafc,0xafe,0xb08};
        std::array<uint16_t,3> current_set_piece{};
        for(size_t i=0;i<3;++i){const size_t at=manager_rec+set_piece_offsets[i];
            current_set_piece[i]=uint16_t(staged.manager_records.at(at)|uint16_t(staged.manager_records.at(at+1))<<8);}
        const uint32_t display_at=uint32_t(::bmh::layout::CareerSession_AnyOf_ExecutableField+request.manager_slot*2);
        const bool editor_action=request.kind==ui::PitchRequestKind::open_set_piece_editor||
            request.kind==ui::PitchRequestKind::close_set_piece_editor||
            request.kind==ui::PitchRequestKind::set_piece_taker||
            request.kind==ui::PitchRequestKind::auto_set_piece_takers;
        if(request.kind==ui::PitchRequestKind::select_pitch_page&&
           (!staged.memory.has(display_at)||request.expected_display_word!=staged.memory.u16(display_at)||
            request.expected_set_piece_values!=current_set_piece||
            request.expected_set_piece_signature!=uint16_t(staged.manager_records.at(manager_rec+0xb0a)|uint16_t(staged.manager_records.at(manager_rec+0xb0b))<<8)||
            request.expected_mode!=mode||request.indoor!=staged.indoor||request.page_control<0||request.page_control>4)){
            error="Der Seitenwechsel am Spielfeld ist veraltet oder ungültig.";return false;
        }
        if (request.kind == ui::PitchRequestKind::set_player_marking) {
            if (!staged.memory.has(display_at) ||
                request.expected_display_word != staged.memory.u16(display_at) ||
                (request.expected_display_word & 0x0040) == 0 || staged.indoor ||
                request.opponent_team < 0 || !selected_source_opponent(request.opponent_team) ||
                size_t(request.opponent_team) >= staged.team_flags.size()) {
                error = "Die Manndeckungsanfrage ist veraltet oder nicht auf der Source-Seite aktiv.";
                return false;
            }
            const SquadRange own = squad_range(request.runtime_club);
            const SquadRange opponent = squad_range(request.opponent_team);
            const size_t own_board = size_t(request.manager_slot) * 0x840;
            if (own.count < 11 || opponent.count < 11 || own.first + 11 > staged.squad_slots.size() ||
                opponent.first + 11 > staged.squad_slots.size() || own_board + 11 * 0x58 > staged.tactics.size()) {
                error = "Die Mannschaftstaktik enthält keine vollständigen Spielerzeilen.";
                return false;
            }
            if (request.opponent_row < 1 || request.opponent_row >= 11 ||
                staged.squad_slots.at(opponent.first + size_t(request.opponent_row)) != request.opponent_slot ||
                (request.opponent_slot & 0xc000) != 0x8000 ||
                (request.opponent_slot & 0x0fff) != request.opponent_player ||
                request.opponent_player < 0 || size_t(request.opponent_player) >= staged.players.size()) {
                error = "Der ausgewählte Gegenspieler gehört nicht mehr zur gegnerischen Feldaufstellung.";
                return false;
            }
            bool clear = request.row == -1 && request.player_id == -1;
            if (clear) {
                if (request.expected_marking_player_id != uint16_t(request.opponent_player)) {
                    error = "Die zu löschende Manndeckung ist veraltet.";
                    return false;
                }
            } else {
                if (request.row < 0 || request.row >= 11 ||
                    staged.squad_slots.at(own.first + size_t(request.row)) != request.slot ||
                    (request.slot & 0xc000) != 0x8000 || (request.slot & 0x0fff) != request.player_id ||
                    request.player_id < 0 || size_t(request.player_id) >= staged.players.size()) {
                    error = "Der ausgewählte eigene Feldspieler gehört nicht mehr zur Aufstellung.";
                    return false;
                }
                const size_t own_row_at = own_board + size_t(request.row) * 0x58;
                const uint16_t current = uint16_t(staged.tactics.at(own_row_at + 0x44) |
                    uint16_t(staged.tactics.at(own_row_at + 0x45)) << 8);
                if (current != request.expected_marking_player_id) {
                    error = "Die bestehende Manndeckung wurde inzwischen geändert.";
                    return false;
                }
            }
        }
        if(editor_action && (!staged.memory.has(display_at)||request.expected_set_piece_values!=current_set_piece||
           request.expected_display_word!=staged.memory.u16(display_at)||request.expected_mode!=mode||request.indoor!=staged.indoor||
           request.expected_set_piece_signature!=uint16_t(staged.manager_records.at(manager_rec+0xb0a)|uint16_t(staged.manager_records.at(manager_rec+0xb0b))<<8)||
           uint16_t(staged.manager_records.at(manager_rec+0xb0a)|uint16_t(staged.manager_records.at(manager_rec+0xb0b))<<8)!=0x3253)){
            error="Der Schützenauftrag ist veraltet oder unvollständig.";return false;
        }
        const bool editor_open=staged.memory.has(display_at)&&(staged.memory.u16(display_at)&0x80)!=0;
        // The low-80 native bit survives Padded when the whole Pitch view
        // returns. Screen-local editor runtime is reset on each new view, so a
        // Source Schütze entry is valid even when the retained word already has
        // bit80; mutations and editor-page exits still require that active bit.
        if((request.kind==ui::PitchRequestKind::close_set_piece_editor||
            request.kind==ui::PitchRequestKind::set_piece_taker||
            request.kind==ui::PitchRequestKind::auto_set_piece_takers)&&!editor_open){
            error="Der Schützenauftrag passt nicht zum aktuellen Anzeigestatus.";return false;
        }
        if(request.kind==ui::PitchRequestKind::set_player_order){
            if(request.expected_mode!=mode||request.expected_display_word!=staged.memory.u16(display_at)||
               request.expected_set_piece_values!=current_set_piece||
               request.expected_set_piece_signature!=uint16_t(staged.manager_records.at(manager_rec+0xb0a)|uint16_t(staged.manager_records.at(manager_rec+0xb0b))<<8)||
               request.row<0||request.row>=11||request.order_aspect<0||request.order_aspect>3||
               mode==1||!staged.memory.has(display_at)||(staged.memory.u16(display_at)&0x0070)!=0x0020||
               request.new_order_value<2||request.new_order_value>40||
               request.new_order_value==request.expected_order_value){
                error="Der Anweisungsauftrag ist ungültig.";return false;
            }
            const size_t row_at=size_t(request.manager_slot)*0x840+size_t(request.row)*0x58;
            if(row_at+6>staged.tactics.size()||
               (staged.squad_slots.at(squad_range(request.runtime_club).first+size_t(request.row))!=request.slot)||
               (request.slot&0x0fff)!=request.player_id||
               staged.tactics[row_at+2+size_t(request.order_aspect)]!=request.expected_order_value){
                error="Die Anweisungszeile wurde inzwischen geändert.";return false;
            }
        }
        switch (request.kind) {
        case ui::PitchRequestKind::set_automatic_weight:
            accepted = set_pitch_automatic_weight_source(staged, request.manager_slot, request.runtime_club,
                request.weight_index, request.old_weights, request.new_weights);
            break;
        case ui::PitchRequestKind::move_player:
            accepted = move_pitch_player_source(staged, request.manager_slot, request.runtime_club,
                request.row, request.player_id, request.slot, request.old_depth, request.old_lane,
                request.new_depth, request.new_lane);
            break;
        case ui::PitchRequestKind::apply_formation:
            accepted = apply_pitch_formation_source(staged, request.manager_slot, request.runtime_club,
                request.formation, request.old_weights);
            break;
        case ui::PitchRequestKind::save_formation:
            accepted = save_pitch_formation_source(staged, request.manager_slot, request.runtime_club,
                request.formation, request.old_weights, request.formation_positions);
            break;
        case ui::PitchRequestKind::open_set_piece_editor: {
            const uint16_t expected=uint16_t((request.expected_display_word&0xff00)|
                ((request.expected_display_word&0x008f)|0x0080));
            accepted=request.new_display_word==expected&&
                normalize_pitch_set_piece_source(staged,request.manager_slot,request.runtime_club);
            if(accepted)staged.memory.set16(display_at,expected);
            break;
        }
        case ui::PitchRequestKind::close_set_piece_editor: {
            const uint16_t expected=uint16_t(request.expected_display_word&~uint16_t(0x80));
            accepted=(request.expected_display_word&0x80)!=0&&request.new_display_word==expected;
            if(accepted)staged.memory.set16(display_at,expected);
            break;
        }
        case ui::PitchRequestKind::set_piece_taker:
            accepted=request.set_piece_selector>=2&&request.set_piece_selector<=4&&
                request.expected_set_piece_value==current_set_piece[size_t(request.set_piece_selector-2)]&&
                set_pitch_set_piece_taker_source(staged,request.manager_slot,request.runtime_club,
                    request.set_piece_selector,request.row,request.player_id,request.slot,request.expected_set_piece_value);
            break;
        case ui::PitchRequestKind::auto_set_piece_takers:
            accepted=auto_pitch_set_piece_takers_source(staged,request.manager_slot,request.runtime_club,
                request.expected_set_piece_values);
            break;
        case ui::PitchRequestKind::set_player_order:
            accepted=set_player_order_source(staged,request.manager_slot,request.runtime_club,request.row,
                request.player_id,request.order_aspect,request.new_order_value);
            break;
        case ui::PitchRequestKind::set_player_marking: {
            const size_t board = size_t(request.manager_slot) * 0x840;
            for (size_t row = 0; row < 11; ++row) {
                const size_t at = board + row * 0x58 + 0x44;
                const uint16_t marker = uint16_t(staged.tactics.at(at) | uint16_t(staged.tactics.at(at + 1)) << 8);
                if (marker == uint16_t(request.opponent_player)) {
                    staged.tactics[at] = 0xff;
                    staged.tactics[at + 1] = 0x0f;
                }
            }
            if (request.row >= 0) {
                const size_t at = board + size_t(request.row) * 0x58 + 0x44;
                staged.tactics[at] = uint8_t(request.opponent_player);
                staged.tactics[at + 1] = uint8_t(uint16_t(request.opponent_player) >> 8);
            }
            accepted = true;
            break;
        }
        case ui::PitchRequestKind::select_pitch_page: {
            const uint16_t old=request.expected_display_word;
            uint16_t next=old;
            switch(request.page_control){
            case 0: next=uint16_t((old&~uint16_t(0x80))^0x0003); break; // Data toggles its source word.
            case 1: next=uint16_t(old^0x000c); break; // Team is the original inner-loop toggle.
            case 2: next=uint16_t(((old&~uint16_t(0x0080))&0x008f)|0x0010); break;
            case 3: next=uint16_t(((old&~uint16_t(0x0080))&0x008f)|0x0020); break;
            case 4: next=uint16_t(((old&~uint16_t(0x0080))&0x008f)|0x0040); break;
            default: break;
            }
            accepted=staged.memory.has(display_at)&&request.new_display_word==next&&
                !((request.page_control==3)&&mode==1);
            if(accepted)staged.memory.set16(display_at,next);
            break;
        }
        default: break;
        }
        if (!accepted) {
            error = "Die Spielfeldaktion ist veraltet oder in dieser Aufstellung nicht verfügbar.";
            return false;
        }
        g_ = std::move(staged);
    } catch (const std::exception&) {
        // Keep pitch requests transactional if a malformed native projection throws.
        error = "Diese Aufstellung kann im aktuellen Spielmodus noch nicht übernommen werden.";
        return false;
    }
    ++revision;
    if (live_context) live_intervention_->current_revision = revision;
    error.clear();
    return true;
}

bool ModernSession::dispatch_live_pitch(const ui::PitchRequest& request, int minute, int source_mode,
        MatchEngine& engine, uint64_t& revision, std::string& error) {
    if (!live_intervention_ || live_intervention_->minute != minute ||
        live_intervention_->source_mode != source_mode || source_mode != 2 ||
        live_intervention_->manager != request.manager_slot ||
        live_intervention_->team != request.runtime_club ||
        live_intervention_->current_revision != revision || request.revision != revision ||
        &engine.state != &g_ || &engine.game != &game_ || &engine.rand != &rand_) {
        error = "Die Spielfeldaktion gehört zu keinem aktiven Live-Menü.";
        return false;
    }
    if (request.kind == ui::PitchRequestKind::swap_players) {
        ui::LineupRequest swap;
        swap.kind = ui::LineupRequestKind::swap_players;
        swap.manager_slot = request.manager_slot; swap.runtime_club = request.runtime_club;
        swap.revision = request.revision;
        swap.first_row = request.row; swap.first_player = request.player_id; swap.first_slot = request.slot;
        swap.second_row = request.second_row; swap.second_player = request.second_player;
        swap.second_slot = request.second_slot;
        return dispatch_live_substitution(swap, minute, source_mode, engine, revision, error);
    }
    // PitchScreen's live param_2 is nonzero: these formation and weight controls
    // exist only in its param_2==0 initial-preparation path.
    if (request.kind == ui::PitchRequestKind::set_automatic_weight ||
        request.kind == ui::PitchRequestKind::apply_formation ||
        request.kind == ui::PitchRequestKind::save_formation) {
        error = "Diese Aufstellungsoption ist außerhalb der Vorbereitungsphase nicht verfügbar.";
        return false;
    }
    return dispatch_pitch(request, revision, error);
}

LiveSubstitutionResult ModernSession::dispatch_live_pitch_result(const ui::PitchRequest& request,
        int minute, int source_mode, MatchEngine& engine, uint64_t& revision) {
    if (request.kind == ui::PitchRequestKind::swap_players) {
        ui::LineupRequest swap;
        swap.kind = ui::LineupRequestKind::swap_players;
        swap.manager_slot = request.manager_slot; swap.runtime_club = request.runtime_club;
        swap.revision = request.revision;
        swap.first_row = request.row; swap.first_player = request.player_id; swap.first_slot = request.slot;
        swap.second_row = request.second_row; swap.second_player = request.second_player;
        swap.second_slot = request.second_slot;
        return dispatch_live_substitution_result(swap, minute, source_mode, engine, revision);
    }
    std::string error;
    const bool applied = dispatch_live_pitch(request, minute, source_mode, engine, revision, error);
    return {applied ? LiveSubstitutionStatus::applied : LiveSubstitutionStatus::rejected,
        0, request.manager_slot, std::move(error)};
}

bool ModernSession::dispatch_pitch_swap(const ui::PitchRequest& request, uint64_t& revision,
                                        std::string& error) {
    if (request.kind != ui::PitchRequestKind::swap_players) {
        error = "Diese Spielfeldaktion ist kein Spielerwechsel.";
        return false;
    }
    return dispatch_pitch(request, revision, error);
}

ui::PreparationState ModernSession::preparation_projection(uint64_t revision) const {
    return preparation_projection(g_, revision);
}

std::optional<int> ModernSession::prepared_lineup_opponent(int manager_slot, int runtime_club,
                                                            uint64_t revision) const {
    if (turn_driver_.stage != TurnDriver::Stage::MatchdayDesks || g_.indoor ||
        manager_slot < 0 || manager_slot >= int(g_.manager_teams.size()) ||
        manager_slot != lineup_entry_manager_ || runtime_club != lineup_entry_club_ ||
        !g_.memory.has(::bmh::layout::ActiveManager) ||
        int(g_.memory.u8(::bmh::layout::ActiveManager)) != manager_slot ||
        g_.manager_teams[size_t(manager_slot)] != runtime_club ||
        runtime_club == no_team() || runtime_club < 0 ||
        runtime_club >= int(exe_.u16(::bmh::layout::CareerSession_DispatchLineup_RuntimeField)) || g_.manager_of(runtime_club) != manager_slot ||
        size_t(manager_slot) >= source_desk_callargs_.size())
        return std::nullopt;

    const ui::DeskMatchCallargs& source = source_desk_callargs_[size_t(manager_slot)];
    const int opponent = source.opponent;
    if (opponent < 0 || opponent == runtime_club || opponent >= int(exe_.u16(::bmh::layout::CareerSession_DispatchLineup_RuntimeField)) ||
        source.competition == 32 || source.competition == 33 || source.competition == 34)
        return std::nullopt;
    uint32_t competitions = modern::day_competitions(g_, turn_driver_.calendar_entry);
    if (competitions == 0x800000) competitions = 0;
    const int default_competition = competitions != 0 ? 34 : 33;
    ModernState manager_view = g_;
    manager_view.memory.set8(::bmh::layout::ActiveManager, uint8_t(manager_slot));
    const ui::DeskState projected = ui::desk_from_game(manager_view, exe_, revision,
        &turn_driver_.groups, default_competition);
    if (projected.manager_slot != manager_slot || projected.runtime_club != runtime_club ||
        !projected.next_opponent || *projected.next_opponent != opponent ||
        !projected.next_competition || *projected.next_competition != source.competition)
        return std::nullopt;
    const SquadRange rival_roster = squad_range(opponent);
    if (rival_roster.count < 11 || rival_roster.first + rival_roster.count > g_.squad_slots.size())
        return std::nullopt;

    // The captured DayCompetitions2 callargs already encode Source's group order and
    // overwrite rules. Confirm that exact pair still exists in this Matchday;
    // never substitute another fixture if it has become void or unsupported.
    bool current_pair = false;
    for (const MatchGroup& group : turn_driver_.groups) {
        if (group.fixtures.size() < size_t(group.matches()) * 2) continue;
        const int type = group.desc.empty() ? -1 : group.desc.at(0);
        int mapped_competition = -1, league = -1;
        if (type == 0) {
            if (group.desc.size() < 0xd0) continue;
            league = int(le32(group.desc, 0xcc));
            if (league >= 0 && league < 9)
                mapped_competition = int(exe_.u8(::bmh::layout::LeagueCalendarBits + uint32_t(league)));
        } else if (type > 0) {
            mapped_competition = type + 4;
        }
        for (int match = 0; match < group.matches(); ++match) {
            const int home = group.fixtures.at(size_t(match) * 2);
            const int away = group.fixtures.at(size_t(match) * 2 + 1);
            if (!((home == runtime_club && away == opponent) ||
                  (away == runtime_club && home == opponent))) continue;
            bool valid = mapped_competition == source.competition;
            const uint32_t bit = uint32_t(1) << match;
            valid = valid && !(group.kickoff_void_mask & bit) && !(group.void_mask() & bit);
            if (valid && type == 0) {
                bool has_result = false;
                uint8_t raw_result = 0xcc;
                if (size_t(match) < group.results.size()) {
                    has_result = true;
                    raw_result = group.results[size_t(match)];
                } else if (league >= 0 && league < int(g_.league_rounds.size())) {
                    const int round = int(g_.league_rounds[size_t(league)]);
                    const size_t result_at = size_t(league) * 0x17c + size_t(round) * 10 + size_t(match);
                    if (round >= 0 && result_at < g_.results.size()) {
                        has_result = true;
                        raw_result = g_.results[result_at];
                    }
                }
                if (has_result && raw_result != 0xcc) valid = false;
            }
            // MatchdayNeedsManagerDesks overwrites the per-manager callargs in group order.
            // The last matching pair therefore decides whether this exact
            // callarg is still a supported, unplayed fixture.
            current_pair = valid;
        }
    }
    return current_pair ? std::optional<int>(opponent) : std::nullopt;
}

ui::PreparationState ModernSession::preparation_projection(const ModernState& state, uint64_t revision) const {
    auto result = ui::preparation_from_game(state, exe_, revision);
    result.desktop = desktop_projection(state, revision);
    if (lineup_entry_manager_ == result.lineup.manager_slot &&
        lineup_entry_club_ == result.lineup.runtime_club) {
        result.lineup.entry_prepared = true;
        if (result.pitch) {
            const auto opponent = prepared_lineup_opponent(result.lineup.manager_slot,
                result.lineup.runtime_club, revision);
            if (opponent)
                result.pitch = ui::pitch_extras_from_game(state, exe_, revision, *opponent);
        }
    }
    if (result.bank) {
        result.bank->last_tip = last_stock_tip_;
        result.bank->property_notice = last_property_notice_;
        result.bank->credit_notice = last_credit_notice_;
    }
    if (result.stadium) {
        const int team = result.stadium->runtime_club;
        const int stable = int(state.club_ids.at(size_t(team)));
        const bool owned = stadium_.manager == result.stadium->manager_slot &&
            stadium_.club == team && stadium_.stable_club == stable && stadium_.revision == revision;
        *result.stadium = ui::stadium_from_game(state, exe_, revision, owned ? stadium_.price_percent : 100);
        auto& stadium = *result.stadium;
        stadium.source = result.desktop.source;
        stadium.club_name = result.desktop.club_name;
        stadium.stable_club_identity = stable;
        stadium.plain_background = source_runtime_byte(state, club_modal_runtime_, exe_, ::bmh::layout::PlainBackground) != 0;
        stadium.decorated_stadium_header = !stadium.plain_background;
        stadium.compact_dialog_frame = source_runtime_byte(state, club_modal_runtime_, exe_, ::bmh::layout::CompactDialogFrame) != 0;
        stadium.solid_panel_background = source_runtime_byte(state, club_modal_runtime_, exe_, ::bmh::layout::SolidPanelBackground) != 0;
        stadium.gradient_text = source_runtime_byte(state, club_modal_runtime_, exe_, ::bmh::layout::TextGradientDisabled) == 0 &&
            source_runtime_byte(state, club_modal_runtime_, exe_, ::bmh::layout::TextGradientMode) == 2;
        stadium.source_level = state.level;
        stadium.season_tickets = uint16_t(state.manager_records.at(size_t(stadium.manager_slot) * 0xb0c + 0x2ac)) |
            uint16_t(state.manager_records.at(size_t(stadium.manager_slot) * 0xb0c + 0x2ad)) << 8;
        const int source_team = int16_t(uint16_t(state.manager_records.at(size_t(stadium.manager_slot) * 0xb0c + 0x290)) |
            uint16_t(state.manager_records.at(size_t(stadium.manager_slot) * 0xb0c + 0x291)) << 8);
        const int source_league = source_team > 0x164 ? 0 : team_league(source_team);
        if (source_league < 0 || size_t(source_league) >= state.league_rounds.size())
            throw FormatError("stadium source league is outside the live league table");
        stadium.live_league_round = state.league_rounds.at(size_t(source_league));
        stadium.ticket_refund_rate = std::bit_cast<int32_t>(exe_.u32(::bmh::layout::CareerSession_FormatError_RuntimeField5 + uint32_t(source_league) * 4));
        stadium.investments_enabled = state.memory.u8(::bmh::layout::GameMode) != 1;
        for (size_t i = 0; i < stadium.investments.size(); ++i) {
            stadium.investments[i].original_label = texts_[734 + i];
            if (!stadium.investments_enabled) stadium.investments[i].available = false;
        }
        if (owned) {
            stadium.opened = stadium_.opened;
            stadium.scope = stadium_.scope; stadium.sequence = stadium_.sequence;
            if (stadium_.data_open)
                stadium.data = ui::stadium_data_from_game(state, exe_, stadium.manager_slot, stadium_.data_flags);
            stadium.selected_investment = stadium_.investment;
            if (stadium_.option_index >= 0) stadium.selected_option = stadium_.option_index;
            stadium.investment_text_variant = stadium_.text_variant;
            stadium.action_feedback = stadium_.feedback;
            if (stadium_.tip) {
                PendingOfferQuestion question;
                question.manager = stadium_.manager; question.source_flags = 0x204;
                question.original_message = message_text(state, ::bmh::layout::CareerSession_FormatError_TextPointer8) +
                    format_number(state, stadium_.tip->percent, 1) + message_text(state, ::bmh::layout::CareerSession_FormatError_TextPointer9);
                stadium.construction_tip_dialog = ui::pending_offer_question_dialog(state, question);
                stadium.construction_tip_price = stadium_.tip->offered_price;
            }
            if (stadium_.periphery_quote) {
                stadium.quoted_object = stadium_.periphery_quote->first;
                stadium.quoted_price = stadium_.periphery_quote->second;
                PendingOfferQuestion question;
                question.manager = stadium_.manager;
                question.source_flags = 1; // ConstructionPricePercent -> PresentManagerDialog: ordinary JA/NEIN question.
                question.original_message = message_text(state, ::bmh::layout::CareerSession_FormatError_TextPointer6) +
                    format_number(state, stadium.quoted_price, 0x106) + message_text(state, ::bmh::layout::CareerSession_FormatError_TextPointer7);
                stadium.periphery_quote_dialog = ui::pending_offer_question_dialog(state, question);
                stadium.periphery_quote_dialog->compact_dialog_frame = stadium.compact_dialog_frame;
            }
            if (stadium_.block_expansion) {
                const auto& draft = *stadium_.block_expansion;
                ui::StadiumBlockExpansionEditor editor;
                editor.quote = draft.quote;
                editor.total_target_places = draft.total_target_places;
                editor.desired_features = draft.desired_features;
                editor.header_text_variant = draft.header_text_variant;
                editor.offer = draft.tip;
                const uint32_t current = uint32_t(draft.quote.current_capacity_thousands) * 1000;
                const uint32_t added = draft.total_target_places - current;
                editor.current_places_text = format_number(state, int32_t(current), 5);
                editor.new_places_text = format_number(state, int32_t(added), 5);
                editor.target_places_text = format_number(state, int32_t(draft.total_target_places), 5);
                const auto cost_text = [&](uint32_t amount) {
                    return format_number(state, int32_t(amount), 0x181) + exe_.cstring(::bmh::layout::CareerSession_FormatNumber_ExecutableField);
                };
                editor.per_thousand_cost_text = format_number(state, int32_t(draft.quote.price_per_1000), 0x181);
                editor.new_places_cost_text = cost_text(draft.quote.new_places_cost);
                editor.seats_cost_text = cost_text(draft.quote.seats_cost);
                editor.roof_cost_text = cost_text(draft.quote.roof_cost);
                editor.total_cost_text = cost_text(draft.quote.base_price);
                editor.discount_text = exe_.cstring(::bmh::layout::CareerSession_FormatNumber_TextPointer) +
                    format_number(state, 100 - stadium_.price_percent, 1) + exe_.cstring(::bmh::layout::CareerSession_FormatNumber_ExecutableField2);
                stadium.block_expansion = std::move(editor);
            }
            if (stadium_.block_expansion && stadium_.block_expansion->tip) {
                PendingOfferQuestion question;
                question.manager = stadium_.manager;
                question.source_flags = 0x204;
                question.original_message = message_text(state, ::bmh::layout::CareerSession_FormatError_TextPointer8) +
                    format_number(state, stadium_.block_expansion->tip->percent, 1) +
                    message_text(state, ::bmh::layout::CareerSession_FormatError_TextPointer9);
                stadium.construction_tip_dialog = ui::pending_offer_question_dialog(state, question);
                stadium.construction_tip_dialog->compact_dialog_frame = stadium.compact_dialog_frame;
                stadium.construction_tip_price = stadium_.block_expansion->tip->offered_price;
            }
        }
    }
    return result;
}

bool ModernSession::dispatch_training(const ui::TrainingRequest& request, uint64_t& revision,
                                      std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    const int active = int(g_.memory.u8(::bmh::layout::ActiveManager));
    if (request.manager_slot != active || request.manager_slot < 0 || request.manager_slot > 3 ||
        size_t(request.manager_slot) >= g_.manager_teams.size() ||
        request.runtime_club != g_.manager_teams[size_t(request.manager_slot)] || request.revision != revision) {
        error = "stale_manager_or_revision";
        return false;
    }

    try {
        const ui::TrainingState current = ui::training_from_game(g_, exe_, revision);
        if (current.manager_slot != request.manager_slot || current.runtime_club != request.runtime_club) {
            error = "stale_manager_or_revision";
            return false;
        }
        const size_t slot = size_t(request.manager_slot);
        const size_t rec = slot * 0xb0c;
        const size_t option_at = slot * 5 + 4;
        if (g_.manager_records.size() < rec + 0xb0c || g_.manager_options.size() <= option_at) {
            error = "invalid_training_state";
            return false;
        }

        auto valid_plan = [](const std::array<uint8_t, 4>& types, const std::array<uint8_t, 9>& areas) {
            int type_sum = 0, fitness = 0, skills = 0;
            for (uint8_t value : types) { if (value < 1 || value > 3) return false; type_sum += value; }
            for (size_t i = 0; i < areas.size(); ++i) {
                const uint8_t value = areas[i];
                if (value < 1 || value > 3) return false;
                if (i < 2) fitness += value; else skills += value;
            }
            return type_sum <= 8 && fitness <= 4 && skills <= 18;
        };
        if (!valid_plan(request.desired_types, request.desired_areas)) {
            error = "invalid_training_plan";
            return false;
        }

        ui::TrainingRequestKind kind = request.kind;
        auto types = current.types;
        auto areas = current.areas;
        uint8_t automation = current.automation;
        bool run_automatic = false;
        // NeedsInitialization initializes an uninitialized screen via Initialize;
        // the four subordinate toggles exist only while the global flag is on.
        // NeedsInitialization2 calls that same deterministic routine after an enabled
        // automation toggle. Manual field edits only write their field and clear
        // the matching assistant flag; they do not run training or draw RNG.
        switch (kind) {
        case ui::TrainingRequestKind::initialize:
            if (!current.needs_initialization() || request.index != -1 || request.value != -1 ||
                request.player_id != -1 || request.desired_types != types || request.desired_areas != areas ||
                request.desired_automation != 0x9f) {
                error = "invalid_training_initialization";
                return false;
            }
            automation = 0x9f;
            run_automatic = true;
            break;
        case ui::TrainingRequestKind::set_type:
            if (current.needs_initialization() || request.index < 0 || request.index >= int(types.size()) ||
                request.value < 1 || request.value > 3 || request.player_id != -1 ||
                request.value == types[size_t(request.index)]) {
                error = "invalid_training_type";
                return false;
            }
            types[size_t(request.index)] = uint8_t(request.value);
            automation = uint8_t(automation & ~uint8_t(1u << 1));
            break;
        case ui::TrainingRequestKind::set_area:
            if (current.needs_initialization() || request.index < 0 || request.index >= int(areas.size()) ||
                request.value < 1 || request.value > 3 || request.player_id != -1 ||
                request.value == areas[size_t(request.index)]) {
                error = "invalid_training_area";
                return false;
            }
            areas[size_t(request.index)] = uint8_t(request.value);
            automation = uint8_t(automation & ~uint8_t(1u << 2));
            break;
        case ui::TrainingRequestKind::set_player_intensity:
        case ui::TrainingRequestKind::set_player_focus: {
            if (current.needs_initialization() || request.index < 0 || request.index >= current.squad_capacity ||
                request.player_id < 0 || request.player_id > 0x0ffe) {
                error = "invalid_training_player";
                return false;
            }
            const auto player = std::find_if(current.players.begin(), current.players.end(), [&](const auto& p) {
                return p.row == request.index && p.player_id == request.player_id;
            });
            const SquadRange squad = squad_range(request.runtime_club);
            if (player == current.players.end() || squad.count != size_t(current.squad_capacity) ||
                squad.first + size_t(request.index) >= g_.squad_slots.size() ||
                (g_.squad_slots[squad.first + size_t(request.index)] & 0x0fff) != request.player_id) {
                error = "stale_training_player";
                return false;
            }
            if (kind == ui::TrainingRequestKind::set_player_intensity) {
                if (request.value < 1 || request.value > 3 || request.value == player->intensity) {
                    error = "invalid_training_intensity";
                    return false;
                }
                automation = uint8_t(automation & ~uint8_t(1u << 3));
            } else if (request.value < 0 || request.value > 7 || request.value != (player->focus + 1) % 8) {
                error = "invalid_training_focus";
                return false;
            }
            break;
        }
        case ui::TrainingRequestKind::set_automation: {
            if (current.needs_initialization() || request.index < 0 || request.index > 4 ||
                (request.value != 0 && request.value != 1) || request.player_id != -1) {
                error = "invalid_training_automation";
                return false;
            }
            if (request.index != 0 && !(current.automation & 1)) {
                error = "training_automation_group_hidden";
                return false;
            }
            const uint8_t mask = uint8_t(1u << request.index);
            automation = uint8_t(automation ^ mask);
            if (((automation & mask) != 0) != (request.value != 0)) {
                error = "stale_training_automation";
                return false;
            }
            run_automatic = (automation & 1) != 0;
            break;
        }
        case ui::TrainingRequestKind::none:
        default:
            error = "unsupported_training_request";
            return false;
        }

        if (request.desired_types != types || request.desired_areas != areas ||
            request.desired_automation != automation) {
            error = "stale_training_projection";
            return false;
        }

        ModernState staged = g_;
        std::copy(types.begin(), types.end(), staged.manager_records.begin() + rec + 0xc5);
        std::copy(areas.begin(), areas.end(), staged.manager_records.begin() + rec + 0xc9);
        staged.manager_options[option_at] = automation;
        if (kind == ui::TrainingRequestKind::set_player_intensity ||
            kind == ui::TrainingRequestKind::set_player_focus) {
            const size_t board = slot * 0x840 + size_t(request.index) * 0x58 + 0x0a;
            if (board >= staged.tactics.size()) {
                error = "invalid_training_board";
                return false;
            }
            if (kind == ui::TrainingRequestKind::set_player_intensity)
                staged.tactics[board] = uint8_t((staged.tactics[board] & 0xfc) | request.value);
            else
                staged.tactics[board] = uint8_t((staged.tactics[board] & 0x03) | (request.value << 2));
        }
        if (run_automatic) modern::auto_training(staged, request.manager_slot);
        g_ = std::move(staged);
        ++revision;
        error.clear();
        return true;
    } catch (const std::exception&) {
        error = "invalid_training_state";
        return false;
    }
}

void ModernSession::capture_source_desk_callargs() {
    source_desk_callargs_.clear();
    const auto stage = turn_driver_.stage;
    if (stage != TurnDriver::Stage::MatchdayDesks &&
        stage != TurnDriver::Stage::SeasonEndDesk)
        return;
    uint32_t competitions = 0;
    int default_competition = 32;
    const std::vector<MatchGroup>* groups = nullptr;
    if (stage == TurnDriver::Stage::MatchdayDesks) {
        competitions = modern::day_competitions(g_, turn_driver_.calendar_entry);
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
        ModernState manager_view = g_;
        if (!manager_view.memory.has(::bmh::layout::ActiveManager))
            throw FormatError("desk: active manager field is missing while capturing Source callargs");
        manager_view.memory.set8(::bmh::layout::ActiveManager, uint8_t(slot));
        const ui::DeskState source = ui::desk_from_game(
            manager_view, exe_, turn_driver_.revision, groups, default_competition);
        source_desk_callargs_[slot] = {source.next_competition.value_or(default_competition),
                                       source.next_opponent.value_or(-1)};
    }
}

ui::DeskState ModernSession::desktop_projection(uint64_t revision) const {
    return desktop_projection(g_, revision);
}

ui::DeskState ModernSession::desktop_projection(const ModernState& state, uint64_t revision) const {
    std::optional<int> source_default;
    if (turn_driver_.stage == TurnDriver::Stage::SeasonEndDesk) {
        source_default = 32;
    } else if (turn_driver_.stage == TurnDriver::Stage::MatchdayDesks) {
        uint32_t competitions = modern::day_competitions(state, turn_driver_.calendar_entry);
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

void ModernSession::configure_autosave_directory(const std::filesystem::path& directory) {
    if (autosave_directory_ && *autosave_directory_ == directory) return;
    if (calendar_advance_active_) throw FormatError("autosave directory change during active turn");
    autosave_directory_ = directory;
    turn_driver_.desk_autosave = [this](const ModernState& state, WatcomRandom& random) {
        write_desk_autosave(state, random);
    };
}

std::string ModernSession::take_autosave_error() {
    return std::exchange(autosave_error_, {});
}

void ModernSession::write_desk_autosave(const ModernState& state, WatcomRandom& random) {
    // OpenBMH deviates from the original: native saves do not consume gameplay randomness for encryption.
    if (content::find(data_directory_)) {
        native_autosave_pending_ = bool(autosave_directory_);
        return;
    }
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
        const auto saved = modern::make_savegame(state, staged);
        temporary = target;
        temporary += ".tmp-" + std::to_string(saved.id2);
        if (std::filesystem::exists(std::filesystem::symlink_status(temporary))) {
            temporary.clear();
            throw FormatError("autosave temporary target already exists");
        }
        modern::write_savegame(temporary, saved);
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

cli::Json ModernSession::native_session_context() const {
    cli::Json::Array runtime, callargs, managers;
    for (const auto &[address, value] : club_modal_runtime_)
        runtime.emplace_back(cli::Json::Object{{"address", int64_t(address)}, {"value", int(value)}});
    for (const auto &args : source_desk_callargs_)
        callargs.emplace_back(cli::Json::Array{args.competition, args.opponent});
    for (const auto &manager : managers_)
        managers.emplace_back(cli::Json::Array{manager.slot, manager.master_club, manager.runtime_club});
    return cli::Json::Object{{"schema", "classic-session@1.0"},
                             {"runtime", std::move(runtime)},
                             {"desk_callargs", std::move(callargs)},
                             {"managers", std::move(managers)}};
}
void ModernSession::restore_native_session_context(const cli::Json &root) {
    const auto field = [&](const char *name) -> const cli::Json & {
        if (!root.object() || !root.object()->contains(name))
            throw FormatError("Native session field missing");
        return root.object()->at(name);
    };
    if (!field("schema").string() || *field("schema").string() != "classic-session@1.0")
        throw FormatError("Unsupported native session");
    const auto array = [](const cli::Json &j, size_t maximum) -> const cli::Json::Array & {
        if (!j.array() || j.array()->size() > maximum)
            throw FormatError("Invalid native session array");
        return *j.array();
    };
    const auto integer = [](const cli::Json &j, int64_t low, int64_t high) -> int64_t {
        if (!j.integer() || *j.integer() < low || *j.integer() > high)
            throw FormatError("Invalid native session integer");
        return *j.integer();
    };
    std::map<uint32_t, uint8_t> runtime;
    for (const auto &row : array(field("runtime"), 8192)) {
        if (!row.object() || !row.object()->contains("address") || !row.object()->contains("value"))
            throw FormatError("Invalid runtime byte");
        const auto at = uint32_t(integer(row.object()->at("address"), 0, UINT32_MAX));
        if (!club_modal_runtime_.contains(at) ||
            !runtime.emplace(at, uint8_t(integer(row.object()->at("value"), 0, 255))).second)
            throw FormatError("Unknown or duplicated runtime byte");
    }
    if (runtime.size() != club_modal_runtime_.size())
        throw FormatError("Incomplete native runtime bytes");
    std::vector<ui::DeskMatchCallargs> callargs;
    for (const auto &row : array(field("desk_callargs"), 4)) {
        const auto &fields = array(row, 2);
        if (fields.size() != 2)
            throw FormatError("Invalid desk arguments");
        callargs.push_back({int(integer(fields[0], -1, 255)), int(integer(fields[1], -1, 4095))});
    }
    std::vector<ui::InitializedManager> managers;
    for (const auto &row : array(field("managers"), 4)) {
        const auto &fields = array(row, 3);
        if (fields.size() != 3)
            throw FormatError("Invalid manager mapping");
        managers.push_back({int(integer(fields[0], 0, 3)), int(integer(fields[1], 0, 4095)),
                            int(integer(fields[2], 0, 4095))});
    }
    club_modal_runtime_ = std::move(runtime);
    source_desk_callargs_ = std::move(callargs);
    managers_ = std::move(managers);
}
void ModernSession::flush_native_autosave() {
    if (!std::exchange(native_autosave_pending_, false))
        return;
    try {
        if (!autosave_directory_ || !std::filesystem::is_directory(*autosave_directory_))
            throw FormatError("Autosave directory unavailable");
        const auto resource = content::find(data_directory_);
        if (!resource)
            throw FormatError("Native content unavailable");
        if (const World* world = current_world())
            save_world_career(*autosave_directory_ / "AUTOSAVE.obs", *world, g_, rand_, game_, turn_driver_, true,
                              native_session_context());
        else
            content::save_career(*autosave_directory_ / "AUTOSAVE.obs", *resource->content, g_, rand_, game_,
                                 turn_driver_, true, native_session_context());
    } catch (const std::exception &e) {
        autosave_error_ = e.what();
    }
}

bool ModernSession::save_game(const std::filesystem::path& path, int manager_slot, bool overwrite, std::string& error) {
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
        extension != (content::find(data_directory_) ? ".obs" : ".fbm") || path.filename().empty()) {
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
        if (const auto resource = content::find(data_directory_)) {
            if (const World* world = current_world())
                save_world_career(path, *world, g_, rand_, game_, turn_driver_, overwrite, native_session_context());
            else
                content::save_career(path, *resource->content, g_, rand_, game_, turn_driver_, overwrite,
                                     native_session_context());
            return true;
        }
        WatcomRandom staged_rand = rand_;
        const Savegame save = modern::make_savegame(g_, staged_rand);
        modern::write_savegame(path, save);
        rand_ = staged_rand;
        return true;
    } catch (const std::exception&) {
        error = "save_write_failed"; return false;
    }
}

bool ModernSession::dispatch_menu_layout(const ui::SaveLoadRequest& request, uint64_t& revision,
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
    if (extension != (content::find(data_directory_) ? ".oblayout" : ".men")) {
        error = "invalid_menu_layout_extension";
        return false;
    }
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
        try {
            payload = content::find(data_directory_)
                          ? content::load_preferences(request.path, "layout", kLayoutBytes)
                          : read_file(request.path);
        } catch (const std::exception &) {
            error = "menu_layout_read_failed";
            return false;
        }
        if (!validate_payload(payload)) return false;
        ModernState staged = g_;
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
        if (content::find(data_directory_)) {
            content::save_preferences(request.path, "layout", payload, request.overwrite);
            ++revision;
            error.clear();
            return true;
        }
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

bool ModernSession::dispatch_training_camp(const ui::TrainingCampRequest& request, uint64_t& revision,
                                          std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    if (request.manager_slot < 0 || size_t(request.manager_slot) >= g_.manager_teams.size() ||
        request.manager_slot != int(g_.memory.u8(::bmh::layout::ActiveManager)) ||
        request.runtime_club != g_.manager_teams[size_t(request.manager_slot)] || request.revision != revision) {
        error = "stale_manager_or_revision";
        return false;
    }
    const size_t rec = size_t(request.manager_slot) * 0xb0c;
    if (request.kind == ui::TrainingCampRequestKind::set_focus) {
        const std::array<uint8_t, 4> current{
            g_.manager_records.at(rec + 0xc1), g_.manager_records.at(rec + 0xc2),
            g_.manager_records.at(rec + 0xc3), g_.manager_records.at(rec + 0xc4)};
        if (request.focus == current) {
            error = "focus_unchanged";
            return false;
        }
        if (!modern::set_training_camp_focus(g_, request.manager_slot, request.focus)) {
            error = "invalid_focus_request";
            return false;
        }
        ++revision;
        return true;
    }
    if (request.kind == ui::TrainingCampRequestKind::book) {
        if (request.resort < 0 || size_t(request.resort) >= g_.training_camps.size()) {  // the world's catalogue
            error = "invalid_resort";
            return false;
        }
        const std::array<uint8_t, 4> current{
            g_.manager_records.at(rec + 0xc1), g_.manager_records.at(rec + 0xc2),
            g_.manager_records.at(rec + 0xc3), g_.manager_records.at(rec + 0xc4)};
        if (request.focus != current) {
            error = "stale_focus";
            return false;
        }
        switch (modern::training_camp_booking_block(g_, request.manager_slot)) {
        case TrainingCampBookingBlock::booked_this_week: error = "booked_this_week"; return false;
        case TrainingCampBookingBlock::visit_limit: error = "visit_limit"; return false;
        case TrainingCampBookingBlock::invalid_request: error = "invalid_request"; return false;
        case TrainingCampBookingBlock::none: break;
        }
        if (!modern::book_training_camp(g_, request.manager_slot, request.resort, rand_)) {
            error = "insufficient_funds";
            return false;
        }
        ++revision;
        return true;
    }
    error = "unsupported_training_camp_request";
    return false;
}

bool ModernSession::dispatch_bank(const ui::BankRequest& request, uint64_t& revision, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    error.clear();
    if (revision == std::numeric_limits<uint64_t>::max()) {
        error = "revision_exhausted"; return false;
    }
    const int active = int(g_.memory.u8(::bmh::layout::ActiveManager));
    if (request.manager_slot != active ||
        request.manager_slot < 0 || size_t(request.manager_slot) >= g_.manager_teams.size() ||
        request.runtime_club != g_.manager_teams[size_t(request.manager_slot)] || request.revision != revision) {
        error = "stale_manager_or_revision"; return false;
    }
    const size_t rec = size_t(request.manager_slot) * 0xb0c;
    std::optional<ui::BankState::PropertyNotice> property_notice;
    std::optional<ui::BankState::CreditNotice> credit_notice;
    const auto read_signed = [&](size_t offset) {
        const uint32_t v = uint32_t(g_.manager_records.at(rec + offset)) |
            uint32_t(g_.manager_records.at(rec + offset + 1)) << 8 |
            uint32_t(g_.manager_records.at(rec + offset + 2)) << 16 |
            uint32_t(g_.manager_records.at(rec + offset + 3)) << 24;
        return int32_t(v);
    };
    if (request.kind == ui::BankRequestKind::move_to_deposit) {
        if (!request.amount || request.asset_index != -1 || request.count != 0 ||
            int64_t(request.amount > 0 ? request.amount : -int64_t(request.amount)) > 99999999) {
            error = "invalid_amount"; return false;
        }
        const int64_t available = request.amount > 0 ? read_signed(0x314) : read_signed(0x318);
        if (available < 0 || int64_t(request.amount > 0 ? request.amount : -int64_t(request.amount)) > available) {
            error = "amount_unavailable"; return false;
        }
        modern::move_to_deposit(g_, request.manager_slot, request.amount);
    } else if (request.kind == ui::BankRequestKind::buy_shares || request.kind == ui::BankRequestKind::sell_shares ||
               request.kind == ui::BankRequestKind::stock_tip) {
        if (request.asset_index < 0 || request.asset_index >= kStocks ||
            g_.stocks.size() < size_t(kStocks) * 0x5c) { error = "invalid_stock"; return false; }
        const size_t at = size_t(request.asset_index) * 0x5c;
        const int price = int(uint16_t(g_.stocks.at(at + 0x4e) | uint16_t(g_.stocks.at(at + 0x4f)) << 8));
        if (request.kind == ui::BankRequestKind::buy_shares) {
            if (request.count <= 0 || request.count > 9999 || price <= 0 ||
                request.count > modern::max_shares_to_buy(g_, request.manager_slot, request.asset_index)) {
                error = "invalid_share_count"; return false;
            }
            const int64_t raw = int64_t(price) * request.count;
            const int64_t total = raw + raw * 15 / 1000;
            if (total > int64_t(read_signed(0x314)) + read_signed(0x31c)) {
                error = "insufficient_funds"; return false;
            }
            if (!modern::buy_shares(g_, request.manager_slot, request.asset_index, request.count)) {
                error = "investment_limit"; return false;
            }
        } else if (request.kind == ui::BankRequestKind::sell_shares) {
            const size_t held_at = at + 0x50 + size_t(request.manager_slot) * 2;
            const int held = int(uint16_t(g_.stocks.at(held_at) | uint16_t(g_.stocks.at(held_at + 1)) << 8));
            if (request.count <= 0 || request.count > held) { error = "invalid_share_count"; return false; }
            modern::sell_shares(g_, request.manager_slot, request.asset_index, request.count);
        } else {
            if (request.count != 0 || request.amount != 0) { error = "invalid_tip_request"; return false; }
            last_stock_tip_ = modern::stock_tip(g_, request.manager_slot, request.asset_index, rand_);
        }
    } else if (request.kind == ui::BankRequestKind::buy_fund || request.kind == ui::BankRequestKind::sell_fund) {
        if (request.amount != 0 || request.asset_index < 0 || request.asset_index >= kFunds || request.count <= 0 || request.count > 10 ||
            g_.memory.u32(::bmh::layout::FundBlock + uint32_t(request.asset_index) * 0x1c) == 0) {
            error = "invalid_fund_request"; return false;
        }
        const uint32_t fund_at = ::bmh::layout::FundBlock + uint32_t(request.asset_index) * 0x1c;
        const uint16_t held = g_.memory.u16(fund_at + 0x14 + uint32_t(request.manager_slot) * 2);
        if (request.kind == ui::BankRequestKind::buy_fund) {
            if (request.count > modern::max_fund_units(g_, request.manager_slot, request.asset_index)) {
                error = "invalid_fund_count"; return false;
            }
            const int32_t additional_cost = modern::buy_fund_units(g_, request.manager_slot, request.asset_index, request.count, game_);
            if (request.asset_index != 0) {
                const std::string text = message_text(g_, ::bmh::layout::CareerSession_Int32T_TextPointer2) + format_number(g_, additional_cost, 0x101) +
                                         message_text(g_, ::bmh::layout::CareerSession_Int32T_TextPointer);
                property_notice = ui::BankState::PropertyNotice{request.asset_index, additional_cost, ui::original_to_utf8(text)};
            }
        } else {
            if (request.count > held) { error = "invalid_fund_count"; return false; }
            modern::sell_fund_units(g_, request.manager_slot, request.asset_index, request.count);
        }
    } else if (request.kind == ui::BankRequestKind::bank_credit) {
        if (request.amount != 0 || request.asset_index != -1 || request.count != 0 || request.credit_edits.empty()) {
            error = "invalid_credit_request"; return false;
        }
        if (request.credit_counterparty != 0) {
            // Source case 5 scans configured human managers, excluding the borrower.
            // A runtime national-manager entry is not an additional human lender.
            const int configured = int(g_.memory.u8(::bmh::layout::ManagerCount));
            if (request.credit_counterparty < 1 || request.credit_counterparty > 4) {
                error = "invalid_credit_request"; return false;
            }
            const int lender = request.credit_counterparty - 1;
            if (configured < 1 || configured > 4 || request.manager_slot >= configured ||
                lender < 0 || lender >= configured || lender == request.manager_slot) {
                error = "invalid_credit_request"; return false;
            }
        }
        const auto result = modern::commit_bank_credit(g_, exe_, request.manager_slot,
            request.expected_credit, request.credit_edits, request.accept_credit_fees, request.credit_counterparty);
        if (result.status != BankCreditStatus::accepted) {
            error = result.status == BankCreditStatus::no_change ? "credit_no_change" :
                    result.status == BankCreditStatus::stale_request ? "stale_credit_request" :
                    result.status == BankCreditStatus::confirmation_required ? "credit_confirmation_required" :
                    "invalid_credit_request";
            return false;
        }
        if (result.refund != 0) {
            // "Die Bank" (CareerSession_Int32T_TextPointer3) or, in case 5, the lender's name (record +0xa8)
            const int lender = request.credit_counterparty - 1;
            std::string who = message_text(g_, ::bmh::layout::CareerSession_Int32T_TextPointer3);
            if (lender >= 0) {
                who.clear();
                for (size_t i = 0; i < 26; ++i) {
                    const uint8_t ch = g_.manager_records.at(size_t(lender) * 0xb0c + 0xa8 + i);
                    if (!ch) break;
                    who += char(ch);
                }
            }
            const std::string text = who + message_text(g_, ::bmh::layout::CareerSession_Int32T_TextPointer4) +
                format_number(g_, result.refund, 0x101) + message_text(g_, ::bmh::layout::CareerSession_Int32T_TextPointer5);
            credit_notice = ui::BankState::CreditNotice{result.refund, ui::original_to_utf8(text)};
        }
    } else {
        error = "unsupported_bank_request"; return false;
    }
    last_property_notice_ = std::move(property_notice);
    last_credit_notice_ = std::move(credit_notice);
    ++revision;
    return true;
}

bool ModernSession::stadium_tip_deadline_expired() const {
    if (!stadium_.opened || (!stadium_.tip &&
        (!stadium_.block_expansion || !stadium_.block_expansion->tip))) return false;
    const auto byte = [this](uint32_t address) {
        if (g_.memory.has(address)) return g_.memory.u8(address);
        const auto found = club_modal_runtime_.find(address);
        return found == club_modal_runtime_.end() ? exe_.u8(address) : found->second;
    };
    const auto word = [&byte](uint32_t address) {
        uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) value |= uint32_t(byte(address + i)) << (8 * i);
        return value;
    };
    return word(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField2) != 0 && int32_t(word(::bmh::layout::CareerSession_FormatError_ExecutableField2)) < 1;
}

bool ModernSession::dispatch_stadium(const ui::StadiumRequest& supplied_request, uint64_t& revision, std::string& error,
                                    const ConstructionNoticeHook& notice_presenter,
                                    const SeasonTicketCompensationNoticeHook& compensation_presenter) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    const int active = int(g_.memory.u8(::bmh::layout::ActiveManager));
    auto request = supplied_request;
    // Existing CLI periphery callers predate the UI scope envelope. Restrict
    // this compatibility adapter to their four original operations and an
    // entirely absent envelope; investments and close remain strictly owned.
    const bool legacy_periphery = !request.stable_club_identity && request.scope == 0 && request.sequence == 0 &&
        (request.kind == ui::StadiumRequestKind::open || request.kind == ui::StadiumRequestKind::quote_periphery ||
         request.kind == ui::StadiumRequestKind::decline_periphery || request.kind == ui::StadiumRequestKind::build_periphery);
    if (legacy_periphery && active >= 0 && active <= 3 && size_t(active) < g_.manager_teams.size() &&
        request.runtime_club >= 0 && size_t(request.runtime_club) < g_.club_ids.size()) {
        request.stable_club_identity = int(g_.club_ids[size_t(request.runtime_club)]);
        if (request.kind != ui::StadiumRequestKind::open) {
            request.scope = stadium_.scope; request.sequence = stadium_.sequence;
        }
    }
    const bool block_action = is_block_expansion_action(request.kind);
    if (!block_action && (request.block_slot != -1 || request.block_total_places != 0 ||
                          request.block_features != 0 || request.expected_block_quote)) {
        error = "unexpected_block_expansion_fields"; return false;
    }
    if (request.manager_slot != active || active < 0 || active > 3 ||
        size_t(active) >= g_.manager_teams.size() || request.runtime_club != g_.manager_teams[size_t(active)] ||
        request.runtime_club < 0 || request.runtime_club >= no_team() ||
        size_t(request.runtime_club) >= g_.club_ids.size() ||
        request.stable_club_identity != std::optional<int>(int(g_.club_ids[size_t(request.runtime_club)])) ||
        request.revision != revision || revision == UINT64_MAX || !g_.memory.has(::bmh::layout::GameMode)) {
        error = "stale_manager_or_revision"; return false;
    }
    const size_t record = size_t(active) * 0xb0c;
    if (record + 0xb0c > g_.manager_records.size() ||
        (int(g_.manager_records[record + 0x290]) | int(g_.manager_records[record + 0x291]) << 8) != request.runtime_club) {
        error = "invalid_stadium_owner"; return false;
    }
    const int mode = g_.memory.u8(::bmh::layout::GameMode);
    if (mode < 1 || mode > 3) { error = "invalid_stadium_mode"; return false; }
    if (request.kind == ui::StadiumRequestKind::open) {
        const bool legacy_reentry = legacy_periphery && !stadium_.data_open && !stadium_.investment && !stadium_.tip &&
            !stadium_.block_expansion;
        if ((stadium_.opened && !legacy_reentry) || request.scope != 0 || request.sequence != 0 || request.object != -1 ||
            request.option != -1 || request.price != 0 || request.accept_tip || next_stadium_scope_ == UINT64_MAX) {
            error = "invalid_stadium_open"; return false;
        }
    } else if (!stadium_.opened || stadium_.manager != active || stadium_.club != request.runtime_club ||
               stadium_.stable_club != *request.stable_club_identity || stadium_.mode != mode ||
               stadium_.revision != revision || request.scope != stadium_.scope ||
               request.sequence != stadium_.sequence || stadium_.sequence == UINT64_MAX) {
        error = "stale_stadium_scope"; return false;
    }
    auto before = stadium_checkpoint();
    const uint64_t before_revision = revision;
    try {
        const auto apply = [&]() -> bool {
            const auto invalid = [&error](const char* reason) { error = reason; return false; };
            const auto plain = [&request](int object = -1) {
                return request.object == object && request.option == -1 && request.price == 0 && !request.accept_tip;
            };
            const auto clear_investment = [this] {
                stadium_.investment.reset(); stadium_.selection_status.reset(); stadium_.option.reset(); stadium_.tip.reset();
                stadium_.option_index = -1; stadium_.text_variant = -1;
            };
            StadiumInvestmentConstants constants;
            for (unsigned i = 0; i < 8; ++i) {
                constants.price[i] = exe_.u32(::bmh::layout::CareerSession_DispatchStadium_RuntimeField + i * 4);
                constants.weeks[i] = exe_.u8(::bmh::layout::CareerSession_DispatchStadium_RuntimeField2 + (14 + i) * 5);
            }
            const auto options = [&](StadiumInvestment investment) {
                return modern::stadium_investment_options_source(g_, active, investment, stadium_.price_percent, constants);
            };
            const auto selected_valid = [&]() {
                if (!stadium_.investment || !stadium_.option || stadium_.option_index < 0) return false;
                const auto current = options(*stadium_.investment);
                return size_t(stadium_.option_index) < current.size() &&
                    current[size_t(stadium_.option_index)] == *stadium_.option;
            };
            if (request.kind == ui::StadiumRequestKind::open) {
                StadiumRuntime opened;
                opened.opened = true; opened.manager = active; opened.club = request.runtime_club;
                opened.stable_club = *request.stable_club_identity; opened.mode = mode;
                opened.scope = next_stadium_scope_++;
                opened.price_percent = modern::construction_price_percent(g_, active, game_);
                if (opened.price_percent < 1 || opened.price_percent > 427) return invalid("invalid_stadium_discount");
                stadium_ = std::move(opened);
                return true;
            }
            if (request.kind == ui::StadiumRequestKind::close) {
                if (stadium_.data_open || !plain() || stadium_.investment || stadium_.periphery_quote || stadium_.tip ||
                    stadium_.block_expansion)
                    return invalid("invalid_stadium_close");
                stadium_.opened = false; stadium_.periphery_quote.reset(); clear_investment();
                stadium_.feedback.clear(); return true;
            }
            const bool data_action = request.kind == ui::StadiumRequestKind::open_data ||
                request.kind == ui::StadiumRequestKind::toggle_data_curve ||
                request.kind == ui::StadiumRequestKind::toggle_data_scale ||
                request.kind == ui::StadiumRequestKind::set_data_ticket_price ||
                request.kind == ui::StadiumRequestKind::close_data;
            if (data_action) {
                if (stadium_.investment || stadium_.periphery_quote || stadium_.tip || stadium_.block_expansion ||
                    request.option != -1 || request.price != 0 || request.accept_tip)
                    return invalid("invalid_stadium_data_context");
                if (request.kind == ui::StadiumRequestKind::open_data) {
                    if (stadium_.data_open || request.object != -1) return invalid("invalid_stadium_data_open");
                    const uint32_t flags_at = ::bmh::layout::Flags + uint32_t(active) * 3 + 1;
                    if (!g_.memory.has(flags_at)) return invalid("missing_stadium_data_flags");
                    stadium_.data_flags = g_.memory.u8(flags_at);
                    // Build the pure projection before accepting modal ownership.
                    (void)ui::stadium_data_from_game(g_, exe_, active, stadium_.data_flags);
                    stadium_.data_open = true;
                } else {
                    if (!stadium_.data_open) return invalid("stadium_data_not_open");
                    if (request.kind == ui::StadiumRequestKind::toggle_data_curve) {
                        if (request.object < 0 || request.object > 5) return invalid("invalid_stadium_data_curve");
                        stadium_.data_flags ^= uint8_t(1u << request.object);
                    } else if (request.kind == ui::StadiumRequestKind::toggle_data_scale) {
                        if (request.object < 0 || request.object > 1) return invalid("invalid_stadium_data_scale");
                        stadium_.data_flags ^= 0xc0; // Either ABS or REL button toggles both Source bits.
                    } else if (request.kind == ui::StadiumRequestKind::set_data_ticket_price) {
                        if (request.object < 10 || request.object > 25) return invalid("invalid_stadium_data_ticket");
                        g_.manager_records[record + 0x26e] = uint8_t(request.object);
                        (void)ui::stadium_data_from_game(g_, exe_, active, stadium_.data_flags);
                    } else {
                        if (request.object != -1) return invalid("invalid_stadium_data_close");
                        g_.memory.set8(::bmh::layout::Flags + uint32_t(active) * 3 + 1, stadium_.data_flags);
                        stadium_.data_open = false;
                    }
                }
                stadium_.feedback.clear();
                return true;
            }
            if (stadium_.data_open) return invalid("stadium_data_active");
            if (mode == 1 && !legacy_periphery &&
                (request.kind == ui::StadiumRequestKind::quote_periphery ||
                 request.kind == ui::StadiumRequestKind::build_periphery ||
                 request.kind == ui::StadiumRequestKind::decline_periphery))
                return invalid("stadium_periphery_disabled");
            if (request.kind == ui::StadiumRequestKind::quote_periphery) {
                if (stadium_.investment || stadium_.periphery_quote || stadium_.block_expansion || request.object < 0 ||
                    request.object >= 14 || !plain(request.object)) return invalid("invalid_periphery_offer");
                const size_t object = size_t(request.object);
                const uint8_t level = g_.manager_records[record + object];
                if (g_.manager_records[record + 0x1e + object] != 0 || (object == 0 ? level >= 0xab : level != 0))
                    return invalid("periphery_unavailable");
                const int32_t price = modern::periphery_offer(g_, request.object, game_);
                if (price <= 0) return invalid("invalid_periphery_price");
                stadium_.periphery_quote = std::make_pair(request.object, price);
                stadium_.feedback.clear(); return true;
            }
            if (request.kind == ui::StadiumRequestKind::decline_periphery ||
                request.kind == ui::StadiumRequestKind::build_periphery) {
                if (stadium_.investment || stadium_.block_expansion || !stadium_.periphery_quote ||
                    request.option != -1 || request.accept_tip ||
                    request.object != stadium_.periphery_quote->first || request.price != stadium_.periphery_quote->second)
                    return invalid("stale_periphery_offer");
                if (request.kind == ui::StadiumRequestKind::build_periphery) {
                    const size_t object = size_t(request.object);
                    const uint8_t level = g_.manager_records[record + object];
                    if (g_.manager_records[record + 0x1e + object] != 0 || (object == 0 ? level >= 0xab : level != 0))
                        return invalid("periphery_unavailable");
                    // ConstructionPricePercent consumes this genuine JA even when PayConstruction's
                    // payment fails. The Source presenter runs before the quote
                    // is cleared; payment/sponsor effects are not rolled back.
                    const bool built = modern::build_periphery(g_, active, request.object,
                        uint32_t(request.price), rand_, notice_presenter);
                    stadium_.feedback = built || notice_presenter ? std::string{} : message_text(g_, ::bmh::layout::Advertising_FormatError_TextPointer18);
                }
                stadium_.periphery_quote.reset();
                if (request.kind == ui::StadiumRequestKind::decline_periphery) stadium_.feedback.clear();
                return true;
            }

            // A–H uses its own quote/commit domain and remains available in
            // Source mode 1, where facilities are disabled. All request fields
            // outside the typed block envelope must stay neutral.
            if (block_action) {
                if (request.object != -1 || request.option != -1 || request.price != 0 ||
                    (request.kind != ui::StadiumRequestKind::expansion_answer_tip && request.accept_tip))
                    return invalid("invalid_block_expansion_envelope");
                StadiumBlockExpansionConstants block_constants;
                for (unsigned i = 0; i < 8; ++i) {
                    block_constants.price_per_1000[i] = exe_.u32(::bmh::layout::CareerSession_DispatchStadium_RuntimeField + (i + 8) * 4);
                    block_constants.weeks[i] = exe_.u8(::bmh::layout::CareerSession_DispatchStadium_RuntimeField2 + (i + 22) * 5);
                    block_constants.max_places[i] = uint32_t(exe_.u8(::bmh::layout::CareerSession_Invalid_RuntimeField + i)) * 1000;
                }
                const auto quote_block = [&](StadiumBlockSlot slot, uint32_t total, uint8_t features) {
                    return modern::quote_stadium_block_expansion(g_, active, slot, total, features,
                                                         stadium_.price_percent, block_constants);
                };
                if (request.kind == ui::StadiumRequestKind::expansion_select) {
                    if (stadium_.investment || stadium_.periphery_quote || stadium_.block_expansion ||
                        request.block_slot < 0 || request.block_slot >= 8 || request.block_total_places != 0 ||
                        request.block_features != 0 || request.expected_block_quote)
                        return invalid("invalid_block_expansion_selection");
                    const auto slot = static_cast<StadiumBlockSlot>(request.block_slot);
                    const uint8_t raw = g_.manager_records.at(record + 0x44 + size_t(request.block_slot));
                    const uint32_t current = uint32_t(g_.manager_records.at(record + 0x4c + size_t(request.block_slot))) * 1000;
                    auto quote = quote_block(slot, current, uint8_t(raw & kBlockFeatures));
                    if (!quote) return invalid("block_expansion_unavailable");
                    StadiumRuntime::BlockExpansion draft;
                    draft.quote = *quote;
                    draft.total_target_places = current;
                    draft.desired_features = uint8_t(raw & kBlockFeatures);
                    game_.set_seed(int32_t(g_.memory.u32(::bmh::layout::CareerSession_Invalid_RuntimeField2)));
                    draft.header_text_variant = game_.range(3, 10); // Front, once on editor entry.
                    stadium_.block_expansion = std::move(draft);
                    stadium_.feedback.clear();
                    return true;
                }

                if (!stadium_.block_expansion || stadium_.investment || stadium_.periphery_quote ||
                    request.block_slot < 0 || request.block_slot >= 8 ||
                    request.block_slot != int(stadium_.block_expansion->quote.slot) ||
                    !request.expected_block_quote || *request.expected_block_quote != stadium_.block_expansion->quote)
                    return invalid("stale_block_expansion_quote");
                auto& draft = *stadium_.block_expansion;
                if (draft.tip) {
                    const auto& tip = *draft.tip;
                    const bool valid_tip = tip.decision == StandBlockOfferDecision::Pending &&
                        tip.base_price == draft.quote.base_price && tip.base_days == draft.quote.base_weeks &&
                        tip.price == tip.base_price && tip.percent >= 6 && tip.percent <= 14 &&
                        tip.offered_price == uint32_t(uint64_t(tip.base_price) * uint32_t(100 + tip.percent) / 100);
                    if (!valid_tip) return invalid("stale_block_expansion_tip");
                }
                auto fresh = quote_block(draft.quote.slot, draft.total_target_places, draft.desired_features);
                if (!fresh || *fresh != draft.quote)
                    return invalid("block_expansion_source_changed");

                const auto matches_draft = [&] {
                    return request.block_total_places == draft.total_target_places &&
                        request.block_features == draft.desired_features;
                };
                switch (request.kind) {
                case ui::StadiumRequestKind::expansion_set_total: {
                    if (draft.tip || request.block_features != draft.desired_features)
                        return invalid("stale_block_expansion_draft");
                    auto next_quote = quote_block(draft.quote.slot, request.block_total_places, draft.desired_features);
                    if (!next_quote) return invalid("invalid_block_expansion_total");
                    draft.total_target_places = request.block_total_places;
                    draft.quote = *next_quote;
                    stadium_.feedback.clear();
                    return true;
                }
                case ui::StadiumRequestKind::expansion_toggle_feature: {
                    const uint8_t changed = uint8_t(request.block_features ^ draft.desired_features);
                    if (draft.tip || request.block_total_places != draft.total_target_places ||
                        (request.block_features & uint8_t(~kBlockFeatures)) != 0 ||
                        (changed != kBlockSeat && changed != kBlockRoof) ||
                        (request.block_features & uint8_t(draft.quote.current_raw_flags & kBlockFeatures)) !=
                            uint8_t(draft.quote.current_raw_flags & kBlockFeatures))
                        return invalid("invalid_block_expansion_feature");
                    auto next_quote = quote_block(draft.quote.slot, draft.total_target_places, request.block_features);
                    if (!next_quote) return invalid("invalid_block_expansion_feature");
                    draft.desired_features = request.block_features;
                    draft.quote = *next_quote;
                    stadium_.feedback.clear();
                    return true;
                }
                case ui::StadiumRequestKind::expansion_cancel:
                    if (draft.tip || !matches_draft()) return invalid("stale_block_expansion_cancel");
                    stadium_.block_expansion.reset();
                    stadium_.feedback.clear();
                    return true;
                case ui::StadiumRequestKind::expansion_confirm: {
                    if (draft.tip || !matches_draft()) return invalid("stale_block_expansion_confirm");
                    if (draft.quote.base_price == 0) {
                        stadium_.block_expansion.reset();
                        stadium_.feedback.clear();
                        return true;
                    }
                    auto tip = modern::prepare_stand_block_offer(draft.quote.base_price, draft.quote.base_weeks, rand_);
                    if (tip.offered_price > uint32_t(INT32_MAX))
                        return invalid("block_expansion_tip_price_overflow");
                    draft.tip = tip;
                    stadium_.feedback.clear();
                    return true;
                }
                case ui::StadiumRequestKind::expansion_answer_tip: {
                    if (!draft.tip || !matches_draft() || request.accept_tip &&
                        uint64_t(draft.tip->base_price) * uint32_t(100 + draft.tip->percent) >
                            uint64_t(INT32_MAX))
                        return invalid("invalid_block_expansion_tip_answer");
                    StandBlockOffer resolved = *draft.tip;
                    modern::resolve_stand_block_offer(resolved, request.accept_tip, rand_);
                    const bool built = modern::build_stadium_block_expansion(g_, active, draft.quote.slot,
                        block_constants, draft.quote, resolved, rand_, notice_presenter, compensation_presenter);
                    stadium_.block_expansion.reset();
                    stadium_.feedback = built || notice_presenter ? std::string{} : message_text(g_, ::bmh::layout::Advertising_FormatError_TextPointer18);
                    return true;
                }
                default:
                    return invalid("unsupported_block_expansion_request");
                }
            }
            if (mode == 1) return invalid("stadium_investments_disabled");
            if (request.kind == ui::StadiumRequestKind::select_investment) {
                if (stadium_.investment || stadium_.periphery_quote || stadium_.block_expansion || request.object < 0 ||
                    request.object >= 8 || !plain(request.object)) return invalid("invalid_investment_selection");
                const auto investment = static_cast<StadiumInvestment>(request.object);
                const auto current = options(investment);
                if (current.empty()) return invalid("investment_unavailable");
                game_.set_seed(int32_t(g_.memory.u32(::bmh::layout::CareerSession_Invalid_RuntimeField2)));
                stadium_.text_variant = game_.range(3, 10); // Front: exactly once at dialog entry.
                stadium_.investment = investment; stadium_.selection_status = current.front().current_status;
                stadium_.option_index = -1; stadium_.option.reset(); // Source iVar5=-1 until an explicit tier click.
                stadium_.feedback.clear(); return true;
            }
            if (request.kind == ui::StadiumRequestKind::cancel_investment) {
                if (!stadium_.investment || stadium_.tip || !plain(int(*stadium_.investment)))
                    return invalid("stale_investment_selection");
                clear_investment(); stadium_.feedback.clear(); return true; // Initial Abbruch precedes W(6,14).
            }
            if (request.kind == ui::StadiumRequestKind::select_investment_option) {
                if (!stadium_.investment || stadium_.tip || request.object != int(*stadium_.investment) ||
                    request.option < 0 || request.accept_tip || !stadium_.selection_status ||
                    g_.manager_records[record + 0x3c + size_t(*stadium_.investment)] != *stadium_.selection_status)
                    return invalid("stale_investment_selection");
                const auto current = options(*stadium_.investment);
                if (size_t(request.option) >= current.size() || request.price != int32_t(current[size_t(request.option)].base_price))
                    return invalid("stale_investment_price");
                stadium_.option_index = request.option; stadium_.option = current[size_t(request.option)];
                return true; // Pure tier choice: neither Source generator advances.
            }
            if (request.kind == ui::StadiumRequestKind::confirm_investment) {
                if (stadium_.tip || !selected_valid() || request.object != int(*stadium_.investment) ||
                    request.option != stadium_.option_index || request.accept_tip ||
                    request.price != int32_t(stadium_.option->base_price)) return invalid("stale_investment_selection");
                stadium_.tip = modern::prepare_stand_block_offer(stadium_.option->base_price, stadium_.option->base_weeks, rand_);
                if (stadium_.tip->offered_price > uint32_t(INT32_MAX)) return invalid("invalid_investment_tip_price");
                return true;
            }
            if (request.kind == ui::StadiumRequestKind::answer_construction_tip) {
                if (!stadium_.tip || !selected_valid() || request.object != int(*stadium_.investment) ||
                    request.option != stadium_.option_index || request.price != int32_t(request.accept_tip ?
                        stadium_.tip->offered_price : stadium_.tip->base_price)) return invalid("stale_construction_tip");
                auto resolved = *stadium_.tip;
                modern::resolve_stand_block_offer(resolved, request.accept_tip, rand_);
                const bool built = modern::build_stadium_investment_source(g_, active, *stadium_.investment,
                    stadium_.price_percent, constants, *stadium_.option, resolved, rand_, notice_presenter);
                // A genuine PayConstruction/BookMoney2 refusal consumes the answer and
                // retains the sponsor share clearing and Source W draws.
                // Interactive callers have already shown Source's timed funds
                // notice; headless legacy callers retain their text feedback.
                clear_investment();
                stadium_.feedback = built || notice_presenter ? std::string{} : message_text(g_, ::bmh::layout::Advertising_FormatError_TextPointer18);
                return true;
            }
            return invalid("unsupported_stadium_request");
        };
        if (!apply()) { restore_stadium_checkpoint(std::move(before)); return false; }
        ++revision; ++stadium_.sequence; stadium_.revision = revision;
        error.clear(); return true;
    } catch (...) {
        restore_stadium_checkpoint(std::move(before)); revision = before_revision; throw;
    }
}

bool ModernSession::dispatch_toto(const ui::TotoRequest& request, uint64_t& revision, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    const int active = int(g_.memory.u8(::bmh::layout::ActiveManager));
    if (request.kind != ui::TotoRequestKind::submit || request.manager_slot != active || request.manager_slot < 0 ||
        size_t(request.manager_slot) >= g_.manager_teams.size() ||
        request.runtime_club != g_.manager_teams[size_t(request.manager_slot)] || request.revision != revision) {
        error = "stale_manager_or_revision"; return false;
    }
    if (g_.memory.u8(::bmh::layout::CareerSession_DispatchToto_RuntimeField) != 11) { error = "toto_coupon_requires_eleven_matches"; return false; }
    // one column: the original coupon; earlier extra columns of this manager are dropped
    if (modern::submit_pools_columns(g_, request.manager_slot, request.stake, {request.tips}) != PoolsColumnsResult::ok) {
        error = "invalid_toto_coupon"; return false;
    }
    ++revision;
    return true;
}

bool ModernSession::dispatch_pools_columns(int manager, uint32_t stake, const std::vector<std::array<uint8_t, 11>>& columns,
                                           uint64_t& revision, std::string& error) {
    const int active = int(g_.memory.u8(::bmh::layout::ActiveManager));
    if (manager != active || manager < 0 || size_t(manager) >= g_.manager_teams.size()) {
        error = "stale_manager_or_revision"; return false;
    }
    if (g_.memory.u8(::bmh::layout::CareerSession_DispatchToto_RuntimeField) != 11) { error = "toto_coupon_requires_eleven_matches"; return false; }
    switch (modern::submit_pools_columns(g_, manager, stake, columns)) {
        case PoolsColumnsResult::ok: ++revision; return true;
        case PoolsColumnsResult::too_many_columns: error = "too_many_columns"; return false;
        case PoolsColumnsResult::stake_out_of_range: error = "stake_out_of_range"; return false;
        case PoolsColumnsResult::total_too_high: error = "total_stake_too_high"; return false;
        case PoolsColumnsResult::duplicate_column: error = "duplicate_column"; return false;
        case PoolsColumnsResult::invalid: break;
    }
    error = "invalid_toto_coupon";
    return false;
}

}  // namespace bmh::modern::app

namespace bmh::modern::app {

PendingOfferHooks ModernSession::pending_hooks_for_dispatch(PendingOfferHooks hooks) const {
    // Presenters own answers and notice delivery, never allocator provenance.
    hooks.human_buyer_allocator_gate = turn_driver_.transfer_allocator_context;
    return hooks;
}

void ModernSession::install_default_hooks() {
    // InstallDefaultHooks pauses at the Original contracts screen when a term expires.
    // Never synthesize a two-year renewal here: callers must install the real
    // interactive ContractExpiryPresenter or month_change stops without a decision.
    g_.contract_screen = {};
    g_.loan_return_screen = {};
    g_.loan_return_offer_screen = {};
}

void ModernSession::set_contract_expiry_presenter(ContractExpiryPresenter presenter) {
    // Clear any previously installed presenter before replacing it. The only
    // fallback is an explicit unsupported expiry, never a synthesized decision.
    auto loan_return_hook = std::move(g_.loan_return_screen);
    auto loan_offer_hook = std::move(g_.loan_return_offer_screen);
    install_default_hooks();
    g_.loan_return_screen = std::move(loan_return_hook);
    g_.loan_return_offer_screen = std::move(loan_offer_hook);
    contract_expiry_presenter_installed_ = bool(presenter);
    if (!presenter) return;
    g_.contract_screen = [this, presenter = std::move(presenter)](GameState& base_state, int manager, int row) {
        auto& g = static_cast<ModernState&>(base_state);
        return presenter(g, rand_, manager, row);
    };
}

void ModernSession::set_loan_return_presenter(LoanReturnPresenter presenter) {
    auto contract_hook = std::move(g_.contract_screen);
    auto loan_offer_hook = std::move(g_.loan_return_offer_screen);
    install_default_hooks();
    g_.contract_screen = std::move(contract_hook);
    g_.loan_return_offer_screen = std::move(loan_offer_hook);
    loan_return_presenter_installed_ = bool(presenter);
    if (!presenter) return;
    g_.loan_return_screen = [this, presenter = std::move(presenter)](
        GameState& base_state, int manager, const std::string& question) {
        auto& g = static_cast<ModernState&>(base_state);
        return presenter(g, rand_, manager, question);
    };
}

void ModernSession::set_loan_return_offer_presenter(LoanReturnOfferPresenter presenter,
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
        GameState& base_state, GameRandom& game, WatcomRandom& rand, int manager,
        int source_slot, int list_entry, const HumanBuyerSaleHooks&) mutable {
        auto& g = static_cast<ModernState&>(base_state);
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

}  // namespace bmh::modern::app
