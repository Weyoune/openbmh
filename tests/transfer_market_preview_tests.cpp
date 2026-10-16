// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "formats/textd.hpp"
#include "game/desk_actions.hpp"
#include "game/save.hpp"

using namespace bmh;
namespace {
void require(bool yes,const std::string& why){if(!yes)throw std::runtime_error(why);}
void put32(std::vector<uint8_t>& bytes,size_t at,uint32_t n){for(size_t k=0;k<4;++k)bytes.at(at+k)=uint8_t(n>>(k*8));}
uint32_t read32(const std::vector<uint8_t>& bytes,size_t at){return uint32_t(bytes.at(at))|uint32_t(bytes.at(at+1))<<8|uint32_t(bytes.at(at+2))<<16|uint32_t(bytes.at(at+3))<<24;}
struct WOracle {
    uint32_t seed=37;uint64_t calls=0;
    uint32_t low_value(){seed=seed*0x41c64e6du+0x3039u;++calls;return 50000u+(((seed>>16)&0x7fffu)*30001u>>15);}
};
struct Scalar {uint32_t fee,salary;};
uint64_t observed_w=0;bool only_w=true;
void observe_draw(int generator){if(generator==1)++observed_w;else only_w=false;}

// Independent years1/extra0 format field equations. High-value increments are
// the Original EXE bytes PlayerCard_PlayerCardMarketValue_RuntimeField..a4327, not a production constants/helper read.
// No production valuation or quote function is called in this oracle.
Scalar source_value(const Player& p,bool loan,WOracle& w){
    constexpr std::array<int,10> high={10,15,20,30,40,45,55,75,85,95};
    const int age=std::min(20,int(p.w364>>21&31)+1);
    const int attrs=int(p.w35c>>7&127)+int(p.w35c>>14&127);
    int points=std::clamp(attrs*5/2+150-2*std::abs(11-age),12,620);
    if(points>500)points+=points<600?high[size_t((points-500)/10)]:100;
    const int curve=((points/4+1)*points*(points/8+100))/10;
    const int pos=int(p.w364&3),experience=int(p.w364>>26&63)/4;
    const int skill=(int(p.w364>>10&63)*3+int(p.w368>>7&63))/2;
    const int factor=experience+(pos==0?110:100+(pos*skill/3)/2);
    uint32_t fee=uint32_t(factor*curve/100);if(fee<50000)fee=w.low_value();
    const uint32_t salary=(fee/40u)/100u*100u;
    return {loan?fee/4u:fee,salary};
}
int32_t signed32(uint32_t value){return std::bit_cast<int32_t>(value);}
void control(Player& p,int technique,int condition,int age,int position,int x=0,int y=0,int experience=0){
    // Declared scalar controls on a genuine player, never an Original runtime
    // clock or quote capture. The source pool/outgoing identity is unchanged.
    p.w35c=(p.w35c&~((127u<<7)|(127u<<14)))|uint32_t(technique)<<7|uint32_t(condition)<<14;
    p.w364=(p.w364&~((31u<<21)|3u|(63u<<10)|(63u<<26)))|uint32_t(age-16)<<21|uint32_t(position)|uint32_t(x)<<10|uint32_t(experience)<<26;
    p.w368=(p.w368&~(63u<<7))|uint32_t(y)<<7;
}
TransferMarketPreviewRequest purchase(const GameState& g){
    const auto raw=g.memory.u16(::bmh::layout::EditorList+8);
    return {0,g.manager_teams.at(0),2928,int(g.squad_slots.at(2928)&0xfff),raw==0xffff?-1:int(raw&0x7fff),true,false};
}
TransferMarketPreviewRequest outgoing(const GameState& g,int manager){
    const auto range=squad_range(g.manager_teams.at(size_t(manager)));
    for(size_t slot=range.first;slot<range.first+range.count;++slot)if((g.squad_slots.at(slot)&0xfff)!=0xfff)
        return {manager,g.manager_teams.at(size_t(manager)),int(slot),int(g.squad_slots.at(slot)&0xfff),g.manager_teams.at(size_t(manager)),false,false};
    throw std::runtime_error("genuine manager must have an occupied outgoing Source row");
}
void compare(GameState g,const TransferMarketPreviewRequest& r,const std::string& name,Scalar hard,uint64_t calls){
    const auto before=save_stream(g);const auto boards=g.tactics;
    WOracle oracle;const auto expected=source_value(g.players.at(size_t(r.expected_player)),r.loan,oracle);
    require(expected.fee==hard.fee&&expected.salary==hard.salary&&oracle.calls==calls,name+": independent hard Source vector");
    const int32_t signed_fee=signed32(r.purchase?0u-expected.fee:expected.fee);
    const uint32_t rounded=uint32_t(signed_fee/10000)*10000u;
    const int32_t delta=signed32(rounded),balance=signed32(read32(g.manager_records,size_t(r.manager)*0xb0c+0x314)+rounded);
    WatcomRandom actual_w(37);GameRandom game(987654);const auto game_seed=game.seed();const auto game_calls=game.calls();
    require(draw_hook==nullptr,"preview observer has sole draw-hook ownership");observed_w=0;only_w=true;draw_hook=observe_draw;
    struct ResetObserver{~ResetObserver(){draw_hook=nullptr;}} reset_observer;
    const auto actual=quote_transfer_market_preview_source(g,r,actual_w);
    require(only_w&&observed_w==calls,name+": observed RNG events contain only exact Watcom fallback calls");
    require(actual.supported&&actual.reason.empty(),name+": supported typed quote");
    require(actual.fee==expected.fee&&actual.salary==expected.salary&&actual.delta==delta&&actual.projected_balance==balance,name+": complete Source value/NEG/IDIV/IMUL/ADD32 result");
    require(actual_w.seed()==oracle.seed&&actual_w.calls()==oracle.calls,name+": exact0/1 W fallback sequence");
    require(game.seed()==game_seed&&game.calls()==game_calls,name+": G unchanged");
    require(save_stream(g)==before&&g.tactics==boards,name+": complete Native and all boards unchanged");
}
void unsupported(GameState g,const TransferMarketPreviewRequest& r,const std::string& name){
    const auto before=save_stream(g);WatcomRandom w(37);
    const auto actual=quote_transfer_market_preview_source(g,r,w);
    require(!actual.supported&&!actual.reason.empty(),name+": typed unsupported with reason");
    require(w.seed()==37&&w.calls()==0&&save_stream(g)==before,name+": reject before Native/RNG mutation");
}
}
int main(int argc,char** argv) try{
    require(argc==3,"usage: transfer_market_preview_tests DATA_DIR genuine_2PNWK001.FBM");
    if(!*argv[1]){std::puts("market preview tests: skipped (Original data not configured)");return 77;}
    const auto fbm=load_savegame(argv[2]);require(fbm.checksum_ok,"genuine Native checksum");
    auto texts=load_textd(std::filesystem::path(argv[1])/"TEXTD.BLK");
    auto base=GameState::from_save(SaveState(fbm.stream));base.texts=&texts;
    require(base.manager_teams==std::vector<int>({1,54}),"genuine two-manager club identity");
    base.memory.set8(::bmh::layout::ActiveManager,0);const auto r=purchase(base);
    require(r.expected_player!=0xfff,"genuine ordinary market row occupied");
    struct Vector {int t,c,age,pos,x,y,experience;Scalar sale,loan;uint64_t draws;};
    constexpr Vector vectors[]={
        {0,0,16,1,0,0,0,{50389,1200},{12597,1200},1},
        {0,0,47,1,0,0,0,{52060,1300},{13015,1300},0},
        {70,70,26,1,0,0,0,{1020600,25500},{255150,25500},0},
        {71,70,26,1,0,0,0,{1083187,27000},{270796,27000},0},
        {80,80,26,1,0,0,0,{1542597,38500},{385649,38500},0},
        {90,90,26,0,0,0,0,{2534224,63300},{633556,63300},0},
        {127,127,47,3,63,63,63,{4407422,110100},{1101855,110100},0}};
    for(const auto& v:vectors)for(bool loan:{false,true}){
        auto g=base;auto q=r;q.loan=loan;control(g.players.at(size_t(q.expected_player)),v.t,v.c,v.age,v.pos,v.x,v.y,v.experience);
        compare(g,q,"purchase scalar",loan?v.loan:v.sale,v.draws);
        // Source identity is deliberately changed in declared controls below;
        // both canonical free-agent and flagged seller words must be observed.
    }
    for(int manager:{0,1})for(bool loan:{false,true}){
        auto g=base;g.memory.set8(::bmh::layout::ActiveManager,uint8_t(manager));auto q=outgoing(g,manager);q.loan=loan;
        control(g.players.at(size_t(q.expected_player)),80,80,26,1);
        put32(g.manager_records,size_t(manager)*0xb0c+0x314,0x7ffff000u);
        compare(g,q,"outgoing ADD32 balance wrap",loan?Scalar{385649,38500}:Scalar{1542597,38500},0);
    }
    {auto g=base;auto q=r;control(g.players.at(size_t(q.expected_player)),80,80,26,1);put32(g.manager_records,0x314,0x80001000u);compare(g,q,"purchase signed balance wrap",{1542597,38500},0);}
    for(uint16_t raw:{uint16_t(0x0fff),uint16_t(0xffff),uint16_t(0x8002)}){
        auto g=base;g.memory.set16(::bmh::layout::EditorList+8,raw);auto q=purchase(g);control(g.players.at(size_t(q.expected_player)),80,80,26,1);compare(g,q,"exact Source seller normalization",{1542597,38500},0);
    }
    auto bad=r;bad.manager=1;unsupported(base,bad,"inactive manager");
    bad=r;bad.runtime_club=54;unsupported(base,bad,"changed runtime club");
    bad=r;bad.expected_player^=1;unsupported(base,bad,"recycled selected player");
    bad=r;bad.source_slot=2968;unsupported(base,bad,"special pending slot is not ordinary market");
    bad=r;bad.seller=2;unsupported(base,bad,"changed Native seller");
    {auto g=base;g.manager_records.at(0x290)=54;unsupported(g,r,"record/runtime team disagreement");}
    {auto g=base;g.squad_slots.at(2928)=0x0fff;unsupported(g,r,"empty Native market slot");}
    {auto q=outgoing(base,0);q.source_slot=24+24;q.expected_player=int(base.squad_slots.at(48)&0xfff);unsupported(base,q,"outgoing outside actual squad");}
    {auto q=outgoing(base,0);q.seller=54;unsupported(base,q,"outgoing seller is another club");}
    {auto g=base;auto q=outgoing(g,0);
        // Declared Native loan-owner control: Source's one borrowed row removes
        // the final ordinary physical row from the displayed selectable range.
        g.squad_slots.at(47)=uint16_t(q.expected_player);q.source_slot=47;
        g.memory.set16(::bmh::layout::Career_CareerManagersEnd_RuntimeField,uint16_t(q.expected_player));g.memory.set8(::bmh::layout::ClubInfo_FormatError_RuntimeField2,0);
        unsupported(g,q,"borrowed display row cannot be reinterpreted as outgoing ordinary slot");}

    {auto g=base;auto q=r;control(g.players.at(size_t(q.expected_player)),0,0,16,1);const auto native=save_stream(g);WatcomRandom w(37);
        require(draw_hook==nullptr,"sole draw-hook ownership");draw_hook=[](int){throw std::runtime_error("controlled fallback interruption");};
        bool threw=false;try{(void)quote_transfer_market_preview_source(g,q,w);}catch(const std::runtime_error&){threw=true;}draw_hook=nullptr;
        require(threw&&save_stream(g)==native,"fallback callback exception never writes Native; owner rolls back staged W");}
    std::puts("PASS independent format field/42307 Native identity/value/rounding/0-or1W preview vectors");return 0;
}catch(const std::exception& e){draw_hook=nullptr;std::fprintf(stderr,"transfer_market_preview_tests: %s\n",e.what());return 1;}
