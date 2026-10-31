// SPDX-License-Identifier: GPL-3.0-only
#include "app/world12_manager.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>
namespace bmh::app::world12 {
static_assert(std::is_nothrow_copy_assignable_v<std::optional<bmh::world12::ManagerFinance>>);
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
