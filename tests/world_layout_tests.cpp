// SPDX-License-Identifier: GPL-3.0-only
// The Classic world layout must give exactly the numbers that stood as literals in the engine until
// 1.0.12, and squad_range() must return what its former table arithmetic returned for every team id.
// A layout with inconsistent ranges must be refused. Needs no game data.
#include <cstdio>
#include <stdexcept>
#include <string>

#include "game/state.hpp"
#include "game/world_layout.hpp"

using namespace bmh;

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

// squad_range as it was written before the world layout existed (state.cpp up to 1.0.12).
SquadRange former_squad_range(int team) {
    if (team == 0x165) return {3072, 24};
    if (team < 90) return {size_t(team) * 24, 24};
    if (team < 138) return {size_t(team) * 16 + 90 * 8, 16};
    return {0, 0};
}
}  // namespace

int main() {
    try {
        const WorldLayout w = classic_world_layout();
        check(w.league_teams == 138 && w.domestic_teams == 90 && w.amateur_teams == 30 && w.foreign_cup_teams == 189 &&
              w.national_teams == 62, "Classic team ranges are not the original ones");
        check(w.first_amateur() == 138 && w.first_foreign_cup() == 168 && w.no_team() == 0x165 && w.no_team() == kNoTeam &&
              w.total_teams() == 419, "derived team ids differ from 138 / 168 / 0x165 / 419");
        check(w.transfer_list == 90 * 24 + 48 * 16 && w.transfer_list == 0xB70 && w.free_agents == 0xB9C &&
              w.foreign_offers == 0xBEC && w.pool_end == 0xC00 && w.pool_end == 3072 && w.no_team_slots == 24 &&
              w.total_slots == 3096 + 54, "pool offsets differ from the original slot table");
        check(w.has_squad(0) && w.has_squad(137) && !w.has_squad(138) && !w.has_squad(167) && !w.has_squad(168) &&
              w.has_squad(0x165) && !w.has_squad(0x166) && !w.has_squad(-1), "has_squad differs");
        // the running game uses the Classic layout unless another one is installed
        check(world_layout().league_teams == 138 && world_layout().squad_start.size() == 138, "default layout is not Classic");
        for (int team = -3; team < 450; ++team) {
            const SquadRange now = squad_range(team), then = former_squad_range(team);
            if (now.first != then.first || now.count != then.count)
                throw std::runtime_error("squad_range differs from the former table for team " + std::to_string(team));
        }
        // inconsistent layouts are refused and leave the installed one alone
        const auto refused = [](WorldLayout layout) {
            try { set_world_layout(std::move(layout)); } catch (const std::invalid_argument&) { return true; }
            return false;
        };
        WorldLayout bad = classic_world_layout(); bad.squad_size[5] = 20;                // squads no longer back to back
        check(refused(bad), "a gap in the squad table was accepted");
        bad = classic_world_layout(); bad.squad_start.pop_back();                         // table shorter than the club count
        check(refused(bad), "a short squad table was accepted");
        bad = classic_world_layout(); bad.transfer_list += 16;                            // pools not behind the squads
        check(refused(bad), "pools away from the squads were accepted");
        bad = classic_world_layout(); bad.foreign_cup_teams = 900;                        // more teams than a loan can name
        check(refused(bad), "more than 1023 teams were accepted");
        check(world_layout().league_teams == 138 && squad_range(137).first == 137 * 16 + 720, "a refused layout changed the game");
        // a consistent layout is accepted (here: the Classic one again)
        set_world_layout(classic_world_layout());
        check(squad_range(89).first == 89 * 24 && squad_range(90).first == 2160 && squad_range(0x165).first == 3072,
              "installing the Classic layout changed the squad table");
        std::printf("world layout: Classic numbers, squad table and validation passed\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
