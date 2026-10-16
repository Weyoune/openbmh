// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/club_player_detail.hpp"
#include "game/match_engine.hpp"
#include "game/pending_offer.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace bmh;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
GameState fixture() {
    GameState g;
    g.manager_teams = {0}; g.manager_records.resize(5 * 0xb0c); g.tactics.resize(5 * 0x840);
    g.memory.add_block(::bmh::layout::ManagerCount,1); g.memory.set8(::bmh::layout::ManagerCount,1);
    g.memory.add_block(::bmh::layout::ActiveManager,1); g.memory.set8(::bmh::layout::ActiveManager,0);
    g.memory.add_block(::bmh::layout::Career_RemoveJoinLoans_RuntimeField,800); g.memory.add_block(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField,200);
    g.memory.add_block(::bmh::layout::Career_CareerManagersEnd_RuntimeField,24); g.memory.add_block(::bmh::layout::ClubInfo_FormatError_RuntimeField2,12); g.memory.add_block(::bmh::layout::ClubInfo_FormatError_RuntimeField,0x420);
    g.memory.add_block(::bmh::layout::OfferSlots,16);
    for(int i=0;i<12;++i)g.memory.set16(::bmh::layout::Career_CareerManagersEnd_RuntimeField+uint32_t(i)*2,0x0fff);
    for(int i=0;i<4;++i)g.memory.set32(::bmh::layout::OfferSlots+uint32_t(i)*4,0xffffffff);
    g.squad_slots.assign(3096,kEmptySlot); g.players.resize(3); g.player_cards.resize(3); g.player_bonus.resize(3);
    g.player_name_offsets = {0,5,10}; g.player_names = {'T','E','S','T',0,'H','I','G','H',0,'L','O','A','N',0};
    g.players[0].w364 = 1; // raw fee49764: Source low-value W branch
    g.players[1].w35c = (80u<<7)|(80u<<14); g.players[1].w364 = 1;
    g.players[2].w364 = 1;
    g.squad_slots[0]=0x8000; g.squad_slots[24]=0x8001;
    g.tactics[0xf]=62;
    return g;
}
struct Oracle {
    uint32_t w, g; uint64_t wc=0,gc=0;
    int W(int low,int high){++wc;w=w*0x41c64e6d+0x3039;return int(((w>>16)&32767)*uint32_t(high-low+1)/32768)+low;}
    int G(int low,int high){++gc;g=(g*16+167)%4000000;return int(g*uint32_t(high-low+1)/4000000)+low;}
};
void expect_portrait(const ClubPlayerDetailRedraw& actual, const GameRandom& game, const WatcomRandom& rand,
                     Oracle& oracle, const LeImage& exe, const std::string& name, bool low) {
    oracle.g=0x14344;
    for(size_t i=0;i<name.size();++i)oracle.g+=uint32_t(int32_t(int8_t(name[i])))*uint32_t(i)*10;
    oracle.G(0,100);
    check(actual.portrait_base==oracle.G(0,3),"Source portrait base draw");
    for(size_t i=0;i<5;++i)check(actual.portrait_features[i]==oracle.G(0,exe.u8(::bmh::layout::ClubPlayerDetail_ClubPlayerDetailRedraw_RuntimeField2+uint32_t(i))-1),"Source portrait feature draw order");
    const int burn=oracle.W(100,300);for(int i=0;i<burn;++i)oracle.G(0,1);
    check(actual.portrait_burn_count==burn,"format field W-controlled G burn");
    if(low){const uint32_t fee=uint32_t(oracle.W(50000,80000));check(actual.market_value==((fee+(fee>>2))/10000)*10000,"Source low-value W then7a998 rounding");}
    check(uint32_t(game.seed())==oracle.g && game.calls()==oracle.gc && rand.seed()==oracle.w && rand.calls()==oracle.wc,"exact Source G/W streams and call counts");
}

