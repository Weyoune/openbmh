// SPDX-License-Identifier: GPL-3.0-only
#include "game/calendar.hpp"
#include "game/world12/calendar.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Function> void rejects(Function function,const char* message) {
    bool rejected=false;
    try { function(); } catch (const std::exception&) { rejected=true; }
    check(rejected,message);
}
std::vector<uint8_t> executable_fixture() {
    std::vector<uint8_t> bytes(507925);
    bytes[0]='M'; bytes[1]='Z';
    const std::vector<uint8_t> instructions={
        0xc6,0x45,0xf6,5, 0xc6,0x45,0xf7,7,
        0x66,0xc7,0x45,0xf8,0xcb,7, 0x66,0xc7,0x45,0xfa,3,0
    };
    std::copy(instructions.begin(),instructions.end(),bytes.begin()+0x4ff1);
    return bytes;
}
}

int main() try {
    using namespace bmh::world12;
    auto bytes=executable_fixture();
    auto epoch=bmh::calendar_epoch_from_fltd(bytes);
    check(epoch.year==1995 && epoch.weekday==5,"English 16-bit epoch not read");
    // A supported edition may change the operands, but never the instructions.
    bytes[0x4ff1+3]=2;
    bytes[0x4ff1+12]=0xea; bytes[0x4ff1+13]=7;
    epoch=bmh::calendar_epoch_from_fltd(bytes);
    check(epoch.year==2026 && epoch.weekday==2,"patched epoch operands lost");
    for (size_t offset=0;offset<20;++offset) {
        if (offset==3 || offset==12 || offset==13) continue;
        auto corrupt=bytes; corrupt[0x4ff1+offset]^=1;
        rejects([&]{ bmh::calendar_epoch_from_fltd(corrupt); },"unknown instruction layout accepted");
    }
    bytes[0x4ff1+3]=7;
    rejects([&]{ bmh::calendar_epoch_from_fltd(bytes); },"invalid weekday accepted");
    bytes=executable_fixture(); bytes[0x4ff1+12]=0; bytes[0x4ff1+13]=0;
    rejects([&]{ bmh::calendar_epoch_from_fltd(bytes); },"invalid year accepted");
    bytes=executable_fixture(); bytes.pop_back();
    rejects([&]{ bmh::calendar_epoch_from_fltd(bytes); },"unknown executable size accepted");
    bytes=executable_fixture(); bytes[0]='N';
    rejects([&]{ bmh::calendar_epoch_from_fltd(bytes); },"non-executable accepted");

    for (int source=0;source<7;++source) for (int target=0;target<7;++target) {
        std::vector<uint16_t> words(366);
        words[10]=0x4002; words[40]=15; words[340]=0x2020;
        const auto aligned=align_source_calendar(words,source,target,365);
        check(aligned.day_shift>=-3 && aligned.day_shift<=3,"non-nearest weekday shift");
        check(aligned.entries.size()==365,"target season length lost");
        for (int day : {10,40,340}) {
            const int destination=day+aligned.day_shift;
            check((day+source)%7==(destination+target)%7,"appointment weekday changed");
            check(aligned.entries[size_t(destination)]==words[size_t(day)],"appointment bits changed");
        }
        check(std::count_if(aligned.entries.begin(),aligned.entries.end(),[](auto w){return w!=0;})==3,
            "appointment dropped or duplicated");
    }
    std::vector<uint16_t> edge(365);
    edge[0]=0x4002;
    rejects([&]{ align_source_calendar(edge,0,1,365); },"negative edge appointment discarded");
    edge[0]=0; edge[364]=15;
    rejects([&]{ align_source_calendar(edge,1,0,365); },"end appointment wrapped");
    check(align_source_calendar(edge,1,0,366).entries[365]==15,"leap target last day lost");
    std::vector<uint16_t> leap(366); leap[365]=0x20;
    rejects([&]{ align_source_calendar(leap,0,0,365); },"leap appointment discarded");
    rejects([&]{ align_source_calendar(edge,-1,0,365); },"negative epoch weekday accepted");
    rejects([&]{ align_source_calendar(edge,0,7,365); },"invalid target weekday accepted");
    rejects([&]{ align_source_calendar(edge,0,0,364); },"partial target year accepted");

    // The English source's Saturday league dates move together to Saturday
    // in the German Wednesday-based season. Cup identities remain separate.
    std::vector<uint16_t> de(366),en(366);
    for (int r=0;r<34;++r) de[size_t(24+r*7)]=15;
    for (int r=0;r<38;++r) en[size_t(21+r*7)]=15;
    de[18]=en[15]=0x20;
    const auto aligned=align_source_calendar(en,5,2,366);
    const auto calendar=SeasonCalendar::from_sources(Catalogue::dual_classic(),de,aligned.entries);
    check(aligned.day_shift==3 && calendar.leagues_on(24).size()==13,"mixed country Saturday rounds separated");
    const auto cups=calendar.others_on(18);
    check(cups.size()==2 && cups[0].domain==CalendarDomain::germany && cups[1].domain==CalendarDomain::england,
        "aligned domestic cups lost country ownership");
    std::cout<<"Verified English epoch, 49 weekday alignments, season edges and country ownership passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
}
