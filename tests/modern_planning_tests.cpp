// SPDX-License-Identifier: GPL-3.0-only
#include "modern/ui/demo_calendar.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

using namespace bmh::modern_ui;
namespace {
void require(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
std::string league(int group) { return "competition:planning-" + std::to_string(group); }
std::string club(int group, int index) {
  return "club:planning-" + std::to_string(group) + "-" + std::to_string(index);
}
struct Fixture {
  fs::path root;
  std::shared_ptr<const Catalog> world;
  explicit Fixture(int own_count = 18, bool populated = true) {
    root = fs::temp_directory_path() / ("openbmh-planning-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "world");
    const auto write = [&](const std::string &file, const Json &value) {
      std::ofstream out(root / file); out << value.dump();
      require(bool(out), "Cannot write synthetic fixture");
    };
    write("manifest.json", {{"format", "openbmh-world"}, {"kind", "world"},
        {"format_version", "1.1"}, {"id", "world:synthetic-planning"},
        {"requires", {"editor:world-v1"}},
        {"x-editor", {{"profile", "editor-world-v1"}}}});
    write("world/nations.json", {{"items", Json::array({
        {{"id", "nation:de"}}, {{"id", "nation:foreign"}}, {{"id", "nation:empty"}}})}});
    Json competitions = Json::array(), clubs = Json::array();
    // Five manageable German leagues, plus foreign 20- and 18-club previews.
    if (populated) for (int group = 0; group < 7; ++group) {
      const auto nation = group < 5 ? "nation:de" : "nation:foreign";
      competitions.push_back({{"id", league(group)}, {"nation", nation}, {"type", "league"}});
      const int count = group == 0 ? own_count : group == 5 ? 20 : 18;
      for (int index = 0; index < count; ++index)
        clubs.push_back({{"id", club(group, index)}, {"name", "Synthetic club"},
                         {"nation", nation}, {"competition", league(group)}});
    }
    write("world/competitions.json", {{"items", competitions}});
    write("world/clubs.json", {{"items", clubs}});
    write("world/players.json", {{"items", Json::array()}});
    world = Catalog::open(root);
  }
  ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
};
void edit(Json &state, const Catalog &world, const Json &action,
          const std::string &managed = club(0, 3)) {
  const auto error = edit_demo_calendar(state, action, world, managed);
  if (!error.empty()) throw std::runtime_error("Unexpected rejection: " + error + " " + action.dump());
  require(valid_demo_calendar(state, world, managed), "Action produced invalid calendar");
}
void reject(Json &state, const Catalog &world, const Json &action,
            const std::string &managed = club(0, 3)) {
  const auto before = state;
  require(!edit_demo_calendar(state, action, world, managed).empty() && state == before,
          "Rejected action threw or changed state");
}
void select(Json &state, const Catalog &world, int day) {
  edit(state, world, {{"kind", "select"}, {"value", day}});
}
void move(Json &state, const Catalog &world, int source, int target) {
  select(state, world, source); edit(state, world, {{"kind", "postpone-source"}});
  select(state, world, target); edit(state, world, {{"kind", "postpone-move"}});
}
bool same(const std::vector<CalendarFixture> &a, const std::vector<CalendarFixture> &b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (a[i].day != b[i].day || a[i].from != b[i].from || a[i].round != b[i].round ||
        a[i].home != b[i].home || a[i].away != b[i].away) return false;
  return true;
}
} // namespace
int main() try {
  Fixture fixture;
  const auto &world = *fixture.world;
  const auto immutable_clubs = world.rows("clubs"), immutable_competitions = world.rows("competitions");
  auto state = demo_calendar(world, "2027-07-01", club(0, 3));
  require(valid_demo_calendar(state, world, club(0, 3)) && state["selected"] == 6 &&
      state["opponent"] == club(0, 0), "Default planning state is invalid");
  require(calendar_days(state["start"]).size() == 366 &&
      calendar_days("2099-07-01").size() == 365, "Leap-year season boundary incorrect");
  require(calendar_own_league(world, club(2, 7)) == league(2) &&
      calendar_own_league(world) == league(0) &&
      !valid_demo_calendar(state, world, club(5, 0)), "Manager identity fallback is unsafe");
  for (int group = 0; group < 7; ++group) {
    auto shown = state; shown["league"] = league(group);
    const auto managed = group < 5 ? club(group, 7) : club(0, 3);
    const auto fixtures = calendar_fixtures(shown, world, managed);
    require(fixtures.size() == size_t(group == 5 ? 38 : 34), "Wrong preview league size");
    std::map<std::string, int> opponents;
    const auto followed = group < 5 ? managed : club(group, 0);
    for (const auto &f : fixtures) {
      require((f.home == followed || f.away == followed) && f.home != f.away &&
          f.from == 30 + 7 * f.round && f.day == f.from, "Incorrect stable fixture identity");
      ++opponents[f.home == followed ? f.away : f.home];
    }
    require(opponents.size() == size_t(group == 5 ? 19 : 17) &&
        std::all_of(opponents.begin(), opponents.end(), [](const auto &p) { return p.second == 2; }),
        "Preview does not meet every opponent twice");
  }
  edit(state, world, {{"kind", "friendly-opponent"}, {"value", club(0, 4)}});
  require(calendar_friendly_fee(state, world) == 27000, "Fee filtered own club out of opponent index");
  edit(state, world, {{"kind", "friendly-venue"}, {"value", false}});
  require(calendar_friendly_fee(state, world) == 14000, "Away fee incorrect");
  edit(state, world, {{"kind", "friendly-country"}, {"value", "nation:foreign"}});
  require(state["friendly_league"] == league(5) && state["opponent"] == club(5, 0) &&
      state["league"] == league(0), "Friendly picker overwrote league preview");
  edit(state, world, {{"kind", "friendly-stage"}, {"value", 1}});
  select(state, world, 7); edit(state, world, {{"kind", "friendly-book"}});
  require(state["friendlies"].front() == Json({{"day", 7}, {"opponent", club(5, 0)},
      {"league", league(5)}, {"home", false}}), "Booking did not retain stable identity and venue");
  reject(state, world, {{"kind", "friendly-book"}});
  select(state, world, 30); reject(state, world, {{"kind", "friendly-book"}});
  move(state, world, 30, 8);
  auto own_fixtures = calendar_fixtures(state, world, club(0, 3));
  require(own_fixtures.front().from == 30 && own_fixtures.front().day == 8 &&
      state["source_day"] == -1 && !calendar_day_occupied(state, world, club(0, 3), 30),
      "Rescheduling lost source identity or did not free original date");
  move(state, world, 37, 9); move(state, world, 8, 10);
  require(state["moves"] == Json::array({{{"from", 37}, {"to", 9}}, {{"from", 30}, {"to", 10}}}),
      "Repeated move failed to replace source and append to undo order");
  edit(state, world, {{"kind", "postpone-undo"}});
  require(calendar_fixtures(state, world, club(0, 3)).front().day == 30 &&
      state["moves"].size() == 1, "Undo did not restore original round date");
  move(state, world, 30, 11); select(state, world, 30);
  edit(state, world, {{"kind", "friendly-book"}});
  reject(state, world, {{"kind", "postpone-undo"}});
  select(state, world, 11); edit(state, world, {{"kind", "postpone-source"}});
  select(state, world, 7); reject(state, world, {{"kind", "postpone-move"}});
  select(state, world, 44); reject(state, world, {{"kind", "postpone-move"}});
  edit(state, world, {{"kind", "postpone-cancel"}});
  reject(state, world, {{"kind", "postpone-move"}});
  select(state, world, 365); edit(state, world, {{"kind", "friendly-book"}});
  auto foreign = state; foreign["league"] = league(5);
  auto foreign_base = foreign; foreign_base["moves"] = Json::array();
  require(same(calendar_fixtures(foreign, world, club(0, 3)),
      calendar_fixtures(foreign_base, world, club(0, 3))), "Own moves leaked into foreign preview");
  require(calendar_day_occupied(foreign, world, club(0, 3), 11) &&
      !calendar_day_occupied(foreign, world, club(0, 3), 30 + 7 * 35),
      "Planning occupancy used selected preview league");
  auto full = demo_calendar(world, "2026-07-01", club(0, 3));
  for (int day = 7; full["friendlies"].size() < 20; ++day) {
    if (calendar_day_occupied(full, world, club(0, 3), day)) continue;
    select(full, world, day); edit(full, world, {{"kind", "friendly-book"}});
  }
  select(full, world, 29); reject(full, world, {{"kind", "friendly-book"}});
  const std::vector<Json> bad_actions = {nullptr, Json::array(), "bad", Json::object(),
      {{"kind", 1}}, {{"kind", "unknown"}}, {{"kind", "select"}},
      {{"kind", "select"}, {"value", true}}, {{"kind", "select"}, {"value", 7.0}},
      {{"kind", "select"}, {"value", uint64_t(-1)}},
      {{"kind", "select"}, {"value", -1}}, {{"kind", "select"}, {"value", 366}},
      {{"kind", "friendly-country"}, {"value", "nation:empty"}},
      {{"kind", "friendly-country"}, {"value", "nation:unknown"}},
      {{"kind", "friendly-league"}, {"value", "competition:unknown"}},
      {{"kind", "friendly-opponent"}, {"value", club(0, 3)}},
      {{"kind", "friendly-opponent"}, {"value", club(1, 0)}},
      {{"kind", "friendly-opponent"}, {"value", ""}},
      {{"kind", "friendly-venue"}, {"value", 1}},
      {{"kind", "friendly-stage"}, {"value", 2}},
      {{"kind", "friendly-stage"}, {"value", false}},
      {{"kind", "postpone-cancel"}, {"value", 0}},
      {{"kind", "friendly-book"}, {"unexpected", 1}}};
  for (const auto &action : bad_actions) reject(state, world, action);
  std::vector<Json> bad_states;
  for (const auto &value : std::vector<Json>{true, 1.5, -2, 366, uint64_t(-1)}) {
    auto bad = state; bad["source_day"] = value; bad_states.push_back(bad);
  }
  auto bad = state; bad["source_day"] = 31; bad_states.push_back(bad);
  bad = state; bad["moves"].push_back(bad["moves"].front()); bad_states.push_back(bad);
  bad = state; bad["moves"][0]["to"] = bad["moves"][1]["to"]; bad_states.push_back(bad);
  bad = state; bad["moves"][0]["to"] = 44; bad_states.push_back(bad);
  bad = state; bad["moves"][0]["from"] = 31; bad_states.push_back(bad);
  bad = state; bad["moves"][0]["to"] = 7; bad_states.push_back(bad);
  bad = state; bad["moves"][0]["to"] = 366; bad_states.push_back(bad);
  bad = state; bad["friendlies"].push_back(bad["friendlies"].front()); bad_states.push_back(bad);
  bad = state; bad["friendlies"][0]["opponent"] = club(0, 3); bad_states.push_back(bad);
  bad = state; bad["friendlies"][0]["league"] = league(0); bad_states.push_back(bad);
  bad = state; bad["friendlies"][0]["day"] = 44; bad_states.push_back(bad);
  bad = state; bad["friendlies"][0]["home"] = 1; bad_states.push_back(bad);
  bad = state; bad["friendlies"][0]["unexpected"] = true; bad_states.push_back(bad);
  bad = state; bad["unexpected"] = 1; bad_states.push_back(bad);
  for (auto &invalid : bad_states) {
    if (valid_demo_calendar(invalid, world, club(0, 3)))
      throw std::runtime_error("Corrupt schedule accepted: " + invalid.dump());
    reject(invalid, world, {{"kind", "postpone-cancel"}});
    const auto before = invalid;
    require(!migrate_demo_calendar(invalid, world, club(0, 3)) && invalid == before,
        "Migration repaired present corrupt data");
  }
  auto old = demo_calendar(world, "2027-07-01", club(0, 3));
  for (const auto *key : {"source_day", "friendly_league", "opponent", "home", "friendly_stage"})
    old.erase(key);
  require(!valid_demo_calendar(old, world, club(0, 3)) &&
      migrate_demo_calendar(old, world, club(0, 3)) &&
      valid_demo_calendar(old, world, club(0, 3)), "Old checkpoint migration failed");
  const auto defaults = demo_calendar(world, "2027-07-01", club(0, 3));
  for (const auto &entry : defaults.items())
    require(old.at(entry.key()) == entry.value(), "Migration changed an old field or default");
  require(valid_demo_calendar(Json::parse(state.dump()), world, club(0, 3)),
      "JSON checkpoint roundtrip lost valid planning data");
  auto different_manager = demo_calendar(world, "2026-07-01", club(3, 7));
  require(different_manager["friendly_league"] == league(3) &&
      different_manager["opponent"] == club(3, 0), "Nonfirst manager adopted first career league");
  Fixture tiny(1); auto one = demo_calendar(*tiny.world, "2026-07-01", club(0, 0));
  require(one["opponent"] == "" && calendar_friendly_fee(one, *tiny.world) == 0,
      "Single-club league invented an opponent");
  require(edit_demo_calendar(one, {{"kind", "friendly-book"}}, *tiny.world, club(0, 0)) != "",
      "Single-club friendly booking accepted");
  Fixture empty(0, false); const auto none = demo_calendar(*empty.world, "2026-07-01");
  require(valid_demo_calendar(none, *empty.world) && none["opponent"] == "" &&
      calendar_fixtures(none, *empty.world, "").empty(), "Empty world preview is unsafe");
  require(world.rows("clubs") == immutable_clubs && world.rows("competitions") == immutable_competitions,
      "Planning changed immutable world data");
  std::cout << "Modern planning checks passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n'; return 1;
}