void pool_boundaries_and_offers(const LeImage& exe) {
    auto g=fixture();
    // Literal format field+entry, independent of the exported port constants.
    g.squad_slots[2928]=0x4002;g.squad_slots[2967]=0x8002;
    ClubPlayerDetailSelection pool{-1,2,3,-1,-1,ClubPlayerDetailOrigin::market_pool,2967};
    check(club_player_detail_context(g,pool).source_slot==2967,
          "last entry39 uses its Native identity despite sorted visible row3");
    auto first=pool;first.market_pool_slot=2928;first.rendered_row=17;
    check(club_player_detail_context(g,first).displayed_slot==0x4002,
          "first entry0 retains Native flags despite visible row17");
    GameRandom game(123456);WatcomRandom rand(987654321);
    const auto rejected_before_rng=[&](const ClubPlayerDetailSelection& bad) {
        bool rejected=false;
        try{club_player_detail_redraw(g,bad,exe,game,rand);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected && game.seed()==123456 && game.calls()==0 && rand.seed()==987654321 && rand.calls()==0,
              "invalid Native pool identity rejects before either RNG");
    };
    auto bad=pool;bad.market_pool_slot=2927;rejected_before_rng(bad);
    bad.market_pool_slot=2968;rejected_before_rng(bad);
    bad=pool;bad.rendered_row=-1;rejected_before_rng(bad);
    bad.rendered_row=40;rejected_before_rng(bad);
    bad=pool;bad.player_id=1;rejected_before_rng(bad);
    g.squad_slots[2967]=0x0fff;rejected_before_rng(pool);
    g.squad_slots[2967]=0x4fff;bad=pool;bad.player_id=4095;rejected_before_rng(bad);
    // 4fff is flagged player4095, not the canonical 0fff empty word.
    g.players.resize(4096);g.player_cards.resize(4096);g.player_bonus.resize(4096);
    g.player_name_offsets.resize(4096,0);
    check(club_player_detail_context(g,bad).displayed_slot==0x4fff,
          "flagged fff is accepted only with all actual global player tables");
    g.squad_slots[2967]=0x8002;
    g.memory.set32(::bmh::layout::OfferSlots,(1u<<10)|0u); // wrong player, owner0
    g.memory.set32(::bmh::layout::Weekly_PreMatchOffer_RuntimeField,(2u<<10)|3u); // right player, owner3
    check(club_player_detail_context(g,pool).active_offer==1,
          "unowned ff wildcard skips wrong ID and matches a different owner");
    g.memory.set32(::bmh::layout::OfferSlots,(2u<<10)|1u);
    check(club_player_detail_context(g,pool).active_offer==0,
          "format field returns the first match irrespective of offer owner");
    g.squad_slots[1]=2;const ClubPlayerDetailSelection owned{0,2,1,1,-1};
    check(club_player_detail_context(g,owned).active_offer==-1,
          "ordinary owner0 does not wildcard owners1/3");
    g.memory.set32(::bmh::layout::Weekly_PreMatchOffer_RuntimeField2,(2u<<10)|0u);
    check(club_player_detail_context(g,owned).active_offer==2,
          "ordinary owner0 selects only matching owner and player");
    const auto live_slots=g.squad_slots;const auto live_boards=g.tactics;const auto live_records=g.manager_records;
    const uint32_t live_offer=g.memory.u32(::bmh::layout::OfferSlots);
    auto staged=g;auto staged_game=game;auto staged_rand=rand;
    MinuteReport report;PendingOfferHooks hooks;
    const auto result=process_player_card_pending_offers(staged,pool,staged_game,staged_rand,hooks,report);
    check(report.unsupported && !result.completed,
          "available unowned pool offer action remains explicit unsupported at the existing adapter");
    check(staged.squad_slots==live_slots && staged.tactics==live_boards && staged.manager_records==live_records &&
          staged.memory.u32(::bmh::layout::OfferSlots)==live_offer && staged_game.calls()==0 && staged_rand.calls()==0,
          "no-board adapter rejects without mutating staged Native tables or RNG");
    // Discard staged state/RNG on unsupported; do not commit them to the caller.
    check(g.squad_slots==live_slots && g.tactics==live_boards && g.manager_records==live_records &&
          g.memory.u32(::bmh::layout::OfferSlots)==live_offer && game.calls()==0 && rand.calls()==0,
          "unsupported staged dispatch leaves live Native identity, offers and both RNGs intact");
    g.memory.set32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField,2u|(1u<<12)|(4u<<22));g.memory.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField,9);
    const auto history=club_player_detail_context(g,pool);
    check(history.primary_club==1 && history.secondary_club==4 && history.loan_duration==9 &&
          history.loan_record==0 && !history.board && history.source_manager==-1,
          "packed pool history supplies display clubs/duration without inventing ownership");
}

