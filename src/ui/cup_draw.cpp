// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/cup_draw.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <stdexcept>
#include <utility>

#include "game/cup.hpp"
#include "game/rating.hpp"
#include "game/messages.hpp"
#include "game/match.hpp"
#include "game/schedule.hpp"
#include "game/state.hpp"
#include "game/season.hpp"
#include "ui/manager.hpp"

namespace bmh::ui {
namespace {
constexpr std::array<uint8_t, 5> kTierSourceX{0,16,32,32,24};
constexpr int kCupOnlyTeams=30;

bool valid_rect(const Image& dst,const Image& src,int sx,int sy,int x,int y,int w,int h) {
    return w>0&&h>0&&sx>=0&&sy>=0&&x>=0&&y>=0&&sx+w<=src.width&&sy+h<=src.height&&
           x+w<=dst.width&&y+h<=dst.height;
}
void copy(Image& dst,const Image& src,int sx,int sy,int x,int y,int w,int h) {
    // FormatError2 passes ff as the VGA plane bitmask. The copy is opaque and an
    // out-of-bounds rectangle is rejected in full by SourceMaskCopy.
    if(!valid_rect(dst,src,sx,sy,x,y,w,h))return;
    for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx)
        dst.at(x+xx,y+yy)=src.pixels[size_t(sy+yy)*size_t(src.width)+size_t(sx+xx)];
}
void copy_key0(Image& dst,const Image& src,int sx,int sy,int x,int y,int w,int h) {
    if(!valid_rect(dst,src,sx,sy,x,y,w,h))return;
    for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx){
        const uint8_t pixel=src.pixels[size_t(sy+yy)*size_t(src.width)+size_t(sx+xx)];
        if(pixel!=0)dst.at(x+xx,y+yy)=pixel;
    }
}
void fill(Image& out,int x,int y,int w,int h,uint8_t c) {
    for(int yy=std::max(0,y);yy<std::min(out.height,y+h);++yy)
        for(int xx=std::max(0,x);xx<std::min(out.width,x+w);++xx)out.at(xx,yy)=c;
}
void source_frame(Image& out,int x,int y) {
    for(int xx=x-1;xx<=x+38;++xx)if(xx>=0&&xx<out.width&&y-1>=0&&y-1<out.height)out.at(xx,y-1)=0x46;
    for(int yy=y-1;yy<=y+38;++yy)if(yy>=0&&yy<out.height&&x-1>=0&&x-1<out.width)out.at(x-1,yy)=0x46;
    for(int xx=x-1;xx<=x+38;++xx)if(xx>=0&&xx<out.width&&y+38>=0&&y+38<out.height)out.at(xx,y+38)=0x40;
    for(int yy=y-1;yy<=y+38;++yy)if(yy>=0&&yy<out.height&&x+38>=0&&x+38<out.width)out.at(x+38,yy)=0x40;
}
int region_for_team(int id) {
    if(id==kNoTeam)return 0;
    int t=id;
    for(int league=0;league<kLeagueCount;++league){t-=kLeagueSizes[league];if(t<0)return league>=5?league-5+1:0;}
    if(t-kCupOnlyTeams>=0)return (t-kCupOnlyTeams)/7-5+10;
    return 0;
}
int league_for_team(int id) { return id>0x164?0:team_league(id); }
uint8_t manager_color(const GameState& game,int id,uint8_t base) {
    const int active=game.memory.has(::bmh::layout::ActiveManager)?game.memory.u8(::bmh::layout::ActiveManager):-1;
    const int count=game.memory.has(::bmh::layout::ManagerCount)?game.memory.u8(::bmh::layout::ManagerCount):int(game.manager_teams.size());
    const bool active_context=game.memory.has(::bmh::layout::DeskPosition)&&game.memory.u8(::bmh::layout::DeskPosition)<count;
    for(size_t slot=0;slot<std::min<size_t>(4,game.manager_teams.size());++slot) {
        const size_t rec=slot*0xb0c+0xa8;
        if(rec>=game.manager_records.size()||!game.manager_records[rec])continue;
        if(game.manager_teams[slot]==id)return int(slot)==active&&active_context?0x7a:0x75;
    }
    if(game.memory.has(::bmh::layout::AlltimeTable_TeamColor_RuntimeField)&&game.memory.u16(::bmh::layout::AlltimeTable_TeamColor_RuntimeField)==id)return 0xf7;
    return base;
}
std::string source_field(int value,int width) {
    std::string result=std::to_string(value);
    if(int(result.size())<width)result.insert(0,size_t(width-int(result.size())),'%');
    return result;
}
std::string text_at(const TextDb& texts,size_t i) {
    if(i>=texts.strings.size())throw FormatError("cup draw: missing Source TEXTD entry");
    return original_to_utf8(texts[i]);
}
}

