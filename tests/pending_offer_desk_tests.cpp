// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "formats/textd.hpp"
#include "game/pending_offer.hpp"
#include "game/save.hpp"

using namespace bmh;
namespace {
constexpr uint32_t kOffers=::bmh::layout::OfferSlots, kOrder=::bmh::layout::NextDeskManager, kPosition=::bmh::layout::DeskPosition;
constexpr size_t kPool40=0xb70+40, kClickedBoard=5*0x58;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
uint32_t read32(const std::vector<uint8_t>& bytes,size_t at){uint32_t v=0;for(int i=0;i<4;++i)v|=uint32_t(bytes.at(at+size_t(i)))<<(8*i);return v;}
PendingOfferDeskRequest request(const GameState& g,int position=0,
    PendingOfferDeskRuntimeMode mode=PendingOfferDeskRuntimeMode::normal_interactive){
 return {mode,int(g.memory.u8(::bmh::layout::ManagerCount)),position};
}
void duplicate_entry(GameState& g,int raw,uint32_t word,uint32_t salary){
 const uint32_t target=::bmh::layout::EditorList+uint32_t(raw+40)*0x58;
 for(uint32_t i=0;i<0x58;++i)g.memory.set8(target+i,g.memory.u8(::bmh::layout::EditorList+40*0x58+i));
 g.memory.set32(target+0x0c,salary);
 g.memory.set32(kOffers+uint32_t(raw)*4,word);
 const int player=int((word>>10)&0xfff);
 g.squad_slots.at(kPool40+size_t(raw))=uint16_t(player);
}
bool contains(const GameState& g,int team,int player){const auto r=squad_range(team);for(size_t i=r.first;i<r.first+r.count;++i)if((g.squad_slots.at(i)&0xfff)==player)return true;return false;}
}

