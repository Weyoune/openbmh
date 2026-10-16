#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "test_inputs.hpp"
// The club environment (desk menu 24, src/game/environment.hpp) on a real savegame: staff
// levels, and the supported edition youth academy - only in July/August, the league's season price,
// no switching off once paid.

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "formats/fbm.hpp"
#include "formats/le.hpp"
#include "formats/savestate.hpp"
#include "formats/textd.hpp"
#include "game/career.hpp"
#include "game/environment.hpp"
#include "game/schedule.hpp"

using namespace bmh;

namespace {

void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

void set_rec32(GameState& g, size_t o, uint32_t v) {
    for (int i = 0; i < 4; i++) g.manager_records.at(o + size_t(i)) = uint8_t(v >> (8 * i));
}

void environment_test(GameState g) {
    // staff: level * 5 000
    check(set_environment(g, 0, kDoctor, 4) != EnvironmentResult::invalid, "doctor level");
    EnvironmentState s = environment_state(g, 0);
    check(s.monthly[kDoctor] == 20000 && s.level[kDoctor] == 4, "doctor 4 x 5 000");
    check(set_environment(g, 0, kPhysio, 7) == EnvironmentResult::invalid, "level 7 is no level");
    // the other two youth groups are always 0
    set_environment(g, 0, 0, 6);
    check(environment_state(g, 0).monthly[0] == 0, "item 0 stays 0");
    // the academy out of season (the savegame is in spring)
    set_rec32(g, 0x880, 0);
    std::string message;
    check(set_environment(g, 0, kYouthAcademy, 6, &message) == EnvironmentResult::too_late, "too late in spring");
    check(message.find("Zu sp") == 0 && environment_state(g, 0).monthly[kYouthAcademy] == 0, "too late message, stays off");
    // July: on at the league's price, a second click switches it off again before it is paid
    g.date = g.season_start + 5;
    g.league_rounds.at(0) = 0;
    s = environment_state(g, 0);
    check(s.youth_open && !s.youth_paid, "July: the academy can be started");
    check(set_environment(g, 0, kYouthAcademy, 6) == EnvironmentResult::changed, "academy on");
    const int league = team_league(g.manager_teams.at(0));
    const int32_t price = league == 0 ? 1500000 : league == 1 ? 1000000 : league <= 3 ? 580000 : 280000;
    check(environment_state(g, 0).monthly[kYouthAcademy] == price, "season price of the league");
    check(set_environment(g, 0, kYouthAcademy, 6) == EnvironmentResult::changed &&
              environment_state(g, 0).monthly[kYouthAcademy] == 0, "a click on the active academy switches it off");
    // paid this season: stays on
    set_environment(g, 0, kYouthAcademy, 6);
    set_rec32(g, 0x880, uint32_t(price));
    message.clear();
    check(set_environment(g, 0, kYouthAcademy, 0, &message) == EnvironmentResult::contract_runs, "paid: no switching off");
    check(message == bmh::test::expected_text("environment_tests.0") && environment_state(g, 0).monthly[kYouthAcademy] == price,
          "paid: the contract runs");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 3, "usage: environment_tests ORIGINAL_DATA SAVEGAME");
        if (!*argv[1]) {
            std::cout << "environment tests: skipped (Original data directory not configured)\n";
            return 0;
        }
        const auto exe = bmh::test::load_executable(std::filesystem::path(argv[1]) / "BMH.exe");
        const auto file = load_savegame(argv[2]);
        const DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
        GameState g = GameState::from_save(SaveState(file.stream, counts), counts);
        competition_pointers(g.memory, exe);
        const auto texts = load_textd(std::filesystem::path(argv[1]) / "TEXTD.BLK");
        g.texts = &texts;
        environment_test(g);
        std::cout << "environment tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "environment tests: " << error.what() << "\n";
        return 1;
    }
}
