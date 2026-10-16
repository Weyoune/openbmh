#include "../../tests/test_inputs.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/opportunities.hpp"
#include "ui/manager.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace bmh;
using namespace bmh::ui;

namespace {
void check(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
OpportunitiesState fixture(){
    OpportunitiesState s;s.identity={0,45,17,196,8,12,0};s.mode=1;s.league_size=18;
    s.current_rank=3;s.ranks.fill(1);s.ranks[0]=0;s.ranks[2]=0xff;s.underlay=Image(320,240);return s;
}
void select(OpportunitiesScreen& s,int index,int mode){
    const int x=0x7c+20,y=0x6c+index*11+4;
    check(s.pointer_down(x,y),"mode button down should arm");
    check(s.release(x,y),"matching mode up should be consumed");
    check(s.pending()&&s.pending()->action==OpportunitiesAction::select_mode&&s.pending()->mode==mode,
          "the four Original controls carry modes 1/2/4/8");
}
}
int main(int argc,char** argv){
    try{
        if(argc!=2)throw std::runtime_error("usage: ui_opportunities_tests <BMH data>");
        const std::filesystem::path data(argv[1]);
        OpportunitiesScreen screen(data,fixture());
        auto frame=screen.render();check(frame.width==320&&frame.height==240,"Original logical screen size");
        const auto exe=load_le(asset_path(data,"BMH.exe"));
        auto ladder=load_vga(asset_path(data/"KB","37.VGA"));
        check(frame.at(97,41)==ladder.at(0,0),"Original ladder is actually blitted at97,41");
        check(frame.at(102,71)==exe.u8(::bmh::layout::Opportunities_Render_RuntimeField2)&&frame.at(112,72)==exe.u8(::bmh::layout::Opportunities_Render_RuntimeField2),
              "rank strip has inclusive11x2 Original bounds and unreachable color");
        const auto panel=load_vga(asset_path(data/"KB","56.VGA"));
        const auto adjacent=ladder.pixels[size_t(31)*ladder.width+16];
        check(frame.at(113,72)==(adjacent==0xff?panel.pixels[size_t(52)*panel.width+112]:adjacent),
              "rank strip stops at112 and does not overwrite the legend/window body");
        auto flat_state=fixture();flat_state.gradient_text=false;
        OpportunitiesScreen flat_screen(data,flat_state);auto flat_frame=flat_screen.render();
        const auto fnt=load_font(asset_path(data/"F","BMH3.FNT"));
        const auto texts=load_textd(asset_path(data,"TEXTD.BLK"));
        const auto label=texts[(::bmh::layout::Opportunities_Render_TextPointer-::bmh::layout::TextPointerTable)/4];
        Image legend_mask(fnt.text_width(label),fnt.height);fnt.draw(legend_mask,0,0,label,1);
        int legend_ink=0;
        for(int y=0;y<fnt.height;++y)for(int x=0;x<legend_mask.width;++x)if(legend_mask.at(x,y)){
            check(flat_frame.at(123+x,86-fnt.baseline+y)==exe.u8(::bmh::layout::Opportunities_Render_RuntimeField3),
                  "legend uses FNT3 and its own Original palette entry");++legend_ink;
        }
        check(legend_ink>0,"legend has actual source glyphs");
        check(frame.at(102,79)==exe.u8(::bmh::layout::Opportunities_Render_RuntimeField)&&frame.at(112,80)==exe.u8(::bmh::layout::Opportunities_Render_RuntimeField),
              "secured rank uses signed-minus-one palette lookup");
        check(frame.at(89,27)==0x46&&frame.at(240,180)==0x40,
              "Original Chancen window frame uses EXE bounds");
        auto missing=fixture();missing.current_rank=0;
        OpportunitiesScreen no_rank(data,missing);
        check(!no_rank.pointer_down(144,112)&&!no_rank.release(144,112)&&!no_rank.pending(),
              "rank zero has no Original A-D controls");
        const auto missing_frame=no_rank.render();
        check(missing_frame.pixels!=frame.pixels,"rank-zero message replaces normal ladder/button body");
        check(no_rank.pointer_down(1,1,true)&&no_rank.pending()->action==OpportunitiesAction::close,
              "rank-zero page retains Original right-close");
        DeskRequest menu_owner;menu_owner.kind=DeskRequestKind::open_menu;menu_owner.manager_slot=0;
        menu_owner.menu_kind=23;menu_owner.revision=7;
        StatisticsMenuScreen statistics(data,StatisticsMenuState{menu_owner,Image(320,240)});
        auto parent_frame=statistics.render();
        check(parent_frame.width==320,"menu23 presents Original nine-cell selector");
        auto buttons=load_vga(asset_path(data/"KB","0.VGA"));
        check(parent_frame.at(3,203)==buttons.at(24,74),"parent cell actually uses Original KB0 art");
        check(statistics.render_page(7).at(18,24)==0x46&&statistics.render_page(7).at(299,190)==0x40,
              "Source title-history parentpane uses index7 EXE bounds");
        auto alltime_header=statistics.render_page(3);
        const int alltime_top=exe.u8(::bmh::layout::Opportunities_FormatError_RuntimeField3+3);
        const int alltime_left=exe.u16(::bmh::layout::Opportunities_FormatError_RuntimeField + 3*2),alltime_right=exe.u16(::bmh::layout::Opportunities_FormatError_RuntimeField2 + 3*2);
        auto expected_header=statistics.render();
        // Reconstruct only legacy routine's header strip from Original assets and its
        // two distinct font calls; compare all pixels, including glyph gaps.
        for(int y=alltime_top;y<=alltime_top+12;++y)
            for(int x=alltime_left;x<=alltime_right;++x){
                if(exe.u8(::bmh::layout::SolidPanelBackground)!=0)expected_header.at(x,y)=0x42;
                else if(x>alltime_left&&x<alltime_right&&y>alltime_top){
                    const auto c=panel.pixels[size_t(y-20)*panel.width+size_t(x-1)];
                    if(c!=0xc0)expected_header.at(x,y)=c;
                }
                if(x==alltime_left||y==alltime_top)expected_header.at(x,y)=0x46;
                if(x==alltime_right)expected_header.at(x,y)=0x40;
            }
        const auto header_font=load_font(asset_path(data/"F","BMH2.FNT"));
        header_font.draw(expected_header,alltime_left+5,alltime_top+10-header_font.baseline,
                         texts[(::bmh::layout::Opportunities_FormatError_TextPointer+3*4-::bmh::layout::TextPointerTable)/4],0x6a);
        fnt.draw(expected_header,107,alltime_top+10-fnt.baseline,texts[(::bmh::layout::Opportunities_FormatError_TextPointer2-::bmh::layout::TextPointerTable)/4],0x6a);
        for(int x=alltime_left+3;x<=alltime_right-4;++x)
            expected_header.at(x,alltime_top+11)=0x6a;
        auto expected_flat=expected_header;
        const auto& detail=texts[(::bmh::layout::Opportunities_FormatError_TextPointer2-::bmh::layout::TextPointerTable)/4];
        Image detail_mask(fnt.text_width(detail),6);
        fnt.draw(detail_mask,0,5-fnt.baseline,detail,1);
        int ramp=0;while(ramp<32&&exe.u8(::bmh::layout::Advertising_Text_RuntimeField2+uint32_t(ramp))!=0x6a)++ramp;
        int gradient_ink=0;
        if(exe.u8(::bmh::layout::TextGradientMode)==2)
            for(int y=0;y<6;++y)for(int x=0;x<detail_mask.width;++x)
                if(detail_mask.at(x,y)){
                    const auto c=exe.u8(::bmh::layout::Advertising_Text_RuntimeField+uint32_t(ramp*6+y));
                    expected_header.at(107+x,alltime_top+5+y)=c;
                    if(c!=0x6a)++gradient_ink;
                }
        for(int y=alltime_top;y<=alltime_top+12;++y)
            for(int x=alltime_left;x<=alltime_right;++x)
                check(alltime_header.at(x,y)==expected_header.at(x,y),
                      "Alltime header uses flat font 2, gradient font 3 and the complete Source separator");
        StatisticsMenuScreen flat_statistics(data,StatisticsMenuState{menu_owner,Image(320,240),0,false});
        auto flat_header=flat_statistics.render_page(3);
        for(int y=alltime_top;y<=alltime_top+12;++y)
            for(int x=alltime_left;x<=alltime_right;++x)
                check(flat_header.at(x,y)==expected_flat.at(x,y),
                      "desktop gradient-text option off retains plain font-3 heading colors");
        check(exe.u8(::bmh::layout::TextGradientMode)!=2||gradient_ink>0,
              "Source palette ramp changes actual heading ink, not only empty pixels");
        check(statistics.pointer_down(99,213)&&statistics.release(99,213)&&statistics.pending()->selected_index==0,
              "Source button raised-height and inclusive end are clickable");
        statistics.clear_pending();
        check(statistics.pointer_down(120,232)&&statistics.release(120,232)&&statistics.pending()->selected_index==7,
              "index7 remains an original typed child route");
        statistics.clear_pending();
        const auto old_parent=statistics.render();auto newer_owner=menu_owner;++newer_owner.revision;
        statistics.update_owner(newer_owner);
        check(statistics.render().pixels==old_parent.pixels&&statistics.state().owner==newer_owner,
              "parent owner refresh changes no rendered frame");
        auto wrong_owner=newer_owner;wrong_owner.manager_slot=1;bool wrong_owner_rejected=false;
        try{statistics.update_owner(wrong_owner);}catch(const FormatError&){wrong_owner_rejected=true;}
        check(wrong_owner_rejected&&statistics.state().owner==newer_owner,"owner refresh cannot change manager");
        menu_owner=newer_owner;
        check(statistics.pointer_down(12,207),"first statistics cell owns its physical DOWN");
        const auto held_statistics=statistics.state_json();
        check(!statistics.pointer_down(0,0,true)&&statistics.state_json()==held_statistics,
              "event8 cannot replace the selector's physical release owner");
        check(statistics.release(12,207),"first statistics cell accepts matched down/up");
        check(statistics.pending()&&statistics.pending()->owner==menu_owner&&statistics.pending()->selected_index==0&&
              !statistics.pending()->close,"Original entry zero is the typed route into Möglichkeiten");
        statistics.clear_pending();
        check(statistics.pointer_down(120,207)&&statistics.release(120,207),"other original entries remain selectable");
        check(statistics.pending()&&statistics.pending()->selected_index==1&&statistics.pending()->owner==menu_owner,
              "implemented transfer history carries its original menu owner");
        statistics.clear_pending();
        check(statistics.pointer_down(12,219)&&statistics.release(12,219),"global page 3 remains selectable");
        check(statistics.pending()&&statistics.pending()->selected_index==3&&statistics.pending()->owner==menu_owner,
              "page 3 keeps the original typed menu owner");
        statistics.clear_pending();
        check(statistics.pointer_down(120,219)&&statistics.release(120,219)&&statistics.pending()->selected_index==4,
              "page 4 remains a typed menu route");
        statistics.clear_pending();
        check(statistics.pointer_down(228,219)&&statistics.release(228,219)&&statistics.pending()->selected_index==5,
              "page 5 remains a typed menu route");
        statistics.clear_pending();
        check(statistics.pointer_down(12,231)&&statistics.release(12,231)&&statistics.pending()->selected_index==6,
              "page 6 remains a typed menu route");
        statistics.clear_pending();
        bool invalid_page_rejected=false;
        try { (void)statistics.render_page(9); } catch(const FormatError&) { invalid_page_rejected=true; }
        check(invalid_page_rejected,"out-of-range page index is still rejected");
        check(statistics.pointer_down(120,207,true)&&statistics.pending()&&statistics.pending()->close&&
              statistics.pending()->owner==menu_owner,"selector close preserves the menu-23 owner token");
        for(const auto& [index,mode]:{std::pair{1,2},std::pair{2,4},std::pair{3,8},std::pair{0,1}}){
            select(screen,index,mode);
            const auto before=screen.state();
            auto accepted=before;accepted.mode=mode;++accepted.identity.sequence;++accepted.identity.revision;
            screen.acknowledge(std::move(accepted));
            check(screen.state().mode==mode&&!screen.pending(),"mode is applied only on accepted ACK");
        }
        select(screen,1,2);
        auto wrong_scope=screen.state();wrong_scope.mode=2;++wrong_scope.identity.revision;
        ++wrong_scope.identity.sequence;++wrong_scope.identity.scope;
        bool scope_rejected=false;try{screen.acknowledge(wrong_scope);}catch(const FormatError&){scope_rejected=true;}
        check(scope_rejected&&screen.pending(),"ACK cannot replace original scope owner");
        screen.reject(bmh::test::expected_text("opportunities_tests-external-1").c_str());
        check(screen.pointer_down(233,129)&&screen.release(233,129)&&screen.pending()->mode==2,
              "A-D includes Original right/bottom descriptor endpoint");
        screen.reject(bmh::test::expected_text("opportunities_tests-external-1").c_str());
        check(screen.pointer_down(120,160,true)&&screen.pending()&&screen.pending()->action==OpportunitiesAction::close,
              "Original right-button close is typed");
        std::cout<<"Original statistics renderer asset/bounds/rank-zero/owner checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
