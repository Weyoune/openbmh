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
    uint64_t revision=0;openbmh::audio::Playback audio;std::optional<uint64_t> audio_session;int entry=-1,player=-1,expected_slot=-1,seller=-1;
    Context(const fs::path& d,const fs::path& genuine,bool low=false,bool own=false,int scenario=0):data(d),
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
        expected_slot=2928+entry;
        if(scenario) {
            // Declared controlled seller identity on a genuine free listing.
            // Scenario1: unmanaged16;2:managed24;3:managed16. The existing
            // seller board is retained, never a synthesized buyer board.
            seller=scenario==2?54:90;
            if(scenario==3) {
                native.manager_teams[1]=90;
                native.manager_records[0xb0c+0x290]=90;native.manager_records[0xb0c+0x291]=0;
            }
            native.memory.set16(::bmh::layout::EditorListUnsigned+uint32_t(entry)*0x58+8,uint16_t(seller));
            const auto range=squad_range(seller);const int row=scenario==2?23:15;
            for(size_t n=0;n<range.count;++n)if((native.squad_slots[range.first+n]&0xfff)==player)native.squad_slots[range.first+n]=0xfff;
            native.squad_slots[range.first+size_t(row)]=uint16_t(0x8000|player);
            if(scenario>1)expected_slot=int(range.first)+row;
        }
        if(low||own||scenario){WatcomRandom encryption(1);write_savegame(directory/"AUTOSAVE.FBM",make_savegame(native,encryption));}
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
    std::string serve(const ui::FlowResult& r,app::TransferOfferPlayerInfoPresenter presenter={}){
        return app::serve_flow_request(flow,r,session,data,directory,1,revision,audio,audio_session,false,
            {},{},{},{},{},{},{},{},PendingOfferHooks{},app::PendingOfferEntryPolicy::legacy_unpresented,
            HumanBuyerSaleHooks{},app::HumanBuyerMarketEntryPolicy::normal_interactive,{},std::move(presenter));}
    ui::FlowResult footer(){auto request=flow.click(95,219);check(request.request==ui::FlowRequest::submenu_action&&
        std::holds_alternative<ui::OutgoingTransferRequest>(request.submenu_action)&&
        std::get<ui::OutgoingTransferRequest>(request.submenu_action).kind==ui::OutgoingTransferRequestKind::player_info,
        "physical second KB31 footer emits typed PlayerInfo");return request;}
};

