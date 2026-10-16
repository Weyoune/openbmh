// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "formats/vga.hpp"
#include "game/season.hpp"
#include "ui/sprite_font.hpp"

namespace bmh::ui {

struct CupDrawClub {
    uint16_t runtime_id = 0;
    uint16_t badge_selector = 0;
    uint8_t league_tier = 0;
    uint8_t country = 0; // TeamRegion region/affiliation; 0 for the first five leagues
    uint8_t name_color = 0x5f;
    uint8_t reveal_name_color = 0;
    std::string original_name;
    std::string source_context;
};

struct CupDrawGroupRow {
    uint16_t runtime_id = 0;
    int rank = 0;
    int games = 0;
    int wins = 0;
    int draws = 0;
    int defeats = 0;
    int previous_rank = 0;
    int goals_for = 0;
    int goals_against = 0;
    int points = 0;
    uint32_t form = 0;
    uint8_t rank_color = 0xf7;
    uint8_t name_color = 0x5f;
};

// Owned snapshot of the corresponding Source hook boundary. `draw` keeps the
// shuffled entry order; `overview` keeps Source's post-sort order. The renderer
// never advances cup state or consumes either game RNG.
struct CupDrawProjection {
    SeasonCupDrawPhase phase = SeasonCupDrawPhase::draw;
    uint32_t entry_address = 0;
    int competition = 0;
    int round = 0;
    int offset = 0;
    uint8_t flags = 0;
    bool group_stage = false;
    int group_index = 0;
    int group_count = 0;
    size_t count = 0;
    std::string heading;  // Original-encoded TEXTD round name
    std::string footer;   // Original-encoded TEXTD competition label
    std::string group_caption; // owned TEXTD 574/577 variants; never edits the shared TEXTD database
    std::string group_round_suffix;
    std::array<std::string, 8> group_form_glyphs{};
    bool decorated_background = true;
    bool original_table_panel = true;
    bool gradient_text = true;
    bool previous_rank_visible = false;
    bool source_eligible = false; // CupDrawFromGame world-mode and per-competition guards
    int source_option_delay_ms = 0; // AdvanceStep(8), derived from the saved speed option
    std::vector<CupDrawClub> clubs;
    std::vector<CupDrawGroupRow> group_rows;
    std::vector<std::array<uint16_t, 2>> group_fixtures;
};

CupDrawProjection cup_draw_from_game(const GameState& game,
                                     const SeasonCupDrawEvent& event,
                                     const LeImage& exe,
                                     const TextDb& texts);

class CupDrawScreen {
public:
    CupDrawScreen(const std::filesystem::path& data_directory,
                  CupDrawProjection projection);

    // Draw-phase stepping consumes the Source bytecode scripts from BMH.exe.
    // Each call advances one timeline step. Initial setup performs ten
    // AdvanceStep(8)/poll steps even if skip was requested. Script wait tokens
    // expose AdvanceStep(8), input-poll, presentation, and fixed DecodeTaxAnimationScript2 delay
    // separately; copy tokens request no presentation or delay.
    bool advance_step();
    bool present_needed() const;
    bool queue_flush_needed() const;
    int option_delay_milliseconds() const;
    int wait_milliseconds() const;
    bool input_poll_needed() const;
    bool held_mouse_check_needed() const;
    bool reveal_complete() const;
    bool reveal_aborted() const;
    size_t revealed_count() const;
    // Queue codes 2 (left down) and 8 (right down) are consumed only at the
    // initial/per-club OriginalMouseSound polls. The initial ten polls remember a skip
    // but still all run. During a KB74 script, pass held-buttons separately:
    // SourceInput checks the held mouse bitmask after each option delay.
    bool source_input(uint8_t source_code);
    bool source_held_mouse(uint8_t left_right_mask);

    // `render()` returns the current draw/reveal framebuffer or the complete
    // CupGroupOverviewEffects(..., 300) overview, depending on projection.phase.
    Image render() const;
    // Interaction reports the runtime club id under a Source team-name hit box;
    // callers route it to the full Source club-information flow.
    std::optional<uint16_t> hit_test_team(int x, int y) const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace bmh::ui
