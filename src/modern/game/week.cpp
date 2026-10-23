// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "modern/game/modern_state.hpp"
#include "modern/game/week.hpp"

#include "modern/game/matchday.hpp"
#include "game/lineup.hpp"
#include "game/schedule.hpp"

#include <algorithm>

namespace bmh::modern {

namespace {

constexpr size_t kManagerRecord = 0xB0C;

int32_t rd32(const std::vector<uint8_t>& v, size_t o) {
    return int32_t(uint32_t(v.at(o)) | uint32_t(v.at(o + 1)) << 8 | uint32_t(v.at(o + 2)) << 16 | uint32_t(v.at(o + 3)) << 24);
}
void wr32(std::vector<uint8_t>& v, size_t o, int32_t x) {
    for (int i = 0; i < 4; i++) v.at(o + i) = uint8_t(uint32_t(x) >> (8 * i));
}

}  // namespace

void daily_interest(ModernState& g, WatcomRandom& rand) {
    // (DailyInterest before the day step: the training camp visits of today are over)
    for (size_t m = 0; m < g.manager_teams.size(); m++) g.manager_records.at(m * kManagerRecord + 0x21f) &= 0x7f;
    for (int k = 0; k < 4; k++) {
        const size_t rec = size_t(k) * kManagerRecord;
        const int32_t balance = rd32(g.manager_records, rec + 0x314);
        const int32_t deposit = rd32(g.manager_records, rec + 0x318);
        const int32_t credit = rd32(g.manager_records, rec + 0x31c);
        int32_t amount = 0;  // positive = cost
        if (balance < 0) {
            if (balance + credit < 0) amount = ((balance + credit) * 18) / -36500;
            int32_t used = -credit < balance ? balance : -credit;
            amount += (used * 12) / -36500;
            wr32(g.manager_records, rec + 0x89c, rd32(g.manager_records, rec + 0x89c) + amount);
            modern::book_money(g, k, amount, true);
            amount = 0;
        } else {
            amount = balance / -36500;
        }
        amount += ((deposit / 10000) * 20000) / -36500;
        wr32(g.manager_records, rec + 0x870, rd32(g.manager_records, rec + 0x870) - amount);
        modern::book_money(g, k, amount, true);
        if (rd32(g.manager_records, rec + 0x314) > 100000000) {  // read again after the bookings
            int32_t extra = int32_t(rand.range(10000, 20000)) * 1000;
            // BookMoney2 with the statistics flag set: counts as an expense (+0x8a0)
            wr32(g.manager_records, rec + 0x8a0, rd32(g.manager_records, rec + 0x8a0) + extra);
            modern::book_money(g, k, extra, true);
        }
    }
}

}  // namespace bmh::modern

