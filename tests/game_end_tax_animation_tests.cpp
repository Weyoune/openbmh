// SPDX-License-Identifier: GPL-3.0-only
#include "test_inputs.hpp"
#include "ui/game_end_tax_animation.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace bmh;
using namespace bmh::ui;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void rejected(const std::function<void()>& action) {
    try { action(); } catch (const FormatError&) { return; }
    throw std::runtime_error("expected malformed animation rejection");
}
// Independent native-script expectations are supplied externally; no game-script bytes are embedded.
void original_copy(Image& out, const Image& source, unsigned sx, unsigned sy,
                   unsigned dx, unsigned dy, unsigned width, unsigned height) {
    for (unsigned row=0; row<height; ++row)
        std::copy_n(source.pixels.begin()+size_t(sy+row)*source.width+sx,width,
                    out.pixels.begin()+size_t(dy+row)*out.width+dx);
}
Image original_frame(const Image& backdrop, const Image& kb56, bool flat) {
    auto out=backdrop;
    if (flat)
        for (int row=71; row<=168; ++row) std::fill_n(out.pixels.begin()+row*320+93,133,0x42);
    else original_copy(out,kb56,93,52,94,72,131,96);
    for (int row=71; row<=168; ++row) { out.at(93,row)=0x46;out.at(225,row)=0x40; }
    for (int col=93; col<225; ++col) { out.at(col,71)=0x46;out.at(col,168)=0x40; }
    out.at(93,168)=0x40;
    return out;
}
}

