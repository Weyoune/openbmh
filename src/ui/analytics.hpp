// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "formats/vga.hpp"
#include "game/state.hpp"
#include "ui/sprite_font.hpp"
#include "ui/desktop.hpp"

namespace bmh::ui {

// Case 2 of StatisticsMenuScreen opens CopyOpaqueSlice(board, 0): the German
// “Analytics” page, with six Source series. Values are copied, never simulated.
struct AnalyticsSeries {
    std::string label;
    uint8_t color = 0;
    int32_t absolute_min = 0;
    int32_t absolute_max = 0;
    std::vector<int32_t> values;
    bool operator==(const AnalyticsSeries&) const = default;
};

struct AnalyticsState {
    DeskRequest owner;
    int manager_slot = -1;
    uint8_t source_flags = 0; // Flags[manager*3], param2=0.
    std::array<AnalyticsSeries, 6> series;
    Image underlay; // supplied by the accepted statistics-parent render.
};

enum class AnalyticsRequestKind : uint8_t { toggle_metric, toggle_absolute_scale, close };
struct AnalyticsRequest {
    DeskRequest owner;
    AnalyticsRequestKind kind = AnalyticsRequestKind::toggle_metric;
    int manager_slot = -1;
    int metric = -1;
    uint8_t expected_flags = 0;
    uint8_t target_flags = 0;
    bool operator==(const AnalyticsRequest&) const = default;
};

AnalyticsState analytics_from_game(const GameState&, const LeImage&, int manager_slot,
                                   const Image& accepted_parent_underlay);

class AnalyticsScreen {
public:
    AnalyticsScreen(const std::filesystem::path& data, AnalyticsState);
    Image render() const;
    bool click(int x, int y);
    bool pointer_down(int x, int y, bool right = false);
    const std::optional<AnalyticsRequest>& pending() const { return pending_; }
    void acknowledge(uint8_t accepted_flags, DeskRequest accepted_owner);
    void reject() { pending_.reset(); }
    // Local display preference only. The containing owner must persist/ack
    // source_flags through its normal statistics-session contract.
    bool toggle_metric(int index);
    bool toggle_absolute_scale();
    const AnalyticsState& state() const { return state_; }
private:
    AnalyticsState state_;
    LeImage exe_;
    Image buttons_;
    SpriteFont sprite_;
    TextDb texts_;
    std::optional<AnalyticsRequest> pending_;
    int pressed_metric_ = -1;
    bool pressed_absolute_ = false;
};

} // namespace bmh::ui
