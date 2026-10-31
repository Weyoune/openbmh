// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/settings.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>

#include "game/messages.hpp"
#include "ui/manager.hpp"
#include "ui/setup.hpp"

namespace bmh::ui {
namespace {
void blit_cell(Image& out, const Image& atlas, int sx, int sy, int x, int y,
               bool overlay = false) {
    if (sx < 0 || sy < 0 || sx + 23 > atlas.width || sy + 9 > atlas.height)
        throw FormatError("settings: cell outside Original atlas");
    for (int yy = 0; yy < 9; ++yy) for (int xx = 0; xx < 23; ++xx) {
        const auto color = atlas.pixels[size_t(sy + yy) * atlas.width + sx + xx];
        // UsesForTheseSprites compares against active DesktopRuntimeField=0 in this path.
        // Its 0xff argument is a blit mask, not the indexed software compare key.
        if (!overlay || color != 0) out.at(x + xx, y + yy) = color;
    }
}

void blit_button_strip(Image& out, const Image& atlas, int x, int y) {
    // FormatError3 normal button path (w=53,h=10): first 31 pixels from
    // KB/0.VGA (24,74), a 2-pixel middle tile from (24,84), then 20 pixels
    // from (25,94). PaletteOverride(0,0,0,0,1) supplies this Original atlas.
    auto copy = [&](int sx, int sy, int width, int dx) {
        for (int yy = 0; yy < 10; ++yy) for (int xx = 0; xx < width; ++xx) {
            const auto color = atlas.pixels[size_t(sy + yy) * size_t(atlas.width) + size_t(sx + xx)];
            if (dx + xx >= 0 && dx + xx < out.width && y + yy >= 0 && y + yy < out.height)
                out.at(dx + xx, y + yy) = color;
        }
    };
    copy(24, 74, 31, x);
    copy(24, 84, 2, x + 31);
    copy(25, 94, 20, x + 33);
}

std::string number(int value) {
    // Replace(..., 2): the Original percent glyph pads a two-digit field.
    auto s = std::to_string(value);
    if (s.size() < 2) s.insert(s.begin(), '%');
    return s;
}
}

std::array<uint8_t, 180> project_settings_availability(
    const std::array<uint8_t, 180>& raw_cells, const SettingsAvailabilityInputs& in,
    const LeImage& exe) {
    auto cells = raw_cells;
    auto cell = [&](int row, int col) -> uint8_t& { return cells.at(size_t(row * 9 + col)); };
    auto lock = [&](int row, int col) { cell(row, col) |= 0x80; };

    // ProjectSettingsAvailability first strips the prior dynamic lock bit from columns 1–7.
    for (int row = 0; row < 20; ++row)
        for (int col = 0; col < 7; ++col)
            cell(row, col) &= 0x7f;

    if (in.music_configuration_locked != 0) lock(0, 5);  // Audio_OriginalAudioOptions_ExecutableField
    if (in.effects_configuration_locked != 0) lock(0, 6);  // Audio_OriginalAudioOptions_ExecutableField2

    bool all_column7_rows4to8_nonzero = true;
    for (int row = 3; row < 8; ++row)
        if (cell(row, 6) == 0) all_column7_rows4to8_nonzero = false;
    if (all_column7_rows4to8_nonzero) lock(2, 6);  // C7, Controller_FormatError_RuntimeField2

    bool all_column4_nonzero = true;
    for (int row = 0; row < 20; ++row)
        if (cell(row, 3) == 0) all_column4_nonzero = false;
    if (all_column4_nonzero)
        for (int row = 2; row < 8; ++row) lock(row, 6);  // C7-H7, Audio_OriginalAudioOptions_ExecutableField2 + row*9

    if (in.mode != 3) lock(1, 4);  // B5, CareerSession_DispatchLiveTactic_RuntimeField3
    if (in.mode < 3) {
        lock(16, 4);  // Q5, CalendarState_FormatError_RuntimeField
        lock(17, 4);  // R5, CalendarState_FormatError_RuntimeField2
    }
    if (in.mode == 1) lock(15, 4);  // P5, MatchdaySetup_WinterPostponements_RuntimeField2

    if ((in.competitions & 1u) == 0) {
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 20; ++row)
                if ((row < 9 || row > 12) && row != 15) lock(row, col);
        lock(6, 6);  // G7, SportschauAwards_FinishSportschauAwards_RuntimeField
    }
    if ((in.competitions & 2u) == 0)
        for (int col = 0; col < 4; ++col)
            for (int row = 9; row < 13; ++row) lock(row, col);

