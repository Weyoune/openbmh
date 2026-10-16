// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <vector>
#include "app/career_session.hpp"
#include "formats/fbm.hpp"
#include "formats/textd.hpp"
#include "game/desk_actions.hpp"
#include "game/match_engine.hpp"
#include "game/save.hpp"
#include "ui/pending_offer.hpp"

namespace fs = std::filesystem;
using namespace bmh;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool contains(const GameState& g,int team,int player){const auto r=squad_range(team);
 for(size_t i=r.first;i<r.first+r.count;++i)if((g.squad_slots.at(i)&0xfff)==player)return true;
 return false;
}
struct LiveSnapshot {
 Bytes native; GameRandom game; WatcomRandom rand; uint64_t revision;
 LiveSnapshot(app::CareerSession& s,uint64_t r):native(save_stream(s.state())),game(s.game()),rand(s.rand()),revision(r){}
 bool equals(app::CareerSession& s,uint64_t r)const{return native==save_stream(s.state())&&revision==r&&
  game.seed()==s.game().seed()&&game.calls()==s.game().calls()&&
  rand.seed()==s.rand().seed()&&rand.calls()==s.rand().calls();}
};
struct ControlInput {
 fs::path directory,input;
 ClubPlayerDetailSelection selected;
 ControlInput(const fs::path& data,const fs::path& genuine){
  const auto base=load_savegame(genuine);check(base.checksum_ok,"genuine two-manager checksum");
  auto state=GameState::from_save(SaveState(base.stream));
  const auto texts=load_textd(data/"TEXTD.BLK");state.texts=&texts;
  check(state.manager_teams==std::vector<int>({1,54})&&state.date==0&&
        state.memory.u8(::bmh::layout::DeskPosition)==0&&state.memory.u16(::bmh::layout::NextDeskManager)==0,
        "genuine two-manager input identity");
  WatcomRandom helper(37);MinuteReport report;
  const auto listing=list_manager_player(state,0,5,helper,report);
  check(listing.status==TransferActionStatus::accepted&&!report.unsupported,
        "real seller listing helper");
  const uint32_t ordinary=::bmh::layout::EditorList+uint32_t(listing.list_entry)*0x58;
  TransferBidRequest bid;bid.manager=1;bid.list_entry=listing.list_entry;bid.player=listing.player;
  bid.fee=int32_t((state.memory.u32(ordinary+0x2c)/10000u)*0x30d4u);
  bid.annual_salary=int32_t(((100u*state.memory.u32(ordinary+0x30))/10000u)*200u);
  bid.contract_years=1;
  check(submit_transfer_bid(state,bid,helper).status==TransferActionStatus::pending,
        "real human buyer bid helper");
  int raw=-1;for(int i=0;i<4;++i)if(state.memory.u16(ordinary+uint32_t(i)*2)==54)raw=i;
  check(raw>=0,"real buyer row exists");
  // Declared test-only promotion of an actual ordinary bid to special row40.
  // This is a control built from a genuine save, not an Original pending offer.
  constexpr uint32_t special=::bmh::layout::EditorList+40*0x58;
  for(uint32_t i=0;i<0x58;++i)state.memory.set8(special+i,state.memory.u8(ordinary+i));
  for(uint32_t i=0;i<4;++i)state.memory.set16(special+i*2,i==0?54:0xffff);
  state.memory.set32(special+0x1c,state.memory.u32(ordinary+0x1c+uint32_t(raw)*4));
  state.memory.set32(special+0x0c,state.memory.u32(ordinary+0x0c+uint32_t(raw)*4));
  state.memory.set8(special+0x34,state.memory.u8(ordinary+0x34+uint32_t(raw)));
  for(uint32_t i=0;i<7;++i)
   state.memory.set8(special+0x38+i,state.memory.u8(ordinary+0x38+uint32_t(raw)*7+i));
  state.squad_slots.at(0xb70+40)=state.squad_slots.at(0xb70+size_t(listing.list_entry));
  state.memory.set32(::bmh::layout::OfferSlots,uint32_t(listing.player)<<10);
  selected={1,listing.player,5,5,-1};
  directory=fs::temp_directory_path()/
   ("openbmh-human-buyer-app-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(directory);input=directory/"CONTROL.FBM";
  WatcomRandom serialize(1);write_savegame(input,make_savegame(state,serialize));
  const auto saved=load_savegame(input);
  check(saved.checksum_ok&&saved.stream==save_stream(state),"generated control Native roundtrip");
 }
 ~ControlInput(){std::error_code ignored;fs::remove_all(directory,ignored);}
};
}

