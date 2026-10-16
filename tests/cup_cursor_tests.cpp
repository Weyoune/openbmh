#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "formats/fbm.hpp"
#include "formats/le.hpp"
#include "game/career.hpp"
#include "game/cup.hpp"
#include "game/turn_driver.hpp"
#include "game/national.hpp"
#include "game/save.hpp"
#include "game/schedule.hpp"
#include "game/season.hpp"

using namespace bmh;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
GameState load(const std::filesystem::path& path, const LeImage& exe, const TextDb& texts) {
    const auto file=load_savegame(path);
    check(file.checksum_ok && file.version=="V2.24", "native fixture checksum/version");
    const DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
    GameState g=GameState::from_save(SaveState(file.stream,counts),counts);
    competition_pointers(g.memory,exe);g.texts=&texts;national_coach_after_load(g);return g;
}


}
int main(int argc,char**argv){try {
 check(argc>=3,"usage: cup_cursor_tests <BMH data> <SESEND01.FBM> [outputstream]");
 if(std::string(argv[1]).empty()){std::cout<<"SKIP: BMH_DATA not configured\n";return 77;}
 auto exe=bmh::test::load_executable(std::filesystem::path(argv[1])/"BMH.exe");auto texts=load_textd(std::filesystem::path(argv[1])/"TEXTD.BLK");auto g=load(argv[2],exe,texts);
 auto game=GameRandom(0);auto rand=WatcomRandom(589388519);MinuteReport report;TurnDriver driver;std::string error;
 check(begin_turn_driver(driver,g,{},0,{},1,1,error),"begin native driver");driver.stage=TurnDriver::Stage::SeasonEndDesk;
 check(g.memory.has(::bmh::layout::DeskPosition) && g.memory.u8(::bmh::layout::DeskPosition)==0,"loaded desk owns initial cursor0");
 int observed=0;
#ifndef CUP_CURSOR_BASELINE
 driver.season_cup_draw=[&](const GameState& state,const SeasonCupDrawEvent& event){
  check(state.memory.u8(::bmh::layout::DeskPosition)==state.memory.u8(::bmh::layout::ManagerCount),"both annual cup phases see Source completed manager cursor");
  const DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
  const auto bytes=save_stream(state);
  const SaveState raw(bytes,counts);
  check(raw.data(::bmh::layout::DeskPosition)[0]==state.memory.u8(::bmh::layout::DeskPosition),"native modal snapshot serializes completed cursor");
  check(GameState::from_save(raw,counts).memory.u8(::bmh::layout::DeskPosition)==state.memory.u8(::bmh::layout::DeskPosition),"raw GameState roundtrip preserves completed cursor");
  check(event.phase==(observed==0?SeasonCupDrawPhase::draw:SeasonCupDrawPhase::overview),"two ordered cursor observations");++observed;
 };
#endif
 TurnAdvanceRequest request;request.manager_slot=g.memory.u8(::bmh::layout::ActiveManager);request.revision=1;
 const auto result=advance_to_next_desk(driver,g,request,game,rand,report);
 check(!report.unsupported && result.kind==TurnDeskKind::NextDesk,"native annual transition reaches next real desk");
#ifndef CUP_CURSOR_BASELINE
 check(observed==2,"both cup hooks completed");
#else
 (void)observed;
#endif
 check(g.memory.u8(::bmh::layout::DeskPosition)==0 && driver.manager_position==0,"next real desk restores cursor0");
 std::cout<<"nextdesk date "<<g.date<<" G "<<game.seed()<<'/'<<game.calls()<<" W "<<rand.seed()<<'/'<<rand.calls()<<" cursor "<<int(g.memory.u8(::bmh::layout::DeskPosition))<<'\n';
 if(argc>3){auto stream=save_stream(g);std::ofstream f(argv[3],std::ios::binary);f.write(reinterpret_cast<const char*>(stream.data()),stream.size());}
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
