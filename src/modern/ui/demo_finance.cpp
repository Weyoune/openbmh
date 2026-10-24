// SPDX-License-Identifier: GPL-3.0-only
#include "modern/ui/demo_finance.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>

namespace bmh::modern_ui {
namespace {
constexpr int64_t money_cap = 99999999, debt_cap = 5000000, salary_cap = 200000;
constexpr std::array<int64_t, 6> property_prices{115000, 340000, 470000, 780000, 1350000, 3650000};
constexpr std::array<const char *, 6> property_names{"Eigentumswohnung", "Mehrfamilienhaus",
    "Einfamilienhaus", "Villa", "Hochhaus", "Bürogebäude"};
constexpr std::array<int64_t, 12> stock_prices{124, 86, 207, 59, 310, 145, 97, 225, 68, 178, 112, 264};
constexpr std::array<const char *, 12> stock_names{"MainTech", "Nordlicht", "Hafenwerk", "Bergstrom",
    "Solaris", "Westbahn", "Rheinwerk", "Ballwerk", "Feldpunkt", "Stadtfunk", "Werkbank", "Sportnetz"};
constexpr std::array<int64_t, 7> incomes{134000, 92000, 86200, 950000, 120000, 50000, 25000};
constexpr std::array<const char *, 7> income_names{"Trikotwerbung", "Bandenwerbung", "Fernsehrechte",
    "Zuschauer", "Stadionperipherie", "Mieten, Zinsen, Tilgung", "Sonstiges"};
constexpr std::array<int64_t, 11> costs{650000, 420000, 80000, 120000, 60000, 45000,
    250000, 50000, 35000, 0, 30000};
constexpr std::array<const char *, 11> cost_names{"Gehälter und Prämien", "Stadionkosten", "Nachwuchs",
    "Co-Trainer", "Arzt", "Physio", "Trainingslager", "Fanbetreuung", "PR-Arbeit",
    "Zinsen und Tilgung", "Sonstiges"};
constexpr std::array<const char *, 7> bonus_names{"Torprämie", "Punktprämie", "Auflaufprämie",
    "Meisterprämie", "Prämie Pl. 2–5", "Prämie Pl. 6–10", "Nichtabstiegsprämie"};
bool keys(const Json &v, std::initializer_list<const char *> expected) {
  return v.is_object() && v.size() == expected.size() &&
      std::all_of(expected.begin(), expected.end(), [&](auto *key) { return v.contains(key); });
}
bool integer(const Json &v, int64_t lo, int64_t hi) {
  if (v.is_number_unsigned())
    return hi >= 0 && v.get<uint64_t>() <= uint64_t(hi) &&
           (lo <= 0 || v.get<uint64_t>() >= uint64_t(lo));
  return v.is_number_integer() && v.get<int64_t>() >= lo && v.get<int64_t>() <= hi;
}
bool list(const Json &v, size_t size, int64_t lo, int64_t hi) {
  return v.is_array() && v.size() == size && std::all_of(v.begin(), v.end(),
      [&](const auto &item) { return integer(item, lo, hi); });
}
Json error(const char *code) { return {{"ok", false}, {"error", code}}; }
std::string own(const Catalog &world, const std::string &managed, const Json *state = nullptr) {
  if (!managed.empty()) {
    if (!world.manageable(managed)) throw editor::Error("invalid_finance_manager");
    return managed;
  }
  if (state && state->contains("managers")) {
    const auto &managers = state->at("managers");
    if (!managers.is_array() || managers.empty() || !managers[0].is_object() ||
        !managers[0].contains("club") || !managers[0]["club"].is_string())
      throw editor::Error("invalid_finance_manager");
    if (managers[0]["club"].get<std::string>().empty())
      throw editor::Error("invalid_finance_manager");
    return own(world, managers[0]["club"].get<std::string>());
  }
  for (const auto *club : world.clubs())
    if (world.manageable(club->value("id", ""))) return club->value("id", "");
  return "";
}
bool valid_date(const Json &v) {
  if (!v.is_string()) return false;
  const auto d = v.get<std::string>();
  if (d.size() != 10 || d[4] != '-' || d[7] != '-') return false;
  for (size_t i = 0; i < d.size(); ++i)
    if (i != 4 && i != 7 && (d[i] < '0' || d[i] > '9')) return false;
  const int y = std::stoi(d.substr(0, 4)), m = std::stoi(d.substr(5, 2)), day = std::stoi(d.substr(8, 2));
  return y > 0 && std::chrono::year_month_day{std::chrono::year{y},
      std::chrono::month{unsigned(m)}, std::chrono::day{unsigned(day)}}.ok();
}
Json initial_contract(const Catalog &world, const Json &p, size_t index, int64_t factor) {
  const auto date = world.manifest().value("start_date", Json("2026-07-01"));
  std::vector<const Json *> active;
  if (valid_date(date)) for (const auto &c : world.rows("contracts")) {
    if (c.value("person", Json()) != p.at("id") ||
        c.value("team", Json()) != p.value("team", Json())) continue;
    bool in_range = true;
    for (const auto *bound : {"start", "end"})
      if (c.contains(bound) && (!valid_date(c.at(bound)) ||
          (std::string(bound) == "start" ? date < c.at(bound) : date > c.at(bound)))) in_range = false;
    if (in_range) active.push_back(&c);
  }
  Json salary = nullptr;
  std::string source = "unavailable";
  if (active.size() == 1) {
    const auto &c = *active[0];
    if (c.contains("salary") && integer(c.at("salary"), 0, salary_cap * factor * 12)) {
      // OBW stores a season salary. The editable preview uses whole monthly
      // display units, rounded half up; no Classic initializer is invoked.
      const auto raw = c.at("salary").get<int64_t>();
      const auto monthly = (raw + 6 * factor) / (12 * factor);
      salary = monthly * factor;
      source = "obw_season";
    } else if (!c.contains("salary") && c.contains("initialization") && c.at("initialization").is_object()) {
      salary = (4500 + int64_t(index % 40) * 750) * factor;
      source = "demo";
    }
  }
  return {{"salary", salary}, {"months", 24 + int(index % 3) * 12},
          {"insurance", 0}, {"source", source}};
}
Json default_finance() {
  return {{"term", 12}, {"properties", {1, 0, 0, 0, 0, 0}},
          {"stocks", Json::array({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0})}};
}
Json default_balance(int64_t factor) {
  Json plan = Json::array();
  for (const auto value : costs) plan.push_back(value * factor);
  return {{"plan", plan}};
}
Json attribute(const Json &player, const char *key) {
  const auto found = player.find("attributes");
  if (found == player.end() || !found->is_object()) return nullptr;
  const auto value = found->find(key);
  return value != found->end() && value->is_number() && std::isfinite(value->get<double>())
      ? *value : Json(nullptr);
}
std::string position(const Json &p) {
  const auto found = p.find("positions");
  if (found == p.end() || !found->is_array()) return "";
  if (std::find(found->begin(), found->end(), "gk") != found->end()) return "gk";
  for (const auto &v : *found) if (v.is_string()) return v.get<std::string>();
  return "";
}
Json age(const Json &player, const Json &state) {
  if (player.contains("birth_date") && valid_date(player.at("birth_date")) &&
      state.contains("date") && valid_date(state.at("date"))) {
    const auto birth = player.at("birth_date").get<std::string>(), today = state.at("date").get<std::string>();
    const int years = std::stoi(today.substr(0, 4)) - std::stoi(birth.substr(0, 4)) -
                      (today.substr(5) < birth.substr(5) ? 1 : 0);
    return years >= 0 && years <= 100 ? Json(years) : Json(nullptr);
  }
  const auto initial = player.value("age_at_start", Json());
  return integer(initial, 0, 100) ? initial : Json(nullptr);
}
std::string source_label(const std::string &source) {
  return source == "obw_season" ? "Monatsvorschau aus OBW-Saisongehalt · Laufzeit fiktiv"
      : source == "demo" ? "Fiktives Demogehalt · Laufzeit fiktiv"
      : source == "offer" ? "Lokales Demoangebot" : "Kein Gehalt überliefert · Laufzeit fiktiv";
}
} // namespace

int64_t finance_currency_factor(const Catalog &world) {
  const auto currency = world.manifest().value("currency", Json::object());
  if (!currency.is_object()) throw editor::Error("unsupported_currency_precision");
  const auto precision = currency.value("minor_units", Json(0));
  if (!integer(precision, 0, 2) || precision == 1)
    throw editor::Error("unsupported_currency_precision");
  return precision == 2 ? 100 : 1;
}
Json demo_contracts(const Catalog &world, const std::string &managed) {
  const auto club = own(world, managed);
  const auto factor = finance_currency_factor(world);
  Json players = Json::object();
  size_t index = 0;
  if (!club.empty()) for (const auto *p : world.squad(club))
    players[p->at("id").get<std::string>()] = initial_contract(world, *p, index++, factor);
  return {{"club", club}, {"players", players}, {"bonuses", {0, 0, 0, 0, 0, 0, 0}}};
}
bool valid_demo_finance(const Json &state, const Catalog &world, const std::string &managed) {
  try {
    if (!state.is_object()) return false;
    const auto factor = finance_currency_factor(world), cap = money_cap * factor;
    for (const auto *key : {"cash", "deposit", "debt"})
      if (!state.contains(key) || !integer(state.at(key), 0, std::string(key) == "debt" ? debt_cap * factor : cap)) return false;
    if (state.contains("ticket") && !integer(state.at("ticket"), 0, 25)) return false;
    const auto &f = state.at("finance"), &b = state.at("balance"), &c = state.at("contracts");
    if (!keys(f, {"term", "properties", "stocks"}) || !integer(f.at("term"), 6, 60) ||
        f.at("term").get<int>() % 6 || !list(f.at("properties"), 6, 0, 9999) ||
        !list(f.at("stocks"), 12, 0, 9999) || !keys(b, {"plan"}) ||
        !list(b.at("plan"), 11, 0, cap) || !keys(c, {"club", "players", "bonuses"}) ||
        !c.at("club").is_string() || !c.at("players").is_object() ||
        !list(c.at("bonuses"), 7, 0, 255)) return false;
    const auto club = own(world, managed, &state);
    const auto squad = club.empty() ? std::vector<const Json *>{} : world.squad(club);
    if (c.at("club") != club || c.at("players").size() != squad.size()) return false;
    for (const auto *p : squad) {
      const auto &v = c.at("players").at(p->at("id").get<std::string>());
      if (!keys(v, {"salary", "months", "insurance", "source"}) ||
          !integer(v.at("months"), 1, 48) || !integer(v.at("insurance"), 0, 24) ||
          !(v.at("insurance") == 0 || v.at("insurance") == 12 || v.at("insurance") == 24) ||
          !v.at("source").is_string()) return false;
      const auto source = v.at("source").get<std::string>();
      if (source == "unavailable") { if (!v.at("salary").is_null()) return false; }
      else if ((source != "demo" && source != "obw_season" && source != "offer") ||
               !integer(v.at("salary"), 0, salary_cap * factor)) return false;
    }
    return true;
  } catch (const std::exception &) { return false; }
}
bool migrate_demo_finance(Json &state, const Catalog &world, const std::string &managed) {
  try {
    if (!state.is_object()) return false;
    auto next = state;
    const auto factor = finance_currency_factor(world);
    if (!next.contains("finance")) next["finance"] = default_finance();
    if (!next.contains("balance")) next["balance"] = default_balance(factor);
    if (!next.contains("contracts")) next["contracts"] = demo_contracts(world, own(world, managed, &state));
    if (!valid_demo_finance(next, world, managed)) return false;
    state = std::move(next);
    return true;
  } catch (const std::exception &) { return false; }
}
Json demo_bank_terms() { return Json::array({6, 12, 18, 24, 30, 36, 42, 48, 54, 60}); }
Json demo_bank_assets(const Catalog &world) {
  const auto factor = finance_currency_factor(world);
  Json properties = Json::array(), stocks = Json::array();
  for (size_t i = 0; i < property_prices.size(); ++i)
    properties.push_back({{"index", i}, {"name", property_names[i]}, {"price", property_prices[i] * factor}});
  for (size_t i = 0; i < stock_prices.size(); ++i)
    stocks.push_back({{"index", i}, {"name", stock_names[i]}, {"price", stock_prices[i] * factor},
        {"previous", (stock_prices[i] - (int(i % 3) - 1) * 3) * factor}});
  return {{"properties", properties}, {"stocks", stocks}};
}
Json demo_bank_quote(const Json &state, const Json &request, const Catalog &world) {
  if (!valid_demo_finance(state, world)) return error("invalid_finance_state");
  try {
    if (!request.is_object() || !request.contains("kind") || !request.at("kind").is_string())
      return error("invalid_finance_action");
    const auto kind = request.at("kind").get<std::string>();
    const bool property = kind == "property-buy" || kind == "property-sell";
    const bool stock = kind == "stock-buy" || kind == "stock-sell";
    const bool quantity = property || stock;
    if (!(quantity ? keys(request, {"kind", "index"}) : keys(request, {"kind"})))
      return error("invalid_finance_action");
    const auto factor = finance_currency_factor(world), cap = money_cap * factor;
    const auto cash = state.at("cash").get<int64_t>(), deposit = state.at("deposit").get<int64_t>(), debt = state.at("debt").get<int64_t>();
    int64_t max = 0, price = factor;
    Json index = nullptr;
    if (quantity) {
      if (!integer(request.at("index"), 0, property ? 5 : 11)) return error("invalid_finance_action");
      const auto i = request.at("index").get<size_t>();
      index = i;
      price = (property ? property_prices[i] : stock_prices[i]) * factor;
      const auto owned = state.at("finance").at(property ? "properties" : "stocks")[i].get<int64_t>();
      max = kind.ends_with("buy") ? std::min<int64_t>(9999 - owned, cash / price)
          : std::min<int64_t>(owned, (cap - cash) / price);
    } else if (kind == "deposit") max = std::min(cash, cap - deposit) / factor;
    else if (kind == "withdraw") max = std::min(deposit, cap - cash) / factor;
    else if (kind == "loan") max = debt ? 0 : std::min(debt_cap * factor, cap - cash) / factor;
    else if (kind == "repay") max = std::min(cash, debt) / factor;
    else return error("unknown_finance_action");
    return {{"ok", true}, {"kind", kind}, {"min", 1}, {"max", max}, {"quantity", quantity},
            {"index", index}, {"unit_price", price}, {"step", quantity ? 1 : 100000}};
  } catch (const std::exception &) { return error("invalid_finance_action"); }
}
std::string edit_demo_finance(Json &state, const Json &action, const Catalog &world,
                              const std::string &managed) {
  if (!valid_demo_finance(state, world, managed)) return "invalid_finance_state";
  try {
    if (!action.is_object() || !action.contains("kind") || !action.at("kind").is_string())
      return "invalid_finance_action";
    const auto kind = action.at("kind").get<std::string>();
    auto next = state;
    if (kind == "term") {
      if (!keys(action, {"kind", "value"}) || !integer(action.at("value"), 6, 60) ||
          action.at("value").get<int>() % 6 || state.at("debt") != 0) return "invalid_finance_value";
      next["finance"]["term"] = action.at("value");
    } else if (kind == "plan" || kind == "bonus") {
      if (!keys(action, {"kind", "index", "value"}) || !integer(action.at("index"), 0, kind == "plan" ? 10 : 6) ||
          !integer(action.at("value"), 0, kind == "plan" ? money_cap : 255)) return "invalid_finance_value";
      const auto i = action.at("index").get<size_t>();
      const auto value = action.at("value").get<int64_t>();
      if (kind == "plan") {
        if (value % 1000) return "invalid_finance_value";
        next["balance"]["plan"][i] = value * finance_currency_factor(world);
      } else next["contracts"]["bonuses"][i] = value;
    } else if (kind == "contract-offer" || kind == "insurance") {
      if (!(kind == "contract-offer" ? keys(action, {"kind", "player", "salary", "months"})
                                     : keys(action, {"kind", "player", "value"})) ||
          !action.at("player").is_string()) return "invalid_finance_action";
      const auto id = action.at("player").get<std::string>();
      if (!state.at("contracts").at("players").contains(id)) return "invalid_finance_player";
      auto &contract = next["contracts"]["players"][id];
      if (kind == "contract-offer") {
        if (!integer(action.at("salary"), 0, salary_cap) || !integer(action.at("months"), 1, 48))
          return "invalid_finance_value";
        contract["salary"] = action.at("salary").get<int64_t>() * finance_currency_factor(world);
        contract["months"] = action.at("months");
        contract["source"] = "offer";
      } else {
        if (!integer(action.at("value"), 0, 24) ||
            !(action.at("value") == 0 || action.at("value") == 12 || action.at("value") == 24))
          return "invalid_finance_value";
        contract["insurance"] = action.at("value");
      }
    } else {
      const bool quantity = kind == "property-buy" || kind == "property-sell" || kind == "stock-buy" || kind == "stock-sell";
      if (!(quantity ? keys(action, {"kind", "index", "value"}) : keys(action, {"kind", "value"})))
        return "invalid_finance_action";
      auto request = action;
      request.erase("value");
      const auto quote = demo_bank_quote(state, request, world);
      if (!quote.value("ok", false)) return quote.at("error").get<std::string>();
      if (!integer(action.at("value"), 1, quote.at("max").get<int64_t>())) return "invalid_finance_value";
      const auto value = action.at("value").get<int64_t>(), total = value * quote.at("unit_price").get<int64_t>();
      const auto cash = state.at("cash").get<int64_t>(), deposit = state.at("deposit").get<int64_t>(), debt = state.at("debt").get<int64_t>();
      if (kind == "deposit") { next["cash"] = cash - total; next["deposit"] = deposit + total; }
      else if (kind == "withdraw") { next["cash"] = cash + total; next["deposit"] = deposit - total; }
      else if (kind == "loan") { next["cash"] = cash + total; next["debt"] = total; }
      else if (kind == "repay") { next["cash"] = cash - total; next["debt"] = debt - total; }
      else {
        const bool buying = kind.ends_with("buy"), property = kind.starts_with("property-");
        const auto i = action.at("index").get<size_t>();
        next["cash"] = cash + (buying ? -total : total);
        next["finance"][property ? "properties" : "stocks"][i] =
            state.at("finance").at(property ? "properties" : "stocks")[i].get<int64_t>() + (buying ? value : -value);
      }
    }
    if (!valid_demo_finance(next, world, managed)) return "invalid_finance_state";
    state = std::move(next);
    return {};
  } catch (const std::exception &) { return "invalid_finance_action"; }
}
Json demo_bank_summary(const Json &state, const Catalog &world) {
  if (!valid_demo_finance(state, world)) return error("invalid_finance_state");
  const auto factor = finance_currency_factor(world);
  const auto cash = state.at("cash").get<int64_t>(), deposit = state.at("deposit").get<int64_t>(), debt = state.at("debt").get<int64_t>();
  int64_t properties = 0, stocks = 0;
  for (size_t i = 0; i < property_prices.size(); ++i)
    properties += property_prices[i] * factor * state.at("finance").at("properties")[i].get<int64_t>();
  for (size_t i = 0; i < stock_prices.size(); ++i)
    stocks += stock_prices[i] * factor * state.at("finance").at("stocks")[i].get<int64_t>();
  const int64_t term = state.at("finance").at("term"), principal = debt ? debt : 1000000 * factor;
  const int64_t denominator = 150 * term * factor;
  const int64_t monthly = ((debt * (150 + term) + denominator - 1) / denominator) * factor;
  Json schedule = Json::array();
  for (int month = 0; month <= term; ++month)
    schedule.push_back({{"month", month}, {"remaining", (principal * (term - month) + term / 2) / term}});
  return {{"ok", true}, {"cash", cash}, {"deposit", deposit}, {"debt", debt},
      {"properties", properties}, {"stocks", stocks}, {"net", cash + deposit + properties + stocks - debt},
      {"term", term}, {"savings_rate", 2.5}, {"credit_rate", 8},
      {"savings_interest", (deposit * 25 + 500) / 1000}, {"credit_interest", (debt * 8 + 50) / 100},
      {"monthly_rate", monthly}, {"schedule_principal", principal}, {"schedule_example", debt == 0},
      {"schedule", schedule}};
}
Json demo_balance_rows(const Json &state, const Catalog &world) {
  if (!valid_demo_finance(state, world)) return error("invalid_finance_state");
  const auto factor = finance_currency_factor(world);
  Json in = Json::array(), out = Json::array(), plan = Json::array();
  int64_t sum_in = 0, sum_out = 0, sum_plan = 0;
  for (size_t i = 0; i < incomes.size(); ++i) {
    const auto value = incomes[i] * factor;
    in.push_back({{"name", income_names[i]}, {"amount", value}}); sum_in += value;
  }
  for (size_t i = 0; i < costs.size(); ++i) {
    const auto actual = costs[i] * factor, planned = state.at("balance").at("plan")[i].get<int64_t>();
    out.push_back({{"name", cost_names[i]}, {"amount", actual}});
    plan.push_back({{"name", cost_names[i]}, {"amount", planned}}); sum_out += actual; sum_plan += planned;
  }
  return {{"ok", true}, {"income", in}, {"costs", out}, {"plan", plan},
      {"income_total", sum_in}, {"cost_total", sum_out}, {"plan_total", sum_plan}, {"balance", sum_in - sum_out},
      {"cash", state.at("cash")}, {"ticket", state.value("ticket", 0) * factor},
      {"league_home_games", 14}, {"other_home_games", 4}, {"attendance_average", 28450}};
}
Json demo_contract_rows(const Json &state, const Catalog &world, const std::string &managed) {
  if (!valid_demo_finance(state, world, managed)) return error("invalid_finance_state");
  const auto club = own(world, managed, &state);
  Json rows = Json::array();
  int64_t total = 0, known = 0, unknown = 0;
  if (!club.empty()) for (const auto *p : world.squad(club)) {
    const auto &c = state.at("contracts").at("players").at(p->at("id").get<std::string>());
    if (c.at("salary").is_null()) ++unknown;
    else {
      const auto salary = c.at("salary").get<int64_t>();
      if (total > std::numeric_limits<int64_t>::max() - salary) return error("finance_sum_overflow");
      total += salary; ++known;
    }
    rows.push_back({{"id", p->at("id")}, {"name", world.name(*p)},
        {"position", position(*p).empty() ? Json(nullptr) : Json(position(*p))},
        {"age", age(*p, state)},
        {"strength", attribute(*p, "strength")}, {"condition", attribute(*p, "condition")},
        {"technique", attribute(*p, "technique")}, {"form", attribute(*p, "form")},
        {"shoot", attribute(*p, "shooting")}, {"pass", attribute(*p, "passing")},
        {"team", attribute(*p, "teamplay")}, {"games", attribute(*p, "games")},
        {"grade", attribute(*p, "grade")},
        {"positions", p->value("positions", Json::array())},
        {"attributes", p->value("attributes", Json::object())},
        {"birth_date", p->value("birth_date", Json())},
        {"salary", c.at("salary")}, {"months", c.at("months")}, {"insurance", c.at("insurance")},
        {"source", c.at("source")}, {"source_label", source_label(c.at("source").get<std::string>())}});
  }
  Json bonuses = Json::array();
  for (size_t i = 0; i < bonus_names.size(); ++i)
    bonuses.push_back({{"name", bonus_names[i]}, {"value", state.at("contracts").at("bonuses")[i]}});
  return {{"ok", true}, {"club", club}, {"rows", rows}, {"salary_total", known ? Json(total) : Json(nullptr)},
      {"salary_known", known}, {"salary_unknown", unknown}, {"bonuses", bonuses}};
}
} // namespace bmh::modern_ui
