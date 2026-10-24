// SPDX-License-Identifier: GPL-3.0-only
#include "modern/ui/demo_environment.hpp"
#include "modern/ui/demo_finance.hpp"
#include <array>
#include <set>

namespace bmh::modern_ui {
namespace {
constexpr std::array<int, 6> defaults{2, 3, 4, 3, 4, 2};
constexpr std::array<int64_t, 6> rates{2000, 3000, 5000, 7500, 6000, 4500};
constexpr std::array<const char *, 6> ids{"youth-a", "youth-b", "youth-c", "coach", "doctor", "physio"};
constexpr std::array<const char *, 6> names{"Nachwuchs · Jüngste", "Nachwuchs · Mittlere",
    "Nachwuchs · Ältere", "Co-Trainer", "Vereinsarzt", "Masseur"};
constexpr std::array<int, 3> ranks{11, 3, 7}, leagues{1, 2, 1};
bool integer(const Json &value, int lo, int hi) {
  return value.is_number_integer() && value >= lo && value <= hi;
}
bool keys(const Json &value, std::initializer_list<const char *> expected) {
  if (!value.is_object() || value.size() != expected.size()) return false;
  for (const auto *key : expected) if (!value.contains(key)) return false;
  return true;
}
} // namespace
bool valid_demo_environment(const Json &state, const Catalog &world) {
  try {
    if (!state.is_object() || !state.contains("environment") ||
        !keys(state.at("environment"), {"levels"})) return false;
    const auto &levels = state.at("environment").at("levels");
    if (!levels.is_array() || levels.size() != defaults.size()) return false;
    for (const auto &level : levels) if (!integer(level, 0, 6)) return false;
    (void)finance_currency_factor(world);
    return true;
  } catch (const std::exception &) { return false; }
}
bool migrate_demo_environment(Json &state, const Catalog &world) {
  if (!state.is_object()) return false;
  auto next = state;
  if (!next.contains("environment")) next["environment"] = {{"levels", defaults}};
  if (!valid_demo_environment(next, world)) return false;
  state = std::move(next);
  return true;
}
std::string edit_demo_environment(Json &state, const Json &action, const Catalog &world) {
  if (!valid_demo_environment(state, world)) return "invalid_environment_state";
  if (!keys(action, {"kind", "index", "value"}) || !action.at("kind").is_string())
    return "invalid_environment_action";
  if (action.at("kind") != "level") return "unknown_environment_action";
  if (!integer(action.at("index"), 0, 5) || !integer(action.at("value"), 0, 6))
    return "invalid_environment_level";
  auto next = state;
  next["environment"]["levels"][action.at("index").get<size_t>()] = action.at("value");
  if (!valid_demo_environment(next, world)) return "invalid_environment_state";
  state = std::move(next);
  return {};
}
Json demo_environment_rows(const Json &state, const Catalog &world) {
  if (!valid_demo_environment(state, world))
    return {{"ok", false}, {"error", "invalid_environment_state"}};
  const auto factor = finance_currency_factor(world);
  Json rows = Json::array();
  int64_t total = 0;
  for (size_t i = 0; i < defaults.size(); ++i) {
    const int level = state.at("environment").at("levels")[i].get<int>();
    const int64_t rate = rates[i] * factor, cost = rate * level;
    total += cost;
    rows.push_back({{"id", ids[i]}, {"name", names[i]}, {"level", level},
        {"rate", rate}, {"cost", cost}, {"image", i < 3 ? "youth" : "staff"},
        {"cell", i < 3 ? int(i) : int(i - 3)},
        {"rank", i < 3 ? Json(ranks[i]) : Json()},
        {"league", i < 3 ? Json(leagues[i]) : Json()}});
  }
  return {{"ok", true}, {"rows", rows}, {"total", total}};
}
} // namespace bmh::modern_ui
