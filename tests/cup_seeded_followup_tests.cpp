// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "game/cup.hpp"

using namespace bmh;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

void entry(GameState& g, int index, int type, uint32_t teams, uint32_t results,
           uint8_t flags, uint8_t team_count, uint8_t rounds, uint8_t played,
           uint8_t groups, uint8_t quota, uint8_t seeds, int next, int parallel) {
    auto& m = g.memory;
    const uint32_t e = cup_entry_address(index);
    m.set_pointer(e, teams);
    m.set_pointer(e + 8, flags & 4 ? 0 : ::bmh::layout::CareerSession_RuntimeError_RuntimeField);  // four-club group schedule
    m.set_pointer(e + 0x0c, results);
    m.set_pointer(e + 0x10, results);
    m.set8(e + 0x14, flags);
    m.set8(e + 0x16, uint8_t(next));
    m.set8(e + 0x17, uint8_t(parallel));
    m.set16(e + 0x18, uint16_t(type));
    m.set8(e + 0x1a, team_count);
    m.set8(e + 0x1b, rounds);
    m.set8(e + 0x1d, played);
    m.set8(e + 0x1e, uint8_t(groups));
    m.set8(e + 0x1f, quota);
    m.set8(e + 0x20, seeds);
}

std::vector<uint16_t> read_teams(const GameState& g, uint32_t address, int count) {
    std::vector<uint16_t> out;
    for (int i = 0; i < count; ++i) out.push_back(g.memory.u16(address + uint32_t(i) * 2));
    std::sort(out.begin(), out.end());
    return out;
}

GameState blank() {
    GameState g;
    g.memory.add_block(kCupTable, kCupEntries * kCupEntrySize);
    g.memory.add_block(0x1100, 0x100);
    g.memory.add_block(0x10fc, 4);
    g.memory.add_block(0x1200, 0x100);
    g.memory.add_block(0x1300, 0x100);
    g.memory.add_block(0x1400, 0x100);
    return g;
}

void group_to_seeded_linked_groups(int source_seed_count) {
    GameState g = blank();
    auto& m = g.memory;
    // Two completed source groups; the source's +0x20 direct entrant is reserved just
    // before the first group list, as legacy routine reads it.
    entry(g, 0, 5, 0x1100, 0x1200, 0, 2, 1, 0, 2, 4, uint8_t(source_seed_count), 2, 1);
    entry(g, 1, 5, 0x1120, 0x1220, 0, 2, 1, 0, 0x12, 0, 0, 2, 0);
    m.set16(0x10fe, 99); m.set16(0x111e, 98); m.set16(0x10fc, 97);
    m.set16(0x1100, 10); m.set16(0x1102, 11);
    m.set16(0x1120, 12); m.set16(0x1122, 13);
    m.set16(0x1200, 0xcccc); m.set16(0x1220, 0xcccc);
    // Two linked destination groups. Four qualifiers/seeds fill the target field.
    entry(g, 2, 5, 0x1300, 0x1400, 0, 2, 1, 0, 2, 2, 0, 0, 3);
    entry(g, 3, 5, 0x1320, 0x1420, 0, 2, 1, 0, 0x12, 0, 1, 0, 0);
    MinuteReport report;
    WatcomRandom w(1);
    advance_cup(g, 5, w, report);
    check(!report.unsupported, "seeded linked-group follow-up completes");
    if (source_seed_count == 1) {
        const std::vector<uint16_t> group_fields = {
            m.u16(0x1300), m.u16(0x1302), m.u16(0x1320), m.u16(0x1322), m.u16(0x131e)
        };
        check(group_fields == std::vector<uint16_t>({12, 11, 13, 99, 10}),
              "single overflow matches legacy routine reverse writes and final-group swap");
    } else {
        const std::vector<uint16_t> group_fields = {
            m.u16(0x1300), m.u16(0x1302), m.u16(0x1320), m.u16(0x1322),
            m.u16(0x131e), m.u16(0x131c)
        };
        check(group_fields == std::vector<uint16_t>({13, 99, 98, 97, 12, 10}),
              "multiple overflow entrants rewrite each reserved slot per linked group in Source order");
    }
    check(m.u8(cup_entry_address(0) + 0x1d) == 1 &&
          m.u8(cup_entry_address(1) + 0x1d) == 1 &&
          m.u8(cup_entry_address(2) + 0x1d) == 0 &&
          m.u8(cup_entry_address(3) + 0x1d) == 0,
          "group and linked-stage cursors advance at the Source boundary");
}

void knockout_to_seeded_knockout() {
    GameState g = blank();
    auto& m = g.memory;
    entry(g, 0, 5, 0x1100, 0x1200, 4, 8, 1, 0, 1, 0, 1, 1, 0);
    m.set16(0x10fe, 99);
    for (int i = 0; i < 8; ++i) m.set16(0x1100 + uint32_t(i) * 2, uint16_t(10 + i));
    for (int i = 0; i < 4; ++i) m.set16(0x1200 + uint32_t(i) * 2, uint16_t((2 << 5) | 0));
    entry(g, 1, 5, 0x1300, 0x1400, 4, 8, 2, 2, 1, 0, 1, 0, 0);
    m.set16(0x1300 + 12 * 2, 0xcccc);
    m.set16(0x1300 + 13 * 2, 0xcccc);
    MinuteReport report;
    WatcomRandom w(1);
    advance_cup(g, 5, w, report);
    check(!report.unsupported, "seeded knockout follow-up completes");
    check(m.u16(0x12fe) == 11,
          "knockout reserved slot gets the first Source winner");
    check(m.u16(0x131c) == 13 && m.u16(0x131e) == 17,
          "knockout round pairing starts after the seeded pair index");
    check(m.u16(0x1318) == 0xcccc && m.u16(0x131a) == 0xcccc,
          "knockout seeded pair slot remains untouched by the qualifier pair loop");
    check(m.u8(cup_entry_address(0) + 0x1d) == 1,
          "knockout source cursor reaches its completed-round boundary");
}
}

int main() {
    try {
        group_to_seeded_linked_groups(1);
        group_to_seeded_linked_groups(3);
        knockout_to_seeded_knockout();
        std::cout << "Seeded group/knockout follow-up and linked-group cursors OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
