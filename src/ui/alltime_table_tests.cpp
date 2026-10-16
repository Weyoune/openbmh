// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "alltime_table.hpp"
#include "game/table.hpp"
#include "game/messages.hpp"
#include "ui/manager.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void put16(bmh::Bytes& b,uint32_t base,uint32_t a,uint16_t x){b.at(a-base)=uint8_t(x);b.at(a-base+1)=uint8_t(x>>8);}
void rec(bmh::GameState&g,int team,uint16_t points,uint16_t gf,uint16_t ga,uint16_t w,uint16_t d,uint16_t l){size_t p=size_t(team)*0x4a+0x1e;for(auto [o,v]:{std::pair{0,points},std::pair{2,gf},std::pair{4,ga},std::pair{6,w},std::pair{8,d},std::pair{10,l}}){g.club_table[p+o]=uint8_t(v);g.club_table[p+o+1]=uint8_t(v>>8);}}
}
int main(int argc,char**argv){
 constexpr uint32_t base=::bmh::layout::GermanDataSegmentBegin; bmh::Bytes bytes(0x5000);auto set=[&](uint32_t a,uint16_t v){put16(bytes,base,a,v);};bytes[::bmh::layout::AlltimeTable_Layout_ExecutableField-base]=0;set(::bmh::layout::First,0);set(::bmh::layout::AlltimeTable_AlltimeTableFromGame_RuntimeField,4);set(::bmh::layout::FixtureAllTimeAppearanceColumn,4);set(::bmh::layout::FixtureAllTimePointsColumn,4);
 auto exe=bmh::LeImage::from_bytes(base,std::move(bytes));bmh::GameState g;g.club_table.resize(168*0x4a);g.club_ids.resize(0x16a);g.club_names={'A',0,'B',0,'C',0,'D',0};g.club_ids[0]=0;g.club_ids[1]=2;g.club_ids[2]=4;g.club_ids[3]=6;g.club_ids[0x165]=0;
 rec(g,0,10,8,3,2,1,1);rec(g,1,10,8,3,2,1,1);rec(g,2,11,9,2,3,1,0);rec(g,3,0,0,0,0,0,0);
 auto owner=bmh::ui::DeskRequest{};auto abs=bmh::ui::alltime_table_from_game(g,exe,owner,bmh::Image(320,240));check(abs.has_data&&abs.rows.size()==3,"played rows are projected");check(abs.rows[0].team==2,"points order is descending");check(abs.rows[1].team==0&&abs.rows[2].team==1,"ties preserve scan order");check(abs.records[0].points==10,"record points are decoded");
 auto rel=bmh::ui::alltime_table_from_game(g,exe,owner,bmh::Image(320,240),0,true);check(rel.rows.size()==3,"relative rows are projected");check(rel.rows[0].points==uint32_t(11)*bmh::league_matchdays(0)*10/4,"relative points use full season scale");check(rel.rows[0].goals_for==uint32_t(9)*bmh::league_matchdays(0)*10/4,"relative goals use full season scale");
 auto empty=g;empty.club_table.clear();auto absent=bmh::ui::alltime_table_from_game(empty,exe,owner,bmh::Image(320,240));check(!absent.has_data&&absent.rows.empty(),"missing optional table remains empty");
 if(argc>1&&argv[1][0]){
  bmh::ui::AllTimeTableScreen screen(argv[1],abs);auto absolute_frame=screen.render();
  const auto original_exe=bmh::load_le(bmh::ui::asset_path(argv[1],"BMH.exe"));
  const auto font=bmh::load_font(bmh::ui::asset_path(std::filesystem::path(argv[1])/"F","BMH3.FNT"));
  bmh::ui::AllTimeTableScreen relative_screen(argv[1],rel);
  auto relative_frame=relative_screen.render();
  auto expected=rel.underlay;
  const auto& first=rel.rows.front();const auto& record=rel.records[size_t(first.team)];
  std::string stats=bmh::format_number(g,first.played,0xc4)+original_exe.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField);
  for(auto value:{record.wins,record.draws,record.losses})
   stats+=bmh::format_number(g,value,0x44)+original_exe.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField);
  stats+=original_exe.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField3);
  stats+=bmh::format_number(g,first.points/10,7)+original_exe.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField4)+
         bmh::format_number(g,first.points%10,1)+original_exe.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField5);
  stats+=bmh::format_number(g,first.goals_for/10,5)+original_exe.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField4)+
         bmh::format_number(g,first.goals_for%10,1)+original_exe.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField7);
  stats+=bmh::format_number(g,first.goals_against/10,3)+original_exe.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField4)+
         bmh::format_number(g,first.goals_against%10,1)+original_exe.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField8);
  font.draw(expected,0x67,38-font.baseline,stats,rel.club_name_colors[size_t(first.team)]);
  for(int y=38-font.baseline;y<38-font.baseline+font.height;++y)
   for(int x=0x67;x<320;++x)
    check(relative_frame.at(x,y)==expected.at(x,y),
          "relative numeric row preserves Source spaces reserved for unused grouping dots");
  check(!screen.state().relative&&screen.pointer_down(210,189)&&screen.release(210,189),"relative control accepts its Source hitbox");
  check(screen.state().relative&&screen.pending()&&screen.pending()->kind==bmh::ui::AllTimeRequestKind::toggle_relative&&screen.pending()->relative,"relative hit requests relative mode, not absolute");
  check(screen.render().pixels!=absolute_frame.pixels,"toggling relative mode changes the table and selected control rendering");
  std::cout<<"all-time projection and original-renderer checks passed\n";
 }else std::cout<<"all-time projection checks passed; original-renderer check skipped (no BMH_DATA)\n";
}
