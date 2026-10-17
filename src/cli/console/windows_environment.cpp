// SPDX-License-Identifier: GPL-3.0-only
// The console and desktop share all six monthly environment budgets.

#include "app/desktop_actions.hpp"
#include "cli/console/console.hpp"
#include "cli/console/detail.hpp"
#include "game/environment.hpp"

namespace bmh::cli::console {

using namespace detail;

void Console::register_environment_commands() {
    auto show = [this] {
        const EnvironmentState s = environment_state(session_->state(), active_slot());
        std::vector<std::vector<std::string>> rows;
        constexpr const char* names[] = {"YouthYoungest", "YouthAcademy", "YouthOldest", "AssistantCoach", "Doctor", "Physio"};
        for (int i = 0; i < kEnvironmentItems; ++i)
            rows.push_back({names[i], std::to_string(s.level[size_t(i)]), money(s.monthly[size_t(i)])});
        emit_table({"Item", "Level", "MonthlyCosts"}, rows);
        rows.clear();
        for (size_t i = 0; i < 3; ++i)
            rows.push_back({names[i], std::to_string(s.youth_position[i] + 1), std::to_string(s.youth_league[i] + 1)});
        emit_table({"YouthTeam", "Position", "Division"}, rows);
    };
    openers_[24] = [this, show] {
        open_window("Environment");
        show();
    };
    add({"Environment", "ShowEnvironment", {}, "all six monthly budgets and youth tables", false}, [show](const Values&) { show(); });
    auto apply = [this, show](int item, int level) {
        std::string error;
        const auto r=app::EnvironmentActions(*session_).set_console(
            ui::environment_from_game(session_->state(),revision_),item,level,revision_,error,
            [this](const GameState&,const std::string& message) { emit('i',utf8(message));return true; });
        if(!r||*r==EnvironmentResult::invalid) fail("E_BAD_ARGUMENT", "no such item or level");
        show();
    };
    add({"Environment", "SetStaffLevel", {choice("role", {"AssistantCoach", "Doctor", "Physio"}), integer("level", 0, 6)},
         "click a staff tile: level 0..6, 5 000 a month each", true},
        [apply](const Values& v) { apply(kCoTrainer + v.choice("role"), int(v.integer("level"))); });
    add({"Environment", "SetYouthLevel", {choice("team", {"Youngest", "Academy", "Oldest"}), integer("level", 0, 6)},
         "set one youth team's monthly budget, level 0..6", true},
        [apply](const Values& v) { apply(v.choice("team"), int(v.integer("level"))); });
    add({"Environment", "SetYouthAcademy", {choice("state", {"On", "Off"})}, "set the middle youth budget to level 6 or 0", true},
        [this, apply](const Values& v) {
            const bool on = environment_state(session_->state(), active_slot()).monthly[kYouthAcademy] != 0;
            const bool want = v.choice("state") == 0;
            // Retain the old On/Off command as an alias for the middle youth budget.
            if (on == want) emit('+', std::string("youth academy already ") + (on ? "on" : "off"));
            else apply(kYouthAcademy, want ? 6 : 0);
        });
}

}  // namespace bmh::cli::console
