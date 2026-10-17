// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/environment.hpp"
#include "ui/live_timing.hpp"
#include <iostream>
#include <stdexcept>

using namespace bmh;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
GameState fixture() {
    GameState g;
    g.manager_teams = {0, 1};
    g.manager_records.resize(2 * 0xb0c);
    g.league_rounds.resize(1);
    g.memory.add_block(layout::EnvironmentYouthTeams, 0x90);
    for (uint32_t m = 0; m < 2; ++m)
        for (uint32_t i = 0; i < 3; ++i) {
            g.memory.set8(layout::EnvironmentYouthTeams + m * 0x24 + i, uint8_t(60 - i * 10));
            g.memory.set8(layout::EnvironmentYouthTeams + m * 0x24 + i + 3, 6);
            g.memory.set8(layout::EnvironmentYouthTeams + m * 0x24 + i + 6, uint8_t(i));
        }
    return g;
}
void budgets() {
    auto g = fixture();
    for (int i = 0; i < 6; ++i) check(set_environment(g, 0, i, 6) == EnvironmentResult::changed, "all six budgets accept maximum");
    const auto s = environment_state(g, 0);
    check(s.monthly == std::array<int32_t, 6>{12000,24000,30000,30000,30000,30000}, "six monthly costs match the supported price units");
    check(environment_state(g, 1).monthly == std::array<int32_t, 6>{}, "other manager's budgets are isolated");
    check(set_environment(g, 0, kYouthAcademy, 6) == EnvironmentResult::unchanged, "repeating a level does not toggle the youth budget");
    for (int i = 0; i < 6; ++i) check(environment_monthly_cost(g, 0, i) == s.monthly[size_t(i)], "all six selected budgets reach monthly accounting");
    check(set_environment(g, 0, kYouthAcademy, 0) == EnvironmentResult::changed && !environment_academy_active(g, 0), "youth budget can be stopped");
    check(set_environment(g, 0, 6, 2) == EnvironmentResult::invalid &&
          set_environment(g, -1, 0, 1) == EnvironmentResult::invalid &&
          set_environment(g, 0, 0, 7) == EnvironmentResult::invalid, "invalid requests rejected");
    g.memory.set32(layout::Spending + 4, 1500000);
    check(environment_monthly_cost(g, 0, 1) == 1500000 && !environment_academy_active(g, 0), "unpaid legacy contract retains one season payment");
    for (int i = 0; i < 4; ++i) g.manager_records[0x880 + size_t(i)] = uint8_t(1500000u >> (i * 8));
    check(environment_monthly_cost(g, 0, 1) == 0 && environment_academy_active(g, 0), "paid legacy contract is never debited again");
    set_environment(g, 0, 1, 2);
    check(environment_monthly_cost(g, 0, 1) == 8000 && environment_academy_active(g, 0), "an explicit edit replaces the legacy contract with a monthly budget");
}
void progression() {
    auto funded = fixture(), unfunded = fixture();
    for (int i = 0; i < 3; ++i) set_environment(funded, 0, i, 6);
    WatcomRandom rich(4711), poor(4711);
    for (int round = 0; round < 100; ++round) {
        funded.league_rounds[0] = unfunded.league_rounds[0] = uint8_t(round % 34);
        advance_youth_teams(funded, 0, rich);
        advance_youth_teams(unfunded, 0, poor);
    }
    const auto a = environment_state(funded, 0), b = environment_state(unfunded, 0);
    check(a.youth_strength[0] > b.youth_strength[0] && a.youth_strength[1] > b.youth_strength[1] &&
          a.youth_strength[2] > b.youth_strength[2], "investment changes development in every youth group");
    check(rich.seed() == poor.seed() && rich.calls() == poor.calls(), "budgets do not change the youth algorithm's RNG call sequence");
    check(environment_state(funded, 1).youth_position == std::array<uint8_t,3>{6,6,6}, "youth results remain manager-local");
    auto g = fixture();
    for (uint32_t i = 0; i < 3; ++i) g.memory.set8(layout::EnvironmentYouthTeams + i, uint8_t(30 + 20 * i));
    g.memory.set8(layout::EnvironmentYouthTeams + 3, 0);
    g.memory.set8(layout::EnvironmentYouthTeams + 6, 1);
    g.memory.set8(layout::EnvironmentYouthTeams + 4, 11);
    g.memory.set8(layout::EnvironmentYouthTeams + 7, 0);
    g.memory.set8(layout::EnvironmentYouthTeams + 5, 11);
    finish_youth_season(g, 0);
    const auto s = environment_state(g, 0);
    check(s.youth_league == std::array<uint8_t,3>{0,1,2} && s.youth_position == std::array<uint8_t,3>{10,1,5}, "season transition handles promotion, relegation and bottom division");
    check(s.youth_strength == std::array<uint8_t,3>{30,43,63}, "age-group handover uses previous strengths without cascading");
}
void timing() {
    using bmh::ui::live_delay_milliseconds;
    check(live_delay_milliseconds(0) == 0 && live_delay_milliseconds(1) == 0 &&
          live_delay_milliseconds(2) == 20 && live_delay_milliseconds(5) == 40 &&
          live_delay_milliseconds(6) == 120 && live_delay_milliseconds(30) == 600 &&
          live_delay_milliseconds(198) == 3960, "original integer delay scale includes zero and manual speed range");
}
}
int main() {
    try { budgets(); progression(); timing(); std::cout << "environment and live timing: ok\n"; return 0; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
