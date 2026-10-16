// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <optional>

#include "ui/desk_state.hpp"
#include "formats/textd.hpp"

namespace bmh::ui {

// Manager record +0xd2..0xd9. Runtime club is never a master database ID.
struct TacticsState {
    int manager_slot = 0, runtime_club = -1;
    uint64_t revision = 0;
    std::string source, manager_name, club_name;
    bool decorated_background = true;
    // Original menu gate: CareerSession_DispatchLiveTactic_RuntimeField3 == 0 || CareerSession_DispatchLiveTactic_RuntimeField2[index] != 0.
    bool live_scene_option = false;
    uint8_t assistant_attitude = 0;
    std::array<uint8_t, 8> values{};
    // Board rows 1..10, signed coordinates at +0x41/+0x42.
    std::array<int8_t, 11> depth{}, lane{};
};
TacticsState tactics_from_save(const SaveState&, const LeImage&, uint64_t revision = 0);
TacticsState tactics_from_game(const GameState&, const LeImage&, uint64_t revision);

enum class TacticsRequestKind { none, set_tactic };
struct TacticsRequest {
    TacticsRequestKind kind = TacticsRequestKind::none;
    int manager_slot = -1, runtime_club = -1, index = -1, desired_value = -1;
    uint64_t revision = 0;
    // Setting the offensive/defensive arrows disables assistant attitude control.
    std::optional<uint8_t> desired_assistant_attitude;
};
std::string tactics_request_json(const TacticsRequest&);

class TacticsScreen {
public:
    TacticsScreen(const std::filesystem::path& data, TacticsState state);
    Image render() const;
    TacticsRequest click(int x, int y, bool right = false);
    TacticsRequest key(std::string_view key);
    const TacticsState& state() const { return state_; }
    const std::optional<TacticsRequest>& pending() const { return pending_; }
    bool close_requested() const { return close_requested_; }
    void acknowledge(TacticsState updated);
    void reject_action(std::string reason);
    std::string state_json() const;
private:
    TacticsState state_;
    LeImage exe_;
    TextDb texts_;
    Image background_, board_, sprites_, letters_;
    std::optional<TacticsRequest> pending_;
    std::string last_rejection_;
    bool close_requested_ = false;
    bool enabled(int index) const;
    void validate(const TacticsState&) const;
    int text_width(std::string_view value) const;
    void text(Image&, int x, int baseline, std::string_view value) const;
    void draw_setting(Image&, int index) const;
};

} // namespace bmh::ui
