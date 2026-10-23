// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/balance_plan.hpp"
// All three youth budgets and staff costs share the monthly environment model.
// The balance plan ("Bilanz", desk menu 28, mode III; H2 p. 26-27; BMH.exe V2.24 + supported rule profile
// patch): the screen BalanceFromGame2 projects the club's income and expenses for the business
// year and lets the manager change every item. When he leaves the screen the resulting
// profit/loss becomes the plan figure (manager record +0x308). On the 16th Bundesliga
// matchday the plan is handed to the DFB (+0x308 -> +0x320, matchday.cpp); from then on it
// lifts or lowers the advertising offers (weekly.cpp, plan / 100000 within -10..10) and at
// the end of the season the DFB punishes a result more than 1 million below the plan
// (season.cpp, SeasonEvent).
//
// The screen has three views (left button, captions BalancePlan_DrawButtons_TextPointer):
//   so_far  "bisher"  - the booked figures of the season so far (record +0x85c..), no changes
//   real    "real"    - booked figures plus a projection for the months left
//   future  "zukunft" - a projection for twelve months
// The manager's changes are kept per item normalised to twelve months (record +0x2b0, the
// screen's local_c8) and are added again on the next visit; "Werte akt." (right button)
// drops them.
//
// The screen's state is a BalancePlanDraft (the locals of BalanceFromGame2); the functions below
// are its event handlers. Projections run on a copy of the ModernState: the wage projection
// (MonthlyPay -> table prediction) writes scratch tables, and a console must be able to
// look at the plan without changing the game.

#include <array>
#include <cstdint>

#include "modern/game/modern_state.hpp"

namespace bmh::modern {

constexpr int kBalanceItems = 18;  // 0-6 income, 7-17 expenses (labels BalancePlan_DrawRows_TextPointer5)
// Rows after the items (labels BalancePlan_DrawRows_TextPointer4): league home games, other home games, average
// gate (calculated), ticket price, average gate so far, account balance.



// BalanceFromGame2 up to its first redraw: the draft of `manager` with the stored view, the
// stored changes and the projection of that view.
// StadiumRequiredAttendance: minimum average attendance at the current stadium entry price.
// Pure preview, using a copy for Source prediction scratch writes; no RNG.
int32_t stadium_required_attendance(const ModernState&, int manager);

BalancePlanDraft open_balance_plan(const ModernState& g, int manager);

// The left button: the next view (so_far -> real -> future -> so_far), projected again.
void cycle_balance_view(const ModernState& g, BalancePlanDraft& draft);
// The right button "Werte akt.": drops the manager's changes and projects again.
void reset_balance_plan(const ModernState& g, BalancePlanDraft& draft);

// Range of a row in the number input (BalanceFromGame): 0 .. maximum.
int32_t balance_row_maximum(const BalancePlanDraft& draft, int row);
// A click on a row and a new value: items keep the change in `adjustments` (scaled to twelve
// months); the home games, the gate and the ticket price project the plan again.
BalanceEditStatus edit_balance_plan(const ModernState& g, BalancePlanDraft& draft, int row, int32_t value);

// Leaving the screen: writes the changes (+0x2b0), the plan figure (+0x308), the other home
// games and the gate (+0x2f8/+0x2fc), the view (+0x310) and the ticket price of the plan
// (PlanTicketPrice). Returns true when the DFB demands a better balance (account + plan below
// -4 million, message BalancePlan_BalanceFromGame_TextPointer2) - the plan is stored nevertheless.
bool close_balance_plan(ModernState& g, const BalancePlanDraft& draft);

}  // namespace bmh::modern
