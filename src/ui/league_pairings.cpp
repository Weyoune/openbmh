// SPDX-License-Identifier: GPL-3.0-only
#include "game/state.hpp"
#include "formats/executable_layout.hpp"
#include "ui/league_pairings.hpp"

#include <algorithm>
#include <cstdio>
#include <sstream>
#include "game/messages.hpp"
#include "game/schedule.hpp"
#include "game/season.hpp"
#include "ui/desk_state.hpp"
#include "ui/manager.hpp"

namespace bmh::ui {
namespace {
std::string field(int n,int width) {
    auto s=std::to_string(n);if(int(s.size())<width)s.insert(0,size_t(width-int(s.size())),'%');return s;
}
std::string date_string(uint32_t day,const LeImage& exe,bool weekday,const GameState& g) {
    if(day==0xffff)return exe.cstring(::bmh::layout::Fltd_Layout_TextPointer5);
    const auto d=desk_date(day,exe);char digits[20];
    std::snprintf(digits,sizeof digits,"%02d.%02d.%04d",d.day,d.month,d.year);
    return (weekday?message_text(g,::bmh::layout::LeaguePairings_DateString_TextPointer2 + uint32_t(d.weekday)*4)+exe.cstring(::bmh::layout::LeaguePairings_DateString_TextPointer):std::string{})+digits;
}
uint8_t name_color(const GameState& g,int team,uint8_t normal) {
    for(size_t slot=0;slot<std::min<size_t>(g.manager_teams.size(),4);++slot) {
        if(g.manager_teams[slot]!=team||g.manager_records.at(slot*0xb0c + 0xa8)==0)continue;
        return int(slot)==g.memory.u8(::bmh::layout::ActiveManager)&&g.memory.u8(::bmh::layout::DeskPosition)<g.memory.u8(::bmh::layout::ManagerCount)?0x7a:0x75;
    }
    return g.memory.has(::bmh::layout::AlltimeTable_TeamColor_RuntimeField)&&g.memory.u16(::bmh::layout::AlltimeTable_TeamColor_RuntimeField)==team?0xf7:normal;
}
void copy(Image& out,const Image& src,int sx,int sy,int x,int y,int w,int h,int transparent=-1) {
    if(sx<0||sy<0||w<0||h<0||sx+w>src.width||sy+h>src.height||x<0||y<0||x+w>out.width||y+h>out.height)
        throw FormatError("pairings: Original atlas bounds (source "+std::to_string(src.width)+"x"+
            std::to_string(src.height)+", crop "+std::to_string(sx)+","+std::to_string(sy)+" "+
            std::to_string(w)+"x"+std::to_string(h)+", destination "+std::to_string(x)+","+std::to_string(y)+")");
    for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx) {
        const uint8_t v=src.pixels[size_t(sy+yy)*size_t(src.width)+size_t(sx+xx)];if(v!=transparent)out.at(x+xx,y+yy)=v;
    }
}
void fill(Image& out,int x,int y,int w,int h,uint8_t color) {
    for(int yy=std::max(0,y);yy<std::min(out.height,y+h);++yy)
        for(int xx=std::max(0,x);xx<std::min(out.width,x+w);++xx)out.at(xx,yy)=color;
}
}

LeaguePairingsConstants league_pairings_constants(const LeImage& exe) {
    LeaguePairingsConstants c;c.domestic_league_count=exe.u8(::bmh::layout::LeaguePairings_LeaguePairingsConstants_RuntimeField);
    for(size_t n=0;n<9;++n) {c.points_mode[n]=exe.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField + uint32_t(n));if(n<c.domestic_league_count)c.calendar_class[n]=exe.u8(::bmh::layout::LeagueCalendarBits + uint32_t(n));}
    for(size_t n=0;n<c.computer_tip_gap.size();++n)c.computer_tip_gap[n]=exe.u8(::bmh::layout::CareerSession_DispatchLeagueTableControl_RuntimeField+uint32_t(n));
    return c;
}
uint64_t league_pairings_source_seconds(uint64_t milliseconds) {
    return milliseconds/1000+(milliseconds%1000>=500?1:0);
}

