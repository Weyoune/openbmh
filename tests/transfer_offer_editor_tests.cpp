// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
// Reuse the published independent SettleHumanBuyerSale/42707 Native composition and Source
// fixture producers, not any production evaluator/settlement as an oracle.
// Its baseline main is not invoked by this bounded editor test target.
#define main human_market_oracle_baseline_main
#include "human_buyer_market_tests.cpp"
#undef main

#include <bit>
#include <optional>

namespace {
struct EditorW {
    uint32_t seed=37;uint64_t calls=0;
    uint32_t range(uint32_t lo,uint32_t hi){seed=seed*0x41c64e6du+0x3039u;++calls;return lo+(((seed>>16)&32767u)*(hi-lo+1u)>>15);}
};
struct EditorG {
    int32_t seed=73;uint64_t calls=0;
    int range(){seed=std::bit_cast<int32_t>(uint32_t(seed)*16u+167u)%4000000;++calls;return std::bit_cast<int32_t>(uint32_t(seed)*17u)/4000000+92;}
};
std::pair<uint32_t,uint32_t> editor_source_quote(const Player& p,bool loan,int years,uint32_t percent,EditorW& w){
    constexpr int development[]={6,6,6,5,5,5,4,4,3,1,10,15,20,30,40,45,55,75,85,95};
    const int age=std::min(20,int(p.w364>>21&31)+1);const uint8_t growth=age>10?uint8_t(-age*10-56):uint8_t(100);
    uint16_t t=uint16_t(p.w35c>>7&127),c=uint16_t(p.w35c>>14&127);
    for(int n=1;n<std::min(years,4);++n){t=uint16_t(t+uint32_t(growth)*uint32_t(development[int16_t(t)/10])/100u);c=uint16_t(c+uint32_t(growth)*uint32_t(development[int16_t(c)/10])/100u);}
    int points=std::clamp((int16_t(t)+int16_t(c))*5/2+150-std::abs(11-age)*2,12,620);
    if(points>500)points+=points<600?development[10+(points-500)/10]:100;
    const int curve=(points/4+1)*points*(points/8+100)/10;
    const int pos=int(p.w364&3),skill=(int(p.w364>>10&63)*3+int(p.w368>>7&63))/2;
    const int factor=int(p.w364>>26&63)/4+(pos==0?110:100+(pos*skill/3)/2);
    uint32_t fee=uint32_t(percent*uint32_t(factor*curve/100)/100u);if(fee<50000)fee=w.range(50000,80000);
    const uint32_t salary=(fee/40u)/100u*100u;if(loan)fee>>=2;
    return {((fee+(fee>>2))/10000u)*10000u,(salary/100u)*100u};
}
TransferOfferEditorRequest editor_request(const GameState& g,const Control& c){
    return {c.buyer_manager,c.buyer,c.request.list_entry,int(c.request.source_pool_slot),c.player,g.memory.u16(c.request.list_address+8),c.request.bid_slot,false};
}
struct EditorOracle {
    GameState state;EditorW w;EditorG game;uint8_t percent=0;bool loan=false;int bid=0;uint32_t list=0;int player=-1;
    void quote(const TransferOfferEditorState& actual,const std::string& name){
        const int years=bid<0?1:(int(state.memory.u8(list+0x34+uint32_t(bid))&127)-1)/12+1;
        const auto expected=editor_source_quote(state.players.at(size_t(player)),loan,years,percent,w);
        check(actual.supported&&actual.quoted_fee==expected.first&&actual.quoted_salary==expected.second,name+": independent42307/422ac quote");
    }
    void opening(const TransferOfferEditorRequest& r){
        list=list_start+uint32_t(r.list_entry)*row_size;bid=r.expected_bid_slot;player=r.expected_player;loan=r.loan;
        if(state.memory.has(::bmh::layout::DeskActions_EditorFail_RuntimeField2))state.memory.set8(::bmh::layout::DeskActions_EditorFail_RuntimeField2,0);
        game.seed=std::bit_cast<int32_t>(state.memory.u32(::bmh::layout::CareerSession_Invalid_RuntimeField2));
        const bool mirror=state.memory.has(::bmh::layout::CareerSession_Save_RuntimeField)&&state.memory.has(::bmh::layout::DeskActions_EditorFail_RuntimeField);
        if(mirror)state.memory.set32(::bmh::layout::CareerSession_Save_RuntimeField,uint32_t(game.seed));
        percent=uint8_t(game.range());
        if(mirror)state.memory.set32(::bmh::layout::CareerSession_Save_RuntimeField,uint32_t(game.seed));
        if(bid>=0&&state.memory.u16(list+uint32_t(bid)*2)==0xffff){state.memory.set32(list+0x0c+uint32_t(bid)*4,0);state.memory.set32(list+0x1c+uint32_t(bid)*4,0);state.memory.set8(list+0x34+uint32_t(bid),uint8_t(24|(r.loan?128:0)));}
    }
    void event(const TransferOfferEditorEvent& e,bool initial_loan=false){
        const auto n=unsigned(e.field);if(n==10){loan=!loan;return;}
        if(!e.accepted)return;
        if(n==0)state.memory.set32(list+0x1c+uint32_t(bid)*4,e.value);
        else if(n==1)state.memory.set32(list+0x0c+uint32_t(bid)*4,e.value);
        else if(n==2)state.memory.set8(list+0x34+uint32_t(bid),uint8_t(e.value*12)|(initial_loan?128:0));
        else state.memory.set8(list+0x38+uint32_t(bid)*7+n-3,uint8_t(n<6?e.value:e.value/5));
    }
};
void editor_exact(const GameState& actual,const WatcomRandom& w,const GameRandom& game,const EditorOracle& expected,const std::string& name){
    check(save_stream(actual)==save_stream(expected.state),name+": complete Native Source snapshot");
    check(w.seed()==expected.w.seed&&w.calls()==expected.w.calls&&game.seed()==expected.game.seed&&game.calls()==expected.game.calls,name+": complete Source W/G seed and counts");
}
void editor_refs(GameState& g,const Control& c){
    g.memory.set8(::bmh::layout::GameMode,3);g.memory.set32(::bmh::layout::CareerSession_Invalid_RuntimeField2,0);
    g.memory.set32(c.request.list_address+0x2c,1234567);g.memory.set32(c.request.list_address+0x30,10000);
}
void editor_event_case(GameState& g,TransferOfferEditorState& state,EditorOracle& oracle,GameRandom& game,WatcomRandom& w,TransferOfferEditorEvent event,const std::string& name){
    oracle.event(event,state.request.loan);state=edit_transfer_offer_editor_source(g,state,event,game,w);
    if(event.accepted||event.field==TransferOfferEditorField::fee||event.field==TransferOfferEditorField::salary)oracle.quote(state,name);
    editor_exact(g,w,game,oracle,name);
}
void editor_unsupported_event(GameState g,TransferOfferEditorState state,const TransferOfferEditorEvent& event,const std::string& name){
    const auto native=save_stream(g);WatcomRandom w(37);GameRandom game(73);
    const auto after=edit_transfer_offer_editor_source(g,state,event,game,w);
    check(!after.supported&&!after.reason.empty()&&w.calls()==0&&game.calls()==0&&save_stream(g)==native,name+": reject beforeNative/W/G");
}
void editor_e2e(GameState input,const Control& c,uint8_t season_bonus){
    editor_refs(input,c);const uint32_t list=c.request.list_address;
    auto product=input;EditorOracle expected{input};GameRandom game(73);WatcomRandom w(37);
    const auto req=editor_request(product,c);expected.opening(req);
    auto state=open_transfer_offer_editor_source(product,req,game,w);expected.quote(state,"E2E opening");editor_exact(product,w,game,expected,"E2E opening");
    const std::array<TransferOfferEditorEvent,10> events={{{TransferOfferEditorField::fee,3075000},{TransferOfferEditorField::salary,24000},{TransferOfferEditorField::years,2},
        {TransferOfferEditorField::bonus0,3},{TransferOfferEditorField::bonus1,4},{TransferOfferEditorField::bonus2,5},
        {TransferOfferEditorField::bonus3,505},{TransferOfferEditorField::bonus4,1000},{TransferOfferEditorField::bonus5,505},{TransferOfferEditorField::bonus6,uint32_t(season_bonus)*5u}}};
    for(const auto& event:events)editor_event_case(product,state,expected,game,w,event,"E2E editor field");
    auto source=expected.state;WatcomRandom source_w(expected.w.seed); // count qualification separately, then account for prior editor draws.
    GameRandom source_g(expected.game.seed);auto request=c.request;request.bid_slot=state.bid_slot;
    source.memory.set16(list+uint32_t(state.bid_slot)*2,uint16_t(c.buyer));
    const auto pcts=source_direct_rating(source,request,source_w);
    const uint32_t salary_gate=uint32_t(source_w.range(120,190));check(pcts.salary>=salary_gate,"independent raw101/200 bonus salary gate");
    check(uint32_t(source_w.range(92,150))<pcts.fee,"independent ordinary fee gate");
    const auto settled=source_settle(source,request,HumanBuyerSaleAllocatorGate::zero,source_w,source_g,true,false);
    std::vector<Boundary> seen;auto hooks=observing_hooks(HumanBuyerSaleAllocatorGate::zero,seen,w,game,false);hooks.direct_mode=HumanBuyerSaleDirectMode::ordinary_ai_purchase;
    MinuteReport report;const auto result=submit_transfer_offer_editor_source(product,state,w,hooks,report);
    check(result.editor.supported&&result.editor.closed&&!report.unsupported&&result.action.status==TransferActionStatus::accepted&&settled.status==HumanBuyerSaleStatus::moved,"ordinary source-qualified submit/closed/moved");
    check(save_stream(product)==save_stream(source)&&w.seed()==source_w.seed()&&w.calls()==expected.w.calls+source_w.calls()&&game.seed()==expected.game.seed&&game.calls()==expected.game.calls,"independent opening+all7/editor/qualification/accounts/move fullNative/W/G");
    check(seen.size()==settled.observations.size(),"independent af304/account notice count");
    for(size_t n=0;n<seen.size();++n){const auto& a=seen[n];const auto& b=settled.observations[n];check(a.native==b.native&&a.manager==b.manager&&a.address==b.address&&a.flags==b.flags&&a.text==b.text&&a.w_seed==b.w_seed&&a.w_calls==expected.w.calls+b.w_calls&&a.g_seed==expected.game.seed&&a.g_calls==expected.game.calls,"SourceNative/W/G notice checkpoint");}
}
}
int main(int argc,char** argv) try{
    check(argc==3,"usage: transfer_offer_editor_tests DATA_DIR genuine_2PNWK001.FBM");
    if(!*argv[1]){std::puts("offer editor tests skipped: Original data not configured");return 77;}
    const auto fbm=load_savegame(argv[2]);check(fbm.checksum_ok,"genuine Native checksum");auto texts=load_textd(std::filesystem::path(argv[1])/"TEXTD.BLK");
    auto original=GameState::from_save(SaveState(fbm.stream));original.texts=&texts;
    check(original.memory.has(::bmh::layout::DeskActions_OpenTransferOfferEditorSource_RuntimeField)&&!original.memory.has(::bmh::layout::CareerSession_Save_RuntimeField),"genuine Native owns saved bbcd8 but no transient af974 block");
    auto input=original;const auto c=make_control(input,false,-1,false,0,250000,70000,false);editor_refs(input,c);const uint32_t list=c.request.list_address;
    const std::array<uint8_t,7> initial={3,4,5,101,102,103,200};for(uint32_t n=0;n<7;++n)input.memory.set8(list+0x38+n,initial[n]);
    {auto product=input;EditorOracle source{input};GameRandom game(73);WatcomRandom w(37);const auto request=editor_request(input,c);source.opening(request);
        auto state=open_transfer_offer_editor_source(product,request,game,w);source.quote(state,"new opening");editor_exact(product,w,game,source,"new opening");
        check(state.term==24&&state.years==2&&state.fee==0&&state.salary==0&&state.bonuses==initial&&product.memory.u16(list)==0xffff,"Source initializes only0/0/24 and preserves7Nativebonuses/FFFFbuyer");
        const std::array<std::optional<uint32_t>,8> fees={1080000,1230000,1380000,1530000,1680000,1830000,0,3075000};
        const std::array<std::optional<uint32_t>,8> salaries={9000,10000,11000,12000,13000,14000,0,200000};
        const std::array<std::optional<uint32_t>,8> years={1,2,3,4,std::nullopt,std::nullopt,1,4};
        check(state.asking_fee==1537500&&state.asking_salary==12000&&state.money_profiles[0].presets==fees&&state.money_profiles[1].presets==salaries,"hardSource4a13a auto money presets/askingrefs");
        check(state.mode3_profiles[0].presets==years&&state.mode3_profiles[4].initial==101&&state.mode3_profiles[4].presets[0]==50&&state.mode3_profiles[4].presets[7]==1000,"term and seasonbonus Source profiles retain raw initial not times5");
        editor_event_case(product,state,source,game,w,{TransferOfferEditorField::fee,1200000},"fee write");
        editor_event_case(product,state,source,game,w,{TransferOfferEditorField::salary,50000},"salary write");
        editor_event_case(product,state,source,game,w,{TransferOfferEditorField::years,4},"term quotechange");
        for(unsigned n=0;n<7;++n)editor_event_case(product,state,source,game,w,{TransferOfferEditorField(unsigned(TransferOfferEditorField::bonus0)+n),n<3?20u:1000u},"all7 Native eventwrites");
        editor_unsupported_event(product,state,{TransferOfferEditorField::years,5},"term upperbound");editor_unsupported_event(product,state,{TransferOfferEditorField::bonus0,21},"first3 bonus bound");editor_unsupported_event(product,state,{TransferOfferEditorField::bonus6,1001},"season bonus bound");
        const auto before=save_stream(product);const auto cancelled=cancel_transfer_offer_editor_source(product,state);check(cancelled.supported&&cancelled.closed&&save_stream(product)==before&&product.memory.u16(list)==0xffff,"Cancel retains exact editorwrites and still no buyerword");
        auto stale=product;stale.memory.set8(list+0x55,uint8_t(stale.memory.u8(list+0x55)^1));editor_unsupported_event(stale,state,{TransferOfferEditorField::fee,1},"stale full Native entry");
    }
    {auto product=input;product.players.at(size_t(c.player)).w35c=0;product.players.at(size_t(c.player)).w364=1;product.players.at(size_t(c.player)).w368=0;
        EditorOracle source{product};GameRandom game(73);WatcomRandom w(37);const auto req=editor_request(product,c);source.opening(req);auto state=open_transfer_offer_editor_source(product,req,game,w);source.quote(state,"weak opening");editor_exact(product,w,game,source,"weak opening");
        editor_event_case(product,state,source,game,w,{TransferOfferEditorField::years,1},"oneYearFallback");
        editor_event_case(product,state,source,game,w,{TransferOfferEditorField::fee,0,false},"moneyCalculatorCancel stillQuote");
        editor_event_case(product,state,source,game,w,{TransferOfferEditorField::bonus0,0,false},"bonusCalculatorCancel noQuote");
        editor_event_case(product,state,source,game,w,{TransferOfferEditorField::years,0,false},"termCalculatorCancel noQuote");
        const auto term=state.term;editor_event_case(product,state,source,game,w,{TransferOfferEditorField::toggle_quote_loan,0},"headerLoanToggle");check(state.term==term&&state.quote_loan&&game.calls()==1,"quoteLoan is local only; redraw never consumes newG");
    }
    {auto product=input;product.memory.set16(list,uint16_t(c.buyer));product.memory.set32(list+0x1c,123456);product.memory.set32(list+0x0c,50000);product.memory.set8(list+0x34,48);
        EditorOracle source{product};const auto req=editor_request(product,c);source.opening(req);GameRandom game(73);WatcomRandom w(37);auto state=open_transfer_offer_editor_source(product,req,game,w);source.quote(state,"own opening");editor_exact(product,w,game,source,"own opening");check(state.existing&&state.term==48&&state.bonuses==initial,"own opening retains exact term/bonuses/fees");
        editor_event_case(product,state,source,game,w,{TransferOfferEditorField::bonus6,505},"own raw101 edit");
        const auto native=save_stream(product);const auto wc=w.calls(),gc=game.calls();HumanBuyerSaleHooks hooks;hooks.notice=[](const GameState&,const HumanBuyerSaleNotice&){throw std::runtime_error("own submit must not notice or qualify");};MinuteReport report;
        const auto submitted=submit_transfer_offer_editor_source(product,state,w,hooks,report);check(submitted.editor.supported&&!submitted.editor.closed&&submitted.action.status==TransferActionStatus::pending&&!report.unsupported&&save_stream(product)==native&&w.calls()==wc&&game.calls()==gc,"own Submit staysopen with no new qualification/Native/RNG");
        check(cancel_transfer_offer_editor_source(product,submitted.editor).closed,"own Cancel closes only modal");
    }
    for(uint8_t raw:{uint8_t(101),uint8_t(200)})editor_e2e(input,c,raw);
    {auto human=original;const auto h=make_control(human,true,-1,false,0,250000,70000,false);editor_e2e(human,h,200);}
    // End-to-end the visible OutgoingTransfer loan choice into bid encoding and
    // the shared human-buyer loan settlement (AI owner -> human borrower).
    {
        auto g=input;auto control=make_control(g,false,-1,false,0,250000,70000,false);
        editor_refs(g,control);auto request=editor_request(g,control);request.loan=true;
        GameRandom game(73);WatcomRandom w(0x13579);
        auto editor=open_transfer_offer_editor_source(g,request,game,w);
        check(editor.supported&&editor.editable&&(editor.term&0x80)&&editor.years==2,
              "OutgoingTransfer loan choice initializes a real loan-marked editor term");
        editor.fee=editor.asking_fee*2u;editor.salary=editor.asking_salary*2u;editor.years=2;
        HumanBuyerSaleHooks hooks;hooks.allocator_gate=HumanBuyerSaleAllocatorGate::zero;
        hooks.direct_mode=HumanBuyerSaleDirectMode::ordinary_ai_purchase;
        hooks.notice=[](const GameState&,const HumanBuyerSaleNotice&){};
        MinuteReport report;
        const auto submitted=submit_transfer_offer_editor_source(g,editor,w,hooks,report);
        check(!report.unsupported&&submitted.editor.closed&&submitted.action.status==TransferActionStatus::accepted,
              "loan term survives qualification and submits through the actual offer-editor action");
        int ledger=-1;for(int n=0;n<200&&ledger<0;++n)if(g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+uint32_t(n)*4)!=0&&
            int(g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+uint32_t(n)*4)&0xfff)==control.player)ledger=n;
        check(ledger>=0&&g.memory.u8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField+uint32_t(ledger))==24,
              "offer-editor loan submission writes the Source return ledger");
    }
    for(uint8_t mode:{uint8_t(1),uint8_t(2)}) {auto g=input;g.memory.set8(::bmh::layout::GameMode,mode);GameRandom game(73);WatcomRandom w(37);const auto state=open_transfer_offer_editor_source(g,editor_request(g,c),game,w);check(state.supported&&state.mode==mode,"Native modes1/2 open exact same money/quote owner");editor_unsupported_event(g,state,{TransferOfferEditorField::years,3},"mode3-only field refused in modes1/2");}
    {auto g=input;for(uint32_t n=0;n<4;++n)g.memory.set16(list+n*2,uint16_t(n+2));auto req=editor_request(g,c);req.expected_bid_slot=-1;EditorOracle source{g};source.opening(req);GameRandom game(73);WatcomRandom w(37);const auto state=open_transfer_offer_editor_source(g,req,game,w);source.quote(state,"full-board readonly");editor_exact(g,w,game,source,"full-board readonly");check(!state.editable&&state.bid_slot==-1,"full4buyer board opens exact readonly Sourcequote");editor_unsupported_event(g,state,{TransferOfferEditorField::fee,1},"readonly cannot write a fifthrow");}

    {auto g=input;g.memory.set16(list,0xffff);g.memory.set16(list+2,uint16_t(c.buyer));const auto req=editor_request(g,c);GameRandom game(73);WatcomRandom w(37);const auto state=open_transfer_offer_editor_source(g,req,game,w);check(state.supported&&state.bid_slot==0&&!state.existing,"Source firstFFFF precedes later own word");}
    {auto g=input;auto req=editor_request(g,c);req.expected_bid_slot=3;const auto native=save_stream(g);GameRandom game(73);WatcomRandom w(37);const auto state=open_transfer_offer_editor_source(g,req,game,w);check(!state.supported&&game.calls()==0&&w.calls()==0&&save_stream(g)==native,"stale raw bid slot reject beforeNative/W/G");}
    std::puts("PASS independent format field Native/open/edit/cancel/presets/quote/W/G and ordinary all7 qualification/settlement");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"transfer_offer_editor_tests: %s\n",e.what());return 1;}
