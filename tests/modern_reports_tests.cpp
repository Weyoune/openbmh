// SPDX-License-Identifier: GPL-3.0-only
#include "modern/ui/demo_reports.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

using namespace bmh::modern_ui;
namespace {
void require(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
void write(const fs::path &root, const std::string &name, const Json &value) {
  fs::create_directories((root / name).parent_path());
  bmh::editor::write_atomic(root / name, value.dump());
}
void table(const fs::path &root, const std::string &name, const Json &items) {
  write(root, "world/" + name + ".json", {{"items", items}});
}
std::string league(int i) { return "competition:synthetic-" + std::to_string(i); }
std::string club(int group, int i) {
  return "club:synthetic-" + std::to_string(group) + "-" + std::to_string(i);
}
struct Fixture {
  fs::path root;
  std::shared_ptr<const Catalog> world;
  explicit Fixture(int home = 18, bool leagues = true) {
    root = fs::temp_directory_path() /
           ("openbmh-reports-" + std::to_string(
              std::chrono::steady_clock::now().time_since_epoch().count()));
    write(root, "manifest.json", {{"format", "openbmh-world"}, {"kind", "world"},
        {"format_version", "1.1"}, {"id", "world:synthetic-reports"},
        {"requires", {"editor:world-v1"}},
        {"x-editor", {{"profile", "editor-world-v1"}}}});
    table(root, "nations", Json::array({
      {{"id", "nation:de"}, {"name", "Synthetic home"}},
      {{"id", "nation:synthetic-foreign"}, {"name", "Synthetic away"}},
      {{"id", "nation:synthetic-empty"}, {"name", "Empty nation"}}}));
    Json competitions = Json::array(), clubs = Json::array(), players = Json::array();
    const std::array<int, 4> counts{20, home, 4, 48};
    if (leagues) {
      for (int group = 0; group < 4; ++group)
        competitions.push_back({{"id", league(group)}, {"type", "league"},
          {"nation", group == 0 ? "nation:synthetic-foreign" : "nation:de"},
          {"name", "Synthetic league " + std::to_string(group)}});
      for (int i = 0; i < 48; ++i)
        for (int group = 0; group < 4; ++group)
          if (i < counts[size_t(group)]) {
            const auto name = group != 1 ? "Synthetic " + std::to_string(i)
                : i == 0 ? "Zulu 10" : i == 1 || i == 3 ? "alpha 2"
                : i == 2 ? "Älpha 10" : i == 4 ? "beta" : "Group " + std::to_string(i);
            clubs.push_back({{"id", club(group, i)}, {"name", name},
                {"competition", league(group)},
                {"nation", group == 0 ? "nation:synthetic-foreign" : "nation:de"}});
          }
    }
    competitions.push_back({{"id", "competition:synthetic-cup"}, {"type", "cup"}});
    int serial = 0;
    const auto player = [&](int index, const Json &positions, const Json &strength) {
      if (index >= home || !leagues) return;
      players.push_back({{"id", "player:synthetic-" + std::to_string(serial++)},
          {"team", club(1, index)}, {"squad_order", serial},
          {"positions", positions}, {"attributes", {{"strength", strength}}}});
    };
    player(0, {"gk"}, 80); player(0, {"gk"}, 60);
    player(0, {"def"}, 90); player(0, {"mid"}, 70); player(0, {"fwd"}, 50);
    player(1, {"def"}, 88); player(1, {"gk"}, "not-a-rating");
    player(3, {"def", "mid"}, 40);
    player(4, {"gk"}, 101); player(5, {"gk"}, 99);
    player(6, {"gk"}, 70); player(7, {"gk"}, 0);
    table(root, "competitions", competitions); table(root, "clubs", clubs);
    table(root, "players", players); world = Catalog::open(root);
  }
  ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
};
void edit(Json &state, const Json &action, const Catalog &world) {
  const auto error = edit_demo_reports(state, action, world);
  if (!error.empty()) throw std::runtime_error("Unexpected rejection: " + error);
  require(valid_demo_reports(state, world), "Accepted action produced invalid state");
}
std::map<std::string, Json> by_club(const Json &rows) {
  std::map<std::string, Json> result;
  for (const auto &r : rows) result.emplace(r.at("club").get<std::string>(), r);
  return result;
}
void check_league(const Catalog &world, const std::string &id, int count) {
  auto state = demo_reports(world); edit(state, {{"kind", "league"}, {"value", id}}, world);
  const int rounds = report_round_count(world, state);
  require(rounds == (count - 1) * 2, "League round count is not dynamic");
  std::set<std::pair<std::string, std::string>> ordered_pairs;
  std::vector<Json> round_rows;
  std::map<std::string, int> scored, conceded;
  for (int r = 0; r < rounds; ++r) {
    state["round"] = r;
    const auto matches = report_match_rows(world, state, false);
    require(matches.size() == size_t(count / 2), "Wrong number of matches per round");
    require(matches == report_match_rows(world, state, true), "Competition zero differs from league");
    std::set<std::string> teams;
    for (const auto &m : matches) {
      const auto home = m.at("home").get<std::string>(), away = m.at("away").get<std::string>();
      require(home != away && teams.insert(home).second && teams.insert(away).second,
              "A team appears twice or plays itself");
      require(ordered_pairs.emplace(home, away).second, "Ordered league pair repeated");
      require(m.at("round") == r && m.at("home_goals").is_number_integer() &&
              m.at("away_goals").is_number_integer(), "Malformed match projection");
      if (r < 26) {
        scored[home] += m.at("home_goals").get<int>();
        scored[away] += m.at("away_goals").get<int>();
        conceded[home] += m.at("away_goals").get<int>();
        conceded[away] += m.at("home_goals").get<int>();
      }
    }
    require(teams.size() == size_t(count), "League round does not contain every team");
    round_rows.push_back(matches);
  }
  require(ordered_pairs.size() == size_t(count * (count - 1)), "Not a complete two-leg schedule");
  for (int r = 0; r < rounds / 2; ++r)
    for (size_t i = 0; i < round_rows[size_t(r)].size(); ++i) {
      const auto &a = round_rows[size_t(r)][i], &b = round_rows[size_t(r + rounds / 2)][i];
      require(a.at("home") == b.at("away") && a.at("away") == b.at("home"),
              "Second half does not reverse venues");
    }
  state["round"] = 26;
  const auto all = by_club(report_table_rows(world, state));
  int gf = 0, ga = 0, wins = 0, loss = 0, draws = 0, points = 0;
  for (const auto &[id, row] : all) {
    require(row.at("played") == 26, "Table must project exactly26 played rounds");
    const int w = row.at("wins"), d = row.at("draw"), l = row.at("loss");
    require(w + d + l == 26 && row.at("points") == w * 3 + d, "Points/results conservation failed");
    require(row.at("gf") == scored[id] && row.at("ga") == conceded[id],
            "Table goals disagree with match projections");
    require(row.at("diff") == row.at("gf").get<int>() - row.at("ga").get<int>(), "Wrong goal difference");
    const auto text = row.at("form_text").get<std::string>();
    require(text.size() == 8, "Last-eight form length is wrong");
    int form = 0;
    for (char c : text) {
      require(c == 'S' || c == 'U' || c == 'N', "Unknown form symbol");
      form += c == 'S' ? 3 : c == 'U' ? 1 : 0;
    }
    require(row.at("form") == form, "Form metric is not numeric points");
    gf += row.at("gf").get<int>(); ga += row.at("ga").get<int>();
    wins += w; loss += l; draws += d; points += row.at("points").get<int>();
  }
  require(gf == ga && wins == loss && draws % 2 == 0 && points == wins * 3 + draws,
          "League totals are not conserved");
  std::array<std::map<std::string, Json>, 5> filtered;
  filtered[0] = all;
  for (int filter = 1; filter < 5; ++filter) {
    edit(state, {{"kind", "filter"}, {"value", filter}}, world);
    filtered[size_t(filter)] = by_club(report_table_rows(world, state));
  }
  for (const auto &[id, row] : all)
    for (const char *key : {"played", "wins", "draw", "loss", "gf", "ga", "points", "diff"}) {
      const int value = row.at(key);
      require(value == filtered[1].at(id).at(key).get<int>() + filtered[2].at(id).at(key).get<int>(),
              "First/second half filters do not reconstruct full table");
      require(value == filtered[3].at(id).at(key).get<int>() + filtered[4].at(id).at(key).get<int>(),
              "Home/away filters do not reconstruct full table");
    }
}
} // namespace
int main() try {
  Fixture fixture;
  const auto &world = *fixture.world;
  const auto immutable_clubs = world.rows("clubs"), immutable_players = world.rows("players");
  auto state = demo_reports(world);
  require(valid_demo_reports(state, world) && state.at("league") == league(1) &&
          state.at("round") == 26, "Default state did not choose first German league");
  check_league(world, league(1), 18); check_league(world, league(0), 20);
  auto first = state; first["round"] = 0;
  const auto first_match = report_match_rows(world, first, false).front();
  require(first_match.at("home") == club(1, 0) && first_match.at("away") == club(1, 17) &&
          first_match.at("home_goals") == 0 && first_match.at("away_goals") == 2,
          "Pinned synthetic Gold preview score changed");
  const auto ranked = report_table_rows(world, state);
  edit(state, {{"kind", "sort"}, {"view", "tables"}, {"key", "rank"}}, world);
  require(state["sorts"]["tables"]["desc"] == false && report_table_rows(world, state) == ranked,
          "Rank sort used club names instead of places");
  edit(state, {{"kind", "sort"}, {"view", "tables"}, {"key", "rank"}}, world);
  require(report_table_rows(world, state).front().at("rank") == 18, "Rank sort toggle failed");
  edit(state, {{"kind", "sort"}, {"view", "tables"}, {"key", "gf"}}, world);
  const auto goals = report_table_rows(world, state);
  for (size_t i = 1; i < goals.size(); ++i)
    require(goals[i-1]["gf"] > goals[i]["gf"] ||
            (goals[i-1]["gf"] == goals[i]["gf"] && goals[i-1]["original"] < goals[i]["original"]),
            "Numeric sort or stable original tiebreak failed");
  auto strengths = report_strength_rows(world, state);
  const auto indexed = by_club(strengths);
  require(indexed.at(club(1, 0)).at("keeper") == 70 && indexed.at(club(1, 0)).at("strength") == 70,
          "Strengths did not average actual squad attributes");
  require(indexed.at(club(1, 1)).at("keeper").is_null() && indexed.at(club(1, 1)).at("defence") == 88 &&
          indexed.at(club(1, 2)).at("strength").is_null() && indexed.at(club(1, 4)).at("keeper").is_null(),
          "Missing/malformed positions invented ratings");
  require(indexed.at(club(1, 3)).at("defence") == 40 && indexed.at(club(1, 3)).at("midfield") == 40 &&
          indexed.at(club(1, 7)).at("keeper") == 0, "Multi-position or valid zero rating lost");
  edit(state, {{"kind", "sort"}, {"view", "strengths"}, {"key", "club"}}, world);
  strengths = report_strength_rows(world, state);
  require(strengths[0]["club"] == club(1, 1) && strengths[1]["club"] == club(1, 3) &&
          strengths[2]["club"] == club(1, 2), "Club alphabetic/numeric/accent sort or stable tie failed");
  edit(state, {{"kind", "sort"}, {"view", "strengths"}, {"key", "keeper"}}, world);
  strengths = report_strength_rows(world, state);
  require(strengths[0]["keeper"] == 99 && strengths.back()["keeper"].is_null(), "Nullable descending strength sort failed");
  const auto saved_strength_sort = state["sorts"]["strengths"];
  edit(state, {{"kind", "league"}, {"value", league(0)}}, world); state["round"] = 37;
  edit(state, {{"kind", "competition"}, {"value", 1}}, world);
  require(state["round"] == 33 && report_round_count(world, state, true) == 34,
          "Other preview did not clamp its34-round range");
  state["round"] = 37;
  require(valid_demo_reports(state, world) && report_match_rows(world, state, true)[0]["round"] == 33,
          "Shared state was not safely projected in other context");
  edit(state, {{"kind", "league"}, {"value", league(2)}}, world);
  require(state["round"] == 5, "Small league change did not clamp round");
  edit(state, {{"kind", "country"}, {"value", "nation:de"}}, world);
  require(state["league"] == league(1), "Country reselect did not select first league");
  edit(state, {{"kind", "filter"}, {"value", 4}}, world);
  state["round"] = 33;
  edit(state, {{"kind", "reset"}}, world);
  require(state["league"] == league(1) && state["filter"] == 0 && state["round"] == 33 &&
          state["sorts"]["tables"]["key"] == "points" && state["sorts"]["tables"]["desc"] == true &&
          state["sorts"]["strengths"] == saved_strength_sort && state["competition"] == 1,
          "Reset lost unrelated sort/competition state or failed defaults");
  edit(state, {{"kind", "round"}, {"step", 1}}, world);
  require(state["round"] == 0, "Round wrap forward failed");
  edit(state, {{"kind", "round"}, {"step", -1}}, world);
  require(state["round"] == 33, "Round wrap backward failed");
  auto foreign_reset = state;
  edit(foreign_reset, {{"kind", "league"}, {"value", league(0)}}, world);
  foreign_reset["round"] = 37;
  edit(foreign_reset, {{"kind", "reset"}}, world);
  require(foreign_reset["league"] == league(1) && foreign_reset["round"] == 33,
          "Reset from foreign20-team league did not restore/clamp German league");
  auto other_wrap = state;
  edit(other_wrap, {{"kind", "league"}, {"value", league(0)}}, world);
  other_wrap["round"] = 37;
  edit(other_wrap, {{"kind", "round"}, {"step", 1}, {"other", true}}, world);
  require(other_wrap["round"] == 0, "Other round context did not clamp/wrap34 rounds");
  edit(other_wrap, {{"kind", "round"}, {"step", -1}, {"other", true}}, world);
  require(other_wrap["round"] == 33, "Other round backward wrap failed");
  edit(other_wrap, {{"kind", "round"}, {"step", 1}, {"other", false}}, world);
  require(other_wrap["round"] == 34, "League round context lost its38-round range");
  const std::array<int, 9> counts{9,8,4,8,4,4,8,8,1};
  for (int competition = 1; competition <= 8; ++competition) {
    edit(state, {{"kind", "competition"}, {"value", competition}}, world);
    const auto matches = report_match_rows(world, state, true);
    require(matches.size() == size_t(counts[size_t(competition)]), "Wrong other competition pair count");
    for (const auto &m : matches)
      require(world.find("clubs", m.at("home"))->at("competition") == league(1) &&
              world.find("clubs", m.at("away"))->at("competition") == league(1),
              "Other preview used clubs outside first German league");
  }
  const auto opening = report_opening_pairs(world);
  require(opening.size() == 32 && opening[0]["home"] == club(1,0) && opening[8]["away"] == club(1,17) &&
          opening[9]["home"] == club(2,0) && opening[11]["home"] == club(3,0) &&
          opening.back()["away"] == club(3,41), "Opening draw order or64-team bound changed");
  for (const auto &m : opening) require(m["home_goals"].is_null() && m["away_goals"].is_null() && m["round"] == 0,
                                       "Opening draw invented results");
  const std::vector<Json> bad_actions = {nullptr, Json::array(), "bad", Json::object(),
    {{"kind", nullptr}}, {{"kind", "unknown"}}, {{"kind", "country"}, {"value", "nation:synthetic-empty"}},
    {{"kind", "country"}, {"value", 1}}, {{"kind", "country"}, {"value", "missing"}},
    {{"kind", "league"}, {"value", "competition:synthetic-cup"}}, {{"kind", "league"}},
    {{"kind", "filter"}, {"value", true}}, {{"kind", "filter"}, {"value", 5}},
    {{"kind", "filter"}, {"value", 1.0}}, {{"kind", "competition"}, {"value", -1}},
    {{"kind", "competition"}, {"value", 9}}, {{"kind", "round"}, {"step", 0}},
    {{"kind", "round"}, {"step", 2}}, {{"kind", "round"}, {"step", true}},
    {{"kind", "round"}, {"step", 1}, {"other", "false"}},
    {{"kind", "sort"}, {"view", "tables"}, {"key", "club"}},
    {{"kind", "sort"}, {"view", "__proto__"}, {"key", "points"}},
    {{"kind", "sort"}, {"view", "strengths"}, {"key", "form_text"}},
    {{"kind", "reset"}, {"unexpected", 1}}, {{"kind", "filter"}, {"value", 0}, {"round", 4}}};
  for (const auto &action : bad_actions) {
    const auto before = state;
    require(!edit_demo_reports(state, action, world).empty() && state == before,
            "Invalid action threw or mutated reports");
  }
  for (const auto *key : {"league", "round", "filter", "competition", "sorts"}) {
    auto bad = state; bad.erase(key); require(!valid_demo_reports(bad, world), "Missing reports field accepted");
    const auto before = bad; require(!edit_demo_reports(bad, {{"kind", "reset"}}, world).empty() && bad == before,
                                   "Corrupt reports state was partially repaired");
  }
  std::vector<Json> bad_states;
  for (const auto &value : std::vector<Json>{-1, 38, 1.5, true, uint64_t(-1)}) {
    auto invalid = state;
    invalid["round"] = value;
    bad_states.push_back(invalid);
  }
  auto bad = state; bad["sorts"]["tables"]["key"] = "club"; bad_states.push_back(bad);
  bad = state; bad["sorts"]["strengths"]["desc"] = 1; bad_states.push_back(bad);
  bad = state; bad["unexpected"] = 0; bad_states.push_back(bad);
  bad = state; bad["league"] = "missing"; bad_states.push_back(bad);
  bad = state; bad["filter"] = true; bad_states.push_back(bad);
  bad = state; bad["sorts"]["tables"]["unexpected"] = 0; bad_states.push_back(bad);
  for (const auto &v : bad_states) require(!valid_demo_reports(v, world), "Malformed reports state accepted");
  Fixture tiny(2); auto small = demo_reports(*tiny.world);
  require(small["round"] == 1, "Default round did not clamp for two clubs");
  edit(small, {{"kind", "competition"}, {"value", 1}}, *tiny.world); small["round"] = 33;
  require(report_match_rows(*tiny.world, small, false)[0]["round"] == 1, "Local league projection did not clamp shared round");
  edit(small, {{"kind", "competition"}, {"value", 0}}, *tiny.world);
  require(small["round"] == 1, "Switching back to league clamped to the wrong round");
  Fixture empty(0, false); const auto none = demo_reports(*empty.world);
  require(valid_demo_reports(none, *empty.world) && report_round_count(*empty.world, none) == 1 &&
          report_table_rows(*empty.world, none).empty() && report_opening_pairs(*empty.world).empty(),
          "Empty presentation catalogue is unsafe");
  require(world.rows("clubs") == immutable_clubs && world.rows("players") == immutable_players,
          "Report projection mutated immutable world data");
  std::cout << "Modern report projection checks passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