LeaguePairingsState league_pairings_from_game(const GameState& g,const LeaguePairingsOwner& owner,
    const LeImage& exe,uint64_t revision,uint64_t scope,uint64_t sequence,
    const std::optional<LeaguePairingsActivePacket>& active_packet) {
    LeaguePairingsState out;out.source=league_pairings_view_source(g,owner,league_pairings_constants(exe));
    out.identity={owner.manager,owner.runtime_club,owner.club_identity,owner.league,owner.round,revision,scope,sequence};
    if(owner.active_phase&&!active_packet)
        throw FormatError("pairings: active presentation phase has no owned packet");
    if(active_packet) {
        validate_league_pairings_active_packet_source(g,owner,*active_packet);
        if(active_packet->league!=owner.league||active_packet->round!=owner.round||
            active_packet->rows.size()!=out.source.rows.size())
            throw FormatError("pairings: active packet does not cover this projection");
        for(size_t n=0;n<out.source.rows.size();++n) {
            const auto& source=out.source.rows[n];const auto& active=active_packet->rows[n];
            if(source.source_round!=active.source_round||source.fixture_index!=active.fixture_index||
                source.home.runtime_team!=active.home_runtime_team||source.home.club_identity!=active.home_club_identity||
                source.away.runtime_team!=active.away_runtime_team||source.away.club_identity!=active.away_club_identity)
                throw FormatError("pairings: active packet row identity differs from visible fixture");
        }
        out.active_packet=active_packet;
    }
    out.decorated=g.memory.u8(::bmh::layout::PlainBackground)==0;out.original_panel=g.memory.u8(::bmh::layout::SolidPanelBackground)==0;
    out.gradient=g.memory.u8(::bmh::layout::TextGradientDisabled)==0&&exe.u8(::bmh::layout::TextGradientMode)==2;
    out.secondary_leagues=g.memory.u8(::bmh::layout::CalendarState_FormatError_RuntimeField2)==0;
    // Keep original bytes in BMH3 fields. SpriteFont receives UTF8 explicitly.
    //51329 ->LeagueName uses the league names prepared by6b98e, including
    //the shared Regionalliga A/B text and foreign country's Native name.
    //TeamStatistics_SourceLeagueName_RuntimeField's temporary prefix is only set around other callers, not this Desk.
    const auto league_title=owner.league<5?league_name(g,owner.league):g.club_name(kNoTeam+owner.league-4);
    if(owner.postponed_active) {
        out.title=message_text(g,::bmh::layout::LeaguePairings_FormatError_TextPointer12)+league_title;
        out.subtitle.clear();
    } else if(owner.filter_active&&owner.filter_team>=0) {
        out.title=message_text(g,::bmh::layout::LeaguePairings_FormatError_TextPointer11)+league_title;
        out.subtitle=g.club_name(owner.filter_team);
    } else {
        out.title=message_text(g,::bmh::layout::LeaguePairings_FormatError_TextPointer10)+league_title;
        out.subtitle=owner.title_round+1>out.source.total_rounds?message_text(g,::bmh::layout::CareerSession_FormatError_TextPointer3):
            message_text(g,::bmh::layout::LeaguePairings_FormatError_TextPointer4)+field(owner.title_round+1,1)+exe.cstring(::bmh::layout::CareerSession_FormatError_TextPointer)+message_text(g,::bmh::layout::CareerSession_FormatError_TextPointer4);
    }
    const bool selected_filter=owner.filter_active&&owner.filter_team>=0;
    if(!selected_filter&&!owner.postponed_active)out.date=date_string(out.source.match_date,exe,true,g);
    for(size_t n=0;n<8;++n)out.buttons[n]=message_text(g,::bmh::layout::LeaguePairings_FormatError_TextPointer5 + uint32_t(n)*4);
    for(const auto& row:out.source.rows) {
        LeaguePairingsDisplayRow display;
        const std::array<const LeaguePairingsTeam*,2> teams{&row.home,&row.away};
        for(size_t side=0;side<2;++side) {
            const auto& team=*teams[side];display.names[side]=g.club_name(team.runtime_team);
            const bool selected=team.runtime_team==owner.search_first||team.runtime_team==owner.search_second||
                (owner.filter_active&&team.runtime_team==owner.filter_team);
            display.name_colors[side]=name_color(g,team.runtime_team,selected?0xfb:0x5f);
            auto& detail=display.details[side];
            if(team.previous_rank>=0)detail=field(team.previous_rank,2)+exe.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer8)+exe.cstring(::bmh::layout::LeaguePairings_FormatError_ExecutableField);
            detail+=exe.cstring(::bmh::layout::CupDraw_FormatError_ExecutableField2);
            detail+=field(team.strength[0],2)+message_text(g,::bmh::layout::CupDraw_FormatError_TextPointer5);
            detail+=field(team.strength[1],2)+message_text(g,::bmh::layout::LeaguePairings_FormatError_TextPointer8);
            detail+=field(team.strength[2],2)+message_text(g,::bmh::layout::LeaguePairings_FormatError_TextPointer9);
            detail+=field(team.strength[3],2)+exe.cstring(::bmh::layout::CupDraw_FormatError_TextPointer2);
            detail+=field(team.strength[4],2)+exe.cstring(::bmh::layout::CupDraw_FormatError_TextPointer3);
        }
        const uint8_t displayed_result=row.computer_tip_result.value_or(row.raw_result);
        if(owner.active_phase&&displayed_result==0xcc) {
            if(!active_packet)throw FormatError("pairings: unplayed active row has no descriptor");
            const auto found=std::find_if(active_packet->rows.begin(),active_packet->rows.end(),[&](const auto& active) {
                return active.source_round==row.source_round&&active.fixture_index==row.fixture_index;
            });
            if(found==active_packet->rows.end())throw FormatError("pairings: missing active row identity");
            display.active=*found;
            display.temperature=format_number(g,int(found->temperature),0x81)+message_text(g,::bmh::layout::LeaguePairings_FormatError_TextPointer14);
            display.details[0]=found->referee_name+exe.cstring(::bmh::layout::LeaguePairings_FormatError_TextPointer2)+
                message_text(g,::bmh::layout::LeaguePairings_FormatError_TextPointer6+uint32_t(found->referee_profile&3)*4)+exe.cstring(::bmh::layout::LeaguePairings_FormatError_TextPointer3)+
                message_text(g,::bmh::layout::LeaguePairings_FormatError_TextPointer7+uint32_t((int(int8_t(found->referee_profile))>>3)&3)*4)+
                exe.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer11);
            display.details[1].clear();
        }
        if(league_pairings_played_source(displayed_result))
            display.result=field(displayed_result>>4,1)+exe.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer9)+field(displayed_result&15,1);
        else if(row.postponed_date) {display.result=date_string(*row.postponed_date,exe,false,g);display.result_x=255;}
        if(!owner.postponed_active&&row.scheduled_date) {
            display.scheduled_ordinal=field(row.source_round+1,2)+message_text(g,::bmh::layout::CupDraw_FormatError_TextPointer6);
            display.scheduled_date=exe.cstring(::bmh::layout::LeaguePairings_FormatError_TextPointer)+date_string(*row.scheduled_date,exe,false,g)+exe.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer11);
        }
        if(!owner.active_phase&&!owner.postponed_active&&!selected_filter&&row.first_leg_result)display.first_leg=field(*row.first_leg_result&15,1)+exe.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer9)+
            field(*row.first_leg_result>>4,1)+message_text(g,::bmh::layout::LeaguePairings_FormatError_TextPointer13);
        out.rows.push_back(std::move(display));
    }
    return out;
}

