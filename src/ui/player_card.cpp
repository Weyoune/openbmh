// SPDX-License-Identifier: GPL-3.0-only
#include "game/state.hpp"
#include "formats/badge_policy.hpp"
#include "formats/executable_layout.hpp"
#include "ui/player_card.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "formats/savestate.hpp"
#include "game/state.hpp"
#include "ui/manager.hpp"

namespace bmh::ui {
namespace {
Bytes block(const SaveState& s,uint32_t address) {
    const auto& b=s.block(address);
    return Bytes(s.stream().begin()+b.offset,s.stream().begin()+b.offset+b.size);
}
std::string pool_name(const Bytes& pool,size_t at) {
    if(at>=pool.size())throw FormatError("player card: name outside string pool");
    const auto end=std::find(pool.begin()+at,pool.end(),0);
    if(end==pool.end())throw FormatError("player card: unterminated name");
    return std::string(pool.begin()+at,end);
}
void validate(const PlayerCardState& s) {
    if(s.row<0||size_t(s.row)>=s.lineup.players.size()||s.lineup.players.at(size_t(s.row)).player_id<0)
        throw FormatError("player card: nonempty squad row required");
    if(s.lineup.manager_slot<0||s.lineup.manager_slot>3||s.lineup.runtime_club<0||s.badge<0||s.badge>511||s.money_denominator<=0)
        throw FormatError("player card: invalid manager/club/currency");
    const auto& p=s.lineup.players.at(size_t(s.row));
    if(p.player_id!=(p.slot&0xfff)||p.row!=s.row)throw FormatError("player card: inconsistent player identity");
    if(s.statistics && s.statistics->months>12)throw FormatError("player statistics: more than twelve monthly samples");
    if(s.loan&&(s.loan->owner<0||s.loan->owner>=runtime_dimensions.record_teams||s.loan->borrower!=s.lineup.runtime_club||
        s.loan->borrower>=runtime_dimensions.record_teams||s.loan->months<0||s.loan->months>255||
        std::any_of(s.loan->badges.begin(),s.loan->badges.end(),[](int badge){return badge<0||badge>511;})))
        throw FormatError("player card: inconsistent ordinary loan context");
}
PlayerCardState project(LineupState lineup,const LeImage& exe,int row,const Bytes& club_heap,
                        const std::vector<uint16_t>& offsets,const Bytes& names,const Bytes& short_names) {
    PlayerCardState out;out.lineup=std::move(lineup);out.row=row;
    if(row<0||size_t(row)>=out.lineup.players.size()||out.lineup.players.at(size_t(row)).player_id<0)
        throw FormatError("player card: empty or invalid squad row");
    const auto& p=out.lineup.players.at(size_t(row));
    out.badge=le16(club_heap,size_t(out.lineup.runtime_club)*2)&511;
    const int nation=p.attributes.nationality()+((p.bonus&0x80)?32:0);
    const size_t nation_club=exe.u16(::bmh::layout::ClubPlayerDetail_ClubPlayerDetailFromGame_RuntimeField3)+exe.u16(::bmh::layout::NoTeamSquadStart)+nation;
    out.nationality_name=pool_name(names,offsets.at(nation_club));
    if (out.lineup.runtime_club == kNoTeam) {
        // The national runtime slot is absent from the ordinary short-name
        // table; its projected club name is already the full country label.
        out.club_short_name=out.lineup.club_name;
    } else {
        const uint8_t short_name=short_names.at(size_t(out.lineup.runtime_club));
        out.club_short_name=out.lineup.club_name.substr(short_name>>4,(short_name&15)+1);
    }
    out.money_numerator=exe.u32(::bmh::layout::CurrencyMultiplier);out.money_denominator=exe.u32(::bmh::layout::CurrencyDivisor);
    validate(out);return out;
}
void context(PlayerCardState& out,const StaticMemory& memory,const Bytes& club_heap,
             const std::vector<uint16_t>& offsets,const Bytes& names,const Bytes& short_names) {
    const int player=out.lineup.players.at(size_t(out.row)).player_id;
    for(int i=0;i<200;++i)if(memory.has(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+i*4)&&memory.has(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+i*4+3)){
        const auto loan=memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+i*4);
        if(loan&&(loan&0xfff)==uint32_t(player)){
            // BoughtEntry supplies both clubs and the duration to42dbd. This is
            // the ordinary borrower row, not the lender's ClubInfo_FormatError_RuntimeField snapshot.
            PlayerCardLoan detail;
            detail.owner=int(loan>>12&0x3ff);detail.borrower=int(loan>>22&0x3ff);
            if(detail.borrower!=out.lineup.runtime_club||detail.owner>=runtime_dimensions.record_teams||detail.borrower>=runtime_dimensions.record_teams||out.loan||!memory.has(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField+i)){
                out.unsupported_record="loan_record";continue;
            }
            detail.months=memory.u8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField+i);
            for(int n=0;n<2;++n){
                const size_t club=size_t(n?detail.borrower:detail.owner);
                if(club>=short_names.size()||club>=offsets.size()||club*2+1>=club_heap.size()){
                    out.unsupported_record="loan_record";break;
                }
                detail.badges[size_t(n)]=le16(club_heap,club*2)&511;
                detail.full_names[size_t(n)]=pool_name(names,offsets.at(club));
                const uint8_t span=short_names.at(club);
                detail.short_names[size_t(n)]=detail.full_names[size_t(n)].substr(span>>4,(span&15)+1);
            }
            if(out.unsupported_record.empty())out.loan=std::move(detail);
        }
    }
    for(int i=0;i<4;++i)if(memory.has(::bmh::layout::OfferSlots+i*4)&&memory.has(::bmh::layout::OfferSlots+i*4+3)){
        const auto offer=memory.u32(::bmh::layout::OfferSlots+i*4);
        if((offer&31)==uint32_t(out.lineup.manager_slot)&&((offer>>10)&0xfff)==uint32_t(player))out.unsupported_record="negotiation_offer";
    }
}
void rect(Image& out,int x,int y,int w,int h,uint8_t c) {
    for(int yy=std::max(0,y);yy<std::min(out.height,y+h);++yy)
        for(int xx=std::max(0,x);xx<std::min(out.width,x+w);++xx)out.at(xx,yy)=c;
}
void copy(Image& out,const Image& src,int sx,int sy,int x,int y,int w,int h,int transparent=-1) {
    if(sx<0||sy<0||sx+w>src.width||sy+h>src.height)
        throw FormatError("player card: original atlas bounds "+std::to_string(sx)+","+std::to_string(sy)+
            ":"+std::to_string(w)+"x"+std::to_string(h)+" in "+std::to_string(src.width)+"x"+std::to_string(src.height));
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
} // namespace

