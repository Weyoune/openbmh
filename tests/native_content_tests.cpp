// SPDX-License-Identifier: GPL-3.0-only
#include "content/package.hpp"
#include "content/save.hpp"
#include "launch/mods.hpp"
#include "launch/process.hpp"
#include "launch/archive.hpp"
#include "formats/content.hpp"
#include "formats/native_state.hpp"
#include "formats/vga.hpp"
#include <chrono>
#include <iostream>
using namespace bmh;
namespace fs = std::filesystem;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f, const char *message) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, message);
}
int main(int argc, char **argv) try {
    const auto arguments = launch::utf8_arguments(argc, argv);
    if (arguments.size() > 1 && arguments[1] == "--child-check") {
        return arguments == std::vector<std::string>{arguments[0], "--child-check", "Grüße with spaces",
                                                     "quote\" and trailing\\", ""}
                   ? 42
                   : 43;
    }
    const auto executable = launch::executable_path("not-the-executable");
    check(fs::is_regular_file(executable),
          "portable root uses the actual process path rather than argv or current directory");
    check(launch::run_child(executable, {"--child-check", "Grüße with spaces", "quote\" and trailing\\", ""},
                            {}) == 42,
          "child startup preserves Unicode, spaces, quotes, trailing slashes and empty arguments");
    check(content::sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "SHA256 empty vector");
    check(content::sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA256 short vector");
    check(content::sha256(std::string(1000000, 'a')) ==
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "SHA256 multi-block vector");
    const auto root = fs::temp_directory_path() /
                      ("openbmh-native-test-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    struct Cleanup {
        fs::path p;
        ~Cleanup() {
            std::error_code ec;
            fs::remove_all(p, ec);
        }
    } cleanup{root};
    auto classic = std::make_shared<content::Classic>();
    classic->images.emplace("kb/7", Image(2, 2, 19));
    classic->audio["2000"] = {1, 2, 3};
    classic->desktops = {4, 5};
    classic->palette.argb[9] = 0xffabcdef;
    {
        content::Mount mount(root / "content.obw", classic);
        check(!fs::exists(mount.root()), "mounted contents need no materialized legacy files");
        check(load_vga(mount.root() / "KB/7.VGA").pixels == Bytes(4, 19),
              "indexed OBW image reaches existing renderer");
        check(read_file(mount.root() / "S/2000") == Bytes({1, 2, 3}),
              "audio bank comes from immutable store");
        check(read_file(mount.root() / "M3.MEN") == Bytes({4, 5}),
              "startup input comes from immutable store");
        check(load_palette(mount.root() / "M3PC.COL").argb[9] == 0xffabcdef,
              "palette keeps exact imported RGB8");
        rejects([&] { load_vga(mount.root() / "KB/999.VGA"); },
                "missing content must not fall through to disk");
        rejects([&] { content::Mount duplicate(mount.root(), classic); },
                "duplicate mounts must not replace live contents");
    }
    check(!content::find(root / "content.obw/KB/7.VGA"), "mount lifetime is bounded");
    GameState state;
    state.date = 99;
    state.season_start = 12;
    state.display_start_year = 1994;
    state.squad_slots = {0x8fff, 42};
    state.manager_teams = {-1, 12};
    state.manager_records = {1, 2, 3};
    state.player_names = {0, 0xff};
    state.memory.add_block(0x1000, 4);
    state.memory.set32(0x1000, 0xff123456);
    state.players.push_back({std::string("A\xff", 2), 0xdeadbeef, 0x12345678, 0xffffffff});
    state.autosave_month = 4;
    state.list_toggle = 1;
    state.sportschau_homes[9] = 41;
    state.reality.initialized = true;
    state.reality.grid.place(0, 5, 7, 0);
    state.reality.cue_actors[3] = 19;
    const auto encoded = encode_native_state(state);
    const auto restored = decode_native_state(cli::parse_json(cli::dump_json(encoded)));
    check(cli::dump_json(encoded) == cli::dump_json(encode_native_state(restored)),
          "all native fields round-trip without host padding/pointers");
    check(restored.reality == state.reality, "Reality residues survive exactly");
    auto damaged = encoded;
    damaged.object()->at("schema") = "classic-state@99.0";
    rejects([&] { decode_native_state(damaged); }, "unsupported checkpoint schema rejected");
    TurnDriver driver;
    driver.referees = {1, 0x81};
    driver.next_event_id = UINT64_MAX;
    TurnDriver copy;
    content::restore_driver(copy, content::driver_checkpoint(driver));
    check(copy.next_event_id == UINT64_MAX && copy.referees == driver.referees,
          "driver counters and referees round-trip");
    WatcomRandom random(7);
    random.rand();
    WatcomRandom continued;
    continued.restore(random.seed(), random.calls());
    check(continued.rand() == random.rand() && continued.calls() == random.calls(),
          "Watcom stream resumes including counter");
    content::Documents docs{{"manifest.json", "{}"}, {"assets/test.pixels", std::string("a\0b", 3)}};
    const auto archive = root / "sample.obs";
    content::write_archive(docs, archive);
    check(content::read_archive(archive) == docs, "binary native archive round-trip");
    {
        content::Mount mount(archive, std::make_shared<content::Classic>());
        check(content::read_archive(archive) == docs,
              "archive reads bypass the mounted virtual resource namespace");
    }
    rejects([&] { content::write_archive({{"../outside", "x"}}, root / "unsafe.obs"); },
            "archive traversal rejected");
    rejects([&] { content::write_archive({}, archive); }, "existing saves require explicit overwrite");
    check(content::read_archive(archive) == docs, "rejected overwrite retains existing save");
    content::write_archive({{"new", "value"}}, archive, true);
    check(content::read_archive(archive).contains("new"), "atomic replacement publishes complete archive");
    content::save_preferences(root / "test.obsettings", "settings", Bytes(180, 0x82), false);
    check(content::load_preferences(root / "test.obsettings", "settings", 180) == Bytes(180, 0x82),
          "native settings retain availability bits");
    rejects([&] { content::load_preferences(root / "test.obsettings", "layout", 180); },
            "preferences kinds cannot be confused");
    launch::ModLibrary library(root);
    check(library.list().empty(), "first launch has no implicit installation");
    for (const char *name : {"../escape", "CON", "lpt1.txt", "trailing.", "bad/name", ""})
        rejects([&] { launch::ModLibrary::validate_name(name); }, "portable mod name validation");
    fs::create_directories(root / "mods/BMH-classic");
    auto entries = library.list();
    check(entries.size() == 1 && !entries[0].error.empty(), "damaged mods remain visible with error");
    rejects([&] { library.rename(entries[0], "bmh-CLASSIC"); }, "case-insensitive duplicate name rejected");
    library.rename(entries[0], "Renamed");
    entries = library.list();
    check(entries[0].name == "Renamed", "rename changes directory name");
    entries[0].content_id = std::string(64, 'a');
    const auto personal = library.user_directory(entries[0]);
    content::write_atomic(personal / "keep.obs", "saved", false);
    library.remove(entries[0]);
    check(library.list().empty() && fs::exists(personal / "keep.obs"), "delete retains personal saves");
    std::cout << "native content, checkpoints and portable mod lifecycle passed\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
