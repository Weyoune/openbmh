// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/strengths.hpp"

#include <algorithm>
#include <sstream>
#include "ui/manager.hpp"

namespace bmh::ui {
namespace {
void copy(Image& out, const Image& src, int sx, int sy, int x, int y, int w, int h, int transparent = -1) {
    if (sx<0||sy<0||x<0||y<0||w<0||h<0||sx+w>src.width||sy+h>src.height||x+w>out.width||y+h>out.height)
        throw FormatError("strengths: Original atlas bounds");
    for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx) {
        const auto v=src.pixels[size_t(sy+yy)*size_t(src.width)+size_t(sx+xx)]; if(v!=transparent)out.at(x+xx,y+yy)=v;
    }
}
void fill(Image& out,int x,int y,int w,int h,uint8_t c) {
    for(int yy=y;yy<y+h;++yy)for(int xx=x;xx<x+w;++xx)out.at(xx,yy)=c;
}
std::string number(unsigned value,int width) {
    auto s=std::to_string(value);if(int(s.size())<width)s.insert(0,size_t(width-int(s.size())),'%');return s;
}
}
StrengthsScreen::StrengthsScreen(const std::filesystem::path& data,StrengthsState state)
    :exe_(load_le(asset_path(data,"BMH.exe"))),texts_(load_textd(asset_path(data,"TEXTD.BLK"))),
     frame_(load_vga(asset_path(data/"KB","129.VGA"))),panel_(load_vga(asset_path(data/"KB","56.VGA"))),
     arrows_(load_vga(asset_path(data/"KB","1.VGA"))),widgets_(load_vga(asset_path(data/"KB","0.VGA"))),
     font_(load_font(asset_path(data/"F","BMH3.FNT"))),sprite_(data),state_(std::move(state)) {
    const auto& id=state_.identity;const auto& p=state_.projection;
    if(id.manager<0||id.manager>3||id.league<0||id.league>8||!id.scope||
       p.manager_slot!=id.manager||p.league!=id.league||state_.league_group<0||state_.league_group>1||
       id.expected_settings_word!=p.words.post_draw_settings_word||
       p.rows.size()!=size_t(exe_.u8(::bmh::layout::LeagueSizes+uint32_t(id.league)))||p.age_scratch.league!=id.league||
       p.age_scratch.tenths.size()!=p.rows.size()||state_.title.empty()||state_.subtitle.empty())
        throw FormatError("strengths: incomplete owned projection");
    (void)render();
}
void StrengthsScreen::text(Image& out,int x,int baseline,std::string_view bytes,uint8_t color) const {
    const int width=font_.text_width(bytes);if(x<0||x+width>out.width||baseline<5||baseline>=out.height)return;
    Image mask(std::max(1,width),font_.height);font_.draw(mask,0,0,bytes,1);
    int index=0;while(index<32&&exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2+uint32_t(index))!=color)++index;
    for(int yy=0;yy<font_.height;++yy)for(int xx=0;xx<width;++xx)if(mask.at(xx,yy))
        out.at(x+xx,baseline-font_.baseline+yy)=state_.gradient&&index<32?
            exe_.u8(::bmh::layout::Advertising_Text_RuntimeField+uint32_t(index*6+yy)):color;
}
void StrengthsScreen::button(Image& out,int x,int y,int width,std::string_view label,bool selected) const {
    int dx=x,dy=y;
    if(selected) {const auto bg=out.at(x,y+10);fill(out,x,y,width+1,2,bg);fill(out,x,y,2,11,bg);dx+=2;dy+=2;}
    else {fill(out,x+2,y+10,width+1,2,0x40);fill(out,x+width,y+2,2,10,0x40);}
    const int sy=selected?44:74;copy(out,widgets_,24,sy,dx,dy,31,10);
    int used=31;while(width-used>20) {const int n=std::min(8,width-used-20);copy(out,widgets_,24,sy+10,dx+used,dy,n,10);used+=n;}
    const int left=width-used;copy(out,widgets_,45-left,sy+20,dx+used,dy,left,10);
    const auto utf8=original_to_utf8(std::string(label));
    sprite_.draw(out,dx+width/2-sprite_.text_width(utf8)/2,dy+7,utf8,exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + uint32_t(selected)));
}
void StrengthsScreen::selector(Image& out,int x,int y,int style) const {
    const int height=exe_.u8(::bmh::layout::Strengths_Selector_RuntimeField);copy(out,arrows_,198,0,x,y,55,38);
    copy(out,arrows_,198,76-height,x,y+37,55,height-38);
    if(style<3)copy(out,arrows_,exe_.u8(::bmh::layout::LeaguePairings_Selector_RuntimeField3+uint32_t(style)),0,x+1,y+exe_.u8(::bmh::layout::Strengths_Selector_RuntimeField2),
        exe_.u8(::bmh::layout::LeaguePairings_Selector_RuntimeField4+uint32_t(style)),38);
}
int StrengthsScreen::row_step() const {
    const int original=exe_.u8(layout::Strengths_PointerDown_RuntimeField+uint32_t(state_.identity.league));
    return exe_.expanded_profile() ? std::min(original, 144/int(state_.projection.rows.size())) : original;
}
Image StrengthsScreen::render() const {
    Image out=state_.decorated?frame_:Image(320,240);
    if(!state_.decorated) {fill(out,0,4,320,13,0x44);fill(out,1,5,318,11,0x39);fill(out,0,228,320,10,0x44);fill(out,1,229,318,8,0x39);}
    const auto title=original_to_utf8(state_.title);sprite_.draw(out,SpriteFont::centered_title_x(sprite_.text_width(title)),13,title);
    sprite_.draw(out,5,235,original_to_utf8(state_.subtitle));
    const int step=row_step(),bottom=step*int(state_.projection.rows.size())+27;
    if(state_.original_panel)copy(out,panel_,0,7,1,27,318,bottom-27);
    else fill(out,0,26,320,bottom-25,0x42);
    fill(out,0,26,320,1,0x46);fill(out,0,26,1,bottom-25,0x46);
    fill(out,0,bottom,320,1,0x40);fill(out,319,26,1,bottom-25,0x40);
    const uint16_t word=state_.projection.words.post_draw_settings_word;
    unsigned emphasis=0;
    if(word&0x200) {copy(out,arrows_,188,16,exe_.u16(::bmh::layout::Strengths_Fill_RuntimeField3+10),18,10,8);emphasis=0x20;}
    else if((word&0xff)==0xff) {copy(out,arrows_,188,16,exe_.u16(::bmh::layout::Strengths_Fill_RuntimeField3+8),18,10,8);emphasis=0x10;}
    else for(int n=0;n<4;++n)if(word&(1u<<n)) {copy(out,arrows_,188,16,exe_.u16(::bmh::layout::Strengths_Fill_RuntimeField3+uint32_t(n*2)),18,10,8);emphasis|=1u<<n;}
    for(size_t n=0;n<state_.projection.rows.size();++n) {
        const auto& row=state_.projection.rows[n];const int y=33+int(n)*step;
        text(out,5,y,number(unsigned(n+1),2)+exe_.cstring(::bmh::layout::CareerSession_FormatError_TextPointer),0x68);
        text(out,18,y,utf8_to_original(row.team_name),row.name_color);
        const std::string metrics=exe_.cstring(::bmh::layout::CupDraw_FormatError_ExecutableField2)+number(row.criterion_values[0],2)+texts_[834]+
            number(row.criterion_values[1],2)+texts_[835]+number(row.criterion_values[2],2)+texts_[836]+
            number(row.criterion_values[3],2)+exe_.cstring(::bmh::layout::CupDraw_FormatError_TextPointer3);
        for(int m=0;m<4;++m)
            text(out,120+20*m,y,metrics.substr(size_t(m*6),5),(emphasis&(1u<<m))?0x6c:0x68);
        auto aggregate=exe_.cstring(::bmh::layout::Strengths_Fill_TextPointer);if(aggregate.size()>1)aggregate[1]=char(0x7f);
        aggregate+=number(row.aggregate_value,2)+exe_.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer11);
        text(out,205,y,aggregate,(emphasis&0x10)?0x6c:0x68);
        auto age=texts_[938];if(!age.empty())age[0]=char(0x7f);
        age+=number(unsigned(row.age_tenths)/10+16,2)+exe_.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer2)+number(unsigned(row.age_tenths)%10,1)+texts_[939];
        text(out,236,y,age,(emphasis&0x20)?0x6c:0x68);
    }
    for(int n=0;n<12;++n)button(out,1+n/4*65,176+n%4*13,63,texts_[103+size_t(n)],pressed_==n||(word&(1u<<n)));
    button(out,202,215,114,texts_[115+size_t(state_.projection.words.raw_settings_word>>15)],pressed_==12);
    selector(out,205,176,3);selector(out,262,176,state_.league_group);
    int league=state_.league_group*5;
    for(int r=0;r<4;++r) {const int count=exe_.u8(::bmh::layout::CalendarLeagueCounts+uint32_t(state_.league_group*4+r));
        for(int n=0;n<count;++n,++league)copy(out,arrows_,188,state_.identity.league==league?8:0,
            233-count*4+n*8,exe_.u8(::bmh::layout::Strengths_Fill_RuntimeField)+exe_.u8(::bmh::layout::Strengths_Fill_RuntimeField2)*r,8,8);}
    return out;
}
int StrengthsScreen::button_at(int x,int y) const {
    for(int n=0;n<12;++n) {const int bx=1+n/4*65,by=176+n%4*13;if(x>=bx&&x<=bx+63&&y>=by&&y<=by+10)return n;}
    return x>=202&&x<=316&&y>=215&&y<=225?12:-1;
}
std::optional<StrengthsRequest> StrengthsScreen::selector_at(int x,int y) const {
    if(x>=205&&x<=260&&y>=176&&y<=216) {
        auto image=render();if(image.at(x,y)==0x19||image.at(x,y)==0x42)return {};
        const int r=(y-exe_.u8(::bmh::layout::Strengths_SelectorAt_RuntimeField))/exe_.u8(::bmh::layout::Strengths_SelectorAt_RuntimeField2);if(r<0||r>=4)return {};
        int column=0,league;
        if(state_.league_group==0) {
            const int count=exe_.u8(::bmh::layout::CalendarLeagueCounts+uint32_t(r));int left=233-count*4;
            while(column<count&&(x<left||x>left+8)) {++column;left+=8;}
            league=exe_.u8(::bmh::layout::CalendarFirstLeague+uint32_t(r))+column;
        } else {
            column=(exe_.u8(::bmh::layout::LeaguePairings_SelectorEvent_RuntimeField + uint32_t(r))*4+x-233)/8;
            league=exe_.u8(::bmh::layout::CareerSession_FormatError_RuntimeField4)+r+column;
        }
        if(league>=0&&league<9&&league!=state_.identity.league)
            return StrengthsRequest{state_.identity,StrengthsAction::select_league,StrengthsControl::condition,league};
    }
    if(x>=262&&x<=317&&y>=176&&y<=216&&state_.secondary_leagues)
        return StrengthsRequest{state_.identity,StrengthsAction::toggle_group};
    return {};
}
void StrengthsScreen::post(StrengthsAction kind,int value,StrengthsControl control) {
    pending_=StrengthsRequest{state_.identity,kind,control,value};rejection_.clear();
}
bool StrengthsScreen::pointer_down(int x,int y,bool right) {
    if(pending_)return false;
    if(right) {post(StrengthsAction::close);return true;}
    if(auto selector=selector_at(x,y)) {pending_=*selector;return true;}
    const int step=row_step();
    const int top=exe_.expanded_profile()?27:24;
    if(y>=top&&y<int(state_.projection.rows.size())*step+top) {
        const int row=(y-top)/step;post(StrengthsAction::team_info,state_.projection.rows[size_t(row)].runtime_team);return true;
    }
    pressed_=button_at(x,y);return pressed_>=0;
}
bool StrengthsScreen::move(int x,int y) {if(pressed_>=0&&button_at(x,y)!=pressed_) {pressed_=-1;return true;}return false;}
bool StrengthsScreen::release(int x,int y) {
    if(pending_)return false;
    const int b=pressed_;pressed_=-1;
    if(b<0||button_at(x,y)!=b)return b>=0;
    if(b==10||b==11)post(b==10?StrengthsAction::pairings:StrengthsAction::table);
    else post(StrengthsAction::control,-1,static_cast<StrengthsControl>(b));
    return true;
}
void StrengthsScreen::reject_action(std::string reason) {
    if(!pending_||reason.empty())throw FormatError("strengths: no pending rejection");
    pending_.reset();pressed_=-1;rejection_=std::move(reason);
}
std::string StrengthsScreen::state_json() const {
    std::ostringstream s;const auto& id=state_.identity;s<<"{\"screen\":\"strengths\",\"manager\":"<<id.manager
        <<",\"runtime_club\":"<<id.runtime_club<<",\"league\":"<<id.league<<",\"revision\":"<<id.revision
        <<",\"scope\":"<<id.scope<<",\"sequence\":"<<id.sequence<<",\"settings_word\":"<<id.expected_settings_word
        <<",\"family_word\":"<<id.expected_family_word<<",\"league_group\":"<<state_.league_group
        <<",\"pressed_button\":"<<pressed_<<",\"pending\":"<<(pending_?"true":"false")<<",\"last_rejection\":"<<json_quote(rejection_)<<",\"rows\":[";
    for(size_t n=0;n<state_.projection.rows.size();++n) {if(n)s<<',';const auto& r=state_.projection.rows[n];s<<"{\"team\":"<<r.runtime_team<<",\"value\":"<<r.aggregate_value<<",\"age_tenths\":"<<unsigned(r.age_tenths)<<'}';}
    s<<"]}";return s.str();
}
} // namespace bmh::ui
