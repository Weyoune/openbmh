// SPDX-License-Identifier: GPL-3.0-only
#include "modern/ui/demo_almanac.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace bmh::modern_ui {
namespace {
bool keys(const Json &v, std::initializer_list<const char *> required,
          std::initializer_list<const char *> optional = {}) {
  if (!v.is_object()) return false;
  std::set<std::string> allowed;
  for (auto *key : required) {
    if (!v.contains(key)) return false;
    allowed.emplace(key);
  }
  for (auto *key : optional) allowed.emplace(key);
  for (const auto &entry : v.items())
    if (!allowed.contains(entry.key())) return false;
  return true;
}
bool integer(const Json &v, int lo, int hi) {
  if (v.is_number_unsigned())
    return hi >= 0 && v.get<uint64_t>() <= uint64_t(hi) &&
           (lo <= 0 || v.get<uint64_t>() >= uint64_t(lo));
  return v.is_number_integer() && v.get<int64_t>() >= lo && v.get<int64_t>() <= hi;
}
bool text(const Json &v, bool nonempty = true) {
  return v.is_string() && (!nonempty || !v.get_ref<const std::string &>().empty());
}
bool date(const std::string &iso) {
  if (iso.size() != 10 || iso[4] != '-' || iso[7] != '-') return false;
  for (size_t i = 0; i < iso.size(); ++i)
    if (i != 4 && i != 7 && (iso[i] < '0' || iso[i] > '9')) return false;
  const auto y = std::stoi(iso.substr(0, 4)), m = std::stoi(iso.substr(5, 2)),
             d = std::stoi(iso.substr(8, 2));
  return y >= 1 && std::chrono::year_month_day{std::chrono::year{y},
      std::chrono::month{unsigned(m)}, std::chrono::day{unsigned(d)}}.ok();
}
std::string iso(int year, int month, int day) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setfill('0') << std::setw(4) << year << '-' << std::setw(2)
      << month << '-' << std::setw(2) << day;
  return out.str();
}
std::string season(int year) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setfill('0') << std::setw(4) << year << '/' << std::setw(2)
      << (year + 1) % 100;
  return out.str();
}
bool person(const Json &p, bool position = false) {
  return keys(p, {"id", "name"}, position
      ? std::initializer_list<const char *>{"position"}
      : std::initializer_list<const char *>{}) && text(p.at("id")) &&
      text(p.at("name"), false) && (!p.contains("position") || text(p.at("position")));
}
bool score(const Json &v) {
  return v.is_array() && v.size() == 2 && integer(v[0], 0, 99) && integer(v[1], 0, 99);
}
std::string position(const Json &p) {
  const auto found = p.find("positions");
  if (found == p.end() || !found->is_array()) return "";
  if (std::find(found->begin(), found->end(), "gk") != found->end()) return "gk";
  for (const auto &v : *found)
    if (text(v)) return v.get<std::string>();
  return "";
}
Json snapshot_player(const Catalog &world, const Json &p) {
  Json result = {{"id", p.at("id")}, {"name", world.name(p)}};
  const auto role = position(p);
  if (!role.empty()) result["position"] = role;
  return result;
}
Json lineup(const Catalog &world, const std::string &club) {
  const auto players = world.squad(club);
  const Json *keeper = nullptr;
  std::vector<const Json *> outfield;
  for (auto *p : players) {
    if (position(*p) == "gk") {
      if (!keeper) keeper = p;
    } else outfield.push_back(p);
  }
  // A partial squad is missing information, never an invented starting XI.
  if (!keeper || outfield.size() < 10) return nullptr;
  Json starters = Json::array({snapshot_player(world, *keeper)}), bench = Json::array();
  std::set<std::string> selected{keeper->at("id").get<std::string>()};
  for (size_t i = 0; i < 10; ++i) {
    starters.push_back(snapshot_player(world, *outfield[i]));
    selected.emplace(outfield[i]->at("id").get<std::string>());
  }
  for (auto *p : players)
    if (!selected.contains(p->at("id").get<std::string>()))
      bench.push_back(snapshot_player(world, *p));
  return {{"starters", starters}, {"bench", bench}};
}
Json actor(const Json &team, int index, bool bench = false) {
  if (team.is_null()) return nullptr;
  const auto &players = team.at(bench ? "bench" : "starters");
  if (index < 0 || size_t(index) >= players.size()) return nullptr;
  const auto &p = players.at(size_t(index));
  return {{"id", p.at("id")}, {"name", p.at("name")}};
}
Json club_snapshot(const Catalog &world, const Json &club, int manager_index = -1) {
  Json manager = nullptr;
  if (manager_index >= 0)
    manager = {{"id", "demo:almanac:manager:" + std::to_string(manager_index)},
               {"name", "Demo-Manager " + std::to_string(manager_index + 1)}};
  Json crest = nullptr;
  if (club.contains("crest") && text(club.at("crest"))) crest = club.at("crest");
  return {{"id", club.at("id")}, {"name", world.name(club)},
          {"crest", crest}, {"manager", manager}};
}
Json competition(const Catalog &world, const Json &home, const Json &away, bool cup) {
  if (!cup && home.value("competition", "") == away.value("competition", "")) {
    const auto *league = world.find("competitions", home.value("competition", ""));
    if (league && league->value("type", "") == "league")
      return {{"id", league->at("id")}, {"name", world.name(*league)}, {"type", "league"}};
  }
  if (cup)
    for (const auto &c : world.rows("competitions"))
      if (c.value("type", "") == "cup")
        return {{"id", c.at("id")}, {"name", world.name(c)}, {"type", "cup"}};
  return {{"id", cup ? "demo:competition:cup" : "demo:competition:friendly"},
          {"name", cup ? "Demo-Pokal" : "Demo-Freundschaftsspiel"},
          {"type", cup ? "cup" : "friendly"}};
}
Json make_match(const Catalog &world, const Json &home, const Json &away,
                int year, int ordinal) {
  const std::array<std::pair<int, int>, 6> scores{{{3, 1}, {0, 0}, {2, 0},
                                                {2, 1}, {2, 2}, {1, 2}}};
  const auto [hg, ag] = scores[size_t(ordinal)];
  const bool extra = ordinal == 3 || ordinal == 4, shootout = ordinal == 4;
  const auto day = ordinal < 3 ? iso(year, 8, 3) : ordinal == 3 ? iso(year, 12, 14)
      : ordinal == 4 ? iso(year + 1, 2, 20) : iso(year + 1, 5, 18);
  Json rosters = {{"home", lineup(world, home.at("id"))},
                  {"away", lineup(world, away.at("id"))}};
  // Historical recording can be incomplete even when the current world has a squad.
  if (ordinal == 5) rosters["away"] = nullptr;
  Json events = Json::array();
  const auto event = [&](const char *kind, int minute, const char *side, int index,
                         int detail = 0, Json other = nullptr, bool bench = false) {
    events.push_back({{"kind", kind}, {"minute", minute}, {"side", side},
        {"player", actor(rosters.at(side), index, bench)}, {"other", std::move(other)},
        {"detail", detail}});
  };
  if (ordinal == 0) {
    event("miss", 6, "home", 1, 0);
    event("goal", 9, "home", 1, 0, actor(rosters.at("home"), 3));
    event("goal", 18, "away", 1, 1);
    event("yellow", 23, "home", 2);
    event("miss", 25, "away", 3, 1);
    event("miss", 41, "home", 4, 2);
    event("injury", 49, "home", 5);
    if (!rosters.at("home").is_null() && !rosters["home"]["bench"].empty())
      event("substitution", 50, "home", 0, 0, actor(rosters.at("home"), 5), true);
    else if (rosters.at("home").is_null())
      event("substitution", 50, "home", -1);
    event("goal", 61, "home", 2, 2);
    event("red", 67, "away", 4);
    event("second_yellow", 72, "home", 2);
    event("miss", 79, "away", 6, 3);
    event("goal", 82, "home", 3, 3);
  } else if (ordinal == 1) {
    event("miss", 27, "home", 1, 1);
    event("yellow", 54, "away", 3);
  } else if (ordinal == 2) {
    event("goal", 29, "home", 2, 1);
    event("goal", 76, "home", 3, 0);
  } else if (extra) {
    event("goal", 19, "home", 1, 0);
    event("goal", 58, "away", 1, 3);
    event("goal", 103, "home", 2, 2);
    if (shootout) {
      event("goal", 117, "away", 2, 1);
      const std::array<int, 5> h{1, 1, 0, 1, 1}, a{1, 1, 0, 1, 0};
      for (int i = 0; i < 5; ++i) {
        event("shootout", 121, "home", i + 1, h[size_t(i)]);
        event("shootout", 121, "away", i + 1, a[size_t(i)]);
      }
    }
  } else {
    event("goal", 12, "away", 1, 0);
    event("goal", 43, "home", 1, 3);
    event("goal", 88, "away", 2, 2);
  }
  return {{"id", "demo:almanac:" + std::to_string(year) + ":" + std::to_string(ordinal)},
      {"date", day}, {"season", season(year)},
      {"competition", competition(world, home, away, extra)},
      {"round", ordinal + 1}, {"home", club_snapshot(world, home, ordinal == 0 ? 0 : -1)},
      {"away", club_snapshot(world, away, ordinal == 0 ? 1 : -1)},
      {"result", {{"regulation", extra ? Json::array({1, 1}) : Json::array({hg, ag})},
          {"extra", extra ? Json::array({hg - 1, ag - 1}) : Json(nullptr)},
          {"penalties", shootout ? Json::array({4, 3}) : Json(nullptr)},
          {"final", {hg, ag}}, {"extra_played", extra}, {"penalties_played", shootout}}},
      {"referee", ordinal == 5 ? Json(nullptr) : Json("Demo-Schiedsrichter " + std::to_string(ordinal % 3 + 1))},
      {"attendance", ordinal == 5 ? Json(nullptr) : Json(ordinal == 1 ? 0 : 14500 + ordinal * 3275)},
      {"weather", {{"temperature", ordinal == 5 ? Json(nullptr) : Json(ordinal == 1 ? 0 : ordinal == 4 ? 3 : 16 + ordinal)},
                   {"rain", ordinal == 5 ? Json(nullptr) : Json(ordinal == 1 ? 0 : ordinal % 8)}}},
      {"lineups", rosters}, {"events", events}};
}
bool valid_club(const Json &club) {
  return keys(club, {"id", "name", "crest", "manager"}) && text(club.at("id")) &&
      text(club.at("name"), false) && (club.at("crest").is_null() || text(club.at("crest"))) &&
      (club.at("manager").is_null() || person(club.at("manager")));
}
struct Roster {
  bool known = false;
  std::map<std::string, std::string> names;
  std::set<std::string> active, bench, yellow, dismissed;
};
bool valid_roster(const Json &value, Roster &out) {
  if (value.is_null()) return true;
  if (!keys(value, {"starters", "bench"}) || !value.at("starters").is_array() ||
      value.at("starters").size() != 11 || !value.at("bench").is_array()) return false;
  out.known = true;
  int keepers = 0, unknown_positions = 0;
  for (const auto *key : {"starters", "bench"})
    for (const auto &p : value.at(key)) {
      if (!person(p, true)) return false;
      const auto id = p.at("id").get<std::string>();
      if (!out.names.emplace(id, p.at("name").get<std::string>()).second) return false;
      if (std::string(key) == "starters") {
        out.active.insert(id);
        if (!p.contains("position")) ++unknown_positions;
        if (p.value("position", "") == "gk") ++keepers;
      } else out.bench.insert(id);
    }
  return keepers == 1 || (keepers == 0 && unknown_positions > 0);
}
bool valid_match(const Json &m) {
  if (!keys(m, {"id", "date", "season", "competition", "round", "home", "away",
                "result", "referee", "attendance", "weather", "lineups", "events"}) ||
      !text(m.at("id")) || !text(m.at("date")) || !date(m.at("date").get<std::string>()) ||
      !text(m.at("season")) || !integer(m.at("round"), 1, 999) ||
      !valid_club(m.at("home")) || !valid_club(m.at("away")) ||
      m.at("home").at("id") == m.at("away").at("id")) return false;
  const auto when = m.at("date").get<std::string>();
  const int year = std::stoi(when.substr(0, 4)) - (when.substr(5, 2) < "07" ? 1 : 0);
  if (year < 1 || m.at("season") != season(year)) return false;
  const auto &c = m.at("competition"), &r = m.at("result"), &w = m.at("weather");
  if (!keys(c, {"id", "name", "type"}) || !text(c.at("id")) || !text(c.at("name"), false) ||
      !text(c.at("type")) ||
      !keys(r, {"regulation", "extra", "penalties", "final", "extra_played", "penalties_played"}) ||
      !score(r.at("final")) || !r.at("extra_played").is_boolean() ||
      !r.at("penalties_played").is_boolean() ||
      !(r.at("regulation").is_null() || score(r.at("regulation"))) ||
      !(r.at("extra").is_null() || score(r.at("extra"))) ||
      !(r.at("penalties").is_null() || score(r.at("penalties"))) ||
      !(m.at("referee").is_null() || text(m.at("referee"))) ||
      !(m.at("attendance").is_null() || integer(m.at("attendance"), 0, 1000000)) ||
      !keys(w, {"temperature", "rain"}) ||
      !(w.at("temperature").is_null() || integer(w.at("temperature"), -100, 100)) ||
      !(w.at("rain").is_null() || integer(w.at("rain"), 0, 7))) return false;
  const bool extra = r.at("extra_played"), penalties = r.at("penalties_played");
  if ((!extra && !r.at("extra").is_null()) ||
      (!extra && !r.at("regulation").is_null() && r.at("regulation") != r.at("final")) ||
      (extra && !r.at("regulation").is_null() && r.at("regulation")[0] != r.at("regulation")[1]) ||
      (penalties != !r.at("penalties").is_null()) ||
      (penalties && (r.at("final")[0] != r.at("final")[1] ||
                     r.at("penalties")[0] == r.at("penalties")[1]))) return false;
  if (!keys(m.at("lineups"), {"home", "away"}) || !m.at("events").is_array()) return false;
  std::array<Roster, 2> rosters;
  if (!valid_roster(m.at("lineups").at("home"), rosters[0]) ||
      !valid_roster(m.at("lineups").at("away"), rosters[1])) return false;
  for (const auto &[id, name] : rosters[0].names) {
    (void)name;
    if (rosters[1].names.contains(id)) return false;
  }
  std::array<int, 2> goals{}, regular{}, added{}, kicks{}, taken{};
  int last_minute = 0, kick_count = 0, first_kicker = -1;
  bool decided = false;
  for (const auto &e : m.at("events")) {
    if (!keys(e, {"kind", "minute", "side", "player", "other", "detail"}) ||
        !text(e.at("kind")) || !integer(e.at("minute"), 1, 121) ||
        !(e.at("side") == "home" || e.at("side") == "away") ||
        !(e.at("player").is_null() || person(e.at("player"))) ||
        !(e.at("other").is_null() || person(e.at("other")))) return false;
    const auto kind = e.at("kind").get<std::string>();
    const bool kick = kind == "shootout", chance = kind == "goal" || kind == "miss";
    if (!(chance || kick || kind == "yellow" || kind == "second_yellow" || kind == "red" ||
          kind == "injury" || kind == "substitution") ||
        !integer(e.at("detail"), 0, kick ? 1 : chance ? 3 : 0)) return false;
    const int minute = e.at("minute"), side = e.at("side") == "home" ? 0 : 1;
    if (minute < last_minute || (kick ? (!penalties || minute != 121) : minute > (extra ? 120 : 90)))
      return false;
    last_minute = minute;
    auto &roster = rosters[size_t(side)];
    for (const auto *key : {"player", "other"}) {
      const auto &p = e.at(key);
      if (!p.is_null() && roster.known) {
        const auto found = roster.names.find(p.at("id").get<std::string>());
        if (found == roster.names.end() || found->second != p.at("name").get<std::string>())
          return false;
      }
    }
    const auto player = e.at("player").is_null() ? "" : e.at("player").at("id").get<std::string>();
    const auto other = e.at("other").is_null() ? "" : e.at("other").at("id").get<std::string>();
    if (!other.empty() && !chance && kind != "substitution") return false;
    if (roster.known) {
      if (kind == "substitution") {
        if (!player.empty() && other.empty() &&
            roster.active.size() >= 11 - roster.dismissed.size()) return false;
        if (!player.empty() && !roster.bench.erase(player)) return false;
        if (!other.empty() && !roster.active.erase(other)) return false;
        if (!player.empty()) roster.active.insert(player);
      } else if ((!player.empty() && !roster.active.contains(player)) ||
                 (!other.empty() && (!roster.active.contains(other) || player == other))) return false;
      if (kind == "yellow" && !player.empty()) {
        if (!roster.yellow.insert(player).second) return false;
      } else if ((kind == "red" || kind == "second_yellow") && !player.empty()) {
        if (kind == "second_yellow" && !roster.yellow.contains(player)) return false;
        roster.active.erase(player);
        roster.dismissed.insert(player);
      }
    }
    if (kind == "goal") {
      ++goals[size_t(side)];
      if (minute <= 90) ++regular[size_t(side)];
      else ++added[size_t(side)];
    } else if (kick) {
      if (first_kicker < 0) first_kicker = side;
      if (decided || side != (first_kicker + kick_count) % 2) return false;
      ++kick_count;
      ++taken[size_t(side)];
      kicks[size_t(side)] += e.at("detail").get<int>();
      if (taken[0] <= 5 && taken[1] <= 5)
        decided = kicks[0] > kicks[1] + 5 - taken[1] || kicks[1] > kicks[0] + 5 - taken[0];
      else if (taken[0] == taken[1]) decided = kicks[0] != kicks[1];
    }
  }
  for (int side = 0; side < 2; ++side) {
    if (r.at("final")[size_t(side)] != goals[size_t(side)] ||
        (!r.at("regulation").is_null() && r.at("regulation")[size_t(side)] != regular[size_t(side)]) ||
        (!r.at("extra").is_null() && r.at("extra")[size_t(side)] != added[size_t(side)]) ||
        (penalties && r.at("penalties")[size_t(side)] != kicks[size_t(side)])) return false;
  }
  return penalties ? decided : kick_count == 0;
}
Json error(const char *message) { return {{"ok", false}, {"error", message}}; }
} // namespace