    // Original link tables are in BMH.exe. Each trigger coordinate is tested
    // for raw value exactly 1; its [start,end) dependent coordinates get bit 7.
    for (uint32_t trigger = 0; trigger < 7; ++trigger) {
        const uint8_t encoded = exe.u8(::bmh::layout::Settings_ProjectSettingsAvailability_RuntimeField + trigger);
        const int col = encoded & 7;
        const int row = encoded >> 3;
        if (cell(row, col) != 1) continue;
        const uint8_t begin = exe.u8(::bmh::layout::Settings_ProjectSettingsAvailability_RuntimeField2 + trigger);
        const uint8_t end = exe.u8(::bmh::layout::Settings_ProjectSettingsAvailability_RuntimeField3 + trigger);
        for (uint32_t i = begin; i < end; ++i) {
            const uint8_t dependent = exe.u8(::bmh::layout::Settings_ProjectSettingsAvailability_RuntimeField4 + i);
            lock(dependent >> 3, dependent & 7);
        }
    }
    return cells;
}

SettingsState settings_from_game(const GameState& g, int manager, uint64_t revision,
                                 const LeImage& exe, std::optional<SettingsSoundCapabilities> sound) {
    if (manager < 0 || manager > 3 || manager >= int(g.manager_teams.size()))
        throw FormatError("settings: invalid manager");
    SettingsState state;
    state.manager_slot = manager;
    state.revision = revision;
    state.audio_config_known = sound.has_value();
    for (size_t i = 0; i < state.cells.size(); ++i) {
        const uint32_t address = ::bmh::layout::LeaguePresentationOptions + uint32_t(i);
        if (!g.memory.has(address)) throw FormatError("settings: missing switch block");
        state.cells[i] = g.memory.u8(address);
    }
    if (g.memory.has(::bmh::layout::GameMode) && g.memory.has(::bmh::layout::CompetitionSelection) && g.memory.has(::bmh::layout::Settings_FormatError_RuntimeField)) {
        auto display = project_settings_availability(state.cells,
            {g.memory.u8(::bmh::layout::GameMode), g.memory.u32(::bmh::layout::CompetitionSelection),
             uint8_t(sound && !sound->music), uint8_t(sound && !sound->effects)}, exe);
        // A6/A7 depend on runtime sound setup. Preserve raw flags for headless
        // callers without a host capability provider. None of the seven
        // Original link-table triggers references either cell, so every other
        // availability result is independent of these two unknown inputs.
        if (!sound) { display[5] = state.cells[5]; display[6] = state.cells[6]; }
        state.display_cells = std::move(display);
    }
    state.decorated_background = !g.memory.has(::bmh::layout::PlainBackground) || g.memory.u8(::bmh::layout::PlainBackground) == 0;
    state.title = original_to_utf8(message_text(g, ::bmh::layout::Settings_FormatError_TextPointer3));
    state.footer = original_to_utf8(message_text(g, ::bmh::layout::Settings_FormatError_TextPointer4));
    state.version = original_to_utf8(message_text(g, ::bmh::layout::Settings_FormatError_TextPointer5) + exe.string_at_pointer(::bmh::layout::Settings_FormatError_TextPointer));
    for (size_t i = 0; i < 3; ++i)
        state.button_labels[i] = original_to_utf8(message_text(g, ::bmh::layout::Settings_FormatError_TextPointer2 + uint32_t(i) * 4));
    return state;
}

