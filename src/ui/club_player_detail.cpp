// SPDX-License-Identifier: GPL-3.0-only
#include "game/state.hpp"
#include "formats/badge_policy.hpp"
#include "formats/executable_layout.hpp"
#include "ui/club_player_detail.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include "ui/manager.hpp"

namespace bmh::ui {
namespace {
void rect(Image& out,int x,int y,int w,int h,uint8_t c) {
    for(int yy=std::max(0,y);yy<std::min(out.height,y+h);++yy)
        for(int xx=std::max(0,x);xx<std::min(out.width,x+w);++xx)out.at(xx,yy)=c;
}
void copy(Image& out,const Image& src,int sx,int sy,int x,int y,int w,int h,int transparent=-1) {
    if(sx<0||sy<0||sx+w>src.width||sy+h>src.height)throw FormatError("player card: original atlas bounds");
    for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx){
        const auto c=src.pixels.at(size_t(sy+yy)*src.width+sx+xx);
        if(c!=transparent&&x+xx>=0&&x+xx<out.width&&y+yy>=0&&y+yy<out.height)out.at(x+xx,y+yy)=c;
    }
}
std::string number(int64_t value,int width=1,bool grouped=false) {
    auto n=std::to_string(value);const bool negative=value<0;
    if(grouped)for(int at=int(n.size())-3;at>int(negative);at-=3)n.insert(size_t(at),".");
    const int digits=int(std::to_string(value).size());
    if(digits<width)n.insert(0,size_t(width-digits),'%');
    return n;
}

void validate(const ClubPlayerDetailProjection& p) {
    if(p.currency_denominator<=0 || p.statistics_months>12 || p.redraw.context.original_name.empty())
        throw FormatError("club player detail: invalid owned projection");
    if(!p.redraw.context.human_loan && !p.redraw.market_value)
        throw FormatError("club player detail: Core valuation required");
}
} // namespace

ClubPlayerDetailProjection club_player_detail_from_game(const GameState& g,
    ClubPlayerDetailRedraw redraw,const LeImage& exe) {
    ClubPlayerDetailProjection p;p.redraw=std::move(redraw);
    const auto& c=p.redraw.context;
    const int nation=c.attributes.nationality()+((c.bonus&0x80)?32:0);
    p.original_nationality_name=g.club_name(exe.u16(::bmh::layout::ClubPlayerDetail_ClubPlayerDetailFromGame_RuntimeField3)+exe.u16(::bmh::layout::NoTeamSquadStart)+nation);
    for(int i=0;i<2;++i){
        const int id=i?c.secondary_club:c.primary_club;if(id<0||id==0xffff)continue;
        ClubPlayerDetailClub club;club.runtime_team=id;club.original_full_name=g.club_name(id);club.original_short_name=club.original_full_name;
        if(id<runtime_dimensions.record_teams){
            const uint8_t raw=g.club_short_offset(id);
            club.original_short_name=club.original_short_name.substr(raw>>4,(raw&15)+1);
        }
        // SourceBadgeFallback consults the selector only for real atlas clubs.
        if(!original_badge_fallback(id,&exe)){
            if(size_t(id)*2+1>=g.club_heap_78.size())throw FormatError("club player detail: truncated badge table");
            club.badge_selector=le16(g.club_heap_78,size_t(id)*2)&0x1ff;
        }
        p.clubs[size_t(i)]=std::move(club);
    }
    p.currency_numerator=int(exe.u32(::bmh::layout::CurrencyMultiplier));p.currency_denominator=int(exe.u32(::bmh::layout::CurrencyDivisor));
    if(g.memory.has(::bmh::layout::ClubPlayerDetail_ClubPlayerDetailFromGame_RuntimeField))p.currency_numerator=int(g.memory.u32(::bmh::layout::CurrencyMultiplier));
    if(g.memory.has(::bmh::layout::ClubPlayerDetail_ClubPlayerDetailFromGame_RuntimeField2))p.currency_denominator=int(g.memory.u32(::bmh::layout::CurrencyDivisor));
    p.gradient_text=(!g.memory.has(::bmh::layout::TextGradientDisabled)||g.memory.u8(::bmh::layout::TextGradientDisabled)==0)&&exe.u8(::bmh::layout::TextGradientMode)==2;
    p.injury_as_text=g.memory.has(::bmh::layout::ClubPlayerDetail_ClubPlayerDetailFromGame_RuntimeField4)?g.memory.u8(::bmh::layout::ClubPlayerDetail_ClubPlayerDetailFromGame_RuntimeField4)!=0:exe.u8(::bmh::layout::ClubPlayerDetail_ClubPlayerDetailFromGame_RuntimeField4)!=0;
    if(c.board && !c.human_loan){
        const size_t month=size_t(c.active_manager)*0xb0c+0x286;
        if(month>=g.manager_records.size())throw FormatError("club player detail: missing active statistics months");
        p.statistics_months=g.manager_records[month];
        const uint32_t flags=::bmh::layout::Flags+uint32_t(c.active_manager)*3+2;
        if(g.memory.has(flags))p.statistics_flags=g.memory.u8(flags);
    }
    if(c.national_mode==0 && c.board && !c.human_loan && c.active_offer>=0){
        const uint32_t offer=g.memory.u32(::bmh::layout::OfferSlots+uint32_t(c.active_offer)*4);
        if(offer&0x80000000){
            // The offer-record amount field; local34 is offer index+1.
            const uint32_t amount=::bmh::layout::EditorList+uint32_t(40+c.active_offer)*0x58+12;
            if(!g.memory.has(amount+3))throw FormatError("club player detail: missing offered amount");
            p.displayed_offer_amount=g.memory.u32(amount);
        }
    }
    validate(p);return p;
}

