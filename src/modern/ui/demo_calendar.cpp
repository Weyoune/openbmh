// SPDX-License-Identifier: GPL-3.0-only
#include "modern/ui/demo_calendar.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace bmh::modern_ui {
std::vector<CalendarDay> calendar_days(const std::string &start) {
  using namespace std::chrono;
  if (start.size() != 10 || start.substr(4) != "-07-01" ||
      !std::all_of(start.begin(), start.begin() + 4,
                   [](char c) { return c >= '0' && c <= '9'; }))
    return {};
  const int y = std::stoi(start.substr(0, 4));
  if (y < 1 || y > 9998)
    return {};
  const sys_days first = year{y} / July / 1, last = year{y + 1} / July / 1;
  std::vector<CalendarDay> result;
  for (auto date = first; date < last; date += days{1}) {
    const year_month_day parts{date};
    const int month = unsigned(parts.month()), day = unsigned(parts.day());
    std::ostringstream iso;
    iso.imbue(std::locale::classic());
    iso << std::setfill('0') << std::setw(4) << int(parts.year()) << '-'
        << std::setw(2) << month << '-' << std::setw(2) << day;
    result.push_back({iso.str(), int(parts.year()), month, day,
                       int((weekday{date}.c_encoding() + 6) % 7)});
  }
  return result;
}
Json demo_calendar(const Catalog &world, const std::string &date) {
  auto year = std::stoi(date.substr(0, 4));
  if (date.substr(5, 2) < "07")
    --year;
  const auto leagues = world.leagues("nation:de");
  std::ostringstream start;
  start.imbue(std::locale::classic());
  start << std::setfill('0') << std::setw(4) << year << "-07-01";
  if (calendar_days(start.str()).empty())
    throw editor::Error("invalid_demo_calendar");
  return {{"start", start.str()},
          {"selected", 6},
          {"league", leagues.empty() ? "" : leagues.front()->value("id", "")},
          {"friendlies", Json::array()},
          {"moves", Json::array()}};
}
bool valid_demo_calendar(const Json &v, const Catalog &world) {
  try {
    const auto days = calendar_days(v.at("start").get<std::string>());
    if (days.empty() || !v.at("selected").is_number_integer() ||
        v["selected"] < 0 || v["selected"] >= days.size())
      return false;
    const auto league = v.at("league").get<std::string>();
    const auto leagues = world.leagues();
    if (std::none_of(leagues.begin(), leagues.end(), [&](auto *r) {
          return r->value("id", "") == league;
        }))
      return false;
    // Reserved projections for later native planning menus; no unimplemented
    // booking or rescheduling action is silently accepted by this preview.
    return v.at("friendlies").is_array() && v["friendlies"].empty() &&
           v.at("moves").is_array() && v["moves"].empty();
  } catch (const Json::exception &) {
    return false;
  }
}
std::string edit_demo_calendar(Json &state, const Json &action,
                                const Catalog &world) {
  const auto kind = action.at("kind").get<std::string>();
  auto next = state;
  if (kind == "select")
    next["selected"] = action.at("value");
  else if (kind == "league")
    next["league"] = action.at("value");
  else
    return "unknown_calendar_action";
  if (!valid_demo_calendar(next, world))
    return "invalid_calendar_selection";
  state = std::move(next);
  return {};
}
std::vector<CalendarFixture> calendar_fixtures(const Json &state,
                                              const Catalog &world,
                                              const std::string &managed_club) {
  const auto clubs = world.clubs(state.at("league").get<std::string>());
  if (clubs.size() < 2)
    return {};
  const auto found = std::find_if(clubs.begin(), clubs.end(), [&](auto *c) {
    return c->value("id", "") == managed_club;
  });
  const int followed = found == clubs.end() ? 0 : int(found - clubs.begin());
  const int count = int(clubs.size()) + int(clubs.size() % 2);
  std::vector<int> ring(size_t(count), 0);
  std::iota(ring.begin(), ring.end(), 0);
  std::vector<CalendarFixture> first, result;
  for (int round = 0; round < count - 1; ++round) {
    for (int pair = 0; pair < count / 2; ++pair) {
      auto home = ring[size_t(pair)], away = ring[size_t(count - 1 - pair)];
      if ((pair == 0 ? round : pair) % 2)
        std::swap(home, away);
      if ((home != followed && away != followed) ||
          home >= int(clubs.size()) || away >= int(clubs.size()))
        continue;
      first.push_back({30 + round * 7, round,
                         clubs[size_t(home)]->value("id", ""),
                         clubs[size_t(away)]->value("id", "")});
    }
    std::rotate(ring.begin() + 1, ring.end() - 1, ring.end());
  }
  result = first;
  for (const auto &fixture : first)
    result.push_back({fixture.day + (count - 1) * 7,
                        fixture.round + count - 1, fixture.away, fixture.home});
  const auto days = calendar_days(state.at("start").get<std::string>()).size();
  std::erase_if(result, [&](const auto &f) { return size_t(f.day) >= days; });
  return result;
}
} // namespace bmh::modern_ui
