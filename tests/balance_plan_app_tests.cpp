// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <memory>
#include <limits>
#include "app/controller.hpp"
#include "app/career_session.hpp"
#include "game/balance_plan.hpp"
#include "game/save.hpp"
#include "ui/balance_plan.hpp"

using namespace bmh;
namespace fs=std::filesystem;
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static void immediate_close(const fs::path& data,const fs::path& save){
 app::CareerSession session(data,911,save);uint64_t revision=23;std::string error;
 const int manager=session.state().memory.u8(::bmh::layout::ActiveManager);auto& records=session.state().manager_records;
 const size_t account=size_t(manager)*0xb0c+0x314;
 for(unsigned i=0;i<4;++i)records[account+i]=uint8_t(uint32_t(std::numeric_limits<int32_t>::min())>>(i*8));
 auto checkpoint=session.balance_checkpoint();
 const auto set_gate=[&](uint32_t address,uint8_t value){
  if(checkpoint.state.memory.has(address))checkpoint.state.memory.set8(address,value);
  else checkpoint.runtime[address]=value;
 };
 set_gate(::bmh::layout::CompactDialogFrame,0);set_gate(::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField2,1);set_gate(::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField,1);
 session.restore_balance_checkpoint(std::move(checkpoint));
 const Image underlay(320,240,0);
 auto page=session.open_balance_plan({ui::DeskRequestKind::open_menu,manager,-1,28,-1,revision},
  revision,underlay,error);
 check(page.has_value(),error.empty()?"notice-skip entry succeeds":error.c_str());
 const auto before=session.balance_checkpoint();auto expected=before.draft->state;
 bmh::close_balance_plan(expected,page->draft);
 auto result=session.dispatch_balance_plan({page->identity,ui::BalanceAction::close,-1,0},revision,error);
 check(result&&result->closed&&!result->page.pending_close,"Source notice gate skips timed warning and commits immediately");
 check(result->page.identity.revision==24&&result->page.identity.sequence==page->identity.sequence+1&&revision==24&&
  save_stream(session.state())==save_stream(expected),"immediate close advances revision and owner sequence exactly once");
}
static void actual_controller(const fs::path& data){
 ui::StartupFlow flow(data);flow.click(150,215);flow.click(5,225,true);flow.click(5,225,true);
 auto start=flow.click(90,169);std::unique_ptr<app::CareerSession> session;uint64_t revision=0;
 openbmh::audio::Playback audio;audio.set_muted(true);std::optional<uint64_t> audio_session;
 auto serve=[&](const ui::FlowResult& request){return app::serve_flow_request(flow,request,session,data,data,
  0x1789abcd,revision,audio,audio_session,false);};
 check(serve(start).empty()&&session&&flow.desktop(),"real career reaches the accepted desk through controller");
 const int manager=flow.desktop()->state().manager_slot;
 auto& records=session->state().manager_records;const size_t account=size_t(manager)*0xb0c+0x314;
 check(account+4<=records.size(),"career contains manager account record");
 for(unsigned i=0;i<4;++i)records[account+i]=uint8_t(uint32_t(std::numeric_limits<int32_t>::min())>>(i*8));
 auto menu=[&](){const auto& items=flow.desktop()->state().items;
  const auto it=std::find_if(items.begin(),items.end(),[](const auto& item){return item.kind==28&&!item.disabled;});
  check(it!=items.end(),"actual desk exposes Balance menu 28");
  return flow.click(it->x+it->width/2,it->y+it->height/2);};
 auto open=menu();check(open.request==ui::FlowRequest::desk_action,"menu 28 creates a pending desk action");
 const auto before_open=session->balance_checkpoint();std::string error;
 auto projected=session->open_balance_plan(open.desk_action,revision,flow.desktop()->render(),error);
 check(projected.has_value(),error.empty()?"preflight opening for ACK rollback":error.c_str());
 auto corrupt=*projected;corrupt.identity.scope=0;
 bool rejected=false;try{flow.show_balance(std::move(corrupt),open.desk_action);}catch(const std::exception&){rejected=true;}
 check(rejected,"malformed UI opening ACK is rejected");
 session->restore_balance_checkpoint(before_open);
 check(!session->balance_active()&&session->club_modal_runtime_checkpoint()==before_open.runtime&&
  session->balance_checkpoint().next_scope==before_open.next_scope&&revision==open.desk_action.revision,
  "failed opening ACK rolls back scope and a4856 runtime publication");
 const auto retry_open_error=serve(open);
 check(retry_open_error.empty()&&flow.in_balance(),retry_open_error.empty()?"same pending menu retries after opening ACK rollback":retry_open_error.c_str());
 const auto accepted_native=save_stream(session->state());const auto accepted_runtime=session->club_modal_runtime_checkpoint();
 ui::FlowResult foreign{};foreign.request=ui::FlowRequest::desk_action;
 check(!serve(foreign).empty()&&flow.in_balance()&&session->balance_active()&&
  save_stream(session->state())==accepted_native&&session->club_modal_runtime_checkpoint()==accepted_runtime,
  "cross-family session requests are blocked while the Balance owner is open");
 const auto G=session->game();const auto W=session->rand();const auto accepted_revision=revision;
 auto page_id=flow.balance()->state().identity;
 if(flow.balance()->state().draft.view==BalanceView::so_far){
  flow.pointer_down(171,215);const auto make_editable=flow.click(171,215);
  check(make_editable.request==ui::FlowRequest::balance_action&&serve(make_editable).empty(),
   "read-only Balance view cycles through typed controller action");
 }
 page_id=flow.balance()->state().identity;
 flow.pointer_down(5,40);
 flow.pointer_down(105,81);
 flow.move_action(155,95);
 const auto moved=flow.click(155,95);
 check(moved.request==ui::FlowRequest::none&&flow.balance()->state().calculator_x==150&&
  flow.balance()->state().calculator_y==94&&save_stream(session->state())==accepted_native&&
  session->game().seed()==G.seed()&&session->rand().seed()==W.seed(),
  "calculator drag is local UI state and preserves Native/RNG");
 flow.send_key("ESC");
 flow.pointer_down(171,215);
 const auto cycle=flow.hold(171,215,2000);
 check(cycle.request==ui::FlowRequest::balance_action&&serve(cycle).empty(),"long-held Balance button releases through controller action");
 check(save_stream(session->state())==accepted_native&&session->club_modal_runtime_checkpoint()==accepted_runtime&&
  session->game().seed()==G.seed()&&session->game().calls()==G.calls()&&session->rand().seed()==W.seed()&&
  session->rand().calls()==W.calls()&&revision==accepted_revision&&flow.balance()->state().identity.sequence==page_id.sequence+1&&
  flow.balance()->state().calculator_x==150&&flow.balance()->state().calculator_y==94,
  "actual controller preview changes only the owner sequence and retains dragged calculator position");
 auto close=flow.pointer_down(0,0,true);
 check(close.request==ui::FlowRequest::balance_action&&serve(close).empty(),"actual close enters controller path");
 check(flow.in_balance()&&flow.balance()->state().pending_close&&save_stream(session->state())==accepted_native&&
  session->club_modal_runtime_checkpoint()==accepted_runtime&&revision==accepted_revision,
  "timed warning holds live Native/runtime/revision until confirmation");
 auto dismiss=flow.pointer_down(1,1);
 check(dismiss.request==ui::FlowRequest::balance_action,"left press produces typed warning confirmation");
 const auto close_checkpoint=session->balance_checkpoint();const auto close_revision=revision;
 auto committed=session->dispatch_balance_plan(dismiss.balance_action,revision,error);
 check(committed&&committed->closed,"staged warning confirmation commits Core close");
 auto malformed=*committed;malformed.page.identity.sequence++;
 rejected=false;try{flow.close_balance(std::move(malformed.page),std::move(malformed.preparation));}
 catch(const std::exception&){rejected=true;}
 check(rejected,"malformed post-commit close ACK is rejected");
 session->restore_balance_checkpoint(close_checkpoint);revision=close_revision;
 flow.reject_balance("simulated close projection acknowledgement fault");
 check(flow.in_balance()&&flow.balance()->state().pending_close&&save_stream(session->state())==accepted_native&&
  session->club_modal_runtime_checkpoint()==accepted_runtime&&revision==accepted_revision,
  "post-commit ACK failure restores Native/runtime/revision and retains warning for retry");
 auto retry=flow.pointer_down(1,1);
 check(retry.request==ui::FlowRequest::balance_action&&serve(retry).empty()&&!flow.in_balance()&&revision==accepted_revision+1,
  "retained warning retries and commits exactly once");
 const auto orphan_release=flow.click(1,1);
 check(orphan_release.request==ui::FlowRequest::none,"warning dismissal mouse-up cannot leak into parent desk");
}
static bool same_draft(const BalancePlanDraft& a,const BalancePlanDraft& b){
 return a.manager==b.manager&&a.view==b.view&&a.rows==b.rows&&a.adjustments==b.adjustments&&
  a.refresh==b.refresh&&a.months_left==b.months_left&&a.days_left==b.days_left&&
  a.depreciation==b.depreciation&&a.total==b.total;
}
int main(int argc,char** argv){
 try{
  if(argc<3)throw std::runtime_error("usage: balance_plan_app_tests DATA SAVE");
  const fs::path data=argv[1],save=argv[2];
  immediate_close(data,save);
  actual_controller(data);
  app::CareerSession session(data,731,save);uint64_t revision=17;std::string error;
  const auto& live=session.state();
  const int manager=live.memory.u8(::bmh::layout::ActiveManager);
  auto& manager_records=session.state().manager_records;
  const size_t account_at=size_t(manager)*0xb0c+0x314;
  check(account_at+4<=manager_records.size(),"fixture has a complete Source manager account record");
  const uint32_t forced_balance=uint32_t(std::numeric_limits<int32_t>::min());
  for(unsigned i=0;i<4;++i)manager_records[account_at+i]=uint8_t(forced_balance>>(i*8));
  const ui::DeskRequest entry{ui::DeskRequestKind::open_menu,manager,-1,28,-1,revision};
  const Image underlay(320,240,0x1a);
  const auto native=save_stream(live);const auto runtime=session.club_modal_runtime_checkpoint();
  const auto G=session.game();const auto W=session.rand();
  auto page=session.open_balance_plan(entry,revision,underlay,error);
  check(bool(page),error.empty()?"actual menu 28 owner opens":error.c_str());
  check(page->underlay.pixels==underlay.pixels&&page->identity.manager_slot==manager&&
   page->identity.revision==revision&&page->identity.sequence==0&&page->identity.scope>0,
   "opening binds the exact request, captured underlay and scoped sequence");
  check(same_draft(page->draft,bmh::open_balance_plan(live,manager)),"SDL projection is the Core preview");
  const int32_t initial_price=page->draft.rows[kBalanceTicketPrice];
  auto accepted_runtime=runtime;
  if(!live.memory.has(::bmh::layout::PlanTicketPrice+uint32_t(manager)))
   accepted_runtime[::bmh::layout::PlanTicketPrice+uint32_t(manager)]=uint8_t(page->draft.rows[kBalanceTicketPrice]);
  check(session.club_modal_runtime_checkpoint()==accepted_runtime,
   "accepted open publishes the a4856 fallback to live runtime only");
  check(session.balance_checkpoint().draft->original_runtime==accepted_runtime,
   "stale-owner guard snapshots the accepted runtime cache after open");
  auto stable=[&]{return save_stream(session.state())==native&&session.club_modal_runtime_checkpoint()==accepted_runtime&&
   session.game().seed()==G.seed()&&session.game().calls()==G.calls()&&session.rand().seed()==W.seed()&&
   session.rand().calls()==W.calls()&&revision==17;};
  check(stable(),"opening leaves Native/RNG/runtime/revision unchanged");
  const auto open_id=page->identity;
  const auto expected_view=BalanceView((int(page->draft.view)+1)%3);
  auto first=session.dispatch_balance_plan({open_id,ui::BalanceAction::cycle_view,-1,0},revision,error);
  check(first&&!first->closed&&first->page.identity.sequence==1&&first->page.identity.revision==revision&&
   first->page.draft.view==expected_view&&stable(),"view cycle stays staged and increments only owner sequence");
  page=first->page;
  check(!session.dispatch_balance_plan({open_id,ui::BalanceAction::take_actuals,-1,0},revision,error)&&stable(),
   "replayed request is rejected without changing live state");
  check(!session.open_balance_plan(entry,revision,underlay,error)&&stable(),
   "nested Balance opening is rejected while the accepted owner holds the session");
  const int32_t maximum=balance_row_maximum(page->draft,0);
  check(maximum>0,"fixture exposes an editable balance row");
  const auto edit_request=ui::BalanceRequest{page->identity,ui::BalanceAction::edit_row,0,maximum/2};
  auto edited=session.dispatch_balance_plan(edit_request,revision,error);
  check(edited&&!edited->closed&&edited->page.identity.sequence==2&&stable(),
   "accepted numeric edit changes the staged Core draft only");
  page=edited->page;
  const int32_t ticket_price=initial_price==25?12:25;
  auto ticket=session.dispatch_balance_plan({page->identity,ui::BalanceAction::edit_row,
   kBalanceTicketPrice,ticket_price},revision,error);
  check(ticket&&!ticket->closed&&ticket->page.draft.rows[kBalanceTicketPrice]==ticket_price&&stable(),
   "ticket price is edited in the staged Source draft");
  page=ticket->page;
  auto close=session.balance_checkpoint();
  auto expected=close.draft->state;
  bmh::close_balance_plan(expected,page->draft);
  auto closed=session.dispatch_balance_plan({page->identity,ui::BalanceAction::close,-1,0},revision,error);
  check(bool(closed),error.empty()?"close action accepted":error.c_str());
  check(!closed->closed&&closed->page.pending_close,
   "forced low balance enters the Source timed warning phase before Native publication");
  if(!closed->closed){
   check(closed->page.pending_close&&stable(),"DFB warning keeps the once-closed Native state staged");
   const auto pending_warning=session.balance_checkpoint();
   check(!session.dispatch_balance_plan({closed->page.identity,ui::BalanceAction::confirm_close,0,0},revision,error),
    "malformed DFB confirmation is rejected");
   session.restore_balance_checkpoint(pending_warning);
   check(stable()&&session.balance_checkpoint().draft->page.pending_close,
    "malformed warning confirmation leaves the original warning retryable");
   auto confirmed=session.dispatch_balance_plan({closed->page.identity,ui::BalanceAction::confirm_close,-1,0},revision,error);
   check(confirmed&&confirmed->closed,"timed warning confirmation publishes the staged close");
   closed=std::move(confirmed);
  }
  check(revision==18&&save_stream(session.state())==save_stream(expected),
   "close publishes the exact Core write set once and advances revision once");
  check(!session.balance_active(),"closed Balance owner releases its scope");
  if(!session.state().memory.has(::bmh::layout::PlanTicketPrice+uint32_t(manager))){
   const auto runtime_after=session.club_modal_runtime_checkpoint();
   check(runtime_after.at(::bmh::layout::PlanTicketPrice+uint32_t(manager))==uint8_t(ticket_price),
    "format field ticket cache remains in checkpointed runtime, outside serialized Native");
   const ui::DeskRequest reopen{ui::DeskRequestKind::open_menu,manager,-1,28,-1,revision};
   auto again=session.open_balance_plan(reopen,revision,underlay,error);
   check(again&&again->draft.rows[kBalanceTicketPrice]==ticket_price,
    "same-process reopen uses the staged runtime ticket cache");
  }
  app::CareerSession fresh(data,731,save);const auto fresh_native=save_stream(fresh.state());
  const auto fresh_manager=fresh.state().memory.u8(::bmh::layout::ActiveManager);
  auto fresh_page=fresh.open_balance_plan({ui::DeskRequestKind::open_menu,int(fresh_manager),-1,28,-1,revision},
   revision,underlay,error);
  check(fresh_page&&fresh_page->draft.rows[kBalanceTicketPrice]==initial_price&&save_stream(fresh.state())==fresh_native,
   "fresh session falls back to the saved manager ticket price and leaves Native unchanged");
  std::cout<<"balance app owner, staged preview, replay rejection and close write set passed\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