ClubPlayerDetailScreen::ClubPlayerDetailScreen(const std::filesystem::path& data,
    ClubPlayerDetailProjection p,Image parent):projection_(std::move(p)),
    exe_(load_le(asset_path(data,"BMH.exe"))),
    small_(load_font(asset_path(data/"F","BMH3.FNT"))),heading_(load_font(asset_path(data/"F","BMH2.FNT"))),
    texts_(load_textd(asset_path(data,"TEXTD.BLK"))),widgets_(load_vga(asset_path(data/"KB","0.VGA"))),
    atlas_(load_vga(asset_path(data/"KB","2.VGA"))),flags_(load_vga(asset_path(data/"KB","24.VGA"))),
    portrait_(load_vga(asset_path(data/"KB","29.VGA"))),letters_(load_vga(asset_path(data/"KB","54.VGA"))),parent_(std::move(parent)) {
    validate(projection_);
    if(parent_.width!=320||parent_.height!=240)throw FormatError("club player detail: parent must be320x240");
    if(atlas_.width==314){Image padded(320,atlas_.height);padded.blit(atlas_,0,0);atlas_=std::move(padded);}
    for(size_t i=0;i<2;++i)if(projection_.clubs[i]){
        auto& club=*projection_.clubs[i];const int id=club.runtime_team;
        if(!original_badge_fallback(id,&exe_)){
            badges_[i]=load_vga(asset_path(data/"W",std::to_string(club.badge_selector>>3)+".VGA"));
        }
        // TextWidth national/foreign names: longest word, then remove two
        // trailing characters until FNT3 fits57 pixels.
        if(id>=runtime_dimensions.record_teams && small_.text_width(club.original_short_name)>57){
            std::string longest;size_t at=0;
            while(at<club.original_short_name.size()){
                const size_t end=club.original_short_name.find(' ',at);
                const auto word=club.original_short_name.substr(at,end==std::string::npos?end:end-at);
                if(word.size()>longest.size())longest=word;
                if(end==std::string::npos)break;
                at=end+1;
            }
            if(!longest.empty())club.original_short_name=longest;
            while(small_.text_width(club.original_short_name)>57){
                const size_t n=club.original_short_name.size();if(n<2)break;club.original_short_name.resize(n-2);
            }
        }
    }
}
const std::string& ClubPlayerDetailScreen::label(uint32_t address) const {return texts_[(address-::bmh::layout::TextPointerTable)/4];}
void ClubPlayerDetailScreen::text(Image& out,int x,int baseline,std::string_view value,uint8_t color,int right,bool heading) const {
    const auto& font=heading?heading_:small_;const int width=font.text_width(value);
    if(right>=0)x+=(right-x)/2-width/2;
    if(x<0||x+width>out.width||baseline<font.baseline||baseline>=out.height)return;
    if(heading){font.draw(out,x,baseline-font.baseline,value,color);return;}
    Image mask(std::max(1,width),font.height);font.draw(mask,0,0,value,1);
    int gradient=0;while(gradient<32&&exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2+gradient)!=color)++gradient;
    for(int yy=0;yy<font.height;++yy)for(int xx=0;xx<width;++xx)if(mask.at(xx,yy))
        out.at(x+xx,baseline-font.baseline+yy)=projection_.gradient_text?exe_.u8(::bmh::layout::Advertising_Text_RuntimeField+gradient*6+yy):color;
}
void ClubPlayerDetailScreen::frame(Image& out,int x,int y,int w,int h) const {
    for(int yy=4;yy<h-3;++yy)for(int xx=4;xx<w-3;++xx)out.at(x+xx,y+yy)=exe_.u8(::bmh::layout::Advertising_PatternRect_RuntimeField+((yy-4)%16)*16+(xx-4)%16);
    copy(out,widgets_,0,0,x,y,4,4);copy(out,widgets_,4,0,x+w-4,y,4,4);
    copy(out,widgets_,0,4,x,y+h-4,4,4);copy(out,widgets_,4,4,x+w-4,y+h-4,4,4);
    for(int at=4;at<w-4;at+=74){const int n=std::min(74,w-4-at);copy(out,widgets_,0,104,x+at,y,n,4);copy(out,widgets_,0,108,x+at,y+h-4,n,4);}
    for(int at=4;at<h-4;at+=96){const int n=std::min(96,h-4-at);copy(out,widgets_,0,8,x,y+at,4,n);copy(out,widgets_,4,8,x+w-4,y+at,4,n);}
}
void ClubPlayerDetailScreen::button(Image& out,int x,int y,int width,std::string_view value,bool active) const {
    const int height=width>31?10:9;
    if(active){const auto base=out.at(x,y+height);rect(out,x,y,width+1,2,base);rect(out,x,y,2,height+1,base);x+=2;y+=2;}
    else {rect(out,x+2,y+height,width,2,0x40);rect(out,x+width,y+2,2,height,0x40);}
    if(width>31){
        const int sy=active?44:74;
        copy(out,widgets_,24,sy,x,y,31,10);int at=31,remaining=width-31;
        while(remaining>20){const int n=std::min(8,remaining-20);copy(out,widgets_,24,sy+10,x+at,y,n,10);at+=n;remaining-=n;}
        copy(out,widgets_,45-remaining,sy+20,x+at,y,remaining,10);
    }else frame(out,x,y,width,height);
    const int font=exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + int(active));
    if(font==3){text(out,x,y+height/2+2,value,0,x+width);return;}
    int length=0;for(unsigned char c:value){const int g=exe_.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + c);length+=g?exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2+g*2)-exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField+g*2):2;}
    int cursor=x+width/2-length/2;
    for(unsigned char c:value){const int g=exe_.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + c);if(!g){cursor+=2;continue;}
        const int sx=exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField+g*2),n=exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2+g*2)-sx;
        copy(out,letters_,sx,(font-4)*7,cursor,y+height/2-4,n,7,0);cursor+=n;}
}

