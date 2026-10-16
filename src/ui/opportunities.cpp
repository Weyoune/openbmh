// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/opportunities.hpp"

#include <algorithm>
#include <sstream>

#include "ui/manager.hpp"

namespace bmh::ui {
namespace {
void copy(Image& out, const Image& src, int sx, int sy, int x, int y, int w, int h,
          int transparent = -1) {
    if (sx < 0 || sy < 0 || x < 0 || y < 0 || w < 0 || h < 0 ||
        sx + w > src.width || sy + h > src.height || x + w > out.width || y + h > out.height)
        throw FormatError("opportunities: Original sprite bounds");
    for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx) {
        const auto pixel = src.pixels[size_t(sy + yy) * size_t(src.width) + size_t(sx + xx)];
        if (pixel != transparent) out.at(x + xx, y + yy) = pixel;
    }
}
void fill(Image& out, int x, int y, int w, int h, uint8_t color) {
    for (int yy = y; yy < y + h; ++yy) for (int xx = x; xx < x + w; ++xx)
        out.at(xx, yy) = color;
}
std::string original_text(const TextDb& texts, uint32_t at) {
    if (at < ::bmh::layout::TextPointerTable || ((at - ::bmh::layout::TextPointerTable) & 3U))
        throw FormatError("opportunities: invalid TEXTD address");
    const size_t ix = (at - ::bmh::layout::TextPointerTable) / 4;
    if (ix >= texts.strings.size()) throw FormatError("opportunities: TEXTD address out of range");
    return original_to_utf8(texts[ix]);
}

// FormatError3's 96/109-wide cells use the three KB0 strips, transparent index c0.
// Width is a count for the art, but ExpireQuestion includes both descriptor end coordinates.
void original_button(Image& out, const Image& atlas, const SpriteFont& sprite,
                     const LeImage& exe, int x, int y, int width,
                     std::string_view label, bool selected) {
    constexpr int height = 10; // FormatError3 raises requested height 9 to 10.
    int dx = x, dy = y;
    if (selected) {
        const auto bg = out.at(x, y + height);
        fill(out, x, y, width + 1, 2, bg);
        fill(out, x, y, 2, height + 1, bg);
        dx += 2; dy += 2;
    } else {
        // ColorBottomRightBevel clips the inclusive shadow ends to 319/239.
        const int right = std::min(319, x + width + 1);
        const int bottom = std::min(239, y + height + 1);
        fill(out, x + 2, y + height, right - x - 1, bottom - y - height + 1, 0x40);
        fill(out, x + width, y + 2, right - x - width + 1, bottom - y - 1, 0x40);
    }
    const int sy = selected ? 44 : 74;
    copy(out, atlas, 24, sy, dx, dy, 31, height, 0xc0);
    int used = 31;
    while (width - used > 20) {
        const int count = std::min(8, width - used - 20);
        copy(out, atlas, 24, sy + 10, dx + used, dy, count, height, 0xc0);
        used += count;
    }
    const int tail = width - used;
    copy(out, atlas, 45 - tail, sy + 20, dx + used, dy, tail, height, 0xc0);
    sprite.draw(out, dx + width / 2 - sprite.text_width(label) / 2,
                dy + height / 2 + 2, label, exe.u8(::bmh::layout::Advertising_Button_RuntimeField + uint32_t(selected)));
}

void statistics_window(Image& out, const Image& background, const LeImage& exe, int index = 0,
                       int top_override = -1, int bottom_override = -1) {
    // StatisticsMenuScreen -> LoadGameEndTaxAnimationAssets. These are inclusive window endpoints.
    const int left = exe.u16(::bmh::layout::Opportunities_FormatError_RuntimeField + uint32_t(index) * 2);
    const int top = top_override >= 0 ? top_override : exe.u8(::bmh::layout::Opportunities_FormatError_RuntimeField3 + uint32_t(index));
    const int right = exe.u16(::bmh::layout::Opportunities_FormatError_RuntimeField2 + uint32_t(index) * 2),
        bottom = bottom_override >= 0 ? bottom_override : exe.u8(::bmh::layout::Opportunities_StatisticsWindow_RuntimeField + uint32_t(index));
    if (left < 0 || top < 19 || right >= out.width || bottom >= out.height ||
        right <= left + 1 || bottom <= top + 1)
        throw FormatError("opportunities: invalid Original window bounds");
    if (exe.u8(::bmh::layout::SolidPanelBackground) == 0)
        copy(out, background, left, top - 19, left + 1, top + 1,
             right - left - 1, bottom - top - 1, 0xc0);
    else fill(out, left, top, right - left + 1, bottom - top + 1, 0x42);
    fill(out, left, top, 1, bottom - top + 1, 0x46);
    fill(out, left, top, right - left + 1, 1, 0x46);
    fill(out, left, bottom, right - left + 1, 1, 0x40);
    fill(out, right, top, 1, bottom - top + 1, 0x40);
}
std::string rank_number(int value) {
    // FormatNumber width 2: '%' is the Original blank-position glyph, not a zero.
    const auto number = std::to_string(value);
    return number.size() < 2 ? "%" + number : number;
}
}

