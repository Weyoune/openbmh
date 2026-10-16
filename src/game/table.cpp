// SPDX-License-Identifier: GPL-3.0-only
#include "game/table.hpp"

#include <vector>

#include "game/lineup.hpp"
#include "game/schedule.hpp"

namespace bmh {


// AddResult3: one result (goals of both sides) for the two clubs (draw positions).
// flags 1 = count only the home side, 2 = only the away side.
void add_result(LeagueTable& t, const int club[2], const uint8_t goals_in[2], int flags, int win_bonus) {
    if (club[0] > 0x13 || club[1] > 0x13) return;
    const uint8_t goals[2] = {uint8_t(goals_in[0] & 0x1F), uint8_t(goals_in[1] & 0x1F)};
    const int first = flags == 2 ? 1 : 0, last = flags == 1 ? 1 : 2;
    for (int s = first; s < last; s++) {
        const int c = club[s];
        t.goals_for[c] = uint8_t(t.goals_for[c] + goals[s]);
        t.goals_against[c] = uint8_t(t.goals_against[c] + goals[1 - s]);
        t.rank_word[c]++;  // games in bits 0-5
        int outcome = 0, bonus = 0;
        if (goals[s] > goals[1 - s]) {
            outcome = 2;
            bonus = win_bonus;
            t.wins[c]++;
        } else if (goals[s] == goals[1 - s]) {
            outcome = 1;
        } else {
            t.defeats[c]++;
        }
        t.points[c] = int8_t(t.points[c] + outcome + bonus);
        t.form[c] = (t.form[c] << 3) | uint32_t(s * 4 + outcome + 1);
    }
}

// SortKey: sort key of a club; `parts` bits switch off points (1), goals scored
// (2) and goals conceded (4).
uint32_t sort_key(const LeagueTable& t, int c, int parts) {
    uint32_t goals = 0;
    int diff = 0x100, pts = 0, per_game = 0;
    if (!(parts & 2)) {
        diff = t.goals_for[c] + 0x100;
        goals = t.goals_for[c];
    }
    if (!(parts & 4)) diff -= t.goals_against[c];
    if (!(parts & 1)) {
        pts = t.points[c] < 0 ? 0 : t.points[c];
        per_game = pts * 2 + 0x80 - (t.rank_word[c] & 0x3F) * 2;
    }
    return uint32_t(pts) << 25 | uint32_t(per_game) << 17 | uint32_t(diff) << 8 | goals;
}

// AddResult2: sort (only if the keys are not already descending) and store the ranks;
// the old rank moves to bits 6-10.
void sort_table(LeagueTable& t, int parts) {
    int16_t order[20];
    uint32_t keys[20];
    bool unsorted = false;
    uint32_t previous = 0xFFFFFFFFu;
    for (int c = 0; c < t.size; c++) {
        order[c] = int16_t(c);
        keys[c] = sort_key(t, c, parts);
        if (previous < keys[c]) unsorted = true;
        previous = keys[c];
    }
    if (unsorted) quicksort_by_key(order, keys, t.size, false);
    for (int i = 0; i < t.size; i++) {
        uint16_t& w = t.rank_word[order[i]];
        w = uint16_t(((w >> 11) << 6) + i * 0x800 + (w & 0x3F));
    }
}


int league_matchdays(int league) { return kLeagueSizes[league] * 2 - 2; }

LeagueTable compute_league_table(const GameState& g, int league, int from, int to, int flags, int points_mode) {
    LeagueTable t;
    t.league = league;
    t.size = kLeagueSizes[league];
    int parts = 0;
    if (points_mode & 0xE) {
        parts = points_mode >> 1;
        points_mode &= 1;
    }
    const int first_team = league_first_team(league);
    if (flags & 4) {
        // a human manager's point deduction (manager record +0x220: 3 << 5 | points)
        for (int c = 0; c < t.size; c++) {
            int manager = g.manager_of(first_team + c);
            if (manager < 0) continue;
            uint8_t d = g.manager_records.at(size_t(manager) * 0xB0C + 0x220);
            if (d >> 5 == 3) t.points[c] = int8_t(t.points[c] - (d & 0x1F));
        }
        flags &= ~4;
    }
    const int rounds_played = league < int(g.league_rounds.size()) ? g.league_rounds[league] : 0;
    int last_round = uint8_t(rounds_played - 1);  // local_1c
    int before_last = 0x80;                       // local_10
    if (last_round > 1) before_last = uint8_t(rounds_played - 2);
    if (last_round > 0x7F) last_round = 0;
    const int matchdays = league_matchdays(league);
    if (to > matchdays) to = matchdays;
    if (to == 0) to = 1;
    for (int r = from; r < to; r++) {
        std::vector<Fixture> fx = generate_fixtures(league_table_teams(league), r + 1, matchdays / 2, first_team);
        bool counted = false;
        for (size_t i = 0; i < fx.size(); i++) {
            uint8_t b = g.results.at(size_t(league) * 0x17C + size_t(r) * 10 + i);
            int club[2] = {fx[i].home - first_team, fx[i].away - first_team};
            if ((b & 0xCC) == 0xCC) {
                if (b == 0xCD)
                    for (int s = 0; s < 2; s++) t.form[club[s]] = t.form[club[s]] << 3 | 4;
                continue;
            }
            const uint8_t goals[2] = {uint8_t(b >> 4), uint8_t(b & 0xF)};
            add_result(t, club, goals, flags, points_mode);
            counted = true;
        }
        if (to <= r + 2 || r == before_last || r == last_round || (last_round < r && counted)) sort_table(t, parts);
    }
    for (int c = 0; c < t.size; c++)
        if (t.points[c] < 0) t.points[c] = 0;
    return t;
}

}  // namespace bmh
