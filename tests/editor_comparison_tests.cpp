// SPDX-License-Identifier: GPL-3.0-only
#include "editor/comparison.hpp"
#include "editor/mcp.hpp"
#include "editor/session.hpp"
#include <cmath>
#include <iostream>
#include <limits>

using namespace bmh::editor;
namespace {
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class Fn> void rejects(const char *code, Fn fn) {
    try {
        fn();
    } catch (const Error &error) {
        check(error.code == code, "unexpected rejection");
        return;
    }
    throw std::runtime_error(std::string("accepted: ") + code);
}
Json find(const Json &items, const std::string &id) {
    for (const auto &row : items)
        if (str(row, "id") == id)
            return row;
    throw std::runtime_error("missing fixture row " + id);
}
World fixture() {
    auto world = World::blank("Synthetic comparison");
    world.rows("nations").push_back({{"id", "nation:de"}, {"name", "Testland"}});
    world.rows("competitions") = Json::array({{{"id", "comp:first"},
                                               {"name", {{"de", "Testliga"}, {"en", "Test league"}}},
                                               {"nation", "nation:de"},
                                               {"type", "league"},
                                               {"members", {"club:north", "club:south"}}},
                                              {{"id", "comp:empty"},
                                               {"name", "Empty league"},
                                               {"nation", "nation:de"},
                                               {"type", "league"},
                                               {"members", {"club:empty"}}},
                                              {{"id", "comp:cup"},
                                               {"name", "Not a league"},
                                               {"nation", "nation:de"},
                                               {"type", "cup"},
                                               {"members", {"club:north"}}}});
    for (const std::string id : {"north", "south", "empty"})
        world.rows("clubs").push_back(
            {{"id", "club:" + id}, {"name", id}, {"type", "club"}, {"nation", "nation:de"}});
    // No club.competition: membership-only imported worlds must work too.
    world.rows("rules").push_back(
        {{"id", "rule:bmh-classic@1"},
         {"type", "attribute_set"},
         {"fields", {{"strength", {{"min", 0}, {"max", 99}}}, {"form", {{"min", 0}, {"max", 127}}}}}});
    const Json ratings{90, 70, 50, nullptr, 90, 99};
    const std::vector<std::string> ids{"a", "b", "c", "d", "free", "duplicate"};
    for (size_t i = 0; i < ids.size(); ++i) {
        const auto team = i < 2 || i == 5 ? Json("club:north") : i < 4 ? Json("club:south") : Json();
        world.rows("players").push_back(
            {{"id", "player:" + ids[i]},
             {"name", ids[i]},
             {"team", team},
             {"born", "2000-01-01"},
             {"positions", {"mid"}},
             {"attribute_set", "bmh-classic@1"},
             {"attributes", {{"strength", ratings[i]}, {"form", i ? Json(100) : Json()}}}});
    }
    world.rows("players")[1]["x-simulation"]["squad_role"] = "source-reserve";
    world.rows("players")[5]["x-simulation"]["squad_role"] = "source-only-duplicate";
    auto contract = [&](std::string id, std::string person, std::string team, int64_t salary,
                        std::string start = "2026-07-01", std::string end = "2027-06-30") {
        world.rows("contracts")
            .push_back({{"id", "contract:" + id},
                        {"name", id},
                        {"person", "player:" + person},
                        {"team", "club:" + team},
                        {"salary", salary},
                        {"start", start},
                        {"end", end}});
    };
    contract("a", "a", "north", 9000);
    contract("b", "b", "north", 10000);
    contract("c", "c", "south", 50000);
    contract("c-conflict", "c", "south", 60000);
    contract("d", "d", "south", 0); // Explicit zero is known, not missing.
    contract("duplicate", "duplicate", "north", 99999999);
    contract("a-expired", "a", "north", 1000000, "2025-07-01", "2026-06-30");
    contract("a-future", "a", "north", 2000000, "2026-07-02", "2027-06-30");
    contract("a-old-team", "a", "south", 3000000);
    world.rows("staff").push_back({{"id", "staff:test"}, {"name", "Staff"}});
    world.rows("contracts")
        .push_back({{"id", "contract:staff"},
                    {"name", "Staff contract"},
                    {"person", "staff:test"},
                    {"team", "club:north"},
                    {"salary", 9999999}});
    return world;
}
Json compare(const World &world, Json args = Json::object()) {
    return query_comparison(comparison_data(world, args), args);
}
} // namespace
int main() {
    const auto root = fs::temp_directory_path() / new_id("comparison-tests-");
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
        const auto original_players = world.rows("players"), original_contracts = world.rows("contracts");
        auto players = compare(world, {{"sort", "salary"}, {"descending", true}});
        check(players["total"] == 5 && players["excluded_players"] == 1,
              "all real rows, reserve included, source duplicate excluded");
        check(players["items"][0]["id"] == "player:b" && players["items"][1]["salary"] == 9000,
              "numeric salary sort and active current-team contracts");
        check(players["items"][2]["salary"] == 0 && players["items"][3]["salary"].is_null(),
              "zero before missing salary descending");
        check(find(players["items"], "player:c")["salary_ambiguous"] == 1,
              "conflicting active contracts stay unknown");
        check(players["currency"]["minor_units"] == 2, "integer currency units retained");
        players = compare(world, {{"sort", "salary"}});
        check(players["items"][0]["id"] == "player:d" && players["items"][4]["salary"].is_null(),
              "missing salary last ascending too");
        auto rating = compare(
            world, {{"sort", "attributes.strength"}, {"descending", true}, {"offset", 1}, {"limit", 1}});
        check(rating["items"][0]["id"] == "player:free" && rating["total"] == 5,
              "stable tie by ID and full-result sort before paging");
        check(compare(world, {{"team", "club:north"}})["total"] == 2,
              "team filter excludes source duplicate");
        check(compare(world, {{"nation", "nation:de"}, {"competition", "comp:first"}})["total"] == 4,
              "league and nation follow membership fallback");
        auto teams =
            compare(world, {{"table", "clubs"}, {"sort", "attributes.strength"}, {"descending", true}});
        auto north = find(teams["items"], "club:north"), south = find(teams["items"], "club:south");
        check(north["attributes"]["strength"] == 80 && north["salary_total"] == 19000,
              "club means and player-only payroll");
        check(north["salary_average"] == 9500 && north["salary_complete"] == true,
              "mean wage and coverage complete");
        check(south["attributes"]["strength"] == 50 && south["attribute_counts"]["strength"] == 1,
              "unknown attributes excluded from mean denominator");
        check(south["salary_total"] == 0 && south["salary_known"] == 1 && south["salary_unknown"] == 1 &&
                  south["salary_complete"] == false,
              "partial payroll distinguished from complete zero");
        check(find(teams["items"], "club:empty")["salary_total"].is_null(),
              "empty team is not a zero-payroll team");
        auto leagues = compare(world, {{"table", "leagues"}, {"language", "en"}});
        auto league = find(leagues["items"], "comp:first");
        check(leagues["total"] == 2 && league["club_count"] == 2 && league["player_count"] == 4,
              "cups excluded; exact unique league population");
        check(league["attributes"]["strength"] == 70,
              "league strength is player-weighted, not a mean of club means");
        check(league["salary_total"] == 19000 && league["salary_average"] == 6333 &&
                  league["salary_known"] == 3,
              "league payroll uses known contracts once and integer rounded mean");
        check(league["name"] == "Test league", "localized names");
        check(world.rows("players") == original_players && world.rows("contracts") == original_contracts,
              "comparison never changes source records");
        auto classic = world;
        classic.rows("contracts") = Json::array();
        classic.manifest["currency"] = {{"code", "GBP"}, {"minor_units", 0}};
        check(compare(classic)["items"][0]["salary"].is_null() &&
                  compare(classic)["currency"]["code"] == "GBP",
              "Classic without contracts remains unknown in its actual currency");
        world.rows("rules").push_back(
            {{"id", "rule:fc@1"},
             {"type", "attribute_set"},
             {"attribute_set", "fc@1"},
             {"fields", {{"overall", {{"min", 0}, {"max", 99}}}, {"strength", {{"min", 0}, {"max", 99}}}}}});
        auto &modern = world.rows("players")[1];
        modern["attribute_set"] = "fc@1";
        modern["attributes"] = {{"overall", 85}, {"strength", 35}};
        check(compare(world)["mixed_attribute_sets"] == true && compare(world)["attributes"].empty(),
              "different strength systems are never implicitly combined");
        rejects("invalid_comparison_sort", [&] { compare(world, {{"sort", "attributes.strength"}}); });
        auto fc = compare(world, {{"table", "clubs"}, {"attribute_set", "fc@1"}});
        check(find(fc["items"], "club:north")["attributes"]["overall"] == 85 &&
                  find(fc["items"], "club:north")["player_count"] == 1,
              "attribute system filter also defines aggregation population");
        rejects("invalid_attribute_set", [&] { compare(world, {{"attribute_set", "unknown@1"}}); });
        rejects("unknown_table", [&] { compare(world, {{"table", "staff"}}); });
        auto many = fixture();
        many.rows("players") = Json::array();
        many.rows("contracts") = Json::array();
        for (int i = 0; i < 231; ++i)
            many.rows("players").push_back({{"id", "player:" + std::to_string(i)},
                                            {"name", std::to_string(i)},
                                            {"attribute_set", "synthetic@1"},
                                            {"attributes", {{"rating", i}}}});
        const auto second_page =
            compare(many, {{"sort", "attributes.rating"}, {"descending", true}, {"offset", 200}});
        check(second_page["total"] == 231 && second_page["items"].size() == 31 &&
                  second_page["items"][0]["attributes"]["rating"] == 30,
              "sorting spans all pages, no 200-row truncation");
        auto overflow = fixture();
        overflow.rows("contracts")[0]["salary"] = std::numeric_limits<int64_t>::max();
        const auto extreme = find(compare(overflow, {{"table", "clubs"}})["items"], "club:north");
        check(extreme["salary_overflow"] == true && extreme["salary_total"].is_null(),
              "payroll overflow never wraps into a plausible negative value");
        auto invalid = fixture();
        invalid.rows("contracts")[0]["start"] = 20260701;
        check(find(compare(invalid)["items"], "player:a")["salary"].is_null(),
              "malformed contract dates are not silently treated as open bounds");
        invalid.rows("contracts")[1]["salary"] = -1;
        check(find(compare(invalid)["items"], "player:b")["salary"].is_null(),
              "invalid salary is unknown, not a negative payroll credit");
        invalid.manifest["start_date"] = nullptr;
        check(find(compare(invalid)["items"], "player:d")["salary"].is_null(),
              "unknown world date does not substitute the wall clock");
        invalid.manifest["x-simulation"] = nullptr;
        check(compare(invalid)["fictional_completion"] == false,
              "unrecognized optional extension metadata does not break comparisons");
        auto saved = fixture();
        saved.save(root / "fixture.obw");
        Session session;
        session.execute("open", {{"path", utf8(root / "fixture.obw")}});
        const auto info = session.execute("info");
        const auto projection = session.execute("compare", {{"table", "clubs"}});
        check(info == session.execute("info") && info["dirty"] == false,
              "session comparison is read-only including revision and undo state");
        session.execute("apply", {{"changes", Json::array({{{"table", "players"},
                                                            {"id", "player:a"},
                                                            {"set", {{"/attributes/strength", 80}}}}})}});
        check(find(session.execute("compare", {{"table", "clubs"}})["items"],
                   "club:north")["attributes"]["strength"] == 75,
              "cached comparison refreshes after an edit");
        session.execute("undo");
        check(session.execute("compare", {{"table", "clubs"}})["items"] == projection["items"],
              "cached comparison refreshes after undo");
        McpServer server(session);
        const auto reply =
            server.dispatch({{"jsonrpc", "2.0"},
                             {"id", 1},
                             {"method", "tools/call"},
                             {"params",
                              {{"name", "editor_compare"},
                               {"arguments", {{"table", "leagues"}, {"sort", "salary_total"}}}}}});
        check(!reply["result"].value("isError", false) && reply["result"]["structuredContent"]["total"] == 2,
              "MCP uses the same comparison projection");
        bool read_only = false;
        for (const auto &tool : McpServer::tools())
            if (tool["name"] == "editor_compare")
                read_only = tool["annotations"]["readOnlyHint"] == true;
        check(read_only, "MCP comparison advertises read-only semantics");
        std::cout << checks << " editor comparison checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