CupDrawProjection cup_draw_from_game(const GameState& game,const SeasonCupDrawEvent& event,
                                     const LeImage& exe,const TextDb& texts) {
    if(event.entry_address<kCupTable||(event.entry_address-kCupTable)%kCupEntrySize||
       event.count>64||event.participants.size()<event.count||event.round<0||event.round>6)
        throw FormatError("cup draw: invalid Source event");
    const uint32_t entry=event.entry_address;
    const uint8_t flags=event.flags?event.flags:game.memory.u8(entry+0x14);
    const uint8_t qualifier=game.memory.u8(entry+0x1c);
    int round_index=((flags&4)?event.round:0)+1+qualifier;
    if(qualifier==1&&event.round==0)--round_index;
    if(round_index<0||round_index>31)throw FormatError("cup draw: invalid Source round text index");
    CupDrawProjection out;
    out.phase=event.phase;out.entry_address=event.entry_address;out.competition=event.competition;
    out.round=event.round;out.offset=event.offset;out.count=event.count;
    out.flags=flags;out.group_stage=event.group_stage;out.group_index=event.group_index;out.group_count=event.group_count;
    out.heading=text_at(texts,size_t((::bmh::layout::CupDraw_FormatError_ExecutableField4-::bmh::layout::TextPointerTable)/4)+size_t(round_index));
    out.footer=text_at(texts,size_t((::bmh::layout::CupDraw_FormatError_ExecutableField6-::bmh::layout::TextPointerTable)/4)+size_t(event.competition));
    if(out.group_stage) {
        out.group_round_suffix=text_at(texts,size_t((::bmh::layout::CupDraw_FormatError_TextPointer6-::bmh::layout::TextPointerTable)/4));
        const size_t round_digit_index=size_t((::bmh::layout::Calendar_Information_TextPointer2-::bmh::layout::TextPointerTable)/4);
        const size_t group_digit_index=size_t((::bmh::layout::CupDraw_FormatError_ExecutableField5-::bmh::layout::TextPointerTable)/4);
        std::string round_digit=text_at(texts,round_digit_index);
        std::string group_digit=text_at(texts,group_digit_index);
        if(round_digit.size()>16)round_digit[16]=char('0'+event.group_index+1);
        if(group_digit.size()>25)
            group_digit[25]=char(event.phase==SeasonCupDrawPhase::draw?'0'+event.group_index+1:
                (event.group_index==0?'1':'0'+event.group_index));
        out.group_caption=group_digit;
        const size_t heading_index=size_t((::bmh::layout::CupDraw_FormatError_ExecutableField4-::bmh::layout::TextPointerTable)/4)+size_t(round_index);
        const size_t footer_index=size_t((::bmh::layout::CupDraw_FormatError_ExecutableField6-::bmh::layout::TextPointerTable)/4)+size_t(event.competition);
        if(heading_index==round_digit_index)out.heading=round_digit;
        else if(heading_index==group_digit_index)out.heading=group_digit;
        if(footer_index==round_digit_index)out.footer=round_digit;
        else if(footer_index==group_digit_index)out.footer=group_digit;
        const auto& form=texts[72];
        for(size_t i=0;i<out.group_form_glyphs.size();++i)
            if(i<form.size())out.group_form_glyphs[i]=original_to_utf8(form.substr(i,1));
    }
    out.decorated_background=!game.memory.has(::bmh::layout::PlainBackground)||game.memory.u8(::bmh::layout::PlainBackground)==0;
    out.original_table_panel=!game.memory.has(::bmh::layout::SolidPanelBackground)||game.memory.u8(::bmh::layout::SolidPanelBackground)==0;
    out.gradient_text=(!game.memory.has(::bmh::layout::TextGradientDisabled)||game.memory.u8(::bmh::layout::TextGradientDisabled)==0)&&exe.u8(::bmh::layout::TextGradientMode)==2;
    out.previous_rank_visible=false;
    out.source_eligible=(event.competition>=4&&event.competition<=7)||
        (game.memory.has(::bmh::layout::CompetitionSelection)&&(game.memory.u32(::bmh::layout::CompetitionSelection)&1));
    if(game.memory.has(::bmh::layout::CupDraw_FormatError_RuntimeField)) {
        const int speed=game.memory.u8(::bmh::layout::CupDraw_FormatError_RuntimeField);
        out.source_option_delay_ms=speed>=0&&speed<=5?(speed/2)*20:speed*20;
    }
    out.clubs.reserve(event.count);
    for(size_t i=0;i<event.count;++i) {
        const uint16_t id=event.participants[i];
        if(size_t(id)*2+1>=game.club_heap_78.size())throw FormatError("cup draw: missing Source badge selector");
        const uint16_t selector=uint16_t(game.club_heap_78[size_t(id)*2]|uint16_t(game.club_heap_78[size_t(id)*2+1])<<8);
        const uint8_t league=uint8_t(std::clamp(league_for_team(id),0,255));
        const uint8_t base=event.group_stage?0x6a:(region_for_team(id)?0x67:0x5f);
        const uint8_t color=manager_color(game,id,base);
        const uint8_t reveal_color=color==base?0:0x27;
        std::string context;
        if(event.group_stage&&event.phase==SeasonCupDrawPhase::overview) {
        const bool human_national=game.manager_of(id)>=0||
            (game.memory.has(::bmh::layout::CupDraw_FormatError_RuntimeField2)&&game.memory.has(::bmh::layout::CupDraw_FormatError_RuntimeField3)&&game.memory.u16(::bmh::layout::CupDraw_FormatError_RuntimeField2)==0x165&&game.memory.u16(::bmh::layout::CupDraw_FormatError_RuntimeField3)==id);
        if(id>=168&&!human_national) {
            if(size_t(id-168)>=game.foreign_strength.size())throw FormatError("cup draw: missing Source foreign-strength byte");
            const uint8_t strength=game.foreign_strength[size_t(id-168)];
            const unsigned band=strength>>5;
            context=text_at(texts,size_t((::bmh::layout::CupDraw_FormatError_TextPointer4-::bmh::layout::TextPointerTable)/4)+band)+original_to_utf8(exe.cstring(::bmh::layout::CupDraw_FormatError_TextPointer));
            for(unsigned bit=0;bit<3;++bit)
                context+=original_to_utf8(exe.cstring(::bmh::layout::CupDraw_FormatError_ExecutableField3+(((strength>>(3-bit))&1u)*2u)));
            context+=original_to_utf8(exe.cstring(::bmh::layout::CupDraw_FormatError_ExecutableField));
        } else {
            std::array<int,5> values{};
            if(id>=168&&id!=kNoTeam) {
                const auto snapshot=std::find_if(event.group_average_snapshots.begin(),event.group_average_snapshots.end(),
                    [&](const auto& v){return v.runtime_id==id;});
                if(snapshot==event.group_average_snapshots.end())
                    throw FormatError("cup draw: missing live TeamRating group-average snapshot");
                values=snapshot->values;
            } else {
                values={team_average(game,id,0x0f1).value,team_average(game,id,0x0f2).value,
                        team_average(game,id,0x0f4).value,team_average(game,id,0x0f8).value,
                        team_average(game,id,0x0ff).value};
            }
            context=original_to_utf8(exe.cstring(::bmh::layout::CupDraw_FormatError_ExecutableField2));
            for(size_t i=0;i<std::size(values);++i) {
                context+=source_field(values[i],2);
                if(i<3)context+=text_at(texts,size_t((uint32_t(::bmh::layout::CupDraw_FormatError_TextPointer5+i*4)-::bmh::layout::TextPointerTable)/4));
                else context+=original_to_utf8(exe.cstring(i==3?::bmh::layout::CupDraw_FormatError_TextPointer2: ::bmh::layout::CupDraw_FormatError_TextPointer3));
            }
        }
        }
        out.clubs.push_back({id,selector,league,uint8_t(region_for_team(id)),color,reveal_color,
                             original_to_utf8(game.club_name(id)),std::move(context)});
    }
    if(out.group_stage&&event.phase==SeasonCupDrawPhase::overview) {
        const LeagueTable table=cup_group_table(game.memory,entry);
        if(game.memory.has(entry+0x1d)&&game.memory.u8(entry+0x1d)>1)
            for(size_t i=0;i<event.count;++i)out.previous_rank_visible|=table.previous_rank(int(i))!=0;
        const uint32_t list=game.memory.pointer(entry);
        out.group_rows.reserve(event.count);
        for(size_t i=0;i<event.count;++i) {
            const int played=table.games(int(i)),wins=table.wins[int(i)],defeats=table.defeats[int(i)];
            const int rank=table.rank(int(i));
            const int qualifying=game.memory.u8(entry+0x1e)&0x0f;
            const int qualification_limit=game.memory.u8(entry+0x1f);
            const uint8_t rank_color=qualifying*(rank+1)<=qualification_limit?0x7a:
                qualifying*rank+1<=qualification_limit?0x27:0xf7;
            out.group_rows.push_back({game.memory.u16(list+uint32_t(i)*2),rank+1,played,wins,
                                      played-wins-defeats,defeats,table.previous_rank(int(i))+1,
                                      table.goals_for[int(i)],table.goals_against[int(i)],table.points[int(i)],
                                      table.form[int(i)],rank_color,
                                      manager_color(game,game.memory.u16(list+uint32_t(i)*2),0x5f)});
        }
        const int schedule_teams=cup_group_schedule_teams(game.memory,entry);
        if(schedule_teams!=int(event.count))throw FormatError("cup draw: Source group schedule pointer disagrees with its entry team count");
        const int rounds=game.memory.u8(entry+0x1b)/((flags&1)+1);
        for(const Fixture& fixture:generate_fixtures(schedule_teams,event.round+1,rounds,0))
            out.group_fixtures.push_back({game.memory.u16(list+uint32_t(fixture.home)*2),
                                          game.memory.u16(list+uint32_t(fixture.away)*2)});
    }
    return out;
}

