// SPDX-License-Identifier: GPL-3.0-only
#include "game/calendar.hpp"
#include "formats/executable_layout.hpp"
#include "formats/le.hpp"
namespace bmh {
namespace { CalendarEpoch installed_epoch{2026, 2}; }
CalendarEpoch calendar_epoch_from_fltd(std::span<const uint8_t> executable) {
    constexpr size_t setup = 0x4ff1;
    if (executable.size() != 507925 || executable[0] != 'M' || executable[1] != 'Z')
        throw FormatError("calendar: unsupported Football Limited executable");
    // The DOS V2.20 routine uses a frame pointer and 16-bit year operands,
    // unlike the German LE routine. Verify every surrounding instruction;
    // only the weekday and year operands may differ between editions.
    constexpr uint8_t signature[] = {
        0xc6,0x45,0xf6,0, 0xc6,0x45,0xf7,7,
        0x66,0xc7,0x45,0xf8,0,0, 0x66,0xc7,0x45,0xfa,3,0
    };
    for (size_t i=0;i<sizeof signature;++i)
        if (i!=3 && i!=12 && i!=13 && executable[setup+i]!=signature[i])
            throw FormatError("calendar: unverified Football Limited date setup");
    const int year=int(executable[setup+12]) | int(executable[setup+13])<<8;
    const int weekday=executable[setup+3];
    if (year<1600 || year>9999 || weekday>6)
        throw FormatError("calendar: invalid Football Limited epoch");
    return {year,weekday};
}
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