void ClubPlayerDetailScreen::draw_portrait(Image& out) const {
    const auto& r=projection_.redraw;if(!r.portrait_available)return;
    // Copy2:FormatError2 c0 is opaque. CopyCrop forwards ff as VGA mask;
    // actual14a15 transparency uses DesktopRuntimeField, reset0 by preceding text calls.
    copy(out,portrait_,108+int(r.portrait_base)*38,168,181,109,38,38);
    for(int i=0;i<5;++i){
        int sx=exe_.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField+i),sy=exe_.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField2+i);
        for(int n=0;n<r.portrait_features[size_t(i)];++n){
            sx+=exe_.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField3+i);
            if(sx>exe_.u16(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField7+i*2)){sx=exe_.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField+i);sy+=exe_.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField4+i);}
        }
        copy(out,portrait_,sx,sy,182+exe_.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField5+i),114+exe_.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField6+i),
             exe_.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField3+i),exe_.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField4+i),0);
    }
}

Image ClubPlayerDetailScreen::render() const {
    return statistics_open_?render_statistics():render_card();
}
Image ClubPlayerDetailScreen::render_card() const {
    auto out=restored_card_frame_?*restored_card_frame_:parent_;
    const auto& r=projection_.redraw;const auto& c=r.context;const auto& a=c.attributes;
    const Bytes board=c.board?Bytes(c.board->begin(),c.board->end()):Bytes{};
    const auto underline=[&](int x,int y,std::string_view value){
        rect(out,x,y+1,small_.text_width(value.substr(0,value.size()-1))+4,1,0x69);
    };
    const auto cash=[&](uint32_t n,int width){return number(int64_t(n)*projection_.currency_numerator/projection_.currency_denominator,width,true)+exe_.cstring(::bmh::layout::CurrencySuffixText);};
    if(!restored_card_frame_){rect(out,2,225,318,2,0x40);frame(out,0,38,320,187);}
    for(int y=52;y<=197;++y)for(int x=2;x<=317;++x)
        out.at(x,y)=exe_.u8(::bmh::layout::Advertising_PatternRect_RuntimeField+((y-52)%16)*16+(x-2)%16);
    text(out,4,49,c.original_name,0x6c,-1,true);
    rect(out,3,50,313,1,0x69);rect(out,3,70,313,1,0x69);
    if(c.board){
        const int games=board[0x11]+1;
        const auto seniority=label(::bmh::layout::ClubPlayerDetail_RenderCard_ExecutableField)+number(games)+label(::bmh::layout::ClubPlayerDetail_RenderCard_ExecutableField2)+(games!=1?label(::bmh::layout::ClubPlayerDetail_RenderCard_ExecutableField3):"")+label(::bmh::layout::ClubPlayerDetail_RenderCard_ExecutableField4);
        text(out,314-small_.text_width(seniority),49,seniority,0x6a);
    }
    const auto full_name=[&](int index)->std::string {
        const auto& club=projection_.clubs.at(size_t(index));
        // ClubName2 masks the -1 club sentinel to fff and appends NoClubText.
        return club?club->original_full_name:label(::bmh::layout::NoClubText);
    };
    if(c.national_mode==2){
        text(out,4,58,label(::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer3),0x27);
        text(out,4,66,(c.primary_club<0?full_name(0):label(::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer4)+full_name(0))+exe_.cstring(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField),0x6b);
    }else if(c.human_loan){
        text(out,4,58,label(::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer5)+full_name(1)+exe_.cstring(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField),0x6b);
    }else if(c.loan_record>=0){
        text(out,4,58,label(::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer6)+full_name(0)+label(::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer7)+full_name(1)+exe_.cstring(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField),0x6b);
        text(out,4,66,label(::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer8)+number(c.loan_duration,2)+label(::bmh::layout::ClubPlayerDetail_RenderCard_ExecutableField2)+
            (c.loan_duration!=1?exe_.cstring(::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer):"")+exe_.cstring(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField),0x6b);
    }else text(out,0,63,label(c.primary_club<0?::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer9: ::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer10),0x6b,319);
    text(out,181,106,c.original_name,0x6a,218);frame(out,179,107,42,42);draw_portrait(out);
    text(out,181,155,number(a.age(),2)+label(::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer11),0x6a,218);
    for(int i=0;i<2;++i){
        const int y=80+i*57;frame(out,254,y,42,42);
        if(!projection_.clubs[size_t(i)])continue;
        const auto& club=*projection_.clubs[size_t(i)];
        text(out,256,y-1,label(::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer2+uint32_t(i)*4),0x6a,293);
        rect(out,255,y+1,1,40,0x46);rect(out,255,y+1,40,1,0x46);
        rect(out,255,y+40,40,1,0x40);rect(out,294,y+1,1,40,0x40);
        if(badges_[size_t(i)])copy(out,*badges_[size_t(i)],(club.badge_selector&7)*38,0,256,y+2,38,38);
        else copy(out,widgets_,8,0,256,y+2,38,38);
        text(out,256,y+48,club.original_short_name,0x6a,293);
    }
    if(projection_.clubs[0]){
        copy(out,widgets_,8,93,220,118,15,11,0);
        copy(out,widgets_,8,93,230,projection_.clubs[1]?132:104,15,11,0);
        copy(out,widgets_,8,93,240,projection_.clubs[1]?147:90,15,11,0);
    }
    for(int i=0;i<7;++i){const int x=8+i*43;
        frame(out,x,196,41,9);text(out,x,191,label(::bmh::layout::ClubPlayerDetail_Copy_TextPointer4+i*4),0x6b);underline(x,191,label(::bmh::layout::ClubPlayerDetail_Copy_TextPointer4+i*4));
        text(out,x+2,203,label(::bmh::layout::ClubPlayerDetail_Copy_TextPointer5+a.skill(Player::Skill(i))*4),0x6b);
        copy(out,atlas_,256+a.skill(Player::Skill(i))*16,32,x+34,198,16,5,0);
    }
    for(int i=0;i<6;++i){const int x=(i%2)*80+10,base=exe_.u8(::bmh::layout::ClubPlayerDetail_Copy_RuntimeField+i);
        text(out,x,base,label(::bmh::layout::ClubPlayerDetail_Copy_TextPointer3+i*4),0x6b);underline(x,base,label(::bmh::layout::ClubPlayerDetail_Copy_TextPointer3+i*4));
        std::string value;
        if(i>1&&c.human_loan)value=label(::bmh::layout::ClubPlayerDetail_Copy_TextPointer8);
        else if(i>1&&!c.board&&i!=5)value=label(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField2);
        else if(i==0){
            value=projection_.original_nationality_name;text(out,10,105,exe_.cstring(::bmh::layout::ClubPlayerDetail_Copy_TextPointer),0x6a);
            const int nation=a.nationality()+((c.bonus&0x80)?32:0);
            copy(out,flags_,(nation%12)*9,(nation/12)*6,13,99,9,6);
        }else if(i==1){
            const int unavailable=int(a.w35c>>27),injury=int(a.w364>>16&31);
            if(unavailable==0&&injury==0){
                const int status=(c.displayed_slot&0x8000)?0:(c.displayed_slot&0x4000)?1:2;
                copy(out,atlas_,272,status*5,x+4,base+14,32,5,0);
            }else{
                const int status=unavailable==31?8:unavailable==0?5:injury==0?4:3;
                copy(out,atlas_,197,(status-3)*6+1,x+9,base+14,10,5);
                if(status==3||status==4)value=label(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField3+unavailable*4);
                if(status==5||(status==3&&projection_.injury_as_text))value=label(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField7)+number(int(a.w364>>16&15))+label(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField8);
                else if(status==3)copy(out,atlas_,304-(std::min(int(a.w364>>16&15),11)/3)*16,32,136,100,16,5,0);
            }
        }else if(i==2){
            text(out,x,base+19,board[0xc]==255?label(::bmh::layout::ClubPlayerDetail_Copy_TextPointer9):label(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField7)+number(board[0xc])+label(::bmh::layout::ClubPlayerDetail_Copy_TextPointer10),0x6a);
            if(projection_.displayed_offer_amount)
                text(out,x,base+27,label(::bmh::layout::ClubPlayerDetail_Copy_TextPointer11)+cash(*projection_.displayed_offer_amount,1)+exe_.cstring(::bmh::layout::ClubPlayerDetail_Copy_TextPointer2),0x27);
            value=cash(le32(board,0x50),5)+label(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField4);
        }else if(i==3&&board[0x43]){
            text(out,x,base+19,cash(le16(board,0x4e)&0x7fff,5)+label(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField4),0x6a);
            text(out,x,base+27,label(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField7)+number(board[0x43]&63)+label(::bmh::layout::ClubPlayerDetail_Copy_TextPointer10),0x6a);
            value=label(::bmh::layout::ClubPlayerDetail_Copy_TextPointer6+(board[0x43]>>6)*4);
        }else if(i==4)value=le32(board,0x54)?cash(le32(board,0x54),1)+exe_.cstring(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField):label(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField9);
        else if(i==5)value=cash(*r.market_value,1)+exe_.cstring(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField);
        text(out,x,base+11,value,0x6a);
    }
    button(out,8,211,60,label(::bmh::layout::ClubPlayerDetail_Copy_TextPointer7),pressed_==0);
    const bool stats=c.board&&!c.human_loan;
    if(stats)button(out,80,211,96,label(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField5),pressed_==1);
    if(c.national_action_available||c.active_offer>=0)
        button(out,stats?182:80,211,122,label(c.national_action_available&&c.active_offer<0?::bmh::layout::ClubPlayerDetail_Copy_ExecutableField2+c.national_mode*4: ::bmh::layout::ClubPlayerDetail_Copy_ExecutableField6),pressed_==2);
    return out;
}