SettingsScreen::SettingsScreen(const std::filesystem::path& data, SettingsState state, int origin_item_id)
    : state_(std::move(state)), origin_item_id_(origin_item_id),
      background_(load_vga(asset_path(data / "KB", "129.VGA"))),
      atlas_(load_vga(asset_path(data / "KB", "15.VGA"))),
      button_atlas_(load_vga(asset_path(data / "KB", "0.VGA"))),
      font_(load_font(asset_path(data / "F", "BMH3.FNT"))),
      exe_(load_le(asset_path(data, "BMH.exe"))), sprite_font_(data) {
    if (state_.manager_slot < 0 || state_.manager_slot > 3 ||
        background_.width != 320 || background_.height != 240 ||
        atlas_.width != 138 || atlas_.height != 18 ||
        button_atlas_.width != 77 || button_atlas_.height != 112)
        throw FormatError("settings: invalid state or Original asset dimensions");
    for (const auto& s : {state_.title, state_.footer, state_.version}) (void)utf8_to_original(s);
    for (const auto& s : state_.button_labels) (void)utf8_to_original(s);
}

void SettingsScreen::text(Image& out, int x, int baseline, std::string_view original,
                          uint8_t color, int right) const {
    std::string bytes(original);
    while (!bytes.empty() && font_.text_width(bytes) > right - x) bytes.pop_back();
    const int y = baseline - font_.baseline;
    if (x < 0 || y < 0 || y + font_.height > out.height) return;

    // The original indexed-VGA settings screen uses Mask's six-row
    // FNT3 color table when settings cell TextGradientDisabled is zero and TextGradientMode has its
    // normal value 2. The shadow call from DrawnFirstBy is drawn first by
    // SolidColorMustBypassThisForegroundMap with solid color 0x40 and must bypass this foreground map.
    // TextWidth2 maps the incoming palette token through Advertising_Text_RuntimeField2; U83
    // then paints each set FNT bit with that row's Advertising_Text_RuntimeField color. Zero remains
    // transparent in the scratch bitmap copied by CopyCrop.
    const auto gradient_disabled = state_.display_cells
        ? (*state_.display_cells)[::bmh::layout::TextGradientDisabled - ::bmh::layout::LeaguePresentationOptions]
        : state_.cells[::bmh::layout::TextGradientDisabled - ::bmh::layout::LeaguePresentationOptions];
    if (gradient_disabled != 0 || exe_.u8(::bmh::layout::TextGradientMode) != 2 || color == 0x40) {
        font_.draw(out, x, y, bytes, color);
        return;
    }
    unsigned slot = 0;
    for (; slot < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + slot) != color; ++slot) {}
    int pen_x = x;
    for (unsigned char ch : bytes) {
        const auto& glyph = font_.glyph(ch);
        for (int row = 0; row < font_.height; ++row) {
            const uint8_t row_color = exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + slot * 6 + row);
            if (row_color == 0) continue;
            for (int col = 0; col < glyph.bit_width; ++col) {
                const int bit = glyph.bit_offset + col;
                if (!(font_.bitmap[size_t(row) * font_.modulo + bit / 8] & (0x80 >> (bit % 8)))) continue;
                const int px = pen_x + col;
                if (px >= 0 && px < out.width) out.at(px, y + row) = row_color;
            }
        }
        pen_x += glyph.advance;
    }
}