StatisticsMenuScreen::StatisticsMenuScreen(const std::filesystem::path& data, StatisticsMenuState state)
    : exe_(load_le(asset_path(data, "BMH.exe"))), texts_(load_textd(asset_path(data, "TEXTD.BLK"))),
      sprite_(data), backdrop_(load_vga(asset_path(data / "KB", "129.VGA"))),
      buttons_(load_vga(asset_path(data / "KB", "0.VGA"))),
      window_(load_vga(asset_path(data / "KB", "56.VGA"))),
      header_font_(load_font(asset_path(data / "F", "BMH2.FNT"))),
      body_font_(load_font(asset_path(data / "F", "BMH3.FNT"))), state_(std::move(state)) {
    if (state_.owner.kind != DeskRequestKind::open_menu || state_.owner.menu_kind != 23 ||
        state_.owner.manager_slot < 0 || state_.owner.manager_slot > 3 ||
        state_.underlay.width != 320 || state_.underlay.height != 240 ||
        backdrop_.width != 320 || backdrop_.height != 240)
        throw FormatError("statistics menu: invalid Source selector owner/frame");
}
Image StatisticsMenuScreen::render() const {
    // PlainWindowFrameUsedBy constructs the statistics desktop, retaining the current manager header.
    Image out = backdrop_;
    if (exe_.u8(::bmh::layout::PlainBackground) != 0) {
        out = Image(320, 240);
        fill(out, 0, 4, 320, 13, 0x44);
        fill(out, 1, 5, 318, 11, 0x39);
    }
    copy(out, state_.underlay, 0, 0, 0, 0, 172, 19);
    const auto title = original_text(texts_, ::bmh::layout::Opportunities_Render_TextPointer6);
    sprite_.draw(out, SpriteFont::centered_title_x(sprite_.text_width(title)), 13, title, 5);
    fill(out, 0, 228, 320, 11, exe_.u8(::bmh::layout::PlainBackground) == 0 ? 0x44 : 0);
    // StatisticsMenuScreen's nine FormatError3 cells: x=3,111,219; y=203,215,227.
    for (int i = 0; i < 9; ++i) {
        const int x = 3 + (i % 3) * 0x6c, y = 0xcb + (i / 3) * 0x0c;
        const bool selected = pressed_ == i || (pending_ && pending_->selected_index == i);
        original_button(out, buttons_, sprite_, exe_, x, y, 0x60,
                        original_text(texts_, ::bmh::layout::Opportunities_FormatError_TextPointer + uint32_t(i) * 4), selected);
    }
    if (!rejection_.empty()) sprite_.draw(out, 88, 198, rejection_, 5);
    return out;
}
Image StatisticsMenuScreen::render_page(int selected_index) const {
    if (selected_index < 0 || selected_index > 8)
        throw FormatError("statistics menu: unsupported Source child pane");
    auto out = render();
    int top_override=-1,bottom_override=-1;
    if(selected_index==1) {
        if(state_.transfer_history_count<0||state_.transfer_history_count>25)
            throw FormatError("statistics menu: invalid transfer-history count");
        const int adjustment=(28-std::max(1,state_.transfer_history_count))*3;
        top_override=adjustment+13;
        bottom_override=uint8_t(-0x3a-adjustment);
    }
    statistics_window(out, window_, exe_, selected_index, top_override, bottom_override);
    const uint32_t index = uint32_t(selected_index);
    const int left = exe_.u16(::bmh::layout::Opportunities_FormatError_RuntimeField + index * 2), right = exe_.u16(::bmh::layout::Opportunities_FormatError_RuntimeField2 + index * 2);
    const int top = top_override >= 0 ? top_override : exe_.u8(::bmh::layout::Opportunities_FormatError_RuntimeField3 + index);
    const auto title = utf8_to_original(original_text(texts_, ::bmh::layout::Opportunities_FormatError_TextPointer + index * 4));
    if(selected_index==3) {
        header_font_.draw(out,left+5,top+10-header_font_.baseline,title,0x6a);
        const auto detail=utf8_to_original(original_text(texts_,::bmh::layout::Opportunities_FormatError_TextPointer2));
        // TextWidth3 restores font 3 after the first font-2 heading in StatisticsMenuScreen.
        if(state_.gradient_text&&exe_.u8(::bmh::layout::TextGradientMode)==2) {
            // TextWidth3 -> Mask: a six-row FNT3 mask maps each row
            // through the Advertising_Text_RuntimeField palette ramp selected by TextWidth2(0x6a).
            const int width=body_font_.text_width(detail);
            if(!detail.empty()&&width<319) {
                Image mask(width,6);
                body_font_.draw(mask,0,5-body_font_.baseline,detail,1);
                int ramp=0;while(ramp<32&&exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2+uint32_t(ramp))!=0x6a)++ramp;
                for(int y=0;y<6;++y)for(int x=0;x<width;++x)
                    if(mask.at(x,y))out.at(107+x,top+5+y)=exe_.u8(::bmh::layout::Advertising_Text_RuntimeField+uint32_t(ramp*6+y));
            }
        } else body_font_.draw(out,107,top+10-body_font_.baseline,detail,0x6a);
    } else {
        header_font_.draw(out, left + (right - left) / 2 - header_font_.text_width(title) / 2,
                          top + 10 - header_font_.baseline, title, 0x6a);
    }
    // StatisticsMenuScreen draws this inclusive line for every child, including Alltime.
    fill(out, left + 3, top + 11, right - left - 6, 1, 0x6a);
    return out;
}
bool StatisticsMenuScreen::pointer_down(int x, int y, bool right) {
    if (pending_ || (right && pressed_ >= 0)) return false;
    if (right) { pending_ = StatisticsMenuRequest{state_.owner, -1, true}; return true; }
    pressed_ = -1;
    for (int i = 0; i < 9; ++i) {
        const int left = 3 + (i % 3) * 0x6c, top = 0xcb + (i / 3) * 0x0c;
        if (x >= left && x <= left + 0x60 && y >= top && y <= top + 10) {
            pressed_ = i; return true;
        }
    }
    return false;
}
bool StatisticsMenuScreen::release(int x, int y) {
    if (pending_) return false;
    const int index = pressed_; pressed_ = -1;
    if (index < 0) return false;
    const int left = 3 + (index % 3) * 0x6c, top = 0xcb + (index / 3) * 0x0c;
    if (x < left || x > left + 0x60 || y < top || y > top + 10) return true;
    if (index == 0 || index == 1 || index == 2 || (index >= 3 && index <= 8)) pending_ = StatisticsMenuRequest{state_.owner, index, false};
    else rejection_ = "Diese Statistikansicht ist noch nicht verfuegbar.";
    return true;
}
void StatisticsMenuScreen::update_owner(DeskRequest owner) {
    auto expected = state_.owner;
    expected.revision = owner.revision;
    if (pending_ || owner != expected || owner.revision < state_.owner.revision)
        throw FormatError("statistics menu: mismatched parent owner update");
    state_.owner = owner;
}
std::string StatisticsMenuScreen::state_json() const {
    std::ostringstream out; out << "{\"screen\":\"statistics_menu\",\"owner\":" << desk_request_json(state_.owner)
        << ",\"selected\":" << (pending_ ? pending_->selected_index : -1) << ",\"pending\":" << (pending_ ? "true" : "false")
        << ",\"unavailable\":" << (!rejection_.empty() ? "true" : "false") << "}"; return out.str();
}

