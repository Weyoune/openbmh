#include "../../tests/runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/settings.hpp"

#include <iostream>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <stdexcept>

#include "game/state.hpp"
#include "formats/savestate.hpp"
#include "formats/fbm.hpp"
#include "formats/textd.hpp"
#include "ui/setup.hpp"

using namespace bmh;
using namespace bmh::ui;

namespace {
void check(bool condition, const char* why) {
    if (!condition) throw std::runtime_error(why);
}
}

int main(int argc, char** argv) try {
    if (argc != 2 && argc != 3) throw std::runtime_error("usage: ui_settings_tests <data> [Original settings save]");
    const std::filesystem::path data = argv[1];
    const auto exe = bmh::test::load_executable(asset_path(data, "BMH.exe"));
    const auto atlas = load_vga(asset_path(data / "KB", "15.VGA"));
    const auto button_atlas = load_vga(asset_path(data / "KB", "0.VGA"));
    const auto make_readme_root = [&](bool with_readme, const std::filesystem::path& original) {
        const auto root = std::filesystem::temp_directory_path() /
            ("openbmh-settings-readme-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root / "KB");
        std::filesystem::create_directories(root / "F");
        std::filesystem::copy_file(asset_path(data, "BMH.exe"), root / "BMH.exe");
        std::filesystem::copy_file(asset_path(data, "TEXTD.BLK"), root / "TEXTD.BLK");
        std::filesystem::copy_file(asset_path(data / "KB", "129.VGA"), root / "KB" / "129.VGA");
        std::filesystem::copy_file(asset_path(data / "F", "BMH3.FNT"), root / "F" / "BMH3.FNT");
        if (with_readme) std::filesystem::copy_file(original, root / "LIESMICH.TXT");
        return root;
    };
    {
        const auto root = make_readme_root(false, {});
        SettingsReadmeScreen missing(root, true);
        check(!missing.file_available() && missing.displayed_bytes() == 0 &&
              !missing.close_requested(), "missing Original readme starts in the no-file page");
        const auto first = missing.render().pixels;
        (void)missing.render();
        check(missing.page_index() == 0 && missing.render().pixels == first,
              "rendering the missing-file view does not advance or alter its page");
        check(missing.pointer_down(true) && missing.close_requested() &&
              !missing.pointer_down(true), "left input closes the missing-file view exactly once");
        SettingsReadmeScreen right_close(root, true);
        check(right_close.pointer_down(true) && right_close.close_requested(),
              "right input closes the missing-file view");
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    if (const char* readme = std::getenv("BMH_UI_README_FILE"); readme && *readme) {
        const std::filesystem::path original(readme);
        const auto root = make_readme_root(true, original);
        SettingsReadmeScreen pages(root, true);
        check(pages.file_available() && pages.displayed_bytes() == std::filesystem::file_size(original),
              "external Original readme overlay loads without bundling the document");
        size_t page_count = 1;
        size_t presses = 0;
        while (!pages.close_requested()) {
            check(++presses < 100, "Original readme paging terminates within the bounded fixture");
            const size_t before = pages.page_index();
            const auto frame = pages.render().pixels;
            (void)pages.render();
            check(pages.page_index() == before && pages.render().pixels == frame,
                  "rendering the current Original readme page does not advance it");
            check(pages.pointer_down(true), "left input advances or closes the Original readme");
            if (pages.close_requested()) {
                check(pages.page_index() == before, "final readme input closes without a phantom page");
            } else {
                check(pages.page_index() == before + 1, "each readme input advances exactly one page");
                page_count = pages.page_index() + 1;
            }
        }
        check(!pages.pointer_down(true), "closed Original readme ignores additional input");
        std::cout << "Original readme pages: " << page_count << '\n';
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    {
        std::array<uint8_t, 180> raw{};
        SettingsAvailabilityInputs inputs{3, 3, 0, 0};
        auto projected = project_settings_availability(raw, inputs, exe);
        check(projected == raw, "mode III with both competitions and no active sound latches leaves empty cells available");
        inputs.mode = 2;
        projected = project_settings_availability(raw, inputs, exe);
        check((projected[13] & 0x80) && (projected[148] & 0x80) && (projected[157] & 0x80) &&
              !(projected[139] & 0x80), "Original modes below III lock B5/Q5/R5; only mode I also locks P5");
        inputs.mode = 4;
        projected = project_settings_availability(raw, inputs, exe);
        check((projected[13] & 0x80) && !(projected[148] & 0x80) &&
              !(projected[157] & 0x80) && !(projected[139] & 0x80),
              "mode IV locks B5 by !=3, but leaves Q5/R5/P5 enabled by the separate <3/==1 guards");
        inputs.mode = 1;
        projected = project_settings_availability(raw, inputs, exe);
        check(projected[139] & 0x80, "Original mode I locks P5");
        auto nonzero = raw;
        nonzero.fill(2);  // Nonzero but not an active (=1) link-table trigger.
        for (int row = 3; row < 8; ++row) nonzero[size_t(row * 9 + 6)] = 2;
        projected = project_settings_availability(nonzero, {3, 3, 0, 0}, exe);
        check((projected[2 * 9 + 6] & 0x80) && projected[2 * 9 + 6] % 128 == 2,
              "nonzero C7-H7 prerequisites lock C7 and preserve its value bits");
        nonzero = raw;
        nonzero.fill(2);
        projected = project_settings_availability(nonzero, {3, 3, 0, 0}, exe);
        for (int row = 2; row < 8; ++row)
            check(projected[size_t(row * 9 + 6)] & 0x80,
                  "nonzero full column 4 locks dependent cells C7-H7");
        inputs = {3, 2, 0, 0};
        projected = project_settings_availability(raw, inputs, exe);
        check((projected[0] & 0x80) && !(projected[9 * 10] & 0x80) &&
              !(projected[9 * 15] & 0x80) && (projected[6 * 9 + 6] & 0x80),
              "league-off rule preserves rows J-M and P, and locks G7");
        inputs = {3, 1, 0, 0};
        projected = project_settings_availability(raw, inputs, exe);
        check((projected[9 * 9] & 0x80) && (projected[12 * 9 + 3] & 0x80) &&
              !(projected[8 * 9] & 0x80) && !(projected[13 * 9] & 0x80),
              "world-off rule locks rows J-M in columns 1-4 only");
        inputs = {3, 3, 1, 1};
        projected = project_settings_availability(raw, inputs, exe);
        check((projected[5] & 0x80) && (projected[6] & 0x80), "sound-config latches lock A6/A7");
        raw[16 * 9 + 4] = 1;  // Q5: Original link-table trigger 0x84
        projected = project_settings_availability(raw, {3, 3, 0, 0}, exe);
        check(raw[16 * 9 + 4] == 1 && (projected[16 * 9 + 1] & 0x80) &&
              (projected[16 * 9 + 2] & 0x80) && (projected[16 * 9 + 3] & 0x80),
              "link dependencies are projected from Original executable tables without changing input");
    }
    {
        // Controlled UI fixture: old SOUND.CFG availability bits in a Native
        // save are distinct from the portable host's current capabilities.
        GameState sound;
        sound.manager_teams = {1};
        sound.memory.add_block(::bmh::layout::LeaguePresentationOptions, 180);
        sound.memory.add_block(::bmh::layout::GameMode, 1);
        sound.memory.add_block(::bmh::layout::CompetitionSelection, 4);
        sound.memory.set8(::bmh::layout::GameMode, 3);
        sound.memory.set32(::bmh::layout::CompetitionSelection, 3);
        sound.memory.set8(::bmh::layout::Audio_OriginalAudioOptions_ExecutableField, 0x80); // Music enabled, saved old lock.
        sound.memory.set8(::bmh::layout::Audio_OriginalAudioOptions_ExecutableField2, 0x81); // Clicks disabled, saved old lock.
        const auto unknown = settings_from_game(sound, 0, 23, exe);
        check(!unknown.audio_config_known && unknown.display_cells &&
              (*unknown.display_cells)[5] == 0x80 && (*unknown.display_cells)[6] == 0x81,
              "headless sound projection preserves both unknown Native lock bytes");
        for (bool music : {false, true}) for (bool effects : {false, true}) {
            const auto projected = settings_from_game(sound, 0, 23, exe,
                SettingsSoundCapabilities{music, effects});
            check(projected.audio_config_known && projected.display_cells &&
                  projected.cells == unknown.cells && projected.manager_slot == 0 && projected.revision == 23,
                  "runtime sound capabilities do not rewrite raw cells or ownership");
            check((*projected.display_cells)[5] == (music ? 0 : 0x80) &&
                  (*projected.display_cells)[6] == (effects ? 1 : 0x81),
                  "A6 music and A7 effects availability are independent of saved lock bits");
            for (size_t i = 0; i < projected.cells.size(); ++i)
                if (i != 5 && i != 6)
                    check((*projected.display_cells)[i] == (*unknown.display_cells)[i],
                          "sound capability projection leaves other settings availability unchanged");
            SettingsScreen view(data, projected);
            const auto frame = view.render();
            for (int cell = 5; cell <= 6; ++cell) {
                const bool supported = cell == 5 ? music : effects;
                const int source = cell == 5 ? 115 : 46;
                for (int y = 0; y < 9; ++y) for (int x = 0; x < 23; ++x) {
                    const auto overlay = atlas.pixels[size_t(y + 9) * atlas.width + x];
                    const auto base = atlas.pixels[size_t(y) * atlas.width + source + x];
                    check(frame.pixels[size_t(24 + y) * 320 + 67 + cell * 24 + x] ==
                          (!supported && overlay ? overlay : base),
                          "known sound capability controls the actual A6/A7 availability overlay");
                }
                check(view.click(70 + cell * 24, 27) && view.pending() &&
                      view.pending()->row == 0 && view.pending()->column == cell &&
                      view.pending()->expected_raw == unknown.cells &&
                      view.pending()->operation == SettingsEdit::toggle_binary,
                      "sound click sends the raw snapshot and exact cell to Session validation");
                if (supported) {
                    auto changed = projected;
                    changed.revision++;
                    changed.cells[size_t(cell)] = uint8_t((changed.cells[size_t(cell)] & 0x7f) ^ 1);
                    (*changed.display_cells)[size_t(cell)] = changed.cells[size_t(cell)];
                    view.acknowledge(changed);
                    check(!view.pending() && view.state().cells == changed.cells,
                          "sound acknowledgement accepts an intentional toggle clearing an old lock");
                    // Use the same original snapshot for the second independent cell.
                    view = SettingsScreen(data, projected);
                } else {
                    view.reject("Dieses Einstellungsfeld ist gesperrt.");
                    check(!view.pending() && view.state().cells == unknown.cells,
                          "rejected unavailable sound click preserves raw settings");
                    const auto rejected = view.render();
                    for (int y = 24; y < 33; ++y) for (int x = 187; x < 234; ++x)
                        check(rejected.pixels[size_t(y) * 320 + x] == frame.pixels[size_t(y) * 320 + x],
                              "rejected unavailable sound click preserves both rendered sound cells");
                }
            }
            check(sound.memory.u8(::bmh::layout::Audio_OriginalAudioOptions_ExecutableField) == 0x80 && sound.memory.u8(::bmh::layout::Audio_OriginalAudioOptions_ExecutableField2) == 0x81,
                  "projection, rendering and UI requests do not write Native sound bytes");
        }
    }
    GameState g;
    g.manager_teams = {1};
    g.memory.add_block(::bmh::layout::LeaguePresentationOptions, 180);
    for (uint32_t i = 0; i < 180; ++i) g.memory.set8(::bmh::layout::LeaguePresentationOptions + i, 2);
    g.memory.set8(::bmh::layout::LeaguePresentationOptions, 0);
    g.memory.set8(::bmh::layout::FixtureSettingsFirstCell, 1);
    g.memory.set8(::bmh::layout::Controller_FormatError_ExecutableField2, 0x80);
    g.memory.set8(::bmh::layout::Audio_MatchSoundEnabled_RuntimeField, 0x81);
    g.memory.set8(::bmh::layout::Audio_OriginalAudioOptions_ExecutableField, 0x82);
    g.memory.set8(::bmh::layout::CareerSession_FormatError_RuntimeField, 9);
    g.memory.set8(::bmh::layout::Main_FormatError_ExecutableField, 198); // Column 9 stores twice the displayed value.
    g.memory.set8(::bmh::layout::LeaguePresentationOptions + 0x60, 0); // format field selects the mode-III FNT3 shade path.
    g.memory.set8(::bmh::layout::FixtureSettingsBlankNumericalCell, 200); // A valid blank numerical cell.
    auto state = settings_from_game(g, 0, 14, exe);
    SettingsScreen screen(data, state);
    g.memory.set8(::bmh::layout::LeaguePresentationOptions, 1);
    state.cells.fill(1);
    const auto image = screen.render();
    check(screen.state().cells[0] == 0 && screen.state().cells[3] == 0x80 &&
          screen.state().cells[8] == 198, "raw Original settings and ownership preserved");
    int overlay_transparent = 0, overlay_opaque = 0;
    for (int y = 0; y < 9; ++y) for (int x = 0; x < 23; ++x) {
        check(image.pixels[size_t(24 + y) * 320 + 67 + x] ==
              atlas.pixels[size_t(y) * atlas.width + 115 + x], "A1 Original on sprite and position");
        check(image.pixels[size_t(24 + y) * 320 + 91 + x] ==
              atlas.pixels[size_t(y) * atlas.width + 46 + x], "A2 Original off sprite and position");
        check(image.pixels[size_t(24 + y) * 320 + 115 + x] ==
              atlas.pixels[size_t(y) * atlas.width + x], "A3 Original blank sprite");
        const auto overlay = atlas.pixels[size_t(9 + y) * atlas.width + x];
        overlay_transparent += overlay == 0; overlay_opaque += overlay != 0;
        const auto expected = overlay == 0 ? atlas.pixels[size_t(y) * atlas.width + 115 + x] : overlay;
        check(image.pixels[size_t(24 + y) * 320 + 139 + x] == expected,
              "A4 availability overlay keeps the base sprite under source index zero");
        check(image.pixels[size_t(24 + y) * 320 + 187 + x] ==
              atlas.pixels[size_t(y) * atlas.width + x], "masked value two remains blank");
        check(image.pixels[size_t(34 + y) * 320 + 259 + x] ==
              atlas.pixels[size_t(y) * atlas.width + x], "numeric 200 is blank");
    }
    check(overlay_transparent == 147 && overlay_opaque == 60,
          "capture-verified KB15 overlay has 147 transparent zeros and 60 opaque stipple pixels");
    check(image.pixels[size_t(24) * 320 + 140] == atlas.pixels[116],
          "overlay zero preserves the underlying on-sprite pixel");
    const auto font = load_font(asset_path(data / "F", "BMH3.FNT"));
    check(screen.state().cells[0x60] == 0 && exe.u8(::bmh::layout::TextGradientMode) == 2,
          "Original settings cell gradient_disabled=0 and executable a3648=2 select legacy routine's mode-III text path");
    check(exe.u8(::bmh::layout::Advertising_Text_RuntimeField2 + 28) == 0x5f && exe.u8(::bmh::layout::Advertising_Text_RuntimeField + 28 * 6) == 0x5c &&
          exe.u8(::bmh::layout::Advertising_Text_RuntimeField + 28 * 6 + 5) == 0x5f,
          "Original text-color lookup selects the 5c..5f label gradient");
    check(exe.u8(::bmh::layout::Advertising_Text_RuntimeField2 + 10) == 0xf7 && exe.u8(::bmh::layout::Advertising_Text_RuntimeField + 10 * 6) == 0xf5 &&
          exe.u8(::bmh::layout::Advertising_Text_RuntimeField + 10 * 6 + 5) == 0xfa && exe.u8(::bmh::layout::Advertising_Text_RuntimeField2 + 2) == 0x75 &&
          exe.u8(::bmh::layout::Advertising_Text_RuntimeField + 2 * 6) == 0x77 && exe.u8(::bmh::layout::Advertising_Text_RuntimeField + 2 * 6 + 5) == 0x7c,
          "Original numeric color tokens select the f5..fa and 77..7c gradients");
    auto check_glyph_gradient = [&](char ch, int x, int baseline, uint8_t token,
                                    const char* why) {
        unsigned slot = 0;
        for (; slot < 32 && exe.u8(::bmh::layout::Advertising_Text_RuntimeField2 + slot) != token; ++slot) {}
        const auto& glyph = font.glyph(uint8_t(ch));
        const int top = baseline - font.baseline;
        for (int row = 0; row < font.height; ++row) {
            const auto color = exe.u8(::bmh::layout::Advertising_Text_RuntimeField + slot * 6 + row);
            if (!color) continue;
            for (int col = 0; col < glyph.bit_width; ++col) {
                const int bit = glyph.bit_offset + col;
                if (!(font.bitmap[size_t(row) * font.modulo + bit / 8] & (0x80 >> (bit % 8)))) continue;
                check(image.pixels[size_t(top + row) * 320 + x + col] == color, why);
            }
        }
    };
    for (int row = 0; row < 20; ++row)
        check_glyph_gradient(char('A' + row), 59, 31 + row * 10, 0x5f,
                             "row label uses the six-row Original glyph colors");
    for (int col = 0; col < 9; ++col)
        check_glyph_gradient(char('1' + col), 76 + col * 24, 22, 0x5f,
                             "column label uses the six-row Original glyph colors");
    check_glyph_gradient('9', 74 + 7 * 24 + font.glyph('%').advance, 31, 0x75,
                         "numeric digit uses the Original 77..7c row colors");
    check_glyph_gradient('9', 74 + 8 * 24, 31, 0x75,
                         "numeric first digit uses the Original 77..7c row colors");
    check_glyph_gradient('9', 74 + 8 * 24 + font.glyph('9').advance, 31, 0x75,
                         "numeric second digit uses the Original 77..7c row colors");
    const auto& one = font.glyph('1');
    auto glyph_ink = [&](int col, int row) {
        if (col < 0 || row < 0 || col >= one.bit_width || row >= font.height) return false;
        const int bit = one.bit_offset + col;
        return (font.bitmap[size_t(row) * font.modulo + bit / 8] & (0x80 >> (bit % 8))) != 0;
    };
    bool found_solid_shadow = false;
    for (int row = 0; row < font.height; ++row) for (int col = 0; col < one.bit_width; ++col) {
        if (!glyph_ink(col, row) || glyph_ink(col + 2, row + 2)) continue;
        if (image.pixels[size_t(17 + row + 2) * 320 + 76 + col + 2] == 0x40)
            found_solid_shadow = true;
    }
    check(found_solid_shadow,
          "legacy routine draws a solid 0x40 shadow at an uncovered glyph pixel");
    for (const int y : {188, 201, 214}) {
        check(image.pixels[size_t(y + 10) * 320 + 3] == 0x40 &&
              image.pixels[size_t(y + 11) * 320 + 55] == 0x40 &&
              image.pixels[size_t(y + 2) * 320 + 54] == 0x40,
              "Original settings button positions have the legacy routine bottom/right bevel");
        for (int yy = 0; yy < 10; ++yy) for (int xx = 0; xx < 5; ++xx)
            check(image.pixels[size_t(y + yy) * 320 + 1 + xx] ==
                  button_atlas.pixels[size_t(74 + yy) * button_atlas.width + 24 + xx],
                  "normal button body copies the first KB/0.VGA tile outside centered label");
    }
    check(!screen.click(-1, 26) && !screen.click(319, 239), "out-of-control input is inert");
    check(screen.click(70, 27) && screen.pending() && screen.pending()->row == 0 &&
          screen.pending()->column == 0 && screen.pending()->operation == SettingsEdit::toggle_binary &&
          screen.state().cells[0] == 0 && g.memory.u8(::bmh::layout::LeaguePresentationOptions) == 1,
          "cell click creates a pending owned action without local state mutation");
    check(screen.state_json().find("\"pending\":true") != std::string::npos,
          "pending action is visible in the screen projection");
    screen.reject("Aktion abgelehnt.");
    check(!screen.pending() && screen.state_json().find("Aktion abgelehnt") != std::string::npos,
          "rejection clears pending state and retains feedback");
    const auto before_escape = screen.render().pixels;
    check(!screen.key("ESC") && !screen.close_requested() && screen.render().pixels == before_escape,
          "Source settings ignores ESC and preserves its displayed feedback");
    check(screen.pointer_down(310, 230, true) && screen.close_requested() && !screen.click(70, 27),
          "Source right-button press closes only this view");
    SettingsScreen guarded(data, state);
    check(guarded.click(70, 27) && guarded.pending(), "guarded ACK fixture creates a pending A1 edit");
    auto forged_ack = state;
    forged_ack.revision = state.revision + 1;
    forged_ack.cells[0] = 1;
    forged_ack.cells[179] ^= 1;
    bool unrelated_byte_rejected = false;
    try { guarded.acknowledge(forged_ack); }
    catch (const FormatError&) { unrelated_byte_rejected = true; }
    check(unrelated_byte_rejected && guarded.pending() && guarded.state().revision == state.revision,
          "ACK cannot forge changes to unrelated bytes or consume the pending request");
    auto valid_ack = state;
    valid_ack.revision = state.revision + 1;
    valid_ack.cells[0] = uint8_t(valid_ack.cells[0] ^ 1);
    for (size_t field = 0; field < 6; ++field) {
        auto invalid_text = valid_ack;
        const std::string malformed(1, char(0xc3));
        if (field == 0) invalid_text.title = malformed;
        else if (field == 1) invalid_text.footer = malformed;
        else if (field == 2) invalid_text.version = malformed;
        else invalid_text.button_labels[field - 3] = malformed;
        const auto old_projection = guarded.state_json();
        bool malformed_text_rejected = false;
        try { guarded.acknowledge(std::move(invalid_text)); }
        catch (const FormatError&) { malformed_text_rejected = true; }
        check(malformed_text_rejected && guarded.pending() && guarded.state_json() == old_projection,
              "malformed ACK text preserves the complete owned state and pending request");
    }
    guarded.acknowledge(std::move(valid_ack));
    check(!guarded.pending() && guarded.state().revision == state.revision + 1 &&
          guarded.state().cells[0] == uint8_t(state.cells[0] ^ 1),
          "matching one-cell ACK advances the owned projection exactly once");
    SettingsState max_revision = state;
    max_revision.revision = std::numeric_limits<uint64_t>::max();
    SettingsScreen overflow_screen(data, max_revision);
    check(overflow_screen.click(70, 27) && overflow_screen.pending(), "overflow fixture creates pending action");
    auto overflow_ack = max_revision; overflow_ack.revision = 0; overflow_ack.cells[0] = 1;
    bool overflow_ack_rejected = false;
    try { overflow_screen.acknowledge(std::move(overflow_ack)); }
    catch (const FormatError&) { overflow_ack_rejected = true; }
    check(overflow_ack_rejected && overflow_screen.pending(),
          "UINT64_MAX ACK cannot wrap or consume the pending action");
    SettingsScreen right_close(data, screen.state());
    check(right_close.click(10, 10, true) && right_close.close_requested(), "Original right-button close");
    if (argc == 3 && std::string_view(argv[2]).size()) {
        const auto original = load_savegame(argv[2]);
        check(original.checksum_ok && original.version == "V2.24", "Original reference checksum and version are valid");
        const DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
        const SaveState parsed(original.stream, counts);
        auto loaded = GameState::from_save(parsed, counts);
        const auto texts = load_textd(asset_path(data, "TEXTD.BLK"));
        loaded.texts = &texts;
        const int manager = loaded.memory.u8(::bmh::layout::ActiveManager);
        const auto projection = settings_from_game(loaded, manager, 21, exe);
        SettingsAvailabilityInputs availability{
            loaded.memory.u8(::bmh::layout::GameMode), loaded.memory.u32(::bmh::layout::CompetitionSelection), 1, 1};
        const auto dynamic_cells = project_settings_availability(projection.cells, availability, exe);
        for (uint32_t i = 0; i < 180; ++i)
            check(projection.cells[i] == loaded.memory.u8(::bmh::layout::LeaguePresentationOptions + i), "actual Original save projects every raw setting byte");
        if ((availability.competitions & 2u) == 0)
            for (int row = 9; row < 13; ++row)
                for (int col = 0; col < 4; ++col)
                    check((dynamic_cells[size_t(row * 9 + col)] & 0x80) != 0,
                          "world-disabled Original save projects the J-M columns 1-4 availability gate");
        check(projection.display_cells.has_value() && (*projection.display_cells)[5] == projection.cells[5] &&
              (*projection.display_cells)[6] == projection.cells[6],
              "actual entry projection preserves unknown A6/A7 sound setup flags without guessing SDL capabilities");
        for (size_t i = 0; i < projection.cells.size(); ++i) if (i != 5 && i != 6)
            check((*projection.display_cells)[i] == dynamic_cells[i],
                  "all non-sound availability gates are independent of the unknown sound config latches");
        SettingsScreen reference(data, projection);
        check(reference.render().pixels.size() == 320 * 240, "actual Original save renders through the native settings screen");
        const auto before = reference.state_json();
        (void)reference.render();
        check(reference.state_json() == before, "actual Original save rendering leaves the owned projection unchanged");
    }
    std::cout << "Original settings sprites, raw availability, numeric storage and pending actions passed.\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "ui_settings_tests: " << e.what() << '\n';
    return 1;
}