Image SettingsScreen::render() const {
    auto out = state_.decorated_background ? background_ : Image(320, 240);
    if (!state_.decorated_background) {
        for (int y = 4; y <= 16; ++y) for (int x = 0; x < 320; ++x) out.at(x, y) = 0x44;
        for (int y = 5; y <= 15; ++y) for (int x = 1; x < 319; ++x) out.at(x, y) = 0x39;
        for (int y = 228; y <= 237; ++y) for (int x = 0; x < 320; ++x) out.at(x, y) = 0x44;
        for (int y = 229; y <= 236; ++y) for (int x = 1; x < 319; ++x) out.at(x, y) = 0x39;
    }
    sprite_font_.draw(out, SpriteFont::centered_title_x(sprite_font_.text_width(state_.title)), 13, state_.title);
    sprite_font_.draw(out, 5, 235, state_.footer);
    text(out, 2, 41, utf8_to_original(state_.version), 0x5f, 58);
    for (int col = 0; col < 9; ++col) {
        const auto label = std::to_string(col + 1);
        // DrawnFirstBy uses x directly when right=-1 and shadow=0x40.
        text(out, 78 + col * 24, 24, label, 0x40);
        text(out, 76 + col * 24, 22, label, 0x5f);
        for (int row = 0; row < 20; ++row) {
            const int x = 67 + col * 24, y = 24 + row * 10;
            if (col == 0) {
                const std::string letter(1, char('A' + row));
                text(out, 61, y + 9, letter, 0x40);
                text(out, 59, y + 7, letter, 0x5f);
            }
            const uint8_t raw = state_.display_cells ? (*state_.display_cells)[size_t(row * 9 + col)] :
                                                      state_.cells[size_t(row * 9 + col)];
            const uint8_t value = col < 7 ? raw & 0x3f : raw;
            const bool unavailable = col < 7 && (raw & 0xc0) && value != 2;
            // BlitCell selects KB/15 x=0 blank, x=46 off, x=115 on;
            // numeric values >=200 denote a blank cell, not malformed input.
            const int source = col < 7 ? value == 2 ? 0 : value == 1 ? 46 : 115 : raw < 200 ? 23 : 0;
            blit_cell(out, atlas_, source, 0, x, y);
            if (unavailable) blit_cell(out, atlas_, 0, 9, x, y, true);
            if (col >= 7 && raw < 200) {
                const auto label = number(col == 8 ? raw / 2 : raw);
                text(out, 74 + col * 24, y + 7, label, exe_.u8(::bmh::layout::Settings_Letter_TextPointer + (raw != 0)), x + 23);
            }
        }
    }
    blit_cell(out, atlas_, 0, 0, 259, 214);
    for (size_t i = 0; i < 3; ++i) {
        // SettingsSourceRefusal calls FormatError3 at x=1, y=0xbc+i*0xd, w=0x35,
        // h=10. The fill tiles KB/0.VGA; the normal (param8=0) outline is
        // ColorBottomRightBevel's color-0x40 bottom/right bevel.
        const int y = 188 + int(i) * 13;
        blit_button_strip(out, button_atlas_, 1, y);
        for (int yy = y + 2; yy <= y + 11; ++yy) {
            out.at(54, yy) = 0x40;
            out.at(55, yy) = 0x40;
        }
        for (int x = 3; x <= 55; ++x) {
            out.at(x, y + 10) = 0x40;
            out.at(x, y + 11) = 0x40;
        }
        const int width = sprite_font_.text_width(state_.button_labels[i]);
        sprite_font_.draw(out, 1 + std::max(0, (53 - width) / 2), y + 7,
                          state_.button_labels[i], 6);
    }
    if (!feedback_.empty()) text(out, 5, 235, utf8_to_original(feedback_), 0x5f, 318);
    return out;
}

bool SettingsScreen::click(int x, int y, bool right) {
    if (closed_ || pending_ || pending_file_ || x < 0 || y < 0 || x >= 320 || y >= 240) return false;
    if (right) { closed_ = true; return true; }
    // SettingsSourceRefusal's three Original controls overlay the lower settings rows:
    // Read me (index 0), load EIN (1), save EIN (2).
    if (x >= 1 && x < 55 && y >= 188 && y < 224) {
        const int index = (y - 188) / 13;
        if (index == 1 || index == 2) {
            pending_file_ = SaveLoadRequest{index == 1 ? SaveLoadAction::load : SaveLoadAction::save,
                state_.manager_slot, state_.revision, {}, false, SaveLoadFileKind::settings, origin_item_id_};
            feedback_.clear();
            return true;
        }
        readme_requested_ = true;
        feedback_.clear();
        return true;
    }
    if (x >= 67 && x <= 281 && y >= 26 && y <= 224) {
        const uint8_t column = uint8_t((x - 67) / 24);
        const uint8_t row = uint8_t((y - 25) / 10);
        pending_ = SettingsWriteRequest{state_.manager_slot, state_.revision, state_.cells, row, column,
            column < 7 ? SettingsEdit::toggle_binary : (x - (67 + int(column) * 24) < 11 ?
                SettingsEdit::increment : SettingsEdit::decrement)};
        feedback_.clear();
        return true;
    }
    return false;
}

