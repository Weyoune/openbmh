// SPDX-License-Identifier: GPL-3.0-only
#include "editor/images.hpp"
#include "editor/mcp.hpp"
#include "editor/session.hpp"
#include <httplib.h>
#include <cstdlib>
#include <iostream>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

using namespace bmh::editor;
namespace {
int checks = 0;
void check(bool result, const char *message) {
    ++checks;
    if (!result)
        throw std::runtime_error(message);
}
template <class Fn> void rejects(const char *code, Fn fn) {
    try {
        fn();
    } catch (const Error &error) {
        check(error.code == code, "unexpected rejection code");
        return;
    }
    throw std::runtime_error(std::string("accepted: ") + code);
}
Json update(const char *table, const char *id, Json set) {
    return {{"changes", Json::array({{{"table", table}, {"id", id}, {"set", set}}})}};
}
Json row(Session &session, const char *table, const char *id) {
    return session.execute("get", {{"table", table}, {"id", id}})["record"];
}
} // namespace
int main() {
    auto root = fs::temp_directory_path() / new_id("editor-test-");
    fs::create_directory(root);
    struct Cleanup {
        fs::path root;
        ~Cleanup() {
            std::error_code ec;
            fs::remove_all(root, ec);
        }
    } cleanup{root};
    try {
        Session s;
        s.execute("demo");
        const auto generation = s.execute("info")["world_generation"];
        check(s.execute("list", {{"table", "players"}})["total"] == 52, "all squads retained");
        check(s.execute("list", {{"table", "players"}, {"team", "club:north"}, {"limit", 24}})["items"].size() == 24,
              "24 view");
        check(s.execute("list", {{"table", "players"}, {"team", "club:north"}, {"offset", 24}})["items"].size() == 2,
              "remaining squad not discarded");
        auto initial = row(s, "players", "player:demo-0");
        const auto revision = s.execute("info")["revision"];
        auto change = update("players", "player:demo-0", {{"/name/last", "Änderung"}, {"/attributes/strength", 99}});
        change["expected_revision"] = revision;
        s.execute("apply", change);
        check(row(s, "players", "player:demo-0")["name"]["last"] == "Änderung", "edit applied");
        rejects("stale_revision", [&] { s.execute("apply", change); });
        s.execute("undo");
        check(row(s, "players", "player:demo-0") == initial, "undo complete row");
        check(s.execute("info")["revision"] > revision, "revision monotonic across undo");
        check(s.execute("info")["world_generation"] == generation,
              "edits and undo keep navigation world identity");
        s.execute("redo");
        check(row(s, "players", "player:demo-0")["attributes"]["strength"] == 99, "redo");
        rejects("validation_failed",
                [&] { s.execute("apply", update("players", "player:demo-0", {{"/attributes/strength", 999}})); });
        check(row(s, "players", "player:demo-0")["attributes"]["strength"] == 99, "failed edit rolled back");
        rejects("validation_failed",
                [&] { s.execute("apply", update("players", "player:demo-0", {{"/born", "2001-02-29"}})); });
        rejects("record_referenced", [&] {
            s.execute("apply",
                      {{"changes", Json::array({{{"op", "delete"}, {"table", "clubs"}, {"id", "club:north"}}})}});
        });
        check(!row(s, "clubs", "club:north").empty(), "delete rolled back");
        s.execute("roster.swap", {{"first", "player:demo-0"}, {"second", "player:demo-26"}});
        check(row(s, "players", "player:demo-0")["team"] == "club:south", "roster swap first");
        check(row(s, "players", "player:demo-26")["team"] == "club:north", "roster swap second");
        s.execute("undo");
        auto squad =
            s.execute("list", {{"table", "players"}, {"team", "club:north"}, {"sort", "squad_order"}})["items"];
        Json reversed = Json::array();
        for (auto it = squad.rbegin(); it != squad.rend(); ++it)
            reversed.push_back((*it)["id"]);
        s.execute("roster.order", {{"team", "club:north"}, {"players", reversed}});
        check(row(s, "players", "player:demo-25")["squad_order"] == 0, "complete squad reorder");
        s.execute("undo");
        check(row(s, "players", "player:demo-0")["squad_order"] == 0, "squad order undo");
        rejects("invalid_change", [&] {
            s.execute("roster.order", {{"team", "club:north"}, {"players", Json::array({"player:demo-0"})}});
        });
        rejects("validation_failed",
                [&] { s.execute("apply", update("players", "player:demo-0", {{"/team", "nation:de"}})); });
        rejects("immutable_id",
                [&] { s.execute("apply", update("manifest", "manifest", {{"/x-editor/playable", true}})); });
        s.execute("apply",
                  {{"changes",
                    Json::array(
                        {{{"op", "create"}, {"table", "competitions"}, {"id", "comp:second"}, {"name", "Second"}}})}});
        s.execute("apply", update("clubs", "club:north", {{"/competition", "comp:second"}}));
        check(row(s, "competitions", "comp:second")["members"] == Json::array({"club:north"}), "new league membership");
        check(row(s, "competitions", "comp:demo")["members"] == Json::array({"club:south"}),
              "old league membership removed");
        s.execute("undo");
        check(row(s, "competitions", "comp:demo")["members"].size() == 2, "membership undo atomic");
        const auto image = images::Raster{2, 2, {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 128}};
        const auto png = images::encode_png(image);
        write_atomic(root / "input.png", png);
        check(images::decode(png, ".png").pixels == image.pixels, "PNG lossless RGBA");
        // Authored synthetic pixels: a solid teal 17x19 VP8 image and the
        // same four RGBA pixels as above encoded as VP8L, including alpha.
        const std::string webp_lossy(
            "\x52\x49\x46\x46\x4a\x00\x00\x00\x57\x45\x42\x50\x56\x50\x38\x20"
            "\x3e\x00\x00\x00\x90\x03\x00\x9d\x01\x2a\x11\x00\x13\x00\x3e\x31"
            "\x18\x8a\x43\xa2\x21\xa1\x14\x04\x00\x20\x03\x04\xb2\x80\x3b\x00"
            "\x7e\x00\x7e\x00\x00\x49\x95\x7c\x00\x00\xfe\xe8\x38\xbf\xff\xf6"
            "\x96\x42\x15\xd7\xfc\x95\x7f\xff\xee\x9e\x90\x81\xa3\xfe\x4a\x00\x00\x00", 82);
        const std::string webp_lossless(
            "\x52\x49\x46\x46\x30\x00\x00\x00\x57\x45\x42\x50\x56\x50\x38\x4c"
            "\x23\x00\x00\x00\x2f\x01\x40\x00\x10\x1f\x20\x10\x48\xde\x1f\x3a"
            "\x8d\xf9\x17\x10\x14\xfc\x1f\x9d\x80\xa0\xe8\xba\xe5\x22\x66\x0f"
            "\xc2\x0d\x18\x22\xfa\x1f\x02\x00", 56);
        const auto webp = images::decode(webp_lossy, ".webp");
        check(webp.width == 17 && webp.height == 19 && webp.pixels.size() == 17 * 19 * 4 &&
                  std::abs(int(webp.pixels[0]) - 40) <= 4 && std::abs(int(webp.pixels[1]) - 140) <= 4 &&
                  std::abs(int(webp.pixels[2]) - 200) <= 4 && webp.pixels[3] == 255,
              "WebP lossy colour and dimensions");
        check(images::decode(webp_lossless, ".webp").pixels == image.pixels, "WebP lossless RGBA");
        check(images::transform(png, ".png", Json::object()).bytes == png, "identity preserves exact bytes");
        auto flip = images::decode(images::transform(png, ".png", {{"flip_x", true}}).bytes, ".png");
        check(flip.pixels[1] == 255 && flip.pixels[4] == 255, "horizontal mirror pixels");
        auto rotation = images::transform(png, ".png", {{"angle", 90.0}});
        check(rotation.width == 2 && rotation.height == 2, "rotation bounds");
        auto svg = images::silhouette();
        check(images::transform(svg, ".svg", {}).bytes == svg, "identity keeps SVG");
        auto vector =
            images::transform(svg, ".svg", {{"angle", 17.25}, {"flip_y", true}, {"crop", {{"x", .1}, {"width", .8}}}});
        check(vector.extension == ".svg" && vector.bytes.find("17.25") != std::string::npos,
              "arbitrary transform remains SVG");
        rejects("external_svg_resource", [&] {
            images::decode("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" "
                           "height=\"10\"><image href=\"file:///tmp/test.png\"/></svg>",
                           ".svg");
        });
        auto imported = s.execute("asset.import", {{"file", utf8(root / "input.png")},
                                                   {"key", "asset:test"},
                                                   {"target", {{"table", "players"}, {"id", "player:demo-0"}}}});
        check(row(s, "players", "player:demo-0")["photo"] == "asset:test", "image assigned");
        check(s.asset_bytes("asset:test").first == png, "source exact on import");
        rejects("record_referenced", [&] {
            s.execute("apply",
                      {{"changes", Json::array({{{"table", "assets"}, {"id", "asset:test"}, {"op", "delete"}}})}});
        });
        s.execute("apply", update("players", "player:demo-1", {{"/photo", "asset:test"}}));
        s.execute("asset.edit", {{"key", "asset:individual"},
                                 {"source_key", "asset:test"},
                                 {"transform", {{"angle", 18.0}}},
                                 {"target", {{"table", "players"}, {"id", "player:demo-0"}, {"field", "photo"}}}});
        check(row(s, "players", "player:demo-0")["photo"] == "asset:individual", "individual image assigned");
        check(row(s, "players", "player:demo-1")["photo"] == "asset:test" && s.asset_bytes("asset:test").first == png,
              "shared fallback and other person remain unchanged");
        s.execute("undo");
        check(row(s, "players", "player:demo-0")["photo"] == "asset:test", "image assignment undo atomic");
        const auto isolated = s.execute(
            "asset.import", {{"file", utf8(root / "input.png")},
                             {"key", "asset:test"},
                             {"transform", {{"flip_y", true}}},
                             {"target", {{"table", "players"}, {"id", "player:demo-0"}, {"field", "photo"}}}});
        check(isolated["key"] != "asset:test" && s.asset_bytes("asset:test").first == png,
              "targeted import cannot overwrite another entity's shared image");
        s.execute("undo");
        s.execute("asset.edit", {{"key", "asset:test"}, {"transform", {{"angle", 30.5}}}});
        auto metadata = row(s, "assets", "asset:test")["x-editor"];
        check(metadata["transform"]["angle"] == 30.5, "float angle metadata");
        check(s.asset_bytes(metadata["original"].get<std::string>()).first == png, "original retained");
        s.execute("undo");
        check(s.asset_bytes("asset:test").first == png, "image undo restores native");
        s.execute("redo");
        s.execute("save", {{"path", utf8(root / "world.obw")}});
        check(s.execute("info")["dirty"] == false, "save checkpoint");
        s.execute("undo");
        check(s.execute("info")["dirty"] == true, "undo away from save dirty");
        s.execute("redo");
        check(s.execute("info")["dirty"] == false, "redo back to save clean");
        Session reopened;
        reopened.execute("open", {{"path", utf8(root / "world.obw")}});
        check(row(reopened, "assets", "asset:test")["x-editor"]["transform"]["angle"] == 30.5,
              "roundtrip float transform");
        reopened.execute("save"); // Snapshot permits atomic overwrite on every platform.
        auto w = World::open(root / "world.obw");
        const std::string opaque = "{\"future\":1.125,\"ignored\":{\"preserve\":true}}\n";
        w.store->set("x-future/notes.json", std::make_shared<const std::string>(opaque));
        w.save(root / "future.obw");
        auto future = World::open(root / "future.obw");
        future.save(root / "future2.obw");
        check(ArchiveStore::open(root / "future2.obw")->read("x-future/notes.json") == opaque,
              "unknown extension byte preservation");
        const auto unpacked = root / "unpacked";
        fs::create_directory(unpacked);
        auto original_store = ArchiveStore::open(root / "future.obw");
        for (const auto &name : original_store->names()) {
            const auto file = unpacked / path(name);
            fs::create_directories(file.parent_path());
            write_atomic(file, original_store->read(name));
        }
        auto snapshot = ArchiveStore::open(unpacked);
        write_atomic(unpacked / "x-future/notes.json", "external modification");
        check(snapshot->read("x-future/notes.json") == opaque, "unpacked source snapshot stable");
        check(!safe_name("../bad") && !safe_name("/bad") && !safe_name("C:/bad") && safe_name("assets/a.svg"),
              "archive paths");
        rejects("obw_only", [&] { future.save(root / "legacy.dat"); });
        auto options = Options::load(root / "settings.json");
        check(options.token.size() >= 64 && options.mcp_enabled, "local MCP enabled with token");
        options.apply({{"language", "en"}, {"theme", "silver"}, {"mcp_port", 0}});
        options.save();
        check(Options::load(root / "settings.json").language == "en", "persistent language");
#ifdef _WIN32
        {
            wchar_t old_value[32768]{};
            const auto length = GetEnvironmentVariableW(L"APPDATA", old_value, 32768);
            struct Restore {
                std::wstring value;
                bool present;
                ~Restore() { SetEnvironmentVariableW(L"APPDATA", present ? value.c_str() : nullptr); }
            } restore{old_value, length > 0};
            const auto profile = root / L"Profil-ÄÖÜ";
            SetEnvironmentVariableW(L"APPDATA", profile.c_str());
            const auto native_options = Options::load();
            check(native_options.settings_file == profile / "OpenBMH/editor.json", "Unicode Windows profile path");
            check(fs::is_regular_file(native_options.settings_file), "Unicode Windows settings saved");
        }
#endif
        check(tr("de", "clubs") != tr("en", "clubs"), "bilingual labels");
        {
            Session reopened;
            reopened.execute("demo");
            const auto identity = reopened.execute("info")["world_generation"];
            reopened.execute("save", {{"path", utf8(root / "navigation-world.obw")}});
            check(reopened.execute("info")["world_generation"] == identity,
                  "saving keeps navigation world identity");
            reopened.execute("open", {{"path", utf8(root / "navigation-world.obw")}});
            check(reopened.execute("info")["world_generation"] > identity,
                  "reopening identical IDs resets navigation identity");
        }
        McpServer mcp(s);
        mcp.start(options);
        httplib::Client client("127.0.0.1", mcp.port());
        client.set_connection_timeout(5);
        client.set_read_timeout(10);
        httplib::Headers headers{{"Authorization", "Bearer " + options.token},
                                 {"Accept", "application/json, text/event-stream"}};
        auto request = Json{{"jsonrpc", "2.0"},
                            {"id", 1},
                            {"method", "initialize"},
                            {"params",
                             {{"protocolVersion", "2025-11-25"},
                              {"capabilities", Json::object()},
                              {"clientInfo", {{"name", "test"}, {"version", "1"}}}}}};
        auto result = client.Post("/mcp", headers, request.dump(), "application/json");
        check(result && result->status == 200 && Json::parse(result->body)["result"]["protocolVersion"] == "2025-11-25",
              "MCP initialize HTTP");
        request["method"] = "tools/list";
        request["params"] = Json::object();
        result = client.Post("/mcp", headers, request.dump(), "application/json");
        check(result && Json::parse(result->body)["result"]["tools"].size() >= 18, "MCP tool catalog");
        request["method"] = "tools/call";
        request["params"] = {{"name", "editor_roster_move"},
                             {"arguments", {{"player", "player:demo-0"}, {"team", "club:south"}}}};
        result = client.Post("/mcp", headers, request.dump(), "application/json");
        check(result && Json::parse(result->body)["result"]["isError"] == false, "MCP edit");
        check(row(s, "players", "player:demo-0")["team"] == "club:south", "MCP shares GUI session");
        s.execute("undo");
        check(row(s, "players", "player:demo-0")["team"] == "club:north", "shared undo");
        auto no_token = client.Post("/mcp", request.dump(), "application/json");
        check(no_token && no_token->status == 401, "reject unauthenticated MCP");
        headers.emplace("Origin", "https://example.org");
        result = client.Post("/mcp", headers, request.dump(), "application/json");
        check(result && result->status == 403, "reject foreign origin");
        mcp.stop();
        check(!mcp.running(), "server shutdown");
        std::cout << checks << " editor checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Editor check failed after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
