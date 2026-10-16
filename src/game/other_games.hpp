// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <vector>

#include "formats/textd.hpp"
#include "game/state.hpp"
#include "game/table.hpp"

namespace bmh {

enum class OtherGamesRowKind : uint8_t { match, scheduled_competition };
// Source local20==0 (`compact`) draws the complete round in dense six-pixel rows;
// local20==1 (`detail`) draws a 16-participant window in the taller 16-pixel layout.
enum class OtherGamesDisplayMode : uint8_t { compact, detail };

struct OtherGamesClubMetadata {
    bool has_squad_values = false;
    std::array<int, 5> averages{};
    // -1 for squad-backed clubs, otherwise the raw ForeignClubStrength byte for foreign-club rendering.
    int foreign_strength = -1;
};

// One Source row in the ordinary CupGroupOverviewEffects desk view. Names and text are kept in
// Original encoding so the presentation layer can pass them through the existing FNT3 path.
struct OtherGamesRow {
    OtherGamesRowKind kind = OtherGamesRowKind::match;
    int home_team = -1;
    int away_team = -1;
    std::string home_name;
    std::string away_name;
    uint16_t source_result = 0xcccc;  // primary +0x0c result, always retained by Source
    uint16_t source_second_leg = 0xcccc;  // reversed +0x10 result, shown only in Source's local4c case
    bool second_leg_array_present = false;
    bool second_leg_visible = false;
    bool played = false;
    int home_goals = 0;
    int away_goals = 0;
    int second_leg_home_goals = 0;
    int second_leg_away_goals = 0;
    // CupTie result encoding: 0 home, 1 away, 2 tied; -1 when primary is 0xcccc.
    int source_winner = -1;
    // Competition id 12 stores signed day offsets, not scorewords.
    uint32_t scheduled_source_day = 0;
    // Void6 metadata is kept per side; foreign-club labels are formatted by the UI with the
    // Original EXE strings, while these values remain source-faithful raw data.
    OtherGamesClubMetadata home_metadata;
    OtherGamesClubMetadata away_metadata;
    // The zero-based match in the descriptor's current round. This is presentation metadata,
    // not a writable result-array index supplied by the UI.
    int source_match = -1;
};

struct OtherGamesSelector {
    int slot = -1;
    int competition = -1;
    std::string title;
    bool active = false;
};

struct OtherGamesLinkedPage {
    int ordinal = -1;
    int entry_index = -1;
    int competition = -1;
    int group_index = 0;
    int group_count = 0;
};

struct OtherGamesProjection {
    int competition = -1;
    int descriptor_competition = -1;
    int linked_page = 0;
    int entry_index = -1;
    uint32_t entry_address = 0;
    uint8_t flags = 0;
    bool knockout = false;
    bool two_legs = false;
    int current_round = 0;
    int round = 0;
    int rounds = 0;
    int group_index = 0;
    int group_count = 0;
    std::string title;
    std::string round_heading;
    std::vector<OtherGamesSelector> selectors;
    // InDescriptorTable's flattened linked-stage/group selector. The ordinal is the value retained by
    // Source CareerSession_FormatError_RuntimeField8 and consumed by FindCup on the next draw.
    std::vector<OtherGamesLinkedPage> linked_pages;
    // Source's toggle gate uses participants in the displayed round (>16), before rows are
    // paired into matches. Detail mode shows a 16-participant window; compact mode shows all.
    int source_participant_count = 0;
    int participant_offset = 0;
    bool display_mode_toggle_available = false;
    OtherGamesDisplayMode display_mode = OtherGamesDisplayMode::detail;
    bool round_arrows_visible = false;
    bool fixtures_available = true;
    bool unfinished_predecessor = false;
    bool scheduled_day_mode = false;
    std::vector<LeagueTable> group_tables;
    std::vector<OtherGamesRow> rows;
};

// Const projection of the ordinary menu-5 call to CupGroupOverviewEffects. This API intentionally models
// only the visible values: Original entry separately updates serialized CareerSession_FormatError_RuntimeField3/selector-12 state,
// while complete/compact and page navigation persist the active manager's CareerSession_DispatchOtherGames_RuntimeField byte. It is
// therefore not evidence that the entire Original menu interaction is Native-neutral.
// `linked_page` follows the descriptor's Source +0x17 parallel-group link, then +0x16 next-stage
// link. `round == -1` selects CupRound's live round; otherwise it is range-checked.
// `participant_offset` is Source's low-seven-bit display cursor (multiples of 16). The
// displayed-round group row list remains complete; the mode/offset applies to knockout rows.
OtherGamesProjection other_games_from_game(const GameState& game, const TextDb& texts,
                                            int competition = 0, int linked_page = 0,
                                            int round = -1,
                                            OtherGamesDisplayMode display_mode = OtherGamesDisplayMode::detail,
                                            int participant_offset = 0,
                                            bool source_new_selection = true);

}  // namespace bmh