int main(int argc, char** argv) {
    try {
        if (argc!=2) throw std::runtime_error("usage: game_end_tax_animation_tests <BMH data>");
        if (!*argv[1]) { std::cout << "Original asset test skipped: BMH_DATA not configured\n"; return 77; }
        const auto assets=load_game_end_tax_animation_assets(argv[1]);
        check(assets.art.width==248 && assets.art.height==69 && assets.background.width==318 &&
              assets.background.height==207,"configured Original KB17/56 dimensions");
        const auto script = read_file(bmh::test::external_input("tax-animation-script.bin"));
        check(script.size() == 33, "external animation expectation has four complete records");
        const auto decoded=decode_tax_animation_script(script);
        std::array<TaxAnimationCopy,4> rectangles;
        std::array<unsigned,4> ticks, offsets;
        std::array<bool,4> copy_after_present;
        for (size_t i = 0; i < 4; ++i) {
            const auto at = i * 8;
            rectangles[i] = {script[at+1],script[at+2],script[at+3],script[at+4],script[at+5],script[at+6]};
            ticks[i] = script[at+7] > 100 ? script[at+7]-100 : script[at+7];
            offsets[i] = unsigned(at+7);
            copy_after_present[i] = script[at+7] <= 100;
        }
        check(assets.steps.size()==4 && decoded.size()==4,"four wait markers define four variants");
        for (unsigned i=0; i<4; ++i) {
            check(assets.steps[i].copies==std::vector<TaxAnimationCopy>{rectangles[i]} &&
                  assets.steps[i].wait_ticks==ticks[i] && assets.steps[i].script_offset==offsets[i] &&
                  assets.steps[i].copy_after_present==copy_after_present[i],"exact EXE record/wait/presentation sequence");
            check(decoded[i].copies==assets.steps[i].copies && decoded[i].wait_ticks==ticks[i] &&
                  decoded[i].copy_after_present==copy_after_present[i],"fixed-size script decoding");
        }
        constexpr std::array<uint8_t,9> field255{0,255,0,1,2,3,4,101,255};
        const auto field=decode_tax_animation_script(field255);
        check(field.size()==1 && field[0].copies[0].source_x==255 && field[0].wait_ticks==1 &&
              !field[0].copy_after_present,"255 in rectangle is data,101 is flip-only wait1");
        constexpr std::array<uint8_t,3> boundary{100,101,255};
        const auto waits=decode_tax_animation_script(boundary);
        check(waits.size()==2 && waits[0].wait_ticks==100 && waits[0].copy_after_present &&
              waits[1].wait_ticks==1 && !waits[1].copy_after_present,"Source marker100/101 boundary");
        rejected([&]{decode_tax_animation_script(std::array<uint8_t,2>{0,1});});
        rejected([&]{decode_tax_animation_script(std::array<uint8_t,1>{40});});
        std::cout<<"EXE a5118 four rectangles/waits, fixed-length fields and100/101 boundary OK\n";

        const Image backdrop(320,240,3);
        std::array<unsigned,4> milliseconds{},totals{};
        for(size_t i=0;i<ticks.size();++i){milliseconds[i]=ticks[i]*20;totals[i]=milliseconds[i]+(i?totals[i-1]:0);}
        for (const bool flat:{false,true}) {
            std::array<Image,4> expected, expected_working;
            auto base=original_frame(backdrop,assets.background,flat);
            auto visible=backdrop,working=base;
            for(size_t i=0;i<rectangles.size();++i){
                const auto& r=rectangles[i];
                original_copy(working,assets.art,r.source_x,r.source_y,r.x,r.y,r.width,r.height);
                expected[i]=working;
                expected_working[i]=copy_after_present[i]?working:visible;
                visible=expected[i];working=expected_working[i];
            }
            for (unsigned variant=1; variant<=4; ++variant) {
                GameEndTaxAnimationScreen screen(assets,variant,backdrop,flat);
                check(screen.render().pixels==backdrop.pixels && screen.drawing_frame().pixels==base.pixels,
                      "frame preparation draws only on working buffer with no early present");
                check(screen.total_milliseconds()==totals[variant-1],"Source variant total wait");
                unsigned waited=0;
                for (unsigned step=0; step<variant; ++step) {
                    check(screen.advance_step() && screen.steps_presented()==step+1 &&
                          screen.wait_milliseconds()==milliseconds[step],"Source step wait includes final wait");
                    waited+=screen.wait_milliseconds();
                    check(screen.render().pixels==expected[step].pixels,"complete Source frame/copy sequence");
                    check(screen.drawing_frame().pixels==expected_working[step].pixels,"presentation preserves the independently interpreted drawing buffer");
                }
                check(waited==totals[variant-1] && screen.finished() && !screen.advance_step(),"bounded prefix complete");
                const auto rendered=screen.render();
                check(rendered.pixels[70*320+93]==3 && rendered.pixels[169*320+225]==3 &&
                      rendered.pixels[71*320+92]==3 && rendered.pixels[168*320+226]==3,
                      "no full-screen black clear or extra shadow");
                check(rendered.pixels[71*320+93]==0x46 && rendered.pixels[71*320+225]==0x40 &&
                      rendered.pixels[168*320+93]==0x40,"Source frame line order/colors");
            }
        }
        std::cout<<"Variants1..4 patterned/flat backgrounds, external wait durations and both buffers OK\n";

        // Exercise exact full-frame copy semantics outside the animation rectangle.
        GameEndTaxAnimationScreen different(assets,1,Image(320,240,7),backdrop,false);
        check(different.render().pixels[0]==7 && different.drawing_frame().pixels[0]==3,"distinct initial buffers");
        check(different.advance_step() && different.render().pixels[0]==3 &&
              different.drawing_frame().pixels[0]==3,"present1 copies entire visible buffer, not only animation rectangle");
        auto opaque_assets=assets;
        const auto& first=rectangles[0];
        opaque_assets.art.at(first.source_x,first.source_y)=0;opaque_assets.art.at(first.source_x+1,first.source_y)=255;
        GameEndTaxAnimationScreen opaque(opaque_assets,1,backdrop,true);
        check(opaque.advance_step() && opaque.render().pixels[first.y*320+first.x]==0 &&
              opaque.render().pixels[first.y*320+first.x+1]==255,"format field copies0 and255 opaquely");
        rejected([&]{GameEndTaxAnimationScreen invalid(assets,0,backdrop);});
        rejected([&]{GameEndTaxAnimationScreen invalid(assets,5,backdrop);});
        rejected([&]{GameEndTaxAnimationScreen invalid(assets,1,Image(319,240));});
        auto malformed=assets;malformed.steps[0].copies[0].width=255;
        rejected([&]{GameEndTaxAnimationScreen invalid(malformed,1,backdrop);});
        std::cout<<"Owned distinct buffers, opaque pixel copies and invalid-input rejection OK\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
