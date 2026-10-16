#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "app/career_session.hpp"
#include "game/desk.hpp"
#include "game/save.hpp"
#include "game/week.hpp"
#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "ui/settings.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace bmh;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

void period_one(app::CareerSession& session, const std::filesystem::path& data, uint64_t& revision) {
    const auto exe = bmh::test::load_executable(data / "BMH.exe");
    while (session.state().memory.u8(::bmh::layout::Desk_EnterDesk_RuntimeField) > 1) {
        auto state = ui::settings_from_game(session.state(), 0, revision, exe);
        ui::SettingsScreen screen(data, state);
        check(screen.click(252, 130) && screen.pending(), "real K8 decrement request");
        std::string error;
        check(session.dispatch_settings(*screen.pending(), revision, error), "real K8 writer");
    }
    check(session.state().memory.u8(::bmh::layout::Desk_EnterDesk_RuntimeField) == 1, "period one through normal settings");
}

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("autosave_tests data genuine-save");
        const std::filesystem::path data(argv[1]), input(argv[2]);
        auto saved = load_savegame(input);
        check(saved.checksum_ok, "genuine input checksum");
        auto base = GameState::from_save(SaveState(saved.stream));
        base.autosave_month = month_code(base.date);
        base.autosave_months = base.memory.u8(::bmh::layout::Desk_EnterDesk_RuntimeField);
        check(base.autosave_months > 0, "genuine autosave period");
        auto plain = base, observed = base;
        WatcomRandom plain_w(8128), observed_w(8128);
        GameRandom plain_g(123), observed_g(123);
        int calls = 0;
        std::optional<Savegame> snapshot;
        enter_desk(plain, 0, plain_g, plain_w);
        enter_desk(observed, 0, observed_g, observed_w,
            [&](const GameState& state, WatcomRandom& random) {
                ++calls;
                check(state.autosave_months == base.autosave_months,
                      "Source snapshot precedes autosave counter reset");
                check(state.manager_records[0x211] == base.manager_records[0x211],
                      "Source snapshot precedes prestige update");
                const auto before = random.calls();
                snapshot = make_savegame(state, random);
                check(random.calls() == before + 4, "exactly four Source Save draws");
            });
        check(calls == 1 && snapshot && snapshot->checksum_ok, "one real Source snapshot");
        check(observed.autosave_months == 0 && plain.autosave_months == 0, "Source counter reset");
        check(save_stream(plain) == save_stream(observed) && plain_w.seed() == observed_w.seed() &&
              plain_w.calls() == observed_w.calls() && plain_g.seed() == observed_g.seed() &&
              plain_g.calls() == observed_g.calls(), "successful writer preserves full Native and W/G");
        enter_desk(observed, 0, observed_g, observed_w,
            [&](const GameState&, WatcomRandom&) { ++calls; });
        check(calls == 1, "same month and reset counter cannot duplicate autosave");

        const auto temporary = std::filesystem::temp_directory_path() /
            ("openbmh-autosave-" + std::to_string(reinterpret_cast<uintptr_t>(&base)));
        const auto directory = temporary / std::filesystem::path(u8"Gr\u00fc\u00dfe \u4fdd\u5b58 Saison");
        std::filesystem::create_directories(directory);
        struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code e; std::filesystem::remove_all(path,e); } } cleanup{temporary};
        app::CareerSession manual(data, 8128, input);
        const auto manual_stream = save_stream(manual.state());
        const auto manual_path = directory / "MANUAL.FBM";
        std::string manual_error;
        check(manual.save_game(manual_path, manual.state().memory.u8(::bmh::layout::ActiveManager), false, manual_error),
              "normal save in Unicode directory with spaces");
        const auto manual_saved = load_savegame(manual_path);
        check(manual_saved.checksum_ok && manual_saved.stream == manual_stream &&
              read_file(manual_path) == savegame_bytes(manual_saved),
              "Unicode-path save reload preserves every Native stream and FBM byte");
        app::CareerSession manual_reloaded(data, 8128, manual_path);
        check(manual_reloaded.state().date == manual.state().date &&
              manual_reloaded.state().manager_teams == manual.state().manager_teams,
              "normal CareerSession reload accepts the Unicode save path");
        app::CareerSession baseline(data, 8128, input), actual(data, 8128, input);
        check(actual.state().autosave_months == 0 &&
              actual.state().autosave_month == month_code(actual.state().date),
              "format field resets loaded autosave counter and remembers current month");
        uint64_t br = 1, ar = 1;
        period_one(baseline, data, br); period_one(actual, data, ar);
        actual.configure_autosave_directory(directory);
        ui::DeskRequest request; request.kind = ui::DeskRequestKind::advance_turn;
        request.manager_slot = 0; request.revision = br;
        std::string error;
        int steps = 0;
        const int loaded_month = month_code(actual.state().date);
        while (!std::filesystem::exists(directory / "AUTOSAVE.FBM") && steps < 12) {
            request.revision = br;
            auto b = baseline.dispatch_kickoff(request, br, error);
            request.revision = ar;
            auto a = actual.dispatch_kickoff(request, ar, error);
            if (a.kind != b.kind || a.kind != TurnDeskKind::NextDesk)
                throw std::runtime_error("autosave month traversal: " + error);
            ++steps;
            check(save_stream(actual.state()) == save_stream(baseline.state()) &&
                  actual.rand().seed() == baseline.rand().seed() && actual.rand().calls() == baseline.rand().calls() &&
                  actual.game().seed() == baseline.game().seed() && actual.game().calls() == baseline.game().calls(),
                  "whole Native/W/G equality at every intervening desk");
            if (month_code(actual.state().date) == loaded_month)
                check(!std::filesystem::exists(directory / "AUTOSAVE.FBM"), "loaded same month does not trigger early autosave");
        }
        check(steps > 1 && month_code(actual.state().date) != loaded_month,
              "autosave first becomes due after a genuine month change");
        check(std::filesystem::is_regular_file(directory / "AUTOSAVE.FBM"), "actual session writes Source filename");
        check(load_savegame(directory / "AUTOSAVE.FBM").checksum_ok, "actual autosave reload checksum");
        check(save_stream(actual.state()) == save_stream(baseline.state()) &&
              actual.rand().seed() == baseline.rand().seed() && actual.rand().calls() == baseline.rand().calls() &&
              actual.game().seed() == baseline.game().seed() && actual.game().calls() == baseline.game().calls(),
              "actual file writer has no extra Native or RNG effects");
        check(actual.take_autosave_error().empty(), "successful write has no failure latch");
        app::CareerSession failed(data, 8128, input);
        uint64_t fr = 1; period_one(failed, data, fr);
        failed.configure_autosave_directory(directory / "missing");
        TurnAdvanceResult f;
        for (int i = 0; i < steps; ++i) {
            request.revision = fr;
            f = failed.dispatch_kickoff(request, fr, error);
            check(f.kind == TurnDeskKind::NextDesk, "failed writer's actual month traversal");
        }
        check(!failed.take_autosave_error().empty(), "failed open reports failure without stopping play");
        check(failed.take_autosave_error().empty(), "failure consumed once");
        check(save_stream(failed.state()) == save_stream(baseline.state()) &&
              failed.rand().calls() + 4 == baseline.rand().calls(), "failed open draws no Save randomness");
        std::cout << "autosave_tests: Unicode normal save/reload, Source boundary, four draws, duplicate guard, real writer and failed-open behavior passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
