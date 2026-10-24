// SPDX-License-Identifier: GPL-3.0-only
#include "modern/ui/demo_finance.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace bmh::modern_ui;
namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
std::string player(int i) { return "player:finance-test-" + std::to_string(i); }
struct Fixture {
  fs::path root;
  std::shared_ptr<const Catalog> world;
  explicit Fixture(Json precision = 0, bool populated = true) {
    root = fs::temp_directory_path() / ("openbmh-finance-ui-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "world");
    const auto write = [&](const std::string &file, const Json &value) {
      std::ofstream out(root / file); out << value.dump();
      require(bool(out), "Cannot write synthetic catalogue");
    };
    write("manifest.json", {{"format", "openbmh-world"}, {"kind", "world"},
        {"format_version", "1.1"}, {"id", "world:synthetic-finance-ui"},
        {"start_date", "2026-07-01"}, {"currency", {{"code", "EUR"}, {"minor_units", precision}}},
        {"requires", {"editor:world-v1"}}, {"x-editor", {{"profile", "editor-world-v1"}}}});
    write("world/nations.json", {{"items", {{{"id", "nation:de"}, {"name", "Synthetic nation"}}}}});
    write("world/competitions.json", {{"items", {{{"id", "competition:finance-test"},
        {"name", "Synthetic league"}, {"nation", "nation:de"}, {"type", "league"}}}}});
    Json players = Json::array(), clubs = Json::array(), contracts = Json::array();
    if (populated) {
      clubs = {{{"id", "club:finance-home"}, {"name", "Synthetic home"}, {"nation", "nation:de"},
                {"competition", "competition:finance-test"}},
               {{"id", "club:finance-other"}, {"name", "Synthetic other"}, {"nation", "nation:de"},
                {"competition", "competition:finance-test"}}};
      for (int i = 0; i < 13; ++i) {
        Json p = {{"id", player(i)}, {"name", "Synthetic player " + std::to_string(i)},
            {"team", "club:finance-home"}, {"squad_order", i}, {"positions", {i ? "mid" : "gk"}}};
        if (i == 0) {
          p["birth_date"] = "2000-07-02";
          p["attributes"] = {{"strength", 82}, {"condition", 80}, {"technique", 75}, {"form", 77},
              {"shooting", 61}, {"passing", 72}, {"teamplay", 68}};
        }
        if (i == 1) p["age_at_start"] = 31;
        players.push_back(p);
      }
      players.push_back({{"id", "player:finance-other"}, {"team", "club:finance-other"},
          {"name", "Synthetic other player"}, {"positions", {"def"}}});
      const int64_t factor = precision == 2 ? 100 : 1;
      const auto add = [&](int i, Json salary, Json extra = Json::object()) {
        Json c = {{"id", "contract:synthetic-" + std::to_string(contracts.size())},
            {"person", player(i)}, {"team", "club:finance-home"}};
        if (!salary.is_null()) c["salary"] = salary;
        for (const auto &item : extra.items()) c[item.key()] = item.value();
        contracts.push_back(c);
      };
      add(0, 120000 * factor);
      add(1, nullptr, {{"initialization", {{"policy", "classic-player-contract@1"},
          {"source_identity_word", 65535}, {"source_skill_word", 65535}}}});
      add(3, 0);
      add(4, 120000 * factor); add(4, 240000 * factor);
      add(5, 120000 * factor, {{"end", "2026-06-30"}});
      add(6, 120000 * factor, {{"start", "2026-07-02"}});
      add(7, 120000 * factor, {{"team", "club:finance-other"}});
      add(8, 120006 * factor, {{"start", "2026-07-01"}, {"end", "2026-07-01"}});
      add(9, -1, {{"initialization", Json::object()}});
      add(10, nullptr, {{"initialization", Json::object()}, {"start", "2026-02-30"}});
      add(11, uint64_t(-1));
      add(12, "not-a-salary");
      contracts.push_back({{"id", "contract:synthetic-other"}, {"person", "player:finance-other"},
          {"team", "club:finance-other"}, {"initialization", Json::object()}});
    }
    write("world/clubs.json", {{"items", clubs}});
    write("world/players.json", {{"items", players}});
    write("world/contracts.json", {{"items", contracts}});
    world = Catalog::open(root);
  }
  ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
};
Json legacy(int64_t factor) {
  return {{"cash", 12450000 * factor}, {"deposit", 600000 * factor}, {"debt", 0},
      {"ticket", 13}, {"date", "2026-07-01"},
      {"managers", {{{"club", "club:finance-home"}, {"name", "Synthetic manager"}}}},
      {"unrelated", {{"revision_marker", 97}, {"other_plan", {4, 5, 6}}}}};
}
void edit(Json &state, const Catalog &world, const Json &action) {
  const auto error = edit_demo_finance(state, action, world);
  if (!error.empty()) throw std::runtime_error("Unexpected rejection: " + error + " " + action.dump());
  require(valid_demo_finance(state, world), "Accepted action left invalid state");
}
void reject(Json &state, const Catalog &world, const Json &action) {
  const auto before = state;
  require(!edit_demo_finance(state, action, world).empty() && state == before,
      "Rejected action mutated state or was accepted");
}
int64_t quote(const Json &state, const Catalog &world, const char *kind, int index = -1) {
  Json request = {{"kind", kind}};
  if (index >= 0) request["index"] = index;
  const auto result = demo_bank_quote(state, request, world);
  require(result.value("ok", false) && result.at("min") == 1,
      "Bank quote failed or used zero minimum");
  return result.at("max").get<int64_t>();
}
int64_t net(const Json &state, const Catalog &world) {
  return demo_bank_summary(state, world).at("net").get<int64_t>();
}
void check_currency(int precision) {
  Fixture fixture(precision);
  const auto &world = *fixture.world;
  const auto factor = precision ? 100 : 1;
  const auto original_players = world.rows("players"), original_contracts = world.rows("contracts");
  auto state = legacy(factor);
  const auto original_legacy = state;
  require(!valid_demo_finance(state, world) && migrate_demo_finance(state, world) &&
      valid_demo_finance(state, world), "Finance legacy migration failed");
  require(finance_currency_factor(world) == factor && state.at("cash") == original_legacy.at("cash") &&
      state.at("deposit") == original_legacy.at("deposit") && state.at("debt") == original_legacy.at("debt") &&
      state.at("unrelated") == original_legacy.at("unrelated") && !state["finance"].contains("cash"),
      "Migration replaced sole shared balances or unrelated state");
  const auto &contracts = state.at("contracts").at("players");
  require(contracts.at(player(0)).at("salary") == 10000 * factor &&
      contracts.at(player(0)).at("source") == "obw_season" &&
      contracts.at(player(1)).at("salary") == 5250 * factor &&
      contracts.at(player(1)).at("source") == "demo" &&
      contracts.at(player(1)).at("months") == 36 &&
      contracts.at(player(3)).at("salary") == 0 &&
      contracts.at(player(3)).at("source") == "obw_season" &&
      contracts.at(player(8)).at("salary") == 10001 * factor,
      "Salary season/month conversion, rounding, source or explicit zero incorrect");
  for (int i : {2, 4, 5, 6, 7, 9, 10, 11, 12})
    require(contracts.at(player(i)).at("salary").is_null() &&
        contracts.at(player(i)).at("source") == "unavailable", "Unavailable or ambiguous salary was invented");
  const auto rows = demo_contract_rows(state, world);
  require(rows.at("salary_total") == 25251 * factor && rows.at("salary_known") == 4 &&
      rows.at("salary_unknown") == 9 && rows.at("rows")[0].at("age") == 25 &&
      rows.at("rows")[1].at("age") == 31 && rows.at("rows")[0].at("position") == "gk" &&
      rows.at("rows")[0].at("shoot") == 61 && rows.at("rows")[0].at("pass") == 72 &&
      rows.at("rows")[0].at("team") == 68 && rows.at("rows")[0].at("games").is_null() &&
      rows.at("rows")[0].at("grade").is_null(), "Contract rows lost real values or invented game statistics");
  const auto assets = demo_bank_assets(world);
  require(assets.at("properties").size() == 6 && assets.at("stocks").size() == 12 &&
      assets.at("properties")[5].at("price") == 3650000 * factor &&
      assets.at("stocks")[0].at("price") == 124 * factor &&
      assets.at("stocks")[0].at("previous") == 127 * factor && demo_bank_terms() == Json::array({6,12,18,24,30,36,42,48,54,60}),
      "Gold asset catalogue, terms or currency scaling changed");
  const auto initial = state;
  const auto total = net(state, world);
  require(quote(state, world, "deposit") == 12450000 && quote(state, world, "withdraw") == 600000 &&
      quote(state, world, "loan") == 5000000 && quote(state, world, "repay") == 0,
      "Bank maximum is in raw minor units or ignores balance limits");
  edit(state, world, {{"kind", "deposit"}, {"value", 100000}});
  require(state.at("cash") == 12350000 * factor && state.at("deposit") == 700000 * factor &&
      net(state, world) == total, "Deposit failed value conservation or unit conversion");
  edit(state, world, {{"kind", "withdraw"}, {"value", 100000}});
  require(state.at("cash") == initial.at("cash") && state.at("deposit") == initial.at("deposit"),
      "Withdrawal did not reverse deposit");
  edit(state, world, {{"kind", "term"}, {"value", 24}});
  edit(state, world, {{"kind", "loan"}, {"value", 1000000}});
  require(state.at("debt") == 1000000 * factor && net(state, world) == total &&
      quote(state, world, "loan") == 0, "Loan changed net assets or permits duplicate loans");
  reject(state, world, {{"kind", "term"}, {"value", 12}});
  reject(state, world, {{"kind", "loan"}, {"value", 1}});
  const auto before_projection = state;
  const auto summary = demo_bank_summary(state, world);
  require(summary.at("monthly_rate") == 48334 * factor && summary.at("credit_interest") == 80000 * factor &&
      summary.at("schedule").size() == 25 && summary.at("schedule")[0].at("remaining") == 1000000 * factor &&
      summary.at("schedule").back().at("remaining") == 0 && summary.at("schedule_example") == false && state == before_projection,
      "Interest/rate/schedule projection is incorrect or booked effects");
  edit(state, world, {{"kind", "repay"}, {"value", 500000}});
  edit(state, world, {{"kind", "repay"}, {"value", 500000}});
  require(state.at("cash") == initial.at("cash") && state.at("debt") == 0 && net(state, world) == total,
      "Loan repayment did not conserve balances");
  edit(state, world, {{"kind", "term"}, {"value", 60}});
  require(demo_bank_summary(state, world).at("schedule_example") == true &&
      demo_bank_summary(state, world).at("schedule_principal") == 1000000 * factor,
      "No-loan schedule is not marked as a currency-correct example");
  edit(state, world, {{"kind", "property-buy"}, {"index", 2}, {"value", 2}});
  require(state["finance"]["properties"][2] == 2 && state["finance"]["properties"][0] == 1 &&
      net(state, world) == total, "Property acquisition changed total value");
  edit(state, world, {{"kind", "property-sell"}, {"index", 0}, {"value", 1}});
  require(state["finance"]["properties"][0] == 0 && state["finance"]["properties"][2] == 2 &&
      net(state, world) == total, "Sale ignored chosen owned property type");
  reject(state, world, {{"kind", "property-sell"}, {"index", 0}, {"value", 1}});
  edit(state, world, {{"kind", "stock-buy"}, {"index", 11}, {"value", 1000}});
  require(state["finance"]["stocks"][11] == 1000 && net(state, world) == total,
      "Stock purchase did not conserve synthetic asset value");
  reject(state, world, {{"kind", "stock-sell"}, {"index", 1}, {"value", 1}});
  edit(state, world, {{"kind", "stock-sell"}, {"index", 11}, {"value", 1000}});
  require(state["finance"]["stocks"][11] == 0 && net(state, world) == total, "Stock sale changed total value");
  const auto current_balance = demo_balance_rows(state, world);
  require(current_balance.at("income").size() == 7 && current_balance.at("costs").size() == 11 &&
      current_balance.at("cash") == state.at("cash") && current_balance.at("ticket") == 13 * factor,
      "Balance projection duplicated cash/ticket or lost Gold categories");
  const auto cash = state.at("cash");
  edit(state, world, {{"kind", "plan"}, {"index", 0}, {"value", 1000}});
  require(state.at("cash") == cash && demo_balance_rows(state, world).at("costs") == current_balance.at("costs") &&
      demo_balance_rows(state, world).at("plan")[0].at("amount") == 1000 * factor,
      "Expense plan booked cash or changed fictional actual expenses");
  reject(state, world, {{"kind", "plan"}, {"index", 0}, {"value", 1234}});
  reject(state, world, {{"kind", "plan"}, {"index", 0}, {"value", 99999999}});
  edit(state, world, {{"kind", "plan"}, {"index", 10}, {"value", 99999000}});
  state["balance"]["plan"][1] = 123 * factor + (precision ? 45 : 0);
  const auto legacy_plan = state;
  require(valid_demo_finance(state, world) && migrate_demo_finance(state, world) && state == legacy_plan,
      "Migration repaired or rejected a valid older non-step plan value");
  edit(state, world, {{"kind", "insurance"}, {"player", player(0)}, {"value", 24}});
  edit(state, world, {{"kind", "bonus"}, {"index", 6}, {"value", 255}});
  edit(state, world, {{"kind", "contract-offer"}, {"player", player(0)}, {"salary", 12345}, {"months", 18}});
  require(state["contracts"]["players"][player(0)]["salary"] == 12345 * factor &&
      state["contracts"]["players"][player(0)]["months"] == 18 &&
      state["contracts"]["players"][player(0)]["insurance"] == 24 &&
      state["contracts"]["players"][player(0)]["source"] == "offer" && state.at("cash") == cash,
      "Contract offer converted units twice, reset insurance or charged cash");
  edit(state, world, {{"kind", "contract-offer"}, {"player", player(2)}, {"salary", 0}, {"months", 1}});
  edit(state, world, {{"kind", "contract-offer"}, {"player", player(3)}, {"salary", 200000}, {"months", 48}});
  auto near_cap = initial; near_cap["cash"] = 99999999LL * factor - 2 * factor;
  require(quote(near_cap, world, "loan") == 2 && quote(near_cap, world, "withdraw") == 2 &&
      quote(near_cap, world, "property-sell", 0) == 0, "Cash cap is ignored by loan, withdrawal or sale");
  reject(near_cap, world, {{"kind", "loan"}, {"value", 3}});
  edit(near_cap, world, {{"kind", "loan"}, {"value", 2}});
  require(near_cap["cash"] == 99999999LL * factor && quote(near_cap, world, "withdraw") == 0,
      "Cash cap exact boundary failed");
  auto full_asset = initial; full_asset["finance"]["stocks"][0] = 9998;
  require(quote(full_asset, world, "stock-buy", 0) == 1, "Asset quantity cap missing");
  reject(full_asset, world, {{"kind", "stock-buy"}, {"index", 0}, {"value", 2}});
  auto stale = initial;
  const auto stale_max = quote(stale, world, "stock-buy", 11);
  edit(stale, world, {{"kind", "deposit"}, {"value", quote(stale, world, "deposit")}});
  reject(stale, world, {{"kind", "stock-buy"}, {"index", 11}, {"value", stale_max}});
  auto empty_cash = initial; empty_cash["cash"] = 0;
  require(quote(empty_cash, world, "deposit") == 0 && quote(empty_cash, world, "stock-buy", 0) == 0,
      "Zero cash quote did not disable confirmation");
  reject(empty_cash, world, {{"kind", "deposit"}, {"value", 0}});
  for (const auto &action : std::vector<Json>{nullptr, Json::array(), "bad", Json::object(),
      {{"kind", 1}}, {{"kind", "unknown"}, {"value", 1}}, {{"kind", "deposit"}},
      {{"kind", "deposit"}, {"value", -1}}, {{"kind", "deposit"}, {"value", true}},
      {{"kind", "deposit"}, {"value", 1.0}}, {{"kind", "deposit"}, {"value", uint64_t(-1)}},
      {{"kind", "deposit"}, {"value", 99999999999999LL}},
      {{"kind", "deposit"}, {"value", 1}, {"cash", 0}},
      {{"kind", "property-buy"}, {"index", 6}, {"value", 1}},
      {{"kind", "stock-buy"}, {"index", uint64_t(-1)}, {"value", 1}},
      {{"kind", "stock-buy"}, {"index", 0.0}, {"value", 1}},
      {{"kind", "term"}, {"value", 7}}, {{"kind", "term"}, {"value", 66}},
      {{"kind", "plan"}, {"index", 11}, {"value", 1000}},
      {{"kind", "bonus"}, {"index", 7}, {"value", 1}},
      {{"kind", "bonus"}, {"index", 0}, {"value", 256}},
      {{"kind", "insurance"}, {"player", player(0)}, {"value", 6}},
      {{"kind", "insurance"}, {"player", "player:finance-other"}, {"value", 12}},
      {{"kind", "contract-offer"}, {"player", player(0)}, {"salary", 200001}, {"months", 12}},
      {{"kind", "contract-offer"}, {"player", player(0)}, {"salary", 1000}, {"months", 0}},
      {{"kind", "contract-offer"}, {"player", player(0)}, {"salary", 1000}, {"months", 49}},
      {{"kind", "contract-offer"}, {"player", player(0)}, {"salary", "1000"}, {"months", 12}}})
    reject(state, world, action);
  std::vector<Json> bad_states;
  for (const auto &value : std::vector<Json>{-1, true, 1.5, uint64_t(-1), 99999999999999LL}) {
    auto bad = state; bad["cash"] = value; bad_states.push_back(bad);
  }
  auto bad = state; bad["debt"] = 5000001LL * factor; bad_states.push_back(bad);
  bad = state; bad["finance"]["cash"] = 0; bad_states.push_back(bad);
  bad = state; bad["finance"].erase("term"); bad_states.push_back(bad);
  bad = state; bad["finance"]["properties"][0] = 10000; bad_states.push_back(bad);
  bad = state; bad["finance"]["stocks"].push_back(0); bad_states.push_back(bad);
  bad = state; bad["balance"]["plan"][0] = -1; bad_states.push_back(bad);
  bad = state; bad["balance"]["plan"].erase(bad["balance"]["plan"].end() - 1); bad_states.push_back(bad);
  bad = state; bad["contracts"]["players"].erase(player(0)); bad_states.push_back(bad);
  bad = state; bad["contracts"]["players"]["player:finance-other"] = bad["contracts"]["players"][player(0)]; bad_states.push_back(bad);
  bad = state; bad["contracts"]["club"] = "club:finance-other"; bad_states.push_back(bad);
  bad = state; bad["contracts"]["players"][player(0)]["salary"] = -1; bad_states.push_back(bad);
  bad = state; bad["contracts"]["players"][player(0)]["source"] = "unavailable"; bad_states.push_back(bad);
  bad = state; bad["contracts"]["players"][player(0)]["insurance"] = 1; bad_states.push_back(bad);
  bad = state; bad["contracts"]["bonuses"][0] = 256; bad_states.push_back(bad);
  bad = state; bad["managers"][0]["club"] = ""; bad_states.push_back(bad);
  for (auto &invalid : bad_states) {
    require(!valid_demo_finance(invalid, world), "Corrupt finance state accepted");
    reject(invalid, world, {{"kind", "term"}, {"value", 12}});
    const auto before = invalid;
    require(!migrate_demo_finance(invalid, world) && invalid == before,
        "Migration repaired present invalid finance data");
  }
  auto reset = state;
  reset["managers"][0]["club"] = "club:finance-other";
  require(!valid_demo_finance(reset, world), "Manager change inherited another roster's contracts");
  reset["contracts"] = demo_contracts(world, "club:finance-other");
  require(valid_demo_finance(reset, world) && reset["contracts"]["players"].size() == 1 &&
      reset["finance"] == state["finance"] && reset["cash"] == state["cash"],
      "Manager contract reset altered shared bank holdings");
  auto migrated = state; migrated.erase("balance"); migrated.erase("finance");
  const auto existing_contracts = migrated["contracts"];
  require(migrate_demo_finance(migrated, world) && migrated["contracts"] == existing_contracts,
      "Migration reset an existing valid contract draft");
  require(valid_demo_finance(Json::parse(state.dump()), world), "Finance save roundtrip invalid");
  if (precision == 2) {
    auto fractional = initial; fractional["cash"] = 12345; fractional["deposit"] = 876;
    require(quote(fractional, world, "deposit") == 123 && quote(fractional, world, "stock-buy", 0) == 0,
        "Quote did not convert raw cents into whole-unit limits");
    edit(fractional, world, {{"kind", "deposit"}, {"value", 123}});
    require(fractional["cash"] == 45 && fractional["deposit"] == 13176,
        "Whole-unit deposit lost pre-existing fractional balances");
  }
  require(state["unrelated"] == original_legacy["unrelated"] && world.rows("players") == original_players &&
      world.rows("contracts") == original_contracts, "Presentation finance modified world or unrelated state");
}
} // namespace
int main() try {
  check_currency(0); check_currency(2);
  for (const auto &precision : std::vector<Json>{1, 3, -1, true, "2", uint64_t(-1)}) {
    Fixture malformed(precision);
    auto state = legacy(1); const auto before = state;
    require(!migrate_demo_finance(state, *malformed.world) && state == before,
        "Unsupported currency precision was silently substituted");
  }
  Fixture empty(0, false); auto state = Json{{"cash", 0}, {"deposit", 0}, {"debt", 0}};
  require(migrate_demo_finance(state, *empty.world) && valid_demo_finance(state, *empty.world) &&
      demo_contract_rows(state, *empty.world).at("rows").empty(), "Empty catalogue unsafe");
  std::cout << "Modern UI finance checks passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n'; return 1;
}
