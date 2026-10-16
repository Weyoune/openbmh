// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/sportschau_presentation.hpp"

#include <algorithm>
#include <bit>
#include "formats/le.hpp"

namespace bmh {

SportschauRatingTables sportschau_rating_tables(const LeImage& exe) {
    SportschauRatingTables t;
    t.domestic_teams = exe.u16(::bmh::layout::CareerSession_DispatchLineup_RuntimeField);
    for (size_t i = 0; i < 4; ++i) {
        const auto offset = uint32_t(i);
        t.position_limit[i] = std::bit_cast<int8_t>(exe.u8(::bmh::layout::SportschauRating_SportschauRatingTables_RuntimeField + offset));
        t.opponent_misses[i] = std::bit_cast<int8_t>(exe.u8(::bmh::layout::SportschauRating_SportschauRatingTables_RuntimeField2 + offset));
        t.chance_actor[i] = std::bit_cast<int8_t>(exe.u8(::bmh::layout::SportschauRating_SportschauRatingTables_RuntimeField3 + offset));
        t.own_chances[i] = std::bit_cast<int8_t>(exe.u8(::bmh::layout::SportschauRating_SportschauRatingTables_RuntimeField4 + offset));
        t.scorer_bonus[i] = std::bit_cast<int8_t>(exe.u8(::bmh::layout::SportschauRating_SportschauRatingTables_RuntimeField5 + offset));
        t.award_begin[i] = exe.u8(::bmh::layout::SportschauRating_SportschauRatingTables_RuntimeField6 + offset);
        t.award_end[i] = exe.u8(::bmh::layout::SportschauRating_SportschauRatingTables_RuntimeField7 + offset);
        if (t.award_begin[i] >= t.award_end[i] || t.award_end[i] > 11 ||
            (i && t.position_limit[i] <= t.position_limit[i - 1]))
            throw FormatError("sportschau: invalid executable award tables");
    }
    return t;
}

void rate_sportschau_players(const GameState& state, MatchGroup& group,
    const SportschauFixtureView& fixture, WatcomRandom& rand, const SportschauRatingTables& t) {
    // F7, D7, E7 and selected status only gate presentation. G7/C4 gate the
    // rating even with the report hidden or skipped (5cd8a, 5c389).
    if (!state.memory.has(::bmh::layout::SportschauAwards_FinishSportschauAwards_RuntimeField) || state.memory.u8(::bmh::layout::SportschauAwards_FinishSportschauAwards_RuntimeField) != 0 ||
        fixture.source_setting_index < 0 || fixture.source_setting_index > 19 ||
        !state.memory.has(::bmh::layout::Controller_FormatError_ExecutableField2 + uint32_t(fixture.source_setting_index) * 9) ||
        state.memory.u8(::bmh::layout::Controller_FormatError_ExecutableField2 + uint32_t(fixture.source_setting_index) * 9) != 0) return;

    struct Actors {
        uint8_t goals = 0, misses = 0;
        std::vector<uint16_t> scorers, assistants, missed;
    };
    std::array<Actors, 2> actors;
    auto add = [](std::vector<uint16_t>& ids, uint16_t id) {
        if (ids.size() < 12) ids.push_back(id); // independent twelve-entry buffers
    };
    for (size_t i = 0; i + 1 < group.log.size(); i += 2) {
        const int team = group.log[i + 1] >> 7;
        if (team != fixture.home_team && team != fixture.away_team) continue;
        auto& a = actors[size_t(team == fixture.away_team)];
        const int code = group.log[i] >> 12;
        const uint16_t player = group.log[i] & 0xfff;
        if (code > 1) continue;
        if (code == 0) { ++a.goals; add(a.scorers, player); }
        else { ++a.misses; add(a.missed, player); }
        const bool following = i + 2 < group.log.size();
        const int following_code = following ? group.log[i + 2] >> 12 : 0;
        const int kind = following && following_code > 6 ? following_code - 6 : 0;
        i += 2; // both chance codes consume the next record, regardless of its team
        if (i < group.log.size() && (following_code < 3 || following_code > 6)) {
            const uint16_t assistant = group.log[i] & 0xfff;
            if (assistant != 0xfff && (kind == 0 || kind == 3)) add(a.assistants, assistant);
        }
    }

    for (size_t side = 0; side < 2; ++side) {
        const int team = side ? fixture.away_team : fixture.home_team;
        if (team < 0 || team >= t.domestic_teams) continue;
        const auto squad = squad_range(team);
        const auto& a = actors[side]; const auto& opponent = actors[1 - side];
        for (size_t row = 0; row < squad.count; ++row) {
            const uint16_t id = state.squad_slots.at(squad.first + row) & 0xfff;
            if (id == 0xfff) continue;
            const uint32_t flags = state.players.at(id).w364;
            if ((flags & 0x1000) == 0 || ((flags >> 16) & 0x1f) != 0) continue;
            int16_t score = int16_t(rand.range(0, 7) + 5);
            int depth = 0, lane = 0;
            state.formation_position(team, int(row), depth, lane);
            size_t profile = 0;
            while (profile < 4 && depth >= t.position_limit[profile]) ++profile;
            if (profile == 4) throw FormatError("sportschau: formation depth outside award profiles");
            auto plus = [&](int value) { score = std::bit_cast<int16_t>(uint16_t(uint16_t(score) + value)); };
            plus((int(opponent.misses) - int(opponent.goals)) * t.opponent_misses[profile]);
            int goals = 0;
            if (t.chance_actor[profile]) {
                if (profile == 1) plus(rand.range(0, 4));
                goals = int(std::count(a.scorers.begin(), a.scorers.end(), id));
                const int assists = int(std::count(a.assistants.begin(), a.assistants.end(), id));
                const int misses = int(std::count(a.missed.begin(), a.missed.end(), id));
                plus((goals + assists - misses) * t.chance_actor[profile]);
            }
            // SportschauRating_SportschauRatingTables_RuntimeField4 is a predicate, not a multiplier (5cf77..5cf8e).
            if (t.own_chances[profile]) plus(int(a.goals) + int(a.misses));
            plus(goals * t.scorer_bonus[profile]);

            int minimum = 120; size_t selected = 0;
            for (size_t slot = t.award_begin[profile]; slot < t.award_end[profile]; ++slot) {
                const int prior = group.desc.at(0x70 + slot);
                if (prior < minimum) { minimum = prior; selected = slot; }
            }
            // Equal scores replace the first minimum; score storage wraps to
            // one byte. The u16 identity is a GLOBAL squad slot, not a player id.
            if (score >= minimum) {
                group.desc.at(0x70 + selected) = uint8_t(score);
                const uint16_t slot = uint16_t(squad.first + row);
                group.desc.at(0x9c + selected * 2) = uint8_t(slot);
                group.desc.at(0x9d + selected * 2) = uint8_t(slot >> 8);
            }
        }
    }
}

} // namespace bmh
