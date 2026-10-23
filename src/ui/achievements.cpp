// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/world_layout.hpp"
#include "ui/achievements.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

#include "formats/binary.hpp"
#include "game/messages.hpp"
#include "game/schedule.hpp"
#include "ui/manager.hpp"
#include "ui/setup.hpp"

namespace bmh::ui {
namespace {
constexpr int kFirstPlotX = 0x41;
constexpr int kPlotRight = 0x107;
constexpr int kWindowLeft = 0x2b, kWindowTop = 0x14;
constexpr int kWindowRight = 0x11a, kWindowBottom = 0xd2;
constexpr uint32_t kLeagueHeaderTable = ::bmh::layout::LeagueHeaderTable;
constexpr uint32_t kRoundNameTable = ::bmh::layout::RoundNameTable;
constexpr uint32_t kEuropeNameTable = ::bmh::layout::EuropeNameTable;

void fill(Image& out, int x, int y, int w, int h, uint8_t color) {
    const int x0=std::max(0,x), y0=std::max(0,y), x1=std::min(out.width,x+w), y1=std::min(out.height,y+h);
    for(int yy=y0;yy<y1;++yy) for(int xx=x0;xx<x1;++xx) out.at(xx,yy)=color;
}
void copy(Image& out,const Image& source,int sx,int sy,int x,int y,int w,int h,int transparent=-1) {
    if(sx<0||sy<0||sx+w>source.width||sy+h>source.height)throw FormatError("achievements: Source KB56 bounds");
    for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx){const uint8_t c=source.pixels[size_t(sy+yy)*size_t(source.width)+size_t(sx+xx)];
        if(c!=transparent&&x+xx>=0&&y+yy>=0&&x+xx<out.width&&y+yy<out.height)out.at(x+xx,y+yy)=c;}
}
uint16_t rd16(const std::vector<uint8_t>& b,size_t at){return uint16_t(b.at(at))|uint16_t(uint16_t(b.at(at+1))<<8);}
// FNT files consume the game's single-byte encoding; only JSON/test boundaries use UTF-8.
std::string text(const TextDb& db,size_t id){return db.strings.at(id);}
std::string source_text(const GameState& game,uint32_t pointer_variable){return message_text(game,pointer_variable);}
}

bool achievements_available(const GameState& game) {
    // AchievementsFromGame's Source gate is LeagueRoundCounters: no league position has been recorded yet.
    // This is the first byte of the serialized nine-byte league_rounds block. Read the
    // decoded authoritative block so a cold-loaded save does not depend on an optional
    // runtime-memory mirror being present.
    return !game.league_rounds.empty() && game.league_rounds.front()!=0;
}

