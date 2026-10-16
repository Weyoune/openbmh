// SPDX-License-Identifier: GPL-3.0-only
#include <cstdio>
#include <stdexcept>

#include "game/lineup.hpp"

namespace {
void check(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(what);
}

bmh::GameState fixture(uint8_t style, uint8_t option, bool indoor) {
    bmh::GameState g;
    g.manager_teams = {0};
    g.team_flags = {0};
    g.manager_records.assign(0xb0c, 0);
    g.manager_options = {option, 0, 0, 0, 0};
    g.manager_records[0x288] = style;
    g.squad_slots.resize(24);
    g.players.resize(24);
    g.player_cards.resize(24);
    g.tactics.resize(0x840);
    g.formations.resize(0x160);
    g.club_heap_78.resize(2);
    g.indoor = indoor;
    g.level = 6;
    // First scoring slot: x=7, y=0, hence a defender role. Their w35c depth
    // preferences oppose their deliberately different w364 age bits.
    g.formations[0] = 7;
    g.formations[11] = 0;
    g.formations[0xb0] = 7;
    g.formations[0xb0 + 11] = 0;
    for (size_t i = 0; i < 24; ++i) {
        g.squad_slots[i] = uint16_t(i);
        const int form = 60;
        const int technique = i == 0 ? 90 : (i == 1 ? 85 : 40);
        const int condition = i == 0 ? 90 : (i == 1 ? 85 : 40);
        const int preferred_lane = 0;
        const int preferred_depth = i == 1 ? 7 : 0;
        const int age_low = i == 0 ? 7 : 0;
        g.players[i].w35c = uint32_t(form) | (uint32_t(technique) << 7) |
            (uint32_t(condition) << 14) | (uint32_t(preferred_depth) << 21) |
            (uint32_t(preferred_lane) << 24);
        g.players[i].w364 = 1u | (uint32_t(age_low) << 21); // defender
        auto* board = g.tactics.data() + i * 0x58;
        board[0x0f] = 25;
    }
    return g;
}

int first_player(const bmh::GameState& g) { return g.squad_slots[0] & 0x0fff; }

bmh::GameState second_pass_fixture() {
    auto g = fixture(0x0c, 1, false); // level-6 detail weight 3
    for (size_t i = 0; i < 24; ++i) {
        // A 4-4-2 with one stronger, fit defender in reserve. His preferred
        // lane/depth makes him lose the first positional scoring pass.
        const int role = i == 0 ? 0 : (i <= 4 || i == 11 ? 1 : (i <= 8 ? 2 : 3));
        const int x = role == 0 ? 0 : (role == 1 ? 7 : (role == 2 ? 12 : 17));
        const int y = int(i % 4);
        if (i < 11) {
            g.formations[i] = uint8_t(x);
            g.formations[i + 11] = uint8_t(y);
        }
        const int preferred_x = role == 1 ? 7 : (role == 2 ? 4 : 3);
        g.players[i].w35c = 60u | (80u << 7) | (80u << 14) |
            (uint32_t(preferred_x) << 21) | (uint32_t(y) << 24);
        g.players[i].w364 = uint32_t(role) | (i >= 12 ? 1u << 16 : 0);
        g.player_cards[i] = uint16_t(0xa5e0 | i);
    }
    g.players[11].w35c = 60u | (90u << 7) | (90u << 14) | (7u << 24);
    return g;
}

bmh::GameState ai_bench_fixture() {
    auto g = fixture(0, 1, false);
    g.manager_teams.clear();
    g.manager_records.clear();
    g.manager_options.clear();
    g.tactics.clear();
    g.team_flags = {0, 0};
    g.squad_slots.resize(48);
    for (size_t i = 0; i < 24; ++i) g.squad_slots[24 + i] = uint16_t(i);
    g.formations.resize(22);
    return g;
}

bmh::GameState manager3_indoor_formation7() {
    auto g = fixture(0x04, 1, true);
    g.manager_teams = {2, 2, 2, 0};
    g.manager_records.resize(4 * 0xb0c);
    g.manager_records[3 * 0xb0c + 0x288] = 0x04;
    g.manager_options.resize(20);
    g.manager_options[15] = 8; // saved Formation 7
    g.tactics.resize(4 * 0x840);
    g.formations.resize(0x580);
    // Source table index 0x60a is 0x8a in the following tactics block.
    g.tactics[0x8a] = 7;
    g.tactics[0x95] = 0;
    return g;
}
}