OpportunitiesScreen::OpportunitiesScreen(const std::filesystem::path& data, OpportunitiesState state)
    : exe_(load_le(asset_path(data, "BMH.exe"))), texts_(load_textd(asset_path(data, "TEXTD.BLK"))),
      backdrop_(load_vga(asset_path(data / "KB", "56.VGA"))),
      ladder_(load_vga(asset_path(data / "KB", "37.VGA"))), buttons_(load_vga(asset_path(data / "KB", "0.VGA"))),
      font_(load_font(asset_path(data / "F", "BMH3.FNT"))),
      header_font_(load_font(asset_path(data / "F", "BMH2.FNT"))), sprite_(data), state_(std::move(state)) {
    if (state_.identity.manager_slot < 0 || state_.identity.manager_slot > 3 || state_.identity.runtime_team < 0 ||
        state_.identity.stable_club_identity < 0 || state_.league_size < 1 ||
        state_.league_size > int(state_.ranks.size()) ||
        state_.current_rank < 0 || state_.current_rank > state_.league_size ||
        (state_.mode != 1 && state_.mode != 2 && state_.mode != 4 && state_.mode != 8) ||
        state_.underlay.width != 320 || state_.underlay.height != 240 ||
        ladder_.width < 21 || ladder_.height < 138)
        throw FormatError("opportunities: invalid original screen state");
    for (int i = 0; i < state_.league_size; ++i)
        if (state_.ranks[size_t(i)] != 0 && state_.ranks[size_t(i)] != 1 && state_.ranks[size_t(i)] != 0xff)
            throw FormatError("opportunities: invalid Original rank result");
}