PlayerCardState player_card_from_save(const SaveState& s,const LeImage& exe,int row,uint64_t revision) {
    const auto offsets=block(s,::bmh::layout::ClubIdentities);std::vector<uint16_t> names;
    for(size_t at=0;at<offsets.size();at+=2)names.push_back(le16(offsets,at));
    auto out=project(lineup_from_save(s,exe,revision),exe,row,block(s,::bmh::layout::ClubBadgeSelectors),names,s.pool(0),block(s,::bmh::layout::ClubShortNameOffsets));
    context(out,StaticMemory(s),block(s,::bmh::layout::ClubBadgeSelectors),names,s.pool(0),block(s,::bmh::layout::ClubShortNameOffsets));
    out.statistics=PlayerStatisticsContext{s.data(::bmh::layout::ManagerRecords)[size_t(out.lineup.manager_slot)*0xb0c+0x286],
        s.data(::bmh::layout::Flags)[size_t(out.lineup.manager_slot)*3+2]};
    validate(out);
    // layout field checks available heap; the Linux renderer has enough memory and
    // reconstructs the same deterministic face from the name without engine RNG.
    return out;
}
PlayerCardState player_card_from_game(const GameState& g,const LeImage& exe,int row,uint64_t revision) {
    Bytes short_names;
    for(int club=0;club<exe.u16(::bmh::layout::CareerSession_DataDirectory_RuntimeField2);++club)short_names.push_back(g.club_short_offset(club));
    auto out=project(lineup_from_game(g,exe,revision),exe,row,g.club_heap_78,g.club_ids,g.club_names,short_names);
    context(out,g.memory,g.club_heap_78,g.club_ids,g.club_names,short_names);
    const size_t months=size_t(out.lineup.manager_slot)*0xb0c+0x286;
    const uint32_t flags=::bmh::layout::Flags+uint32_t(out.lineup.manager_slot)*3+2;
    if(months<g.manager_records.size() && g.memory.has(flags))
        out.statistics=PlayerStatisticsContext{g.manager_records[months],g.memory.u8(flags)};
    if(g.memory.has(::bmh::layout::CurrencyMultiplier)&&g.memory.has(::bmh::layout::ClubPlayerDetail_ClubPlayerDetailFromGame_RuntimeField))out.money_numerator=int(g.memory.u32(::bmh::layout::CurrencyMultiplier));
    if(g.memory.has(::bmh::layout::CurrencyDivisor)&&g.memory.has(::bmh::layout::ClubPlayerDetail_ClubPlayerDetailFromGame_RuntimeField2))out.money_denominator=int(g.memory.u32(::bmh::layout::CurrencyDivisor));
    validate(out);return out;
}
std::optional<uint32_t> player_card_market_value(const Player& p,const LeImage& exe) {
    // Zero-year projection: no future skill growth tables are involved.
    const int age_index=std::min((p.age()-16)+1,20);
    int a=((p.technique()+p.condition())*5)/2+150-std::abs(11-age_index)*2;
    a=std::clamp(a,12,620);
    if(a>500){if(a<600)a+=exe.u8(::bmh::layout::PlayerCard_PlayerCardMarketValue_RuntimeField + uint32_t(a-500)/10);else a+=100;}
    a=((a/4+1)*a*(a/8+100))/10;
    int position_factor=int(p.w364>>26)/4;
    if(p.position()==0)position_factor+=110;
    else {
        const int skills=int(((p.w364>>10&63)*3+(p.w368>>7&63))>>1);
        position_factor+=100+((p.position()*skills/3)>>1);
    }
    const uint32_t value=uint32_t(position_factor*a/100);
    if(value<50000)return std::nullopt;
    return ((value+(value>>2))/10000)*10000;
}

