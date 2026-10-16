// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/font.hpp"
#include "formats/textd.hpp"
#include "ui/league_family_request.hpp"
#include "ui/strengths_state.hpp"
#include "ui/sprite_font.hpp"

namespace bmh::ui {

enum class StrengthsAction : uint8_t { open, control, select_league, toggle_group, close, pairings, table, team_info };
struct StrengthsRequest {
    StrengthsIdentity identity;
    StrengthsAction kind = StrengthsAction::open;
    StrengthsControl control = StrengthsControl::condition;
    int value = -1;
    bool operator==(const StrengthsRequest&) const = default;
};
struct StrengthsState {
    StrengthsIdentity identity;
    StrengthsProjection projection;
    int league_group = 0;
    std::string title, subtitle; // Original bytes
    bool decorated = true, original_panel = true, gradient = true, secondary_leagues = true;
    bool operator==(const StrengthsState&) const = default;
};

class StrengthsScreen {
public:
    StrengthsScreen(const std::filesystem::path&, StrengthsState);
    Image render() const;
    bool pointer_down(int x, int y, bool right = false);
    bool release(int x, int y);
    bool move(int x, int y);
    void reject_action(std::string reason);
    const StrengthsState& state() const { return state_; }
    const std::optional<StrengthsRequest>& pending() const { return pending_; }
    std::string state_json() const;
private:
    void text(Image&, int x, int baseline, std::string_view bytes, uint8_t color) const;
    void selector(Image&, int x, int y, int style) const;
    void button(Image&, int x, int y, int width, std::string_view label, bool selected) const;
    int button_at(int x, int y) const;
    std::optional<StrengthsRequest> selector_at(int x, int y) const;
    void post(StrengthsAction, int value = -1, StrengthsControl = StrengthsControl::condition);
    LeImage exe_;
    TextDb texts_;
    Image frame_, panel_, arrows_, widgets_;
    Font font_;
    SpriteFont sprite_;
    StrengthsState state_;
    std::optional<StrengthsRequest> pending_;
    int pressed_ = -1;
    std::string rejection_;
};
} // namespace bmh::ui
