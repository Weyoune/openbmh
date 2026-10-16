// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <filesystem>
#include <string>

#include "formats/le.hpp"
#include "formats/vga.hpp"

namespace bmh::ui {

// Values are the original setup representation: duration 5 = unlimited;
// difficulty 0..9 is displayed as 1..10 balls. No engine state is mutated here.
// Engine handoff, LE/save mapping and required side effects: ENGINE_BINDING.md.
struct SetupOptions {
    int duration = 5;
    int difficulty = 2;
    int managers = 1;
    int mode = 3;
    bool league = true;
    bool world = true;
    bool operator==(const SetupOptions&) const = default;
};

enum class SetupRequest { none, new_game, load_game };
struct SetupResult {
    bool changed = false;
    SetupRequest request = SetupRequest::none;
};

// SetupScreen2 / SetupScreen, with layout tables read from the user's BMH.exe.
// Render and hit testing share the same tables. Coordinates are native 320x240.
class SetupScreen {
public:
    explicit SetupScreen(const std::filesystem::path& data);
    // Original joining-manager setup: continue from the active setup values.
    SetupScreen(const std::filesystem::path& data, SetupOptions current);
    const SetupOptions& options() const { return options_; }
    SetupResult click(int x, int y, bool right_button = false);
    Image render() const;

private:
    SetupOptions options_;
    Image background_, atlas_;
    std::array<int, 8> button_x_{}, button_y_{};
    std::array<int, 8> joining_arrow_permission_{};
    std::array<int, 4> minimum_{}, maximum_{};
    std::array<int, 3> toggle_x_{}, toggle_source_x_{};
    std::array<int, 6> toggle_source_y_{};
    std::array<int, 4> display_width_{}, display_height_{};
    int button_source_y_ = 0;
    bool joining_ = false, difficulty_arrows_locked_ = false;
};

// Match DOS filename case on Linux without modifying or renaming original files.
std::filesystem::path asset_path(const std::filesystem::path& directory, const std::string& name);
std::string setup_json(const SetupOptions& options);

}  // namespace bmh::ui
