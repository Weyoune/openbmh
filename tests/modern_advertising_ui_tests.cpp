// SPDX-License-Identifier: GPL-3.0-only
#include "modern/ui/demo_advertising.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace bmh::modern_ui;
namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
struct Fixture {
  fs::path root;
  std::shared_ptr<const Catalog> world;
  explicit Fixture(Json precision) {
    root = fs::temp_directory_path() / ("openbmh-advertising-ui-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "world");
    const auto write = [&](const fs::path &file, const Json &value) {
      std::ofstream out(file); out << value.dump();
      require(bool(out), "Cannot write synthetic catalogue");
    };
    write(root / "manifest.json", {{"format", "openbmh-world"}, {"kind", "world"},
        {"format_version", "1.1"}, {"id", "world:synthetic-advertising"},
        {"currency", {{"code", "EUR"}, {"minor_units", precision}}},
        {"requires", {"editor:world-v1"}}, {"x-editor", {{"profile", "editor-world-v1"}}}});
    for (const auto *table : {"clubs", "players", "competitions"})
      write(root / "world" / (std::string(table) + ".json"), {{"items", Json::array()}});
    world = Catalog::open(root);
  }
  ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
};
void reject(Json &state, const Json &action, const Catalog &world) {
  const auto before = state;
  require(!edit_demo_advertising(state, action, world).empty() && state == before,
      "Rejected advertising action mutated state or was accepted");
}
void edit(Json &state, const Json &action, const Catalog &world) {
  const auto error = edit_demo_advertising(state, action, world);
  if (!error.empty()) throw std::runtime_error("Unexpected advertising rejection: " + error);
  require(valid_demo_advertising(state, world), "Accepted action produced invalid advertising state");
}
Json without_ads(Json state) { state.erase("advertising"); return state; }
void check_currency(int precision) {
  Fixture fixture(precision);
  const auto &world = *fixture.world;
  const int64_t factor = precision == 2 ? 100 : 1;
  Json state = {{"cash", 12450000 * factor}, {"deposit", 600000 * factor}, {"debt", 0}, {"ticket", 13},
      {"training", {{"untouched", true}}}, {"contracts", {{"unrelated", {4, 5, 6}}}}};
  const auto original = state, manifest = world.manifest(), clubs = world.rows("clubs");
  require(!valid_demo_advertising(state, world) && migrate_demo_advertising(state, world) &&
      valid_demo_advertising(state, world) && without_ads(state) == original,
      "Migration lost unrelated fields or failed to add defaults");
  const auto defaults = state;
  const auto view = demo_advertising_view(state, world);
  require(view.at("slots").size() == 16 && view.at("spending").size() == 3 &&
      view.at("income") == 0 && view.at("expenses") == 75000 * factor &&
      view.at("balance") == -75000 * factor && view.at("spending")[0].at("suggestion_step") == 50000 &&
      view.at("spending")[1].at("suggestion_step") == 5000, "Default budgets or aggregate projection incorrect");
  const auto board = demo_advertising_quote(state, 0, 0, world), shirt = demo_advertising_quote(state, 14, 2, world), tv = demo_advertising_quote(state, 15, 1, world);
  require(board.at("amount") == 7000 * factor && board.at("name") == "MAINWERK" && board.at("months") == 12 &&
      shirt.at("amount") == 158000 * factor && shirt.at("months") == 36 && shirt.at("sponsor") == 4 &&
      tv.at("amount") == 104200 * factor && tv.at("name") == "FUSSBALL TV" && tv.at("months") == 24 &&
      demo_advertising_quote(state, 13, 2, world).at("amount") == 17800 * factor,
      "Pinned fictional offer prices, issuer or terms changed");
  auto copied = board; copied["amount"] = 1;
  require(demo_advertising_quote(state, 0, 0, world) == board && state == defaults,
      "Query result aliases state or projection mutates state");
  for (const auto &pair : {std::pair{-1, 0}, {16, 0}, {0, -1}, {0, 3}})
    require(demo_advertising_quote(state, pair.first, pair.second, world).at("ok") == false && state == defaults,
        "Out-of-range offer query accepted or mutated state");
  edit(state, {{"kind", "accept"}, {"slot", 15}, {"offer", 1}}, world);
  require(state.at("advertising").at("contracts")[15] == Json({{"sponsor", 1}, {"months", 24}, {"amount", 104200 * factor}}) &&
      demo_advertising_view(state, world).at("income") == 104200 * factor && without_ads(state) == original,
      "Acceptance booked cash, stored the wrong offer or failed totals");
  const auto active = demo_advertising_quote(state, 15, 2, world);
  require(active.at("active") == true && active.at("amount") == tv.at("amount") && active.at("months") == 24,
      "Viewing another offer replaced an active contract");
  reject(state, {{"kind", "accept"}, {"slot", 15}, {"offer", 2}}, world);
  for (int slot = 0; slot < 15; ++slot) edit(state, {{"kind", "accept"}, {"slot", slot}, {"offer", slot % 3}}, world);
  int64_t sum = 0;
  for (const auto &c : state.at("advertising").at("contracts")) sum += c.at("amount").get<int64_t>();
  require(demo_advertising_view(state, world).at("income") == sum && without_ads(state) == original,
      "Full contract projection lost an income or changed wallet");
  edit(state, {{"kind", "spending"}, {"index", 0}, {"value", 0}}, world);
  edit(state, {{"kind", "spending"}, {"index", 1}, {"value", 12345}}, world);
  edit(state, {{"kind", "spending"}, {"index", 2}, {"value", 99999999}}, world);
  require(state["advertising"]["spending"][0] == 0 && state["advertising"]["spending"][1] == 12345 * factor &&
      demo_advertising_view(state, world).at("expenses") == (99999999LL + 12345) * factor && without_ads(state) == original,
      "Budget editor unit conversion or monetary isolation failed");
  const auto preserved = state;
  require(migrate_demo_advertising(state, world) && state == preserved &&
      valid_demo_advertising(Json::parse(state.dump()), world), "Migration changed a valid saved agreement");
  for (const auto &action : std::vector<Json>{nullptr, Json::array(), "bad", Json::object(), {{"kind", 1}},
      {{"kind", "unknown"}}, {{"kind", "accept"}}, {{"kind", "accept"}, {"slot", 16}, {"offer", 0}},
      {{"kind", "accept"}, {"slot", 0}, {"offer", 3}}, {{"kind", "accept"}, {"slot", 0}, {"offer", true}},
      {{"kind", "accept"}, {"slot", uint64_t(-1)}, {"offer", 0}},
      {{"kind", "accept"}, {"slot", 0}, {"offer", 0}, {"amount", 1}},
      {{"kind", "spending"}}, {{"kind", "spending"}, {"index", 3}, {"value", 1}},
      {{"kind", "spending"}, {"index", 0}, {"value", -1}}, {{"kind", "spending"}, {"index", 0}, {"value", 100000000}},
      {{"kind", "spending"}, {"index", 0}, {"value", uint64_t(-1)}},
      {{"kind", "spending"}, {"index", 0}, {"value", 1.0}}, {{"kind", "spending"}, {"index", 0}, {"value", true}},
      {{"kind", "spending"}, {"index", 0}, {"value", 1}, {"cash", 0}}}) reject(state, action, world);
  std::vector<Json> invalid;
  auto bad = state; bad["advertising"] = nullptr; invalid.push_back(bad);
  bad = state; bad["advertising"]["selected"] = 0; invalid.push_back(bad);
  bad = state; bad["advertising"].erase("spending"); invalid.push_back(bad);
  bad = state; bad["advertising"]["spending"].push_back(0); invalid.push_back(bad);
  bad = state; bad["advertising"]["spending"][0] = uint64_t(-1); invalid.push_back(bad);
  bad = state; bad["advertising"]["spending"][0] = 100000000LL * factor; invalid.push_back(bad);
  bad = state; bad["advertising"]["contracts"][0]["sponsor"] = uint64_t(-1); invalid.push_back(bad);
  bad = state; bad["advertising"]["contracts"][0]["sponsor"] = 10; invalid.push_back(bad);
  bad = state; bad["advertising"]["contracts"][15]["sponsor"] = 5; invalid.push_back(bad);
  bad = state; bad["advertising"]["contracts"][0]["months"] = 6; invalid.push_back(bad);
  bad = state; bad["advertising"]["contracts"][0]["amount"] = 0; invalid.push_back(bad);
  bad = state; bad["advertising"]["contracts"][0]["extra"] = true; invalid.push_back(bad);
  bad = defaults; bad["advertising"]["contracts"][0]["months"] = 12; invalid.push_back(bad);
  bad = defaults; bad["advertising"]["contracts"][0]["amount"] = 1; invalid.push_back(bad);
  bad = defaults; bad["advertising"]["contracts"].erase(bad["advertising"]["contracts"].end() - 1); invalid.push_back(bad);
  for (auto &v : invalid) {
    require(!valid_demo_advertising(v, world), "Malformed stored advertising accepted");
    const auto before = v;
    require(!migrate_demo_advertising(v, world) && before == v, "Migration repaired present malformed data");
    reject(v, {{"kind", "spending"}, {"index", 0}, {"value", 0}}, world);
    require(demo_advertising_view(v, world).at("ok") == false, "Malformed state projected as valid");
  }
  auto maximum = state;
  for (auto &c : maximum["advertising"]["contracts"]) c["amount"] = 99999999LL * factor;
  for (auto &s : maximum["advertising"]["spending"]) s = 99999999LL * factor;
  const auto totals = demo_advertising_view(maximum, world);
  require(totals.at("income") == 16 * 99999999LL * factor && totals.at("expenses") == 3 * 99999999LL * factor &&
      totals.at("balance") == 13 * 99999999LL * factor, "Aggregate overflow or unsafe money scaling");
  auto cents = defaults;
  if (precision == 2) {
    cents["advertising"]["spending"][0] = 12345;
    const auto original_cents = cents;
    require(migrate_demo_advertising(cents, world) && cents == original_cents,
        "Migration silently changed saved fractional currency values");
  }
  require(world.manifest() == manifest && world.rows("clubs") == clubs, "Advertising altered immutable world");
}
} // namespace
int main() try {
  check_currency(0); check_currency(2);
  for (const auto &precision : std::vector<Json>{1, 3, true, "2", nullptr, uint64_t(-1)}) {
    Fixture invalid_currency(precision); Json state = {{"cash", 5}}; const auto before = state;
    require(!migrate_demo_advertising(state, *invalid_currency.world) && state == before,
        "Unsupported precision was silently substituted");
  }
  std::cout << "Modern UI advertising checks passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n'; return 1;
}
