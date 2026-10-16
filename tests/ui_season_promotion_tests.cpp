// SPDX-License-Identifier: GPL-3.0-only
#include "ui/season_promotion.hpp"

#include <algorithm>
#include <iostream>
#include <string>

#include "formats/vga.hpp"
#include "season_promotion_fixture_support.hpp"
#include "ui/manager.hpp"

using namespace bmh;
using namespace bmh::ui;

namespace {
bool check(bool value,const char* label) {
    if(!value)std::cerr<<"FAIL: "<<label<<'\n';
    return value;
}
uint8_t pixel(const Image& image,int x,int y) {
    return image.pixels[size_t(y)*image.width+x];
}
bool matching_logo_pixels(const Image& output,int x,int y,const Image& atlas,int sx) {
    for(int yy=0;yy<38;++yy)for(int xx=0;xx<38;++xx)
        if(pixel(output,x+xx,y+yy)!=atlas.pixels[size_t(yy)*atlas.width+size_t(sx+xx)])return false;
    return true;
}
}

int main(int argc,char** argv) {
    if(argc!=3){std::cerr<<"usage: ui_season_promotion_tests <Original data> <group FBM directory>\n";return 2;}
    if(!*argv[1]) { std::cout<<"Promotion UI tests skipped: Original data not configured\n"; return 77; }
    const std::filesystem::path data=argv[1],fixtures=argv[2];
    bool ok=true;
    for(int group=0;group<4;++group) {
        promotion_test::NativeState source(data,fixtures/("group"+std::to_string(group)+".FBM"));
        const auto event=promotion_test::event_for(group);
        const auto projection=season_promotion_from_game(source.game,event,source.exe,source.texts);
        ok &= check(projection.group==group&&projection.count==event.count,"group/count projection");
        ok &= check(projection.relegated.size()==event.count&&projection.promoted.size()==event.count,
                    "projection owns only Source-visible rows");
        ok &= check(projection.league_footer==league_name(source.game,std::array<int,4>{0,1,2,4}[size_t(group)]),
                    "footer uses Source league_name mapping");
        ok &= check(projection.relegated.front().original_name==source.game.club_name(event.relegated.front()),
                    "club name is captured before swaps");
        SeasonPromotionScreen screen(data,projection);
        const Image initial=screen.render(0);
        ok &= check(initial.width==320&&initial.height==240,"320x240 original canvas");
        ok &= check(screen.render(0).pixels==initial.pixels,"render is deterministic and side-effect free");
        const Image first_scan=screen.render(1);
        const Image final_scan=screen.render(38);
        ok &= check(first_scan.pixels!=initial.pixels&&final_scan.pixels!=initial.pixels,
                    "Source scanline progress is explicitly rendered");
        ok &= check(screen.render(38).pixels==final_scan.pixels,"full 38-step history is deterministic");
    }

    // Group 1 may carry extra computed relegation rows; Source displays only
    // count entries. This matches legacy routine's `i < param5` guard.
    promotion_test::NativeState source(data,fixtures/"group1.FBM");
    auto short_event=promotion_test::event_for(1);short_event.count=2;
    const auto short_projection=season_promotion_from_game(source.game,short_event,source.exe,source.texts);
    ok &= check(short_projection.relegated.size()==2&&short_projection.promoted.size()==2,
                "event count caps longer owned arrays");

    auto eight_event=promotion_test::event_for(0);
    eight_event.relegated.resize(8,eight_event.relegated.front());
    eight_event.promoted.resize(8,eight_event.promoted.front());
    eight_event.count=8;
    const auto eight_projection=season_promotion_from_game(source.game,eight_event,source.exe,source.texts);
    ok &= check(SeasonPromotionScreen(data,eight_projection).render(38).width==320,
                "maximum Source row count clips safely to the viewport");
    auto nine_event=eight_event;
    nine_event.relegated.resize(9,nine_event.relegated.front());
    nine_event.promoted.resize(9,nine_event.promoted.front());
    nine_event.count=9;
    bool rejected_nine=false;
    try { (void)season_promotion_from_game(source.game,nine_event,source.exe,source.texts); }
    catch(const FormatError&) { rejected_nine=true; }
    ok &= check(rejected_nine,"rows beyond Source's eight-entry buffers are rejected");

    // Source's club-id gate (legacy routine) is independent of the logo selector.
    auto fallback_event=promotion_test::event_for(0);
    fallback_event.count=1;fallback_event.relegated={90};fallback_event.promoted={91};
    const auto fallback_projection=season_promotion_from_game(source.game,fallback_event,source.exe,source.texts);
    ok &= check(fallback_projection.relegated[0].badge_fallback&&fallback_projection.promoted[0].badge_fallback,
                "fallback selection follows runtime club id");

    auto logo_projection=promotion_test::event_for(0);
    auto logo_state=season_promotion_from_game(source.game,logo_projection,source.exe,source.texts);
    logo_state.gradient_text=false;
    SeasonPromotionScreen logos(data,logo_state);
    const Image logo_image=logos.render(0);
    const auto& team=logo_state.relegated.front();
    const int atlas_id=(team.badge_selector&0x1ff)>>3;
    const Image atlas=load_vga(asset_path(data/"W",std::to_string(atlas_id)+".VGA"));
    ok &= check(matching_logo_pixels(logo_image,10,23,atlas,(team.badge_selector&7)*38),
                "normal team crest copies Original W atlas opaquely");
    ok &= check(pixel(logo_image,9,22)==0x46&&pixel(logo_image,9,61)==0x40&&
                pixel(logo_image,48,22)==0x40,"badge frame Source bevel colors/coordinates");

    if(!ok)return 1;
    std::cout<<"Season-promotion native Source projection, assets, and renderer tests passed\n";
    return 0;
}
