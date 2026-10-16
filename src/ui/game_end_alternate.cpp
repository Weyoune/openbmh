// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/game_end_alternate.hpp"

#include <algorithm>
#include <utility>

#include "formats/textd.hpp"

namespace bmh::ui {
// Existing setup.cpp asset resolver; do not duplicate filename matching rules.
std::filesystem::path asset_path(const std::filesystem::path&, const std::string&);

namespace {
void copy(Image& out, const Image& src, int sx, int sy, int dx, int dy, int w, int h) {
    for (int y=0; y<h; ++y) for (int x=0; x<w; ++x)
        if (dx+x>=0 && dx+x<out.width && dy+y>=0 && dy+y<out.height)
            out.at(dx+x,dy+y)=src.pixels.at(size_t(sy+y)*src.width+sx+x);
}
void fill(Image& out, int x0, int y0, int x1, int y1, uint8_t color) {
    for (int y=std::max(0,y0); y<=std::min(out.height-1,y1); ++y)
        for (int x=std::max(0,x0); x<=std::min(out.width-1,x1); ++x) out.at(x,y)=color;
}
std::string grouped(unsigned n) {
    std::string value=std::to_string(n);
    for (int i=int(value.size())-3; i>0; i-=3) value.insert(size_t(i),".");
    return value;
}
}

GameEndAlternateScoreAssets load_game_end_alternate_score_assets(const std::filesystem::path& data) {
    auto text=load_textd(asset_path(data,"TEXTD.BLK"));
    return {load_le(asset_path(data,"BMH.exe")),load_font(asset_path(data/"F","BMH3.FNT")),
            load_vga(asset_path(data/"KB","6.VGA")),{text[890],text[891],text[892]}};
}

GameEndAlternateScoreScreen::GameEndAlternateScoreScreen(GameEndAlternateScoreAssets assets,
                                                       Image backdrop, bool gradient_text)
    : assets_(std::move(assets)),backdrop_(std::move(backdrop)),gradient_text_(gradient_text) {
    if (backdrop_.width!=320 || backdrop_.height!=240 || backdrop_.pixels.size()!=320*240)
        throw FormatError("alternate highscore requires a 320x240 backdrop");
    if (assets_.calculator.width<113 || assets_.calculator.height<150 ||
        !assets_.exe.contains(::bmh::layout::GameEndAlternate_FormatError_ExecutableField))
        throw FormatError("alternate highscore requires original KB6 and EXE calculator tables");
}

std::optional<unsigned> GameEndAlternateScoreScreen::accepted_value() const {
    if (phase_==AlternateScorePhase::Calculator || phase_==AlternateScorePhase::Cancelled) return {};
    return value_;
}
std::optional<unsigned> GameEndAlternateScoreScreen::button_at(int x, int y) const {
    if (phase_!=AlternateScorePhase::Calculator) return {};
    for (unsigned i=0; i<23; ++i) {
        const int bx=window_x_+assets_.exe.u16(::bmh::layout::Advertising_Click_RuntimeField+2*i);
        const int by=window_y_+assets_.exe.u8(::bmh::layout::Advertising_Click_RuntimeField2+i);
        if (x>=bx && x<=bx+(i<15?16:53) && y>=by && y<=by+14) return i;
    }
    return {};
}
bool GameEndAlternateScoreScreen::activate(unsigned button) {
    if (phase_!=AlternateScorePhase::Calculator || button>=23) return false;
    if (button<10) {
        if (digit_count_<10) { value_=std::min(maximum,value_*10+button); ++digit_count_; }
    }
    else if (button==10) { if (digit_count_!=0) { value_/=10; --digit_count_; } }
    else if (button==11) { value_=initial; digit_count_=3; }
    else if (button==12) { value_=0; digit_count_=0; }
    else if (button==13) { phase_=AlternateScorePhase::Points; dragging_=false; }
    else if (button==14) { phase_=AlternateScorePhase::Cancelled; dragging_=false; }
    else value_=presets[button-15];
    if (value_!=0) digit_count_=unsigned(std::to_string(value_).size());
    return true;
}
bool GameEndAlternateScoreScreen::click(int x, int y) {
    if (phase_==AlternateScorePhase::AwaitAck) return acknowledge();
    if (dragging_) return false;
    const auto button=button_at(x,y);
    return button && activate(*button);
}
bool GameEndAlternateScoreScreen::key(std::string_view key_name) {
    if (phase_==AlternateScorePhase::AwaitAck) return false;
    if (phase_!=AlternateScorePhase::Calculator) return false;
    if (key_name=="ESC") return false;
    if (key_name=="BACKSPACE") return activate(10);
    if (key_name=="ENTER") return activate(13);
    if (key_name=="SPACE") return activate(12);
    // D/R/C/J/N are mouse legends. BalanceFromGame does not map letter keystrokes.
    return text(key_name);
}
bool GameEndAlternateScoreScreen::cancel() { return activate(14); }
bool GameEndAlternateScoreScreen::text(std::string_view input) {
    if (phase_!=AlternateScorePhase::Calculator) return false;
    bool handled=false;
    for (unsigned char ch:input) {
        if (ch>='0' && ch<='9') handled=activate(ch-'0') || handled;
        else if (ch==8) handled=activate(10) || handled;
        else if (ch==13) { activate(13); return true; }
        else if (ch==' ') handled=activate(12) || handled;
    }
    return handled;
}
bool GameEndAlternateScoreScreen::mouse_down(int x, int y) {
    if (phase_!=AlternateScorePhase::Calculator) return click(x,y);
    if (x>=window_x_ && x<=window_x_+113 && y>=window_y_ && y<=window_y_+16) {
        dragging_=true; drag_x_=x-window_x_; drag_y_=y-window_y_; return true;
    }
    return click(x,y);
}
bool GameEndAlternateScoreScreen::mouse_move(int x, int y) {
    if (!dragging_) return false;
    window_x_=std::clamp(x-drag_x_,0,207);
    window_y_=std::clamp(y-drag_y_,0,104);
    return true;
}
bool GameEndAlternateScoreScreen::mouse_up() {
    const bool handled=dragging_; dragging_=false; return handled;
}
bool GameEndAlternateScoreScreen::advance_point() {
    if (phase_!=AlternateScorePhase::Points || points_drawn_==value_) return false;
    ++points_drawn_; return true;
}
bool GameEndAlternateScoreScreen::finish_points() {
    if (phase_!=AlternateScorePhase::Points || points_drawn_!=value_) return false;
    phase_=AlternateScorePhase::AwaitAck; return true;
}
bool GameEndAlternateScoreScreen::acknowledge() {
    if (phase_!=AlternateScorePhase::AwaitAck) return false;
    phase_=AlternateScorePhase::Closed; return true;
}

void GameEndAlternateScoreScreen::draw_text(Image& out, int x, int baseline,
        std::string_view bytes, uint8_t color, bool gradient) const {
    const int top=baseline-assets_.font.baseline;
    unsigned slot=0;
    if (gradient && gradient_text_ && assets_.exe.u8(::bmh::layout::TextGradientMode)==2 && color!=0x40)
        while (slot<32 && assets_.exe.u8(::bmh::layout::Advertising_Text_RuntimeField2+slot)!=color) ++slot;
    else slot=32;
    if (slot==32) { assets_.font.draw(out,x,top,bytes,color); return; }
    int pen_x=x;
    for (unsigned char ch:bytes) {
        const auto& glyph=assets_.font.glyph(ch);
        for (int row=0; row<assets_.font.height; ++row) {
            const uint8_t ink=assets_.exe.u8(::bmh::layout::Advertising_Text_RuntimeField+slot*6+size_t(row));
            if (!ink) continue;
            for (int col=0; col<glyph.bit_width; ++col) {
                const int bit=glyph.bit_offset+col;
                if (assets_.font.bitmap[size_t(row)*assets_.font.modulo+size_t(bit/8)] & (0x80>>(bit%8))) {
                    const int px=pen_x+col,py=top+row;
                    if (px>=0 && px<out.width && py>=0 && py<out.height) out.at(px,py)=ink;
                }
            }
        }
        pen_x+=glyph.advance;
    }
}
Image GameEndAlternateScoreScreen::render(std::optional<unsigned> pressed) const {
    if (phase_==AlternateScorePhase::Cancelled) return backdrop_;
    if (phase_!=AlternateScorePhase::Calculator) {
        Image out(320,240);
        for (unsigned i=0; i<points_drawn_; ++i) out.at(10+int(i%52)*6,4+int(i/52)*3)=0x27;
        if (phase_==AlternateScorePhase::AwaitAck || phase_==AlternateScorePhase::Closed)
            for (unsigned i=0; i<2; ++i)
                draw_text(out,159-assets_.font.text_width(assets_.texts[i+1])/2,230+int(i)*7,
                          assets_.texts[i+1],0x5f,true);
        return out;
    }
    Image out=backdrop_;
    const int x0=window_x_,y0=window_y_;
    copy(out,assets_.calculator,0,0,x0,y0,113,136);
    // ColorBottomRightBevel uses inclusive rectangles, clipped to 319/239 at the right/bottom.
    const int shadow_x=std::min(318,x0+113)+1,shadow_y=std::min(238,y0+136)+1;
    fill(out,x0+2,y0+136,shadow_x,shadow_y,0x40);
    fill(out,x0+113,y0+2,shadow_x,shadow_y,0x40);
    draw_text(out,x0+8+(105-8)/2-assets_.font.text_width(assets_.texts[0])/2,y0+14,
              assets_.texts[0],0x62,true);
    constexpr std::string_view keys="0123456789DRCJN";
    for (unsigned i=0; i<15; ++i) {
        const int p=pressed && *pressed==i ? 1:0;
        const int x=x0+assets_.exe.u16(::bmh::layout::Advertising_Click_RuntimeField+2*i),y=y0+assets_.exe.u8(::bmh::layout::Advertising_Click_RuntimeField2+i);
        copy(out,assets_.calculator,15*p,136,x+p,y+p,15-p,14-p);
        draw_text(out,x+p+5,y+p+9,keys.substr(i,1),0x62,false);
    }
    for (unsigned i=0; i<8; ++i) {
        const int x=x0+assets_.exe.u16(::bmh::layout::Advertising_Click_RuntimeField+2*(i+15)),y=y0+assets_.exe.u8(::bmh::layout::Advertising_Click_RuntimeField2+i+15);
        const auto value=grouped(presets[i]);
        draw_text(out,x+47-assets_.font.text_width(value),y+12,value,0x62,true);
    }
    const auto value=grouped(value_);
    const int x=x0+assets_.exe.u16(::bmh::layout::Advertising_Click_RuntimeField+2*23),y=y0+assets_.exe.u8(::bmh::layout::Advertising_Click_RuntimeField2+23);
    draw_text(out,x+47-assets_.font.text_width(value),y+12,value,0,true);
    return out;
}

} // namespace bmh::ui
