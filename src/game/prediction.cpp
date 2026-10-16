// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/prediction.hpp"

#include <string>
#include <stdexcept>

#include "game/matchday.hpp"
#include "game/messages.hpp"
#include "game/schedule.hpp"
#include "game/table.hpp"

namespace bmh {

namespace {

constexpr size_t kRecord = 0xB0C;     // manager record
constexpr size_t kBoard = 0x840;      // tactics board of a manager
constexpr size_t kBoardSlot = 0x58;   // one slot of the board

// Static league tables of V2.24 (not part of the savegame):
// (European places per league, CareerSession_InstallLeagueSizes_RuntimeField, are kLeagueEurope of the loaded game; league 0 uses 4)
// (the relegation places per league, CareerSession_InstallLeagueSizes_RuntimeField2, are kLeagueRelegation of the loaded game)
constexpr int kLeagueTeams = 138;  // CareerSession_DispatchLineup_RuntimeField: clubs in the league system

// Text of a pointer variable of the text table TextPointerTable.
}  // namespace

int table_prediction(GameState& g, int team, int mode, std::array<uint8_t, 24>& buf, int* size_word) {
    buf.fill(1);
    int league = team_league(team);  // local_30
    if (league >= kLeagueCount) return 0;  // reads CareerRuntimeField6[0] = 0 as the round count
    const int rounds = int8_t(g.league_rounds.at(size_t(league)));  // local_1c
    const int matchdays = league_matchdays(league);                 // LeagueMatchdayCounts
    if (rounds < 2 || matchdays <= rounds) return 0;
    const int manager = g.manager_of(team);  // local_18, 0xFF = none
    // (Fill would refresh the desk warning flags PredictionRuntimeField.. here; they are
    //  run-time only and not needed by the game logic.)
    const size_t rec = size_t(manager) * kRecord;
    const int size = kLeagueSizes[league];  // local_2c
    if (size_word) *size_word = size;       // (stored as a word, see the header)
    if (team >= kLeagueTeams) return 9;
    const int pos = team - league_first_team(league);  // local_24
    const int target[3] = {0, league == 0 ? 4 : kLeagueEurope[league], size - kLeagueRelegation[league]};

    int from = 0;  // local_10
    if (mode > 3) {
        from = rounds - 5 * (mode >> 2);
        if (from < 0) from = 0;
    }
    auto per_game = [&](const LeagueTable& t, int club) {
        const int games = t.games(club);
        return games ? t.points[club] * 500 / games : 0;  // the original divides by zero here
    };
    int own_rate = 1500;  // local_34: three points per remaining game
    if (mode != 1) own_rate = per_game(compute_league_table(g, league, from, rounds, 3, 0), pos);
    const LeagueTable full = compute_league_table(g, league, 0, rounds, 7, 0);
    int points[20] = {}, remaining[20] = {};  // acStack_60, acStack_4c
    for (int c = 0; c < size; c++) {
        points[c] = full.points[c];
        remaining[c] = int8_t(matchdays - full.games(c));
    }
    // The scratch table the rates of the other clubs are read from: the recent form table
    // for mode >= 4, otherwise still the full table.
    const LeagueTable rates = mode > 3 ? compute_league_table(g, league, from, rounds, 3, 0) : full;
    uint16_t own = uint16_t(points[pos] * 500 + remaining[pos] * own_rate);  // local_28 (low word)

    int ahead = size;  // local_20: ranks that are out of reach
    if (mode == 1) {
        ahead = 0;
        for (int c = 0; c < size; c++) ahead += int(own) < points[c] * 500;
    }
    for (int r = 0; r < ahead; r++) buf.at(size_t(r)) = 0;

    int league_index = league;  // local_30 is changed by the league name of message 1
    if (mode == 1) {
        for (int k = 0; k < 3; k++) {
            if (buf.at(size_t(target[k])) != 0) continue;
            if (manager >= 0 && (g.manager_records.at(rec + 0x28b) >> k & 1)) continue;
            // the goal can no longer be reached
            if (manager >= 0) g.manager_records.at(rec + 0x28b) |= uint8_t(1 << k);
            std::string msg = g.club_name(team) + message_text(g, ::bmh::layout::Prediction_TablePrediction_TextPointer2);
            if (k == 0) msg += message_text(g, ::bmh::layout::Prediction_TablePrediction_TextPointer3);
            if (k == 1) {
                std::string tail;
                if (league_index == 0) {
                    tail = message_text(g, ::bmh::layout::Prediction_TablePrediction_TextPointer4);
                } else if (league_index == 2 || league_index == 3) {
                    tail = message_text(g, ::bmh::layout::Prediction_TablePrediction_TextPointer5);
                } else {
                    msg += message_text(g, ::bmh::layout::Prediction_TablePrediction_TextPointer6);
                    if (league_index > 2) league_index--;
                    msg += message_text(g, ::bmh::layout::Prediction_TablePrediction_TextPointer + uint32_t(league_index) * 4);  // league name
                    tail = message_text(g, ::bmh::layout::Prediction_TablePrediction_TextPointer7);
                }
                msg += tail;
            }
            if (k == 2) msg += message_text(g, ::bmh::layout::Prediction_TablePrediction_TextPointer8);
            if (manager >= 0) post_composed(g, manager, msg);
        }
        own = uint16_t(points[pos] * 500);
    }

    int behind = 0;  // local_14: clubs that can no longer catch up
    for (int c = 0; c < size; c++) {
        if (c == pos) continue;
        int rate;
        if (mode == 1) rate = 1500;
        else if (mode == 2) rate = 0;
        else rate = int16_t(per_game(rates, c));
        if (uint16_t(points[c] * 500 + remaining[c] * rate) < own) behind++;
    }
    int r = 0;
    for (; r < behind; r++) buf.at(size_t(size - r - 1)) = 0xFF;
    if (mode != 1) {
        if (r == size) r++;  // cannot happen: the club itself is never counted
        buf.at(size_t(size - r - 1)) = 1;
    }
    if (mode == 1) {
        for (int k = 0; k < 3; k++) {
            if (buf.at(size_t(target[k])) != 0xFF) continue;
            if (manager >= 0 && (g.manager_records.at(rec + 0x28b) >> (k + 3) & 1)) continue;
            // the goal is secured (or relegation avoided / certain)
            if (manager >= 0) g.manager_records.at(rec + 0x28b) |= uint8_t(1 << (k + 3));
            std::string msg = g.club_name(team) + message_text(g, ::bmh::layout::Prediction_TablePrediction_TextPointer9);
            if (k == 0) msg += message_text(g, ::bmh::layout::Prediction_TablePrediction_TextPointer10);
            else if (k == 1)
                msg += message_text(g, league_index == 0 ? ::bmh::layout::Prediction_TablePrediction_TextPointer11 : (league_index == 2 || league_index == 3) ? ::bmh::layout::Prediction_TablePrediction_TextPointer12 : ::bmh::layout::Prediction_TablePrediction_TextPointer13);
            else msg += message_text(g, ::bmh::layout::Prediction_TablePrediction_TextPointer14);
            msg += message_text(g, ::bmh::layout::Prediction_TablePrediction_TextPointer15);
            // without a manager the original posts to "manager" 0xFE; not needed here
            if (manager >= 0) post_composed(g, manager, msg);
        }
    }
    int first = 0;
    while (first < 24 && buf[size_t(first)] == 0) first++;
    return (first + 1) & 0xFF;
}

static uint32_t season_bonus_impl(GameState& g, int manager, int team, int board_slot, int rank, bool pay) {
    const int league = team_league(team);
    uint32_t amount = 0;
    if (league == 0 && rank == 0) amount = pay_bonus(g, manager, board_slot, 3, pay);
    if (amount == 0 && rank < 5) amount = pay_bonus(g, manager, board_slot, 4, pay);
    if (amount == 0 && rank < 10) amount = pay_bonus(g, manager, board_slot, 5, pay);
    if (amount == 0 && league < kLeagueCount && rank < kLeagueSizes[league] - kLeagueRelegation[league])
        amount = pay_bonus(g, manager, board_slot, 6, pay);
    return amount;
}

uint32_t season_bonus(GameState& g, int manager, int board_slot, int rank, bool pay) {
    return season_bonus_impl(g,manager,g.manager_teams.at(size_t(manager)),board_slot,rank,pay);
}

static uint32_t expected_bonus_impl(GameState& g, int manager, int team, int rank, int board_slot, int player, bool fitness) {
    static const uint8_t kPerSeason[3] = {0, 34, 25};   // PredictionRuntimeField2: -, points, appearances
    static const uint8_t kGoals[4] = {0, 2, 4, 8};      // PredictionRuntimeField3: goals by position
    const int position = int(g.players.at(size_t(player)).w364 & 3);
    if (rank == 0xFF) {
        std::array<uint8_t, 24> buf;
        const int r = table_prediction(g, team, 4, buf);
        rank = r == 0 ? 0xB : r + 1;
    }
    const size_t slot = size_t(manager) * kBoard + size_t(board_slot) * kBoardSlot;
    const uint32_t base = uint32_t(g.tactics.at(slot + 0x50) | g.tactics.at(slot + 0x51) << 8 |
                                   g.tactics.at(slot + 0x52) << 16) | uint32_t(g.tactics.at(slot + 0x53)) << 24;
    uint32_t sum = 0;
    for (int k = 0; k < 3; k++) {
        uint32_t times = k == 0 ? kGoals[position] : kPerSeason[k];
        if (fitness && k != 1) {
            // the fitness correction: a player weaker than the team average (QuoteNationalFriendlyMatch
            // flags 499) earns goal and appearance bonuses less often - PredictionRuntimeField4 per gap
            static const uint8_t kPerGap[3] = {1, 0, 5};  // PredictionRuntimeField4
            const uint32_t w = g.players.at(size_t(player)).w35c;
            int gap = int16_t(team_average(g, team, 499).value -
                              int((((w >> 14) & 0x7F) + ((w >> 7) & 0x7F)) >> 1));
            if (gap < -1) gap = -1;
            const int t = int(times) - kPerGap[k] * gap;
            times = t < 0 ? 0 : uint32_t(t);
        }
        sum += g.tactics.at(slot + 0x12 + size_t(k)) * base / 100 * times / 12;
    }
    return season_bonus_impl(g, manager, team, board_slot, rank & 0xFF, false) / 12 + sum;
}

uint32_t expected_bonus(GameState& g, int manager, int rank, int board_slot, int player, bool fitness) {
    return expected_bonus_impl(g,manager,g.manager_teams.at(size_t(manager)),rank,board_slot,player,fitness);
}

uint32_t expected_bonus_parked_club_source(GameState& g, int rank, int board_slot, int player, bool fitness) {
    constexpr size_t team_at=4*kRecord+0x290;
    if(g.manager_teams!=std::vector<int>{kNoTeam} || !g.memory.has(::bmh::layout::CoachActive) ||
        g.memory.u8(::bmh::layout::CoachActive)==0 || !g.memory.has(::bmh::layout::ManagerCount) || g.memory.u8(::bmh::layout::ManagerCount)!=1 ||
        !g.memory.has(::bmh::layout::OriginalManagerCount) || g.memory.u8(::bmh::layout::OriginalManagerCount)<1 || g.memory.u8(::bmh::layout::OriginalManagerCount)>4 ||
        g.manager_records.size()<5*kRecord ||
        (int(g.manager_records[0x290]) | int(g.manager_records[0x291])<<8)!=kNoTeam ||
        board_slot<0 || board_slot>=24 || g.tactics.size()<5*kBoard)
        throw std::invalid_argument("expected_bonus: invalid FindManagerForClub parked club context");
    const int team=int(g.manager_records[team_at]) | int(g.manager_records[team_at+1])<<8;
    if(team<0 || team>=kLeagueTeams)
        throw std::invalid_argument("expected_bonus: parked record4 has no ordinary club");
    return expected_bonus_impl(g,4,team,rank,board_slot,player,fitness);
}

}  // namespace bmh