// Literal unsigned Source G/W arithmetic, independent of player redraw/helper.
struct Oracle {
    uint32_t w,g;uint64_t wc,gc;
    int W(int lo,int hi){++wc;w=w*0x41c64e6du+0x3039u;return int(((w>>16)&32767)*uint32_t(hi-lo+1)/32768)+lo;}
    int G(int lo,int hi){++gc;g=(g*16u+167u)%4000000u;return int(g*uint32_t(hi-lo+1)/4000000u)+lo;}
};
void portrait_oracle(const GameState& state,int player,const LeImage& exe,
                     Oracle& o,const ClubPlayerDetailRedraw& actual) {
    o.g=0x14344;
    if(uint32_t(player)<exe.u32(::bmh::layout::ClubPlayerDetail_ClubPlayerDetailRedraw_RuntimeField)){
        const auto name=state.player_name(player);
        for(size_t n=0;n<name.size();++n)o.g+=uint32_t(int32_t(int8_t(name[n])))*uint32_t(n)*10u;
    }
    o.G(0,100);check(actual.portrait_base==o.G(0,3),"independent Source portrait base");
    for(size_t n=0;n<5;++n)check(actual.portrait_features[n]==o.G(0,exe.u8(::bmh::layout::ClubPlayerDetail_ClubPlayerDetailRedraw_RuntimeField2+uint32_t(n))-1),"independent Source portrait feature order");
    const int count=o.W(100,300);for(int n=0;n<count;++n)o.G(0,1);
    check(actual.portrait_burn_count==count,"independent W-controlled portrait G burn");
}
void card_and_zero_flags(const fs::path& data,const fs::path& genuine,int scenario,bool low=false){
    Context c(data,genuine,low,false,scenario);const auto exe=bmh::test::load_executable(data/"BMH.exe");
    auto runtime=c.session->live_club_modal_services(c.revision);runtime.set_runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField,2);
    const Snapshot before(*c.session,c.revision);const auto old=*c.session->transfer_offer_owner();
    if(scenario>=2){
        auto duplicate=c.session->state();const auto range=squad_range(c.seller);
        duplicate.squad_slots[range.first+2]=uint16_t(0x4000|c.player);
        auto opening=old;opening.player_info_selection.reset();
        const auto first=transfer_offer_player_info_selection_source(duplicate,opening);
        check(first.physical_row==2,"Source managed seller chooses FIRST actual masked-ID match at opening");
        check(transfer_offer_player_info_selection_source(duplicate,old).physical_row==(scenario==2?23:15),"Source retained local40 does not reselect a later matching copy");
        auto missing=c.session->state();missing.squad_slots[size_t(c.expected_slot)]=0xfff;
        bool rejected=false;try{(void)transfer_offer_player_info_selection_source(missing,old);}catch(const std::exception&){rejected=true;}
        check(rejected,"managed seller without actual squad ID is explicit Sourceerror10 boundary");
    }
    Oracle oracle{before.rand.seed(),uint32_t(before.game.seed()),before.rand.calls(),before.game.calls()};
    int calls=0;
    auto present=[&](app::ClubModalServices& svc,const ClubPlayerDetailSelection& selection,const Image& parent){
        ++calls;check(svc.transactional&&&svc.state()!=&c.session->state()&&before.same(*c.session,c.revision),"direct modal borrows staged state and leaves live session intact");
        check(svc.runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField)==1&&svc.runtime_byte(::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableField)==0&&svc.runtime_byte(::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableField2)==0,"actual direct Source persistent mode1 and zero return flags");
        check(selection.player_id==c.player,"direct selected actual player ID");
        if(scenario<2)check(selection.origin==ClubPlayerDetailOrigin::market_pool&&selection.runtime_team==-1&&selection.market_pool_slot==2928+c.entry,"free and AI-unmanaged seller use actual Native pool identity");
        else check(selection.origin==ClubPlayerDetailOrigin::squad&&selection.runtime_team==c.seller&&selection.physical_row==(scenario==2?23:15),"managed seller uses actual24/16 squad row");
        auto redraw=svc.redraw_player(selection);
        check(redraw.context.source_slot==c.expected_slot&&redraw.context.source_manager==(scenario<2?-1:1)&&bool(redraw.context.board)==(scenario>=2),"resolved Source slot and board ownership are seller-derived");
        portrait_oracle(svc.state(),c.player,exe,oracle,redraw);
        // Independent valuation primitive owns only the value draw after the
        // literal portrait oracle; never replay the production redraw helper.
        auto value_rand=before.rand;(void)value_rand.range(100,300);
        uint32_t fee=0,salary=0;market_value(svc.state(),c.expected_slot,0,100,fee,salary,0,0,value_rand);
        check(redraw.market_value==((fee+(fee>>2))/10000u)*10000u,"Source player-card value rounding");
        while(oracle.wc<value_rand.calls())(void)oracle.W(50000,80000);
        check(oracle.w==value_rand.seed(),"value random draw matches manual Source W state");
        ui::ClubPlayerDetailScreen card(data,ui::club_player_detail_from_game(svc.state(),redraw,exe),parent);
        const auto native=save_stream(svc.state());const auto pixels=card.render().pixels;
        check(card.render().pixels==pixels&&native==save_stream(svc.state()),"actual repeated rendering is Native-pure");
        card.press(20,215);card.release(20,215);const auto exit=card.take_request();
        check(exit.kind==ui::ClubPlayerDetailRequestKind::close&&exit.source_return==0,"physical normal card close returns zero to direct caller");
        return true; // Completion independent of close-parent value.
    };
    check(c.serve(c.footer(),present).empty()&&calls==1,"physical PlayerInfo controller called once");
    check(save_stream(c.session->state())==before.native,"ordinary direct card leaves Native bytes unchanged");
    check(c.session->game().calls()==oracle.gc&&uint32_t(c.session->game().seed())==oracle.g&&c.session->rand().calls()==oracle.wc&&c.session->rand().seed()==oracle.w,"complete direct card G/W state agrees with manual oracle; no extra editor opening G");
    const auto now=*c.session->transfer_offer_owner();
    check(now.native_entry==old.native_entry&&now.percent==old.percent&&now.bid_slot==old.bid_slot&&now.quoted_fee==old.quoted_fee&&now.quoted_salary==old.quoted_salary&&now.initialized_empty_bid==old.initialized_empty_bid,"zero flags skip rescan/profiles/quote and retain local1c");
    check(c.session->live_club_modal_services(c.revision).runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField)==1&&c.revision==before.revision+1&&!c.flow.pending_transfer_offer(),"persistent direct mode1 commits with exactly one outer Flow revision");
    check(c.serve(c.flow.click(280,219)).empty()&&c.flow.screen_name()=="transfer_market","subsequent typed cancel uses refreshed editor owner");
}
void flags_and_edits(const fs::path& data,const fs::path& genuine,uint32_t flag){
    Context c(data,genuine,true,true);auto svc=c.session->live_club_modal_services(c.revision);svc.set_runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField,1);
    const Snapshot before(*c.session,c.revision);const auto old=*c.session->transfer_offer_owner();
    const auto no_draw=[](app::ClubModalServices&,const ClubPlayerDetailSelection&,const Image&){return true;};
    check(c.serve(c.footer(),no_draw).empty()&&before.same(*c.session,c.revision),"zero-flags no-effect return publishes zero revisions and retains quote");
    const uint32_t at=::bmh::layout::EditorListUnsigned+uint32_t(c.entry)*0x58;
    const std::array<uint8_t,7> bonuses{3,17,20,101,150,199,200};
    auto expected=c.session->state();auto ew=c.session->rand();const auto eg=c.session->game();
    const auto control=[&](GameState& state){state.memory.set16(at,54);state.memory.set16(at+2,0xffff);state.memory.set32(at+0x10,12400);state.memory.set32(at+0x20,222000);state.memory.set8(at+0x35,12);for(uint32_t n=0;n<7;++n)state.memory.set8(at+0x3f+n,bonuses[n]);};
    control(expected);expected.memory.set32(at+0x10,0);expected.memory.set32(at+0x20,0);expected.memory.set8(at+0x35,24);
    uint32_t fee=0,salary=0;market_value(expected,old.request.source_slot,int(old.quote_loan),old.percent,fee,salary,2,0,ew);
    const auto resume=[&](app::ClubModalServices& s,const ClubPlayerDetailSelection&,const Image&){control(s.state());s.set_runtime_byte(flag,1);return true;};
    check(c.serve(c.footer(),resume).empty(),"nonzero Source flag enters real rescan");
    const auto now=*c.session->transfer_offer_owner();
    check(now.bid_slot==1&&now.initialized_empty_bid&&now.fee==0&&now.salary==0&&now.term==24&&now.bonuses==bonuses&&now.percent==old.percent&&now.quoted_fee==((fee+(fee>>2))/10000u)*10000u&&now.quoted_salary==(salary/100u)*100u,"flagged own-to-empty rescan initializes local1c once, preserving all seven raw bonuses and initial percent");
    check(save_stream(c.session->state())==save_stream(expected)&&c.session->rand().seed()==ew.seed()&&c.session->rand().calls()==ew.calls()&&c.session->game().seed()==eg.seed()&&c.session->game().calls()==eg.calls()&&c.revision==before.revision+1&&!c.flow.pending_transfer_offer(),"flagged direct return commits complete Native/W/G and one coherent Flow revision");
    // Drive the actual typed fee calculator after the direct rescan.
    c.serve(c.flow.click(30,123));check(c.flow.screen_name()=="outgoing_transfer","Source fee calculator remains in KB31");
    c.flow.text("10000");check(c.serve(c.flow.send_key("ENTER")).empty(),"subsequent typed field event accepted after direct rescan");
    check(c.session->transfer_offer_owner()->bid_slot==1&&c.session->transfer_offer_owner()->fee==10000,"subsequent typed edit commits fee on rescanned raw bid identity");
}
void rollback_and_bounds(const fs::path& data,const fs::path& genuine){
    Context c(data,genuine);const Snapshot before(*c.session,c.revision);const auto owner=*c.session->transfer_offer_owner();
    const auto runtime=c.session->club_modal_runtime_checkpoint();
    const auto unchanged=[&]{check(before.same(*c.session,c.revision)&&runtime==c.session->club_modal_runtime_checkpoint()&&owner.native_entry==c.session->transfer_offer_owner()->native_entry&&!c.flow.pending_transfer_offer(),"full Native/G/W/runtime/editor/revision rollback and Flow input released");};
    c.serve(c.footer());unchanged();
    bool throw_reached=false,stop_reached=false,row_reached=false;
    c.serve(c.footer(),[&](app::ClubModalServices& s,const ClubPlayerDetailSelection& id,const Image&){s.set_runtime_byte(::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableField,1);(void)s.redraw_player(id);throw_reached=true;throw std::runtime_error("controlled direct throw");return true;});unchanged();check(throw_reached,"throw occurs after actual staged redraw");
    c.serve(c.footer(),[&](app::ClubModalServices& s,const ClubPlayerDetailSelection& id,const Image&){(void)s.redraw_player(id);stop_reached=true;return false;});unchanged();check(stop_reached,"stop occurs after actual staged redraw");
    c.serve(c.footer(),[&](app::ClubModalServices& s,const ClubPlayerDetailSelection&,const Image&){s.state().memory.set32(::bmh::layout::EditorListUnsigned+uint32_t(c.entry)*0x58+0x1c,1234);row_reached=true;return true;});unchanged();check(row_reached,"changed zero-flag row reaches explicit unsupported resume");
    // A matching wildcard offer remains visible; unowned3140f dispatch is
    // explicitly unsupported and discards staging through the real services.
    bool offer_button=false,unowned_unsupported=false;
    c.serve(c.footer(),[&](app::ClubModalServices& s,const ClubPlayerDetailSelection& id,const Image& parent){
        for(uint32_t n=0;n<4;++n)s.state().memory.set32(::bmh::layout::OfferSlots+n*4,0xffffffffu);
        s.state().memory.set32(::bmh::layout::OfferSlots,(uint32_t(c.player)<<10)|1u);
        const auto exe=bmh::test::load_executable(data/"BMH.exe");auto redraw=s.redraw_player(id);
        check(redraw.context.active_offer==0&&!redraw.context.board,"format field wildcard keeps unowned action without a fake board");
        ui::ClubPlayerDetailScreen card(data,ui::club_player_detail_from_game(s.state(),redraw,exe),parent);card.press(100,215);card.release(100,215);
        const auto action=card.take_request();check(action.kind==ui::ClubPlayerDetailRequestKind::offer_action&&action.source_return==255,"physical unowned Original action is available");
        offer_button=true;
        try{(void)s.pending_offers(action.selection,{});}catch(const std::exception& e){
            unowned_unsupported=std::string(e.what()).find("legacy routine: clicked card has no ordinary pending action/board")!=std::string::npos;
            throw;
        }
        return true;
    });unchanged();check(offer_button&&unowned_unsupported,"actual visible wildcard action reaches the explicit unowned3140f boundary");
    // Pure Source resolver validates stale/bounds before any RNG or modal.
    for(int bad=0;bad<4;++bad){auto state=c.session->state();auto invalid=owner;
        if(bad==0)invalid.request.source_slot=2927;
        if(bad==1)invalid.request.list_entry=40;
        if(bad==2)state.squad_slots[size_t(owner.request.source_slot)]=0xfff;
        if(bad==3)state.memory.set16(::bmh::layout::EditorListUnsigned+uint32_t(c.entry)*0x58+8,54);
        bool rejected=false;try{(void)transfer_offer_player_info_selection_source(state,invalid);}catch(const std::exception&){rejected=true;}check(rejected,"invalid/stale direct identity rejected before presentation");
    }
    int presentations=0;
    const auto healthy=[&](app::ClubModalServices&,const ClubPlayerDetailSelection&,const Image&){++presentations;return true;};
    const auto pending=c.footer();auto stale=pending;
    --std::get<ui::OutgoingTransferRequest>(stale.submenu_action).identity.revision;
    c.serve(stale,healthy);
    check(before.same(*c.session,c.revision)&&runtime==c.session->club_modal_runtime_checkpoint()&&presentations==0&&c.flow.pending_transfer_offer(),"stale typed controller event cannot invoke modal or consume RNG");
    auto invalid=std::get<ui::OutgoingTransferRequest>(pending.submenu_action);++invalid.identity.player;
    std::string error;const auto rejected=c.session->dispatch_transfer_offer(invalid,{},c.revision,error,{},healthy);
    check(!rejected&&!error.empty()&&presentations==0&&before.same(*c.session,c.revision),"session independently rejects mismatched direct player identity before presentation");
    check(c.serve(pending,healthy).empty()&&c.session->live_club_modal_services(c.revision).runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField)==1&&c.revision==before.revision+1,"healthy retry commits runtime-only Source effect once");
}
}
int main(int argc,char** argv){try{check(argc==3,"usage: transfer_offer_player_info_tests DATA GENUINE_2PNWK001");
    for(int scenario=0;scenario<4;++scenario)card_and_zero_flags(argv[1],argv[2],scenario);
    card_and_zero_flags(argv[1],argv[2],0,true);flags_and_edits(argv[1],argv[2],::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableField2);flags_and_edits(argv[1],argv[2],::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableField);rollback_and_bounds(argv[1],argv[2]);
    std::cout<<"Direct PlayerInfo physical controller / Native pool+managed24/16 / manual G/W / flags / rollback PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