std::vector<std::pair<int,int>> player_statistics_points(const PlayerCardState& state,const LeImage& exe,int curve,bool absolute) {
    validate(state);
    if(!state.statistics||curve<0||curve>3)throw FormatError("player statistics: missing context or invalid curve");
    const auto& board=state.lineup.players.at(size_t(state.row)).board;
    std::vector<int> values;
    for(int month=0;month<state.statistics->months;++month){
        int value=0;
        if(curve<3)value=board[size_t(0x19+curve*12+month)];
        else for(int series=0;series<3;++series)value+=board[size_t(0x19+series*12+month)];
        values.push_back(value);
    }
    int low=int32_t(exe.u32(::bmh::layout::Analytics_FormatError_RuntimeField3+48+curve*4)),high=int32_t(exe.u32(::bmh::layout::Analytics_FormatError_RuntimeField4+48+curve*4));
    if(!absolute&&!values.empty()){
        const auto [lo,hi]=std::minmax_element(values.begin(),values.end());const int minimum=*lo,range=*hi-minimum;
        for(auto& value:values)value=range?(value-minimum)*100/range:50;
        low=0;high=100;
    }
    if(low>=high)throw FormatError("player statistics: invalid original graph scale");
    std::vector<std::pair<int,int>> points;
    for(size_t month=0;month<values.size();++month){
        const int value=std::clamp(values[month],low,high);
        // PlotsEachPointThrough: seventeen columns for player history, 83 vertical pixels.
        // Amount2 truncates the floating-point quotient toward zero.
        const int height=int(double(value-low)*83.0/double(high-low));
        points.emplace_back(int(month)*298/17+13,136-height);
    }
    return points;
}

