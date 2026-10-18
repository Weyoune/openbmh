// SPDX-License-Identifier: GPL-3.0-only
#include "app/career_session.hpp"
#include "content/save.hpp"
#include "launch/mods.hpp"
#include "launch/game_data.hpp"
#include "ui/setup.hpp"
#include "ui/sportschau.hpp"
#include "game/save.hpp"
#include <chrono>
#include <iostream>
using namespace bmh;
namespace fs = std::filesystem;
void check(bool value, const std::string &text) {
    if (!value)
        throw std::runtime_error(text);
}
int main(int argc, char **argv) try {
    if (argc < 2)
        return 77;
    const auto root = fs::temp_directory_path() /
                      ("openbmh-native-career-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    struct Cleanup {
        fs::path p;
        ~Cleanup() {
            std::error_code ec;
            fs::remove_all(p, ec);
        }
    } cleanup{root};
    launch::ModLibrary library(root);
    launch::AddMod request;
    request.source = argv[1];
    request.name = "Test Classic";
    request.migrate = false;
    const auto result = library.add(request, {});
    const auto package = result.mod.directory / "content.obw";
    auto content = content::open(package);
    content::Mount mount(package, content);
    // Exercise renderers through aliases while the mod contains only content.obw.
    check(ui::SetupScreen(mount.root()).render().pixels.size() == 320 * 240, "OBW-only setup renderer");
    (void)ui::SportschauIntro(mount.root()).render_frame(0);
    const auto resolved = launch::resolve_game(request.source, root / "legacy-test-cache");
    check(resolved.error.empty(), resolved.error);
    app::CareerSession old(resolved.check.directory, 12345);
    app::CareerSession session(mount.root(), 12345);
    check(save_stream(old.state()) == save_stream(session.state()) &&
              old.rand().seed() == session.rand().seed() && old.game().seed() == session.game().seed(),
          "OBW bootstrap matches legacy state and RNG");
    ui::SetupOptions options;
    options.managers = 1;
    ui::ManagerChoice manager;
    manager.slot = 0;
    manager.master_club = 0;
    manager.start_league = 0;
    manager.strength = 1;
    manager.portrait = 0;
    manager.name = "Native QA";
    std::string error;
    check(session.confirm_manager(options, manager, error), error);
    const auto personal = library.user_directory(result.mod);
    session.configure_autosave_directory(personal);
    session.state().memory.set8(layout::Desk_EnterDesk_RuntimeField, 1);
    ui::CareerInitializationResult initial;
    check(session.finish(initial, error), error);
    const auto before = cli::dump_json(encode_native_state(session.state()));
    const auto random = session.rand();
    const auto game = session.game();
    const auto save = library.user_directory(result.mod) / "QA.obs";
    check(session.save_game(save, 0, false, error), error);
    check(session.rand().seed() == random.seed() && session.rand().calls() == random.calls() &&
              session.game().seed() == game.seed() && session.game().calls() == game.calls(),
          "native saving is RNG-neutral");
    app::CareerSession loaded(mount.root(), 999, save);
    check(cli::dump_json(encode_native_state(loaded.state())) == before, "native career state round-trip");
    check(loaded.rand().seed() == random.seed() && loaded.rand().calls() == random.calls() &&
              loaded.game().seed() == game.seed() && loaded.game().calls() == game.calls(),
          "both RNG states and counters restored");
    check(cli::dump_json(content::driver_checkpoint(session.manager_join_checkpoint().driver)) ==
              cli::dump_json(content::driver_checkpoint(loaded.manager_join_checkpoint().driver)),
          "native turn cursor round-trip");
    session.state().memory.set8(layout::Desk_EnterDesk_RuntimeField, 1);
    loaded.state().memory.set8(layout::Desk_EnterDesk_RuntimeField, 1);
    session.state().autosave_month = 0;
    loaded.state().autosave_month = 0;
    uint64_t first_revision = 1, second_revision = 1;
    for (int step = 0; step < 8; ++step) {
        const auto first_desk = session.desktop_projection(first_revision),
                   second_desk = loaded.desktop_projection(second_revision);
        ui::DeskRequest first_request;
        first_request.kind = ui::DeskRequestKind::advance_turn;
        first_request.manager_slot = first_desk.manager_slot;
        first_request.revision = first_revision;
        auto second_request = first_request;
        second_request.manager_slot = second_desk.manager_slot;
        second_request.revision = second_revision;
        std::string first_error, second_error;
        const auto first = session.dispatch_kickoff(first_request, first_revision, first_error);
        const auto second = loaded.dispatch_kickoff(second_request, second_revision, second_error);
        check(first.kind == second.kind && first_error == second_error,
              "native continuation has identical outcome");
        check(cli::dump_json(encode_native_state(session.state())) ==
                  cli::dump_json(encode_native_state(loaded.state())),
              "native continuation preserves every game-state field");
        check(session.rand().seed() == loaded.rand().seed() &&
                  session.rand().calls() == loaded.rand().calls() &&
                  session.game().seed() == loaded.game().seed() &&
                  session.game().calls() == loaded.game().calls(),
              "native continuation preserves both random streams");
        if (first.kind == TurnDeskKind::Failed || first.kind == TurnDeskKind::WaitingAtDesk)
            break;
        if (step == 0) {
            const auto autosave_error = session.take_autosave_error();
            check(fs::exists(personal / "AUTOSAVE.obs"), "native autosave at stable desk: " + autosave_error);
            app::CareerSession autosaved(mount.root(), 0, personal / "AUTOSAVE.obs");
            check(cli::dump_json(encode_native_state(autosaved.state())) ==
                      cli::dump_json(encode_native_state(session.state())),
                  "autosave matches completed desk, including runtime flags");
        }
        check(session.save_game(save, first.manager_slot, true, error), error);
        auto fresh = std::make_unique<app::CareerSession>(mount.root(), 0, save);
        check(cli::dump_json(content::driver_checkpoint(session.manager_join_checkpoint().driver)) ==
                  cli::dump_json(content::driver_checkpoint(fresh->manager_join_checkpoint().driver)),
              "prepared match groups and previous results survive saving");
    }
    // Original save migration must not require the original installation afterwards.
    auto old_save = make_savegame(session.state(), session.rand());
    write_savegame(root / "legacy.FBM", old_save);
    app::CareerSession migrated(mount.root(), 4321, root / "legacy.FBM", true);
    check(migrated.save_game(root / "migrated.obs", 0, false, error), error);
    app::CareerSession continued(mount.root(), 1, root / "migrated.obs");
    check(continued.state().players.size() == session.state().players.size(),
          "legacy save migrates to native continuation");
    if (argc > 2) {
        app::CareerSession original(mount.root(), 0x12345678, argv[2], true);
        check(original.save_game(root / "original-import.obs",
                                 original.state().memory.u8(layout::ActiveManager), false, error),
              error);
        app::CareerSession resumed(mount.root(), 0, root / "original-import.obs");
        check(cli::dump_json(encode_native_state(original.state())) ==
                  cli::dump_json(encode_native_state(resumed.state())),
              "original edition save migration round-trip");
    }
    auto changed = content->documents;
    changed["assets/classic/images/kb/29.pixels"][0] ^= 1;
    bool rejected = false;
    try {
        content::verify(changed);
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "modified Classic assets rejected");
    std::cout << "OBW-only GUI/bootstrap, career/RNG round-trip and migration passed\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
