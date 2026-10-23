// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/world_layout.hpp"
#include "ui/environment.hpp"

#include <algorithm>
#include <sstream>

#include "formats/savestate.hpp"
#include "game/messages.hpp"
#include "game/state.hpp"
#include "ui/manager.hpp"

namespace bmh::ui {
namespace {
void crop(Image& dst, const Image& src, int sx, int sy, int dx, int dy, int w, int h, int transparent = -1) {
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const int tx = dx + x, ty = dy + y, px = sx + x, py = sy + y;
        if (tx < 0 || ty < 0 || tx >= dst.width || ty >= dst.height ||
            px < 0 || py < 0 || px >= src.width || py >= src.height) continue;
        const uint8_t c = src.pixels[size_t(py) * size_t(src.width) + size_t(px)];
        if (c != transparent) dst.at(tx, ty) = c;
    }
}

int text_width(const LeImage& exe, std::string_view text) {
    int width = 0;
    for (unsigned char c : text) {
        const unsigned glyph = c < 128 ? exe.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + c) : 0;
        width += glyph ? int(exe.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2 + glyph * 2) - exe.u16(::bmh::layout::ClubInfo_Raw_RuntimeField + glyph * 2)) : 2;
    }
    return width;
}

void sprite_text(Image& out, const Image& atlas, const LeImage& exe, int x, int baseline,
                 std::string_view value, int font) {
    for (unsigned char c : value) {
        if (c <= 31 || c >= 128) { x += 2; continue; }
        const int glyph = exe.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + c);
        if (!glyph) { x += 2; continue; }
        const int sx = exe.u16(::bmh::layout::ClubInfo_Raw_RuntimeField + glyph * 2);
        const int w = exe.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2 + glyph * 2) - sx;
        crop(out, atlas, sx, (font - 4) * 7, x, baseline - 6, w, 7, 0);
        x += w;
    }
}

const char* result_name(EnvironmentRequestKind kind) {
    return kind == EnvironmentRequestKind::set_item ? "set_environment" : "none";
}
bool generic_badge(int club) {
    // SourceBadgeFallback gates on the runtime club ID, before looking up its W sprite.
    const unsigned team = unsigned(club) & 0xfff;
    return team == 0xfff || (team >= 90 && team <= 137) || (team >= 168 && team <= 356);
}
} // namespace

EnvironmentView environment_from_game(const bmh::GameState& game, uint64_t revision) {
    const int manager = game.memory.u8(::bmh::layout::ActiveManager);
    if (manager < 0 || size_t(manager) >= game.manager_teams.size())
        throw FormatError("environment: active manager is outside the source roster");
    const int team = game.manager_teams[size_t(manager)];
    if (team < 0 || team == no_team()) throw FormatError("environment: no club environment for this manager");
    EnvironmentView view;
    view.manager_slot = manager;
    view.runtime_club = team;
    view.revision = revision;
    view.club_name = game.club_name(team);
    if (game.club_heap_78.size() < size_t(team + 1) * 2)
        throw FormatError("environment: missing Source club badge mapping");
    view.badge_sprite = (uint16_t(game.club_heap_78[size_t(team) * 2]) |
        uint16_t(game.club_heap_78[size_t(team) * 2 + 1]) << 8) & 0x1ff;
    view.values = bmh::environment_state(game, manager);
    for (int i = 0; i < kEnvironmentItems; ++i)
        view.cost_text[size_t(i)] = bmh::format_number(game, view.values.monthly[size_t(i)], 0x185);
    for (size_t i = 0; i < 3; ++i) {
        view.youth_position_text[i] = format_number(game, view.values.youth_position[i] + 1, 0x81);
        view.youth_league_text[i] = format_number(game, view.values.youth_league[i] + 1, 2) +
            message_text(game, ::bmh::layout::EnvironmentYouthLeagueTextPointer);
    }
    view.decorated_background = !game.memory.has(::bmh::layout::PlainBackground) || game.memory.u8(::bmh::layout::PlainBackground) == 0;
    view.gradient_text = (!game.memory.has(::bmh::layout::TextGradientDisabled) || game.memory.u8(::bmh::layout::TextGradientDisabled) == 0);
    view.solid_panel_background = game.memory.has(::bmh::layout::SolidPanelBackground) && game.memory.u8(::bmh::layout::SolidPanelBackground) != 0;
    view.plain_background = game.memory.has(::bmh::layout::PlainBackground) && game.memory.u8(::bmh::layout::PlainBackground) != 0;
    return view;
}

