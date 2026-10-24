// SPDX-License-Identifier: GPL-3.0-only
#include "modern/ui/desktop.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace bmh::modern_ui {
namespace {
constexpr int pitch = 248, gap = 24, cell = 224, left = 64, top = 224;
bool overlaps(const Json &a, const Json &b, int spacing) {
  const int x = a.at("x"), y = a.at("y"), bx = b.at("x"), by = b.at("y");
  return x < bx + b.at("w").get<int>() + spacing &&
         x + a.at("w").get<int>() + spacing > bx &&
         y < by + b.at("h").get<int>() + spacing &&
         y + a.at("h").get<int>() + spacing > by;
}
bool find_space(Json &layout, const std::string &id, int near_x, int near_y,
                bool nearest) {
  auto &object = layout.at("objects").at(id);
  const int width = object.at("w"), height = object.at("h");
  const bool grid = layout.value("grid", true);
  const int step = grid ? pitch : 32;
  double best = std::numeric_limits<double>::max();
  int found_x = -1, found_y = -1;
  for (int y = top; y + height <= 2160 - gap; y += step) {
    for (int x = left; x + width <= 3840 - gap; x += step) {
      Json candidate = object;
      candidate["x"] = x;
      candidate["y"] = y;
      bool collision = false;
      for (const auto &[other_id, other] : layout.at("objects").items()) {
        if (other_id != id && other.value("visible", false) &&
            overlaps(candidate, other, grid ? gap : 20)) {
          collision = true;
          break;
        }
      }
      if (collision)
        continue;
      const double distance = nearest ? std::hypot(x - near_x, y - near_y) : 0;
      if (distance < best) {
        best = distance;
        found_x = x;
        found_y = y;
      }
      if (!nearest)
        break;
    }
    if (!nearest && found_x >= 0)
      break;
  }
  if (found_x < 0)
    return false;
  object["x"] = found_x;
  object["y"] = found_y;
  return true;
}
Json object(int column, int row, int columns, int rows, bool widget,
            bool visible) {
  return {{"x", left + column * pitch},
          {"y", top + row * pitch},
          {"w", columns * pitch - gap},
          {"h", rows * pitch - gap},
          {"pinned", true},
          {"visible", visible},
          {"widget", widget}};
}
} // namespace
const std::vector<std::string> &desktop_routes() {
  static const std::vector<std::string> value = {
      "tables",   "league",      "strengths", "lineup",     "achievements",
      "other",    "friendlies",  "postpone",  "calendar",   "indoor",
      "load",     "save",        "kickoff",   "toto",       "bank",
      "camp",     "training",    "transfer",  "tactics",    "settings",
      "scorers",  "advertising", "stadium",   "statistics", "environment",
      "fairplay", "highscore",   "contracts", "balance",    "newplayer",
      "news", "almanac"};
  return value;
}
const std::vector<WidgetDefinition> &widget_definitions() {
  static const std::vector<WidgetDefinition> value = {
      {"calendar", "Kalender", {{2, 2}, {2, 3}, {3, 3}}},
      {"account", "Konto & Eintritt", {{4, 1}, {2, 2}, {4, 2}, {4, 3}}},
      {"competition", "Wettbewerb & Gegner", {{2, 2}, {4, 2}, {4, 3}}},
      {"portrait", "Managerporträt", {{2, 2}, {3, 3}}},
      {"crest", "Vereinswappen", {{2, 2}, {3, 3}}},
      {"opponent-crest", "Gegnerwappen", {{2, 2}, {3, 3}}},
      {"warnings", "Warnlampen", {{4, 1}, {2, 2}, {4, 2}, {4, 3}}},
      {"fans", "Fan-Stimmung", {{2, 1}, {4, 1}}},
      {"own-curve",
       "Tabellenplatz · eigener Verein",
       {{2, 2}, {3, 3}, {4, 2}, {4, 4}}},
      {"opponent-curve",
       "Tabellenplatz · Gegner",
       {{2, 2}, {3, 3}, {4, 2}, {4, 4}}},
      {"comparison-curve",
       "Tabellenplätze · Vergleich",
       {{3, 3}, {4, 2}, {4, 4}}},
      {"results", "Letzte sechs Spiele", {{4, 1}, {2, 2}, {4, 2}, {4, 4}}},
      {"duels", "Direkter Vergleich", {{2, 2}, {4, 2}}},
      {"own-summary", "Platz & Stärke · eigener Verein", {{2, 1}, {4, 1}}},
      {"opponent-summary", "Platz & Stärke · Gegner", {{2, 1}, {4, 1}}},
      {"news", "Nachrichten", {{2, 2}, {4, 2}, {4, 4}}},
      {"trash", "Papierkorb", {{1, 1}, {2, 2}}}};
  return value;
}
Json default_desktop() {
  Json layout = {{"schema", "openbmh-modern-desktop@1"},
                 {"grid", true},
                 {"objects", Json::object()}};
  for (const auto &definition : widget_definitions()) {
    const auto [w, h] = definition.sizes.front();
    layout["objects"][definition.id] = object(0, 0, w, h, true, false);
  }
  auto &objects = layout["objects"];
  objects["calendar"] = object(0, 0, 2, 3, true, true);
  objects["account"] = object(2, 0, 4, 2, true, true);
  objects["competition"] = object(2, 2, 4, 2, true, true);
  objects["portrait"] = object(6, 0, 2, 2, true, true);
  objects["crest"] = object(8, 0, 2, 2, true, true);
  objects["opponent-crest"] = object(10, 0, 2, 2, true, true);
  objects["warnings"] = object(6, 2, 4, 2, true, true);
  objects["trash"] = object(14, 6, 1, 1, true, true);
  for (const auto &route : desktop_routes()) {
    const auto id = "app-" + route;
    objects[id] = object(0, 0, 1, 1, false, false);
    if (!find_space(layout, id, left, top, false))
      throw editor::Error("default_desktop_full");
    objects[id]["visible"] = true;
  }
  return layout;
}
bool valid_desktop(const Json &layout) {
  try {
    if (layout.at("schema") != "openbmh-modern-desktop@1" ||
        !layout.at("grid").is_boolean())
      return false;
    const auto &objects = layout.at("objects");
    if (!objects.is_object() ||
        objects.size() != widget_definitions().size() + desktop_routes().size())
      return false;
    const auto check = [&](const std::string &id, bool widget) {
      const auto &o = objects.at(id);
      for (const auto *key : {"x", "y", "w", "h"})
        if (!o.at(key).is_number_integer() || o.at(key) < 0 || o.at(key) > 3840)
          return false;
      if (!o.at("visible").is_boolean() || !o.at("pinned").is_boolean() ||
          o.at("widget") != widget)
        return false;
      const int x = o.at("x"), y = o.at("y"), w = o.at("w"), h = o.at("h");
      return w > 0 && h > 0 && w <= 3840 && h <= 2160 - top && x >= 0 &&
             y >= top && x <= 3840 - w && y <= 2160 - h &&
             (widget || (w == cell && h == cell && o.at("visible") == true));
    };
    for (const auto &d : widget_definitions()) {
      if (!check(d.id, true))
        return false;
      const auto &o = objects.at(d.id);
      if (std::none_of(d.sizes.begin(), d.sizes.end(), [&](auto s) {
            return o.at("w") == s.first * pitch - gap &&
                   o.at("h") == s.second * pitch - gap;
          }))
        return false;
    }
    for (const auto &route : desktop_routes())
      if (!check("app-" + route, false))
        return false;
    return true;
  } catch (const Json::exception &) {
    return false;
  }
}
bool migrate_desktop(Json &layout) {
  try {
    if (layout.at("objects").contains("app-almanac"))
      return valid_desktop(layout);
    auto next = layout;
    next["objects"]["app-almanac"] = object(0, 0, 1, 1, false, true);
    if (!valid_desktop(next) ||
        !find_space(next, "app-almanac", left, top, false))
      return false;
    layout = std::move(next);
    return true;
  } catch (const Json::exception &) {
    return false;
  }
}
bool place_desktop_object(Json &layout, const std::string &id, int x, int y,
                          bool nearest) {
  if (!valid_desktop(layout) || !layout["objects"].contains(id))
    return false;
  auto next = layout;
  auto &o = next["objects"][id];
  if (o.at("pinned") == true)
    return false;
  if (!next.at("grid").get<bool>()) {
    o["x"] = std::clamp(x, 0, 3840 - o.at("w").get<int>());
    o["y"] = std::clamp(y, top, 2160 - o.at("h").get<int>());
  } else if (!find_space(next, id, x, y, nearest))
    return false;
  o["visible"] = true;
  layout = std::move(next);
  return true;
}
bool resize_desktop_widget(Json &layout, const std::string &id, int columns,
                           int rows) {
  if (!valid_desktop(layout))
    return false;
  const auto found =
      std::find_if(widget_definitions().begin(), widget_definitions().end(),
                   [&](const auto &d) { return d.id == id; });
  if (found == widget_definitions().end() ||
      std::find(found->sizes.begin(), found->sizes.end(),
                std::pair{columns, rows}) == found->sizes.end())
    return false;
  auto next = layout;
  auto &o = next["objects"][id];
  if (o.at("pinned") == true)
    return false;
  o["w"] = columns * pitch - gap;
  o["h"] = rows * pitch - gap;
  if (!find_space(next, id, o.at("x"), o.at("y"), true))
    return false;
  o["visible"] = true;
  layout = std::move(next);
  return true;
}
bool arrange_desktop(Json &layout) {
  if (!valid_desktop(layout))
    return false;
  auto next = layout;
  next["grid"] = true;
  std::vector<std::string> loose;
  for (auto &[id, o] : next["objects"].items())
    if (o.at("visible") == true && o.at("pinned") == false) {
      loose.push_back(id);
      o["visible"] = false;
    }
  std::stable_sort(
      loose.begin(), loose.end(), [&](const auto &a, const auto &b) {
        const auto &x = next["objects"][a], &y = next["objects"][b];
        return x.at("w").template get<int>() * x.at("h").template get<int>() >
               y.at("w").template get<int>() * y.at("h").template get<int>();
      });
  for (const auto &id : loose) {
    auto &o = next["objects"][id];
    if (!find_space(next, id, o.at("x"), o.at("y"), false))
      return false;
    o["visible"] = true;
  }
  layout = std::move(next);
  return true;
}
} // namespace bmh::modern_ui
