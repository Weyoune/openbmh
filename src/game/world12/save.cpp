// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/save.hpp"
#include "game/world12/codec.hpp"
#include <bit>
namespace bmh::world12 {
void CareerSnapshot::validate()const{world.validate();results.validate(world.catalogue);for(size_t i=0;i<14;++i)if(league_rounds[i]>world.catalogue.leagues[i].rounds())throw FormatError("world12: round cursor past season end");}
Savegame make_save(const CareerSnapshot& snapshot){snapshot.validate();Savegame save;save.version="O1.2";save.key=0;save.checksum_ok=true;save.stream={'O','B','M','H','1','2','S','1'};
    const auto put=[&](uint32_t v){for(int i=0;i<4;++i)save.stream.push_back(uint8_t(v>>(i*8)));};auto world=encode_world(snapshot.world);put(uint32_t(world.size()));put(snapshot.date);put(snapshot.season);put(snapshot.watcom_seed);put(std::bit_cast<uint32_t>(snapshot.game_seed));save.stream.insert(save.stream.end(),world.begin(),world.end());for(auto round:snapshot.league_rounds)save.stream.push_back(round);
    for(const auto&league:snapshot.results.leagues)for(const auto&r:league){save.stream.push_back(uint8_t(r.played));save.stream.push_back(r.home);save.stream.push_back(r.away);}for(auto byte:save.stream)save.checksum=uint8_t(save.checksum+byte);return save;
}
CareerSnapshot read_save(const Savegame& save){if(save.version!="O1.2"||!save.checksum_ok)throw FormatError("world12: incompatible or damaged save");const auto&raw=save.stream;if(raw.size()<28||std::string(raw.begin(),raw.begin()+8)!="OBMH12S1")throw FormatError("world12: unknown save stream");const auto size=le32(raw,8);if(size>32*1024*1024||size_t(size)+28+14>raw.size())throw FormatError("world12: truncated world stream");CareerSnapshot state;state.date=le32(raw,12);state.season=le32(raw,16);state.watcom_seed=le32(raw,20);state.game_seed=std::bit_cast<int32_t>(le32(raw,24));state.world=decode_world(Bytes(raw.begin()+28,raw.begin()+28+size));size_t at=28+size;for(auto&round:state.league_rounds)round=raw.at(at++);state.results=SeasonResults::empty(state.world.catalogue);
    for(auto&league:state.results.leagues)for(auto&r:league){if(at+3>raw.size()||raw[at]>1)throw FormatError("world12: truncated/invalid results");r={raw[at]!=0,raw[at+1],raw[at+2]};at+=3;}if(at!=raw.size())throw FormatError("world12: trailing save bytes");state.validate();return state;
}
}
