#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
// The balance plan (desk menu 28, src/game/balance_plan.hpp) on a real savegame: the three
// views, a change of an item and of the gate, the reset, and what leaving the screen stores.

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "formats/fbm.hpp"
#include "formats/le.hpp"
#include "formats/savestate.hpp"
#include "game/balance_plan.hpp"
#include "game/career.hpp"

using namespace bmh;

namespace {

void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

int32_t rec32(const GameState& g, size_t o) {
    return int32_t(g.manager_records.at(o) | g.manager_records.at(o + 1) << 8 | g.manager_records.at(o + 2) << 16 |
                   uint32_t(g.manager_records.at(o + 3)) << 24);
}

double total_of(const BalancePlanDraft& d) {
    double t = 0;
    for (int i = 0; i < kBalanceItems; i++) t += double(d.rows[size_t(i)]) * (i < 7 ? 1 : -1);
    return t - double(d.depreciation);
}

void print(const BalancePlanDraft& d) {
    static const char* views[] = {"bisher", "real", "zukunft"};
    std::cout << views[int(d.view)] << " (" << d.months_left << " months):";
    for (int i = 0; i < kBalanceRows; i++) std::cout << " " << d.rows[size_t(i)];
    std::cout << " | depreciation " << d.depreciation << " total " << d.total << "\n";
}

void balance_test(const GameState& start, bool verbose) {
    GameState g = start;
    const GameState before = g;
    BalancePlanDraft d = open_balance_plan(g, 0);
    check(g.manager_records == before.manager_records && g.tactics == before.tactics, "opening the plan changes nothing");
    for (int v = 0; v < 3; v++) {
        if (verbose) print(d);
        check(std::abs(d.total - total_of(d)) < 0.5, "the total is the sum of the items");
        for (int i = 0; i < kBalanceItems; i++) check(d.rows[size_t(i)] >= 0, "items are not negative");
        cycle_balance_view(g, d);
    }
    // the view "bisher" shows the booked figures and refuses changes
    while (d.view != BalanceView::so_far) cycle_balance_view(g, d);
    check(d.rows[0] == rec32(g, 0x85c), "so far: shirt income as booked");
    check(edit_balance_plan(g, d, 0, 1000) == BalanceEditStatus::view_so_far, "no changes in the view so far");
    // future: raising the TV income by one million raises the plan by one million
    while (d.view != BalanceView::future) cycle_balance_view(g, d);
    const double plan = d.total;
    const int32_t tv_before = d.rows[2];
    check(edit_balance_plan(g, d, 3, 0) == BalanceEditStatus::not_editable, "the gate income is computed");
    check(edit_balance_plan(g, d, 2, d.rows[2] + 1000000) == BalanceEditStatus::changed, "change the TV income");
    check(std::abs(d.total - plan - 1000000) < 0.5, "the plan follows the change");
    check(d.adjustments[2] == 1000000, "a change in the future view counts for twelve months");
    // the gate projects the spectator income again
    const int32_t gate_income = d.rows[3];
    check(edit_balance_plan(g, d, kBalanceAverageGate, d.rows[kBalanceAverageGate] + 1000) == BalanceEditStatus::changed, "gate");
    check(d.rows[3] > gate_income, "a higher gate brings more spectator income");
    check(edit_balance_plan(g, d, kBalanceAverageGate, 100001) == BalanceEditStatus::out_of_range, "gate maximum");
    // leaving stores the plan; the next visit keeps the change
    const bool warning = close_balance_plan(g, d);
    check(rec32(g, 0x308) == int32_t(std::lround(d.total)), "plan figure stored");
    check(rec32(g, 0x2b8) == 1000000 && rec32(g, 0x310) == 2, "changes and view stored");
    check(warning == (double(rec32(g, 0x314)) + d.total < -4000000.0), "DFB warning");
    BalancePlanDraft again = open_balance_plan(g, 0);
    check(again.view == BalanceView::future && again.rows[2] == d.rows[2], "the next visit shows the change");
    reset_balance_plan(g, again);
    check(again.adjustments[2] == 0 && again.rows[2] == tv_before, "Werte akt. drops the change");
    std::cout << "plan " << int32_t(std::lround(d.total)) << (warning ? " (DFB warning)" : "") << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        check(argc >= 3, "usage: balance_plan_tests ORIGINAL_DATA SAVEGAME [verbose]");
        if (!*argv[1]) {
            std::cout << "balance plan tests: skipped (Original data directory not configured)\n";
            return 0;
        }
        const auto exe = bmh::test::load_executable(std::filesystem::path(argv[1]) / "BMH.exe");
        const auto file = load_savegame(argv[2]);
        const DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
        GameState g = GameState::from_save(SaveState(file.stream, counts), counts);
        competition_pointers(g.memory, exe);
        balance_test(g, argc > 3);
        std::cout << "balance plan tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "balance plan tests: " << error.what() << "\n";
        return 1;
    }
}
