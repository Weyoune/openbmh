// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <filesystem>
#include <optional>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "formats/vga.hpp"
#include "game/state.hpp"
#include "ui/sprite_font.hpp"

namespace bmh::ui {

enum class TrainingCampFeedback { none, booked, insufficient_funds, booked_this_week, visit_limit };

struct TrainingCampState {
    int manager_slot = -1;
    int runtime_club = -1;
    uint64_t revision = 0;
    std::string source;
    std::string club_name;
    std::string manager_name;
    uint16_t badge = 0;
    uint8_t portrait = 0;
    std::array<uint8_t, 4> focus{};
    bool focus_normalization_required = false;
    uint8_t stays = 0;
    int32_t balance = 0;
    std::array<int32_t, 8> prices{};
    std::array<std::array<uint8_t, 4>, 8> quality{};
    int32_t currency_multiplier = 1;
    int32_t currency_divisor = 1;
    std::string currency_suffix; // empty direct fixtures inherit the loaded executable
    TrainingCampFeedback feedback = TrainingCampFeedback::none;
    bool gradient_text = true;

    bool booked_this_week() const { return (stays & 0x80) != 0; }
    int visit_count() const { return stays & 0x7f; }
    bool booking_allowed() const { return !booked_this_week() && visit_count() < 3; }
};

enum class TrainingCampRequestKind { none, set_focus, book };
struct TrainingCampRequest {
    TrainingCampRequestKind kind = TrainingCampRequestKind::none;
    int manager_slot = -1;
    int runtime_club = -1;
    uint64_t revision = 0;
    int resort = -1;
    std::array<uint8_t, 4> focus{};
};

TrainingCampState training_camp_from_game(const GameState&, const LeImage&, int manager, uint64_t revision);
std::string training_camp_json(const TrainingCampState&);
std::string training_camp_price_text(const TrainingCampState&, int resort);
std::string training_camp_request_json(const TrainingCampRequest&);

class TrainingCampScreen {
public:
    TrainingCampScreen(const std::filesystem::path& data, TrainingCampState state);
    Image render() const;
    TrainingCampRequest click(int x, int y, bool right = false);
    TrainingCampRequest key(std::string_view key);
    bool move(int x, int y);
    const TrainingCampState& state() const { return state_; }
    int selected_resort() const { return selected_resort_; }
    int chosen_resort() const { return chosen_resort_; }
    const std::optional<TrainingCampRequest>& pending() const { return pending_; }
    bool close_requested() const { return close_requested_; }
    bool timed_notice_open() const {
        return state_.feedback!=TrainingCampFeedback::none && state_.feedback!=TrainingCampFeedback::booked;
    }
    bool expire_timed_notice();
    TrainingCampRequest initialize_focus();
    void acknowledge(TrainingCampState);
    void reject_action(std::string reason);
    std::string state_json() const;

private:
    TrainingCampState state_;
    int selected_resort_ = -1;
    int preview_resort_ = -1; // Text(-1) preserves the last photo while clearing details.
    int chosen_resort_ = -1;
    bool close_requested_ = false;
    std::optional<TrainingCampRequest> pending_;
    std::string rejection_;
    LeImage exe_;
    TextDb texts_;
    Image background_, map_, resorts_one_, resorts_two_, widgets_, buttons_, letters_, portraits_, badge_;
    Font small_, large_;
    SpriteFont sprite_font_;

    void text(Image&, int x, int baseline, std::string_view, uint8_t color, int right = -1) const;
    void sprite_text(Image&, int x, int baseline, std::string_view, int font) const;
    void resort_details(Image&, int resort) const;
    void focus_controls(Image&) const;
    TrainingCampRequest request(TrainingCampRequestKind, int resort = -1,
                                const std::array<uint8_t, 4>& focus = {});
};

}  // namespace bmh::ui
