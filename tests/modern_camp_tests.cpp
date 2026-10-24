// SPDX-License-Identifier: GPL-3.0-only
// Modern training camps (docs/new_engine/trainingslager.md, M4) on a synthetic squad:
// the built-in catalogue, the step cap at six stars, the catering and lodging extras.
#include <cstdio>
#include <set>
#include <string>

#include "game/random.hpp"
#include "modern/game/desk_actions.hpp"
#include "modern/game/training_camps.hpp"

using namespace bmh;
using namespace bmh::modern;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Manager 0 coaches club 0 (Classic layout: 24 slots), 20 players, one of them injured.
ModernState squad() {
    ModernState g;
    g.manager_teams = {0};
    g.manager_records.assign(5 * 0xb0c, 0);
    g.manager_records[0x290] = 0;                       // club
    for (int u = 0; u < 3; ++u) g.manager_records[0xc1 + size_t(u)] = 15;  // focus
    g.manager_records[0xc1 + 3] = 0;                    // recovery = 15 - 45 / 3
    g.team_morale.assign(419, 0x10);
    g.tactics.assign(4 * 0x840, 0x20);
    g.squad_slots.assign(3150, kEmptySlot);
    for (int i = 0; i < 20; ++i) {
        Player p;
        p.w35c = uint32_t(40 + i) | uint32_t(50 + i) << 7 | uint32_t(60 + i) << 14;  // form, technique, condition
        p.w364 = uint32_t(i % 4) | uint32_t(9) << 21;                              // raw age 9
        g.players.push_back(p);
        g.squad_slots[size_t(i)] = uint16_t(i);
    }
    g.players[3].w364 |= 3u << 16;  // injured for three weeks
    return g;
}

TrainingCamp camp(std::array<uint8_t, 4> quality) {
    TrainingCamp c;
    c.id = "resort:test";
    c.quality = quality;
    c.base_price = 100000;
    return c;
}

ModernState after_camp(std::array<uint8_t, 4> quality, uint32_t seed) {
    ModernState g = squad();
    g.training_camps = {camp(quality)};
    WatcomRandom rand(seed);
    training_camp_effect(g, 0, 0, rand);
    return g;
}
}  // namespace

int main() {
    const CampCatalogue& d = default_camp_catalogue();
    check(d.camps.size() == 18 && d.maps.size() == 3, "18 camps on three maps");
    std::set<std::string> ids, maps;
    int six_star = 0;
    for (const auto& m : d.maps) maps.insert(m.id);
    for (const auto& c : d.camps) {
        ids.insert(c.id);
        check(maps.count(c.map) == 1, "every camp is on a known map");
        check(!c.x && !c.y, "positions stay open until the interface sets them");
        for (const uint8_t q : c.quality) check(q <= 6, "grades 0-6");
        six_star += c.quality == std::array<uint8_t, 4>{6, 6, 6, 6};
    }
    check(ids.size() == 18 && ids.count("resort:0") && ids.count("resort:17"), "ids resort:0-17");
    check(six_star == 3, "three six-star camps (Middle East)");

    for (uint32_t seed = 1; seed <= 20; ++seed) {
        // Six stars with full focus would index beyond the step table without the cap.
        const ModernState top = after_camp({6, 6, 6, 6}, seed);
        for (int i = 0; i < 20; ++i)
            check(top.players[size_t(i)].technique() <= 99 && top.players[size_t(i)].condition() <= 99,
                  "values stay within 0-99");
        // Catering 6 adds exactly one point of condition to the same draws.
        const ModernState five = after_camp({5, 5, 5, 5}, seed);
        const ModernState catering = after_camp({5, 5, 6, 5}, seed);
        for (int i = 0; i < 20; ++i) {
            const int a = five.players[size_t(i)].condition(), b = catering.players[size_t(i)].condition();
            check(b == std::min(a + 1, 99), "catering 6: one more point of condition");
            check(five.players[size_t(i)].technique() == catering.players[size_t(i)].technique(),
                  "catering does not change technique");
        }
        check((catering.team_morale[0] & 0x3f) == std::min(0x10 + 14, 0x2f) &&
              (five.team_morale[0] & 0x3f) == 0x10 + 12, "morale +14 at six stars, +12 at five");
        // Lodging 6 shortens the injury by one week; five stars leave it.
        check((after_camp({5, 5, 5, 6}, seed).players[3].w364 >> 16 & 0x1f) == 2, "lodging 6: one week less injury");
        check((five.players[3].w364 >> 16 & 0x1f) == 3, "lodging 5: injury unchanged");
    }
    {
        // The club is a word: a club beyond 127 (Modern: international clubs, the original read a
        // signed byte) books like any other; the national coach's desk (record names the
        // national team, no_team()) books none.
        ModernState g = squad();
        g.training_camps = {camp({5, 5, 5, 5})};
        g.manager_records[0x290] = 131;
        check(training_camp_booking_block(g, 0) == TrainingCampBookingBlock::none, "club 131 may book");
        bool thrown = false;
        try {
            WatcomRandom rand(1);
            training_camp_effect(g, 0, 0, rand);
        } catch (const std::exception&) {
            thrown = true;
        }
        check(!thrown, "camp effect for club 131");
        g.manager_records[0x290] = uint8_t(no_team() & 0xff), g.manager_records[0x291] = uint8_t(no_team() >> 8);
        check(training_camp_booking_block(g, 0) == TrainingCampBookingBlock::invalid_request,
              "no camp at the national coach's desk");
    }
    if (failures) return 1;
    std::puts("modern camp checks passed");
    return 0;
}
