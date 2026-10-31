// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/save.hpp"
#include "game/world12/codec.hpp"
#include <bit>
namespace bmh::world12 {
void CareerSnapshot::validate()const{
    world.validate();results.validate(world.catalogue);validate_cup_programmes(world,cups);for(size_t i=0;i<14;++i)if(league_rounds[i]>world.catalogue.leagues[i].rounds())throw FormatError("world12: round cursor past season end");
    if(calendar){calendar->schedule.validate(world.catalogue);if(calendar->next_day>calendar->schedule.days)throw FormatError("world12: invalid calendar cursor");std::array<uint8_t,14> expected{};for(const auto&a:calendar->schedule.leagues)if(a.day<calendar->next_day)expected[a.league.value]=a.round;if(expected!=league_rounds)throw FormatError("world12: saved rounds disagree with committed calendar days");for(uint32_t l=0;l<14;++l)for(int r=1;r<=world.catalogue.leagues[l].rounds();++r)for(int m=0;m<world.catalogue.leagues[l].clubs/2;++m)if(results.result(world.catalogue,{l},r,m).played!=(r<=league_rounds[l]))throw FormatError("world12: saved scores disagree with committed rounds");}
}
Savegame make_save(const CareerSnapshot& snapshot){snapshot.validate();Savegame save;save.version="O1.2";save.key=0;save.checksum_ok=true;save.stream={'O','B','M','H','1','2','S','3'};
    const auto put=[&](uint64_t v,int width=4){for(int i=0;i<width;++i)save.stream.push_back(uint8_t(v>>(i*8)));};auto world=encode_world(snapshot.world);put(world.size());put(snapshot.date);put(snapshot.season);put(snapshot.watcom_seed);put(std::bit_cast<uint32_t>(snapshot.game_seed));save.stream.insert(save.stream.end(),world.begin(),world.end());for(auto round:snapshot.league_rounds)put(round,1);
    for(const auto&league:snapshot.results.leagues)for(const auto&r:league){put(r.played?1:0,1);put(r.home,1);put(r.away,1);}
    put(snapshot.calendar?1:0,1);if(snapshot.calendar){const auto&cursor=*snapshot.calendar;put(cursor.schedule.days,2);put(cursor.next_day,2);put(cursor.revision,8);put(cursor.schedule.leagues.size());put(cursor.schedule.others.size());for(const auto&a:cursor.schedule.leagues){put(a.day,2);put(a.league.value);put(a.round,1);}for(const auto&a:cursor.schedule.others){put(a.day,2);put(uint8_t(a.domain),1);put(a.source_entry,2);}}
    put(snapshot.cups.size(),2);
    for(const auto& stage:snapshot.cups){
        put(uint8_t(stage.cup),1);put(stage.source_entry,1);put(stage.flags,1);put(stage.planned_rounds,1);put(stage.next_entry,1);put(stage.seeded.size(),1);for(auto club:stage.seeded)put(club.value);put(stage.revision,8);put(stage.rounds.size(),1);
        for(const auto& round:stage.rounds){
            put(round.fixtures.size(),1);for(const auto& fixture:round.fixtures){put(fixture.home.value);put(fixture.away.value);}
            for(const auto* scores:{&round.first_leg,&round.second_leg}){put(scores->size(),1);for(auto score:*scores)put(score,2);}
        }
    }
    for(auto byte:save.stream)save.checksum=uint8_t(save.checksum+byte);return save;
}
CareerSnapshot read_save(const Savegame& save){
    if(save.version!="O1.2"||!save.checksum_ok)throw FormatError("world12: incompatible or damaged save");const auto&raw=save.stream;if(raw.size()<28||std::string(raw.begin(),raw.begin()+8)!="OBMH12S3")throw FormatError("world12: unknown development save schema");size_t at=8;
    const auto get=[&](int width=4){if(at+size_t(width)>raw.size())throw FormatError("world12: truncated save field");uint64_t value=0;for(int i=0;i<width;++i)value|=uint64_t(raw[at++])<<(8*i);return value;};
    auto size=get();if(size>32*1024*1024)throw FormatError("world12: world stream too large");CareerSnapshot state;state.date=uint32_t(get());state.season=uint32_t(get());state.watcom_seed=uint32_t(get());state.game_seed=std::bit_cast<int32_t>(uint32_t(get()));if(at+size>raw.size())throw FormatError("world12: truncated world stream");state.world=decode_world(Bytes(raw.begin()+at,raw.begin()+at+size));at+=size;for(auto&round:state.league_rounds)round=uint8_t(get(1));state.results=SeasonResults::empty(state.world.catalogue);
    for(auto&league:state.results.leagues)for(auto&r:league){auto played=get(1);if(played>1)throw FormatError("world12: invalid result flag");r={played!=0,uint8_t(get(1)),uint8_t(get(1))};}
    auto present=get(1);if(present>1)throw FormatError("world12: invalid calendar flag");if(present){CalendarCursor cursor;cursor.schedule.days=uint16_t(get(2));cursor.next_day=uint16_t(get(2));cursor.revision=get(8);auto leagues=get(),others=get();if(leagues>1000||others>4096)throw FormatError("world12: calendar entries exceed limits");for(uint64_t i=0;i<leagues;++i)cursor.schedule.leagues.push_back({uint16_t(get(2)),{uint32_t(get())},uint8_t(get(1))});for(uint64_t i=0;i<others;++i)cursor.schedule.others.push_back({uint16_t(get(2)),CalendarDomain(get(1)),uint16_t(get(2))});state.calendar=std::move(cursor);}
    const auto cup_count=get(2);if(cup_count>180)throw FormatError("world12: too many domestic cup owners");
    for(uint64_t c=0;c<cup_count;++c){
        KnockoutStage stage;stage.cup=DomesticCup(get(1));stage.source_entry=uint8_t(get(1));stage.flags=uint8_t(get(1));stage.planned_rounds=uint8_t(get(1));stage.next_entry=uint8_t(get(1));const auto seeds=get(1);if(seeds>128)throw FormatError("world12: seeded cup count exceeds limits");for(uint64_t i=0;i<seeds;++i)stage.seeded.push_back({uint32_t(get())});stage.revision=get(8);
        const auto rounds=get(1);if(rounds>7)throw FormatError("world12: cup round count exceeds limits");
        for(uint64_t r=0;r<rounds;++r){
            CupRound round;const auto matches=get(1);if(matches>64)throw FormatError("world12: cup fixture count exceeds limits");
            for(uint64_t m=0;m<matches;++m)round.fixtures.push_back({{uint32_t(get())},{uint32_t(get())}});
            for(auto* scores:{&round.first_leg,&round.second_leg}){const auto count=get(1);if(count>64)throw FormatError("world12: cup score count exceeds limits");for(uint64_t m=0;m<count;++m)scores->push_back(uint16_t(get(2)));}
            stage.rounds.push_back(std::move(round));
        }
        state.cups.push_back(std::move(stage));
    }
    if(at!=raw.size())throw FormatError("world12: trailing save bytes");state.validate();return state;
}
}
