// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <optional>
#include <vector>

#include "formats/font.hpp"
#include "formats/textd.hpp"
#include "ui/setup.hpp"

namespace bmh { struct GameState; class SaveState; }

namespace bmh::ui {

struct TrainingPlayer {
    int row = 0, player_id = -1, position = 0, used = 0;
    std::string name;
    uint8_t intensity = 0, focus = 0;
    uint32_t skills = 0;
};
// Owned, copyable projection; rows refer to the original squad/tactics board.
struct TrainingState {
    int manager_slot = 0, runtime_club = -1, mode = 3;
    std::string club_name, source;
    std::array<uint8_t, 4> types{}; // TOR, ABW, MIT, STU, manager +0xc5
    std::array<uint8_t, 9> areas{}; // Kondition, Technik, Eckball .. Spiel, +0xc9
    uint8_t automation = 0; // manager_options[slot*5+4], original CareerSessionRuntimeField3
    int squad_capacity = 0;
    bool decorated_background = true, gradient_text = true;
    uint64_t revision = 0;
    std::vector<TrainingPlayer> players;
    bool needs_initialization() const { return (automation & 0x80) == 0; }
};

TrainingState training_from_save(const SaveState&, const LeImage&, uint64_t revision = 0);
TrainingState training_from_game(const GameState&, const LeImage&, uint64_t revision);
std::string training_json(const TrainingState&);

enum class TrainingRequestKind { none, initialize, set_type, set_area, set_player_intensity, set_player_focus, set_automation };
struct TrainingRequest {
    TrainingRequestKind kind = TrainingRequestKind::none;
    int manager_slot = -1, runtime_club = -1, index = -1, value = -1, player_id = -1;
    uint64_t revision = 0;
    std::array<uint8_t, 4> desired_types{};
    std::array<uint8_t, 9> desired_areas{};
    uint8_t desired_automation = 0;
};
std::string training_request_json(const TrainingRequest&);

class TrainingScreen {
public:
    TrainingScreen(const std::filesystem::path& data, TrainingState state);
    Image render() const;
    TrainingRequest click(int x, int y, bool right = false);
    TrainingRequest key(std::string_view key);
    // Opening an uninitialized original screen invokes Initialize. This
    // explicit request must be serviced by an engine adapter, never by render().
    TrainingRequest initialize();
    const TrainingState& state() const { return state_; }
    const std::optional<TrainingRequest>& pending() const { return pending_; }
    bool close_requested() const { return close_requested_; }
    void clear_close_request() { close_requested_ = false; }
    void acknowledge(TrainingState updated);
    void reject_action(std::string reason);
    std::string state_json() const;
private:
    TrainingState state_;
    LeImage exe_;
    Image background_, widgets_, skill_atlas_, training_atlas_, letters_;
    Font small_;
    TextDb texts_;
    std::optional<TrainingRequest> pending_;
    bool close_requested_ = false;
    std::string last_rejection_;
    void text(Image&, int x, int baseline, std::string_view, uint8_t color, int right = -1, bool shadow = true) const;
    void sprite_text(Image&, int x, int baseline, std::string_view, int font) const;
    void button(Image&, int x, int y, int width, int height, std::string_view, bool active) const;
    TrainingRequest request(TrainingRequestKind, int index = -1, int value = -1, int player_id = -1);
};

} // namespace bmh::ui