struct CupDrawScreen::Impl {
    CupDrawProjection p;std::filesystem::path data;LeImage exe;Image panel,background,widgets,tier,regions,animation;
    TextDb texts;SpriteFont sprite;Font font;Image screen,drawing,bottom;
    bool present=false,flush=false,poll=false,held=false,aborted=false,complete=false,skip=false;
    bool initial_poll_done=false,script_started=false;
    int initial=0,club=0,script=0,wait=0,option_delay=0;
    std::map<int,Image> badges;
    struct Op { bool copy; std::array<int,6> rect; int token; };
    std::vector<Op> ops;
    std::vector<size_t> revealed;
    size_t op_index=0,total_revealed=0;
    Impl(const std::filesystem::path& d,CupDrawProjection projection)
      :p(std::move(projection)),data(d),exe(load_le(asset_path(d,"BMH.exe"))),
       panel(load_vga(asset_path(d/"KB","67.VGA"))),background(load_vga(asset_path(d/"KB","56.VGA"))),
       widgets(load_vga(asset_path(d/"KB","0.VGA"))),tier(load_vga(asset_path(d/"KB","1.VGA"))),
       regions(load_vga(asset_path(d/"KB","24.VGA"))),
       animation(load_vga(asset_path(d/"KB","74.VGA"))),texts(load_textd(asset_path(d,"TEXTD.BLK"))),
       sprite(d),font(load_font(asset_path(d/"F","BMH3.FNT"))) {
        if(panel.width<272||panel.height<203||background.width<318||background.height<207||
           p.count>64||p.clubs.size()<p.count)throw FormatError("cup draw: invalid projection or Original assets");
        if(p.phase==SeasonCupDrawPhase::draw){screen=make_base();
            draw_header(screen,p.group_stage?utf8_to_original(p.heading):p.heading,
                        p.group_stage?utf8_to_original(p.footer):p.footer);
            copy(screen,panel,0,0,29,22,261,203);bottom=screen;drawing=screen;present=true;flush=true;}
        else {draw_overview();drawing=screen;}
        for(size_t i=0;i<p.count;++i){const int id=p.clubs[i].runtime_id,sel=p.clubs[i].badge_selector&0x1ff;
            if(!((id>=0x5a&&id<=0x89)||(id>=0xa8&&id<=0x164)||id==0xfff)){
                const int atlas=sel>>3;if(!badges.contains(atlas))badges.emplace(atlas,load_vga(asset_path(data/"W",std::to_string(atlas)+".VGA")));
            }
        }
      }
    Image make_base() const {
        Image out=p.decorated_background?load_vga(asset_path(data/"KB","129.VGA")):Image(320,240,0);
        if(!p.decorated_background){fill(out,0,4,320,13,0x44);fill(out,1,5,318,11,0x39);fill(out,0,228,320,10,0x44);fill(out,1,229,318,8,0x39);}
        return out;
    }
    void draw_header(Image& out,const std::string& heading,const std::string& footer) const {
        const int x=172+(282-172)/2-sprite.text_width(heading)/2;
        sprite.draw(out,x,13,heading,5);sprite.draw(out,5,235,footer,5);
    }
    void draw_gradient_text(Image& out,int x,int baseline,const std::string& name,uint8_t color) const {
        const int top=baseline-font.baseline,width=font.text_width(name);
        int ramp=0;while(ramp<32&&exe.u8(::bmh::layout::Advertising_Text_RuntimeField2Unsigned+uint32_t(ramp))!=color)++ramp;
        if(!p.gradient_text||ramp==32){font.draw(out,x,top,name,color);return;}
        Image mask(std::max(1,width),font.height,0);font.draw(mask,0,0,name,1);
        for(int y=0;y<font.height;++y)for(int xx=0;xx<width;++xx)
            if(mask.at(xx,y)&&x+xx>=0&&x+xx<out.width&&top+y>=0&&top+y<out.height)
                out.at(x+xx,top+y)=exe.u8(::bmh::layout::Advertising_Text_RuntimeField+uint32_t(ramp*6+std::min(y,5)));
    }
    void source_badge(Image& out,const CupDrawClub& c,int x,int y) const {
        source_frame(out,x,y);
        const int id=c.runtime_id,selector=c.badge_selector&0x1ff;
        if((id>=0x5a&&id<=0x89)||(id>=0xa8&&id<=0x164)||id==0xfff)
            copy(out,widgets,8,0,x,y,38,38);
        else {
            const int atlas=selector>>3;copy(out,badges.at(atlas),(selector&7)*38,0,x,y,38,38);
        }
    }
    void group_marker(Image& out,const CupDrawClub& c,int x,int y) const {
        if(c.league_tier<5) {
            const int source_x=exe.u8(::bmh::layout::CupDraw_GroupMarker_RuntimeField+c.league_tier);
            // Copy -> UsesForTheseSprites skips the atlas's transparent color DesktopRuntimeField.
            copy_key0(out,tier,source_x,0x26,x,y,16,5);
        } else {
            const int region=c.country;
            const int sx=(region%12)*8+region%12,sy=(region/12)*6;
            copy(out,regions,sx,sy,x,y,9,6);
        }
    }
    int club_x(int n) const {return p.group_stage?38:((n&1)?168:38);}
    int club_y(int n) const {return p.group_stage?193+n*7:193+(n/2)*7;}
    void draw_revealed() {
        // Source restores only the bottom strip from the pre-reveal snapshot.
        copy(drawing,bottom,29,182,29,182,254,34);
        for(size_t j=0;j<revealed.size();++j){
            const int n=int(j),x=club_x(n),y=club_y(n);
            const auto& c=p.clubs[revealed[j]];
            const std::string raw=utf8_to_original(c.original_name);
            draw_gradient_text(drawing,x,y,raw,c.reveal_name_color);
            if(c.league_tier<5)copy_key0(drawing,tier,kTierSourceX[c.league_tier],38,
                                         x+(p.group_stage?90:90),y-5,16,5);
            if(!p.group_stage&&(n&1))draw_gradient_text(drawing,154,y,utf8_to_original(original_to_utf8(exe.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer))),0);
        }
    }
    void apply_script_copy(int sx,int sy,int dx,int dy,int w,int h){copy(drawing,animation,sx,sy,dx,dy,w,h);}
    void present_frame(bool copy_back,bool queue_flush=false) {std::swap(screen,drawing);if(copy_back)drawing=screen;present=true;flush=queue_flush;}
    void begin_script() {
        ops.clear();op_index=0;
        const auto c=[&](int sx,int sy,int dx,int dy,int w,int h){ops.push_back({true,{sx,sy,dx,dy,w,h},0});};
        const auto w=[&](int token){ops.push_back({false,{},token});};
        if(script==0){c(56,0,121,36,25,31);c(109,0,143,82,42,61);w(1);c(0,0,124,37,56,113);w(1);
          c(81,0,121,36,26,31);c(56,31,127,65,53,84);w(1);c(109,72,130,80,50,40);w(1);
          c(151,0,130,66,51,44);w(1);c(109,61,127,59,18,11);w(101);w(101);w(101);w(101);w(101);}
        else {c(202,0,136,70,48,44);w(1);c(195,44,136,92,50,45);w(1);
          c(159,44,149,82,36,69);w(1);c(145,61,168,143,12,10);w(1);}
    }
    bool process_script() {
        if(op_index>=ops.size()){
            if(script==0){script=1;begin_script();return true;}
            const size_t window=p.group_stage?4:8,drop=p.group_stage?1:2;
            if(revealed.size()>=window)revealed.erase(revealed.begin(),revealed.begin()+drop);
            revealed.push_back(size_t(club));++total_revealed;draw_revealed();present_frame(true,true);++club;script=0;script_started=false;
            if(size_t(club)>=p.count)complete=true;else poll=true;
            return true;
        }
        const auto op=ops[op_index++];
        if(op.copy){apply_script_copy(op.rect[0],op.rect[1],op.rect[2],op.rect[3],op.rect[4],op.rect[5]);return true;}
        const bool visible=op.token<=100;
        present_frame(visible,false);option_delay=p.source_option_delay_ms;wait=visible?op.token*20:(op.token-100)*20;held=true;return true;
    }
    void draw_overview() {
        screen=make_base();
        if(p.group_stage){draw_group_overview();return;}
        const int count=int(p.count),firstrow=(64-count)/2;
        const int baseline=firstrow*3+26;
        // CupGroupOverviewEffects(param3=300) keeps the compact list. Its LoadGameEndTaxAnimationAssets backing
        // rectangle follows the rows, even when fewer than 64 clubs remain:
        // y=baseline-7 .. baseline+6*(count/2)-5, sampling KB56 at y-19.
        if(p.original_table_panel&&count!=0){
            const int top=baseline-7,bottom=baseline+6*(count/2)-5;
            copy(screen,background,0,top-19,1,top+1,318,bottom-top-1);
            for(int x=0;x<320;++x)screen.at(x,top)=0x46;
            for(int y=top;y<=bottom;++y)screen.at(0,y)=0x46;
            for(int x=0;x<320;++x)screen.at(x,bottom)=0x40;
            for(int y=top;y<=bottom;++y)screen.at(319,y)=0x40;
        }
        draw_header(screen,p.footer,p.heading);
        for(int pair=0;pair<count/2;++pair){
            const int y=baseline+pair*6;
            draw_overview_club(screen,p.clubs[size_t(pair*2)],2,y);
            draw_overview_club(screen,p.clubs[size_t(pair*2+1)],110,y);
            draw_gradient_text(screen,100,y,utf8_to_original(original_to_utf8(exe.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer))),0x68);
        }
    }
    void draw_group_overview() {
        const int count=int(p.count);
        const auto exetext=[&](uint32_t address){return original_to_utf8(exe.cstring(address));};
        draw_header(screen,utf8_to_original(p.footer),utf8_to_original(p.heading));
        if((p.flags&4)==0&&count>2) {
            const std::string round=source_field(p.round+1,1)+p.group_round_suffix;
            draw_gradient_text(screen,2,22,utf8_to_original(round),0x5f);
        }
        const int fixture_baseline=count==2?0x68:0x27;
        const int fixture_top=fixture_baseline-8;
        const int fixture_bottom=fixture_baseline+16*(count/2)-6;
        if(p.original_table_panel&&count>0) {
            copy(screen,background,1,fixture_top-19,1,fixture_top+1,318,fixture_bottom-fixture_top-1);
            for(int x=0;x<320;++x){screen.at(x,fixture_top)=0x46;screen.at(x,fixture_bottom)=0x40;}
            for(int y=fixture_top;y<=fixture_bottom;++y){screen.at(0,y)=0x46;screen.at(319,y)=0x40;}
        }
        const int rounds=int(p.group_fixtures.size());
        for(int i=0;i<rounds;++i) {
            const int baseline=fixture_baseline+i*16;
            const auto pair=p.group_fixtures[size_t(i)];
            const auto find=[&](uint16_t id)->const CupDrawClub& {
                const auto it=std::find_if(p.clubs.begin(),p.clubs.end(),[&](const CupDrawClub& c){return c.runtime_id==id;});
                if(it==p.clubs.end())throw FormatError("cup draw: fixture references a club outside its owned group");
                return *it;
            };
            const auto& home=find(pair[0]);const auto& away=find(pair[1]);
            draw_gradient_text(screen,5,baseline,utf8_to_original(home.original_name),home.name_color);
            draw_gradient_text(screen,132,baseline,utf8_to_original(away.original_name),away.name_color);
            draw_gradient_text(screen,125,baseline,utf8_to_original(exetext(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer)),0x68);
            group_marker(screen,home,115,baseline-5);
            group_marker(screen,away,242,baseline-5);
            if(!home.source_context.empty())draw_gradient_text(screen,3,baseline+7,utf8_to_original(home.source_context),0x68);
            if(!away.source_context.empty())draw_gradient_text(screen,133,baseline+7,utf8_to_original(away.source_context),0x68);
        }
        if(count>2) {
            const int top=125,bottom=count*7+126;
            if(p.original_table_panel) {
                copy(screen,background,1,top-19,1,top+1,318,bottom-top-1);
                for(int x=0;x<320;++x){screen.at(x,top)=0x46;screen.at(x,bottom)=0x40;}
                for(int y=top;y<=bottom;++y){screen.at(0,y)=0x46;screen.at(319,y)=0x40;}
            }
            for(size_t i=0;i<p.group_rows.size();++i) {
                const auto& r=p.group_rows[i];const int baseline=132+(r.rank-1)*7;
                const auto club=std::find_if(p.clubs.begin(),p.clubs.end(),[&](const CupDrawClub& c){return c.runtime_id==r.runtime_id;});
                if(club==p.clubs.end())throw FormatError("cup draw: table references a club outside its owned group");
                draw_gradient_text(screen,30,baseline,utf8_to_original(club->original_name),r.name_color);
                const std::string rank=source_field(r.rank,2)+exetext(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer2);
                const std::string games=source_field(r.games,2)+exetext(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer4)+
                    source_field(r.wins,2)+exetext(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer5)+source_field(r.draws,2)+exetext(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer5)+
                    source_field(r.defeats,2)+exetext(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer4);
                std::string points=exetext(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer6);
                if(p.flags&0x80)points+=exetext(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer7);
                points+=source_field(r.points,2)+exetext(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer8);
                if(!(p.flags&0x80))
                    points+=exetext(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer9)+source_field(2*r.games-r.points,2)+exetext(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer8);
                std::string signed_diff;
                if(r.goals_for-r.goals_against<0)signed_diff=exetext(::bmh::layout::Bank_OriginalToUtf8_ExecutableField5);
                else if(r.goals_for-r.goals_against==0)signed_diff=exetext(::bmh::layout::Bank_OriginalToUtf8_ExecutableField7);
                else signed_diff=exetext(::bmh::layout::Bank_OriginalToUtf8_ExecutableField6);
                const std::string goals=source_field(r.goals_for,3)+exetext(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer9)+
                    source_field(r.goals_against,3)+exetext(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer10)+
                    signed_diff+source_field(std::abs(r.goals_for-r.goals_against),3)+exetext(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer11);
                draw_gradient_text(screen,1,baseline,utf8_to_original(rank),r.rank_color);
                if(p.previous_rank_visible) {
                    const int old=r.previous_rank;
                    draw_gradient_text(screen,12,baseline,utf8_to_original("("+source_field(old,2)+exetext(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer3)),
                        r.rank<old?0x75:old<r.rank?0xf7:0x6a);
                }
                draw_gradient_text(screen,114,baseline,utf8_to_original(games),0x6a);
                for(int form=0;form<8;++form) {
                    const unsigned bits=(r.form>>(3*form))&7u;
                    const unsigned glyph=(bits&4u)?(bits&3u):(bits&3u)+4u;
                    if(!p.group_form_glyphs[glyph].empty())
                        draw_gradient_text(screen,207-form*6,baseline,
                                           utf8_to_original(p.group_form_glyphs[glyph]),0x6a);
                }
                draw_gradient_text(screen,206,baseline,utf8_to_original(points),0x6a);
                draw_gradient_text(screen,255,baseline,utf8_to_original(goals),0x6a);
            }
        }
    }
    void draw_overview_club(Image& out,const CupDrawClub& c,int x,int baseline) const {
        const std::string raw=utf8_to_original(c.original_name);
        const int width=font.text_width(raw);if(x+width<=out.width&&baseline-font.baseline>=0)
            draw_gradient_text(out,x,baseline,raw,c.name_color);
        if(c.league_tier<5)copy_key0(out,tier,kTierSourceX[c.league_tier],38,x+(x==2?88:90),baseline-5,16,5);
    }
};

