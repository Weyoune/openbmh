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
  int from; // Stable original season day, even after repeated moves.
};
// Presentation fixtures only. No scheduler, RNG, session or match is called.
std::vector<CalendarDay> calendar_days(const std::string &start);
Json demo_calendar(const Catalog &, const std::string &date,
                   const std::string &managed_club = "");
bool valid_demo_calendar(const Json &, const Catalog &,
                         const std::string &managed_club = "");
std::string edit_demo_calendar(Json &, const Json &, const Catalog &,
                               const std::string &managed_club = "");
// Fill absent planning fields in older presentation checkpoints, atomically.
// Present values are validated, never repaired or silently discarded.
bool migrate_demo_calendar(Json &, const Catalog &,
                           const std::string &managed_club = "");
std::string calendar_own_league(const Catalog &,
                               const std::string &managed_club = "");
// Planning always checks the manager's schedule, independently of the preview.
bool calendar_day_occupied(const Json &, const Catalog &,
                           const std::string &managed_club, int day);
int calendar_friendly_fee(const Json &, const Catalog &);
std::vector<CalendarFixture> calendar_fixtures(const Json &, const Catalog &,
                                              const std::string &managed_club);
} // namespace bmh::modern_ui
