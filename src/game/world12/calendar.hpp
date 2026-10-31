// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/world12/fixtures.hpp"
namespace bmh::world12 {
struct LeagueAppointment {
    uint16_t day=0;LeagueId league;uint8_t round=0;
    bool operator==(const LeagueAppointment&)const=default;
};
enum class CalendarDomain:uint8_t { germany,england,shared };
// Non-league entries retain their source identity. They must be handled by the
// correct cup/national transaction rather than reinterpreted under a global flag.
struct OtherAppointment {
    uint16_t day=0;CalendarDomain domain=CalendarDomain::shared;uint16_t source_entry=0;
    bool operator==(const OtherAppointment&)const=default;
};
struct SeasonCalendar {
    uint16_t days=365;
    std::vector<LeagueAppointment> leagues;
    std::vector<OtherAppointment> others;
    // Source dates must already be aligned to the common season epoch. No
    // filename/year heuristic silently shifts or substitutes missing dates.
    static SeasonCalendar from_sources(const Catalogue&,const std::vector<uint16_t>& german,const std::vector<uint16_t>& english);
    void validate(const Catalogue&)const;
    std::vector<LeagueAppointment> leagues_on(uint16_t day)const;
    std::vector<OtherAppointment> others_on(uint16_t day)const;
    bool operator==(const SeasonCalendar&)const=default;
};
}
