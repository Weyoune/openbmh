// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#pragma once
// OpenBMH deviates from the original: retains the enhanced academy player-generation strength bands with monthly youth budgets.
// From one season to the next (BMH.exe V2.24 + supported rule profile patch).
//
// On 1 July the "Weiter" loop (DailyInterest) ends the season: CareerSession_RuntimeError_RuntimeField4 = 1, the season
// bonuses (SeasonAwards) and the manager's desk "Saisonende". When he leaves it with
// "Anstoss" the offers still open for his players are answered (DispatchClubPlayerPendingOffers, from
// AdvanceTurn) and the new season is set up (the CareerSession_RuntimeError_RuntimeField4 branch of DailyInterest):
//   season + 1 (SeasonIndex), the clubs' moods and flags (ClubBalance), the European
//   cups and their foreign clubs (CareerSeasonSetup(1)), contracts (GameEndTaxAnimationScreen, Void),
//   the league tables to the cups (CareerSeasonSetup(2)), the German clubs in Europe shown
//   (SeasonOperation), the national cup draw (Getenv), promotion and relegation
//   (CareerSeasonSetup3, SeasonEndSteps), the managers' season records (ManagerOf), the day steps
//   up to the first Wednesday and the new transfer list (CareerFirstSteps); the season day
//   DaysIntoSeason restarts at 0.
// Annual accounts expose the Original acknowledgement/decision boundary before
// booking tax. Headless callers without a selector use the Original NO branch.

#include <functional>
#include <array>
#include <vector>

#include "game/match_engine.hpp"
#include "game/random.hpp"
#include "game/state.hpp"
#include "game/transfer_allocator.hpp"

