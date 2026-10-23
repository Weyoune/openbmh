// SPDX-License-Identifier: GPL-3.0-only
#include "modern/game/world_calendar.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

#include "formats/executable_layout.hpp"

namespace bmh::modern {

namespace {

constexpr uint32_t kCalendar = ::bmh::layout::CompetitionCalendar;
constexpr int kCalendarDays = 366;
constexpr uint16_t kSpecialDay = 0x4000;

// Days since 1970-01-01 of a proleptic Gregorian date (H. Hinnant's days_from_civil).
long days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = unsigned((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + long(doe) - 719468;
}

}  // namespace

int season_day_of(const std::string& date, int start_year) {
    int y = 0, m = 0, d = 0;
    if (date.size() != 10 || std::sscanf(date.c_str(), "%4d-%2d-%2d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 ||
        d > 31)
        return -1;
    return int(days_from_civil(y, m, d) - days_from_civil(start_year, 7, 1));
}

LeagueDatesResult install_league_dates(ModernState& g, int calendar_bit, const std::vector<int>& season_days) {
    if (calendar_bit < 0 || calendar_bit > 4) throw std::invalid_argument("league dates: calendar bit outside 0-4");
    if (!g.memory.has(kCalendar) || !g.memory.has(kCalendar + kCalendarDays * 2 - 1))
        throw std::invalid_argument("league dates: the season calendar is not loaded");
    const uint16_t bit = uint16_t(1u << calendar_bit);
    const auto entry = [&](int day) { return g.memory.u16(kCalendar + uint32_t(day) * 2); };
    const auto set = [&](int day, uint16_t v) { g.memory.set16(kCalendar + uint32_t(day) * 2, v); };
    for (int day = 0; day < kCalendarDays; ++day)
        if (!(entry(day) & kSpecialDay)) set(day, uint16_t(entry(day) & ~bit));
    std::vector<int> days = season_days;
    std::sort(days.begin(), days.end());
    LeagueDatesResult result;
    for (const int wanted : days) {
        if (wanted < 0 || wanted >= kCalendarDays) throw std::invalid_argument("league dates: date outside the season");
        int day = wanted;
        while (day < kCalendarDays && (entry(day) & (kSpecialDay | bit))) ++day;
        if (day == kCalendarDays) throw std::invalid_argument("league dates: no free day left in the season");
        if (day != wanted)
            result.moved.push_back("season day " + std::to_string(wanted) + " -> " + std::to_string(day));
        set(day, uint16_t(entry(day) | bit));
        ++result.dates;
    }
    return result;
}

}  // namespace bmh::modern
