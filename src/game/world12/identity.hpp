// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <limits>
#include <compare>
namespace bmh::world12 {
// Strong identities must never be interchanged with physical roster positions.
template<class Tag> struct Id {
    uint32_t value=std::numeric_limits<uint32_t>::max();
    constexpr bool valid()const{return value!=std::numeric_limits<uint32_t>::max();}
    auto operator<=>(const Id&)const=default;
};
struct PlayerTag;struct ClubTag;struct LeagueTag;struct CompetitionTag;struct CrestTag;
using PlayerId=Id<PlayerTag>;using ClubId=Id<ClubTag>;using LeagueId=Id<LeagueTag>;
using CompetitionId=Id<CompetitionTag>;using CrestId=Id<CrestTag>;
enum class Country:uint8_t { germany=0,spain=1,england=2,france=3,italy=4,portugal=5 };
enum class TableGroup:uint8_t { germany,england,europe };
enum class RosterFlag:uint16_t { lineup=1,reserve=2,unavailable=4,auxiliary=8 };
struct RosterSlot {
    PlayerId player;
    uint16_t flags=0;
    bool empty()const{return !player.valid();}
    bool operator==(const RosterSlot&)const=default;
};
}