void pool_national_resolution(const LeImage& exe) {
    auto g=fixture();g.manager_teams={357};g.manager_records[4*0xb0c+0x290]=1;
    g.squad_slots[2928]=0x4001;g.squad_slots[2967]=0x8002;
    g.tactics[4*0x840+0xf]=53;
    const ClubPlayerDetailSelection ordinary_copy{-1,1,9,-1,-1,ClubPlayerDetailOrigin::market_pool,2928};
    const auto candidate=club_player_detail_context(g,ordinary_copy);
    check(candidate.national_mode==1 && candidate.source_slot==24 && candidate.displayed_slot==0x8001 &&
          candidate.primary_club==1 && candidate.source_manager==4 && candidate.board &&
          (*candidate.board)[0xf]==53 && candidate.national_action_available && candidate.active_offer==-1 &&
          candidate.selection.market_pool_slot==2928 && candidate.selection.rendered_row==9 && candidate.market_pool,
          "mode1 pool origin remaps first ordinary copy before owner/board and retains typed pool identity");
    g.memory.add_block(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField3,1);g.memory.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField3,0);g.tactics[0xf]=71;
    const auto overridden=club_player_detail_context(g,ordinary_copy);
    check(overridden.source_manager==0 && overridden.board && (*overridden.board)[0xf]==71,
          "format field bbce2 owner override applies after pool-origin national remap");
    g.memory.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField3,0xff);
    check(!club_player_detail_context(g,ordinary_copy).board,
          "Source ff override remains unowned without a synthetic board");
    g.memory.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField3,0);
    GameRandom game(123456);WatcomRandom rand(987654321);Oracle oracle{987654321,123456};
    const auto redraw=club_player_detail_redraw(g,ordinary_copy,exe,game,rand);
    expect_portrait(redraw,game,rand,oracle,exe,"HIGH",false);
    check(redraw.context.source_slot==24 && redraw.market_value && *redraw.market_value>80000,
          "remapped national pool redraw uses Source ordinary copy and exact portrait/RNG path");
    g.squad_slots[3072]=1;
    const auto selected=club_player_detail_context(g,ordinary_copy);
    check(selected.national_mode==2 && selected.source_slot==24 && selected.primary_club==1 &&
          selected.secondary_club==357 && selected.selection.market_pool_slot==2928 && selected.national_action_available,
          "mode2 pool origin uses first copy and adds the national display club");
    const ClubPlayerDetailSelection pool_only{-1,2,0,-1,-1,ClubPlayerDetailOrigin::market_pool,2967};
    const auto only=club_player_detail_context(g,pool_only);
    check(only.national_mode==1 && only.source_slot==2967 && only.displayed_slot==0x8002 &&
          only.source_manager==-1 && !only.board && only.primary_club==-1 && only.national_action_available,
          "pool-only German candidate retains an unowned card and the Source nomination button");
    g.players[2].w368=1u<<27;
    check(!club_player_detail_context(g,pool_only).national_action_available,
          "foreign candidate has no direct national button without a matching offer");
    g.players[2].w368=0;g.player_bonus[2]=0x80;
    check(!club_player_detail_context(g,pool_only).national_action_available,
          "Source nationality bonus flag32 also rejects direct national eligibility");
    g.player_bonus[2]=0;g.memory.add_block(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField2,1);g.memory.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField2,2);
    check(!club_player_detail_context(g,pool_only).national_action_available,
          "af43d bit2 selects the offer fallback and no button without an offer");
    g.memory.set32(::bmh::layout::Weekly_PreMatchOffer_RuntimeField2,(2u<<10)|3u);
    const auto fallback=club_player_detail_context(g,pool_only);
    check(fallback.active_offer==2 && fallback.national_mode==1 && fallback.national_action_available && !fallback.board,
          "af43d override preserves wildcard fallback button with national dispatch");
    g.memory.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField2,8);
    check(club_player_detail_context(g,pool_only).active_offer==2,
          "af43d bit8 also selects the format field fallback");
    g.memory.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField2,1);
    const auto unrelated_bit=club_player_detail_context(g,pool_only);
    check(unrelated_bit.active_offer==-1 && unrelated_bit.national_action_available,
          "unrelated af43d bit1 does not override the direct German button");
    g.players[2].w368=1u<<27;
    check(club_player_detail_context(g,pool_only).national_action_available,
          "foreign player with an actual fallback offer preserves Source national dispatch gate");
    auto action_state=fixture();action_state.manager_teams={357};action_state.lineups_frozen=true;
    action_state.manager_records[0]=1;action_state.squad_slots[2967]=0x4002;
    MinuteReport report;WatcomRandom action_rand(987654321);Oracle action_oracle{987654321,0};
    auto expected_removed_slots=action_state.squad_slots;
    expected_removed_slots[2967]=0x0fff; // format field also clears matching CareerRuntimeField..CareerRuntimeField2 pool copies.
    check(club_player_detail_national_action(action_state,pool_only,action_rand,report),
          "actual pool-only German nomination is supported by the existing national mutation");
    check(action_state.squad_slots[3072]==0x4002 && action_state.squad_slots[2967]==0x4002 &&
          action_state.tactics[0xf]==action_oracle.W(40,60),
          "national nomination copies actual pool Native flags and initializes Source morale first");
    action_oracle.W(95,105);
    check(action_rand.calls()==2 && action_rand.seed()==action_oracle.w && !report.unsupported,
          "pool-only nomination retains the independently expected two W draws");
    const auto nominated_calls=action_rand.calls();const auto nominated_seed=action_rand.seed();
    check(club_player_detail_national_action(action_state,pool_only,action_rand,report),
          "same typed pool selection becomes mode2 and dispatches actual national removal");
    check(action_state.squad_slots==expected_removed_slots && action_rand.calls()==nominated_calls &&
          action_rand.seed()==nominated_seed && !report.unsupported,
          "Source national removal clears the national and matching transfer-pool copies without RNG");
}
}
int main(int argc,char**argv){try{
    if(argc!=2 || std::string(argv[1]).empty()){std::cout<<"SKIP: BMH_DATA unavailable\n";return 77;}
    const auto exe=load_le(std::filesystem::path(argv[1])/"BMH.exe");
    pool_boundaries_and_offers(exe);pool_national_resolution(exe);
    auto g=fixture();const auto slots=g.squad_slots;const auto records=g.manager_records,boards=g.tactics;
    ClubPlayerDetailSelection human{0,0,0,0,-1},ai{1,1,0,0,-1};
    const auto human_context=club_player_detail_context(g,human),ai_context=club_player_detail_context(g,ai);
    check(human_context.board && (*human_context.board)[0xf]==62 && human_context.source_manager==0,"actual human runtime board");
    check(!ai_context.board && ai_context.source_manager==-1 && ai_context.primary_club==1,"AI context has no substituted active manager board");
    GameRandom game(123456);WatcomRandom rand(987654321);Oracle oracle{987654321,123456};
    const auto first=club_player_detail_redraw(g,human,exe,game,rand);
    expect_portrait(first,game,rand,oracle,exe,"TEST",true);
    const auto second=club_player_detail_redraw(g,human,exe,game,rand);
    expect_portrait(second,game,rand,oracle,exe,"TEST",true);
    check(first.portrait_base==second.portrait_base && first.portrait_features==second.portrait_features,"name reset preserves portrait on repeated real redraw");
    const auto high=club_player_detail_redraw(g,ai,exe,game,rand);
    expect_portrait(high,game,rand,oracle,exe,"HIGH",false);
    check(high.market_value && *high.market_value>80000,"high-value card uses computed value without extra W draw");
    const int pool_slot=kClubPlayerDetailMarketPoolFirst+7;
    g.squad_slots[size_t(pool_slot)]=0x4002;
    const auto pool_slots=g.squad_slots;
    g.memory.set32(::bmh::layout::OfferSlots,0x80000000u|(2u<<10)); // format field: ff owner matches any offer owner.
    const ClubPlayerDetailSelection pool{-1,2,7,-1,-1,ClubPlayerDetailOrigin::market_pool,pool_slot};
    const auto pool_context=club_player_detail_context(g,pool);
    check(pool_context.market_pool && pool_context.source_slot==pool_slot && pool_context.displayed_slot==0x4002 &&
          !pool_context.board && pool_context.source_manager==-1 && pool_context.primary_club==-1 &&
          pool_context.active_offer==0 && !pool_context.national_action_available,
          "market pool retains actual slot/flags and wildcard offer without inventing owner/board");
    const auto pool_redraw=club_player_detail_redraw(g,pool,exe,game,rand);
    expect_portrait(pool_redraw,game,rand,oracle,exe,"LOAN",true);
    check(pool_redraw.market_value.has_value() && pool_redraw.context.displayed_slot==0x4002,
          "market pool computes its Source value from the actual Native slot");
    auto wrong_pool=pool;wrong_pool.player_id=1;
    const auto before_pool_g=game.seed();
    const auto before_pool_w=rand.seed();
    const auto before_pool_gc=game.calls(),before_pool_wc=rand.calls();
    bool stale_pool_rejected=false;
    try{club_player_detail_redraw(g,wrong_pool,exe,game,rand);}catch(const std::invalid_argument&){stale_pool_rejected=true;}
    check(stale_pool_rejected && game.seed()==before_pool_g && rand.seed()==before_pool_w &&
          game.calls()==before_pool_gc && rand.calls()==before_pool_wc,"stale pool identity rejects before RNG mutation");
    auto empty_pool=pool;empty_pool.market_pool_slot=kClubPlayerDetailMarketPoolFirst+8;empty_pool.rendered_row=8;
    bool empty_pool_rejected=false;
    try{club_player_detail_context(g,empty_pool);}catch(const std::invalid_argument&){empty_pool_rejected=true;}
    check(empty_pool_rejected,"canonical fff Native pool row is rejected as empty");
    auto invalid_origin=pool;invalid_origin.origin=static_cast<ClubPlayerDetailOrigin>(0xff);
    bool invalid_origin_rejected=false;
    try{club_player_detail_redraw(g,invalid_origin,exe,game,rand);}catch(const std::invalid_argument&){invalid_origin_rejected=true;}
    check(invalid_origin_rejected && game.seed()==before_pool_g && rand.seed()==before_pool_w &&
          game.calls()==before_pool_gc && rand.calls()==before_pool_wc,"invalid origin rejects before RNG mutation");
    check(g.squad_slots==pool_slots && g.manager_records==records && g.tactics==boards,"card redraw leaves game tables unchanged");
    auto invalid=human;invalid.player_id=2;
    const auto oldg=game.seed();const auto oldw=rand.seed();const auto oldgc=game.calls(),oldwc=rand.calls();
    bool rejected=false;try{club_player_detail_redraw(g,invalid,exe,game,rand);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected && game.seed()==oldg && rand.seed()==oldw && game.calls()==oldgc && rand.calls()==oldwc,"stale selection rejects before any RNG mutation");
    g.memory.set16(::bmh::layout::Career_CareerManagersEnd_RuntimeField,2);g.memory.set8(::bmh::layout::ClubInfo_FormatError_RuntimeField2,0);g.memory.set8(::bmh::layout::ClubInfo_FormatError_RuntimeField+0xf,71);
    g.memory.set32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField,2u|(0u<<12)|(1u<<22));g.memory.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField,6);
    ClubPlayerDetailSelection loan{0,2,23,-1,0};
    const auto loan_context=club_player_detail_context(g,loan);
    check(loan_context.human_loan && loan_context.board && (*loan_context.board)[0xf]==71 &&
          loan_context.primary_club==0 && loan_context.secondary_club==1 && loan_context.loan_duration==6,"loan board and lender/borrower identities are explicit");
    const auto loan_redraw=club_player_detail_redraw(g,loan,exe,game,rand);
    expect_portrait(loan_redraw,game,rand,oracle,exe,"LOAN",false);
    check(!loan_redraw.market_value,"Source human-loan card suppresses fields2..5 and valuation");
    GameRandom no_portrait(123456);WatcomRandom no_portrait_w(987654321);Oracle only_value{987654321,123456};
    const auto hidden=club_player_detail_redraw(g,human,exe,no_portrait,no_portrait_w,false);
    const uint32_t fee=uint32_t(only_value.W(50000,80000));
    check(!hidden.portrait_available && no_portrait.calls()==0 && no_portrait.seed()==123456 &&
          no_portrait_w.calls()==1 && no_portrait_w.seed()==only_value.w && hidden.market_value==((fee+(fee>>2))/10000)*10000,"Source heap gate omits portrait draws but retains valuation");
    g.manager_teams={kNoTeam};g.manager_records[4*0xb0c+0x290]=1;
    g.tactics[4*0x840+0xf]=53;
    const auto fallback=club_player_detail_context(g,ai);
    check(fallback.source_manager==4 && fallback.board && (*fallback.board)[0xf]==53 && fallback.national_mode==1,"national fifth manager fallback for runtime club");
    g.squad_slots[3072]=1;
    const auto nominated=club_player_detail_context(g,ClubPlayerDetailSelection{kNoTeam,1,0,0,-1});
    check(nominated.national_mode==2 && nominated.source_slot==24 && nominated.primary_club==1 && nominated.secondary_club==kNoTeam,"national card resolves explicit domestic slot and both clubs");
    // The annual cup modal runs with format field (lineups frozen). The button
    // still copies/removes the national slot and board; it does not alter the
    // ordinary club. New national row0 draws morale and salary-percent; the
    // two-year development exceeds the low-value gate for this fixture.
    auto national_action=fixture(); national_action.manager_teams={kNoTeam};
    national_action.manager_records[0]=1; national_action.lineups_frozen=true;
    national_action.memory.set32(::bmh::layout::OfferSlots,0); // Source German-national button takes precedence over an offer.
    ClubPlayerDetailSelection candidate{0,0,0,0,-1};
    MinuteReport report; WatcomRandom action_rand(987654321);
    Oracle action_oracle{987654321,0};
    const auto ordinary_before=national_action.squad_slots[0];
    check(club_player_detail_national_action(national_action,candidate,action_rand,report),"Source national nominate button accepts candidate");
    check(national_action.squad_slots[3072]==ordinary_before && national_action.squad_slots[0]==ordinary_before,"nomination copies slot without moving ordinary club");
    for(int at=2;at<6;++at)check(national_action.tactics[size_t(at)]==1,"first national row training values are literal1");
    check(national_action.tactics[0xf]==action_oracle.W(40,60),"nomination board morale draw precedes salary draws");
    action_oracle.W(95,105);
    check(action_rand.calls()==2 && action_rand.seed()==action_oracle.w,"exact nomination W draw order/count");
    auto selected=candidate; selected.runtime_team=kNoTeam;
    const auto before_remove_seed=action_rand.seed();const auto before_remove_calls=action_rand.calls();
    national_action.squad_slots[3073]=1; national_action.tactics[0x58]=77;
    check(club_player_detail_national_action(national_action,selected,action_rand,report),"Source national remove button accepts selected player");
    check(national_action.squad_slots[3072]==1 && national_action.squad_slots[3095]==kEmptySlot && national_action.tactics[0]==77,"national removal compacts slots and manager board");
    check(action_rand.seed()==before_remove_seed && action_rand.calls()==before_remove_calls,"national remove has no random draws");
    national_action.memory.add_block(::bmh::layout::MatchEventsDisabled,1); national_action.memory.set8(::bmh::layout::MatchEventsDisabled,1);
    auto remove_blocked=ClubPlayerDetailSelection{kNoTeam,1,0,0,-1};
    const auto blocked_slots=national_action.squad_slots;const auto blocked_board=national_action.tactics;
    club_player_detail_national_action(national_action,remove_blocked,action_rand,report);
    check(national_action.squad_slots==blocked_slots && national_action.tactics==blocked_board,"format field suppresses removal while frozen lineup stays unchanged");
    check(club_player_detail_input_mode(0)==1 && club_player_detail_input_mode(1)==1 && club_player_detail_input_mode(2)==0,"format field temporary mode mapping");
    std::cout<<"Generic club player-detail context and exact Source redraw RNG tests passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
