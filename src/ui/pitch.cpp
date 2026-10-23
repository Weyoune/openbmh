// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/world_layout.hpp"
#include "ui/pitch.hpp"

#include <algorithm>
#include <sstream>

#include "formats/savestate.hpp"
#include "game/messages.hpp"
#include "game/state.hpp"
#include "ui/manager.hpp"

namespace bmh::ui {
namespace {
Bytes block(const SaveState& save, uint32_t address) {
    const auto& b=save.block(address);
    return Bytes(save.stream().begin()+b.offset, save.stream().begin()+b.offset+b.size);
}
void copy(Image& out,const Image& src,int sx,int sy,int x,int y,int w,int h,int transparent=-1) {
    if(sx<0||sy<0||sx+w>src.width||sy+h>src.height)throw FormatError("pitch: original atlas bounds");
    for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx){const auto c=src.pixels.at(size_t(sy+yy)*src.width+sx+xx);
        if(c!=transparent&&x+xx>=0&&y+yy>=0&&x+xx<out.width&&y+yy<out.height)out.at(x+xx,y+yy)=c;}
}
void source_mask_copy(Image& out,const Image& src,int sx,int sy,int x,int y,int w,int h,
                      int transparent=-1) {
    for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx){
        const int source_x=sx+xx,source_y=sy+yy,target_x=x+xx,target_y=y+yy;
        if(source_x<0||source_y<0||source_x>=src.width||source_y>=src.height||
           target_x<0||target_y<0||target_x>=out.width||target_y>=out.height)continue;
        const auto pixel=src.pixels.at(size_t(source_y)*src.width+source_x);
        if(transparent>=0&&pixel==transparent)continue;
        // UsesForTheseSprites/DrawOpaquePlaneMask program VGA bit mask 0x80: only palette plane 7
        // is changed, while the low seven destination bits remain untouched.
        auto& destination=out.at(target_x,target_y);
        destination=uint8_t((destination&0x7f)|(pixel&0x80));
    }
}
void rect(Image& out,int x,int y,int w,int h,uint8_t color){
    for(int yy=std::max(0,y);yy<std::min(out.height,y+h);++yy)
        for(int xx=std::max(0,x);xx<std::min(out.width,x+w);++xx)out.at(xx,yy)=color;
}
const char* kind_name(PitchRequestKind k){switch(k){case PitchRequestKind::none:return "none";
    case PitchRequestKind::move_player:return "move_player";case PitchRequestKind::swap_players:return "swap_players";
    case PitchRequestKind::open_player_info:return "open_player_info";
    case PitchRequestKind::set_automatic_weight:return "set_automatic_weight";
    case PitchRequestKind::apply_formation:return "apply_formation";
    case PitchRequestKind::save_formation:return "save_formation";
    case PitchRequestKind::open_set_piece_editor:return "open_set_piece_editor";
    case PitchRequestKind::close_set_piece_editor:return "close_set_piece_editor";
    case PitchRequestKind::set_piece_taker:return "set_piece_taker";
    case PitchRequestKind::auto_set_piece_takers:return "auto_set_piece_takers";
    case PitchRequestKind::set_player_order:return "set_player_order";
    case PitchRequestKind::select_pitch_page:return "select_pitch_page";
    case PitchRequestKind::set_player_marking:return "set_player_marking";}return "none";}
PitchExtras extras(const StaticMemory& memory,const Bytes& managers,const Bytes& formations,
                  const Bytes& badges,const std::vector<int>& teams,const LeImage& exe,uint64_t revision) {
    PitchExtras e;e.manager_slot=memory.u8(::bmh::layout::ActiveManager);e.revision=revision;
    const int count=memory.u8(::bmh::layout::ManagerCount);
    if(count<1||count>4||e.manager_slot>=count||teams.size()!=size_t(count))throw FormatError("pitch: invalid active manager");
    e.runtime_club=teams.at(e.manager_slot);
    if(e.runtime_club<0||e.runtime_club>=world_layout().league_teams)throw FormatError("pitch: unsupported club");
    const size_t m=size_t(e.manager_slot)*0xb0c,f=size_t(e.manager_slot)*0x160;
    if(managers.size()<m+0xb0c||formations.size()<f+0x160||badges.size()<size_t(e.runtime_club+1)*2)
        throw FormatError("pitch: truncated manager/formations/badges");
    e.formation_mask=managers.at(m+0x218);e.automatic_weights=managers.at(m+0x288);
    auto manager_u16=[&](size_t at){return uint16_t(managers.at(m+at)|uint16_t(managers.at(m+at+1))<<8);};
    e.set_piece_takers={manager_u16(0xafc),manager_u16(0xafe),manager_u16(0xb08)};
    e.set_piece_signature=manager_u16(0xb0a);
    if(memory.has(uint32_t(::bmh::layout::CareerSession_AnyOf_ExecutableField+e.manager_slot*2)))
        e.native_display_word=memory.u16(uint32_t(::bmh::layout::CareerSession_AnyOf_ExecutableField+e.manager_slot*2));
    e.badge=le16(badges,size_t(e.runtime_club)*2)&0x1ff;
    e.mode=memory.has(::bmh::layout::GameMode)?memory.u8(::bmh::layout::GameMode):exe.u8(::bmh::layout::GameMode);
    std::copy_n(formations.begin()+f,8*22,e.formations.begin());
    return e;
}
}