TaxDialogProjection environment_notice_dialog(const bmh::GameState& game, int manager,
                                               std::string body) {
    if (manager < 0 || size_t(manager) >= game.manager_teams.size())
        throw FormatError("environment notice: invalid manager");
    const size_t record = size_t(manager) * 0xb0c;
    if (record + 0x222 >= game.manager_records.size())
        throw FormatError("environment notice: truncated manager record");
    TaxDialogProjection view;
    view.kind = TaxDialogKind::acknowledgement;
    // Uint8T6 -> SDLFlushEvents3 adds the active manager's 0x40 timed-notice flags.
    view.source_manager_flags = uint8_t(manager) | 0x40;
    view.source_flags = 0;
    view.compact_dialog_frame = game.memory.has(::bmh::layout::CompactDialogFrame) && game.memory.u8(::bmh::layout::CompactDialogFrame) != 0;
    view.body = std::move(body);
    const auto first = game.manager_records.begin() + static_cast<std::ptrdiff_t>(record + 0xa8);
    const auto last = std::find(first, first + 26, uint8_t(0));
    view.manager_name.assign(first, last);
    view.portrait = game.manager_records[record + 0x221];
    // PresentManagerDialog delegates to SourceBadgeFallback: fallback depends on the runtime club,
    // not the W-atlas selector. W/0 sprite 0 is a valid club badge.
    const int team = game.manager_teams[size_t(manager)] & 0xfff;
    view.badge_fallback = generic_badge(team);
    if (!view.badge_fallback) {
        if (game.club_heap_78.size() < size_t(team + 1) * 2)
            throw FormatError("environment notice: truncated club badge table");
        view.badge_sprite = (uint16_t(game.club_heap_78[size_t(team) * 2]) |
            uint16_t(game.club_heap_78[size_t(team) * 2 + 1]) << 8) & 0x1ff;
    }
    return view;
}

std::string environment_view_json(const EnvironmentView& s) {
    std::ostringstream out;
    out << "{\"manager_slot\":" << s.manager_slot << ",\"runtime_club\":" << s.runtime_club
        << ",\"revision\":" << s.revision << ",\"badge_sprite\":" << s.badge_sprite << ",\"monthly\":[";
    for (size_t i = 0; i < s.values.monthly.size(); ++i) { if (i) out << ','; out << s.values.monthly[i]; }
    out << "],\"level\":[";
    for (size_t i = 0; i < s.values.level.size(); ++i) { if (i) out << ','; out << s.values.level[i]; }
    out << "],\"youth_teams\":[";
    for (size_t i = 0; i < 3; ++i) {
        if (i) out << ',';
        out << "{\"strength\":" << int(s.values.youth_strength[i])
            << ",\"position\":" << int(s.values.youth_position[i])
            << ",\"league\":" << int(s.values.youth_league[i]) << '}';
    }
    out << "]}";
    return out.str();
}

std::string environment_request_json(const EnvironmentRequest& r) {
    std::ostringstream out;
    out << "{\"kind\":" << json_quote(result_name(r.kind)) << ",\"manager_slot\":" << r.manager_slot
        << ",\"runtime_club\":" << r.runtime_club << ",\"revision\":" << r.revision
        << ",\"request_id\":" << r.request_id << ",\"item\":" << r.item << ",\"level\":" << r.level
        << ",\"owner\":" << desk_request_json(r.owner) << '}';
    return out.str();
}

EnvironmentScreen::EnvironmentScreen(const std::filesystem::path& data, EnvironmentView state, Image underlay)
    : state_(std::move(state)), exe_(load_le(asset_path(data, "BMH.exe"))),
      texts_(load_textd(asset_path(data, "TEXTD.BLK"))),
      background_(load_vga(asset_path(data / "KB", "129.VGA"))),
      tiles_(load_vga(asset_path(data / "KB", "19.VGA"))), panel_(load_vga(asset_path(data / "KB", "56.VGA"))),
      letters_(load_vga(asset_path(data / "KB", "54.VGA"))),
      badge_atlas_(load_vga(asset_path(data / (generic_badge(state_.runtime_club) ? "KB" : "W"),
          generic_badge(state_.runtime_club) ? "0.VGA" : std::to_string(state_.badge_sprite >> 3) + ".VGA"))),
      font_(load_font(asset_path(data / "F", "BMH3.FNT"))) {
    if (state_.manager_slot < 0 || state_.manager_slot > 3 || state_.runtime_club < 0 ||
        underlay.width != 320 || underlay.height != 240 || background_.width != 320 ||
        background_.height != 240 || tiles_.width != 306 || tiles_.height != 111 ||
        panel_.width <= 0 || texts_.strings.size() != TEXT_COUNT)
        throw FormatError("environment: unsupported owner or Original assets");
}