namespace bmh::modern {

namespace {

uint16_t rd16w(const std::vector<uint8_t>& v, size_t o) { return uint16_t(v.at(o) | v.at(o + 1) << 8); }
void wr16w(std::vector<uint8_t>& v, size_t o, uint16_t x) {
    v.at(o) = uint8_t(x);
    v.at(o + 1) = uint8_t(x >> 8);
}
int8_t clamp8(int v, int lo, int hi) {  // Int8T on a signed char
    int8_t c = int8_t(v);
    int s = c < lo ? lo : c;
    if (s > hi) s = hi;
    return int8_t(s);
}

// Training areas (WeekRuntimeField shift, WeekRuntimeField2 mask): 0 condition, 1 technique (w35c),
// 2-8 the seven skills (w368, 2 bits each).
constexpr uint8_t kAreaShift[9] = {14, 7, 12, 10, 8, 6, 4, 2, 0};
constexpr uint8_t kAreaMask[9] = {127, 127, 3, 3, 3, 3, 3, 3, 3};
constexpr uint8_t kIntensityValue[4] = {0, 80, 100, 120};  // WeekRuntimeField3
constexpr int8_t kFamiliarityStep[4] = {0, -4, 0, 4};      // WeekRuntimeField4

// Attribute: read (write = false) or change an attribute by `delta` within 0..max.
int attribute(Player& p, int area, bool write, int delta, int max) {
    uint32_t& word = area < 2 ? p.w35c : p.w368;
    const int shift = kAreaShift[area], mask = kAreaMask[area];
    int v = int(int32_t(word) >> shift) & mask;
    if (!write) return v;
    int8_t n = clamp8(int8_t(delta + v), 0, max);
    word = (word & ~(uint32_t(mask) << shift)) | uint32_t(int32_t(n)) << shift;
    return 0;
}

}  // namespace

int weekday_code(uint32_t date) {
    int c = int(date % 7) + 2;
    if (c > 6) c = int(date % 7) - 5;
    return c;
}

void familiarity_decay(ModernState& g, GameRandom& game, WatcomRandom& rand) {
    for (size_t k = 0; k < g.manager_teams.size(); k++) {
        const int team = g.manager_teams[k];
        const SquadRange range = squad_range(team);
        for (size_t s = range.first; s < range.first + range.count; s++) {
            const int player = g.squad_slots.at(s) & 0x0FFF;
            if (player == 0xFFF) continue;
            const Player& p = g.players.at(player);
            uint8_t& used = g.tactics.at(k * 0x840 + (s - range.first) * 0x58 + 0xf);
            int r = rand.range(0, 2);
            uint8_t rate = uint8_t(r + 4);
            if (p.condition() < p.technique()) rate = uint8_t(r + 6);
            if (unsigned(rand.range(12, 16)) < ((p.w364 >> 21) & 0x1F)) rate = uint8_t(rate + 2);
            int16_t cap = int16_t(8 - int16_t(rd16w(g.manager_records, k * 0xB0C + 0x8b0)) / 0x113);
            if (team == no_team()) cap = 8;
            if (cap < int16_t(rate)) rate = uint8_t(cap);
            // the player pool of a manager without a club: a low value is drawn afresh
            // (game generator)
            if (team == no_team() && used < 0x1e) used = uint8_t(game.range(0x18, 0x30));
            used = uint8_t(used - used / rate);
        }
    }
}

void train_squad(ModernState& g, int manager, GameRandom& game, WatcomRandom& rand) {
    const size_t rec = size_t(manager) * 0xB0C;
    uint8_t table[4][9];
    for (int pos = 0; pos < 4; pos++)
        for (int a = 0; a < 9; a++)
            table[pos][a] = uint8_t(kIntensityValue[int8_t(g.manager_records.at(rec + 0xc5 + pos))] +
                                    kIntensityValue[int8_t(g.manager_records.at(rec + 0xc9 + a))]);
    const int team = int16_t(rd16w(g.manager_records, rec + 0x290));
    const SquadRange range = squad_range(team);
    const int league = team_league(team);
    const int16_t quality = int16_t(rd16w(g.manager_records, rec + 0x8ac));
    uint8_t max_value[9] = {99, 99, 3, 3, 3, 3, 3, 3, 3};  // WeekRuntimeField5
    for (size_t s = range.first; s < range.first + range.count; s++) {
        const uint16_t entry = g.squad_slots.at(s);
        if (entry == 0x0FFF) continue;
        const int player = entry & 0x0FFF;
        Player& p = g.players.at(player);
        const size_t board = size_t(manager) * 0x840 + (s - range.first) * 0x58;
        const uint32_t w364 = p.w364;
        const int games = int32_t(w364) >> 26;
        const int pos = int(w364 & 3);
        int8_t used_delta = 0;  // local_48
        int8_t trend = 0;       // local_54
        const uint8_t used = g.tactics.at(board + 0xf);
        const int intensity = g.tactics.at(board + 0xa) & 3;
        int second = 10;                                    // local_4c
        int focus = (g.tactics.at(board + 0xa) >> 2) + 1;  // local_58
        if (attribute(p, focus < 9 ? focus : 8, false, 0, 0) < 3) {
            do {
                second = game.range(2, 8);
            } while (second == focus);
        } else {
            focus = 10;
        }
        for (int a = 0; a < 9; a++) {
            // register EDI of the original: a value from an earlier branch may survive
            uint32_t edi = uint32_t(pos * 9 + a);
            int ebx = table[pos][a] + quality / 110 - 5;
            if (rand.range(0, 4) == 0) used_delta = int8_t(used_delta + int8_t(g.manager_records.at(rec + 0xc5 + a)) * 2 - 4);
            ebx = (int(uint16_t(ebx)) + kIntensityValue[intensity]) / 3;
            uint8_t rest = uint8_t(0xa0 - used);
            if (rest > 100) rest = 100;
            ebx = (int(uint16_t(ebx)) + rest) / 2 - 0x50;
            if (focus == a) ebx += 8;
            if (second == a) ebx -= 8;
            int sign;
            if (uint16_t(ebx) < 20) {
                sign = -1;
                ebx = 20 - ebx;
            } else {
                sign = 1;
                ebx -= 20;
            }
            // OpenBMH deviates from the original: uses the enhanced training rules: focus strength is at least five below its target, and completed skills rotate to another focus.
// FocusAreaGetsAtLeastStrengthBelow (supported rule profile): the focus area gets at least strength 5 below
            uint32_t reg = uint16_t(ebx);
            if (focus == a) reg |= 0x10000;
            if (sign == 1) {
                edi = 3;
                if (g.league_rounds.at(league) / 3 > games) {
                    reg = 0;  // no match practice yet: no progress
                } else {
                    edi = (w364 >> 21) & 0x1F;                   // age - 16
                    if (rand.range(13, 40) < int(edi)) reg = 0;  // older players progress less
                }
            }
            if (a > 1) {
                edi = uint32_t(a);
                if (table[pos][a] < 0x6e && game.range(0, 50) == 0) max_value[a] = 2;
            }
            if (game.range(0, 8) == 0) {
                // OpenBMH deviates from the original: uses the enhanced training rules: focus strength is at least five below its target, and completed skills rotate to another focus.
// Int9 (supported rule profile)
                uint32_t dx = reg & 0xFFFF;
                if ((reg & 0x10000) && dx < 5) dx = 5;
                edi = dx * 15;
                reg &= 0xFFFF;
                if (int(edi) > game.range(0, 0x96)) attribute(p, a, true, sign, int8_t(max_value[a]));
            }
            (void)edi;
            if (a > 1) max_value[a] = 3;
            if (reg & 0xFFFF) trend = int8_t(trend + sign);
        }
        used_delta = clamp8(used_delta, -15, 15);
        g.tactics.at(board + 0xf) = uint8_t(g.tactics.at(board + 0xf) + kFamiliarityStep[intensity] + used_delta);
        trend = int8_t(trend / int8_t(game.range(1, 4)));
        trend = clamp8(trend, -1, 1);
        g.tactics.at(board + 0xe) = uint8_t((g.tactics.at(board + 0xe) & 0x3F) + uint8_t((trend + 1) << 6));
        g.tactics.at(board + 0xf) = uint8_t(clamp8(int8_t(g.tactics.at(board + 0xf)), 0x18, 99));
    }
    // the assistant's training plan (training automatic on)
    if (g.manager_options.at(size_t(manager) * 5 + 4) & 1) modern::auto_training(g, manager);
}

void auto_training(ModernState& g, int manager) {
    uint8_t& option = g.manager_options.at(size_t(manager) * 5 + 4);  // CareerSessionRuntimeField3
    if (!(option & 0x80)) option = 0x9f;
    const uint8_t flags = (option & 1) ? option : 0;  // local_14
    const size_t rec = size_t(manager) * 0xB0C;
    const int team = int16_t(rd16w(g.manager_records, rec + 0x290));
    uint8_t plan[13];
    for (int i = 0; i < 13; i++) plan[i] = g.manager_records.at(rec + 0xc5 + size_t(i));
    // QuoteNationalFriendlyMatch for keepers, defence, midfield, attack and the whole team (WeekRuntimeField6)
    static constexpr uint16_t kPositions[5] = {0x1f, 0x2f, 0x4f, 0x8f, 0xff};
    int value[5];
    for (int i = 0; i < 5; i++) value[i] = team_average(g, team, kPositions[i]).value;
    int16_t order[7];
    uint32_t key[7];
    if (flags & 2) {
        for (int i = 0; i < 4; i++) {
            plan[i] = 2;
            order[i] = int16_t(i);
            key[i] = uint32_t((value[i] * 100) / value[4]);
        }
        quicksort_by_key(order, key, 4, true);  // Games ascending
        for (int i = 0; i < 2; i++)
            if (key[i] < 0x62) plan[order[i]] = 3, plan[order[3 - i]] = 1;
    }
    const int condition = team_average(g, team, 0xf1).value, technique = team_average(g, team, 0xf2).value;
    if (flags & 4) {
        plan[4] = 2, plan[5] = 2;
        if (condition - 1 > technique) plan[4] = 1, plan[5] = 3;
        if (technique - 1 > condition) plan[4] = 3, plan[5] = 1;
    }
    for (int i = 0; i < 7; i++) {
        if (flags & 4) plan[6 + i] = 2;
        order[i] = int16_t(i);
        key[i] = 0x32;
    }
    const SquadRange range = squad_range(team);
    for (size_t s = range.first; s < range.first + range.count; s++) {
        const uint16_t v = g.squad_slots.at(s);
        if (v == kEmptySlot) continue;
        const Player& p = g.players.at(v & 0x0FFF);
        uint8_t* board = &g.tactics.at(size_t(manager) * 0x840 + (s - range.first) * 0x58);
        if (flags & 8) {
            uint8_t intensity = 2;
            if (board[0xf] < 0x1f) intensity = 3;
            if (board[0xf] > 0x3c) intensity = 1;
            board[10] = uint8_t((board[10] & 0xfc) | intensity);
        }
        // the skills 6..0 (w368 bits 12..0, Attribute with WeekRuntimeField/WeekRuntimeField2, index 2..8):
        // + 1 for a player at 0 (WeekRuntimeField7), - 1 at 3 (WeekRuntimeField8)
        bool at_top[8] = {};  // OpenBMH deviates from the original: uses the enhanced training rules: focus strength is at least five below its target, and completed skills rotate to another focus.
// by index k = 7 - skill (the supported rule profile patch's bit pairs)
        for (int i = 0; i < 7; i++) {
            const int skill = 6 - i;
            const int level = int(p.w368 >> (2 * skill) & 3);
            at_top[i + 1] = level == 3;
            if (level == 0) key[i]++;
            if (level == 3) key[i]--;
        }
        if (flags & 0x10) {
            // Position: the skill to train (+0x0a bits 2-4) is kept unless it is at 3;
            // else the first of the position's order (nibbles of Week_AutoTraining_ExecutableField3, Week_AutoTraining_ExecutableField4, Week_AutoTraining_ExecutableField2,
            // Week_AutoTraining_ExecutableField, low first, each 2 * k) that is not at 3 yet
            static constexpr uint32_t kOrder[4] = {::bmh::layout::Week_AutoTraining_ExecutableField3, ::bmh::layout::Week_AutoTraining_ExecutableField4, ::bmh::layout::Week_AutoTraining_ExecutableField2, ::bmh::layout::Week_AutoTraining_ExecutableField};
            const int current = (board[10] >> 2) & 7;
            if (current == 0 || at_top[current]) {
                for (uint32_t list = kOrder[p.position()]; (list & 0xf) != 0; list >>= 4) {
                    const int k = int(list & 0xf) / 2;
                    if (!at_top[k]) {
                        board[10] = uint8_t((board[10] & 3) | (k << 2));
                        break;
                    }
                }
            }
        }
    }
    quicksort_by_key(order, key, 7);  // Games descending
    if (flags & 4)
        for (int i = 0; i < 3; i++)
            if (key[i] > 0x34) plan[6 + order[i]] = 3, plan[6 + order[6 - i]] = 1;
    for (int i = 0; i < 13; i++) g.manager_records.at(rec + 0xc5 + size_t(i)) = plan[i];
}

void stock_market(ModernState& g, GameRandom& game) {
    auto u32 = [&](size_t o) {
        return uint32_t(g.stocks.at(o) | g.stocks.at(o + 1) << 8 | g.stocks.at(o + 2) << 16 | uint32_t(g.stocks.at(o + 3)) << 24);
    };
    auto w32 = [&](size_t o, uint32_t x) {
        for (int i = 0; i < 4; i++) g.stocks.at(o + i) = uint8_t(x >> (8 * i));
    };
    for (int k = 0; k < 12; k++) w32(size_t(k) * 0x5c, u32(size_t(k) * 0x5c) + u32(size_t(k) * 0x5c) * 3 / 10000);
    for (int k = 0; k < 12; k++) {
        const size_t s = size_t(k) * 0x5c;
        const uint32_t base = u32(s) / 300;
        int v = rd16w(g.stocks, s + 0x4e) / 3 + (g.stocks.at(s + 0x58) - 1);
        if (v < 10) v = 10;
        uint8_t& trend = g.stocks.at(s + 0x58);
        uint8_t& left = g.stocks.at(s + 0x59);
        if (left != 0) left--;
        if (left == 0) {
            trend = uint8_t(game.range(0, 1) * 2);
            left = uint8_t(game.range(2, (trend != 0) + 8));
        }
        if (game.range(0, 400 - k * 25) == 0) {
            int dir = game.range(0, 1);
            v += game.range(v / 5, v / 2) * (dir * 2 - 1);
            if (game.range(0, 3) != 0) {
                int d2 = game.range(0, 1);
                w32(s, uint32_t(v * 300 + (d2 * 2 - 1) * 3000));
            }
        }
        if (game.range(0, 0x16 - k) == 0) {
            int dir = game.range(0, 1);
            v += game.range(2, 10) * (dir * 2 - 1);
            if (game.range(0, 3) == 0) {
                int d2 = game.range(0, 1);
                int m = game.range(2, 10);
                w32(s, uint32_t(int32_t(u32(s)) + (d2 * 2 - 1) * m * 300));
            }
        }
        if (game.range(0, 0x5a) == 0 && (v + 0x14 < int(base) || int(base) < v - 0x14)) {
            int dir = game.range(0, 1);
            int m = game.range(2, 10);
            v = (dir * 2 - 1) * m + int(base);
        }
        if (game.range(0, 1) != 0) v += game.range(0, 2) - 1;
        if (int(base) < v) {
            if (trend == 2) v--;
        } else if (game.range(0, 1) != 0) {
            if (trend == 2) v--;
        }
        if (v < int(base)) {
            if (trend == 0) v++;
        } else if (game.range(0, 1) != 0) {
            if (trend == 0) v++;
        }
        std::copy(g.stocks.begin() + s + 0x16, g.stocks.begin() + s + 0x16 + 0x3a, g.stocks.begin() + s + 0x14);
        if (v < 10) {
            v = 10;
            w32(s, 3000);
        }
        if (v > 2000) {
            v = 1000;
            w32(s, 300000);
            for (int i = 0; i < 4; i++) wr16w(g.stocks, s + 0x50 + i * 2, uint16_t(rd16w(g.stocks, s + 0x50 + i * 2) << 1));
        }
        wr16w(g.stocks, s + 0x4e, uint16_t(v * 3));
    }
}

}  // namespace bmh::modern

namespace bmh::modern {

int month_code(uint32_t date) {
    // DeskState_DeskDate_RuntimeField: days per month (index 1-12; February 29 in leap years)
    uint8_t days[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int leap = 3;  // sVar4: 4 = the current year is a leap year
    while (date > uint32_t((leap == 4 ? 1 : 0) + 0x16c)) {
        if (leap == 4) {
            leap = 0;
            date--;
        }
        date -= 0x16d;
        leap++;
    }
    days[2] = uint8_t((leap == 4 ? 1 : 0) + 0x1c);
    int month = 7;
    while (date > days[month]) {
        date -= days[month];
        month = month == 12 ? 1 : month + 1;
    }
    if (date + 1 > days[month]) month = month == 12 ? 1 : month + 1;
    return month;
}

}  // namespace bmh::modern
