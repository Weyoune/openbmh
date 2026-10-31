// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/world12/cup_draw.hpp"
#include "game/world12/progression.hpp"

namespace bmh::app::world12 {
struct CupSources {const LeImage& german;const LeImage& english;};
// Sources/rankings are read-only. State and the corresponding RNG seed publish
// together only after every source stage and score receipt has been validated.
std::vector<bmh::world12::CupDrawNotice> install_domestic_cup_fields(bmh::world12::CareerSnapshot&,
    const bmh::world12::CountryCupRanking& german,const bmh::world12::CountryCupRanking& english,const CupSources&);
struct CupScores {
    bmh::world12::DomesticCup cup=bmh::world12::DomesticCup::german_cup;
    uint8_t source_entry=0;uint64_t revision=0;
    std::vector<uint16_t> scores;
};
// Called on the match owner's staged post-match snapshot. This function draws
// subsequent fields; it neither simulates scores nor invents match RNG effects.
bmh::world12::DayPresentation commit_day_with_domestic_draws(bmh::world12::CareerSnapshot&,
    const bmh::world12::DayTicket&,const std::vector<bmh::world12::CompletedLeagueRound>&,
    const std::vector<CupScores>&,const CupSources&);
}
