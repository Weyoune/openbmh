// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/world12/world.hpp"
#include "game/state.hpp"
namespace bmh::world12 {
// Verified scalar/row fields only, never pointers, packed player IDs or an
// implicit conversion of the legacy manager/club ownership.
ManagerTactics manager_tactics_from_legacy(const GameState&,int manager);
enum class TacticalActionKind {order,move,weights,swap};
struct TacticalAction {
    TacticalActionKind kind=TacticalActionKind::order;
    int row=-1,aspect=-1,value=0;
    PlayerId player;
    int second_row=-1;RosterSlot first_slot,second_slot;
    uint8_t expected_order=0,old_weights=0,new_weights=0;
    int old_depth=0,old_lane=0,depth=0,lane=0;
};
// Session-owned rules. Not editable request fields from a frontend.
struct TacticalPolicy {int mode=3;bool indoor=false,frozen=false,foreign_rule_enabled=false;};
struct TacticalLineup {
    std::array<RosterSlot,24> roster;
    std::array<uint16_t,24> cards_by_row{};
};
struct TacticalOutcome {bool allowed=false,changed=false;ManagerTactics tactics;std::optional<TacticalLineup> lineup={};};
TacticalOutcome calculate_manager_tactical_action(const World&,uint8_t manager,
    const TacticalAction&,TacticalPolicy);
}
