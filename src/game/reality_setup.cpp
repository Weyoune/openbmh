// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/reality_setup.hpp"

#include <algorithm>
#include <optional>

namespace bmh {
namespace {
int manager(const GameState& state, int team) {
    const int found = state.manager_of(team);
    if (found >= 0) return found;
    // FindManagerForClub's separate national-manager record is not in manager_teams.
    if (!state.manager_teams.empty() && state.manager_teams[0] == kNoTeam &&
        state.manager_records.size() >= 4*0xb0c+0x292 &&
        le16(state.manager_records,4*0xb0c+0x290) == team) return 4;
    return -1;
}

std::optional<std::pair<int,int>> marked_position(const GameState& state, const RealitySetup& setup, int figure) {
    const int side = figure > 10, other = 1-side;
    const int own_manager = manager(state,setup.teams[size_t(side)]);
    const int other_manager = manager(state,setup.teams[size_t(other)]);
    int target = 0xfff;
    if (own_manager >= 0) {
        target = le16(state.tactics,size_t(own_manager)*0x840+size_t(figure-side*11)*0x58+0x44);
        for (int row = other*11; row < other*11+11; ++row)
            if (setup.players[size_t(row)].player == target) { target = row;break; }
    } else if (other_manager >= 0) {
        for (int row = 0; row < 11; ++row)
            if (le16(state.tactics,size_t(other_manager)*0x840+size_t(row)*0x58+0x46) ==
                setup.players[size_t(figure)].player) { target = other*11+row;break; }
    }
    // RealityMarkedPosition also tests the unchanged low numeric target after a failed lookup.
    if (target < 24 && setup.motion.figures[size_t(target)].active) {
        if (target == 23) return std::pair{setup.motion.figures[23].next_x,setup.motion.figures[23].next_y};
        return setup.grid.position(target);
    }
    if (target >= 24 && target < 34)
        throw FormatError("Reality setup: legacy marker addresses an untracked Source figure");
    return std::nullopt;
}

void validate(const GameState& state, const std::array<int,2>& teams, int side, const RealityFormations& positions) {
    if (side < 0 || side > 1 || teams[0] == teams[1]) throw FormatError("Reality setup: invalid fixture");
    for (int s = 0; s < 2; ++s) {
        const int team = teams[size_t(s)];
        if (team < 0) throw FormatError("Reality setup: invalid team");
        const auto range = squad_range(team);
        if (range.first+range.count > state.squad_slots.size() || range.count < 11)
            throw FormatError("Reality setup: truncated squad");
        const int m = manager(state,team);
        if (m >= 0 && (state.manager_records.size() < size_t(m)*0xb0c+0xd9 ||
                       state.tactics.size() < size_t(m+1)*0x840))
            throw FormatError("Reality setup: truncated manager/board");
        for (const auto& p : positions[size_t(s)])
            if (p.first < 0 || p.first > 30 || p.second < 0 || p.second > 8)
                throw FormatError("Reality setup: prepared formation outside supported Source grid");
        for (size_t row = 0; row < 11; ++row) {
            const auto slot = state.squad_slots[range.first+row];
            if (!(slot & kSlotInLineup)) continue;
            const size_t player = slot & 0xfff;
            if (player == 0xfff || player >= state.players.size() || player >= state.player_cards.size())
                throw FormatError("Reality setup: invalid starting player");
            if (m >= 0) {
                const size_t board = size_t(m)*0x840+row*0x58;
                if (state.tactics[board+2] > 38 || state.tactics[board+3] > 99 || state.tactics[board+4] > 99)
                    throw FormatError("Reality setup: unsupported player movement limits");
            }
        }
    }
}
} // namespace

std::optional<std::pair<int,int>> reality_marked_position(const GameState& state, const RealitySetup& setup, int figure) {
    if (figure < 0 || figure >= 22) throw FormatError("Reality: invalid marked player");
    for (int side=0;side<2;++side) {
        const int m=manager(state,setup.teams[size_t(side)]);
        if (m>=0 && state.tactics.size()<size_t(m+1)*0x840)
            throw FormatError("Reality: truncated marking board");
    }
    return marked_position(state,setup,figure);
}

RealityFormations reality_formations(const GameState& state, const std::array<int,2>& teams) {
    RealityFormations result{};
    for (int side = 0; side < 2; ++side) for (int row = 0; row < 11; ++row) {
        const int m = manager(state,teams[size_t(side)]);
        if (m >= 0) {
            const size_t at = size_t(m)*0x840+size_t(row)*0x58;
            result[size_t(side)][size_t(row)] = {int(int8_t(state.tactics.at(at+0x41))),int(int8_t(state.tactics.at(at+0x42)))};
        } else {
            int x = 0,y = 0;state.formation_position(teams[size_t(side)],row,x,y);
            result[size_t(side)][size_t(row)] = {x,y};
        }
    }
    return result;
}

void initialize_reality(GameState& state, const std::array<int,2>& teams, int attacking_side,
    const RealityFormations& positions, const LeImage& exe, WatcomRandom& rand, GameRandom& game, RealitySetup& setup) {
    validate(state,teams,attacking_side,positions);
    setup.ready = false;setup.teams = teams;setup.attacking_side = attacking_side;
    for (int side = 0; side < 2; ++side) {
        const int m = manager(state,teams[size_t(side)]);
        for (int option = 0; option < 7; ++option)
            setup.tactics[size_t(side)][size_t(option)] = uint8_t(m < 0 ?
                rand.range(0,exe.u8(::bmh::layout::RealitySetup_InitializeReality_RuntimeFieldUnsigned+uint32_t(option))) :
                state.manager_records.at(size_t(m)*0xb0c+0xd2+size_t(option)));
    }
    setup.grid.reset_occupancy(); // SizeT2 clears only the two occupancy planes.
    for (int pass = 0; pass < 2; ++pass) {
        const int side = pass ^ attacking_side, team = teams[size_t(side)], m = manager(state,team);
        const auto range = squad_range(team);
        std::array<std::pair<int,int>,5> candidates{};size_t candidates_count = 0, formation_cursor = 0;
        for (int row = 0; row < 11; ++row) {
            const int figure = side*11+row;auto& info = setup.players[size_t(figure)];
            auto& motion = setup.motion.figures[size_t(figure)];
            motion.active = false;info.player = 0;
            const uint16_t slot = state.squad_slots.at(range.first+size_t(row));
            if (!(slot & kSlotInLineup)) continue;
            // Source advances the prepared-formation cursor only for active rows.
            const auto formation = positions[size_t(side)].at(formation_cursor++);
            int x = int(int8_t(formation.first*14/10)), y = int(uint8_t(12-(4-formation.second)*25/10));
            if (x) x = std::max(1,int(int8_t(x+(side == attacking_side ? setup.tactics[size_t(side)][0]*2+4 : setup.tactics[size_t(side)][0]-2))));
            x = std::min(x,27);
            if (side) { x = 42-x;y = 24-y; }
            info.center_x = x;info.center_y = y;
            const size_t board = m >= 0 ? size_t(m)*0x840+size_t(row)*0x58 : 0;
            if (m < 0) {
                info.min_x = x-4;info.max_x = x+4;info.min_y = y-2;info.max_y = y+2;
            } else {
                const int bias = state.tactics[board+2];
                const int left = side ? bias : 38-bias, right = side ? 38-bias : bias;
                info.min_x = x-(int(state.tactics[board+3])+19+left)*42/235;
                info.max_x = x+(int(state.tactics[board+3])+19+right)*42/235;
                const int vertical = (int(state.tactics[board+4])*2/3+5)*25/117;
                info.min_y = y-vertical;info.max_y = y+vertical;
            }
            if (row && (info.max_x < 2 || info.min_x > 40 || info.max_y < 1 || info.min_y > 23))
                throw FormatError("Reality setup: movement box cannot reach the Source pitch");
            for (int tries = 0; tries < 30; ++tries) {
                if (row) {
                    do { x = int(int8_t(rand.range(info.min_x,info.max_x))); } while (x < 2 || x > 40);
                    do { y = int(int8_t(rand.range(info.min_y,info.max_y))); } while (y < 1 || y > 23);
                }
                if (!setup.grid.occupant(x,y,side)) break;
            }
            if (m >= 0) {
                int increment = setup.tactics[size_t(attacking_side)][6] != 0;
                const int distance = int(int8_t(std::abs(x-info.center_x)+std::abs(y-info.center_y)));
                if (distance > 6 && rand.range(0,distance) > 6) ++increment;
                if (increment) state.tactics[board+0xf] = uint8_t(std::clamp(int(int8_t(state.tactics[board+0xf]))+increment,0,99));
            }
            if (side == attacking_side && x > 10 && x < 30 && candidates_count < candidates.size())
                candidates[candidates_count++] = {x,y};
            info.player = slot & 0xfff;info.squad_slot = int(range.first)+row;
            info.card_value = state.player_cards.at(size_t(info.player)) & 31;
            if (pass) if (const auto marker = marked_position(state,setup,figure)) { x = marker->first;y = marker->second; }
            while (!setup.grid.place(figure,x,y,side)) {
                const int next_x = int(int8_t(rand.range(x-1,x+1)));
                const int next_y = int(int8_t(rand.range(y-1,y+1)));
                x = std::clamp(next_x,0,42);
                y = std::max(next_y,0);
                // FormatError4's y>24 branch writes the packed x byte, leaving y
                // outside the playable grid. Do not silently normalize this
                // legacy spare-row/memory case into a different simulation.
                if (y > 24)
                    throw FormatError("Reality setup: legacy collision enters an unsupported spare row");
            }
            motion.active = true;motion.old_x = motion.next_x = x;motion.old_y = motion.next_y = y;
        }
        if (candidates_count) {
            const auto ball = candidates[size_t(rand.range(1,int(candidates_count))-1)];
            setup.grid.place(22,ball.first,ball.second,2);
            setup.motion.figures[22] = {true,ball.first,ball.second,ball.first,ball.second,0};
        } else if (side == attacking_side) return;
    }
    // RealityReporterPanel draws all 24 initial phases only after a successful FormatError4.
    for (int figure = 0; figure < 24; ++figure) {
        const int pose = figure < 22 ? (figure > 10 ? 7 : 0) : figure-14;
        setup.motion.figures[size_t(figure)].phase = uint8_t(game.range(0,exe.u8(::bmh::layout::RealityMotion_FormatError_RuntimeField7Unsigned+uint32_t(pose))-1));
    }
    setup.motion.figures[23].active = setup.motion.figures[22].active;
    setup.motion.ball_airborne = false;
    setup.motion.numbers = (state.memory.has(::bmh::layout::RealityMatch_FormatError_ExecutableField) ? state.memory.u8(::bmh::layout::RealityMatch_FormatError_ExecutableField) : exe.u8(::bmh::layout::RealityMatch_FormatError_ExecutableField)) == 0;
    setup.ready = true;
}

} // namespace bmh
