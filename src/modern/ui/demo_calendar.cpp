// SPDX-License-Identifier: GPL-3.0-only
#include "modern/ui/demo_calendar.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <set>

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
namespace {
bool integer(const Json &v, int low, int high) {
  if (v.is_number_unsigned())
    return high >= 0 && v.get<uint64_t>() <= uint64_t(high) &&
           (low <= 0 || v.get<uint64_t>() >= uint64_t(low));
  return v.is_number_integer() && v.get<int64_t>() >= low &&
         v.get<int64_t>() <= high;
}
bool keys(const Json &v, std::initializer_list<const char *> expected) {
  if (!v.is_object() || v.size() != expected.size()) return false;
  return std::all_of(expected.begin(), expected.end(),
                     [&](const auto *key) { return v.contains(key); });
}
std::string own_club(const Catalog &world, const std::string &managed) {
  if (!managed.empty()) return world.manageable(managed) ? managed : "";
  for (const auto *club : world.clubs())
    if (world.manageable(club->value("id", ""))) return club->value("id", "");
  return "";
}
bool league_exists(const Catalog &world, const std::string &id) {
  const auto leagues = world.leagues();
  return leagues.empty() ? id.empty() : std::any_of(
      leagues.begin(), leagues.end(), [&](auto *l) { return l->at("id") == id; });
}
std::string first_opponent(const Catalog &world, const std::string &league,
                           const std::string &managed) {
  if (league.empty()) return "";
  for (auto *club : world.clubs(league))
    if (club->at("id") != managed) return club->at("id").get<std::string>();
  return "";
}
bool opponent_exists(const Catalog &world, const std::string &league,
                     const std::string &opponent, const std::string &managed) {
  if (!league_exists(world, league)) return false;
  if (opponent.empty()) return first_opponent(world, league, managed).empty();
  const auto *club = world.find("clubs", opponent);
  return club && opponent != managed && club->value("type", "club") == "club" &&
         !league.empty() && club->value("competition", "") == league;
}
Json own_calendar(const Json &state, const Catalog &world,
                  const std::string &managed) {
  auto own = state;
  own["league"] = calendar_own_league(world, managed);
  return own;
}
} // namespace
std::string calendar_own_league(const Catalog &world, const std::string &managed) {
  const auto *club = world.find("clubs", own_club(world, managed));
  return club ? club->value("competition", "") : "";
}
Json demo_calendar(const Catalog &world, const std::string &date,
                   const std::string &managed) {
  if (date.size() != 10 || date[4] != '-' || date[7] != '-' ||
      !std::all_of(date.begin(), date.end(), [](char c) {
        return (c >= '0' && c <= '9') || c == '-';
      })) throw editor::Error("invalid_demo_calendar");
  int year;
  try { year = std::stoi(date.substr(0, 4)); }
  catch (const std::exception &) { throw editor::Error("invalid_demo_calendar"); }
  if (date.substr(5, 2) < "07") --year;
  std::ostringstream start;
  start.imbue(std::locale::classic());
  start << std::setfill('0') << std::setw(4) << year << "-07-01";
  const auto days = calendar_days(start.str());
  if (days.empty() || std::none_of(days.begin(), days.end(),
        [&](const auto &day) { return day.iso == date; }))
    throw editor::Error("invalid_demo_calendar");
  auto leagues = world.leagues("nation:de");
  if (leagues.empty()) leagues = world.leagues();
  const auto preview = leagues.empty() ? "" : leagues.front()->value("id", "");
  const auto own = calendar_own_league(world, managed);
  const auto friendly = own.empty() ? preview : own;
  Json result = {{"start", start.str()}, {"selected", 6}, {"league", preview},
      {"source_day", -1}, {"friendly_league", friendly},
      {"opponent", first_opponent(world, friendly, own_club(world, managed))},
      {"home", true}, {"friendly_stage", 0},
      {"friendlies", Json::array()}, {"moves", Json::array()}};
  if (!valid_demo_calendar(result, world, managed))
    throw editor::Error("invalid_demo_calendar");
  return result;
}
bool valid_demo_calendar(const Json &v, const Catalog &world,
                         const std::string &managed) {
  try {
    if (!keys(v, {"start", "selected", "league", "source_day", "friendly_league",
                  "opponent", "home", "friendly_stage", "friendlies", "moves"}) ||
        (!managed.empty() && !world.manageable(managed))) return false;
    const auto days = calendar_days(v.at("start").get<std::string>());
    if (days.empty() || !integer(v.at("selected"), 0, int(days.size()) - 1) ||
        !league_exists(world, v.at("league").get<std::string>()) ||
        !v.at("home").is_boolean() || !integer(v.at("friendly_stage"), 0, 1) ||
        !opponent_exists(world, v.at("friendly_league").get<std::string>(),
                         v.at("opponent").get<std::string>(), own_club(world, managed)) ||
        !v.at("friendlies").is_array() || v.at("friendlies").size() > 20 ||
        (!v.at("friendlies").empty() && own_club(world, managed).empty()) ||
        !v.at("moves").is_array()) return false;
    auto base = own_calendar(v, world, managed);
    base["moves"] = Json::array();
    const auto fixtures = calendar_fixtures(base, world, own_club(world, managed));
    std::set<int> sources, destinations, occupied, originals;
    for (const auto &f : fixtures) originals.insert(f.from);
    if (!integer(v.at("source_day"), -1, int(days.size()) - 1) ||
        (v.at("source_day") != -1 && !originals.contains(v.at("source_day").get<int>())))
      return false;
    for (const auto &m : v.at("moves")) {
      if (!keys(m, {"from", "to"}) || !integer(m.at("from"), 0, int(days.size()) - 1) ||
          !integer(m.at("to"), 7, int(days.size()) - 1)) return false;
      const int from = m.at("from"), to = m.at("to");
      if (!originals.contains(from) || !sources.insert(from).second ||
          !destinations.insert(to).second) return false;
    }
    for (const auto &f : calendar_fixtures(own_calendar(v, world, managed), world,
                                            own_club(world, managed)))
      if (!occupied.insert(f.day).second) return false;
    for (const auto &f : v.at("friendlies")) {
      if (!keys(f, {"day", "opponent", "league", "home"}) ||
          !integer(f.at("day"), 7, int(days.size()) - 1) ||
          !f.at("home").is_boolean() || f.at("opponent") == "" ||
          !opponent_exists(world, f.at("league").get<std::string>(),
                           f.at("opponent").get<std::string>(), own_club(world, managed)) ||
          !occupied.insert(f.at("day").get<int>()).second) return false;
    }
    return true;
  } catch (const Json::exception &) { return false; }
}
bool migrate_demo_calendar(Json &state, const Catalog &world,
                           const std::string &managed) {
  try {
    if (!state.is_object()) return false;
    // OpenBMH deviates from the original: the native season follows its saved
    // start year (including leap days), rather than the HTML's fixed 2027/28.
    const auto defaults = demo_calendar(world, state.at("start").get<std::string>(), managed);
    auto next = state;
    for (const auto *field : {"source_day", "friendly_league", "opponent", "home",
                               "friendly_stage", "friendlies", "moves"})
      if (!next.contains(field)) next[field] = defaults.at(field);
    if (!state.contains("opponent"))
      next["opponent"] = first_opponent(world, next.at("friendly_league").get<std::string>(),
                                        own_club(world, managed));
    if (!valid_demo_calendar(next, world, managed)) return false;
    state = std::move(next);
    return true;
  } catch (const std::exception &) { return false; }
}
bool calendar_day_occupied(const Json &state, const Catalog &world,
                           const std::string &managed, int day) {
  for (const auto &f : calendar_fixtures(own_calendar(state, world, managed), world,
                                          own_club(world, managed)))
    if (f.day == day) return true;
  for (const auto &f : state.at("friendlies"))
    if (f.at("day") == day) return true;
  return false;
}
int calendar_friendly_fee(const Json &state, const Catalog &world) {
  const auto clubs = world.clubs(state.at("friendly_league").get<std::string>());
  const auto found = std::find_if(clubs.begin(), clubs.end(),
      [&](auto *club) { return club->at("id") == state.at("opponent"); });
  if (found == clubs.end()) return 0;
  return (state.at("home").get<bool>() ? 25000 : 12000) +
         int(found - clubs.begin()) * 500;
}
std::string edit_demo_calendar(Json &state, const Json &action,
                               const Catalog &world, const std::string &managed) {
  if (!valid_demo_calendar(state, world, managed)) return "invalid_calendar_state";
  try {
    if (!action.is_object() || !action.contains("kind") || !action.at("kind").is_string())
      return "invalid_calendar_action";
    const auto kind = action.at("kind").get<std::string>();
    const bool value = kind == "select" || kind == "league" ||
        kind == "friendly-country" || kind == "friendly-league" ||
        kind == "friendly-opponent" || kind == "friendly-venue" || kind == "friendly-stage";
    const bool command = kind == "friendly-book" || kind == "postpone-source" ||
        kind == "postpone-cancel" || kind == "postpone-move" || kind == "postpone-undo";
    if (!value && !command) return "unknown_calendar_action";
    if (!(value ? keys(action, {"kind", "value"}) : keys(action, {"kind"})))
      return "invalid_calendar_action";
    auto next = state;
    if (kind == "select") next["selected"] = action.at("value");
    else if (kind == "league") next["league"] = action.at("value");
    else if (kind == "friendly-country" || kind == "friendly-league") {
      const auto id = action.at("value").get<std::string>();
      auto league = id;
      if (kind == "friendly-country") {
        if (!world.find("nations", id)) return "invalid_calendar_selection";
        const auto leagues = world.leagues(id);
        if (leagues.empty()) return "invalid_calendar_selection";
        league = leagues.front()->at("id").get<std::string>();
      }
      if (!league_exists(world, league)) return "invalid_calendar_selection";
      next["friendly_league"] = league;
      next["opponent"] = first_opponent(world, league, own_club(world, managed));
    } else if (kind == "friendly-opponent") next["opponent"] = action.at("value");
    else if (kind == "friendly-venue") next["home"] = action.at("value");
    else if (kind == "friendly-stage") next["friendly_stage"] = action.at("value");
    else if (kind == "friendly-book" || kind == "postpone-move") {
      const int day = state.at("selected");
      if (day < 7 || calendar_day_occupied(state, world, managed, day))
        return "invalid_calendar_selection";
      if (kind == "friendly-book") {
        if (state.at("friendlies").size() >= 20 || state.at("opponent") == "" ||
            own_club(world, managed).empty()) return "invalid_calendar_selection";
        next["friendlies"].push_back({{"day", day}, {"opponent", state.at("opponent")},
            {"league", state.at("friendly_league")}, {"home", state.at("home")}});
      } else {
        const int from = state.at("source_day");
        if (from < 0) return "invalid_calendar_selection";
        auto &moves = next["moves"];
        moves.erase(std::remove_if(moves.begin(), moves.end(),
                    [&](const auto &m) { return m.at("from") == from; }), moves.end());
        moves.push_back({{"from", from}, {"to", day}});
        next["source_day"] = -1;
      }
    } else if (kind == "postpone-source") {
      const auto fixtures = calendar_fixtures(own_calendar(state, world, managed), world,
                                               own_club(world, managed));
      const auto found = std::find_if(fixtures.begin(), fixtures.end(),
          [&](const auto &f) { return state.at("selected") == f.day; });
      if (found == fixtures.end()) return "invalid_calendar_selection";
      next["source_day"] = found->from;
    } else if (kind == "postpone-cancel") next["source_day"] = -1;
    else if (kind == "postpone-undo") {
      if (state.at("moves").empty() || calendar_day_occupied(state, world, managed,
            state.at("moves").back().at("from").get<int>()))
        return "invalid_calendar_selection";
      next["moves"].erase(next["moves"].end() - 1);
    }
    if (!valid_demo_calendar(next, world, managed)) return "invalid_calendar_selection";
    state = std::move(next);
    return {};
  } catch (const Json::exception &) { return "invalid_calendar_action"; }
}
std::vector<CalendarFixture> calendar_fixtures(const Json &state,
                                              const Catalog &world,
                                              const std::string &managed_club) {
  const auto league = state.at("league").get<std::string>();
  if (league.empty()) return {};
  const auto clubs = world.clubs(league);
  if (clubs.size() < 2)
    return {};
  const auto managed = own_club(world, managed_club);
  const auto found = std::find_if(clubs.begin(), clubs.end(), [&](auto *c) {
    return c->value("id", "") == managed;
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
                         clubs[size_t(away)]->value("id", ""), 30 + round * 7});
    }
    std::rotate(ring.begin() + 1, ring.end() - 1, ring.end());
  }
  result = first;
  for (const auto &fixture : first)
    result.push_back({fixture.day + (count - 1) * 7,
                        fixture.round + count - 1, fixture.away, fixture.home,
                        fixture.from + (count - 1) * 7});
  const auto days = calendar_days(state.at("start").get<std::string>()).size();
  std::erase_if(result, [&](const auto &f) { return size_t(f.day) >= days; });
  // Foreign league previews keep their original dates and followed club.
  if (found != clubs.end() && league == calendar_own_league(world, managed_club))
    for (auto &fixture : result)
      for (const auto &move : state.at("moves"))
        if (move.at("from") == fixture.from) {
          fixture.day = move.at("to").get<int>();
          break;
        }
  return result;
}
} // namespace bmh::modern_ui
