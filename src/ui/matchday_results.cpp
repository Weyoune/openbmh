// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/matchday_results.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "formats/le.hpp"
#include "game/cup.hpp"
#include "game/match_engine.hpp"
#include "game/messages.hpp"
#include "game/schedule.hpp"
#include "game/turn_driver.hpp"
#include "ui/desk_state.hpp"
#include "ui/manager.hpp"

namespace bmh::ui {
namespace {
std::string date_label(uint32_t counter, const LeImage& exe) {
    const DeskDate d = desk_date(counter, exe);
    std::ostringstream out;
    out << std::setfill('0') << std::setw(2) << d.day << '.'
        << std::setw(2) << d.month << '.' << std::setw(4) << d.year;
    return out.str();
}

void competition_identity(MatchdayResultGroup& group, const MatchGroup& source,
                          const LeImage& exe, const GameState* game) {
    if (source.desc.size() != 0xd0)
        throw FormatError("results: incomplete competition descriptor");
    const int type = source.desc[0];
    const uint32_t id = le32(source.desc, 0xcc);
    group.cup = type != 0;
    if (!group.cup) {
        if (id >= kLeagueCount) throw FormatError("results: invalid league ID");
        group.league_id = int(id);
        return;
    }
    // SetupCupGroups stores the LE data address of the active cup stage here;
    // CareerSession normalizes saved guest pointers when rebuilding groups.
    if (id < kCupTable || id >= kCupTable + kCupEntries * kCupEntrySize ||
        (id - kCupTable) % kCupEntrySize != 0)
        throw FormatError("results: invalid cup entry address");
    if (game && (!game->memory.has(id + 0x18) || !game->memory.has(id + 0x19)))
        throw FormatError("results: missing cup metadata");
    const int cup = game ? int(game->memory.u16(id + 0x18)) : int(exe.u16(id + 0x18));
    if (cup != type - 1 || cup != int(exe.u16(id + 0x18)))
        throw FormatError("results: cup ID disagrees with descriptor");
    group.cup_id = cup;
    group.cup_entry_id = cup_entry_index(id);
}

std::string competition_name(const MatchdayResultGroup& group,
                             const TurnResultSnapshot& snapshot, const GameState& game) {
    if (!game.texts) throw FormatError("results: missing edition competition texts");
    std::string name;
    if (group.league_id) {
        const int league = *group.league_id;
        name = league < 5 ? league_name(game, league) : game.club_name(0x165 + league - 4);
    } else if (*group.cup_id == 0xb && !bmh::kEnglishCompetitions) {  // (English: the League Cup, named below)
        // FinishEvent/BuildSideRecords: descriptor type 0x0c reads the runtime
        // TEXTD slot TournamentName, then appends edition TEXTD suffix MatchdayResults_CompetitionName_TextPointer.
        // The snapshot owns the name from before finish_event advances cups.
        const auto end = std::find(snapshot.tournament_name.begin(), snapshot.tournament_name.end(), 0);
        name.assign(snapshot.tournament_name.begin(), end);
        if (name.empty()) throw FormatError("results: missing frozen tournament name");
        name += message_text(game, ::bmh::layout::MatchdayResults_CompetitionName_TextPointer);
    } else {
        name = message_text(game, ::bmh::layout::DeskState_FormatError_ExecutableField3 + uint32_t(*group.cup_id + 1) * 4);
    }
    if (name.empty()) throw FormatError("results: missing competition name");
    return original_to_utf8(name);
}

MatchdayResults project_results(const TurnResultSnapshot& snapshot,
                               const MatchdayTeamNameResolver& resolve_team_name,
                               const LeImage& exe, const GameState* game) {
    if (!snapshot.valid) throw FormatError("results: turn result snapshot is invalid");
    MatchdayResults out;
    out.valid = true;
    out.date = snapshot.date;
    out.date_label = date_label(snapshot.date, exe);
    out.calendar_entry = snapshot.calendar_entry;
    out.matchday = snapshot.matchday;
    out.groups.reserve(snapshot.groups.size());
    for (const auto& source : snapshot.groups) {
        const int count = source.matches();
        if (source.fixtures.size() != size_t(count) * 2)
            throw FormatError("results: invalid fixture pairs");
        MatchdayResultGroup group;
        competition_identity(group, source, exe, game);
        if (game) group.competition = competition_name(group, snapshot, *game);
        group.matches.reserve(size_t(count));
        for (int m = 0; m < count; ++m) {
            const size_t idx = size_t(m);
            const int home_team = source.fixtures[idx * 2];
            const int away_team = source.fixtures[idx * 2 + 1];
            MatchdayResultMatch match;
            match.home = resolve_team_name(home_team);
            match.away = resolve_team_name(away_team);
            if (match.home.empty()) match.home = "Team " + std::to_string(home_team);
            if (match.away.empty()) match.away = "Team " + std::to_string(away_team);
            // A match that did not take place is in the kickoff mask (local_70). The descriptor
            // field +0xb8 (void_mask) is not usable here: the minute loop also sets a bit for
            // every match that has ended (driver.cpp finish_group_phase -> set_void), so after
            // the day it holds every played match (issue #19, see matchday.cpp process_group).
            const bool voided = (source.kickoff_void_mask >> m) & 1u;
            if (voided) {
                match.status = MatchResultStatus::not_played;
            } else if (group.cup && source.cup_results.size() == size_t(count)) {
                const uint16_t raw = source.cup_results[idx];
                if (raw == 0xcccc) {
                    match.status = MatchResultStatus::not_played;
                } else {
                    match.home_goals = int((raw >> 5) & 0x1f);
                    match.away_goals = int(raw & 0x1f);
                    match.decided = int((raw >> 10) & 3);
                    match.status = MatchResultStatus::played;
                }
            } else if (!group.cup && source.results.size() == size_t(count)) {
                const uint8_t raw = source.results[idx];
                if ((raw & 0xcc) == 0xcc) {
                    match.status = MatchResultStatus::postponed;
                } else {
                    match.home_goals = int(raw >> 4);
                    match.away_goals = int(raw & 0x0f);
                    match.status = MatchResultStatus::played;
                }
            } else {
                match.status = MatchResultStatus::unknown;
            }
            group.matches.push_back(std::move(match));
        }
        out.groups.push_back(std::move(group));
    }
    return out;
}
}

MatchdayResults matchday_results_from_snapshot(const TurnResultSnapshot& snapshot,
                                                const MatchdayTeamNameResolver& resolve_team_name,
                                                const LeImage& exe) {
    return project_results(snapshot, resolve_team_name, exe, nullptr);
}

MatchdayResults matchday_results_from_snapshot(const TurnResultSnapshot& snapshot,
                                                const MatchdayTeamNameResolver& resolve_team_name,
                                                const LeImage& exe, const GameState& game) {
    return project_results(snapshot, resolve_team_name, exe, &game);
}
} // namespace bmh::ui