PlayerCardScreen::PlayerCardScreen(const std::filesystem::path& data,PlayerCardState state,std::optional<Image> parent_frame)
    :state_(std::move(state)),exe_(load_le(asset_path(data,"BMH.exe"))),
     small_(load_font(asset_path(data/"F","BMH3.FNT"))),heading_(load_font(asset_path(data/"F","BMH2.FNT"))),
     texts_(load_textd(asset_path(data,"TEXTD.BLK"))),widgets_(load_vga(asset_path(data/"KB","0.VGA"))),
     atlas_(load_vga(asset_path(data/"KB","2.VGA"))),flags_(load_vga(asset_path(data/"KB","24.VGA"))),
     portrait_(load_vga(asset_path(data/"KB","29.VGA"))),letters_(load_vga(asset_path(data/"KB","54.VGA"))) {
    validate(state_);
    if(!state_.unsupported_record.empty())throw FormatError("player card: unsupported "+state_.unsupported_record);
    if(state_.statistics)statistics_flags_=state_.statistics->display_flags;
    badge_=load_vga(asset_path(data/"W",std::to_string(state_.badge>>3)+".VGA"));
    if(state_.loan)for(size_t i=0;i<2;++i){
        const int club=i?state_.loan->borrower:state_.loan->owner;
        if(!original_badge_fallback(club,&exe_))
            loan_badges_[i]=load_vga(asset_path(data/"W",std::to_string(state_.loan->badges[i]>>3)+".VGA"));
    }
    if(atlas_.width==314){Image padded(320,atlas_.height);padded.blit(atlas_,0,0);atlas_=std::move(padded);}
    parent_frame_=parent_frame?std::move(*parent_frame):LineupScreen(data,state_.lineup).render();
    if(parent_frame_.width!=320||parent_frame_.height!=240)throw FormatError("player card: parent framebuffer must be 320x240");
}
const std::string& PlayerCardScreen::label(uint32_t address) const {return texts_[(address-::bmh::layout::TextPointerTable)/4];}
void PlayerCardScreen::text(Image& out,int x,int baseline,std::string_view value,uint8_t color,int right,bool heading) const {
    const auto& font=heading?heading_:small_;const int width=font.text_width(value);
    if(right>=0)x+=(right-x)/2-width/2;
    if(x<0||x+width>out.width||baseline<font.baseline||baseline>=out.height)return;
    if(heading){font.draw(out,x,baseline-font.baseline,value,color);return;}
    Image mask(std::max(1,width),font.height);font.draw(mask,0,0,value,1);
    int gradient=0;while(gradient<32&&exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2+gradient)!=color)++gradient;
    for(int yy=0;yy<font.height;++yy)for(int xx=0;xx<width;++xx)if(mask.at(xx,yy))
        out.at(x+xx,baseline-font.baseline+yy)=state_.lineup.gradient_text?exe_.u8(::bmh::layout::Advertising_Text_RuntimeField+gradient*6+yy):color;
}
void PlayerCardScreen::frame(Image& out,int x,int y,int w,int h) const {
    for(int yy=4;yy<h-3;++yy)for(int xx=4;xx<w-3;++xx)out.at(x+xx,y+yy)=exe_.u8(::bmh::layout::Advertising_PatternRect_RuntimeField+((yy-4)%16)*16+(xx-4)%16);
    copy(out,widgets_,0,0,x,y,4,4);copy(out,widgets_,4,0,x+w-4,y,4,4);
    copy(out,widgets_,0,4,x,y+h-4,4,4);copy(out,widgets_,4,4,x+w-4,y+h-4,4,4);
    for(int at=4;at<w-4;at+=74){const int n=std::min(74,w-4-at);copy(out,widgets_,0,104,x+at,y,n,4);copy(out,widgets_,0,108,x+at,y+h-4,n,4);}
    for(int at=4;at<h-4;at+=96){const int n=std::min(96,h-4-at);copy(out,widgets_,0,8,x,y+at,4,n);copy(out,widgets_,4,8,x+w-4,y+at,4,n);}
}
void PlayerCardScreen::button(Image& out,int x,int y,int width,std::string_view value,bool active) const {
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
void PlayerCardScreen::draw_portrait(Image& out,const LineupPlayer& p) const {
    if(!state_.portrait_available)return;
    uint32_t seed=0x14344;
    for(size_t i=0;i<p.name.size();++i)seed+=int(int8_t(p.name[i]))*uint32_t(i)*10;
    const auto next=[&seed](int max){seed=(seed*16+0xa7)%4000000;return int(seed*uint32_t(max+1)/4000000);};
    next(100);copy(out,portrait_,108+next(3)*38,168,181,109,38,38);
    for(int i=0;i<5;++i){int sx=exe_.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField+i),sy=exe_.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField2+i);const int n=next(exe_.u8(::bmh::layout::ClubPlayerDetail_ClubPlayerDetailRedraw_RuntimeField2+i)-1);
        for(int at=0;at<n;++at){sx+=exe_.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField3+i);if(sx>exe_.u16(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField7+i*2)){sx=exe_.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField+i);sy+=exe_.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField4+i);}}
        // UsesForTheseSprites reads the packed source stride linearly. Some valid
        //25301 features (BURKARDT:243+27 in KB29's269px rows) cross one row
        // edge; a rectangular atlas bound would reject an ordinary card.
        for(int y=0;y<exe_.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField4+i);++y)for(int x=0;x<exe_.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField3+i);++x){
            const auto color=portrait_.pixels.at(size_t(sy+y)*size_t(portrait_.width)+size_t(sx+x));
            const int dx=182+exe_.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField5+i)+x,dy=114+exe_.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField6+i)+y;
            if(color&&dx>=0&&dx<out.width&&dy>=0&&dy<out.height)out.at(dx,dy)=color;
        }
    }
}
Image PlayerCardScreen::render() const {return statistics_open_?render_statistics():render_card();}
Image PlayerCardScreen::render_card() const {
    auto out=restored_card_frame_?*restored_card_frame_:parent_frame_;const auto& p=state_.lineup.players.at(size_t(state_.row));const auto& a=p.attributes;
    const Bytes board(p.board.begin(),p.board.end());
    const auto pattern=[&](int x,int y,int right,int bottom){for(int yy=y;yy<=bottom;++yy)for(int xx=x;xx<=right;++xx)
        out.at(xx,yy)=exe_.u8(::bmh::layout::Advertising_PatternRect_RuntimeField+((yy-y)%16)*16+(xx-x)%16);};
    const auto underline=[&](int x,int y,std::string_view value){rect(out,x,y+1,small_.text_width(value.substr(0,value.size()-1))+4,1,0x69);};
    const auto cash=[&](uint32_t n,int width){return number(int64_t(n)*state_.money_numerator/state_.money_denominator,width,true)+exe_.cstring(::bmh::layout::CurrencySuffixText);};
    if(!restored_card_frame_){rect(out,2,225,318,2,0x40);frame(out,0,38,320,187);}
    pattern(2,52,317,197);
    text(out,4,49,p.name,0x6c,-1,true);rect(out,3,50,313,1,0x69);rect(out,3,70,313,1,0x69);
    const int games=p.board[0x11]+1;
    const auto seniority=label(::bmh::layout::ClubPlayerDetail_RenderCard_ExecutableField)+number(games)+label(::bmh::layout::ClubPlayerDetail_RenderCard_ExecutableField2)+(games!=1?label(::bmh::layout::ClubPlayerDetail_RenderCard_ExecutableField3):"")+label(::bmh::layout::ClubPlayerDetail_RenderCard_ExecutableField4);
    text(out,314-small_.text_width(seniority),49,seniority,0x6a);
    if(state_.loan){
        const auto& loan=*state_.loan;
        text(out,4,58,label(::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer6)+loan.full_names[0]+label(::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer7)+loan.full_names[1]+exe_.cstring(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField),0x6b);
        text(out,4,66,label(::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer8)+number(loan.months,2)+label(::bmh::layout::ClubPlayerDetail_RenderCard_ExecutableField2)+
            (loan.months!=1?exe_.cstring(::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer):"")+exe_.cstring(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField),0x6b);
    }else text(out,0,63,label(::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer10),0x6b,319);
    text(out,181,106,p.name,0x6a,218);frame(out,179,107,42,42);draw_portrait(out,p);
    text(out,181,155,number(a.age(),2)+label(::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer11),0x6a,218);
    frame(out,254,80,42,42);frame(out,254,137,42,42);
    for(int i=0;i<(state_.loan?2:1);++i){
        const int y=80+i*57;
        text(out,256,y-1,label(::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer2+uint32_t(i)*4),0x6a,293);
        rect(out,255,y+1,1,40,0x46);rect(out,255,y+1,40,1,0x46);rect(out,255,y+40,40,1,0x40);rect(out,294,y+1,1,40,0x40);
        if(state_.loan){
            const auto& atlas=loan_badges_[size_t(i)];
            if(atlas.width)copy(out,atlas,(state_.loan->badges[size_t(i)]&7)*38,0,256,y+2,38,38);
            else copy(out,widgets_,8,0,256,y+2,38,38);
            text(out,256,y+48,state_.loan->short_names[size_t(i)],0x6a,293);
        }else{
            copy(out,badge_,(state_.badge&7)*38,0,256,y+2,38,38);
            text(out,256,y+48,state_.club_short_name,0x6a,293);
        }
    }
    copy(out,widgets_,8,93,220,118,15,11,0);copy(out,widgets_,8,93,230,state_.loan?132:104,15,11,0);copy(out,widgets_,8,93,240,state_.loan?147:90,15,11,0);
    for(int i=0;i<7;++i){const int x=8+i*43;
        frame(out,x,196,41,9);text(out,x,191,label(::bmh::layout::ClubPlayerDetail_Copy_TextPointer4+i*4),0x6b);underline(x,191,label(::bmh::layout::ClubPlayerDetail_Copy_TextPointer4+i*4));
        text(out,x+2,203,label(::bmh::layout::ClubPlayerDetail_Copy_TextPointer5+a.skill(Player::Skill(i))*4),0x6b);
        copy(out,atlas_,256+a.skill(Player::Skill(i))*16,32,x+34,198,16,5,0);
    }
    for(int i=0;i<6;++i){const int x=(i%2)*80+10,base=exe_.u8(::bmh::layout::ClubPlayerDetail_Copy_RuntimeField+i);
        text(out,x,base,label(::bmh::layout::ClubPlayerDetail_Copy_TextPointer3+i*4),0x6b);underline(x,base,label(::bmh::layout::ClubPlayerDetail_Copy_TextPointer3+i*4));
        std::string value;
        if(i==0){value=state_.nationality_name;text(out,10,105,exe_.cstring(::bmh::layout::ClubPlayerDetail_Copy_TextPointer),0x6a);
            const int nation=a.nationality()+((p.bonus&0x80)?32:0);copy(out,flags_,(nation%12)*9,(nation/12)*6,13,99,9,6);}
        if(i==1){
            const int unavailable=int(a.w35c>>27),injury=int(a.w364>>16&31);
            if(unavailable==0&&injury==0){const int status=(p.slot&0x8000)?0:(p.slot&0x4000)?1:2;copy(out,atlas_,272,status*5,x+4,base+14,32,5,0);}
            else {const int status=unavailable==31?8:unavailable==0?5:injury==0?4:3;
                copy(out,atlas_,197,(status-3)*6+1,x+9,base+14,10,5);
                if(status==3||status==4)text(out,x,base+11,label(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField3+unavailable*4),0x6a);
                if(status==5||(status==3&&exe_.u8(::bmh::layout::ClubPlayerDetail_ClubPlayerDetailFromGame_RuntimeField4)))text(out,x,base+11,label(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField7)+number(int(a.w364>>16&15))+label(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField8),0x6a);
                else if(status==3)copy(out,atlas_,304-(std::min(int(a.w364>>16&15),11)/3)*16,32,136,100,16,5,0);
            }
        }
        if(i==2){text(out,x,base+19,p.board[0xc]==255?label(::bmh::layout::ClubPlayerDetail_Copy_TextPointer9):label(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField7)+number(p.board[0xc])+label(::bmh::layout::ClubPlayerDetail_Copy_TextPointer10),0x6a);
            value=cash(le32(board,0x50),5)+label(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField4);}
        if(i==3&&p.board[0x43]){text(out,x,base+19,cash(le16(board,0x4e)&0x7fff,5)+label(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField4),0x6a);
            text(out,x,base+27,label(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField7)+number(p.board[0x43]&63)+label(::bmh::layout::ClubPlayerDetail_Copy_TextPointer10),0x6a);
            value=label(::bmh::layout::ClubPlayerDetail_Copy_TextPointer6+(p.board[0x43]>>6)*4);}
        if(i==4)value=le32(board,0x54)?cash(le32(board,0x54),1)+exe_.cstring(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField):label(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField9);
        if(i==5){const auto market=player_card_market_value(a,exe_);value=market?cash(*market,1)+exe_.cstring(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField):"?";}
        text(out,x,base+11,value,0x6a);
    }
    button(out,8,211,60,label(::bmh::layout::ClubPlayerDetail_Copy_TextPointer7));button(out,80,211,96,label(::bmh::layout::ClubPlayerDetail_Copy_ExecutableField5),statistics_open_);
    return out;
}

