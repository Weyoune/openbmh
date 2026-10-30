// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/desktop.hpp"

#include <algorithm>
#include <limits>
#include <sstream>

#include "ui/manager.hpp"
#include "game/state.hpp"
#include "game/messages.hpp"

namespace bmh::ui {
namespace {
void rect(Image& out, int x, int y, int w, int h, uint8_t color) {
    for (int yy = std::max(0, y); yy < std::min(out.height, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(out.width, x + w); ++xx) out.at(xx, yy) = color;
}
void copy(Image& out, const Image& src, int sx, int sy, int x, int y, int w, int h, bool transparent = false) {
    if (sx < 0 || sy < 0 || sx + w > src.width || sy + h > src.height)
        throw FormatError("desktop: sprite outside original atlas");
    for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx) {
        const auto color = src.pixels[size_t(sy + yy) * src.width + sx + xx];
        if (x + xx >= 0 && y + yy >= 0 && x + xx < out.width && y + yy < out.height && (!transparent || color))
            out.at(x + xx, y + yy) = color;
    }
}
void border(Image& out, int x, int y) {
    rect(out, x - 1, y - 1, 40, 1, 0x46); rect(out, x - 1, y - 1, 1, 40, 0x46);
    rect(out, x - 1, y + 38, 40, 1, 0x40); rect(out, x + 38, y - 1, 1, 40, 0x40);
}
std::string money(int64_t value) {
    const bool negative = value < 0;
    auto digits = std::to_string(negative ? -value : value);
    for (int at = int(digits.size()) - 3; at > 0; at -= 3) digits.insert(size_t(at), ".");
    const int separators = int(std::count(digits.begin(), digits.end(), '.'));
    const int padding = std::max(0, 8 + separators - int(negative) - int(digits.size()));
    digits.insert(0, size_t(padding), '%');
    // FormatGroupedDigits(0x88): percent glyphs pad a field; remaining groups add spaces.
    digits.insert(0, size_t(std::max(0, 2 - separators)), ' ');
    if (negative) digits.insert(0, "-");
    return digits;
}
const char* request_name(DeskRequestKind kind) {
    switch (kind) {
    case DeskRequestKind::none: return "none";
    case DeskRequestKind::open_menu: return "open_menu";
    case DeskRequestKind::advance_turn: return "advance_turn";
    case DeskRequestKind::change_layout: return "change_layout";
    case DeskRequestKind::change_option: return "change_option";
    case DeskRequestKind::change_ticket_price: return "change_ticket_price";
    case DeskRequestKind::end_session: return "end_session";
    }
    return "none";
}
const char* layout_operation_name(DeskLayoutOperation operation) {
    switch(operation) {
    case DeskLayoutOperation::none:return "none";
    case DeskLayoutOperation::move_item:return "move_item";
    case DeskLayoutOperation::set_folder:return "set_folder";
    case DeskLayoutOperation::toggle_quickbar_drawer:return "toggle_quickbar_drawer";
    case DeskLayoutOperation::return_folder_child:return "return_folder_child";
    case DeskLayoutOperation::send_to_folder:return "send_to_folder";
    case DeskLayoutOperation::restore_folder_child:return "restore_folder_child";
    }
    return "none";
}
} // namespace

std::string_view desk_menu_name(int kind) {
    // These menu kinds have original hotkey + screen observations in the laboratory.
    switch (kind) {
    case 0: return "league_table";
    case 1: return "league_pairings";
    case 53: return "menu_layout";
    case 63: return "mailbox";
    case 19: return "settings";
    case 20: return "top_scorers";
    case 25: return "fair_play";
    case 26: return "highscore";
    case 23: return "opportunities";
    case 21: return "advertising";
    case 3: return "lineup";
    case 15: return "training_camp";
    case 16: return "training";
    case 18: return "tactics";
    case 22: return "stadium";
    case 13: return "toto";
    case 14: return "bank";
    case 17: return "transfer_market";
    case 10: return "load_game";
    case 11: return "save_game";
    case 12: return "advance_turn";
    default: return "unknown";
    }
}

std::string desk_request_json(const DeskRequest& r) {
    std::ostringstream out;
    out << "{\"kind\":" << json_quote(request_name(r.kind)) << ",\"manager_slot\":" << r.manager_slot
        << ",\"item_id\":" << r.item_id << ",\"menu_kind\":" << r.menu_kind << ",\"value\":" << r.value
        << ",\"revision\":" << r.revision << ",\"menu\":" << json_quote(desk_menu_name(r.menu_kind))
        << ",\"layout_operation\":" << json_quote(layout_operation_name(r.layout_operation))
        << ",\"layout_target_item\":" << r.layout_target_item << '}';
    return out.str();
}

DesktopScreen::DesktopScreen(const std::filesystem::path& data, DeskState state)
    : data_(data), state_(std::move(state)), exe_(load_le(asset_path(data, "BMH.exe"))),
      background_(load_vga(asset_path(data / "KB", "129.VGA"))),
      icons_(load_vga(asset_path(data / "KB", "3.VGA"))), widgets_(load_vga(asset_path(data / "KB", "0.VGA"))),
      portraits_(load_vga(asset_path(data / "KB", "26.VGA"))), letters_(load_vga(asset_path(data / "KB", "54.VGA"))),
      small_(load_font(asset_path(data / "F", "BMH3.FNT"))), texts_(load_textd(asset_path(data, "TEXTD.BLK"))), drawers_(data) {
    validate_desk_state(state_);
    if (background_.width != 320 || background_.height != 240 || state_.portrait < 0 || state_.portrait > 11)
        throw FormatError("desktop: unsupported assets/state");
    badge_ = load_vga(asset_path(data / "W", std::to_string(state_.badge >> 3) + ".VGA"));
    if (state_.next_opponent && *state_.next_opponent >= 0 && state_.next_opponent_badge &&
        !state_.next_opponent_badge_fallback) {
        const int selector = *state_.next_opponent_badge & 0x1ff;
        next_opponent_badge_ = load_vga(asset_path(data / "W", std::to_string(selector >> 3) + ".VGA"));
    }
    shortcut_bar_visible_ = state_.shortcut_bar_always;
}

void DesktopScreen::text(Image& out, int x, int baseline, std::string_view value, uint8_t color, int right) const {
    const int width = small_.text_width(value);
    if (right >= 0) x += (right - x) / 2 - width / 2;
    if (x < 0 || x + width > out.width || baseline < 5 || baseline >= out.height) return;
    Image mask(std::max(1, width), 6); small_.draw(mask, 0, 0, value, 1);
    int gradient = 0;
    while (gradient < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + gradient) != color) ++gradient;
    for (int yy = 0; yy < 6; ++yy) for (int xx = 0; xx < width; ++xx)
        if (mask.at(xx, yy)) out.at(x + xx, baseline - 5 + yy) = state_.gradient_text ? exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + gradient * 6 + yy) : color;
}
void DesktopScreen::sprite_text(Image& out, int x, int baseline, std::string_view value, int font) const {
    for (unsigned char c : value) {
        if (c <= 31 || c >= 128) continue;
        const int glyph = exe_.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + c);
        if (!glyph) { x += 2; continue; }
        const int sx = exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField + glyph * 2), width = exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2 + glyph * 2) - sx;
        copy(out, letters_, sx, (font - 4) * 7, x, baseline - 6, width, 7, true); x += width;
    }
}
void DesktopScreen::frame(Image& out, int x, int y, int w, int h) const {
    if (w < 8 || h < 8) throw FormatError("desktop: invalid drawer size");
    for (int yy = 4; yy < h - 3; ++yy) for (int xx = 4; xx < w - 3; ++xx)
        if (x + xx >= 0 && y + yy >= 0 && x + xx < out.width && y + yy < out.height)
            out.at(x + xx, y + yy) = exe_.u8(::bmh::layout::Advertising_PatternRect_RuntimeField + ((yy - 4) % 16) * 16 + (xx - 4) % 16);
    copy(out, widgets_, 0, 0, x, y, 4, 4); copy(out, widgets_, 4, 0, x + w - 4, y, 4, 4);
    copy(out, widgets_, 0, 4, x, y + h - 4, 4, 4); copy(out, widgets_, 4, 4, x + w - 4, y + h - 4, 4, 4);
    for (int k = 4; k < w - 4; k += 74) {
        const int n = std::min(74, w - 4 - k);
        copy(out, widgets_, 0, 104, x + k, y, n, 4); copy(out, widgets_, 0, 108, x + k, y + h - 4, n, 4);
    }
    for (int k = 4; k < h - 4; k += 96) {
        const int n = std::min(96, h - 4 - k);
        copy(out, widgets_, 0, 8, x, y + k, 4, n); copy(out, widgets_, 4, 8, x + w - 4, y + k, 4, n);
    }
}
void DesktopScreen::digits(Image& out, int value, int sx, int sy, int w, int h, int x, int y, int base, int gap) const {
    int count = 1;
    for (int n = value; n >= base; n /= base) ++count;
    int cursor = x - w + (w + gap) * count - ((w + gap) * count) / 2;
    do {
        // SelectsTransparentIndex -> UsesForTheseSprites: DesktopRuntimeField selects transparent index 0.
        for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx) {
            const auto color = icons_.pixels.at(size_t(sy + yy) * icons_.width + sx + (value % base) * w + xx);
            if (color != 0 && cursor + xx >= 0 && cursor + xx < out.width && y + yy >= 0 && y + yy < out.height)
                out.at(cursor + xx, y + yy) = color;
        }
        value /= base; cursor -= w + gap;
    } while (value);
}

