// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "formats/textd.hpp"
#include "game/matchday.hpp"
#include "game/messages.hpp"
#include "game/month.hpp"
#include "game/pending_offer.hpp"
#include "game/save.hpp"
#include "game/season.hpp"

using namespace bmh;
namespace {
constexpr uint32_t offers=::bmh::layout::OfferSlots, list40=::bmh::layout::EditorList+40*0x58;
constexpr size_t pool40=0xb70+40, board5=5*0x58;
void check(bool v,const char* what){if(!v)throw std::runtime_error(what);}
uint32_t read(const std::vector<uint8_t>& b,size_t at){uint32_t v=0;for(int i=0;i<4;++i)v|=uint32_t(b.at(at+size_t(i)))<<(8*i);return v;}
void write(std::vector<uint8_t>& b,size_t at,uint32_t v){for(int i=0;i<4;++i)b.at(at+size_t(i))=uint8_t(v>>(8*i));}
bool contains(const GameState& g,int team,int player){const auto r=squad_range(team);for(size_t i=r.first;i<r.first+r.count;++i)if((g.squad_slots.at(i)&0xfff)==player)return true;return false;}
void duplicate(GameState& g,int raw,int player,uint32_t word,uint32_t salary){
 const uint32_t target=list40+uint32_t(raw)*0x58;
 for(uint32_t i=0;i<0x58;++i)g.memory.set8(target+i,g.memory.u8(list40+i));
 g.memory.set32(target+0x0c,salary);g.memory.set32(offers+uint32_t(raw)*4,word);
 g.squad_slots.at(pool40+size_t(raw))=uint16_t(player);
}
}
int main(int argc,char** argv){try{
 check(argc==3,"usage: pending_offer_tests DATA QA_INPUT.FBM");
 if(!*argv[1]){std::puts("pending offer tests: skipped (Original data directory not configured)");return 77;}
 const auto save=load_savegame(argv[2]);check(save.checksum_ok,"native input checksum");
 auto original=GameState::from_save(SaveState(save.stream));
 const auto texts=load_textd(std::filesystem::path(argv[1])/"TEXTD.BLK");original.texts=&texts;
 const ClubPlayerDetailSelection clicked{33,14,5,5,-1};
 check(original.memory.u32(offers)==0x80003800&&original.memory.u16(list40)==17&&
       original.memory.u32(list40+0x0c)==52300&&original.memory.u32(list40+0x1c)==2470000,
       "genuine Original N.BROWN offer identity, salary and 2.470.000 EU fee");
 check(read(original.tactics,board5+0x50)==38178,"genuine clicked salary");
 // JA: only the clicked salary fields and owned pending cleanup change.
 {
  auto g=original,expected=original;GameRandom game(73);WatcomRandom rand(1),reference(1);MinuteReport report;
  uint32_t fee=0,salary=0;market_value(expected,797,0,98,fee,salary,(expected.tactics.at(board5+12)+6)/12,0,reference);
  write(expected.tactics,board5+0x50,52300);expected.tactics.at(board5+13)=uint8_t(52300u*100/salary);
  expected.memory.set32(offers,0xffffffff);expected.squad_slots.at(pool40)=0x0fff;
  PendingOfferHooks hooks;hooks.answer=[&](const GameState& state,const PendingOfferQuestion& q){
   check(q.kind==PendingOfferQuestionKind::salary_demand&&q.source_flags==1&&q.manager==0&&
         q.raw_offer_slot==0&&q.offered_player==14&&q.clicked_player==14&&q.clicked_source_slot==797,
         "owned salary question identities/flags");
   check(q.current_clicked_name==state.player_name(14)&&q.original_message==state.player_name(14)+
         message_text(state,::bmh::layout::Weekly_ProcessPendingBatch_TextPointer)+format_amount(state,52300)+message_text(state,::bmh::layout::Weekly_ProcessPendingBatch_TextPointer2),
         "literal Source salary question text");return PendingOfferAnswer::yes;};
  const auto result=process_player_card_pending_offers(g,clicked,game,rand,hooks,report);
  check(result.completed&&result.processed==1&&result.salary_accepted==1&&!result.forced_acceptance&&!report.unsupported,"JA completes");
  check(save_stream(g)==save_stream(expected)&&rand.seed()==reference.seed()&&rand.calls()==reference.calls()&&game.calls()==0&&game.seed()==73,
        "JA complete native fields and exact market W/no G");
  check(read(g.tactics,board5+0x50)==52300&&g.tactics.at(board5+13)==141&&contains(g,33,14),"Original salary JA outcome");
 }
 // bit5 skips both market quote and both salary writes, but still clears owned pending slot.
 {
  auto g=original;g.memory.set32(offers,0x80003820);auto expected=g;
  expected.memory.set32(offers,0xffffffff);expected.squad_slots.at(pool40)=0x0fff;
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;PendingOfferHooks h;
  h.answer=[](const auto&,const auto&){return PendingOfferAnswer::yes;};
  const auto r=process_player_card_pending_offers(g,clicked,game,rand,h,report);
  check(r.completed&&!report.unsupported&&save_stream(g)==save_stream(expected)&&game.calls()==0&&rand.calls()==0,"bit5 JA is quote/write inert");
 }
 // NO: bypass the chooser/G restart, move to the actual buyer, preserve bid words.
 {
  auto plain=original,observed=original;GameRandom a(73),b(73);WatcomRandom wa(1),wb(1);MinuteReport ra,rb;
  PendingOfferHooks h;h.answer=[](const auto&,const auto& q){check(q.kind==PendingOfferQuestionKind::salary_demand,"normal salary NO needs no severance");return PendingOfferAnswer::no;};
  h.select_transfer=[](const auto&,const auto&)->PendingOfferTransferChoice{throw std::runtime_error("sticky NO must bypass chooser");};
  const auto first=process_player_card_pending_offers(plain,clicked,a,wa,h,ra);
  std::vector<uint32_t> notices;h.notice=[&](const auto&,const auto& n){notices.push_back(n.text_address);};
  const auto second=process_player_card_pending_offers(observed,clicked,b,wb,h,rb);
  check(first.completed&&second.completed&&first.transferred==1&&first.forced_acceptance&&!ra.unsupported&&!rb.unsupported,"salary NO forced acceptance");
  check(!contains(plain,33,14)&&contains(plain,17,14)&&plain.memory.u32(offers)==0xffffffff&&plain.squad_slots.at(pool40)==0x0fff,"Original NO actual sale and cleanup");
  check(plain.memory.u16(list40)==17&&plain.memory.u32(list40+0x1c)==2470000,"3140f cleanup retains special bid entry");
  check(a.calls()==0&&a.seed()==73&&wa.calls()==1&&wa.seed()==1103527590,"forced format field only W90..100, no quote/ranking G");
  check(read(plain.manager_records,0x314)==read(original.manager_records,0x314)+2470000&&
        read(plain.manager_records,0x330)==read(original.manager_records,0x330)-2470000&&
        read(plain.manager_records,0x874)==read(original.manager_records,0x874)+2470000,"actual fee cash/transfer ledger/income statistics");
  check(read(plain.manager_records,0x760)==original.date*4+247u*0x80000&&
        read(plain.manager_records,0x7d8)==uint32_t(original.player_name_offsets.at(14))+uint32_t(original.club_ids.at(17))*0x10000,"format field sale history");
  check(save_stream(plain)==save_stream(observed)&&a.seed()==b.seed()&&wa.seed()==wb.seed()&&a.calls()==b.calls()&&wa.calls()==wb.calls()&&notices.empty(),"read-only notices preserve all native fields and both RNGs");
 }
 // The legacy API intentionally still follows its historical NO decline behavior.
 {
  auto g=original;GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  answer_offers(g,0,false,rand,report,&game);
  check(!report.unsupported&&contains(g,33,14)&&!contains(g,17,14)&&game.calls()==1&&rand.calls()==0&&g.memory.u32(offers)==0xffffffff,"legacy answer_offers(false) unchanged");
  check(!project_transfer_offer_list(original,40).valid,"legacy projection still rejects special entries");
 }
 // Salary questions in the same batch retain the clicked slot and board, not the offered row.
 {
  auto g=original;const int other=g.squad_slots.at(798)&0xfff;check(other!=14&&other!=0xfff,"second genuine squad player");
  duplicate(g,1,other,0x80000000u|uint32_t(other)*1024,60000);
  duplicate(g,2,other,0x80000000u|uint32_t(other)*1024|1,70000);
  const auto original_other_board=read(g.tactics,6*0x58+0x50);const auto foreign_word=g.memory.u32(offers+8);
  int questions=0;PendingOfferHooks h;h.answer=[&](const auto&,const auto& q){
   check(q.clicked_source_slot==797&&q.clicked_player==14&&q.current_clicked_name==g.player_name(14),"fixed clicked slot/name source in batch");
   check(q.offered_player==(questions==0?14:other),"offered identity stays separate");++questions;return PendingOfferAnswer::yes;};
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  const auto r=process_player_card_pending_offers(g,clicked,game,rand,h,report);
  check(r.completed&&!report.unsupported&&questions==2&&r.processed==2&&read(g.tactics,board5+0x50)==60000&&
        read(g.tactics,6*0x58+0x50)==original_other_board&&g.memory.u32(offers+8)==foreign_word&&g.squad_slots.at(pool40+2)==other,
        "clicked-board quirk, raw order and other-manager untouched");
 }
 // After a first NO sale, the next question rereads the fixed clicked slot's new occupant.
 {
  auto g=original;const int other=g.squad_slots.at(798)&0xfff;
  duplicate(g,1,other,0x80000000u|uint32_t(other)*1024,60000);
  int salary_questions=0;PendingOfferHooks h;
  h.answer=[&](const auto& state,const auto& q){
   if(q.kind==PendingOfferQuestionKind::severance)return PendingOfferAnswer::yes;
   check(q.clicked_player==14&&q.clicked_source_slot==797,"initial identity/slot survives compaction");
   if(salary_questions++==0)return PendingOfferAnswer::no;
   check(q.offered_player==other&&q.current_clicked_name==state.player_name(other),"4fa50 rereads current fixed-slot occupant after compaction");
   return PendingOfferAnswer::yes;
  };
  h.select_transfer=[](const auto&,const auto&)->PendingOfferTransferChoice{throw std::runtime_error("sticky batch bypasses chooser");};
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  const auto r=process_player_card_pending_offers(g,clicked,game,rand,h,report);
  check(r.completed&&!report.unsupported&&r.processed==2&&r.transferred==1&&r.salary_accepted==1&&r.forced_acceptance&&salary_questions==2&&
        contains(g,17,14)&&contains(g,33,other)&&read(g.tactics,board5+0x50)==60000&&game.calls()==0,"sticky NO survives subsequent salary JA and clicked board compaction");
 }
 // Once sticky is set, a later fee offer also bypasses all chooser quote/ranking draws.
 {
  auto g=original;const int other=g.squad_slots.at(798)&0xfff;
  duplicate(g,1,other,uint32_t(other)*1024,60000);
  PendingOfferHooks h;h.answer=[](const auto&,const auto& q){return q.kind==PendingOfferQuestionKind::salary_demand?PendingOfferAnswer::no:PendingOfferAnswer::yes;};
  h.select_transfer=[](const auto&,const auto&)->PendingOfferTransferChoice{throw std::runtime_error("later fee must use sticky force");};
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  const auto r=process_player_card_pending_offers(g,clicked,game,rand,h,report);
  check(r.completed&&!report.unsupported&&r.transferred==2&&r.processed==2&&contains(g,17,14)&&contains(g,17,other)&&game.calls()==0&&rand.calls()==2,"NO sticky propagates to later fee offer, exactly two settlement W draws");
 }
 // An explicit human-loan selection writes its original snapshot board, never the tactics row.
 {
  auto g=original;g.memory.set16(::bmh::layout::Career_CareerManagersEnd_RuntimeField,14);g.memory.set8(::bmh::layout::ClubInfo_FormatError_RuntimeField2,0);
  for(uint32_t i=0;i<0x58;++i)g.memory.set8(::bmh::layout::ClubInfo_FormatError_RuntimeField+i,g.tactics.at(board5+i));
  g.memory.set32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField,14u+33u*0x1000+17u*0x400000);g.memory.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField,12);
  const auto tactics=g.tactics;PendingOfferHooks h;h.answer=[](const auto&,const auto&){return PendingOfferAnswer::yes;};
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  const auto r=process_player_card_pending_offers(g,{33,14,5,-1,0},game,rand,h,report);
  check(r.completed&&!report.unsupported&&g.memory.u32(::bmh::layout::ClubInfo_FormatError_RuntimeField+0x50)==52300&&g.tactics==tactics&&game.calls()==0,"owned human-loan board storage is distinct and stable");
 }
 // A missing offered player still gets cleanup, without another question or RNG.
 {
  auto g=original;g.memory.set32(offers,0x80003820);duplicate(g,1,3000,0x80000000u|3000u*1024,60000);
  int questions=0;PendingOfferHooks h;h.answer=[&](const auto&,const auto&){++questions;return PendingOfferAnswer::yes;};
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  const auto r=process_player_card_pending_offers(g,clicked,game,rand,h,report);
  check(r.completed&&!report.unsupported&&r.processed==2&&questions==1&&g.memory.u32(offers+4)==0xffffffff&&g.squad_slots.at(pool40+1)==0xfff&&rand.calls()==0&&game.calls()==0,"missing player unconditional owned cleanup");
 }
 // A real salary decrease reaches severance305; NO leaves the player and cash unchanged.
 for(bool severance_yes:{false,true}) {
  auto g=original;g.memory.set32(list40+0x0c,1000);
  int questions=0;std::vector<uint32_t> notices;uint32_t severance=0;
  PendingOfferHooks h;h.answer=[&](const auto& state,const auto& q){
   ++questions;if(q.kind==PendingOfferQuestionKind::salary_demand)return PendingOfferAnswer::no;
   check(q.source_flags==0x305&&q.amount>=1000&&q.amount%1000==0&&
         q.original_message==message_text(state,::bmh::layout::Weekly_PendingSettle_TextPointer)+format_amount(state,int32_t(q.amount))+".","Source salary-drop severance flags/rounded amount/text");
   severance=q.amount;return severance_yes?PendingOfferAnswer::yes:PendingOfferAnswer::no;};
  h.notice=[&](const auto&,const auto& n){notices.push_back(n.text_address);};
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  const auto r=process_player_card_pending_offers(g,clicked,game,rand,h,report);
  check(r.completed&&!report.unsupported&&questions==2&&game.calls()==0&&rand.calls()==1&&g.memory.u32(offers)==0xffffffff,"severance flow and cleanup without chooser G");
  check(contains(g,33,14)==!severance_yes&&contains(g,17,14)==severance_yes,"severance answer controls actual transfer");
  if(severance_yes)check(read(g.manager_records,0x314)==read(original.manager_records,0x314)+2470000-severance&&notices.empty(),"fee then actual severance debit, forced param10 suppresses ACK");
  else check(read(g.manager_records,0x314)==read(original.manager_records,0x314)&&notices==std::vector<uint32_t>{::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField4},"severance NO refusal is not a mailbox substitute or debit");
 }
 // Explicit missing choice and stale identity never silently choose NEIN.
 {
  auto g=original;const auto before=save_stream(g);GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  const auto r=process_player_card_pending_offers(g,clicked,game,rand,{},report);
  check(!r.completed&&report.unsupported&&save_stream(g)==before&&game.calls()==0&&rand.calls()==0,"missing salary hook inert and unsupported");
  report={};bool threw=false;auto stale=clicked;stale.player_id=15;
  try{process_player_card_pending_offers(g,stale,game,rand,{},report);}catch(const std::invalid_argument&){threw=true;}
  check(threw&&save_stream(g)==before&&game.calls()==0&&rand.calls()==0,"stale clicked physical identity inert");
 }
 // A manual fee refusal opens the real quote once; no sale settlement/weekly ranking.
 {
  auto g=original;g.memory.set32(offers,0x00003800);GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  PendingOfferHooks h;int choose=0;h.select_transfer=[&](const auto&,const auto& view){
   ++choose;check(view.valid&&view.list_entry==40&&view.player==14&&view.offers[0].buyer==17&&view.quoted_fee%10000==0&&view.quoted_monthly_salary%100==0&&view.preferred_bid_index==-1,"owned special-entry quote without invented ranked row");
   return PendingOfferTransferChoice{PendingOfferTransferChoiceKind::decline,-1};};
  const auto r=process_player_card_pending_offers(g,clicked,game,rand,h,report);
  check(r.completed&&!report.unsupported&&choose==1&&contains(g,33,14)&&game.calls()==1&&r.transferred==0,"manual fee decline consumes quote G only and clears offer");
 }
 // Normal manual acceptance performs the 125% salary recheck, then settlement.
 {
  auto g=original;g.memory.set32(offers,0x00003800);PendingOfferHooks h;int choices=0;std::vector<uint32_t> notices;
  h.select_transfer=[&](const auto& state,const auto& view){++choices;
   check(state.memory.u32(list40+0x0c)==52300&&view.offers[0].monthly_salary==52300,"temporary salary recheck is restored outside Core evaluation");
   return PendingOfferTransferChoice{PendingOfferTransferChoiceKind::bid,0};};
  h.notice=[&](const auto&,const auto& n){notices.push_back(n.text_address);};
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  const auto r=process_player_card_pending_offers(g,clicked,game,rand,h,report);
  check(r.completed&&!report.unsupported&&r.transferred==1&&!r.forced_acceptance&&choices==1&&contains(g,17,14)&&
        game.calls()==1&&rand.calls()==2&&g.memory.u32(list40+0x0c)==52300&&notices==std::vector<uint32_t>{::bmh::layout::PendingOffer_FormatError_ExecutableField2},"normal manual offer: quote G, recheck W, severance W and ordinary success ACK");
 }
 // With its sole bid rejected, Source param9==1 shows PendingOffer_PendingOfferNoticeDialog_ExecutableField3 and stops without another quote.
 {
  auto g=original;g.memory.set32(offers,0x00003800);g.memory.set32(list40+0x0c,1);
  PendingOfferHooks h;int choices=0;std::vector<uint32_t> notices;
  h.select_transfer=[&](const auto&,const auto&){++choices;return PendingOfferTransferChoice{PendingOfferTransferChoiceKind::bid,0};};
  h.notice=[&](const auto&,const auto& n){notices.push_back(n.text_address);};
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  const auto r=process_player_card_pending_offers(g,clicked,game,rand,h,report);
  check(r.completed&&!report.unsupported&&r.transferred==0&&choices==1&&contains(g,33,14)&&game.calls()==1&&rand.calls()==1&&
        g.memory.u16(list40)==0xffff&&g.memory.u32(list40+0x0c)==1&&notices==std::vector<uint32_t>{::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField3},"salary recheck rejects raw0, restores salary, terminal notice has no extra quote");
 }
 // A loan-count refusal retries the same live quote; right click then declines.
 {
  auto g=original;g.memory.set32(offers,0x00003800);g.memory.set8(list40+0x34,0x8c);g.memory.set8(::bmh::layout::DeskActions_Unsupported_RuntimeField,3);
  PendingOfferHooks h;int choices=0;std::vector<uint32_t> notices;
  h.select_transfer=[&](const auto&,const auto& v){++choices;
   if(choices==1)return PendingOfferTransferChoice{PendingOfferTransferChoiceKind::bid,0};
   check(v.notice_text_address==::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField2,"Source loan refusal retained in owned quote");
   return PendingOfferTransferChoice{PendingOfferTransferChoiceKind::decline,-1};};
  h.notice=[&](const auto&,const auto& n){notices.push_back(n.text_address);};
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  const auto r=process_player_card_pending_offers(g,clicked,game,rand,h,report);
  check(r.completed&&!report.unsupported&&choices==2&&contains(g,33,14)&&game.calls()==2&&rand.calls()==0&&
        notices==std::vector<uint32_t>({::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField2,::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField}),"Source loan refusal redraw resets/draws quote G each time, no bid or settlement W");
 }
 // format field shifts both complete history blocks and saturates its count at25.
 for(uint8_t count:{24,25}) {
  auto g=original;g.manager_records.at(0x21d)=count;
  for(size_t i=0;i<30;++i){write(g.manager_records,0x760+i*4,0x100000u+uint32_t(i));write(g.manager_records,0x7d8+i*4,0x200000u+uint32_t(i));}
  PendingOfferHooks h;h.answer=[](const auto&,const auto&){return PendingOfferAnswer::no;};
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  const auto r=process_player_card_pending_offers(g,clicked,game,rand,h,report);
  check(r.completed&&!report.unsupported&&r.transferred==1&&g.manager_records.at(0x21d)==25,"Source history count saturates at25");
  for(size_t i=1;i<30;++i)check(read(g.manager_records,0x760+i*4)==0x100000u+uint32_t(i-1)&&read(g.manager_records,0x7d8+i*4)==0x200000u+uint32_t(i-1),"both complete30-entry histories shift newest first");
 }
 // A successful owner-to-AI loan preserves its actual board and records its return.
 {
  auto g=original;g.memory.set8(list40+0x34,0x8c);
  std::vector<uint8_t> owner_board(g.tactics.begin()+board5,g.tactics.begin()+board5+0x58);
  int snapshot=-1,ledger=-1;
  for(int i=0;i<12&&snapshot<0;++i)if(g.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField+uint32_t(i)*2)==0xfff)snapshot=i;
  for(int i=0;i<200&&ledger<0;++i)if(g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+uint32_t(i)*4)==0)ledger=i;
  check(snapshot>=0&&ledger>=0,"genuine input has available loan records");
  const uint8_t old_count=g.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField);
  PendingOfferHooks h;h.answer=[](const auto&,const auto&){return PendingOfferAnswer::no;};
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  const auto r=process_player_card_pending_offers(g,clicked,game,rand,h,report);
  check(r.completed&&!report.unsupported&&r.transferred==1&&!contains(g,33,14)&&contains(g,17,14)&&game.calls()==0&&rand.calls()==1,"forced loan uses only actual human seller W and moves player");
  check(g.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField+uint32_t(snapshot)*2)==14&&g.memory.u8(::bmh::layout::ClubInfo_FormatError_RuntimeField2+uint32_t(snapshot))==0&&g.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField)==old_count+1&&g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+uint32_t(ledger)*4)==14u+33u*0x1000+17u*0x400000&&g.memory.u8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField+uint32_t(ledger))==12,"Source owner snapshot mapping/count and packed return ledger");
  for(uint32_t i=0;i<0x58;++i)check(g.memory.u8(::bmh::layout::ClubInfo_FormatError_RuntimeField+uint32_t(snapshot)*0x58+i)==owner_board.at(i),"loan retains original seller board before compaction");
  check(read(g.manager_records,0x760)==2u+original.date*4+247u*0x80000,"history loan bit retained");
 }
 // Full AI squads receive the Source notice after accounting/news for sales and loans.
 for(bool loan:{false,true}) {
  auto g=original;if(loan)g.memory.set8(list40+0x34,0x8c);
  const auto target=squad_range(17);int fill=-1;
  for(size_t i=target.first;i<target.first+target.count;++i)if((g.squad_slots.at(i)&0xfff)!=0xfff){fill=g.squad_slots.at(i);break;}
  check(fill>=0,"genuine AI squad has a player to fill test slots");
  for(size_t i=target.first;i<target.first+target.count;++i)g.squad_slots.at(i)=uint16_t(fill);
  const auto before=g;g.memory.add_block(::bmh::layout::Weekly_PendingSettleHumanBuyer_RuntimeField,1);std::vector<uint32_t> notices;
  PendingOfferHooks h;h.answer=[](const auto&,const auto&){return PendingOfferAnswer::no;};
  h.notice=[&](const auto& state,const auto& n){notices.push_back(n.text_address);check(read(state.manager_records,0x314)==read(before.manager_records,0x314)+2470000&&state.mailbox!=before.mailbox,"full-squad notice follows actual fee booking and news");};
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  const auto r=process_player_card_pending_offers(g,clicked,game,rand,h,report);
  check(r.completed&&!report.unsupported&&r.transferred==0&&contains(g,33,14)&&!contains(g,17,14)&&notices==std::vector<uint32_t>{::bmh::layout::PendingOffer_FormatError_ExecutableField}&&g.memory.u8(::bmh::layout::Weekly_PendingSettleHumanBuyer_RuntimeField)==1&&game.calls()==0&&rand.calls()==1,"full AI squad is supported refusal after Source settlement, including loans");
  check(read(g.manager_records,0x330)==read(before.manager_records,0x330)-2470000&&g.tactics==before.tactics&&g.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField)==before.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField)&&g.memory.u32(offers)==0xffffffff&&g.squad_slots.at(pool40)==0xfff,"full squad clears offer without move/history/loan effects");
  for(size_t i=0x760;i<0x850;++i)check(g.manager_records.at(i)==before.manager_records.at(i),"full squad has no successful transfer history");
 }
 // Either representation of transient DeskActions_EditorFail_RuntimeField2 suppresses the Source decline ACK.
 for(bool native:{false,true}) {
  auto g=original;g.memory.set32(offers,0x00003800);
  if(native){g.memory.add_block(::bmh::layout::DeskActions_EditorFail_RuntimeField2,1);g.memory.set8(::bmh::layout::DeskActions_EditorFail_RuntimeField2,1);}else g.transfer_notice_suppressed=true;
  PendingOfferHooks h;std::vector<uint32_t> notices;
  h.select_transfer=[](const auto&,const auto&){return PendingOfferTransferChoice{PendingOfferTransferChoiceKind::decline,-1};};
  h.notice=[&](const auto&,const auto& n){notices.push_back(n.text_address);};
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  const auto r=process_player_card_pending_offers(g,clicked,game,rand,h,report);
  check(r.completed&&!report.unsupported&&r.transferred==0&&notices.empty()&&game.calls()==1&&rand.calls()==0,"format field suppresses decline without altering quote RNG");
  check(native?g.memory.u8(::bmh::layout::DeskActions_EditorFail_RuntimeField2)==1:g.transfer_notice_suppressed,"decline preserves caller suppression context");
 }
 // An unknown unsaved format field gate remains unsupported; Native absence is not zero.
 {
  auto g=original;g.manager_teams.push_back(17);g.manager_records.resize(g.manager_teams.size()*0xb0c);
  const auto before=save_stream(g);PendingOfferHooks h;h.answer=[](const auto&,const auto&){return PendingOfferAnswer::no;};
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  const auto r=process_player_card_pending_offers(g,clicked,game,rand,h,report);
  check(!r.completed&&report.unsupported&&report.reason.find("unknown allocator")!=std::string::npos&&save_stream(g)==before&&game.calls()==0&&rand.calls()==0,"unknown human-buyer gate is unsupported before mutation/settlement RNG");
 }
 std::puts("format field pending-offer tests passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"pending_offer_tests: %s\n",e.what());return 1;}}