PitchExtras pitch_extras_from_save(const SaveState& save,const LeImage& exe,uint64_t revision){
    StaticMemory memory(save);const auto managers=block(save,::bmh::layout::ManagerRecords);std::vector<int> teams;
    const int count=memory.u8(::bmh::layout::ManagerCount);
    if(count<1||count>4||managers.size()<size_t(count)*0xb0c)throw FormatError("pitch: unsupported save managers");
    for(int i=0;i<count;++i)teams.push_back(le16(managers,size_t(i)*0xb0c+0x290));
    auto e=extras(memory,managers,block(save,::bmh::layout::Formations),block(save,::bmh::layout::ClubBadgeSelectors),teams,exe,revision);
    e.source="original_save_read_only";return e;
}
PitchExtras pitch_extras_from_game(const GameState& game,const LeImage& exe,uint64_t revision,int opponent_team){
    auto e=extras(game.memory,game.manager_records,game.formations,game.club_heap_78,game.manager_teams,exe,revision);
    e.source="engine_projection";e.indoor=game.indoor;
    // The independent indoor template set uses the same per-manager stride.
    if(game.indoor){const size_t at=size_t(e.manager_slot)*0x160+0xb0;
        std::copy_n(game.formations.begin()+at,8*22,e.formations.begin());
        e.formation_mask=game.manager_records.at(size_t(e.manager_slot)*0xb0c+0x219);}
    if(opponent_team>=0){
        const auto range=squad_range(opponent_team);
        if(!range.count||range.first+range.count>game.squad_slots.size())
            throw FormatError("pitch: unsupported live opponent squad");
        e.opponent_team=opponent_team;
        e.opponent_name=game.club_name(opponent_team);
        const size_t own_base=size_t(e.manager_slot)*0x840;
        for(size_t row=1;row<std::min<size_t>(range.count,11);++row){
            const uint16_t slot=game.squad_slots.at(range.first+row);
            const int id=slot&0x0fff;
            if(id==0x0fff||!(slot&0x8000)||size_t(id)>=game.players.size()||
               size_t(id)>=game.player_name_offsets.size())continue;
            const auto& player=game.players.at(size_t(id));
            if(player.position()==0)continue;
            const size_t name_at=game.player_name_offsets.at(size_t(id));
            if(name_at>=game.player_names.size())throw FormatError("pitch: opponent name outside pool");
            const auto end=std::find(game.player_names.begin()+name_at,game.player_names.end(),uint8_t(0));
            if(end==game.player_names.end())throw FormatError("pitch: unterminated opponent name");
            PitchOpponent opponent;opponent.row=int(row);opponent.player_id=id;opponent.slot=slot;
            opponent.name=std::string(game.player_names.begin()+name_at,end);
            opponent.position=player.position();
            opponent.detail_text=message_text(game,::bmh::layout::Pitch_FormatError_TextPointer)+exe.cstring(::bmh::layout::CupDraw_FormatError_TextPointer)+
                format_number(game,player.strength(true),2)+exe.cstring(::bmh::layout::CupDraw_FormatError_ExecutableField)+message_text(game,::bmh::layout::Pitch_FormatError_TextPointer2)+
                format_number(game,int((player.w364>>2)&0x3f),2)+message_text(game,::bmh::layout::Pitch_FormatError_TextPointer3)+
                format_number(game,int((player.w368>>18)&0x3f),2)+exe.cstring(::bmh::layout::ClubInfo_Raw_ExecutableField);
            for(int own=0;own<11;++own){
                const size_t assignment=own_base+size_t(own)*0x58+0x44;
                if(assignment+1>=game.tactics.size())throw FormatError("pitch: truncated own marking rows");
                const uint16_t marked=uint16_t(game.tactics[assignment]|uint16_t(game.tactics[assignment+1])<<8);
                if(marked==id){const int own_id=game.squad_slots.at(squad_range(e.runtime_club).first+size_t(own))&0x0fff;
                    if(own_id!=0x0fff)opponent.current_marker_player_id=own_id;break;}
            }
            e.opponent_players.push_back(std::move(opponent));
        }
    }
    return e;
}
std::string pitch_request_json(const PitchRequest& r){std::ostringstream out;
    out<<"{\"kind\":"<<json_quote(kind_name(r.kind))<<",\"manager_slot\":"<<r.manager_slot
       <<",\"runtime_club\":"<<r.runtime_club<<",\"revision\":"<<r.revision
       <<",\"indoor\":"<<(r.indoor?"true":"false")<<",\"expected_mode\":"<<int(r.expected_mode)
       <<",\"row\":"<<r.row<<",\"player_id\":"<<r.player_id<<",\"slot\":"<<r.slot
       <<",\"old_depth\":"<<r.old_depth<<",\"old_lane\":"<<r.old_lane
       <<",\"new_depth\":"<<r.new_depth<<",\"new_lane\":"<<r.new_lane
       <<",\"second_row\":"<<r.second_row<<",\"second_player\":"<<r.second_player
       <<",\"second_slot\":"<<r.second_slot;
    if(r.kind==PitchRequestKind::set_automatic_weight||r.kind==PitchRequestKind::apply_formation||r.kind==PitchRequestKind::save_formation)
        out<<",\"formation\":"<<r.formation<<",\"weight_index\":"<<r.weight_index
           <<",\"old_weights\":"<<int(r.old_weights)<<",\"new_weights\":"<<int(r.new_weights);
    if(r.kind==PitchRequestKind::save_formation){out<<",\"formation_positions\":[";
        for(size_t i=0;i<r.formation_positions.size();++i){if(i)out<<',';out<<int(r.formation_positions[i]);}out<<']';}
    if(r.kind==PitchRequestKind::open_set_piece_editor||r.kind==PitchRequestKind::close_set_piece_editor||
       r.kind==PitchRequestKind::set_piece_taker||r.kind==PitchRequestKind::auto_set_piece_takers){
        out<<",\"expected_set_piece_values\":["<<r.expected_set_piece_values[0]<<','<<r.expected_set_piece_values[1]<<','<<r.expected_set_piece_values[2]
           <<"],\"expected_display_word\":"<<r.expected_display_word<<",\"new_display_word\":"<<r.new_display_word
           <<",\"set_piece_selector\":"<<r.set_piece_selector<<",\"expected_set_piece_value\":"<<r.expected_set_piece_value
           <<",\"expected_set_piece_signature\":"<<r.expected_set_piece_signature;
    }
    if(r.kind==PitchRequestKind::set_player_order)
        out<<",\"order_aspect\":"<<r.order_aspect<<",\"expected_order_value\":"<<r.expected_order_value
           <<",\"new_order_value\":"<<r.new_order_value
           <<",\"expected_display_word\":"<<r.expected_display_word
           <<",\"expected_set_piece_values\":["<<r.expected_set_piece_values[0]<<','<<r.expected_set_piece_values[1]<<','<<r.expected_set_piece_values[2]
           <<"],\"expected_set_piece_signature\":"<<r.expected_set_piece_signature;
    if(r.kind==PitchRequestKind::select_pitch_page)out<<",\"page_control\":"<<r.page_control
        <<",\"expected_display_word\":"<<r.expected_display_word<<",\"new_display_word\":"<<r.new_display_word
        <<",\"expected_set_piece_values\":["<<r.expected_set_piece_values[0]<<','<<r.expected_set_piece_values[1]<<','<<r.expected_set_piece_values[2]
        <<"],\"expected_set_piece_signature\":"<<r.expected_set_piece_signature;
    if(r.kind==PitchRequestKind::set_player_marking||
       (r.kind==PitchRequestKind::select_pitch_page&&r.page_control==4))
        out<<",\"opponent_team\":"<<r.opponent_team<<",\"opponent_row\":"<<r.opponent_row
           <<",\"opponent_player\":"<<r.opponent_player<<",\"opponent_slot\":"<<r.opponent_slot
           <<",\"expected_marking_player_id\":"<<r.expected_marking_player_id;
    out<<"}";return out.str();}

