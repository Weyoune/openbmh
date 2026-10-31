// SPDX-License-Identifier: GPL-3.0-only
#include "world12_fixture.hpp"
#include "app/world12_manager.hpp"
#include "game/world12/codec.hpp"
#include "game/world12/save.hpp"
#include "game/matchday.hpp"
#include "formats/fbm.hpp"
#include "formats/le.hpp"
#include <bit>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace bmh::world12;namespace app=bmh::app::world12;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class Function> void rejects(Function function,const char* message){bool rejected=false;try{function();}catch(const std::exception&){rejected=true;}check(rejected,message);}
void set_word(bmh::Bytes& bytes,size_t at,uint32_t value){for(size_t i=0;i<4;++i)bytes.at(at+i)=uint8_t(value>>(i*8));}
bmh::GameState reference_state(const ManagerFinance& finance,int difficulty,int slot=0) {
    bmh::GameState state;state.level=difficulty;state.manager_records.assign(size_t(slot+1)*0xb0c,0x5a);
    const auto at=size_t(slot)*0xb0c;
    set_word(state.manager_records,at+0x314,std::bit_cast<uint32_t>(finance.balance));set_word(state.manager_records,at+0x31c,finance.credit);
    set_word(state.manager_records,at+0x874,finance.income);set_word(state.manager_records,at+0x8a0,finance.expenses);set_word(state.manager_records,at+0x334,finance.bonus_paid);
    return state;
}
void math_cases() {
    const auto imax=std::numeric_limits<int32_t>::max(),imin=std::numeric_limits<int32_t>::min();
    const auto umax=std::numeric_limits<uint32_t>::max();
    for(const auto input:{ManagerFinance{100,50,3,4,5},ManagerFinance{-50,100,umax-3,umax-4,7},ManagerFinance{imax,1,0,0,umax},ManagerFinance{imin,umax,umax,umax,1}})
        for(int difficulty:{0,1,9})for(int32_t amount:{int32_t(0),int32_t(120),int32_t(151),int32_t(-30),imin,imax})
            for(bool force:{false,true})for(bool statistics:{false,true}) {
                auto native=reference_state(input,difficulty,2);const auto untouched=native.manager_records;
                const bool allowed=bmh::book_money(native,2,amount,force,statistics);
                const auto expected=manager_finance_from_legacy(native,2);
                const auto actual=calculate_manager_booking(input,difficulty,amount,force,statistics);
                check(actual.allowed==allowed && actual.finance==expected && actual.changed==(expected!=input),"typed financial words differ from direct Original booking");
                for(size_t i=0;i<2*0xb0c;++i)check(native.manager_records[i]==untouched[i],"Original booking touched a different manager slot");
            }
    check(!calculate_manager_booking({100,50,0,0,0},1,151,false,true).allowed,"insufficient credit was accepted");
    check(calculate_manager_booking({100,50,0,0,0},1,120,false,true).finance.balance==-20,"credit-funded payment incorrectly clamped cash to zero");
    rejects([&]{calculate_manager_booking({},10,1,false,false);},"unknown difficulty accepted");
    rejects([&]{manager_finance_from_legacy(bmh::GameState{},0);},"missing legacy record fabricated a zero balance");
}
void ownership_cases() {
    auto world=bmh::test::world12::complete_world();const auto club=world.membership[0].clubs[0];
    world.managers={{2,"Manager",club,{0},2}};
    app::ManagerActionOwner owner{91,7};const app::ManagerActionIdentity identity{2,club,91,7};const auto initial=world;
    rejects([&]{app::book_manager_funds(world,owner,identity,1,10,false,true);},"uninitialized manager finances accepted");check(world==initial&&owner.revision==7,"failed initialization check mutated manager state");
    world.managers[0].finance=ManagerFinance{100,50,3,4,5};const auto before=world;
    const auto denied=app::book_manager_funds(world,owner,identity,1,151,false,true);
    check(!denied.allowed&&world==before&&owner.revision==7,"rejected payment changed the world or revision");
    const auto disabled=app::book_manager_funds(world,owner,identity,0,120,true,true);
    check(disabled.allowed&&!disabled.changed&&world==before&&owner.revision==7,"no-money mode altered funds or counters");
    const auto applied=app::book_manager_funds(world,owner,identity,1,120,false,true);
    check(applied.allowed&&applied.changed&&owner.revision==8&&world.managers[0].finance->balance==-20&&world.managers[0].finance->expenses==124,"accepted payment did not publish balance/statistics/revision together");
    const auto committed=world;
    rejects([&]{app::book_manager_funds(world,owner,identity,1,120,false,true);},"payment replay accepted");check(world==committed&&owner.revision==8,"replay mutated funds");
    auto foreign=identity;foreign.revision=8;foreign.club=world.membership[5].clubs[0];
    rejects([&]{app::book_manager_funds(world,owner,foreign,1,1,true,true);},"different club's financial request accepted");
    auto stale_scope=identity;stale_scope.revision=8;stale_scope.scope=92;
    rejects([&]{app::book_manager_funds(world,owner,stale_scope,1,1,true,true);},"old session scope accepted");
    check(decode_world(encode_world(world))==world,"optional manager finances lost on world roundtrip");
    CareerSnapshot snapshot;snapshot.world=world;snapshot.results=SeasonResults::empty(world.catalogue);
    check(read_save(make_save(snapshot))==snapshot,"manager financial words lost on snapshot roundtrip");
}
void original_save(const std::filesystem::path& data,const std::filesystem::path& save) {
    const auto exe=bmh::load_le(data/"BMH.exe");
    const bmh::DbCounts counts{exe.u32(bmh::layout::CareerSession_DataDirectory_RuntimeField),exe.u32(bmh::layout::CareerSession_DataDirectory_RuntimeField3),exe.u32(bmh::layout::CareerSession_DataDirectory_RuntimeField2),exe.u32(bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
    const auto file=bmh::load_savegame(save);check(file.checksum_ok&&file.version=="V2.24","invalid reference save");
    const auto source=bmh::GameState::from_save(bmh::SaveState(file.stream,counts),counts);
    const auto finance=manager_finance_from_legacy(source,0);
    for(int32_t amount:{100,-30,std::numeric_limits<int32_t>::min()}) {
        auto expected=source;const bool allowed=bmh::book_money(expected,0,amount,true,true);
        const auto actual=calculate_manager_booking(finance,source.level,amount,true,true);
        check(actual.allowed==allowed&&actual.finance==manager_finance_from_legacy(expected,0),"financial extraction/booking differs for original-save manager");
    }
    std::cout<<"Original-save financial words and bookings matched; reference career was not converted or written\n";
}
}
int main(int argc,char** argv)try {
    math_cases();ownership_cases();if(argc==3)original_save(argv[1],argv[2]);else if(argc!=1)throw std::runtime_error("usage: world12_manager_finance_tests [reference-directory original.FBM]");
    std::cout<<"288 financial cases, initialized-state ownership, replay protection and financial codec/save roundtrips passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
