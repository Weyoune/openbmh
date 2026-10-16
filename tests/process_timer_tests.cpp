// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/process_timer.hpp"
#include "ui/manager.hpp"
#include <iostream>
#include <stdexcept>

using namespace bmh;
using namespace bmh::ui;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
int main(int argc,char**argv) {try {
    if(argc!=2)throw std::runtime_error("data directory required");
    const auto exe=load_le(asset_path(argv[1],"BMH.exe"));
    ProcessTimer timer;
    check(!timer.sample(100),"unstarted process clock is inactive");
    for(uint8_t minutes:{uint8_t(0),uint8_t(200),uint8_t(255)}) {
        timer.start(minutes,0);check(!timer.sample(50),"Original disabled timer gates");
    }
    timer.start(1,10);
    check(timer.sample(10)==ProcessTimer::Sample{60,100,false},"one Source minute is sixty host seconds");
    check(timer.sample(30)==ProcessTimer::Sample{40,66,false},"percentage uses integer truncation");
    check(timer.sample(69)==ProcessTimer::Sample{1,1,false},"last second is still live");
    check(timer.sample(70)->expired&&timer.sample(75)->expired,"actual signed x86 gate expires at/after zero");
    timer.start(1,0);timer.pause(20);
    check(!timer.sample(1000),"save/settings pause consumes no process budget");
    timer.resume(2,1000);
    check(timer.sample(1000)==ProcessTimer::Sample{100,83,false},"H8 adjustment preserves elapsed twenty seconds");
    check(timer.sample(1099)->remaining_seconds==1&&timer.sample(1100)->expired,"pause preserves actual deadline");
    timer.pause(1100);timer.resume(0,2000);
    check(!timer.sample(2001),"changing H8 to zero disables the owner");
    timer.pause(2002);timer.resume(1,3000);
    check(timer.sample(3000)->remaining_seconds==60,"enabling a previously zero budget starts fresh");
    timer.stop();check(!timer.sample(9999),"match entry tears down the process clock");
    timer.start(199,0);check(timer.sample(11939)->remaining_seconds==1,"highest active Source gate");
    check(timer.sample(UINT64_MAX)->expired,"large monotonic jump does not overflow or revive a deadline");
    timer.start(1,100);check(timer.sample(99)->remaining_seconds==60,"pre-anchor sample does not wrap unsigned elapsed time");
    for(unsigned percent:{0u,1u,33u,34u,66u,67u,68u,99u,100u}) {
        Image image(320,240,0x73);
        ProcessTimer::paint(image,{1,percent,false},exe);
        const unsigned end=percent*319/100;
        for(unsigned x=0;x<320;++x)check(image.at(x,0)==(x<=end?exe.u8(::bmh::layout::ProcessTimer_FormatError_RuntimeField+percent/34):0x44),"Original inclusive line and three palette bands");
        for(size_t n=320;n<image.pixels.size();++n)check(image.pixels[n]==0x73,"clock touches exactly the top row");
    }
    Image expired(320,240,0x73);ProcessTimer::paint(expired,{0,0,true},exe);
    for(auto pixel:expired.pixels)check(pixel==0x73,"expired gate returns event8 before painting");
    std::cout<<"PASS: Original process clock, signed expiry, pauses/H8 budget, inclusive top-row raster\n";
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
