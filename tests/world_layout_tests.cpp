// SPDX-License-Identifier: GPL-3.0-only
// WorldLayout: the Classic layout reproduces the original squad table and team ranges;
// set_world_layout accepts a consistent Modern-shaped layout and rejects broken ones.
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#include "game/state.hpp"
#include "game/world_layout.hpp"

using namespace bmh;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}
bool rejects(WorldLayout w) {
    try {
        set_world_layout(std::move(w));
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}
// A layout with `league` clubs of 24 slots each, the pools behind them.
WorldLayout uniform(int league, int german) {
    WorldLayout w;
    w.league_teams = league;
    w.german_teams = german;
    w.squad_start.clear();
    w.squad_size.clear();
    for (int t = 0; t < league; ++t) {
        w.squad_start.push_back(uint32_t(t) * 24);
        w.squad_size.push_back(24);
    }
    w.transfer_list = uint32_t(league) * 24;
    w.free_agents = w.transfer_list + 44;
    w.foreign_offers = w.free_agents + 80;
    w.pool_end = w.foreign_offers + 20;
    w.total_slots = w.pool_end + 24 + 54;
    return w;
}
}  // namespace

int main() {
    // Classic: the original numbers (V2.24).
    const WorldLayout c = classic_world_layout();
    check(world_layout().league_teams == 138, "default layout is Classic");
    check(c.no_team() == kNoTeam && no_team() == kNoTeam, "Classic no_team is 0x165");
    check(c.first_foreign_cup() == 168 && c.total_teams() == 419, "Classic team ranges");
    check(squad_range(0).first == 0 && squad_range(0).count == 24, "German club squad");
    check(squad_range(89).first == 89 * 24 && squad_range(89).count == 24, "last German club squad");
    check(squad_range(90).first == 90 * 24 && squad_range(90).count == 16, "first international club squad");
    check(squad_range(137).first == 137 * 16 + 720 && squad_range(137).count == 16, "last international club squad");
    check(squad_range(137).first + 16 == c.transfer_list && c.transfer_list == 0xB70, "pools follow the squads");
    check(squad_range(kNoTeam).first == 3072 && squad_range(kNoTeam).count == 24, "no-club squad at 3072");
    check(squad_range(138).count == 0 && squad_range(200).count == 0, "squadless clubs");
    check(!rejects(classic_world_layout()), "Classic layout is accepted");

    // Modern shape: 168 league clubs with 24 slots each.
    WorldLayout m = uniform(168, 90);
    check(!rejects(m), "Modern layout is accepted");
    check(world_layout().first_foreign_cup() == 198 && no_team() == 387 && world_layout().total_teams() == 449,
          "Modern team ranges");
    check(squad_range(150).first == 150 * 24 && squad_range(150).count == 24, "Modern international squad");
    check(squad_range(no_team()).first == world_layout().pool_end, "Modern no-club squad");

    // Inconsistent layouts are rejected and leave the running layout unchanged.
    WorldLayout gap = uniform(168, 90);
    gap.squad_start[5] += 1;
    check(rejects(gap), "squads with a gap are rejected");
    WorldLayout pools = uniform(168, 90);
    pools.free_agents += 1;
    check(rejects(pools), "misplaced pools are rejected");
    WorldLayout too_many = uniform(168, 90);
    too_many.national_teams = 700;
    check(rejects(too_many), "more than 1023 teams are rejected");
    check(world_layout().league_teams == 168, "a rejected layout leaves the previous one");

    set_world_layout(classic_world_layout());
    if (failures) return 1;
    std::puts("world layout checks passed");
    return 0;
}
