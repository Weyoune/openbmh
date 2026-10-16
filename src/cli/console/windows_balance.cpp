// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
// The balance plan (menu 28, mode III; docs/CLI_CONCEPT/06-verein.md 11, H2 p. 26-27): the
// projected income and expenses of the business year in three views, every item can be
// changed; leaving the window stores the plan figure the DFB gets on the 16th Bundesliga
// matchday.
//
// The screen logic is the engine's (src/game/balance_plan.hpp, a port of BalanceFromGame2); the
// console keeps the screen's draft while the window is open and closes it on GoBack.

#include <algorithm>

#include "app/desktop_actions.hpp"
#include "cli/console/console.hpp"
#include "cli/console/detail.hpp"
#include "game/balance_plan.hpp"
#include "game/messages.hpp"

namespace bmh::cli::console {

using namespace detail;

namespace {

// row names in screen order (labels BalancePlan_DrawRows_TextPointer5 and BalancePlan_DrawRows_TextPointer4)
const std::vector<std::string> kRowNames = {
    "IncomeShirt",     "IncomeBoards",    "IncomeTv",         "IncomeGate",      "IncomePeriphery", "IncomeRentInterest",
    "IncomeMisc",      "ExpenseSalaries", "ExpenseStadium",   "ExpenseYouth",    "ExpenseCoTrainer", "ExpenseDoctor",
    "ExpensePhysio",   "ExpenseTrainingCamp", "ExpenseFanCare", "ExpensePr",     "ExpenseInterest", "ExpenseMisc",
    "LeagueHomeGames", "OtherHomeGames",  "AverageGate",      "TicketPrice",     "GateSoFar",       "Account"};
const char* const kViews[] = {"SoFar", "Real", "Future"};

}  // namespace

void Console::register_balance_commands() {
    auto show = [this] {
        const BalancePlanDraft& d = *balance_draft_;
        std::vector<std::vector<std::string>> rows;
        for (int i = 0; i < kBalanceRows; i++) {
            const bool money_row = i < kBalanceItems || i == kBalanceAccount;
            rows.push_back({kRowNames[size_t(i)], money_row ? money(d.rows[size_t(i)]) : std::to_string(d.rows[size_t(i)]),
                            balance_row_maximum(d, i) >= 0 ? "yes" : "no"});
        }
        emit('i', std::string("view ") + kViews[int(d.view)] + ", " + std::to_string(d.months_left) + " months left");
        emit_table({"Item", "Value", "Changeable"}, rows);
        if (d.depreciation != 0) emit('i', "depreciation " + money(d.depreciation));
        emit('i', std::string(d.total < 0 ? "planned loss " : "planned profit ") + money((long long)(d.total < 0 ? -d.total : d.total)));
    };
    openers_[28] = [this, show] {
        balance_draft_ = app::BalanceActions(*session_).open_draft(active_slot());
        open_window("Balance");
        show();
    };
    closers_["Balance"] = [this] {
        const BalancePlanDraft d = *balance_draft_;
        balance_draft_.reset();
        const bool warning = app::BalanceActions(*session_).close_draft(d, revision_);
        close_window();
        // the original's message box BalancePlan_BalanceFromGame_TextPointer2 (the plan is stored nevertheless)
        if (warning) emit('i', bmh::message_text(session_->state(), ::bmh::layout::BalancePlan_BalanceFromGame_TextPointer2));
        emit('+', "balance plan stored: " + money((long long)d.total));
    };
    add({"Balance", "ShowBalance", {}, "all items, the view and the planned profit or loss", false}, [show](const Values&) { show(); });
    add({"Balance", "SetBalanceView", {choice("view", {kViews[0], kViews[1], kViews[2]})}, "left button: so far / real / future", false},
        [this, show](const Values& v) {
            while (int(balance_draft_->view) != v.choice("view")) app::BalanceActions(*session_).cycle_draft(*balance_draft_);
            show();
        });
    std::vector<std::string> editable;
    for (int i = 0; i < 0x16; i++)
        if (balance_row_maximum(BalancePlanDraft{}, i) >= 0) editable.push_back(kRowNames[size_t(i)]);
    add({"Balance", "SetBalanceItem", {choice("item", editable), integer("value", 0, 30000000)}, "change a planned value (views Real and Future)", false},
        [this, show, editable](const Values& v) {
            const std::string& name = editable[size_t(v.choice("item"))];
            const int row = int(std::find(kRowNames.begin(), kRowNames.end(), name) - kRowNames.begin());
            switch (app::BalanceActions(*session_).edit_draft(*balance_draft_, row, int32_t(v.integer("value")))) {
            case BalanceEditStatus::changed: break;
            case BalanceEditStatus::view_so_far: fail("E_RULE", "Bitte den linken Button auf REAL oder ZUKUNFT stellen (SetBalanceView)");
            case BalanceEditStatus::out_of_range:
                fail("E_BAD_ARGUMENT", name + " goes up to " + std::to_string(balance_row_maximum(*balance_draft_, row)));
            default: fail("E_RULE", name + " cannot be changed");
            }
            show();
        });
    add({"Balance", "TakeActuals", {}, "\"Werte akt.\": drop the own changes, back to the game's figures", false}, [this, show](const Values&) {
        app::BalanceActions(*session_).reset_draft(*balance_draft_);
        show();
    });
}

}  // namespace bmh::cli::console