LeaguePairingsScreen::LeaguePairingsScreen(const std::filesystem::path& data,LeaguePairingsState state)
    :exe_(load_le(asset_path(data,"BMH.exe"))),frame_(load_vga(asset_path(data/"KB","129.VGA"))),
    panel_(load_vga(asset_path(data/"KB","56.VGA"))),arrows_(load_vga(asset_path(data/"KB","1.VGA"))),
    widgets_(load_vga(asset_path(data/"KB","0.VGA"))),weather_(load_vga(asset_path(data/"KB","22.VGA"))),
    font_(load_font(asset_path(data/"F","BMH3.FNT"))),
    sprite_(data),state_(std::move(state)) {
    const auto& id=state_.identity;
    const auto& owner=state_.source.owner;
    if(id.manager<0||id.manager>=4||id.league<0||id.league>=9||id.round<0||id.round>=state_.source.total_rounds||
        !id.scope||state_.source.owner.closed||state_.rows.size()!=state_.source.rows.size()||
        !owner.supported||owner.manager!=id.manager||owner.runtime_club!=id.runtime_club||
        owner.club_identity!=id.club_identity||owner.league!=id.league||owner.round!=id.round||
        (!owner.postponed_active&&state_.rows.empty())||state_.rows.size()>10||state_.source.total_rows<int(state_.rows.size())||
        ((!owner.filter_active||owner.filter_team<0)&&!owner.postponed_active&&
            state_.rows.size()!=league_fixtures(id.league,id.round+1).size())||
        state_.title.empty()||(!owner.postponed_active&&state_.subtitle.empty())||
        (owner.postponed_active&&(
            (state_.source.preferences&0x0f)!=8||owner.filter_active||owner.filter_team!=-1||owner.filter_identity!=-1||
            owner.filter_offset!=0||owner.search_first!=-1||owner.search_second!=-1||
            owner.search_first_identity!=-1||owner.search_second_identity!=-1||owner.search_second_click||
            state_.source.total_rows<0||state_.source.total_rows>255||owner.postponed_offset<0||
            owner.postponed_offset%10!=0||state_.source.page_offset!=owner.postponed_offset||
            (state_.source.total_rows==0?owner.postponed_offset!=0:
                owner.postponed_offset>=state_.source.total_rows)||
            state_.rows.size()!=size_t(state_.source.total_rows==0?0:
                std::min(10,state_.source.total_rows-owner.postponed_offset)))))
        throw FormatError("pairings: incomplete owned normal projection");
    if(owner.computer_tip_active) {
        if(owner.postponed_active||owner.filter_active||owner.computer_tip_league!=owner.league||
            owner.computer_tip_round!=owner.round||owner.computer_tip_results.size()!=state_.rows.size()||
            (state_.source.preferences&0x10)==0)
            throw FormatError("pairings: incomplete Computer Tip projection");
        for(size_t n=0;n<state_.source.rows.size();++n)
            if(!state_.source.rows[n].computer_tip_result||
                *state_.source.rows[n].computer_tip_result!=owner.computer_tip_results[n].predicted_result)
                throw FormatError("pairings: missing Computer Tip score overlay");
    } else for(const auto& row:state_.source.rows)
        if(row.computer_tip_result)throw FormatError("pairings: unowned Computer Tip score overlay");
    if(state_.active_packet)for(const auto& row:state_.active_packet->rows)
        if(int(row.weather)*25+25>weather_.width||weather_.height<11)
            throw FormatError("pairings: Original weather atlas crop is out of bounds");
    for(const auto& label:state_.buttons)if(label.empty())throw FormatError("pairings: missing Original button text");
    (void)render(); // Validate every original atlas crop before exposing the screen.
}
LeaguePairingsScreen::LeaguePairingsScreen(const std::filesystem::path& data,LeaguePairingsState state,FamilyMode family)
    :LeaguePairingsScreen(data,std::move(state)) {
    if(family.identity.manager!=state_.identity.manager||family.identity.runtime_club!=state_.identity.runtime_club||
       family.identity.club_identity!=state_.identity.club_identity)
        throw FormatError("pairings: mismatched family identity");
    family_=std::move(family);
}
void LeaguePairingsScreen::text(Image& out,int x,int baseline,std::string_view value,uint8_t color) const {
    const int width=font_.text_width(value);
    if(x<0||x+width>out.width||baseline<5||baseline>=out.height)return;
    Image mask(std::max(width,1),font_.height);font_.draw(mask,0,0,value,1);
    int index=0;while(index<32&&exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + uint32_t(index))!=color)++index;
    for(int yy=0;yy<font_.height;++yy)for(int xx=0;xx<width;++xx)if(mask.at(xx,yy))
        out.at(x+xx,baseline-font_.baseline+yy)=state_.gradient&&index<32?
            exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + uint32_t(index*6+yy)):color;
}
void LeaguePairingsScreen::selector(Image& out,int x,int y,int style) const {
    const int height=exe_.u8(::bmh::layout::LeaguePairings_Selector_RuntimeField);
    copy(out,arrows_,198,0,x,y,55,38);
    copy(out,arrows_,198,76-height,x,y+37,55,height-38);
    if(style<3)copy(out,arrows_,exe_.u8(::bmh::layout::LeaguePairings_Selector_RuntimeField3 + uint32_t(style)),0,x+1,y+exe_.u8(::bmh::layout::LeaguePairings_Selector_RuntimeField2),
        exe_.u8(::bmh::layout::LeaguePairings_Selector_RuntimeField4 + uint32_t(style)),38);
}
Image LeaguePairingsScreen::render() const {
    Image out=state_.decorated?frame_:Image(320,240);
    if(!state_.decorated) {
        fill(out,0,4,320,13,0x44);fill(out,1,5,318,11,0x39);
        fill(out,0,228,320,10,0x44);fill(out,1,229,318,8,0x39);
    }
    const auto title=original_to_utf8(state_.title);
    sprite_.draw(out,SpriteFont::centered_title_x(sprite_.text_width(title)),13,title);
    if(!state_.subtitle.empty())sprite_.draw(out,5,235,original_to_utf8(state_.subtitle));
    if(!state_.date.empty())text(out,319-font_.text_width(state_.date),22,state_.date,0x5f);
    const bool empty_postponed=state_.source.owner.postponed_active&&state_.rows.empty();
    const int bottom=int(state_.rows.size())*15+23;
    if(!empty_postponed) {
        if(state_.original_panel)copy(out,panel_,0,3,1,23,318,bottom-23);
        else fill(out,0,22,320,bottom-21,0x42);
        fill(out,0,22,320,1,0x46);fill(out,0,22,1,bottom-21,0x46);
        fill(out,0,bottom,320,1,0x40);fill(out,319,22,1,bottom-21,0x40);
    }
    for(size_t n=0;n<state_.rows.size();++n) {
        const auto& row=state_.rows[n];const int y=29+int(n)*15;
        if(row.active) {
            copy(out,weather_,int(row.active->weather)*25,0,293,23+int(n)*15,25,11);
            const int temperature_x=305-font_.text_width(row.temperature)/2;
            text(out,temperature_x,34+int(n)*15,row.temperature,0x62);
        }
        text(out,125,y,exe_.cstring(::bmh::layout::LeaguePairings_Fill_TextPointer),0x68);
        for(int side=0;side<2;++side) {
            text(out,5+side*127,y,row.names[size_t(side)],row.name_colors[size_t(side)]);
            text(out,3+side*130,y+6,row.details[size_t(side)],0x68);
        }
        if(!state_.source.owner.postponed_active&&!row.scheduled_ordinal.empty())text(out,220,y,row.scheduled_ordinal,0x5f);
        const int result_x=!row.scheduled_date.empty()?319-font_.text_width(row.scheduled_date):row.result_x;
        text(out,result_x,y,row.scheduled_date.empty()?row.result:row.scheduled_date,0x5f);text(out,263,y+7,row.first_leg,0x69);
    }
    for(int n=0;n<8;++n) {
        const int x=1+(n/4)*74,y=176+(n%4)*13;
        const bool selected=pressed_button_==n||(state_.source.preferences&(1u<<n));
        int dx=x,dy=y;
        if(selected) {
            const uint8_t background=out.at(x,y+10);fill(out,x,y,71,2,background);fill(out,x,y,2,11,background);
            dx+=2;dy+=2;
        } else {fill(out,x+2,y+10,70,2,0x40);fill(out,x+70,y+2,2,10,0x40);}
        const int sy=selected?44:74;copy(out,widgets_,24,sy,dx,dy,31,10);
        int used=31;while(70-used>20) {const int amount=std::min(8,70-used-20);
            copy(out,widgets_,24,sy+10,dx+used,dy,amount,10);used+=amount;}
        const int left=70-used;copy(out,widgets_,45-left,sy+20,dx+used,dy,left,10);
        const auto label=original_to_utf8(state_.buttons[size_t(n)]);
        sprite_.draw(out,dx+35-sprite_.text_width(label)/2,dy+7,label,exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + uint32_t(selected)));
    }
    selector(out,148,176,2);selector(out,205,176,3);selector(out,262,176,state_.source.owner.league_group);
    int league=state_.source.owner.league_group*5;
    for(int r=0;r<4;++r) {
        const int count=exe_.u8(::bmh::layout::CalendarLeagueCounts + uint32_t(state_.source.owner.league_group*4+r));
        const int y=exe_.u8(::bmh::layout::LeaguePairings_Fill_RuntimeField)+exe_.u8(::bmh::layout::LeaguePairings_Fill_RuntimeField2)*r;
        for(int n=0;n<count;++n,++league)copy(out,arrows_,188,state_.identity.league==league?8:0,
            233-count*4+n*8,y,8,8);
    }
    if(pressed_arrow_>=0)copy(out,arrows_,136+pressed_arrow_*26,0,149+pressed_arrow_*26,182,26,38);
    return out;
}
int LeaguePairingsScreen::button_at(int x,int y) const {
    for(int n=0;n<8;++n) {const int bx=1+n/4*74,by=176+n%4*13;
        if(x>=bx&&x<=bx+70&&y>=by&&y<=by+10)return n;}return -1;
}
int LeaguePairingsScreen::arrow_at(int x,int y) const {
    if(y>=182&&y<=220)for(int n=0;n<2;++n)if(x>=149+n*26&&x<=175+n*26)return n;
    return -1;
}
bool LeaguePairingsScreen::in_arrow(int arrow,int x,int y) const {
    return arrow>=0&&arrow<2&&x>=149+arrow*26&&x<=175+arrow*26&&y>=182&&y<=220;
}
void LeaguePairingsScreen::post(LeaguePairingsEvent e) {pending_=LeaguePairingsRequest{state_.identity,e};rejection_.clear();}
void LeaguePairingsScreen::post_family(LeagueFamilyRequest request) {
    if(!family_||pending_family_action_)return;
    request.identity=family_->identity;request.from_child=2;
    pending_family_action_=std::move(request);rejection_.clear();
}
bool LeaguePairingsScreen::tick_source_seconds(uint64_t now) {
    // Move toggles local28 only for param1==0. Phase/keyboard callers
    // carry their nonzero Source minute through the existing family identity.
    if(family_&&family_->identity.source_param2!=0)return false;
    if(pending_||pressed_button_>=0||pressed_arrow_>=0||!state_.active_packet||!last_presented_source_seconds_)
        return false;
    const uint64_t last=*last_presented_source_seconds_;
    if(now<=last||now-last<=1)return false;
    post({LeaguePairingsAction::presentation_redraw,state_.source.owner.active_phase?0:1});
    return true;
}
void LeaguePairingsScreen::mark_presented_source_seconds(uint64_t now) {
    last_presented_source_seconds_=now;
}
std::optional<LeaguePairingsEvent> LeaguePairingsScreen::selector_event(int x,int y) const {
    if(pending_)return std::nullopt;
    if(x>=205&&x<=260&&y>=176&&y<=216) {
        auto image=render();if(image.at(x,y)==0x19||image.at(x,y)==0x42)return std::nullopt;
        const int r=(y-exe_.u8(::bmh::layout::LeaguePairings_SelectorEvent_RuntimeField2))/exe_.u8(::bmh::layout::LeaguePairings_SelectorEvent_RuntimeField3);if(r<0||r>=4)return std::nullopt;
        const bool group=state_.source.owner.league_group;
        int column=0,league=0;
        if(!group) {
            const int count=exe_.u8(::bmh::layout::CalendarLeagueCounts + uint32_t(r));int left=233-count*4;
            while(column<count&&(x<left||x>left+8)){++column;left+=8;}
            league=exe_.u8(::bmh::layout::CalendarFirstLeague + uint32_t(r))+column;
        } else {
            column=(exe_.u8(::bmh::layout::LeaguePairings_SelectorEvent_RuntimeField + uint32_t(r))*4+x-233)/8;
            league=exe_.u8(::bmh::layout::CareerSession_FormatError_RuntimeField4)+r+column;
        }
        if(league<0||league>=9)return std::nullopt;
        return LeaguePairingsEvent{LeaguePairingsAction::select_league,league};
    }
    if(x>=262&&x<=317&&y>=176&&y<=216&&state_.secondary_leagues) {
        return LeaguePairingsEvent{LeaguePairingsAction::toggle_group};
    }
    return std::nullopt;
}
bool LeaguePairingsScreen::pointer_down(int x,int y,bool right) {
    if(pending_||pending_family_action_)return false;
    if(right) {
        pressed_button_=pressed_arrow_=-1;
        if(family_)post_family({{},LeagueFamilyRequestKind::close,2,0});
        else post({LeaguePairingsAction::close});
        return true;
    }
    if(y>21&&y<int(state_.rows.size())*15+21) {
        const auto& row=state_.source.rows.at(size_t((y-22)/15));const auto& team=x>120?row.away:row.home;
        const auto& owner=state_.source.owner;
        const auto action=owner.filter_active?LeaguePairingsAction::filter_club:
            owner.search_first==-1?LeaguePairingsAction::team_info:LeaguePairingsAction::search_team;
        post({action,
            team.runtime_team,team.club_identity});return true;
    }
    if(const auto event=selector_event(x,y)) {
        // InLeaguePairings leaves CareerSession_BeginFriendlyOpponentTable_RuntimeField2 unchanged for the current league;Move does
        // not redraw or reset its clock. Consume without a pending action.
        if(event->kind==LeaguePairingsAction::select_league &&
           event->value==state_.source.owner.league)return false;
        post(*event);return true;
    }
    // Source outer arrow bounds precede the per-arrow held/release helper.
    if(x>147&&x<203&&y>175&&y<226) {
        pressed_arrow_=x>175?1:0;pressed_button_=-1;
        if(!in_arrow(pressed_arrow_,x,y))pressed_arrow_=-1;
        return true;
    }
    pressed_button_=button_at(x,y);pressed_arrow_=-1;return pressed_button_>=0;
}
bool LeaguePairingsScreen::move(int x,int y) {
    if(pressed_button_>=0&&button_at(x,y)!=pressed_button_) {pressed_button_=-1;return true;}
    if(pressed_arrow_>=0&&!in_arrow(pressed_arrow_,x,y)) {pressed_arrow_=-1;return true;}return false;
}
bool LeaguePairingsScreen::release(int x,int y) {
    if(pending_||pending_family_action_)return false;
    const int button=pressed_button_,arrow=pressed_arrow_;pressed_button_=pressed_arrow_=-1;
    if(button>=0&&button_at(x,y)==button) {
        if(family_&&(button==6||button==7)) {
            post_family({{},LeagueFamilyRequestKind::navigate,2,uint8_t(button==6?1:3)});
            return true;
        }
        if(button==5)return true; // Source ellipsis has no action.
        post({button==0?LeaguePairingsAction::reset:button==1?LeaguePairingsAction::search:
            button==2?LeaguePairingsAction::filter_by_club:button==3?LeaguePairingsAction::postponed_list:
            button==4?LeaguePairingsAction::computer_tip:LeaguePairingsAction::unsupported,button});return true;
    }
    if(in_arrow(arrow,x,y)) {post({arrow?LeaguePairingsAction::next:LeaguePairingsAction::previous});return true;}
    return button>=0||arrow>=0;
}
void LeaguePairingsScreen::reject_action(std::string reason) {
    if(!pending_||reason.empty())throw FormatError("pairings: no pending rejection");
    pending_.reset();pressed_button_=pressed_arrow_=-1;rejection_=std::move(reason);
}
void LeaguePairingsScreen::reject_family_action(std::string reason) {
    if(!family_||!pending_family_action_||reason.empty())throw FormatError("pairings: no pending family rejection");
    pending_family_action_.reset();pressed_button_=pressed_arrow_=-1;rejection_=std::move(reason);
}
std::string LeaguePairingsScreen::state_json() const {
    std::ostringstream out;const auto& id=state_.identity;
    out<<"{\"screen\":\"league_pairings\",\"manager\":"<<id.manager<<",\"runtime_club\":"<<id.runtime_club
        <<",\"league\":"<<id.league<<",\"round\":"<<id.round<<",\"revision\":"<<id.revision
        <<",\"scope\":"<<id.scope<<",\"sequence\":"<<id.sequence<<",\"league_group\":"<<int(state_.source.owner.league_group)
        <<",\"pressed_button\":"<<pressed_button_<<",\"pressed_arrow\":"<<pressed_arrow_
        <<",\"search_first\":"<<state_.source.owner.search_first<<",\"search_second\":"<<state_.source.owner.search_second
        <<",\"search_second_click\":"<<(state_.source.owner.search_second_click?"true":"false")
        <<",\"filter_active\":"<<(state_.source.owner.filter_active?"true":"false")
        <<",\"filter_team\":"<<state_.source.owner.filter_team<<",\"filter_offset\":"<<state_.source.owner.filter_offset
        <<",\"filter_rows_total\":"<<state_.source.total_rows
        <<",\"postponed_active\":"<<(state_.source.owner.postponed_active?"true":"false")
        <<",\"postponed_offset\":"<<state_.source.owner.postponed_offset
        <<",\"computer_tip_active\":"<<(state_.source.owner.computer_tip_active?"true":"false")
        <<",\"active_phase\":"<<(state_.source.owner.active_phase?"true":"false")
        <<",\"active_group\":"<<state_.source.owner.active_group
        <<",\"active_packet_rows\":"<<(state_.active_packet?state_.active_packet->rows.size():0)
        <<",\"last_presented_source_seconds\":";
    if(last_presented_source_seconds_)out<<*last_presented_source_seconds_;else out<<"null";
    out
        <<",\"pending\":"<<(pending_?"true":"false")<<",\"last_rejection\":"<<json_quote(rejection_)<<",\"rows\":[";
    for(size_t n=0;n<state_.source.rows.size();++n) {if(n)out<<',';const auto& r=state_.source.rows[n];
        out<<"{\"home\":"<<r.home.runtime_team<<",\"away\":"<<r.away.runtime_team
           <<",\"source_round\":"<<r.source_round<<",\"fixture_index\":"<<r.fixture_index
           <<",\"raw_result\":"<<int(r.raw_result);
        const auto& display=state_.rows[n];
        if(display.active)out<<",\"active_referee\":"<<json_quote(original_to_utf8(display.active->referee_name))
            <<",\"active_referee_profile\":"<<int(display.active->referee_profile)
            <<",\"active_weather\":"<<int(display.active->weather)
            <<",\"active_temperature\":"<<int(display.active->temperature);
        if(r.computer_tip_result)out<<",\"computer_tip_result\":"<<int(*r.computer_tip_result);
        if(r.postponed_date)out<<",\"postponed_date\":"<<*r.postponed_date;
        if(r.postponed_day)out<<",\"postponed_day\":"<<*r.postponed_day;
        if(r.postponed_queue_slot)out<<",\"postponed_queue_slot\":"<<int(*r.postponed_queue_slot);
        if(r.scheduled_date)out<<",\"scheduled_date\":"<<*r.scheduled_date;
        out<<'}';}
    out<<"]}";return out.str();
}
} // namespace bmh::ui
