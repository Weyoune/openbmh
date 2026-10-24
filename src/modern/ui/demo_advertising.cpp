// SPDX-License-Identifier: GPL-3.0-only
#include "modern/ui/demo_advertising.hpp"
#include "modern/ui/demo_finance.hpp"
#include <algorithm>
#include <array>

namespace bmh::modern_ui {
namespace {
constexpr int64_t cap = 99999999;
constexpr std::array<const char *, 10> sponsors{"MAINWERK", "BALLWERK", "NORDLICHT", "FELDPUNKT", "RHEINWERK",
    "SPORTNETZ", "WESTBAHN", "HAFENWERK", "BERGSTROM", "STADTFUNK"};
constexpr std::array<const char *, 5> broadcasters{"SPORTKANAL", "FUSSBALL TV", "SPIELTAG TV", "ARENA PLUS", "LIGA LIVE"};
constexpr std::array<const char *, 3> spending_names{"Fanartikel", "Öffentlichkeitsarbeit", "Fanbetreuung"};
bool keys(const Json &v, std::initializer_list<const char *> expected) {
  return v.is_object() && v.size() == expected.size() && std::all_of(expected.begin(), expected.end(),
      [&](const auto *key) { return v.contains(key); });
}
bool integer(const Json &v, int64_t low, int64_t high) {
  if (v.is_number_unsigned())
    return high >= 0 && v.get<uint64_t>() <= uint64_t(high) &&
           (low <= 0 || v.get<uint64_t>() >= uint64_t(low));
  return v.is_number_integer() && v.get<int64_t>() >= low && v.get<int64_t>() <= high;
}
Json error(const char *code) { return {{"ok", false}, {"error", code}}; }
std::string label(int slot) {
  return slot < 14 ? "Bande " + std::to_string(slot + 1) : slot == 14 ? "Trikotwerbung" : "TV-Rechte";
}
std::string name(int slot, int sponsor) {
  return sponsor < 0 ? "frei" : slot == 15 ? broadcasters[size_t(sponsor)] : sponsors[size_t(sponsor)];
}
Json offer(int slot, int choice, int64_t factor) {
  return {{"sponsor", (slot * 3 + choice) % (slot == 15 ? 5 : 10)},
      {"months", (choice + 1) * 12},
      {"amount", ((slot == 15 ? 85000 : slot == 14 ? 110000 : 7000) +
          (slot == 15 ? 18000 : slot == 14 ? 24000 : 1800) * choice + (slot % 7) * 1200) * factor}};
}
} // namespace
bool valid_demo_advertising(const Json &state, const Catalog &world) {
  try {
    if (!state.is_object()) return false;
    const auto factor = finance_currency_factor(world);
    const auto &a = state.at("advertising");
    if (!keys(a, {"spending", "contracts"}) || !a.at("spending").is_array() || a.at("spending").size() != 3 ||
        !a.at("contracts").is_array() || a.at("contracts").size() != 16) return false;
    for (const auto &value : a.at("spending")) if (!integer(value, 0, cap * factor)) return false;
    for (size_t slot = 0; slot < 16; ++slot) {
      const auto &c = a.at("contracts")[slot];
      if (!keys(c, {"sponsor", "months", "amount"}) || !integer(c.at("sponsor"), -1, slot == 15 ? 4 : 9) ||
          !integer(c.at("months"), 0, 36) || !integer(c.at("amount"), 0, cap * factor)) return false;
      if (c.at("sponsor") == -1) {
        if (c.at("months") != 0 || c.at("amount") != 0) return false;
      } else if (!(c.at("months") == 12 || c.at("months") == 24 || c.at("months") == 36) ||
                 c.at("amount") == 0) return false;
    }
    return true;
  } catch (const std::exception &) { return false; }
}
bool migrate_demo_advertising(Json &state, const Catalog &world) {
  try {
    if (!state.is_object()) return false;
    if (state.contains("advertising")) return valid_demo_advertising(state, world);
    const auto factor = finance_currency_factor(world);
    auto next = state;
    Json contracts = Json::array();
    for (int slot = 0; slot < 16; ++slot) contracts.push_back({{"sponsor", -1}, {"months", 0}, {"amount", 0}});
    next["advertising"] = {{"spending", {50000 * factor, 15000 * factor, 10000 * factor}}, {"contracts", contracts}};
    if (!valid_demo_advertising(next, world)) return false;
    state = std::move(next);
    return true;
  } catch (const std::exception &) { return false; }
}
Json demo_advertising_quote(const Json &state, int slot, int choice, const Catalog &world) {
  if (!valid_demo_advertising(state, world)) return error("invalid_advertising_state");
  if (slot < 0 || slot > 15 || choice < 0 || choice > 2) return error("invalid_advertising_selection");
  const auto &saved = state.at("advertising").at("contracts")[size_t(slot)];
  const bool active = saved.at("sponsor") != -1;
  const auto c = active ? saved : offer(slot, choice, finance_currency_factor(world));
  return {{"ok", true}, {"slot", slot}, {"offer", choice}, {"active", active}, {"label", label(slot)},
      {"name", name(slot, c.at("sponsor").get<int>())},
      {"sponsor", c.at("sponsor")}, {"months", c.at("months")}, {"amount", c.at("amount")}};
}
std::string edit_demo_advertising(Json &state, const Json &action, const Catalog &world) {
  if (!valid_demo_advertising(state, world)) return "invalid_advertising_state";
  try {
    if (!action.is_object() || !action.contains("kind") || !action.at("kind").is_string()) return "invalid_advertising_action";
    const auto kind = action.at("kind").get<std::string>();
    auto next = state;
    if (kind == "accept") {
      if (!keys(action, {"kind", "slot", "offer"}) || !integer(action.at("slot"), 0, 15) ||
          !integer(action.at("offer"), 0, 2)) return "invalid_advertising_selection";
      const auto slot = action.at("slot").get<size_t>();
      if (state.at("advertising").at("contracts")[slot].at("sponsor") != -1) return "advertising_contract_active";
      next["advertising"]["contracts"][slot] = offer(int(slot), action.at("offer").get<int>(), finance_currency_factor(world));
    } else if (kind == "spending") {
      if (!keys(action, {"kind", "index", "value"}) || !integer(action.at("index"), 0, 2) ||
          !integer(action.at("value"), 0, cap)) return "invalid_advertising_value";
      next["advertising"]["spending"][action.at("index").get<size_t>()] = action.at("value").get<int64_t>() * finance_currency_factor(world);
    } else return "unknown_advertising_action";
    if (!valid_demo_advertising(next, world)) return "invalid_advertising_state";
    state = std::move(next);
    return {};
  } catch (const std::exception &) { return "invalid_advertising_action"; }
}
Json demo_advertising_view(const Json &state, const Catalog &world) {
  if (!valid_demo_advertising(state, world)) return error("invalid_advertising_state");
  Json slots = Json::array(), spending = Json::array();
  int64_t income = 0, expenses = 0;
  const auto &a = state.at("advertising");
  for (int i = 0; i < 16; ++i) {
    const auto &c = a.at("contracts")[size_t(i)];
    const auto sponsor = c.at("sponsor").get<int>();
    slots.push_back({{"slot", i}, {"label", label(i)}, {"name", name(i, sponsor)}, {"sponsor", sponsor},
        {"months", c.at("months")}, {"amount", c.at("amount")}, {"active", sponsor >= 0}});
    income += c.at("amount").get<int64_t>();
  }
  for (int i = 0; i < 3; ++i) {
    const auto value = a.at("spending")[size_t(i)].get<int64_t>();
    spending.push_back({{"index", i}, {"name", spending_names[size_t(i)]}, {"amount", value}, {"suggestion_step", i == 0 ? 50000 : 5000}});
    expenses += value;
  }
  return {{"ok", true}, {"slots", slots}, {"spending", spending}, {"income", income}, {"expenses", expenses}, {"balance", income - expenses}};
}
} // namespace bmh::modern_ui