SettingsReadmeScreen::SettingsReadmeScreen(const std::filesystem::path& data,
                                           bool decorated_background)
    : background_(decorated_background ? load_vga(asset_path(data / "KB", "129.VGA")) : Image(320, 240)),
      font_(load_font(asset_path(data / "F", "BMH3.FNT"))),
      exe_(load_le(asset_path(data, "BMH.exe"))),
      texts_(load_textd(asset_path(data, "TEXTD.BLK"))),
      decorated_background_(decorated_background) {
    if (background_.width != 320 || background_.height != 240)
        throw FormatError("settings readme: invalid Original background dimensions");

    // Use the same TEXTD filename that SourceName appends to SettingsRuntimeField.
    // DOS resolves it case-insensitively; also try its uppercase spelling on
    // case-sensitive host filesystems.
    const std::filesystem::path source_name(texts_[839]);
    std::ifstream file(data / source_name, std::ios::binary);
    if (!file) {
        auto uppercase_name = source_name.string();
        std::transform(uppercase_name.begin(), uppercase_name.end(), uppercase_name.begin(),
            [](unsigned char c) { return char(std::toupper(c)); });
        file.open(data / uppercase_name, std::ios::binary);
    }
    if (!file) return;
    file_available_ = true;
    std::array<char, 20000> bytes{};
    file.read(bytes.data(), std::streamsize(bytes.size()));
    content_.assign(bytes.data(), size_t(file.gcount()));
    pages_.emplace_back();
    int x = 0, baseline = 19;
    bool emphasized = false, new_page_before_next_byte = false;
    const auto source_map = exe_.string_at_pointer(::bmh::layout::Settings_File_ExecutableField);
    // Startup assigns SettingsRuntimeField2 to this EXE string, but load_le() does not
    // execute that initializer; read the proven target map directly.
    const auto target_map = exe_.cstring(::bmh::layout::Settings_File_ExecutableField2);
    for (size_t i = 0; i < content_.size(); ++i) {
        if (new_page_before_next_byte) {
            pages_.emplace_back();
            x = 0; baseline = 19;
            new_page_before_next_byte = false;
        }
        const unsigned char ch = static_cast<unsigned char>(content_[i]);
        if (ch == '\n') {
            x = 0; baseline += 8;
            if (baseline > 230) new_page_before_next_byte = true;
            continue;
        }
        if (ch == '\t' && x < 0x11d) {
            x = (x / 20 + 1) * 20;
            continue;
        }
        if (ch == '#') {
            if (i + 1 >= content_.size()) break;
            const char code = content_[i + 1];
            if (code == '0') emphasized = false;
            else if (code == '1') emphasized = true;
            else break; // Source treats an unrecognized directive as end-of-text.
            ++i;
            continue;
        }
        // Source wraps a completed glyph before it would place another glyph
        // beyond x=0x136. This is normally reached by a full-width glyph.
        if (x > 0x136) {
            x = 0; baseline += 8;
            if (baseline > 230) {
                new_page_before_next_byte = true;
                continue;
            }
        }
        char mapped = char(ch);
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9'))) {
            const auto index = source_map.find(mapped);
            if (index != std::string::npos && index < target_map.size()) mapped = target_map[index];
        }
        const int advance = font_.glyph(uint8_t(mapped)).advance;
        // SettingsReadmeScreen measures the translated byte after Back.
        pages_.back().push_back({x, baseline, uint8_t(mapped), emphasized});
        x += advance;
        if (x > 0x136) {
            x = 0; baseline += 8;
            if (baseline > 230) new_page_before_next_byte = true;
        }
    }
    if (pages_.empty()) pages_.emplace_back();
}