Image PlayerCardScreen::render_statistics() const {
    auto out=statistics_frame_?*statistics_frame_:render_card();
    // The original draws statistics inside the card frame, preserving its name,
    // seniority, bottom card buttons and underlying lineup.
    if(!statistics_frame_)for(int y=52;y<=210;++y)for(int x=2;x<=317;++x)
        out.at(x,y)=exe_.u8(::bmh::layout::Advertising_PatternRect_RuntimeField+((y-52)%16)*16+(x-2)%16);
    const auto line=[&](int x,int y,int end_x,int end_y,uint8_t color){
        const int dx=std::abs(end_x-x),sx=x<end_x?1:-1;
        const int dy=std::abs(end_y-y),sy=y<end_y?1:-1;int error=0;
        // Abs accumulates from zero; it does not round at half a pixel.
        for(int step=0;step<=std::max(dx,dy);++step){
            if(x>=0&&x<out.width&&y>=0&&y<out.height)out.at(x,y)=color;
            if(dx>dy){x+=sx;error+=dy;if(error>=dx){error-=dx;y+=sy;}}
            else if(dy){y+=sy;error+=dx;if(error>=dy){error-=dy;x+=sx;}}
        }
    };
    if(!statistics_frame_)for(int edge=0;edge<4;++edge)line(exe_.u16(::bmh::layout::Analytics_Render_RuntimeField3+edge*4+2),exe_.u8(::bmh::layout::Analytics_Render_RuntimeField2 + edge*2+1),
        exe_.u16(::bmh::layout::Analytics_Render_RuntimeField3+edge*4),exe_.u8(::bmh::layout::Analytics_Render_RuntimeField2 + edge*2),exe_.u8(::bmh::layout::Analytics_Render_RuntimeField+edge));
    if(statistics_frame_)for(int y=52;y<=137;++y)for(int x=12;x<=310;++x)
        out.at(x,y)=exe_.u8(::bmh::layout::Advertising_PatternRect_RuntimeField+((y-52)%16)*16+(x-12)%16);
    for(int curve=0;curve<4;++curve){
        const int x=27+(curve%2)*120,y=142+(curve/2)*11;const auto color=exe_.u8(::bmh::layout::Analytics_FormatError_RuntimeField+curve);
        rect(out,x-7,y+3,5,4,color);
        line(x-8,y+2,x-2,y+2,0x68);line(x-8,y+2,x-8,y+7,0x68);
        line(x-2,y+3,x-2,y+7,0x62);line(x-8,y+7,x-2,y+7,0x62);out.at(x-8,y+2)=0x6a;
        button(out,x,y,96,exe_.cstring(exe_.u32(::bmh::layout::Analytics_FormatError_ExecutableField+(12+curve)*4)),(statistics_flags_&(1<<curve))!=0);
        if(!(statistics_flags_&(1<<curve)))continue;
        const auto points=player_statistics_points(state_,exe_,curve,(statistics_flags_&0x40)!=0);
        for(size_t i=0;i<points.size();++i){const auto [px,py]=points[i];
            if(i)line(points[i-1].first,points[i-1].second,px,py,color);
            rect(out,px-1,py-1,3,3,color);
        }
    }
    button(out,265,142,30,label(::bmh::layout::ClubPlayerDetail_RenderStatistics_ExecutableField),(statistics_flags_&0x40)!=0);
    button(out,265,153,30,label(::bmh::layout::ClubPlayerDetail_RenderStatistics_ExecutableField2),(statistics_flags_&0x80)!=0);
    button(out,8,185,140,label(::bmh::layout::ClubPlayerDetail_RenderStatistics_ExecutableField3));
    return out;
}

