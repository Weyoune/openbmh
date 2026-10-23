// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/month.hpp"
// The month change (DayStepRest2), run by the day step (DayStepRest) of the first day of
// a month in the full game (CompetitionSelection bit 0), after the training / weekly processing:
//
//   1. advertising contracts of the human managers count down (record +0xa34, 18 x 0xC)
//   2. new players on the transfer list (CareerFirstSteps): free agents of the pool and players
//      the computer clubs want to sell
//   3. loaned players whose time is up return to their clubs (table Career_RemoveJoinLoans_RuntimeField)
//   4. per human manager: the monthly ratings (ManagerMatchRecord2), expenses minus income, and the
//      monthly bookkeeping of every squad player on the tactics board (contract months,
//      mood, value history, retirement wishes)
//   5. share prices grow, dividends and loans between managers are booked (MonthChange2)
//
// Monthly bookkeeping uses Watcom draws; player moves also require the game RNG.
// Human decisions use caller-supplied contract/loan presenters. Missing context
// and invalid state report through MinuteReport, rather than guessing a decision.

#include <array>
#include <cstdint>

#include "game/match_engine.hpp"
#include "game/random.hpp"
#include "modern/game/modern_state.hpp"

namespace bmh::modern {

// `game` is needed when a player leaves at the end of his contract (MonthChange).
void month_change(ModernState& g, WatcomRandom& rand, MinuteReport& report, GameRandom* game = nullptr);

// CareerFirstSteps: new players on the transfer list (part of the month change; also at the
// start of a season).
void new_transfer_list_entries(ModernState& g, WatcomRandom& rand, MinuteReport& report);

// AddToSquad(slot, loan, percent, &fee, &salary, years, extra): market value (fee) and
// salary of the player in an absolute squad slot, from strength, age, position and
// experience; `percent` scales the fee, `years` - 1 steps of expected development raise
// it, `extra` * (11 - age) percent change the salary. A fee below 50000 is replaced by W(50000,
// 80000); a loan (1) costs a quarter.
void market_value(const ModernState& g, int slot, int loan, int percent, uint32_t& fee, uint32_t& salary, int years,
                  int extra, WatcomRandom& rand);

// ContractBonuses, the salary page of the contract talks (contracts screen InstallDefaultHooks): the
// four demands for 1-4 years when the talks open, AddToSquad with the level + 100 percent
// and the salary rise for the years beyond the current contract, times the player's mood
// (+0xd) / 100. Opened from the desk (`from_desk`, param_5 0x80) a player of 36 or more
// at the end of the term, or one flagged 0x20 / 0x40, wants no contract (no-contract sentinel).
std::array<uint32_t, 4> contract_demands(ModernState& g, int manager, int row, bool from_desk, WatcomRandom& rand);

// ContractBonuses "OK" with an offer for `column` + 1 years against that column's demand: W(8, 12)
// (loyalty from the months at the club, +0x11) and - unless the player asked himself
// (`request`, param_5 0x40) - W(80, 100). The offer plus 80 % of the expected bonus
// (ContractOffer) must reach that percentage of the demand, and the offer itself 61 %.
// Accepted: salary, term and mood (+0x50, +0xc, +0xd) change, AddToSquad for the term.
// Rejected: the player is offended (+0x4f bit 7, mood + 5); for the row whose contract
// ended (`screen_row`) he will not talk again (+0xb bit 6). Returns whether he accepted.
bool contract_offer(ModernState& g, int manager, int row, int column, uint32_t offer, uint32_t demand, bool request,
                    bool screen_row, WatcomRandom& rand,
                    const std::array<uint8_t, 7>* proposed_bonuses = nullptr);

// ContractBonuses "Prämien" page. `row == -1` edits the squad's shared proposal; each
// stored row is raised to the requested value. A bonus-only submission is accepted only
// when it does not lower any player's current rate, and must change at least one rate.
bool contract_bonuses(ModernState& g, int manager, int row, const std::array<uint8_t, 7>& rates);


// ContractBonuses insurance button: one-time per player, duration 1..60 months, and insurance
// category bound to the selected 1..4 year term. This action consumes no RNG.
ContractInsuranceResult contract_insurance(ModernState& g, int manager, int row, int insurance_type,
                                           uint8_t months);

// SearchProfileNews(manager, list entry, player, 1): tells a human manager about a new player on
// the transfer list that matches his search profile (record +0x1f6 .. +0x207).
void search_profile_news(ModernState& g, int manager, uint32_t list, int player);

// Put an existing squad slot on the shared transfer list (human desk action).
bool list_transfer_player(ModernState& g, int slot, WatcomRandom& rand, MinuteReport& report);

}  // namespace bmh::modern
