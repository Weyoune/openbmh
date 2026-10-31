// SPDX-License-Identifier: GPL-3.0-only
#include "game/calendar.hpp"
#include "formats/executable_layout.hpp"
#include "formats/le.hpp"
namespace bmh {
namespace { CalendarEpoch installed_epoch{2026, 2}; }
CalendarEpoch calendar_epoch_from_executable(const LeImage& exe) {
    if(const auto* policy=exe.runtime_policy())return {int(policy->year),int(policy->weekday)};
    constexpr auto setup = layout::CalendarDateSetup;
    constexpr uint8_t weekday_prefix[] = {0xc6,0x44,0x24,0x04};
    constexpr uint8_t month_and_year_prefix[] = {0xc6,0x44,0x24,0x0c,0x07,0xc7,0x04,0x24};
    if (!exe.contains(setup) || !exe.contains(setup + 16))
        throw FormatError("calendar: missing original date setup");
    for (uint32_t i=0;i<sizeof weekday_prefix;++i)
        if(exe.u8(setup+i)!=weekday_prefix[i])throw FormatError("calendar: unverified weekday setup");
    for (uint32_t i=0;i<sizeof month_and_year_prefix;++i)
        if(exe.u8(setup+5+i)!=month_and_year_prefix[i])throw FormatError("calendar: unverified year setup");
    const auto year=exe.u32(setup+13);const int weekday=exe.u8(setup+4);
    if(year<1600||year>9999||weekday>6)throw FormatError("calendar: invalid original epoch");
    return {int(year),weekday};
}
void install_calendar_rules(const LeImage& exe) { installed_epoch=calendar_epoch_from_executable(exe); }
CalendarEpoch calendar_epoch() { return installed_epoch; }
}
