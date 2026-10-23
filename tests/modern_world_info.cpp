// SPDX-License-Identifier: GPL-3.0-only
// modern_world_info <world.obw> - opens a Modern world package and prints its leagues, clubs
// and players (docs/new_engine, M3). Exit code 0 when the package opens, 1 otherwise.
#include <cstdio>
#include <exception>
#include <map>

#include "modern/content/world_package.hpp"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: modern_world_info WORLD.obw\n");
        return 2;
    }
    try {
        const auto package = bmh::modern::open_world_package(argv[1]);
        const auto& w = *package.world;
        std::printf("world %s, revision %s, start %s, content %s\n", w.id.c_str(),
                    w.data_revision.empty() ? "-" : w.data_revision.c_str(), w.start_date.c_str(),
                    package.classic->content_id.c_str());
        int league_clubs = 0;
        for (const auto& l : w.leagues) {
            int added = 0;
            for (const auto& m : l.members)
                if (const auto* c = w.club(m); c && c->legacy_index < 0) ++added;
            std::printf("league %d %-28s %2zu clubs (%d added), %s, %zu dates\n", l.index, l.name.c_str(),
                        l.members.size(), added, l.generator.c_str(), l.dates.size());
            league_clubs += int(l.members.size());
        }
        int legacy = 0, without_team = 0, coded = 0;
        std::map<std::string, int> squads;
        for (const auto& p : w.players) {
            legacy += p.legacy;
            if (p.team.empty()) ++without_team;
            else ++squads[p.team];
            coded += w.nationality.count(p.nation) && w.nationality.at(p.nation) < 32;
        }
        int largest = 0, smallest = 99;
        for (const auto& l : w.leagues)
            for (const auto& m : l.members) {
                const int n = squads.count(m) ? squads.at(m) : 0;
                largest = std::max(largest, n);
                smallest = std::min(smallest, n);
            }
        std::printf("%d league clubs, %zu clubs in all; %zu players (%d original, %d without club, "
                    "%d with a Classic nationality code); league squads %d-%d\n",
                    league_clubs, w.clubs.size(), w.players.size(), legacy, without_team, coded, smallest, largest);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "cannot open world: %s\n", e.what());
        return 1;
    }
}
