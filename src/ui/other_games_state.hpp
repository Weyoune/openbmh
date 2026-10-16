// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "formats/le.hpp"
#include "formats/vga.hpp"
#include "game/other_games.hpp"

namespace bmh::ui {

using OtherGamesDisplayMode = bmh::OtherGamesDisplayMode;

enum class OtherGamesAction : uint8_t {
    select_competition, select_linked_page, previous_round, next_round,
    toggle_display_mode, team_info, close
};

struct OtherGamesIdentity {
    int manager_slot = -1;
    int runtime_team = -1;
    int club_identity = -1;
    uint32_t day_counter = 0;
    uint64_t revision = 0;
    uint64_t scope = 0;
    uint64_t sequence = 0;
    uint8_t expected_view_byte = 0;
    bool operator==(const OtherGamesIdentity&) const = default;
};

// The value is a selector SLOT, linked-page ordinal, or visible team ID. Other
// actions require -1. No fixture data or Source pointers are accepted from UI.
struct OtherGamesRequest {
    OtherGamesIdentity identity;
    OtherGamesAction action = OtherGamesAction::close;
    int value = -1;
    bool operator==(const OtherGamesRequest&) const = default;
};

struct OtherGamesDisplayRow {
    std::array<uint8_t, 2> name_colors{};
    std::array<uint8_t, 2> league_tiers{};
    std::array<uint8_t, 2> countries{};
    std::array<std::string, 2> details;
    std::string primary_result;
    std::string second_leg_result;
};

struct OtherGamesTableRow {
    int runtime_team = -1;
    int rank = -1;
    int previous_rank = -1;
    std::string name;
    uint8_t name_color = 0;
    uint8_t rank_color = 0;
    uint32_t form = 0;
    int games = 0, wins = 0, draws = 0, defeats = 0;
    int goals_for = 0, goals_against = 0, points = 0;
};

struct OtherGamesState {
    OtherGamesIdentity identity;
    OtherGamesProjection projection;
    // Source local14 survives compact-mode toggles; projection's row offset can
    // be zero there. It also differs from saved CareerSession_DispatchOtherGames_RuntimeField low7 after round arrows.
    int source_participant_offset = 0;
    std::array<std::string, 8> selector_labels;
    std::vector<OtherGamesDisplayRow> rows;
    std::vector<OtherGamesTableRow> table_rows;
    std::string date_heading;
    std::string matchday_heading;
    bool previous_rank_visible = false;
    Image underlay;
    bool decorated_background = true;
    bool original_panel = true;
    bool gradient_text = true;
    bool cached_backdrop = false;
};

// Formats immutable Source bytes while the GameState is available. Opening and
// navigation writes belong to CareerSession, never this value adapter/renderer.
OtherGamesState other_games_state_from_game(
    const GameState&, const OtherGamesIdentity&, const TextDb&, const LeImage&,
    int competition, int linked_page, int round, OtherGamesDisplayMode,
    int participant_offset, bool source_new_selection, const Image& underlay,
    const std::map<uint32_t, uint8_t>& runtime = {});

} // namespace bmh::ui