int main(int argc,char** argv){try{
 check(argc==3,"usage: human_buyer_pending_app_tests ORIGINAL_DATA GENUINE_2PNWK001");
 if(!*argv[1]){std::puts("human buyer App tests: skipped (Original data not configured)");return 77;}
 const fs::path data(argv[1]);const ControlInput fixture(data,argv[2]);const auto& input=fixture.input;
 const auto native=load_savegame(input);check(native.checksum_ok,"declared control checksum");
 const auto control=GameState::from_save(SaveState(native.stream));
 check(control.manager_teams==std::vector<int>({1,54})&&
       control.memory.u32(::bmh::layout::OfferSlots)==(uint32_t(fixture.selected.player_id)<<10),
       "declared genuine-two-manager based human buyer control identity");
 const auto selected=fixture.selected;

 // Both the presenter and mutable Native copy are deliberately denied ownership
 // of match_events_disabled. The actual loaded EXE supplies zero to the production session.
 {
  app::CareerSession session(data,1,input);uint64_t revision=1;std::string error;
  check(!session.state().memory.has(::bmh::layout::MatchEventsDisabled),"allocator gate is not a Native FBM field");
  const LiveSnapshot before(session,revision);int acknowledgements=0;
  PendingOfferHooks hooks;hooks.human_buyer_allocator_gate=PendingOfferAllocatorGate::blocked;
  hooks.answer=[](const auto&,const auto&){return PendingOfferAnswer::unsupported;};
  hooks.select_transfer=[&](const GameState&,const TransferOfferListView& view){
   check(before.equals(session,revision),"live state stays unchanged during staged choice");
   check(view.offers[0].buyer_manager==1,"actual second human manager owns selected bid");
   return PendingOfferTransferChoice{PendingOfferTransferChoiceKind::bid,0};
  };
  hooks.notice=[&](const GameState& staged,const PendingOfferNotice& notice){
   check(before.equals(session,revision),"live state/RNG/revision stay unchanged during staged notice");
   check(notice.text_address==::bmh::layout::PendingOffer_FormatError_ExecutableField2&&notice.manager==1,"normal settlement ACK belongs to human buyer");
   const auto projection=ui::pending_offer_notice_dialog(staged,notice);
   check(projection.source_manager_flags==0x81&&projection.source_flags==0,
         "buyer ACK retains Original manager and mode bits");++acknowledgements;
  };
  const auto result=session.dispatch_club_player_pending_offers(selected,revision,hooks,revision,error);
  check(result&&result->completed&&result->transferred==1&&result->buyer_allocator_blocked==0&&
        acknowledgements==1&&revision==2&&error.empty(),
        "actual session EXE gate overrides blocked presenter and commits one complete sale");
  check(!contains(session.state(),1,selected.player_id)&&contains(session.state(),54,selected.player_id)&&
        session.state().memory.u32(::bmh::layout::OfferSlots)==0xffffffffu,
        "player ownership and raw pending cleanup committed");
 }

 // An unavailable Source presenter is unsupported, never implicit acceptance;
 // even quotation draws performed on the staging copy must be discarded.
 {
  app::CareerSession session(data,1,input);uint64_t revision=1;std::string error;
  const LiveSnapshot before(session,revision);PendingOfferHooks hooks;
  hooks.select_transfer=[](const auto&,const auto&){return PendingOfferTransferChoice{PendingOfferTransferChoiceKind::bid,0};};
  const auto result=session.dispatch_club_player_pending_offers(selected,revision,hooks,revision,error);
  check(!result&&!error.empty()&&before.equals(session,revision),
        "missing notice producer restores the complete live Native/W/G/revision");
 }

 // A real presenter exception after the buyer/seller debit-credit stage must
 // leave the input available for an ordinary retry, with no partial live cash.
 {
  app::CareerSession session(data,1,input);uint64_t revision=1;std::string error;
  const LiveSnapshot before(session,revision);PendingOfferHooks hooks;
  hooks.select_transfer=[](const auto&,const auto&){return PendingOfferTransferChoice{PendingOfferTransferChoiceKind::bid,0};};
  hooks.notice=[](const GameState&,const PendingOfferNotice&){throw std::runtime_error("controlled buyer ACK exception");};
  const auto result=session.dispatch_club_player_pending_offers(selected,revision,hooks,revision,error);
  check(!result&&error=="controlled buyer ACK exception"&&before.equals(session,revision),
        "notice exception after settlement rolls back the complete live Native/W/G/revision");
 }
 return 0;
}catch(const std::exception& ex){std::fprintf(stderr,"human buyer App tests: %s\n",ex.what());return 1;}}
