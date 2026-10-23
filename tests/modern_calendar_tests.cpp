// SPDX-License-Identifier: GPL-3.0-only
// Modern league dates: season days of ISO dates and placing a league group's dates in the
// season calendar around special days (docs/new_engine, M2). Synthetic calendar only.
#include <cstdio>
#include <stdexcept>
#include <vector>

#include "formats/executable_layout.hpp"
#include "modern/game/world_calendar.hpp"

using namespace bmh;
using namespace bmh::modern;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}
constexpr uint32_t kCalendar = ::bmh::layout::CompetitionCalendar;
uint16_t entry(const ModernState& g, int day) { return g.memory.u16(kCalendar + uint32_t(day) * 2); }
}  // namespace

int main() {
    check(season_day_of("2026-07-01", 2026) == 0, "1 July is day 0");
    check(season_day_of("2026-08-08", 2026) == 38, "8 August");
    check(season_day_of("2027-02-28", 2026) == 242, "28 February");
    check(season_day_of("2027-07-01", 2026) == 365, "next 1 July");
    check(season_day_of("2026-13-01", 2026) == -1 && season_day_of("26-07-01", 2026) == -1, "malformed dates");

    ModernState g;
    g.memory.add_block(kCalendar, 366 * 2);
    // Ordinary days with the old group-4 dates, a German league day, special days.
    for (int day = 10; day < 100; day += 10) g.memory.set16(kCalendar + uint32_t(day) * 2, 0x10);
    g.memory.set16(kCalendar + 40 * 2, 0x11);     // Bundesliga and group 4
    g.memory.set16(kCalendar + 50 * 2, 0x4002);   // indoor tournament: special
    g.memory.set16(kCalendar + 51 * 2, 0x4006);   // special
    g.memory.set16(kCalendar + 300 * 2, 0x4014);  // special day that happens to carry bit 4

    const auto r = install_league_dates(g, 4, {50, 5, 52, 360});
    check(r.dates == 4, "all dates placed");
    check(entry(g, 10) == 0 && entry(g, 90) == 0, "old ordinary group dates cleared");
    check(entry(g, 40) == 0x01, "other bits of a cleared day stay");
    check(entry(g, 300) == 0x4014, "special days are not touched");
    check(entry(g, 5) == 0x10 && entry(g, 360) == 0x10, "dates on ordinary days");
    check(entry(g, 50) == 0x4002 && entry(g, 51) == 0x4006, "special days keep their meaning");
    check(entry(g, 52) == 0x10 && entry(g, 53) == 0x10, "a date on a special day moves to the next free day");
    check(r.moved.size() == 2, "both moved dates are reported (50 -> 52 pushes 52 -> 53)");

    bool threw = false;
    try {
        install_league_dates(g, 4, {365, 365});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "a date that cannot be placed is rejected");
    threw = false;
    try {
        ModernState empty;
        install_league_dates(empty, 4, {1});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "a missing calendar is rejected");

    if (failures) return 1;
    std::puts("modern calendar checks passed");
    return 0;
}
