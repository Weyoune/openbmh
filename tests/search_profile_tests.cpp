// SPDX-License-Identifier: GPL-3.0-only
// Search profile of a human manager (game/search_profile.*): the closing rule of the dialog and the
// matching of a transfer-list entry. The engine was also compared with the original routines sub_190B5 /
// sub_18F06 of the Diamond 3.7 executable on whole savegames (private emulator harness, not part of this test).
#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "formats/executable_layout.hpp"
#include "game/search_profile.hpp"

using namespace bmh;

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

// Manager 0 manages club 3; one listing of a defender of club 5 (age field 6, price byte 0x5a = class 1).
GameState fixture() {
    GameState g;
    g.manager_teams = {3};
    g.manager_records.resize(0xb0c);
    g.squad_slots.assign(3072, 0xfff);
    g.squad_slots[0xb70] = 0;
    g.players.resize(1);
    g.players[0].name = "Listed";
    g.players[0].w35c = 70u << 14 | 70u << 7 | 70u;
    g.players[0].w364 = 6u << 21 | 1u;  // age field 6, position 1 (defender)
    g.memory.add_block(layout::EditorList, 44 * 0x58);
    g.memory.set16(layout::EditorList + 8, 5);
    g.memory.set8(layout::EditorList + 0x54, 0x5a);
    return g;
}

SearchProfile open_profile() {
    SearchProfile p{};
    p[4] = 0; p[5] = 24; p[6] = 0; p[7] = 99;
    return p;
}
}  // namespace

int main() try {
    {   // a fresh manager has no active profile
        auto g = fixture();
        g.manager_records[kSearchProfileOffset] = kSearchProfileOff;
        require(!search_profile_matches(g, 0, 0), "an inactive profile matches nothing");
    }
    {   // an open profile for defenders matches the listing, one for goalkeepers only does not
        auto g = fixture();
        auto p = open_profile(); p[1] = 1;
        require(set_search_profile(g, 0, p, false) && search_profile(g, 0) == p, "profile with a position is stored active");
        require(search_profile_matches(g, 0, 0), "defender profile matches the defender");
        p[1] = 0; p[0] = 1;
        require(set_search_profile(g, 0, p, false) && !search_profile_matches(g, 0, 0), "goalkeeper profile does not match");
        require(!search_profile_matches(g, 0, 1), "an empty list slot never matches");
    }
    {   // the dialog's closing rule
        auto g = fixture();
        SearchProfile zero{};
        require(!set_search_profile(g, 0, zero, false) && (search_profile(g, 0)[0] & kSearchProfileOff), "all zero switches the profile off");
        SearchProfile ones; ones.fill(1);
        require(!set_search_profile(g, 0, ones, false) && (search_profile(g, 0)[0] & kSearchProfileOff), "all one switches the profile off");
        auto p = open_profile(); p[2] = 1;
        require(!set_search_profile(g, 0, p, true) && (search_profile(g, 0)[0] & kSearchProfileOff), "leaving by right click switches the profile off");
        require(search_profile(g, 0)[2] == 1, "the edited values are kept when the dialog is cancelled");
        auto bad = p; bad[9] = 2;
        const auto before = search_profile(g, 0);
        require(!set_search_profile(g, 0, bad, false) && search_profile(g, 0) == before, "a switch above 1 is refused and nothing is stored");
        bad = p; bad[5] = 25;
        require(!set_search_profile(g, 0, bad, false), "the age bar has 25 cells");
        require(!set_search_profile(g, 5, p, false), "an invalid manager is refused");
    }
    {   // age and price class
        auto g = fixture();
        auto p = open_profile(); p[1] = 1; p[4] = 10; p[5] = 20;
        require(set_search_profile(g, 0, p, false) && !search_profile_matches(g, 0, 0), "age field 6 is outside 10..20");
        p[4] = 2; p[5] = 8; p[15] = 1;   // only the cheapest price class wanted, the listing asks 90 % (class 1)
        require(set_search_profile(g, 0, p, false) && !search_profile_matches(g, 0, 0), "price class 1 is above the wanted class 0");
        p[16] = 1;
        require(set_search_profile(g, 0, p, false) && search_profile_matches(g, 0, 0), "price class 1 is wanted");
    }
    {   // the own club's listings never match
        auto g = fixture();
        g.memory.set16(layout::EditorList + 8, 3);
        auto p = open_profile(); p[1] = 1;
        require(set_search_profile(g, 0, p, false) && !search_profile_matches(g, 0, 0), "an own listing never matches");
    }
    std::cout << "PASS search profile closing rule and matching\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
