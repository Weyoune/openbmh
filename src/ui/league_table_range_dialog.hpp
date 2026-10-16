// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "formats/vga.hpp"

namespace bmh::ui {

// BalanceFromGame's result after its final source Present and any required wait.
enum class TableRangeDialogResult : uint8_t { accept, cancel };
enum class TableRangeTextMode : uint8_t { flat, source_gradient };

struct TableRangeDialogOutcome {
    TableRangeDialogResult result = TableRangeDialogResult::cancel;
    uint32_t value = 0; // meaningful only for accept
    int x = 140;
    int y = 80;
};

struct TableRangeDialogProfile {
    // Only the original Table calls are valid: CareerSession_CurrentLeagueTableDispatch_ExecutableField Startspieltag or
    // CareerSession_CurrentLeagueTableDispatch_ExecutableField2 Endspieltag (TEXTD addresses in the loaded German executable).
    uint32_t title_textd_address = ::bmh::layout::CareerSession_CurrentLeagueTableDispatch_ExecutableField;
    uint32_t minimum = 1;
    uint32_t maximum = 34;
    uint32_t default_value = 1; // BalanceFromGame param7, independent from preset recurrence
    // Supplied from the actual caller context (TextGradientDisabled == 0 && CareerSession_DispatchLeagueTableControl_RuntimeField2 == 3).
    // Missing is an error: the presenter must not guess a global setting.
    std::optional<TableRangeTextMode> text_mode;
    int x = 140;
    int y = 80;                 // carried from the first modal invocation
};

// LeagueTableRangeDialog's fixed BalanceFromGame call, independent from the Table profile.
struct TotoStakeDialogProfile {
    TableRangeTextMode text_mode = TableRangeTextMode::source_gradient;
    int x = 103, y = 55;
};

// Isolated, stateful UI presenter for the original table's numeric dialog.
// The caller supplies the already-rendered frame under the modal. This object
// owns that backdrop and all loaded source presentation resources; it has no
// GameState, RNG, save writer, or mutation callback.
class LeagueTableRangeDialog {
public:
    LeagueTableRangeDialog(const std::filesystem::path& data_directory,
                           Image backdrop,
                           TableRangeDialogProfile profile);
    LeagueTableRangeDialog(const std::filesystem::path& data_directory,
                           Image backdrop, TotoStakeDialogProfile profile);

    Image render() const;

    // Source mouse event 2 is handled on button-down. pointer_up only ends a
    // drag; it does not commit a second action. right=true is intentionally
    // ignored: BalanceFromGame proves raw event 8 cancels, but does not establish that
    // the host's right mouse button maps to that event.
    bool pointer_down(int x, int y, bool right = false);
    bool move(int x, int y);
    bool pointer_up(int x, int y, bool right = false);

    // Recognized source key names: digit as one-byte ASCII; BACKSPACE;
    // SPACE; ENTER/RETURN. There is no inferred ESC mapping.
    bool key(std::string_view key);
    // SDL text adapter: exactly one ASCII digit per call, matching one source
    // keyboard digit event (not a multi-digit paste).
    bool text(std::string_view text);

    // Explicit adapter for BalanceFromGame raw event code 8. Its host/device origin is
    // unknown here; the application decides whether any SDL input maps to it.
    bool cancel_from_source_event();

    // Advances only explicit DecodeTaxAnimationScript2 wait units (4 after pressed input,
    // 50 on visible Cancel control 14). Raw event 8 has no proven 50-unit wait.
    // One unit is exactly the routine's argument unit;
    // caller must not convert wall-clock milliseconds by assumption.
    bool tick_source_units(uint32_t units);

    // Call only after the rendered Image has actually been presented. Returns
    // the accepted/cancelled value only after Source flash/wait and exit Present.
    std::optional<TableRangeDialogOutcome> after_present();

    bool open() const { return !outcome_delivered_; }
    // The SDL host retains keyboard events while a Source wait/Present owns
    // input; it must not consume those events by probing mutation methods.
    bool source_input_ready() const { return input_ready(); }
    bool waiting_for_exit_present() const { return awaiting_exit_present_ && !outcome_delivered_; }
    const Image& source_backdrop() const { return backdrop_; }
    uint32_t value() const { return value_; }
    uint32_t minimum() const { return profile_.minimum; }
    uint32_t maximum() const { return profile_.maximum; }
    std::array<std::optional<uint32_t>, 8> presets() const { return presets_; }
    int x() const { return x_; }
    int y() const { return y_; }

private:
    LeagueTableRangeDialog(const std::filesystem::path&, Image,
                          TableRangeDialogProfile, bool toto);
    enum class Completion : uint8_t { none, accept, cancel };

    Image backdrop_;
    Image calculator_art_;
    Font body_;
    LeImage exe_;
    TextDb texts_;
    TableRangeDialogProfile profile_;
    std::array<uint16_t, 24> cell_x_{};
    std::array<uint8_t, 24> cell_y_{};
    std::array<std::optional<uint32_t>, 8> presets_{};
    uint32_t value_ = 0;
    uint32_t displayed_value_ = 0;
    unsigned digit_count_ = 0;
    unsigned displayed_format_ = 7;
    int x_ = 140;
    int y_ = 80;
    int pointer_x_ = -1;
    int pointer_y_ = -1;
    int drag_offset_x_ = 0;
    int drag_offset_y_ = 0;
    int pressed_control_ = -1;
    uint32_t source_wait_remaining_ = 0;
    bool flash_waiting_for_present_ = false;
    bool flash_presented_ = false;
    bool initial_presented_ = false;
    bool updated_modal_present_pending_ = true;
    bool dragging_ = false;
    bool awaiting_exit_present_ = false;
    bool outcome_delivered_ = false;
    Completion completion_ = Completion::none;

    std::string_view source_text(uint32_t address) const;
    void draw_source_text(Image&, int x, int baseline, std::string_view, uint8_t color,
                          bool allow_gradient = false) const;
    void copy_region(Image&, int sx, int sy, int dx, int dy, int width, int height,
                     uint8_t transparent) const;
    void fill_region(Image&, int x, int y, int width, int height, uint8_t color) const;
    void draw_control(Image&, int index, bool pressed) const;
    bool activate_cell(int index);
    bool apply_control_key(int index);
    void begin_flash(int control, Completion completion = Completion::none);
    void begin_cancel();
    void append_digit(unsigned digit);
    void clamp_to_source_max();
    bool input_ready() const;
    bool inside_cell(int index, int x, int y) const;
};

} // namespace bmh::ui
