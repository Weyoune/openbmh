// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/club_player_detail.hpp"
#include "ui/source_hold.hpp"
#include "ui/manager.hpp"
#include <iostream>
#include <stdexcept>

using namespace bmh;
using namespace bmh::ui;
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
GameState fixture(const LeImage& exe){
    GameState g;g.manager_teams={0};g.manager_records.resize(5*0xb0c);g.manager_options.resize(20);
    g.tactics.resize(5*0x840);g.squad_slots.assign(3096,0x0fff);
    g.players.resize(2);g.player_cards.resize(2);g.player_bonus.resize(2);
    g.player_names={'N','E','U','E','R',0};g.player_name_offsets={0,0};
    g.club_names={'F','C','B',0};g.club_ids.resize(419);g.club_heap_78.resize(838);
    g.memory.add_block(::bmh::layout::ManagerCount,1);g.memory.set8(::bmh::layout::ManagerCount,1);
    g.memory.add_block(::bmh::layout::ActiveManager,1);g.memory.add_block(::bmh::layout::Flags,12);
    g.memory.add_block(::bmh::layout::ClubShortNameOffsets,exe.u16(::bmh::layout::CareerSession_DataDirectory_RuntimeField2));g.memory.set8(::bmh::layout::ClubShortNameOffsets,2);g.memory.set8(::bmh::layout::ClubShortNameOffsets+1,2);
    g.memory.add_block(::bmh::layout::OfferSlots,16);for(int i=0;i<4;++i)g.memory.set32(::bmh::layout::OfferSlots+i*4,0xffffffff);
    g.memory.add_block(::bmh::layout::Career_RemoveJoinLoans_RuntimeField,800);g.memory.add_block(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField,200);
    g.players[0].w364=g.players[1].w364=1;g.squad_slots[0]=0x8000;g.squad_slots[24]=0x8001;
    g.manager_records[0x286]=3;g.memory.set8(::bmh::layout::FixturePlayerCardFlagByte,0x41);
    g.tactics[0xc]=24;g.tactics[0x11]=2;g.tactics[0x19]=30;g.tactics[0x1a]=60;g.tactics[0x1b]=90;
    return g;
}
// Independent format field oracle: opaque base, DesktopRuntimeField color0 feature key.
Image portrait_oracle(const ClubPlayerDetailRedraw& r,const LeImage& exe,const Image& atlas){
    Image expected(38,38);
    for(int y=0;y<38;++y)for(int x=0;x<38;++x)
        expected.at(x,y)=atlas.pixels[size_t(168+y)*atlas.width+108+r.portrait_base*38+x];
    for(int i=0;i<5;++i){
        int sx=exe.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField+i),sy=exe.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField2+i);
        const int width=exe.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField3+i),height=exe.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField4+i);
        for(int n=0;n<r.portrait_features[size_t(i)];++n){sx+=width;if(sx>exe.u16(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField7+i*2)){sx=exe.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField+i);sy+=height;}}
        const int dx=1+exe.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField5+i),dy=5+exe.u8(::bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField6+i);
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){
            const uint8_t color=atlas.pixels[size_t(sy+y)*atlas.width+sx+x];
            if(color!=0&&dx+x<38&&dy+y<38)expected.at(dx+x,dy+y)=color;
        }
    }
    return expected;
}

