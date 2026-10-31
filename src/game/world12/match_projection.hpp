// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/world12/world.hpp"
#include "game/state.hpp"
namespace bmh::world12 {
// The original match calculations operate on bounded local references. Global
// 32-bit IDs never enter their packed fields; mutations are mapped back explicitly.
// Human manager/tactics projection is deliberately not enabled yet.
class AiMatchProjection {
public:
    static AiMatchProjection create(const World&,const std::vector<ClubId>&,const GameState& rules_template);
    GameState state;
    ClubId global_club(uint32_t local)const;
    PlayerId global_player(uint32_t local)const;
    uint32_t local_club(ClubId)const;
    uint32_t local_player(PlayerId)const;
    void commit_player_attributes(World&)const;
    void commit_match_state(World&)const;
private:
    std::vector<ClubId> clubs_;
    std::vector<PlayerId> players_;
    std::vector<PlayerRecord> originals_;
    std::vector<ClubRecord> original_clubs_;
    std::vector<Manager> original_managers_;
};
}
