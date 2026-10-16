// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <iostream>
#include <stdexcept>
#include "ui/cup_draw.hpp"
using namespace bmh;using namespace bmh::ui;
static void require(bool ok,const char* m){if(!ok)throw std::runtime_error(m);}
int main(int argc,char**argv){try{
 require(argc==2,"usage: cup_script_oracle <BMH data>");if(!*argv[1]){std::cout<<"SKIP: Original data not configured\n";return 77;}std::filesystem::path d=argv[1];auto exe=load_le(d/"BMH.exe");auto atlas=load_vga(d/"KB"/"74.VGA");
 CupDrawProjection p;p.phase=SeasonCupDrawPhase::draw;p.count=64;p.source_option_delay_ms=60;p.clubs.resize(64);for(int i=0;i<64;++i){p.clubs[size_t(i)].runtime_id=uint16_t(90+i);p.clubs[size_t(i)].badge_selector=0;p.clubs[size_t(i)].original_name="TEAM";}
 CupDrawScreen screen(d,p);Image visible=screen.render(),drawing=visible;std::vector<Image> expected;std::vector<bool> copy_back;
 for(auto [address,limit]:{std::pair{::bmh::layout::FixtureCupFirstDrawingScriptUnsigned,10},std::pair{::bmh::layout::FixtureCupSecondDrawingScriptUnsigned,4}}){unsigned at=address;int waits=0;while(waits<limit){auto token=exe.u8(at++);require(token!=255,"Source script has required wait markers");if(token==0){int sx=exe.u8(at++),sy=exe.u8(at++),dx=exe.u8(at++),dy=exe.u8(at++),w=exe.u8(at++),h=exe.u8(at++);for(int y=0;y<h;++y)for(int x=0;x<w;++x)drawing.at(dx+x,dy+y)=atlas.at(sx+x,sy+y);}else{std::swap(visible,drawing);if(token<101)drawing=visible;expected.push_back(visible);copy_back.push_back(token<101);++waits;}}}
 require(expected.size()==14,"Source10+4 waits");int actual=0,initial=0,polls=0;bool saw_name=false;
 for(int step=0;step<200 && screen.advance_step();++step){if(screen.input_poll_needed())++polls;if(screen.option_delay_milliseconds()==60 && !screen.held_mouse_check_needed())++initial;if(screen.held_mouse_check_needed()){
  require(actual<14,"renderer has no extra script wait");require(screen.present_needed(),"both Source0/1 swap frames are visible");require(screen.wait_milliseconds()==20 && screen.option_delay_milliseconds()==60,"Source option then fixed20ms");require(screen.render().pixels==expected[size_t(actual)].pixels,"actual renderer frame matches executable script and Source0/1 buffer semantics");require(!screen.queue_flush_needed(),"script bdb88 prevents queue flush");++actual;
 }if(screen.revealed_count()==1){require(screen.queue_flush_needed() && screen.present_needed(),"names commit Source1 and flush");saw_name=true;break;}}
 require(actual==14 && initial==10 && polls==12 && saw_name,"initial10, beforefirstclubpoll,14waits and completefirstclub");
 require(expected[5].pixels==expected[7].pixels && expected[7].pixels==expected[9].pixels && expected[6].pixels==expected[8].pixels && expected[5].pixels!=expected[6].pixels,"Source101 blink alternates distinct exact frames");
 std::cout<<"14/14 full script frames match actual EXE; Source101 blink,10 initial option polls, held checks and name flush verified\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