bool PlayerCardScreen::click(int x,int y,bool right) {
    if(close_)return false;
    if(statistics_open_){
        if(right||(x>=8&&x<=148&&y>=185&&y<=195)){
            restored_card_frame_=render_statistics();statistics_open_=false;return true;
        }
        for(int curve=0;curve<4;++curve){const int bx=27+(curve%2)*120,by=142+(curve/2)*11;
            if(x>=bx&&x<=bx+96&&y>=by&&y<=by+10){statistics_frame_=render_statistics();statistics_flags_^=uint8_t(1<<curve);return true;}}
        if(x>=265&&x<=295&&((y>=142&&y<=151)||(y>=153&&y<=162))){
            statistics_frame_=render_statistics();statistics_flags_^=0xc0;return true;
        }
        return false;
    }
    if(right||(x>=8&&x<=68&&y>=211&&y<=220)){close_=true;return true;}
    if(x>=80&&x<=176&&y>=211&&y<=220){
        if(!state_.statistics){unsupported_navigation_="missing_player_statistics_context";return true;}
        statistics_open_=true;statistics_frame_.reset();unsupported_navigation_.clear();return true;
    }
    return false;
}
bool PlayerCardScreen::key(std::string_view key) {if(key=="ESC"||key=="ESCAPE")return click(0,0,true);return false;}
std::string PlayerCardScreen::state_json() const {
    const auto& p=state_.lineup.players.at(size_t(state_.row));const auto value=player_card_market_value(p.attributes,exe_);
    std::ostringstream s;s<<"{\"screen\":"<<json_quote(statistics_open_?"player_statistics":"player_card")<<",\"manager_slot\":"<<state_.lineup.manager_slot<<",\"runtime_club\":"<<state_.lineup.runtime_club
        <<",\"revision\":"<<state_.lineup.revision<<",\"source\":"<<json_quote(state_.lineup.source)<<",\"row\":"<<state_.row<<",\"player_id\":"<<p.player_id
        <<",\"name\":"<<json_quote(original_to_utf8(p.name))<<",\"age\":"<<p.attributes.age()<<",\"nationality\":"<<json_quote(original_to_utf8(state_.nationality_name))
        <<",\"contract_months\":"<<int(p.board[0xc])<<",\"salary\":"<<le32(Bytes(p.board.begin(),p.board.end()),0x50)<<",\"purchase_price\":"<<le32(Bytes(p.board.begin(),p.board.end()),0x54)
        <<",\"market_value\":"<<(value?std::to_string(*value):"null")<<",\"close_requested\":"<<(close_?"true":"false")<<",\"unsupported_navigation\":"
        <<(unsupported_navigation_.empty()?"null":json_quote(unsupported_navigation_))<<",\"statistics_context_available\":"<<(state_.statistics?"true":"false")
        <<",\"statistics_months\":"<<(state_.statistics?std::to_string(state_.statistics->months):"null")
        <<",\"statistics_display_flags\":"<<(state_.statistics?std::to_string(statistics_flags_):"null")
        <<",\"statistics_plot_cleared\":"<<(statistics_frame_?"true":"false")
        <<",\"statistics_preferences_persisted\":false,\"read_only\":true}";return s.str();
}
} // namespace bmh::ui