void pool_national_cards(const std::filesystem::path& data,const LeImage& exe,const Image& parent) {
    auto g=fixture(exe);g.manager_teams={357};g.manager_records[4*0xb0c+0x290]=1;
    g.squad_slots[2928]=0x4001;g.tactics[4*0x840+0xf]=53;
    GameRandom game(123456);WatcomRandom rand(987654321);
    const ClubPlayerDetailSelection pool{-1,1,17,-1,-1,ClubPlayerDetailOrigin::market_pool,2928};
    auto p=club_player_detail_from_game(g,club_player_detail_redraw(g,pool,exe,game,rand),exe);
    check(p.redraw.context.national_mode==1 && p.redraw.context.source_slot==24 && p.redraw.context.board &&
          p.redraw.context.displayed_slot==0x8001 && p.clubs[0] && p.clubs[0]->runtime_team==1 &&
          p.redraw.context.selection.rendered_row==17 && p.redraw.context.selection.market_pool_slot==2928,
          "national candidate projection uses first ordinary copy/board while preserving sorted pool selection");
    ClubPlayerDetailScreen candidate(data,p,parent);candidate.render();candidate.press(190,215);candidate.release(190,215);
    const auto nominate=candidate.take_request();
    check(nominate.kind==ClubPlayerDetailRequestKind::national_action && nominate.source_return==255 &&
          nominate.selection.origin==ClubPlayerDetailOrigin::market_pool && nominate.selection.market_pool_slot==2928 &&
          nominate.selection.rendered_row==17,
          "mode1 pool-origin owned card emits national request from Source x182 action button");
    g.squad_slots[3072]=1;
    auto selected_p=club_player_detail_from_game(g,club_player_detail_redraw(g,pool,exe,game,rand),exe);
    check(selected_p.redraw.context.national_mode==2 && selected_p.redraw.context.source_slot==24 &&
          selected_p.clubs[1] && selected_p.clubs[1]->runtime_team==357,
          "mode2 pool projection retains resolved ordinary card plus national club");
    ClubPlayerDetailScreen selected_card(data,selected_p,parent);selected_card.render();
    selected_card.press(190,215);selected_card.release(190,215);
    check(selected_card.take_request().kind==ClubPlayerDetailRequestKind::national_action,
          "mode2 pool-origin selected card emits explicit national removal request");
    ClubPlayerDetailScreen held(data,p,parent);
    check(!held.held_left_ticks(uint32_t(pointer_hold_source_ticks(520)))&&
          !held.held_left_ticks(uint32_t(pointer_hold_source_ticks(1428))),
          "selection card stays open at520ms and before26 BIOS ticks, unlike the former20ms conversion");
    check(held.held_left_ticks(uint32_t(pointer_hold_source_ticks(1429))),"national resolved copy honors Source held exit");
    check(held.take_request().source_return==25,"Source held return uses resolved slot24 plus1, never visible row17/pool2928");
    // Remove the ordinary copy: both modes can resolve to the actual pool slot.
    g.squad_slots[24]=0x0fff;g.squad_slots[3072]=0x0fff;
    auto only_p=club_player_detail_from_game(g,club_player_detail_redraw(g,pool,exe,game,rand),exe);
    check(only_p.redraw.context.national_mode==1 && only_p.redraw.context.source_slot==2928 &&
          !only_p.redraw.context.board && !only_p.clubs[0] && only_p.statistics_months==0 &&
          !only_p.displayed_offer_amount && only_p.redraw.context.national_action_available,
          "German pool-only candidate has a real nomination action with no board/statistics/salary");
    const auto before_g=game.calls(),before_w=rand.calls();
    ClubPlayerDetailScreen only_card(data,only_p,parent);const auto direct_frame=only_card.render();
    only_card.press(100,215);only_card.release(100,215);
    check(!only_card.in_statistics() && only_card.take_request().kind==ClubPlayerDetailRequestKind::national_action &&
          game.calls()==before_g && rand.calls()==before_w,
          "no-stats x80 national button uses paired input without RNG");
    g.squad_slots[3072]=1;
    auto only_selected=club_player_detail_from_game(g,club_player_detail_redraw(g,pool,exe,game,rand),exe);
    check(only_selected.redraw.context.national_mode==2 && !only_selected.clubs[0] && only_selected.clubs[1],
          "selected pool-only card has absent ordinary club and actual national club");
    ClubPlayerDetailScreen only_selected_card(data,only_selected,parent);only_selected_card.render();
    only_selected_card.press(100,215);only_selected_card.release(100,215);
    check(only_selected_card.take_request().kind==ClubPlayerDetailRequestKind::national_action,
          "pool-only mode2 renderer handles format field no-club sentinel and removal action");
    g.squad_slots[3072]=0x0fff;g.players[1].w368=1u<<27;
    auto foreign_p=club_player_detail_from_game(g,club_player_detail_redraw(g,pool,exe,game,rand),exe);
    ClubPlayerDetailScreen foreign(data,foreign_p,parent);foreign.render();foreign.press(100,215);
    check(foreign.take_request().kind==ClubPlayerDetailRequestKind::close,
          "foreign pool candidate with no actual offer has no invented footer action");
    g.players[1].w368=0;g.memory.add_block(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField2,1);g.memory.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField2,2);
    auto override_p=club_player_detail_from_game(g,club_player_detail_redraw(g,pool,exe,game,rand),exe);
    ClubPlayerDetailScreen overridden(data,override_p,parent);overridden.press(100,215);
    check(overridden.take_request().kind==ClubPlayerDetailRequestKind::close,
          "Source override falls back to absent offers without synthesizing a button");
    g.memory.set32(::bmh::layout::Weekly_PreMatchOffer_RuntimeField3,0x80000000u|(1u<<10)|3u);
    auto fallback_p=club_player_detail_from_game(g,club_player_detail_redraw(g,pool,exe,game,rand),exe);
    check(fallback_p.redraw.context.active_offer==3 && fallback_p.redraw.context.national_action_available &&
          !fallback_p.displayed_offer_amount && !fallback_p.redraw.context.board,
          "national override shows real wildcard offer button while salary stays board-gated");
    ClubPlayerDetailScreen fallback(data,fallback_p,parent);const auto fallback_frame=fallback.render();
    bool footer_label_differs=false;
    for(int y=211;y<=221;++y)for(int x=80;x<=202;++x)
        footer_label_differs |= direct_frame.pixels[size_t(y)*320+x]!=fallback_frame.pixels[size_t(y)*320+x];
    check(footer_label_differs,"Source fallback offer label differs from the direct national nomination label");
    fallback.press(100,215);fallback.release(100,215);
    check(fallback.take_request().kind==ClubPlayerDetailRequestKind::national_action,
          "format field dispatch stays national even when af43d selected the offer label");
}
}
int main(int argc,char** argv){try{
    if(argc!=2||std::string(argv[1]).empty()){std::cout<<"SKIP: BMH_DATA unavailable\n";return 77;}
    const std::filesystem::path data=argv[1];const auto exe=load_le(asset_path(data,"BMH.exe"));
    auto g=fixture(exe);GameRandom game(123456);WatcomRandom rand(987654321);
    const ClubPlayerDetailSelection selected{0,0,0,0,-1};
    auto redraw=club_player_detail_redraw(g,selected,exe,game,rand);
    auto p=club_player_detail_from_game(g,redraw,exe);
    check(p.statistics_flags==0x41&&p.statistics_months==3,"active Source statistics preferences/months owned");
    Image parent(320,240,0x52);ClubPlayerDetailScreen card(data,p,parent);
    {
        const auto timer_g=game.calls(),timer_w=rand.calls();
        ClubPlayerDetailScreen timed(data,p,parent);
        timed.press(100,215);
        check(!timed.process_timer_expired() && timed.source_mouse_wait_release_pending(),
              "timer cannot steal format field's paired statistics release");
        timed.release(100,215);
        check(timed.in_statistics() && timed.process_timer_expired(),
              "expired statistics returns through format field before the card");
        const auto statistics_return=timed.take_request();
        check(statistics_return.kind==ClubPlayerDetailRequestKind::redraw_card &&
              statistics_return.statistics_flags==p.statistics_flags && !statistics_return.close_parent() &&
              !timed.process_timer_expired(),"statistics expiry emits one preference-preserving rebuild request");
        timed.replace_redraw(p);
        check(timed.process_timer_expired(),"after the owned rebuild, expiry closes the card");
        const auto close=timed.take_request();
        check(close.kind==ClubPlayerDetailRequestKind::close && close.source_return==0 &&
              !close.close_parent() && !timed.process_timer_expired() &&
              !timed.release(100,215) && timed.take_request().kind==ClubPlayerDetailRequestKind::none,
              "timer returns0 once, never activates a departed button or closes ClubInfo with a selection");
        auto actionable=p;actionable.redraw.context.national_action_available=true;
        actionable.redraw.context.national_mode=1;
        ClubPlayerDetailScreen action_timer(data,actionable,parent);
        check(action_timer.process_timer_expired() &&
              action_timer.take_request().kind==ClubPlayerDetailRequestKind::close,
              "expiry cannot nominate a player through an available national button");
        ClubPlayerDetailScreen committed(data,actionable,parent);
        committed.press(190,215);committed.release(190,215);
        check(!committed.process_timer_expired() &&
              committed.take_request().kind==ClubPlayerDetailRequestKind::national_action,
              "timer cannot replace an already committed typed action");
        check(game.calls()==timer_g && rand.calls()==timer_w,
              "timer dispatch, painting and modal requests consume neither RNG");
    }
    const auto rendered=card.render();const auto gc=game.calls(),wc=rand.calls();
    check(card.render().pixels==rendered.pixels&&game.calls()==gc&&rand.calls()==wc,"pure repeated render does not consume RNG");
    const auto face=portrait_oracle(redraw,exe,load_vga(asset_path(data/"KB","29.VGA")));
    for(int y=0;y<38;++y)for(int x=0;x<38;++x)
        check(rendered.pixels[size_t(109+y)*320+181+x]==face.pixels[size_t(y)*38+x],"all1444 portrait pixels match Source opaque/color0 copy oracle");
    check(!card.release(20,215)&&!card.key("ESC")&&card.take_request().kind==ClubPlayerDetailRequestKind::none,"unpaired release and ESC ignored");
    check(card.press(100,215)&&card.render().pixels!=rendered.pixels,"stats button press has Original depressed pixels");
    check(card.move(100,180)&&!card.release(100,215)&&!card.in_statistics(),"moving outside cancels button press");
    card.press(100,215);card.release(100,215);check(card.in_statistics(),"actual paired stats press/release opens graph");
    check(card.take_request().kind==ClubPlayerDetailRequestKind::none&&game.calls()==gc&&rand.calls()==wc,"opening statistics has no redraw or RNG");
    const auto graph=card.render();card.press(30,145);card.release(30,145);
    check(card.projection().statistics_flags==0x40&&g.memory.u8(::bmh::layout::FixturePlayerCardFlagByte)==0x41,"curve toggle changes local flags only, Source persists on stats exit");
    check(card.render().pixels!=graph.pixels&&card.take_request().kind==ClubPlayerDetailRequestKind::none,"curve redraw remains entirely pure");
    card.press(100,190);card.release(100,190);
    const auto req=card.take_request();
    check(req.kind==ClubPlayerDetailRequestKind::redraw_card&&req.statistics_flags==0x40&&!req.close_parent()&&card.awaiting_redraw(),"statistics exit requests exactly one actual Core redraw");
    check(card.take_request().kind==ClubPlayerDetailRequestKind::none,"redraw request consumed once");
    g.memory.set8(::bmh::layout::FixturePlayerCardFlagByte,req.statistics_flags);
    card.replace_redraw(club_player_detail_from_game(g,club_player_detail_redraw(g,selected,exe,game,rand),exe));
    check(!card.awaiting_redraw()&&rand.calls()==wc+2&&game.calls()>gc,"actual stats return consumes one portrait and low-market redraw");
    card.render();check(rand.calls()==wc+2,"presenting rebuilt card never draws again");
    check(!card.held_left_ticks(25)&&card.held_left_ticks(26),"Source held-left threshold is greater than25 ticks");
    check(card.take_request().source_return==1,"held-left returns selected Source slot+1 and closes parent");
    auto ai_selection=ClubPlayerDetailSelection{1,1,0,0,-1};
    auto ai_p=club_player_detail_from_game(g,club_player_detail_redraw(g,ai_selection,exe,game,rand),exe);
    check(!ai_p.redraw.context.board,"AI card owns no active manager board");
    ClubPlayerDetailScreen ai(data,ai_p,parent);ai.render();ai.press(100,215);
    check(ai.take_request().kind==ClubPlayerDetailRequestKind::close,"AI card has no phantom statistics button; fresh outside press closes");
    const int market_slot=2928;
    g.squad_slots[size_t(market_slot)]=0x8001;
    const ClubPlayerDetailSelection market{-1,1,11,-1,-1,ClubPlayerDetailOrigin::market_pool,market_slot};
    auto market_redraw=club_player_detail_redraw(g,market,exe,game,rand);
    auto market_projection=club_player_detail_from_game(g,market_redraw,exe);
    check(market_redraw.context.market_pool && market_redraw.context.source_slot==market_slot &&
          !market_redraw.context.board && market_redraw.market_value.has_value() &&
          market_projection.statistics_months==0 && market_projection.statistics_flags==0,
          "market pool projection has actual slot/value and no owner statistics");
    const auto market_g=game.calls(),market_w=rand.calls();
    ClubPlayerDetailScreen market_card(data,market_projection,parent);
    const auto market_frame=market_card.render();
    check(market_card.render().pixels==market_frame.pixels && game.calls()==market_g && rand.calls()==market_w,
          "market pool card reuses pure painter without presentation RNG");
    market_card.press(100,215);
    check(!market_card.in_statistics() && market_card.take_request().kind==ClubPlayerDetailRequestKind::close,
          "unowned pool card with no matching offer has no footer action or statistics");
    g.memory.set32(::bmh::layout::Weekly_PreMatchOffer_RuntimeField,0x80000000u|(1u<<10)|3u);
    auto offered=club_player_detail_from_game(g,club_player_detail_redraw(g,market,exe,game,rand),exe);
    check(offered.redraw.context.active_offer==1 && !offered.displayed_offer_amount &&
          !offered.redraw.context.board && offered.statistics_months==0,
          "format field wildcard offer is projected without salary or owner statistics");
    const auto offered_g=game.calls(),offered_w=rand.calls();
    ClubPlayerDetailScreen offered_card(data,offered,parent);const auto offered_frame=offered_card.render();
    check(!offered_card.release(100,215),"unpaired pool-offer release never dispatches");
    offered_card.press(100,215);
    check(offered_card.take_request().kind==ClubPlayerDetailRequestKind::none &&
          offered_card.render().pixels!=offered_frame.pixels,"actual pool-offer button presses before dispatch");
    offered_card.release(100,215);const auto offered_request=offered_card.take_request();
    check(offered_request.kind==ClubPlayerDetailRequestKind::offer_action && offered_request.source_return==255 &&
          offered_request.close_parent() && offered_request.selection.market_pool_slot==2928 &&
          offered_request.selection.rendered_row==11 && offered_request.selection.origin==ClubPlayerDetailOrigin::market_pool &&
          !offered_card.in_statistics() && game.calls()==offered_g && rand.calls()==offered_w &&
          offered_card.take_request().kind==ClubPlayerDetailRequestKind::none,
          "paired Source x80/211 unowned offer button emits one typed action with original Native selection and no RNG");
    pool_national_cards(data,exe,parent);
    ClubPlayerDetailScreen right(data,p,parent);right.press(0,0,true);
    check(right.take_request().kind==ClubPlayerDetailRequestKind::close&&!right.take_request().close_parent(),"right-down returns zero to rebuild parent ClubInfo");
    auto loan_p=p;loan_p.redraw.context.human_loan=true;loan_p.redraw.market_value.reset();
    loan_p.redraw.context.secondary_club=1;loan_p.clubs[1]=ClubPlayerDetailClub{1,0,"FCB","FCB"};
    ClubPlayerDetailScreen loan(data,loan_p,parent);loan.render();
    check(!loan.held_left_ticks(26),"human loan suppresses Source hold-to-parent-close");
    auto national_p=p;national_p.redraw.context.national_action_available=true;national_p.redraw.context.national_mode=1;
    ClubPlayerDetailScreen national(data,national_p,parent);national.press(190,215);national.release(190,215);
    const auto action=national.take_request();
    check(action.kind==ClubPlayerDetailRequestKind::national_action&&action.source_return==0xff,"real Source third button returns explicit national action and parent close");
    std::cout<<"Generic card Source portrait1444 pixels, owned AI/human/loan, press-release/hold/keys, statistics lifecycle and exact redraw boundary passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
