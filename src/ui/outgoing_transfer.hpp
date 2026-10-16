// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "formats/vga.hpp"
#include "ui/incoming_transfer.hpp"
#include "ui/sprite_font.hpp"

namespace bmh::ui {

enum class OutgoingTransferField : uint8_t {
    fee, monthly_salary, duration_years,
    bonus_0, bonus_1, bonus_2, bonus_3, bonus_4, bonus_5, bonus_6, quote_loan
};

enum class OutgoingTransferRequestKind : uint8_t {
    none, edit_field, quote_change, team_info, player_info, submit, cancel
};

// BalanceFromGame inputs are supplied by the Core/App owner; the renderer must not
// invent calculator limits or presets. Missing presets are not drawn.
struct OutgoingTransferCalculatorProfile {
    bool supported = false;
    bool currency = false;
    uint32_t minimum = 0, maximum = 0, initial = 0;
    uint32_t source_text_address = 0;
    std::array<std::optional<uint32_t>, 8> presets{};
};

// Owned projection for OpenTransferEditor's interactive outgoing-bid editor. `panel`
// holds the shared Original player/seller/header and four raw bid rows.
// `bid_slot` is the actual Source row: a present row must belong to
// manager_slot; a new bid must point at an empty row.
struct OutgoingTransferEditorState {
    IncomingTransferDecisionState panel;
    int manager_slot = -1, runtime_club = -1;
    uint64_t revision = 0;
    int list_entry = -1, source_slot = -1, player = -1, seller = -1;
    int bid_slot = -1;
    uint8_t source_mode = 0;
    bool editable = false, closed = false, quote_loan = false;
    // Original OpenTransferEditor action parameter: controls TEXTD 227/228. It is
    // intentionally separate from the local quote-loan toggle and Native term.
    bool initial_action_loan = false;
    bool gradient_text = true;
    uint32_t fee = 0, monthly_salary = 0;
    // OpenTransferEditor initializes a new empty row to a 24-month term (two years).
    uint8_t duration_years = 2;
    std::array<uint8_t, 7> bonuses{};
    std::array<OutgoingTransferCalculatorProfile, 2> money_calculators{};
    std::array<OutgoingTransferCalculatorProfile, 8> mode3_calculators{}; // years, then seven bonuses
};

struct OutgoingTransferIdentity {
    int manager_slot = -1, runtime_club = -1;
    uint64_t revision = 0;
    int list_entry = -1, source_slot = -1, player = -1, seller = -1;
    int bid_slot = -1;
};

struct OutgoingTransferRequest {
    OutgoingTransferRequestKind kind = OutgoingTransferRequestKind::none;
    OutgoingTransferIdentity identity;
    OutgoingTransferField field = OutgoingTransferField::fee;
    uint32_t value = 0;
    bool accepted = true;
    bool quote_loan = false;
    uint32_t fee = 0, monthly_salary = 0;
    uint8_t duration_years = 0;
    std::array<uint8_t, 7> bonuses{};
    // The Source quote being edited is carried as owner context. It is a
    // projection, not a value recomputed by the UI.
    uint32_t quoted_fee = 0, quoted_monthly_salary = 0;
    uint8_t quoted_years = 0;
};

std::string outgoing_transfer_request_json(const OutgoingTransferRequest&);

class OutgoingTransferScreen {
public:
    OutgoingTransferScreen(const std::filesystem::path& data,
                           OutgoingTransferEditorState state);
    Image render() const;
    OutgoingTransferRequest click(int x, int y, bool right = false, bool double_click = false);
    // Source event8 returns calculator cancellation before closing its editor.
    OutgoingTransferRequest process_timer_expired();
    OutgoingTransferRequest key(std::string_view key);
    bool text(std::string_view text);
    bool calculator_open() const { return calculator_open_; }
    const OutgoingTransferEditorState& state() const { return state_; }
    const std::optional<OutgoingTransferRequest>& pending() const { return pending_; }
    void acknowledge(OutgoingTransferEditorState);
    void reject_action(std::string reason);
    std::string state_json() const;

private:
    OutgoingTransferEditorState state_;
    OutgoingTransferEditorState confirmed_state_;
    std::filesystem::path data_;
    IncomingTransferScreen panel_;
    LeImage exe_;
    TextDb texts_;
    Font body_;
    Image calculator_art_, panel_art_;
    Image button_art_;
    SpriteFont sprite_font_;
    OutgoingTransferCalculatorProfile active_profile_;
    OutgoingTransferField active_field_ = OutgoingTransferField::fee;
    uint32_t calculator_value_ = 0;
    std::string numeric_text_, feedback_;
    bool calculator_open_ = false;
    bool closed_ = false;
    std::optional<OutgoingTransferRequest> pending_;

    OutgoingTransferIdentity identity() const;
    uint32_t field_value(OutgoingTransferField) const;
    uint32_t field_maximum(OutgoingTransferField) const;
    void set_field_value(OutgoingTransferField, uint32_t);
    OutgoingTransferRequest make_request(OutgoingTransferRequestKind,
                                        OutgoingTransferField, uint32_t) const;
    OutgoingTransferRequest commit_field(bool accepted = true);
    void draw_editor_fields(Image&) const;
    void draw_calculator(Image&) const;
    void draw_footer(Image&) const;
};

} // namespace bmh::ui