Json make_demo_almanac(const Catalog &world) {
  Json archive = {{"schema", "openbmh-ui-demo-almanac@1"}, {"matches", Json::array()}};
  const auto clubs = world.clubs();
  if (clubs.size() < 2) return archive;
  const auto start = world.manifest().value("start_date", Json("2026-07-01"));
  if (!text(start) || !date(start.get<std::string>())) return archive;
  const auto when = start.get<std::string>();
  const int current = std::stoi(when.substr(0, 4)) - (when.substr(5, 2) < "07" ? 1 : 0);
  if (current < 3) return archive;
  for (int year = current - 2; year < current; ++year)
    for (int ordinal = 0; ordinal < 6; ++ordinal) {
      const size_t pair = ordinal < 3 ? size_t(ordinal) * 2 : 0;
      const auto *home = clubs[pair % clubs.size()], *away = clubs[(pair + 1) % clubs.size()];
      archive["matches"].push_back(make_match(world, *home, *away, year, ordinal));
    }
  return archive;
}
bool valid_demo_almanac(const Json &archive) {
  try {
    if (!keys(archive, {"schema", "matches"}) ||
        archive.at("schema") != "openbmh-ui-demo-almanac@1" || !archive.at("matches").is_array())
      return false;
    std::set<std::string> ids;
    for (const auto &m : archive.at("matches"))
      if (!valid_match(m) || !ids.insert(m.at("id").get<std::string>()).second) return false;
    return true;
  } catch (const Json::exception &) { return false; }
}
Json query_demo_almanac(const Json &archive, const Json &request, const std::string &as_of) {
  if (!date(as_of)) return error("invalid_almanac_cutoff");
  if (!valid_demo_almanac(archive)) return error("invalid_demo_almanac");
  try {
    if (!request.is_object() || !request.contains("kind") || !text(request.at("kind")))
      return error("invalid_almanac_request");
    const auto kind = request.at("kind").get<std::string>();
    const bool index = kind == "index", calendar = kind == "calendar", day = kind == "day", match = kind == "match";
    if (!(index || calendar || day || match) ||
        !(index ? keys(request, {"kind"}) : calendar ? keys(request, {"kind", "year"})
             : day ? keys(request, {"kind", "date"}) : keys(request, {"kind", "id"})) ||
        (calendar && !integer(request.at("year"), 1, 9999)) ||
        (day && (!text(request.at("date")) || !date(request.at("date").get<std::string>()))) ||
        (match && !text(request.at("id")))) return error("invalid_almanac_request");
    std::vector<const Json *> visible;
    for (const auto &m : archive.at("matches"))
      if (m.at("date").get<std::string>() <= as_of) visible.push_back(&m);
    std::stable_sort(visible.begin(), visible.end(), [](const auto *a, const auto *b) {
      return a->at("date").template get<std::string>() < b->at("date").template get<std::string>();
    });
    if (index) {
      std::set<int> years;
      for (const auto *m : visible) years.insert(std::stoi(m->at("date").template get<std::string>().substr(0, 4)));
      Json listed = Json::array();
      for (int year : years) listed.push_back(year);
      return {{"ok", true}, {"years", listed}, {"match_count", visible.size()},
          {"first_date", visible.empty() ? Json(nullptr) : visible.front()->at("date")},
          {"last_date", visible.empty() ? Json(nullptr) : visible.back()->at("date")}};
    }
    if (calendar) {
      const int year = request.at("year");
      std::map<std::string, Json> days;
      for (const auto *m : visible) {
        const auto when = m->at("date").template get<std::string>();
        if (std::stoi(when.substr(0, 4)) != year) continue;
        auto [it, inserted] = days.try_emplace(when, Json{{"date", when}, {"count", 0}, {"highlights", Json::array()}});
        (void)inserted;
        it->second["count"] = it->second.at("count").get<int>() + 1;
        const auto &r = m->at("result");
        if (r.at("extra_played") == true || r.at("penalties_played") == true ||
            r.at("final")[0].get<int>() + r.at("final")[1].get<int>() >= 4)
          it->second["highlights"].push_back(m->at("id"));
      }
      Json listed = Json::array();
      for (const auto &[when, item] : days) { (void)when; listed.push_back(item); }
      return {{"ok", true}, {"year", year}, {"days", listed}};
    }
    if (day) {
      Json matches = Json::array();
      for (const auto *m : visible)
        if (m->at("date") == request.at("date")) matches.push_back(*m);
      return {{"ok", true}, {"date", request.at("date")}, {"matches", matches}};
    }
    for (const auto *m : visible)
      if (m->at("id") == request.at("id")) return {{"ok", true}, {"match", *m}};
    return error("almanac_match_not_found");
  } catch (const Json::exception &) { return error("invalid_almanac_request"); }
}
} // namespace bmh::modern_ui
