// SPDX-License-Identifier: GPL-3.0-only
#include "game/rating.hpp"

#include <array>
#include <cstdlib>
#include <stdexcept>

#include "game/prediction.hpp"

namespace bmh {

int player_value(const GameState& g, int player, uint16_t attrs, int scale, int team, const RatingContext& ctx, int x, int y) {
    if (player == 0xFFF) return 0;
    const Player& p = g.players.at(player);
    const uint32_t w1 = p.w35c, w3 = p.w364;

    int sum = 0, weight = 0, form_term = 0;
    // local_1c / local_20, only set when selected (see RatingContext::stale_condition)
    int condition = ctx.stale_condition >= 0 ? ctx.stale_condition : 0;
    int technique = ctx.stale_technique >= 0 ? ctx.stale_technique : 0;
    int skill[7] = {-1, -1, -1, -1, -1, -1, -1};  // selected skills * 5 (local_38, local_2c, ...)
    for (int bit = 0; bit < 16; bit++) {
        if (!((int16_t(attrs) >> bit) & 1)) continue;
        if (bit < 2) {
            weight += 100;
            int v = bit == 0 ? int(w1 >> 14 & 0x7F) : int(w1 >> 7 & 0x7F);
            (bit == 0 ? condition : technique) = v;
            sum += v;
        } else if (bit == 2) {
            form_term += int(w1 & 0x7F) / 8 - 6;
        } else if (bit == 3 || bit == 4) {
            int pos = int(w3 & 3);
            sum += bit == 3 ? 9 - 3 * pos : 3 * pos;
            weight += 9;
        } else if (bit == 12) {
            int v = int(w3 >> 2 & 0x3F) * 3;
            sum += v > 20 ? 20 : v;
            weight += 20;
        }
        if (bit > 4 && bit < 12) {  // the seven skills, bit 5 = skill 0
            int v = int(p.w368 >> (bit * 2 - 10) & 3) * 5;
            skill[bit - 5] = v;
            sum += v;
            weight += 15;
        }
    }

    int multiplier = 100;
    if (attrs & 3) {
        int bonus = g.player_bonus.at(player) & 0x7F;
        if (bonus != 0) multiplier = (bonus > 50 ? 50 : bonus) / 2 + 100;
        multiplier = multiplier + form_term - (6 - ((g.team_morale.at(team) & 0x3F) >> 2));
        // Imbalance between condition and technique costs extra in certain match situations.
        if (condition < technique && (ctx.chance_rain > 2 || int8_t(ctx.chance_temperature) < 0)) multiplier -= ctx.chance_rain * 3;
        if (technique < condition && int8_t(ctx.chance_temperature) > 25) multiplier -= int8_t(ctx.chance_temperature) - 25;
    }
    // Human managers from game level 4 on: the place on the tactics board counts. Field
    // players get skill bonuses for fitting places and lose for the distance between the
    // place and their preferred depth/lane (w35c bits 21-22 and 24-25), and for age.
    if (g.level > 3 && x != 0xFF && (w3 & 3) != 0 && g.manager_of(team) >= 0) {
        static const int kDepthBias[4] = {0, 4, 10, 16};  // RatingRuntimeField
        if (x < 31) {
            // tackling near the own goal, teamplay + passing in midfield, speed on the wings
            if (skill[4] != -1 && x != 0 && x < 4 && y > 2 && y < 6)
                multiplier = (technique < condition ? multiplier - 5 : multiplier - 10) + skill[4];
            if (skill[0] != -1 && skill[3] != -1 && x > 9 && x < 13 && y > 2 && y < 6)
                multiplier = (condition < technique ? multiplier - 10 : multiplier - 20) + skill[0] + skill[3];
            if (skill[1] != -1 && x > 11 && (y < 2 || y > 6)) multiplier = multiplier - 7 + skill[1];
        } else {
            x = ((x - 30) * 10) / 14;  // between the grid lines
        }
        if (y > 30) {
            y = 4 - ((12 - (y - 30)) * 10) / 25;
            y = y < 0 ? 0 : y > 8 ? 8 : y;
        }
        int depth = std::abs(int(w1 >> 21 & 3) + kDepthBias[w3 & 3] - 3 - x);
        int lane = std::abs(int((w1 >> 24 & 3) << 3) / 7 - y);
        multiplier -= (depth >> 1) + (lane >> 1);
        int age = int(w3 >> 21 & 0x1F);  // age - 16
        if (age > 14) multiplier -= age - 14;
        if (age < 6) multiplier -= 6 - age;
    }

    if (w1 >> 27) weight += weight / 20;  // players with bits 27-31 set count 5 % less
    if (weight == 0) return 0;
    return scale * (sum * multiplier / 100) / weight;
}

namespace {

// The corrections of TeamRating for a player of a human manager (layout field-layout field),
// applied to his player_value `v`:
//   - from game level 5 on (not for the goalkeeper group): minus the distance between the
//     slot's own place (bytes 0/1) and the place he is put on, times the group weight / 100,
//     plus 3
//   - +5 if G(20, 100) < expected bonus * 100 / base pay: a good bonus motivates
//   - levels 8 and 9: minus level - 6
//   - slot byte 0x0f above 70: minus (value - 50) / 4
//   - with the live scene, half of slot byte 5 (10 = neutral): minus W(0, 10 - value)
//     below 10, minus W(0, value) above 10
int human_correction(GameState& g, RatingContext& ctx, int manager, int rank, int board_slot, int player, int group,
                     int weight, int x, int y, int board_depth, int board_live, int v) {
    const size_t board = size_t(manager) * 0x840 + size_t(board_slot) * 0x58;
    if (group != 0 && g.level > 4) {
        v = int16_t(v - std::abs(g.tactics.at(board) - x) * weight / 100);
        v = v - std::abs(g.tactics.at(board + 1) - y) * weight / 100 + 3;
    }
    if (!ctx.game || !ctx.rand) throw std::invalid_argument("team_rating: a human team needs both generators");
    const uint32_t base = uint32_t(g.tactics.at(board + 0x50) | g.tactics.at(board + 0x51) << 8 |
                                   g.tactics.at(board + 0x52) << 16) | uint32_t(g.tactics.at(board + 0x53)) << 24;
    if (base == 0) throw std::invalid_argument("team_rating: board slot without base pay (the original divides by zero)");
    const uint32_t motivation = expected_bonus(g, manager, rank, board_slot, player) * 100u / base;
    if (uint32_t(ctx.game->range(0x14, 100)) < motivation) v += 5;
    if (g.level > 7) v -= g.level - 6;
    if (board_depth > 0x46) v = int16_t(v) - (board_depth - 0x32) / 4;
    if (board_live < 10) v -= ctx.rand->range(0, 10 - board_live);
    if (board_live > 10) v -= ctx.rand->range(0, board_live);
    return v;
}

}  // namespace

int team_rating(GameState& g, int team, const RatingProfile& profile, RatingContext& ctx, int* keeper) {
    if (team >= runtime_dimensions.record_teams && team != kNoTeam) {
        // Clubs without squad: per group a random value around the level of the club
        // (ForeignClubStrength bits 5-7 -> RatingRuntimeField2), +2 if the group's strength bit is set
        // (bit 3, 3, 2, 1 for goal, defence, midfield, attack: RatingRuntimeField3) and a style
        // offset (RatingRuntimeField4: bit 0 clear = +6 defence -6 attack, set = the opposite).
        static const uint8_t kLevel[8] = {55, 61, 65, 67, 70, 73, 76, 80};
        static const uint8_t kStrongBit[4] = {3, 3, 2, 1};
        static const int8_t kStyle[8] = {0, 6, 0, -6, 0, -6, 0, 6};
        if (!ctx.rand) throw std::invalid_argument("team_rating: a team without squad needs the Watcom generator");
        const uint8_t s = g.foreign_strength.at(size_t(team - runtime_dimensions.record_teams));
        int16_t total = 0;
        for (int i = 0; i < 4; i++) {
            const int level = kLevel[s >> 5];
            const int strong = ((s >> kStrongBit[i]) & 1) * 2;
            const int v = strong + ctx.rand->range(level, level + 3) - 1 + kStyle[(s & 1) * 4 + i];
            total = int16_t(total + profile.weight[i] * int16_t(v) / 100);
        }
        return total;
    }
    // Human managers: the predicted final rank (Void5 in mode 1 before the tenth
    // round of the manager's league, mode 2 after it) for the expected bonus. Mode 1 may
    // post the season goal messages.
    const int manager = team == kNoTeam ? -1 : g.manager_of(team);  // local_5a
    int rank = 0;                                                     // local_5e
    if (manager >= 0) {
        const size_t rec = size_t(manager) * 0xB0C;
        const int league = g.manager_records.at(rec + 0x210);
        const int mode = (league < int(g.league_rounds.size()) && g.league_rounds[size_t(league)] > 9) + 1;
        std::array<uint8_t, 24> buf;
        int size_word = -1;
        const int r = table_prediction(g, g.manager_teams.at(size_t(manager)), mode, buf, &size_word);
        rank = r == 0 ? 0xB : r + 1;
        // Void5 (3 arguments) is called from the same frame as the player values
        // (PlayerValue, 6 arguments): its league size word (local_2c, [ESP+0xfc]) lands in
        // the slot of their technique (local_20). Profiles without the technique attribute
        // (4, 5) compare the condition against it (see RatingContext::stale_technique); it
        // matters only when the chance weather's rain byte exceeds 2.
        if (size_word >= 0 && !(profile.attrs & attr::TECHNIQUE)) ctx.stale_technique = size_word;
    }
    int board_depth = 0x32;  // local_76: slot byte 0x0f of the board
    int board_live = 10;     // local_62: half of slot byte 5, only with the live scene

    static const int8_t kGroupDepth[4] = {1, 8, 13, 22};  // RatingRuntimeField5: group if x < value
    static const int8_t kNominal[4] = {1, 4, 4, 2};        // RatingRuntimeField6: nominal 1-4-4-2

    int16_t bucket[12] = {};    // local_84: 4 groups x 3 lanes
    uint8_t count[12] = {};     // local_6c
    uint8_t in_group[4] = {};   // RatingRuntimeField7
    int8_t missing = 11;        // local_18: 11 - line-up size
    if (keeper) *keeper = 0xFFF;

    SquadRange range = squad_range(team);
    for (size_t s = range.first; s < range.first + range.count; s++) {
        uint16_t slot = g.squad_slots.at(s);
        int player = slot & 0x0FFF;
        if (player == 0xFFF) break;  // the original stops at the first empty slot
        if (!(slot & kSlotInLineup)) continue;
        g.players.at(player).w364 |= 0x1000;  // mark as having played
        missing--;

        int x = 0, y = 0;
        const int board_slot = int(s - range.first);
        g.formation_position(team, board_slot, x, y);
        const size_t board = size_t(manager) * 0x840 + size_t(board_slot) * 0x58;
        if (manager >= 0) {
            board_depth = g.tactics.at(board + 0xf);
            if (ctx.live_scene) board_live = g.tactics.at(board + 5) / 2;
        }
        for (int group = 0; group < 4; group++) {
            if (x >= kGroupDepth[group]) continue;
            in_group[group]++;
            int lane = (group == 1 || group == 2) ? y / 3 : 0;
            if (profile.weight[group] != 0) {
                int v = player_value(g, player, profile.attrs, 100, team, ctx, x, y);
                // the next call finds this player's condition/technique on the stack
                if (profile.attrs & attr::CONDITION) ctx.stale_condition = g.players[player].condition();
                if (profile.attrs & attr::TECHNIQUE) ctx.stale_technique = g.players[player].technique();
                if (manager >= 0) v = human_correction(g, ctx, manager, rank, board_slot, player, group, profile.weight[group], x, y, board_depth, board_live, v);
                int16_t value = int16_t(v);
                if (value < 0) value = 0;
                bucket[group * 3 + lane] = int16_t(bucket[group * 3 + lane] + value);
                count[group * 3 + lane]++;
                if (group == 0) {
                    if (y == 4) {
                        if (keeper) *keeper = player;
                    } else {
                        bucket[lane] = 0;  // a goalkeeper off the centre does not count
                    }
                    if (g.players[player].position() != 0) bucket[lane] = int16_t(bucket[lane] / 6);  // not a keeper
                }
            }
            break;
        }
    }
    if (missing < 0) missing = 0;  // the original reports an internal error (0x6f) here

    if (profile.weight[0] != 0 && bucket[0] == 0)
        for (int i = 0; i < 9; i++) bucket[i] = 0;  // no goalkeeper: goal, defence and 2 mid buckets are void

    int total = 0;  // local_34, used as a short
    for (int group = 0; group < 4; group++) {
        int8_t diff = int8_t(in_group[group] - kNominal[group]);
        for (int lane = 0; lane < 3; lane++) {
            int i = group * 3 + lane;
            if (profile.weight[group] == 0 || count[i] == 0) continue;
            if (diff != 0) bucket[i] = int16_t(bucket[i] * (diff * 10 + 100) / 100);
            bucket[i] = int16_t(profile.weight[group] * bucket[i] / 100);
            if (group == 1 || group == 2) count[i] = uint8_t(count[i] * 3);
            total = int16_t(total) + bucket[i] / count[i];
        }
    }
    if (missing != 0) total = int16_t(total) * 100 / (missing * 10 + 100);
    if (int16_t(total) > 110) total = 110;
    return total;
}

}  // namespace bmh
