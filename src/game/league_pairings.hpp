// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "game/state.hpp"
#include "game/random.hpp"

namespace bmh {

struct MatchGroup;

// Normal LeagueScreenShown(2,0)->Move(0) only. The App owns the staged Native/W/G
// transaction. No mutator here calls a presenter, advances a day, or saves.
struct LeaguePairingsConstants {
    std::array<uint8_t,9> points_mode{}, calendar_class{};
    std::array<uint8_t,10> computer_tip_gap{};
    uint8_t domestic_league_count=0;
};
struct LeaguePairingsTipResult {
    int league=-1,source_round=-1,fixture_index=-1;
    int home_runtime_team=-1,home_club_identity=-1,away_runtime_team=-1,away_club_identity=-1;
    uint8_t original_result=0xcc,predicted_result=0xcc;
    bool operator==(const LeaguePairingsTipResult&) const=default;
};
struct LeaguePairingsOwner {
    int manager=-1, runtime_club=-1, club_identity=-1, league=-1, round=-1, title_round=-1;
    uint8_t league_group=0;
    bool supported=false, closed=false;
    // Move local28 and its current active descriptor. Process-only;
    // neither field is serialized or changes the league fixture/result arrays.
    bool active_phase=false;
    int active_group=-1;
    // Move locals60/50/30. -1 means ordinary row TeamInfo, -2 is the
    // freshly armed search sentinel. Saved preference2 alone does not arm it.
    int search_first=-1,search_second=-1;
    int search_first_identity=-1,search_second_identity=-1;
    bool search_second_click=false;
    // Move mode4 has a separate single-club filter; its selection is not
    // the ordered two-team search state above. -2 is freshly armed, -1 inactive.
    int filter_team=-1,filter_identity=-1,filter_offset=0;
    bool filter_active=false;
    // Move mode8 owns a separate postponed-result list and ten-row page.
    int postponed_offset=0;
    bool postponed_active=false;
    // KI-Tipp is a one-redraw runtime forecast, keyed to the actual fixture
    // identities. Its ten-byte original slice includes unplayed/padding bytes;
    // the forecast never writes that slice back to serialized Native results.
    bool computer_tip_active=false;
    int computer_tip_league=-1,computer_tip_round=-1;
    std::array<uint8_t,10> computer_tip_original{};
    std::vector<LeaguePairingsTipResult> computer_tip_results;
    bool operator==(const LeaguePairingsOwner&) const=default;
};
struct LeaguePairingsTeam {
    int runtime_team=-1, club_identity=-1;
    int previous_rank=-1; // TablePosition, already 1-based; absent before round1.
    std::array<int,5> strength{}; // 1f1,1f2,1f4,1f8,1ff, Void6.
};
struct LeaguePairingsActiveRow {
    int source_round=-1,fixture_index=-1;
    int home_runtime_team=-1,home_club_identity=-1,away_runtime_team=-1,away_club_identity=-1;
    uint8_t referee_index=0,referee_profile=0,weather=0;
    int8_t temperature=0;
    std::string referee_name; // Original bytes from MasterDatabaseMetadata -> ba370.
    bool operator==(const LeaguePairingsActiveRow&) const=default;
};
struct LeaguePairingsActivePacket {
    int league=-1,round=-1,group_index=-1;
    std::vector<LeaguePairingsActiveRow> rows;
    bool operator==(const LeaguePairingsActivePacket&) const=default;
};
struct LeaguePairingsRow {
    LeaguePairingsTeam home,away;
    uint8_t raw_result=0xcc;
    std::optional<uint8_t> computer_tip_result;
    std::optional<uint32_t> postponed_date,scheduled_date;
    std::optional<uint16_t> postponed_day;
    std::optional<uint8_t> postponed_queue_slot;
    std::optional<uint8_t> first_leg_result;
    // Native/source coordinates survive a multi-round filtered projection.
    int source_round=-1,fixture_index=-1;
};
struct LeaguePairingsView {
    LeaguePairingsOwner owner;
    int played_rounds=0, total_rounds=0;
    int total_rows=0,page_offset=0;
    uint8_t preferences=0;
    uint32_t match_date=0xffff;
    std::vector<LeaguePairingsRow> rows;
};
enum class LeaguePairingsAction { reset,previous,next,select_league,toggle_group,team_info,close,unsupported,search,search_team,filter_by_club,filter_club,postponed_list,computer_tip,presentation_redraw };
struct LeaguePairingsEvent {
    LeaguePairingsAction kind=LeaguePairingsAction::unsupported;
    int value=-1;
    int expected_club_identity=-1; // stable team identity for TeamInfo/search/filter.
};

LeaguePairingsOwner open_league_pairings_source(GameState&,int manager,const LeaguePairingsConstants&);
LeaguePairingsOwner edit_league_pairings_source(GameState&,const LeaguePairingsOwner&,
    const LeaguePairingsEvent&,const LeaguePairingsConstants&,GameRandom&,WatcomRandom&);
// Pure, bounded Source projection; renderers never call a Native/RNG mutator.
LeaguePairingsView league_pairings_view_source(const GameState&,const LeaguePairingsOwner&,
    const LeaguePairingsConstants&);
// Read only active process descriptors, never reconstruct weather/referees from
// saved bytes or run event preparation. Last first-pair match follows Move.
std::optional<LeaguePairingsActivePacket> league_pairings_active_packet_source(
    const GameState&,const LeaguePairingsOwner&,const std::vector<MatchGroup>&,
    std::span<const uint8_t> referee_profiles);
void validate_league_pairings_active_packet_source(const GameState&,const LeaguePairingsOwner&,
    const LeaguePairingsActivePacket&);
bool league_pairings_played_source(uint8_t raw);
bool league_pairings_postponed_source(uint8_t raw);

} // namespace bmh
