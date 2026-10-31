// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/world12/world.hpp"
#include "game/state.hpp"
namespace bmh::world12 {
// Verified scalar/row fields only, never pointers, packed player IDs or an
// implicit conversion of the legacy manager/club ownership.
ManagerTactics manager_tactics_from_legacy(const GameState&,int manager);
enum class TacticalActionKind {order,move,weights};
struct TacticalAction {
    TacticalActionKind kind=TacticalActionKind::order;
    int row=-1,aspect=-1,value=0;
    PlayerId player;
    uint8_t expected_order=0,old_weights=0,new_weights=0;
    int old_depth=0,old_lane=0,depth=0,lane=0;
};
// Session-owned rules. Not editable request fields from a frontend.
struct TacticalPolicy {int mode=3;bool indoor=false,frozen=false;};
struct TacticalOutcome {bool allowed=false,changed=false;ManagerTactics tactics;};
TacticalOutcome calculate_manager_tactical_action(const World&,uint8_t manager,
    const TacticalAction&,TacticalPolicy);
}
