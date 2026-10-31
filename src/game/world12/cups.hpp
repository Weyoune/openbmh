// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/world12/calendar.hpp"
#include <optional>
namespace bmh { class LeImage; }
namespace bmh::world12 {
using DomesticCup=CountryRules::Cup;
Country cup_country(DomesticCup);
// No interpretation of indoor/national/group-stage events as domestic knockouts.
std::optional<std::vector<DomesticCup>> domestic_cups_on(const OtherAppointment&);
struct CupRound {
    std::vector<Fixture> fixtures; // first-leg orientation
    std::vector<uint16_t> first_leg,second_leg; // original score/phase words
    bool operator==(const CupRound&)const=default;
};
struct KnockoutStage {
    DomesticCup cup=DomesticCup::german_cup;
    uint8_t source_entry=0,flags=0,planned_rounds=0,next_entry=0;
    uint64_t revision=0;
    std::vector<ClubId> seeded;
    std::vector<CupRound> rounds;
    static KnockoutStage from_source(DomesticCup,const LeImage&,uint8_t entry,
        const std::vector<Fixture>& initial_draw,const World&,const std::vector<ClubId>& seeded={});
    bool two_legs()const {return (flags&1)!=0;}
    bool complete()const;
    uint8_t next_leg()const;
    std::vector<Fixture> next_fixtures()const;
    std::vector<ClubId> qualifiers()const;
    void validate(const World&)const;
    bool operator==(const KnockoutStage&)const=default;
};
struct CupLegResult {
    DomesticCup cup=DomesticCup::german_cup;
    uint8_t source_entry=0;
    uint64_t revision=0;
    std::vector<uint16_t> scores;
    // Supplied by the original draw/seed owner, never guessed by this adapter.
    // Required after each non-terminal round, including any home-order changes.
    std::vector<Fixture> next_draw;
    std::optional<KnockoutStage> next_stage;
};
// Atomic score/leg transition; unresolved ties and missing draws leave it intact.
void commit_cup_leg(KnockoutStage&,const CupLegResult&,const World&);
void validate_cup_programmes(const World&,const std::vector<KnockoutStage>&);
}
