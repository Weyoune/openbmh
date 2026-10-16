// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/game_end_alternate.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace bmh;
using namespace bmh::ui;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
size_t colored(const Image& image, uint8_t color) {
    return size_t(std::count(image.pixels.begin(),image.pixels.end(),color));
}
void press(GameEndAlternateScoreScreen& screen, const LeImage& exe, unsigned index) {
    const int x=screen.window_x()+exe.u16(::bmh::layout::Advertising_Click_RuntimeField+2*index)+1;
    const int y=screen.window_y()+exe.u8(::bmh::layout::Advertising_Click_RuntimeField2+index)+1;
    check(screen.button_at(x,y)==index,"Source button coordinates");
    check(screen.click(x,y),"Source mouse button accepted");
}
}

int main(int argc, char** argv) {
    try {
        if (argc!=2) throw std::runtime_error("usage: game_end_alternate_tests <BMH data>");
        if (!*argv[1]) { std::cout << "Original asset test skipped: BMH_DATA not configured\n"; return 77; }
        const auto assets=load_game_end_alternate_score_assets(argv[1]);
        // Configured Original signature0 texts are three deliberate blank strings.
        check(assets.texts==std::array<std::string,3>{" "," "," "},"TEXTD890/891/892 blank Original strings");
        const Image backdrop(320,240,3);
        auto make=[&] { return GameEndAlternateScoreScreen(assets,backdrop); };
        auto screen=make();
        check(screen.value()==200 && screen.window_x()==100 && screen.window_y()==100,
              "legacy routine original calculator call arguments");
        check(screen.phase()==AlternateScorePhase::Calculator && !screen.accepted_value(),"calculator phase");
        const auto frame=screen.render();
        check(frame.pixels[100*320+100]==assets.calculator.pixels[0],"KB6 top-left at100/100");
        check(frame.pixels[20*320+20]==3,"preserve owned backdrop outside calculator");
        check(frame.pixels[236*320+102]==0x40 && frame.pixels[237*320+214]==0x40,
              "legacy routine inclusive two-pixel shadow");
        check(frame.pixels[238*320+214]==3,"shadow ends at237");
        // Boundary differs from the old AdvertisingScreen half-open hitboxes.
        for (unsigned i: {0u,9u,10u,14u,15u,22u}) {
            const int x=100+assets.exe.u16(::bmh::layout::Advertising_Click_RuntimeField+2*i),y=100+assets.exe.u8(::bmh::layout::Advertising_Click_RuntimeField2+i);
            check(screen.button_at(x+(i<15?16:53),y+14)==i,"inclusive Source key/preset bottom-right");
        }
        check(!screen.button_at(99,99),"outside calculator ignored");
        check(!screen.key("R") && !screen.key("J") && !screen.key("N"),"mouse legends are not keyboard shortcuts");
        check(screen.key("BACKSPACE") && screen.value()==20,"keyboard Backspace");
        press(screen,assets.exe,11);check(screen.value()==200,"mouse reset200");
        check(screen.key("SPACE") && screen.value()==0,"keyboard Space clears");
        check(screen.text("99999") && screen.value()==3536,"Source maximum clamps each entry");
        for (unsigned i=0; i<8; ++i) {
            press(screen,assets.exe,15+i);
            check(screen.value()==GameEndAlternateScoreScreen::presets[i],"Source eight numeric presets");
        }
        check(screen.key("SPACE") && screen.text("0000000000") && screen.text("1") && screen.value()==0,
              "Source ten leading zero digit limit");
        check(screen.key("BACKSPACE") && screen.text("1") && screen.value()==1,"Backspace releases digit limit");
        check(screen.mouse_down(105,105) && screen.mouse_move(500,500) && screen.mouse_up(),"Source title drag");
        check(screen.window_x()==207 && screen.window_y()==104,"drag clamps x207/y104");
        const auto edge=screen.render();
        check(edge.pixels[239*320+319]==assets.calculator.pixels[135*assets.calculator.width+112],
              "at x207/y104 inverted offscreen shadow rectangles are rejected by format field");
        check(screen.mouse_down(212,109) && screen.mouse_move(-50,-50) && screen.mouse_up(),"drag to opposite edge");
        check(screen.window_x()==0 && screen.window_y()==0,"drag clamps zero");
        check(!screen.key("ESC") && screen.cancel() && screen.phase()==AlternateScorePhase::Cancelled && !screen.accepted_value(),"cancel returns no value");
        check(screen.render().pixels==backdrop.pixels,"cancel restores original owned backdrop");
        std::cout<<"KB6 assets/coordinates/keyboard/presets/inclusive hitboxes/drag/cancel OK\n";

        auto points=make();
        check(points.key("ENTER") && points.accepted_value()==200 && points.phase()==AlternateScorePhase::Points,
              "Enter accepts default200 without a score calculation");
        check(colored(points.render(),0)==320*240,"accept clears full display before point1");
        check(!points.finish_points() && !points.click(10,10) && !points.key("ESC"),"drawing cannot skip to ACK");
        check(points.advance_point(),"first Source point");
        auto raster=points.render();
        check(colored(raster,0x27)==1 && raster.pixels[4*320+10]==0x27 && raster.pixels[4*320+4]==0,
              "first point is10/4, not4/4");
        for (unsigned i=1; i<52; ++i) check(points.advance_point(),"first row steps");
        raster=points.render();
        check(raster.pixels[4*320+316]==0x27 && raster.pixels[7*320+10]==0,"52nd point316/4");
        check(points.advance_point() && points.render().pixels[7*320+10]==0x27,"53rd point wraps to10/7");
        while (points.advance_point()) {}
        raster=points.render();
        check(points.points_drawn()==200 && colored(raster,0x27)==200 && raster.pixels[13*320+268]==0x27 &&
              raster.pixels[13*320+274]==0,"200 points: three full rows and44 points at y13");
        check(points.phase()==AlternateScorePhase::Points && points.finish_points() &&
              points.phase()==AlternateScorePhase::AwaitAck,"footer is a separate Source presentation");
        check(points.render().pixels==raster.pixels,"blank TEXTD891/892 preserve points raster");
        check(!points.key("ENTER") && !points.key("A"),"legacy routine ACK ignores physical keyboard");
        check(points.click(319,239) && points.phase()==AlternateScorePhase::Closed,"mouse ACK closes");
        check(!points.advance_point() && !points.finish_points(),"closed screen inert");

        auto zero=make();press(zero,assets.exe,21);check(zero.key("ENTER"),"zero accepts");
        check(!zero.advance_point() && zero.finish_points() && colored(zero.render(),0)==320*240,
              "zero still presents blank footer/ACK");
        check(!zero.key("ESC") && zero.acknowledge() && zero.phase()==AlternateScorePhase::Closed,"mouse cancellation ACK closes");

        auto maximum=make();press(maximum,assets.exe,22);check(maximum.key("ENTER"),"maximum accepts");
        unsigned presents=0;while (maximum.advance_point()) ++presents;
        raster=maximum.render();
        check(presents==3536 && colored(raster,0x27)==3536 && raster.pixels[205*320+316]==0x27,
              "3536 individual presentations fill52 columns x68 rows ending316/205");
        check(std::all_of(raster.pixels.begin()+206*320,raster.pixels.end(),[](uint8_t p){return p==0;}),
              "point raster leaves footer area clear");
        check(maximum.finish_points(),"maximum footer presentation");
        // Original texts are blank in this edition; exercise the exact Source
        // footer coordinates with owned test strings, not replacement assets.
        auto labeled_assets=assets;
        labeled_assets.texts[1]="A";labeled_assets.texts[2]="B";
        GameEndAlternateScoreScreen labeled(labeled_assets,backdrop,false);
        press(labeled,assets.exe,21);check(labeled.key("ENTER") && labeled.finish_points(),"test footer phase");
        Image footer(320,240);
        assets.font.draw(footer,159-assets.font.text_width("A")/2,230-assets.font.baseline,"A",0x5f);
        assets.font.draw(footer,159-assets.font.text_width("B")/2,237-assets.font.baseline,"B",0x5f);
        check(labeled.render().pixels==footer.pixels,"TEXTD891/892 center159 baseline230/237 color95");
        std::cout<<"Source point prefixes0/1/52/53/200/3536, separate footer and ACK OK\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
