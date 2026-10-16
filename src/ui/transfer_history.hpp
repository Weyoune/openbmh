// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "formats/vga.hpp"
#include "game/state.hpp"

namespace bmh::ui {

struct TransferHistoryRow {
    uint16_t source_date_counter = 0;
    uint8_t source_kind = 0; // PoolString low two bits: sell/buy/lend/borrow.
    uint32_t source_fee = 0; // packed high bits times 10,000, as displayed by HandleSourceEvent.
    uint16_t saved_player_name_offset = 0;
    uint16_t counterparty_club_offset = 0xffff; // 0xffff uses the Original special label.
    std::string saved_player_name;              // Pool-1 snapshot referenced by the saved row.
    std::string counterparty_club_name;         // Stable pool-0 offset, never a runtime-team lookup.
    bool special_counterparty = false;
    bool operator==(const TransferHistoryRow&) const = default;
};

struct TransferHistoryProjection {
    int manager_slot = -1;
    std::vector<TransferHistoryRow> rows;
    bool operator==(const TransferHistoryProjection&) const = default;
};

// Projects HandleSourceEvent's manager-owned entries at +0x760 and +0x7d8.
TransferHistoryProjection transfer_history_from_game(const GameState& game, int manager_slot);

class TransferHistoryScreen {
public:
    TransferHistoryScreen(const std::filesystem::path& data_directory,
                          const GameState& formatting_state,
                          TransferHistoryProjection projection);

    // Overlay the original history rows on the accepted global-statistics page.
    // `source_origin_y` is TransferHistoryRuntimeField3 from StatisticsMenuScreen; row baselines are
    // source_origin_y + 19 + 6*row.
    Image render(const Image& accepted_underlay, int source_origin_y) const;

    // HandleSourceEvent exits when source event 8 arrives. This does not specify an
    // SDL keyboard or mouse mapping; the page owner supplies the source event.
    bool handle_source_event(uint8_t event_code);
    bool closed() const { return closed_; }
    const TransferHistoryProjection& projection() const { return projection_; }

private:
    void draw_text(Image&, int x, int baseline, std::string_view, uint8_t color) const;
    std::string date_text(uint16_t source_counter) const;

    LeImage exe_;
    TextDb texts_;
    Font font_;
    GameState formatting_state_;
    TransferHistoryProjection projection_;
    bool closed_ = false;
};

} // namespace bmh::ui
