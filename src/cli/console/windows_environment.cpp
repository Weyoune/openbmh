// SPDX-License-Identifier: GPL-3.0-only
// OpenBMH deviates from the original: uses monthly staff levels in increments of five thousand and the supported youth-academy controls.
// The club environment (menu 24, docs/CLI_CONCEPT/06-verein.md 10, H2 p. 93, supported rule profile p. 107,
// 111): co-trainer, doctor and physio in levels of 5 000 a month, and the supported rule profile youth
// academy that costs a season price of the league.
//
// The rules are the engine's (src/game/environment.hpp, a port of Image with the
// supported rule profile patch); a command is one click of the original screen.

#include "app/desktop_actions.hpp"
#include "cli/console/console.hpp"
#include "cli/console/detail.hpp"
#include "game/environment.hpp"

namespace bmh::cli::console {

using namespace detail;

void Console::register_environment_commands() {
    auto show = [this] {
        const EnvironmentState s = environment_state(session_->state(), active_slot());
        std::vector<std::vector<std::string>> rows = {
            {"AssistantCoach", std::to_string(s.level[kCoTrainer]), money(s.monthly[kCoTrainer])},
            {"Doctor", std::to_string(s.level[kDoctor]), money(s.monthly[kDoctor])},
            {"Physio", std::to_string(s.level[kPhysio]), money(s.monthly[kPhysio])},
            {"YouthAcademy", s.monthly[kYouthAcademy] != 0 ? "on" : "off", money(s.youth_price) + " per season"}};
        emit_table({"Item", "Level", "Costs"}, rows);
        emit('i', std::string("youth academy: ") + (s.youth_paid ? "paid this season" : s.youth_open ? "can be started (July/August)"
                                                                                                        : "too late for this season"));
    };
    openers_[24] = [this, show] {
        open_window("Environment");
        show();
    };
    add({"Environment", "ShowEnvironment", {}, "staff levels and the youth academy", false}, [show](const Values&) { show(); });
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
    add({"Environment", "SetYouthAcademy", {choice("state", {"On", "Off"})}, "start (July/August) or stop the youth academy", true},
        [this, apply](const Values& v) {
            const bool on = environment_state(session_->state(), active_slot()).monthly[kYouthAcademy] != 0;
            const bool want = v.choice("state") == 0;
            // the original's click on an active academy switches it off: click only for a change
            if (on == want) emit('+', std::string("youth academy already ") + (on ? "on" : "off"));
            else apply(kYouthAcademy, want ? 6 : 0);
        });
}

}  // namespace bmh::cli::console
