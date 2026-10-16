// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/training.hpp"

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
void rejected(const std::function<void()>& fn) {
    try { fn(); } catch (const FormatError&) { return; }
    throw std::runtime_error("expected rejected operation");
}
GameState fixture() {
    GameState g;
    g.memory.add_block(::bmh::layout::ManagerCount, 1); g.memory.set8(::bmh::layout::ManagerCount, 2);
    g.memory.add_block(::bmh::layout::ActiveManager, 1); g.memory.set8(::bmh::layout::ActiveManager, 1);
    g.memory.add_block(::bmh::layout::GameMode, 1); g.memory.set8(::bmh::layout::GameMode, 3);
    g.memory.add_block(::bmh::layout::LeaguePresentationOptions, 0xb4);
    g.memory.add_block(::bmh::layout::ManagerRecords, 5 * 0xb0c); // stale copies deliberately differ
    g.memory.add_block(::bmh::layout::TacticsBoards, 5 * 0x840);
    g.memory.add_block(::bmh::layout::ManagerAutomation, 20);
    g.manager_teams = {2, 12};
    g.manager_records.resize(5 * 0xb0c);
    std::fill_n(g.manager_records.begin() + 0xb0c + 0xc5, 13, 2);
    g.manager_options.resize(20); g.manager_options[9] = 0x9f;
    g.tactics.resize(5 * 0x840);
    g.club_ids.resize(419); g.club_names = {'B', 'A', 'Y', 'E', 'R', 'N', 0};
    g.squad_slots.resize(3096, 0x0fff);
    g.players.resize(4); g.player_name_offsets.resize(4);
    for (int i = 0; i < 4; ++i) {
        g.squad_slots[12 * 24 + i] = uint16_t(i | (i < 2 ? 0x8000 : 0x4000));
        g.players[i].w364 = uint32_t(i);
        g.players[i].w368 = 0x1234;
        g.player_name_offsets[i] = uint16_t(g.player_names.size());
        g.player_names.push_back('A' + i); g.player_names.push_back(0);
        g.tactics[0x840 + i * 0x58 + 0x0a] = uint8_t((i << 2) | 2);
        g.tactics[0x840 + i * 0x58 + 0x0f] = uint8_t(30 + i);
    }
    return g;
}
void ack(TrainingScreen& screen, TrainingState state) { ++state.revision; screen.acknowledge(std::move(state)); }
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::runtime_error("original data directory required");
        const std::filesystem::path data = argv[1]; const auto exe = load_le(asset_path(data, "BMH.exe"));
        auto game = fixture();
        const auto records = game.manager_records, options = game.manager_options, board = game.tactics;
        const auto squads = game.squad_slots;
        auto s = training_from_game(game, exe, 7);
        check(s.manager_slot == 1 && s.runtime_club == 12 && s.types[0] == 2 && s.areas[0] == 2 && s.automation == 0x9f,
              "authoritative typed tables override stale static-memory copies");
        check(s.players.size() == 4 && s.players[3].row == 3 && s.players[3].player_id == 3 && s.players[3].focus == 3,
              "squad identity, name pool and tactics bit projection");
        check(game.manager_records == records && game.manager_options == options && game.tactics == board && game.squad_slots == squads,
              "projection may not change original game tables");
        TrainingScreen screen(data, s); const auto frame = screen.render().pixels;
        check(frame == screen.render().pixels, "repeat rendering is deterministic and pure");
        check(screen.click(175, 26).kind == TrainingRequestKind::none, "condition+technique cannot exceed four balls");
        auto r = screen.click(155, 26);
        check(r.kind == TrainingRequestKind::set_area && r.index == 0 && r.value == 1 && r.manager_slot == 1 &&
              r.runtime_club == 12 && r.revision == 7 && r.desired_areas[0] == 1 && r.desired_automation == 0x9b,
              "direct condition slot emits exact context and clears automatic area flag");
        check(screen.state().areas[0] == 2 && screen.render().pixels == frame, "pending command must not fabricate applied values");
        check(screen.click(155, 43).kind == TrainingRequestKind::none, "pending command blocks repeated edits");
        screen.click(120, 214, true); screen.key("ESC"); check(!screen.close_requested(), "pending command blocks close");
        rejected([&] { screen.acknowledge(s); });
        auto wrong = s; ++wrong.revision; wrong.runtime_club = 11; rejected([&] { screen.acknowledge(wrong); });
        wrong = s; ++wrong.revision; wrong.manager_slot = 0; rejected([&] { screen.acknowledge(wrong); });
        auto authoritative = s; authoritative.areas = r.desired_areas; authoritative.automation = r.desired_automation;
        ack(screen, authoritative);
        check(!screen.pending() && screen.state().areas[0] == 1, "only newer authoritative acknowledgment applies values");
        r = screen.click(175, 43); check(r.kind == TrainingRequestKind::set_area && r.index == 1 && r.value == 3,
                                       "three technique balls permitted after reducing condition");
        screen.reject_action("test engine rejection");
        check(!screen.pending() && screen.state().areas[1] == 2 && screen.state_json().find("test engine rejection") != std::string::npos,
              "explicit rejection preserves values and records reason");
        rejected([&] { screen.reject_action("no pending command"); });
        screen.click(120, 214, true); check(screen.close_requested(), "right button returns to desktop locally");
        screen.clear_close_request(); screen.key("ESC"); check(screen.close_requested(), "headless ESC close is explicit");
        screen.clear_close_request();

        TrainingScreen types(data, s);
        check(types.click(13, 206).kind == TrainingRequestKind::none, "type total cannot exceed eight");
        r = types.click(13, 186); check(r.kind == TrainingRequestKind::set_type && r.index == 0 && r.value == 1 &&
                                     r.desired_automation == 0x9d, "type edit clears automatic type flag");
        authoritative = s; authoritative.types = r.desired_types; authoritative.automation = r.desired_automation; ack(types, authoritative);
        r = types.click(35, 206); check(r.kind == TrainingRequestKind::set_type && r.index == 1 && r.value == 3,
                                     "type total exactly eight allowed");

        TrainingScreen skills(data, s);
        r = skills.click(175, 60); check(r.kind == TrainingRequestKind::set_area && r.index == 2 && r.value == 3,
                                       "skill fifteen emits change without immediate simulation");
        authoritative = s; authoritative.areas = r.desired_areas; authoritative.automation = r.desired_automation; ack(skills, authoritative);
        check(skills.state_json().find("\"overtraining\":true") != std::string::npos && skills.render().pixels != frame,
              "fifteen skill balls use original red sprites");
        auto capped = s; capped.areas = {2, 2, 3, 3, 3, 3, 2, 2, 2}; TrainingScreen maxskills(data, capped);
        check(maxskills.click(175, 128).kind == TrainingRequestKind::none, "skill budget cannot exceed eighteen");
        auto invalid = capped; invalid.areas[8] = 3; rejected([&] { TrainingScreen bad(data, invalid); });
        invalid = s; invalid.areas[0] = 3; rejected([&] { TrainingScreen bad(data, invalid); });
        invalid = s; invalid.types[0] = 0; rejected([&] { TrainingScreen bad(data, invalid); });
        invalid = s; invalid.mode = 1; rejected([&] { TrainingScreen bad(data, invalid); });
        invalid = s; invalid.players[0].row = 24; rejected([&] { TrainingScreen bad(data, invalid); });

        TrainingScreen players(data, s);
        r = players.click(312, 25);
        check(r.kind == TrainingRequestKind::set_player_intensity && r.index == 0 && r.player_id == 0 && r.value == 3 &&
              r.desired_automation == 0x97, "individual intensity preserves identity and clears automatic intensity flag");
        players.reject_action("not attached");
        r = players.click(280, 25);
        check(r.kind == TrainingRequestKind::set_player_focus && r.index == 0 && r.value == 1 && r.desired_automation == 0x9f,
              "focus icon cycles without clearing automatic focus bit, as original branch does");
        players.reject_action("not attached");
        auto focus7 = s; focus7.players[0].focus = 7; TrainingScreen cycle(data, focus7);
        check(cycle.click(280, 25).value == 0, "focus seven cycles to zero");

        TrainingScreen automatic(data, s);
        r = automatic.click(125, 200); check(r.kind == TrainingRequestKind::set_automation && r.index == 0 && r.value == 0 &&
                                           r.desired_automation == 0x9e, "automatic button emits engine request only");
        check(automatic.state().automation == 0x9f, "automatic UI never runs random training assignment");
        automatic.reject_action("not attached");
        r = automatic.click(100, 175); check(r.kind == TrainingRequestKind::set_automation && r.index == 1 &&
                                           r.desired_automation == 0x9d, "type AUTO subset toggles bit one");

        auto uninitialized = s; uninitialized.automation = 0; TrainingScreen entry(data, uninitialized);
        check(entry.state().needs_initialization() && entry.render().pixels == entry.render().pixels, "uninitialized projection renders without mutation");
        rejected([&] { entry.click(155, 26); });
        r = entry.initialize(); check(r.kind == TrainingRequestKind::initialize && r.desired_automation == 0x9f &&
                                     entry.state().automation == 0, "first-open initialization is explicit engine work");
        check(entry.initialize().kind == TrainingRequestKind::none, "initialization cannot be replayed while pending");
        authoritative = s; ack(entry, authoritative); check(!entry.state().needs_initialization(), "initialization completes on newer state");

        // Sparse squad rows retain their engine identities; no guessed player insertion.
        game.squad_slots[12 * 24 + 1] = 0x0fff;
        auto sparse = training_from_game(game, exe, 8); check(sparse.players.size() == 3 && sparse.players[1].row == 2,
                                                          "empty rows are omitted without renumbering engine slots");
        game.manager_teams[1] = 138; rejected([&] { training_from_game(game, exe, 9); });

        if (argc > 2 && *argv[2]) {
            const auto fbm = load_savegame(argv[2]); check(fbm.checksum_ok && fbm.version == "V2.24", "original V2.24 reference checksum");
            const auto stream_before = fbm.stream;
            SaveState save(fbm.stream, {exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)});
            auto ref = training_from_save(save, exe, 99);
            check(ref.manager_slot == 0 && ref.runtime_club == 12 && ref.types == std::array<uint8_t, 4>{2, 2, 2, 2} &&
                  ref.areas == std::array<uint8_t, 9>{2, 2, 2, 2, 1, 3, 2, 2, 2} && ref.automation == 0x9f && ref.players.size() == 20,
                  "captured original post-initialization training state");
            TrainingScreen original(data, ref); check(original.render().pixels == original.render().pixels, "original reference pure rendering");
            check(fbm.stream == stream_before, "reading/rendering cannot mutate decrypted save stream");
        }
        std::cout << "training projection, original budgets, player flags, pending/ack and purity tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