CupDrawScreen::CupDrawScreen(const std::filesystem::path& d,CupDrawProjection p)
 :impl_(std::make_shared<Impl>(d,std::move(p))){}
bool CupDrawScreen::advance_step(){auto& s=*impl_;s.present=false;s.flush=false;s.poll=false;s.held=false;s.option_delay=0;s.wait=0;
    if(s.complete)return false;
        if(s.p.phase==SeasonCupDrawPhase::overview){s.complete=true;return false;}
    if(s.initial<10){++s.initial;s.option_delay=s.p.source_option_delay_ms;s.poll=true;
        if(s.initial==10&&s.skip){s.aborted=true;s.complete=true;}return true;}
    if(s.aborted){s.complete=true;return false;}
    if(s.skip){s.aborted=true;s.complete=true;return false;}
    if(!s.initial_poll_done){s.initial_poll_done=true;s.poll=true;return true;}
    if(s.club>=int(s.p.count)){s.complete=true;return false;}
    if(!s.script_started){s.script_started=true;s.script=0;s.begin_script();}
    return s.process_script();
}
bool CupDrawScreen::present_needed()const{return impl_->present;}
bool CupDrawScreen::queue_flush_needed()const{return impl_->flush;}
int CupDrawScreen::option_delay_milliseconds()const{return impl_->option_delay;}
int CupDrawScreen::wait_milliseconds()const{return impl_->wait;}
bool CupDrawScreen::input_poll_needed()const{return impl_->poll;}
bool CupDrawScreen::held_mouse_check_needed()const{return impl_->held;}
bool CupDrawScreen::reveal_complete()const{return impl_->complete&&impl_->p.phase==SeasonCupDrawPhase::draw&&!impl_->aborted;}
bool CupDrawScreen::reveal_aborted()const{return impl_->aborted;}
size_t CupDrawScreen::revealed_count()const{return impl_->total_revealed;}
bool CupDrawScreen::source_input(uint8_t code){auto&s=*impl_;if(!s.poll)return false;if(code==2||code==8){
    if(s.initial<10)s.skip=true;else{s.aborted=true;s.complete=true;}return true;}return false;}