AchievementsState achievements_from_game(const GameState& game,const LeImage& exe,
                                           const DeskRequest& owner,const Image& underlay) {
    if(owner.kind!=DeskRequestKind::open_menu||owner.menu_kind!=4||owner.manager_slot<0||owner.manager_slot>3||
       game.memory.has(::bmh::layout::ActiveManager)&&game.memory.u8(::bmh::layout::ActiveManager)!=owner.manager_slot||
       size_t(owner.manager_slot)>=game.manager_teams.size()||owner.revision==UINT64_MAX||
       underlay.width!=320||underlay.height!=240||!game.texts)
        throw FormatError("achievements: invalid Source desk owner");
    const size_t base=size_t(owner.manager_slot)*0xb0c;
    if(base+0x556>game.manager_records.size())throw FormatError("achievements: truncated Source manager history");
    AchievementsState out;out.owner=owner;out.manager_slot=owner.manager_slot;out.underlay=underlay;
    out.season_count=std::clamp(game.season+1,1,50);out.selected_season=out.season_count-1;
    out.decorated_background=!game.memory.has(::bmh::layout::PlainBackground)||game.memory.u8(::bmh::layout::PlainBackground)==0;
    out.source_panel=!game.memory.has(::bmh::layout::SolidPanelBackground)||game.memory.u8(::bmh::layout::SolidPanelBackground)==0;
    out.gradient_text=!game.memory.has(::bmh::layout::TextGradientDisabled)||game.memory.u8(::bmh::layout::TextGradientDisabled)==0;
    out.title=original_to_utf8(source_text(game,::bmh::layout::Achievements_FormatError_TextPointer6));
    out.manager_team_name=original_to_utf8(game.club_name(game.manager_teams[size_t(owner.manager_slot)]));
    // AchievementsRuntimeField is a u16 per manager at manager-record +0x2aa: seasons before
    // this manager began. The displayed heading is a Source-formatted year number,
    // not the current club name.
    const int manager_start=rd16(game.manager_records,base+0x2aa);
    for(int season=0;season<out.season_count;++season){
        const size_t at=base+0xda+size_t(season)*3;
        auto& row=out.seasons[size_t(season)];row.league_result=game.manager_records.at(at);
        row.cup_result=game.manager_records.at(at+1);row.europe_result=game.manager_records.at(at+2);
        // Abs3 takes byte y endpoints: Source's intermediate char is passed
        // back as an unsigned byte, so coordinates 128..239 stay below the graph.
        row.graph_y=int(uint8_t(0xa4-2*(row.europe_result&0x1f)-
            (2*int(row.cup_result)+(0x48-int(row.league_result))/2)));
        // AchievementsFromGame reads Achievements_FormatError_RuntimeField2 as the 16-bit Source year base.
        row.season_label=format_number(game,int(exe.u16(::bmh::layout::Achievements_FormatError_RuntimeField2))+manager_start+season,0xc4)+
            exe.cstring(::bmh::layout::Achievements_FormatError_RuntimeField);
        if(row.league_result==0){row.league_text=source_text(game,::bmh::layout::Achievements_FormatError_TextPointer5);row.league_color=0x27;}
        else if(row.league_result<0xfe) {
            // Int4: the stored value is a one-based club id. Resolve its
            // Original division, subtract CareerRuntimeField6's first club, then append
            // the corresponding Achievements_FormatError_TextPointer3 Source text pointer.
            const int team=int(row.league_result)-1;
            const int league=team_league(team);
            if(league<kLeagueCount){
                row.league_color=0x69;
                row.league_text=format_number(game,int(row.league_result)-league_first_team(league),2)+
                    exe.cstring(::bmh::layout::Achievements_FormatError_ExecutableField)+source_text(game,::bmh::layout::Achievements_FormatError_TextPointer4)+
                    source_text(game,::bmh::layout::Achievements_FormatError_TextPointer3+uint32_t(league)*4);
            }
        }
        auto cup_text=[&](uint8_t result,int heading_index){
            std::string value=source_text(game,kLeagueHeaderTable+uint32_t(heading_index)*4)+exe.cstring(::bmh::layout::Achievements_FormatError_TextPointer);
            if((result&0x1f)==7)return value+source_text(game,::bmh::layout::Achievements_FormatError_TextPointer2);
            int round=result&7;
            if(round==3&&(result>>5)==1)round=8;
            value+=source_text(game,kRoundNameTable+uint32_t(round)*4);
            return value;
        };
        row.cup_text=cup_text(row.cup_result,1);
        row.europe_text=cup_text(row.europe_result,2);
        const int europe_stage=row.europe_result&0x1f;
        if(europe_stage!=0&&europe_stage!=8){
            const int competition=row.europe_result>>5;
            row.europe_text += source_text(game,::bmh::layout::Achievements_FormatError_TextPointer7)+source_text(game,kEuropeNameTable+uint32_t(competition)*4);
        }
    }
    return out;
}

AchievementsScreen::AchievementsScreen(const std::filesystem::path& data,AchievementsState state)
    :exe_(load_le(asset_path(data,"BMH.exe"))),texts_(load_textd(asset_path(data,"TEXTD.BLK"))),
     body_(load_font(asset_path(data/"F","BMH3.FNT"))),
     header_(load_font(asset_path(data/"F","BMH2.FNT"))),
     sprite_font_(data),background_(load_vga(asset_path(data/"KB","129.VGA"))),
     panel_(load_vga(asset_path(data/"KB","56.VGA"))),state_(std::move(state)) {
    if(state_.manager_slot<0||state_.manager_slot>3||state_.season_count<1||state_.season_count>50||
       state_.selected_season < -1||state_.selected_season>=state_.season_count||state_.underlay.width!=320||
       state_.underlay.height!=240||background_.width!=320||background_.height!=240||
       panel_.width<281||panel_.height<190)
        throw FormatError("achievements: incomplete Original projection/assets");
    (void)render();
}

void AchievementsScreen::draw_text(Image& out,int x,int baseline,std::string_view value,uint8_t color)const{
    if(value.empty())return;
    // DrawnFirstBy validates baseline-font-height, then passes the baseline to Draw.
    body_.draw(out,x,baseline-body_.baseline,value,color);
}

