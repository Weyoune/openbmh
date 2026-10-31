// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/match_projection.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>
namespace bmh::world12 {
namespace {
uint16_t flags(uint16_t source){return uint16_t(((source&1)?0x8000:0)|((source&2)?0x4000:0)|((source&4)?0x2000:0)|((source&8)?0x1000:0));}
}
AiMatchProjection AiMatchProjection::create(const World& world,const std::vector<ClubId>& requested,const GameState& base){
    if(requested.empty()||requested.size()>32)throw std::invalid_argument("world12: match projection requires 1-32 clubs");
    world.validate();
    for(const auto& manager:world.managers)if(std::find(requested.begin(),requested.end(),manager.club)!=requested.end())throw std::invalid_argument("world12: human manager projection is not integrated");
    if(runtime_dimensions.big_squad_teams<32)throw std::invalid_argument("world12: legacy match workspace is unavailable");
    AiMatchProjection projection;projection.original_managers_=world.managers;projection.state=base;auto& state=projection.state;state.manager_teams.clear();state.players.clear();state.player_name_offsets.clear();state.player_names.clear();state.player_cards.clear();state.player_bonus.clear();state.live_matches.clear();state.live_scene=false;state.last_shooter=0xfff;state.projected_match_clubs.clear();
    state.squad_slots.assign(size_t(runtime_dimensions.total_slots),0xfff);state.club_names.clear();state.club_ids.assign(base.club_ids.size(),0);state.club_kits.assign(base.club_ids.size(),0);state.club_short_offsets.assign(base.club_ids.size(),0);state.team_flags.assign(base.team_flags.size(),0);state.team_morale.assign(base.team_morale.size(),25);
    std::set<ClubId> clubs;std::set<PlayerId> players;
    const auto append=[](Bytes& pool,const std::string& value){if(pool.size()+value.size()+1>65535)throw std::invalid_argument("world12: local match string pool overflow");auto offset=uint16_t(pool.size());pool.insert(pool.end(),value.begin(),value.end());pool.push_back(0);return offset;};
    append(state.club_names,"Unused local club");
    for(auto id:requested){const auto it=world.clubs.find(id);if(it==world.clubs.end()||!clubs.insert(id).second||!it->second.league.valid())throw std::invalid_argument("world12: missing, repeated or non-rostered match club");auto team=projection.clubs_.size();if(team>=state.club_ids.size())throw std::invalid_argument("world12: legacy club workspace too small");projection.clubs_.push_back(id);projection.original_clubs_.push_back(it->second);
        const auto& league=world.catalogue.league(it->second.league);
        const int league_class=(league.country==Country::germany||league.country==Country::england)?league.local_index:5+int(league.id.value-10);
        const bool domestic=league.country==Country::germany||league.country==Country::england;
        state.projected_match_clubs.push_back({league_class,int(league.country),domestic?3:2});
        state.club_kits.at(team)=it->second.kit;state.club_short_offsets.at(team)=it->second.short_span;
        state.team_flags.at(team)=it->second.match_flags;state.team_morale.at(team)=it->second.morale;
        state.club_ids[team]=append(state.club_names,it->second.name);
        for(size_t row=0;row<24;++row){const auto& slot=it->second.roster[row];if(slot.empty())continue;const auto found=world.players.find(slot.player);if(found==world.players.end()||!players.insert(slot.player).second||projection.players_.size()>=4095)throw std::invalid_argument("world12: invalid local player mapping");auto local=uint16_t(projection.players_.size());projection.players_.push_back(slot.player);projection.originals_.push_back(found->second);state.players.push_back(found->second.attributes);state.player_name_offsets.push_back(append(state.player_names,found->second.attributes.name));state.player_cards.push_back(found->second.cards);state.player_bonus.push_back(found->second.bonus);state.squad_slots[team*24+row]=uint16_t(local|flags(slot.flags));}
    }
    state.counts.players=uint32_t(state.players.size());return projection;
}
ClubId AiMatchProjection::global_club(uint32_t local)const{return clubs_.at(local);}
PlayerId AiMatchProjection::global_player(uint32_t local)const{return players_.at(local);}
uint32_t AiMatchProjection::local_club(ClubId id)const{auto it=std::find(clubs_.begin(),clubs_.end(),id);if(it==clubs_.end())throw std::out_of_range("world12: club is not projected");return uint32_t(it-clubs_.begin());}
uint32_t AiMatchProjection::local_player(PlayerId id)const{auto it=std::find(players_.begin(),players_.end(),id);if(it==players_.end())throw std::out_of_range("world12: player is not projected");return uint32_t(it-players_.begin());}
void AiMatchProjection::commit_player_attributes(World& world)const {
    if(world.managers!=original_managers_)throw std::invalid_argument("world12: manager ownership changed during projection");
    if(state.players.size()!=players_.size()||state.player_bonus.size()!=players_.size())throw std::invalid_argument("world12: local player storage resized");
    for(size_t i=0;i<players_.size();++i)if(!world.players.contains(players_[i])||world.players.at(players_[i])!=originals_.at(i))throw std::invalid_argument("world12: projected player was removed or changed externally");
    auto candidate=world;
    for(size_t i=0;i<players_.size();++i){auto& target=candidate.players.at(players_[i]);target.attributes=state.players[i];target.bonus=state.player_bonus[i];}
    candidate.validate();world=std::move(candidate);
}
void AiMatchProjection::commit_match_state(World& world)const {
    if(world.managers!=original_managers_)throw std::invalid_argument("world12: manager ownership changed during projection");
    if(state.players.size()!=players_.size() || state.player_bonus.size()!=players_.size() || state.player_cards.size()!=players_.size())
        throw std::invalid_argument("world12: projected player storage resized");
    for(size_t i=0;i<players_.size();++i)if(!world.players.contains(players_[i]) || world.players.at(players_[i])!=originals_.at(i))
        throw std::invalid_argument("world12: projected player changed externally");
    for(size_t i=0;i<clubs_.size();++i)if(!world.clubs.contains(clubs_[i]) || world.clubs.at(clubs_[i])!=original_clubs_.at(i))
        throw std::invalid_argument("world12: projected club or roster changed externally");
    auto candidate=world;
    for(size_t i=0;i<players_.size();++i){auto& target=candidate.players.at(players_[i]);target.attributes=state.players[i];target.bonus=state.player_bonus[i];target.cards=state.player_cards[i];}
    for(size_t team=0;team<clubs_.size();++team) {
        auto& target=candidate.clubs.at(clubs_[team]);std::set<PlayerId> before,after;
        for(const auto& slot:target.roster)if(!slot.empty())before.insert(slot.player);
        for(size_t row=0;row<24;++row) {
            const auto raw=state.squad_slots.at(team*24+row);auto& slot=target.roster[row];
            if((raw&0xfff)==0xfff){if(raw!=0xfff)throw std::invalid_argument("world12: match flags on an empty roster slot");slot={};}
            else {slot.player=global_player(raw&0xfff);slot.flags=uint16_t(((raw&0x8000)?1:0)|((raw&0x4000)?2:0)|((raw&0x2000)?4:0)|((raw&0x1000)?8:0));after.insert(slot.player);}
        }
        if(before!=after)throw std::invalid_argument("world12: match changed roster membership or moved a player between clubs");
        target.match_flags=state.team_flags.at(team);target.morale=state.team_morale.at(team)&63;
    }
    candidate.validate();world=std::move(candidate);
}
}
