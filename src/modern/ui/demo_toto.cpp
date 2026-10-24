// SPDX-License-Identifier: GPL-3.0-only
#include "modern/ui/demo_toto.hpp"
#include "modern/ui/demo_finance.hpp"
#include "modern/ui/demo_reports.hpp"
#include <algorithm>
#include <array>

namespace bmh::modern_ui {
namespace {
constexpr int column_count = 6, row_count = 11;
constexpr int64_t stake_cap = 99999999;
constexpr std::array<int, 3> picks{1, 0, 2};
bool whole(const Json &v, int64_t lo, int64_t hi) {
  if (v.is_number_unsigned()) return v.get<uint64_t>() <= uint64_t(hi) && (lo <= 0 || v.get<uint64_t>() >= uint64_t(lo));
  return v.is_number_integer() && v.get<int64_t>() >= lo && v.get<int64_t>() <= hi;
}
bool keys(const Json &v, std::initializer_list<const char *> names) {
  return v.is_object() && v.size() == names.size() &&
      std::all_of(names.begin(), names.end(), [&](const auto *name) { return v.contains(name); });
}
bool columns_valid(const Json &v) {
  return v.is_array() && v.size() == column_count && std::all_of(v.begin(), v.end(), [](const auto &col) {
    return col.is_array() && col.size() == row_count && std::all_of(col.begin(), col.end(), [](const auto &p) { return whole(p, -1, 2); });
  });
}
int filled(const Json &col) {
  return int(std::count_if(col.begin(), col.end(), [](const auto &p) { return p != -1; }));
}
bool ready(const Json &cols) {
  bool any = false;
  for (const auto &col : cols) {
    const int n = filled(col);
    if (n && n != row_count) return false;
    any |= n == row_count;
  }
  return any;
}
bool stake_valid(const Json &v, int64_t factor, bool positive = false) {
  return whole(v, positive ? factor : 0, stake_cap * factor) && v.get<int64_t>() % factor == 0;
}
Json initial(int64_t factor) {
  auto cols = Json::array();
  for (int c = 0; c < column_count; ++c) {
    auto col = Json::array();
    for (int r = 0; r < row_count; ++r) col.push_back(c ? -1 : picks[size_t(r % 3)]);
    cols.push_back(col);
  }
  return {{"columns", cols}, {"stake", 1000 * factor}, {"coupon", nullptr}};
}
} // namespace
bool valid_demo_toto(const Json &state, const Catalog &world) {
  try {
    const auto factor = finance_currency_factor(world);
    if (!state.is_object() || !state.contains("toto")) return false;
    const auto &v = state.at("toto");
    if (!keys(v, {"columns", "stake", "coupon"}) || !columns_valid(v.at("columns")) || !stake_valid(v.at("stake"), factor)) return false;
    const auto &coupon = v.at("coupon");
    return coupon.is_null() || (keys(coupon, {"columns", "stake"}) && columns_valid(coupon.at("columns")) &&
        ready(coupon.at("columns")) && stake_valid(coupon.at("stake"), factor, true));
  } catch (const std::exception &) { return false; }
}
bool migrate_demo_toto(Json &state, const Catalog &world) {
  try {
    if (!state.is_object()) return false;
    auto next = state;
    if (!next.contains("toto")) next["toto"] = initial(finance_currency_factor(world));
    if (!valid_demo_toto(next, world)) return false;
    state = std::move(next);
    return true;
  } catch (const std::exception &) { return false; }
}
Json demo_toto_programme(const Catalog &world) {
  Json rows = Json::array();
  const auto leagues = world.leagues("nation:de");
  auto reports = demo_reports(world);
  for (size_t i = 0; i < std::min<size_t>(2, leagues.size()); ++i) {
    if (!edit_demo_reports(reports, {{"kind", "league"}, {"value", leagues[i]->at("id")}}, world).empty()) continue;
    reports["round"] = std::min(26, report_round_count(world, reports) - 1);
    const auto matches = report_match_rows(world, reports, false);
    const size_t limit = i == 0 ? 9 : 2;
    for (size_t n = 0; n < std::min(matches.size(), limit); ++n)
      rows.push_back({{"home", matches[n].at("home")}, {"away", matches[n].at("away")},
          {"league", leagues[i]->at("id")}, {"tier", i + 1}, {"round", reports.at("round").get<int>() + 1}});
  }
  auto start = world.manifest().value("start_date", std::string(""));
  std::string season = "—";
  if (start.size() >= 4 && std::all_of(start.begin(), start.begin() + 4, [](char c) { return c >= '0' && c <= '9'; })) {
    const auto year = std::stoi(start.substr(0, 4));
    const auto next = std::to_string((year + 1) % 100);
    season = start.substr(0, 4) + "/" + (next.size() == 1 ? "0" : "") + next;
  }
  return {{"rows", rows}, {"complete", rows.size() == row_count}, {"round", 27}, {"season", season}};
}
std::string edit_demo_toto(Json &state, const Json &action, const Catalog &world) {
  if (!valid_demo_toto(state, world)) return "invalid_toto_state";
  if (!action.is_object() || !action.contains("kind") || !action.at("kind").is_string()) return "invalid_toto_action";
  const auto kind = action.at("kind").get<std::string>();
  auto next = state.at("toto");
  if (kind == "pick" || kind == "auto" || kind == "clear") {
    if (!action.contains("column") || !whole(action.at("column"), 0, 5)) return "invalid_toto_column";
    auto &col = next["columns"][action.at("column").get<size_t>()];
    if (kind == "pick") {
      if (!action.contains("row") || !whole(action.at("row"), 0, 10) ||
          !action.contains("pick") || !whole(action.at("pick"), 0, 2)) return "invalid_toto_pick";
      auto &value = col[action.at("row").get<size_t>()];
      value = value == action.at("pick") ? Json(-1) : action.at("pick");
    } else for (int r = 0; r < row_count; ++r)
      col[size_t(r)] = kind == "clear" ? -1 : picks[size_t((r + action.at("column").get<int>() + 1) % 3)];
  } else if (kind == "stake") {
    if (!action.contains("value") || !whole(action.at("value"), 0, stake_cap)) return "invalid_toto_stake";
    next["stake"] = action.at("value").get<int64_t>() * finance_currency_factor(world);
  } else if (kind == "submit") {
    if (!ready(next.at("columns")) || next.at("stake") == 0) return "incomplete_toto_coupon";
    if (!demo_toto_programme(world).at("complete").get<bool>()) return "unavailable_toto_programme";
    next["coupon"] = {{"columns", next.at("columns")}, {"stake", next.at("stake")}};
  } else return "unsupported_toto_action";
  auto candidate = state;
  candidate["toto"] = std::move(next);
  if (!valid_demo_toto(candidate, world)) return "invalid_toto_action";
  state = std::move(candidate);
  return {};
}
Json demo_toto_summary(const Json &state, const Catalog &world) {
  if (!valid_demo_toto(state, world)) return {{"ok", false}};
  Json counts = Json::array(); int complete = 0, partial = 0;
  for (const auto &col : state.at("toto").at("columns")) {
    const auto n = filled(col); counts.push_back(n); complete += n == row_count; partial += n && n != row_count;
  }
  return {{"ok", true}, {"counts", counts}, {"complete", complete}, {"partial", partial},
      {"can_submit", complete && !partial && state.at("toto").at("stake") != 0}};
}
} // namespace bmh::modern_ui
