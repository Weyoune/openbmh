// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/world12/manager_finance.hpp"
#include "game/world12/manager_tactics.hpp"
namespace bmh::app::world12 {
// Runtime session/view ownership, not persisted as part of a source data pack.
struct ManagerActionOwner {uint64_t scope=0,revision=0;};
struct ManagerActionIdentity {
    uint8_t seat=0;bmh::world12::ClubId club;
    uint64_t scope=0,revision=0;
};
bmh::world12::ManagerMoneyOutcome book_manager_funds(bmh::world12::World&,ManagerActionOwner&,
    const ManagerActionIdentity&,int difficulty,int32_t amount,bool force,bool statistics);
bmh::world12::TacticalOutcome apply_manager_tactical_action(bmh::world12::World&,ManagerActionOwner&,
    const ManagerActionIdentity&,const bmh::world12::TacticalAction&,bmh::world12::TacticalPolicy);
}
