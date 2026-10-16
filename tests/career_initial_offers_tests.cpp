#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/career.hpp"
#include "formats/le.hpp"
#include "game/save.hpp"
#include <iostream>
#include <stdexcept>

using namespace bmh;
namespace {void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}}
int main(int argc,char**argv){try{
    if(argc<2||std::string(argv[1]).empty()){std::cout<<"SKIP: BMH_DATA unavailable\n";return 77;}
    const std::filesystem::path data=argv[1];auto exe=bmh::test::load_executable(data/"BMH.exe");
    auto db=load_database(data/"M3D.MAN");auto texts=load_textd(data/"TEXTD.BLK");
    auto before=startup_state(exe,db,read_file(data/"M3.MEN"),read_file(data/"M3.DAT"));
    before.texts=&texts;
    for(unsigned i=0;i<4;++i)check(exe.u32(::bmh::layout::OfferSlots+i*4)==0&&before.memory.u32(::bmh::layout::OfferSlots+i*4)==0,
        "raw EXE BSS and startup_state carry zero, not empty pending offer sentinel");
    // Use the existing exact6ba3e split helpers as unchanged baseline, with
    // identical full native state and both seeds. No fabricated RNG parity.
    auto expected=before,actual=before;GameRandom ge(123456),ga(123456);
    WatcomRandom we(987654321),wa(987654321);MinuteReport re,ra;
    new_career_markets_and_moods(expected,ge,we);
    check(new_career_players(expected,ge,we,re)&&!re.unsupported,"baseline6ba3e split completes");
    check(new_career_before_dialog(actual,ga,wa,ra)&&!ra.unsupported,"fresh predecessor+6ba3e completes");
    check(ge.seed()==ga.seed()&&ge.calls()==ga.calls()&&we.seed()==wa.seed()&&we.calls()==wa.calls(),
        "four offer sentinel writes consume neither RNG and preserve exact initialization draw sequence");
    auto baseline_bytes=save_stream(expected),actual_bytes=save_stream(actual);
    check(baseline_bytes.size()==actual_bytes.size(),"native stream length unchanged");
    const SaveState native(actual_bytes);const auto offset=native.block(::bmh::layout::OfferSlots).offset;
    unsigned differences=0;
    for(size_t i=0;i<baseline_bytes.size();++i)if(baseline_bytes[i]!=actual_bytes[i]){
        check(i>=offset&&i<offset+16,"only four pending offer words differ from unchanged baseline");++differences;
    }
    check(differences==16,"all16 raw zero pending offer bytes become Source ff");
    for(unsigned i=0;i<4;++i)check(actual.memory.u32(::bmh::layout::OfferSlots+i*4)==0xffffffff,"all four initial offers empty");
    auto loaded=GameState::from_save(native);
    for(unsigned i=0;i<4;++i)check(loaded.memory.u32(::bmh::layout::OfferSlots+i*4)==0xffffffff,"fresh native raw roundtrip preserves sentinel");
    // Loading uses the persisted offer values, not fresh-career bootstrap.
    // A genuine pending manager0/player0 offer remains zero on native reload.
    loaded.memory.set32(::bmh::layout::OfferSlots,0);auto loaded_bytes=save_stream(loaded);
    auto valid_offer=GameState::from_save(SaveState(loaded_bytes));
    check(valid_offer.memory.u32(::bmh::layout::OfferSlots)==0,"loaded legitimate offer0 is preserved");
    std::cout<<"format field initial offers:16 native bytes only; G "<<ga.seed()<<'/'<<ga.calls()
             <<" W "<<wa.seed()<<'/'<<wa.calls()<<"; native roundtrip and loaded genuine offer preserved\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
