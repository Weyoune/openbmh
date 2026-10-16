// SPDX-License-Identifier: GPL-3.0-only
#include "game/top_scorers.hpp"

#include <array>
#include <stdexcept>
#include <utility>

#include "game/schedule.hpp"

namespace bmh::game {
namespace {

constexpr size_t kManagerRecordSize = 0xb0c;
constexpr size_t kNationalBackupManager = 4;
constexpr size_t kManagerTeamOffset = 0x290;

[[noreturn]] void invalid(const std::string& reason) {
    throw FormatError("top scorers: " + reason);
}

bool source_manager_owns(const GameState& g, int team, size_t manager_count) {
    // FindManagerForClub checks every active manager in order, then attributes the
    // national coach's original club to Source manager slot 4 when slot 0 is
    // temporarily assigned to 0x165.
    for (size_t manager = 0; manager < manager_count; ++manager) {
        if (manager < g.manager_teams.size() && g.manager_teams[manager] == team) return true;
    }
    if (!g.manager_teams.empty() && g.manager_teams[0] == kNoTeam) {
        const size_t original_at = kNationalBackupManager * kManagerRecordSize + kManagerTeamOffset;
        if (original_at + 2 > g.manager_records.size())
            invalid("truncated national-coach backup manager record");
        const uint16_t original = uint16_t(g.manager_records[original_at] |
            uint16_t(g.manager_records[original_at + 1]) << 8);
        if (original == team) return true;
    }
    return false;
}

uint8_t metric_score(uint8_t goals, uint8_t assists, TopScorersMetric metric) {
    switch (metric) {
    case TopScorersMetric::goals: return goals;
    case TopScorersMetric::assists: return assists;
    case TopScorersMetric::scorer: return uint8_t(goals + assists);
    }
    invalid("unknown metric");
}

} // namespace

TopScorersProjection top_scorers_from_game(const GameState& g, int manager_slot,
                                            TopScorersMetric metric,
                                            TopScorersScope scope) {
    switch (metric) {
    case TopScorersMetric::goals:
    case TopScorersMetric::assists:
    case TopScorersMetric::scorer: break;
    default: invalid("unknown metric");
    }
    switch (scope) {
    case TopScorersScope::league:
    case TopScorersScope::managers: break;
    default: invalid("unknown scope");
    }
    const size_t manager_count = g.manager_teams.size();
    if (manager_count == 0 || manager_count > 4 || manager_slot < 0 ||
        size_t(manager_slot) >= manager_count || size_t(manager_slot) >= g.manager_teams.size())
        invalid("manager slot is outside the active manager table");
    const int manager_team = g.manager_teams[size_t(manager_slot)];
    if (manager_team < 0)
        invalid("manager has no supported domestic league");
    // TeamLeague2 deliberately maps team IDs above 0x164 (including the
    // national team 0x165) to league zero; ordinary cup-only teams map to 9.
    const int league = manager_team > 0x164 ? 0 : team_league(manager_team);
    if (league < 0 || league >= kLeagueCount)
        invalid("manager team is outside Source's nine domestic leagues");

    const int first_team = league_first_team(league);
    const int team_count = kLeagueSizes[league];
    if (first_team < 0 || first_team + team_count > 138 ||
        g.club_ids.size() < size_t(first_team + team_count) || g.club_names.empty())
        invalid("incomplete domestic club table");

    TopScorersProjection result;
    result.manager_slot = manager_slot;
    result.league = league;
    result.metric = metric;
    result.scope = scope;

    std::array<TopScorersRow, 10> rows{};
    std::array<uint8_t, 10> scores{};
    size_t used = 0;
    for (int team = first_team; team < first_team + team_count; ++team) {
        if (scope == TopScorersScope::managers && !source_manager_owns(g, team, manager_count)) continue;
        const SquadRange range = squad_range(team);
        if (range.first > g.squad_slots.size() || range.count > g.squad_slots.size() - range.first)
            invalid("incomplete squad slots for domestic club " + std::to_string(team));
        for (size_t offset = 0; offset < range.count; ++offset) {
            const uint16_t slot = g.squad_slots[range.first + offset];
            // TopScorersFromGame skips an empty squad position and continues through the
            // fixed-size roster; a gap does not terminate Source traversal.
            if ((slot & 0x0fff) == kEmptySlot) continue;
            const int player = int(slot & 0x0fff);
            if (size_t(player) >= g.players.size())
                invalid("squad references missing player " + std::to_string(player));
            const Player& p = g.players[size_t(player)];
            const uint8_t goals = uint8_t((p.w364 >> 2) & 0x3f);
            const uint8_t assists = uint8_t((p.w368 >> 18) & 0x3f);
            const uint8_t score = metric_score(goals, assists, metric);
            if (score == 0) continue;

            size_t position = 0;
            while (position < used && !(scores[position] < score)) ++position;
            if (position >= rows.size()) continue;
            const size_t new_used = used < rows.size() ? used + 1 : used;
            for (size_t move = new_used; move > position + 1; --move) {
                rows[move - 1] = std::move(rows[move - 2]);
                scores[move - 1] = scores[move - 2];
            }
            const uint8_t source_goals = metric == TopScorersMetric::assists ? 0 : goals;
            const uint8_t source_assists = metric == TopScorersMetric::goals ? 0 : assists;
            rows[position] = TopScorersRow{0, player, team, int(range.first + offset), source_goals, source_assists, score,
                                           g.player_name(player), g.club_name(team)};
            scores[position] = score;
            used = new_used;
        }
    }

    result.rows.assign(rows.begin(), rows.begin() + used);
    for (size_t i = 0; i < result.rows.size(); ++i) result.rows[i].rank = int(i + 1);
    result.no_results = result.rows.empty();
    return result;
}

} // namespace bmh::game