PitchScreen::PitchScreen(const std::filesystem::path& data,LineupState state,PitchExtras e)
    :data_(data),state_(std::move(state)),extras_(std::move(e)),exe_(load_le(asset_path(data,"BMH.exe"))),
     font_(load_font(asset_path(data/"F","BMH3.FNT"))),sprite_(data),texts_(load_textd(asset_path(data,"TEXTD.BLK"))),
     background_(load_vga(asset_path(data/"KB","129.VGA"))),atlas_(load_vga(asset_path(data/"KB","2.VGA"))),
     widgets_(load_vga(asset_path(data/"KB","0.VGA"))),letters_(load_vga(asset_path(data/"KB","54.VGA"))){
    validate(state_,extras_);
    // OpenBMH deviates from the original: supports owner-bound designated set-piece selection in the extended manager record.
// Padded clears only transient runtime state on whole-pitch return; the
    // next NormalizePitchSetPieceSource entry calls Padded2, which reopens the private editor when
    // the retained native display word still carries bit 0x80. NormalizePitchSetPieceSource has
    // established the supported rule profile signature before that restore is attempted.
    shooter_editor_open_ = extras_.set_piece_signature == 0x3253 &&
        (extras_.native_display_word & 0x80) != 0;
    shooter_selection_ = 1;
    order_panel_open_ = !shooter_editor_open_ && (extras_.native_display_word & 0x20) != 0;
    if(atlas_.width==314){Image padded(320,atlas_.height);padded.blit(atlas_,0,0);atlas_=std::move(padded);}
    // PitchScreen patches the private KB2 work atlas with the Source KB33
    // hall surface at (1,2); the ordinary pitch renderer later draws this
    // atlas at (0,108). Never mutate a shared asset or stretch the patch.
    if(extras_.indoor && *extras_.indoor){
        const auto hall=load_vga(asset_path(data_/"KB","33.VGA"));
        if(hall.width!=193||hall.height!=115)throw FormatError("pitch: invalid original hall surface");
        copy(atlas_,hall,0,0,1,2,193,115);
    }
    badge_=load_vga(asset_path(data/"W",std::to_string(extras_.badge>>3)+".VGA"));
    sheet_=std::make_unique<LineupScreen>(data_,state_);
}
PitchScreen::PitchScreen(const PitchScreen& other)
    :PitchScreen(other.data_,other.state_,other.extras_){
    pending_=other.pending_;selected_=other.selected_;hovered_=other.hovered_;ghost_=other.ghost_;
    order_hover_grid_=other.order_hover_grid_;
    scroll_=other.scroll_;close_=other.close_;return_team_=other.return_team_;rejection_=other.rejection_;
    weights_dirty_=other.weights_dirty_;
    shooter_editor_open_=other.shooter_editor_open_;shooter_selection_=other.shooter_selection_;
    zone_cover_open_=other.zone_cover_open_;
    order_panel_open_=other.order_panel_open_;marking_target_row_=other.marking_target_row_;
    marking_scroll_=other.marking_scroll_;pressed_page_control_=other.pressed_page_control_;
    sheet_=std::make_unique<LineupScreen>(*other.sheet_);
}
PitchScreen& PitchScreen::operator=(const PitchScreen& other){
    if(this!=&other){PitchScreen copy(other);*this=std::move(copy);}return *this;
}
void PitchScreen::validate(const LineupState& s,const PitchExtras& e)const{
    if(s.manager_slot<0||s.manager_slot>3||s.runtime_club<0||(s.players.size()!=24&&s.players.size()!=16)||
       e.manager_slot!=s.manager_slot||e.runtime_club!=s.runtime_club||e.revision!=s.revision||e.source!=s.source)
        throw FormatError("pitch: inconsistent authoritative context");
    if(!e.indoor)throw FormatError("pitch: transient indoor/outdoor mode is unknown");
    // OpenBMH deviates from the original: supports owner-bound designated set-piece selection in the extended manager record.
// A loaded career can carry an uninitialized supported rule profile tail until the
    // authoritative lineup entry initializes it. Read-only preparation must
    // remain usable before that entry; editor commands validate the tail below.
    if(e.mode<1||e.mode>3)throw FormatError("pitch: invalid game mode");
    if(s.automatic_formation<0||s.automatic_formation>8)throw FormatError("pitch: invalid automatic formation");
    for(size_t i=0;i<s.players.size();++i){const auto& p=s.players[i];
        if(p.row!=int(i)||p.player_id!=((p.slot&0xfff)==0xfff?-1:p.slot&0xfff))throw FormatError("pitch: inconsistent squad identity");}
}
void PitchScreen::text(Image& out,int x,int baseline,std::string_view value,uint8_t color)const{
    const int width=font_.text_width(value);if(x<0||x+width>out.width||baseline<5||baseline>=out.height)return;
    Image mask(std::max(1,width),6);font_.draw(mask,0,0,value,1);
    int g=0;while(g<32&&exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2+g)!=color)++g;
    for(int yy=0;yy<6;++yy)for(int xx=0;xx<width;++xx)if(mask.at(xx,yy))
        out.at(x+xx,baseline-5+yy)=state_.gradient_text?exe_.u8(::bmh::layout::Advertising_Text_RuntimeField+g*6+yy):color;
}
void PitchScreen::button(Image& out,int x,int y,std::string_view value,bool selected)const{
    constexpr int width=43,height=10;
    if(selected){ // FormatError3 erases the upper/left frame, then offsets its pressed atlas by two pixels.
        rect(out,x,y,width+1,2,0x42);rect(out,x,y,2,height+1,0x42);x+=2;y+=2;
    }else{rect(out,x+2,y+height,width,2,0x40);rect(out,x+width,y+2,2,height,0x40);}
    const int sy=selected?44:74,font=exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + int(selected));
    copy(out,widgets_,24,sy,x,y,31,10);copy(out,widgets_,33,sy+20,x+31,y,12,10);
    if(font==3)text(out,x+width/2-font_.text_width(value)/2,y+7,value,0);
    else {int length=0;for(unsigned char c:value){const int g=exe_.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + c);length+=g?exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2+g*2)-exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField+g*2):2;}
        int xx=x+width/2-length/2;for(unsigned char c:value){if(c<=31||c>=128)continue;const int g=exe_.u8(::bmh::layout::ClubInfo_Raw_RuntimeField3 + c);
            if(!g){xx+=2;continue;}const int sx=exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField+g*2),w=exe_.u16(::bmh::layout::ClubInfo_Raw_RuntimeField2+g*2)-sx;
            copy(out,letters_,sx,(font-4)*7,xx,y+1,w,7,0);xx+=w;}}
}
void PitchScreen::diamond_button(Image& out,int x,int y,std::string_view value,bool selected)const{
    // FormatError3: width60, height9 normalized to 10, with pressed frame and
    // runtime font byte Advertising_Button_RuntimeField+selected. Match the original +2 pressed offset.
    constexpr int width=60,height=10;
    int dx=x,dy=y;const int mode=exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + unsigned(selected));
    if(selected){const uint8_t bg=out.at(x,y+height);rect(out,x,y,width+1,2,bg);rect(out,x,y,2,height+1,bg);dx+=2;dy+=2;}
    else{rect(out,x+2,y+height,width,2,0x40);rect(out,x+width,y+2,2,height,0x40);}
    const int row=selected?44:74;copy(out,widgets_,24,row,dx,dy,31,10,0xc0);
    int used=31;while(width-used>20){const int amount=std::min(8,width-used-20);
        copy(out,widgets_,24,row+10,dx+used,dy,amount,10,0xc0);used+=amount;}
    const int tail=width-used;copy(out,widgets_,45-tail,row+20,dx+used,dy,tail,10,0xc0);
    if(mode==3){const auto native_value=utf8_to_original(value);
        text(out,dx+width/2-font_.text_width(native_value)/2,dy+7,native_value,0);}
    else sprite_.draw(out,dx+width/2-sprite_.text_width(value)/2,dy+7,value,mode);
}
void PitchScreen::jersey(Image& out,const LineupPlayer& p,int depth,int lane,int color,bool number)const{
    if(p.player_id<0||((p.attributes.w364>>16)&31))return;
    int x=depth*8+4,y=lane*10+119;
    if(p.board[0x3f]!=0xff)color=3;
    copy(out,atlas_,208+color*16,0,x,y,16,17,0);
    if(!number)return;
    int n=(p.cards&31)+1;
    if(n>9){copy(out,atlas_,224,27,x+10,y,16,5,0);n-=10;x+=3;}
    copy(out,atlas_,208+(n%7)*16,27+(n/7)*5,x+10,y,16,5,0);
}
std::vector<std::pair<int,int>> PitchScreen::rows()const{
    std::vector<std::pair<int,int>> r;int baseline=36;
    for(size_t i=0;i<state_.players.size();++i){const auto&p=state_.players[i];
        if(i&&((p.slot&0xc000)!=(state_.players[i-1].slot&0xc000)))baseline+=2;
        if(p.player_id<0)continue;
        r.emplace_back(int(i),baseline-scroll_);baseline+=7;}
    return r;
}
std::optional<Image> PitchScreen::formation_save_feedback(bool restored)const{
    if(!pending_||pending_->kind!=PitchRequestKind::save_formation)return std::nullopt;
    auto out=render();
    const int x=243+(pending_->formation%2)*38,y=110+(pending_->formation/2)*23;
    const uint8_t color=restored?0x6b:0;
    // InclusiveHoverOutlineAtYHitThroughYHit draws an inclusive outline; the players inside are untouched.
    rect(out,x,y,35,1,color);rect(out,x,y+20,35,1,color);
    rect(out,x,y,1,21,color);rect(out,x+34,y,1,21,color);
    return out;
}
bool PitchScreen::automatic_page()const{
    // OpenBMH deviates from the original: supports owner-bound designated set-piece selection in the extended manager record.
// PitchScreen draws AutomaticPage and accepts AA weights/formation input only
    // on native page bit 0x10. The full team sheet and supported rule profile editor own
    // their respective areas even when that bit is retained underneath.
    return !shooter_editor_open_&&(extras_.native_display_word&0x18)==0x18;
}
Image PitchScreen::render()const{
    // PitchScreen's page redraw copies the owned offscreen sheet from y=20: all
    // 190 rows when source bit 0x08 is clear, only the upper 88 rows when it is
    // set. In the latter case the pitch and its right panel are composed below.
    const auto list=sheet_->render();
    const uint16_t display_word=shooter_editor_open_
        ? uint16_t(extras_.native_display_word&0xff0f)
        : extras_.native_display_word;
    if((display_word&0x08)==0)return list;
    Image out=list;
    copy(out,background_,0,108,0,108,320,120);
    copy(out,atlas_,0,0,0,108,196,117);
    rect(out,3,225,191,2,0);
    rect(out,196,108,124,119,0x42);
    const bool marking_open=(display_word&0x0040)!=0&&extras_.opponent_team>=0&&
        !extras_.indoor.value_or(false);
    if(scroll_)copy(out,list,0,31+scroll_,0,31,320,77);
    // Names change locally; the full sheets remain the authoritative attributes.
    for(auto[row,baseline]:rows())if(baseline>34&&baseline<107&&(selected_==row||hovered_==row)){
        copy(out,background_,13,baseline-5,13,baseline-5,51,6);
        text(out,13,baseline,state_.players.at(row).name,selected_==row?0x27:0x3e);}
    const auto eligible_order_row = [&](std::optional<int> row) {
        return row && *row >= 0 && *row < 11 &&
            (state_.players.at(size_t(*row)).slot & 0x8000) &&
            ((state_.players.at(size_t(*row)).attributes.w364 >> 16) & 0x1f) == 0;
    };
    std::optional<int> order_row;
    if(eligible_order_row(hovered_))order_row=hovered_;
    else if(eligible_order_row(selected_))order_row=selected_;
    // Both the order page's hovered player and the taker editor's coverage
    // toggle use the native ORDER geometry and VGA plane-7 mask.
    const auto draw_coverage = [&](const LineupPlayer& p) {
            if(!(p.slot&0x8000)||((p.attributes.w364>>16)&0x1f)!=0)return;
            const int depth=int8_t(p.board[0x41]),lane=int8_t(p.board[0x42]);
            const int order_x=p.board[2],order_y=p.board[3];
            const int zone_count=p.board[4]/2;
            const int player_x=depth*8+4, player_y=lane*10+0x77;
            const int radius=order_y/2+order_x+8;
            const int source_left=(player_x-(order_y/2+0x26-order_x))-8;
            const int inner=order_y/2+0x26-order_x+8;
            int backing_left=source_left, backing_half=inner;
            if(player_x-inner<3){backing_left=3;backing_half=player_x-3;}
            int right_span=radius;
            if(player_x+radius+0x15>0xc0)right_span=0xab-player_x;
            const int backing_right=depth*8+0x19;
            // CopyCrop -> UsesForTheseSprites uses the current global transparent key
            // (0 at this Source call site) and VGA write mask 0x80.
            source_mask_copy(out,atlas_,0x122-radius,0x50,backing_right,player_y,right_span,0x12,0);
            source_mask_copy(out,atlas_,0xc4,0x50,backing_left,player_y,backing_half+0x15,0x12,0);
            // The contour uses the unclipped backing geometry: DerivesRedCoverage keeps
            // local18 for the line start and radius for the right endpoint.
            int edge_x0=source_left+8;
            int edge_x1=depth*8+0x19+radius-8;
            int step=1;
            for(int n=0;n<zone_count;++n){
                const int bend=(n==8?3:0)+(n==6?2:0)+(n==3?1:0);
                if(edge_x0 < edge_x1-(step+bend)*2-0x14){
                    const int delta=bend+step;edge_x0+=delta;edge_x1-=delta;
                }
                const int draw_x0=std::clamp(edge_x0,3,0xc0),draw_x1=std::clamp(edge_x1,3,0xc0);
                const int upper=std::max(player_y-n-1,0x71);
                const int lower=std::min(player_y+n+0x12,0xdb);
                if(draw_x1>draw_x0){
                    // SourceMaskCopy delegates to opaque DrawOpaquePlaneMask. Its 0xc0
                    // argument is ignored; 0x80 updates only destination bit 7.
                    source_mask_copy(out,atlas_,0xc4,0x59,draw_x0,upper,draw_x1-draw_x0,1);
                    source_mask_copy(out,atlas_,0xc4,0x59,draw_x0,lower,draw_x1-draw_x0,1);
                }
            }
    };
    if(shooter_editor_open_&&zone_cover_open_){
        for(int row=0;row<11;++row)draw_coverage(state_.players.at(size_t(row)));
    }else if(order_panel_open_&&order_row){
        draw_coverage(state_.players.at(size_t(*order_row)));
    }
    for(int row=0;row<11;++row){const auto& p=state_.players.at(row);if(!(p.slot&0x8000))continue;
        jersey(out,p,int8_t(p.board[0x41]),int8_t(p.board[0x42]),selected_==row||hovered_==row?3:0);}
    if(selected_&&ghost_)jersey(out,state_.players.at(*selected_),ghost_->first,ghost_->second,1);
    // Eight original 35x21 formation panels, AutomaticPage; modal and Anweis
    // source pages own this right-hand area exclusively.
    if(automatic_page())for(int f=0;f<8;++f){const int x=243+(f%2)*38,y=110+(f/2)*23;
        copy(out,atlas_,268,38,x,y,35,21);
        if(state_.automatic_formation==f+1){rect(out,x,y,35,1,0x27);rect(out,x,y+20,35,1,0x27);rect(out,x,y,1,21,0x27);rect(out,x+34,y,1,21,0x27);}
        if(extras_.formation_mask&(1<<f))for(int p=1;p<11;++p){const int depth=extras_.formations.at(f*22+p),lane=extras_.formations.at(f*22+11+p);
            int xx=x+depth*2-1;if(depth>10)--xx;if(depth>17)--xx;
            const auto color=exe_.u8(::bmh::layout::Pitch_Render_RuntimeField+(depth>7)+(depth>13));rect(out,xx-1,y+2+lane*2,2,1,color);}}
    if(automatic_page()&&extras_.mode!=1)for(int f=0;f<3;++f){
        const int x=241+f*27;copy(out,atlas_,196+f*24,38,x,205,24,18);
        copy(out,atlas_,256+((extras_.automatic_weights>>(2*f))&3)*16,32,x+19,211,4,5);}
    button(out,196,110,texts_[(::bmh::layout::Lineup_Render_TextPointer-::bmh::layout::TextPointerTable)/4+(state_.view==LineupView::data_a?1:0)]);
    button(out,196,123,texts_[(::bmh::layout::Pitch_Render_TextPointer-::bmh::layout::TextPointerTable)/4]);
    button(out,196,136,texts_[(::bmh::layout::Pitch_Render_TextPointer2-::bmh::layout::TextPointerTable)/4],(extras_.native_display_word&0x0010)!=0);
    if(extras_.mode!=1)button(out,196,149,texts_[(::bmh::layout::Pitch_Render_TextPointer3-::bmh::layout::TextPointerTable)/4],order_panel_open_);
    if(extras_.opponent_team>=0&&!extras_.indoor.value_or(false))
        button(out,196,162,texts_[(::bmh::layout::Pitch_Render_TextPointer4-::bmh::layout::TextPointerTable)/4],marking_open);
    button(out,196,175,texts_[(::bmh::layout::Pitch_Render_TextPointer5-::bmh::layout::TextPointerTable)/4],shooter_editor_open_);
    // SourceBadgeFallback/PaintsFourBorderEdges: raised frame, drawn left/top then bottom/right.
    rect(out,198,187,1,40,0x46);rect(out,198,187,40,1,0x46);
    rect(out,198,226,40,1,0x40);rect(out,237,187,1,40,0x40);
    copy(out,badge_,(extras_.badge%8)*38,0,199,188,38,38);
    if(shooter_editor_open_){
        rect(out,239,110,80,113,0x42);
        const std::array<std::pair<uint32_t,int>,3> labels{{{::bmh::layout::FreeKickTakerLabel,114},{::bmh::layout::PenaltyTakerLabel,146},{::bmh::layout::CornerTakerLabel,178}}};
        for(size_t i=0;i<labels.size();++i)
            diamond_button(out,249,labels[i].second,original_to_utf8(exe_.cstring(labels[i].first)),
                           shooter_selection_==int(i)+2);
        diamond_button(out,249,211,original_to_utf8(exe_.cstring(::bmh::layout::AutomaticTakersLabel)),
                       extras_.set_piece_takers==std::array<uint16_t,3>{0,0,0});
        for(size_t i=0;i<3;++i){
            const uint16_t encoded=extras_.set_piece_takers[i];
            std::string name="AUTO";
            if(encoded){const int id=int(encoded)-1;for(const auto& p:state_.players)if(p.player_id==id){name=p.name;break;}}
            // Text2 passes 0x6b as color to DrawnFirstBy; the preceding
            // button text restores CareerSession_DispatchLeagueTableControl_RuntimeField2 to FNT3 on return (18234ff).
            text(out,279-font_.text_width(name)/2,133+int(i)*32,name,0x6b);
        }
    }
    if(marking_open){
        // DrawsOpponentHeaderFiveOutfieldRows draws the opponent header and five outfield rows in the
        // right panel; local_64 toggles the two five-row windows with XOR 5.
        std::string team_name=extras_.opponent_name;
        while(!team_name.empty()&&font_.text_width(team_name)>77)team_name.pop_back();
        text(out,243,117,team_name,0x6a);
        for(int visible=0;visible<5;++visible){
            const int source_row=1+marking_scroll_+visible;
            const auto found=std::find_if(extras_.opponent_players.begin(),extras_.opponent_players.end(),
                [&](const PitchOpponent& p){return p.row==source_row;});
            if(found==extras_.opponent_players.end())continue;
            const int baseline=127+visible*14;
            const uint8_t color=marking_target_row_&&
                size_t(*marking_target_row_)<extras_.opponent_players.size()&&
                extras_.opponent_players[size_t(*marking_target_row_)].row==source_row?0x27:0x6a;
            text(out,243,baseline,texts_[(::bmh::layout::ClubInfo_Render_TextPointer2-::bmh::layout::TextPointerTable)/4+found->position],0x67);
            std::string shown=found->name;
            while(!shown.empty()&&font_.text_width(shown)>56)shown.pop_back();
            text(out,263,baseline,shown,color);
            std::string details=found->detail_text;
            while(!details.empty()&&font_.text_width(details)>77)details.pop_back();
            text(out,243,baseline+6,details,0x69);
        }
    }
    if(extras_.mode!=1&&order_panel_open_){
        // The original panel includes its eight atlas controls and up to six
        // marked substitutes above the order bars; none are generated artwork.
        for(int side=0;side<2;++side)for(int aspect=0;aspect<4;++aspect)
            copy(out,atlas_,196+aspect*16,56+side*12,243+side*58,179+aspect*12,15,12,0xc0);
        int bench_row=0;
        for(const auto& player:state_.players){
            if(bench_row==6)break;
            if(player.player_id<0||!(player.slot&0x4000))continue;
            const int baseline=118+7*bench_row++;
            text(out,243,baseline,exe_.string_at_pointer(::bmh::layout::PitchOrderPositionPointers+
                player.attributes.position()*4),0x6b);
            std::string name=player.name;
            while(!name.empty()&&font_.text_width(name)>48)name.pop_back();
            text(out,252,baseline,name,0x6b);
            text(out,302,baseline,"("+std::to_string(player.attributes.strength())+")",0x6b);
        }
        if(order_hover_grid_){
            const auto [depth,lane]=*order_hover_grid_;
            const auto mask=exe_.u16(::bmh::layout::PitchOrderZoneMasks+lane*38+std::min(depth,18)*2);
            std::string position,zone;
            const auto label=[&](int bit)->const std::string&{
                return texts_[(::bmh::layout::PitchOrderZoneTextPointers-::bmh::layout::TextPointerTable)/4+bit];
            };
            for(int bit=0;bit<6;++bit)if(mask&(1<<bit)){position=label(bit);break;}
            for(int bit=6;bit<14;++bit)if(mask&(1<<bit))zone+=label(bit);
            while(!position.empty()&&font_.text_width(position)>78)position.pop_back();
            while(!zone.empty()&&font_.text_width(zone)>78)zone.pop_back();
            text(out,241,165,position,0x6b);
            text(out,241,173,zone,0x69);
        }
    }
    if(extras_.mode!=1&&order_panel_open_&&order_row){
        const auto& player=state_.players.at(size_t(*order_row));
        for(int aspect=0;aspect<4;++aspect){
            const int y=181+12*aspect,value=player.board[size_t(2+aspect)];
            if(value){copy(out,atlas_,0x103,0x3b,0x103,y,value,9,0xc0);
                rect(out,0x105,y+9,value,1,0);rect(out,0x103+value,y+2,1,8,0);}
        }
    }
    return out;
}
std::optional<std::pair<int,int>> PitchScreen::grid(int x,int y){
    if(x<=3||x>=162||y<=117||y>=209)return {};
    return std::pair{(x-4)/8,(y-119)/10};
}
std::optional<int> PitchScreen::hit_row(int x,int y)const{
    if(x>=0&&x<63&&y>=31&&y<107)for(auto[row,baseline]:rows())if(baseline-7<=y&&y<baseline+2&&baseline>34&&baseline<107)return row;
    if(const auto pos=grid(x,y))for(int row=0;row<11;++row){const auto&p=state_.players.at(row);
        if(p.player_id>=0&&(p.slot&0xc000)&&((p.attributes.w364>>16)&31)==0&&int8_t(p.board[0x41])==pos->first&&int8_t(p.board[0x42])==pos->second)return row;}
    return {};
}
std::optional<int> PitchScreen::hit_opponent_row(int x,int y)const{
    if(x<239||x>=320||y<122||y>=192)return {};
    const int source_row=1+marking_scroll_+(y-122)/14;
    for(size_t i=0;i<extras_.opponent_players.size();++i)
        if(extras_.opponent_players[i].row==source_row)return int(i);
    return {};
}
PitchRequest PitchScreen::request(PitchRequestKind kind,int row,int second,int depth,int lane){
    PitchRequest r;r.kind=kind;r.manager_slot=state_.manager_slot;r.runtime_club=state_.runtime_club;r.revision=state_.revision;
    r.indoor=extras_.indoor.value_or(false);r.expected_mode=uint8_t(extras_.mode);
    const auto&p=state_.players.at(row);r.row=row;r.player_id=p.player_id;r.slot=p.slot;
    r.old_depth=int8_t(p.board[0x41]);r.old_lane=int8_t(p.board[0x42]);r.new_depth=depth;r.new_lane=lane;
    if(second>=0){const auto&q=state_.players.at(second);r.second_row=second;r.second_player=q.player_id;r.second_slot=q.slot;}
    pending_=r;rejection_.clear();return r;
}
PitchRequest PitchScreen::automatic_request(PitchRequestKind kind,int index){
    PitchRequest r;r.kind=kind;r.manager_slot=state_.manager_slot;r.runtime_club=state_.runtime_club;
    r.revision=state_.revision;r.indoor=extras_.indoor.value_or(false);r.expected_mode=uint8_t(extras_.mode);r.old_weights=r.new_weights=extras_.automatic_weights;
    if(kind==PitchRequestKind::set_automatic_weight){
        r.weight_index=index;const int shift=2*index;
        const int next=(((r.old_weights>>shift)&3)+1)&3;
        r.new_weights=uint8_t((r.old_weights&~(3<<shift))|(next<<shift));
    }else r.formation=index;
    pending_=r;rejection_.clear();return r;
}
PitchRequest PitchScreen::set_piece_request(PitchRequestKind kind){
    if(extras_.set_piece_signature!=0x3253){
        rejection_="Die Schützenauswahl muss zuerst durch die Aufstellung vorbereitet werden.";
        return {};
    }
    PitchRequest r;r.kind=kind;r.manager_slot=state_.manager_slot;r.runtime_club=state_.runtime_club;
    r.revision=state_.revision;r.indoor=extras_.indoor.value_or(false);r.expected_mode=uint8_t(extras_.mode);
    r.expected_set_piece_values=extras_.set_piece_takers;r.expected_display_word=extras_.native_display_word;
    r.expected_set_piece_signature=extras_.set_piece_signature;
    pending_=r;rejection_.clear();return r;
}
PitchRequest PitchScreen::page_request(int control){
    PitchRequest r;r.kind=PitchRequestKind::select_pitch_page;r.manager_slot=state_.manager_slot;
    r.runtime_club=state_.runtime_club;r.revision=state_.revision;r.indoor=extras_.indoor.value_or(false);r.expected_mode=uint8_t(extras_.mode);
    r.expected_display_word=extras_.native_display_word;r.expected_set_piece_values=extras_.set_piece_takers;
    r.expected_set_piece_signature=extras_.set_piece_signature;r.page_control=control;
    if(control==4)r.opponent_team=extras_.opponent_team;
    const uint16_t old=r.expected_display_word;
    switch(control){case 0:r.new_display_word=uint16_t((old&~uint16_t(0x80))^0x0003);break;
    case 1:r.new_display_word=uint16_t(old^0x000c);break;
    case 2:r.new_display_word=uint16_t(((old&~uint16_t(0x0080))&0x008f)|0x0010);break;
    case 3:r.new_display_word=uint16_t(((old&~uint16_t(0x0080))&0x008f)|0x0020);break;
    case 4:r.new_display_word=uint16_t(((old&~uint16_t(0x0080))&0x008f)|0x0040);break;
    default:break;}
    pending_=r;rejection_.clear();return r;
}
PitchRequest PitchScreen::marking_request(int own_row,int opponent_row){
    if(opponent_row<0||size_t(opponent_row)>=extras_.opponent_players.size())return {};
    const auto& opponent=extras_.opponent_players.at(size_t(opponent_row));
    PitchRequest r;r.kind=PitchRequestKind::set_player_marking;r.manager_slot=state_.manager_slot;
    r.runtime_club=state_.runtime_club;r.revision=state_.revision;r.indoor=extras_.indoor.value_or(false);
    r.expected_mode=uint8_t(extras_.mode);r.expected_display_word=extras_.native_display_word;
    r.opponent_team=extras_.opponent_team;r.opponent_row=opponent.row;r.opponent_player=opponent.player_id;
    r.opponent_slot=opponent.slot;
    if(own_row>=0){
        if(size_t(own_row)>=state_.players.size())return {};
        const auto& own=state_.players.at(size_t(own_row));
        if(own_row>=11||own.player_id<0||!(own.slot&0x8000)||((own.attributes.w364>>16)&31)!=0)return {};
        r.row=own_row;r.player_id=own.player_id;r.slot=own.slot;
        r.expected_marking_player_id=uint16_t(own.board[0x44]|uint16_t(own.board[0x45])<<8);
    }else{
        r.row=-1;r.player_id=-1;r.expected_marking_player_id=uint16_t(opponent.player_id);
    }
    pending_=r;rejection_.clear();return r;
}
PitchRequest PitchScreen::request_page_control(int control){
    if(pending_||close_||control!=1)return {};
    return page_request(control);
}
PitchRequest PitchScreen::order_request(int aspect,int value){
    if(!selected_||*selected_>=11)return {};
    const auto& p=state_.players.at(size_t(*selected_));
    PitchRequest r;r.kind=PitchRequestKind::set_player_order;r.manager_slot=state_.manager_slot;
    r.runtime_club=state_.runtime_club;r.revision=state_.revision;r.indoor=extras_.indoor.value_or(false);r.expected_mode=uint8_t(extras_.mode);
    r.row=*selected_;r.player_id=p.player_id;r.slot=p.slot;r.order_aspect=aspect;
    r.expected_display_word=extras_.native_display_word;r.expected_set_piece_values=extras_.set_piece_takers;
    r.expected_set_piece_signature=extras_.set_piece_signature;
    r.expected_order_value=p.board[size_t(2+aspect)];r.new_order_value=value;pending_=r;rejection_.clear();return r;
}
PitchRequest PitchScreen::click(int x,int y,bool right){
    if(pending_||close_)return {};
    const uint16_t display_word=shooter_editor_open_
        ? uint16_t(extras_.native_display_word&0xff0f)
        : extras_.native_display_word;
    if(right){
        if((display_word&0x08)==0){sheet_->click(x,y,true);close_=sheet_->close_requested();}
        else if(selected_){selected_.reset();ghost_.reset();}
        else {close_=true;zone_cover_open_=false;}
        return {};
    }
    if((display_word&0x08)==0){
        // On the full-sheet redraw, PitchScreen presents its original footer
        // controls at (1,215) and (58,215): data A/B and the pitch/team page.
        if(y>=215&&y<=224&&x>=1&&x<=56)return page_request(0);
        if(y>=215&&y<=224&&x>=58&&x<=113)return page_request(1);
        // Keep the native team-sheet row actions available while this page is
        // shown: left-column selection/swap and right-column player info use
        // the same hitboxes and authoritative row identity as LineupScreen.
        const auto row_action=sheet_->click(x,y,false);
        if(row_action.kind==LineupRequestKind::swap_players)
            return request(PitchRequestKind::swap_players,row_action.first_row,row_action.second_row);
        if(row_action.kind==LineupRequestKind::open_player_info){
            PitchRequest r;r.kind=PitchRequestKind::open_player_info;r.manager_slot=state_.manager_slot;
            r.runtime_club=state_.runtime_club;r.revision=state_.revision;r.row=row_action.first_row;
            const auto& player=state_.players.at(size_t(r.row));r.player_id=player.player_id;r.slot=player.slot;
            pending_=r;rejection_.clear();return r;
        }
        return {};
    }
    if(shooter_editor_open_){
        if(x>=249&&x<=309&&((y>=114&&y<=123)||(y>=146&&y<=155)||(y>=178&&y<=187))){
            const int value=y<=123?2:y<=155?3:4;
            shooter_selection_=shooter_selection_==value?1:value;return {};
        }
        if(x>=249&&x<=309&&y>=211&&y<=219)return set_piece_request(PitchRequestKind::auto_set_piece_takers);
        // PageRequest control 5 remains present in the active editor table. Its
        // repeated click toggles only the private scratch word's coverage bit.
        if(x>=196&&x<=239&&y>=175&&y<=185){zone_cover_open_=!zone_cover_open_;return {};}
        if(x>=196&&x<=239&&y>=110&&y<=120)return page_request(0);
        if(x>=196&&x<=239&&y>=123&&y<=133)return page_request(1);
        if(x>=196&&x<=239&&y>=136&&y<=146)return page_request(2);
        if(extras_.mode!=1&&x>=196&&x<=239&&y>=149&&y<=159)return page_request(3);
        if(extras_.opponent_team>=0&&!extras_.indoor.value_or(false)&&
           x>=196&&x<=239&&y>=162&&y<=172)return page_request(4);
        if(shooter_selection_>=2&&shooter_selection_<=4){
            if(auto row=hit_row(x,y)){
                const auto& p=state_.players.at(size_t(*row));
                if((p.slot&0xc000)==0||((p.attributes.w364>>16)&0x1f)!=0){rejection_="Nur ein gesunder, markierter Kaderspieler kann gewählt werden.";return {};}
                auto r=set_piece_request(PitchRequestKind::set_piece_taker);r.row=*row;r.player_id=p.player_id;r.slot=p.slot;
                r.set_piece_selector=shooter_selection_;r.expected_set_piece_value=extras_.set_piece_takers[size_t(shooter_selection_-2)];
                pending_=r;return r;
            }
        }
        return {};
    }
    if(extras_.mode!=1&&order_panel_open_&&x>=243&&x<=315&&y>=179&&y<=226){
        if(!selected_||*selected_>=11){rejection_="Für Anweisungen muss ein Startspieler markiert sein.";return {};}
        const int aspect=(y-179)/12,xvalue=x;
        const auto current=int(state_.players.at(size_t(*selected_)).board[size_t(2+aspect)]);
        int next=current;
        if(xvalue<258)next=std::max(2,current-1);
        else if(xvalue>300)next=std::min(40,current+1);
        else if(xvalue>=259&&xvalue<=299)next=std::clamp(xvalue-259,2,40);
        if(next==current)return {};
        return order_request(aspect,next);
    }
    if(x>=196&&x<=239&&y>=110&&y<=120)return page_request(0);
    if(x>=196&&x<=239&&y>=123&&y<=133)return page_request(1);
    if(x>=196&&x<=239&&y>=136&&y<=146)return page_request(2);
    if(x>=196&&x<=239&&y>=175&&y<=185){
        auto r=set_piece_request(PitchRequestKind::open_set_piece_editor);
        if(r.kind==PitchRequestKind::none)return {};
        r.new_display_word=uint16_t((extras_.native_display_word&0xff00)|((extras_.native_display_word&0x008f)|0x0080));pending_=r;return r;
    }
    if(extras_.mode!=1&&x>=196&&x<=239&&y>=149&&y<=159)return page_request(3);
    if(extras_.opponent_team>=0&&!extras_.indoor.value_or(false)&&
       x>=196&&x<=239&&y>=162&&y<=172)return page_request(4);
    if(x>=196&&x<=239&&y>=149&&y<=172){
        rejection_="order and marking controls are read only in this context";return {};
    }
    const bool marking_open=(display_word&0x0040)!=0&&extras_.opponent_team>=0&&
        !extras_.indoor.value_or(false);
    if(marking_open&&x>=239&&x<320&&y>=110&&y<=225){
        if(const auto target=hit_opponent_row(x,y)){
            if(marking_target_row_==target){
                const auto r=marking_request(-1,*target);
                marking_target_row_.reset();selected_.reset();return r;
            }
            if(selected_&&*selected_<11){
                const auto r=marking_request(*selected_,*target);
                selected_.reset();marking_target_row_.reset();return r;
            }
            marking_target_row_=target;selected_.reset();hovered_.reset();ghost_.reset();return {};
        }
        if(!marking_target_row_&&(y<123||y>190))marking_scroll_^=5;
        return {};
    }
    if(!state_.entry_prepared){rejection_="lineup entry has not been authoritatively prepared";return {};}
    // PitchScreen: AA controls are unavailable during a player placement. Weights
    // act on press; formation choice acts on release, represented by one click.
    if(automatic_page()&&!state_.in_match&&!selected_&&x>=240&&x<320){
        if(y>=208&&y<222){
            if(extras_.mode==1){rejection_="automatic weights are unavailable in Modus I";return {};}
            return automatic_request(PitchRequestKind::set_automatic_weight,(x-240)/27);
        }
        if(y>=112&&y<210){
            // Original hit grid is 24 high (drawn panels are 23 apart).
            const int formation=((y-112)/24)*2+(x>279);
            if(!(extras_.formation_mask&(1<<formation))){rejection_="formation slot is empty";return {};}
            if(state_.automatic_formation==formation+1&&!weights_dirty_)return {};
            return automatic_request(PitchRequestKind::apply_formation,formation);
        }
    }
    // PitchScreen scrolls the native pitch data viewport on LMT right of the
    // name column. It does not open player info at that location in pitch mode.
    if(x>=63&&x<320&&y>=31&&y<110){
        const auto all=rows();int current=0;
        for(size_t i=0;i<all.size();++i)if(all[i].second==36){current=int(i);break;}
        const int next=std::clamp(current+(y<65?-1:y>65?1:0),0,std::max(0,int(all.size())-10));
        if(next!=current){scroll_+=all.at(next).second-all.at(current).second;hovered_.reset();}
        return {};
    }
    if(selected_){const auto&p=state_.players.at(*selected_);
        if(auto pos=grid(x,y)){if(p.row>=11||((p.attributes.w364>>16)&31))return {};
            if(marking_open&&marking_target_row_){const auto r=marking_request(*selected_,*marking_target_row_);
                selected_.reset();marking_target_row_.reset();ghost_.reset();return r;}
            if(pos->first==int8_t(p.board[0x41])&&pos->second==int8_t(p.board[0x42])){selected_.reset();ghost_.reset();return {};}
            return request(PitchRequestKind::move_player,*selected_,-1,pos->first,pos->second);}
        if(auto row=hit_row(x,y)){if(*row==*selected_){selected_.reset();ghost_.reset();return {};}return request(PitchRequestKind::swap_players,*selected_,*row);}
        return {};
    }
    if(auto row=hit_row(x,y)){
        if(marking_open&&marking_target_row_){const auto r=marking_request(*row,*marking_target_row_);
            marking_target_row_.reset();return r;}
        selected_=row;hovered_=row;ghost_.reset();return {};}
    return {};
}
PitchRequest PitchScreen::pointer_down(int x,int y,bool right){
    source_down_release_barrier_=false;
    const uint16_t display_word=shooter_editor_open_
        ? uint16_t(extras_.native_display_word&0xff0f)
        : extras_.native_display_word;
    const bool sheet_footer=(display_word&0x08)==0&&y>=215&&y<=224&&
        ((x>=1&&x<=56)||(x>=58&&x<=113));
    const bool lower=x>=196&&x<=239&&
        ((y>=110&&y<=146)||(extras_.mode!=1&&y>=149&&y<=159)||
         (extras_.opponent_team>=0&&!extras_.indoor.value_or(false)&&y>=162&&y<=172)||(y>=175&&y<=185));
    const bool editor_hit=shooter_editor_open_&&
        ((x>=249&&x<=309&&((y>=114&&y<=123)||(y>=146&&y<=155)||(y>=178&&y<=187)||(y>=211&&y<=219)))||
         (shooter_selection_>=2&&(x<163&&y>=31&&y<209)));
    const bool order_hit=extras_.mode!=1&&order_panel_open_&&x>=243&&x<=315&&y>=179&&y<=226;
    if(!right&&!lower&&!editor_hit&&!order_hit&&!sheet_footer)return {};
    source_down_release_barrier_=true;
    return click(x,y,right);
}
PitchRequest PitchScreen::save_formation(int x,int y){
    if(pending_||close_||selected_)return {};
    if(state_.in_match){rejection_="formation slots are unavailable during a live intervention";return {};}
    if(!state_.entry_prepared){rejection_="lineup entry has not been authoritatively prepared";return {};}
    if(!automatic_page())return {};
    if(x<240||x>=320||y<112||y>=210)return {};
    const int formation=((y-112)/24)*2+(x>279);
    if(formation<4){rejection_="long press only supports the four own formation slots";return {};}
    for(int row=0;row<11;++row)if(state_.players[row].player_id<0||!(state_.players[row].slot&0x8000)){
        rejection_="saving a formation requires eleven nominated starters";return {};}
    auto r=automatic_request(PitchRequestKind::save_formation,formation);
    for(int row=0;row<11;++row){r.formation_positions[row]=state_.players[row].board[0x41];
        r.formation_positions[11+row]=state_.players[row].board[0x42];}
    pending_=r;return r;
}
bool PitchScreen::move(int x,int y){if(pending_||close_)return false;
    if((extras_.native_display_word&0x08)==0)return sheet_->move(x,y);
    const auto old=hovered_;const auto oldghost=ghost_;hovered_=hit_row(x,y);ghost_=selected_?grid(x,y):std::nullopt;
    const auto old_grid=order_hover_grid_;
    order_hover_grid_=order_panel_open_?grid(x,y):std::nullopt;
    return old!=hovered_||oldghost!=ghost_||old_grid!=order_hover_grid_;
}
PitchRequest PitchScreen::key(std::string_view key){
    // PitchScreen's desktop pages cancel selection/return on mouse event 8.
    // Escape must not synthesize that right click. The separate live caller
    // retains its existing keyboard return.
    if(state_.in_match&&(key=="ESC"||key=="ESCAPE"))return click(0,0,true);
    return {};
}
void PitchScreen::acknowledge(LineupState s,PitchExtras e){
    if(!pending_)throw FormatError("pitch: no command to acknowledge");
    validate(s,e);
    if(s.revision<=state_.revision||s.manager_slot!=state_.manager_slot||s.runtime_club!=state_.runtime_club||s.source!="engine_projection"||!s.entry_prepared)
        throw FormatError("pitch: stale or mismatched acknowledgement");
    if(e.indoor!=extras_.indoor)
        throw FormatError("pitch: acknowledgement changed the captured live pitch mode");
    if(s.players.size()!=state_.players.size())throw FormatError("pitch: changed squad extent");
    if(pending_->kind==PitchRequestKind::move_player){const auto& p=s.players.at(pending_->row);
        if(p.player_id!=pending_->player_id||p.slot!=pending_->slot||int8_t(p.board[0x41])!=pending_->new_depth||int8_t(p.board[0x42])!=pending_->new_lane)
            throw FormatError("pitch: acknowledgement does not confirm placement");}
    if(pending_->kind==PitchRequestKind::swap_players&&
       (s.players.at(pending_->row).player_id!=pending_->second_player||s.players.at(pending_->second_row).player_id!=pending_->player_id))
        throw FormatError("pitch: acknowledgement does not confirm player exchange");
    const auto kind=pending_->kind;
    if(kind==PitchRequestKind::set_automatic_weight||kind==PitchRequestKind::apply_formation||kind==PitchRequestKind::save_formation){
        auto formations=extras_.formations;auto mask=extras_.formation_mask;
        if(kind==PitchRequestKind::save_formation){
            std::copy(pending_->formation_positions.begin(),pending_->formation_positions.end(),formations.begin()+pending_->formation*22);
            mask|=uint8_t(1<<pending_->formation);
        }
        if(e.indoor!=extras_.indoor||e.mode!=extras_.mode||e.badge!=extras_.badge||e.formation_mask!=mask||
           e.formations!=formations||e.automatic_weights!=pending_->new_weights)
            throw FormatError("pitch: acknowledgement changes unrelated AA context or fails to confirm weights");
        if((kind==PitchRequestKind::apply_formation||kind==PitchRequestKind::save_formation)&&s.automatic_formation!=pending_->formation+1)
            throw FormatError("pitch: acknowledgement does not confirm automatic formation");
        if(kind==PitchRequestKind::set_automatic_weight){
            if(s.automatic_formation!=state_.automatic_formation)
                throw FormatError("pitch: weight change must not run automatic selection");
            for(size_t row=0;row<s.players.size();++row)
                if(s.players[row].player_id!=state_.players[row].player_id||s.players[row].slot!=state_.players[row].slot||
                   s.players[row].board!=state_.players[row].board)
                    throw FormatError("pitch: weight change must preserve squad and board");
        }
    }
    if(kind==PitchRequestKind::open_set_piece_editor||kind==PitchRequestKind::close_set_piece_editor||
       kind==PitchRequestKind::set_piece_taker||kind==PitchRequestKind::auto_set_piece_takers){
        auto expected=extras_.set_piece_takers;
        uint16_t expected_word=extras_.native_display_word;
        if(pending_->expected_set_piece_values!=expected||pending_->expected_display_word!=expected_word||
           pending_->expected_set_piece_signature!=extras_.set_piece_signature||pending_->expected_mode!=extras_.mode||
           e.set_piece_signature!=0x3253)
            throw FormatError("pitch: set-piece acknowledgement is stale or has an invalid signature");
        if(kind==PitchRequestKind::open_set_piece_editor||kind==PitchRequestKind::close_set_piece_editor){
            const uint16_t source_word=kind==PitchRequestKind::open_set_piece_editor
                ?uint16_t((expected_word&0xff00)|((expected_word&0x008f)|0x0080))
                :uint16_t(expected_word&~uint16_t(0x80));
            if(pending_->new_display_word!=source_word)
                throw FormatError("pitch: editor transition does not match Source display-word mask");
            auto normalized=expected;
            if(kind==PitchRequestKind::open_set_piece_editor){
                for(auto& value:normalized){
                    const int id=int(value)-1;
                    const bool member=value&&value<=0x0fff&&std::any_of(state_.players.begin(),state_.players.end(),
                        [&](const LineupPlayer& p){return p.player_id==id;});
                    if(!member)value=0;
                }
            }
            if(e.set_piece_takers!=normalized||e.native_display_word!=pending_->new_display_word)
                throw FormatError("pitch: editor-word acknowledgement is inconsistent");
        }else if(kind==PitchRequestKind::set_piece_taker){
            const size_t idx=size_t(pending_->set_piece_selector-2);
            if(idx>=3||expected[idx]!=pending_->expected_set_piece_value||
               e.native_display_word!=expected_word||e.set_piece_takers[idx]!=uint16_t(pending_->player_id+1))
                throw FormatError("pitch: selected set-piece player was not acknowledged");
            for(size_t i=0;i<3;++i)if(i!=idx&&e.set_piece_takers[i]!=expected[i])
                throw FormatError("pitch: unrelated set-piece taker changed");
        }else if(e.set_piece_takers!=std::array<uint16_t,3>{0,0,0}||e.native_display_word!=expected_word)
            throw FormatError("pitch: AUTO did not clear all designated takers");
    }
    if(kind==PitchRequestKind::select_pitch_page){
        uint16_t expected=pending_->expected_display_word;
        switch(pending_->page_control){
        case 0: expected=uint16_t((expected&~uint16_t(0x80))^0x0003);break;
        case 1: expected=uint16_t(expected^0x000c);break;
        case 2: expected=uint16_t(((expected&~uint16_t(0x0080))&0x008f)|0x0010);break;
        case 3: expected=uint16_t(((expected&~uint16_t(0x0080))&0x008f)|0x0020);break;
        case 4: expected=uint16_t(((expected&~uint16_t(0x0080))&0x008f)|0x0040);break;
        default: throw FormatError("pitch: invalid Source page control");
        }
        if(pending_->new_display_word!=expected||e.native_display_word!=expected||e.set_piece_takers!=extras_.set_piece_takers||
           pending_->expected_mode!=extras_.mode||pending_->expected_set_piece_values!=extras_.set_piece_takers||
           pending_->expected_set_piece_signature!=extras_.set_piece_signature||
           e.set_piece_signature!=extras_.set_piece_signature)
            throw FormatError("pitch: Source page ACK changes unrelated set-piece fields or word");
        if(pending_->page_control==4&&
           (pending_->opponent_team<0||pending_->opponent_team!=extras_.opponent_team||
            e.opponent_team!=extras_.opponent_team||e.opponent_players!=extras_.opponent_players))
            throw FormatError("pitch: man-marking page ACK changed its authenticated live opponent");
    }
    if(kind==PitchRequestKind::set_player_marking){
        if(pending_->opponent_team<0||pending_->opponent_team!=extras_.opponent_team||
           e.opponent_team!=extras_.opponent_team||e.native_display_word!=extras_.native_display_word||
           pending_->expected_display_word!=extras_.native_display_word||pending_->expected_mode!=extras_.mode||
           pending_->indoor||pending_->opponent_row<1||pending_->opponent_row>10||pending_->opponent_player<0)
            throw FormatError("pitch: man-marking ACK is stale or changed its live context");
        for(size_t row=0;row<s.players.size();++row){
            if(s.players[row].player_id!=state_.players[row].player_id||s.players[row].slot!=state_.players[row].slot)
                throw FormatError("pitch: man-marking ACK changed squad identity");
            auto expected=state_.players[row].board;
            if(int(row)<11){
                if(int(row)==pending_->row&&pending_->row>=0&&
                   (pending_->player_id!=state_.players[row].player_id||pending_->slot!=state_.players[row].slot||
                    uint16_t(expected[0x44]|uint16_t(expected[0x45])<<8)!=pending_->expected_marking_player_id))
                    throw FormatError("pitch: own marking row changed before assignment");
                if(uint16_t(expected[0x44]|uint16_t(expected[0x45])<<8)==pending_->opponent_player){
                    expected[0x44]=0xff;expected[0x45]=0x0f;
                }
                if(int(row)==pending_->row&&pending_->row>=0){
                    expected[0x44]=uint8_t(pending_->opponent_player);
                    expected[0x45]=uint8_t(pending_->opponent_player>>8);
                }
            }
            if(s.players[row].board!=expected)
                throw FormatError("pitch: man-marking ACK changed unrelated board fields");
        }
        const auto opponent=std::find_if(e.opponent_players.begin(),e.opponent_players.end(),[&](const PitchOpponent& p){
            return p.row==pending_->opponent_row&&p.player_id==pending_->opponent_player&&p.slot==pending_->opponent_slot;});
        if(opponent==e.opponent_players.end())throw FormatError("pitch: opponent identity disappeared during marking");
    }
    if(kind==PitchRequestKind::set_player_order){
        if(pending_->row<0||pending_->row>=11||pending_->order_aspect<0||pending_->order_aspect>3||
           pending_->expected_mode!=extras_.mode||pending_->expected_display_word!=extras_.native_display_word||
           pending_->expected_set_piece_values!=extras_.set_piece_takers||pending_->expected_set_piece_signature!=extras_.set_piece_signature||
           e.set_piece_takers!=extras_.set_piece_takers||e.set_piece_signature!=extras_.set_piece_signature||
           e.native_display_word!=extras_.native_display_word)
            throw FormatError("pitch: Anweisungs-ACK changes unrelated owner fields");
        for(size_t row=0;row<s.players.size();++row){
            auto expected=state_.players[row].board;
            if(int(row)==pending_->row){
                if(expected[size_t(2+pending_->order_aspect)]!=pending_->expected_order_value)
                    throw FormatError("pitch: Anweisungsanfrage ist stale");
                expected[size_t(2+pending_->order_aspect)]=uint8_t(pending_->new_order_value);
            }
            if(s.players[row].player_id!=state_.players[row].player_id||s.players[row].slot!=state_.players[row].slot||
               s.players[row].board!=expected)
                throw FormatError("pitch: Anweisungs-ACK verändert zusätzliche Kaderdaten");
        }
    }
    if(kind!=PitchRequestKind::select_pitch_page||pending_->page_control!=0)s.view=state_.view;
    else s.view=state_.view==LineupView::data_a?LineupView::data_b:LineupView::data_a;
    auto new_sheet=std::make_unique<LineupScreen>(data_,s);
    auto new_badge=load_vga(asset_path(data_/"W",std::to_string(e.badge>>3)+".VGA"));
    if(kind==PitchRequestKind::set_automatic_weight)weights_dirty_=true;
    else if(kind==PitchRequestKind::apply_formation||kind==PitchRequestKind::save_formation)weights_dirty_=false;
    if(kind==PitchRequestKind::open_set_piece_editor){shooter_editor_open_=true;shooter_selection_=1;order_panel_open_=false;}
    if(kind==PitchRequestKind::close_set_piece_editor){shooter_editor_open_=false;shooter_selection_=1;zone_cover_open_=false;}
    if(kind==PitchRequestKind::set_piece_taker||kind==PitchRequestKind::auto_set_piece_takers)shooter_selection_=1;
    if(kind==PitchRequestKind::select_pitch_page){
        if(pending_->page_control==0){shooter_editor_open_=false;shooter_selection_=1;order_panel_open_=false;zone_cover_open_=false;}
        else if(pending_->page_control==1){ /* Source Team xor keeps the current editor/page. */ }
        else if(pending_->page_control==2){shooter_editor_open_=false;shooter_selection_=1;order_panel_open_=false;zone_cover_open_=false;}
        else if(pending_->page_control==3){shooter_editor_open_=false;shooter_selection_=1;order_panel_open_=true;zone_cover_open_=false;}
        else if(pending_->page_control==4){shooter_editor_open_=false;shooter_selection_=1;order_panel_open_=false;zone_cover_open_=false;marking_scroll_=0;marking_target_row_.reset();}
    }
    if(kind==PitchRequestKind::set_player_marking){marking_target_row_.reset();}
    state_=std::move(s);extras_=std::move(e);sheet_=std::move(new_sheet);badge_=std::move(new_badge);
    scroll_=0;
    if(kind!=PitchRequestKind::set_player_order){selected_.reset();hovered_.reset();ghost_.reset();order_hover_grid_.reset();}
    pending_.reset();rejection_.clear();
}
void PitchScreen::reject_action(std::string reason){if(!pending_||reason.empty())throw FormatError("pitch: rejection requires pending action and reason");
    if(sheet_->pending())sheet_=std::make_unique<LineupScreen>(data_,state_);
    pending_.reset();selected_.reset();hovered_.reset();ghost_.reset();order_hover_grid_.reset();marking_target_row_.reset();rejection_=std::move(reason);
}
void PitchScreen::resolve_local_navigation(){
    if(!pending_||pending_->kind!=PitchRequestKind::open_player_info)
        throw FormatError("pitch: only player info can resolve locally");
    if(sheet_->pending())sheet_->resolve_local_navigation(LineupRequestKind::open_player_info);
    pending_.reset();selected_.reset();hovered_.reset();ghost_.reset();rejection_.clear();
}
std::string PitchScreen::state_json()const{std::ostringstream out;
    out<<"{\"screen\":\"pitch\",\"manager_slot\":"<<state_.manager_slot<<",\"runtime_club\":"<<state_.runtime_club
       <<",\"revision\":"<<state_.revision<<",\"source\":"<<json_quote(state_.source)<<",\"entry_prepared\":"<<(state_.entry_prepared?"true":"false")
       <<",\"view\":"<<json_quote(state_.view==LineupView::data_a?"data_a":"data_b")<<",\"indoor\":"<<(extras_.indoor.value_or(false)?"true":"false")
       <<",\"team_sheet\":"<<sheet_->state_json()
       <<",\"close_requested\":"<<(close_?"true":"false")<<",\"return_team_requested\":"<<(return_team_?"true":"false")
       <<",\"shooter_editor_open\":"<<(shooter_editor_open_?"true":"false")
       <<",\"shooter_selection\":"<<shooter_selection_<<",\"order_panel_open\":"<<(order_panel_open_?"true":"false")
       <<",\"man_marking_open\":"<<(((extras_.native_display_word&0x40)!=0&&extras_.opponent_team>=0&&!extras_.indoor.value_or(false))?"true":"false")
       <<",\"opponent_team\":"<<extras_.opponent_team<<",\"opponent_name\":"<<json_quote(original_to_utf8(extras_.opponent_name))
       <<",\"man_marking_scroll\":"<<marking_scroll_<<",\"man_marking_target_row\":";
    if(marking_target_row_)out<<*marking_target_row_;else out<<"null";
    out<<",\"opponent_players\":[";
    for(size_t i=0;i<extras_.opponent_players.size();++i){if(i)out<<',';const auto& p=extras_.opponent_players[i];
        out<<"{\"row\":"<<p.row<<",\"player_id\":"<<p.player_id<<",\"slot\":"<<p.slot
           <<",\"name\":"<<json_quote(original_to_utf8(p.name))<<",\"current_marker_player_id\":"<<p.current_marker_player_id<<"}";}
    out<<"]"
       <<",\"zone_cover_open\":"<<(zone_cover_open_?"true":"false")
       <<",\"native_display_word\":"<<extras_.native_display_word<<",\"set_piece_takers\":["
       <<extras_.set_piece_takers[0]<<','<<extras_.set_piece_takers[1]<<','<<extras_.set_piece_takers[2]<<"],\"selected_row\":";
    if(selected_)out<<*selected_;else out<<"null";out<<",\"hovered_row\":";if(hovered_)out<<*hovered_;else out<<"null";
    out<<",\"ghost\":";if(ghost_)out<<"{\"depth\":"<<ghost_->first<<",\"lane\":"<<ghost_->second<<"}";else out<<"null";
    out<<",\"pending_action\":";if(pending_)out<<pitch_request_json(*pending_);else out<<"null";
    out<<",\"last_rejection\":";if(rejection_.empty())out<<"null";else out<<json_quote(rejection_);
    out<<",\"scroll_pixels\":"<<scroll_<<",\"automatic_formation\":"<<state_.automatic_formation
       <<",\"formation_mask\":"<<int(extras_.formation_mask)<<",\"automatic_weights\":"<<int(extras_.automatic_weights)
       <<",\"automatic_weights_dirty\":"<<(weights_dirty_?"true":"false")
       <<",\"players\":[";
    for(size_t row=0;row<state_.players.size();++row){if(row)out<<',';const auto& p=state_.players[row];
        out<<"{\"row\":"<<p.row<<",\"player_id\":"<<p.player_id<<",\"slot\":"<<p.slot
           <<",\"name\":"<<json_quote(original_to_utf8(p.name))
           <<",\"depth\":"<<int(int8_t(p.board[0x41]))<<",\"lane\":"<<int(int8_t(p.board[0x42]))<<"}";}
    out<<"],\"unsupported\":[\"overwrite_builtin_formation\"]}";return out.str();
}
} // namespace bmh::ui
