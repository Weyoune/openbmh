// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include "app/career_session.hpp"
#include "game/messages.hpp"
#include "game/save.hpp"
#include "ui/pending_offer.hpp"
using namespace bmh;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
constexpr ClubPlayerDetailSelection selection{33,14,5,5,-1};
struct Snapshot { Bytes native; uint32_t w; int32_t g; uint64_t wc,gc; };
Snapshot snap(app::CareerSession& s){return {save_stream(s.state()),s.rand().seed(),s.game().seed(),s.rand().calls(),s.game().calls()};}
void same(app::CareerSession& s,const Snapshot& a){check(save_stream(s.state())==a.native&&s.rand().seed()==a.w&&s.game().seed()==a.g&&s.rand().calls()==a.wc&&s.game().calls()==a.gc,"rollback retains whole native state and both RNGs");}
void duplicate(GameState& g,int raw,int player,uint32_t word){
 const uint32_t from=::bmh::layout::EditorList+40*0x58, to=from+uint32_t(raw)*0x58;
 for(uint32_t i=0;i<0x58;++i)g.memory.set8(to+i,g.memory.u8(from+i));
 g.memory.set32(::bmh::layout::OfferSlots+uint32_t(raw)*4,word);g.squad_slots.at(0xb70+40+size_t(raw))=uint16_t(player);
}
}
int main(int argc,char** argv){try{
 check(argc==3,"usage: pending_offer_app_tests DATA INPUT.FBM");
 if(!*argv[1]){std::cout<<"pending offer app tests: skipped (Original data not configured)\n";return 77;}
 const std::filesystem::path data=argv[1],input=argv[2];
 // legacy routine(4) system identity for the last-week countdown uses the embedded
 // format field title and both Source portrait-14 fallback tiles.
 {app::CareerSession s(data,1,input);TransferDecisionEvent event;event.kind=TransferDecisionEventKind::last_week;
  event.offers.valid=true;event.offers.player=int(s.state().squad_slots.at(0)&0x0fff);event.result_text_address=::bmh::layout::PendingOffer_TransferLastWeekDialog_ExecutableField;
  const auto view=ui::transfer_last_week_dialog(s.state(),event);
  check(view.kind==ui::TaxDialogKind::acknowledgement&&view.source_manager_flags==0x84&&
        view.source_system_identity&&view.portrait==14&&view.badge_fallback&&
        view.body==s.state().player_name(event.offers.player)+message_text(s.state(),::bmh::layout::PendingOffer_TransferLastWeekDialog_ExecutableField),
        "last-week projection retains system identity, both placeholder portraits and Source text");
  ui::GameEndTaxDialog dialog(data,view);
  check(dialog.hit_test(120,170)==ui::GameEndTaxDialog::Button::acknowledge&&
        dialog.render().width==320&&dialog.render().height==240,
        "system countdown uses the existing Source acknowledgement renderer");}
 // Genuine salary JA commits native salary/cleanup and exactly one revision.
 {app::CareerSession s(data,1,input);const auto before=snap(s);uint64_t rev=7;std::string error;
 auto expected=s.state();auto expected_w=s.rand();auto expected_g=s.game();MinuteReport expected_report;PendingOfferHooks expected_hooks;
 expected_hooks.answer=[](const auto&,const auto&){return PendingOfferAnswer::yes;};
 const auto expected_result=process_player_card_pending_offers(expected,selection,expected_g,expected_w,expected_hooks,expected_report);
 check(expected_result.completed&&!expected_report.unsupported,"actual loaded-source salary oracle completes");
 PendingOfferHooks h;h.answer=[&](const GameState& staged,const PendingOfferQuestion& q){check(q.amount==52300&&save_stream(staged)==before.native,"question owns loaded staged state");same(s,before);return PendingOfferAnswer::yes;};
 const auto r=s.dispatch_club_player_pending_offers(selection,7,h,rev,error);
 check(r&&r->completed&&r->salary_accepted==1&&rev==8&&error.empty(),"JA commits once");
 check(s.state().memory.u32(::bmh::layout::OfferSlots)==0xffffffff&&s.state().tactics.at(5*0x58+13)==141,"JA native cleanup and original141pct");
 check(save_stream(s.state())==save_stream(expected)&&s.rand().seed()==expected_w.seed()&&s.rand().calls()==expected_w.calls()&&s.game().seed()==expected_g.seed()&&s.game().calls()==expected_g.calls(),"JA commits the actual Core native result and both RNGs without assuming a W draw");}
 // A complete forced transfer in an earlier raw slot must roll back when next answer is unavailable.
 {app::CareerSession s(data,1,input);const int next=s.state().squad_slots.at(798)&0xfff;duplicate(s.state(),1,next,0x80000000u|uint32_t(next)*1024);
 const auto before=snap(s);uint64_t rev=7;std::string error;int calls=0;PendingOfferHooks h;
 h.answer=[&](const GameState& staged,const PendingOfferQuestion& q){same(s,before);if(q.raw_offer_slot==0){++calls;return PendingOfferAnswer::no;}check(staged.memory.u32(::bmh::layout::OfferSlots)==0xffffffff,"earlier staged forced sale completed before unavailable answer");++calls;return PendingOfferAnswer::unsupported;};
 check(!s.dispatch_club_player_pending_offers(selection,7,h,rev,error)&&calls==2&&rev==7&&!error.empty(),"partial batch unsupported is rejected");same(s,before);}
 // A quote consumes both RNGs before its unavailable row choice; neither can escape.
 {app::CareerSession s(data,1,input);s.state().memory.set32(::bmh::layout::OfferSlots,14u*1024);const auto before=snap(s);uint64_t rev=7;std::string error;PendingOfferHooks h;int rows=0;
 h.select_transfer=[&](const GameState&,const TransferOfferListView& view){++rows;check(view.list_entry==40&&view.valid&&view.quoted_years>0,"actual special quote reaches manual chooser");return PendingOfferTransferChoice{};};
 check(!s.dispatch_club_player_pending_offers(selection,7,h,rev,error)&&rows==1&&rev==7,"unsupported quote chooser rolls back");same(s,before);}
 // Throwing presenters and stale revisions/physical selections cannot consume state.
 {app::CareerSession s(data,1,input);const auto before=snap(s);uint64_t rev=7;std::string error;PendingOfferHooks h;int calls=0;
 h.answer=[&](const auto&,const auto&)->PendingOfferAnswer{++calls;throw std::runtime_error("presenter stopped");};
 check(!s.dispatch_club_player_pending_offers(selection,7,h,rev,error)&&calls==1&&rev==7,"throw rolls back");same(s,before);
 check(!s.dispatch_club_player_pending_offers(selection,6,h,rev,error)&&calls==1,"stale revision invokes no presenter");same(s,before);
 uint64_t max_revision=std::numeric_limits<uint64_t>::max();check(!s.dispatch_club_player_pending_offers(selection,max_revision,h,max_revision,error)&&calls==1&&max_revision==std::numeric_limits<uint64_t>::max(),"revision maximum rejects before hook or mutation");same(s,before);
 auto bad=selection;bad.physical_row=6;check(!s.dispatch_club_player_pending_offers(bad,7,h,rev,error)&&calls==1,"stale physical identity invokes no presenter");same(s,before);
 h.answer=[](const auto&,const auto&){return PendingOfferAnswer::yes;};check(s.dispatch_club_player_pending_offers(selection,7,h,rev,error).has_value()&&rev==8,"exception guard resets for next real request");}
 // Modal lifetime blocks nested batch/kickoff/save; typed missing is not NO.
 {app::CareerSession s(data,1,input);const auto before=snap(s);uint64_t rev=7;std::string error;PendingOfferHooks h;
 h.answer=[&](const auto&,const auto&){std::string nested;PendingOfferHooks none;check(!s.dispatch_club_player_pending_offers(selection,7,none,rev,nested),"nested batch blocked");
 ui::DeskRequest kickoff{ui::DeskRequestKind::advance_turn,0,-1,-1,-1,rev};check(s.dispatch_kickoff(kickoff,rev,nested).kind==TurnDeskKind::Failed,"nested calendar blocked");
 check(!s.save_game(input.parent_path()/"MUST-NOT-WRITE.FBM",0,false,nested)&&nested=="pending_offer_decision_active","partial modal save blocked");return PendingOfferAnswer::unsupported;};
 check(!s.dispatch_club_player_pending_offers(selection,7,h,rev,error)&&rev==7,"missing manual answer preserves original offer");same(s,before);}
 std::cout<<"pending offer app tests: staged JA, partial sale/quote/throw rollback, stale context and modal guards OK\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
