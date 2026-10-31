// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/world12/world.hpp"
#include "game/world12/cup_draw.hpp"
#include "game/cup.hpp"
#include "formats/le.hpp"

namespace bmh::test::world12 {
inline bmh::world12::World complete_world() {
    using namespace bmh::world12;
    World world;uint32_t club_id=100000,player_id=200000;
    for(const auto& league:world.catalogue.leagues) {
        auto& membership=world.membership[league.id.value];membership.league=league.id;
        for(int c=0;c<league.clubs;++c) {
            ClubRecord club;club.id={club_id++};club.name="Club "+std::to_string(club.id.value);
            club.country=league.country;club.league=league.id;club.crest={club.id.value};
            world.crests[club.crest]={"W/TEST.VGA",0};
            for(int p=0;p<11;++p) {
                PlayerRecord player;player.id={player_id++};player.attributes={"Player "+std::to_string(player.id.value),50u|(60u<<7)|(65u<<14),0,0};
                world.players[player.id]=player;club.roster[size_t(p)]={player.id,1};
            }
            membership.clubs.push_back(club.id);world.clubs[club.id]=club;
        }
    }
    world.validate();return world;
}
inline bmh::world12::CountryCupRanking membership_ranking(const bmh::world12::World& world,bmh::world12::Country country) {
    bmh::world12::CountryCupRanking ranking;
    for(uint8_t local=0;local<5;++local)ranking[local]=world.membership.at(world.catalogue.starting_league(country,local).value).clubs;
    return ranking;
}
inline LeImage country_rules(bool english) {
    auto image=LeImage::from_bytes(layout::LeagueSizes,Bytes(0x1000));
    const uint8_t size=english?20:18;image.patch(layout::LeagueSizes,&size,1);
    const auto stage=[&](uint8_t entry,uint8_t type,uint8_t teams,uint8_t rounds,uint8_t flags,uint8_t next,uint8_t seeds){
        const auto at=cup_entry_address(entry);
        image.patch(at+0x14,&flags,1);image.patch(at+0x16,&next,1);image.patch(at+0x18,&type,1);
        image.patch(at+0x1a,&teams,1);image.patch(at+0x1b,&rounds,1);image.patch(at+0x20,&seeds,1);
    };
    stage(23,8,2,1,12,0,0);
    if(!english)stage(0,0,64,6,46,0,0);
    else {stage(0,0,48,2,46,28,20);stage(28,0,32,5,46,0,0);
        stage(25,11,24,1,46,26,20);stage(26,11,32,4,63,27,0);stage(27,11,2,1,44,0,0);}
    return image;
}
}
