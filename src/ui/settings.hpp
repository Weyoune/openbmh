// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <optional>
#include <vector>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "ui/sprite_font.hpp"
#include "ui/save_load.hpp"

namespace bmh { struct GameState; }

namespace bmh::ui {

// Preserve the raw bytes: bit 6/7 availability and value 2 are not Boolean settings.
struct SettingsSoundCapabilities { bool music = false, effects = false; };
struct SettingsState {
    std::array<uint8_t, 180> cells{}; // Unmodified saved bytes.
    std::optional<std::array<uint8_t, 180>> display_cells; // Local entry-time availability; unknown hosts preserve raw A6/A7 flags.
    int manager_slot = -1;
    uint64_t revision = 0;
    bool decorated_background = true;
    std::string title, footer, version;
    std::array<std::string, 3> button_labels;
    bool audio_config_known = false;
};

// Inputs to the Original's entry-time availability pass ProjectSettingsAvailability. The two
// sound-config latches are runtime inputs (SettingsRuntimeField4/3a), not FBM fields;
// callers must supply values from the current config session.
struct SettingsAvailabilityInputs {
    uint8_t mode;               // GameMode
    uint32_t competitions;      // CompetitionSelection: bit 0 league, bit 1 world
    uint8_t music_configuration_locked;    // nonzero locks cell A6
    uint8_t effects_configuration_locked;    // nonzero locks cell A7
};

enum class SettingsEdit : uint8_t { toggle_binary, decrement, increment };
struct SettingsWriteRequest {
    int manager_slot = -1;
    uint64_t revision = 0;
    std::array<uint8_t, 180> expected_raw{};
    uint8_t row = 0;
    uint8_t column = 0;
    SettingsEdit operation = SettingsEdit::toggle_binary;
    friend bool operator==(const SettingsWriteRequest&, const SettingsWriteRequest&) = default;
};

// Reproduce only ProjectSettingsAvailability's raw-cell availability update into a local
// copy. It performs no save/GameState writes, I/O, drawing, or RNG calls.
std::array<uint8_t, 180> project_settings_availability(
    const std::array<uint8_t, 180>& raw_cells, const SettingsAvailabilityInputs&,
    const LeImage& original_exe);

SettingsState settings_from_game(const GameState&, int manager, uint64_t revision,
                                 const LeImage&, std::optional<SettingsSoundCapabilities> sound = std::nullopt);

// The two SettingsSourceRefusal rule refusals call SDLFlushEvents3; generic/stale errors do not.
std::optional<uint32_t> settings_source_refusal(const GameState& state,
    const SettingsWriteRequest& request, uint64_t revision);

class SettingsScreen {
public:
    SettingsScreen(const std::filesystem::path& data, SettingsState state, int origin_item_id = -1);
    Image render() const;
    bool click(int x, int y, bool right = false);
    bool pointer_down(int x, int y, bool right = false);
    bool release_consumed() const { return release_barrier_; }
    void consume_release() { release_barrier_ = false; }
    bool key(std::string_view value);
    bool close_requested() const { return closed_; }
    const SettingsState& state() const { return state_; }
    const SettingsWriteRequest* pending() const { return pending_ ? &*pending_ : nullptr; }
    const SaveLoadRequest* pending_file() const { return pending_file_ ? &*pending_file_ : nullptr; }
    void consume_file_request() { pending_file_.reset(); }
    bool readme_requested() const { return readme_requested_; }
    void consume_readme_request() { readme_requested_ = false; }
    void acknowledge(SettingsState state);
    void acknowledge_file(SettingsState state);
    void reject_file(std::string reason);
    void cancel_file_request() { pending_file_.reset(); }
    void reject(std::string reason);
    void reject_close(std::string reason);
    std::string state_json() const;
private:
    SettingsState state_;
    int origin_item_id_ = -1;
    Image background_, atlas_, button_atlas_;
    Font font_;
    LeImage exe_;
    SpriteFont sprite_font_;
    bool closed_ = false;
    bool release_barrier_ = false;
    bool readme_requested_ = false;
    std::string feedback_;
    std::optional<SettingsWriteRequest> pending_;
    std::optional<SaveLoadRequest> pending_file_;
    void text(Image&, int x, int baseline, std::string_view original,
              uint8_t color, int right = 320) const;
};

// SettingsReadmeScreen's in-settings LIESMICH.TXT viewer. It reads at most the
// Original 20,000-byte buffer, interprets #0/#1 emphasis controls and returns
// only on the Original left/right mouse wait (or application quit).
class SettingsReadmeScreen {
public:
    SettingsReadmeScreen(const std::filesystem::path& data, bool decorated_background);
    Image render() const;
    bool pointer_down(bool left_or_right);
    bool close_requested() const { return close_requested_; }
    bool file_available() const { return file_available_; }
    size_t displayed_bytes() const { return content_.size(); }
    size_t page_index() const { return page_index_; }
private:
    struct Glyph { int x = 0, baseline = 0; uint8_t character = 0; bool emphasized = false; };
    Image background_;
    Font font_;
    LeImage exe_;
    TextDb texts_;
    std::string content_;
    std::vector<std::vector<Glyph>> pages_;
    size_t page_index_ = 0;
    bool decorated_background_ = true;
    bool file_available_ = false;
    bool close_requested_ = false;
};

} // namespace bmh::ui
