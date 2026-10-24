// SPDX-License-Identifier: GPL-3.0-only
#include "modern/ui/demo_environment.hpp"
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace bmh::modern_ui;
namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
struct Fixture {
  fs::path root;
  std::shared_ptr<const Catalog> world;
  explicit Fixture(Json precision = 0) {
    root = fs::temp_directory_path() / ("openbmh-environment-ui-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "world");
    bmh::editor::write_atomic(root / "manifest.json", Json({{"format", "openbmh-world"},
        {"kind", "world"}, {"format_version", "1.1"}, {"id", "world:synthetic-environment"},
        {"requires", {"editor:world-v1"}}, {"x-editor", {{"profile", "editor-world-v1"}}},
        {"currency", {{"code", "EUR"}, {"minor_units", precision}}}}).dump());
    for (const auto *table : {"nations", "clubs", "players", "competitions"})
      bmh::editor::write_atomic(root / "world" / (std::string(table) + ".json"),
                               Json({{"items", Json::array()}}).dump());
    bmh::editor::write_atomic(root / "world/staff.json", Json({{"items", Json::array({
        {{"id", "staff:synthetic"}, {"name", "Synthetic catalogue staff"}, {"role", "doctor"}}})}}).dump());
    world = Catalog::open(root);
  }
  ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
};
void currency_case(int precision, int64_t factor) {
  Fixture f(precision);
  const auto staff = f.world->rows("staff");
  const auto staff_file = bmh::editor::read_file(f.root / "world/staff.json");
  Json state = {{"cash", 123456 * factor}, {"deposit", 750 * factor}, {"debt", 0},
      {"staff", {{"unrelated_legacy_marker", "preserved"}}}, {"edits", {{"other", 17}}}};
  const auto legacy = state;
  require(migrate_demo_environment(state, *f.world) && valid_demo_environment(state, *f.world),
          "Missing environment budgets did not migrate");
  require(state.at("environment").at("levels") == Json::array({2,3,4,3,4,2}),
          "Diamond 3.6 initial budgets changed");
  auto without_environment = state;
  without_environment.erase("environment");
  require(without_environment == legacy, "Migration changed wallet, staff or unrelated legacy fields");
  const auto initial = state;
  require(migrate_demo_environment(state, *f.world) && state == initial,
          "Environment migration is not idempotent");
  const auto projection = demo_environment_rows(state, *f.world);
  require(projection.value("ok", false) && projection.at("rows").size() == 6 &&
              projection.at("total") == 88500 * factor && state == initial,
          "Default monthly preview or read-only projection changed");
  const std::array<int64_t, 6> rates{2000,3000,5000,7500,6000,4500};
  for (size_t i = 0; i < rates.size(); ++i) {
    const auto &row = projection.at("rows")[i];
    require(row.at("rate") == rates[i] * factor &&
                row.at("cost") == rates[i] * factor * initial.at("environment").at("levels")[i].get<int>(),
            "Environment preview double-converted currency or changed its Gold rate");
    require(i < 3 ? row.at("rank").is_number_integer() && row.at("league").is_number_integer()
                  : row.at("rank").is_null() && row.at("league").is_null(),
            "Staff inherited a fictional youth standing");
  }
  for (int i = 0; i < 6; ++i)
    require(edit_demo_environment(state, {{"kind", "level"}, {"index", i}, {"value", 0}}, *f.world).empty(),
            "Zero budget was rejected");
  require(demo_environment_rows(state, *f.world).at("total") == 0,
          "Disabled budgets invented ongoing preview costs");
  for (int i = 0; i < 6; ++i)
    require(edit_demo_environment(state, {{"kind", "level"}, {"index", i}, {"value", 6}}, *f.world).empty(),
            "Sixth star was unavailable");
  require(demo_environment_rows(state, *f.world).at("total") == 168000 * factor,
          "Six-star monthly total or raw currency precision is wrong");
  const auto funded = state;
  require(edit_demo_environment(state, {{"kind", "level"}, {"index", 0}, {"value", 6}}, *f.world).empty() && state == funded,
          "Selecting the current budget toggled it off");
  require(migrate_demo_environment(state, *f.world) && state == funded,
          "Checkpoint migration reset existing budget choices");
  auto unrelated = state;
  unrelated.erase("environment");
  require(unrelated == legacy && f.world->rows("staff") == staff &&
              bmh::editor::read_file(f.root / "world/staff.json") == staff_file,
          "Environment selection booked cash or modified catalogue staff");
  const std::vector<Json> invalid_actions = {nullptr, Json::array(), Json::object(),
      {{"kind", "other"}, {"index", 0}, {"value", 1}},
      {{"kind", "level"}, {"index", -1}, {"value", 1}},
      {{"kind", "level"}, {"index", 6}, {"value", 1}},
      {{"kind", "level"}, {"index", true}, {"value", 1}},
      {{"kind", "level"}, {"index", 0}, {"value", -1}},
      {{"kind", "level"}, {"index", 0}, {"value", 7}},
      {{"kind", "level"}, {"index", 0}, {"value", 6.0}},
      {{"kind", "level"}, {"index", 0}, {"value", false}},
      {{"kind", "level"}, {"index", 0}, {"value", uint64_t(-1)}},
      {{"kind", "level"}, {"index", 0}, {"value", 2}, {"cash", 0}}};
  for (const auto &action : invalid_actions) {
    const auto before = state;
    require(!edit_demo_environment(state, action, *f.world).empty() && state == before,
            "Invalid environment action threw or changed state");
  }
  std::vector<Json> invalid_states{nullptr, Json::array(), Json{{"environment", nullptr}},
      Json{{"environment", {{"levels", {0,0,0,0,0}}}}},
      Json{{"environment", {{"levels", {0,0,0,0,0,7}}}}},
      Json{{"environment", {{"levels", {0,0,0,0,0,true}}}}},
      Json{{"environment", {{"levels", {0,0,0,0,0,1.0}}}}},
      Json{{"environment", {{"levels", {0,0,0,0,0,0}}, {"extra", 1}}}}};
  for (auto invalid : invalid_states) {
    const auto before = invalid;
    require(!valid_demo_environment(invalid, *f.world) &&
                !migrate_demo_environment(invalid, *f.world) && invalid == before &&
                !edit_demo_environment(invalid, {{"kind", "level"}, {"index", 0}, {"value", 1}}, *f.world).empty() &&
                invalid == before && !demo_environment_rows(invalid, *f.world).value("ok", true),
            "Malformed environment data was partially repaired or projected");
  }
}
} // namespace
int main() try {
  currency_case(0, 1);
  currency_case(2, 100);
  Fixture unsupported(1);
  Json state = Json::object();
  require(!migrate_demo_environment(state, *unsupported.world) && state.empty(),
          "Unsupported currency precision initialized a misleading environment budget");
  std::cout << "Modern environment preview checks passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
