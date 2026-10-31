// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/match_projection.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace bmh::world12 {
namespace {
uint16_t flags(uint16_t source){return uint16_t(((source&1)?0x8000:0)|((source&2)?0x4000:0)|((source&4)?0x2000:0)|((source&8)?0x1000:0));}
}
AiMatchProjection AiMatchProjection::create(const World& world,const std::vector<ClubId>& requested,const GameState& base){
    if(requested.empty()||requested.size()>32)throw std::invalid_argument("world12: match projection requires 1-32 clubs");
    if(!world.managers.empty())throw std::invalid_argument("world12: human manager projection is not integrated");
    if(runtime_dimensions.big_squad_teams<32)throw std::invalid_argument("world12: legacy match workspace is unavailable");
    AiMatchProjection projection;projection.state=base;auto& state=projection.state;state.manager_teams.clear();state.players.clear();state.player_name_offsets.clear();state.player_names.clear();state.player_cards.clear();state.player_bonus.clear();state.live_matches.clear();state.live_scene=false;state.last_shooter=0xfff;
    state.squad_slots.assign(size_t(runtime_dimensions.total_slots),0xfff);state.club_names.clear();state.club_ids.assign(base.club_ids.size(),0);state.team_flags.assign(base.team_flags.size(),0);state.team_morale.assign(base.team_morale.size(),25);
    std::set<ClubId> clubs;std::set<PlayerId> players;
    const auto append=[](Bytes& pool,const std::string& value){if(pool.size()+value.size()+1>65535)throw std::invalid_argument("world12: local match string pool overflow");auto offset=uint16_t(pool.size());pool.insert(pool.end(),value.begin(),value.end());pool.push_back(0);return offset;};
    append(state.club_names,"Unused local club");
    for(auto id:requested){const auto it=world.clubs.find(id);if(it==world.clubs.end()||!clubs.insert(id).second||!it->second.league.valid())throw std::invalid_argument("world12: missing, repeated or non-rostered match club");auto team=projection.clubs_.size();if(team>=state.club_ids.size())throw std::invalid_argument("world12: legacy club workspace too small");projection.clubs_.push_back(id);state.club_ids[team]=append(state.club_names,it->second.name);
        for(size_t row=0;row<24;++row){const auto& slot=it->second.roster[row];if(slot.empty())continue;const auto found=world.players.find(slot.player);if(found==world.players.end()||!players.insert(slot.player).second||projection.players_.size()>=4095)throw std::invalid_argument("world12: invalid local player mapping");auto local=uint16_t(projection.players_.size());projection.players_.push_back(slot.player);projection.originals_.push_back(found->second);state.players.push_back(found->second.attributes);state.player_name_offsets.push_back(append(state.player_names,found->second.attributes.name));state.player_cards.push_back(0);state.player_bonus.push_back(found->second.bonus);state.squad_slots[team*24+row]=uint16_t(local|flags(slot.flags));}
    }
    state.counts.players=uint32_t(state.players.size());return projection;
}
ClubId AiMatchProjection::global_club(uint32_t local)const{return clubs_.at(local);}
PlayerId AiMatchProjection::global_player(uint32_t local)const{return players_.at(local);}
uint32_t AiMatchProjection::local_club(ClubId id)const{auto it=std::find(clubs_.begin(),clubs_.end(),id);if(it==clubs_.end())throw std::out_of_range("world12: club is not projected");return uint32_t(it-clubs_.begin());}
uint32_t AiMatchProjection::local_player(PlayerId id)const{auto it=std::find(players_.begin(),players_.end(),id);if(it==players_.end())throw std::out_of_range("world12: player is not projected");return uint32_t(it-players_.begin());}
void AiMatchProjection::commit_player_attributes(World& world)const {
    if(state.players.size()!=players_.size()||state.player_bonus.size()!=players_.size())throw std::invalid_argument("world12: local player storage resized");
    for(size_t i=0;i<players_.size();++i)if(!world.players.contains(players_[i])||world.players.at(players_[i])!=originals_.at(i))throw std::invalid_argument("world12: projected player was removed or changed externally");
    for(size_t i=0;i<players_.size();++i){auto& target=world.players.at(players_[i]);target.attributes=state.players[i];target.bonus=state.player_bonus[i];}
}
}