Image AchievementsScreen::render()const{
    Image out=state_.decorated_background?background_:Image(320,240,0);
    if(!state_.decorated_background){
        fill(out,0,4,320,13,0x44);fill(out,0,0xe4,320,10,0x44);
        fill(out,1,5,318,11,0x39);fill(out,1,0xe5,318,8,0x39);
    }
    sprite_font_.draw(out,SpriteFont::centered_title_x(sprite_font_.text_width(state_.title)),0x0d,state_.title,5);
    sprite_font_.draw(out,5,0xeb,state_.manager_team_name,5);
    if(state_.source_panel)copy(out,panel_,kWindowLeft,kWindowTop-19,kWindowLeft+1,kWindowTop+1,
        kWindowRight-kWindowLeft-1,kWindowBottom-kWindowTop-1,0xc0);
    else fill(out,kWindowLeft,kWindowTop,kWindowRight-kWindowLeft+1,kWindowBottom-kWindowTop+1,0x42);
    for(int x=kWindowLeft;x<=kWindowRight;++x){out.at(x,kWindowTop)=0x46;out.at(x,kWindowBottom)=0x40;}
    for(int y=kWindowTop;y<=kWindowBottom;++y){out.at(kWindowLeft,y)=0x46;out.at(kWindowRight,y)=0x40;}
    // AchievementsFromGame reads these exact y coordinates and palette indices from BMH.exe.
    const std::array<int,3> baselines{{exe_.u8(::bmh::layout::Achievements_Fill_RuntimeField4),exe_.u8(::bmh::layout::Achievements_Fill_RuntimeField5),exe_.u8(::bmh::layout::Achievements_Fill_RuntimeField6)}};
    // AchievementsFromGame draws the three colored, three-pixel graph traces before the headers/bars.
    for(int season=1;season<state_.season_count;++season){
        const int x=0x41+season*4;
        const int previous=state_.seasons[size_t(season-1)].graph_y;
        const int current=state_.seasons[size_t(season)].graph_y;
        for(int line=0;line<3;++line){
            int x0=x-4,y0=uint8_t(previous+line),x1=x,y1=uint8_t(current+line);
            // Abs3 rejects the complete segment if either byte endpoint
            // lies below the screen, rather than clipping a partial line.
            if(y0>out.height||y1>out.height)continue;
            const int dx=std::abs(x1-x0), sx=x0<x1?1:-1;
            const int dy=std::abs(y1-y0), sy=y0<y1?1:-1;
            // FaStartsItsAccumulatorAtZeroMove -> Abs starts its accumulator at zero. Move
            // the major axis first; the minor axis advances only on overflow.
            int accumulator=0;
            auto plot=[&]{if(x0>=0&&x0<320&&y0>=0&&y0<240)
                out.at(x0,y0)=exe_.u8(::bmh::layout::Achievements_Fill_RuntimeField+uint32_t(line));};
            plot();
            if(dy<dx) {
                for(int step=0;step<dx;++step){x0+=sx;accumulator+=dy;
                    if(accumulator>=dx){accumulator-=dx;y0+=sy;}plot();}
            } else {
                for(int step=0;step<dy;++step){y0+=sy;accumulator+=dx;
                    if(accumulator>=dy){accumulator-=dy;x0+=sx;}plot();}
            }
        }
    }
    constexpr std::array<uint32_t,3> headings{{::bmh::layout::LeagueHeaderTable,::bmh::layout::Achievements_Fill_ExecutableField,::bmh::layout::Achievements_Fill_ExecutableField2}};
    for(int column=0;column<3;++column){
        const int y=baselines[size_t(column)];
        const auto& label=texts_[text_id(headings[size_t(column)])];
        // AchievementsFromGame selects font 2 for each centered heading; DrawnFirstBy
        // centers within inclusive x=0..319 and then restores font 3.
        const int width=header_.text_width(label);
        header_.draw(out,319/2-width/2,y+9-header_.baseline,label,0x6b);
        fill(out,0x3f,y,0xca,1,0x5f);
    }
    for(int season=0;season<state_.season_count;++season){
        const auto& row=state_.seasons[size_t(season)];const int x=0x41+season*4;
        if(row.league_result<0xfe){
            int remaining=(world_layout().german_teams-0x12)-row.league_result;
            int y=baselines[0]-1, color_index=0, step_index=4;
            while(remaining>=0){
                const int step=exe_.u8(::bmh::layout::LeagueSizes+uint32_t(step_index));
                const int height=std::min(remaining,step);
                const int palette=row.league_result==0?4:color_index;
                // Fill2 calls Fill3 with inclusive x endpoints (x+1..x+2 and x..x+1).
                fill(out,x+1,y-height,2,height+1,exe_.u8(::bmh::layout::Achievements_Fill_RuntimeField2+uint32_t(palette*2)));
                fill(out,x,y-height-1,2,height+1,exe_.u8(::bmh::layout::Achievements_Fill_RuntimeField3+uint32_t(palette*2)));
                y-=step;remaining-=step;
                ++color_index;
                const int previous=step_index--;
                if(step_index==2)step_index=previous-2;
            }
        }
        if(row.cup_result!=0){
            const int rounds=row.cup_result;
            const int height=rounds*3;
            const int y=baselines[1]-1;
            const int palette=rounds==7?4:2;
            fill(out,x+1,y-height,2,height+1,exe_.u8(::bmh::layout::Achievements_Fill_RuntimeField2+uint32_t(palette*2)));
            fill(out,x,y-height-1,2,height+1,exe_.u8(::bmh::layout::Achievements_Fill_RuntimeField3+uint32_t(palette*2)));
        }
        const int europe_round=row.europe_result&0x1f;
        if(europe_round!=0){
            const int height=europe_round*3;
            const int y=baselines[2]-1;
            const int palette=europe_round==7?4:row.europe_result>>5;
            fill(out,x+1,y-height,2,height+1,exe_.u8(::bmh::layout::Achievements_Fill_RuntimeField2+uint32_t(palette*2)));
            fill(out,x,y-height-1,2,height+1,exe_.u8(::bmh::layout::Achievements_Fill_RuntimeField3+uint32_t(palette*2)));
        }
    }
    const int detail_y=0xbb;
    if(state_.source_panel)copy(out,panel_,0x2d,0xb4-19,0x2e,0xb5,0x118-0x2d-1,0xcd-0xb4-1,0xc0);
    else fill(out,0x2d,0xb4,0x118-0x2d+1,0xcd-0xb4+1,0x42);
    for(int x=0x2d;x<=0x118;++x){out.at(x,0xb4)=0x46;out.at(x,0xcd)=0x40;}
    for(int y=0xb4;y<=0xcd;++y){out.at(0x2d,y)=0x46;out.at(0x118,y)=0x40;}
    if(state_.selected_season>=0){
        const auto& selected=state_.seasons[size_t(state_.selected_season)];
        draw_text(out,0x32,detail_y,selected.season_label,0x69);
        if(selected.league_result==0xff){
            draw_text(out,0x42,0xd9,text(texts_,text_id(::bmh::layout::Achievements_Fill_TextPointer)),0x69);
        }else{
            draw_text(out,0x4c,detail_y,selected.league_text,selected.league_color);
            draw_text(out,0x4c,detail_y+8,selected.cup_text,(selected.cup_result&0x1f)==7?0x27:0x69);
            draw_text(out,0x4c,detail_y+16,selected.europe_text,(selected.europe_result&0x1f)==7?0x27:0x69);
        }
    }
    return out;
}

bool AchievementsScreen::move(int x,int y){
    if(pending_)return false;
    const int previous=state_.selected_season;
    if(x<kFirstPlotX||x>kPlotRight||y<0x16||y>0xd0){
        state_.selected_season=-1;
        return previous!=-1;
    }
    const int season=(x-kFirstPlotX)/4;
    // Source leaves the previous detail intact when the pointer lands in a
    // four-pixel column beyond this manager's saved history.
    if(season<state_.season_count)state_.selected_season=season;
    return state_.selected_season!=previous;
}

AchievementsRequest AchievementsScreen::pointer_down(int x,int y,bool right){
    if(pending_)return {};
    if(right){pending_=AchievementsRequest{AchievementsRequestKind::close,state_.owner,state_.selected_season};return *pending_;}
    if(x<kFirstPlotX||x>kPlotRight||y<0x16||y>0xd0)return {};
    const int season=(x-kFirstPlotX)/4;
    if(season<0||season>=state_.season_count||season==state_.selected_season)return {};
    state_.selected_season=season;return {AchievementsRequestKind::none,state_.owner,season};
}

} // namespace bmh::ui
