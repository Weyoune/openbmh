// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/world12/world.hpp"
#include "game/state.hpp"
namespace bmh::world12 {
// Extract only verified financial words, never club/player/name-pool references.
ManagerFinance manager_finance_from_legacy(const GameState&,int manager);
struct ManagerMoneyOutcome {
    bool allowed=false,changed=false;
    ManagerFinance finance;
};
// Difficulty and force/statistics are trusted engine policy supplied by the
// career owner, not values accepted from an unvalidated UI money proposal.
ManagerMoneyOutcome calculate_manager_booking(const ManagerFinance&,int difficulty,
    int32_t amount,bool force,bool statistics);
}
