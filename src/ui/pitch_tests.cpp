// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/pitch.hpp"
#include "ui/source_hold.hpp"

#include <functional>
#include <algorithm>
#include <iostream>
#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "game/state.hpp"

using namespace bmh;
using namespace bmh::ui;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void rejected(const std::function<void()>& fn){try{fn();}catch(const FormatError&){return;}throw std::runtime_error("expected pitch rejection");}
LineupState lineup(){LineupState s;s.manager_slot=0;s.runtime_club=12;s.revision=7;s.source="engine_projection";
    s.club_name="UNITFC";s.entry_prepared=true;s.view=LineupView::data_b;
    for(int i=0;i<24;++i){LineupPlayer p;p.row=i;p.player_id=i;p.slot=uint16_t(i|(i<11?0x8000:i<17?0x4000:0));
        p.name="PLAYER"+std::to_string(i);p.cards=i;p.board[0x3f]=0xff;p.board[0x41]=uint8_t(i<11?i:0);p.board[0x42]=4;s.players.push_back(p);}
    return s;
}
PitchExtras extras(){PitchExtras e;e.manager_slot=0;e.runtime_club=12;e.revision=7;e.source="engine_projection";e.indoor=false;e.mode=3;e.native_display_word=0x08;return e;}
}
int main(int argc,char**argv){try{
    if(argc<2)throw std::runtime_error("original assets required");
    const std::filesystem::path data=argv[1];
    check(!pointer_source_hold_ready(25)&&pointer_source_hold_ready(26),
          "legacy routine requires more than25 BIOS ticks, not20ms delay units");
    check(pointer_hold_source_ticks(1428)==25&&pointer_hold_source_ticks(1429)==26,
          "BIOS tick conversion crosses26 ticks at1429ms with zero phase");
    check(pointer_hold_source_ticks(55)-pointer_hold_source_ticks(54)==1&&pointer_hold_source_ticks(1)==0,
          "subtracting absolute samples retains the Source counter phase");
    check(pointer_hold_source_ticks((1ull<<32)+1429)>pointer_hold_source_ticks(1ull<<32),
          "64bit host sampling does not wrap at SDL32 milliseconds");
    auto s=lineup();auto e=extras();
    PitchScreen pitch(data,s,e);const auto pixels=pitch.render().pixels;
    check(pixels.size()==320*240&&pitch.render().pixels==pixels,"pitch render is deterministic");
    check(pitch.state().players[3].board[0x41]==3&&!pitch.pending(),"render does not mutate authoritative positions");
    const auto idle_pitch=pitch.state_json();
    check(pitch.key("ESC").kind==PitchRequestKind::none&&
          pitch.key("ESCAPE").kind==PitchRequestKind::none&&
          pitch.state_json()==idle_pitch&&pitch.render().pixels==pixels,
          "Original desktop pitch ignores Escape without closing or changing state/pixels");
    auto escape_live_state=s;escape_live_state.in_match=true;PitchScreen live_escape(data,escape_live_state,e);
    live_escape.key("ESC");check(live_escape.close_requested(),
          "existing live-intervention Escape return remains available");
    auto legacy=e;legacy.set_piece_takers={0xffff,0xffff,0xffff};
    auto legacy_lineup=s;legacy_lineup.entry_prepared=false;
    PitchScreen legacy_preview(data,legacy_lineup,legacy);
    check(legacy_preview.render().pixels.size()==320*240&&
          legacy_preview.extras().set_piece_takers==legacy.set_piece_takers&&
          legacy_preview.click(200,180).kind==PitchRequestKind::none&&!legacy_preview.pending(),
          "legacy uninitialized supported edition tail permits read-only preparation but not editor commands");
    auto bad=e;bad.indoor.reset();rejected([&]{PitchScreen unsupported(data,s,bad);});
    auto hall=e;hall.indoor=true;hall.formation_mask=0x01;
    hall.set_piece_signature=0x3253;hall.native_display_word=0x012b;
    PitchScreen schutze(data,s,hall);
    auto open=schutze.click(200,180);
    check(open.kind==PitchRequestKind::open_set_piece_editor&&open.indoor&&
          open.expected_display_word==0x012b&&open.new_display_word==0x018b,
          "indoor Schütze control requests authenticated native-word open");
    auto opened=hall;opened.revision=8;opened.native_display_word=0x018b;
    auto opened_lineup=s;opened_lineup.revision=8;
    schutze.acknowledge(opened_lineup,opened);
    auto zone_lineup=opened_lineup;
    zone_lineup.players[0].board[2]=8;zone_lineup.players[0].board[3]=6;
    zone_lineup.players[0].board[4]=4;
    PitchScreen zone(data,zone_lineup,opened);
    const auto zone_off=zone.render().pixels;
    check(zone.state_json().find("\"zone_cover_open\":false")!=std::string::npos,
          "Source editor entry starts with its scratch coverage bit cleared");
    check(zone.pointer_down(216,180).kind==PitchRequestKind::none&&zone.take_source_down_release_barrier()&&
          zone.state_json().find("\"zone_cover_open\":true")!=std::string::npos&&
          !zone.pending()&&zone.state().revision==8&&zone.extras().native_display_word==0x018b&&
          zone.render().pixels!=zone_off,
          "second Source control-5 click shows ORDER coverage locally without a Native request");
    check(zone.pointer_down(216,180).kind==PitchRequestKind::none&&zone.take_source_down_release_barrier()&&
          zone.state_json().find("\"zone_cover_open\":false")!=std::string::npos&&
          !zone.pending()&&zone.state().revision==8&&zone.extras().native_display_word==0x018b&&
          zone.render().pixels==zone_off,
          "third Source control-5 click restores coverage-off frame without Native writes");
    PitchScreen team_page(schutze);
    const auto team=team_page.click(210,130);
    check(team.kind==PitchRequestKind::select_pitch_page&&team.page_control==1&&
          team.expected_display_word==0x018b&&team.new_display_word==0x0187,
          "Team accepts Source XOR 0x0c for saved display word while bit80 remains owned");
    auto team_word=opened;team_word.revision=9;team_word.native_display_word=team.new_display_word;
    auto team_lineup=s;team_lineup.revision=9;
    team_page.acknowledge(team_lineup,team_word);
    LineupScreen full_team_sheet(data,team_lineup);
    check(team_page.render().pixels==full_team_sheet.render().pixels&&
          team_page.state_json().find("\"shooter_editor_open\":true")!=std::string::npos,
          "Team redraw uses the full owned sheet when source bit8 is clear, retaining private editor state");
    auto info_page=team_page;
    const auto row_info=info_page.click(80,32);
    check(row_info.kind==PitchRequestKind::open_player_info&&row_info.row==0&&
          info_page.pending()&&info_page.pending()->kind==PitchRequestKind::open_player_info,
          "full Team sheet keeps the original right-column player-info row action");
    info_page.resolve_local_navigation();
    check(info_page.click(80,39).kind==PitchRequestKind::open_player_info&&info_page.pending()->row==1,
          "closing one full-sheet player card permits opening the next player's card");
    auto swap_page=team_page;
    check(swap_page.click(20,32).kind==PitchRequestKind::none&&
          swap_page.click(20,39).kind==PitchRequestKind::swap_players&&
          swap_page.pending()->row==0&&swap_page.pending()->second_row==1,
          "full Team sheet keeps the original left-column row selection and swap action");
    swap_page.reject_action("controller rejected swap");
    check(swap_page.click(20,32).kind==PitchRequestKind::none&&
          swap_page.click(20,39).kind==PitchRequestKind::swap_players,
          "a rejected full-sheet swap does not leave its inner team sheet blocked");
    const auto back_to_pitch=team_page.pointer_down(70,220);
    check(back_to_pitch.kind==PitchRequestKind::select_pitch_page&&back_to_pitch.page_control==1&&
          back_to_pitch.new_display_word==0x018b,
          "full-sheet footer routes the accepted page switch back to the pitch view");
    auto pitch_word=team_word;pitch_word.revision=10;pitch_word.native_display_word=back_to_pitch.new_display_word;
    auto pitch_lineup=s;pitch_lineup.revision=10;
    team_page.acknowledge(pitch_lineup,pitch_word);
    check(team_page.render().pixels!=full_team_sheet.render().pixels&&
          team_page.state_json().find("\"shooter_editor_open\":true")!=std::string::npos,
          "bit8 redraw restores the 88-pixel sheet plus pitch/editor panel after ACK");
    auto outer_closed=schutze;outer_closed.click(0,0,true);
    PitchScreen reopened_after_whole_close(data,opened_lineup,opened);
    check(outer_closed.close_requested()&&opened.native_display_word==0x018b&&
          reopened_after_whole_close.state_json().find("\"shooter_editor_open\":true")!=std::string::npos&&
          reopened_after_whole_close.state_json().find("\"shooter_selection\":1")!=std::string::npos&&
          reopened_after_whole_close.click(260,215).kind==PitchRequestKind::auto_set_piece_takers,
          "legacy routine preserves native bit80 and legacy routine reopens the neutral editor on fresh entry");
    schutze.click(260,118);
    PitchScreen selector_copy(schutze);
    check(selector_copy.state_json().find("\"shooter_selection\":2")!=std::string::npos,
          "transactional presenter copy retains selected supported edition action state");
    auto taker=selector_copy.click(20,31);
    check(taker.kind==PitchRequestKind::set_piece_taker&&taker.set_piece_selector==2&&
          taker.row==0&&taker.player_id==0&&taker.slot==0x8000,
          "Freistoß selection binds a healthy marked player row and real player ID");
    auto selected=opened;selected.revision=9;selected.set_piece_takers[0]=1;
    auto selected_lineup=s;selected_lineup.revision=9;
    selector_copy.acknowledge(selected_lineup,selected);
    check(selector_copy.state_json().find("\"shooter_selection\":1")!=std::string::npos,
          "accepted taker write returns the original selector to neutral mode 1");
    auto automatic=selector_copy.click(260,215);
    check(automatic.kind==PitchRequestKind::auto_set_piece_takers&&
          automatic.expected_set_piece_values[0]==1&&automatic.expected_display_word==0x018b,
          "AUTO request binds all current takers and preserves native editor word");
    auto all_auto=selected;all_auto.revision=10;all_auto.set_piece_takers={0,0,0};
    auto auto_lineup=s;auto_lineup.revision=10;
    selector_copy.acknowledge(auto_lineup,all_auto);
    check(selector_copy.render().pixels.size()==320*240&&selector_copy.state_json().find("auto_set_piece_takers")==std::string::npos,
          "acknowledged AUTO renders with no pending action");
    auto outdoor=hall;outdoor.indoor=false;
    PitchScreen outdoor_editor(data,s,outdoor);
    const auto outdoor_open=outdoor_editor.click(210,180);
    check(outdoor_open.kind==PitchRequestKind::open_set_piece_editor&&!outdoor_open.indoor,
          "Source supported edition editor is not restricted to the indoor live mode");
    PitchScreen indoor(data,s,hall);const auto hall_pixels=indoor.render().pixels;
    check(hall_pixels.size()==pixels.size()&&hall_pixels!=pixels&&
          indoor.state_json().find("\"indoor\":true")!=std::string::npos,
          "live indoor projection patches private KB2 work atlas with bounded KB33 surface");
    const auto data_page=indoor.click(239,120);
    check(data_page.kind==PitchRequestKind::select_pitch_page&&indoor.state().view==LineupView::data_b,
          "inclusive Source button border emits a typed Data transition without optimistic view mutation");
    auto data_ack=hall;data_ack.revision=8;data_ack.native_display_word=data_page.new_display_word;
    auto data_lineup=s;data_lineup.revision=8;
    indoor.acknowledge(data_lineup,data_ack);
    check(indoor.state().view==LineupView::data_a,
          "Source Data view changes only after its matching projection and display-word ACK");
    PitchScreen hall_action(data,s,hall);hall_action.click(30,164);
    const auto hall_move=hall_action.click(40,164);
    check(hall_move.kind==PitchRequestKind::move_player&&hall_move.indoor,
          "indoor semantic action captures transient live mode and manager revision");
    auto hall_next=s;hall_next.revision=8;hall_next.players[3].board[0x41]=4;
    auto forged_hall=hall;forged_hall.revision=8;forged_hall.indoor=false;
    const auto before_wrong_mode=hall_action.state_json();
    rejected([&]{hall_action.acknowledge(hall_next,forged_hall);});
    check(hall_action.state_json()==before_wrong_mode,
          "outdoor ACK cannot consume pending indoor action or replace its owner projection");
    hall_next.players[4].board[0x41]=3;forged_hall.indoor=true;
    hall_action.acknowledge(hall_next,forged_hall);
    check(hall_action.state().players[3].board[0x41]==4&&hall_action.extras().indoor==true,
          "only newer same-mode engine projection acknowledges indoor placement");
    bad=e;bad.manager_slot=1;rejected([&]{PitchScreen unsupported(data,s,bad);});bad=e;bad.revision=8;rejected([&]{PitchScreen unsupported(data,s,bad);});
    check(!pitch.move(3,159),"field left bound is exclusive");
    check(pitch.move(30,164)&&pitch.render().pixels!=pixels,"grid based original hover");
    check(pitch.click(30,164).kind==PitchRequestKind::none&&pitch.selected_row()==3,"first click selects exact grid row");
    pitch.move(40,164);check(pitch.state_json().find("\"depth\":4")!=std::string::npos,"ghost follows quantized grid");
    const auto move=pitch.click(40,164);
    check(move.kind==PitchRequestKind::move_player&&move.row==3&&move.player_id==3&&move.slot==0x8003&&
          move.old_depth==3&&move.old_lane==4&&move.new_depth==4&&move.new_lane==4&&move.revision==7,"move command contains optimistic concurrency context");
    check(pitch.state().players[3].board[0x41]==3,"command does not commit preview");
    PitchScreen copied=pitch;
    check(copied.state_json()==pitch.state_json()&&copied.render().pixels==pitch.render().pixels,"copied pending screen owns all local state and assets");
    copied.reject_action("copy only");check(pitch.pending().has_value()&&!copied.pending(),"copy acknowledgement/rejection cannot modify source screen");
    check(!pitch.move(50,164)&&pitch.click(60,164).kind==PitchRequestKind::none&&pitch.key("ESC").kind==PitchRequestKind::none&&!pitch.close_requested(),"pending request blocks repeats and exits");
    rejected([&]{pitch.acknowledge(s,e);});auto n=s;auto ne=e;n.revision=8;ne.revision=8;n.players[3].board[0x41]=4;pitch.acknowledge(n,ne);
    check(!pitch.pending()&&!pitch.selected_row()&&pitch.state().players[3].board[0x41]==4,"only new engine projection commits placement");
    PitchScreen cancel(data,s,e);cancel.click(30,164);cancel.move(300,220);
    check(cancel.state_json().find("\"ghost\":null")!=std::string::npos,"ghost disappears outside pitch");
    const auto selected_pitch=cancel.state_json();const auto selected_pixels=cancel.render().pixels;
    cancel.key("ESC");check(cancel.state_json()==selected_pitch&&cancel.render().pixels==selected_pixels,
          "Escape preserves desktop placement selection; only right click cancels it");
    cancel.click(0,0,true);check(!cancel.selected_row()&&!cancel.close_requested(),"right click cancels local placement");
    cancel.click(0,0,true);check(cancel.close_requested(),"second right click exits");
    PitchScreen refused(data,s,e);refused.click(30,164);refused.click(40,164);refused.reject_action("collision rejected by engine");
    check(!refused.pending()&&refused.render().pixels==pixels,"rejection preserves authoritative frame");
    rejected([&]{refused.reject_action("no pending action");});
    PitchScreen view(data,s,e);const auto view_request=view.click(200,114);
    check(view_request.kind==PitchRequestKind::select_pitch_page&&view_request.page_control==0&&
          view.state().view==LineupView::data_b&&view.pending(),
          "Source Data toggle waits for its authoritative native-word acknowledgement");
    auto view_state=s;auto view_extras=e;view_state.revision=view_extras.revision=8;
    view_extras.native_display_word=view_request.new_display_word;
    view.acknowledge(view_state,view_extras);
    check(view.state().view==LineupView::data_a&&!view.pending(),"acknowledged Data toggle changes view exactly once");
    const auto team_return=view.click(200,127);
    check(team_return.kind==PitchRequestKind::select_pitch_page&&team_return.page_control==1&&
          team_return.new_display_word==uint16_t(view_extras.native_display_word^0x0c)&&
          !view.close_requested(),"TEAM return waits for the authenticated Source word transition");
    auto team_state=view_state;auto team_extras=view_extras;
    team_state.revision=team_extras.revision=9;team_extras.native_display_word=team_return.new_display_word;
    view.acknowledge(team_state,team_extras);
    check(!view.close_requested()&&!view.return_team_requested()&&
          (view.extras().native_display_word&0x08)==0,
          "TEAM ACK keeps the interactive full-sheet Pitch presenter open");
    check(!cancel.return_team_requested(),"right click exit retains distinct desktop target");
    auto empty_aa=e;empty_aa.native_display_word=0x18;
    PitchScreen readonly(data,s,empty_aa);readonly.click(250,115);
    check(!readonly.pending()&&readonly.state_json().find("slot is empty")!=std::string::npos,"empty formation slot cannot run AA");
    rejected([&]{readonly.resolve_local_navigation();});
    PitchScreen scroll(data,s,e);scroll.click(100,80);
    check(scroll.state_json().find("\"scroll_pixels\":7")!=std::string::npos&&!scroll.pending(),"native pitch attribute column scrolls one row");
    scroll.click(100,40);check(scroll.render().pixels==pixels,"scroll returns to authoritative baseline");
    auto unprepared=s;unprepared.entry_prepared=false;PitchScreen blocked(data,unprepared,e);blocked.click(30,164);
    check(!blocked.selected_row()&&!blocked.pending(),"read only FBM cannot claim lineup entry effects");
    check(blocked.state_json().find("not been authoritatively prepared")!=std::string::npos,"invalid action reason is observable");
    blocked.click(250,215);check(!blocked.pending(),"unprepared reference cannot change automatic weights");

    auto aa=e;aa.native_display_word=0x18;aa.formation_mask=0x0f;aa.automatic_weights=0xd6;
    // legacy routine's hidden AA coordinates must never select, store or edit a
    // formation on another page. Exercise real assets, both pitch modes and
    // Modus I, including a retained AA bit underneath the full team sheet.
    for(bool is_hall:{false,true})for(int mode:{1,3})for(uint16_t word:{0x08,0x28,0x48,0x88,0x14}){
        auto hidden=aa;hidden.indoor=is_hall;hidden.mode=mode;
        hidden.native_display_word=word;hidden.set_piece_signature=0x3253;
        PitchScreen hidden_page(data,s,hidden);const auto before=hidden_page.render();
        check(((word&0x08)==0||
               (hidden_page.click(290,120).kind==PitchRequestKind::none&&
                hidden_page.click(241,215).kind==PitchRequestKind::none))&&
              hidden_page.save_formation(250,166).kind==PitchRequestKind::none&&!hidden_page.pending(),
              "inactive Source AA page ignores hidden formation, weight and held-save coordinates");
        if(word==0x08)for(int y=110;y<131;++y)for(int x=243;x<278;++x)
            check(before.pixels[size_t(y)*before.width+x]==0x42,"inactive Source page does not draw the KB2 formation panel");
        check(hidden_page.extras().formations==aa.formations&&
              hidden_page.extras().automatic_weights==aa.automatic_weights,
              "ignored AA input preserves authoritative templates and packed weights");
    }
    auto order_lineup=s;auto order_extras=e;order_extras.native_display_word=0x28;
    order_lineup.players[0].board[2]=12;order_lineup.players[0].board[3]=10;
    order_lineup.players[0].board[4]=20;order_lineup.players[0].board[5]=8;
    PitchScreen orders(data,order_lineup,order_extras);
    auto order_idle=orders.render();
    auto order_atlas=load_vga(asset_path(data/"KB","2.VGA"));
    for(int side=0;side<2;++side)for(int aspect=0;aspect<4;++aspect)
        for(int y=0;y<12;++y)for(int x=0;x<15;++x){
            const auto value=order_atlas.at(196+aspect*16+x,56+side*12+y);
            if(value!=0xc0)check(order_idle.at(243+side*58+x,179+aspect*12+y)==value,
                "Anweis controls use all eight original atlas sprites");
        }
    check(orders.move(20,38),"Anweis accepts hover over the first starter");
    auto order_hover=orders.render();int coverage_changes=0;
    for(int y=111;y<220;++y)for(int x=3;x<193;++x)
        if((order_idle.at(x,y)^order_hover.at(x,y))&0x80)++coverage_changes;
    check(coverage_changes>100&&!orders.pending()&&orders.state().revision==s.revision&&
          orders.state().players[0].board==order_lineup.players[0].board,
          "hover previews the red ORDER coverage without selecting or mutating a player");
    check(orders.move(150,200),"moving over an empty pitch cell updates its position hints");
    auto hints=orders.render();int hint_pixels=0;
    for(int y=159;y<=173;++y)for(int x=241;x<319;++x)
        if(hints.at(x,y)!=order_idle.at(x,y))++hint_pixels;
    check(hint_pixels>0&&!orders.pending(),"Anweis cell hints come from runtime zone masks and texts");
    PitchScreen orders_copy=orders;
    check(orders_copy.render().pixels==hints.pixels,"copying a preview preserves its hovered position hints");
    orders.move(220,100);
    check(orders.render().pixels==order_idle.pixels,"leaving Anweis hover restores the idle page");
    auto cancel_extras=e;cancel_extras.native_display_word=0;
    PitchScreen team_cancel(data,s,cancel_extras);
    team_cancel.click(20,38);
    team_cancel.pointer_down(310,230,true);
    check(!team_cancel.close_requested()&&!team_cancel.pending(),
          "a live-compatible full sheet first cancels its selected player on right press");
    team_cancel.pointer_down(309,229,true);
    check(team_cancel.close_requested(),"the next right press leaves the full sheet");
    auto hall_pages=aa;hall_pages.indoor=true;
    PitchScreen hall_transition(data,s,hall_pages);
    const auto instructions=hall_transition.click(210,154);
    auto hall_sheet=s;hall_sheet.revision=8;hall_pages.revision=8;
    hall_pages.native_display_word=instructions.new_display_word;
    hall_transition.acknowledge(hall_sheet,hall_pages);
    check(instructions.page_control==3&&hall_transition.click(290,120).kind==PitchRequestKind::none&&
          hall_transition.save_formation(250,166).kind==PitchRequestKind::none,
          "confirmed Hall Anweis transition removes hidden AA handlers");
    const auto to_aa=hall_transition.click(210,141);
    hall_sheet.revision=hall_pages.revision=9;hall_pages.native_display_word=to_aa.new_display_word;
    hall_transition.acknowledge(hall_sheet,hall_pages);
    const auto full_team=hall_transition.click(210,128);
    hall_sheet.revision=hall_pages.revision=10;hall_pages.native_display_word=full_team.new_display_word;
    hall_transition.acknowledge(hall_sheet,hall_pages);
    check((hall_pages.native_display_word&0x10)!=0&&
          hall_transition.save_formation(250,166).kind==PitchRequestKind::none,
          "TEAM full sheet cannot save a hidden Hall formation with retained AA bit");
    const auto to_pitch=hall_transition.click(80,219);
    hall_sheet.revision=hall_pages.revision=11;hall_pages.native_display_word=to_pitch.new_display_word;
    hall_transition.acknowledge(hall_sheet,hall_pages);
    const auto restored_weight=hall_transition.click(250,215);
    check(restored_weight.kind==PitchRequestKind::set_automatic_weight&&restored_weight.indoor&&
          restored_weight.revision==11&&restored_weight.old_weights==aa.automatic_weights,
          "confirmed Hall TEAM roundtrip restores only the visible owned AA controls");
    auto live_aa=aa;live_aa.set_piece_signature=0x3253;
    auto live_state=s;live_state.in_match=true;
    PitchScreen live_pitch(data,live_state,live_aa);
    check(live_pitch.click(250,115).kind==PitchRequestKind::none&&
          live_pitch.click(250,215).kind==PitchRequestKind::none&&!live_pitch.pending(),
          "legacy routine live param2 ignores the param2==0 formation and AA controls");
    check(live_pitch.click(20,32).kind==PitchRequestKind::none&&
          live_pitch.click(30,139).kind==PitchRequestKind::move_player&&
          live_pitch.pending()->player_id==0,
          "live pitch still accepts source-allowed player positioning");
    auto live_shooters=live_pitch;
    check(live_shooters.click(200,180).kind==PitchRequestKind::none,
          "pending live placement blocks unrelated pitch controls");
    PitchScreen live_set_pieces(data,live_state,live_aa);
    auto live_editor=live_set_pieces.click(200,180);
    check(live_editor.kind==PitchRequestKind::open_set_piece_editor,
          "live param2 retains Source set-piece editor path");
    PitchScreen live_longpress_screen(data,live_state,live_aa);
    auto live_longpress=live_longpress_screen.save_formation(250,115);
    check(live_longpress.kind==PitchRequestKind::none&&
          live_longpress_screen.state_json().find("formation slots are unavailable during a live intervention")!=std::string::npos,
          "live param2 cannot write persistent formation slots");
    // All three original packed fields wrap independently, retaining high bits.
    for(int index=0;index<3;++index){
        PitchScreen weights(data,s,aa);const auto frame=weights.render().pixels;
        auto request=weights.click(250+27*index,215);
        const auto expected=uint8_t((aa.automatic_weights&~(3<<(2*index)))|
            (((((aa.automatic_weights>>(2*index))&3)+1)&3)<<(2*index)));
        check(request.kind==PitchRequestKind::set_automatic_weight&&request.weight_index==index&&
              request.old_weights==0xd6&&request.new_weights==expected&&request.row==-1&&
              request.manager_slot==0&&request.runtime_club==12&&request.revision==7,
              "AA command has packed byte and manager revision without fake player row");
        check(weights.render().pixels==frame&&weights.extras().automatic_weights==0xd6,
              "pending weight command preserves authoritative renderer");
        const auto waiting_weight=weights.state_json();weights.click(250+27*index,215);weights.key("ESC");
        check(weights.state_json()==waiting_weight,"pending weight blocks repeated clicks and exit");
        auto next=s;auto context=aa;next.revision=context.revision=8;
        rejected([&]{weights.acknowledge(next,context);});
        context.automatic_weights=expected;
        auto mismatched=next;mismatched.players[1].board[0x41]++;
        rejected([&]{weights.acknowledge(mismatched,context);});
        weights.acknowledge(next,context);
        check(weights.extras().automatic_weights==expected&&
              weights.state_json().find("\"automatic_weights_dirty\":true")!=std::string::npos,
              "acknowledged weight keeps AA pending until explicit formation choice");
        // A weight edit permits reapplying the currently selected formation.
        next.automatic_formation=1;PitchScreen clean(data,next,context);
        check(clean.click(250,120).kind==PitchRequestKind::none,"same AA without weight edit is a no-op");
        auto dirty=weights;auto applied=dirty.click(250,120);
        check(applied.kind==PitchRequestKind::apply_formation&&applied.formation==0&&applied.new_weights==expected,
              "formation choice after weight acknowledgement is an explicit separate command");
        auto maximum=aa;maximum.automatic_weights|=uint8_t(3<<(2*index));
        PitchScreen wrap(data,s,maximum);auto wrapped=wrap.click(250+27*index,215);
        check(wrapped.new_weights==uint8_t(maximum.automatic_weights&~(3<<(2*index))),
              "each original 2-bit field wraps 3 to 0 while preserving both other fields and high bits");
    }
    auto active=s;active.automatic_formation=1;
    PitchScreen changed(data,active,aa);changed.click(250,215);
    auto next=active;auto context=aa;next.revision=context.revision=8;context.automatic_weights=0xd7;
    changed.acknowledge(next,context);
    check(changed.click(250,120).kind==PitchRequestKind::apply_formation,"same formation reapplies after a confirmed weight edit");
    next.revision=context.revision=9;changed.acknowledge(next,context);
    check(changed.click(250,120).kind==PitchRequestKind::none,"successful reapply clears dirty weights");
    PitchScreen formation(data,active,aa);const auto frame=formation.render().pixels;
    auto choice=formation.click(280,120);
    check(choice.kind==PitchRequestKind::apply_formation&&choice.formation==1&&
          formation.render().pixels==frame,"right column original x280 boundary issues AA request without selecting locally");
    const auto waiting=formation.state_json();formation.click(200,127);formation.key("ESC");
    check(formation.state_json()==waiting,"AA command blocks exits and duplicate requests");
    next=active;context=aa;next.revision=context.revision=8;
    rejected([&]{formation.acknowledge(next,context);});
    next.automatic_formation=2;auto wrong=context;wrong.formations[2]++;
    rejected([&]{formation.acknowledge(next,wrong);});
    check(formation.state_json()==waiting,"wrong AA acknowledgement preserves every pending field");
    std::swap(next.players[1],next.players[2]);next.players[1].row=1;next.players[2].row=2;
    formation.acknowledge(next,context);
    check(formation.state().automatic_formation==2&&formation.state().players[1].player_id==2,
          "AA acknowledgement accepts engine-reordered players instead of retaining old row identities");
    PitchScreen cancel_aa(data,active,aa);cancel_aa.click(290,120);cancel_aa.reject_action("unsupported engine scoring");
    check(cancel_aa.render().pixels==frame,"AA rejection preserves previous authoritative formation");
    PitchScreen placing(data,active,aa);placing.click(30,164);placing.click(250,215);
    check(placing.selected_row()==3&&!placing.pending(),"selected player blocks AA controls");
    auto mode1=aa;mode1.mode=1;PitchScreen simple(data,active,mode1);
    simple.click(250,215);check(!simple.pending(),"Modus I has no AA weight command");
    check(simple.click(290,120).kind==PitchRequestKind::apply_formation,"Modus I still allows preset AA selection");
    PitchScreen boundaries(data,s,aa);
    check(boundaries.click(279,136).formation==2,"formation uses original 24-pixel hit grid, not drawn 23-pixel spacing");
    PitchScreen edge(data,s,aa);edge.click(319,221);
    check(edge.pending()&&edge.pending()->weight_index==2,"rightmost weight hit stays within packed byte");
    PitchScreen excluded(data,s,aa);excluded.click(320,215);excluded.click(250,222);
    check(!excluded.pending(),"AA bounds exclude outer right and lower edge");
    PitchScreen own(data,s,aa);
    check(!own.formation_save_feedback(false)&&!own.formation_save_feedback(true),
          "idle pitch has no formation-save feedback");
    for(int f=4;f<8;++f){
        PitchScreen saving_slot(data,s,aa);const auto original=saving_slot.render();
        check(saving_slot.save_formation(250+(f%2)*40,166+(f/2-2)*24).formation==f,
              "each own slot retains its Original hit grid");
        const auto pending_json=saving_slot.state_json();
        for(bool restored:{false,true}){
            const auto feedback=saving_slot.formation_save_feedback(restored);
            check(bool(feedback),"pending save exposes each feedback frame");
            const int left=243+(f%2)*38,top=110+(f/2)*23;
            for(int y=0;y<240;++y)for(int x=0;x<320;++x){
                const bool border=x>=left&&x<=left+34&&y>=top&&y<=top+20&&
                    (x==left||x==left+34||y==top||y==top+20);
                const size_t offset=size_t(y)*320+x;
                check(feedback->pixels[offset]==(border?(restored?0x6b:0):original.pixels[offset]),
                      "save feedback changes only the inclusive35x21 Source outline");
            }
        }
        check(saving_slot.state_json()==pending_json&&saving_slot.render().pixels==original.pixels,
              "feedback never commits or alters the ordinary authoritative frame");
        saving_slot.reject_action("refused save");
        check(!saving_slot.formation_save_feedback(false),"rejection ends feedback ownership");
    }
    check(PitchScreen::formation_save_flash_ms==100,"Source legacy routine(5) uses100 host milliseconds");
    auto saved=own.save_formation(250,166);
    check(saved.kind==PitchRequestKind::save_formation&&saved.formation==4&&
          saved.formation_positions[3]==3&&saved.formation_positions[14]==4&&
          own.extras().formation_mask==0xf,"long press copies eleven coordinates without locally saving the slot");
    const auto saving=own.state_json();own.save_formation(290,166);own.key("ESC");
    check(own.state_json()==saving,"pending save blocks repeated long presses and exit");
    auto saved_state=s;auto saved_extras=aa;saved_state.revision=saved_extras.revision=8;
    saved_state.automatic_formation=5;
    rejected([&]{own.acknowledge(saved_state,saved_extras);});
    saved_extras.formation_mask|=0x10;
    std::copy(saved.formation_positions.begin(),saved.formation_positions.end(),saved_extras.formations.begin()+4*22);
    auto corrupted=saved_extras;corrupted.formations[4*22+3]++;
    rejected([&]{own.acknowledge(saved_state,corrupted);});
    corrupted=saved_extras;corrupted.formations[0]++;
    rejected([&]{own.acknowledge(saved_state,corrupted);});
    check(own.state_json()==saving,"wrong template or unrelated preset changes cannot partially commit");
    own.acknowledge(saved_state,saved_extras);
    check(!own.formation_save_feedback(false),"ACK ends the transient save feedback");
    check(own.extras().formation_mask==0x1f&&own.state().automatic_formation==5,
          "successful save acknowledgement installs the complete template and activates AA");
    check(own.save_formation(250,166).kind==PitchRequestKind::save_formation,
          "own slots may be overwritten by a new explicit long press");
    own.reject_action("engine refused save");
    PitchScreen preset(data,s,aa);preset.save_formation(250,120);
    check(!preset.pending()&&preset.state_json().find("four own")!=std::string::npos,
          "built-in presets are protected without explicit overwrite context");
    auto incomplete=s;incomplete.players[10].slot=10;PitchScreen missing(data,incomplete,aa);
    missing.save_formation(250,166);check(!missing.pending(),"formation saving needs all eleven nominated starters");
    blocked.save_formation(250,166);check(!blocked.pending(),"read-only save cannot claim a prepared entry");
    placing.save_formation(250,166);check(!placing.pending(),"player placement blocks formation saving");
    GameState projected;
    projected.memory.add_block(::bmh::layout::ActiveManager,1);projected.memory.add_block(::bmh::layout::ManagerCount,1);projected.memory.add_block(::bmh::layout::GameMode,1);
    projected.memory.set8(::bmh::layout::ActiveManager,1);projected.memory.set8(::bmh::layout::ManagerCount,2);projected.memory.set8(::bmh::layout::GameMode,3);
    projected.manager_teams={12,11};projected.manager_records.resize(2*0xb0c);projected.formations.resize(2*0x160);
    projected.club_heap_78.resize(24*2);projected.manager_records[0xb0c+0x218]=3;projected.manager_records[0xb0c+0x288]=0x36;
    projected.formations[0x160+22+4]=17;projected.club_heap_78[11*2]=9;
    const auto records=projected.manager_records,forms=projected.formations;
    const auto exe=load_le(asset_path(data,"BMH.exe"));auto authoritative=pitch_extras_from_game(projected,exe,18);
    check(authoritative.manager_slot==1&&authoritative.runtime_club==11&&authoritative.revision==18&&authoritative.indoor==false&&
          authoritative.formation_mask==3&&authoritative.automatic_weights==0x36&&authoritative.formations[26]==17&&authoritative.badge==9,
          "typed manager projection respects per-manager template and record strides");
    projected.formations[0x160+26]=19;check(authoritative.formations[26]==17&&projected.manager_records==records,"owned projection with no manager writes");
    projected.formations=forms;projected.indoor=true;projected.manager_records[0xb0c+0x219]=1;projected.formations[0x160+0xb0]=4;
    const auto projected_indoor=pitch_extras_from_game(projected,exe,19);
    check(projected_indoor.indoor==true&&projected_indoor.formation_mask==1&&projected_indoor.formations[0]==4,
          "live indoor projection owns independent +0x219 availability and +0xb0 formation bytes");
    auto marking_state=s;for(auto& player:marking_state.players){player.board[0x44]=0xff;player.board[0x45]=0x0f;}
    auto marking_extras=e;marking_extras.opponent_team=13;marking_extras.opponent_name="TESTCITY";
    for(int row=1;row<=10;++row){PitchOpponent opponent;opponent.row=row;opponent.player_id=30+row;
        opponent.slot=uint16_t(0x8000|opponent.player_id);opponent.name="OPPONENT"+std::to_string(row);
        opponent.position=row%3+1;marking_extras.opponent_players.push_back(opponent);}
    PitchScreen marking(data,marking_state,marking_extras);
    const auto marking_page=marking.click(210,166);
    check(marking_page.kind==PitchRequestKind::select_pitch_page&&marking_page.page_control==4&&
          marking_page.opponent_team==13&&marking_page.new_display_word==0x48,
          "Mannd selects the Source 0x40 page using its authenticated live opponent");
    auto marking_page_state=marking_state;marking_page_state.revision=8;
    auto marking_page_extras=marking_extras;marking_page_extras.revision=8;marking_page_extras.native_display_word=0x48;
    marking.acknowledge(marking_page_state,marking_page_extras);
    check(marking.render().pixels.size()==320*240&&marking.state_json().find("\"man_marking_open\":true")!=std::string::npos,
          "Mannd page renders the opponent roster in the original pitch panel");
    check(marking.click(270,127).kind==PitchRequestKind::none,"opponent target selection remains local until an own marker is chosen");
    PitchScreen marking_copy=marking;
    const auto assignment=marking_copy.click(12,159);
    check(assignment.kind==PitchRequestKind::set_player_marking&&assignment.row==1&&assignment.player_id==1&&
          assignment.opponent_team==13&&assignment.opponent_row==1&&assignment.opponent_player==31&&
          assignment.expected_marking_player_id==0x0fff&&assignment.revision==8,
          "the completed target/marker pair emits a revision-bound Source assignment");
    check(pitch_request_json(assignment).find("\"opponent_slot\":32799")!=std::string::npos,
          "marking request JSON preserves target roster slot identity");
    auto marked_state=marking_page_state;marked_state.revision=9;marked_state.players[1].board[0x44]=31;marked_state.players[1].board[0x45]=0;
    auto marked_extras=marking_page_extras;marked_extras.revision=9;marked_extras.opponent_players[0].current_marker_player_id=1;
    marking_copy.acknowledge(marked_state,marked_extras);
    marking_copy.click(270,127);const auto clear_mark=marking_copy.click(270,127);
    check(clear_mark.kind==PitchRequestKind::set_player_marking&&clear_mark.row==-1&&clear_mark.player_id==-1&&
          clear_mark.opponent_player==31&&clear_mark.expected_marking_player_id==31,
          "re-clicking a selected opponent emits the Source clear request");
    auto cleared_extras=marked_extras;cleared_extras.revision=10;cleared_extras.opponent_players[0].current_marker_player_id=-1;
    auto cleared_state=marked_state;cleared_state.revision=10;cleared_state.players[1].board[0x44]=0xff;cleared_state.players[1].board[0x45]=0x0f;
    marking_copy.acknowledge(cleared_state,cleared_extras);
    check(marking_copy.state().players[1].board[0x44]==0xff&&marking_copy.state().players[1].board[0x45]==0x0f,
          "clear acknowledgement returns to the authoritative unassigned row");
    if(argc>2&&!std::string(argv[2]).empty()){
        const auto bytes=read_file(argv[2]);const auto exe=load_le(asset_path(data,"BMH.exe"));const auto game=load_savegame(argv[2]);
        check(game.checksum_ok&&game.version=="V2.24","reference save container verified");
        SaveState save(game.stream,{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)});
        auto reference=lineup_from_save(save,exe,3);auto context=pitch_extras_from_save(save,exe,3);
        check(context.runtime_club==12&&context.source==reference.source&&!context.indoor,"save does not invent transient indoor context");
        check(context.formation_mask==0x0f,"four original preset formations enabled");context.indoor=false;
        PitchScreen original(data,reference,context);check(original.render().pixels.size()==320*240,"original pitch raster renders from owned save projection");
        check(read_file(argv[2])==bytes,"original save unchanged");
    }
    std::cout<<"pitch tests passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
