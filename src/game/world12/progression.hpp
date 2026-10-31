// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/world12/save.hpp"
namespace bmh::world12 {
struct ScheduledLeagueRound {
    LeagueAppointment appointment;
    std::vector<Fixture> matches;
    bool operator==(const ScheduledLeagueRound&)const=default;
};
struct ScheduledCupLeg {
    DomesticCup cup=DomesticCup::german_cup;
    uint8_t source_entry=0,leg=0;
    uint64_t revision=0;
    std::vector<Fixture> matches;
    bool operator==(const ScheduledCupLeg&)const=default;
};
struct DayTicket {
    uint16_t day=0;uint64_t revision=0;
    std::vector<ScheduledLeagueRound> leagues;
    std::vector<OtherAppointment> others;
    std::vector<ScheduledCupLeg> cups;
    bool operator==(const DayTicket&)const=default;
};
struct CompletedLeagueRound {LeagueId league;uint8_t round=0;std::vector<LeagueResult> results;};
// Scores come from the match layer. This coordinator never manufactures match
// outcomes or consumes either gameplay RNG to satisfy a calendar transition.
std::optional<DayTicket> prepare_day(const CareerSnapshot&);
void commit_league_day(CareerSnapshot&,const DayTicket&,const std::vector<CompletedLeagueRound>&);
struct DayPresentation {std::vector<CupDrawNotice> cup_draws;};
DayPresentation commit_day(CareerSnapshot&,const DayTicket&,const std::vector<CompletedLeagueRound>&,
    const std::vector<CupLegResult>&);
}
