// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <functional>
#include <string>

#include "game/club_player_detail.hpp"
#include "game/transfer_allocator.hpp"
#include "game/weekly.hpp"

namespace bmh {

// Explicit manual DispatchClubPlayerPendingOffers answers; absence never means NEIN.
enum class PendingOfferAnswer { yes, no, unsupported };
enum class PendingOfferQuestionKind { salary_demand, severance };
struct PendingOfferQuestion {
    PendingOfferQuestionKind kind = PendingOfferQuestionKind::salary_demand;
    int manager = -1;
    int raw_offer_slot = -1;
    int offered_player = -1;
    int clicked_player = -1; // original identity, never remapped to offered_player
    int clicked_source_slot = -1; // fixed throughout this DispatchClubPlayerPendingOffers invocation
    std::string current_clicked_name; // PlayerName reads this fixed slot at each question
    uint32_t amount = 0;
    uint16_t source_flags = 1; // PresentManagerDialog: salary1, severance0x305
    std::string original_message; // Original-encoded text, not UTF-8
};

enum class PendingOfferTransferChoiceKind { bid, decline, unsupported };
struct PendingOfferTransferChoice {
    PendingOfferTransferChoiceKind kind = PendingOfferTransferChoiceKind::unsupported;
    int raw_bid_slot = -1;
};
struct PendingOfferNotice {
    int manager = -1;
    int raw_offer_slot = -1;
    uint32_t text_address = 0;
    std::string original_message;
};
// PendingOfferAnswer reads the unsaved EXE byte MatchEventsDisabled. The caller must copy its observed
// value into this per-dispatch context; neither an absent save byte nor a
// default value is evidence that the allocator is enabled.
using PendingOfferAllocatorGate = TransferAllocatorContext;
struct PendingOfferHooks {
    std::function<PendingOfferAnswer(const GameState&, const PendingOfferQuestion&)> answer;
    std::function<PendingOfferTransferChoice(const GameState&, const TransferOfferListView&)> select_transfer;
    // Read-only owned notices/ACKs; installing it changes neither bytes nor RNG.
    std::function<void(const GameState&, const PendingOfferNotice&)> notice;
    PendingOfferAllocatorGate human_buyer_allocator_gate = PendingOfferAllocatorGate::unsupported;
    // PresentLoanReturn is a full-screen diagnostic, distinct from SystemIdentity notices.
    // Returning acknowledges it; throwing leaves App retryable.
    std::function<void(const GameState&, uint16_t)> program_error;
};
struct PendingOfferResult {
    bool completed = false;
    int processed = 0;
    int salary_accepted = 0;
    int transferred = 0;
    int buyer_funds_refused = 0;
    int buyer_allocator_blocked = 0;
    int buyer_no_room = 0;
    bool forced_acceptance = false; // sticky local18 from DispatchClubPlayerPendingOffers
};

// AdvanceTurn's post-desk caller has transient state which is not saved in FBM. The
// App must own and explicitly identify the supported interactive mode; the
// default/unsupported value never means that missing transient bytes were zero.
enum class PendingOfferDeskRuntimeMode { unsupported, normal_interactive };
struct PendingOfferDeskRequest {
    PendingOfferDeskRuntimeMode runtime_mode = PendingOfferDeskRuntimeMode::unsupported;
    int manager_count = -1;    // caller's validated desk-loop count
    int manager_position = -1; // current position in the saved NextDeskManager permutation
};

// ClubPlayerDetailFromGame's ordinary pending action -> DispatchClubPlayerPendingOffers, not the legacy answer_offers.
// Resolve the clicked slot/board once; execute every active-owner raw offer0..3.
// Intended for a staged GameState + both RNGs owned by the caller: an unsupported
// hook/path reports failure and may follow earlier batch effects, so discard the
// staged state/RNGs on report.unsupported. A distinct human buyer is supported only
// for ordinary sales and an explicitly supplied unsaved PendingOfferAnswer gate. Funds refusal,
// a gate-suppressed allocator and a full buyer squad are expected Source partial
// outcomes; their counters distinguish them from an actual transfer. Loans and
// unported error30/31/32 boundaries remain unsupported. No legacy weekly caller changes.
PendingOfferResult process_player_card_pending_offers(GameState&, const ClubPlayerDetailSelection&,
    GameRandom&, WatcomRandom&, const PendingOfferHooks&, MinuteReport&);

// AdvanceTurn post-desk adapter. It validates native manager
// count/order/cursor, restores ActiveManager from NextDeskManager, anchors the DispatchClubPlayerPendingOffers batch at
// the first raw owned offer whose player exists in an ordinary owned squad slot,
// and preserves offers for other managers. Missing offered players require the
// explicit PresentLoanReturn presenter, then scanning continues in raw order. A genuine
// national takeover uses its current national squad for owner0 and the retained
// raw club record for an ordered owner1..3; the active manager list stays reduced.
// DispatchClubPlayerPendingOffers keeps the first resolved anchor board for all salary questions.
// Verified old-club offers missing from the national squad retain M121's quiet
// cleanup improvement. A present national player follows Source's actual salary
// and transfer path, including its national-board quirk. On the last desk
// only, it performs Source's global four-entry pool/offer cleanup and sets DeskPosition
// to the completed manager count before the caller starts simulation. As with the
// manual batch API, callers must stage GameState and both RNGs and discard on
// unsupported; no weekly or replay wrapper behavior changes.
PendingOfferResult process_natural_pending_offers_after_desk(GameState&,
    const PendingOfferDeskRequest&, GameRandom&, WatcomRandom&,
    const PendingOfferHooks&, MinuteReport&);

// AdvanceTurn's reduced-count national nomination desk has a distinct post-desk
// owner rule: it restores NextDeskManager[0] even though the active national runtime has
// one manager. Validate the precise 4006 calendar/competition identity, then
// reuse the post-desk AdvanceTurn/DispatchClubPlayerPendingOffers sequence above, including owned offers.
// The saved order prefix is preserved and bounded by the saved original count,
// rather than interpreted as a count-one permutation. Call on staged state and
// both RNGs; unavailable/declined presenters require complete caller rollback.
PendingOfferResult process_national_nomination_pending_after_desk(GameState&,
    const PendingOfferDeskRequest&, GameRandom&, WatcomRandom&,
    const PendingOfferHooks&, MinuteReport&);

} // namespace bmh
