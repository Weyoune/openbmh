// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/balance_plan.hpp"
#include "ui/setup.hpp"

#include <cstdio>
#include <algorithm>

using namespace bmh;
using namespace bmh::ui;
namespace { int failures=0; void check(bool ok,const char* m){if(!ok){std::fprintf(stderr,"FAIL: %s\n",m);++failures;}} }

static BalanceState fixture(){
 BalanceState s;s.identity.manager_slot=0;s.identity.runtime_club=1;s.identity.club_identity=1;s.identity.revision=7;s.identity.scope=10;s.identity.sequence=2;
 s.draft.manager=0;s.draft.view=BalanceView::future;s.draft.rows[0]=5000;s.draft.rows[7]=1200;s.draft.total=3800;s.underlay=Image(320,240,0x22);s.club_name="UNITFC";
 for(int i=0;i<kBalanceRows;++i){auto& p=s.calculators[size_t(i)];p.supported=true;p.minimum=0;p.maximum=i==20?100000:(i==19?25:30000000);p.initial=uint32_t(std::clamp(s.draft.rows[size_t(i)],0,int32_t(p.maximum)));p.source_text_address=::bmh::layout::BalancePlan_BalanceFromGame_ExecutableField;p.presets[0]=p.maximum/10;p.presets[1]=p.maximum/10+p.maximum/5;p.presets[6]=0;p.presets[7]=p.maximum;}
 s.read_only_notice.source_manager_flags=0x40;s.read_only_notice.body="Bitte umstellen";s.close_notice.source_manager_flags=0x40;s.close_notice.body="DFB warning";return s;
}
int main(int argc,char** argv){
 if(argc!=2)return 2;
 try{
  GameState raw;raw.manager_teams={1};raw.club_ids={0,0};raw.club_names={'F','C',0};raw.club_heap_78.assign(336,0);raw.manager_records.assign(0xb0c,0);raw.manager_records[0xa8]='M';raw.manager_records[0xa9]=0;raw.manager_records[0x5c]=10;
  raw.memory.add_block(::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField2,1);raw.memory.add_block(::bmh::layout::CompactDialogFrame,1);raw.memory.add_block(::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField,1);raw.memory.set8(::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField2,0);raw.memory.set8(::bmh::layout::CompactDialogFrame,0);raw.memory.set8(::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField,0);
  BalancePlanDraft profile_draft;profile_draft.manager=0;profile_draft.view=BalanceView::future;profile_draft.rows[20]=12345;
  BalanceIdentity profile_id;profile_id.manager_slot=0;profile_id.runtime_club=1;
  const auto data=std::filesystem::path(argv[1]);const auto exe=load_le(asset_path(data,"BMH.exe"));
  auto projected=balance_from_game(raw,profile_draft,exe,profile_id,Image(320,240,0));
  check(projected.calculators[0].maximum==30000000&&projected.calculators[0].presets[0]==3000000&&projected.calculators[0].presets[5]==std::nullopt,"ordinary Source calculator uses true maximum and hides overrun preset");
  check(projected.calculators[19].maximum==25&&projected.calculators[19].presets[0]==2&&projected.calculators[19].presets[4]==22&&!projected.calculators[19].presets[5],"25-unit row keeps its integer preset progression and endpoint slots");
  check(projected.calculators[20].maximum==100000&&projected.calculators[20].presets[0]==9000&&projected.calculators[20].presets[5]==14000,"nonzero average gate uses Source adaptive decimal presets");
  check(!projected.calculators[18].supported&&projected.read_only_notice_enabled&&projected.read_only_notice.source_manager_flags==0x40,"readonly row and legacy routine timer-gate projection");
  profile_draft.rows[20]=0;auto zero_gate=balance_from_game(raw,profile_draft,exe,profile_id,Image(320,240,0));
  check(zero_gate.calculators[20].presets[0]==4000&&zero_gate.calculators[20].presets[5]==9000,"zero average gate presets use manager attendance times 400 then 100");
  raw.memory.set8(::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField,1);auto suppressed=balance_from_game(raw,profile_draft,exe,profile_id,Image(320,240,0));check(!suppressed.read_only_notice_enabled,"Source disabled notice wait does not create a local modal");
  auto s=fixture();BalanceScreen screen(argv[1],s);auto first=screen.render();check(first.width==320&&first.height==240,"renders the original room canvas");check(first.pixels==screen.render().pixels,"render is repeatable");
  {
   BalanceScreen timed(data,fixture());timed.pointer_down(0,38);timed.text("123");
   const auto flashing=timed.state_json();check(!timed.process_timer_expired()&&timed.state_json()==flashing,"expiry retains the calculator key flash");
   timed.tick_source_milliseconds(80);
   check(timed.process_timer_expired()&&!timed.calculator_open()&&!timed.pending()&&timed.state().draft.rows[0]==5000,"expiry cancels unconfirmed calculator digits without an edit request");
   check(timed.process_timer_expired()&&timed.pending()&&timed.pending()->kind==BalanceAction::close,"next outer poll emits the normal draft close");
   const auto closing=timed.state_json();check(!timed.process_timer_expired()&&timed.state_json()==closing,"expiry cannot replace a pending draft close");
   BalanceScreen accepted(data,fixture());accepted.pointer_down(0,38);accepted.key("RETURN");
   check(!accepted.process_timer_expired(),"expiry cannot cancel the deferred accepted calculator");accepted.tick_source_milliseconds(80);
   const auto edit=accepted.state_json();check(accepted.pending()&&!accepted.process_timer_expired()&&accepted.state_json()==edit,"accepted edit keeps its typed ACK");
   BalanceScreen held(data,fixture());held.pointer_down(200,219);
   check(!held.process_timer_expired(),"expiry preserves the held view selector");held.release(200,219);
   check(held.pending()&&held.pending()->kind==BalanceAction::cycle_view&&!held.process_timer_expired(),"held selector releases its own cycle request");
   auto notice=fixture();notice.draft.view=BalanceView::so_far;BalanceScreen waiting(data,notice);waiting.pointer_down(0,38);waiting.mark_presented_source_milliseconds(100);
   check(waiting.notice_open()&&!waiting.process_timer_expired(),"expiry retains the Original read-only notice wait");waiting.tick_source_milliseconds(4300);
   check(waiting.process_timer_expired()&&waiting.pending()->kind==BalanceAction::close,"notice completion resumes the outer expiry poll");
   auto warning=fixture();warning.pending_close=true;BalanceScreen staged(data,warning);staged.mark_presented_source_milliseconds(100);
   check(staged.notice_open()&&!staged.process_timer_expired(),"expiry leaves the staged DFB warning with its Source wait");staged.tick_source_milliseconds(4300);
   check(staged.pending()&&staged.pending()->kind==BalanceAction::confirm_close&&!staged.process_timer_expired(),"DFB warning completes through its existing publish ACK");
   BalanceScreen cancel_wait(data,fixture());cancel_wait.pointer_down(0,38);
   cancel_wait.pointer_down(100+exe.u16(::bmh::layout::Advertising_Click_RuntimeField+14*2)+1,80+exe.u8(::bmh::layout::Advertising_Click_RuntimeField2+14)+1);
   check(!cancel_wait.process_timer_expired(),"expiry preserves the calculator N-key wait");
   cancel_wait.mark_presented_source_milliseconds(100);cancel_wait.tick_source_milliseconds(1099);
   check(!cancel_wait.process_timer_expired(),"N-key cancellation owns its full Source one-second wait");cancel_wait.tick_source_milliseconds(1100);
   check(cancel_wait.process_timer_expired()&&cancel_wait.pending()->kind==BalanceAction::close,"N-key wait completion resumes the close owner");
   BalanceScreen drag(data,fixture());drag.pointer_down(0,38);drag.pointer_down(105,85);
   check(!drag.process_timer_expired(),"expiry leaves calculator dragging with its physical release");drag.release(105,85);
   check(drag.process_timer_expired()&&!drag.calculator_open(),"released calculator can cancel on the next expiry poll");
  }
  check(!screen.pointer_down(0,37),"row top outside source hitbox is ignored");
  check(screen.pointer_down(0,38)&&screen.calculator_open(),"inclusive first income hitbox opens KB6 calculator");
  check(screen.key("SPACE"),"Source Space clears calculator");screen.tick_source_milliseconds(80);check(screen.text("9999999999"),"numeric input clamps at the true row maximum after Source flash");check(screen.state_json().find("\"calculator_open\":true")!=std::string::npos,"calculator state is observable");
  check(!screen.key("SPACE"),"next key is blocked during Source key flash");screen.tick_source_milliseconds(160);check(screen.key("SPACE"),"Space clears after the prior key flash");screen.tick_source_milliseconds(240);check(screen.key("RETURN")&&!screen.pending(),"Return starts an 80 ms pressed phase before commit");screen.tick_source_milliseconds(319);check(!screen.pending(),"typed accept remains deferred before 80 ms");screen.tick_source_milliseconds(320);auto req=screen.pending();check(req&&req->kind==BalanceAction::edit_row&&req->row==0&&req->value==0&&req->identity.scope==10,"typed edit commits after Source flash with row, value and owner identity");
  screen.reject_action("reproject");check(!screen.pending(),"reprojection clears the prior request");
  BalanceScreen append(argv[1],fixture());append.pointer_down(0,38);check(append.text("5")&&!append.key("RETURN"),"Source calculator appends digits and blocks Return during key flash");append.tick_source_milliseconds(80);check(append.key("RETURN")&&!append.pending(),"keyboard Return defers request through its flash");append.tick_source_milliseconds(160);check(append.pending()&&append.pending()->value==50005,"Source calculator appends digits to initial value and commits after 80 ms");
  auto right=fixture();right.calculators[20].presets[0]=9000;BalanceScreen gate(argv[1],right);gate.pointer_down(319,172);check(gate.calculator_open(),"inclusive right-panel expense/summary geometry reaches row 20");gate.key("ESC");gate.pointer_down(233,224);check(!gate.pending(),"bottom-left button waits for release");gate.move(240,230);gate.move(233,224);gate.release(233,224);check(!gate.pending(),"leaving button once latches cancel through re-entry");gate.pointer_down(233,224);gate.release(233,224);check(gate.pending()&&gate.pending()->kind==BalanceAction::cycle_view,"inclusive bottom-left release emits cycle");
  auto view0=fixture();view0.draft.view=BalanceView::so_far;BalanceScreen read_only(argv[1],view0);read_only.pointer_down(3,38);check(read_only.notice_open()&&!read_only.pending()&&!read_only.calculator_open(),"so-far editable click shows a read-only notice without owner mutation");check(!read_only.key("SPACE")&&read_only.notice_open(),"non-Source key does not dismiss the Source notice");read_only.pointer_down(3,38);check(!read_only.notice_open()&&!read_only.pending(),"Source click dismisses local read-only notice");
  BalanceScreen digit8(argv[1],fixture());digit8.pointer_down(0,38);check(digit8.key("8")&&!digit8.key("RETURN"),"physical digit 8 edits calculator value instead of acting as Escape and starts a flash");digit8.tick_source_milliseconds(80);check(digit8.key("RETURN")&&!digit8.pending(),"Return after digit flash defers accept");digit8.tick_source_milliseconds(160);check(digit8.pending()&&digit8.pending()->value==50008,"physical digit 8 remains a typed value");
  auto plain=view0;plain.decorated=false;plain.gradient=false;BalanceScreen styled_notice(argv[1],view0),plain_notice(argv[1],plain);styled_notice.pointer_down(3,38);plain_notice.pointer_down(3,38);check(plain_notice.render().pixels!=styled_notice.render().pixels,"tax notice respects resolved undecorated and disabled-gradient flags");
  auto closing=fixture();closing.pending_close=true;BalanceScreen warning(argv[1],closing);check(warning.notice_open(),"pending close starts the timed Source DFB notice");warning.mark_presented_source_milliseconds(1000);check(!warning.tick_source_milliseconds(5199),"warning waits for Source 4.2 second timeout");check(warning.tick_source_milliseconds(5200)&&warning.pending()&&warning.pending()->kind==BalanceAction::confirm_close,"timeout sends typed confirmation only after warning");
  warning.reject_action("next state");check(warning.notice_open(),"failed confirm restores the timed close warning for retry");warning.key("ESC");check(warning.pending()&&warning.pending()->kind==BalanceAction::confirm_close,"Source escape retries the typed close confirmation");
  BalanceScreen drag(argv[1],fixture());drag.pointer_down(0,38);drag.release(0,38);drag.pointer_down(105,81);drag.move(1000,1000);drag.release(1000,1000);check(drag.state().calculator_x==207&&drag.state().calculator_y==104,"calculator drag clamps to Source maximum origin");
  BalanceScreen flash(argv[1],fixture());flash.tick_source_milliseconds(1000);flash.pointer_down(0,38);const int dx=flash.state().calculator_x+exe.u16(::bmh::layout::Advertising_Click_RuntimeField)+16,dy=flash.state().calculator_y+exe.u8(::bmh::layout::Advertising_Click_RuntimeField2)+14;flash.pointer_down(dx,dy);flash.release(dx,dy);check(!flash.tick_source_milliseconds(1079)&&flash.tick_source_milliseconds(1080),"calculator key redraw flash expires after 80 ms");
  BalanceScreen mouse_accept(argv[1],fixture());mouse_accept.tick_source_milliseconds(1000);mouse_accept.pointer_down(0,38);const int jx=mouse_accept.state().calculator_x+exe.u16(::bmh::layout::Advertising_Click_RuntimeField+13*2)+16,jy=mouse_accept.state().calculator_y+exe.u8(::bmh::layout::Advertising_Click_RuntimeField2+13)+14;mouse_accept.pointer_down(jx,jy);check(!mouse_accept.pending()&&mouse_accept.calculator_open(),"mouse J does not dispatch before its pressed frame");mouse_accept.release(jx,jy);mouse_accept.tick_source_milliseconds(1079);check(!mouse_accept.pending(),"mouse J stays deferred at 79 ms");mouse_accept.tick_source_milliseconds(1080);check(mouse_accept.pending()&&mouse_accept.pending()->kind==BalanceAction::edit_row,"mouse J dispatches after its 80 ms flash");
  BalanceScreen leave(argv[1],fixture());leave.key("8");check(!leave.pending(),"literal key 8 is not an Escape alias");leave.key("ESC");check(leave.pending()&&leave.pending()->kind==BalanceAction::close,"Escape requests close through typed owner path");
  BalanceScreen cancel(argv[1],fixture());cancel.tick_source_milliseconds(10000);cancel.pointer_down(0,38);const int nx=cancel.state().calculator_x+exe.u16(::bmh::layout::Advertising_Click_RuntimeField+14*2)+16,ny=cancel.state().calculator_y+exe.u8(::bmh::layout::Advertising_Click_RuntimeField2+14)+14;check(cancel.pointer_down(nx,ny)&&!cancel.calculator_open(),"inclusive Source N hitbox starts mouse-cancel delay");cancel.mark_presented_source_milliseconds(10000);check(!cancel.pointer_down(10,50)&&!cancel.key("ESC"),"Source N wait blocks further input until the restored frame timer expires");cancel.tick_source_milliseconds(10080);check(!cancel.tick_source_milliseconds(10999)&&cancel.tick_source_milliseconds(11000),"mouse N waits one Source second after presentation");
 }catch(const std::exception& e){std::fprintf(stderr,"EXCEPTION: %s\n",e.what());++failures;}
 return failures?1:0;
}
