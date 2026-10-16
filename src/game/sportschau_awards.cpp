// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/sportschau_presentation.hpp"
#include <algorithm>
#include "game/matchday.hpp"
#include "game/messages.hpp"
#include "game/season.hpp"
#include "game/weekly.hpp"

namespace bmh {

std::optional<MatchdayIntroView> matchday_intro_view(const GameState& state,
    const std::vector<MatchGroup>& groups) {
    if (groups.empty() || !state.memory.has(::bmh::layout::SportschauAwards_MatchdayIntroView_RuntimeField) || state.memory.u8(::bmh::layout::SportschauAwards_MatchdayIntroView_RuntimeField) != 0)
        return std::nullopt;
    MatchdayIntroView view; int previous_type = 100;
    for (const auto& group : groups) {
        const int type = group.desc.at(0);
        // Original emits every league, but only the first adjacent cup type.
        if (type != 0 && type == previous_type) continue;
        previous_type = type;
        std::string name;
        if (type == 0) {
            const uint32_t league = le32(group.desc, 0xcc);
            if (league >= 9) throw FormatError("matchday intro: invalid Source league");
            // LeagueName with TeamStatistics_SourceLeagueName_RuntimeField=1 includes the secondary-league prefix.
            name = league < 5 ? league_name(state, int(league)) :
                message_text(state, ::bmh::layout::SportschauAwards_MatchdayIntroView_TextPointer) + state.club_name(0x165 + int(league) - 4);
        } else {
            if (type > 14) throw FormatError("matchday intro: invalid Source competition type");
            name = message_text(state, ::bmh::layout::DeskState_FormatError_ExecutableField3 + uint32_t(type) * 4);
            // StrcpySoHallReplacesItsCurrentTournament is strcpy, so Hall replaces its current tournament name.
            if (type == 12) name = message_text(state, ::bmh::layout::MatchdayResults_CompetitionName_TextPointer);
        }
        view.competition_names.push_back(std::move(name));
    }
    return view;
}

std::string sportschau_competition_title(const GameState& state, const MatchGroup& group) {
    const uint32_t id = le32(group.desc, 0xcc);
    int index = int(id);
    if (id > 20) index = state.memory.u8(id + 0x18) + 9;
    else if (index >= 9) index = std::min(index + 12, 19);
    if (index < 5) return league_name(state, index);
    if (index < 9) return state.club_name(0x165 + index - 4);
    return message_text(state, ::bmh::layout::CupDraw_FormatError_ExecutableField6 + uint32_t(index - 9) * 4);
}

std::optional<SportschauTeamInfoView> sportschau_team_info_view(const GameState& state,
    const MatchGroup& group, int group_index) {
    const int setting = sportschau_source_setting_index(state, group);
    if (!state.memory.has(::bmh::layout::SportschauAwards_SportschauTeamInfoView_RuntimeField) || state.memory.u8(::bmh::layout::SportschauAwards_SportschauTeamInfoView_RuntimeField) != 0 ||
        setting < 0 || setting >= 20 || !state.memory.has(::bmh::layout::Controller_FormatError_ExecutableField2 + uint32_t(setting) * 9) ||
        state.memory.u8(::bmh::layout::Controller_FormatError_ExecutableField2 + uint32_t(setting) * 9) != 0) return std::nullopt;
    if (group_index < 0 || group_index > 255) throw FormatError("sportschau: invalid team info group");
    SportschauTeamInfoView view;
    view.group = uint8_t(group_index); view.competition_id = le32(group.desc, 0xcc);
    view.competition_title = sportschau_competition_title(state, group);
    // Eligible C4 already guarantees SportschauIntro's any-group predicate. B7
    // controls loading KB57 even when C7 suppresses the intro animation.
    view.source_backdrop_available = state.memory.has(::bmh::layout::Controller_FormatError_RuntimeField) && state.memory.u8(::bmh::layout::Controller_FormatError_RuntimeField) == 0;
    for (size_t i = 0; i < 4; ++i) view.totals[i] = int32_t(le32(group.desc, 0xbc + i * 4));
    return view;
}

std::optional<SportschauAwardsView> finish_sportschau_awards(GameState& state,
    const MatchGroup& group, int group_index) {
    for (size_t i = 0; i < 11; ++i) if (group.desc.at(0x70 + i) == 0) return std::nullopt;
    // The award-statistic update is movzbl [descriptor+70], not the displayed slot word.
    // Preserve the Original statistic even if its presentation is disabled.
    for (size_t i = 0; i < 11; ++i) {
        const int manager = state.manager_of(slot_team(group.desc.at(0x70 + i)));
        if (manager >= 0) {
            auto& counter = state.manager_records.at(size_t(manager) * 0xb0c + 0x21e);
            counter = uint8_t(counter + 1);
        }
    }
    const int setting = sportschau_source_setting_index(state, group);
    if (!state.memory.has(::bmh::layout::SportschauAwards_FinishSportschauAwards_RuntimeField) || state.memory.u8(::bmh::layout::SportschauAwards_FinishSportschauAwards_RuntimeField) != 0 ||
        setting < 0 || setting >= 20 || !state.memory.has(::bmh::layout::Controller_FormatError_ExecutableField2 + uint32_t(setting) * 9) ||
        state.memory.u8(::bmh::layout::Controller_FormatError_ExecutableField2 + uint32_t(setting) * 9) != 0) return std::nullopt;
    if (group_index < 0 || group_index > 255) throw FormatError("sportschau: invalid awards group");
    SportschauAwardsView view;
    view.group = uint8_t(group_index); view.competition_id = le32(group.desc, 0xcc);
    view.competition_title = sportschau_competition_title(state, group);
    view.source_backdrop_available = state.memory.has(::bmh::layout::Controller_FormatError_RuntimeField) && state.memory.u8(::bmh::layout::Controller_FormatError_RuntimeField) == 0;
    for (size_t i = 0; i < 11; ++i) {
        auto& p = view.players[i];
        p.score = group.desc.at(0x70 + i); p.squad_slot = le16(group.desc, 0x9c + i * 2);
        p.player_id = state.squad_slots.at(p.squad_slot) & 0xfff;
        p.team_id = int16_t(slot_team(int16_t(p.squad_slot)));
        p.player_name = state.player_name(p.player_id); p.club_name = state.club_name(p.team_id);
    }
    return view;
}

} // namespace bmh