void DesktopScreen::draw_item(Image& out, const DeskItem& item) const {
    int x = item.x, y = item.y;
    if (item.kind < 50) {
        copy(out, icons_, (item.kind % 8) * 32, (item.kind / 8) * 25, x, y, 32, 25);
        return;
    }
    if (item.open) {
        frame(out, x, y, item.width, item.height);
        // SourceDrawerTeam: the 4x4 drawer handle replaces the upper-left frame corner.
        if (state_.drawer_handles) copy(out, icons_, 192, 95, x, y, 4, 4);
        x += 4; y += 4;
    }
    if (drawers_.handles(item.kind)) {
        drawers_.draw(out, state_, item, badge_, next_opponent_badge_, widgets_);
        return;
    }
    switch (item.kind - 50) {
    case 6:
        for (int i = 0; i < 10; ++i) {
            if (state_.warnings[i]) copy(out, icons_, 0, *state_.warnings[i] ? 111 : 118, x + (i % 5) * 7, y + (i / 5) * 7, 7, 7);
            else text(out, x + (i % 5) * 7, y + (i / 5) * 7 + 5, "?", 0x5f);
        }
        break;
    case 7:
        copy(out, icons_, 8, 111, x + 1, y + 1, 88, 17);
        rect(out, x + 10, y + 8, (state_.fans * 69) / 100 + 1, 2, 0x7a);
        break;
    case 8:
        copy(out, icons_, 210, 111, x + 1, y + 1, 51, 52);
        digits(out, state_.date.day, 90, 146, 12, 12, x + 22, y + 22, 10, 1);
        digits(out, state_.date.weekday, 90, 158, 17, 5, x + 24, y + 15, 7, 1);
        digits(out, state_.date.month - 1, 0, 163, 20, 5, x + 22, y + 36, 12, 1);
        digits(out, state_.date.year, 90, 141, 4, 5, x + 22, y + 43, 10, 1);
        break;
    case 5:
        copy(out, icons_, 0, 129, x + 1, y + 1, 89, 26);
        rect(out, x + 35, y + 26, (state_.ticket_price - 8) * 3 + 1, 1, 0x7a);
        text(out, x + 25, y + 6, money(state_.balance), exe_.u8(::bmh::layout::Desktop_Copy_TextPointer + int(state_.balance >= 0)), x + 96);
        text(out, x + 25, y + 14, money(int64_t(state_.savings) * state_.money_numerator / state_.money_denominator), exe_.u8(::bmh::layout::Desktop_Copy_TextPointer2), x + 96);
        break;
    case 14:
        for (int i = 0; i < 5; ++i) {
            copy(out, icons_, 97, 111, x + 3 + i * 33, y + 2, 32, 30);
            const auto value = state_.automation[i];
            if ((i == 4 ? value & 1 : value) != 0) copy(out, icons_, 129 + i * 16, 111, x + i * 33 + 10, y + 4, 17, 11);
            if (i == 0 && value != 0 && state_.automation_formation) {
                const auto& formation = *state_.automation_formation;
                // The original miniature excludes the keeper and unused positions.
                for (int row = 1; row < 11; ++row)
                    if (formation[size_t(row)] != 0)
                        rect(out, x + 10 + formation[size_t(row)] * 15 / 18,
                             y + 5 + formation[size_t(row + 11)], 1, 1, 0x27);
            }
        }
        break;
    case 15:
        if (!state_.next_competition || state_.next_competition_label.empty()) {
            text(out, x, y + 6, "?", 0x6a, x + 141);
        } else {
            const auto split = state_.next_competition_label.find('\n');
            text(out, x, y + 6, std::string_view(state_.next_competition_label).substr(0, split), 0x6a, x + 141);
            if (split != std::string::npos)
                text(out, x, y + 13, std::string_view(state_.next_competition_label).substr(split + 1), 0x6a, x + 141);
            if (!state_.next_competition_detail.empty())
                text(out, x, y + 21, state_.next_competition_detail, 0x5f, x + 141);
        }
        if (state_.next_opponent && *state_.next_opponent != -1) {
            const std::string& home = state_.next_match_away ? state_.next_opponent_name : state_.club_name;
            const std::string& away = state_.next_match_away ? state_.club_name : state_.next_opponent_name;
            text(out, x, y + 21, home, 0x5f, x + 141);
            text(out, x, y + 27, exe_.cstring(::bmh::layout::DeskDrawers_Recent_TextPointer), 0x5f, x + 141);
            text(out, x, y + 33, away, 0x5f, x + 141);
        }
        break;
    case 18:
        border(out, x + 1, y + 1);
        copy(out, portraits_, (state_.portrait % 6) * 38, (state_.portrait / 6) * 38, x + 1, y + 1, 38, 38);
        break;
    case 19: text(out, x, y + 6, state_.manager_name, 0x5f); break;
    case 13: case 22:
        break; // SourceDrawerTeam default: these native kinds have no content.
    default: throw FormatError("desktop: unsupported drawer kind");
    }
}