Image ClubPlayerDetailScreen::render_statistics() const {
    if(!projection_.redraw.context.board)throw FormatError("club player statistics: missing Source board");
    auto out=statistics_frame_?*statistics_frame_:render_card();
    if(!statistics_frame_)for(int y=52;y<=210;++y)for(int x=2;x<=317;++x)
        out.at(x,y)=exe_.u8(::bmh::layout::Advertising_PatternRect_RuntimeField+((y-52)%16)*16+(x-2)%16);
    const auto line=[&](int x,int y,int ex,int ey,uint8_t color){
        const int dx=std::abs(ex-x),sx=x<ex?1:-1,dy=std::abs(ey-y),sy=y<ey?1:-1;
        int error=0;
        for(int n=0;n<=std::max(dx,dy);++n){
            if(x>=0&&x<out.width&&y>=0&&y<out.height)out.at(x,y)=color;
            if(dx>dy){x+=sx;error+=dy;if(error>=dx){error-=dx;y+=sy;}}
            else if(dy){y+=sy;error+=dx;if(error>=dy){error-=dy;x+=sx;}}
        }
    };
    if(!statistics_frame_)for(int edge=0;edge<4;++edge)
        line(exe_.u16(::bmh::layout::Analytics_Render_RuntimeField3+edge*4+2),exe_.u8(::bmh::layout::Analytics_Render_RuntimeField2 +edge*2+1),
             exe_.u16(::bmh::layout::Analytics_Render_RuntimeField3+edge*4),exe_.u8(::bmh::layout::Analytics_Render_RuntimeField2 +edge*2),exe_.u8(::bmh::layout::Analytics_Render_RuntimeField+edge));
    else for(int y=52;y<=137;++y)for(int x=12;x<=310;++x)
        out.at(x,y)=exe_.u8(::bmh::layout::Advertising_PatternRect_RuntimeField+((y-52)%16)*16+(x-12)%16);
    for(int curve=0;curve<4;++curve){
        const int x=27+(curve%2)*120,y=142+(curve/2)*11;
        const uint8_t color=exe_.u8(::bmh::layout::Analytics_FormatError_RuntimeField+curve);
        rect(out,x-7,y+3,5,4,color);line(x-8,y+2,x-2,y+2,0x68);
        line(x-8,y+2,x-8,y+7,0x68);line(x-2,y+3,x-2,y+7,0x62);
        line(x-8,y+7,x-2,y+7,0x62);out.at(x-8,y+2)=0x6a;
        button(out,x,y,96,exe_.cstring(exe_.u32(::bmh::layout::Analytics_FormatError_ExecutableField+(12+curve)*4)),
               pressed_==curve+10 || (projection_.statistics_flags&(1<<curve))!=0);
        if(!(projection_.statistics_flags&(1<<curve)))continue;
        std::vector<int> values;
        const auto& board=*projection_.redraw.context.board;
        for(int month=0;month<projection_.statistics_months;++month){
            int value=0;if(curve<3)value=board[size_t(0x19+curve*12+month)];
            else for(int series=0;series<3;++series)value+=board[size_t(0x19+series*12+month)];
            values.push_back(value);
        }
        int low=int32_t(exe_.u32(::bmh::layout::Analytics_FormatError_RuntimeField3+48+curve*4)),high=int32_t(exe_.u32(::bmh::layout::Analytics_FormatError_RuntimeField4+48+curve*4));
        if(!(projection_.statistics_flags&0x40)&&!values.empty()){
            const auto [lo,hi]=std::minmax_element(values.begin(),values.end());
            const int minimum=*lo,range=*hi-minimum;
            for(auto& value:values)value=range?(value-minimum)*100/range:50;
            low=0;high=100;
        }
        if(low>=high)throw FormatError("club player statistics: invalid Original scale");
        std::vector<std::pair<int,int>> points;
        for(size_t month=0;month<values.size();++month){
            const int value=std::clamp(values[month],low,high);
            points.emplace_back(int(month)*298/17+13,136-int(double(value-low)*83/double(high-low)));
        }
        for(size_t i=0;i<points.size();++i){
            const auto [px,py]=points[i];if(i)line(points[i-1].first,points[i-1].second,px,py,color);
            rect(out,px-1,py-1,3,3,color);
        }
    }
    button(out,265,142,30,label(::bmh::layout::ClubPlayerDetail_RenderStatistics_ExecutableField),pressed_==14 || (projection_.statistics_flags&0x40)!=0);
    button(out,265,153,30,label(::bmh::layout::ClubPlayerDetail_RenderStatistics_ExecutableField2),pressed_==15 || (projection_.statistics_flags&0x80)!=0);
    button(out,8,185,140,label(::bmh::layout::ClubPlayerDetail_RenderStatistics_ExecutableField3),pressed_==16);
    return out;
}

