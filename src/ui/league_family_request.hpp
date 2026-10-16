// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <optional>

namespace bmh::ui {

// UI ownership token shared by the three Source family presenters. `scope` and
// `sequence` are port-side stale-request guards; source_param2 is the distinct
// Original LeagueScreenShown page/competition argument and must survive page switches.
struct StrengthsIdentity {
    int manager = -1, runtime_club = -1, club_identity = -1, league = -1;
    uint32_t day_counter = 0;
    uint64_t revision = 0, scope = 0, sequence = 0;
    uint16_t expected_settings_word = 0, expected_family_word = 0;
    uint8_t source_param2 = 0;
    uint8_t league_group = 0;
    int manager_default_league = -1;
    uint64_t family_id = 0; // App-owned family nonce; distinct from child scope/sequence.
    uint64_t family_sequence = 0; // Monotone across all child changes; prevents stale loop replay.
    bool operator==(const StrengthsIdentity&) const = default;
};

enum class TableDialogStage : uint8_t { start, end };
enum class TableDialogOutcome : uint8_t { accept, cancel };
struct TableDialogReply {
    TableDialogStage stage = TableDialogStage::start;
    TableDialogOutcome outcome = TableDialogOutcome::cancel;
    int value = 0; // Only an accepted BalanceFromGame result carries a value.
    int x = 140, y = 80; // Source modal position, carried into the next call.
    bool operator==(const TableDialogReply&) const = default;
};

enum class LeagueFamilyRequestKind : uint8_t {
    navigate, close, team_info, control, select_league,
    table_range_result, table_forecast_presented, toggle_league_group, select_opponent
};
struct LeagueFamilyRequest {
    StrengthsIdentity identity;
    LeagueFamilyRequestKind kind = LeagueFamilyRequestKind::close;
    uint8_t from_child = 0; // Original family selectors: table=1, pairings=2, strengths=3.
    uint8_t target_child = 0; // 0 closes the complete family; otherwise table/pairings/strengths.
    int runtime_team = -1; // Set only for the nested TeamInfo request.
    int expected_club_identity = -1;
    // Physical Table row. Its visible identity is bound above; after a KI
    // Present Source resolves the target from restored normal scratch ranks.
    int table_row_index = -1;
    int control_index = -1; // Table control 0..9; pending replies use their distinct kind.
    int league_id = -1; // Semantic selector update; downstream family pages use this chosen league.
    std::optional<TableDialogReply> table_dialog_reply;
    // table_forecast_presented is emitted only after the bound forecast frame
    // reaches the actual renderer Present. It carries no dialog reply.
    bool operator==(const LeagueFamilyRequest&) const = default;
};

} // namespace bmh::ui
