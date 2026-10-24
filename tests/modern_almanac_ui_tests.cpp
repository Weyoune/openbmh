// SPDX-License-Identifier: GPL-3.0-only
#include "modern/ui/demo_almanac.hpp"
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
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
std::string club(int i) { return "club:almanac-test-" + std::to_string(i); }
std::string player(int team, int i) {
  return "player:almanac-test-" + std::to_string(team) + "-" + std::to_string(i);
}
struct Fixture {
  fs::path root;
  std::shared_ptr<const Catalog> world;
  Json clubs = Json::array(), players = Json::array();
  void write(const std::string &file, const Json &value) const {
    fs::create_directories((root / file).parent_path());
    std::ofstream out(root / file); out << value.dump();
    require(bool(out), "Cannot create synthetic world");
  }
  explicit Fixture(int teams = 6, int squad = 18, Json start = "2027-07-07") {
    root = fs::temp_directory_path() / ("openbmh-almanac-ui-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    write("manifest.json", {{"format", "openbmh-world"}, {"kind", "world"},
        {"format_version", "1.1"}, {"id", "world:synthetic-almanac-ui"},
        {"start_date", start}, {"requires", {"editor:world-v1"}},
        {"x-editor", {{"profile", "editor-world-v1"}}}});
    write("world/nations.json", {{"items", {{{"id", "nation:de"}, {"name", "Synthetic nation"}}}}});
    write("world/competitions.json", {{"items", {
        {{"id", "competition:almanac-league"}, {"name", "Synthetic league"},
         {"type", "league"}, {"nation", "nation:de"}},
        {{"id", "competition:almanac-cup"}, {"name", "Synthetic cup"}, {"type", "cup"}}}}});
    for (int team = 0; team < teams; ++team) {
      clubs.push_back({{"id", club(team)}, {"name", "Historical club " + std::to_string(team)},
          {"nation", "nation:de"}, {"competition", "competition:almanac-league"},
          {"crest", "asset:synthetic-crest-" + std::to_string(team)}});
      for (int i = 0; i < squad; ++i) {
        // A keeper is deliberately not the first squad row. Another keeper and
        // every remaining player must survive in the complete bench snapshot.
        const auto role = i == 2 || i == 14 ? "gk" : i < 7 ? "def" : i < 12 ? "mid" : "fwd";
        players.push_back({{"id", player(team, i)}, {"team", club(team)},
            {"name", {{"first", "Synthetic " + std::to_string(team)},
                       {"last", "Ältester " + std::to_string(i)}}},
            {"positions", {role}}, {"squad_order", i}});
      }
    }
    write("world/clubs.json", {{"items", clubs}});
    write("world/players.json", {{"items", players}});
    world = Catalog::open(root);
  }
  ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
};
Json query(const Json &archive, Json request, const std::string &cutoff = "2027-07-07") {
  auto result = query_demo_almanac(archive, request, cutoff);
  if (result.at("ok") != true)
    throw std::runtime_error("Unexpected query error " + result.dump() + " " + request.dump());
  return result;
}
void rejected(const Json &archive, const Json &request, const char *code,
              const std::string &cutoff = "2027-07-07") {
  const auto before = archive;
  const auto result = query_demo_almanac(archive, request, cutoff);
  require(result.at("ok") == false && result.at("error") == code && archive == before,
          "Malformed query was accepted or changed archive");
}
Json changed_match(const Json &archive, const Json &match, size_t index = 0) {
  auto changed = archive;
  changed["matches"][index] = match;
  return changed;
}
} // namespace
int main() try {
  Fixture fixture;
  const auto &world = *fixture.world;
  const auto original_clubs = world.rows("clubs"), original_players = world.rows("players");
  const auto archive = make_demo_almanac(world), original = archive;
  require(valid_demo_almanac(archive) && archive.at("matches").size() == 12 &&
      archive == make_demo_almanac(world), "Archive is invalid or nondeterministic");
  require(world.rows("clubs") == original_clubs && world.rows("players") == original_players,
      "Archive generation changed catalogue");
  const auto index = query(archive, {{"kind", "index"}});
  require(index.at("years") == Json::array({2025, 2026, 2027}) && index.at("match_count") == 12 &&
      index.at("first_date") == "2025-08-03" && index.at("last_date") == "2027-05-18",
      "Wrong archive years or two-season range");
  std::set<std::string> seasons, kinds, ids;
  std::set<int> goals, misses;
  for (const auto &m : archive.at("matches")) {
    seasons.insert(m.at("season").get<std::string>()); ids.insert(m.at("id").get<std::string>());
    require(world.find("clubs", m.at("home").at("id")) &&
        world.find("clubs", m.at("away").at("id")), "Archive invented a club identity");
    for (const auto &e : m.at("events")) {
      kinds.insert(e.at("kind").get<std::string>());
      if (e.at("kind") == "goal") goals.insert(e.at("detail").get<int>());
      if (e.at("kind") == "miss") misses.insert(e.at("detail").get<int>());
    }
    for (const auto *side : {"home", "away"}) {
      const auto &l = m.at("lineups").at(side);
      if (l.is_null()) {
        require(std::string(side) == "away" && m.at("date").get<std::string>().substr(5) == "05-18",
            "Complete catalogue unexpectedly lost its recorded lineup");
        continue;
      }
      require(l.at("starters").size() == 11 && l.at("bench").size() == 7 &&
          l.at("starters")[0].at("position") == "gk", "Starting XI or full bench is implausible");
      std::set<std::string> roster;
      int keepers = 0;
      for (const auto *section : {"starters", "bench"})
        for (const auto &p : l.at(section)) {
          const auto *source = world.find("players", p.at("id"));
          require(source && world.name(*source) == p.at("name").get<std::string>() &&
              source->at("team") == m.at(side).at("id") && roster.insert(p.at("id").get<std::string>()).second,
              "Roster changed a historical name, duplicated or invented a player");
          if (std::string(section) == "starters" && p.at("position") == "gk") ++keepers;
        }
      require(keepers == 1 && roster.size() == world.squad(m.at(side).at("id")).size(),
          "Bench omits squad players or starters contain multiple keepers");
    }
  }
  require(ids.size() == 12 && seasons == std::set<std::string>{"2025/26", "2026/27"} &&
      kinds == std::set<std::string>{"goal", "miss", "yellow", "second_yellow", "red", "injury", "substitution", "shootout"} &&
      goals == std::set<int>{0, 1, 2, 3} && misses == std::set<int>{0, 1, 2, 3},
      "Archive lacks requested event types or chance kinds");
  const auto first = archive.at("matches")[0], extra = archive.at("matches")[3], penalties = archive.at("matches")[4];
  require(!first.at("home").at("manager").is_null() && !first.at("away").at("manager").is_null() &&
      first.at("home").at("manager").at("id") != first.at("away").at("manager").at("id"),
      "No historical human-versus-human manager example");
  require(first.at("result").at("regulation") == Json::array({3, 1}) &&
      first.at("result").at("extra").is_null() && extra.at("result").at("regulation") == Json::array({1, 1}) &&
      extra.at("result").at("extra") == Json::array({1, 0}) &&
      penalties.at("result").at("extra") == Json::array({1, 1}) &&
      penalties.at("result").at("final") == Json::array({2, 2}) &&
      penalties.at("result").at("penalties") == Json::array({4, 3}), "Phase score semantics changed");
  Json kicks = Json::array();
  for (const auto &e : penalties.at("events")) if (e.at("kind") == "shootout") kicks.push_back(e);
  require(kicks.size() == 10 && kicks[0].at("side") == "home" && kicks[9].at("side") == "away" &&
      kicks[9].at("minute") == 121 && kicks[9].at("detail") == 0, "Complete shootout order was lost");
  require(archive.at("matches")[1].at("attendance") == 0 &&
      archive.at("matches")[1].at("weather").at("temperature") == 0 &&
      archive.at("matches")[1].at("weather").at("rain") == 0 &&
      archive.at("matches")[5].at("attendance").is_null() &&
      archive.at("matches")[5].at("referee").is_null() &&
      archive.at("matches")[5].at("weather").at("temperature").is_null() &&
      archive.at("matches")[5].at("lineups").at("away").is_null(),
      "Zero-valued and missing historical metadata were conflated");
  const auto calendar = query(archive, {{"kind", "calendar"}, {"year", 2025}});
  require(calendar.at("days").size() == 2 && calendar.at("days")[0].at("date") == "2025-08-03" &&
      calendar.at("days")[0].at("count") == 3 && calendar.at("days")[0].at("highlights").size() == 1 &&
      calendar.at("days")[1].at("highlights").size() == 1, "Calendar grouping or highlights incorrect");
  const auto day = query(archive, {{"kind", "day"}, {"date", "2025-08-03"}});
  require(day.at("matches").size() == 3 && day.at("matches")[0] == first &&
      query(archive, {{"kind", "match"}, {"id", first.at("id")}}).at("match") == first,
      "Day or match projection lost normalized snapshots");
  auto copied = day; copied["matches"][0]["home"]["name"] = "Modified result copy";
  require(query(archive, {{"kind", "match"}, {"id", first.at("id")}}).at("match") == first,
      "Mutable query result aliases immutable archive");
  auto shuffled = archive;
  std::reverse(shuffled["matches"].begin(), shuffled["matches"].end());
  require(query(shuffled, {{"kind", "calendar"}, {"year", 2025}}).at("days")[0].at("date") == "2025-08-03",
      "Calendar query relies on source ordering");
  const auto before_start = query(archive, {{"kind", "index"}}, "2025-08-02");
  require(before_start.at("years").empty() && before_start.at("match_count") == 0 &&
      before_start.at("first_date").is_null() && before_start.at("last_date").is_null(),
      "Cutoff leaked future archive range");
  require(query(archive, {{"kind", "index"}}, "2025-08-03").at("match_count") == 3 &&
      query(archive, {{"kind", "index"}}, "2025-12-14").at("match_count") == 4 &&
      query(archive, {{"kind", "index"}}, "2025-12-14").at("years") == Json::array({2025}),
      "Cutoff does not include exactly the played dates");
  require(query(archive, {{"kind", "day"}, {"date", "2027-02-20"}}, "2025-08-03").at("matches").empty() &&
      query(archive, {{"kind", "calendar"}, {"year", 2027}}, "2025-08-03").at("days").empty(),
      "Future day or calendar query leaked records");
  rejected(archive, {{"kind", "match"}, {"id", penalties.at("id")}}, "almanac_match_not_found", "2025-08-03");
  rejected(archive, {{"kind", "match"}, {"id", "does-not-exist"}}, "almanac_match_not_found");
  for (const auto &request : std::vector<Json>{nullptr, Json::array(), true, Json::object(),
      {{"kind", 1}}, {{"kind", "unknown"}}, {{"kind", "index"}, {"date", "2025-08-03"}},
      {{"kind", "calendar"}}, {{"kind", "calendar"}, {"year", 2025.0}},
      {{"kind", "calendar"}, {"year", true}}, {{"kind", "calendar"}, {"year", uint64_t(-1)}},
      {{"kind", "calendar"}, {"year", 0}}, {{"kind", "calendar"}, {"year", 10000}},
      {{"kind", "day"}}, {{"kind", "day"}, {"date", "2025-02-29"}},
      {{"kind", "day"}, {"date", "2025-13-01"}}, {{"kind", "day"}, {"date", "2025-8-03"}},
      {{"kind", "day"}, {"date", 20250803}}, {{"kind", "match"}},
      {{"kind", "match"}, {"id", ""}}, {{"kind", "match"}, {"id", true}}})
    rejected(archive, request, "invalid_almanac_request");
  for (const auto &cutoff : {"", "2025-02-29", "0000-01-01", "2025-13-01", "2027-7-07", "2027-07-07T00:00:00"})
    rejected(archive, {{"kind", "index"}}, "invalid_almanac_cutoff", cutoff);
  require(query(archive, {{"kind", "day"}, {"date", "2024-02-29"}}).at("matches").empty(),
      "Valid leap date was rejected");
  std::vector<Json> invalid;
  auto bad = archive; bad["matches"].push_back(first); invalid.push_back(bad);
  bad = archive; bad["schema"] = "unknown"; invalid.push_back(bad);
  bad = archive; bad["unexpected"] = 1; invalid.push_back(bad);
  bad = archive; bad["matches"] = Json::object(); invalid.push_back(bad);
  std::vector<Json> bad_matches;
  for (const auto *key : {"id", "date", "season", "competition", "round", "home", "away", "result", "referee", "attendance", "weather", "lineups", "events"}) {
    auto m = first; m.erase(key); bad_matches.push_back(m);
  }
  auto m = first; m["date"] = "2025-02-29"; bad_matches.push_back(m);
  m = first; m["season"] = "2024/25"; bad_matches.push_back(m);
  m = first; m["away"]["id"] = m["home"]["id"]; bad_matches.push_back(m);
  m = first; m["home"]["manager"]["id"] = nullptr; bad_matches.push_back(m);
  m = first; m["round"] = uint64_t(-1); bad_matches.push_back(m);
  m = first; m["attendance"] = -1; bad_matches.push_back(m);
  m = first; m["weather"]["rain"] = 8; bad_matches.push_back(m);
  m = first; m["weather"]["temperature"] = true; bad_matches.push_back(m);
  m = first; m["result"]["final"] = {4, 1}; bad_matches.push_back(m);
  m = first; m["result"]["regulation"] = {2, 1}; bad_matches.push_back(m);
  m = first; m["result"]["extra_played"] = 1; bad_matches.push_back(m);
  m = first; m["result"]["extra"] = {3, 1}; bad_matches.push_back(m);
  m = first; m["lineups"]["home"]["starters"].erase(m["lineups"]["home"]["starters"].end() - 1); bad_matches.push_back(m);
  m = first; m["lineups"]["home"]["bench"].push_back(m["lineups"]["home"]["starters"][0]); bad_matches.push_back(m);
  m = first; m["lineups"]["home"]["starters"][1]["position"] = "gk"; bad_matches.push_back(m);
  m = first; m["lineups"]["away"]["starters"][1]["id"] = m["lineups"]["home"]["starters"][1]["id"]; bad_matches.push_back(m);
  m = first; m["events"][0]["kind"] = "unsupported"; bad_matches.push_back(m);
  m = first; m["events"][0]["detail"] = 4; bad_matches.push_back(m);
  m = first; m["events"][0]["minute"] = 0; bad_matches.push_back(m);
  m = first; m["events"][0]["side"] = 0; bad_matches.push_back(m);
  m = first; m["events"][1]["minute"] = 5; bad_matches.push_back(m);
  m = first; m["events"][0]["player"]["name"] = "Wrong snapshot name"; bad_matches.push_back(m);
  m = first; m["events"][0]["player"]["id"] = player(1, 0); bad_matches.push_back(m);
  m = first; m["events"][3]["kind"] = "second_yellow"; bad_matches.push_back(m);
  m = first; m["events"][7]["player"] = m["events"][7]["other"]; bad_matches.push_back(m);
  m = first; m["events"][7]["other"] = nullptr; bad_matches.push_back(m);
  m = first; m["events"].back()["player"] = m["events"][3]["player"]; bad_matches.push_back(m);
  for (const auto &record : bad_matches) invalid.push_back(changed_match(archive, record));
  m = penalties; m["result"]["penalties"] = {4, 4}; invalid.push_back(changed_match(archive, m, 4));
  m = penalties; m["result"]["extra"] = {2, 2}; invalid.push_back(changed_match(archive, m, 4));
  m = extra; m["result"]["extra"] = {2, 1}; invalid.push_back(changed_match(archive, m, 3));
  m = penalties; m["events"].erase(m["events"].end() - 1); invalid.push_back(changed_match(archive, m, 4));
  m = penalties; m["events"].back()["detail"] = 1; invalid.push_back(changed_match(archive, m, 4));
  m = penalties; m["events"].back()["side"] = "home"; invalid.push_back(changed_match(archive, m, 4));
  m = penalties; m["events"].back()["minute"] = 120; invalid.push_back(changed_match(archive, m, 4));
  m = penalties; m["events"].push_back(m["events"].back()); invalid.push_back(changed_match(archive, m, 4));
  for (const auto &value : invalid) {
    require(!valid_demo_almanac(value), "Corrupt score, roster, event or archive was accepted");
    rejected(value, {{"kind", "index"}}, "invalid_demo_almanac");
  }
  auto nullable = archive;
  for (auto &record : nullable["matches"]) {
    record["referee"] = nullptr; record["attendance"] = nullptr;
    record["weather"] = {{"temperature", nullptr}, {"rain", nullptr}};
    record["result"]["regulation"] = nullptr;
    if (record["result"]["extra_played"] == true) record["result"]["extra"] = nullptr;
  }
  require(valid_demo_almanac(nullable), "Missing metadata or unknown phase score was invented or rejected");
  auto unknown_position = archive;
  unknown_position["matches"][0]["lineups"]["home"]["starters"][0].erase("position");
  require(valid_demo_almanac(unknown_position), "Optional archived position was required");
  auto away_first = penalties;
  auto &ordered_events = away_first["events"];
  for (size_t i = 4; i < ordered_events.size(); i += 2)
    std::swap(ordered_events[i], ordered_events[i + 1]);
  require(valid_demo_almanac(changed_match(archive, away_first, 4)),
      "Alternating shootout with away side first was rejected");
  require(valid_demo_almanac(Json::parse(archive.dump())), "Archive JSON roundtrip lost validity");
  fixture.clubs[0]["name"] = "Current renamed club";
  fixture.clubs[0]["crest"] = "asset:replacement-crest";
  fixture.players[0]["name"] = "Current renamed player";
  fixture.write("world/clubs.json", {{"items", fixture.clubs}});
  fixture.write("world/players.json", {{"items", fixture.players}});
  const auto renamed = Catalog::open(fixture.root);
  require(renamed->name(*renamed->find("clubs", club(0))) != first.at("home").at("name").get<std::string>() &&
      query(archive, {{"kind", "match"}, {"id", first.at("id")}}).at("match") == first,
      "Query re-looked up historical names or crests in current catalogue");
  Fixture partial(2, 8); const auto incomplete = make_demo_almanac(*partial.world);
  require(valid_demo_almanac(incomplete) && incomplete["matches"].size() == 12 &&
      incomplete["matches"][0]["lineups"]["home"].is_null(), "Partial squad produced fabricated starters");
  for (const auto &e : incomplete["matches"][0]["events"])
    require(e["player"].is_null() && e["other"].is_null(), "Missing squad invented historical players");
  Fixture one(1); const auto empty = make_demo_almanac(*one.world);
  require(valid_demo_almanac(empty) && empty["matches"].empty() &&
      query(empty, {{"kind", "index"}}).at("years").empty() &&
      query(empty, {{"kind", "calendar"}, {"year", 2025}}).at("days").empty() &&
      query(empty, {{"kind", "day"}, {"date", "2025-08-03"}}).at("matches").empty(),
      "Empty archive query is unsafe");
  rejected(empty, {{"kind", "match"}, {"id", first.at("id")}}, "almanac_match_not_found");
  Fixture early(2, 0, "2027-02-12"); const auto winter = make_demo_almanac(*early.world);
  require(query(winter, {{"kind", "index"}}, "2027-02-12").at("years") == Json::array({2024, 2025, 2026}),
      "January-June manifest date selected wrong season");
  Fixture ancient(2, 0, "0002-07-01");
  require(make_demo_almanac(*ancient.world)["matches"].empty(), "Ancient date underflowed demo history");
  Fixture malformed(2, 0, false);
  require(make_demo_almanac(*malformed.world)["matches"].empty(), "Invalid manifest date invented an archive year");
  require(archive == original && world.rows("clubs") == original_clubs && world.rows("players") == original_players,
      "Read-only queries or rejection mutated archive or world");
  std::cout << "Modern UI almanac checks passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n'; return 1;
}
