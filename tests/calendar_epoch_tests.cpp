// SPDX-License-Identifier: GPL-3.0-only
#include "game/calendar.hpp"
#include "game/week.hpp"
#include "game/save.hpp"
#include "formats/executable_layout.hpp"
#include "formats/le.hpp"
#include "ui/desk_state.hpp"
#include <iostream>
#include <stdexcept>
#include <utility>

void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bmh::LeImage image(int year,int weekday) {
    constexpr uint32_t base=0x10000;
    constexpr size_t image_size=0xb0000;
    auto exe=bmh::LeImage::from_bytes(base,bmh::Bytes(image_size,0));
    const uint8_t setup[]={0xc6,0x44,0x24,0x04,uint8_t(weekday),0xc6,0x44,0x24,0x0c,0x07,0xc7,0x04,0x24,
        uint8_t(year),uint8_t(year>>8),0,0};
    exe.patch(bmh::layout::CalendarDateSetup,setup,sizeof setup);
    const uint8_t months[]={0,31,28,31,30,31,30,31,31,30,31,30,31};
    exe.patch(bmh::layout::DeskState_DeskDate_RuntimeField,months,sizeof months);
    return exe;
}
int main()try {
    for(const auto [year,day]:{std::pair{1995,5},std::pair{2026,2}}) {
        auto exe=image(year,day);bmh::install_calendar_rules(exe);
        check(bmh::weekday_code(0)==day && bmh::weekday_code(7)==day,"engine weekday ignores edition epoch");
        const auto date=bmh::ui::desk_date(0,exe);
        check(date.year==year && date.month==7 && date.day==1 && date.weekday==day,"desktop date ignores edition epoch");
        bmh::GameState g;
        g.memory.add_block(bmh::layout::ManagerCount,1);
        const auto header=bmh::save_header(g);
        const auto expected="01.07."+std::to_string(year);
        check(std::string(reinterpret_cast<const char*>(header.data()+1))==expected,"save-list date ignores edition epoch");
        const auto rollover=bmh::ui::desk_date(184,exe);
        check(rollover.year==year+1 && rollover.month==1 && rollover.day==1,"edition year rollover incorrect");
    }
    auto bad=image(2026,7);bool rejected=false;
    try{bmh::install_calendar_rules(bad);}catch(const bmh::FormatError&){rejected=true;}
    check(rejected && bmh::calendar_epoch().year==2026 && bmh::calendar_epoch().weekday==2,"invalid epoch partially installed");
    auto different=image(1995,5);const uint8_t unknown=0;different.patch(bmh::layout::CalendarDateSetup,&unknown,1);
    rejected=false;try{bmh::calendar_epoch_from_executable(different);}catch(const bmh::FormatError&){rejected=true;}
    check(rejected,"unknown date routine was guessed");
    std::cout<<"Edition calendar epoch, save header and year rollover passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
