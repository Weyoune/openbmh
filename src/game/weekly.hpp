// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// OpenBMH deviates from the original: uses the supported weekly processing and transfer rule profile.
// The weekly processing (DayStepRest3, BMH.exe V2.24 + supported rule profile patch).
//
// DayStepRest runs it on the day after a training day when that day has weekday code 3
// (in games with the full economy, CompetitionSelection bit 0). In order:
//   1. per human manager: offers and incidents (Bool), the ageing of the club's
//      facilities (FacilityDamage) and random damage to them (SeasonTicketCompensation), each followed
//      by the season ticket compensation (Void3); every third week new advertising
//      offers (TransferListCountdown2)
//   2. the transfer list (TransferListCountdown(0)): the days left of every listed player count
//      down; at 0 the highest-rated eligible bid wins unless a human-seller selector is set
//   3. new bids for listed players (TransferBid)
//   4. every player in a squad: injuries in training, players out (w35c bits 27-31) come
//      back or retire
//   5. per human manager: debts, the training camp bill, player bonuses, the weekly
//      statistics (FacilityRating)
// Messages to the managers (PostMessage / SystemIdentity) are not produced; dialogs that
// wait for a decision of the player are reported as unsupported.

#include "game/match_engine.hpp"
#include "game/random.hpp"
#include "game/state.hpp"
#include "game/transfer_allocator.hpp"
#include "game/transfer_offer_view.hpp"

#include <array>
#include <cstdint>
#include <functional>

namespace bmh {

// Projects the current live rows without mutation or RNG. The selector caller
// performs the Source-ranked selection and ProjectTransferOfferList post-selection check.
TransferOfferListView project_transfer_offer_list(const GameState& g, int list_entry);

void weekly_processing(GameState& g, GameRandom& game, WatcomRandom& rand, MinuteReport& report,
                       TransferAllocatorContext allocator = TransferAllocatorContext::unsupported);

// The single steps, public for tests.
void manager_offers(GameState& g, int manager, WatcomRandom& rand, MinuteReport& report);       // Bool
void facility_ageing(GameState& g, int manager, GameRandom& game, WatcomRandom& rand, MinuteReport& report);  // FacilityDamage
void facility_damage(GameState& g, int manager, GameRandom& game, WatcomRandom& rand);          // SeasonTicketCompensation
void season_ticket_compensation(GameState& g, int manager);                                     // Void3
struct SeasonTicketCompensationNotice {
    int manager = -1;
    int missing_tickets = 0;
    int usable_places = 0;
    int remaining_weeks = 0;
    int32_t signed_amount = 0;
};
using SeasonTicketCompensationNoticeHook =
    std::function<void(const GameState&, const SeasonTicketCompensationNotice&)>;
// Source-visible compensation path: the notice receives the state after the
// usable-capacity calculation, but before the forced/statistical money booking.
void season_ticket_compensation_source(GameState& g, int manager,
                                      const SeasonTicketCompensationNoticeHook& notice = {});
void advertising_offers(GameState& g, int manager, WatcomRandom& rand, MinuteReport& report);   // TransferListCountdown2
// TransferListCountdown(all): the days left of every listed player count down; at 0 the best bid wins
// (with `all`, at the season end, every list is settled at once)
void transfer_list_countdown(GameState& g, GameRandom& game, WatcomRandom& rand, MinuteReport& report, bool all = false,
                             TransferAllocatorContext allocator = TransferAllocatorContext::unsupported);
// TransferBid(slot, list entry, 0, -1): a new bid of some club for the player in the
// absolute squad slot (a listed player), or with list_entry 0 a move of the player to a
// club that wants him.
void transfer_bid(GameState& g, int slot, uint32_t list_entry, bool high, int months, GameRandom& game,
                  WatcomRandom& rand, MinuteReport& report);
void weekly_statistics(GameState& g, int manager);                                              // FacilityRating

// FacilityRating2(manager, with_rand, &second): rating of a human club's stadium facilities
// (about 24-100). *second receives the covered share of the stands in percent. With `rand`
// (the attendance calculation) every stand under construction costs 10-12 % of the
// facility sum, drawn per stand.
int facility_rating(GameState& g, int manager, int* second, WatcomRandom* rand = nullptr);

// Squad helpers shared with the month change (month.cpp):
// Nationality: nationality of a player (w368 bits 27-31, +32 with the flag in *PlayerBonuses).
int nationality(const GameState& g, int player);
// SquadCount(team, flags): players of a club (bit 1: the injured too; bit 0: plus the
// players bought for later, table Career_RemoveJoinLoans_RuntimeField).
int squad_count(const GameState& g, int team, int flags);
// SlotTeam: club of an absolute squad slot, -1 for the transfer pool.
int slot_team(int slot);
// Int2(team, 100) after a transfer (only clubs with a squad).
void lineup_after_transfer(GameState& g, int team);
// ReleasePlayer(seller, player): the player leaves the club (0xFFF: the pool).
bool remove_from_seller(GameState& g, int seller, int player, MinuteReport& report);
// Finalize a high-confidence manager offer immediately after the seller/player accepts it.
bool accept_transfer_bid_immediately(GameState& g, int list_entry, int bid, WatcomRandom& rand,
                                     MinuteReport& report,
                                     TransferAllocatorContext allocator = TransferAllocatorContext::unsupported);

}  // namespace bmh

namespace bmh {

// PreMatchOffer, before a human manager's matchday: now and then a club makes an offer for
// one of his players (1 in 71 - manager rating / 50).
// Returns whether an offer was shown (its message box waits for a click before the desk).
bool pre_match_offer(GameState& g, int manager, GameRandom& game, WatcomRandom& rand, MinuteReport& report);

// ClubAttraction(team, 0): how attractive a club is for advertisers (0-100): its European
// campaign, its league and table position (weekly.cpp).
int club_attraction(const GameState& g, int team);

}  // namespace bmh
