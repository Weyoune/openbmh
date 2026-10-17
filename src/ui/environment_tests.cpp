#include "../../tests/runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/environment.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "formats/savestate.hpp"
#include "formats/fbm.hpp"
#include "game/career.hpp"
#include "game/messages.hpp"
#include "game/state.hpp"
#include "ui/desk_state.hpp"
#include "ui/manager.hpp"
#include "ui/sprite_font.hpp"

using namespace bmh;
using namespace bmh::ui;

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
uint8_t pixel(const Image& image, int x, int y) {
    return image.pixels.at(size_t(y) * size_t(image.width) + size_t(x));
}
void original_pixels(const std::filesystem::path& data, const LeImage& exe,
                     const TextDb& texts, EnvironmentView state) {
    const auto player = load_vga(asset_path(data / "KB", "129.VGA"));
    const auto art = load_vga(asset_path(data / "KB", "19.VGA"));
    const auto panel = load_vga(asset_path(data / "KB", "56.VGA"));
    const auto body = load_font(asset_path(data / "F", "BMH3.FNT"));
    SpriteFont sprite(data);
    // All six visible groups exercise an empty and an occupied star rail.
    for (int item = 0; item < 6; ++item) {
        state.values.level[size_t(item)] = item < 3 ? 0 : 2;
        state.values.monthly[size_t(item)] = state.values.level[size_t(item)] * kEnvironmentStep[size_t(item)];
    }
    for (int mode = 0; mode < 3; ++mode) {
        state.decorated_background = mode != 1;
        state.plain_background = mode == 2;
        state.solid_panel_background = mode == 2;
        state.gradient_text = mode != 2;
        auto actual = EnvironmentScreen(data, state, Image(320, 240, 0xe1)).render();
        const auto other_desk = EnvironmentScreen(data, state, Image(320, 240, 0x31)).render();
        check(actual.pixels == other_desk.pixels, "environment never inherits desk controls or notices");
        Image base = mode == 0 ? player : Image(320, 240, 0);
        if (mode != 0) {
            for (int y = 4; y <= 16; ++y) for (int x = 0; x < 320; ++x)
                base.at(x, y) = x > 0 && x < 319 && y > 4 && y < 16 ? 0x39 : 0x44;
            for (int y = 228; y <= 237; ++y) for (int x = 0; x < 320; ++x)
                base.at(x, y) = x > 0 && x < 319 && y > 228 && y < 237 ? 0x39 : 0x44;
        }
        const auto title = original_to_utf8(texts[text_id(::bmh::layout::Environment_Render_TextPointer)]);
        sprite.draw(base, 227 - sprite.text_width(title) / 2, 13, title, 5);
        sprite.draw(base, 5, 235, original_to_utf8(state.club_name), 5);
        for (int y = 0; y < 240; ++y) for (int x = 0; x < 320; ++x) {
            bool foreground = x >= 125 && x <= 195 && y >= 82 && y <= 151;
            for (int item : {0, 1, 2, 3, 4, 5}) {
                const int tx = exe.u8(::bmh::layout::Environment_FormatError_RuntimeField + item), ty = exe.u8(::bmh::layout::Environment_FormatError_RuntimeField2 + item);
                foreground |= x >= tx - 4 && x <= tx + 54 && y >= ty - 10 && y <= ty + 49 + (item < 3 ? 6 : 0);
            }
            if (!foreground) check(pixel(actual, x, y) == pixel(base, x, y),
                "Source KB129/flat background, centered title and active club footer");
        }
        int transparent_center = 0, center_ink = 0;
        for (int y = 0; y < 70; ++y) for (int x = 0; x < 71; ++x) {
            const int dx = 125 + x, dy = 82 + y;
            if (dx >= 139 && dx <= 178 && dy >= 96 && dy <= 135) continue;
            const auto ink = pixel(art, x, y);
            transparent_center += ink == 0;
            center_ink += ink != 0;
            check(pixel(actual, dx, dy) == (ink ? ink : pixel(base, dx, dy)),
                "legacy routine central art compares index0, preserving player background");
        }
        check(transparent_center > 0 && center_ink > 0, "central crop oracle covers transparent and ink pixels");
        check(actual.at(139, 96) == 0x46 && actual.at(178, 96) == 0x40 &&
              actual.at(139, 135) == 0x40, "Source badge frame and corner overwrite order");
        for (int item : {0, 1, 2, 3, 4, 5}) {
            const int tx = exe.u8(::bmh::layout::Environment_FormatError_RuntimeField + item), ty = exe.u8(::bmh::layout::Environment_FormatError_RuntimeField2 + item);
            for (int y = 0; y < 41; ++y) for (int x = 0; x < 51; ++x)
                check(pixel(actual, tx + x, ty + y) == pixel(art, item * 51 + x, 70 + y),
                    "legacy routine tile is opaque, without a fabricated c0 comparekey");
            // The Source upper panel is copied first; each nonzero star pixel
            // replaces it. A zero in the star bitmap exposes that panel.
            const int stars = state.values.level[size_t(item)];
            for (int y = ty - 9; y <= ty - 2; ++y) for (int x = tx - 3; x <= tx + 53; ++x) {
                uint8_t expected = state.solid_panel_background ? 0x42 : pixel(panel, x - 1, y - 20);
                for (int n = 0; n < stars; ++n)
                    if (x >= tx + n * 9 && x < tx + n * 9 + 7 && y >= ty - 8 && y < ty - 2) {
                        const auto ink = pixel(art, 30 + x - tx - n * 9, 30 + y - ty + 8);
                        if (ink) expected = ink;
                    }
                check(pixel(actual, x, y) == expected, "Source star zero pixels expose the opaque KB56/flat rail");
            }
            const auto cost = state.cost_text[size_t(item)] + exe.cstring(::bmh::layout::EnvironmentCostSuffix);
            const int width = body.text_width(cost), left = tx + 25 - width / 2;
            Image mask(width, body.height);
            body.draw(mask, 0, 0, cost, 1);
            unsigned ramp = 0;
            while (ramp < 32 && exe.u8(::bmh::layout::Advertising_Text_RuntimeField2 + ramp) != 0x6b) ++ramp;
            for (int y = ty + 42; y <= ty + 48; ++y) for (int x = tx - 3; x <= tx + 53; ++x) {
                uint8_t expected = state.solid_panel_background ? 0x42 : pixel(panel, x - 1, y - 20);
                const int mx = x - left, my = y - (ty + 48 - body.baseline);
                if (mx >= 0 && mx < width && my >= 0 && my < body.height && pixel(mask, mx, my))
                    expected = state.gradient_text && exe.u8(::bmh::layout::TextGradientMode) == 2 && ramp < 32
                        ? exe.u8(::bmh::layout::Advertising_Text_RuntimeField + ramp * 6 + unsigned(my)) : 0x6b;
                check(pixel(actual, x, y) == expected, "Source cost strip uses Font3, exact centering and gradient toggle");
            }
        }
    }
}
void original_badges(const std::filesystem::path& data, EnvironmentView state) {
    const auto source_w = load_vga(asset_path(data / "W", std::to_string(state.badge_sprite >> 3) + ".VGA"));
    const auto source_kb0 = load_vga(asset_path(data / "KB", "0.VGA"));
    // Explicit boundary cases from legacy routine; the expected selection does not
    // call or recreate the presenter's gate. The W sprite stays identical.
    struct Case { int team; bool generic; };
    const Case fixtures[]{{0, false}, {89, false}, {90, true}, {137, true},
        {138, false}, {167, false}, {168, true}, {356, true}, {357, false},
        {4094, false}, {4095, true}, {0x1000 + 89, false}, {0x1000 + 90, true}};
    for (const Case fixture : fixtures) {
        state.runtime_club = fixture.team;
        auto frame = EnvironmentScreen(data, state, Image(320, 240, 0xe1)).render();
        const auto& expected = fixture.generic ? source_kb0 : source_w;
        const int sx = fixture.generic ? 8 : int(state.badge_sprite & 7) * 38;
        for (int y = 0; y < 38; ++y) for (int x = 0; x < 38; ++x)
            check(pixel(frame, 140 + x, 97 + y) == pixel(expected, sx + x, y),
                "Source runtime team gate selects the complete opaque KB0 or W badge crop");
        check(frame.at(139, 96) == 0x46 && frame.at(178, 96) == 0x40 &&
              frame.at(139, 135) == 0x40, "both badge paths retain the Source frame");
    }
    // A generic club must not try to load its irrelevant W sprite at all.
    state.runtime_club = 90;
    state.badge_sprite = 0xffff;
    auto generic = EnvironmentScreen(data, state, Image(320, 240)).render();
    for (int y = 0; y < 38; ++y) for (int x = 0; x < 38; ++x)
        check(pixel(generic, 140 + x, 97 + y) == pixel(source_kb0, 8 + x, y),
            "generic Source badge ignores the W sprite mapping");
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 3, "usage: ui_environment_tests ORIGINAL_DATA SAVEGAME");
        const std::filesystem::path data(argv[1]);
        const auto exe = bmh::test::load_executable(asset_path(data, "BMH.exe"));
        const auto saved = load_savegame(argv[2]);
        const DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
        GameState game = GameState::from_save(SaveState(saved.stream, counts), counts);
        competition_pointers(game.memory, exe);
        const auto texts = load_textd(asset_path(data, "TEXTD.BLK"));
        game.texts = &texts;

        auto state = environment_from_game(game, 17);
        DeskRequest owner;
        owner.kind = DeskRequestKind::open_menu;
        owner.menu_kind = 24;
        owner.manager_slot = state.manager_slot;
        owner.revision = state.revision;
        state.owner = owner;
        original_pixels(data, exe, texts, state);
        original_badges(data, state);
        EnvironmentScreen screen(data, state, Image(320, 240, 0));
        auto frame = screen.render();
        check(frame.width == 320 && frame.height == 240, "Original environment frame dimensions");

        const int x = exe.u8(::bmh::layout::Environment_FormatError_RuntimeField + 3), y = exe.u8(::bmh::layout::Environment_FormatError_RuntimeField2 + 3);
        check(screen.pointer_down(x + 31, y + 5), "Source co-trainer tile owns the press");
        const auto held = screen.state_json();
        check(screen.key("ESC").kind == EnvironmentRequestKind::none && !screen.close_requested() &&
              screen.state_json() == held, "back cannot steal a held staff control before its release");
        const auto request = screen.release(x + 31, y + 5);
        check(request.kind == EnvironmentRequestKind::set_item && request.item == 3 && request.level == 4 &&
              request.manager_slot == state.manager_slot && request.runtime_club == state.runtime_club &&
              request.revision == 17 && request.owner == owner,
              "tile release emits a typed, manager/revision-owned level request");
        const auto pending = screen.state_json();
        check(screen.key("ESC").kind == EnvironmentRequestKind::none && screen.pending() &&
              !screen.close_requested() && screen.state_json() == pending,
              "back cannot discard an issued staff mutation before its ACK");

        auto updated = state;
        updated.revision = 18;
        updated.values.level[3] = 4;
        updated.values.monthly[3] = 20000;
        updated.cost_text[3] = format_number(game, 20000, 0x185);
        updated.owner.revision = 18;
        screen.acknowledge(updated);
        check(!screen.pending() && screen.state().revision == 18 &&
              screen.state().values.monthly[3] == 20000,
              "valid source mutation ACK refreshes the screen and clears the pending action");

        const auto notice = environment_notice_dialog(game, state.manager_slot,
            "Unit academy notice");
        check(notice.kind == TaxDialogKind::acknowledgement &&
              notice.source_manager_flags == (uint8_t(state.manager_slot) | 0x40) &&
              notice.source_flags == 0 && notice.body == "Unit academy notice",
              "academy rejection uses the exact Source timed-notice call shape and body");

        // Independent legacy routine boundary expectations; selector zero is valid
        // and a generic runtime club must not dereference the W table.
        auto identity=game;
        identity.club_heap_78.assign(4095*2,0);
        const struct {int club;bool fallback;} cases[]{{0,false},{45,false},{89,false},
            {90,true},{137,true},{138,false},{167,false},{168,true},{356,true},
            {357,false},{4094,false},{4095,true},{0x1000+45,false}};
        for(const auto entry:cases) {
            identity.manager_teams[size_t(state.manager_slot)]=entry.club;
            auto projected=environment_notice_dialog(identity,state.manager_slot,"Hinweis");
            check(projected.badge_fallback==entry.fallback && projected.badge_sprite==0,
                  "Source runtime club gate preserves valid W sprite zero and fallback boundaries");
            if(entry.fallback) {
                auto no_table=identity;no_table.club_heap_78.clear();
                check(environment_notice_dialog(no_table,state.manager_slot,"Hinweis").badge_fallback,
                      "Source generic badge does not depend on any W-table bytes");
            }
        }
        identity.manager_teams[size_t(state.manager_slot)]=45;
        identity.club_heap_78.clear();bool truncated=false;
        try {(void)environment_notice_dialog(identity,state.manager_slot,"Hinweis");}
        catch(const FormatError&) {truncated=true;}
        check(truncated,"missing real badge table is rejected without inventing a fallback");

        check(screen.pointer_down(10, 10, true), "right-button close is owned by environment screen");
        (void)screen.release(10, 10, true);
        check(screen.close_requested(), "matched right-button release closes the source menu");
        std::cout << "ui environment tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ui environment tests: " << error.what() << "\n";
        return 1;
    }
}
