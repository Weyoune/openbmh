// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "game/state.hpp"

namespace bmh::ui {

enum class StrengthsAgeBasis : uint8_t {
    entry_or_league_change_bit10,
    header_toggle_bit15,
    cached_for_same_league
};

struct StrengthsAgeScratch {
    int league = -1;
    StrengthsAgeBasis basis = StrengthsAgeBasis::entry_or_league_change_bit10;
    bool lineup_only = false;
    std::vector<uint8_t> tenths;
    bool operator==(const StrengthsAgeScratch&) const = default;
};

struct StrengthsProjectionContext {
    std::optional<StrengthsAgeScratch> cached_age;
    StrengthsAgeBasis recompute_age_as = StrengthsAgeBasis::entry_or_league_change_bit10;
    // Set only by an owner-validated same-child ClubInfoFromGame return. Source
    // retains local_68 across that modal even if injury cleanup changes lineups.
    bool retain_validated_owner_age = false;
    bool operator==(const StrengthsProjectionContext&) const = default;
};

struct StrengthsDrawWords {
    uint16_t raw_settings_word = 0;
    uint16_t row_settings_word = 0;
    uint16_t effective_average_flags = 0;
    uint16_t post_draw_settings_word = 0;
    bool operator==(const StrengthsDrawWords&) const = default;
};

enum class StrengthsControl : uint8_t {
    condition = 0,
    technique = 1,
    form = 2,
    morale = 3,
    goalkeeper = 4,
    defender = 5,
    midfielder = 6,
    forward = 7,
    reset = 8,
    age = 9,
    lineup = 12
};

struct StrengthsRow {
    int runtime_team = -1;
    std::array<uint16_t, 4> criterion_values{}; // KO, TE, FO, MO
    uint16_t aggregate_value = 0;
    uint8_t age_tenths = 0x22;
    uint32_t source_sort_key = 0;
    size_t source_row = 0;
    std::string team_name; // UTF-8 for display; Source name bytes are read from the save
    uint8_t name_color = 0x5f;
    bool operator==(const StrengthsRow&) const = default;
};

struct StrengthsProjection {
    int manager_slot = -1;
    int league = -1;
    int first_team = -1;
    StrengthsDrawWords words;
    StrengthsAgeScratch age_scratch;
    std::string league_name; // UTF-8
    std::vector<StrengthsRow> rows; // Source Games order
    bool operator==(const StrengthsProjection&) const = default;
};

// Source redraw first calculates rows from the raw settings and only then normalizes
// the manager word. The caller persists post_draw_settings_word through its owner.
StrengthsDrawWords strengths_draw_words_source(uint16_t raw);

// A null result means the control is unavailable (currently filters while age sort is
// enabled); controls 10/11 are navigation and intentionally not represented here.
std::optional<uint16_t> strengths_control_word_source(uint16_t normalized_word,
                                                       StrengthsControl control);

// Pure projection for one manager and one domestic league. Throws FormatError for
// incomplete or inconsistent save state. Does not mutate the game or use either RNG.
StrengthsProjection strengths_from_game(const GameState& game, int manager_slot, int league,
                                        const StrengthsProjectionContext& context = {});

// Source family-setting initialization used by LeagueScreenShown when opening this screen.
// This returns the word only; its owner decides whether/when to persist it.
uint16_t strengths_family_word_source(uint16_t raw_family_word, uint8_t points_mode);

} // namespace bmh::ui
