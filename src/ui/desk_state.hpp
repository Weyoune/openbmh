// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ui/setup.hpp"

namespace bmh { struct GameState; class SaveState; }
namespace bmh { struct MatchGroup; }

namespace bmh::ui {

struct DeskItem {
    int id = 0, x = 0, y = 0, width = 0, height = 0, kind = 0;
    uint8_t flags = 0, folder = 0;
    bool open = false, disabled = false;
};

struct DeskDate { int year = 0, month = 0, day = 0, weekday = 0; };
struct DeskManagerIdentity {
    int runtime_club = -1, club_identity_key = -1, league = -1, portrait = 0;
    uint16_t badge = 0;
    std::string name;
    bool operator==(const DeskManagerIdentity&) const = default;
};
struct DeskMatchCallargs {
    int competition = 33, opponent = -1;
    bool operator==(const DeskMatchCallargs&) const = default;
};
struct DeskRankSample {
    int matchday = 0;
    int source_rank = -1; // TablePosition's zero-based 5-bit value.
};
struct DeskRankCurve {
    int runtime_team = -1;
    int league = -1;
    std::string team_name;
    std::optional<int> current_position, strength;
    std::vector<DeskRankSample> samples;
};
struct DeskRecentMatch {
    int home_team = -1, away_team = -1;
    std::string home_name, away_name;
    uint8_t source_score = 0xff; // Original packed byte, without inferred home/away reversal.
};
struct DeskOpponentHistory {
    int runtime_team = -1;
    std::string team_name;
    std::array<uint8_t, 10> source_results{}; // 5 home + 5 away nibbled result bytes.
};
// Copyable, read-only rendering boundary. Runtime club IDs are not master IDs.
struct DeskState {
    SetupOptions options;
    int manager_slot = 0, runtime_club = -1, league = -1, portrait = 0;
    // Authoritative slot -> runtime team mapping, copied from GameState::manager_teams.
    std::vector<int> manager_teams;
    std::vector<DeskManagerIdentity> managers;
    // Original active-club roster retained across the temporary national takeover.
    // In a multiplayer career the visible national desk has only the coach in slot 0.
    std::vector<DeskManagerIdentity> national_return_roster;
    bool national_team = false;
    // SwapManagerSlots2 temporary coach assignment (CoachActive). This can make
    // a league-only, single-manager career temporarily project team 0x165.
    bool national_coach_takeover = false;
    std::string manager_name, club_name;
    uint16_t badge = 0;
    uint32_t day_counter = 0;
    int season_index = 0, season_day = 0;
    std::optional<int> league_round, table_position, strength;
    // Desk call arguments are not fully saved in FBM. -1 = confirmed no opponent;
    // nullopt = unknown. The initial reference title is known only at career day zero.
    std::optional<int> next_competition, next_opponent;
    // SourceDrawerTeam swaps the two match labels when the active manager's
    // record +0x28a marks the club as the away side.
    bool next_match_away = false;
    // SourceDrawerTeam's independent pair-count gate: DeskState_FormatError_RuntimeField2==0 and record+28a!=0.
    // Retained even without a next opponent; that separate gate is resolved
    // by the renderer. Match-label home/away semantics remain independent.
    bool paired_drawers_away_order = false;
    // Source SourceDrawerTeam case 15: resolved only from the current prepared event.
    std::string next_competition_label, next_competition_detail, next_opponent_name;
    std::optional<uint16_t> next_opponent_badge;
    bool next_opponent_badge_fallback = false;
    // Read-only contents for SourceDrawerTeam switch-local cases 0..4, 9..10,
    // 0x0c, 0x10..0x11, and 0x14..0x15. Cases 0x0d and 0x16 are source-empty. Portrait/name are
    // already supplied by `portrait` / `manager_name` for cases 0x12/0x13.
    // Folder entries are native layout record IDs (1..30); their displayed
    // dimensions are derived from EXE count tables, while saved layout records
    // remain untouched. Rank samples and result bytes retain native encodings.
    std::array<std::array<uint8_t, 16>, 54> layout_records{};
    std::array<std::vector<uint8_t>, 3> folder_icon_slots;
    bool layout_snap = true; // CareerSession_DispatchDesktopLayout_RuntimeField2 == 0: Source snaps desktop drags to its grid.
    std::array<DeskRankCurve, 2> rank_curves; // Own club and authenticated current rival.
    std::vector<DeskRecentMatch> recent_matches;
    std::vector<DeskOpponentHistory> opponent_histories;
    DeskDate date;
    int32_t balance = 0, savings = 0;
    int ticket_price = 0, fans = 0;
    int32_t money_numerator = 1, money_denominator = 1;
    bool decorated_background = true, gradient_text = true, drawer_handles = true;
    bool shortcut_bar_always = true; // DeskDate: DeskState_FormatError_RuntimeField3 == 0 sets afb86 bit 1.
    std::optional<DeskItem> mailbox_shortcut; // original item 44 / kind 63, including a hidden/foldered record
    bool mailbox_notification_pending = false; // MailboxRecipientMask active-manager bit; transient, non-FBM
    std::optional<DeskItem> menu_layout_shortcut; // final bar cell returns native record 53/kind 72, then dispatches MEN
    std::array<uint8_t, 5> automation{};
    std::array<std::optional<bool>, 10> warnings{};
    std::vector<DeskItem> items; // Original linked-list drawing / hit-test order.
    std::string source;
    uint64_t revision = 0;
};

// Original DeskDate2, including its four-year leap cycle and Wednesday epoch.
DeskDate desk_date(uint32_t day_counter, const LeImage& executable);
DeskState desk_from_save(const SaveState&, const LeImage&, uint64_t revision = 0);
// Uses authoritative typed fields where StaticMemory contains stale copies.
// Does not call enter_desk(), RNG, finance transactions or any engine mutator.
DeskState desk_from_game(const GameState&, const LeImage&, uint64_t revision,
                         const std::vector<MatchGroup>* current_groups = nullptr,
                         std::optional<int> source_default_competition = std::nullopt,
                         const DeskMatchCallargs* source_callargs = nullptr);
DeskState load_reference_desk(const std::filesystem::path& save, const LeImage& executable);
std::string desk_json(const DeskState&);
void validate_desk_profile(const SetupOptions&);
void validate_desk_state(const DeskState&);

} // namespace bmh::ui