Image SettingsReadmeScreen::render() const {
    Image out = background_;
    if (!decorated_background_) {
        // PlainWindowFrameUsedBy's plain-window frame used by Fill4.
        std::fill(out.pixels.begin(), out.pixels.end(), uint8_t(0));
        auto rect = [&](int x1, int y1, int x2, int y2, uint8_t color) {
            for (int y = std::max(0, y1); y <= std::min(out.height - 1, y2); ++y)
                for (int x = std::max(0, x1); x <= std::min(out.width - 1, x2); ++x)
                    out.at(x, y) = color;
        };
        rect(0, 4, 319, 16, 0x44); rect(0, 228, 319, 237, 0x44);
        rect(1, 5, 318, 15, 0x39); rect(1, 229, 318, 236, 0x39);
    }
    auto draw = [&](int x, int baseline, std::string_view value, uint8_t color) {
        font_.draw(out, x, baseline - font_.baseline, value, color);
    };
    if (!file_available_) {
        const std::string missing = exe_.u8(::bmh::layout::LeagueSizes) == 20 ? "No readme file is available." : exe_.cstring(::bmh::layout::MissingInfofileText);
        draw((out.width - font_.text_width(missing)) / 2, 100, missing, 0x5f);
    } else {
        // TEXTD#840 (`SettingsRuntimeField3`) is the complete source title. SettingsReadmeScreen
        // centers it in DrawnFirstBy's 0..0x13f horizontal bounds, then draws
        // a one-pixel separator.
        draw((out.width - font_.text_width(texts_[840])) / 2, 7, texts_[840], 0x5f);
        for (int x = 0; x < 320; ++x) out.at(x, 9) = 0x5f;
        if (page_index_ < pages_.size()) for (const auto& glyph : pages_[page_index_]) {
            const std::string text(1, char(glyph.character));
            if (glyph.emphasized) draw(glyph.x + 1, glyph.baseline + 1, text, 0xf3);
            draw(glyph.x, glyph.baseline, text, 0x5f);
        }
    }
    draw(0x113, 0xee, texts_[841], 0x5f); // PointerDown: TEXTD#841, `KLICK!`.
    return out;
}

bool SettingsReadmeScreen::pointer_down(bool left_or_right) {
    if (close_requested_ || !left_or_right) return false;
    if (file_available_ && page_index_ + 1 < pages_.size()) ++page_index_;
    else close_requested_ = true;
    return true;
}

bool SettingsScreen::pointer_down(int x, int y, bool right) {
    if (closed_ || pending_ || pending_file_) return false;
    // SettingsSourceRefusal cell edits and right-click return act on press; its three
    // footer controls are handled on release by ExpireQuestion.
    if (right || (x >= 67 && x <= 281 && y >= 26 && y <= 224)) {
        const bool changed = click(x, y, right);
        if (changed) release_barrier_ = true;
        return changed;
    }
    return false;
}

std::optional<uint32_t> settings_source_refusal(const GameState& state,
        const SettingsWriteRequest& request, uint64_t revision) {
    if (request.revision != revision || request.manager_slot < 0 || request.manager_slot > 3 ||
        size_t(request.manager_slot) >= state.manager_teams.size() ||
        request.operation != SettingsEdit::toggle_binary || request.column != 4 ||
        (request.row != 16 && request.row != 17) ||
        !state.memory.has(::bmh::layout::GameMode) || state.memory.u8(::bmh::layout::GameMode) < 3) return {};
    for (size_t i = 0; i < request.expected_raw.size(); ++i) {
        const uint32_t address = ::bmh::layout::LeaguePresentationOptions + uint32_t(i);
        if (!state.memory.has(address) || state.memory.u8(address) != request.expected_raw[i]) return {};
    }
    const uint8_t raw = request.expected_raw[size_t(request.row) * 9 + request.column];
    if (raw >= 2) return {}; // Source availability locks cannot create a rule notice.
    if (request.row == 16 && state.season_day() >= 110 && state.season_day() <= 230)
        return ::bmh::layout::CareerSession_DispatchSettings_TextPointer;
    if (request.row == 17 && raw != 0 && state.league_rounds.size() > 5 && state.league_rounds[5] != 0)
        return ::bmh::layout::CareerSession_DispatchSettings_TextPointer2;
    return {};
}

bool SettingsScreen::key(std::string_view) {
    // SettingsSourceRefusal returns on mouse event 8 (right-button press). Keyboard input
    // does not close the settings page, including when embedded in a match.
    return false;
}