Image DesktopScreen::render() const {
    const auto& visible = layout_preview_ ? *layout_preview_ : state_;
    auto out = visible.decorated_background ? background_ : Image(320, 240);
    if (!state_.decorated_background) {
        rect(out, 0, 4, 320, 13, 0x44); rect(out, 1, 5, 318, 11, 0x39);
    }
    rect(out, 0, 228, 320, 11, 0x44);
    // SourceDrawerTeam draws all shadows before the linked-list contents.
    for (const auto& item : visible.items) if (item.kind < 50 || item.open) {
        rect(out, item.x + 2, item.y + item.height, item.width, 2, 0x40);
        rect(out, item.x + item.width, item.y + 2, 2, item.height, 0x40);
    }
    for (const auto& item : visible.items) draw_item(out, item);
    // SourceDrawerTeam's compact drawer bar. Typed layout requests cover all
    // native drawer records; MEN remains a separate file-picker action.
    if (shortcut_bar_visible_) {
        copy(out, icons_, 0, 100, 0x16, 0xd7, 0x114, 0xb);
        copy(out, icons_, exe_.u8(::bmh::layout::Desktop_Render_RuntimeField + uint32_t(bin_mode_)),
             exe_.u8(::bmh::layout::Desktop_Render_RuntimeField2 + uint32_t(bin_mode_)), 1, 0xcf, 0x14, 0x14);
        copy(out, icons_, exe_.u8(::bmh::layout::Desktop_Render_RuntimeField3 + uint32_t(socket_confirmation_armed_)),
             0x4b, 299, 0xcf, 0x14, 0x14);
        if (shortcut_hover_record_ >= 0)
            text(out, 0, 235, texts_[text_id(::bmh::layout::Desktop_Render_TextPointer + uint32_t(shortcut_hover_record_) * 4)], 0x5f, 319);
    }
    if (layout_drag_ && layout_preview_) {
        const auto selected = std::find_if(visible.items.begin(), visible.items.end(),
            [&](const DeskItem& item) { return item.id == layout_drag_->item; });
        if (selected != visible.items.end()) {
            draw_item(out, *selected);
            const auto& item = *selected;
            const int w = item.width - (item.kind >= 50 && !item.open ? 8 : 0);
            const int h = item.height - (item.kind >= 50 && !item.open ? 8 : 0);
            rect(out, item.x, item.y, w, 1, 0x27);
            rect(out, item.x, item.y + h - 1, w, 1, 0x27);
            rect(out, item.x, item.y, 1, h, 0x27);
            rect(out, item.x + w - 1, item.y, 1, h, 0x27);
        }
    }
    return out;
}