void OpportunitiesScreen::draw_text(Image& out, int x, int baseline, std::string_view value, uint8_t color) const {
    const auto s = utf8_to_original(value);
    const int w = font_.text_width(s), top = baseline - font_.baseline;
    if (x < 0 || top < 0 || x + w > out.width || top + font_.height > out.height) return;
    Image mask(std::max(1, w), font_.height); font_.draw(mask, 0, 0, s, 1);
    int index = 0; while (index < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + uint32_t(index)) != color) ++index;
    for (int y = 0; y < font_.height; ++y) for (int xx = 0; xx < w; ++xx) if (mask.at(xx, y))
        out.at(x + xx, top + y) = state_.gradient_text && index < 32
            ? exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + uint32_t(index * 6 + y)) : color;
}

Image OpportunitiesScreen::render() const {
    Image out = state_.underlay;
    statistics_window(out, backdrop_, exe_);
    // StatisticsMenuScreen selects FNT2 before the child heading and its one-pixel underline.
    const auto title = utf8_to_original(original_text(texts_, ::bmh::layout::Opportunities_FormatError_TextPointer));
    const int left = exe_.u16(::bmh::layout::Opportunities_FormatError_RuntimeField), right = exe_.u16(::bmh::layout::Opportunities_FormatError_RuntimeField2), top = exe_.u8(::bmh::layout::Opportunities_FormatError_RuntimeField3);
    header_font_.draw(out, left + (right - left) / 2 - header_font_.text_width(title) / 2,
                      top + 10 - header_font_.baseline, title, 0x6a);
    fill(out, left + 3, top + 11, right - left - 6, 1, 0x6a);
    if (state_.current_rank == 0) {
        // Void5 rank zero disables all four buttons; OriginalText shows Opportunities_Render_TextPointer5.
        const auto text = original_text(texts_, ::bmh::layout::Opportunities_Render_TextPointer5);
        draw_text(out, 0x5b + (0xe5 - 0x5b) / 2 - font_.text_width(utf8_to_original(text)) / 2,
                  0x67, text, 0x6c);
        return out;
    }
    // PaletteOverride(0x25) -> KB/37.VGA; CopyCrop uses transparent index ff.
    copy(out, ladder_, 0, 0, 0x61, 0x29, 0x15, 0x8a, 0xff);
    std::array<int, 3> first{{-1, -1, -1}};
    for (int r = 0; r < state_.league_size; ++r) {
        const uint8_t value = state_.ranks[size_t(r)];
        const int kind = value == 0 ? 0 : value == 1 ? 1 : 2;
        if (first[size_t(kind)] < 0) first[size_t(kind)] = r + 1;
        const uint8_t color = exe_.u8(value == 0xff ? ::bmh::layout::Opportunities_Render_RuntimeField : ::bmh::layout::Opportunities_Render_RuntimeField2 + uint32_t(value));
        // Fill3's inclusive endpoints: x102..112 and y71+4*r..72+4*r.
        fill(out, 0x66, 0x47 + r * 4, 0x70 - 0x66 + 1, 2, color);
    }
    static constexpr uint32_t labels[4] = {::bmh::layout::Opportunities_Render_ExecutableField, ::bmh::layout::Opportunities_Render_ExecutableField2, ::bmh::layout::Opportunities_Render_ExecutableField3, ::bmh::layout::Opportunities_Render_ExecutableField4};
    static constexpr int modes[4] = {1, 2, 4, 8};
    for (int i = 0; i < 4; ++i) {
        const bool selected = state_.mode == modes[i] || pressed_button_ == i ||
            (pending_ && pending_->action == OpportunitiesAction::select_mode && pending_->mode == modes[i]);
        original_button(out, buttons_, sprite_, exe_, 0x7c, 0x6c + i * 0xb, 0x6d,
                        original_text(texts_, labels[i]), selected);
    }
    // TextWidth3 restores FNT3 after each button; legend uses the three Opportunities_Render_RuntimeField3 colors.
    for (int k = 0; k < 3; ++k) {
        const auto color = exe_.u8(::bmh::layout::Opportunities_Render_RuntimeField3 + uint32_t(k));
        draw_text(out, 0x7b, 0x56 + k * 8, original_text(texts_, ::bmh::layout::Opportunities_Render_TextPointer + uint32_t(k) * 4), color);
        std::string value;
        if (first[size_t(k)] < 0) value = original_text(texts_, ::bmh::layout::Opportunities_Render_TextPointer4);
        else {
            int last = state_.league_size;
            for (int n = k + 1; n < 3; ++n) if (first[size_t(n)] >= 0) { last = first[size_t(n)] - 1; break; }
            value = original_text(texts_, ::bmh::layout::Opportunities_Render_TextPointer2) + rank_number(first[size_t(k)]) + exe_.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField8);
            if (first[size_t(k)] != last) value += original_text(texts_, ::bmh::layout::Opportunities_Render_TextPointer3) + rank_number(last);
            value += exe_.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField4);
        }
        draw_text(out, 0xa4, 0x56 + k * 8, value, color);
    }
    if (!rejection_.empty()) draw_text(out, 10, 198, rejection_, 0x6a);
    return out;
}

