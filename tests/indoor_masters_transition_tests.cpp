// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

#include "app/career_session.hpp"
#include "formats/binary.hpp"
#include "game/cup.hpp"
#include "game/save.hpp"

namespace {
using namespace bmh;
void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            path = std::filesystem::temp_directory_path() /
                ("openbmh-masters-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(path)) return;
        }
        throw std::runtime_error("cannot create an isolated Masters save directory");
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
void advance(app::CareerSession& session, uint64_t& revision) {
    ui::DeskRequest request;
    request.kind = ui::DeskRequestKind::advance_turn;
    request.manager_slot = session.state().memory.u8(::bmh::layout::ActiveManager);
    request.revision = revision;
    std::string error;
    const auto result = session.dispatch_kickoff(request, revision, error);
    check(result.kind == TurnDeskKind::NextDesk,
          "Hall continuation did not reach a real desk: " + error + "; " + result.reason);
}
void same(app::CareerSession& a, app::CareerSession& b) {
    check(save_stream(a.state()) == save_stream(b.state()) &&
          a.game().seed() == b.game().seed() && a.rand().seed() == b.rand().seed() &&
          a.game().calls() == b.game().calls() && a.rand().calls() == b.rand().calls(),
          "two fresh continuations differ in native state or random seeds: G=" +
          std::to_string(a.game().seed()) + "/" + std::to_string(b.game().seed()) + ", W=" +
          std::to_string(a.rand().seed()) + "/" + std::to_string(b.rand().seed()));
}
void qualified_field(const GameState& g) {
    const auto& mem = g.memory;
    std::set<uint16_t> field;
    for (uint32_t i = 0; i < 8; ++i) field.insert(mem.u16(::bmh::layout::Teams + i * 2));
    check(field.size() == 8, "Masters field must contain eight different clubs");
    for (uint32_t i = 0; i < 3; ++i)
        check(field.count(mem.u16(::bmh::layout::Cup_AdvanceCup_RuntimeField6 + i * 2)),
              "previous domestic-cup/league/Masters holder is missing from the Masters");
    for (int entry = 25; entry < 30; ++entry)
        check(mem.u8(cup_entry_address(entry) + 0x1d) == 0,
              "Masters draw did not reset all five Hall stage cursors");
}
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    if (!*argv[1]) { std::cout << "SKIP: BMH_DATA not configured\n"; return 77; }
    try {
        const std::filesystem::path data = argv[1], input = argv[2];
        auto session = std::make_unique<app::CareerSession>(data, 1, input);
        const auto start_date = session->state().date;
        check(session->state().indoor && session->state().memory.u8(::bmh::layout::EventPasses) == 3 &&
              session->state().memory.u8(::bmh::layout::Cup_AdvanceCup_RuntimeField4) == 1,
              "unchanged Original WEEK19 must start the second tournament's Saturday");
        const TemporaryDirectory temporary;
        const auto path = temporary.path / "MASTERS.FBM";
        uint64_t revision = 1, fresh_revision = 1;
        // Use the real F6 action to retain desks after the human club is eliminated.
        // This lets us save at the Masters opening without constructing native fixtures.
        check(session->state().memory.u8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField) == 0, "Original WEEK19 F6 gate changed");
        const auto exe = load_le(data / "BMH.exe");
        const auto settings = ui::settings_from_game(session->state(),
            session->state().memory.u8(::bmh::layout::ActiveManager), revision, exe);
        ui::SettingsWriteRequest every_desk{settings.manager_slot, revision, settings.cells,
            5, 5, ui::SettingsEdit::toggle_binary};
        std::string setting_error;
        check(session->dispatch_settings(every_desk, revision, setting_error), setting_error);
        std::unique_ptr<app::CareerSession> fresh;
        std::unique_ptr<app::CareerSession> fresh_control;
        int completed = 1, turns = 0;
        int winner_calls[2] = {};
        uint16_t champion[2] = {0xffff, 0xffff};
        auto observe = [&](app::CareerSession& current, int path_index) {
            SportschauPresentationHooks hooks;
            hooks.winner = [&, path_index](const GameState& g, const CompetitionWinnerView& winner) {
                if (winner.title != 7) return;
                check(g.memory.u8(::bmh::layout::Cup_AdvanceCup_RuntimeField4) == 5 && g.indoor,
                      "Masters winner must be presented in the sixth indoor tournament");
                check(g.memory.u16(cup_entry_address(28) + 0x22) == winner.team,
                      "winner presentation does not identify the actual final-group champion");
                champion[path_index] = winner.team;
                ++winner_calls[path_index];
            };
            current.set_sportschau_presentation_hooks(std::move(hooks));
        };
        observe(*session, 0);
        while (turns++ < 45) {
            const int before = session->state().memory.u8(::bmh::layout::Cup_AdvanceCup_RuntimeField4);
            const auto date = session->state().date;
            const int passes = session->state().memory.u8(::bmh::layout::EventPasses);
            advance(*session, revision);
            if (fresh) {
                advance(*fresh, fresh_revision);
                uint64_t control_revision = fresh_revision - 1;
                advance(*fresh_control, control_revision);
                same(*fresh_control, *fresh);
            }
            auto& g = session->state();
            const int held = g.memory.u8(::bmh::layout::Cup_AdvanceCup_RuntimeField4);
            std::cout << "turn " << turns << " date " << g.date << " held " << held
                      << " passes " << int(g.memory.u8(::bmh::layout::EventPasses)) << " indoor " << g.indoor << '\n';
            if (before < 5 && held != before) {
                // F6 may simulate eliminated/absent managers' tournaments in the
                // background before the next human desk. Do not invent a desk per final.
                check(held > before && held <= 5, "Hall completion counter moved backwards: " +
                      std::to_string(before) + " -> " + std::to_string(held));
                completed = held;
            }
            if (date == start_date && passes > 1)
                check(g.date == date && g.memory.u8(::bmh::layout::EventPasses) == passes - 1,
                      "Saturday group rounds must keep their calendar date");
            if (held == 5 && !fresh && winner_calls[0] == 0) {
                check(completed == 5, "Masters follows all five qualifying tournaments");
                qualified_field(g);
                std::string error;
                auto expected_rand = session->rand();
                const auto expected = savegame_bytes(make_savegame(g, expected_rand));
                const auto native = save_stream(g);
                const auto game_seed = session->game().seed();
                const auto save_game_calls = session->game().calls();
                check(session->save_game(path, g.memory.u8(::bmh::layout::ActiveManager), true, error), error);
                check(read_file(path) == expected && save_stream(g) == native &&
                      session->rand().seed() == expected_rand.seed() &&
                      session->rand().calls() == expected_rand.calls() &&
                      session->game().seed() == game_seed && session->game().calls() == save_game_calls,
                      "normal Masters save differs from the native writer or its four Watcom draws");
                fresh = std::make_unique<app::CareerSession>(data, session->rand().seed(), path);
                fresh_control = std::make_unique<app::CareerSession>(data, session->rand().seed(), path);
                check(load_savegame(path).checksum_ok && fresh->state().indoor &&
                      fresh->rand().calls() == 0 && fresh->game().calls() == 0,
                      "normal Masters load consumes RNG or loses indoor mode");
                check(save_stream(fresh->state()) == native,
                      "normal Masters load changes native state");
                // CareerSession_Save_RuntimeField and the Watcom process state are not fields in legacy routine's
                // native format. Compare equal fresh processes, without injecting
                // the unpersisted live game seed into either loaded session.
                same(*fresh_control, *fresh);
                observe(*fresh, 1);
            }
            // The first following desk can be the national nomination day.
            // Continue through its ordinary owner before requiring a league desk.
            if (winner_calls[0] != 0 && !g.indoor && (g.memory.u32(::bmh::layout::SavedCompetition) & 0x1f) != 0) {
                check(winner_calls[0] == 1 && winner_calls[1] == 1 && fresh && completed == 5,
                      "Masters must settle once after the complete qualification cycle");
                for (int index = 0; index < 2; ++index) {
                    const auto& state = index == 0 ? g : fresh->state();
                    check(state.memory.u16(::bmh::layout::Cup_AdvanceCup_RuntimeField7) == champion[index] &&
                          state.memory.u16(::bmh::layout::Titles + 7 * 100 + uint32_t(std::min<int>(state.season, 49)) * 2) == champion[index],
                          "Masters champion is missing from the saved holder/history");
                    for (uint32_t i = 0; i < 8; ++i)
                        check(state.memory.u8(::bmh::layout::ActiveSelectors + i) != 11,
                              "finished Masters remains on the running-competition list");
                    check(!state.indoor && state.date > date && (state.memory.u32(::bmh::layout::SavedCompetition) & 0x1f) != 0,
                          "Masters completion does not reach the following real outdoor league desk");
                }
                advance(*session, revision);
                uint64_t control_revision = fresh_revision;
                advance(*fresh, fresh_revision); advance(*fresh_control, control_revision);
                same(*fresh_control, *fresh);
                std::cout << "Saved first qualifier -> remaining four -> Masters -> outdoor league desk; "
                             "native save/reload and two fresh continuations OK\n";
                return 0;
            }
        }
        throw std::runtime_error("Masters completion not reached within 45 real desk advances");
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
