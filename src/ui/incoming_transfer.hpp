// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "ui/sprite_font.hpp"

namespace bmh { struct GameState; struct TransferOfferListView; }
namespace bmh::ui {

// Read-only copy of the engine's four raw bid slots for one listed player.
// Slot identity is deliberately preserved: UI row N returns bid_index N.
struct IncomingTransferOfferSlot {
    int bid_index = -1;
    int buyer = -1;
    int buyer_manager = -1;
    uint32_t fee = 0;
    uint32_t monthly_salary = 0;
    uint32_t fee_reference = 0;
    uint32_t salary_reference = 0;
    uint8_t contract_months = 0;
    bool loan = false;
    bool present = false;
    bool eligible = false;
    std::array<uint8_t, 7> bonuses{};
    bool transaction_supported = true;
};

struct IncomingTransferDecisionView {
    bool valid = false;
    // Explicit owner provenance for OpenTransferEditor's KB31 editor projection. Unlike
    // the pending route, this changes only the accepted notice portrait range.
    bool source_outgoing_editor_context = false;
    int list_entry = -1;
    int player = -1;
    int seller = -1;
    int seller_manager = -1;
    int viewer_manager = -1;
    int preferred_bid_index = -1;
    bool has_preferred_bid = false;
    bool preferred_bid_passed_random_check = false;
    std::string negotiator_name; // Source identity: manager or SPIELERBERATER, never inferred here.
    uint32_t asking_fee = 0;
    uint32_t asking_monthly_salary = 0;
    uint32_t quoted_fee = 0;
    uint32_t quoted_monthly_salary = 0;
    uint32_t currency_numerator = 1;
    uint32_t currency_denominator = 1;
    uint8_t quoted_years = 0;
    uint32_t notice_text_address = 0;
    std::string notice_text; // Original game encoding; optional Source retry warning.
    std::array<IncomingTransferOfferSlot, 4> offers{};
};

// Source OpenTransferEditor chooses its title from the original action argument. The
// shared incoming/pending screen keeps the ordinary decision caption by default.
enum class IncomingTransferTitle : uint8_t { decision, player_purchase, player_loan };

struct IncomingTransferDecisionState {
    IncomingTransferTitle title = IncomingTransferTitle::decision;
    bool source_outgoing_editor_context = false;
    // True only for the explicit DispatchClubPlayerPendingOffers special-entry adapter below.
    bool pending_offer_source = false;
    int list_entry = -1;
    int player = -1;
    int seller = -1;
    int seller_team_token = -1; // Unmapped Source seller token used by SourceBadgeFallback fallback dispatch.
    int seller_manager = -1;
    int viewer_manager = -1;
    int preferred_bid_index = -1;
    bool has_preferred_bid = false;
    bool preferred_bid_passed_random_check = false;
    std::string player_name;       // Original game encoding, owned.
    std::string seller_name;       // Original game encoding, owned.
    std::string negotiator_name;   // Original game encoding, owned.
    std::string viewer_name;       // Original game encoding, owned.
    std::string notice_text;       // Original game encoding, owned.
    uint32_t notice_text_address = 0;
    int manager_portrait = -1;
    int badge_sprite = -1;
    int seller_badge_sprite = -1;
    bool notice_badge_fallback = false; // SourceBadgeFallback gate from the viewer club token, not the sprite number
    int age = 0;
    int position = 0;
    int strength = 0;
    uint32_t asking_fee = 0;
    uint32_t asking_monthly_salary = 0;
    uint32_t quoted_fee = 0;
    uint32_t quoted_monthly_salary = 0;
    uint32_t currency_numerator = 1;
    uint32_t currency_denominator = 1;
    uint8_t quoted_years = 0;
    std::array<IncomingTransferOfferSlot, 4> offers{};
    std::array<std::string, 4> buyer_names{}; // Original game encoding, owned.
};

enum class IncomingTransferOutcomeMark { none, blinking_question, rejected, accepted };

struct IncomingTransferOutcomeFrame {
    std::array<IncomingTransferOutcomeMark, 4> marks{};
    std::array<uint8_t, 4> colors{};
    uint8_t blink_phase = 0;
    bool complete = false;
};

IncomingTransferDecisionState incoming_transfer_decision_from_game(
    const GameState&, const IncomingTransferDecisionView&);
// Narrow DispatchClubPlayerPendingOffers route: accepts only a 40..43 special entry with raw bid0
// as its sole offer. Ordinary list projection remains 0..39.
IncomingTransferDecisionState incoming_transfer_pending_decision_from_game(
    const GameState&, const TransferOfferListView&);
std::string incoming_transfer_decision_json(const IncomingTransferDecisionState&);

// Source IncomingTransferScreen chooser layered over the original KB31 negotiation panel.
// click/key return nullopt when no action was taken, -1 for decline, or the raw
// bid_index. The caller remains responsible for the synchronous Source recheck.
class IncomingTransferScreen {
public:
    IncomingTransferScreen(const std::filesystem::path& data,
                           IncomingTransferDecisionState state,
                           bool gradient_text = true);
    Image render() const;
    Image render_at_elapsed(uint64_t elapsed_ms) const;
    // Passive result supplied by the synchronous engine observer after its
    // validation. `row_count` is the number of Source result rows; accepted
    // is a raw bid slot, or -1 when every row was refused. OutcomeDelayDisabled is the
    // original Source delay flag (zero enables the Source delay).
    void begin_outcome(int row_count, int accepted_bid_index, uint8_t outcome_delay_disabled);
    IncomingTransferOutcomeFrame outcome_frame_at(uint64_t elapsed_ms) const;
    uint64_t outcome_duration_ms() const;
    // Original 60fps frames086..264 contain all eight phases of raw bid1:
    // 178/60 seconds /8 =370.833ms. DOS ShowConfirmation's calibrated loop is not
    // a portable SDL delay; use the nearest millisecond of the recorded rate.
    static constexpr uint64_t outcome_phase_ms = 371;
    void show_confirmation(uint32_t original_text_address, const Image& backdrop);
    bool confirmation_dismissed() const { return confirmation_dismissed_; }
    bool outcome_dismissed() const { return outcome_dismissed_; }
    std::optional<int> click(int x, int y, bool right = false, uint64_t elapsed_ms = 0);
    std::optional<int> key(std::string_view name);
    bool dismiss_notice();
    bool notice_active() const { return !state_.notice_text.empty() && notice_active_; }
    const IncomingTransferDecisionState& state() const { return state_; }
    const std::optional<int>& answer() const { return answer_; }
    std::string state_json() const;

private:
    IncomingTransferDecisionState state_;
    LeImage exe_;
    TextDb texts_;
    Font body_, heading_;
    SpriteFont sprite_font_;
    Image background_, negotiation_;
    bool gradient_text_ = true;
    std::optional<int> answer_;
    bool notice_active_ = false;
    int outcome_row_count_ = -1;
    int outcome_accepted_bid_index_ = -1;
    uint8_t outcome_speed_setting_ = 0;
    bool outcome_dismissed_ = false;
    bool confirmation_mode_ = false, confirmation_dismissed_ = false;
    std::optional<Image> confirmation_backdrop_;
    Image widgets_, faces_, badge_atlas_, seller_badge_atlas_;

    void draw_body(Image&, int x, int baseline, std::string_view, uint8_t color) const;
    void draw_top(Image&, int x, int y, std::string_view, uint8_t color) const;
    void draw_heading(Image&, int x, int baseline, std::string_view, uint8_t color) const;
    void draw_notice(Image&) const;
};

} // namespace bmh::ui
