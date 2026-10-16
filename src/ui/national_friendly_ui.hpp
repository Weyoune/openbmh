// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "game/desk_actions.hpp"
#include "game/state.hpp"
#include "ui/game_end.hpp"

namespace bmh::ui {

// Authorization comes from the actual caller, not from the national-team flag
// alone. Normal Desk menu 23 must never construct this owner.
struct NationalFriendlyUiOwner {
    int desk_menu = -1;
    int manager = -1;
    int runtime_team = -1;
    uint64_t revision = 0;
    uint64_t sequence = 0;
    bool operator==(const NationalFriendlyUiOwner&) const = default;
};

enum class NationalFriendlyUiStage : uint8_t {
    picker, venue_question, quote_pending, cost_question, payer_question,
    calendar, booking_pending, finished, cancelled, rejected
};
enum class NationalFriendlyUiActionKind : uint8_t {
    none, open_picker, quote, show_question, open_calendar, book, return_to_caller
};
struct NationalFriendlyUiAction {
    NationalFriendlyUiAction() = default;
    NationalFriendlyUiAction(NationalFriendlyUiActionKind value) : kind(value) {}
    NationalFriendlyUiActionKind kind = NationalFriendlyUiActionKind::none;
    std::optional<TaxDialogProjection> question;
    std::optional<NationalFriendlyOwner> engine_owner;
    int opponent = -1;
    bool manager_home = false;
    std::optional<NationalFriendlyQuote> quote;
    std::optional<NationalFriendlyBookingRequest> booking;
};

struct NationalFriendlyIdentity {
    std::string manager_name;
    int portrait = 0;
    bool badge_fallback = false;
    int badge_sprite = 0;
    int manager_slot = -1;
};

class NationalFriendlyUi {
public:
    // menu_kind must be the verified menu-6 call site; menu 23/default page 7
    // intentionally fails even when the active manager is national.
    NationalFriendlyUi(NationalFriendlyUiOwner owner, NationalFriendlyIdentity identity);
    NationalFriendlyUiStage stage() const { return stage_; }
    const NationalFriendlyOwner& engine_owner() const { return engine_owner_; }
    int opponent() const { return opponent_; }
    bool manager_home() const { return manager_home_; }
    void set_opponent_manager_identity(NationalFriendlyIdentity identity) {
        opponent_identity_ = std::move(identity);
    }

    NationalFriendlyUiAction begin() const;
    NationalFriendlyUiAction select_opponent(const GameState&, int runtime_team);
    NationalFriendlyUiAction answer_venue(bool yes);
    NationalFriendlyUiAction accept_quote(const GameState&, NationalFriendlyQuote);
    NationalFriendlyUiAction answer_cost(const GameState&, bool yes);
    NationalFriendlyUiAction answer_payer(bool yes);
    std::optional<NationalFriendlyBookingRequest> booking_template() const;
    NationalFriendlyUiAction request_booking(uint16_t day);
    NationalFriendlyUiAction accept_booking(const NationalFriendlyBookingResult&);
    NationalFriendlyUiAction cancel();

private:
    NationalFriendlyUiOwner caller_;
    NationalFriendlyIdentity identity_;
    NationalFriendlyIdentity opponent_identity_;
    NationalFriendlyOwner engine_owner_;
    NationalFriendlyUiStage stage_ = NationalFriendlyUiStage::rejected;
    int opponent_ = -1;
    bool manager_home_ = false;
    bool accept_cost_ = false;
    bool accept_opponent_ = false;
    bool payer_approval_needed_ = false;
    bool cost_approval_needed_ = false;
    std::optional<NationalFriendlyQuote> quote_;
    TaxDialogProjection cost_question(const GameState&) const;
    TaxDialogProjection payer_question(const GameState&) const;
    NationalFriendlyUiAction show(TaxDialogProjection) const;
};

} // namespace bmh::ui