bool OpportunitiesScreen::pointer_down(int x,int y,bool right) {
    if(pending_)return false;
    if(right){close_requested_=true;pending_=OpportunitiesRequest{state_.identity,OpportunitiesAction::close,state_.mode};return true;}
    if(state_.current_rank==0)return false;
    pressed_button_=-1;
    for(int i=0;i<4;++i)if(x>=0x7c&&x<=0x7c+0x6d&&y>=0x6c+i*0xb&&y<=0x6c+i*0xb+10){pressed_button_=i;return true;}
    return false;
}
bool OpportunitiesScreen::move(int x,int y) {
    if(pressed_button_<0)return false;
    if(x>=0x7c&&x<=0x7c+0x6d&&y>=0x6c+pressed_button_*0xb&&y<=0x6c+pressed_button_*0xb+10)return false;
    pressed_button_=-1;return true;
}
bool OpportunitiesScreen::release(int x,int y) {
    if(pending_)return false;
    const int pressed=pressed_button_;pressed_button_=-1;
    if(pressed<0)return false;
    if(x<0x7c||x>0x7c+0x6d||y<0x6c+pressed*0xb||y>0x6c+pressed*0xb+10)return true;
    static constexpr int modes[4]={1,2,4,8};
    pending_=OpportunitiesRequest{state_.identity,OpportunitiesAction::select_mode,modes[pressed]};
    rejection_.clear();return true;
}