std::optional<DeskItem> DesktopScreen::folder_child_at(int x,int y) const {
    for(auto it=state_.items.rbegin();it!=state_.items.rend();++it) {
        const auto& folder=*it;
        // DeskDate LAB131af checks the current top record's bounds before
        // looking for its children; LAB131a1 follows +6 only on a miss.
        // An overlaid icon/drawer, including an empty part of another folder,
        // therefore owns the hit instead of exposing a lower folder child.
        const int inset = folder.kind >= 50 && !folder.open ? 8 : 0;
        if (x < folder.x || y < folder.y || x >= folder.x + folder.width - inset ||
            y >= folder.y + folder.height - inset) continue;
        if(folder.disabled||folder.kind<50||folder.kind>52||!folder.open) return std::nullopt;
        const int folder_id=folder.kind-50,left=folder.x+9,top=folder.y+8,right=folder.x+folder.width-4,bottom=folder.y+folder.height-4;
        int child_x=left,child_y=top;
        for(uint8_t id:state_.folder_icon_slots[size_t(folder_id)]) {
            if(child_x+32>right) child_x=left,child_y+=27;
            if(child_y+25>bottom) break;
            if(x>=child_x&&x<child_x+32&&y>=child_y&&y<child_y+25) {
                if(id==0||id>30) return std::nullopt;
                const auto& raw=state_.layout_records[id];
                return DeskItem{int(id),child_x,child_y,32,25,raw[12],raw[0],uint8_t(raw[13]&0x7f),false,false};
            }
            child_x+=34;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

DeskRequest DesktopScreen::pointer_down(int x, int y, bool right) {
    if (pending_) return {};
    if (socket_release_pending_) return {}; // DeskDate waits here for the first RMB release.
    sample_shortcut_pointer(x, y);
    shortcut_hover_record_ = -1; // DeskDate clears its tooltip on event2/event8 before dispatch.
    const bool socket_hit = shortcut_bar_visible_ && right && x > 0x12a && x < 0x13f &&
                            y > 0xce && y < 0xe3;
    if (socket_confirmation_armed_) {
        if (socket_hit && !socket_release_pending_) {
            DeskRequest action{DeskRequestKind::end_session, state_.manager_slot, -1, -1, -1, state_.revision};
            pending_ = action;
            socket_confirmation_armed_ = false;
            return action;
        }
        // DeskDate abandons the pressed socket when the next event is elsewhere.
        if (!socket_hit) socket_confirmation_armed_ = false;
    }
    if (socket_hit) {
        socket_confirmation_armed_ = true;
        socket_release_pending_ = true;
        return {};
    }
    layout_drag_.reset(); layout_preview_.reset();
    if(const auto child=folder_child_at(x,y)) {
        if(!right||!state_.drawer_handles||bin_mode_) return {};
        layout_drag_=LayoutDrag{child->id,x-child->x,y-child->y,child->x,child->y,child->folder,false,true,false,right};
        layout_preview_=state_;
        auto& slots=layout_preview_->folder_icon_slots[size_t(child->folder)];
        slots.erase(std::remove(slots.begin(),slots.end(),uint8_t(child->id)),slots.end());
        layout_preview_->items.push_back(*child);
        // RestoresChildBinMode restores the child, then LAB12dbc unlinks/appends it.
        sound_ids_.insert(sound_ids_.end(), {15, 12, 15});
        return {};
    }
    for (auto it = state_.items.rbegin(); it != state_.items.rend(); ++it) {
        const auto& item = *it;
        const int inset = item.kind >= 50 && !item.open ? 8 : 0;
        if (item.disabled || x < item.x || y < item.y || x >= item.x + item.width - inset ||
            y >= item.y + item.height - inset) continue;
        if (right && !state_.drawer_handles) return {};
        if (right && bin_mode_ && item.kind!=12) return {};
        if (!right && item.kind==55 && item.open && !state_.national_team &&
            x-item.x-4>34 && x-item.x-4<87) return {}; // Original ticket-price scale.
        if (!right && item.kind==64 && item.open) return {}; // Source automation controls remain clicks.
        const bool drawer_handle=item.kind>=50&&state_.drawer_handles&&x<=item.x+3&&y<=item.y+3;
        if(item.kind>=50&&!drawer_handle&&!right) return {};
        if(!right&&!drawer_handle) return {}; // Source LMB activates; RMB starts a drag.
        layout_drag_ = LayoutDrag{item.id, x - item.x, y - item.y, item.x, item.y, -1, false, false, drawer_handle, right};
        layout_preview_ = state_;
        // RemovesBinModeIcon/RestoresChildBinMode run at RMB press, even if no motion follows.
        // The existing LMB handle drag differs from that Source path; do not
        // invent its sound mapping here.
        if (right) sound_ids_.insert(sound_ids_.end(), {12, 15});
        return {};
    }
    return {};
}

bool DesktopScreen::pointer_move(int x, int y) {
    if (pending_ || socket_release_pending_) return false;
    if (!layout_drag_ || !layout_preview_) return sample_shortcut_pointer(x, y);
    auto& drag = *layout_drag_;
    auto found = std::find_if(layout_preview_->items.begin(), layout_preview_->items.end(),
        [&](const DeskItem& item) { return item.id == drag.item; });
    if (found == layout_preview_->items.end()) return false;
    int next_x = x - drag.grab_x, next_y = y - drag.grab_y;
    if (state_.layout_snap) { next_x = (next_x / 5) * 5; next_y = (next_y / 5) * 5; }
    const int max_x=std::max(1,319-found->width), max_y=std::max(1,239-found->height);
    next_x = std::clamp(next_x, 1, max_x);
    next_y = std::clamp(next_y, 1, max_y);
    drag.moved |= next_x != drag.start_x || next_y != drag.start_y;
    const bool changed = found->x != next_x || found->y != next_y;
    found->x = next_x; found->y = next_y;
    return changed;
}

DeskRequest DesktopScreen::pointer_up(int x, int y, bool right) {
    if (socket_release_pending_) {
        if (right) socket_release_pending_ = false;
        return {}; // First socket RMB release is only the Source wait barrier.
    }
    if (!layout_drag_) return click(x, y, right);
    const auto drag = *layout_drag_;
    layout_drag_.reset();
    if (right!=drag.right_button) { layout_preview_.reset(); return {}; }
    if (!drag.moved || !layout_preview_) {
        layout_preview_.reset();
        if(drag.drawer_handle) return {}; // Source handle release is not a body click.
        return click(x,y,right);
    }
    const auto source = std::find_if(state_.items.begin(), state_.items.end(),
        [&](const DeskItem& item) { return item.id == drag.item; });
    const auto preview = std::find_if(layout_preview_->items.begin(), layout_preview_->items.end(),
        [&](const DeskItem& item) { return item.id == drag.item; });
    const DeskItem* source_item=nullptr;
    if(source!=state_.items.end()) source_item=&*source;
    else if(drag.folder_child) {
        const auto found=std::find_if(layout_preview_->items.begin(),layout_preview_->items.end(),[&](const DeskItem& item){return item.id==drag.item;});
        if(found!=layout_preview_->items.end()) source_item=&*found;
    }
    if (!source_item || preview == layout_preview_->items.end()) { layout_preview_.reset(); return {}; }
    auto before = std::array<uint8_t,54*16>{};
    auto after = before;
    for (size_t i=0;i<54;++i) {
        std::copy(state_.layout_records[i].begin(),state_.layout_records[i].end(),before.begin()+i*16);
        std::copy(state_.layout_records[i].begin(),state_.layout_records[i].end(),after.begin()+i*16);
    }
    auto* record = after.data() + size_t(drag.item)*16;
    const auto xflags = uint16_t(record[0]) | (uint16_t(record[1]) << 8);
    const auto native_x = uint16_t((xflags & 0x1000) | uint16_t(preview->x));
    record[0]=uint8_t(native_x); record[1]=uint8_t(native_x>>8); record[2]=uint8_t(preview->y);
    const DeskItem* drop = nullptr;
    for (auto it=state_.items.rbegin();it!=state_.items.rend();++it) {
        if (it->id == drag.item || it->disabled || it->kind < 50 || it->kind > 52) continue;
        if (preview->x >= it->x && preview->x + 32 <= it->x+it->width &&
            preview->y >= it->y && preview->y + 25 <= it->y+it->height) { drop=&*it; break; }
    }
    DeskLayoutOperation op = DeskLayoutOperation::move_item;
    int target = -1;
    if (drop && source_item->kind < 50) {
        // SourceDrawerTeam records drawer membership in the low seven bits of +13;
        // a folder child is absent from the visible next-link chain (+5 == ff).
        const int folder = drop->kind - 50;
        const int folder_count=std::count_if(state_.layout_records.begin()+1,state_.layout_records.begin()+31,
            [&](const auto& candidate){return candidate[5]==0xff && (candidate[1]&0x10)==0 &&
                (candidate[13]&0x7f)==folder;});
        const bool already_in_folder=drag.folder_child&&drag.folder==folder;
        if (folder >= 0 && folder < 3 && drag.item <= 30 &&
            (state_.layout_records[size_t(drag.item)][1]&0x10)==0 && folder_count-(already_in_folder?1:0) < 12) {
            const uint8_t previous = record[6], next = record[5];
            if (previous < 54) after[size_t(previous)*16+5] = next;
            if (next > 0 && next < 54) after[size_t(next)*16+6] = previous;
            record[13] = uint8_t((record[13] & 0x80) | folder);
            record[5] = 0xff;
            record[6] = 0xff;
            op = DeskLayoutOperation::set_folder; target=drop->id;
        }
    }
    auto req = request(DeskRequestKind::change_layout,*source_item);
    if(drag.folder_child&&op==DeskLayoutOperation::move_item) {
        op=DeskLayoutOperation::return_folder_child;target=drag.folder;
        int tail=0;size_t guard=0;
        while(after[size_t(tail)*16+5]!=0) {
            if(++guard>=54) { layout_preview_.reset();return {}; }
            tail=after[size_t(tail)*16+5];if(tail<0||tail>=54){layout_preview_.reset();return {};}
        }
        if(record[5]!=0xff||record[6]!=0xff) { layout_preview_.reset();return {}; }
        record[5]=0;record[6]=uint8_t(tail);after[size_t(tail)*16+5]=uint8_t(drag.item);
    }
    req.layout_operation=op; req.layout_target_item=target;
    req.layout_before=before; req.layout_after=after;
    pending_=req;
    if (op == DeskLayoutOperation::set_folder) sound_ids_.push_back(12);
    layout_preview_.reset();
    return req;
}

std::vector<uint8_t> DesktopScreen::take_sound_ids() const {
    std::vector<uint8_t> result;
    result.swap(sound_ids_);
    return result;
}

DeskRequest DesktopScreen::request(DeskRequestKind kind, const DeskItem& item, int value) {
    DeskRequest r{kind, state_.manager_slot, item.id, item.kind, value, state_.revision};
    shortcut_hover_record_ = -1;
    pending_ = r; last_rejection_.clear(); return r;
}
DeskRequest DesktopScreen::process_timer_expired() {
    if (pending_ || layout_drag_ || socket_release_pending_) return {};
    // DeskDate consumes the shared Source event8 without selecting a menu icon.
    pending_ = DeskRequest{DeskRequestKind::advance_turn, state_.manager_slot,
                           -1, 12, -1, state_.revision};
    shortcut_hover_record_ = -1;
    last_rejection_.clear();
    return *pending_;
}
bool DesktopScreen::sample_shortcut_pointer(int x, int y) {
    const bool visible_before = shortcut_bar_visible_;
    const int hover_before = shortcut_hover_record_;
    // DeskDate samples this before checking event2/event8. Only entry into
    // y239 toggles afb86 bit0; the saved Always option owns independent bit1.
    if (y == 239 && shortcut_last_y_ != 239) shortcut_bar_edge_toggle_ = !shortcut_bar_edge_toggle_;
    shortcut_last_y_ = y;
    shortcut_bar_visible_ = state_.shortcut_bar_always || shortcut_bar_edge_toggle_;
    shortcut_hover_record_ = -1;
    if (shortcut_bar_visible_ && x > 21 && x < 298 && y > 214 && y < 228) {
        const int kind = (x - 22) / 12 + 50;
        for (size_t i = 0; i < state_.layout_records.size(); ++i)
            if (state_.layout_records[i][12] == kind) { shortcut_hover_record_ = int(i); break; }
    }
    return visible_before != shortcut_bar_visible_ || hover_before != shortcut_hover_record_;
}
DeskRequest DesktopScreen::click(int x, int y, bool right) {
    if (pending_) return {};
    sample_shortcut_pointer(x, y);
    if (shortcut_bar_visible_ && x > 0 && x < 0x15 && y > 0xce && y < 0xe3) {
        bin_mode_ = !bin_mode_;
        return {};
    }
    if (y == 0xef) return {}; // Pointer sampling already handled the Source edge transition.
    if (shortcut_bar_visible_ && x > 0x15 && x < 0x12a && y > 0xd6 && y < 0xe4) {
        const int source_cell = (x - 0x16) / 12 + 50;
        if (source_cell == 72 && state_.menu_layout_shortcut) {
            auto action=request(DeskRequestKind::open_menu,*state_.menu_layout_shortcut,right ? 0 : 1);
            action.menu_kind=53; pending_=action; return action;
        }
        if (!right && source_cell == 63 && state_.mailbox_shortcut)
            return request(DeskRequestKind::open_menu, *state_.mailbox_shortcut);
        if (source_cell >= 50 && source_cell <= 71) {
            int record_id = -1;
            for (size_t i = 0; i < state_.layout_records.size(); ++i)
                if (state_.layout_records[i][12] == source_cell) { record_id = int(i); break; }
            if (record_id >= 0) {
                DeskItem target; target.id=record_id; target.kind=source_cell;
                auto req=request(DeskRequestKind::change_layout,target,right ? 8 : 2);
                req.layout_operation=DeskLayoutOperation::toggle_quickbar_drawer;
                req.layout_target_item=record_id;
                for(size_t i=0;i<54;++i) {
                    std::copy(state_.layout_records[i].begin(),state_.layout_records[i].end(),req.layout_before.begin()+i*16);
                    std::copy(state_.layout_records[i].begin(),state_.layout_records[i].end(),req.layout_after.begin()+i*16);
                }
                auto* record=req.layout_after.data()+size_t(record_id)*16;
                const bool append = record[5]==0xff;
                if(record[5]==0xff) {
                    int tail=0;
                    size_t guard=0;
                    while(req.layout_after[size_t(tail)*16+5]!=0) {
                        if(++guard>=54) return {};
                        tail=req.layout_after[size_t(tail)*16+5];
                        if(tail<0 || tail>=54) return {};
                    }
                    record[5]=0; record[6]=uint8_t(tail);
                    req.layout_after[size_t(tail)*16+5]=uint8_t(record_id);
                    const bool opened=!(right && record_id>2);
                    record[13]=uint8_t((record[13]&0x7f)|(opened?0x80:0));
                } else {
                    const uint8_t previous=record[6],next=record[5];
                    if(previous<54) req.layout_after[size_t(previous)*16+5]=next;
                    if(next!=0 && next<54) req.layout_after[size_t(next)*16+6]=previous;
                    record[5]=record[6]=0xff;
                }
                sound_ids_.push_back(append ? 15 : 12);
                pending_=req; return req;
            }
        }
        return {}; // Bar input must never leak into an underlying drawer.
    }
    if(const auto child=folder_child_at(x,y)) {
        if(right&&bin_mode_&&state_.drawer_handles) {
            auto req=request(DeskRequestKind::change_layout,*child);
            req.layout_operation=DeskLayoutOperation::restore_folder_child;
            req.layout_target_item=child->folder;
            for(size_t i=0;i<54;++i)
                std::copy(state_.layout_records[i].begin(),state_.layout_records[i].end(),req.layout_before.begin()+i*16);
            req.layout_after=req.layout_before;
            auto* record=req.layout_after.data()+size_t(child->id)*16;
            int tail=0;size_t guard=0;
            while(req.layout_after[size_t(tail)*16+5]!=0) {
                if(++guard>=54) { pending_.reset();return {}; }
                tail=req.layout_after[size_t(tail)*16+5];
                if(tail<0||tail>=54) { pending_.reset();return {}; }
            }
            record[5]=0;record[6]=uint8_t(tail);
            req.layout_after[size_t(tail)*16+5]=uint8_t(child->id);
            sound_ids_.push_back(15); // RestoresChildBinMode restores the child in bin mode.
            pending_=req;return req;
        }
        if(right) return {};
        return request(child->kind==12?DeskRequestKind::advance_turn:DeskRequestKind::open_menu,*child);
    }
    for (auto it = state_.items.rbegin(); it != state_.items.rend(); ++it) {
        const auto& item = *it;
        const int inset = item.kind >= 50 && !item.open ? 8 : 0;
        if (x < item.x || y < item.y || x >= item.x + item.width - inset || y >= item.y + item.height - inset) continue;
        if(right&&bin_mode_&&state_.drawer_handles&&item.id>0&&item.id<=30&&item.kind<50&&item.kind!=12) {
            const auto& record=state_.layout_records[size_t(item.id)];
            const int folder=int(record[13]&0x7f),previous=record[6],next=record[5];
            if(folder>=0&&folder<3&&record[5]!=0xff&&previous<54&&(next==0||next<54)) {
                auto req=request(DeskRequestKind::change_layout,item);
                req.layout_operation=DeskLayoutOperation::send_to_folder;
                req.layout_target_item=folder;
                for(size_t i=0;i<54;++i)
                    std::copy(state_.layout_records[i].begin(),state_.layout_records[i].end(),req.layout_before.begin()+i*16);
                req.layout_after=req.layout_before;
                req.layout_after[size_t(previous)*16+5]=uint8_t(next);
                if(next!=0)req.layout_after[size_t(next)*16+6]=uint8_t(previous);
                req.layout_after[size_t(item.id)*16+5]=req.layout_after[size_t(item.id)*16+6]=0xff;
                sound_ids_.push_back(12); // RemovesBinModeIcon removes the bin-mode icon.
                pending_=req;return req;
            }
        }
        if (item.disabled || right) return {};
        if (item.kind < 50) return request(item.kind == 12 ? DeskRequestKind::advance_turn : DeskRequestKind::open_menu, item);
        // DeskDate only handles Ticket/Calendar/Automation body clicks;
        // drawer open/close is exclusively the compact quickbar operation.
        if (item.open && state_.drawer_handles && x <= item.x + 3 && y <= item.y + 3) return {};
        if (item.kind == 58 && item.open) return request(DeskRequestKind::advance_turn, item);
        if (item.kind == 55 && item.open && !state_.national_team && x - item.x - 4 > 34 && x - item.x - 4 < 87)
            return request(DeskRequestKind::change_ticket_price, item, (x - item.x - 4 - 35) / 3 + 8);
        if (item.kind == 64 && item.open) return request(DeskRequestKind::change_option, item, std::clamp((x - item.x - 4) / 33, 0, 4));
        return {}; // Noninteractive status fields must not invent layout/game changes.
    }
    return {};
}
DeskRequest DesktopScreen::key(std::string_view key) {
    if (pending_ || socket_release_pending_) return {};
    // DeskDate only confirms after the next event is the socket RMB; any key
    // event after its first release abandons the confirmation state.
    if (socket_confirmation_armed_) socket_confirmation_armed_ = false;
    if (key.size() != 1) return {};
    auto c = uint8_t(key[0]);
    if (c >= 'A' && c <= 'Z') c = uint8_t(c + ('a' - 'A'));
    for (int menu = 0; menu < 30; ++menu) if (exe_.u8(::bmh::layout::Desktop_Key_RuntimeField + menu) == c && c) {
        for (const auto& item : state_.items)
            if (item.kind == menu && !item.disabled)
                return request(menu == 12 ? DeskRequestKind::advance_turn : DeskRequestKind::open_menu, item);
    }
    return {};
}
void DesktopScreen::acknowledge(DeskState updated) {
    validate_desk_state(updated);
    if (!pending_ || updated.revision <= state_.revision ||
        (updated.manager_slot != state_.manager_slot && pending_->kind != DeskRequestKind::advance_turn))
        throw FormatError("desktop: expected a newer state with the same manager roster");
    const bool turn_handoff = pending_->kind == DeskRequestKind::advance_turn;
    const bool previous_national = state_.national_team &&
        (state_.options.world || state_.national_coach_takeover);
    const bool incoming_national = updated.national_team &&
        (updated.options.world || updated.national_coach_takeover);
    const bool national_transition = turn_handoff && (previous_national || incoming_national);
    const bool options_match = updated.options == state_.options ||
        (national_transition && updated.options.duration == state_.options.duration &&
         updated.options.difficulty == state_.options.difficulty && updated.options.mode == state_.options.mode &&
         updated.options.league == state_.options.league && updated.options.world == state_.options.world);
    if (!options_match) throw FormatError("desktop: career options changed during the turn");
    if (updated.manager_teams != state_.manager_teams && !turn_handoff)
        throw FormatError("desktop: manager runtime mapping changed unexpectedly");
    const auto same_identity = [](const DeskManagerIdentity& a, const DeskManagerIdentity& b) {
        return a.club_identity_key == b.club_identity_key && a.name == b.name && a.portrait == b.portrait;
    };
    if (!turn_handoff && state_.national_coach_takeover &&
        (updated.national_return_roster.size() != state_.national_return_roster.size() ||
         !std::equal(state_.national_return_roster.begin(), state_.national_return_roster.end(),
                     updated.national_return_roster.begin(), same_identity)))
        throw FormatError("desktop: national return roster changed unexpectedly");
    if (national_transition && incoming_national) {
        const auto& baseline = previous_national && !state_.national_return_roster.empty()
            ? state_.national_return_roster : state_.managers;
        if ((updated.national_coach_takeover && baseline.size() != updated.national_return_roster.size()) ||
            updated.managers.size() != 1 ||
            (!updated.national_return_roster.empty() &&
             !std::equal(baseline.begin(), baseline.end(), updated.national_return_roster.begin(), same_identity)) ||
            std::none_of(baseline.begin(), baseline.end(), [&](const auto& manager) {
                return same_identity(manager, updated.managers[0]);
            }))
            throw FormatError("desktop: national coach does not match the current manager roster");
    } else if (national_transition && previous_national) {
        const auto& baseline = state_.national_return_roster.empty() ? state_.managers : state_.national_return_roster;
        if (updated.managers.size() != baseline.size() ||
            !std::equal(updated.managers.begin(), updated.managers.end(), baseline.begin(), same_identity) ||
            std::none_of(baseline.begin(), baseline.end(), [&](const auto& manager) {
                return same_identity(manager, state_.managers[0]);
            }))
            throw FormatError("desktop: handback does not restore the manager roster");
    } else {
        if (updated.managers.size() != state_.managers.size())
            throw FormatError("desktop: manager roster changed unexpectedly");
        for (size_t slot=0;slot<updated.managers.size();++slot)
            if (updated.managers[slot].club_identity_key != state_.managers[slot].club_identity_key ||
                updated.managers[slot].name != state_.managers[slot].name ||
                updated.managers[slot].portrait != state_.managers[slot].portrait)
                throw FormatError("desktop: manager identity changed unexpectedly");
    }
    Image next_badge = badge_;
    if ((updated.badge >> 3) != (state_.badge >> 3))
        next_badge = load_vga(asset_path(data_ / "W", std::to_string(updated.badge >> 3) + ".VGA"));
    shortcut_bar_visible_ = updated.shortcut_bar_always || shortcut_bar_edge_toggle_;
    state_ = std::move(updated); badge_ = std::move(next_badge);
    pending_.reset(); last_rejection_.clear();
}
void DesktopScreen::acknowledge_mailbox_parent(DeskState updated) {
    validate_desk_state(updated);
    const bool revision_ok = updated.revision == state_.revision ||
        (state_.revision != std::numeric_limits<uint64_t>::max() && updated.revision == state_.revision + 1);
    if (pending_ || updated.source != "engine_projection" ||
        updated.manager_slot != state_.manager_slot || updated.runtime_club != state_.runtime_club ||
        !revision_ok ||
        (state_.mailbox_notification_pending == false && updated.mailbox_notification_pending))
        throw FormatError("desktop: mailbox parent refresh has a stale owner or revision");
    auto expected = state_;
    expected.revision = updated.revision;
    expected.mailbox_notification_pending = updated.mailbox_notification_pending;
    if (desk_json(expected) != desk_json(updated))
        throw FormatError("desktop: mailbox parent refresh changed unrelated desk state");
    // Keep shortcut-bar visibility and bin mode: DeskDate returns from
    // MailboxStateFromGame inside the existing desk loop, without reconstructing it.
    state_ = std::move(updated);
}
void DesktopScreen::reject_action(std::string reason) {
    if (!pending_ || reason.empty()) throw FormatError("desktop: rejection requires a pending action and reason");
    last_rejection_ = std::move(reason); pending_.reset();
}
void DesktopScreen::resolve_local_menu() {
    if (!pending_ || pending_->kind != DeskRequestKind::open_menu ||
        (pending_->menu_kind != 0 && pending_->menu_kind != 1 && pending_->menu_kind != 2 && pending_->menu_kind != 4 && pending_->menu_kind != 5 && pending_->menu_kind != 6 && pending_->menu_kind != 7 && pending_->menu_kind != 8 && pending_->menu_kind != 9 && pending_->menu_kind != 24 && pending_->menu_kind != 25 && pending_->menu_kind != 26 && pending_->menu_kind != 28 && pending_->menu_kind != 53 && pending_->menu_kind != 63 && pending_->menu_kind != 19 && pending_->menu_kind != 20 && pending_->menu_kind != 21 && pending_->menu_kind != 27 && pending_->menu_kind != 3 && pending_->menu_kind != 10 && pending_->menu_kind != 11 && pending_->menu_kind != 13 && pending_->menu_kind != 14 && pending_->menu_kind != 15 && pending_->menu_kind != 16 && pending_->menu_kind != 17 && pending_->menu_kind != 18 && pending_->menu_kind != 22 && pending_->menu_kind != 23 && pending_->menu_kind != 29))
        throw FormatError("desktop: no supported local-menu navigation pending");
    pending_.reset();
}

std::string DesktopScreen::state_json() const {
    auto out = desk_json(state_); out.pop_back();
    out += ",\"shortcut_bar_visible\":" + std::string(shortcut_bar_visible_ ? "true" : "false") +
           ",\"shortcut_hover_record\":" + std::to_string(shortcut_hover_record_) +
           ",\"socket_confirmation_armed\":" + std::string(socket_confirmation_armed_ ? "true" : "false") +
           ",\"bin_mode\":" + std::string(bin_mode_ ? "true" : "false") +
           ",\"dragging_item\":" + (layout_drag_ ? std::to_string(layout_drag_->item) : "null") +
           ",\"pending_action\":" + (pending_ ? desk_request_json(*pending_) : "null") +
           ",\"last_rejection\":" + (last_rejection_.empty() ? "null" : json_quote(last_rejection_)) + "}";
    return out;
}
} // namespace bmh::ui
