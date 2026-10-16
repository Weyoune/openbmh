// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/desk_records.hpp"

#include <algorithm>
#include <string>

#include "game/lineup.hpp"
#include "game/messages.hpp"
#include "game/schedule.hpp"
#include "ui/manager.hpp"
#include "formats/textd.hpp"

namespace bmh::ui {
std::filesystem::path asset_path(const std::filesystem::path&, const std::string&);
namespace {
void copy(Image& out, const Image& src, int sx, int sy, int x, int y, int w, int h) {
    if (sx < 0 || sy < 0 || x < 0 || y < 0 || sx+w>src.width || sy+h>src.height ||
        x+w>out.width || y+h>out.height) throw FormatError("desk records: Original asset bounds");
    for(int yy=0;yy<h;++yy) for(int xx=0;xx<w;++xx)
        out.at(x+xx,y+yy)=src.pixels[size_t(sy+yy)*size_t(src.width)+size_t(sx+xx)];
}
void fill_rect(Image& out,int x,int y,int w,int h,uint8_t color) {
    for(int yy=std::max(0,y);yy<std::min(out.height,y+h);++yy)
        for(int xx=std::max(0,x);xx<std::min(out.width,x+w);++xx) out.at(xx,yy)=color;
}
uint8_t source_team_color(const GameState& g,int team) {
    const size_t count=std::min<size_t>(g.manager_teams.size(),4);
    const int active=g.memory.has(::bmh::layout::ActiveManager)?g.memory.u8(::bmh::layout::ActiveManager):-1;
    const int managers=g.memory.has(::bmh::layout::ManagerCount)?g.memory.u8(::bmh::layout::ManagerCount):int(count);
    const bool current=g.memory.has(::bmh::layout::DeskPosition)&&g.memory.u8(::bmh::layout::DeskPosition)<managers;
    for(size_t slot=0;slot<count;++slot) {
        const size_t at=slot*0xb0c+0xa8;
        if(at>=g.manager_records.size()||g.manager_records[at]==0)continue;
        if(g.manager_teams[slot]==team)return int(slot)==active&&current?0x7a:0x75;
    }
    if(g.memory.has(::bmh::layout::AlltimeTable_TeamColor_RuntimeField)&&g.memory.u16(::bmh::layout::AlltimeTable_TeamColor_RuntimeField)==team)return 0xf7;
    return 0x5f;
}

}

DeskRecordsState fair_play_state_from_game(const GameState& g, int league, const Image& underlay,int manager_slot) {
    if(league<0||league>=9) throw FormatError("fair play: invalid Source league");
    const int first=league_first_team(league), count=kLeagueSizes[league];
    if(first<0||count<=0||first+count>138||g.club_ids.size()<size_t(first+count))
        throw FormatError("fair play: incomplete Source league");
    std::vector<int16_t> teams(static_cast<size_t>(count)); std::vector<uint32_t> keys(static_cast<size_t>(count));
    for(int i=0;i<count;++i) {
        const int team=first+i; teams[size_t(i)]=int16_t(team);
        const uint16_t cards=g.memory.u16(::bmh::layout::DeskRecords_Teams_RuntimeField+uint32_t(team)*2);
        keys[size_t(i)]=uint32_t((int16_t(cards)>>8)*4+(cards&0xff));
    }
    // FairPlayStateFromGame passes ascending=1 to the same in-place Source quicksort.
    quicksort_by_key(teams.data(),keys.data(),count,true);
    DeskRecordsState out; out.kind=DeskRecordKind::fair_play;out.league=league;out.underlay=underlay;
    out.original_panel=!g.memory.has(::bmh::layout::SolidPanelBackground)||g.memory.u8(::bmh::layout::SolidPanelBackground)==0;
    if(manager_slot>=0&&size_t(manager_slot)<g.manager_teams.size())
        out.manager_name=g.club_name(g.manager_teams[size_t(manager_slot)]);
    out.fair_play_rows.reserve(size_t(count));
    for(int i=0;i<count;++i) {
        const int team=teams[size_t(i)];const uint16_t cards=g.memory.u16(::bmh::layout::DeskRecords_Teams_RuntimeField+uint32_t(team)*2);
        out.fair_play_rows.push_back({team,i+1,int(cards&0xff),int(int16_t(cards)>>8),int(keys[size_t(i)]),g.club_name(team),source_team_color(g,team)});
    }
    return out;
}

DeskRecordsScreen::DeskRecordsScreen(const std::filesystem::path& data, DeskRecordsState state)
    : exe_(load_le(asset_path(data,"BMH.exe"))),texts_(load_textd(asset_path(data,"TEXTD.BLK"))),
      font_(load_font(asset_path(data/"F","BMH3.FNT"))),sprite_font_(data),
      background_(load_vga(asset_path(data/"KB","129.VGA"))),state_(std::move(state)) {
    if(state_.underlay.width!=320||state_.underlay.height!=240) throw FormatError("desk records: missing accepted desk underlay");
    if(state_.kind==DeskRecordKind::fair_play) {
        kb56_=load_vga(asset_path(data/"KB","56.VGA"));
        const int bottom=int(state_.fair_play_rows.size())*9+38;
        if(state_.league<0||state_.league>=9||state_.fair_play_rows.size()!=size_t(kLeagueSizes[state_.league])||
           kb56_.width<296||kb56_.height<9+(bottom-28-1))
            throw FormatError("fair play: incomplete Original view");
    } else if(state_.highscore.ranked) {
        kb51_=load_vga(asset_path(data/"KB","51.VGA"));
        if(kb51_.width<112||kb51_.height<74||state_.highscore.rows.size()!=5)
            throw FormatError("highscore: incomplete Original view");
    } else if(state_.highscore.rows.size()!=5) throw FormatError("highscore: incomplete Original view");
    if(state_.kind==DeskRecordKind::highscore&&!state_.highscore.ranked)
        alternate_=std::make_unique<GameEndAlternateScoreScreen>(load_game_end_alternate_score_assets(data),state_.underlay,state_.gradient_text);
}
void DeskRecordsScreen::text(Image& out,int x,int baseline,std::string_view value,uint8_t color,bool shadow) const {
    const int width=font_.text_width(value), top=baseline-font_.baseline;
    const int offset=shadow?2:0;
    if(x<0||x+width+offset>out.width||top<0||top+font_.height+offset>out.height)return;
    if(shadow)font_.draw(out,x+2,top+2,value,0x40);
    if(!state_.gradient_text){font_.draw(out,x,top,value,color);return;}
    Image mask(std::max(1,width),font_.height);
    font_.draw(mask,0,0,value,1);
    int index=0;while(index<32&&exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2+uint32_t(index))!=color)++index;
    for(int y=0;y<font_.height;++y)for(int xx=0;xx<width;++xx)
        if(mask.at(xx,y))out.at(x+xx,top+y)=index<32?exe_.u8(::bmh::layout::Advertising_Text_RuntimeField+uint32_t(index*6+y)):color;
}
Image DeskRecordsScreen::render() const {
    if(alternate_) {
        return alternate_->render();
    }
    Image out=state_.decorated_background?background_:Image(320,240);
    if(!state_.decorated_background){
        fill_rect(out,0,4,320,13,0x44);fill_rect(out,1,5,318,11,0x39);
        fill_rect(out,0,228,320,10,0x44);fill_rect(out,1,229,318,8,0x39);
    }
    if(state_.kind==DeskRecordKind::fair_play) {
        // LoadGameEndTaxAnimationAssets(0,26,28,296,count*9+38): exact absolute Source edges.
        const int right=296, bottom=int(state_.fair_play_rows.size())*9+38;
        if(state_.original_panel) copy(out,kb56_,26,9,27,29,right-27,bottom-29);
        else fill_rect(out,26,28,right-26+1,bottom-28+1,0x42);
        for(int y=28;y<=bottom;++y)out.at(26,y)=0x46;
        for(int x=26;x<=right;++x)out.at(x,28)=0x46;
        for(int x=26;x<=right;++x)out.at(x,bottom)=0x40;
        for(int y=28;y<=bottom;++y)out.at(right,y)=0x40;
        const auto heading=original_to_utf8(texts_.strings.at((::bmh::layout::DeskRecords_FillRect_TextPointer5-::bmh::layout::TextPointerTable)/4));
        const int heading_x=SpriteFont::centered_title_x(sprite_font_.text_width(heading));
        sprite_font_.draw(out,heading_x,13,heading,5);
        sprite_font_.draw(out,5,235,original_to_utf8(state_.manager_name),5);
        for(size_t i=0;i<state_.fair_play_rows.size();++i) {
            const auto& row=state_.fair_play_rows[i];const int y=40+int(i)*9;
            text(out,30,y,format_number(GameState{},row.rank,0x82)+exe_.cstring(::bmh::layout::DeskRecords_FillRect_TextPointer3)+row.name,row.name_color);
            const size_t yellow=(::bmh::layout::DeskRecords_FillRect_TextPointer6-::bmh::layout::TextPointerTable)/4,red=(::bmh::layout::DeskRecords_FillRect_ExecutableField4-::bmh::layout::TextPointerTable)/4,points=(::bmh::layout::DeskRecords_FillRect_ExecutableField5-::bmh::layout::TextPointerTable)/4;
            std::string stats=format_number(GameState{},row.yellow,0x83)+texts_.strings.at(yellow)+
                format_number(GameState{},row.red,2)+texts_.strings.at(red)+format_number(GameState{},row.points,3)+texts_.strings.at(points);
            text(out,165,y,stats,row.name_color);
        }
    } else {
        // GameEndAlternateScoreScreen(1) calls PaletteOverride(0x33), decimal 51.VGA, at (100,25).
        copy(out,kb51_,0,0,100,25,112,74);
        // ColorBottomRightBevel casts the two clipped lower/right shadow strips around KB51.
        fill_rect(out,102,99,112,2,0x40);
        fill_rect(out,212,27,2,74,0x40);
        const auto& title=texts_.strings.at((::bmh::layout::DeskRecords_FillRect_TextPointer4-::bmh::layout::TextPointerTable)/4);
        const auto heading=original_to_utf8(title);
        const int heading_x=SpriteFont::centered_title_x(sprite_font_.text_width(heading));
        sprite_font_.draw(out,heading_x,13,heading,5);
        sprite_font_.draw(out,5,235,exe_.cstring(::bmh::layout::DeskRecords_FillRect_ExecutableField2),5);
        for(size_t i=0;i<state_.highscore.rows.size();++i) {
            const auto& row=state_.highscore.rows[i];if(row.score==0)continue;
            const int y=120+int(i)*10;
            text(out,33,y,format_number(GameState{},int(i)+1,0x81)+exe_.cstring(::bmh::layout::DeskRecords_FillRect_TextPointer)+row.manager_name,0x5f,true);
            text(out,125,y,row.club_name,0x5f,true);
            std::string score=format_number(GameState{},row.score,0x85);
            score+=exe_.cstring(::bmh::layout::DeskRecords_FillRect_TextPointer2);
            for(size_t n=0;n<row.titles.size();++n) {
                score+=format_number(GameState{},row.titles[n],1)+exe_.cstring(::bmh::layout::DeskRecords_FillRect_ExecutableField2);
                score+=exe_.cstring(n<2?::bmh::layout::DeskRecords_FillRect_ExecutableField3: ::bmh::layout::DeskRecords_FillRect_ExecutableField);
            }
            text(out,230,y,score,0x5f,true);
        }
    }
    return out;
}
bool DeskRecordsScreen::pointer_down(int x,int y,bool right) {
    if(closed_)return false;
    if(!alternate_){closed_=true;return true;}
    if(alternate_->phase()==AlternateScorePhase::AwaitAck) {
        if(alternate_->acknowledge()) { closed_=true; return true; }
        return false;
    }
    if(alternate_->phase()==AlternateScorePhase::Cancelled) { closed_=true; return true; }
    if(alternate_->phase()!=AlternateScorePhase::Calculator)return false;
    if(right) { alternate_->cancel(); closed_=true; return true; }
    const bool changed=alternate_->mouse_down(x,y);
    if(alternate_->phase()==AlternateScorePhase::Cancelled)closed_=true;
    return changed;
}
bool DeskRecordsScreen::pointer_up(int,int,bool) {
    if(closed_||!alternate_)return false;
    return alternate_->mouse_up();
}
bool DeskRecordsScreen::text_input(std::string_view input) {
    if(closed_||!alternate_)return false;
    return alternate_->text(input);
}
bool DeskRecordsScreen::key_input(std::string_view key) {
    if(closed_)return false;
    if(!alternate_)return false; // SDLFlushEvents2 only returns on mouse down or quit.
    return alternate_->key(key);
}
} // namespace bmh::ui
