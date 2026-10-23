// SPDX-License-Identifier: GPL-3.0-only
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