std::optional<int> ClubPlayerDetailScreen::button_at(int x,int y) const {
    const auto inside=[&](int bx,int by,int width){return x>=bx&&x<=bx+width&&y>=by&&y<=by+(width>31?10:9);};
    if(statistics_open_){
        for(int curve=0;curve<4;++curve)if(inside(27+(curve%2)*120,142+(curve/2)*11,96))return curve+10;
        if(inside(265,142,30))return 14;
        if(inside(265,153,30))return 15;
        if(inside(8,185,140))return 16;
        return std::nullopt;
    }
    const auto& c=projection_.redraw.context;const bool stats=c.board&&!c.human_loan;
    if(inside(8,211,60))return 0;
    if(stats&&inside(80,211,96))return 1;
    if((c.national_action_available||c.active_offer>=0)&&inside(stats?182:80,211,122))return 2;
    return std::nullopt;
}
void ClubPlayerDetailScreen::request(ClubPlayerDetailRequestKind kind,int source_return){
    request_={kind,projection_.redraw.context.selection,projection_.statistics_flags,source_return};
}
bool ClubPlayerDetailScreen::finish_statistics(){
    restored_card_frame_=render_statistics();statistics_frame_.reset();
    statistics_open_=false;awaiting_redraw_=true;
    request(ClubPlayerDetailRequestKind::redraw_card);return true;
}
bool ClubPlayerDetailScreen::activate(int id){
    if(id>=10&&id<=13){statistics_frame_=render_statistics();projection_.statistics_flags^=uint8_t(1<<(id-10));return true;}
    if(id==14||id==15){statistics_frame_=render_statistics();projection_.statistics_flags^=0xc0;return true;}
    if(id==16)return finish_statistics();
    if(id==0){closed_=true;request(ClubPlayerDetailRequestKind::close);return true;}
    if(id==1){statistics_open_=true;statistics_frame_.reset();return true;}
    if(id==2){
        closed_=true;
        request(projection_.redraw.context.national_action_available?
                ClubPlayerDetailRequestKind::national_action:ClubPlayerDetailRequestKind::offer_action,0xff);
        return true;
    }
    return false;
}
bool ClubPlayerDetailScreen::move(int x,int y){
    if(!pressed_)return false;
    if(button_at(x,y)!=pressed_){pressed_.reset();return true;}return false;
}
bool ClubPlayerDetailScreen::press(int x,int y,bool right){
    if(closed_||awaiting_redraw_)return false;
    if(right){pressed_.reset();if(statistics_open_)return finish_statistics();closed_=true;request(ClubPlayerDetailRequestKind::close);return true;}
    pressed_=button_at(x,y);if(pressed_)return true;
    if(!statistics_open_){closed_=true;request(ClubPlayerDetailRequestKind::close);return true;}return false;
}
bool ClubPlayerDetailScreen::release(int x,int y,bool right){
    if(right||closed_||awaiting_redraw_||!pressed_)return false;
    const auto chosen=pressed_;pressed_.reset();if(button_at(x,y)!=chosen)return true;
    return activate(*chosen);
}
bool ClubPlayerDetailScreen::held_left_ticks(uint32_t ticks){
    if(ticks<=25||closed_||awaiting_redraw_||statistics_open_||projection_.redraw.context.human_loan)return false;
    pressed_.reset();closed_=true;
    request(ClubPlayerDetailRequestKind::close,projection_.redraw.context.source_slot+1);return true;
}
bool ClubPlayerDetailScreen::process_timer_expired(){
    if(closed_||awaiting_redraw_||pressed_||request_.kind!=ClubPlayerDetailRequestKind::none)return false;
    if(statistics_open_)return finish_statistics(); // CopyOpaqueSlice returns before42dbd rebuilds.
    closed_=true;
    request(ClubPlayerDetailRequestKind::close); //ClubPlayerDetailFromGame returns0; it never chooses an offer/national action.
    return true;
}
ClubPlayerDetailRequest ClubPlayerDetailScreen::take_request(){auto r=request_;request_={};return r;}
void ClubPlayerDetailScreen::replace_redraw(ClubPlayerDetailProjection next){
    if(!awaiting_redraw_)throw FormatError("club player detail: no Source redraw pending");
    validate(next);
    const auto& a=projection_.redraw.context.selection;const auto& b=next.redraw.context.selection;
    if(a.runtime_team!=b.runtime_team||a.player_id!=b.player_id||a.rendered_row!=b.rendered_row||
       a.physical_row!=b.physical_row||a.human_loan_index!=b.human_loan_index||
       a.origin!=b.origin||a.market_pool_slot!=b.market_pool_slot)
        throw FormatError("club player detail: redraw identity changed");
    projection_=std::move(next);awaiting_redraw_=false;pressed_.reset();
}
} // namespace bmh::ui
