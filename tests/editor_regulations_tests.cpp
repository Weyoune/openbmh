// SPDX-License-Identifier: GPL-3.0-only
#include "editor/mcp.hpp"
#include "editor/regulations.hpp"
#include "editor/session.hpp"
#include <iostream>

using namespace bmh::editor;
namespace {
int checks = 0;
void check(bool result, const char *message) {
    ++checks;
    if (!result)
        throw std::runtime_error(message);
}
Json find(const Json &items, const std::string &id) {
    for (const auto &row : items)
        if (str(row, "id") == id)
            return row;
    throw std::runtime_error("missing fixture " + id);
}
Json view(const World &world, const std::string &id) {
    return find(regulation_view(world, Json::object())["items"], id);
}
World fixture() {
    auto world = World::blank("Synthetic regulations");
    world.rows("nations").push_back({{"id", "nation:test"}, {"name", "Testland"}});
    for (int i = 0; i < 9; ++i)
        world.rows("competitions")
            .push_back(
                {{"id", "comp:league-" + std::to_string(i)},
                 {"name", {{"default", "League " + std::to_string(i)}, {"de", "Liga " + std::to_string(i)}}},
                 {"nation", "nation:test"},
                 {"type", "league"},
                 {"tier", i + 1},
                 {"stages",
                  Json::array({{{"id", "regular"}, {"format", "league"}, {"teams", 18}, {"legs", 2}}})}});
    world.rows("competitions")
        .push_back({{"id", "comp:cup"}, {"name", "Test Cup"}, {"type", "club_international"}});
    return world;
}
} // namespace
int main() {
    const auto root = fs::temp_directory_path() / new_id("regulations-tests-");
    fs::create_directory(root);
    struct Cleanup {
        fs::path root;
        ~Cleanup() {
            std::error_code ec;
            fs::remove_all(root, ec);
        }
    } cleanup{root};
    try {
        auto world = fixture();
        auto &league = world.rows("competitions")[1];
        league["movement"] = Json::array({{{"ranks", {1, 2}}, {"to", "comp:league-0"}},
                                          {{"ranks", {3}}, {"to", "comp:playoff"}, {"role", "challenger"}},
                                          {{"ranks", {16, 18}}, {"to", "comp:league-2"}}});
        const Json selector{{"from", "comp:league-1"},
                            {"ranks", {1, 4}},
                            {"stage", "main"},
                            {"fallback", {{"from", "comp:league-0"}, {"next_unqualified", true}}}};
        world.rows("competitions").back()["slots"] =
            Json::array({selector,
                         {{"holder", "comp:cup"}},
                         {{"from", "coefficient"}, {"nation_rank", {1, 4}}, {"places", 4}}});
        const Json cup_winner{{"from", "comp:league-0"}, {"winner", true}, {"to", "comp:cup"}};
        world.rows("competitions")[0]["stages"][0]["slots"] = Json::array({cup_winner});
        auto row = view(world, "comp:league-1");
        check(row["movement"][0]["kind"] == "promotion" && row["movement"][0]["ranks"] == Json({1, 2}),
              "rank ranges preserved");
        check(row["movement"][1]["kind"] == "playoff" && row["movement"][2]["kind"] == "relegation",
              "explicit movement kinds");
        check(row["qualification"][0]["definition"] == selector && row["qualification"][0]["stage"] == "main",
              "outbound qualification retains fallback and stage");
        const auto cup = view(world, "comp:cup");
        check(cup["qualification"].size() == 4 && cup["qualification"][3]["definition"] == cup_winner,
              "competition and stage slots; explicit destination");
        check(!view(world, "comp:league-2")["movement_defined"].get<bool>() &&
                  view(world, "comp:league-2")["classic_qualification"].is_null(),
              "no invented modern rules");
        const auto filtered =
            regulation_view(world, {{"language", "de"}, {"nation", "nation:test"}, {"search", "Liga 1"}});
        check(filtered["items"].size() == 1 && filtered["items"][0]["name"] == "Liga 1",
              "localized search and country filter");
        const auto classic = fixture();
        world = classic;
        const int down[] = {3, 4, 2, 2, 3, 0, 0, 0, 0};
        for (int i = 0; i < 9; ++i)
            world.rows("competitions")[i]["x-legacy"] = {
                {"league_index", i}, {"relegation_places", down[i]}, {"europe_places", 99}};
        world.rows("rules").push_back({{"id", "rule:ladder"},
                                       {"type", "legacy_ladder"},
                                       {"first_league", {0, 1, 2, 4}},
                                       {"league_count", {1, 1, 2, 1}},
                                       {"below", {1, 2, 4, 5}},
                                       {"below_count", {1, 2, 1, 1}},
                                       {"newcomer_count", 30}});
        world.rows("rules").push_back({{"id", "rule:runtime"},
                                       {"type", "classic_runtime"},
                                       {"qualifications", "classic-season-entrants@1"}});
        auto first = view(world, "comp:league-0"), second = view(world, "comp:league-1");
        check(first["movement"][0]["ranks"] == Json({16, 18}), "classic relegation reads loaded counts");
        check(first["classic_qualification"]["uefa_base_places"] == 4 &&
                  view(world, "comp:league-8")["classic_qualification"]["uefa_base_places"] == 3,
              "classic quotas use named policy, not europe_places");
        check(second["movement"][0]["kind"] == "promotion" && second["movement"][1]["targets"].size() == 2 &&
                  second["movement"][1]["conditional_playoffs"] == true,
              "parallel lower leagues and conditional playoff exchange");
        auto third = view(world, "comp:league-2");
        check(third["movement"][0]["ranks"] == Json({1, 1}) &&
                  third["movement"][1]["selector"] == "classic-playoff-winner",
              "parallel league champion plus playoff; no invented automatic runner-up");
        auto bottom = view(world, "comp:league-4");
        check(bottom["movement"].back()["newcomer_pool"] == true &&
                  bottom["movement"].back()["targets"].empty(),
              "bottom uses newcomer pool, never the foreign below index");
        check(view(world, "comp:league-5")["movement_defined"] == true &&
                  view(world, "comp:league-5")["movement"].empty(),
              "classic foreign league closed");
        world.rows("competitions")[0]["movement"] = Json::array();
        world.rows("competitions")[0]["slots"] =
            Json::array({{{"from", "comp:league-0"}, {"to", "comp:cup"}, {"ranks", {1, 2}}}});
        check(view(world, "comp:league-0")["movement"].empty() &&
                  view(world, "comp:league-0")["classic_qualification"].is_null(),
              "declared rules take precedence over Classic summaries");
        world.rows("competitions")[8]["x-legacy"] = nullptr;
        world.rows("competitions")[8]["classic"] = nullptr;
        world.rows("competitions")[8]["stages"] = nullptr;
        check(view(world, "comp:league-8")["classic_qualification"].is_null(), "null optional metadata safe");
        // Only synthetic data enters public archives or tests.
        world = fixture();
        const auto records = world.rows("competitions"), rules = world.rows("rules");
        regulation_view(world, Json::object());
        check(records == world.rows("competitions") && rules == world.rows("rules"),
              "projection leaves documents untouched");
        world.save(root / "fixture.obw");
        Session session;
        session.execute("open", {{"path", utf8(root / "fixture.obw")}});
        const auto info = session.execute("info"), history = session.execute("history");
        session.execute("regulations");
        check(info == session.execute("info") && history == session.execute("history"),
              "session read-only including revision and undo");
        McpServer server(session);
        const auto reply =
            server.dispatch({{"jsonrpc", "2.0"},
                             {"id", 1},
                             {"method", "tools/call"},
                             {"params",
                              {{"name", "editor_regulations"},
                               {"arguments", {{"competition", "comp:league-1"}, {"language", "en"}}}}}});
        check(reply["result"]["structuredContent"]["items"].size() == 1 &&
                  reply["result"]["structuredContent"]["read_only"] == true,
              "MCP shares projection");
        bool readonly = false;
        for (const auto &tool : McpServer::tools())
            if (tool["name"] == "editor_regulations")
                readonly = tool["annotations"]["readOnlyHint"] == true;
        check(readonly, "MCP read-only annotation");
        std::cout << checks << " editor regulations checks passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
