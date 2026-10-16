// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "game/table.hpp"
#include "ui/league_family_request.hpp"
#include "ui/sprite_font.hpp"

namespace bmh { struct GameState; }

namespace bmh::ui {

struct LeagueTableRow {
    int rank = 0;                 // one-based engine rank
    int runtime_team_id = -1;     // current savegame team index
    int club_identity_key = -1;   // stable name-pool offset from GameState::club_ids
    std::string name;             // owned UTF-8; convert only at the font boundary
    uint8_t name_color = 0x5f;    // LeagueTableOperation manager/world highlight result
    int games = 0;
    int wins = 0;
    int draws = 0;
    int losses = 0;
    int goals_for = 0;
    int goals_against = 0;
    int points = 0;
    int previous_rank = 0;
    uint32_t form_bits = 0;       // packed Uint8T result chain
    bool operator==(const LeagueTableRow&) const = default;
};

// Explicit, immutable input for the Source-driven table controls projection.
// first/end use ComputeLeagueTable's half-open matchday interval.
struct LeagueTableProjectionContext {
    int first_round = 0;
    int end_round = 0;
    int aggregation_flags = 4;
    int normal_mode = 0;
    std::optional<uint16_t> display_word_override;
    std::string retained_range_label_source_bytes;
    int accepted_engine_league = -1;
    std::optional<bmh::LeagueTable> accepted_engine_table;
    bool forecast = false;
};

struct LeagueTableState {
    int manager_slot = -1;
    int league_id = -1;
    std::string league_name;      // owned UTF-8 from the original TEXTD pointer table
    std::string header_text;      // owned UTF-8 from FriendlyMatchScreen's original heading pointer
    bool original_table_panel = true; // SolidPanelBackground == 0 loads the KB/56 backdrop
    bool decorated_background = true; // PlainBackground == 0 uses the KB/129 backdrop
    bool secondary_leagues = true;
    int rounds_played = 0;
    uint8_t raw_round_byte = 0;
    int points_mode = 1;          // OpenBMH deviates from the original: uses three points for a win in the supported rule profile.
// supported rule profile engine: three points per win
    uint16_t display_word = 0;    // per-manager LeagueTableDisplayModes source display state
    bool source_controls_projection = false;
    bool source_show_previous_rank = false; // LeagueTable_OriginalToUtf8_RuntimeField plus Source round gate
    int source_aggregation_flags = 4;
    int source_normal_mode = 0;
    std::string retained_range_label_source_bytes;
    bool forecast = false;
    std::vector<LeagueTableRow> rows;
    bool operator==(const LeagueTableState&) const = default;
};

LeagueTableState league_table_from_game(const bmh::GameState&, int manager_slot, int league_id);
LeagueTableState league_table_from_game(const bmh::GameState&, int manager_slot, int league_id,
                                       const LeagueTableProjectionContext&);
std::array<LeagueTableState, 9> league_tables_from_game(const bmh::GameState&, int manager_slot);
std::string league_table_json(const LeagueTableState&);

// A read-only presentation model. League selection changes only this screen's selected index.
// It owns all nine projections and holds no GameState pointer or mutation callback.
class LeagueTableScreen {
public:
    struct FamilyMode { StrengthsIdentity identity; uint8_t source_selection_mode = 0; };
    struct OpponentMode {}; // FriendlyMatchScreen(2,0), local selection without family writes.
    LeagueTableScreen(const std::filesystem::path& data,
                      std::array<LeagueTableState, 9> tables, int initial_league);
    LeagueTableScreen(const std::filesystem::path& data,
                      std::array<LeagueTableState, 9> tables, int initial_league,
                      FamilyMode family);
    LeagueTableScreen(const std::filesystem::path& data,
                      std::array<LeagueTableState, 9> tables, int initial_league,
                      OpponentMode);
    const LeagueTableState& state() const { return tables_[size_t(selected_league_)]; }
    int selected_league() const { return selected_league_; }
    bool close_requested() const { return close_requested_; }
    const std::optional<LeagueFamilyRequest>& pending_family_action() const { return pending_family_action_; }
    bool source_mouse_wait_release_pending() const { return pressed_hit_ >= 0; }
    void reject_family_action(std::string reason);
    // Consume an accepted same-league selector while retaining the visible Table body.
    void acknowledge_same_league_selector(const LeagueFamilyRequest& old_request,
                                         const StrengthsIdentity& accepted_identity);
    void acknowledge_friendly_own_club(const StrengthsIdentity& accepted_identity);
    Image render() const;
    std::string state_json() const;
    bool click(int x, int y, bool right = false);
    bool pointer_down(int x, int y, bool right = false);
    bool move(int x, int y);
    bool key(std::string_view key);

private:
    std::array<LeagueTableState, 9> tables_;
    Font font_;
    Image desktop_background_;
    Image background_;
    Image widget_atlas_;
    Image selector_atlas_;
    LeImage exe_;
    TextDb texts_;
    SpriteFont sprite_font_;
    int selected_league_ = 0;
    int league_group_ = 0;
    bool close_requested_ = false;
    std::optional<FamilyMode> family_;
    bool opponent_mode_ = false;
    int opponent_default_league_ = -1;
    std::optional<LeagueFamilyRequest> pending_family_action_;
    int pressed_hit_ = -1;
    std::string rejection_;
    void text(Image&, int x, int y, std::string_view, uint8_t color, int max_width = 300) const;
    void button(Image&, int index, int x, int y, int width, std::string_view label, bool selected) const;
    void selector(Image&, int x, int y, int group) const;
    std::optional<int> league_at(int x, int y) const;
    int hit_at(int x, int y, bool right = false) const;
    bool activate_hit(int hit);
    void post(LeagueFamilyRequest);
};

} // namespace bmh::ui
