// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "modern/game/modern_state.hpp"
#include "modern/game/club_player_detail.hpp"

#include <algorithm>
#include <stdexcept>

#include "modern/game/matchday.hpp"
#include "modern/game/month.hpp"
#include "modern/game/national.hpp"
#include "game/lineup.hpp"
#include "modern/game/weekly.hpp"

namespace bmh::modern {
namespace {
constexpr size_t kRecord = 0xb0c, kBoard = 0x840, kBoardRow = 0x58;

uint16_t record_word(const ModernState& g, size_t manager, size_t offset) {
    const size_t at = manager * kRecord + offset;
    if (at + 2 > g.manager_records.size()) throw std::invalid_argument("player detail: truncated manager record");
    return uint16_t(g.manager_records[at] | uint16_t(g.manager_records[at + 1]) << 8);
}

int source_manager(const ModernState& g, int club) {
    const size_t count = g.memory.has(::bmh::layout::ManagerCount) ? g.memory.u8(::bmh::layout::ManagerCount) : g.manager_teams.size();
    if (count > 4 || count > g.manager_teams.size()) throw std::invalid_argument("player detail: invalid manager count");
    for (size_t m = 0; m < count; ++m) if (g.manager_teams[m] == club) return int(m);
    if (count && g.manager_teams[0] == no_team() && record_word(g, 4, 0x290) == club) return 4;
    return -1;
}

int club_of_slot(int slot) {
    if (slot < 0) return -1;
    if (slot < 90 * 24) return slot / 24;
    if (slot < 90 * 24 + 48 * 16) return (slot - 90 * 8) / 16;
    if (slot >= pool_end_slot() && slot < pool_end_slot() + 24) return no_team();
    return -1;
}

std::array<uint8_t, kBoardRow> board_copy(const ModernState& g, int manager, int row) {
    const size_t at = size_t(manager) * kBoard + size_t(row) * kBoardRow;
    if (at + kBoardRow > g.tactics.size()) throw std::invalid_argument("player detail: truncated manager board");
    std::array<uint8_t, kBoardRow> out{};
    std::copy_n(g.tactics.begin() + static_cast<std::ptrdiff_t>(at), out.size(), out.begin());
    return out;
}
} // namespace

ClubPlayerDetailContext club_player_detail_context(const ModernState& g, const ClubPlayerDetailSelection& s) {
    if ((s.origin != ClubPlayerDetailOrigin::squad && s.origin != ClubPlayerDetailOrigin::market_pool) ||
        s.player_id < 0 || size_t(s.player_id) >= g.players.size() ||
        size_t(s.player_id) >= g.player_cards.size() || size_t(s.player_id) >= g.player_bonus.size())
        throw std::invalid_argument("player detail: invalid player identity");
    ClubPlayerDetailContext out;
    out.selection = s;
    out.active_manager = g.memory.has(::bmh::layout::ActiveManager) ? g.memory.u8(::bmh::layout::ActiveManager) : 0;
    if (out.active_manager < 0 || size_t(out.active_manager) >= g.manager_teams.size())
        throw std::invalid_argument("player detail: invalid active manager");
    if (s.origin == ClubPlayerDetailOrigin::market_pool) {
        if (s.runtime_team != -1 || s.physical_row != -1 || s.human_loan_index != -1 ||
            s.market_pool_slot < int(transfer_list_slot()) ||
            s.market_pool_slot >= int(transfer_list_slot()) + kClubPlayerDetailMarketPoolCount ||
            s.rendered_row < 0 || s.rendered_row >= kClubPlayerDetailMarketPoolCount ||
            size_t(s.market_pool_slot) >= g.squad_slots.size())
            throw std::invalid_argument("player detail: invalid transfer-market slot");
        out.source_slot = s.market_pool_slot;
        out.displayed_slot = g.squad_slots[size_t(out.source_slot)];
        if (out.displayed_slot == kEmptySlot || (out.displayed_slot & 0x0fff) != s.player_id)
            throw std::invalid_argument("player detail: stale transfer-market identity");
        out.market_pool = true;
        out.source_manager = -1;
        out.primary_club = -1;
    } else {
        if (s.rendered_row < 0 || s.rendered_row >= 24 || s.runtime_team < 0 ||
            (s.runtime_team >= league_clubs() && s.runtime_team != no_team()))
            throw std::invalid_argument("player detail: invalid runtime club");
        const auto range = squad_range(s.runtime_team);
        if (range.first + range.count > g.squad_slots.size()) throw std::invalid_argument("player detail: truncated squad");
        out.source_manager = source_manager(g, s.runtime_team);
        out.primary_club = s.runtime_team;
    }
    if (s.origin == ClubPlayerDetailOrigin::market_pool) {
        // The validated pool identity is already loaded. National resolution
        // below may select a real squad copy; offers never imply a board.
    } else if (s.human_loan_index >= 0) {
        const auto range = squad_range(s.runtime_team);
        if (s.human_loan_index >= 12 || s.physical_row != -1 || s.runtime_team == no_team() || out.source_manager < 0)
            throw std::invalid_argument("player detail: invalid human loan row");
        const uint32_t loan = uint32_t(s.human_loan_index);
        const uint32_t player_at = ::bmh::layout::Career_CareerManagersEnd_RuntimeField + loan * 2, owner_at = ::bmh::layout::ClubInfo_FormatError_RuntimeField2 + loan, board_at = ::bmh::layout::ClubInfo_FormatError_RuntimeField + loan * kBoardRow;
        if (!g.memory.has(player_at + 1) || !g.memory.has(owner_at) || !g.memory.has(board_at + kBoardRow - 1) ||
            g.memory.u16(player_at) != s.player_id || g.memory.u8(owner_at) != out.source_manager)
            throw std::invalid_argument("player detail: stale human loan identity");
        out.human_loan = true;
        out.source_slot = int(range.first) + s.rendered_row;
        out.displayed_slot = uint16_t(s.player_id);
        out.board.emplace();
        for (size_t i = 0; i < kBoardRow; ++i) (*out.board)[i] = g.memory.u8(board_at + uint32_t(i));
    } else {
        const auto range = squad_range(s.runtime_team);
        if (s.human_loan_index != -1 || s.physical_row < 0 || size_t(s.physical_row) >= range.count)
            throw std::invalid_argument("player detail: invalid physical row");
        out.source_slot = int(range.first) + s.physical_row;
        out.displayed_slot = g.squad_slots[size_t(out.source_slot)];
        if ((out.displayed_slot & 0x0fff) != s.player_id) throw std::invalid_argument("player detail: stale physical identity");
    }
    if (!out.human_loan) {
        if (g.manager_teams[size_t(out.active_manager)] == no_team()) {
            const auto national = squad_range(no_team());
            if (national.first + national.count > g.squad_slots.size()) throw std::invalid_argument("player detail: truncated national squad");
            bool selected = false;
            for (size_t row = 0; row < national.count; ++row)
                if ((g.squad_slots[national.first + row] & 0x0fff) == s.player_id) selected = true;
            out.national_mode = selected ? 2 : 1;
            // ClubPlayerDetailFromGame remaps BOTH modes to the first matching slot below
            // LargeSquadClubCount, including pool slots, before resolving club/owner/board.
            size_t slot = 0;
            while (slot < national.first && (g.squad_slots[slot] & 0x0fff) != s.player_id) ++slot;
            if (slot == national.first) throw std::invalid_argument("player detail: national player has no source slot before national squad");
            out.source_slot = int(slot);
            out.primary_club = club_of_slot(out.source_slot);
            out.secondary_club = selected ? no_team() : -1;
            out.displayed_slot = g.squad_slots[slot];
            out.source_manager = out.primary_club < 0 ? -1 : source_manager(g, out.primary_club);
        }
        if (out.national_mode != 0 && out.primary_club == int(record_word(g, 4, 0x290)) && g.memory.has(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField3)) {
            const uint8_t owner = g.memory.u8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField3); // literal SquadRange override
            out.source_manager = owner == 0xff ? -1 : owner;
        }
        if (out.source_manager >= 0) {
            const auto owner_range = squad_range(out.primary_club);
            const int row = out.source_slot - int(owner_range.first);
            if (row < 0 || size_t(row) >= owner_range.count) throw std::invalid_argument("player detail: board row outside owner squad");
            out.board = board_copy(g, out.source_manager, row);
        }
    }
    for (int loan = 0; loan < 200; ++loan) {
        const uint32_t at = ::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(loan) * 4;
        if (!g.memory.has(at + 3)) continue;
        const uint32_t value = g.memory.u32(at);
        if (value == 0 || (value & 0x0fff) != uint32_t(s.player_id)) continue;
        out.loan_record = loan;
        out.loan_duration = g.memory.has(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField + uint32_t(loan)) ? g.memory.u8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField + uint32_t(loan)) : 0;
        if (out.human_loan || out.national_mode == 0) {
            out.primary_club = int((value >> 12) & 0x3ff);
            out.secondary_club = int((value >> 22) & 0x3ff);
        }
        break;
    }
    if (out.human_loan && out.loan_record < 0) throw std::invalid_argument("player detail: missing packed human loan record");
    out.attributes = g.players[size_t(s.player_id)];
    out.cards = g.player_cards[size_t(s.player_id)];
    out.bonus = g.player_bonus[size_t(s.player_id)];
    out.original_name = g.player_name(s.player_id);
    const uint8_t national_override = g.memory.has(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField2) ? uint8_t(g.memory.u8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField2) & 10) : 0;
    const bool national_label = !out.human_loan && out.national_mode != 0 && national_override == 0 &&
        out.attributes.nationality() + ((out.bonus & 0x80) >> 2) == 0;
    for (int offer = 0; !national_label && offer < 4; ++offer) {
        const uint32_t at = ::bmh::layout::OfferSlots + uint32_t(offer) * 4;
        if (!g.memory.has(at + 3)) continue;
        const uint32_t value = g.memory.u32(at);
        if ((out.source_manager < 0 || int(value & 31) == out.source_manager) &&
            int((value >> 10) & 0x0fff) == s.player_id) { out.active_offer = offer; break; }
    }
    // ClubPlayerDetail_InvalidArgument_RuntimeField2/nationality select the label and the ResolvePendingOffer fallback, not the
    // ClubPlayerDetailRedraw dispatch: any existing action remains national in modes1/2.
    out.national_action_available = !out.human_loan && out.national_mode != 0 &&
        (national_label || out.active_offer >= 0);
    return out;
}

