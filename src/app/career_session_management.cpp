// SPDX-License-Identifier: GPL-3.0-only
// CareerSession: club management: training, training camp, bank, stadium, toto.
// (moved out of career_session.cpp unchanged; the class is declared in app/career_session.hpp)
#include "app/career_session_internal.hpp"

namespace bmh::app {
namespace {

constexpr uint8_t kBlockSeat = 1;

constexpr uint8_t kBlockRoof = 2;

constexpr uint8_t kBlockFeatures = kBlockSeat | kBlockRoof;

// True for the stadium requests that belong to the block-expansion dialog (select, total,
// feature toggle, cancel, confirm, answer tip).
bool is_block_expansion_action(ui::StadiumRequestKind kind) {
    return kind == ui::StadiumRequestKind::expansion_select ||
        kind == ui::StadiumRequestKind::expansion_set_total ||
        kind == ui::StadiumRequestKind::expansion_toggle_feature ||
        kind == ui::StadiumRequestKind::expansion_cancel ||
        kind == ui::StadiumRequestKind::expansion_confirm ||
        kind == ui::StadiumRequestKind::expansion_answer_tip;
}

}  // namespace

// Training group: one request changes the types, areas, automation byte or one player's
// intensity or focus of the active manager. The request carries the desired result; it is
// validated against the current projection (stale manager, plan limits, expected values), applied
// on a staged copy and committed with revision plus one. Manual edits clear the matching
// assistant flag and never draw random numbers; initialize and enabling automation run the
// automatic training. Returns false with a short error code in 'error' otherwise.
bool CareerSession::dispatch_training(const ui::TrainingRequest& request, uint64_t& revision,
                                      std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    const int active = int(g_.memory.u8(::bmh::layout::ActiveManager));
    if (request.manager_slot != active || request.manager_slot < 0 || request.manager_slot > 3 ||
        size_t(request.manager_slot) >= g_.manager_teams.size() ||
        request.runtime_club != g_.manager_teams[size_t(request.manager_slot)] || request.revision != revision) {
        error = "stale_manager_or_revision";
        return false;
    }

    try {
        const ui::TrainingState current = ui::training_from_game(g_, exe_, revision);
        if (current.manager_slot != request.manager_slot || current.runtime_club != request.runtime_club) {
            error = "stale_manager_or_revision";
            return false;
        }
        const size_t slot = size_t(request.manager_slot);
        const size_t rec = slot * 0xb0c;
        const size_t option_at = slot * 5 + 4;
        if (g_.manager_records.size() < rec + 0xb0c || g_.manager_options.size() <= option_at) {
            error = "invalid_training_state";
            return false;
        }

        auto valid_plan = [](const std::array<uint8_t, 4>& types, const std::array<uint8_t, 9>& areas) {
            int type_sum = 0, fitness = 0, skills = 0;
            for (uint8_t value : types) { if (value < 1 || value > 3) return false; type_sum += value; }
            for (size_t i = 0; i < areas.size(); ++i) {
                const uint8_t value = areas[i];
                if (value < 1 || value > 3) return false;
                if (i < 2) fitness += value; else skills += value;
            }
            return type_sum <= 8 && fitness <= 4 && skills <= 18;
        };
        if (!valid_plan(request.desired_types, request.desired_areas)) {
            error = "invalid_training_plan";
            return false;
        }

        ui::TrainingRequestKind kind = request.kind;
        auto types = current.types;
        auto areas = current.areas;
        uint8_t automation = current.automation;
        bool run_automatic = false;
        // NeedsInitialization initializes an uninitialized screen via Initialize;
        // the four subordinate toggles exist only while the global flag is on.
        // NeedsInitialization2 calls that same deterministic routine after an enabled
        // automation toggle. Manual field edits only write their field and clear
        // the matching assistant flag; they do not run training or draw RNG.
        switch (kind) {
        case ui::TrainingRequestKind::initialize:
            if (!current.needs_initialization() || request.index != -1 || request.value != -1 ||
                request.player_id != -1 || request.desired_types != types || request.desired_areas != areas ||
                request.desired_automation != 0x9f) {
                error = "invalid_training_initialization";
                return false;
            }
            automation = 0x9f;
            run_automatic = true;
            break;
        case ui::TrainingRequestKind::set_type:
            if (current.needs_initialization() || request.index < 0 || request.index >= int(types.size()) ||
                request.value < 1 || request.value > 3 || request.player_id != -1 ||
                request.value == types[size_t(request.index)]) {
                error = "invalid_training_type";
                return false;
            }
            types[size_t(request.index)] = uint8_t(request.value);
            automation = uint8_t(automation & ~uint8_t(1u << 1));
            break;
        case ui::TrainingRequestKind::set_area:
            if (current.needs_initialization() || request.index < 0 || request.index >= int(areas.size()) ||
                request.value < 1 || request.value > 3 || request.player_id != -1 ||
                request.value == areas[size_t(request.index)]) {
                error = "invalid_training_area";
                return false;
            }
            areas[size_t(request.index)] = uint8_t(request.value);
            automation = uint8_t(automation & ~uint8_t(1u << 2));
            break;
        case ui::TrainingRequestKind::set_player_intensity:
        case ui::TrainingRequestKind::set_player_focus: {
            if (current.needs_initialization() || request.index < 0 || request.index >= current.squad_capacity ||
                request.player_id < 0 || request.player_id > 0x0ffe) {
                error = "invalid_training_player";
                return false;
            }
            const auto player = std::find_if(current.players.begin(), current.players.end(), [&](const auto& p) {
                return p.row == request.index && p.player_id == request.player_id;
            });
            const SquadRange squad = squad_range(request.runtime_club);
            if (player == current.players.end() || squad.count != size_t(current.squad_capacity) ||
                squad.first + size_t(request.index) >= g_.squad_slots.size() ||
                (g_.squad_slots[squad.first + size_t(request.index)] & 0x0fff) != request.player_id) {
                error = "stale_training_player";
                return false;
            }
            if (kind == ui::TrainingRequestKind::set_player_intensity) {
                if (request.value < 1 || request.value > 3 || request.value == player->intensity) {
                    error = "invalid_training_intensity";
                    return false;
                }
                automation = uint8_t(automation & ~uint8_t(1u << 3));
            } else if (request.value < 0 || request.value > 7 || request.value != (player->focus + 1) % 8) {
                error = "invalid_training_focus";
                return false;
            }
            break;
        }
        case ui::TrainingRequestKind::set_automation: {
            if (current.needs_initialization() || request.index < 0 || request.index > 4 ||
                (request.value != 0 && request.value != 1) || request.player_id != -1) {
                error = "invalid_training_automation";
                return false;
            }
            if (request.index != 0 && !(current.automation & 1)) {
                error = "training_automation_group_hidden";
                return false;
            }
            const uint8_t mask = uint8_t(1u << request.index);
            automation = uint8_t(automation ^ mask);
            if (((automation & mask) != 0) != (request.value != 0)) {
                error = "stale_training_automation";
                return false;
            }
            run_automatic = (automation & 1) != 0;
            break;
        }
        case ui::TrainingRequestKind::none:
        default:
            error = "unsupported_training_request";
            return false;
        }

        if (request.desired_types != types || request.desired_areas != areas ||
            request.desired_automation != automation) {
            error = "stale_training_projection";
            return false;
        }

        GameState staged = g_;
        std::copy(types.begin(), types.end(), staged.manager_records.begin() + rec + 0xc5);
        std::copy(areas.begin(), areas.end(), staged.manager_records.begin() + rec + 0xc9);
        staged.manager_options[option_at] = automation;
        if (kind == ui::TrainingRequestKind::set_player_intensity ||
            kind == ui::TrainingRequestKind::set_player_focus) {
            const size_t board = slot * 0x840 + size_t(request.index) * 0x58 + 0x0a;
            if (board >= staged.tactics.size()) {
                error = "invalid_training_board";
                return false;
            }
            if (kind == ui::TrainingRequestKind::set_player_intensity)
                staged.tactics[board] = uint8_t((staged.tactics[board] & 0xfc) | request.value);
            else
                staged.tactics[board] = uint8_t((staged.tactics[board] & 0x03) | (request.value << 2));
        }
        if (run_automatic) auto_training(staged, request.manager_slot);
        g_ = std::move(staged);
        ++revision;
        error.clear();
        return true;
    } catch (const std::exception&) {
        error = "invalid_training_state";
        return false;
    }
}

// Training camp group: set_focus changes the four focus bytes of the active manager (an unchanged
// or invalid focus is refused); book books a resort for the week after checking the focus the UI
// saw, the weekly/visit limits and the funds (the booking draws random numbers). Refused while a
// modal owner is open or when the manager slot, club or revision is stale; revision plus one on
// success, short error codes in 'error' otherwise.
bool CareerSession::dispatch_training_camp(const ui::TrainingCampRequest& request, uint64_t& revision,
                                          std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    if (request.manager_slot < 0 || size_t(request.manager_slot) >= g_.manager_teams.size() ||
        request.manager_slot != int(g_.memory.u8(::bmh::layout::ActiveManager)) ||
        request.runtime_club != g_.manager_teams[size_t(request.manager_slot)] || request.revision != revision) {
        error = "stale_manager_or_revision";
        return false;
    }
    const size_t rec = size_t(request.manager_slot) * kManagerRecordSize;
    if (request.kind == ui::TrainingCampRequestKind::set_focus) {
        const std::array<uint8_t, 4> current{
            g_.manager_records.at(rec + 0xc1), g_.manager_records.at(rec + 0xc2),
            g_.manager_records.at(rec + 0xc3), g_.manager_records.at(rec + 0xc4)};
        if (request.focus == current) {
            error = "focus_unchanged";
            return false;
        }
        if (!set_training_camp_focus(g_, request.manager_slot, request.focus)) {
            error = "invalid_focus_request";
            return false;
        }
        ++revision;
        return true;
    }
    if (request.kind == ui::TrainingCampRequestKind::book) {
        if (request.resort < 0 || request.resort >= kTrainingCampResorts) {
            error = "invalid_resort";
            return false;
        }
        const std::array<uint8_t, 4> current{
            g_.manager_records.at(rec + 0xc1), g_.manager_records.at(rec + 0xc2),
            g_.manager_records.at(rec + 0xc3), g_.manager_records.at(rec + 0xc4)};
        if (request.focus != current) {
            error = "stale_focus";
            return false;
        }
        switch (training_camp_booking_block(g_, request.manager_slot)) {
        case TrainingCampBookingBlock::booked_this_week: error = "booked_this_week"; return false;
        case TrainingCampBookingBlock::visit_limit: error = "visit_limit"; return false;
        case TrainingCampBookingBlock::invalid_request: error = "invalid_request"; return false;
        case TrainingCampBookingBlock::none: break;
        }
        if (!book_training_camp(g_, request.manager_slot, request.resort, rand_)) {
            error = "insufficient_funds";
            return false;
        }
        ++revision;
        return true;
    }
    error = "unsupported_training_camp_request";
    return false;
}

// Bank group: deposit moves, share and fund trades, stock tips and bank credit. Needs the active
// manager, club and current revision and no modal owner open. Every branch validates amounts and
// counts against the manager record and the stock/fund blocks, then applies the change to the
// live state and bumps the revision once. The notices of the last property purchase or credit
// refund are kept for the next projection. Returns false with a short error code otherwise.
bool CareerSession::dispatch_bank(const ui::BankRequest& request, uint64_t& revision, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    error.clear();
    if (revision == std::numeric_limits<uint64_t>::max()) {
        error = "revision_exhausted"; return false;
    }
    const int active = int(g_.memory.u8(::bmh::layout::ActiveManager));
    if (request.manager_slot != active ||
        request.manager_slot < 0 || size_t(request.manager_slot) >= g_.manager_teams.size() ||
        request.runtime_club != g_.manager_teams[size_t(request.manager_slot)] || request.revision != revision) {
        error = "stale_manager_or_revision"; return false;
    }
    const size_t rec = size_t(request.manager_slot) * kManagerRecordSize;
    std::optional<ui::BankState::PropertyNotice> property_notice;
    std::optional<ui::BankState::CreditNotice> credit_notice;
    const auto read_signed = [&](size_t offset) {
        const uint32_t v = uint32_t(g_.manager_records.at(rec + offset)) |
            uint32_t(g_.manager_records.at(rec + offset + 1)) << 8 |
            uint32_t(g_.manager_records.at(rec + offset + 2)) << 16 |
            uint32_t(g_.manager_records.at(rec + offset + 3)) << 24;
        return int32_t(v);
    };
    if (request.kind == ui::BankRequestKind::move_to_deposit) {
        if (!request.amount || request.asset_index != -1 || request.count != 0 ||
            int64_t(request.amount > 0 ? request.amount : -int64_t(request.amount)) > 99999999) {
            error = "invalid_amount"; return false;
        }
        const int64_t available = request.amount > 0 ? read_signed(0x314) : read_signed(0x318);
        if (available < 0 || int64_t(request.amount > 0 ? request.amount : -int64_t(request.amount)) > available) {
            error = "amount_unavailable"; return false;
        }
        move_to_deposit(g_, request.manager_slot, request.amount);
    } else if (request.kind == ui::BankRequestKind::buy_shares || request.kind == ui::BankRequestKind::sell_shares ||
               request.kind == ui::BankRequestKind::stock_tip) {
        if (request.asset_index < 0 || request.asset_index >= kStocks ||
            g_.stocks.size() < size_t(kStocks) * 0x5c) { error = "invalid_stock"; return false; }
        const size_t at = size_t(request.asset_index) * 0x5c;
        const int price = int(uint16_t(g_.stocks.at(at + 0x4e) | uint16_t(g_.stocks.at(at + 0x4f)) << 8));
        if (request.kind == ui::BankRequestKind::buy_shares) {
            if (request.count <= 0 || request.count > 9999 || price <= 0 ||
                request.count > max_shares_to_buy(g_, request.manager_slot, request.asset_index)) {
                error = "invalid_share_count"; return false;
            }
            const int64_t raw = int64_t(price) * request.count;
            const int64_t total = raw + raw * 15 / 1000;
            if (total > int64_t(read_signed(0x314)) + read_signed(0x31c)) {
                error = "insufficient_funds"; return false;
            }
            if (!buy_shares(g_, request.manager_slot, request.asset_index, request.count)) {
                error = "investment_limit"; return false;
            }
        } else if (request.kind == ui::BankRequestKind::sell_shares) {
            const size_t held_at = at + 0x50 + size_t(request.manager_slot) * 2;
            const int held = int(uint16_t(g_.stocks.at(held_at) | uint16_t(g_.stocks.at(held_at + 1)) << 8));
            if (request.count <= 0 || request.count > held) { error = "invalid_share_count"; return false; }
            sell_shares(g_, request.manager_slot, request.asset_index, request.count);
        } else {
            if (request.count != 0 || request.amount != 0) { error = "invalid_tip_request"; return false; }
            last_stock_tip_ = stock_tip(g_, request.manager_slot, request.asset_index, rand_);
        }
    } else if (request.kind == ui::BankRequestKind::buy_fund || request.kind == ui::BankRequestKind::sell_fund) {
        if (request.amount != 0 || request.asset_index < 0 || request.asset_index >= kFunds || request.count <= 0 || request.count > 10 ||
            g_.memory.u32(::bmh::layout::FundBlock + uint32_t(request.asset_index) * 0x1c) == 0) {
            error = "invalid_fund_request"; return false;
        }
        const uint32_t fund_at = ::bmh::layout::FundBlock + uint32_t(request.asset_index) * 0x1c;
        const uint16_t held = g_.memory.u16(fund_at + 0x14 + uint32_t(request.manager_slot) * 2);
        if (request.kind == ui::BankRequestKind::buy_fund) {
            if (request.count > max_fund_units(g_, request.manager_slot, request.asset_index)) {
                error = "invalid_fund_count"; return false;
            }
            const int32_t additional_cost = buy_fund_units(g_, request.manager_slot, request.asset_index, request.count, game_);
            if (request.asset_index != 0) {
                const std::string text = message_text(g_, ::bmh::layout::CareerSession_Int32T_TextPointer2) + format_number(g_, additional_cost, 0x101) +
                                         message_text(g_, ::bmh::layout::CareerSession_Int32T_TextPointer);
                property_notice = ui::BankState::PropertyNotice{request.asset_index, additional_cost, ui::original_to_utf8(text)};
            }
        } else {
            if (request.count > held) { error = "invalid_fund_count"; return false; }
            sell_fund_units(g_, request.manager_slot, request.asset_index, request.count);
        }
    } else if (request.kind == ui::BankRequestKind::bank_credit) {
        if (request.amount != 0 || request.asset_index != -1 || request.count != 0 || request.credit_edits.empty()) {
            error = "invalid_credit_request"; return false;
        }
        if (request.credit_counterparty != 0) {
            // Source case 5 scans configured human managers, excluding the borrower.
            // A runtime national-manager entry is not an additional human lender.
            const int configured = int(g_.memory.u8(::bmh::layout::ManagerCount));
            if (request.credit_counterparty < 1 || request.credit_counterparty > 4) {
                error = "invalid_credit_request"; return false;
            }
            const int lender = request.credit_counterparty - 1;
            if (configured < 1 || configured > 4 || request.manager_slot >= configured ||
                lender < 0 || lender >= configured || lender == request.manager_slot) {
                error = "invalid_credit_request"; return false;
            }
        }
        const auto result = commit_bank_credit(g_, exe_, request.manager_slot,
            request.expected_credit, request.credit_edits, request.accept_credit_fees, request.credit_counterparty);
        if (result.status != BankCreditStatus::accepted) {
            error = result.status == BankCreditStatus::no_change ? "credit_no_change" :
                    result.status == BankCreditStatus::stale_request ? "stale_credit_request" :
                    result.status == BankCreditStatus::confirmation_required ? "credit_confirmation_required" :
                    "invalid_credit_request";
            return false;
        }
        if (result.refund != 0) {
            // "Die Bank" (CareerSession_Int32T_TextPointer3) or, in case 5, the lender's name (record +0xa8)
            const int lender = request.credit_counterparty - 1;
            std::string who = message_text(g_, ::bmh::layout::CareerSession_Int32T_TextPointer3);
            if (lender >= 0) {
                who.clear();
                for (size_t i = 0; i < 26; ++i) {
                    const uint8_t ch = g_.manager_records.at(size_t(lender) * kManagerRecordSize + 0xa8 + i);
                    if (!ch) break;
                    who += char(ch);
                }
            }
            const std::string text = who + message_text(g_, ::bmh::layout::CareerSession_Int32T_TextPointer4) +
                format_number(g_, result.refund, 0x101) + message_text(g_, ::bmh::layout::CareerSession_Int32T_TextPointer5);
            credit_notice = ui::BankState::CreditNotice{result.refund, ui::original_to_utf8(text)};
        }
    } else {
        error = "unsupported_bank_request"; return false;
    }
    last_property_notice_ = std::move(property_notice);
    last_credit_notice_ = std::move(credit_notice);
    ++revision;
    return true;
}

// True when a construction tip dialog is open and the process timer (when running) has no time
// left; read-only.
bool CareerSession::stadium_tip_deadline_expired() const {
    if (!stadium_.opened || (!stadium_.tip &&
        (!stadium_.block_expansion || !stadium_.block_expansion->tip))) return false;
    const auto byte = [this](uint32_t address) {
        if (g_.memory.has(address)) return g_.memory.u8(address);
        const auto found = club_modal_runtime_.find(address);
        return found == club_modal_runtime_.end() ? exe_.u8(address) : found->second;
    };
    const auto word = [&byte](uint32_t address) {
        uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) value |= uint32_t(byte(address + i)) << (8 * i);
        return value;
    };
    return word(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField2) != 0 && int32_t(word(::bmh::layout::CareerSession_FormatError_ExecutableField2)) < 1;
}

