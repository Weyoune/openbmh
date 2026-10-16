#include "../../tests/runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "finance_totals.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "game/messages.hpp"
#include "game/save.hpp"
#include "ui/advertising.hpp"
#include "ui/setup.hpp"

namespace {
void check(bool b,const char* what){if(!b)throw std::runtime_error(what);}
uint32_t u32(const std::vector<uint8_t>& b,size_t at){return uint32_t(b.at(at))|uint32_t(b.at(at+1))<<8|uint32_t(b.at(at+2))<<16|uint32_t(b.at(at+3))<<24;}
uint16_t u16(const std::vector<uint8_t>& b,size_t at){return uint16_t(b.at(at))|uint16_t(b.at(at+1))<<8;}
int32_t si32(const std::vector<uint8_t>& b,size_t at){return int32_t(u32(b,at));}
}
int main(int argc,char**argv){try{
 if(argc!=3)throw std::runtime_error("usage: finance_totals_w196_test DATA_DIR WEEK196.FBM");
 const std::filesystem::path data=argv[1];const auto exe=bmh::test::load_executable(bmh::ui::asset_path(data,"BMH.exe"));
 const auto raw=bmh::load_savegame(argv[2]);check(raw.checksum_ok&&raw.version=="V2.24","W196 save checksum/version");
 const bmh::DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
 const bmh::SaveState save(raw.stream,counts);auto game=bmh::GameState::from_save(save,counts);
 const auto texts=bmh::load_textd(bmh::ui::asset_path(data,"TEXTD.BLK"));game.texts=&texts;game.currency_suffix=exe.cstring(::bmh::layout::CurrencySuffixText);
 const int m=game.memory.u8(::bmh::layout::ActiveManager);check(m>=0&&m<4,"W196 active manager");const size_t rec=size_t(m)*0xb0c;
 const auto before=bmh::save_stream(game);const auto totals=bmh::ui::finance_totals_from_game(game,m);
 check(totals.manager_slot==m,"manager ownership");
 check(totals.rows[0]==si32(game.manager_records,rec+0x314),"Source row 0 b6f2c");
 check(totals.rows[2]==int32_t(uint32_t(totals.rows[0])-(u32(game.manager_records,rec+0x324)+u32(game.manager_records,rec+0x30c))),"Source row 2 b6f3c/b6f24");
 check(totals.rows[3]==si32(game.manager_records,rec+0x320),"Source row 3 b6f38");
 check(totals.rows[5]==si32(game.manager_records,rec+0x338),"Source row 5 b6f50");
 check(totals.rows[6]==si32(game.manager_records,rec+0x334),"Source row 6 b6f4c");
 check(totals.rows[7]==si32(game.manager_records,rec+0x33c),"Source row 7 b6f54");
 uint32_t expected=u32(game.manager_records,rec+0x318);
 for(uint32_t i=0;i<6;++i)expected+=uint32_t(game.memory.u16(::bmh::layout::Advertising_OriginalShareAdjustment_RuntimeField+i*0x1c+uint32_t(m)*2))*game.memory.u32(::bmh::layout::FundBlock+i*0x1c);
 for(uint32_t i=0;i<12;++i){size_t at=size_t(i)*0x5c;expected+=uint32_t(u16(game.stocks,at+0x50+size_t(m)*2))*u16(game.stocks,at+0x4e);}
 for(uint32_t i=0;i<5;++i)expected-=game.memory.u32(::bmh::layout::Analytics_SourceAssetMaskE_RuntimeField+uint32_t(m)*0x78+i*0x18);
 check(totals.rows[1]==int32_t(expected),"legacy routine mask 1e fixed assets, stocks and loan subtraction");
 const auto monthly=bmh::ui::original_finance_preview(game,m);
 check(totals.rows[8]==monthly.income&&totals.rows[9]==monthly.expenses,"Original monthly preview rows");
 check(totals.rows[10]==int32_t(uint32_t(monthly.income)-uint32_t(monthly.expenses)),"Original monthly balance row");
 for(size_t i=0;i<totals.rows.size();++i)if(i!=4)check(totals.amounts[i]==bmh::format_number(game,totals.rows[i],0x108),"Source currency formatting");
 check(totals.rows[4]==0&&totals.amounts[4].empty(),"section separator has no value");
 check(bmh::save_stream(game)==before,"finance projection leaves W196 save state unchanged");
 std::cout<<"W196 finance rows passed for active manager "<<m<<"\n";return 0;
 }catch(const std::exception&e){std::cerr<<"finance totals W196 test: "<<e.what()<<'\n';return 1;}}