namespace bmh {

enum class SeasonTaxEventKind { notice, question, animation, outcome };
struct SeasonTaxEvent {
    SeasonTaxEventKind kind = SeasonTaxEventKind::notice;
    int manager_slot = -1;
    int team = -1;
    int32_t profit = 0;
    int32_t legal_tax = 0;
    int32_t paid_tax = 0;
    int32_t fine = 0;
    int animation_variant = 0;
    bool caught = false;
    std::string text; // owned Original-encoding text, prepared by the accounting engine
};
struct SeasonTaxHooks {
    std::function<void(const GameState&, const SeasonTaxEvent&)> observe;
    std::function<bool(const GameState&, const SeasonTaxEvent&)> choose;
};
// SeasonProfitTax continues GameEndTaxAnimationScreen's loop without reinitialising its local fine.
// This state lives for one annual-accounts pass across all human managers.
struct SeasonTaxState {
    int32_t carried_fine = 0;
};

// CupDrawFromGame: the generated order, then Getenv/CupGroupOverviewEffects.
// Participants are owned copies of the current round at each boundary.
enum class SeasonCupDrawPhase { draw, overview };
struct SeasonCupDrawEvent {
    SeasonCupDrawPhase phase = SeasonCupDrawPhase::draw;
    uint32_t entry_address = ::bmh::layout::CupTable;
    int competition = 0;
    int round = 0;
    int offset = 0;
    uint8_t flags = 0;
    bool group_stage = false;
    int group_index = 0;
    int group_count = 0;
    size_t count = 0;
    std::vector<uint16_t> participants;
    struct GroupAverageSnapshot {
        uint16_t runtime_id = 0;
        std::array<int, 5> values{};
    };
    // Void6 calls TeamRating for owned foreign teams at the live Source
    // boundary. The overview owns those values so a renderer never rerolls.
    std::vector<GroupAverageSnapshot> group_average_snapshots;
};
using SeasonCupDrawHook = std::function<void(const GameState&, const SeasonCupDrawEvent&)>;

// DispatchClubPlayerPendingOffers (the loop at the end of AdvanceTurn): the offers for players of human
// manager `manager` (OfferSlots) are answered. A salary demand (a club tries to lure the
// player away) is accepted with `accept`: the board salary becomes the offered one and
// its percentage of the market salary is noted (+0xd). Declining it needs the transfer
// (IncomingTransferScreen) and is reported. A fee offer opens the negotiation screen (IncomingTransferScreen /
// OpenTransferEditor), which restarts the game generator from CareerSession_Invalid_RuntimeField2 and draws the
// negotiator's picture; with `game` the manager refuses the release (right click, the
// message box "Sie verweigern dem Spieler die Freigabe!"). A transfer_decision callback
// may accept a fee offer. Interactive Main uses process_natural_pending_offers_after_desk
// with its typed presenters and shared human-buyer settlement instead of this legacy API.
// (The offer slots are freed by free_offer_slots after all desks.)
void answer_offers(GameState& g, int manager, bool accept, WatcomRandom& rand, MinuteReport& report,
                   GameRandom* game = nullptr);
// The end of the desk loop of AdvanceTurn, after every manager's desk and answers: the four
// offer slots (transfer list entries 0x28..0x2b, OfferSlots) are freed.
void free_offer_slots(GameState& g);

// OpenBMH deviates from the original: retains the enhanced academy player-generation strength bands with monthly youth budgets.
// Annual recruitment with an active middle youth budget (or a paid legacy
// academy contract), provided the squad has room. Shared player strength bands
// remain in effect; see season.cpp.
bool youth_academy_player(GameState& g, int m, GameRandom& game, WatcomRandom& rand, MinuteReport& report);

// ClubBalance(0): every club's mood (TeamMorale, W(20, 30)) and flags (*TeamFlags): a random
// base, two levels from the final table position (title holders get the top one) with
// random exceptions, and two from the balance of its squad (CareerSeasonSetup4).
// CareerSeasonSetup4(team): bits 8-11 of the club flags from the balance of its squad (W draws).
void club_balance(GameState& g, int team, WatcomRandom& rand);

// The end of InitializeCareer (a new career, after the managers): the national team flag, every
// league club's balance (CareerSeasonSetup4) and automatic line-up (humans with the bit 0x80),
// the title holders of the cups, CareerSeasonSetup(3) (international tournament, European
// entrants, national cup field), the season tickets (CareerSeasonSetup2), Getenv(0x40, 0) and
// the players' season (CareerSeasonSetup3).
// `pause` is called at the screens the original waits at (as in season_transition).
bool career_season_setup(GameState& g, GameRandom& game, WatcomRandom& rand, MinuteReport& report,
                         const std::function<void(const char*)>& pause = nullptr,
                         const SeasonCupDrawHook& cup_draw = {});

// CareerFirstSteps2 after InitializeCareer and the start of DailyInterest(0): the game generator's seed is
// saved (CareerSession_Invalid_RuntimeField2), the clubs of each league change places at random (Getenv2) and the
// transfer list gets its first players (CareerFirstSteps). The "Weiter" loop then prepares the
// first event (AdvanceTurn) up to the first desk.
bool career_first_steps(GameState& g, GameRandom& game, WatcomRandom& rand, MinuteReport& report);

// ClubsChangePlaces(a, b): two clubs (ids below 168) change places in every table per club id.
void clubs_change_places(GameState& g, int a, int b);

// With `new_career` (ClubBalance(1), start of a career) both levels are 1 for every club and
// the position draws are skipped.
void reset_club_moods(GameState& g, GameRandom& game, WatcomRandom& rand, bool new_career = false);

// The CareerSession_RuntimeError_RuntimeField4 branch of DailyInterest (see above). Missing runtime context or
// invalid state is reported through MinuteReport; presenters are supplied by the caller.
// `pause` is called where the original waits for the manager (a screen): week_check compares
// the recorded pauses there.
enum class SeasonTransitionOutcome { NextSeason, CareerComplete, Failed };
// Void -> SeasonPromotionScreen: already computed local lists, before swaps.
// The arrays are owned; only their first `count` entries are displayed/swapped.
// In group 1 without play-offs, count may be smaller than relegated.size().
struct SeasonPromotionEvent {
    int group = 0; // 0..3: first division, second, both third divisions, fourth
    size_t count = 0;
    std::vector<uint16_t> relegated;
    std::vector<uint16_t> promoted;
};
using SeasonPromotionHook = std::function<void(const GameState&, const SeasonPromotionEvent&)>;
using SeasonHighscoreHook = std::function<void(const GameState&, bool display)>;

SeasonTransitionOutcome season_transition(GameState& g, GameRandom& game, WatcomRandom& rand, MinuteReport& report,
                       const std::function<void(const char*)>& pause = {},
                       const SeasonHighscoreHook& highscore = {},
                       const SeasonTaxHooks& tax_hooks = {},
                       const SeasonPromotionHook& promotion_hook = {},
                       const SeasonCupDrawHook& cup_draw = {},
                       TransferAllocatorContext allocator = TransferAllocatorContext::unsupported);

// GameEndTaxAnimationScreen/SeasonProfitTax profit branch: show ACK, ask JA/NEIN, perform its exact RNG
// and sanctions, then store the carry values and debit tax plus any fine once.
// The caller supplies the already rounded seasonal profit, before ticket income.
void season_apply_profit_tax(GameState& g, int manager, int32_t profit,
                             GameRandom& game, WatcomRandom& rand,
                             const SeasonTaxHooks& hooks = {},
                             SeasonTaxState* continuation = nullptr);

// GameEndTaxAnimationScreen / SeasonProfitTax: progressive tax on a rounded seasonal profit.
// Exposed so the threshold and integer-rounding rules can be tested directly.
int32_t season_profit_tax(int32_t profit);
// GameEndTaxAnimationScreen: next-season tax refund after a lower profit; returns zero when
// the original conditions do not grant a refund.
int32_t season_profit_tax_refund(int32_t profit, int32_t previous_taxable_profit,
                                 int32_t previous_tax_paid);

// SeasonAwards, on 1 July before the "Saisonende" desk: per league (< 5) the club of the
// top scorer (goals), of the best scorer (goals + assists, list Season_List_RuntimeField) and the fair
// play winner (fewest card points); a human manager's club gets the prize (SeasonRuntimeField26)
// with a message.
void season_awards(GameState& g);

// The side effects of the cup overview screen CupGroupOverviewEffects (otherwise display only): the
// running competitions' slot 4 (this year's international tournament), the manager's screen
// state and the friendlies 0xc while their entry 30 has teams.
void cup_overview_effects(GameState& g);
// CupGroupOverviewEffects's group cursor is owned by this stage entry; the national cup's
// existing overview path intentionally keeps its original effects unchanged.
void cup_group_overview_effects(GameState& g, int group_index);

// LeagueName: the name of a league below 5 (with its letter, e.g. for the third divisions).
std::string league_name(const GameState& g, int league);

// MonthChange(absolute squad slot): the player leaves his club for the free agents with new
// values (W and game draws) and an age W(4, 15); his club closes the gap (ReleasePlayer,
// with a human club its tactics board as well). False if the source ownership/state
// cannot be validated; the player movement and human board compaction are implemented.
bool release_player(GameState& g, int slot, GameRandom& game, WatcomRandom& rand, MinuteReport& report);

}  // namespace bmh