void SettingsScreen::acknowledge(SettingsState state) {
    if (!pending_ || state.manager_slot != pending_->manager_slot ||
        pending_->revision == std::numeric_limits<uint64_t>::max() ||
        state.revision != pending_->revision + 1)
        throw FormatError("settings acknowledgement does not match pending action");
    const size_t index = size_t(pending_->row) * 9 + pending_->column;
    if (pending_->row >= 20 || pending_->column >= 9 || index >= state.cells.size())
        throw FormatError("settings acknowledgement has invalid cell identity");
    for (size_t i = 0; i < state.cells.size(); ++i) {
        const bool numeric_column = (i % 9) >= 7;
        const uint8_t mask = numeric_column ? 0xff : 0x7f;
        if (i != index && ((state.cells[i] ^ pending_->expected_raw[i]) & mask) != 0)
            throw FormatError("settings acknowledgement changed unrelated cells");
    }
    const uint8_t old = pending_->expected_raw[index];
    const uint8_t actual = state.cells[index];
    uint8_t expected = old;
    if (pending_->operation == SettingsEdit::toggle_binary && (old & 0x7f) <= 1)
        expected = uint8_t((old & 0x7f) ^ 1);
    else if (pending_->column == 8) {
        // SettingsSourceRefusal edits the delay column in two raw units. The ACK must
        // validate the same edit as the Session, including odd saved values.
        if (pending_->operation == SettingsEdit::increment && old < 198)
            expected = uint8_t(old + 2);
        else if (pending_->operation == SettingsEdit::decrement && old > 1 && old < 200)
            expected = uint8_t(old - 2);
    }
    else if (pending_->operation == SettingsEdit::increment && old < 200) expected = uint8_t(old + 1);
    else if (pending_->operation == SettingsEdit::decrement && old > 0 && old < 200) expected = uint8_t(old - 1);
    if (((actual ^ expected) & (pending_->column < 7 ? 0x7f : 0xff)) != 0)
        throw FormatError("settings acknowledgement does not match requested edit");
    // Validate the incoming projection before consuming the pending action.
    // Flow acknowledgements copy this screen and bypass its constructor.
    for (const auto& text : {state.title, state.footer, state.version}) (void)utf8_to_original(text);
    for (const auto& text : state.button_labels) (void)utf8_to_original(text);
    pending_.reset();
    state_ = std::move(state);
    feedback_.clear();
}

void SettingsScreen::acknowledge_file(SettingsState state) {
    if (state.manager_slot != state_.manager_slot || state.revision != state_.revision + 1)
        throw FormatError("settings file acknowledgement does not match the pending request");
    for (const auto& text : {state.title, state.footer, state.version}) (void)utf8_to_original(text);
    for (const auto& text : state.button_labels) (void)utf8_to_original(text);
    pending_file_.reset();
    state_ = std::move(state);
    feedback_.clear();
}

void SettingsScreen::reject_file(std::string reason) {
    if (reason.empty()) throw FormatError("settings file rejection requires a reason");
    pending_file_.reset();
    feedback_ = std::move(reason);
}

void SettingsScreen::reject(std::string reason) {
    if (!pending_) throw FormatError("settings rejection without pending action");
    pending_.reset();
    feedback_ = std::move(reason);
}

void SettingsScreen::reject_close(std::string reason) {
    if (!closed_ || reason.empty()) throw FormatError("settings close rejection without close request");
    closed_ = false;
    feedback_ = std::move(reason);
}

std::string SettingsScreen::state_json() const {
    std::ostringstream out;
    out << "{\"screen\":\"settings\",\"read_only\":false,\"manager_slot\":" << state_.manager_slot
        << ",\"revision\":" << state_.revision << ",\"close_requested\":" << (closed_ ? "true" : "false")
        << ",\"pending\":" << (pending_ ? "true" : "false")
        << ",\"availability\":" << json_quote(state_.display_cells ?
            (state_.audio_config_known ? "entry_projection_sound_known" : "entry_projection_sound_unknown") : "raw_snapshot")
        << ",\"feedback\":" << json_quote(feedback_) << ",\"cells\":[";
    for (size_t i = 0; i < state_.cells.size(); ++i) { if (i) out << ','; out << unsigned(state_.cells[i]); }
    return out.str() + "]}";
}

} // namespace bmh::ui