bool OpportunitiesScreen::key(std::string_view key) {
    (void)key; // Original modal has no keyboard mapping; close is the right-button path.
    return false;
}

void OpportunitiesScreen::acknowledge(OpportunitiesState state) {
    if(!pending_||pending_->action!=OpportunitiesAction::select_mode||
       state.identity.manager_slot!=state_.identity.manager_slot||state.identity.runtime_team!=state_.identity.runtime_team||
       state.identity.stable_club_identity!=state_.identity.stable_club_identity||state.identity.day_counter!=state_.identity.day_counter||
       state.identity.scope!=state_.identity.scope||state.gradient_text!=state_.gradient_text||
       state.identity.sequence!=state_.identity.sequence+1||state.identity.revision<=state_.identity.revision||
       state.mode!=pending_->mode||state.league_size!=state_.league_size||state.underlay.width!=state_.underlay.width||
       state.underlay.height!=state_.underlay.height||state.underlay.pixels!=state_.underlay.pixels)
        throw FormatError("opportunities: Source ACK does not match the pending button");
    if(state.current_rank<0||state.current_rank>state.league_size)
        throw FormatError("opportunities: invalid accepted Original rank");
    for(int i=0;i<state.league_size;++i)if(state.ranks[size_t(i)]!=0&&state.ranks[size_t(i)]!=1&&state.ranks[size_t(i)]!=0xff)
        throw FormatError("opportunities: invalid accepted Original rank result");
    state_=std::move(state);pending_.reset();rejection_.clear();close_requested_=false;
}
void OpportunitiesScreen::reject(std::string reason){if(!pending_||reason.empty())throw FormatError("opportunities: rejection requires a pending action");pending_.reset();rejection_=std::move(reason);close_requested_=false;}
std::string OpportunitiesScreen::state_json() const {
    std::ostringstream o;o<<"{\"screen\":\"opportunities\",\"manager\":"<<state_.identity.manager_slot
        <<",\"team\":"<<state_.identity.runtime_team<<",\"revision\":"<<state_.identity.revision
        <<",\"sequence\":"<<state_.identity.sequence<<",\"mode\":"<<state_.mode<<",\"league_size\":"<<state_.league_size
        <<",\"current_rank\":"<<state_.current_rank<<",\"ranks\":[";
    for(int i=0;i<state_.league_size;++i){if(i)o<<',';o<<unsigned(state_.ranks[size_t(i)]);}o<<"],\"pending\":"<<(pending_?"true":"false")
        <<",\"rejection\":"<<(rejection_.empty()?"null":"\"rejected\"")<<"}";return o.str();
}

} // namespace bmh::ui
