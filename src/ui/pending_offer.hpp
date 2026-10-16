// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <string_view>

#include "formats/le.hpp"
#include "game/pending_offer.hpp"
#include "game/desk_actions.hpp"
#include "game/weekly.hpp"
#include "ui/game_end.hpp"
#include "ui/incoming_transfer.hpp"

namespace bmh::ui {

// Pure Source presenters for the manual DispatchClubPlayerPendingOffers hooks. Text remains in the
// game's original ISO646-DE encoding until the renderer draws it.
// Includes the exact restored post-national-desk owner from retained records;
// it never inserts that owner into the active runtime manager list.
int pending_offer_owner_team(const GameState&, int manager);
bool pending_offer_spare_buyer(const GameState&, int raw_offer_slot, int expected_team);
TaxDialogProjection pending_offer_question_dialog(const GameState&,
                                                   const PendingOfferQuestion&);
TaxDialogProjection loan_return_question_dialog(const GameState&, int manager,
                                                std::string_view original_message);
TaxDialogProjection advertising_renewal_question_dialog(const GameState&, int manager,
                                                        const AdvertisingRenewal&);
TaxDialogProjection advertising_renewal_notice_dialog(const GameState&, int manager,
                                                      const AdvertisingRenewal&);
TaxDialogProjection pending_offer_notice_dialog(const GameState&,
                                                 const PendingOfferNotice&);
TaxDialogProjection transfer_last_week_dialog(const GameState&, const TransferDecisionEvent&);
// Pure Source UI adapter for the stadium payment's nested sponsor/funds notices.
// Composed bodies retain Original ISO646-DE bytes for BMH3 drawing.
TaxDialogProjection construction_notice_dialog(const GameState&, const LeImage&,
                                                const ConstructionNotice&);
TaxDialogProjection season_ticket_compensation_notice_dialog(
    const GameState&, const LeImage&, const SeasonTicketCompensationNotice&);
IncomingTransferDecisionState pending_offer_transfer_decision_from_game(
    const GameState&, const TransferOfferListView&);

} // namespace bmh::ui