int main() {
    try {
        auto plain = fixture(0, 1, false);
        bmh::auto_lineup(plain, 0, 100);
        check(first_player(plain) == 0, "low detail preserves the higher base-value choice");

        auto ai_bench = ai_bench_fixture();
        bmh::auto_lineup(ai_bench, 1, 100);
        check(ai_bench.squad_slots[24] != 0xffff,
              "low-level AI full-lineup and reserve path needs only the eleven formation coordinates");

        auto detailed = fixture(0x04, 1, false);
        bmh::auto_lineup(detailed, 0, 100);
        check(first_player(detailed) == 1,
              "level-6 detailed preference scoring beats a ten-point base-value lead");

        auto manager3_indoor = manager3_indoor_formation7();
        bmh::auto_lineup(manager3_indoor, 0, 100);
        size_t fielded = 0;
        for (size_t row = 0; row < 11; ++row)
            fielded += (manager3_indoor.squad_slots[row] & 0xc000) == 0x8000;
        check(fielded == 11,
              "manager 3 indoor formation 7 scores and assigns from the contiguous tactics bytes");

        auto mode_change = fixture(0x04, 0, true);
        bmh::auto_lineup(mode_change, 0, 100);
        check((uint16_t(mode_change.club_heap_78[0] | uint16_t(mode_change.club_heap_78[1]) << 8) & 0x0200) != 0 &&
              first_player(mode_change) == 1,
              "manual-to-indoor mode transition applies the same detailed assistant scorer");

        auto camp_adjusted = fixture(0x30, 1, false); // familiarity weight 3
        camp_adjusted.tactics[0x0f] = 99; // familiarization changed the Source board byte
        bmh::auto_lineup(camp_adjusted, 0, 100);
        check(first_player(camp_adjusted) == 1,
              "assistant scoring subtracts the Source training-camp familiarity penalty");

        auto first_pass = second_pass_fixture();
        bmh::auto_lineup(first_pass, 0, 100);
        check((first_pass.squad_slots[1] & 0xfff) == 1 &&
              (first_pass.squad_slots[11] & 0xfff) == 11,
              "control: positional first pass leaves the stronger defender on the bench");
        auto second_pass = second_pass_fixture();
        bmh::auto_lineup(second_pass, 0, 0xe4);
        check((second_pass.squad_slots[1] & 0xffff) == 0x800b &&
              (second_pass.squad_slots[11] & 0xffff) == 0x4001,
              "Source bit 7 improves a field slot from the bench after positional scoring");
        check(second_pass.tactics[0x58 + 0x41] == 7 &&
              second_pass.tactics[0x58 + 0x42] == 1 &&
              second_pass.player_cards[11] == 0xa5e1 &&
              second_pass.player_cards[1] == 0xa5eb,
              "replacement takes field coordinates and both cards retain high bits with new rows");
        check(second_pass.squad_slots[0] == first_pass.squad_slots[0],
              "reserve improvement skips the goalkeeper row");
        auto tie = second_pass_fixture();
        tie.players[11].w35c = (tie.players[11].w35c & ~0x001fff80u) |
            (80u << 7) | (80u << 14);
        bmh::auto_lineup(tie, 0, 0xe4);
        check((tie.squad_slots[1] & 0xfff) == 1,
              "an equal reserve score retains the first-pass player");
        auto injured = second_pass_fixture();
        injured.players[11].w364 |= 1u << 16;
        bmh::auto_lineup(injured, 0, 0xe4);
        check((injured.squad_slots[1] & 0xfff) == 1,
              "injured reserves cannot replace a field player in the second pass");
        auto manual = second_pass_fixture();
        manual.manager_options[0] = 0;
        const auto manual_slots = manual.squad_slots;
        bmh::auto_lineup(manual, 0, 0xe5);
        check(manual.squad_slots == manual_slots,
              "bit 7 does not enable the assistant for a manual lineup");
        auto no_reserve_pass = second_pass_fixture();
        bmh::auto_lineup(no_reserve_pass, 0, 0xe5); // 101 selects only the first eleven
        check((no_reserve_pass.squad_slots[1] & 0xfff) == 1 &&
              (no_reserve_pass.squad_slots[11] & 0xfff) == 11,
              "bit 7 does not widen the mode-101 first-eleven candidate range");
        std::puts("assistant lineup: detailed position score, mode transition and camp familiarity OK");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
