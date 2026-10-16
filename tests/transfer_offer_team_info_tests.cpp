#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include "app/controller.hpp"
#include "formats/fbm.hpp"
#include "game/club_info.hpp"
#include "game/club_player_detail.hpp"
#include "game/messages.hpp"
#include "game/month.hpp"
#include "game/save.hpp"
#include "game/weekly.hpp"
#include "ui/club_info.hpp"
#include "ui/club_player_detail.hpp"
#include "ui/team_statistics.hpp"

using namespace bmh;
namespace fs=std::filesystem;
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Snapshot {
    Bytes native; GameRandom game; WatcomRandom rand; uint64_t revision;
    Snapshot(app::CareerSession& s,uint64_t rev):native(save_stream(s.state())),game(s.game()),rand(s.rand()),revision(rev){}
    bool same(app::CareerSession& s,uint64_t rev)const{return native==save_stream(s.state())&&revision==rev&&
        game.seed()==s.game().seed()&&game.calls()==s.game().calls()&&rand.seed()==s.rand().seed()&&rand.calls()==s.rand().calls();}
};
std::vector<int> entries(std::string_view json){
    const std::string key="\"visible_market_entries\":[";const auto p=json.find(key);check(p!=json.npos,"visible Source page");
    auto n=p+key.size();const auto end=json.find(']',n);std::vector<int> out;
    while(n<end){while(n<end&&(json[n]==' '||json[n]==','))++n;if(n==end)break;int v=-1;
        const auto [next,ec]=std::from_chars(json.data()+n,json.data()+end,v);check(ec==std::errc{},"page integer");out.push_back(v);n=size_t(next-json.data());}
    return out;
}
struct Context {
    fs::path data,directory;ui::StartupFlow flow;std::unique_ptr<app::CareerSession> session;
    uint64_t revision=0;openbmh::audio::Playback audio;std::optional<uint64_t> audio_session;int entry=-1,player=-1;
    Context(const fs::path& d,const fs::path& genuine,bool low=false,bool own=false):data(d),
        directory(fs::temp_directory_path()/("openbmh-team-footer-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))),flow(d,directory){
        fs::create_directories(directory);auto input=load_savegame(genuine);check(input.checksum_ok,"genuine checksum");
        auto native=GameState::from_save(SaveState(input.stream));
        for(int e=0;e<40&&entry<0;++e){const auto at=::bmh::layout::EditorListUnsigned+uint32_t(e)*0x58;const int id=native.squad_slots.at(2928+e)&0xfff;
            if(id==0xfff||(low&&native.players[size_t(id)].position()==0)||native.memory.u16(at+8)!=0x0fff||!native.memory.u32(at+0x2c)||!native.memory.u32(at+0x30))continue;
            entry=e;player=id;}
        check(entry>=0&&native.manager_teams==std::vector<int>({1,54}),"genuine two-player free listing and buyer club");
        if(own){
            // Declared existing-own ordinary bid on the same genuine listing.
            const uint32_t at=::bmh::layout::EditorListUnsigned+uint32_t(entry)*0x58;
            native.memory.set16(at,1);native.memory.set32(at+0x0c,12400);native.memory.set32(at+0x1c,222000);
            native.memory.set8(at+0x34,36);
            const std::array<uint8_t,7> bonuses{7,8,9,101,150,199,200};
            for(uint32_t n=0;n<7;++n)native.memory.set8(at+0x38+n,bonuses[n]);
        }
        if(low){
            // Declared low-valuation control of an existing genuine non-GK:
            // zero technique/condition, zero experience/valuation skills, age35;
            // keep position/name/pool/seller/bid identity. Saved G seed selects
            // the actual Source opening percent92, without changing the draw.
            auto& attributes=native.players[size_t(player)];
            attributes.w35c&=~((127u<<7)|(127u<<14));
            attributes.w364=(attributes.w364&~((63u<<26)|(63u<<10)|(31u<<21)))|(19u<<21);
            attributes.w368&=~(63u<<7);
            uint32_t seed=0;for(;;++seed){GameRandom candidate;candidate.set_seed(int32_t(seed));if(candidate.range(92,108)==92)break;}
            native.memory.set32(::bmh::layout::CareerSession_Invalid_RuntimeField2,seed);
        }
        if(low||own){WatcomRandom encryption(1);write_savegame(directory/"AUTOSAVE.FBM",make_savegame(native,encryption));}
        else fs::copy_file(genuine,directory/"AUTOSAVE.FBM");
        audio.set_muted(true);flow.click(214,215);auto load=flow.send_key("ENTER");
        check(load.request==ui::FlowRequest::file_action&&serve(load).empty()&&session&&revision==1,"actual Native load through StartupFlow/controller");
        check(session->game().calls()==0&&session->rand().calls()==0,"Native load draws neither RNG");
        const auto& items=flow.desktop()->state().items;const auto menu=std::find_if(items.begin(),items.end(),[](const auto& i){return i.kind==17&&!i.disabled;});
        check(menu!=items.end(),"actual market menu17");check(serve(flow.click(menu->x+menu->width/2,menu->y+menu->height/2)).empty(),"physical market open");
        int visible=-1;for(int page=0;page<7&&visible<0;++page){const auto es=entries(flow.state_json());const auto at=std::find(es.begin(),es.end(),entry);
            if(at!=es.end())visible=int(at-es.begin());else flow.click(250,40,true);}
        check(visible>=0,"genuine free listing through Source navigation");const int y=32+visible*15;
        check(serve(flow.move_action(220,y)).empty(),"owned physical hover preview");flow.click(220,y);
        check(serve(flow.click(185,207)).empty()&&flow.screen_name()=="outgoing_transfer"&&session->transfer_offer_owner(),"physical Source KB31 opening");
    }
    ~Context(){std::error_code ignored;fs::remove_all(directory,ignored);}
    std::string serve(const ui::FlowResult& r,app::TransferOfferTeamInfoPresenter presenter={}){
        return app::serve_flow_request(flow,r,session,data,directory,1,revision,audio,audio_session,false,
            {},{},{},{},{},{},{},{},PendingOfferHooks{},app::PendingOfferEntryPolicy::legacy_unpresented,
            HumanBuyerSaleHooks{},app::HumanBuyerMarketEntryPolicy::normal_interactive,std::move(presenter));}
    ui::FlowResult footer(){auto request=flow.click(20,219);check(request.request==ui::FlowRequest::submenu_action&&
        std::holds_alternative<ui::OutgoingTransferRequest>(request.submenu_action)&&
        std::get<ui::OutgoingTransferRequest>(request.submenu_action).kind==ui::OutgoingTransferRequestKind::team_info,
        "physical leftmost KB31 footer emits typed TeamInfo");return request;}
};
// Independently compose the post-TeamInfo quote from format field primitive,
// retaining caller local14/years/loan. Never call the production resume helper.
std::pair<uint32_t,uint32_t> resume_quote(const GameState& state,const TransferOfferEditorState& owner,WatcomRandom& rand){
    uint32_t fee=0,salary=0;market_value(state,owner.request.source_slot,int(owner.quote_loan),owner.percent,fee,salary,owner.years,0,rand);
    return {((fee+(fee>>2))/10000u)*10000u,(salary/100u)*100u};
}
void check_state(Context& c,const GameState& expected,const GameRandom& game,const WatcomRandom& rand,uint64_t revision){
    check(save_stream(c.session->state())==save_stream(expected),"complete Native Source composition matches committed footer");
    check(c.session->game().seed()==game.seed()&&c.session->game().calls()==game.calls(),"complete G state/calls matches nested Source composition");
    check(c.session->rand().seed()==rand.seed()&&c.session->rand().calls()==rand.calls(),"complete W state/calls matches nested Source composition");
    check(c.revision==revision&&c.flow.desktop()->state().revision==revision&&!c.flow.pending_transfer_offer(),"one coherent outer revision and Flow acknowledgement");
}
void nested(const fs::path& data,const fs::path& genuine,bool prior_two=false){
    Context c(data,genuine);
    if(prior_two){auto live=c.session->live_club_modal_services(c.revision);live.set_runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField,2);}
    const Snapshot before(*c.session,c.revision);auto expected=c.session->state();auto eg=c.session->game();auto ew=c.session->rand();
    const auto old=*c.session->transfer_offer_owner();const auto exe=bmh::test::load_executable(data/"BMH.exe");const auto texts=load_textd(data/"TEXTD.BLK");
    int calls=0;
    auto present=[&](app::ClubModalServices& services,int team,const Image& backdrop){
        ++calls;check(team==1&&team!=old.request.expected_seller_word,"format field target is buyer, not seller badge");
        check(services.transactional&&&services.state()!=&c.session->state()&&before.same(*c.session,c.revision),"modal borrows staged state; live Native/G/W/revision stable");
        check(backdrop.width==320&&backdrop.height==240,"actual KB31 parent image supplied");
        auto oracle_entry=prepare_club_info_entry(expected,team);auto entry=services.enter_club(team);
        check(entry.displayed_slots==oracle_entry.displayed_slots&&save_stream(services.state())==save_stream(expected),"Source list flag normalization and injury cleanup staged exactly");
        auto projection=ui::club_info_from_game(services.state(),team,entry,exe,texts);ui::ClubInfoScreen club(data,projection);
        club.press(135,219);const auto category=club.release(135,219);check(category.kind==ui::ClubInfoRequestKind::change_category,"physical Source category control");
        const auto oc=set_club_info_category(expected,team,category.requested_category);
        check(services.change_category(team,category.requested_category)==oc,"Source category returned");
        club.press(75,219);const auto stats=club.release(75,219);check(stats.kind==ui::ClubInfoRequestKind::open_statistics,"physical Source team statistics control");
        const auto frozen=save_stream(services.state());
        for(auto mode:{ui::TeamStatisticsMode::home,ui::TeamStatisticsMode::away,ui::TeamStatisticsMode::combined}){
            ui::TeamStatisticsScreen screen(data,ui::team_statistics_from_game(services.state(),team,mode,exe,texts),club.render());
            check(screen.render().width==320,"actual team statistics projector/render");}
        check(frozen==save_stream(services.state()),"statistics projection has no Native writes");
        check(club.move(20,34),"physical buyer roster hover");const auto selected=club.pointer_down(20,34);
        check(selected.kind==ui::ClubInfoRequestKind::open_player_detail&&selected.runtime_team==team,"physical buyer player row enters original player submodal");
        const ClubPlayerDetailSelection identity{selected.runtime_team,selected.player_id,selected.rendered_row,selected.physical_row,selected.human_loan_index};
        const auto prior=services.runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField);
        check(prior==uint8_t(prior_two?2:0),"explicit runtime prior mode and Source!=2 branch");services.set_runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField,club_player_detail_input_mode(prior));
        auto oracle_card=club_player_detail_redraw(expected,identity,exe,eg,ew);auto redraw=services.redraw_player(identity);
        check(redraw.context.source_slot==oracle_card.context.source_slot,"actual buyer squad Source player identity");
        ui::ClubPlayerDetailScreen card(data,ui::club_player_detail_from_game(services.state(),redraw,exe),club.render());
        card.press(100,215);card.release(100,215);check(card.in_statistics(),"physical player statistics enters original screen");
        card.press(30,145);card.release(30,145);card.press(20,190);card.release(20,190);
        const auto request=card.take_request();check(request.kind==ui::ClubPlayerDetailRequestKind::redraw_card,"physical player statistics exit requests Source rebuild");
        const uint32_t preference=::bmh::layout::FlagsUnsignedSpelling2+uint32_t(redraw.context.active_manager)*3u+2u;
        services.state().memory.set8(preference,request.statistics_flags);expected.memory.set8(preference,request.statistics_flags);
        (void)club_player_detail_redraw(expected,identity,exe,eg,ew);auto second=services.redraw_player(identity);
        card.replace_redraw(ui::club_player_detail_from_game(services.state(),second,exe));
        services.set_runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField,prior);
        check(services.runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField)==prior,"legacy routine restores caller input mode");
        check(before.same(*c.session,c.revision),"all nested list/category/statistics/player effects remain staged");
        check(save_stream(services.state())==save_stream(expected),"nested Native effects match independently composed Source primitives");
        return true;
    };
    check(c.serve(c.footer(),present).empty()&&calls==1,"actual controller serves TeamInfo once");
    const auto quotes=resume_quote(expected,old,ew);const auto now=*c.session->transfer_offer_owner();
    check(now.percent==old.percent&&now.quoted_fee==quotes.first&&now.quoted_salary==quotes.second,"Source footer reuses initial percent and independent resumed quote");
    check_state(c,expected,eg,ew,before.revision+1);
    const auto list=::bmh::layout::EditorListUnsigned+uint32_t(old.request.list_entry)*0x58;
    for(size_t n=0;n<88;++n)check(now.native_entry[n]==expected.memory.u8(list+uint32_t(n)),"owner snapshot refreshed coherently");
    check(c.flow.screen_name()=="outgoing_transfer","same KB31 remains open after nested submodals");
    check(c.serve(c.flow.click(280,219)).empty()&&c.flow.screen_name()=="transfer_market","physical cancel works after refreshed owner");
}
void zero_revision_and_rescan(const fs::path& data,const fs::path& genuine){
    Context c(data,genuine);
    const auto only_list=[](app::ClubModalServices& svc,int team,const Image&){(void)svc.enter_club(team);return true;};
    c.serve(c.footer(),only_list);const Snapshot stable(*c.session,c.revision);
    check(c.serve(c.footer(),only_list).empty()&&stable.same(*c.session,c.revision),
        "unchanged high-value TeamInfo resume publishes zero revisions and consumes neither RNG");
    const auto old=*c.session->transfer_offer_owner();check(old.bid_slot==0&&old.initialized_empty_bid,"controlled rescan begins after the first empty initialization");
    auto expected=c.session->state();auto ew=c.session->rand();const auto eg=c.session->game();
    const uint32_t at=::bmh::layout::EditorListUnsigned+uint32_t(old.request.list_entry)*0x58;
    const std::array<uint8_t,7> premiums{7,8,9,101,150,199,200};
    const auto control=[&](GameState& state){
        // Explicit postmodal-row control exercises Source local1c/rescan only.
        // It does not claim that a category/statistics input generates bids.
        state.memory.set16(at,54);state.memory.set8(at+0x34,24);
        state.memory.set16(at+2,0xffff);state.memory.set32(at+0x10,12400);state.memory.set32(at+0x20,222000);
        state.memory.set8(at+0x35,12);for(uint32_t i=0;i<7;++i)state.memory.set8(at+0x3f+i,premiums[i]);
        state.memory.set16(at+6,1);state.memory.set8(at+0x37,12);
    };
    control(expected);const auto native=save_stream(expected);auto quoted=old;quoted.years=1;
    const auto amounts=resume_quote(expected,quoted,ew);
    const auto change=[&](app::ClubModalServices& svc,int,const Image&){control(svc.state());return true;};
    check(c.serve(c.footer(),change).empty(),"controller acknowledges Source rescan changing raw slot");
    check_state(c,expected,eg,ew,stable.revision+1);const auto now=*c.session->transfer_offer_owner();
    check(save_stream(c.session->state())==native&&now.bid_slot==1&&!now.existing&&now.fee==222000&&
        now.salary==12400&&now.term==12&&now.bonuses==premiums&&now.percent==old.percent&&
        now.quoted_fee==amounts.first&&now.quoted_salary==amounts.second,
        "first FFFF precedes later own; local1c preserves every new raw row fee/salary/term/bonus byte");
}
void initially_own_rescan(const fs::path& data,const fs::path& genuine){
    Context c(data,genuine,false,true);const Snapshot opened(*c.session,c.revision);
    const auto old=*c.session->transfer_offer_owner();check(old.existing&&!old.initialized_empty_bid&&old.term==36,
        "existing-own opening leaves Source local1c zero and preserves original row payload");
    // An untouched existing own row must not become an initialized empty row.
    auto expected=c.session->state();auto ew=c.session->rand();const auto eg=c.session->game();
    (void)prepare_club_info_entry(expected,1);const auto own_quote=resume_quote(expected,old,ew);
    const auto only_list=[](app::ClubModalServices& svc,int team,const Image&){(void)svc.enter_club(team);return true;};
    check(c.serve(c.footer(),only_list).empty(),"own-row footer returns");
    const bool changed=save_stream(expected)!=opened.native||ew.calls()!=opened.rand.calls()||ew.seed()!=opened.rand.seed();
    check_state(c,expected,eg,ew,opened.revision+uint64_t(changed));
    const auto unchanged=*c.session->transfer_offer_owner();
    check(!unchanged.initialized_empty_bid&&unchanged.native_entry==old.native_entry&&unchanged.quoted_fee==own_quote.first&&
        unchanged.quoted_salary==own_quote.second,"unchanged existing own row retains local1c zero and every Native byte");
    const Snapshot before(*c.session,c.revision);expected=c.session->state();ew=c.session->rand();
    const uint32_t at=::bmh::layout::EditorListUnsigned+uint32_t(old.request.list_entry)*0x58;
    const std::array<uint8_t,7> premiums{3,17,20,101,150,199,200};
    const auto control=[&](GameState& state){
        // Controlled nested row change only, not a claimed category effect.
        state.memory.set16(at,54);state.memory.set8(at+0x34,36);
        state.memory.set16(at+2,0xffff);state.memory.set32(at+0x10,12400);state.memory.set32(at+0x20,222000);
        state.memory.set8(at+0x35,12);for(uint32_t n=0;n<7;++n)state.memory.set8(at+0x3f+n,premiums[n]);
        state.memory.set16(at+6,1);state.memory.set8(at+0x37,12);
    };
    control(expected);
    // Independent caller composition: local1c is zero and new raw1 is FFFF.
    expected.memory.set32(at+0x10,0);expected.memory.set32(at+0x20,0);expected.memory.set8(at+0x35,24);
    auto quoted=old;quoted.years=2;const auto amounts=resume_quote(expected,quoted,ew);
    const auto modal=[&](app::ClubModalServices& svc,int,const Image&){control(svc.state());return true;};
    check(c.serve(c.footer(),modal).empty(),"own-to-empty rescan is acknowledged by real controller");
    check_state(c,expected,eg,ew,before.revision+1);const auto now=*c.session->transfer_offer_owner();
    check(now.initialized_empty_bid&&now.bid_slot==1&&!now.existing&&now.fee==0&&now.salary==0&&now.term==24&&
        now.bonuses==premiums&&now.percent==old.percent&&now.quoted_fee==amounts.first&&now.quoted_salary==amounts.second,
        "first empty selection after existing-own initializes fee/salary/immutable-action term once, preserving all seven stale bonuses");
    check(c.session->game().seed()==opened.game.seed()&&c.session->game().calls()==opened.game.calls(),
        "own-to-empty rescan never repeats initial G reset/draw");
}
void loan_and_full_initialization(const fs::path& data,const fs::path& genuine){
    auto native=GameState::from_save(SaveState(load_savegame(genuine).stream));
    int entry=-1,player=-1,seller=-1;
    for(int club=0;club<90&&entry<0;++club){
        if(native.manager_of(club)>=0)continue;const auto squad=squad_range(club);
        for(size_t row=11;row<squad.count&&entry<0;++row){
            const int id=native.squad_slots[squad.first+row]&0xfff;if(id==0xfff)continue;
            auto staged=native;WatcomRandom producer(37);MinuteReport report;
            if(!list_transfer_player(staged,int(squad.first+row),producer,report)||report.unsupported)continue;
            for(int n=0;n<40;++n)if((staged.squad_slots[2928+size_t(n)]&0xfff)==id&&staged.memory.u16(::bmh::layout::EditorListUnsigned+uint32_t(n)*0x58+8)==club){
                native=std::move(staged);entry=n;player=id;seller=club;break;}
        }
    }
    check(entry>=0,"genuine Source-generated AI listing supports immutable loan-action initializer control");
    const uint32_t at=::bmh::layout::EditorListUnsigned+uint32_t(entry)*0x58;
    const std::array<uint8_t,7> premiums{3,17,20,101,150,199,200};
    for(int opening=0;opening<2;++opening){
        auto state=native;
        for(uint32_t n=0;n<4;++n){state.memory.set16(at+n*2,opening?uint16_t(n+2):uint16_t(n?0xffff:1));
            state.memory.set32(at+0x0c+n*4,12400);state.memory.set32(at+0x1c+n*4,222000);state.memory.set8(at+0x34+n,36);
            for(uint32_t k=0;k<7;++k)state.memory.set8(at+0x38+n*7+k,premiums[k]);}
        const auto pre=save_stream(state);GameRandom game;WatcomRandom rand(1);
        const TransferOfferEditorRequest request{0,1,entry,2928+entry,player,uint16_t(seller),opening?-1:0,true};
        const auto owner=open_transfer_offer_editor_source(state,request,game,rand);
        check(owner.supported&&!owner.initialized_empty_bid&&save_stream(state)==pre,
            "existing-own and full-readonly openings leave local1c zero without Native row initialization");
        const auto g_before=game;auto expected=state;auto ew=rand;
        const int target=opening?0:1;
        if(!opening){expected.memory.set16(at,54);state.memory.set16(at,54);}
        expected.memory.set16(at+uint32_t(target)*2,0xffff);
        state.memory.set16(at+uint32_t(target)*2,0xffff);
        expected.memory.set32(at+0x0c+uint32_t(target)*4,0);expected.memory.set32(at+0x1c+uint32_t(target)*4,0);
        expected.memory.set8(at+0x34+uint32_t(target),152);
        auto quoted=owner;quoted.years=2;const auto amounts=resume_quote(expected,quoted,ew);
        const auto resumed=resume_transfer_offer_team_info_source(state,owner,rand);
        check(resumed.supported&&resumed.initialized_empty_bid&&resumed.bid_slot==target&&resumed.fee==0&&resumed.salary==0&&
            resumed.term==152&&resumed.bonuses==premiums&&resumed.percent==owner.percent&&resumed.quoted_fee==amounts.first&&resumed.quoted_salary==amounts.second&&
            save_stream(state)==save_stream(expected)&&rand.seed()==ew.seed()&&rand.calls()==ew.calls()&&
            game.seed()==g_before.seed()&&game.calls()==g_before.calls(),
            "first empty after own/full initializes immutable initial-loan term152, preserves stale premiums, and matches complete Native/W/G composition");
    }
}
void quote_and_rollback(const fs::path& data,const fs::path& genuine){
    Context c(data,genuine,true);const Snapshot before(*c.session,c.revision);const auto old=*c.session->transfer_offer_owner();
    const auto runtime=c.session->club_modal_runtime_checkpoint();auto expected=c.session->state();auto eg=c.session->game();auto ew=c.session->rand();
    const auto throwing=[&](app::ClubModalServices& svc,int team,const Image&){
        (void)svc.enter_club(team);(void)svc.change_category(team,2);svc.set_runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField,2);
        check(before.same(*c.session,c.revision),"throwing presenter never changes live state");throw std::runtime_error("controlled presenter failure");return true;};
    check(c.serve(c.footer(),throwing).empty(),"controller handles throwing presenter");
    check(before.same(*c.session,c.revision)&&c.session->club_modal_runtime_checkpoint()==runtime&&
        c.session->transfer_offer_owner()->native_entry==old.native_entry&&!c.flow.pending_transfer_offer(),"throw rolls back Native/G/W/revision/runtime/owner and releases Flow input");
    const auto stopped=[&](app::ClubModalServices& svc,int team,const Image&){(void)svc.enter_club(team);return false;};
    c.serve(c.footer(),stopped);check(before.same(*c.session,c.revision),"stopped presenter also rolls back");
    c.serve(c.footer());check(before.same(*c.session,c.revision),"missing presenter rejects before live mutation");
    const auto unsupported=[&](app::ClubModalServices& svc,int,const Image&){svc.state().memory.set8(::bmh::layout::GameMode,0);return true;};
    c.serve(c.footer(),unsupported);check(before.same(*c.session,c.revision),"unsupported postmodal Source owner discards all staging");
    const auto ok=[&](app::ClubModalServices& svc,int team,const Image&){(void)svc.enter_club(team);return true;};
    (void)prepare_club_info_entry(expected,old.request.runtime_club);const auto w_before=ew.calls();const auto quotes=resume_quote(expected,old,ew);
    check(ew.calls()==w_before+1,"low-value Source quote control proves exactly one resumed W fallback");
    check(c.serve(c.footer(),ok).empty(),"healthy footer retry succeeds after throw/stop/missing/unsupported");
    check_state(c,expected,eg,ew,before.revision+1);
    check(c.session->transfer_offer_owner()->percent==old.percent&&c.session->transfer_offer_owner()->quoted_fee==quotes.first&&
        c.session->transfer_offer_owner()->quoted_salary==quotes.second,"resume quote matches Source rounding and reuses initial percent");
    check(c.session->game().calls()==before.game.calls()&&c.session->game().seed()==before.game.seed(),"TeamInfo-only return never resets/draws initial G");
    check(c.session->transfer_offer_owner()->native_entry==old.native_entry,"return does not initialize Native bid again");
}
}
int main(int argc,char** argv){try{check(argc==3,"usage: transfer_offer_team_info_tests DATA GENUINE_2PNWK001");nested(argv[1],argv[2]);nested(argv[1],argv[2],true);zero_revision_and_rescan(argv[1],argv[2]);initially_own_rescan(argv[1],argv[2]);loan_and_full_initialization(argv[1],argv[2]);quote_and_rollback(argv[1],argv[2]);
    std::cout<<"TeamInfo physical controller / staged nested Source / quote / rollback PASS\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
