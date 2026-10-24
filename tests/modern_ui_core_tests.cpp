// SPDX-License-Identifier: GPL-3.0-only
#include "modern/ui/demo_sports.hpp"
#include "modern/ui/demo_calendar.hpp"
#include "modern/ui/demo_reports.hpp"
#include "modern/ui/desktop.hpp"
#include "modern/ui/gui_assets.hpp"
#include "modern/ui/model.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace bmh::modern_ui;
namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
void write(const fs::path &root, const std::string &file, const Json &value) {
  fs::create_directories((root / file).parent_path());
  bmh::editor::write_atomic(root / file, value.dump());
}
void table(const fs::path &root, const std::string &name, const Json &rows) {
  write(root,
        name == "assets" ? "assets/index.json" : "world/" + name + ".json",
        {{"schema", name + "@1.0"}, {"items", rows}});
}
} // namespace
int main(int argc, char **argv) try {
  if (argc == 2) {
    auto catalog = Catalog::open(bmh::editor::path(argv[1]));
    const auto before = catalog->summary();
    DemoSource demo(catalog);
    require(!demo.apply({0, "kickoff", {}}).ok,
            "Demo must never expose kickoff");
    require(demo.snapshot().revision == 0, "Rejected action changed revision");
    require(catalog->summary() == before, "UI modified its world");
    std::cout << before.dump(2) << '\n';
    return 0;
  }
  const auto root =
      fs::temp_directory_path() /
      ("openbmh-modern-ui-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  struct Cleanup {
    fs::path p;
    ~Cleanup() {
      std::error_code ec;
      fs::remove_all(p, ec);
    }
  } cleanup{root};
  fs::create_directories(root);
  write(root, "manifest.json",
        {{"format", "openbmh-world"},
         {"kind", "world"},
         {"format_version", "1.1"},
         {"id", "world:synthetic-ui"},
         {"title", {{"default", "Synthetic UI world"}}},
         {"requires", {"editor:world-v1"}},
         {"x-editor", {{"profile", "editor-world-v1"}, {"playable", false}}}});
  table(root, "nations",
        Json::array({{{"id", "nation:de"}, {"name", "Testland"}},
                     {{"id", "nation:other"}, {"name", "Otherland"}}}));
  table(root, "competitions",
        Json::array({{{"id", "comp:home"},
                      {"name", "Home league"},
                      {"type", "league"},
                      {"nation", "nation:de"}},
                     {{"id", "comp:other"},
                      {"name", "Other league"},
                      {"type", "league"},
                      {"nation", "nation:other"}}}));
  table(root, "clubs",
        Json::array({{{"id", "club:a"},
                      {"name", "Test A"},
                      {"nation", "nation:de"},
                      {"competition", "comp:home"}},
                     {{"id", "club:b"},
                      {"name", "Test B"},
                      {"nation", "nation:other"},
                      {"competition", "comp:other"}}}));
  table(root, "players",
        Json::array({{{"id", "player:a"},
                      {"name", {{"first", "Alex"}, {"last", "Example"}}},
                      {"team", "club:a"},
                      {"squad_order", 1}},
                     {{"id", "player:b"},
                      {"name", {{"first", "Robin"}, {"last", "Sample"}}},
                      {"team", "club:a"},
                      {"squad_order", 0}}}));
  bmh::editor::write_atomic(root / "small.txt", "low resolution");
  bmh::editor::write_atomic(root / "vector.svg", "vector artwork");
  table(root, "assets",
        Json::array(
            {{{"key", "asset:club/a/crest"},
              {"files", {{"native", "small.txt"}, {"svg", "vector.svg"}}}}}));
  const auto original = bmh::editor::read_file(root / "manifest.json");
  auto world = Catalog::open(root);
  require(world->manageable("club:a") && !world->manageable("club:b"),
          "Foreign management policy");
  require(world->clubs("comp:other").size() == 1,
          "Foreign clubs must remain visible");
  require(world->squad("club:a").front()->at("id") == "player:b",
          "Squad order must come from OBW");
  require(world->name(world->rows("players")[0]) == "Alex Example",
          "Split names were not presented");
  require(world->asset("asset:club/a/crest").first == "vector artwork",
          "Low-resolution native image overrides SVG");
  require(number(-1234567) == "-1.234.567", "Integer presentation");
  require(money(-1, {{"minor_units", 2}, {"code", "EUR"}}) == "-0,01 €",
          "Minor currency units");
  DemoSource source(world);
  const auto before = source.snapshot();
  const auto archive_index = source.read_almanac({{"kind", "index"}});
  require(archive_index.value("ok", false) &&
              source.snapshot().revision == before.revision &&
              source.snapshot().state == before.state && !source.dirty(),
          "Almanac read mutated presentation state or revision");
  require(!source.read_almanac({{"kind", "invalid"}}).value("ok", true) &&
              source.snapshot().state == before.state && !source.dirty(),
          "Rejected almanac query mutated state");
  require(!source
               .apply({0, "managers",
                       Json::array({{{"name", "Pat"}, {"club", "club:b"}}})})
               .ok,
          "Foreign club was selectable");
  require(source.snapshot().state == before.state &&
              source.snapshot().revision == 0,
          "Rejected action mutated state");
  require(source.apply({0, "ticket", 17}).ok && source.dirty(),
          "Accepted draft action");
  require(!source.apply({0, "ticket", 18}).ok &&
              source.snapshot().state["ticket"] == 17,
          "Stale revision mutated state");
  require(!source.apply({1, "kickoff", {}}).ok, "Kickoff became available");
  require(!source.apply({1, "ticket", 26}).ok,
          "Invalid demo ticket was accepted");
  const auto save = root / "demo.ui.json";
  auto layout = default_desktop();
  require(valid_desktop(layout), "Default desktop must validate");
  auto old_layout = layout;
  old_layout["objects"].erase("app-almanac");
  const auto preserved_layout = old_layout;
  require(migrate_desktop(old_layout) && valid_desktop(old_layout) &&
              old_layout["objects"]["app-almanac"]["visible"] == true,
          "Older desktop did not gain its Almanac icon");
  auto without_added_icon = old_layout;
  without_added_icon["objects"].erase("app-almanac");
  require(without_added_icon == preserved_layout,
          "Almanac migration moved or reset existing desktop objects");
  auto bad_old_layout = preserved_layout;
  bad_old_layout["objects"]["calendar"]["x"] = -1;
  const auto before_bad_migration = bad_old_layout;
  require(!migrate_desktop(bad_old_layout) && bad_old_layout == before_bad_migration,
          "Desktop migration partially repaired invalid existing data");
  auto moved = layout;
  require(!place_desktop_object(moved, "account", 3000, 1500) &&
              moved == layout,
          "Pinned widget moved");
  moved["objects"]["account"]["pinned"] = false;
  require(resize_desktop_widget(moved, "account", 4, 1),
          "Approved 4x1 account size failed");
  require(valid_desktop(moved), "Resize produced invalid desktop");
  const auto resized = moved;
  require(!resize_desktop_widget(moved, "account", 3, 7) && moved == resized,
          "Invalid widget size mutated layout");
  auto corrupt = layout;
  corrupt["objects"]["calendar"]["x"] = 999999;
  require(!valid_desktop(corrupt), "Out of bounds desktop accepted");
  source.save(save, layout);
  require(!source.dirty(), "Saved demo remains dirty");
  auto old_icon_save = Json::parse(bmh::editor::read_file(save));
  old_icon_save["desktop"]["objects"].erase("app-almanac");
  write(root, "old-icon.ui.json", old_icon_save);
  DemoSource old_icon_source(world);
  const auto migrated_icon_layout = old_icon_source.load(root / "old-icon.ui.json");
  require(valid_desktop(migrated_icon_layout) &&
              migrated_icon_layout["objects"].contains("app-almanac") &&
              !old_icon_source.dirty(),
          "Older UI checkpoint rejected its missing Almanac desktop icon");
  require(source.apply({1, "ticket", 21}).ok, "Second action");
  const auto desktop = source.load(save);
  require(source.snapshot().state["ticket"] == 17 && desktop == layout,
          "Demo restoration");
  require(bmh::editor::read_file(root / "manifest.json") == original,
          "Demo altered the OBW source");
  auto bad = Json::parse(bmh::editor::read_file(save));
  bad["world"] = "world:different";
  write(root, "bad.ui.json", bad);
  const auto accepted = source.snapshot();
  bool rejected = false;
  try {
    source.load(root / "bad.ui.json");
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected && source.snapshot().state == accepted.state &&
              source.snapshot().revision == accepted.revision,
          "Failed load changed the session");
  bad = Json::parse(bmh::editor::read_file(save));
  bad["desktop"] = corrupt;
  write(root, "bad-layout.ui.json", bad);
  rejected = false;
  try {
    source.load(root / "bad-layout.ui.json");
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected && source.snapshot().state == accepted.state &&
              source.snapshot().revision == accepted.revision,
          "Invalid desktop partially restored demo");
  DemoSource sports(world);
  const auto initial_sports = sports.snapshot();
  require(!sports
               .apply({0,
                       "training",
                       {{"kind", "ball"},
                        {"group", "type"},
                        {"index", 0},
                        {"value", 3}}})
               .ok,
          "Training preview budget exceeded");
  require(sports.snapshot().state == initial_sports.state &&
              sports.snapshot().revision == 0,
          "Rejected training mutated demo");
  require(!sports
               .apply({0,
                       "training",
                       {{"kind", "ball"},
                        {"group", "area"},
                        {"index", 0},
                        {"value", 1.5}}})
               .ok,
          "Fractional training value accepted");
  const auto player = world->squad("club:a").front()->value("id", "");
  require(sports
              .apply({0,
                      "training",
                      {{"kind", "ball"},
                       {"group", "player"},
                       {"player", player},
                       {"value", 1}}})
              .ok,
          "Player intensity action failed");
  require(sports.snapshot().state["training"]["players"][player]["intensity"] ==
              0,
          "Selected intensity must toggle to zero in UI demo");
  require(sports
              .apply({1,
                      "training",
                      {{"kind", "focus"}, {"player", player}, {"value", 7}}})
              .ok,
          "Direct focus selection failed");
  for (int i = 0; i < 4; ++i)
    require(sports
                .apply({sports.snapshot().revision,
                        "tactics",
                        {{"kind", "cycle"}, {"index", 0}}})
                .ok,
            "Tactic cycle failed");
  require(sports.snapshot().state["tactics"]["values"][0] ==
              initial_sports.state["tactics"]["values"][0],
          "Tactic cycle did not wrap");
  require(
      sports
          .apply(
              {sports.snapshot().revision, "training", {{"kind", "auto-all"}}})
          .ok,
      "Global automatic training failed");
  const auto automated = sports.snapshot();
  require(automated.state["training"]["players"][player]["intensity"] == 2 &&
              automated.state["training"]["players"][player]["focus"] == 7,
          "Automatic training must preserve focuses");
  require(!sports.apply({automated.revision,
                         "training",
                         {{"kind", "focus"}, {"player", player}, {"value", 0}}})
                  .ok &&
              sports.snapshot().state == automated.state,
          "Automatic player controls accepted manual changes");
  sports.save(root / "sports.ui.json", default_desktop());
  DemoSource restored_sports(world);
  restored_sports.load(root / "sports.ui.json");
  require(restored_sports.snapshot().state == sports.snapshot().state,
          "Training/tactics were lost on restoration");
  auto legacy = Json::parse(bmh::editor::read_file(root / "sports.ui.json"));
  legacy.erase("training");
  legacy.erase("tactics");
  legacy.erase("calendar");
  write(root, "legacy-ui.json", legacy);
  restored_sports.load(root / "legacy-ui.json");
  require(valid_demo_training(restored_sports.snapshot().state["training"],
                              *world, "club:a"),
          "Older UI checkpoint did not migrate safely");
  require(valid_demo_calendar(restored_sports.snapshot().state["calendar"],
                              *world),
          "Older UI checkpoint lost its season calendar");
  for (const auto *theme : {"midnight", "silver", "matchday"}) {
    const auto diagram = bmh::editor::images::decode(
        tactics_diagram(demo_tactics(), theme), ".svg", 256);
    require(diagram.width == 256 && diagram.height > 0,
            "Tactic SVG failed to decode");
  }
  const auto leap_days = calendar_days("2027-07-01");
  require(leap_days.size() == 366 && leap_days.front().weekday == 3 &&
              leap_days.back().iso == "2028-06-30" &&
              std::count_if(leap_days.begin(), leap_days.end(), [](const auto &d) {
                return d.iso == "2028-02-29";
              }) == 1,
          "Season dates lost weekday alignment or leap day");
  require(calendar_days("2026-07-01").size() == 365 &&
              calendar_days("2099-07-01").size() == 365 &&
              calendar_days("0000-07-01").empty() &&
              calendar_days("2027-06-01").empty(),
          "Season date validation or Gregorian century handling failed");
  auto calendar_clubs = world->rows("clubs");
  for (int i = 1; i < 20; ++i) {
    if (i < 18)
      calendar_clubs.push_back({{"id", "club:home" + std::to_string(i)},
                                {"name", "Home " + std::to_string(i)},
                                {"nation", "nation:de"},
                                {"competition", "comp:home"}});
    calendar_clubs.push_back({{"id", "club:other" + std::to_string(i)},
                              {"name", "Other " + std::to_string(i)},
                              {"nation", "nation:other"},
                              {"competition", "comp:other"}});
  }
  table(root, "clubs", calendar_clubs);
  auto calendar_world = Catalog::open(root);
  DemoSource calendar_source(calendar_world);
  auto calendar = calendar_source.snapshot().state.at("calendar");
  const auto fixtures = calendar_fixtures(calendar, *calendar_world, "club:a");
  require(fixtures.size() == 34, "Eighteen-team season needs 34 preview rounds");
  std::map<std::string, int> opponents;
  std::set<int> occupied;
  for (const auto &fixture : fixtures) {
    require(fixture.home == "club:a" || fixture.away == "club:a",
            "Calendar fixture belongs to another career club");
    require(fixture.home != fixture.away && occupied.insert(fixture.day).second,
            "Preview generated self match or duplicate date");
    ++opponents[fixture.home == "club:a" ? fixture.away : fixture.home];
  }
  require(opponents.size() == 17 &&
              std::all_of(opponents.begin(), opponents.end(), [](const auto &p) {
                return p.second == 2;
              }),
          "Preview must show two meetings per opponent");
  const auto original_calendar = calendar_source.snapshot();
  require(!calendar_source.apply({0, "calendar", {{"kind", "select"}, {"value", -1}}}).ok &&
              !calendar_source.apply({0, "calendar", {{"kind", "select"}, {"value", 365}}}).ok &&
              !calendar_source.apply({0, "calendar", {{"kind", "select"}, {"value", 1.5}}}).ok &&
              calendar_source.snapshot().state == original_calendar.state &&
              calendar_source.snapshot().revision == 0,
          "Invalid calendar selection mutated the demo");
  require(calendar_source.apply({0, "calendar", {{"kind", "select"}, {"value", 30}}}).ok &&
              calendar_source.apply({1, "calendar", {{"kind", "league"}, {"value", "comp:other"}}}).ok,
          "Calendar day or foreign preview league selection failed");
  calendar = calendar_source.snapshot().state.at("calendar");
  require(calendar_fixtures(calendar, *calendar_world, "club:a").size() == 38 &&
              calendar["selected"] == 30 &&
              calendar_source.snapshot().state["managers"] == original_calendar.state["managers"],
          "Foreign calendar preview changed selection or career identity");
  calendar_source.save(root / "calendar.ui.json", default_desktop());
  DemoSource restored_calendar(calendar_world);
  restored_calendar.load(root / "calendar.ui.json");
  require(restored_calendar.snapshot().state["calendar"] == calendar,
          "Calendar selection was lost on process restoration");
  require(restored_calendar.snapshot().state["reports"]["league"] == "comp:other",
          "Calendar and report league selection diverged");
  require(restored_calendar.apply({restored_calendar.snapshot().revision, "reports",
                                   {{"kind", "league"}, {"value", "comp:home"}}}).ok &&
              restored_calendar.snapshot().state["calendar"]["league"] == "comp:home" &&
              restored_calendar.snapshot().state["calendar"]["selected"] == 30,
          "Report selection failed to sync without losing the calendar day");
  require(restored_calendar.apply({restored_calendar.snapshot().revision, "opening", "complete"}).ok &&
              restored_calendar.snapshot().state["opening_created"] == true,
          "Completed opening lost its cup preview marker");
  restored_calendar.save(root / "reports.ui.json", default_desktop());
  DemoSource restored_reports(calendar_world);
  restored_reports.load(root / "reports.ui.json");
  require(restored_reports.snapshot().state == restored_calendar.snapshot().state,
          "Report state or completed opening failed to round-trip");
  auto old_reports = Json::parse(bmh::editor::read_file(root / "calendar.ui.json"));
  old_reports.erase("reports");
  old_reports.erase("opening_created");
  write(root, "old-reports.ui.json", old_reports);
  restored_reports.load(root / "old-reports.ui.json");
  require(valid_demo_reports(restored_reports.snapshot().state["reports"], *calendar_world) &&
              restored_reports.snapshot().state["reports"]["league"] == "comp:other" &&
              restored_reports.snapshot().state["opening_created"] == false,
          "Old checkpoint migration lost the selected league or invented a draw");
  const auto accepted_reports = restored_reports.snapshot();
  auto bad_reports = Json::parse(bmh::editor::read_file(root / "reports.ui.json"));
  bad_reports["reports"]["round"] = -1;
  write(root, "bad-reports.ui.json", bad_reports);
  rejected = false;
  try {
    restored_reports.load(root / "bad-reports.ui.json");
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected && restored_reports.snapshot().state == accepted_reports.state &&
              restored_reports.snapshot().revision == accepted_reports.revision &&
              !restored_reports.apply({accepted_reports.revision, "reports",
                                        {{"kind", "league"}, {"value", "missing"}}}).ok &&
              restored_reports.snapshot().state == accepted_reports.state,
          "Rejected report load/action partially mutated the session");
  const auto planning_before = restored_reports.snapshot();
  const auto plan_action = [&](const Json &change) {
    return restored_reports.apply({restored_reports.snapshot().revision, "calendar", change}).ok;
  };
  require(plan_action({{"kind", "friendly-league"}, {"value", "comp:other"}}) &&
              plan_action({{"kind", "friendly-opponent"}, {"value", "club:b"}}) &&
              plan_action({{"kind", "friendly-venue"}, {"value", true}}) &&
              plan_action({{"kind", "friendly-stage"}, {"value", 1}}) &&
              plan_action({{"kind", "select"}, {"value", 7}}) &&
              plan_action({{"kind", "friendly-book"}}),
          "Planning through the shared source failed");
  require(restored_reports.snapshot().state["cash"] == planning_before.state["cash"] &&
              restored_reports.snapshot().state["managers"] == planning_before.state["managers"] &&
              restored_reports.snapshot().state["training"] == planning_before.state["training"],
          "Presentation planning mutated finances, managers or training");
  require(plan_action({{"kind", "select"}, {"value", 30}}) &&
              plan_action({{"kind", "postpone-source"}}) &&
              plan_action({{"kind", "select"}, {"value", 8}}) &&
              plan_action({{"kind", "postpone-move"}}),
          "Rescheduling used the foreign preview instead of the manager schedule");
  restored_reports.save(root / "planning.ui.json", default_desktop());
  DemoSource restored_planning(calendar_world);
  restored_planning.load(root / "planning.ui.json");
  require(restored_planning.snapshot().state == restored_reports.snapshot().state,
          "Bookings or moves were lost on source round-trip");
  const auto planning_accepted = restored_planning.snapshot();
  auto colliding_plan = Json::parse(bmh::editor::read_file(root / "planning.ui.json"));
  colliding_plan["calendar"]["friendlies"][0]["day"] = 8;
  write(root, "colliding-plan.ui.json", colliding_plan);
  rejected = false;
  try { restored_planning.load(root / "colliding-plan.ui.json"); }
  catch (const std::exception &) { rejected = true; }
  require(rejected && restored_planning.snapshot().state == planning_accepted.state &&
              restored_planning.snapshot().revision == planning_accepted.revision,
          "Conflicting plan partially loaded");
  auto old_planning = Json::parse(bmh::editor::read_file(root / "calendar.ui.json"));
  for (const auto *key : {"source_day", "friendly_league", "opponent", "home", "friendly_stage"})
    old_planning["calendar"].erase(key);
  old_planning["reports"]["sorts"].erase("indoor");
  write(root, "old-planning.ui.json", old_planning);
  restored_planning.load(root / "old-planning.ui.json");
  require(restored_planning.snapshot().state["calendar"]["league"] == "comp:other" &&
              restored_planning.snapshot().state["calendar"]["friendly_league"] == "comp:home" &&
              restored_planning.snapshot().state["reports"]["sorts"]["indoor"]["key"] == "points",
          "Old checkpoint lost league preview or failed planning/indoor migration");
  GuiAssets absent(root / "missing-gui");
  require(!absent.available(),
          "World resources were mistaken for GUI resources");
  std::cout << "Modern UI catalogue, asset priority, ownership, revision and "
               "demo-save checks passed\n";
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
