// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/tactics.hpp"

#include <algorithm>
#include <functional>
#include <iostream>

#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "game/state.hpp"

using namespace bmh;
using namespace bmh::ui;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void rejected(const std::function<void()>& f) {
    try { f(); } catch (const FormatError&) { return; }
    throw std::runtime_error("expected FormatError");
}
GameState fixture() {
    GameState g;
    g.memory.add_block(::bmh::layout::ManagerCount, 1); g.memory.set8(::bmh::layout::ManagerCount, 2);
    g.memory.add_block(::bmh::layout::ActiveManager, 1); g.memory.set8(::bmh::layout::ActiveManager, 1);
    g.memory.add_block(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3, 1); g.memory.add_block(::bmh::layout::PlainBackground, 1); g.memory.set8(::bmh::layout::PlainBackground, 1);
    g.manager_records.resize(5 * 0xb0c); g.tactics.resize(5 * 0x840); g.manager_options.resize(5 * 5);
    g.manager_teams = {2, 12}; g.club_ids.resize(20, 0); g.club_names = {'B','A','Y','E','R','N',0};
    const std::string name = "Second";
    std::copy(name.begin(), name.end(), g.manager_records.begin() + 0xb0c + 0xa8);
    g.manager_records[0xb0c + 0x290] = 2; // Intentionally stale copy; typed identity must win.
    g.manager_records[0xb0c + 0xd2] = 3;
    g.manager_records[0xb0c + 0xd9] = 11;
    g.manager_options[5 + 2] = 1;
    for (int row = 0; row < 11; ++row) {
        g.tactics[0x840 + row * 0x58 + 0x41] = uint8_t(row % 4 + 2);
        g.tactics[0x840 + row * 0x58 + 0x42] = uint8_t(row % 7);
    }
    return g;
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::runtime_error("data directory required");
        const std::filesystem::path data = argv[1]; const auto exe = load_le(asset_path(data, "BMH.exe"));
        auto game = fixture(); const auto before_records = game.manager_records, before_board = game.tactics;
        auto s = tactics_from_game(game, exe, 7);
        check(s.manager_slot == 1 && s.runtime_club == 12 && s.manager_name == "Second" && s.values[0] == 3,
            "typed manager identity/projection");
        check(s.depth[10] == 4 && s.lane[10] == 3 && s.assistant_attitude == 1, "board/options projection");
        check(game.manager_records == before_records && game.tactics == before_board, "projection must not mutate engine");
        auto invalid = game; invalid.manager_records[0xb0c + 0xd7] = 2;
        rejected([&] { tactics_from_game(invalid, exe, 8); });
        invalid = game; invalid.memory.set8(::bmh::layout::ActiveManager, 4); rejected([&] { tactics_from_game(invalid, exe, 8); });
        invalid = game; invalid.tactics.resize(0x840); rejected([&] { tactics_from_game(invalid, exe, 8); });
        TacticsScreen screen(data, s); const auto pixels = screen.render().pixels;
        check(pixels == screen.render().pixels, "pure/repeated render");
        const auto r = screen.click(180, 50);
        check(r.kind == TacticsRequestKind::set_tactic && r.index == 5 && r.desired_value == 1 &&
            r.manager_slot == 1 && r.runtime_club == 12 && r.revision == 7 && !r.desired_assistant_attitude,
            "original wing switch and explicit desired value");
        check(screen.state().values[5] == 0 && screen.render().pixels == pixels, "request must not fake committed state");
        check(screen.click(250,50).kind == TacticsRequestKind::none &&
            screen.click(0,0,true).kind == TacticsRequestKind::none && !screen.close_requested(), "pending blocks repeated input/navigation");
        rejected([&] { screen.acknowledge(s); });
        auto next = s; next.revision = 8; next.manager_slot = 0;
        rejected([&] { screen.acknowledge(next); });
        next = s; next.revision = 8; next.runtime_club = 2;
        rejected([&] { screen.acknowledge(next); });
        next = s; next.revision = 8; next.values[5] = 2;
        rejected([&] { screen.acknowledge(next); });
        check(screen.pending().has_value() && screen.state().revision == 7, "failed acknowledgement preserves pending/state");
        next = s; next.revision = 8; next.values[5] = 1; screen.acknowledge(next);
        check(!screen.pending() && screen.state().values[5] == 1, "authoritative acknowledgement");
        auto toggle = screen.click(180,50); check(toggle.desired_value == 0, "wing switch wraps");
        screen.reject_action("controller rejected");
        check(screen.state().values[5] == 1 && screen.state_json().find("controller rejected") != std::string::npos,
            "rejection preserves authoritative value");
        auto arrow = screen.click(40,40);
        check(arrow.index == 0 && arrow.desired_value == 0 && arrow.desired_assistant_attitude == 0,
            "arrow cycle wraps and disables assistant attitude");
        screen.reject_action("test rollback");
        auto importance = screen.click(273,140);
        check(importance.index == 7 && importance.desired_value == 11, "importance upper cap");
        screen.reject_action("test rollback");
        importance = screen.click(203,140);
        check(importance.index == 7 && importance.desired_value == 0, "importance lower cap");
        screen.reject_action("test rollback");
        check(screen.click(-1,50).kind == TacticsRequestKind::none && !screen.pending(), "outside hitbox");
        screen.click(10,10,true); check(screen.close_requested(), "original right button returns to desk");
        TacticsScreen escape(data, s);
        const auto before_escape = escape.state_json();
        escape.key("ESC"); escape.key("ESCAPE");
        check(!escape.close_requested() && escape.state_json() == before_escape,
            "format field ignores physical Escape without changing the view");
        escape.click(10,10,true);
        check(escape.close_requested(), "right-button return still works after ignored Escape");
        if (argc > 2 && *argv[2]) {
            const auto original = read_file(argv[2]); auto fbm = load_savegame(argv[2]);
            check(fbm.checksum_ok && fbm.version == "V2.24", "original reference integrity");
            SaveState save(fbm.stream, {exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)});
            auto ref = tactics_from_save(save,exe,3);
            check(ref.manager_slot == 0 && ref.runtime_club == 12 && ref.manager_name == "BMHLAB" &&
                ref.source == "original_save_read_only", "original save identity");
            TacticsScreen reference(data,ref); check(reference.render().pixels.size() == 320 * 240, "original render");
            check(original == read_file(argv[2]), "original reference remains unchanged");
        }
        std::cout << "tactics tests passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
