// SPDX-License-Identifier: GPL-3.0-only
#include "game/state.hpp"
#include "formats/executable_layout.hpp"
#include "ui/lineup.hpp"

#include <algorithm>
#include <sstream>

#include "formats/savestate.hpp"
#include "game/state.hpp"
#include "ui/manager.hpp"

namespace bmh::ui {
namespace {
Bytes block(const SaveState& s, uint32_t address) {
    const auto& b = s.block(address);
    return Bytes(s.stream().begin() + b.offset, s.stream().begin() + b.offset + b.size);
}
std::vector<uint16_t> words(const Bytes& b) {
    if (b.size() % 2) throw FormatError("lineup: odd word table");
    std::vector<uint16_t> out;
    for (size_t at = 0; at < b.size(); at += 2) out.push_back(le16(b,at));
    return out;
}
std::string name(const Bytes& b, size_t offset) {
    if (offset >= b.size()) throw FormatError("lineup: name outside string pool");
    const auto end = std::find(b.begin() + offset, b.end(), 0);
    if (end == b.end()) throw FormatError("lineup: unterminated name");
    return std::string(b.begin() + offset, end);
}
LineupState project(const StaticMemory& memory, const LeImage& exe, const std::vector<int>& teams,
                    const std::vector<uint16_t>& squad, const std::vector<Player>& players,
                    const Bytes& tactics, const Bytes& options, const std::vector<uint16_t>& cards,
                    const Bytes& bonus, const std::vector<uint16_t>& player_offsets, const Bytes& player_names,
                    const std::vector<uint16_t>& club_offsets, const Bytes& club_names, uint64_t revision) {
    LineupState out;
    out.manager_slot = memory.u8(::bmh::layout::ActiveManager); out.revision = revision;
    if (out.manager_slot < 0 || out.manager_slot > 3 || size_t(out.manager_slot) >= teams.size())
        throw FormatError("lineup: unsupported active manager");
    out.runtime_club = teams.at(out.manager_slot);
    const int split = exe.u16(::bmh::layout::TeamsWithSquads), limit = exe.u16(::bmh::layout::CareerSession_DispatchLineup_RuntimeField);
    const bool national = out.runtime_club == kNoTeam && out.manager_slot == 0 && teams.size() == 1 &&
                          ((memory.u32(::bmh::layout::CompetitionSelection) & 2) != 0 ||
                           (memory.has(::bmh::layout::CoachActive) && memory.u8(::bmh::layout::CoachActive) != 0));
    if (out.runtime_club < 0 || (out.runtime_club >= limit && !national))
        throw FormatError("lineup: only domestic league squads are supported");
    const size_t count = national || out.runtime_club < split ? 24 : 16;
    const size_t first = national ? runtime_dimensions.national_slots_first : out.runtime_club < split ? size_t(out.runtime_club) * 24 : size_t(out.runtime_club) * 16 + split * 8;
    const size_t board_base = size_t(out.manager_slot) * 0x840;
    if (first + count > squad.size() || board_base + count * 0x58 > tactics.size() || size_t(out.manager_slot) * 5 >= options.size())
        throw FormatError("lineup: truncated squad/tactics/options");
    if (size_t(out.runtime_club) >= club_offsets.size()) throw FormatError("lineup: club outside name table");
    out.club_name = name(club_names,club_offsets.at(out.runtime_club));
    out.saved_display_flags = memory.u16(::bmh::layout::CareerSession_AnyOf_ExecutableField + uint32_t(out.manager_slot) * 2);
    // A saved pitch view is retained as metadata. Only the two complete team
    // lists are currently rendered; this is exposed explicitly in state_json().
    out.view = (out.saved_display_flags & 3) == 2 ? LineupView::data_b : LineupView::data_a;
    out.gradient_text = memory.u8(::bmh::layout::TextGradientDisabled) == 0 && exe.u8(::bmh::layout::TextGradientMode) == 2;
    out.decorated_background = memory.u8(::bmh::layout::PlainBackground) == 0;
    out.automatic_formation = options.at(size_t(out.manager_slot) * 5);
    for (size_t row = 0; row < count; ++row) {
        LineupPlayer p; p.row = int(row); p.slot = squad.at(first + row);
        std::copy_n(tactics.begin() + board_base + row * 0x58,0x58,p.board.begin());
        const int id = p.slot & 0x0fff;
        if (id != 0x0fff) {
            if (size_t(id) >= players.size() || size_t(id) >= player_offsets.size() || size_t(id) >= cards.size() || size_t(id) >= bonus.size())
                throw FormatError("lineup: player outside authoritative tables");
            p.player_id = id; p.attributes = players.at(id);
            p.name = name(player_names,player_offsets.at(id));
            p.cards = cards.at(id); p.bonus = bonus.at(id);
        }
        out.players.push_back(std::move(p));
    }
    return out;
}
void rect(Image& out, int x, int y, int w, int h, uint8_t color) {
    for (int yy = std::max(0,y); yy < std::min(out.height,y+h); ++yy)
        for (int xx = std::max(0,x); xx < std::min(out.width,x+w); ++xx) out.at(xx,yy) = color;
}
void copy(Image& out, const Image& src, int sx, int sy, int x, int y, int w, int h, int transparent = -1) {
    if (sx < 0 || sy < 0 || sx+w > src.width || sy+h > src.height) throw FormatError("lineup: original atlas bounds " + std::to_string(sx) + "," + std::to_string(sy) + ":" + std::to_string(w) + "x" + std::to_string(h) + " in " + std::to_string(src.width) + "x" + std::to_string(src.height));
    for (int yy=0; yy<h; ++yy) for (int xx=0; xx<w; ++xx) {
        const auto c=src.pixels.at(size_t(sy+yy)*src.width+sx+xx);
        if (c != transparent && x+xx>=0 && x+xx<out.width && y+yy>=0 && y+yy<out.height) out.at(x+xx,y+yy)=c;
    }
}
std::string number(int value, int width = 2) {
    auto s=std::to_string(value);
    if (int(s.size())<width) s.insert(0,size_t(width-int(s.size())),'%');
    return s;
}
const char* request_name(LineupRequestKind kind) {
    switch(kind) {
    case LineupRequestKind::none:return "none";
    case LineupRequestKind::prepare_lineup:return "prepare_lineup";
    case LineupRequestKind::swap_players:return "swap_players";
    case LineupRequestKind::open_player_info:return "open_player_info";
    case LineupRequestKind::open_pitch:return "open_pitch";
    }
    return "none";
}
} // namespace

LineupState lineup_from_save(const SaveState& s,const LeImage& exe,uint64_t revision) {
    StaticMemory memory(s); auto managers=block(s,::bmh::layout::ManagerRecords);
    std::vector<int> teams;
    for(int i=0;i<memory.u8(::bmh::layout::ManagerCount);++i) teams.push_back(le16(managers,size_t(i)*0xb0c+0x290));
    auto w1=block(s,::bmh::layout::PlayerConditionTechniqueForm),w2=block(s,::bmh::layout::PlayerSkillWords),w3=block(s,::bmh::layout::PlayerIdentityWords);
    if(w1.size()!=w2.size()||w1.size()!=w3.size()||w1.size()%4) throw FormatError("lineup: inconsistent player tables");
    std::vector<Player> players;
    for(size_t at=0;at<w1.size();at+=4) players.push_back({{},le32(w1,at),le32(w2,at),le32(w3,at)});
    auto out=project(memory,exe,teams,words(block(s,::bmh::layout::SquadSlots)),players,block(s,::bmh::layout::TacticsBoards),block(s,::bmh::layout::ManagerAutomation),
        words(block(s,::bmh::layout::PlayerCards)),block(s,::bmh::layout::PlayerBonuses),words(block(s,::bmh::layout::PlayerNameOffsets)),s.pool(1),words(block(s,::bmh::layout::ClubIdentities)),s.pool(0),revision);
    out.source="original_save_read_only"; return out;
}
LineupState lineup_from_game(const GameState& g,const LeImage& exe,uint64_t revision) {
    auto out=project(g.memory,exe,g.manager_teams,g.squad_slots,g.players,g.tactics,g.manager_options,
        g.player_cards,g.player_bonus,g.player_name_offsets,g.player_names,g.club_ids,g.club_names,revision);
    out.source="engine_projection"; return out;
}

std::string lineup_request_json(const LineupRequest& r) {
    std::ostringstream s;
    s<<"{\"kind\":"<<json_quote(request_name(r.kind))<<",\"manager_slot\":"<<r.manager_slot
     <<",\"runtime_club\":"<<r.runtime_club<<",\"revision\":"<<r.revision
     <<",\"first_row\":"<<r.first_row<<",\"second_row\":"<<r.second_row
     <<",\"first_player\":"<<r.first_player<<",\"second_player\":"<<r.second_player
     <<",\"first_slot\":"<<r.first_slot<<",\"second_slot\":"<<r.second_slot<<"}";
    return s.str();
}
LineupScreen::LineupScreen(const std::filesystem::path& data,LineupState state)
    :state_(std::move(state)),exe_(load_le(asset_path(data,"BMH.exe"))),
     font_(load_font(asset_path(data/"F","BMH3.FNT"))),texts_(load_textd(asset_path(data,"TEXTD.BLK"))),
     background_(load_vga(asset_path(data/"KB","129.VGA"))),widgets_(load_vga(asset_path(data/"KB","0.VGA"))),
     atlas_(load_vga(asset_path(data/"KB","2.VGA"))),flags_(load_vga(asset_path(data/"KB","24.VGA"))),
     letters_(load_vga(asset_path(data/"KB","54.VGA"))) {
    // Original planar bitmap allocation rounds the 314-pixel atlas to 320.
    // Fast 16-pixel sprites at x=304 therefore include six transparent padding pixels.
    if (atlas_.width == 314) { Image padded(320,atlas_.height,0); padded.blit(atlas_,0,0); atlas_=std::move(padded); }
    if(state_.manager_slot<0||state_.manager_slot>3||state_.runtime_club<0||
       (state_.players.size()!=24&&state_.players.size()!=16)) throw FormatError("lineup: invalid screen state");
    for(size_t i=0;i<state_.players.size();++i)
        if(state_.players[i].row!=int(i)||state_.players[i].player_id!=((state_.players[i].slot&0xfff)==0xfff?-1:state_.players[i].slot&0xfff))
            throw FormatError("lineup: inconsistent player/slot identity");
}
void LineupScreen::text(Image& out,int x,int baseline,std::string_view value,uint8_t color,int right) const {
    const int width=font_.text_width(value);
    if(right>=0)x+=(right-x)/2-width/2;
    if(x<0||x+width>out.width||baseline<5||baseline>=out.height)return;
    Image mask(std::max(1,width),6);font_.draw(mask,0,0,value,1);
    int gradient=0;while(gradient<32&&exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2+gradient)!=color)++gradient;
    for(int yy=0;yy<6;++yy)for(int xx=0;xx<width;++xx)if(mask.at(xx,yy))
        out.at(x+xx,baseline-5+yy)=state_.gradient_text?exe_.u8(::bmh::layout::Advertising_Text_RuntimeField+gradient*6+yy):color;
}
void LineupScreen::sprite_text(Image& out,int x,int baseline,std::string_view value,int font) const {
    for(unsigned char c:value) {
        if(c<=31||c>=128)continue;
        const int glyph=exe_.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + c);
        if(!glyph){x+=2;continue;}
        const int sx=exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField+glyph*2),width=exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2+glyph*2)-sx;
        copy(out,letters_,sx,(font-4)*7,x,baseline-6,width,7,0);x+=width;
    }
}
void LineupScreen::button(Image& out,int x,int y,std::string_view value) const {
    constexpr int width=55,height=10;
    rect(out,x+2,y+height,width,2,0x40);rect(out,x+width,y+2,2,height,0x40);
    copy(out,widgets_,24,74,x,y,31,10);
    int at=31,remaining=width-31;
    while(remaining>20){const int n=std::min(8,remaining-20);copy(out,widgets_,24,84,x+at,y,n,10);at+=n;remaining-=n;}
    copy(out,widgets_,45-remaining,94,x+at,y,remaining,10);
    if(exe_.u8(::bmh::layout::Advertising_Button_RuntimeField)==3)text(out,x,y+7,value,0,x+width);
    else {
        int length=0;for(unsigned char c:value){const int g=exe_.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + c);length+=g?exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2+g*2)-exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField+g*2):2;}
        sprite_text(out,x+width/2-length/2,y+7,value,exe_.u8(::bmh::layout::Advertising_Button_RuntimeField));
    }
}
std::vector<std::pair<int,int>> LineupScreen::rows() const {
    std::vector<std::pair<int,int>> out;int baseline=36;
    for(size_t i=0;i<state_.players.size();++i){
        const auto& p=state_.players[i];
        if(i&&((p.slot&0xc000)!=(state_.players[i-1].slot&0xc000)))baseline+=2;
        if(p.player_id<0)continue;
        out.emplace_back(int(i),baseline);baseline+=7;
    }
    return out;
}
Image LineupScreen::render() const {
    auto out=state_.decorated_background?background_:Image(320,240);
    if(!state_.decorated_background){rect(out,0,4,320,13,0x44);rect(out,1,5,318,11,0x39);rect(out,0,228,320,10,0x44);rect(out,1,229,318,8,0x39);}
    const auto title=texts_[(::bmh::layout::Lineup_Render_TextPointer3-::bmh::layout::TextPointerTable)/4];
    int title_width=0;for(unsigned char c:title){const int g=exe_.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + c);title_width+=g?exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2+g*2)-exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField+g*2):2;}
    sprite_text(out,172+(282-172)/2-title_width/2,13,title);
    sprite_text(out,5,235,state_.club_name);
    if(state_.automatic_formation || state_.in_match)sprite_text(out,268,13,texts_[(::bmh::layout::Lineup_Render_TextPointer4-::bmh::layout::TextPointerTable)/4]);
    // The list's offscreen bitmap starts at y=0 and is copied to the screen at y=20.
    // Header/rows below already include that final offset.
    const auto header=texts_[((state_.view==LineupView::data_a?::bmh::layout::ClubInfo_Render_TextPointer3: ::bmh::layout::ClubInfo_Render_TextPointer4)-::bmh::layout::TextPointerTable)/4];
    text(out,0,26,header,0x51);
    for(const auto& [row,baseline]:rows()) {
        const auto& p=state_.players.at(size_t(row));const auto& a=p.attributes;
        if(p.slot&0xc000) {
            const int jersey=(p.cards&31)+1;
            const auto color=uint8_t(p.slot&0x1000?0xf3:p.slot&0x8000?0x6c:0x68);
            text(out,4,baseline,std::string(1,char('0'+jersey%10)),color);
            if(jersey>=10)text(out,0,baseline,exe_.cstring(::bmh::layout::ClubInfo_Render_TextPointer),color);
        }
        const uint8_t name_color=selected_==row?0x27:hovered_==row?0x3e:uint8_t(row%2?0x67:0x6b);
        text(out,13,baseline,p.name,name_color);
        if(state_.view==LineupView::data_a) {
            text(out,66,baseline,texts_[(::bmh::layout::ClubInfo_Render_TextPointer2-::bmh::layout::TextPointerTable)/4+a.position()],0x6c,82);
            for(int field=0;field<2;++field){int value=(a.w35c>>(field==0?24:21))&7;if(value>4)--value;
                copy(out,atlas_,208+value*16,field==0?17:22,field==0?88:103,baseline-5,16,5,0);}
            std::string attrs=number(a.condition())+exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField)+number(a.technique())+exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField)+
                number(a.form())+exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField2)+number(a.strength())+exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField3)+exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField4);
            text(out,117,baseline,attrs,0x6c);
            copy(out,atlas_,304,int(p.board[0xe]>>6)*3,179,baseline-4,16,3,0);
            const int injury=(a.w364>>16)&31,marker=a.w35c>>27;
            if(marker==0&&injury==0){const int status=p.slot&0x8000?0:p.slot&0x4000?1:2;copy(out,atlas_,272,status*5,191,baseline-5,32,5,0);}
            else {const int status=marker==0?5:marker==31?8:injury==0?4:3;copy(out,atlas_,197,(status-3)*6+1,196,baseline-5,10,5);}
            const int nationality=a.nationality()+((p.bonus&0x80)>>2);
            copy(out,flags_,(nationality%12)*9,(nationality/12)*6,217,baseline-6,9,6);
            text(out,232,baseline,number(p.board[0xf]),p.board[0xf]<61?0x66:p.board[0xf]<71?0x27:0xfb);
            text(out,247,baseline,number(a.w364>>26)+exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField4)+number((a.w364>>2)&63)+exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField4)+
                number((a.w368>>18)&63)+exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField4)+number((a.w368>>14)&15)+exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField4)+number((a.w368>>24)&7)+exe_.cstring(::bmh::layout::ClubInfo_Raw_ExecutableField),0x6c);
        } else {
            text(out,65,baseline,number(a.age())+texts_[(::bmh::layout::ClubInfo_Render_TextPointer5-::bmh::layout::TextPointerTable)/4],0x6c);
            for(int skill=0;skill<7;++skill)copy(out,atlas_,256+a.skill(Player::Skill(skill))*16,32,87+skill*10,baseline-5,16,5,0);
            text(out,165,baseline,number(p.bonus&127,3)+exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField5)+number(p.board[7],3)+exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField5)+
                number(p.board[9],3)+exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField5)+number(le16(Bytes(p.board.begin(),p.board.end()),0x48),3)+exe_.cstring(::bmh::layout::ClubInfo_Raw_ExecutableField2)+
                number(le16(Bytes(p.board.begin(),p.board.end()),0x4a),3)+exe_.cstring(::bmh::layout::ClubInfo_Raw_ExecutableField2)+number(le16(Bytes(p.board.begin(),p.board.end()),0x4c),3)+exe_.cstring(::bmh::layout::ClubInfo_Raw_ExecutableField),0x6c);
        }
    }
    button(out,1,215,texts_[(::bmh::layout::Lineup_Render_TextPointer-::bmh::layout::TextPointerTable)/4+(state_.view==LineupView::data_a?1:0)]);
    button(out,58,215,texts_[(::bmh::layout::Lineup_Render_TextPointer2-::bmh::layout::TextPointerTable)/4]);
    return out;
}
LineupRequest LineupScreen::request(LineupRequestKind kind,int first,int second) {
    LineupRequest r;r.kind=kind;r.manager_slot=state_.manager_slot;r.runtime_club=state_.runtime_club;r.revision=state_.revision;
    r.first_row=first;r.second_row=second;
    if(first>=0){const auto& p=state_.players.at(size_t(first));r.first_player=p.player_id;r.first_slot=p.slot;}
    if(second>=0){const auto& p=state_.players.at(size_t(second));r.second_player=p.player_id;r.second_slot=p.slot;}
    pending_=r;rejection_.clear();return r;
}
LineupRequest LineupScreen::initialize() {
    if(pending_||close_||state_.entry_prepared)return {};
    return request(LineupRequestKind::prepare_lineup);
}
LineupRequest LineupScreen::click(int x,int y,bool right) {
    if(pending_||close_)return {};
    if(right){if(selected_)selected_.reset();else close_=true;return {};}
    if(y>=215&&y<=224){
        if(x>=1&&x<=56){state_.view=state_.view==LineupView::data_a?LineupView::data_b:LineupView::data_a;return {};}
        if(x>=58&&x<=113)return request(LineupRequestKind::open_pitch);
    }
    if(x<0||x>=320)return {};
    for(const auto& [row,baseline]:rows())if(y>=baseline-7&&y<baseline+2){
        if(x>=63)return request(LineupRequestKind::open_player_info,row);
        if(!selected_){selected_=row;return {};}
        if(*selected_==row){selected_.reset();return {};}
        return request(LineupRequestKind::swap_players,*selected_,row);
    }
    return {};
}
bool LineupScreen::move(int x,int y) {
    if(pending_||close_)return false;
    std::optional<int> next;
    if(x>=0&&x<63)for(const auto& [row,baseline]:rows())if(y>=baseline-7&&y<baseline+2){next=row;break;}
    if(next==hovered_)return false;
    hovered_=next;return true;
}
LineupRequest LineupScreen::key(std::string_view key) {
    // The desktop sheet belongs to PitchScreen and ignores Escape. The separate
    // live-intervention caller retains its existing keyboard return.
    if(state_.in_match&&(key=="ESC"||key=="ESCAPE"))return click(0,0,true);
    return {};
}
void LineupScreen::acknowledge(LineupState updated) {
    if (pending_ && (pending_->kind == LineupRequestKind::open_pitch || pending_->kind == LineupRequestKind::open_player_info))
        throw FormatError("lineup: local navigation requires context resolution, not an engine acknowledgement");
    if(!pending_||updated.revision<=state_.revision||updated.manager_slot!=state_.manager_slot||updated.runtime_club!=state_.runtime_club)
        throw FormatError("lineup: expected newer state for the same manager/club");
    if(updated.players.size()!=state_.players.size())throw FormatError("lineup: squad size changed");
    if(pending_->kind==LineupRequestKind::prepare_lineup&&!updated.entry_prepared)
        throw FormatError("lineup: entry acknowledgement must confirm preparation");
    for(size_t row=0;row<updated.players.size();++row)
        if(updated.players[row].row!=int(row)||updated.players[row].player_id!=((updated.players[row].slot&0xfff)==0xfff?-1:updated.players[row].slot&0xfff))
            throw FormatError("lineup: inconsistent acknowledged player identity");
    updated.view=state_.view;state_=std::move(updated);pending_.reset();selected_.reset();hovered_.reset();rejection_.clear();
}
void LineupScreen::reject_action(std::string reason) {
    if(!pending_||reason.empty())throw FormatError("lineup: rejection requires pending action and reason");
    pending_.reset();rejection_=std::move(reason);selected_.reset();
}
void LineupScreen::resolve_local_navigation(LineupRequestKind kind) {
    if (!pending_ || pending_->kind != kind ||
        (kind != LineupRequestKind::open_pitch && kind != LineupRequestKind::open_player_info))
        throw FormatError("lineup: expected a local navigation request");
    pending_.reset(); selected_.reset(); hovered_.reset(); rejection_.clear();
}
std::string LineupScreen::state_json() const {
    std::ostringstream s;s<<"{\"screen\":\"lineup\",\"view\":"<<json_quote(state_.view==LineupView::data_a?"data_a":"data_b")
        <<",\"manager_slot\":"<<state_.manager_slot<<",\"runtime_club\":"<<state_.runtime_club<<",\"revision\":"<<state_.revision
        <<",\"source\":"<<json_quote(state_.source)<<",\"saved_display_flags\":"<<state_.saved_display_flags
        <<",\"saved_pitch_view\":"<<((state_.saved_display_flags&8)?"true":"false")
        <<",\"in_match\":"<<(state_.in_match?"true":"false")
        <<",\"saved_pitch_view_supported\":false,\"entry_prepared\":"<<(state_.entry_prepared?"true":"false")<<",\"close_requested\":"<<(close_?"true":"false")
        <<",\"selected_row\":"<<(selected_?std::to_string(*selected_):"null")<<",\"hovered_row\":"<<(hovered_?std::to_string(*hovered_):"null")<<",\"pending_action\":"
        <<(pending_?lineup_request_json(*pending_):"null")<<",\"last_rejection\":"<<(rejection_.empty()?"null":json_quote(rejection_))<<",\"players\":[";
    for(size_t i=0;i<state_.players.size();++i){if(i)s<<',';const auto& p=state_.players[i];s<<"{\"row\":"<<p.row<<",\"player_id\":"<<p.player_id
        <<",\"slot\":"<<p.slot<<",\"name\":"<<json_quote(original_to_utf8(p.name));
        if(p.player_id>=0)s<<",\"starting\":"<<((p.slot&0x8000)?"true":"false")<<",\"bench\":"<<((p.slot&0x4000)?"true":"false")
            <<",\"position_type\":"<<p.attributes.position()<<",\"condition\":"<<p.attributes.condition()<<",\"technique\":"<<p.attributes.technique()
            <<",\"form\":"<<p.attributes.form()<<",\"strength\":"<<p.attributes.strength()<<",\"age\":"<<p.attributes.age()<<",\"exhaustion\":"<<int(p.board[0xf]);
        s<<'}';}
    s<<"]}";return s.str();
}
} // namespace bmh::ui
