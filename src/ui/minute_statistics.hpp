// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string_view>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/vga.hpp"
#include "game/state.hpp"

namespace bmh::ui {

// HandleSourceEvent2 reads four independent 18-byte blocks from the active manager
// record. Keep every stored byte, including the second series whose producer
// is not yet identified in the current port/source search.
struct MinuteStatisticsProjection {
    int manager_slot = -1;
    std::array<std::array<uint8_t, 18>, 4> source_series{};
    bool operator==(const MinuteStatisticsProjection&) const = default;
};

MinuteStatisticsProjection minute_statistics_from_game(const GameState& game, int manager_slot);

// This is an overlay only. Its caller supplies the accepted page-8 underlay;
// the Original function draws no new title, caption, or menu controls here.
class MinuteStatisticsScreen {
public:
    MinuteStatisticsScreen(const std::filesystem::path& data_directory,
                           MinuteStatisticsProjection projection);

    Image render(const Image& accepted_underlay) const;

    // HandleSourceEvent2 exits on source event 8. This API does not assert which SDL
    // device action, if any, maps to that event.
    bool handle_source_event(uint8_t event_code);
    bool closed() const { return closed_; }
    const MinuteStatisticsProjection& projection() const { return projection_; }

private:
    void draw_text(Image& image, int x, int baseline, std::string_view text, uint8_t color) const;

    LeImage exe_;
    Image chart_atlas_;
    Font font_;
    MinuteStatisticsProjection projection_;
    bool closed_ = false;
};

} // namespace bmh::ui