ClubPlayerDetailRedraw club_player_detail_redraw(const ModernState& g, const ClubPlayerDetailSelection& selection,
                                                const LeImage& original, GameRandom& game, WatcomRandom& rand,
                                                bool portrait_available) {
    ClubPlayerDetailRedraw out;
    out.context = modern::club_player_detail_context(g, selection);
    std::array<uint8_t, 5> feature_count{};
    for (size_t i = 0; i < feature_count.size(); ++i) {
        feature_count[i] = original.u8(::bmh::layout::ClubPlayerDetail_ClubPlayerDetailRedraw_RuntimeField2 + uint32_t(i));
        if (feature_count[i] == 0) throw std::invalid_argument("player detail: empty Original portrait feature range");
    }
    const uint32_t named_players = original.u32(::bmh::layout::ClubPlayerDetail_ClubPlayerDetailRedraw_RuntimeField);
    out.portrait_available = portrait_available;
    if (portrait_available) {
        uint32_t seed = 0x14344;
        if (uint32_t(selection.player_id) < named_players)
            for (size_t i = 0; i < out.context.original_name.size(); ++i)
                seed += uint32_t(int32_t(int8_t(out.context.original_name[i]))) * uint32_t(i) * 10;
        game.set_seed(int32_t(seed));
        game.range(0, 100);
        out.portrait_base = uint8_t(game.range(0, 3));
        for (size_t i = 0; i < out.portrait_features.size(); ++i)
            out.portrait_features[i] = uint8_t(game.range(0, feature_count[i] - 1));
        const uint64_t before = game.calls();
        modern::stir_random(game, rand);
        out.portrait_burn_count = uint16_t(game.calls() - before);
    }
    if (!out.context.human_loan) {
        uint32_t value = 0, salary = 0;
        modern::market_value(g, out.context.source_slot, 0, 100, value, salary, 0, 0, rand);
        out.market_value = ((value + (value >> 2)) / 10000) * 10000;
    }
    return out;
}

bool club_player_detail_national_action(ModernState& g, const ClubPlayerDetailSelection& selection,
                                       WatcomRandom& rand, MinuteReport& report) {
    const auto context = modern::club_player_detail_context(g, selection);
    if (!context.national_action_available)
        throw std::invalid_argument("player detail: national action is not offered by Source");
    const auto national = squad_range(no_team());
    bool applied = true;
    if (context.national_mode == 1)
        applied = modern::add_to_squad(g, context.active_manager, context.source_slot, int(national.first), rand) >= 0;
    else if (!g.memory.has(::bmh::layout::MatchEventsDisabled) || g.memory.u8(::bmh::layout::MatchEventsDisabled) == 0)
        applied = modern::remove_from_seller(g, no_team(), selection.player_id, report);
    auto_lineup(g, no_team(), 0xe4); // ClubPlayerDetailFromGame follows either action with this call.
    return applied;
}
} // namespace bmh::modern