// Stadium group: an owner (stadium_) is opened with a scope id and every later request must echo
// scope and sequence, the manager, club, stable club identity, game mode and revision. All
// actions (data page, periphery quote/build, block expansion editor with its tip, investments)
// run inside the apply lambda below; on failure the stadium checkpoint is restored, on success
// revision, sequence and the owner's revision advance. Exceptions restore the checkpoint and
// the revision and are rethrown. Short error codes in 'error' otherwise.
// Older periphery callers send no scope envelope; a compatibility step fills it in for their four
// original operations only.
bool CareerSession::dispatch_stadium(const ui::StadiumRequest& supplied_request, uint64_t& revision, std::string& error,
                                    const ConstructionNoticeHook& notice_presenter,
                                    const SeasonTicketCompensationNoticeHook& compensation_presenter) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    const int active = int(g_.memory.u8(::bmh::layout::ActiveManager));
    auto request = supplied_request;
    // Existing CLI periphery callers predate the UI scope envelope. Restrict
    // this compatibility adapter to their four original operations and an
    // entirely absent envelope; investments and close remain strictly owned.
    const bool legacy_periphery = !request.stable_club_identity && request.scope == 0 && request.sequence == 0 &&
        (request.kind == ui::StadiumRequestKind::open || request.kind == ui::StadiumRequestKind::quote_periphery ||
         request.kind == ui::StadiumRequestKind::decline_periphery || request.kind == ui::StadiumRequestKind::build_periphery);
    if (legacy_periphery && active >= 0 && active <= 3 && size_t(active) < g_.manager_teams.size() &&
        request.runtime_club >= 0 && size_t(request.runtime_club) < g_.club_ids.size()) {
        request.stable_club_identity = int(g_.club_ids[size_t(request.runtime_club)]);
        if (request.kind != ui::StadiumRequestKind::open) {
            request.scope = stadium_.scope; request.sequence = stadium_.sequence;
        }
    }
    const bool block_action = is_block_expansion_action(request.kind);
    if (!block_action && (request.block_slot != -1 || request.block_total_places != 0 ||
                          request.block_features != 0 || request.expected_block_quote)) {
        error = "unexpected_block_expansion_fields"; return false;
    }
    if (request.manager_slot != active || active < 0 || active > 3 ||
        size_t(active) >= g_.manager_teams.size() || request.runtime_club != g_.manager_teams[size_t(active)] ||
        request.runtime_club < 0 || request.runtime_club >= 357 ||
        size_t(request.runtime_club) >= g_.club_ids.size() ||
        request.stable_club_identity != std::optional<int>(int(g_.club_ids[size_t(request.runtime_club)])) ||
        request.revision != revision || revision == UINT64_MAX || !g_.memory.has(::bmh::layout::GameMode)) {
        error = "stale_manager_or_revision"; return false;
    }
    const size_t record = size_t(active) * kManagerRecordSize;
    if (record + 0xb0c > g_.manager_records.size() ||
        (int(g_.manager_records[record + 0x290]) | int(g_.manager_records[record + 0x291]) << 8) != request.runtime_club) {
        error = "invalid_stadium_owner"; return false;
    }
    const int mode = g_.memory.u8(::bmh::layout::GameMode);
    if (mode < 1 || mode > 3) { error = "invalid_stadium_mode"; return false; }
    if (request.kind == ui::StadiumRequestKind::open) {
        const bool legacy_reentry = legacy_periphery && !stadium_.data_open && !stadium_.investment && !stadium_.tip &&
            !stadium_.block_expansion;
        if ((stadium_.opened && !legacy_reentry) || request.scope != 0 || request.sequence != 0 || request.object != -1 ||
            request.option != -1 || request.price != 0 || request.accept_tip || next_stadium_scope_ == UINT64_MAX) {
            error = "invalid_stadium_open"; return false;
        }
    } else if (!stadium_.opened || stadium_.manager != active || stadium_.club != request.runtime_club ||
               stadium_.stable_club != *request.stable_club_identity || stadium_.mode != mode ||
               stadium_.revision != revision || request.scope != stadium_.scope ||
               request.sequence != stadium_.sequence || stadium_.sequence == UINT64_MAX) {
        error = "stale_stadium_scope"; return false;
    }
    auto before = stadium_checkpoint();
    const uint64_t before_revision = revision;
    try {
        // Performs the validated stadium action and returns true on success; 'invalid' records the
        // reason and returns false.
        const auto apply = [&]() -> bool {
            const auto invalid = [&error](const char* reason) { error = reason; return false; };
            const auto plain = [&request](int object = -1) {
                return request.object == object && request.option == -1 && request.price == 0 && !request.accept_tip;
            };
            const auto clear_investment = [this] {
                stadium_.investment.reset(); stadium_.selection_status.reset(); stadium_.option.reset(); stadium_.tip.reset();
                stadium_.option_index = -1; stadium_.text_variant = -1;
            };
            StadiumInvestmentConstants constants;
            for (unsigned i = 0; i < 8; ++i) {
                constants.price[i] = exe_.u32(::bmh::layout::CareerSession_DispatchStadium_RuntimeField + i * 4);
                constants.weeks[i] = exe_.u8(::bmh::layout::CareerSession_DispatchStadium_RuntimeField2 + (14 + i) * 5);
            }
            const auto options = [&](StadiumInvestment investment) {
                return stadium_investment_options_source(g_, active, investment, stadium_.price_percent, constants);
            };
            const auto selected_valid = [&]() {
                if (!stadium_.investment || !stadium_.option || stadium_.option_index < 0) return false;
                const auto current = options(*stadium_.investment);
                return size_t(stadium_.option_index) < current.size() &&
                    current[size_t(stadium_.option_index)] == *stadium_.option;
            };
            if (request.kind == ui::StadiumRequestKind::open) {
                StadiumRuntime opened;
                opened.opened = true; opened.manager = active; opened.club = request.runtime_club;
                opened.stable_club = *request.stable_club_identity; opened.mode = mode;
                opened.scope = next_stadium_scope_++;
                opened.price_percent = construction_price_percent(g_, active, game_);
                if (opened.price_percent < 1 || opened.price_percent > 427) return invalid("invalid_stadium_discount");
                stadium_ = std::move(opened);
                return true;
            }
            if (request.kind == ui::StadiumRequestKind::close) {
                if (stadium_.data_open || !plain() || stadium_.investment || stadium_.periphery_quote || stadium_.tip ||
                    stadium_.block_expansion)
                    return invalid("invalid_stadium_close");
                stadium_.opened = false; stadium_.periphery_quote.reset(); clear_investment();
                stadium_.feedback.clear(); return true;
            }
            const bool data_action = request.kind == ui::StadiumRequestKind::open_data ||
                request.kind == ui::StadiumRequestKind::toggle_data_curve ||
                request.kind == ui::StadiumRequestKind::toggle_data_scale ||
                request.kind == ui::StadiumRequestKind::set_data_ticket_price ||
                request.kind == ui::StadiumRequestKind::close_data;
            if (data_action) {
                if (stadium_.investment || stadium_.periphery_quote || stadium_.tip || stadium_.block_expansion ||
                    request.option != -1 || request.price != 0 || request.accept_tip)
                    return invalid("invalid_stadium_data_context");
                if (request.kind == ui::StadiumRequestKind::open_data) {
                    if (stadium_.data_open || request.object != -1) return invalid("invalid_stadium_data_open");
                    const uint32_t flags_at = ::bmh::layout::Flags + uint32_t(active) * 3 + 1;
                    if (!g_.memory.has(flags_at)) return invalid("missing_stadium_data_flags");
                    stadium_.data_flags = g_.memory.u8(flags_at);
                    // Build the pure projection before accepting modal ownership.
                    (void)ui::stadium_data_from_game(g_, exe_, active, stadium_.data_flags);
                    stadium_.data_open = true;
                } else {
                    if (!stadium_.data_open) return invalid("stadium_data_not_open");
                    if (request.kind == ui::StadiumRequestKind::toggle_data_curve) {
                        if (request.object < 0 || request.object > 5) return invalid("invalid_stadium_data_curve");
                        stadium_.data_flags ^= uint8_t(1u << request.object);
                    } else if (request.kind == ui::StadiumRequestKind::toggle_data_scale) {
                        if (request.object < 0 || request.object > 1) return invalid("invalid_stadium_data_scale");
                        stadium_.data_flags ^= 0xc0; // Either ABS or REL button toggles both Source bits.
                    } else if (request.kind == ui::StadiumRequestKind::set_data_ticket_price) {
                        if (request.object < 10 || request.object > 25) return invalid("invalid_stadium_data_ticket");
                        g_.manager_records[record + 0x26e] = uint8_t(request.object);
                        (void)ui::stadium_data_from_game(g_, exe_, active, stadium_.data_flags);
                    } else {
                        if (request.object != -1) return invalid("invalid_stadium_data_close");
                        g_.memory.set8(::bmh::layout::Flags + uint32_t(active) * 3 + 1, stadium_.data_flags);
                        stadium_.data_open = false;
                    }
                }
                stadium_.feedback.clear();
                return true;
            }
            if (stadium_.data_open) return invalid("stadium_data_active");
            if (mode == 1 && !legacy_periphery &&
                (request.kind == ui::StadiumRequestKind::quote_periphery ||
                 request.kind == ui::StadiumRequestKind::build_periphery ||
                 request.kind == ui::StadiumRequestKind::decline_periphery))
                return invalid("stadium_periphery_disabled");
            if (request.kind == ui::StadiumRequestKind::quote_periphery) {
                if (stadium_.investment || stadium_.periphery_quote || stadium_.block_expansion || request.object < 0 ||
                    request.object >= 14 || !plain(request.object)) return invalid("invalid_periphery_offer");
                const size_t object = size_t(request.object);
                const uint8_t level = g_.manager_records[record + object];
                if (g_.manager_records[record + 0x1e + object] != 0 || (object == 0 ? level >= 0xab : level != 0))
                    return invalid("periphery_unavailable");
                const int32_t price = periphery_offer(g_, request.object, game_);
                if (price <= 0) return invalid("invalid_periphery_price");
                stadium_.periphery_quote = std::make_pair(request.object, price);
                stadium_.feedback.clear(); return true;
            }
            if (request.kind == ui::StadiumRequestKind::decline_periphery ||
                request.kind == ui::StadiumRequestKind::build_periphery) {
                if (stadium_.investment || stadium_.block_expansion || !stadium_.periphery_quote ||
                    request.option != -1 || request.accept_tip ||
                    request.object != stadium_.periphery_quote->first || request.price != stadium_.periphery_quote->second)
                    return invalid("stale_periphery_offer");
                if (request.kind == ui::StadiumRequestKind::build_periphery) {
                    const size_t object = size_t(request.object);
                    const uint8_t level = g_.manager_records[record + object];
                    if (g_.manager_records[record + 0x1e + object] != 0 || (object == 0 ? level >= 0xab : level != 0))
                        return invalid("periphery_unavailable");
                    // ConstructionPricePercent consumes this genuine JA even when PayConstruction's
                    // payment fails. The Source presenter runs before the quote
                    // is cleared; payment/sponsor effects are not rolled back.
                    const bool built = build_periphery(g_, active, request.object,
                        uint32_t(request.price), rand_, notice_presenter);
                    stadium_.feedback = built || notice_presenter ? std::string{} : message_text(g_, ::bmh::layout::Advertising_FormatError_TextPointer18);
                }
                stadium_.periphery_quote.reset();
                if (request.kind == ui::StadiumRequestKind::decline_periphery) stadium_.feedback.clear();
                return true;
            }

            // A–H uses its own quote/commit domain and remains available in
            // Source mode 1, where facilities are disabled. All request fields
            // outside the typed block envelope must stay neutral.
            if (block_action) {
                if (request.object != -1 || request.option != -1 || request.price != 0 ||
                    (request.kind != ui::StadiumRequestKind::expansion_answer_tip && request.accept_tip))
                    return invalid("invalid_block_expansion_envelope");
                const auto block_constants=stadium_block_expansion_constants(exe_);
                const auto quote_block = [&](StadiumBlockSlot slot, uint32_t total, uint8_t features) {
                    return quote_stadium_block_expansion(g_, active, slot, total, features,
                                                         stadium_.price_percent, block_constants);
                };
                if (request.kind == ui::StadiumRequestKind::expansion_select) {
                    if (stadium_.investment || stadium_.periphery_quote || stadium_.block_expansion ||
                        request.block_slot < 0 || request.block_slot >= 8 || request.block_total_places != 0 ||
                        request.block_features != 0 || request.expected_block_quote)
                        return invalid("invalid_block_expansion_selection");
                    const auto slot = static_cast<StadiumBlockSlot>(request.block_slot);
                    const uint8_t raw = g_.manager_records.at(record + 0x44 + size_t(request.block_slot));
                    const uint32_t current = uint32_t(g_.manager_records.at(record + 0x4c + size_t(request.block_slot))) * 1000;
                    auto quote = quote_block(slot, current, uint8_t(raw & kBlockFeatures));
                    if (!quote) return invalid("block_expansion_unavailable");
                    StadiumRuntime::BlockExpansion draft;
                    draft.quote = *quote;
                    draft.total_target_places = current;
                    draft.desired_features = uint8_t(raw & kBlockFeatures);
                    game_.set_seed(int32_t(g_.memory.u32(::bmh::layout::CareerSession_Invalid_RuntimeField2)));
                    draft.header_text_variant = game_.range(3, 10); // Front, once on editor entry.
                    stadium_.block_expansion = std::move(draft);
                    stadium_.feedback.clear();
                    return true;
                }

                if (!stadium_.block_expansion || stadium_.investment || stadium_.periphery_quote ||
                    request.block_slot < 0 || request.block_slot >= 8 ||
                    request.block_slot != int(stadium_.block_expansion->quote.slot) ||
                    !request.expected_block_quote || *request.expected_block_quote != stadium_.block_expansion->quote)
                    return invalid("stale_block_expansion_quote");
                auto& draft = *stadium_.block_expansion;
                if (draft.tip) {
                    const auto& tip = *draft.tip;
                    const bool valid_tip = tip.decision == StandBlockOfferDecision::Pending &&
                        tip.base_price == draft.quote.base_price && tip.base_days == draft.quote.base_weeks &&
                        tip.price == tip.base_price && tip.percent >= 6 && tip.percent <= 14 &&
                        tip.offered_price == uint32_t(uint64_t(tip.base_price) * uint32_t(100 + tip.percent) / 100);
                    if (!valid_tip) return invalid("stale_block_expansion_tip");
                }
                auto fresh = quote_block(draft.quote.slot, draft.total_target_places, draft.desired_features);
                if (!fresh || *fresh != draft.quote)
                    return invalid("block_expansion_source_changed");

                const auto matches_draft = [&] {
                    return request.block_total_places == draft.total_target_places &&
                        request.block_features == draft.desired_features;
                };
                switch (request.kind) {
                case ui::StadiumRequestKind::expansion_set_total: {
                    if (draft.tip || request.block_features != draft.desired_features)
                        return invalid("stale_block_expansion_draft");
                    auto next_quote = quote_block(draft.quote.slot, request.block_total_places, draft.desired_features);
                    if (!next_quote) return invalid("invalid_block_expansion_total");
                    draft.total_target_places = request.block_total_places;
                    draft.quote = *next_quote;
                    stadium_.feedback.clear();
                    return true;
                }
                case ui::StadiumRequestKind::expansion_toggle_feature: {
                    const uint8_t changed = uint8_t(request.block_features ^ draft.desired_features);
                    if (draft.tip || request.block_total_places != draft.total_target_places ||
                        (request.block_features & uint8_t(~kBlockFeatures)) != 0 ||
                        (changed != kBlockSeat && changed != kBlockRoof) ||
                        (request.block_features & uint8_t(draft.quote.current_raw_flags & kBlockFeatures)) !=
                            uint8_t(draft.quote.current_raw_flags & kBlockFeatures))
                        return invalid("invalid_block_expansion_feature");
                    auto next_quote = quote_block(draft.quote.slot, draft.total_target_places, request.block_features);
                    if (!next_quote) return invalid("invalid_block_expansion_feature");
                    draft.desired_features = request.block_features;
                    draft.quote = *next_quote;
                    stadium_.feedback.clear();
                    return true;
                }
                case ui::StadiumRequestKind::expansion_cancel:
                    if (draft.tip || !matches_draft()) return invalid("stale_block_expansion_cancel");
                    stadium_.block_expansion.reset();
                    stadium_.feedback.clear();
                    return true;
                case ui::StadiumRequestKind::expansion_confirm: {
                    if (draft.tip || !matches_draft()) return invalid("stale_block_expansion_confirm");
                    if (draft.quote.base_price == 0) {
                        stadium_.block_expansion.reset();
                        stadium_.feedback.clear();
                        return true;
                    }
                    auto tip = prepare_stand_block_offer(draft.quote.base_price, draft.quote.base_weeks, rand_);
                    if (tip.offered_price > uint32_t(INT32_MAX))
                        return invalid("block_expansion_tip_price_overflow");
                    draft.tip = tip;
                    stadium_.feedback.clear();
                    return true;
                }
                case ui::StadiumRequestKind::expansion_answer_tip: {
                    if (!draft.tip || !matches_draft() || request.accept_tip &&
                        uint64_t(draft.tip->base_price) * uint32_t(100 + draft.tip->percent) >
                            uint64_t(INT32_MAX))
                        return invalid("invalid_block_expansion_tip_answer");
                    StandBlockOffer resolved = *draft.tip;
                    resolve_stand_block_offer(resolved, request.accept_tip, rand_);
                    const bool built = build_stadium_block_expansion(g_, active, draft.quote.slot,
                        block_constants, draft.quote, resolved, rand_, notice_presenter, compensation_presenter);
                    stadium_.block_expansion.reset();
                    stadium_.feedback = built || notice_presenter ? std::string{} : message_text(g_, ::bmh::layout::Advertising_FormatError_TextPointer18);
                    return true;
                }
                default:
                    return invalid("unsupported_block_expansion_request");
                }
            }
            if (mode == 1) return invalid("stadium_investments_disabled");
            if (request.kind == ui::StadiumRequestKind::select_investment) {
                if (stadium_.investment || stadium_.periphery_quote || stadium_.block_expansion || request.object < 0 ||
                    request.object >= 8 || !plain(request.object)) return invalid("invalid_investment_selection");
                const auto investment = static_cast<StadiumInvestment>(request.object);
                const auto current = options(investment);
                if (current.empty()) return invalid("investment_unavailable");
                game_.set_seed(int32_t(g_.memory.u32(::bmh::layout::CareerSession_Invalid_RuntimeField2)));
                stadium_.text_variant = game_.range(3, 10); // Front: exactly once at dialog entry.
                stadium_.investment = investment; stadium_.selection_status = current.front().current_status;
                stadium_.option_index = -1; stadium_.option.reset(); // Source iVar5=-1 until an explicit tier click.
                stadium_.feedback.clear(); return true;
            }
            if (request.kind == ui::StadiumRequestKind::cancel_investment) {
                if (!stadium_.investment || stadium_.tip || !plain(int(*stadium_.investment)))
                    return invalid("stale_investment_selection");
                clear_investment(); stadium_.feedback.clear(); return true; // Initial Abbruch precedes W(6,14).
            }
            if (request.kind == ui::StadiumRequestKind::select_investment_option) {
                if (!stadium_.investment || stadium_.tip || request.object != int(*stadium_.investment) ||
                    request.option < 0 || request.accept_tip || !stadium_.selection_status ||
                    g_.manager_records[record + 0x3c + size_t(*stadium_.investment)] != *stadium_.selection_status)
                    return invalid("stale_investment_selection");
                const auto current = options(*stadium_.investment);
                if (size_t(request.option) >= current.size() || request.price != int32_t(current[size_t(request.option)].base_price))
                    return invalid("stale_investment_price");
                stadium_.option_index = request.option; stadium_.option = current[size_t(request.option)];
                return true; // Pure tier choice: neither Source generator advances.
            }
            if (request.kind == ui::StadiumRequestKind::confirm_investment) {
                if (stadium_.tip || !selected_valid() || request.object != int(*stadium_.investment) ||
                    request.option != stadium_.option_index || request.accept_tip ||
                    request.price != int32_t(stadium_.option->base_price)) return invalid("stale_investment_selection");
                stadium_.tip = prepare_stand_block_offer(stadium_.option->base_price, stadium_.option->base_weeks, rand_);
                if (stadium_.tip->offered_price > uint32_t(INT32_MAX)) return invalid("invalid_investment_tip_price");
                return true;
            }
            if (request.kind == ui::StadiumRequestKind::answer_construction_tip) {
                if (!stadium_.tip || !selected_valid() || request.object != int(*stadium_.investment) ||
                    request.option != stadium_.option_index || request.price != int32_t(request.accept_tip ?
                        stadium_.tip->offered_price : stadium_.tip->base_price)) return invalid("stale_construction_tip");
                auto resolved = *stadium_.tip;
                resolve_stand_block_offer(resolved, request.accept_tip, rand_);
                const bool built = build_stadium_investment_source(g_, active, *stadium_.investment,
                    stadium_.price_percent, constants, *stadium_.option, resolved, rand_, notice_presenter);
                // A genuine PayConstruction/BookMoney2 refusal consumes the answer and
                // retains the sponsor share clearing and Source W draws.
                // Interactive callers have already shown Source's timed funds
                // notice; headless legacy callers retain their text feedback.
                clear_investment();
                stadium_.feedback = built || notice_presenter ? std::string{} : message_text(g_, ::bmh::layout::Advertising_FormatError_TextPointer18);
                return true;
            }
            return invalid("unsupported_stadium_request");
        };
        if (!apply()) { restore_stadium_checkpoint(std::move(before)); return false; }
        ++revision; ++stadium_.sequence; stadium_.revision = revision;
        error.clear(); return true;
    } catch (...) {
        restore_stadium_checkpoint(std::move(before)); revision = before_revision; throw;
    }
}

// Submits a pools coupon for the active manager; only valid when the coupon has eleven matches.
// Revision plus one; false with a short error code otherwise.
bool CareerSession::dispatch_toto(const ui::TotoRequest& request, uint64_t& revision, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    const int active = int(g_.memory.u8(::bmh::layout::ActiveManager));
    if (request.kind != ui::TotoRequestKind::submit || request.manager_slot != active || request.manager_slot < 0 ||
        size_t(request.manager_slot) >= g_.manager_teams.size() ||
        request.runtime_club != g_.manager_teams[size_t(request.manager_slot)] || request.revision != revision) {
        error = "stale_manager_or_revision"; return false;
    }
    if (g_.memory.u8(::bmh::layout::CareerSession_DispatchToto_RuntimeField) != 11) { error = "toto_coupon_requires_eleven_matches"; return false; }
    if (!submit_pools_coupon(g_, request.manager_slot, request.stake, request.tips)) {
        error = "invalid_toto_coupon"; return false;
    }
    ++revision;
    return true;
}

}  // namespace bmh::app
