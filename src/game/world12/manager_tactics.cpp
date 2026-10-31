// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/manager_tactics.hpp"
#include "game/lineup.hpp"
#include <algorithm>
#include <stdexcept>
namespace bmh::world12 {
namespace {
constexpr size_t board_size=0x840,row_size=0x58,record_size=0xb0c;
void put_rows(GameState& state,const ManagerTactics& input) {
    for(size_t i=0;i<24;++i){const auto at=i*row_size;const auto& r=input.rows[i];
        std::copy(r.orders.begin(),r.orders.end(),state.tactics.begin()+at+2);
        state.tactics[at+0x3f]=r.partner;state.tactics[at+0x41]=r.depth;state.tactics[at+0x42]=r.lane;}
}
GameState workspace(const World& world,const Manager& manager,TacticalPolicy policy) {
    if(runtime_dimensions.big_squad_teams<1 || squad_range(0).count!=24)
        throw std::invalid_argument("world12: human tactical workspace requires a 24-player squad");
    const auto& club=world.clubs.at(manager.club);GameState state;
    state.indoor=policy.indoor;state.lineups_frozen=policy.frozen;state.manager_teams={0};
    state.club_ids={0};state.team_flags={club.match_flags};state.club_heap_78.assign(2,0);
    state.manager_records.assign(record_size,0);state.manager_records[0x288]=manager.tactics->automatic_weights;
    state.manager_options.assign(5,0);state.manager_options[0]=manager.tactics->automatic_formation;
    // These actions only check formation-buffer capacity; they do not read or
    // synthesize any formation. Formation application is a separate future API.
    state.formations.resize(0x160);state.tactics.resize(board_size);put_rows(state,*manager.tactics);
    state.memory.add_block(layout::ActiveManager,1);state.memory.set8(layout::ActiveManager,0);
    state.memory.add_block(layout::GameMode,1);state.memory.set8(layout::GameMode,uint8_t(policy.mode));
    state.squad_slots.assign(24,0xfff);
    for(size_t row=0;row<24;++row)if(!club.roster[row].empty()) {
        const auto& slot=club.roster[row];const auto& player=world.players.at(slot.player);
        const auto local=uint16_t(state.players.size());state.players.push_back(player.attributes);state.player_cards.push_back(player.cards);
        state.squad_slots[row]=uint16_t(local|((slot.flags&1)?0x8000:0)|((slot.flags&2)?0x4000:0)|((slot.flags&4)?0x2000:0)|((slot.flags&8)?0x1000:0));
    }
    return state;
}
}
ManagerTactics manager_tactics_from_legacy(const GameState& state,int manager) {
    if(manager<0 || manager>3 || state.tactics.size()<size_t(manager+1)*board_size ||
        state.manager_records.size()<size_t(manager+1)*record_size || state.manager_options.size()<size_t(manager+1)*5)
        throw std::invalid_argument("world12: missing initialized tactical manager fields");
    ManagerTactics result;result.automatic_weights=state.manager_records[size_t(manager)*record_size+0x288];
    result.automatic_formation=state.manager_options[size_t(manager)*5];
    for(size_t row=0;row<24;++row){const auto at=size_t(manager)*board_size+row*row_size;auto& r=result.rows[row];
        std::copy_n(state.tactics.begin()+at+2,4,r.orders.begin());r.partner=state.tactics[at+0x3f];r.depth=state.tactics[at+0x41];r.lane=state.tactics[at+0x42];}
    validate_manager_tactics(result);return result;
}
TacticalOutcome calculate_manager_tactical_action(const World& world,uint8_t seat,const TacticalAction& action,TacticalPolicy policy) {
    world.validate();if(policy.mode<1 || policy.mode>3)throw std::invalid_argument("world12: invalid tactical game mode");
    const auto manager=std::find_if(world.managers.begin(),world.managers.end(),[&](const auto& row){return row.seat==seat;});
    if(manager==world.managers.end() || !manager->tactics)throw std::invalid_argument("world12: manager tactics have not been initialized by the career owner");
    const auto& before=*manager->tactics;TacticalOutcome result{false,false,before};if(policy.frozen)return result;
    if(action.kind!=TacticalActionKind::weights) {
        if(action.row<0 || action.row>=24 || !action.player.valid() ||
            world.clubs.at(manager->club).roster[size_t(action.row)].player!=action.player)return result;
    }
    auto local=workspace(world,*manager,policy);bool allowed=false;
    switch(action.kind) {
    case TacticalActionKind::order:
        if(action.aspect<0 || action.aspect>3 || before.rows[size_t(action.row)].orders[size_t(action.aspect)]!=action.expected_order)return result;
        allowed=set_player_order_source(local,0,0,action.row,local.squad_slots[size_t(action.row)]&0xfff,action.aspect,action.value);break;
    case TacticalActionKind::move:
        allowed=move_pitch_player_source(local,0,0,action.row,local.squad_slots[size_t(action.row)]&0xfff,local.squad_slots[size_t(action.row)],
            action.old_depth,action.old_lane,action.depth,action.lane);break;
    case TacticalActionKind::weights:
        allowed=set_pitch_automatic_weight_source(local,0,0,action.aspect,action.old_weights,action.new_weights);break;
    default:throw std::invalid_argument("world12: unsupported tactical action");
    }
    if(allowed){result.allowed=true;result.tactics=manager_tactics_from_legacy(local,0);result.changed=result.tactics!=before;}
    return result;
}
}