int main(int argc,char** argv){try{
 check(argc==3,"usage: pending_offer_desk_tests ORIGINAL_DATA QA_INPUT.FBM");
 if(!*argv[1]){std::puts("pending offer desk tests: skipped (Original data directory not configured)");return 77;}
 const auto save=load_savegame(argv[2]);check(save.checksum_ok,"genuine QA_INPUT checksum");
 auto original=GameState::from_save(SaveState(save.stream));
 const auto texts=load_textd(std::filesystem::path(argv[1])/"TEXTD.BLK");original.texts=&texts;
 check(original.memory.u8(::bmh::layout::ManagerCount)==1&&original.memory.u8(kPosition)==0&&
       original.memory.u16(kOrder)==0&&original.memory.u32(kOffers)==0x80003800,
       "genuine one-manager QA_INPUT native order and pending seller offer");
 const ClubPlayerDetailSelection manual_clicked{33,14,5,5,-1};

 // Natural post-desk salary JA is byte-for-byte the existing manual Core batch
 // plus only Source's completed-loop DeskPosition value (and W/G remain identical).
 {
  auto actual=original,oracle=original;GameRandom actual_game(73),oracle_game(73);
  WatcomRandom actual_rand(1),oracle_rand(1);MinuteReport actual_report,oracle_report;
  PendingOfferHooks hooks;hooks.answer=[](const GameState&,const PendingOfferQuestion& q){
   check(q.raw_offer_slot==0&&q.clicked_source_slot==797&&q.clicked_player==14,
         "natural JA anchors first raw owned offer at its absolute ordinary slot");
   return PendingOfferAnswer::yes;
  };
  const auto a=process_natural_pending_offers_after_desk(actual,request(actual),actual_game,actual_rand,hooks,actual_report);
  const auto o=process_player_card_pending_offers(oracle,manual_clicked,oracle_game,oracle_rand,hooks,oracle_report);
  oracle.memory.set8(kPosition,1);
  check(a.completed&&a.processed==1&&!actual_report.unsupported&&o.completed&&!oracle_report.unsupported,
        "natural and manual salary JA complete");
  check(save_stream(actual)==save_stream(oracle)&&actual_game.seed()==oracle_game.seed()&&
        actual_game.calls()==oracle_game.calls()&&actual_rand.seed()==oracle_rand.seed()&&
        actual_rand.calls()==oracle_rand.calls(),"natural JA equals direct Core oracle with only loop completion byte");
  check(actual.memory.u8(kPosition)==1&&contains(actual,33,14),"last desk writes completed position before simulation");
 }

 // Natural salary NO keeps the existing DispatchClubPlayerPendingOffers sticky forced-sale result and adds
 // no chooser quote/G draw; the post-desk path agrees with the direct Core oracle.
 {
  auto actual=original,oracle=original;GameRandom actual_game(73),oracle_game(73);
  WatcomRandom actual_rand(1),oracle_rand(1);MinuteReport actual_report,oracle_report;
  PendingOfferHooks hooks;hooks.answer=[](const GameState&,const PendingOfferQuestion&){return PendingOfferAnswer::no;};
  hooks.select_transfer=[](const GameState&,const TransferOfferListView&)->PendingOfferTransferChoice{
   throw std::runtime_error("sticky salary NO must bypass chooser");
  };
  const auto a=process_natural_pending_offers_after_desk(actual,request(actual),actual_game,actual_rand,hooks,actual_report);
  const auto o=process_player_card_pending_offers(oracle,manual_clicked,oracle_game,oracle_rand,hooks,oracle_report);
  oracle.memory.set8(kPosition,1);
  check(a.completed&&a.forced_acceptance&&a.transferred==1&&!actual_report.unsupported&&
        o.completed&&!oracle_report.unsupported,"natural and manual salary NO sale complete");
  check(save_stream(actual)==save_stream(oracle)&&actual_game.seed()==oracle_game.seed()&&
        actual_game.calls()==oracle_game.calls()&&actual_rand.seed()==oracle_rand.seed()&&
        actual_rand.calls()==oracle_rand.calls(),"natural NO equals direct Core oracle with exact W/G progression");
  check(!contains(actual,33,14)&&contains(actual,17,14),"natural NO keeps Source forced seller transfer");
 }

 // The first raw owned offer anchors the fixed clicked slot/board. Later same-owner
 // entries are processed in raw order; an offer for a later manager is retained.
 {
  auto g=original;g.manager_teams.push_back(17);g.memory.set8(::bmh::layout::ManagerCount,2);
  g.memory.set16(kOrder,0);g.memory.set16(kOrder+2,1);g.memory.set8(kPosition,0);
  const int second=int(g.squad_slots.at(798)&0xfff);
  duplicate_entry(g,1,0x80000000u|(uint32_t(second)<<10),60000);
  duplicate_entry(g,2,0x80000000u|(14u<<10)|1u,70000); // later owner's raw word
  const uint32_t foreign=g.memory.u32(kOffers+8);const auto later_pool=g.squad_slots.at(kPool40+2);
  std::vector<int> questions;PendingOfferHooks hooks;hooks.answer=[&](const GameState&,const PendingOfferQuestion& q){
   questions.push_back(q.raw_offer_slot);check(q.clicked_source_slot==797&&q.clicked_player==14,
       "batch retains first Source slot and clicked identity");return PendingOfferAnswer::yes;};
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  const auto r=process_natural_pending_offers_after_desk(g,request(g,0),game,rand,hooks,report);
  check(r.completed&&r.processed==2&&!report.unsupported&&questions==std::vector<int>({0,1}),
        "raw owned order handled once from first usable offer");
 check(read32(g.tactics,kClickedBoard+0x50)==60000&&g.memory.u32(kOffers+8)==foreign&&
        g.squad_slots.at(kPool40+2)==later_pool&&g.memory.u8(kPosition)==0,
        "clicked board is fixed and later-owner offer/pool survives nonfinal desk");
 }

 // The manager order is authoritative even when its first owner is not numeric
 // manager0. The adapter restores that owner before invoking the shared batch.
 {
  auto g=original;g.manager_teams.push_back(17);g.memory.set8(::bmh::layout::ManagerCount,2);
  g.memory.set16(kOrder,1);g.memory.set16(kOrder+2,0);g.memory.set8(kPosition,0);
  const auto seller=squad_range(17);check(seller.count>0,"reordered owner has ordinary squad");
  const int player=int(g.squad_slots.at(seller.first)&0xfff);
  duplicate_entry(g,0,0x80000000u|(uint32_t(player)<<10)|1u,52300);
  g.memory.set16(::bmh::layout::EditorList+40*0x58+8,17);
  const uint32_t later=0x80000000u|(14u<<10);
  g.memory.set32(kOffers+4,later);g.squad_slots.at(kPool40+1)=14;
  PendingOfferHooks hooks;hooks.answer=[&](const GameState&,const PendingOfferQuestion& q){
   check(q.manager==1&&q.offered_player==player&&q.raw_offer_slot==0,
         "bd6cc order resolves the first offer owner");return PendingOfferAnswer::yes;};
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  const auto r=process_natural_pending_offers_after_desk(g,request(g,0),game,rand,hooks,report);
  check(r.completed&&!report.unsupported&&r.processed==1&&g.memory.u8(::bmh::layout::ActiveManager)==1&&
        g.memory.u32(kOffers+4)==later&&g.squad_slots.at(kPool40+1)==14&&g.memory.u8(kPosition)==0,
        "reordered owner is restored and later owner remains untouched");
 }

 // Missing first owned player needs format field's unavailable error presenter and
 // leaves the staged state untouched; a missing later word belongs to DispatchClubPlayerPendingOffers's
 // scan after the first usable anchor and therefore follows that existing batch.
 {
  auto missing_first=original;duplicate_entry(missing_first,0,0x80000000u|(4094u<<10),52300);
  duplicate_entry(missing_first,1,0x80000000u|(14u<<10),60000);
  const auto before=save_stream(missing_first);GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  PendingOfferHooks hooks;hooks.answer=[](const auto&,const auto&){return PendingOfferAnswer::yes;};
  const auto r=process_natural_pending_offers_after_desk(missing_first,request(missing_first),game,rand,hooks,report);
  check(!r.completed&&report.unsupported&&save_stream(missing_first)==before&&game.calls()==0&&rand.calls()==0,
        "missing first owned player is explicit unsupported and mutation-free");
 }
 {
  auto later_missing=original;duplicate_entry(later_missing,1,0x80000000u|(4094u<<10),60000);
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;int questions=0;
  PendingOfferHooks hooks;hooks.answer=[&](const auto&,const PendingOfferQuestion& q){
   ++questions;check(q.raw_offer_slot==0,"later missing entry does not preempt valid first anchor");return PendingOfferAnswer::yes;};
  const auto r=process_natural_pending_offers_after_desk(later_missing,request(later_missing),game,rand,hooks,report);
  check(r.completed&&!report.unsupported&&r.processed==2&&questions==1&&
        later_missing.memory.u32(kOffers+4)==0xffffffffu&&later_missing.squad_slots.at(kPool40+1)==0x0fff,
        "later missing offer uses existing 3140f batch cleanup behavior");
 }

 // With no current-owner offer, the final desk still performs Source's global
 // four-entry pool/word cleanup without asking, quoting or drawing either RNG.
 {
  auto g=original;for(int i=0;i<4;++i){g.memory.set32(kOffers+uint32_t(i)*4,0x80000000u|(14u<<10)|uint32_t(i));g.squad_slots.at(kPool40+size_t(i))=uint16_t(14+i);}
  // These words are for manager1 and are intentionally unowned by this one-manager desk.
  for(int i=0;i<4;++i)g.memory.set32(kOffers+uint32_t(i)*4,(uint32_t(14+i)<<10)|1u);
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;PendingOfferHooks hooks;
  const auto r=process_natural_pending_offers_after_desk(g,request(g),game,rand,hooks,report);
  check(r.completed&&r.processed==0&&!report.unsupported&&game.calls()==0&&rand.calls()==0,
        "empty last-desk path completes without RNG");
  check(g.memory.u8(kPosition)==1,"empty last-desk path sets completed-loop cursor");
  for(int i=0;i<4;++i)check(g.memory.u32(kOffers+uint32_t(i)*4)==0xffffffffu&&
      g.squad_slots.at(kPool40+size_t(i))==0x0fff,"last manager clears every special offer and pool slot");
 }

 // Unknown transient mode, malformed manager order/count/cursor, and mismatched
 // owner contexts fail before mutation, even in NDEBUG builds.
 {
  auto g=original;const auto before=save_stream(g);GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  auto q=request(g);q.runtime_mode=PendingOfferDeskRuntimeMode::unsupported;
  const auto r=process_natural_pending_offers_after_desk(g,q,game,rand,{},report);
  check(!r.completed&&report.unsupported&&save_stream(g)==before&&game.calls()==0&&rand.calls()==0,
        "unspecified transient-gate context does not default to normal");
 }
 for(int variant=0;variant<3;++variant){
  auto g=original;auto q=request(g);
  if(variant==0)g.memory.set16(kOrder,2); // outside the native one-manager permutation
  if(variant==1)g.memory.set8(kPosition,1); // cursor/request disagreement
  if(variant==2)q.manager_count=2; // caller/native count disagreement
  const auto before=save_stream(g);GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  const auto r=process_natural_pending_offers_after_desk(g,q,game,rand,{},report);
  check(!r.completed&&report.unsupported&&save_stream(g)==before&&game.calls()==0&&rand.calls()==0,
        "invalid owner order/count/position fails before state or RNG change");
 }
 for(int owner:std::vector<int>{-1,kNoTeam}){
  auto g=original;g.manager_teams[0]=owner;const auto before=save_stream(g);
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  const auto r=process_natural_pending_offers_after_desk(g,request(g),game,rand,{},report);
  check(!r.completed&&report.unsupported&&save_stream(g)==before&&game.calls()==0&&rand.calls()==0,
        "national/unknown owner with a pending offer is explicitly unsupported");
 }
 std::puts("pending offer desk tests: passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"pending_offer_desk_tests: %s\n",e.what());return 1;}}