void EnvironmentScreen::text(Image& out, int x, int baseline, std::string_view value,
                             uint8_t color, bool gradient) const {
    const int y = baseline - font_.baseline;
    if (x < 0 || y < 0 || y + font_.height > out.height ||
        x + font_.text_width(value) > out.width) return;
    if (!gradient || !state_.gradient_text || exe_.u8(::bmh::layout::TextGradientMode) != 2 || color == 0x40) {
        font_.draw(out, x, y, value, color);
        return;
    }
    unsigned slot = 0;
    while (slot < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + slot) != color) ++slot;
    if (slot >= 32) { font_.draw(out, x, y, value, color); return; }
    Image mask(std::max(1, font_.text_width(value)), font_.height, 0);
    font_.draw(mask, 0, 0, value, 1);
    for (int row = 0; row < mask.height; ++row)
        for (int col = 0; col < mask.width; ++col)
            if (mask.at(col, row)) out.at(x + col, y + row) = exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + slot * 6 + std::min(row, 5));
}

Image EnvironmentScreen::render() const {
    // Image -> PlainWindowFrameUsedBy starts a new player background, not the desk's
    // controls. Its null CareerSession_ApplyBalanceSourceStyle_ExecutableField path chooses KB129 or the PlainBackground flat frame.
    Image out = state_.decorated_background && !state_.plain_background ? background_ : Image(320, 240, 0);
    if (!state_.decorated_background || state_.plain_background) {
        for (int y = 4; y <= 16; ++y) for (int x = 0; x < 320; ++x) out.at(x, y) = 0x44;
        for (int y = 5; y <= 15; ++y) for (int x = 1; x <= 318; ++x) out.at(x, y) = 0x39;
        for (int y = 228; y <= 237; ++y) for (int x = 0; x < 320; ++x) out.at(x, y) = 0x44;
        for (int y = 229; y <= 236; ++y) for (int x = 1; x <= 318; ++x) out.at(x, y) = 0x39;
    }
    sprite_text(out, letters_, exe_, 172 + (282 - 172) / 2 - text_width(exe_, texts_[text_id(::bmh::layout::Environment_Render_TextPointer)]) / 2,
                13, texts_[text_id(::bmh::layout::Environment_Render_TextPointer)], 5);
    sprite_text(out, letters_, exe_, 5, 235, state_.club_name, 5);
    // CopyCrop -> UsesForTheseSprites compares DesktopRuntimeField=0; ff is its blit mode.
    crop(out, tiles_, 0, 0, 0x7d, 0x52, 0x47, 0x46, 0);
    // SourceBadgeFallback first adds its PaintsFourBorderEdges frame, then an opaque FormatError2 crop.
    for (int y = 96; y <= 135; ++y) out.at(139, y) = 0x46;
    for (int x = 139; x <= 178; ++x) out.at(x, 96) = 0x46;
    for (int x = 139; x <= 178; ++x) out.at(x, 135) = 0x40;
    for (int y = 96; y <= 135; ++y) out.at(178, y) = 0x40;
    // KB0 has a narrower atlas than W; validate the selected crop, not the
    // full eight-cell W strip, before drawing the generic Source badge.
    const int badge_x = generic_badge(state_.runtime_club) ? 8 : int(state_.badge_sprite & 7) * 38;
    if (badge_atlas_.width < badge_x + 38 || badge_atlas_.height < 38)
        throw FormatError("environment: Original badge crop outside atlas");
    crop(out, badge_atlas_, badge_x, 0, 0x8c, 0x61, 0x26, 0x26);
    for (int item = 0; item < kEnvironmentItems; ++item) {
        const int x = exe_.u8(::bmh::layout::Environment_FormatError_RuntimeField + item), y = exe_.u8(::bmh::layout::Environment_FormatError_RuntimeField2 + item);
        crop(out, tiles_, item * 0x33, 0x46, x, y, 0x33, 0x29);
        // CallsTwiceForUpperLowerCaptionStrips calls LoadGameEndTaxAnimationAssets twice for the upper and lower caption strips.
        // Match its KB/56-backed panel mode and its SolidPanelBackground flat-fill branch.
        const auto panel = [&](int left, int top, int right, int bottom) {
            if (state_.solid_panel_background) {
                for (int yy = top + 1; yy < bottom; ++yy)
                    for (int xx = left + 1; xx < right; ++xx) out.at(xx, yy) = 0x42;
            } else {
                crop(out, panel_, left, top - 19, left + 1, top + 1,
                     right - left - 1, bottom - top - 1);
            }
            for (int xx = left; xx <= right; ++xx) out.at(xx, top) = 0x46;
            for (int yy = top; yy <= bottom; ++yy) out.at(left, yy) = 0x46;
            for (int xx = left; xx <= right; ++xx) out.at(xx, bottom) = 0x40;
            for (int yy = top; yy <= bottom; ++yy) out.at(right, yy) = 0x40;
        };
        panel(x - 4, y - 10, x + 0x36, y - 1);
        panel(x - 4, y + 0x29, x + 0x36, y + 0x31 + (item < 3 ? 6 : 0));
        const int count = state_.values.level[size_t(item)];
        for (int star = 0; star < count; ++star)
            crop(out, tiles_, 0x1e, 0x1e, x + star * 9, y - 8, 7, 6, 0);
        // The tile artwork already contains its Source caption. CallsTwiceForUpperLowerCaptionStrips draws
        // the dynamic expense in the lower strip at baseline y+0x30; a second
        // invented TEXTD caption here overwrote the baked-in names and shifted
        // the cost down into the hit area.
        const auto cost = state_.cost_text[size_t(item)] + exe_.cstring(::bmh::layout::EnvironmentCostSuffix);
        text(out, x + 0x32 / 2 - font_.text_width(cost) / 2, y + 0x30, cost, 0x6b);
        if (item < 3) {
            const auto caption = state_.youth_position_text[size_t(item)] +
                exe_.cstring(::bmh::layout::EnvironmentYouthPositionSuffix) + state_.youth_league_text[size_t(item)];
            text(out, x + 0x32 / 2 - font_.text_width(caption) / 2, y + 0x36,
                caption, exe_.u8(::bmh::layout::EnvironmentYouthColors + item));
        }
    }
    return out;
}

