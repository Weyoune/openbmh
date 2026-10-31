// SPDX-License-Identifier: GPL-3.0-only
#include "app/world12_manager.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>
namespace bmh::app::world12 {
static_assert(std::is_nothrow_move_assignable_v<bmh::world12::World>);
static_assert(std::is_nothrow_copy_assignable_v<std::optional<bmh::world12::ManagerFinance>>);
static_assert(std::is_nothrow_copy_assignable_v<std::optional<bmh::world12::ManagerTactics>>);
bmh::world12::TacticalOutcome apply_manager_tactical_action(bmh::world12::World& world,ManagerActionOwner& owner,
    const ManagerActionIdentity& identity,const bmh::world12::TacticalAction& action,bmh::world12::TacticalPolicy policy) {
    if(!owner.scope || identity.scope!=owner.scope || identity.revision!=owner.revision || owner.revision==std::numeric_limits<uint64_t>::max())
        throw std::invalid_argument("world12: stale tactical manager action");
    world.validate();
    const auto manager=std::find_if(world.managers.begin(),world.managers.end(),[&](const auto& row){return row.seat==identity.seat;});
    if(manager==world.managers.end() || manager->club!=identity.club)throw std::invalid_argument("world12: tactical action belongs to another manager/club");
    const auto result=bmh::world12::calculate_manager_tactical_action(world,identity.seat,action,policy);
    if(result.changed) {
        if(result.lineup) {
            auto candidate=world;
            const auto index=size_t(manager-world.managers.begin());candidate.managers[index].tactics=result.tactics;
            candidate.clubs.at(identity.club).roster=result.lineup->roster;
            for(size_t row=0;row<24;++row)if(!result.lineup->roster[row].empty())
                candidate.players.at(result.lineup->roster[row].player).cards=result.lineup->cards_by_row[row];
            candidate.validate();world=std::move(candidate);
        }else manager->tactics=result.tactics;
        ++owner.revision;
    }
    return result;
}
bmh::world12::ManagerMoneyOutcome book_manager_funds(bmh::world12::World& world,ManagerActionOwner& owner,
    const ManagerActionIdentity& identity,int difficulty,int32_t amount,bool force,bool statistics) {
    if(!owner.scope||identity.scope!=owner.scope||identity.revision!=owner.revision||owner.revision==std::numeric_limits<uint64_t>::max())
        throw std::invalid_argument("world12: stale financial manager action");
    world.validate();
    const auto manager=std::find_if(world.managers.begin(),world.managers.end(),[&](const auto& row){return row.seat==identity.seat;});
    if(manager==world.managers.end()||manager->club!=identity.club)throw std::invalid_argument("world12: financial action belongs to another manager/club");
    if(!manager->finance)throw std::invalid_argument("world12: manager finances have not been initialized by the career owner");
    const auto result=bmh::world12::calculate_manager_booking(*manager->finance,difficulty,amount,force,statistics);
    if(result.changed){manager->finance=result.finance;++owner.revision;}
    return result;
}
}
