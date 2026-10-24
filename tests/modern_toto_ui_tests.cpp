// SPDX-License-Identifier: GPL-3.0-only
#include "modern/ui/demo_toto.hpp"
#include "modern/ui/demo_reports.hpp"
#include "modern/ui/model.hpp"
#include "modern/ui/desktop.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace bmh::modern_ui;
namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
struct Fixture {
  fs::path path; std::shared_ptr<const Catalog> world;
  Fixture(int precision = 0, bool full = true) {
    path = fs::temp_directory_path() / ("openbmh-toto-ui-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(path / "world");
    auto write = [&](const char *name, const Json &data) { std::ofstream out(path / name); out << data.dump(); require(bool(out), "Cannot write fixture"); };
    write("manifest.json", {{"format", "openbmh-world"}, {"kind", "world"}, {"format_version", "1.1"},
      {"id", "world:synthetic-toto"}, {"start_date", "2026-07-01"}, {"currency", {{"code", "EUR"}, {"minor_units", precision}}},
      {"requires", {"editor:world-v1"}}, {"x-editor", {{"profile", "editor-world-v1"}}}});
    write("world/nations.json", {{"items", {{{"id", "nation:de"}, {"name", "Synthetic nation"}}}}});
    Json leagues = Json::array(), clubs = Json::array();
    for (int tier = 1; tier <= 2; ++tier) {
      auto id = "league:synthetic-" + std::to_string(tier);
      leagues.push_back({{"id", id}, {"name", "Synthetic league " + std::to_string(tier)},
          {"nation", "nation:de"}, {"type", "league"}, {"tier", tier}});
      for (int c = 0; c < (full ? 18 : 2); ++c)
        clubs.push_back({{"id", "club:synthetic-" + std::to_string(tier) + "-" + std::to_string(c)},
          {"name", "Synthetic club " + std::to_string(tier) + "-" + std::to_string(c)},
          {"competition", id}, {"nation", "nation:de"}, {"order", c}});
    }
    write("world/competitions.json", {{"items", leagues}}); write("world/clubs.json", {{"items", clubs}});
    write("world/players.json", {{"items", Json::array()}});
    world = Catalog::open(path);
  }
  ~Fixture() { std::error_code ec; fs::remove_all(path, ec); }
};
void successful(Json &s, const Json &a, const Catalog &w) { require(edit_demo_toto(s, a, w).empty(), "Expected valid Toto action"); }
void rejected(Json &s, const Json &a, const Catalog &w) {
  const auto before = s; require(!edit_demo_toto(s, a, w).empty(), "Invalid Toto action accepted"); require(s == before, "Rejected action mutated state");
}
void exercise(int precision) {
  Fixture fixture(precision); const auto &world = *fixture.world;
  const int64_t factor = precision == 2 ? 100 : 1;
  Json state = {{"cash", 900000 * factor}, {"deposit", 400000 * factor}, {"other", {"untouched"}}};
  require(migrate_demo_toto(state, world), "Legacy migration failed");
  const auto initial = state;
  require(migrate_demo_toto(state, world) && state == initial, "Migration changed existing state");
  require(state["toto"]["stake"] == 1000 * factor && demo_toto_summary(state, world)["complete"] == 1, "Initial stakes/columns differ");
  auto programme = demo_toto_programme(world);
  require(programme["complete"] == true && programme["rows"].size() == 11, "No complete 9+2 programme");
  std::set<std::string> sides;
  for (size_t i = 0; i < 11; ++i) {
    const auto &m = programme["rows"][i]; require(m["tier"] == (i < 9 ? 1 : 2), "Wrong league mixture");
    require(m["home"] != m["away"] && world.find("clubs", m["home"]) && world.find("clubs", m["away"]), "Invented opponent");
    require(sides.insert(m["home"]).second && sides.insert(m["away"]).second, "Duplicate team in programme");
  }
  successful(state, {{"kind", "submit"}}, world);
  const auto receipt = state["toto"]["coupon"];
  successful(state, {{"kind", "pick"}, {"column", 1}, {"row", 4}, {"pick", 2}}, world);
  require(demo_toto_summary(state, world)["partial"] == 1 && state["toto"]["columns"][0] == initial["toto"]["columns"][0], "Columns not independent");
  rejected(state, {{"kind", "submit"}}, world);
  successful(state, {{"kind", "pick"}, {"column", 1}, {"row", 4}, {"pick", 2}}, world);
  require(demo_toto_summary(state, world)["partial"] == 0, "Clicking cross again does not clear");
  for (int c = 1; c < 6; ++c) successful(state, {{"kind", "auto"}, {"column", c}}, world);
  require(demo_toto_summary(state, world)["complete"] == 6 && state["toto"]["coupon"] == receipt, "Working picks mutated receipt");
  successful(state, {{"kind", "stake"}, {"value", 0}}, world); rejected(state, {{"kind", "submit"}}, world);
  successful(state, {{"kind", "stake"}, {"value", 99999999}}, world);
  require(state["toto"]["stake"] == 99999999 * factor, "Money precision changed twice");
  successful(state, {{"kind", "submit"}}, world);
  const auto full = state["toto"]["coupon"];
  for (int c = 0; c < 6; ++c) successful(state, {{"kind", "clear"}, {"column", c}}, world);
  rejected(state, {{"kind", "submit"}}, world);
  require(state["toto"]["coupon"] == full, "Clearing picks altered the receipt");
  for (auto a : {Json{{"kind", "pick"}, {"column", -1}, {"row", 0}, {"pick", 1}},
      Json{{"kind", "pick"}, {"column", 0}, {"row", 11}, {"pick", 1}},
      Json{{"kind", "pick"}, {"column", 0}, {"row", 0}, {"pick", 3}},
      Json{{"kind", "stake"}, {"value", 100000000}}, Json{{"kind", "stake"}, {"value", 1.5}},
      Json{{"kind", "auto"}, {"column", uint64_t(-1)}}, Json{{"kind", "unrecognized"}}}) rejected(state, a, world);
  auto rest = state; rest.erase("toto"); auto original = initial; original.erase("toto");
  require(rest == original && demo_toto_programme(world) == programme, "Toto booked money or changed programme");
  auto bad = state; bad["toto"]["columns"][0].push_back(0); auto before = bad;
  require(!migrate_demo_toto(bad, world) && bad == before, "Invalid existing state repaired");
  bad = state; bad["toto"]["coupon"]["columns"][0][3] = -1;
  require(!valid_demo_toto(bad, world), "Partial saved coupon accepted");
  DemoSource source(fixture.world); const auto old = source.snapshot();
  require(source.apply({old.revision, "toto", {{"kind", "stake"}, {"value", 2100}}}).ok, "DataSource cannot edit stake");
  require(!source.apply({old.revision, "toto", {{"kind", "submit"}}}).ok, "Stale revision accepted");
  auto accepted = source.snapshot();
  require(!source.apply({accepted.revision, "toto", {{"kind", "stake"}, {"value", -1}}}).ok && source.snapshot().state == accepted.state, "Failed action partially committed");
  auto file = fixture.path / "state.json"; source.save(file, default_desktop());
  source.apply({source.snapshot().revision, "toto", {{"kind", "clear"}, {"column", 0}}});
  source.load(file); require(source.snapshot().state == accepted.state && !source.dirty(), "Checkpoint roundtrip failed");
  auto legacy = accepted.state; legacy.erase("toto"); legacy["desktop"] = default_desktop();
  { std::ofstream out(file); out << legacy.dump(); } source.load(file);
  require(source.snapshot().state["toto"] == initial["toto"] && source.snapshot().state["cash"] == accepted.state["cash"], "Legacy load altered wallet");
}
} // namespace
int main() try {
  exercise(0); exercise(2);
  Fixture partial(0, false); Json state = Json::object();
  require(migrate_demo_toto(state, *partial.world), "Incomplete world migration failed");
  require(demo_toto_programme(*partial.world)["complete"] == false, "Incomplete programme masked");
  rejected(state, {{"kind", "submit"}}, *partial.world);
  std::cout << "Modern Toto UI tests passed\n";
  return 0;
} catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