bool CupDrawScreen::source_held_mouse(uint8_t mask){auto&s=*impl_;if(!s.held)return false;if(mask&3){s.aborted=true;s.complete=true;s.club=int(s.p.count);s.poll=false;s.held=false;return true;}return false;}
Image CupDrawScreen::render()const{return impl_->screen;}
std::optional<uint16_t> CupDrawScreen::hit_test_team(int x,int y)const {
    const auto&s=*impl_;if(s.p.phase!=SeasonCupDrawPhase::overview)return{};
    if(s.p.group_stage) {
        for(size_t i=0;i<s.p.group_fixtures.size();++i) {
            const int baseline=(s.p.count==2?0x68:0x27)+int(i)*16;
            if(y<=baseline-6||y>=baseline)continue;
            const auto pair=s.p.group_fixtures[i];
            if(x>0&&x<127)return pair[0];
            if(x>125&&x<319)return pair[1];
        }
        return{};
    }
    const int first=(64-int(s.p.count))/2,base=first*3+26;
    for(size_t pair=0;pair<s.p.count/2;++pair){const int b=base+int(pair)*6;
        if(y<=b-6||y>=b)continue;
        if(x>0&&x<102)return s.p.clubs[pair*2].runtime_id;
        if(x>103&&x<319)return s.p.clubs[pair*2+1].runtime_id;
    }
    return{};
}
} // namespace bmh::ui
