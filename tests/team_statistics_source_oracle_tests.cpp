#include "runtime_fixture_tables.hpp"
#include "test_inputs.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/team_statistics.hpp"
#include "ui/manager.hpp"
#include "formats/fbm.hpp"
#include "game/save.hpp"
#include "game/cup.hpp"
#include "game/national.hpp"
#include "game/schedule.hpp"
#include "game/career.hpp"
#include <iostream>
#include <stdexcept>

using namespace bmh;
using namespace bmh::ui;
namespace {
int failures=0, assertions=0;
void check(bool ok,const std::string& why){++assertions;if(!ok){++failures;std::cerr<<"FAIL "<<why<<'\n';}}
// Independent FNT-bit oracle. No screen text helper or render output supplies
// expected glyphs, placement, color ramp, or pattern pixels.
void letters(Image& image,const Font& font,const LeImage& exe,int x,int baseline,
             const std::string& bytes,uint8_t color,bool gradient=true){
    unsigned ramp=0;while(ramp<32&&exe.u8(::bmh::layout::Advertising_Text_RuntimeField2+ramp)!=color)++ramp;
    for(unsigned char c:bytes){const auto& glyph=font.glyph(c);
        for(int y=0;y<font.height;++y)for(int b=0;b<glyph.bit_width;++b){
            int bit=glyph.bit_offset+b;
            if(font.bitmap[size_t(y)*font.modulo+bit/8]&(0x80>>(bit%8)))
                image.at(x+b,baseline-font.baseline+y)=gradient?exe.u8(::bmh::layout::Advertising_Text_RuntimeField+ramp*6+y):color;
        }x+=glyph.advance;
    }
}
Image pattern(const LeImage& exe){Image image(320,240,0x52);
    for(int y=42;y<=196;++y)for(int x=4;x<=316;++x)
        image.at(x,y)=exe.u8(::bmh::layout::Advertising_PatternRect_RuntimeField+((y-42)&15)*16+((x-4)&15));
    return image;
}
void region(const Image& actual,const Image& expected,int x,int y,int w,int h,const std::string& why){
    int differences=0;for(int yy=y;yy<y+h;++yy)for(int xx=x;xx<x+w;++xx)
        differences+=actual.pixels[size_t(yy)*320+xx]!=expected.pixels[size_t(yy)*320+xx];
    check(!differences,why+" ("+std::to_string(differences)+" pixels)");
}
void atlas_region(const Image& actual,const Image& atlas,int sx,int sy,int x,int y,int w,int h,const std::string& why){
    int differences=0;for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx)
        differences+=actual.pixels[size_t(y+yy)*320+x+xx]!=atlas.pixels[size_t(sy+yy)*atlas.width+sx+xx];
    check(!differences,why+" ("+std::to_string(differences)+" pixels)");
}
TeamStatisticsProjection fixture(){TeamStatisticsProjection p;
    p.runtime_team=0;p.league=0;p.league_row=0;p.table.league=0;p.table.size=18;
    p.original_team_name="TEST";p.original_league_name="TESTLIGA";
    p.source_badge_fallback=true;p.gradient_text=true;
    p.active_manager_results.fill(0xff);p.source_averages={11,22,33,44,55};
    p.average_strength={50,60,70,80,65};return p;
}
}
int main(int argc,char**argv){try{
    if(argc<2||std::string(argv[1]).empty()){std::cout<<"SKIP: BMH_DATA unavailable\n";return 77;}
    std::filesystem::path data=argv[1];auto exe=bmh::test::load_executable(asset_path(data,"BMH.exe"));
    auto texts=load_textd(asset_path(data,"TEXTD.BLK"));auto font=load_font(asset_path(data/"F","BMH3.FNT"));
    auto textd=[&](uint32_t address){return texts[(address-::bmh::layout::TextPointerTable)/4];};
    auto show=[&](const TeamStatisticsProjection& p){return TeamStatisticsScreen(data,p,Image(320,240,0x52)).render();};
    for(unsigned a:{::bmh::layout::TeamStatistics_Render_RuntimeField3Unsigned,::bmh::layout::TeamStatistics_Render_TextPointer5Unsigned,::bmh::layout::TeamStatistics_Render_TextPointer6Unsigned,::bmh::layout::TeamStatistics_Render_TextPointer7Unsigned,::bmh::layout::TeamStatistics_Render_TextPointer8Unsigned,::bmh::layout::TeamStatistics_Render_TextPointer10Unsigned,::bmh::layout::TeamStatistics_Render_TextPointerUnsigned,::bmh::layout::TeamStatistics_Render_TextPointer22Unsigned,::bmh::layout::TeamStatistics_Render_TextPointer18Unsigned,::bmh::layout::TeamStatistics_Render_TextPointer19Unsigned,::bmh::layout::TeamStatistics_Render_TextPointer20Unsigned})
        check(exe.u32(a)==0&&!textd(a).empty(),"TEXTD replaces EXE zero pointer "+std::to_string(a));
    check(textd(::bmh::layout::TeamStatistics_Render_TextPointer5)==bmh::test::expected_text("team_statistics_source_oracle_tests-external-2").c_str()&&textd(::bmh::layout::TeamStatistics_Render_TextPointer7)==bmh::test::expected_text("team_statistics_source_oracle_tests-external-1").c_str(),"Original TEXTD words, not EXE pointer labels");
    auto p=fixture();p.mode=TeamStatisticsMode::home;
    auto actual=show(p),expected=pattern(exe);
    letters(expected,font,exe,6,62,textd(::bmh::layout::TeamStatistics_Render_TextPointer5),0x6b);
    letters(expected,font,exe,80,62,"%0"+textd(::bmh::layout::TeamStatistics_Render_TextPointer6)+"%0"+textd(::bmh::layout::TeamStatistics_Render_TextPointer8)+"%0"+textd(::bmh::layout::TeamStatistics_Render_TextPointer9),0x6c);
    letters(expected,font,exe,6,70,textd(::bmh::layout::TeamStatistics_Render_TextPointer11)+textd(::bmh::layout::TeamStatistics_Render_RuntimeField3),0x6b);
    letters(expected,font,exe,80,70,"%0.0:%0.0",0x6c);
    region(actual,expected,6,57,204,14,"zero-game Source labels, decimal zeroes, TEXTD mode suffix");
    letters(expected,font,exe,314-font.text_width("TESTLIGA"),48,"TESTLIGA",0x6c);
    region(actual,expected,200,43,115,6,"zero-game league header omits rank and separator");
    check(actual.at(315,50)==0x69&&actual.at(316,50)!=0x69,"Source horizontal rule stops at315");
    p.table.rank_word[0]=uint16_t(7*2048+2);p.table.wins[0]=1;p.table.defeats[0]=1;
    p.table.goals_for[0]=5;p.table.goals_against[0]=1;
    actual=show(p);expected=pattern(exe);
    letters(expected,font,exe,6,70,textd(::bmh::layout::TeamStatistics_Render_TextPointer11)+textd(::bmh::layout::TeamStatistics_Render_RuntimeField3),0x6b);
    letters(expected,font,exe,80,70,"%2.5:%0.5",0x6c);
    region(actual,expected,6,65,204,6,"format field truncates goals times10/games and uses literal period/colon");
    p.mode=TeamStatisticsMode::combined;p.has_team_record=true;
    p.club_name_pool={'O','P','P',0};
    // Asymmetric scores require away-nibble reversal, sign inversion and
    // opposite home/away selections. Goal-only low-nibble fallback also tested.
    p.team_record[0x15]=0x41;p.team_record[0x19]=0x15;
    p.team_record[0x16]=0x14;p.team_record[0x1a]=0x31;
    p.team_record[0x17]=0x20;p.team_record[0x1b]=0x03;
    p.team_record[0x18]=0x03;p.team_record[0x1c]=0x20;
    actual=show(p);expected=pattern(exe);
    const std::array<std::string,4> values{"5:1"+textd(::bmh::layout::TeamStatistics_Render_TextPointer25)+"OPP"+textd(::bmh::layout::TeamStatistics_Render_TextPointer16),
        "1:4"+textd(::bmh::layout::TeamStatistics_Render_TextPointer25)+"OPP"+textd(::bmh::layout::TeamStatistics_Render_TextPointer15),
        "3"+textd(::bmh::layout::TeamStatistics_Render_TextPointer25)+"OPP"+textd(::bmh::layout::TeamStatistics_Render_TextPointer16),
        "3"+textd(::bmh::layout::TeamStatistics_Render_TextPointer25)+"OPP"+textd(::bmh::layout::TeamStatistics_Render_TextPointer15)};
    for(int n=0;n<4;++n){uint8_t color=exe.u8(::bmh::layout::TeamStatistics_Render_RuntimeField2+(n%2));
        letters(expected,font,exe,6,91+n*8,textd(::bmh::layout::FixtureTeamStatisticsColumnLabels+n*4),color);
        letters(expected,font,exe,84,91+n*8,values[size_t(n)],color);
        region(actual,expected,6,86+n*8,204,6,"Source mode4 extrema row "+std::to_string(n));
    }
    p.team_record[exe.u8(::bmh::layout::TeamStatistics_Render_RuntimeField+2)]=3;
    actual=show(p);expected=pattern(exe);
    letters(expected,font,exe,6,137,"3"+textd(::bmh::layout::TeamStatistics_Render_TextPointer13)+textd(::bmh::layout::TeamStatistics_Render_TextPointer3+exe.u8(::bmh::layout::TeamStatistics_Render_RuntimeField+2)*4)+textd(::bmh::layout::TeamStatistics_Render_TextPointer4),0x5f);
    region(actual,expected,6,132,204,6,"Source recent-record prefix suffix and side/metric tokens");
    // Away-only emits exactly LOW:HIGH and omits combined H/A qualifier.
    p.mode=TeamStatisticsMode::away;actual=show(p);expected=pattern(exe);
    letters(expected,font,exe,6,91,textd(::bmh::layout::FixtureTeamStatisticsFirstLabel),exe.u8(::bmh::layout::TeamStatistics_Render_RuntimeField2));
    letters(expected,font,exe,84,91,"5:1"+textd(::bmh::layout::TeamStatistics_Render_TextPointer25)+"OPP",exe.u8(::bmh::layout::TeamStatistics_Render_RuntimeField2));
    region(actual,expected,6,86,204,6,"Source away-only extrema score orientation without qualifier");
    // First-eleven owned averages fixture yields exact heights1/2/17/19.
    p=fixture();actual=show(p);expected=pattern(exe);
    auto bars=load_vga(asset_path(data/"KB","30.VGA"));
    const std::array<int,4> heights{1,2,17,19};
    for(int n=0;n<4;++n){int x=220+23*n,h=heights[size_t(n)];
        auto layer=[&](int sy,int dy,int rows){for(int yy=0;yy<rows;++yy)for(int xx=0;xx<21;++xx){
            uint8_t c=bars.pixels[size_t(sy+yy)*bars.width+xx];if(c)expected.at(x+xx,dy+yy)=c;}};
        layer(27,130,5);layer(5,130-h,h);layer(0,127-h,5);
        region(actual,expected,x,105,21,30,"Source KB30 base/fill/cap, key0, height "+std::to_string(h));
    }
    p=fixture();p.source_has_head_to_head=true;p.has_active_head_to_head=true;
    p.active_manager_team_name="OWN";
    for(int i=0;i<3;++i){p.active_manager_results[size_t(i+5)]=0x30;p.active_manager_results[size_t(i)]=0x03;}
    auto faces=load_vga(asset_path(data/"KB","27.VGA"));
    for(int mode=0;mode<3;++mode){p.mode=TeamStatisticsMode(mode);actual=show(p);
        const int index=mode==0?3:mode==1?1:2;
        atlas_region(actual,faces,index*38,0,222,55,38,38,"Source mode-gated H2H face "+std::to_string(mode));
    }
    expected=pattern(exe);letters(expected,font,exe,6,176,textd(::bmh::layout::TeamStatistics_Render_TextPointer18)+"3:0,3:0,3:0"+textd(::bmh::layout::TeamStatistics_Render_TextPointer20)+"0:3,0:3,0:3",0x6c);
    region(actual,expected,6,171,204,6,"Source packed H2H tokens/series orientation");
    p.active_manager_results.fill(0xff);p.active_manager_results[6]=0x30;
    actual=show(p);expected=pattern(exe);
    letters(expected,font,exe,6,176,textd(::bmh::layout::TeamStatistics_Render_TextPointer18)+",3:0"+textd(::bmh::layout::TeamStatistics_Render_TextPointer20)+textd(::bmh::layout::TeamStatistics_Render_TextPointer19),0x6c);
    region(actual,expected,6,171,204,6,"Source H2H physical gap keeps leading comma, missing away uses noch keine");
    p=fixture();p.flag_index=13;actual=show(p);
    atlas_region(actual,load_vga(asset_path(data/"KB","24.VGA")),9,6,5,42,9,6,"Source country flag has9px horizontal stride");
    p.source_has_position_history=true;p.league_rounds=3;
    const uint16_t packed=uint16_t(0x8000|31|(18<<5)|(0<<10));
    p.position_history[0]=uint8_t(packed);p.position_history[1]=uint8_t(packed>>8);
    actual=show(p);
    const std::array<int,3> ranks{31,18,0};
    for(int n=0;n<3;++n){int rank=ranks[size_t(n)],kind=1;
        if(rank<int(exe.u8(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField))+int(exe.u8(::bmh::layout::DeskDrawers_Curve_RuntimeField))||rank==0)kind=0;
        if(int(exe.u8(::bmh::layout::LeagueSizes))-int(exe.u8(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField2))<=rank)++kind;
        int start=uint8_t(193-2*(int(exe.u8(::bmh::layout::LeagueSizes))-rank));
        for(int stripe=0;stripe<2;++stripe){bool equal=true;
            for(int y=std::min(start,191);y<=std::max(start,191);++y)
                equal&=actual.at(226+n*2+stripe,y)==exe.u8(::bmh::layout::DeskDrawers_Curve_ExecutableField +kind*8+stripe*4);
            check(equal,"Source signed packed history rank "+std::to_string(rank)+" unbounded panel line");
        }
    }
    TeamStatisticsScreen input(data,p,Image(320,240,0x52));
    check(bool(input.button_at(167,195)),"format field inclusive bottom y195");
    check(input.release(180,190).kind==TeamStatisticsRequestKind::none,"unpaired release inert");
    input.press(180,190);input.move(220,180);
    check(input.release(180,190).kind==TeamStatisticsRequestKind::none,"moving out cancels press");
    if(argc>2){
        auto file=load_savegame(argv[2]);
        DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
        auto game=GameState::from_save(SaveState(file.stream,counts),counts);
        competition_pointers(game.memory,exe);game.texts=&texts;national_coach_after_load(game);
        const auto before=save_stream(game);
        auto owned=team_statistics_from_game(game,0,TeamStatisticsMode::combined,exe,texts);
        const auto range=squad_range(0);std::array<unsigned,5> sums{},amounts{};
        for(unsigned row=0;row<11;++row){unsigned id=game.squad_slots[range.first+row]&0xfff;if(id==0xfff)continue;
            const auto& player=game.players[id];unsigned raw=player.w35c;
            unsigned strength=(((raw&127)>>3)+94)*(((raw>>7)&127)+((raw>>14)&127))/100;
            strength=std::min(strength/2,99u);unsigned position=player.w364&3;
            sums[position]+=strength;sums[4]+=strength;++amounts[position];++amounts[4];
        }
        for(unsigned i=0;i<5;++i)check(owned.average_strength[i]==(amounts[i]?sums[i]/amounts[i]:0),
            "native builder first11 category average "+std::to_string(i));
        check(save_stream(game)==before,"native projection builder leaves all serialized state unchanged");
        auto foreign=team_statistics_from_game(game,104,TeamStatisticsMode::combined,exe,texts);
        check(foreign.flag_index==uint8_t(6-exe.u8(::bmh::layout::LeaguePairings_LeaguePairingsConstants_RuntimeField)+1),"format field league6 foreign country identity");
        game.manager_teams[0]=357;game.memory.set8(::bmh::layout::ActiveManager,0);game.memory.set8(::bmh::layout::ManagerCount,1);
        owned=team_statistics_from_game(game,0,TeamStatisticsMode::combined,exe,texts);
        check(!owned.source_has_head_to_head,"national active manager suppresses ordinary-team H2H");
        game.club_table.clear();owned=team_statistics_from_game(game,0,TeamStatisticsMode::combined,exe,texts);
        check(!owned.has_team_record,"absent format field owns no fabricated record panel");
    }
    std::cout<<assertions<<" Source oracle assertions, "<<failures<<" failures\n";return failures?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
