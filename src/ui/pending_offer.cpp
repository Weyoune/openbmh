// SPDX-License-Identifier: GPL-3.0-only
#include "game/state.hpp"
#include "formats/badge_policy.hpp"
#include "formats/executable_layout.hpp"
#include "ui/pending_offer.hpp"

#include <algorithm>

#include "formats/binary.hpp"
#include "game/messages.hpp"
#include "game/state.hpp"

namespace bmh::ui {
namespace {
constexpr size_t kManagerRecord = 0xb0c;

} // namespace

int pending_offer_owner_team(const GameState& game, int manager) {
    if (manager >= 0 && size_t(manager) < game.manager_teams.size())
        return game.manager_teams[size_t(manager)];
    // AdvanceTurn restores NextDeskManager[0] after the temporary one-manager national
    // desk. Records1..3 still exist even though they have no runtime desk.
    const bool retained = manager > 0 && manager < 4 &&
        game.manager_teams.size() == 1 && game.manager_teams[0] == kNoTeam &&
        game.memory.has(::bmh::layout::CoachActive) && game.memory.u8(::bmh::layout::CoachActive) != 0 &&
        game.memory.has(::bmh::layout::ManagerCount) && game.memory.u8(::bmh::layout::ManagerCount) == 1 &&
        game.memory.has(::bmh::layout::OriginalManagerCount) && game.memory.u8(::bmh::layout::OriginalManagerCount) <= 4 &&
        manager < game.memory.u8(::bmh::layout::OriginalManagerCount) &&
        game.memory.has(::bmh::layout::PendingOffer_PendingOfferOwnerTeam_RuntimeField) && game.memory.u16(::bmh::layout::NextDeskManager) == manager &&
        game.memory.has(::bmh::layout::DeskPosition) && game.memory.u8(::bmh::layout::DeskPosition) == 0 &&
        game.memory.has(::bmh::layout::ActiveManager) && game.memory.u8(::bmh::layout::ActiveManager) == manager;
    if (!retained) throw FormatError("pending offer UI: invalid Source manager");
    const size_t base = size_t(manager) * kManagerRecord;
    if (base + 0x292 > game.manager_records.size())
        throw FormatError("pending offer UI: missing retained Source club");
    const int team = int(game.manager_records[base + 0x290]) |
        (int(game.manager_records[base + 0x291]) << 8);
    if (team >= runtime_dimensions.league_teams) throw FormatError("pending offer UI: invalid retained Source club");
    return team;
}

bool pending_offer_spare_buyer(const GameState& game, int raw_offer_slot, int expected_team) {
    if (raw_offer_slot < 0 || raw_offer_slot >= 4 ||
        game.manager_teams.size() != 1 || game.manager_teams[0] != kNoTeam ||
        !game.memory.has(::bmh::layout::CoachActive) || game.memory.u8(::bmh::layout::CoachActive) == 0 ||
        !game.memory.has(::bmh::layout::ManagerCount) || game.memory.u8(::bmh::layout::ManagerCount) != 1 ||
        !game.memory.has(::bmh::layout::OriginalManagerCount) || game.memory.u8(::bmh::layout::OriginalManagerCount) < 1 || game.memory.u8(::bmh::layout::OriginalManagerCount) > 4 ||
        !game.memory.has(::bmh::layout::PendingOffer_PendingOfferOwnerTeam_RuntimeField) || !game.memory.has(::bmh::layout::ActiveManager) ||
        game.manager_records.size() < 5 * kManagerRecord)
        return false;
    const int owner = game.memory.u16(::bmh::layout::NextDeskManager);
    if (owner >= game.memory.u8(::bmh::layout::OriginalManagerCount) ||
        (game.memory.u8(::bmh::layout::ActiveManager) != owner && game.memory.u8(::bmh::layout::ActiveManager) != 4))
        return false;
    const auto team_at = [&](size_t record) {
        const size_t at = record * kManagerRecord + 0x290;
        return int(game.manager_records[at]) | (int(game.manager_records[at + 1]) << 8);
    };
    const int buyer = team_at(4);
    const uint32_t offer = ::bmh::layout::OfferSlots + uint32_t(raw_offer_slot) * 4;
    const uint32_t list = ::bmh::layout::EditorList + uint32_t(raw_offer_slot + 40) * 0x58;
    const size_t pool = runtime_dimensions.pool_first + size_t(raw_offer_slot + 40);
    if (team_at(0) != kNoTeam || buyer >= runtime_dimensions.league_teams || buyer != expected_team || !game.memory.has(offer + 3) ||
        !game.memory.has(list + 0x57) || pool >= game.squad_slots.size())
        return false;
    const uint32_t word = game.memory.u32(offer);
    const int player = int((word >> 10) & 0xfff);
    const int seller = game.memory.u16(list + 8) & 0x7fff;
    if (word == 0xffffffffu || int(word & 31) != owner ||
        player == 0xfff || size_t(player) >= game.players.size() ||
        (game.squad_slots[pool] & 0xfff) != player || game.memory.u16(list) != buyer ||
        seller >= runtime_dimensions.league_teams || seller == buyer || (owner > 0 && team_at(size_t(owner)) != seller))
        return false;
    return true;
}

namespace {

void manager_identity(const GameState& game, int manager, TaxDialogProjection& out,
                      bool pending_owner = false) {
    if (manager < 0 || (!pending_owner && size_t(manager) >= game.manager_teams.size()) ||
        size_t(manager) * kManagerRecord + 0x223 > game.manager_records.size())
        throw FormatError("pending offer UI: invalid Source manager");
    const int team = pending_owner ? pending_offer_owner_team(game, manager)
                                   : game.manager_teams[size_t(manager)];
    const size_t base = size_t(manager) * kManagerRecord;
    const size_t name_at = base + 0xa8;
    const size_t name_end = std::min(name_at + 30, game.manager_records.size());
    size_t nul = name_at;
    while (nul < name_end && game.manager_records[nul] != 0) ++nul;
    out.manager_name.assign(reinterpret_cast<const char*>(game.manager_records.data() + name_at), nul - name_at);
    out.portrait = game.manager_records[base + 0x221];
    // PresentManagerDialog's original face selector accepts atlas portraits 0..13 and
    // reserves 14 for the manager-placeholder tile drawn from KB0.VGA.
    if (out.portrait > 14) throw FormatError("pending offer UI: invalid Source manager portrait");

    out.badge_fallback = original_badge_fallback(team);
    if (out.badge_fallback) {
        out.badge_sprite = 0;
    } else {
        if (team < 0 || size_t(team) * 2 + 2 > game.club_heap_78.size())
            throw FormatError("pending offer UI: missing Source manager badge selector");
        out.badge_sprite = (int(game.club_heap_78[size_t(team) * 2]) |
            int(game.club_heap_78[size_t(team) * 2 + 1]) << 8) & 0x1ff;
    }
    out.compact_dialog_frame = game.memory.has(::bmh::layout::CompactDialogFrame) && game.memory.u8(::bmh::layout::CompactDialogFrame) != 0;
}

} // namespace

TaxDialogProjection pending_offer_question_dialog(const GameState& game,
                                                   const PendingOfferQuestion& question) {
    if (question.kind != PendingOfferQuestionKind::salary_demand &&
        question.kind != PendingOfferQuestionKind::severance)
        throw FormatError("pending offer UI: unsupported Source question kind");
    TaxDialogProjection out;
    out.kind = TaxDialogKind::question;
    out.source_flags = question.source_flags;
    out.source_manager_flags = uint8_t(question.manager);
    out.body = question.original_message;
    manager_identity(game, question.manager, out, true);
    return out;
}

TaxDialogProjection loan_return_question_dialog(const GameState& game, int manager,
                                                std::string_view original_message) {
    if (manager < 0 || manager > 3 || original_message.empty())
        throw FormatError("loan return UI: invalid Source decision owner");
    TaxDialogProjection out;
    out.kind = TaxDialogKind::question;
    out.source_flags = 1; // DayStepRest2 -> PresentManagerDialog(manager, text+Month_ReturnLoans_TextPointer6, 1).
    out.source_manager_flags = uint8_t(manager);
    out.body.assign(original_message);
    manager_identity(game, manager, out);
    return out;
}

TaxDialogProjection advertising_renewal_question_dialog(const GameState& game, int manager,
                                                        const AdvertisingRenewal& renewal) {
    if (renewal.status != AdvertisingRenewal::Status::offered || manager < 0 || manager > 3 ||
        renewal.contract_slot < 0 || renewal.contract_slot > 15 || renewal.question.empty())
        throw FormatError("advertising renewal UI: invalid Source question owner");
    TaxDialogProjection out;
    out.kind = TaxDialogKind::question;
    out.source_flags = 1; // CheckAdvertisingRenewal -> PresentManagerDialog(manager, question, 1).
    out.source_manager_flags = uint8_t(manager);
    out.body = renewal.question;
    manager_identity(game, manager, out);
    return out;
}

TaxDialogProjection advertising_renewal_notice_dialog(const GameState& game, int manager,
                                                      const AdvertisingRenewal& renewal) {
    if (renewal.status != AdvertisingRenewal::Status::declined_by_sponsor || manager < 0 ||
        manager > 3 || renewal.contract_slot < 0 || renewal.contract_slot > 15 ||
        renewal.notice.empty())
        throw FormatError("advertising renewal UI: invalid Source refusal notice owner");
    TaxDialogProjection out;
    out.kind = TaxDialogKind::acknowledgement;
    // CheckAdvertisingRenewal calls SystemIdentity(manager | 0x80, composedText, 0)
    // before the staged Source settlement is published.
    out.source_manager_flags = uint8_t(manager) | 0x80;
    out.body = renewal.notice;
    manager_identity(game, manager, out);
    return out;
}

TaxDialogProjection pending_offer_notice_dialog(const GameState& game,
                                                 const PendingOfferNotice& notice) {
    TaxDialogProjection out;
    out.kind = TaxDialogKind::acknowledgement;
    out.body = notice.original_message;
    switch (notice.text_address) {
    case ::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField: // SubmitTransferBidSourceHumanBuyer -> SystemIdentity: ordinary direct buyer agreement.
    case ::bmh::layout::DeskActions_Unsupported_ExecutableField2: // SubmitTransferBidSourceHumanBuyer -> SystemIdentity: free player awaits the ordinary auction.
    case ::bmh::layout::DeskActions_Unsupported_TextPointer3: // SubmitTransferBidSourceHumanBuyer -> SystemIdentity: composed seller/ordinary-auction notice.
    case ::bmh::layout::DeskActions_Unsupported_ExecutableField3: // SubmitTransferBidSourceHumanBuyer -> SystemIdentity: composed player/salary refusal.
    case ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField: // SystemIdentity(manager|0x80): interactive ACK after explicit decline.
    case ::bmh::layout::PendingOffer_FormatError_ExecutableField2: // SystemIdentity(manager|0x80): interactive ACK after normal settlement.
    case ::bmh::layout::PendingOffer_ConstructionNoticeDialog_TextPointer: // SettleHumanBuyerSale -> SystemIdentity: buyer subsidy, composed amounts.
    case ::bmh::layout::PendingOffer_FormatError_ExecutableField3: // SettleHumanBuyerSale -> SystemIdentity: buyer subsidy threshold exceeded.
        out.source_manager_flags = uint8_t(notice.manager) | 0x80;
        break;
    case ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField2: // SDLFlushEvents3(manager|0x40): timed warning.
    case ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField3: // SDLFlushEvents3(manager|0x40): timed failed-salary notice.
    case ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField4: // SDLFlushEvents3(manager|0x40): timed severance refusal.
    case ::bmh::layout::PendingOffer_FormatError_ExecutableField: // SDLFlushEvents3(manager|0x40): timed full-buyer-squad notice.
    case ::bmh::layout::Advertising_FormatError_TextPointer18: // BookMoney2 -> SDLFlushEvents3: temporarily active buyer lacks funds.
    case ::bmh::layout::DeskActions_SubmitTransferOfferEditorSource_TextPointer: // OpenTransferEditor -> SDLFlushEvents3: missing fee or salary.
    case ::bmh::layout::DeskActions_SubmitTransferOfferEditorSource_RuntimeField: // OpenTransferEditor -> SDLFlushEvents3: buyer's board refuses the fee.
    case ::bmh::layout::DeskActions_Unsupported_ExecutableField: // SubmitTransferBidSourceHumanBuyer -> SDLFlushEvents3: buyer's reserved squad is full.
    case ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField5: // PitchScreen: timed injury/unavailable notice.
    case ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField6: // PitchScreen: timed injury-related bench-player notice.
    case ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField7: // PitchScreen: timed injury-related field-player notice.
    case ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField8: // PitchScreen: timed foreign-player limit notice.
    case ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField9: // PitchScreen: timed already-played incoming-player notice.
    case ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField10: // PitchScreen: timed substitution-count limit notice.
        out.source_manager_flags = uint8_t(notice.manager) | 0x40;
        break;
    default:
        throw FormatError("pending offer UI: unknown Source notice call mode");
    }
    if (notice.manager == 4) {
        const size_t team_at = 4 * kManagerRecord + 0x290;
        const int buyer = team_at + 2 <= game.manager_records.size()
            ? int(game.manager_records[team_at]) | (int(game.manager_records[team_at + 1]) << 8) : -1;
        const bool buyer_notice = notice.text_address == ::bmh::layout::PendingOffer_FormatError_ExecutableField2 || notice.text_address == ::bmh::layout::PendingOffer_ConstructionNoticeDialog_TextPointer ||
            notice.text_address == ::bmh::layout::PendingOffer_FormatError_ExecutableField3 || notice.text_address == ::bmh::layout::Advertising_FormatError_TextPointer18 || notice.text_address == ::bmh::layout::PendingOffer_FormatError_ExecutableField;
        if (!buyer_notice || !pending_offer_spare_buyer(game, notice.raw_offer_slot, buyer))
            throw FormatError("pending offer UI: invalid Source spare buyer notice");
        // PresentManagerDialog uses the system heading and portrait14 twice for low-six-bit
        // identities above3, even when FindManagerForClub identified a parked club.
        out.source_system_identity = true;
        out.portrait = 14;
        out.badge_fallback = true;
        out.compact_dialog_frame = game.memory.has(::bmh::layout::CompactDialogFrame) && game.memory.u8(::bmh::layout::CompactDialogFrame) != 0;
    } else manager_identity(game, notice.manager, out, true);
    return out;
}

TaxDialogProjection transfer_last_week_dialog(const GameState& game,
                                              const TransferDecisionEvent& event) {
    if (event.kind != TransferDecisionEventKind::last_week || !event.offers.valid ||
        event.offers.player < 0 || size_t(event.offers.player) >= game.players.size() ||
        event.result_text_address != ::bmh::layout::PendingOffer_TransferLastWeekDialog_ExecutableField)
        throw FormatError("transfer last-week UI: invalid Source system notice");
    TaxDialogProjection out;
    out.kind = TaxDialogKind::acknowledgement;
    out.source_manager_flags = 0x84; // SystemIdentity(4) -> PresentManagerDialog(0x84, ..., 0)
    out.source_system_identity = true; // Original SystemManagerNameText and both portrait-14 placeholders
    out.compact_dialog_frame = game.memory.has(::bmh::layout::CompactDialogFrame) && game.memory.u8(::bmh::layout::CompactDialogFrame) != 0;
    out.badge_fallback = true;
    out.portrait = 14;
    out.body = game.player_name(event.offers.player) + message_text(game, event.result_text_address);
    return out;
}

TaxDialogProjection construction_notice_dialog(const GameState& game, const LeImage& exe,
                                                const ConstructionNotice& notice) {
    TaxDialogProjection out;
    out.kind = TaxDialogKind::acknowledgement;
    switch (notice.kind) {
    case ConstructionNoticeKind::SponsorContribution:
        out.source_manager_flags = uint8_t(notice.manager) | 0x80;
        out.body = message_text(game, ::bmh::layout::PendingOffer_ConstructionNoticeDialog_TextPointer) +
            format_number(game, int32_t(notice.subsidy), 0x101) +
            message_text(game, ::bmh::layout::PendingOffer_ConstructionNoticeDialog_TextPointer2) +
            format_number(game, int32_t(notice.gross_price), 0x101) +
            exe.cstring(::bmh::layout::CareerSession_FormatNumber_ExecutableField);
        break;
    case ConstructionNoticeKind::SponsorExploited:
        out.source_manager_flags = uint8_t(notice.manager) | 0x80;
        out.body = exe.cstring(::bmh::layout::Fltd_Layout_ExecutableField3) + format_number(game, notice.percent, 1) +
            exe.cstring(::bmh::layout::Fltd_Layout_ExecutableField4);
        break;
    case ConstructionNoticeKind::InsufficientFunds:
        out.source_manager_flags = uint8_t(notice.manager) | 0x40;
        out.body = message_text(game, ::bmh::layout::Advertising_FormatError_TextPointer18);
        break;
    default:
        throw FormatError("construction notice UI: unsupported Source notice kind");
    }
    manager_identity(game, notice.manager, out);
    return out;
}

TaxDialogProjection season_ticket_compensation_notice_dialog(
    const GameState& game, const LeImage& exe, const SeasonTicketCompensationNotice& notice) {
    TaxDialogProjection out;
    out.kind = TaxDialogKind::acknowledgement;
    // Void3 uses SystemIdentity: interactive manager acknowledgement, before
    // force-booking the compensation. These are direct EXE strings.
    out.source_manager_flags = uint8_t(notice.manager) | 0x80;
    out.body = exe.cstring(::bmh::layout::Fltd_Layout_ExecutableField) +
        format_number(game, notice.signed_amount, 0x101) + exe.cstring(::bmh::layout::Fltd_Layout_ExecutableField2);
    manager_identity(game, notice.manager, out);
    return out;
}

IncomingTransferDecisionState pending_offer_transfer_decision_from_game(
    const GameState& game, const TransferOfferListView& source) {
    return incoming_transfer_pending_decision_from_game(game, source);
}

} // namespace bmh::ui
