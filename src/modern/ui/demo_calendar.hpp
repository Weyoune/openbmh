// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "modern/ui/catalog.hpp"

namespace bmh::modern_ui {
struct CalendarDay {
  std::string iso;
  int year, month, day, weekday;
};
struct CalendarFixture {
  int day, round;
  std::string home, away;
};
// Presentation fixtures only. No scheduler, RNG, session or match is called.
std::vector<CalendarDay> calendar_days(const std::string &start);
Json demo_calendar(const Catalog &, const std::string &date);
bool valid_demo_calendar(const Json &, const Catalog &);
std::string edit_demo_calendar(Json &, const Json &, const Catalog &);
std::vector<CalendarFixture> calendar_fixtures(const Json &, const Catalog &,
                                              const std::string &managed_club);
} // namespace bmh::modern_ui
