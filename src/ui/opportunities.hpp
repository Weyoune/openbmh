// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "formats/vga.hpp"
#include "ui/sprite_font.hpp"
#include "ui/desktop.hpp"

namespace bmh { struct GameState; }

namespace bmh::ui {

struct OpportunitiesIdentity {
    int manager_slot = -1;
    int runtime_team = -1;
    int stable_club_identity = -1;
    uint32_t day_counter = 0;
    uint64_t revision = 0;
    uint64_t scope = 0;
    uint64_t sequence = 0;
    bool operator==(const OpportunitiesIdentity&) const = default;
};

enum class OpportunitiesAction : uint8_t { select_mode, close };
struct OpportunitiesRequest {
    OpportunitiesIdentity identity;
    OpportunitiesAction action = OpportunitiesAction::select_mode;
    int mode = 0; // Original buttons A..D pass 1,2,4,8 to Void5.
    bool operator==(const OpportunitiesRequest&) const = default;
};

struct OpportunitiesState {
    OpportunitiesIdentity identity;
    int mode = 1;
    int league_size = 0;
    int current_rank = 0;
    std::array<uint8_t, 24> ranks{}; // 0 unreachable, 1 possible, 0xff secured.
    Image underlay;
    bool gradient_text = true;
};

struct StatisticsMenuState {
    DeskRequest owner;
    Image underlay;
    int transfer_history_count = 0;
    bool gradient_text = true;
};
struct StatisticsMenuRequest {
    DeskRequest owner;
    int selected_index = -1; // Original StatisticsMenuScreen index.
    bool close = false;
    bool operator==(const StatisticsMenuRequest&) const = default;
};

class StatisticsMenuScreen {
public:
    StatisticsMenuScreen(const std::filesystem::path& data, StatisticsMenuState state);
    Image render() const;
    // StatisticsMenuScreen prepares the actual implemented child pane before handing it over.
    Image render_page(int selected_index) const;
    bool pointer_down(int x, int y, bool right = false);
    bool release(int x, int y);
    const std::optional<StatisticsMenuRequest>& pending() const { return pending_; }
    void clear_pending() { pending_.reset(); pressed_=-1; }
    // Advance the preserved parent owner after an acknowledged child return.
    void update_owner(DeskRequest owner);
    void reject(std::string reason) { pending_.reset(); rejection_=std::move(reason); }
    const StatisticsMenuState& state() const { return state_; }
    std::string state_json() const;
private:
    LeImage exe_;
    TextDb texts_;
    SpriteFont sprite_;
    Image backdrop_, buttons_, window_;
    Font header_font_, body_font_;
    StatisticsMenuState state_;
    std::optional<StatisticsMenuRequest> pending_;
    std::string rejection_;
    int pressed_=-1;
};

class OpportunitiesScreen {
public:
    OpportunitiesScreen(const std::filesystem::path& data, OpportunitiesState state);
    Image render() const;
    bool pointer_down(int x, int y, bool right = false);
    bool move(int x, int y);
    bool release(int x, int y);
    bool key(std::string_view key);
    void acknowledge(OpportunitiesState state);
    void reject(std::string reason);
    const OpportunitiesState& state() const { return state_; }
    const std::optional<OpportunitiesRequest>& pending() const { return pending_; }
    bool close_requested() const { return close_requested_; }
    std::string state_json() const;
private:
    LeImage exe_;
    TextDb texts_;
    Image backdrop_, ladder_, buttons_;
    Font font_, header_font_;
    SpriteFont sprite_;
    OpportunitiesState state_;
    std::optional<OpportunitiesRequest> pending_;
    std::string rejection_;
    bool close_requested_ = false;
    int pressed_button_ = -1;
    void draw_text(Image&, int x, int baseline, std::string_view text, uint8_t color) const;
};

} // namespace bmh::ui
