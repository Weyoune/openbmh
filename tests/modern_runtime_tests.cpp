// SPDX-License-Identifier: GPL-3.0-only
#include "content/save.hpp"
#include "formats/native_state.hpp"
#include "launch/mods.hpp"
#include "modern/json.hpp"
#include "modern/model.hpp"
#include <chrono>
#include <iostream>
using namespace bmh;
namespace fs = std::filesystem;
void check(bool condition, const char *message) {
    if (!condition)
        throw FormatError(message);
}
template <class F> void rejects(F action, const char *message) {
    bool failed = false;
    try {
        action();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, message);
}
std::string fingerprint(const app::CareerSession &session) {
    const auto checkpoint = session.manager_join_checkpoint();
    return cli::dump_json(encode_native_state(session.state())) + std::to_string(checkpoint.rand.seed()) +
           ":" + std::to_string(checkpoint.rand.calls()) + ":" + std::to_string(checkpoint.game.seed()) +
           ":" + std::to_string(checkpoint.game.calls());
}
int main(int argc, char **argv) try {
    if (argc < 2)
        return 77;
    const auto root = fs::temp_directory_path() /
                      ("openbmh-modern-test-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    struct Cleanup {
        fs::path root;
        ~Cleanup() {
            std::error_code error;
            fs::remove_all(root, error);
        }
    } cleanup{root};
    launch::ModLibrary library(root);
    launch::AddMod add;
    add.source = argv[1];
    add.name = "Classic";
    add.migrate = false;
    const auto original = library.add(add, {}).mod;
    add.source = original.directory / "content.obw";
    add.name = "Modern";
    add.type = content::ModType::Modern;
    add.from_classic = true;
    const auto variant = library.add(add, {}).mod;
    const auto classic = content::open(original.directory), modern = content::open(variant.directory);
    check(classic->content_id == modern->content_id, "presentation must not fork content identity");
    check(library.user_directory(original) == library.user_directory(variant),
          "presentation variants share native careers");
    const auto mods = library.list();
    check(mods.size() == 2 && mods[1].type == "Modern" && mods[1].error.empty(),
          "Modern survives library rediscovery");
    check(content::mod_type(original.directory) == content::ModType::Classic, "old mods default Classic");
    check(content::mod_type(variant.directory) == content::ModType::Modern, "Modern descriptor persists");
    content::Mount cmount(original.directory / "content.obw", classic),
        mmount(variant.directory / "content.obw", modern);
    modern::Model model(mmount.root(), library.user_directory(variant), modern, 12345);
    auto malformed_metadata = std::make_shared<content::Classic>(*modern);
    auto invalid_manifest = cli::parse_json(malformed_metadata->documents.at("manifest.json"));
    invalid_manifest.object()->at("currency") = "not an object";
    malformed_metadata->documents["manifest.json"] = cli::dump_json(invalid_manifest);
    rejects(
        [&] {
            modern::Model invalid(mmount.root(), library.user_directory(variant), malformed_metadata, 12345);
        },
        "editable currency metadata is validated before displaying a career");
    model.begin_new(0, 1);
    const auto choices = model.clubs();
    check(!choices.empty(), "club choices from imported setup");
    check(model.confirm_manager("Modern QA", choices.front().selection), "new career reaches desk");
    auto before = fingerprint(model.session());
    auto snapshot = model.snapshot();
    check(!snapshot.tables.empty() && !snapshot.tables.front().rows.empty(), "real standings available");
    const auto manager = snapshot.desk.manager_slot;
    const auto &identity = snapshot.desk.managers.at(size_t(manager));
    check(model.image(model.crest(identity.club_identity_key)).has_value(), "logical crest resolves");
    for (const auto &player : snapshot.squad.players)
        if (player.player_id >= 0 && player.player_id != 0xfff) {
            const auto photo = model.image(model.photo(player.player_id));
            check(photo && photo->width > 0 && photo->rgba.size() == size_t(photo->width * photo->height * 4),
                  "frozen portrait resolves to RGBA");
            break;
        }
    check(before == fingerprint(model.session()),
          "read projections and asset rendering preserve state and both RNGs");
    model.prepare();
    snapshot = model.snapshot();
    const auto prepared = fingerprint(model.session());
    model.prepare();
    check(prepared == fingerprint(model.session()), "lineup preparation occurs once");
    const auto a = snapshot.squad.players.at(0), b = snapshot.squad.players.at(1);
    model.swap(snapshot.squad, a.row, b.row);
    check(model.snapshot().squad.players.at(0).player_id == b.player_id,
          "real lineup action swaps player identities");
    const auto after_swap = fingerprint(model.session());
    rejects([&] { model.swap(snapshot.squad, a.row, b.row); }, "stale lineup request rejected");
    check(after_swap == fingerprint(model.session()), "rejected action rolls back state and random streams");
    model.save("Modern QA");
    check(!model.dirty(), "save clears dirty marker");
    check(after_swap == fingerprint(model.session()), "saving does not alter state or RNG");
    const auto career = library.user_directory(variant) / "Modern QA.obs";
    app::CareerSession classic_loaded(cmount.root(), 999, career);
    check(after_swap == fingerprint(classic_loaded),
          "Classic loads Modern-written career with identical world, state and "
          "RNGs");
    std::string error;
    const auto reverse = library.user_directory(original) / "Classic QA.obs";
    check(classic_loaded.save_game(reverse, manager, false, error), error.c_str());
    model.load(reverse);
    check(after_swap == fingerprint(model.session()),
          "Modern loads Classic-written career without conversion");
    const auto missing = library.user_directory(variant) / "missing.obs";
    rejects([&] { model.load(missing); }, "missing career rejected");
    check(after_swap == fingerprint(model.session()), "failed load retains current career");
    model.begin_new(0, 1);
    model.cancel_new();
    check(after_swap == fingerprint(model.session()), "cancelled setup preserves old career");
    rejects([&] { model.save("../outside"); }, "save path traversal rejected");
    // Modern ignores missing/stale importer signatures, but still validates data.
    auto docs = modern->documents;
    docs.erase("x-classic/seal.json");
    docs.erase("x-classic/inventory.json");
    content::write_archive(docs, variant.directory / "content.obw", true);
    const auto editable = content::open(variant.directory);
    check(editable->content_id == classic->content_id, "removing signatures does not change world identity");
    rejects([&] { content::open(variant.directory, content::ModType::Classic); },
            "Classic still requires its seal");
    auto manifest = cli::parse_json(docs.at("manifest.json"));
    manifest.object()->at("start_date") = "1994-07-01";
    docs["manifest.json"] = cli::dump_json(manifest);
    content::write_archive(docs, variant.directory / "content.obw", true);
    const auto changed = content::open(variant.directory);
    check(changed->content_id != classic->content_id, "changed world creates a new identity");
    rejects([&] { content::load_career(career, *changed); },
            "changed world cannot silently attach old career");
    docs["world/clubs.json"] = "{}";
    content::write_archive(docs, variant.directory / "content.obw", true);
    rejects([&] { content::open(variant.directory); }, "Modern rejects structurally invalid content");
    launch::AddMod direct;
    direct.source = argv[1];
    direct.name = "Direct Modern";
    direct.migrate = false;
    direct.type = content::ModType::Modern;
    const auto imported = library.add(direct, {}).mod;
    check(imported.kind == content::ModType::Modern &&
              content::open(imported.directory)->content_id == classic->content_id,
          "direct Modern installation import uses the same world and runtime");
    // New-game manager selection remains staged and follows the remapped roster.
    model.begin_new(0, 2);
    auto first = model.clubs().front();
    check(!model.confirm_manager("First QA", first.selection), "first multiplayer manager remains staged");
    auto second = model.clubs().front();
    check(second.master != first.master, "second manager cannot reuse first club");
    check(model.confirm_manager("Second QA", second.selection), "second manager completes new career");
    check(model.snapshot().desk.managers.size() == 2, "both managers retained");
    std::cout << "Modern import, assets, preparation, shared Classic saves, "
                 "owner/revision checks and editable-world validation passed\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
