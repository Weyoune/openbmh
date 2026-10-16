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
#include "game/environment.hpp"
#include "ui/game_end.hpp"
#include "ui/desktop.hpp"

namespace bmh::ui {

struct EnvironmentView {
    EnvironmentState values;
    int manager_slot = -1;
    int runtime_club = -1;
    uint64_t revision = 0;
    std::string club_name;
    uint16_t badge_sprite = 0;
    std::array<std::string, 6> cost_text{};
    bool decorated_background = true;
    bool gradient_text = true;
    bool solid_panel_background = false;
    bool plain_background = false;
    DeskRequest owner{};
};

EnvironmentView environment_from_game(const bmh::GameState&, uint64_t revision);
TaxDialogProjection environment_notice_dialog(const bmh::GameState&, int manager_slot,
                                               std::string exact_source_body);

enum class EnvironmentRequestKind : uint8_t { none, set_item };
struct EnvironmentRequest {
    EnvironmentRequestKind kind = EnvironmentRequestKind::none;
    int manager_slot = -1, runtime_club = -1, item = -1, level = -1;
    uint64_t revision = 0, request_id = 0;
    DeskRequest owner{};
    bool operator==(const EnvironmentRequest&) const = default;
};

std::string environment_view_json(const EnvironmentView&);
std::string environment_request_json(const EnvironmentRequest&);

class EnvironmentScreen {
public:
    EnvironmentScreen(const std::filesystem::path& data, EnvironmentView state, Image underlay);
    Image render() const;
    bool pointer_down(int x, int y, bool right = false);
    EnvironmentRequest release(int x, int y, bool right = false);
    EnvironmentRequest click(int x, int y, bool right = false);
    EnvironmentRequest key(std::string_view key);
    const EnvironmentView& state() const { return state_; }
    const std::optional<EnvironmentRequest>& pending() const { return pending_; }
    bool close_requested() const { return close_requested_; }
    void acknowledge(EnvironmentView);
    void reject_action(std::string);
    void set_feedback(std::string text) { feedback_ = std::move(text); }
    std::string state_json() const;
private:
    EnvironmentView state_;
    LeImage exe_;
    TextDb texts_;
    Image background_, tiles_, panel_, letters_, badge_atlas_;
    Font font_;
    bool close_requested_ = false;
    bool pressed_right_ = false;
    bool press_seen_ = false;
    int pressed_item_ = -1, pressed_level_ = -1;
    uint64_t next_request_id_ = 1;
    std::optional<EnvironmentRequest> pending_;
    std::string feedback_;
    void text(Image&, int, int, std::string_view, uint8_t, bool gradient = true) const;
};

} // namespace bmh::ui
