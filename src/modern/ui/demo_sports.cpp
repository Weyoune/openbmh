// SPDX-License-Identifier: GPL-3.0-only
#include "modern/ui/demo_sports.hpp"
#include <algorithm>
#include <array>
#include <numeric>
#include <sstream>

namespace bmh::modern_ui {
namespace {
bool integers(const Json &a, size_t size, int low, int high) {
  if (!a.is_array() || a.size() != size)
    return false;
  return std::all_of(a.begin(), a.end(), [&](const auto &v) {
    return v.is_number_integer() && v >= low && v <= high;
  });
}
int sum(const Json &a, size_t begin, size_t end) {
  int n = 0;
  for (size_t i = begin; i < end; ++i)
    n += a.at(i).template get<int>();
  return n;
}
constexpr std::array<int, 8> tactic_limits = {3, 3, 3, 3, 1, 1, 1, 11};
} // namespace
Json demo_training(const Catalog &world, const std::string &club) {
  Json players = Json::object();
  size_t i = 0;
  for (auto *player : world.squad(club)) {
    players[player->value("id", "")] = {{"intensity", i % 5 == 0 ? 1 : 2},
                                        {"focus", int(i % 8)}};
    ++i;
  }
  return {{"club", club},
          {"areas", {2, 2, 1, 1, 2, 2, 2, 2, 3}},
          {"types", {2, 2, 2, 2}},
          {"autos", {false, false, false, false}},
          {"players", players}};
}
bool valid_demo_training(const Json &value, const Catalog &world,
                         const std::string &club) {
  try {
    if (!value.is_object() || value.at("club") != club ||
        !integers(value.at("areas"), 9, 1, 3) ||
        !integers(value.at("types"), 4, 1, 3))
      return false;
    if (sum(value["types"], 0, 4) > 8 || sum(value["areas"], 0, 2) > 4 ||
        sum(value["areas"], 2, 9) > 18)
      return false;
    const auto &autos = value.at("autos");
    if (!autos.is_array() || autos.size() != 4)
      return false;
    for (const auto &v : autos)
      if (!v.is_boolean())
        return false;
    const auto &players = value.at("players");
    const auto squad = world.squad(club);
    if (!players.is_object() || players.size() != squad.size())
      return false;
    for (auto *p : squad) {
      const auto &v = players.at(p->value("id", ""));
      if (!v.at("intensity").is_number_integer() || v["intensity"] < 0 ||
          v["intensity"] > 3 || !v.at("focus").is_number_integer() ||
          v["focus"] < 0 || v["focus"] > 7)
        return false;
    }
    return true;
  } catch (const Json::exception &) {
    return false;
  }
}
std::string edit_demo_training(Json &state, const Json &action) {
  for (const auto *key : {"index", "value"})
    if (action.contains(key) && (!action[key].is_number_integer() ||
                                 action[key] < 0 || action[key] > 100))
      return "invalid_training_value";
  auto next = state;
  const auto kind = action.at("kind").get<std::string>();
  if (kind == "auto" || kind == "auto-all") {
    const int group = kind == "auto-all" ? -1 : action.at("index").get<int>();
    if (group < -1 || group > 3)
      return "invalid_training_group";
    const bool enabled =
        kind == "auto-all"
            ? !std::all_of(next["autos"].begin(), next["autos"].end(),
                           [](const auto &v) { return v == true; })
            : !next["autos"][size_t(group)].get<bool>();
    for (int i = 0; i < 4; ++i)
      if (group == -1 || group == i) {
        next["autos"][size_t(i)] = enabled;
        if (!enabled)
          continue;
        if (i == 0)
          next["types"] = {2, 2, 2, 2};
        if (i == 1) {
          next["areas"][0] = 2;
          next["areas"][1] = 2;
        }
        if (i == 2)
          for (size_t j = 2; j < 9; ++j)
            next["areas"][j] = 2;
        if (i == 3)
          for (auto &[id, p] : next["players"].items()) {
            (void)id;
            p["intensity"] = 2;
          }
      }
  } else if (kind == "focus") {
    const auto player = action.at("player").get<std::string>();
    const int value = action.at("value").get<int>();
    if (!next["players"].contains(player) || value < 0 || value > 7)
      return "invalid_training_player";
    if (next["autos"][3] == true)
      return "training_automatic";
    next["players"][player]["focus"] = value;
  } else if (kind == "ball") {
    const auto group = action.at("group").get<std::string>();
    const int value = action.at("value").get<int>();
    if (value < 1 || value > 3)
      return "invalid_training_value";
    if (group == "player") {
      const auto player = action.at("player").get<std::string>();
      if (!next["players"].contains(player))
        return "invalid_training_player";
      if (next["autos"][3] == true)
        return "training_automatic";
      auto &v = next["players"][player]["intensity"];
      v = v == value ? 0 : value;
    } else {
      const int index = action.at("index").get<int>();
      const bool area = group == "area";
      if ((!area && group != "type") || index < 0 || index >= (area ? 9 : 4))
        return "invalid_training_group";
      if (next["autos"][size_t(area ? (index < 2 ? 1 : 2) : 0)] == true)
        return "training_automatic";
      auto &list = next[area ? "areas" : "types"];
      list[size_t(index)] = value;
      if (sum(next["types"], 0, 4) > 8 || sum(next["areas"], 0, 2) > 4 ||
          sum(next["areas"], 2, 9) > 18)
        return "training_preview_budget";
    }
  } else
    return "unknown_training_action";
  state = std::move(next);
  return {};
}
Json demo_tactics() { return {{"values", {2, 1, 1, 0, 1, 0, 1, 6}}}; }
bool valid_demo_tactics(const Json &value) {
  try {
    const auto &v = value.at("values");
    if (!integers(v, 8, 0, 11))
      return false;
    for (size_t i = 0; i < 8; ++i)
      if (v[i] > tactic_limits[i])
        return false;
    return true;
  } catch (const Json::exception &) {
    return false;
  }
}
std::string edit_demo_tactics(Json &state, const Json &action) {
  for (const auto *key : {"index", "value"})
    if (action.contains(key) && (!action[key].is_number_integer() ||
                                 action[key] < 0 || action[key] > 100))
      return "invalid_tactic_value";
  const auto kind = action.at("kind").get<std::string>();
  const int index = kind == "effort" ? 7 : action.at("index").get<int>();
  if (index < 0 || index > 7)
    return "invalid_tactic_index";
  if (kind == "cycle")
    state["values"][size_t(index)] =
        (state["values"][size_t(index)].get<int>() + 1) %
        (tactic_limits[size_t(index)] + 1);
  else if (kind == "effort") {
    const int v = action.at("value").get<int>();
    if (v < 0 || v > 11)
      return "invalid_tactic_value";
    state["values"][7] = v;
  } else
    return "unknown_tactic_action";
  return {};
}
std::string tactics_diagram(const Json &state, const std::string &theme) {
  const auto values = state.at("values").get<std::array<int, 8>>();
  const std::string line = theme == "silver" ? "#536f66" : "#dfe8bf";
  const auto field_line = theme == "silver" ? "#536f66" : "#d7edbf",
             man_line = theme == "silver" ? "#536f66" : "#c9e6ae",
             curve_line = theme == "silver" ? "#536f66" : "#d4e7ba";
  const std::string defence = theme == "silver" ? "#85600c" : "#ffdd6b";
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 295 "
         "206'><defs><marker id='arrow' markerWidth='5' markerHeight='5' "
         "refX='4' refY='2.5' orient='auto'><path d='M0 0L5 2.5L0 5Z' fill='"
      << line << "'/></marker></defs>";
  const auto field = [&](double x, double y, double w, double h) {
    out << "<g fill='none' stroke='" << field_line
        << "' stroke-width='.65'><path d='M" << x << ' ' << y << 'h' << w << 'v'
        << h << 'h' << -w << "Z M" << x + w / 2 << ' ' << y << 'v' << h << " M"
        << x << ' ' << y + h * .25 << 'h' << w * .15 << 'v' << h * .5 << 'h'
        << -w * .15 << " M" << x + w << ' ' << y + h * .25 << 'h' << -w * .15
        << 'v' << h * .5 << 'h' << w * .15 << "'/><circle cx='" << x + w / 2
        << "' cy='" << y + h / 2 << "' r='" << h * .14 << "'/></g>";
  };
  const auto dot = [&](double x, double y, bool opponent = false) {
    out << "<circle cx='" << x << "' cy='" << y << "' r='2.4' fill='"
        << (opponent ? "#e46145" : defence)
        << "' stroke='#809344' stroke-width='.4'/>";
  };
  const auto arrow = [&](double x, double y, double dx, double dy,
                         bool dashed = false) {
    out << "<path d='M" << x << ' ' << y << 'l' << dx << ' ' << dy
        << "' fill='none' stroke='" << line << "' stroke-width='.85' "
        << (dashed ? "stroke-dasharray='2 1.3'" : "")
        << " marker-end='url(#arrow)'/>";
  };
  field(19, 9, 104, 62);
  field(19, 82, 104, 62);
  field(137, 10, 58, 37);
  field(205, 10, 58, 37);
  const std::array<std::pair<int, int>, 10> formation = {{{5, 1},
                                                          {5, 3},
                                                          {5, 5},
                                                          {5, 7},
                                                          {10, 1},
                                                          {10, 3},
                                                          {10, 5},
                                                          {10, 7},
                                                          {16, 3},
                                                          {16, 5}}};
  for (const auto &[x, y] : formation) {
    const double px = 25 + x / 18.0 * 82, py = 16 + y / 8.0 * 49;
    arrow(px, py, std::array<int, 4>{-16, -7, 7, 16}[size_t(values[0])], 0);
    dot(px, py);
  }
  for (int k = 0; k < 2; ++k) {
    const int x = 137 + k * 68;
    dot(x + 12, 27);
    dot(x + 25, 20);
    dot(x + 25, 37);
    dot(x + 43, 28, true);
    if (values[size_t(5 + k)] == 0)
      out << "<path d='M" << x + 3 << " 12l52 32 M" << x + 55
          << " 12l-52 32' stroke='" << line << "' stroke-width='1.6'/>";
  }
  dot(29, 111);
  dot(76, 94);
  dot(76, 119);
  dot(101, 118);
  dot(111, 120, true);
  arrow(29, 111, 10 + values[3] * 10, -values[3] * 5, true);
  arrow(76, 94, 10 + values[2] * 8, 12 - values[2] * 5);
  arrow(76, 119, 10 + values[1] * 8, -8 + values[1] * 4, true);
  for (int x : {181, 222})
    out << "<g fill='none' stroke='" << man_line
        << "' stroke-width='.8'><circle cx='" << x
        << "' cy='65' r='2'/><path d='M" << x
        << " 67l-2 7 5 4 m-5-4 -4 4 m5-8 -5 2 m5-2 5 2'/></g>";
  out << "<path d='M188 72Q201 " << (values[4] ? 45 : 72)
      << " 215 72' fill='none' stroke='" << curve_line
      << "' stroke-width='.9' stroke-dasharray='1 1.5' "
         "marker-end='url(#arrow)'/></svg>";
  return out.str();
}
} // namespace bmh::modern_ui
