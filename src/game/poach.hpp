// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Poaching ("Abwerben", transfer screen menu 17, H2 p. 70; BMH.exe V2.24 + supported rule profile patch):
// the manager picks a player of another club in the league tables and makes him an offer
// that is decided at once. Button 4 of the transfer screen Reset:
//
//   - at most five attempts per season: the players tried are kept in manager record
//     +0x8b6 (5 words, 0xffff free; cleared at the season start, season.cpp) - CareerSession_DispatchPoachUi_TextPointer2
//   - not with a transfer ban (record +0x220 >> 5 == 1, PushBack, CareerSession_DispatchPoachUi_TextPointer)
//   - not for a player on loan (BoughtEntry, Poach_StartPoach_TextPointer), not twice for the same player
//     (Poach_StartPoach_TextPointer2), not for a player on the transfer list (PlayerOnTransferListFirstPoolSlots, Poach_StartPoach_TextPointer3)
//   - the club must let him go (PoachAttemptsLeft): its squad must be big enough, the position
//     must not be short (and a club keeps its second goalkeeper); the asking percentage
//     follows how much the club needs him and how good he is compared with the team
//   - the offer uses one of the special list rows 40..43 (RowWhose: a row whose
//     OfferSlots word is free, else "Transfermarkt überlastet"), prepared like a market
//     row by Range2: asking percentage, demand W(80, 100), market value and salary.
//
// The offer itself is the ordinary offer editor (desk_actions.hpp, OpenTransferEditor) on that
// row with `poach` set; its submit (SubmitTransferBidSourceHumanBuyer) decides at once. finish_poach frees the
// row again and counts the attempt, as the original does when the editor returns.

#include <string>
#include <vector>

#include "game/random.hpp"
#include "game/state.hpp"

namespace bmh {

enum class PoachStatus : uint8_t {
    ready,              // the offer row is prepared: open the offer editor on it
    attempts_used,      // five attempts this season (CareerSession_DispatchPoachUi_TextPointer2)
    transfer_ban,       // the DFB has banned all transfers (CareerSession_DispatchPoachUi_TextPointer)
    invalid,            // no player, or a player of the own club
    on_loan,            // "Dieser Spieler ist nur ausgeliehen!" (Poach_StartPoach_TextPointer)
    tried_before,       // "Nur 1 Abwerbeversuch pro Spieler pro Saison!" (Poach_StartPoach_TextPointer2)
    on_transfer_list,   // Poach_StartPoach_TextPointer3
    club_refuses,       // PoachAttemptsLeft refused (its reason, then Poach_StartPoach_TextPointer5)
    market_busy,        // no free special row (Poach_StartPoach_TextPointer4); the attempt counts
};

struct PoachStart {
    PoachStatus status = PoachStatus::invalid;
    std::vector<std::string> messages;  // the original's message boxes, in order
    int manager = -1;
    int squad_slot = -1;  // absolute SquadSlots slot of the player at his club
    int player = -1;
    int club = -1;
    int offer_row = -1;   // k (0..3); the list row is 40 + k, its pool slot 0xb70 + 40 + k
    int asking_percent = 0;
};

// Attempts left this season (record +0x8b6).
int poach_attempts_left(const GameState& g, int manager);

// Button 4 after the player was picked (FriendlyMatchScreen returned `squad_slot`): the checks,
// PoachAttemptsLeft, RowWhose and Range2. Draws W(12, 20) (asking percentage), W(80,
// 100) (demand) and what the market value draws. `market_busy` counts the attempt itself.
PoachStart start_poach(GameState& g, int manager, int squad_slot, WatcomRandom& rand);

// When the offer editor has returned: the pool slot of the row is emptied, its OfferSlots
// word freed and the attempt recorded in +0x8b6.
void finish_poach(GameState& g, const PoachStart& poach);

}  // namespace bmh
