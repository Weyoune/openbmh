// SPDX-License-Identifier: GPL-3.0-only
#include "modern/ui/demo_reports.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <set>
#include <string_view>

namespace bmh::modern_ui {
namespace {
constexpr int played_rounds = 26;
constexpr int other_rounds = 34;
constexpr std::array<int, 9> other_pairs = {9, 8, 4, 8, 4, 4, 8, 8, 1};
struct Fixture {
  int home, away, home_goals, away_goals, round;
};
bool integer(const Json &v, int low, int high) {
  return v.is_number_integer() && v >= low && v <= high;
}
bool keys(const Json &v, std::initializer_list<const char *> required,
          std::initializer_list<const char *> optional = {}) {
  if (!v.is_object())
    return false;
  std::set<std::string> allowed;
  for (const auto *key : required) {
    if (!v.contains(key))
      return false;
    allowed.emplace(key);
  }
  for (const auto *key : optional)
    allowed.emplace(key);
  for (const auto &entry : v.items())
    if (!allowed.contains(entry.key()))
      return false;
  return true;
}
int rounds(size_t size) {
  if (size < 2)
    return 1;
  if (size > size_t(std::numeric_limits<int>::max() / 2 - 1))
    return 1;
  return 2 * (int(size) + int(size % 2) - 1);
}
std::string initial_league(const Catalog &world) {
  auto leagues = world.leagues("nation:de");
  if (leagues.empty())
    leagues = world.leagues();
  return leagues.empty() ? "" : leagues.front()->at("id").get<std::string>();
}
bool league_exists(const Catalog &world, const std::string &id) {
  const auto leagues = world.leagues();
  if (leagues.empty())
    return id.empty();
  return std::any_of(leagues.begin(), leagues.end(), [&](const auto *league) {
    return league->at("id") == id;
  });
}
int league_rounds(const Catalog &world, const std::string &id) {
  return id.empty() ? 1 : rounds(world.clubs(id).size());
}
int league_index(const Catalog &world, const std::string &id) {
  const auto leagues = world.leagues();
  const auto found = std::find_if(leagues.begin(), leagues.end(), [&](auto *l) {
    return l->at("id") == id;
  });
  return found == leagues.end() ? 0 : int(found - leagues.begin());
}
bool sort_key(const std::string &view, const std::string &key) {
  static const std::set<std::string> table_keys = {
      "original", "rank", "played", "wins", "draw", "loss", "form",
      "points", "gf", "diff"};
  static const std::set<std::string> strength_keys = {
      "original", "club", "keeper", "defence", "midfield", "attack",
      "strength"};
  return view == "tables" ? table_keys.contains(key)
         : view == "strengths" && strength_keys.contains(key);
}
std::vector<Fixture> fixtures(size_t club_count, int league) {
  if (club_count < 2 || club_count > size_t(std::numeric_limits<int>::max() / 2 - 1))
    return {};
  const int count = int(club_count), ring_size = count + count % 2;
  std::vector<int> ring(size_t(ring_size), 0);
  std::iota(ring.begin(), ring.end(), 0);
  std::vector<Fixture> result;
  for (int round = 0; round < ring_size - 1; ++round) {
    for (int pair = 0; pair < ring_size / 2; ++pair) {
      int home = ring[size_t(pair)], away = ring[size_t(ring_size - 1 - pair)];
      if ((pair == 0 ? round : pair) % 2)
        std::swap(home, away);
      if (home < count && away < count)
        result.push_back({home, away, 0, 0, round});
    }
    std::rotate(ring.begin() + 1, ring.end() - 1, ring.end());
  }
  const auto first_size = result.size();
  for (size_t i = 0; i < first_size; ++i) {
    const auto first = result[i];
    result.push_back({first.away, first.home, 0, 0,
                      first.round + ring_size - 1});
  }
  for (auto &match : result) {
    // HTML preview arithmetic, deliberately independent of every game RNG.
    const uint32_t seed = uint32_t(match.round + 1) * 97u +
                          uint32_t(match.home + 3) * 37u +
                          uint32_t(match.away + 5) * 19u +
                          uint32_t(league) * 53u;
    match.home_goals = int((seed ^ (seed >> 3)) % 5);
    match.away_goals = int((seed ^ (seed >> 5)) % 4);
  }
  return result;
}
// Deterministic name ordering without a host locale dependency. Common Latin
// accents are primary-equivalent, and digit runs compare numerically.
std::string name_key(std::string name) {
  for (const auto &[from, to] :
       std::initializer_list<std::pair<std::string_view, std::string_view>>{
           {"Ä", "a"}, {"ä", "a"}, {"Ö", "o"}, {"ö", "o"},
           {"Ü", "u"}, {"ü", "u"}, {"ß", "ss"}, {"ẞ", "ss"},
           {"Á", "a"}, {"á", "a"}, {"À", "a"}, {"à", "a"},
           {"Â", "a"}, {"â", "a"}, {"É", "e"}, {"é", "e"},
           {"È", "e"}, {"è", "e"}, {"Ê", "e"}, {"ê", "e"},
           {"Í", "i"}, {"í", "i"}, {"Ó", "o"}, {"ó", "o"},
           {"Ô", "o"}, {"ô", "o"}, {"Ú", "u"}, {"ú", "u"},
           {"Ç", "c"}, {"ç", "c"}, {"Ñ", "n"}, {"ñ", "n"}}) {
    size_t at = 0;
    while ((at = name.find(from, at)) != std::string::npos) {
      name.replace(at, from.size(), to);
      at += to.size();
    }
  }
  for (char &c : name)
    if (c >= 'A' && c <= 'Z')
      c += 'a' - 'A';
  return name;
}
int name_compare(const std::string &a, const std::string &b) {
  const auto x = name_key(a), y = name_key(b);
  size_t i = 0, j = 0;
  const auto digit = [](char c) { return c >= '0' && c <= '9'; };
  while (i < x.size() && j < y.size()) {
    if (digit(x[i]) && digit(y[j])) {
      size_t ei = i, ej = j;
      while (ei < x.size() && digit(x[ei])) ++ei;
      while (ej < y.size() && digit(y[ej])) ++ej;
      while (i < ei && x[i] == '0') ++i;
      while (j < ej && y[j] == '0') ++j;
      if (ei - i != ej - j)
        return ei - i < ej - j ? -1 : 1;
      const int n = x.compare(i, ei - i, y, j, ej - j);
      if (n)
        return n < 0 ? -1 : 1;
      i = ei;
      j = ej;
      continue;
    }
    if (x[i] != y[j])
      return static_cast<unsigned char>(x[i]) < static_cast<unsigned char>(y[j]) ? -1 : 1;
    ++i;
    ++j;
  }
  return i == x.size() ? (j == y.size() ? 0 : -1) : 1;
}
Json sorted(std::vector<Json> rows, const Json &sort) {
  const auto key = sort.at("key").get<std::string>();
  const bool desc = sort.at("desc").get<bool>();
  std::stable_sort(rows.begin(), rows.end(), [&](const Json &a, const Json &b) {
    const auto &x = a.at(key == "club" ? "name" : key);
    const auto &y = b.at(key == "club" ? "name" : key);
    if (x.is_null() != y.is_null())
      return !x.is_null(); // Missing OBW data remains last in both directions.
    const int comparison = x.is_null() ? 0
                           : key == "club" ? name_compare(x.get<std::string>(), y.get<std::string>())
                           : x == y ? 0 : x < y ? -1 : 1;
    return comparison ? (desc ? comparison > 0 : comparison < 0)
                      : a.at("original") < b.at("original");
  });
  return Json(rows);
}
Json average(const std::vector<const Json *> &squad, const std::string &position) {
  double sum = 0;
  int count = 0;
  for (auto *player : squad) {
    const auto attrs = player->find("attributes");
    if (attrs == player->end() || !attrs->is_object())
      continue;
    const auto strength = attrs->find("strength");
    if (strength == attrs->end() || !strength->is_number())
      continue;
    const double value = strength->get<double>();
    if (!std::isfinite(value) || value < 0 || value > 100)
      continue;
    if (!position.empty()) {
      const auto positions = player->find("positions");
      if (positions == player->end() || !positions->is_array() ||
          std::find(positions->begin(), positions->end(), position) == positions->end())
        continue;
    }
    sum += value;
    ++count;
  }
  return count ? Json(int(std::lround(sum / count))) : Json();
}
} // namespace

Json demo_reports(const Catalog &world) {
  const auto league = initial_league(world);
  return {{"league", league},
          {"round", std::min(26, league_rounds(world, league) - 1)},
          {"filter", 0}, {"competition", 0},
          {"sorts", {{"tables", {{"key", "points"}, {"desc", true}}},
                     {"strengths", {{"key", "original"}, {"desc", false}}}}}};
}
bool valid_demo_reports(const Json &state, const Catalog &world) {
  try {
    if (!keys(state, {"league", "round", "filter", "competition", "sorts"}) ||
        !state.at("league").is_string() ||
        !league_exists(world, state.at("league").get<std::string>()) ||
        !integer(state.at("filter"), 0, 4) ||
        !integer(state.at("competition"), 0, 8))
      return false;
    const int count = std::max(league_rounds(world, state.at("league")),
                               state.at("competition") == 0 ? 1 : other_rounds);
    if (!integer(state.at("round"), 0, count - 1) ||
        !keys(state.at("sorts"), {"tables", "strengths"}))
      return false;
    for (const auto *view : {"tables", "strengths"}) {
      const auto &sort = state.at("sorts").at(view);
      if (!keys(sort, {"key", "desc"}) || !sort.at("key").is_string() ||
          !sort.at("desc").is_boolean() || !sort_key(view, sort.at("key")))
        return false;
    }
    return true;
  } catch (const Json::exception &) {
    return false;
  }
}
int report_round_count(const Catalog &world, const Json &state, bool other) {
  if (!valid_demo_reports(state, world))
    return 1;
  return other && state.at("competition") != 0
             ? other_rounds : league_rounds(world, state.at("league"));
}
std::string edit_demo_reports(Json &state, const Json &action, const Catalog &world) {
  if (!valid_demo_reports(state, world))
    return "invalid_reports_state";
  try {
    if (!action.is_object() || !action.contains("kind") || !action.at("kind").is_string())
      return "invalid_reports_action";
    const auto kind = action.at("kind").get<std::string>();
    auto next = state;
    if (kind == "country" || kind == "league") {
      if (!keys(action, {"kind", "value"}) || !action.at("value").is_string())
        return "invalid_reports_action";
      const auto value = action.at("value").get<std::string>();
      if (kind == "country") {
        if (!world.find("nations", value))
          return "invalid_reports_country";
        const auto leagues = world.leagues(value);
        if (leagues.empty())
          return "invalid_reports_country";
        next["league"] = leagues.front()->at("id");
      } else {
        if (value.empty() || !league_exists(world, value))
          return "invalid_reports_league";
        next["league"] = value;
      }
      next["round"] = std::min(next.at("round").get<int>(),
                                 league_rounds(world, next.at("league")) - 1);
    } else if (kind == "filter" || kind == "competition") {
      if (!keys(action, {"kind", "value"}) ||
          !integer(action.at("value"), 0, kind == "filter" ? 4 : 8))
        return "invalid_reports_action";
      next[kind] = action.at("value").get<int>();
      if (kind == "competition")
        next["round"] = std::min(next.at("round").get<int>(),
                                   (next.at("competition") == 0
                                        ? league_rounds(world, next.at("league"))
                                        : other_rounds) - 1);
    } else if (kind == "round") {
      if (!keys(action, {"kind", "step"}, {"other"}) ||
          !integer(action.at("step"), -1, 1) || action.at("step") == 0 ||
          (action.contains("other") && !action.at("other").is_boolean()))
        return "invalid_reports_action";
      const int count = report_round_count(world, next, action.value("other", false));
      const int current = std::clamp(next.at("round").get<int>(), 0, count - 1);
      next["round"] = (current + action.at("step").get<int>() + count) % count;
    } else if (kind == "sort") {
      if (!keys(action, {"kind", "view", "key"}) ||
          !action.at("view").is_string() || !action.at("key").is_string())
        return "invalid_reports_action";
      const auto view = action.at("view").get<std::string>(), key = action.at("key").get<std::string>();
      if (!sort_key(view, key))
        return "invalid_reports_sort";
      auto &sort = next["sorts"][view];
      sort["desc"] = sort.at("key") == key ? !sort.at("desc").get<bool>()
                       : key != "club" && key != "rank" && key != "original";
      sort["key"] = key;
    } else if (kind == "reset") {
      if (!keys(action, {"kind"}))
        return "invalid_reports_action";
      next["league"] = initial_league(world);
      next["filter"] = 0;
      next["sorts"]["tables"] = {{"key", "points"}, {"desc", true}};
      next["round"] = std::min(next.at("round").get<int>(),
                                 league_rounds(world, next.at("league")) - 1);
    } else {
      return "unknown_reports_action";
    }
    // OpenBMH deviates from the HTML preview by clamping shared round state
    // when a league/competition changes. The other-competition 34-round range
    // is a bounded presentation fixture, never an engine scheduling rule.
    if (!valid_demo_reports(next, world))
      return "invalid_reports_selection";
    state = std::move(next);
    return {};
  } catch (const Json::exception &) {
    return "invalid_reports_action";
  }
}
Json report_table_rows(const Catalog &world, const Json &state) {
  if (!valid_demo_reports(state, world))
    return Json::array();
  const auto league = state.at("league").get<std::string>();
  const auto clubs = league.empty() ? std::vector<const Json *>{} : world.clubs(league);
  std::vector<Json> rows;
  for (size_t i = 0; i < clubs.size(); ++i)
    rows.push_back({{"club", clubs[i]->at("id")}, {"original", i}, {"rank", 0},
                    {"played", 0}, {"wins", 0}, {"draw", 0}, {"loss", 0},
                    {"gf", 0}, {"ga", 0}, {"points", 0}, {"diff", 0},
                    {"form", 0}, {"form_text", ""}});
  const int filter = state.at("filter"), half = league_rounds(world, league) / 2;
  for (const auto &match : fixtures(clubs.size(), league_index(world, league))) {
    if (match.round >= played_rounds || (filter == 1 && match.round >= half) ||
        (filter == 2 && match.round < half))
      continue;
    for (int side = 0; side < 2; ++side) {
      if ((filter == 3 && side != 0) || (filter == 4 && side != 1))
        continue;
      auto &row = rows[size_t(side == 0 ? match.home : match.away)];
      const int gf = side == 0 ? match.home_goals : match.away_goals,
                ga = side == 0 ? match.away_goals : match.home_goals;
      row["played"] = row["played"].get<int>() + 1;
      row["gf"] = row["gf"].get<int>() + gf;
      row["ga"] = row["ga"].get<int>() + ga;
      const char form = gf > ga ? 'S' : gf == ga ? 'U' : 'N';
      const char *metric = form == 'S' ? "wins" : form == 'U' ? "draw" : "loss";
      row[metric] = row[metric].get<int>() + 1;
      row["points"] = row["points"].get<int>() + (form == 'S' ? 3 : form == 'U' ? 1 : 0);
      auto text = row["form_text"].get<std::string>() + form;
      if (text.size() > 8)
        text.erase(0, text.size() - 8);
      row["form_text"] = text;
    }
  }
  for (auto &row : rows) {
    row["diff"] = row["gf"].get<int>() - row["ga"].get<int>();
    int form = 0;
    for (char c : row["form_text"].get<std::string>())
      form += c == 'S' ? 3 : c == 'U' ? 1 : 0;
    row["form"] = form;
  }
  std::stable_sort(rows.begin(), rows.end(), [](const Json &a, const Json &b) {
    for (const char *key : {"points", "diff", "gf"})
      if (a.at(key) != b.at(key)) return a.at(key) > b.at(key);
    return a.at("original") < b.at("original");
  });
  for (size_t i = 0; i < rows.size(); ++i) {
    rows[i]["rank"] = i + 1;
    rows[i]["original"] = i; // Gold's stable default-table order, not name order.
  }
  return sorted(std::move(rows), state.at("sorts").at("tables"));
}
Json report_strength_rows(const Catalog &world, const Json &state) {
  if (!valid_demo_reports(state, world))
    return Json::array();
  const auto league = state.at("league").get<std::string>();
  const auto clubs = league.empty() ? std::vector<const Json *>{} : world.clubs(league);
  std::vector<Json> rows;
  for (size_t i = 0; i < clubs.size(); ++i) {
    const auto id = clubs[i]->at("id").get<std::string>();
    const auto squad = world.squad(id);
    rows.push_back({{"club", id}, {"id", id}, {"name", world.name(*clubs[i])},
                    {"original", i}, {"keeper", average(squad, "gk")},
                    {"defence", average(squad, "def")}, {"midfield", average(squad, "mid")},
                    {"attack", average(squad, "fwd")}, {"strength", average(squad, "")}});
  }
  return sorted(std::move(rows), state.at("sorts").at("strengths"));
}
Json report_match_rows(const Catalog &world, const Json &state, bool other) {
  Json result = Json::array();
  if (!valid_demo_reports(state, world))
    return result;
  const int round = std::clamp(state.at("round").get<int>(), 0,
                                report_round_count(world, state, other) - 1);
  const auto league = state.at("league").get<std::string>();
  const auto add = [&](const Json &home, const Json &away, int hg, int ag) {
    result.push_back({{"home", home}, {"away", away}, {"home_goals", hg},
                      {"away_goals", ag}, {"round", round}});
  };
  if (!other || state.at("competition") == 0) {
    const auto clubs = league.empty() ? std::vector<const Json *>{} : world.clubs(league);
    for (const auto &match : fixtures(clubs.size(), league_index(world, league)))
      if (match.round == round)
        add(clubs[size_t(match.home)]->at("id"), clubs[size_t(match.away)]->at("id"),
            match.home_goals, match.away_goals);
  } else {
    const auto leagues = world.leagues("nation:de");
    if (leagues.empty()) return result;
    const auto clubs = world.clubs(leagues.front()->at("id"));
    if (clubs.size() < 2) return result;
    const int competition = state.at("competition"), seed = round + competition * 3;
    for (int i = 0; i < other_pairs[size_t(competition)]; ++i)
      add(clubs[size_t(i * 2 + seed) % clubs.size()]->at("id"),
          clubs[size_t(i * 2 + 1 + seed) % clubs.size()]->at("id"),
          (i + seed) % 4, (i * 3 + seed) % 3);
  }
  return result;
}
Json report_opening_pairs(const Catalog &world) {
  std::vector<const Json *> pool;
  for (const auto *league : world.leagues("nation:de"))
    for (const auto *club : world.clubs(league->at("id")))
      if (pool.size() < 64)
        pool.push_back(club);
  Json result = Json::array();
  for (size_t i = 0; i + 1 < pool.size(); i += 2)
    result.push_back({{"home", pool[i]->at("id")},
                      {"away", pool[i + 1]->at("id")},
                      {"home_goals", nullptr}, {"away_goals", nullptr},
                      {"round", 0}});
  return result;
}
} // namespace bmh::modern_ui