bool EnvironmentScreen::pointer_down(int x, int y, bool right) {
    if (pending_) return false;
    press_seen_ = true;
    pressed_item_ = pressed_level_ = -1;
    pressed_right_ = right;
    if (right) return true;
    for (int item = 0; item < kEnvironmentItems; ++item) {
        const int left = exe_.u8(::bmh::layout::Environment_FormatError_RuntimeField + item), top = exe_.u8(::bmh::layout::Environment_FormatError_RuntimeField2 + item);
        if (x >= left - 9 && x <= left + 0x36 && y >= top - 10 && y <= top + 0x29) {
            pressed_item_ = item;
            pressed_level_ = std::clamp((x - left + 9) / 9, 0, 6);
            return true;
        }
    }
    return false;
}

EnvironmentRequest EnvironmentScreen::release(int x, int y, bool right) {
    const bool had_press = press_seen_;
    const bool was_right = pressed_right_;
    press_seen_ = false;
    if (had_press && was_right && right) { close_requested_ = true; pressed_right_ = false; return {}; }
    if (!had_press || was_right || right || pressed_item_ < 0) { pressed_right_ = false; pressed_item_ = -1; return {}; }
    const int item = pressed_item_, level = pressed_level_;
    pressed_item_ = pressed_level_ = -1;
    const int left = exe_.u8(::bmh::layout::Environment_FormatError_RuntimeField + item), top = exe_.u8(::bmh::layout::Environment_FormatError_RuntimeField2 + item);
    if (x < left - 9 || x > left + 0x36 || y < top - 10 || y > top + 0x29) return {};
    EnvironmentRequest r{EnvironmentRequestKind::set_item, state_.manager_slot, state_.runtime_club,
                         item, level, state_.revision, next_request_id_++};
    r.owner = state_.owner;
    pending_ = r;
    return r;
}

EnvironmentRequest EnvironmentScreen::click(int x, int y, bool right) {
    if (!press_seen_) pointer_down(x, y, right);
    return release(x, y, right);
}

EnvironmentRequest EnvironmentScreen::key(std::string_view key) {
    // Keep an issued level change or a physical press with its own ACK/release.
    if (pending_ || press_seen_) return {};
    if (key == "ESC" || key == "Escape") close_requested_ = true;
    return {};
}

void EnvironmentScreen::acknowledge(EnvironmentView updated) {
    const bool unchanged = pending_ && updated.revision == state_.revision &&
        environment_view_json(updated) == environment_view_json(state_);
    if (!pending_ || pending_->manager_slot != updated.manager_slot || pending_->runtime_club != updated.runtime_club ||
        pending_->revision != state_.revision || (!unchanged && updated.revision != state_.revision + 1) ||
        updated.owner.manager_slot != state_.owner.manager_slot || updated.owner.menu_kind != 24 ||
        updated.owner.revision != updated.revision)
        throw FormatError("environment: acknowledgement does not match the pending action");
    state_ = std::move(updated);
    pending_.reset();
    feedback_.clear();
}

void EnvironmentScreen::reject_action(std::string reason) {
    if (!pending_) throw FormatError("environment: rejection without a pending action");
    pending_.reset(); feedback_ = std::move(reason);
}

std::string EnvironmentScreen::state_json() const {
    return "{\"screen\":\"environment\",\"state\":" + environment_view_json(state_) +
        ",\"close_requested\":" + (close_requested_ ? "true" : "false") +
        ",\"pending\":" + (pending_ ? environment_request_json(*pending_) : "null") + "}";
}

} // namespace bmh::ui
