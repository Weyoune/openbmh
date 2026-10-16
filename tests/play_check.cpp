// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
// play_check - drives app::CareerSession like the UI does (options, managers one by one,
// finish) without a window and prints the desk the UI would get.
//
// Usage: play_check <BMH directory> <Watcom seed> <first savegame of a recorded career>
//                   <master club>:<league>:<strength>:<portrait>:<name> ...
// The options (duration, level, managers, mode, competitions) come from the savegame.
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "app/career_session.hpp"
#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "ui/desk_state.hpp"

using namespace bmh;

int main(int argc, char** argv) {
    if (argc < 5) {
        std::printf("usage: play_check <BMH dir> <seed> <savegame> club:league:strength:portrait:name ...\n");
        return 1;
    }
    try {
        Savegame sg = load_savegame(argv[3]);
        SaveState st(sg.stream);
        ui::SetupOptions o;
        o.duration = st.data(::bmh::layout::CareerDuration)[0];
        o.difficulty = st.data(::bmh::layout::Difficulty)[0];
        o.managers = st.data(::bmh::layout::ManagerCount)[0];
        o.mode = st.data(::bmh::layout::GameMode)[0];
        o.league = (st.data(::bmh::layout::CompetitionSelection)[0] & 1) != 0;
        o.world = (st.data(::bmh::layout::CompetitionSelection)[0] & 2) != 0;
        app::CareerSession s(argv[1], uint32_t(std::strtoul(argv[2], nullptr, 0)));
        for (int i = 4; i < argc; i++) {
            ui::ManagerChoice c;
            std::stringstream in(argv[i]);
            std::string part;
            std::getline(in, part, ':'), c.master_club = std::stoi(part);
            std::getline(in, part, ':'), c.start_league = std::stoi(part);
            std::getline(in, part, ':'), c.strength = std::stoi(part);
            std::getline(in, part, ':'), c.portrait = std::stoi(part);
            std::getline(in, c.name);
            c.slot = i - 4;
            std::string error;
            if (!s.confirm_manager(o, c, error)) {
                std::printf("manager %d: %s\n", i - 4, error.c_str());
                return 2;
            }
            std::printf("manager %d confirmed: runtime club %d; seeds game %d, Watcom %u\n", i - 4,
                        s.state().manager_teams.at(size_t(i - 4)), s.game().seed(), s.rand().seed());
        }
        ui::CareerInitializationResult r;
        std::string error;
        if (!s.finish(r, error)) {
            std::printf("finish: %s\n", error.c_str());
            return 2;
        }
        std::printf("first desk: seeds game %d, Watcom %u\n%s\n", s.game().seed(), s.rand().seed(), ui::desk_json(r.desktop).c_str());
        return 0;
    } catch (const std::exception& e) {
        std::printf("error: %s\n", e.what());
        return 3;
    }
}
