// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "national_friendly_ui.hpp"

#include "game/messages.hpp"

namespace bmh::ui {
namespace {
constexpr int kNationalTeam = 0x165;
constexpr int kFirstNationTeam = 0x166; // Nation zero is intentionally not selectable.
constexpr int kLastNationTeam = 0x165 + 61;

TaxDialogProjection question(const NationalFriendlyIdentity& identity, uint16_t flags,
                             int manager, const GameState& game, std::string body) {
    TaxDialogProjection p;
    p.kind = TaxDialogKind::question;
    p.compact_dialog_frame = game.memory.has(::bmh::layout::CompactDialogFrame) && game.memory.u8(::bmh::layout::CompactDialogFrame) != 0;
    p.source_manager_flags = uint8_t(manager);
    p.source_flags = flags;
    p.manager_name = identity.manager_name;
    p.portrait = identity.portrait;
    p.badge_fallback = identity.badge_fallback;
    p.badge_sprite = identity.badge_sprite;
    p.body = std::move(body);
    return p;
}
}

NationalFriendlyUi::NationalFriendlyUi(NationalFriendlyUiOwner owner,
                                       NationalFriendlyIdentity identity)
    : caller_(owner), identity_(std::move(identity)), opponent_identity_(identity_) {
    // This is intentionally stricter than “national manager”: only menu 6's
    // explicit national case can open the mutating picker.
    if (caller_.desk_menu != 6 || caller_.manager < 0 || caller_.manager >= 8 ||
        caller_.runtime_team != kNationalTeam || caller_.revision == 0 || caller_.sequence == 0) {
        return;
    }
    engine_owner_ = {caller_.sequence, caller_.revision, caller_.manager, caller_.runtime_team, 6};
    stage_ = NationalFriendlyUiStage::picker;
}

NationalFriendlyUiAction NationalFriendlyUi::begin() const {
    if (stage_ != NationalFriendlyUiStage::picker) return {};
    return {NationalFriendlyUiActionKind::open_picker};
}

TaxDialogProjection NationalFriendlyUi::cost_question(const GameState& g) const {
    // Source menu6 appends the quoted amount with FormatNumber/0x101 before57ef7.
    return question(identity_, 0x404, caller_.manager, g,
                    bmh::message_text(g, ::bmh::layout::Controller_FormatError_TextPointer2) + bmh::format_number(g, quote_->terms.cost, 0x101));
}

TaxDialogProjection NationalFriendlyUi::payer_question(const GameState& g) const {
    // PresentManagerDialog receives the Controller_FormatError_TextPointer3 template, followed by the source literal
    // and the same 0x101 amount format when the other manager pays.
    auto body = bmh::message_text(g, ::bmh::layout::Controller_FormatError_TextPointer3);
    if (!manager_home_ && quote_->terms.payer_manager >= 0) {
        body += bmh::kFriendlyPayerPrefixText;
        body += bmh::format_number(g, quote_->terms.cost, 0x101);
    }
    return question(opponent_identity_, 0x303, quote_->terms.opponent_manager, g, std::move(body));
}

NationalFriendlyUiAction NationalFriendlyUi::show(TaxDialogProjection p) const {
    NationalFriendlyUiAction a;
    a.kind = NationalFriendlyUiActionKind::show_question;
    a.question = std::move(p);
    return a;
}

NationalFriendlyUiAction NationalFriendlyUi::select_opponent(const GameState& g, int team) {
    if (stage_ != NationalFriendlyUiStage::picker) return {};
    if (team < kFirstNationTeam || team > kLastNationTeam) return cancel();
    opponent_ = team;
    stage_ = NationalFriendlyUiStage::venue_question;
    return show(question(identity_, 0x606, caller_.manager, g, bmh::message_text(g, ::bmh::layout::Controller_ClubFriendlyState_TextPointer)));
}

NationalFriendlyUiAction NationalFriendlyUi::answer_venue(bool yes) {
    if (stage_ != NationalFriendlyUiStage::venue_question || opponent_ < kFirstNationTeam) return {};
    manager_home_ = yes;
    stage_ = NationalFriendlyUiStage::quote_pending;
    NationalFriendlyUiAction a;
    a.kind = NationalFriendlyUiActionKind::quote;
    a.engine_owner = engine_owner_;
    a.opponent = opponent_;
    a.manager_home = manager_home_;
    return a;
}

NationalFriendlyUiAction NationalFriendlyUi::accept_quote(const GameState& g,
                                                            NationalFriendlyQuote quote) {
    if (stage_ != NationalFriendlyUiStage::quote_pending || quote.owner != engine_owner_ ||
        quote.terms.status != bmh::FriendlyQuoteStatus::quoted || quote.terms.opponent != opponent_ ||
        quote.terms.manager_home != manager_home_ || quote.terms.manager_team != kNationalTeam) {
        stage_ = NationalFriendlyUiStage::rejected;
        return cancel();
    }
    quote_ = std::move(quote);
    if (quote_->terms.opponent_manager >= 0 &&
        opponent_identity_.manager_slot != quote_->terms.opponent_manager) {
        quote_.reset();
        return cancel();
    }
    payer_approval_needed_ = quote_->terms.opponent_manager >= 0 &&
                             quote_->terms.opponent_manager != caller_.manager;
    cost_approval_needed_ = quote_->terms.payer_manager >= 0;
    if (manager_home_) {
        stage_ = NationalFriendlyUiStage::cost_question;
        return show(cost_question(g));
    }
    if (payer_approval_needed_ || cost_approval_needed_) {
        stage_ = NationalFriendlyUiStage::payer_question;
        return show(payer_question(g));
    }
    accept_opponent_ = true;
    stage_ = NationalFriendlyUiStage::calendar;
    return {NationalFriendlyUiActionKind::open_calendar};
}

NationalFriendlyUiAction NationalFriendlyUi::answer_cost(const GameState& g, bool yes) {
    if (stage_ != NationalFriendlyUiStage::cost_question) return {};
    if (!yes) return cancel();
    accept_cost_ = true;
    if (quote_ && quote_->terms.opponent_manager >= 0 &&
        quote_->terms.opponent_manager != caller_.manager) {
        payer_approval_needed_ = true;
        stage_ = NationalFriendlyUiStage::payer_question;
        return show(payer_question(g));
    }
    stage_ = NationalFriendlyUiStage::calendar;
    return {NationalFriendlyUiActionKind::open_calendar};
}

NationalFriendlyUiAction NationalFriendlyUi::answer_payer(bool yes) {
    if (stage_ != NationalFriendlyUiStage::payer_question) return {};
    if (!yes) return cancel();
    accept_opponent_ = true;
    if (!manager_home_) accept_cost_ = cost_approval_needed_;
    stage_ = NationalFriendlyUiStage::calendar;
    return {NationalFriendlyUiActionKind::open_calendar};
}

std::optional<NationalFriendlyBookingRequest> NationalFriendlyUi::booking_template() const {
    if (stage_ != NationalFriendlyUiStage::calendar || !quote_) return {};
    NationalFriendlyBookingRequest request;
    request.quote = *quote_;
    request.owner = engine_owner_;
    request.accept_opponent = accept_opponent_;
    request.consenting_manager = payer_approval_needed_ ? quote_->terms.opponent_manager : -1;
    request.accept_cost = accept_cost_;
    request.approving_manager = !request.accept_cost ? -1 :
        (manager_home_ ? caller_.manager : quote_->terms.payer_manager);
    return request;
}

NationalFriendlyUiAction NationalFriendlyUi::request_booking(uint16_t day) {
    if (stage_ != NationalFriendlyUiStage::calendar || !quote_) return {};
    auto maybe_request=booking_template();
    if(!maybe_request)return {};
    NationalFriendlyBookingRequest request=std::move(*maybe_request);
    request.season_day = day;
    stage_ = NationalFriendlyUiStage::booking_pending;
    NationalFriendlyUiAction a;
    a.kind = NationalFriendlyUiActionKind::book;
    a.engine_owner = engine_owner_;
    a.opponent = opponent_;
    a.manager_home = manager_home_;
    a.quote = quote_;
    a.booking = request;
    return a;
}

NationalFriendlyUiAction NationalFriendlyUi::accept_booking(const NationalFriendlyBookingResult& result) {
    if (stage_ != NationalFriendlyUiStage::booking_pending || !quote_ ||
        result.manager != caller_.manager || result.manager_team != kNationalTeam ||
        result.opponent != opponent_ || result.manager_home != manager_home_ ||
        result.payer_manager != quote_->terms.payer_manager || result.cost != quote_->terms.cost) return {};
    if (result.status == bmh::FriendlyBookingStatus::booked) {
        stage_ = NationalFriendlyUiStage::finished;
        return {NationalFriendlyUiActionKind::return_to_caller};
    }
    stage_ = NationalFriendlyUiStage::calendar;
    return {NationalFriendlyUiActionKind::open_calendar};
}

NationalFriendlyUiAction NationalFriendlyUi::cancel() {
    stage_ = NationalFriendlyUiStage::cancelled;
    quote_.reset();
    opponent_ = -1;
    return {NationalFriendlyUiActionKind::return_to_caller};
}

} // namespace bmh::ui
