// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/lineup.hpp"
#include "formats/le.hpp"
#include "game/match.hpp"
#include "game/weekly.hpp"

#include <cstdlib>
#include <stdexcept>
#include <algorithm>
#include <utility>

namespace bmh {

namespace {
bool original_foreign_limit_enabled = false;

// Position type by formation depth x (table at layout field).
constexpr int8_t kTypeByDepth[24] = {0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 0, 1, 2, 2};
// Position type of bench slots by slot index (table at layout field); 9 = any.
constexpr int8_t kTypeBySlot[24] = {2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 0, 1, 2, 2, 3, 3, 9, 9, 9, 9, 9, 9, 9};
constexpr int16_t kUnavailable = -32000;  // 0x8300
constexpr int kMismatchWeight = 0x40;      // local_54 for computer teams

constexpr size_t kManagerRecordSize = 0xB0C;
constexpr size_t kManagerBoardSize = 0x840;
constexpr size_t kBoardRowSize = 0x58;
constexpr size_t kFormationManagerSize = 0x160;
constexpr size_t kNativeFormationBlockSize = 0x580;

uint8_t formation_byte(const GameState& g, size_t index) {
    // The native byte table runs directly into the tactics block at 0x580.
    // Int2 indexes through that contiguous memory for manager 3's
    // indoor formations and reserve rows.
    if (index < kNativeFormationBlockSize) return g.formations.at(index);
    return g.tactics.at(index - kNativeFormationBlockSize);
}

bool pitch_context_valid(const GameState& g, int manager, int team) {
    if (manager < 0 || manager > 3 || team < 0 || team >= runtime_dimensions.league_teams || g.lineups_frozen ||
        !g.memory.has(::bmh::layout::ActiveManager) || !g.memory.has(::bmh::layout::GameMode) ||
        g.memory.u8(::bmh::layout::ActiveManager) != manager || g.memory.u8(::bmh::layout::GameMode) < 1 || g.memory.u8(::bmh::layout::GameMode) > 3 ||
        size_t(manager) >= g.manager_teams.size() || g.manager_teams[size_t(manager)] != team ||
        g.manager_of(team) != manager || size_t(team) >= g.team_flags.size() ||
        size_t(team) >= g.club_ids.size() || size_t(team + 1) * 2 > g.club_heap_78.size() ||
        g.manager_records.size() < size_t(manager + 1) * kManagerRecordSize ||
        g.manager_options.size() < size_t(manager + 1) * 5 ||
        g.tactics.size() < size_t(manager + 1) * kManagerBoardSize ||
        g.formations.size() < size_t(manager + 1) * kFormationManagerSize ||
        g.player_cards.size() != g.players.size())
        return false;
    const size_t record = size_t(manager) * kManagerRecordSize;
    const uint16_t record_team = uint16_t(g.manager_records[record + 0x290] |
        uint16_t(g.manager_records[record + 0x291]) << 8);
    if (record_team != uint16_t(team)) return false;
    const SquadRange range = squad_range(team);
    if ((range.count != 16 && range.count != 24) || range.first + range.count > g.squad_slots.size())
        return false;
    bool empty_seen = false;
    std::array<bool, 0x1000> seen{};
    size_t occupied = 0;
    for (size_t row = 0; row < range.count; ++row) {
        const uint16_t slot = g.squad_slots[range.first + row];
        const size_t id = slot & 0x0fff;
        if (id == 0x0fff) { empty_seen = true; continue; }
        if (empty_seen || id >= g.players.size() || seen[id]) return false;
        seen[id] = true;
        ++occupied;
    }
    if (occupied < 11) return false;
    const size_t board = size_t(manager) * kManagerBoardSize;
    for (size_t row = 0; row < 11; ++row) {
        const uint16_t slot = g.squad_slots[range.first + row];
        const size_t id = slot & 0x0fff;
        const int depth = int8_t(g.tactics[board + row * kBoardRowSize + 0x41]);
        const int lane = int8_t(g.tactics[board + row * kBoardRowSize + 0x42]);
        if (id >= g.players.size() || (slot & 0xc000) != 0x8000 ||
            depth < 0 || depth > 19 || lane < 0 || lane > 8)
            return false;
        const bool fit = ((g.players[id].w364 >> 16) & 0x1f) == 0;
        if (fit) for (size_t prior = 0; prior < row; ++prior) {
            const size_t prior_id = g.squad_slots[range.first + prior] & 0x0fff;
            if (((g.players[prior_id].w364 >> 16) & 0x1f) != 0) continue;
            const size_t other = board + prior * kBoardRowSize;
            if (int8_t(g.tactics[other + 0x41]) == depth &&
                int8_t(g.tactics[other + 0x42]) == lane)
                return false;
        }
    }
    return true;
}

bool optimized_pitch_valid(const GameState& g, int manager, int team) {
    if (!pitch_context_valid(g, manager, team)) return false;
    const SquadRange range = squad_range(team);
    const size_t board = size_t(manager) * kManagerBoardSize;
    std::array<bool, 0x1000> seen{};
    for (size_t row = 0; row < 11; ++row) {
        const uint16_t slot = g.squad_slots[range.first + row];
        const size_t id = slot & 0x0fff;
        const int depth = int8_t(g.tactics[board + row * kBoardRowSize + 0x41]);
        const int lane = int8_t(g.tactics[board + row * kBoardRowSize + 0x42]);
        if (id >= g.players.size() || seen[id] || (slot & 0xc000) != 0x8000 ||
            depth < 0 || depth > 19 || lane < 0 || lane > 8)
            return false;
        seen[id] = true;
    }
    return true;
}

// Abs2-based detail score in Int2. Both preference fields come
// from the w35c value loaded from PlayerConditionTechniqueForm: +21 for depth and +24 for lane.
int16_t detailed_position_value(int16_t base, const Player& player, int x, int y, int detail) {
    if (detail == 0 || player.position() == 0) return base;
    const int role = player.position();
    const int preferred_y = int(player.w35c >> 24 & 7);
    const int preferred_x = int(player.w35c >> 21 & 7) + (role - 1) * 7 + (role == 2);
    const int delta_y = std::abs(y - preferred_y);
    const int delta_x = std::abs(x - preferred_x);
    const int loss = (detail * delta_y + (detail + 1) * delta_x) >> 1;
    return int16_t(((100 - loss) * int(base)) / 100);
}

bool run_pitch_optimizer(GameState& staged, int manager, int team, int formation) {
    if (staged.lineups_frozen || formation < 0 || formation > 7) return false;
    try {
        auto_lineup(staged, team, 0xe4);
    } catch (...) {
        return false;
    }
    return staged.manager_options[size_t(manager) * 5] == formation + 1 &&
           optimized_pitch_valid(staged, manager, team);
}

}  // namespace

void prepare_lineup_entry(GameState& g, int team) {
    const int manager = g.manager_of(team);
    const SquadRange range = squad_range(team);
    if (manager < 0 || team < 0 || range.count < 11 || range.first + range.count > g.squad_slots.size())
        throw std::invalid_argument("prepare_lineup_entry: unsupported manager squad");
    const size_t badge = size_t(team) * 2;
    if (badge + 1 >= g.club_heap_78.size())
        throw std::invalid_argument("prepare_lineup_entry: incomplete club badge table");

    // SquadCount(team, 2) counts occupied IDs over the whole squad range.
    // Original team rows are compact; reject a sparse map rather than applying
    // its count as a row bound to a different layout.
    size_t players = 0;
    for (size_t row = 0; row < range.count; ++row)
        players += (g.squad_slots[range.first + row] & 0x0fff) != 0x0fff;
    if (players < 11) throw std::invalid_argument("prepare_lineup_entry: too few players");
    for (size_t row = 0; row < players; ++row) {
        const int id = g.squad_slots[range.first + row] & 0x0fff;
        if (id == 0x0fff || size_t(id) >= g.players.size())
            throw std::invalid_argument("prepare_lineup_entry: sparse or invalid squad");
    }

    // PrepareLineupEntry scans backward for the last row with either field/bench
    // marker, then reassigns those marker bits by row, through row 16 only.
    int last = -1;
    for (int row = int(players) - 1; row >= 0; --row)
        if (g.squad_slots[range.first + size_t(row)] & 0xc000) { last = row; break; }
    for (int row = 0; row <= last && row < 17; ++row) {
        uint16_t& slot = g.squad_slots[range.first + size_t(row)];
        slot = uint16_t((slot & 0x3fff) | (row < 11 ? 0x8000 : 0x4000));
    }
}

bool set_pitch_automatic_weight_source(GameState& g, int manager_slot, int runtime_club,
                                       int weight_index, uint8_t old_weights, uint8_t new_weights) {
    if (!pitch_context_valid(g, manager_slot, runtime_club) || weight_index < 0 || weight_index > 2 ||
        g.memory.u8(::bmh::layout::GameMode) == 1)
        return false;
    const size_t at = size_t(manager_slot) * kManagerRecordSize + 0x288;
    if (g.manager_records[at] != old_weights) return false;
    const unsigned shift = unsigned(weight_index * 2);
    const uint8_t mask = uint8_t(3u << shift);
    const uint8_t next_field = uint8_t((((old_weights >> shift) & 3u) + 1u) & 3u);
    const uint8_t expected = uint8_t((old_weights & uint8_t(~mask)) | uint8_t(next_field << shift));
    if (new_weights != expected) return false;
    try {
        GameState staged = g;
        staged.manager_records[at] = new_weights;
        g = std::move(staged);
        return true;
    } catch (...) {
        return false;
    }
}

bool move_pitch_player_source(GameState& g, int manager_slot, int runtime_club,
                              int row, int player_id, uint16_t slot,
                              int old_depth, int old_lane, int new_depth, int new_lane) {
    if (!pitch_context_valid(g, manager_slot, runtime_club) || row < 0 || row >= 11 ||
        old_depth < 0 || old_depth > 19 || old_lane < 0 || old_lane > 8 ||
        new_depth < 0 || new_depth > 19 || new_lane < 0 || new_lane > 8 ||
        (old_depth == new_depth && old_lane == new_lane))
        return false;
    const SquadRange range = squad_range(runtime_club);
    const size_t record = range.first + size_t(row);
    const size_t board = size_t(manager_slot) * kManagerBoardSize;
    const size_t row_at = board + size_t(row) * kBoardRowSize;
    if (g.squad_slots[record] != slot || (slot & 0x0fff) != player_id ||
        player_id < 0 || size_t(player_id) >= g.players.size() || (slot & 0xc000) != 0x8000 ||
        ((g.players[size_t(player_id)].w364 >> 16) & 0x1f) != 0 ||
        int8_t(g.tactics[row_at + 0x41]) != old_depth ||
        int8_t(g.tactics[row_at + 0x42]) != old_lane)
        return false;
    int exact_occupant = -1;
    int indoor_partner = -1;
    for (int other = 0; other < 11; ++other) {
        if (other == row) continue;
        const uint16_t other_slot = g.squad_slots[range.first + size_t(other)];
        const size_t other_id = other_slot & 0x0fff;
        if (other_id >= g.players.size() || ((g.players[other_id].w364 >> 16) & 0x1f) != 0) continue;
        const size_t other_at = board + size_t(other) * kBoardRowSize;
        const int depth = int8_t(g.tactics[other_at + 0x41]);
        const int lane = int8_t(g.tactics[other_at + 0x42]);
        if (depth == new_depth && lane == new_lane) {
            exact_occupant = other;
            break;
        }
        const int dx = std::abs(new_depth - depth);
        const int dy = std::abs(new_lane - lane);
        if (dx < 2 && dy < 2) {
            if (!g.indoor) return false; // Outdoor source branch refuses adjacent targets.
            // PitchScreen keeps the last eligible hall neighbor encountered.
            // The exact-coordinate branch above remains the ordinary swap path.
            indoor_partner = other;
        }
    }
    try {
        GameState staged = g;
        auto* selected = staged.tactics.data() + row_at;
        if (exact_occupant >= 0) {
            auto* occupied = staged.tactics.data() + board + size_t(exact_occupant) * kBoardRowSize;
            const uint8_t occupied_depth = occupied[0x41], occupied_lane = occupied[0x42];
            occupied[0x41] = selected[0x41];
            occupied[0x42] = selected[0x42];
            selected[0x41] = occupied_depth;
            selected[0x42] = occupied_lane;
        } else {
            // PitchScreen clears this row, its old hall partner and every
            // reciprocal reference to either before installing the new move.
            for (size_t other = 0; other < 24; ++other) {
                auto* candidate = staged.tactics.data() + board + other * kBoardRowSize;
                if (other == size_t(row) ||
                    (indoor_partner >= 0 && other == size_t(indoor_partner)) ||
                    candidate[0x3f] == uint8_t(row) ||
                    (indoor_partner >= 0 && candidate[0x3f] == uint8_t(indoor_partner)))
                    candidate[0x3f] = 0xff;
            }
            selected[0x41] = uint8_t(new_depth);
            selected[0x42] = uint8_t(new_lane);
            if (g.indoor && indoor_partner >= 0) {
                auto* partner = staged.tactics.data() + board + size_t(indoor_partner) * kBoardRowSize;
                selected[0x3f] = uint8_t(indoor_partner);
                partner[0x3f] = uint8_t(row);
            }
        }
        staged.manager_options[size_t(manager_slot) * 5] = 0; // successful placement selects manual AA.
        g = std::move(staged);
        return true;
    } catch (...) {
        return false;
    }
}

bool apply_pitch_formation_source(GameState& g, int manager_slot, int runtime_club,
                                  int formation, uint8_t expected_weights) {
    if (!pitch_context_valid(g, manager_slot, runtime_club) || formation < 0 || formation > 7)
        return false;
    const size_t record = size_t(manager_slot) * kManagerRecordSize;
    if (g.manager_records[record + 0x288] != expected_weights ||
        (g.manager_records[record + 0x218 + size_t(g.indoor)] & uint8_t(1u << formation)) == 0)
        return false;
    try {
        GameState staged = g;
        staged.manager_options[size_t(manager_slot) * 5] = uint8_t(formation + 1);
        if (!run_pitch_optimizer(staged, manager_slot, runtime_club, formation)) return false;
        g = std::move(staged);
        return true;
    } catch (...) {
        return false;
    }
}

bool save_pitch_formation_source(GameState& g, int manager_slot, int runtime_club,
                                 int formation, uint8_t expected_weights,
                                 const std::array<uint8_t, 22>& formation_positions) {
    if (!pitch_context_valid(g, manager_slot, runtime_club) || formation < 4 || formation > 7)
        return false; // Ordinary Source access exposes only the four own slots for saving.
    const size_t record = size_t(manager_slot) * kManagerRecordSize;
    const size_t board = size_t(manager_slot) * kManagerBoardSize;
    const size_t table = size_t(manager_slot) * kFormationManagerSize +
                         (g.indoor ? 0xb0 : 0) + size_t(formation) * 0x16;
    const size_t mask = record + 0x218 + size_t(g.indoor);
    if (g.manager_records[record + 0x288] != expected_weights ||
        table + formation_positions.size() > g.formations.size())
        return false;
    for (size_t row = 0; row < 11; ++row) {
        const uint16_t slot = g.squad_slots[squad_range(runtime_club).first + row];
        const int depth = int8_t(g.tactics[board + row * kBoardRowSize + 0x41]);
        const int lane = int8_t(g.tactics[board + row * kBoardRowSize + 0x42]);
        if ((slot & 0xc000) != 0x8000 || depth < 0 || depth > 19 || lane < 0 || lane > 8 ||
            formation_positions[row] != uint8_t(depth) ||
            formation_positions[11 + row] != uint8_t(lane))
            return false;
    }
    try {
        GameState staged = g;
        std::copy(formation_positions.begin(), formation_positions.end(), staged.formations.begin() + std::ptrdiff_t(table));
        staged.manager_records[mask] |= uint8_t(1u << formation);
        staged.manager_options[size_t(manager_slot) * 5] = uint8_t(formation + 1);
        if (!run_pitch_optimizer(staged, manager_slot, runtime_club, formation)) return false;
        g = std::move(staged);
        return true;
    } catch (...) {
        return false;
    }
}

void install_lineup_rules(const LeImage& exe) {
    original_foreign_limit_enabled = exe.u8(layout::ForeignPlayerLimitEnabled) != 0;
}

static void auto_lineup_impl(GameState& g, int team, int mode, int manager) {
    if (team >= runtime_dimensions.league_teams && team != kNoTeam) return;  // teams without squads
    if (g.lineups_frozen) return;  // LineupRuntimeField (the season change)
    const bool improve_from_reserves = (mode & 0x80) != 0;
    mode &= 0x7F;

    // HumanManagerLetsAssistantPickTeamWhen: a human manager lets the assistant pick the team when his option byte 0
    // is set; its value - 1 is the formation, taken from the manager's own formation set.
    int formation = 0, set = 0;
    int weight = kMismatchWeight, familiarity = 0, detail = 0;
    if (manager >= 0) {
        const uint8_t raw_option = manager < 4 ? g.manager_options.at(size_t(manager) * 5) : 0;
        const int option = int8_t(raw_option - 1);
        if (raw_option == 0) {
            // Int2: with a manual human line-up, modes below 100 only
            // copy formation coordinates to a transient scratch table; mode
            // 0x65 (101) and all other modes below 100 return without mutating
            // the saved line-up. GameState has no such scratch table, so that
            // non-persistent coordinate copy is intentionally not represented.
            // The source enters DispatchLiveOverview only when the club-table bit in
            // *ClubBadgeSelectors agrees with CareerSessionRuntimeField. We retain that boundary rather
            // than applying the human normalizer to a different game mode.
            const size_t club_word = size_t(team) * 2;
            if (club_word + 1 >= g.club_heap_78.size())
                throw std::invalid_argument("auto_lineup: incomplete club badge table");
            uint16_t badge_word = uint16_t(g.club_heap_78[club_word] |
                                           uint16_t(g.club_heap_78[club_word + 1]) << 8);
            const bool mode_mismatch = ((badge_word >> 9) & 1) != unsigned(g.indoor);
            if (mode_mismatch) {
                // Int2 does not reject a manual club on a mode transition.
                // It clears the stored badge selector to bit 0, sets its indoor bit
                // from CareerSessionRuntimeField, and falls through to the ordinary assistant
                // scorer with formation 0. Keep the manager's assistant scoring
                // parameters below; AA itself remains off.
                badge_word = uint16_t((badge_word & 0x01ff) | (unsigned(g.indoor) << 9));
                g.club_heap_78[club_word] = uint8_t(badge_word);
                g.club_heap_78[club_word + 1] = uint8_t(badge_word >> 8);
                formation = 0;
                set = manager;
                const uint8_t style = g.manager_records.at(size_t(manager) * 0xB0C + 0x288);
                weight = 1 << ((style & 3) + 4);
                familiarity = (style >> 4) & 3;
                if (g.level >= 6) detail = (style >> 2) & 3;
            } else {
                if (mode == 0x65 || mode < 100) return;
                const SquadRange range = squad_range(team);
                const size_t board_base = size_t(manager) * 0x840;
                if (range.count < 11 || range.first + range.count > g.squad_slots.size() ||
                    g.tactics.size() < board_base + 0x840 || g.player_cards.size() != g.players.size())
                    throw std::invalid_argument("auto_lineup: incomplete manual line-up state");

                size_t player_count = 0;
                for (size_t row = 0; row < range.count; ++row)
                    player_count += (g.squad_slots[range.first + row] & 0x0fff) != 0x0fff;
                // DispatchLiveOverview works on whatever count the squad has: a short squad simply
                // fields fewer players. That happens to the national team outside the world
                // mode, which nobody nominates, when its human coach takes it over on an
                // international day (SwapManagerSlots2 -> Int2(0x165, 0xe4)).
                if (player_count > range.count)
                    throw std::invalid_argument("auto_lineup: manual line-up has an invalid squad size");
                for (size_t row = 0; row < player_count; ++row) {
                    const size_t id = g.squad_slots[range.first + row] & 0x0fff;
                    if (id == 0x0fff || id >= g.players.size())
                        throw std::invalid_argument("auto_lineup: manual line-up has a sparse squad");
                }

                auto board = [&](size_t row) { return g.tactics.data() + board_base + row * 0x58; };
                auto fit = [&](size_t row) {
                    const int id = g.squad_slots[range.first + row] & 0x0fff;
                    return id != 0x0fff && size_t(id) < g.players.size() &&
                           (g.players[size_t(id)].w364 >> 16 & 0x1f) == 0;
                };
                // DispatchLiveOverview / Fit: move a fit player from the end of the
                // roster into each injured row in the first 17 slots. Coordinates
                // stay with the destination board row after the full 0x58-byte swap.
                for (size_t row = 0; row < 17 && row < player_count; ++row) {
                    if (fit(row)) continue;
                    size_t replacement = player_count;
                    while (replacement > 0) {
                        --replacement;
                        if (fit(replacement)) break;
                    }
                    if (!fit(replacement) || replacement <= row) break;
                    std::swap(g.squad_slots[range.first + row], g.squad_slots[range.first + replacement]);
                    std::swap_ranges(board(row), board(row) + 0x58, board(replacement));
                    std::swap(board(row)[0x41], board(replacement)[0x41]);
                    std::swap(board(row)[0x42], board(replacement)[0x42]);
                }
                // DispatchLiveOverview: clear old status nibbles; fit players in the first
                // 11 rows start, the next six are bench. Invalid fit-player
                // coordinates are initialized to (0,0) as in the original.
                for (size_t row = 0; row < player_count; ++row) {
                    uint16_t& slot = g.squad_slots[range.first + row];
                    slot &= 0x0fff;
                    if (fit(row) && row < 17) {
                        slot |= uint16_t(row < 11 ? 0x8000 : 0x4000);
                        const int8_t x = int8_t(board(row)[0x41]), y = int8_t(board(row)[0x42]);
                        if (row < 11 && (x < 0 || x > 19 || y < 0 || y > 8)) {
                            board(row)[0x42] = 0;
                            board(row)[0x41] = board(row)[0x42];
                        }
                    }
                }
                // GatedByParamLive updates only players in flagged rows, preserving the
                // card's high bits and replacing its slot index in low five bits.
                for (size_t row = 0; row < player_count; ++row) {
                    const uint16_t slot = g.squad_slots[range.first + row];
                    if ((slot & 0xc000) == 0) continue;
                    const size_t id = slot & 0x0fff;
                    g.player_cards[id] = uint16_t((g.player_cards[id] & 0xffe0) | row);
                }
                return;
            }
        }
        if (raw_option != 0) {
            if (option == -1) throw std::invalid_argument("auto_lineup: invalid human formation option");
            formation = option;
            set = manager;
            const uint8_t style = g.manager_records.at(size_t(manager) * 0xB0C + 0x288);  // assistant settings
            weight = 1 << ((style & 3) + 4);
            familiarity = (style >> 4) & 3;
            if (g.level >= 6) detail = (style >> 2) & 3;
        }
    } else {
        formation = g.formation(team);
    }
    // formation table (block Formations): x at +0..10, y at +11..21; the original walks on past 11
    const size_t table = size_t(formation) * 0x16 + (g.indoor ? 0xb0 : 0) + size_t(set) * 0x160;
    auto board = [&](int slot) -> uint8_t* { return &g.tactics.at(size_t(manager) * 0x840 + size_t(slot) * 0x58); };

    g.team_flags.at(team) &= 0xFFFE;
    SquadRange range = squad_range(team);
    if (manager >= 0)
        for (size_t s = 0; s < range.count; s++) board(int(s))[0x3f] = 0xFF;
    int count = int(range.count);
    bool first_eleven_only = mode == 0x65 || mode < 100;
    if (first_eleven_only) count = 11;

    int16_t value[24];
    int16_t best_score[24];
    for (int i = 0; i < 24; i++) value[i] = best_score[i] = kUnavailable;

    int found = 0;
    for (; found < count; found++) {
        uint16_t& slot = g.squad_slots.at(range.first + found);
        int id = slot & 0x0FFF;
        if (id == 0x0FFF) break;
        const Player& p = g.players.at(id);
        if ((p.w364 >> 16 & 0x1F) == 0) {
            value[found] = int16_t(p.form() / 3 + p.condition() + p.technique());
            if (manager >= 0 && familiarity != 0) {
                // players not used to their place on the board are worth less
                int16_t f = int16_t(board(found)[0xf] - 0x19);
                if (f > 0x2d) f = int16_t(f * 2 - 0x19);
                value[found] = int16_t(value[found] - (f * familiarity) / 2);
            }
        }
        slot &= 0x3FFF;  // clear line-up and bench flags
    }
    int players = first_eleven_only ? 11 : found;

    for (int pos = 0; pos < players; pos++) {
        int type;
        if (pos < 11) type = kTypeByDepth[formation_byte(g, table + size_t(pos))];
        else type = kTypeBySlot[pos];
        int best = -1;
        for (int j = pos; j < players; j++) {
            uint16_t entry = g.squad_slots.at(range.first + j);
            int id = entry & 0x0FFF;
            if (value[j] == kUnavailable || (entry & 0xC000) != 0 || id == 0x0FFF) continue;
            int player_type = g.players.at(id).position();
            int mismatch;
            if (type == 9) mismatch = 0;
            else if ((player_type == 0) != (type == 0)) mismatch = int16_t(weight) * 25;
            else mismatch = int16_t(weight) * (player_type > type ? player_type - type : type - player_type);
            const Player& candidate = g.players.at(size_t(id));
            int16_t position_value = value[j];
            // Int2 reads the coordinate pointers only inside local_20's
            // level-6, non-goalkeeper branch. Normal and keeper/bench paths must
            // not require bytes beyond the eleven formation coordinates.
            if (detail != 0 && candidate.position() != 0) {
                const int x = formation_byte(g, table + size_t(pos));
                const int y = formation_byte(g, table + 0xb + size_t(pos));
                position_value = detailed_position_value(value[j], candidate, x, y, detail);
            }
            int score = int16_t(position_value - mismatch);
            if (best_score[pos] < score) {
                best = j;
                best_score[pos] = int16_t(score);
            }
        }
        if (best == -1) continue;
        if (type != 9) {
            if (manager >= 0) {
                // the chosen player gets the place on the board
                board(best)[0x41] = formation_byte(g, table + size_t(pos));
                board(best)[0x42] = formation_byte(g, table + 0xb + size_t(pos));
            }
            if (value[best] != kUnavailable) g.squad_slots.at(range.first + best) |= uint16_t(pos < 11 ? 0x8000 : 0x4000);
        }
        if (best != pos) {
            // SquadSlotsBoardEntriesChangePlaces: squad slots and the board entries change places
            std::swap(g.squad_slots.at(range.first + pos), g.squad_slots.at(range.first + best));
            if (manager >= 0) std::swap_ranges(board(pos), board(pos) + 0x58, board(best));
            std::swap(value[pos], value[best]);
        }
    }
    // Int2, local_34: bit 7 requests a second pass for field rows
    // 1..10. Compare reserves against each first-pass score using the base
    // value and role penalty, without the detailed coordinate penalty.
    // The keeper is excluded; first-eleven-only modes have no reserve range.
    if (improve_from_reserves) {
        for (int pos = 1; pos < 11; ++pos) {
            const int type = kTypeByDepth[formation_byte(g, table + size_t(pos))];
            int16_t best = best_score[pos];
            int replacement = -1;
            for (int row = 11; row < players; ++row) {
                const int id = g.squad_slots.at(range.first + row) & 0x0fff;
                if (value[row] == kUnavailable || id == 0x0fff) continue;
                const int role = g.players.at(id).position();
                if (role == 0) continue;
                const int16_t score = int16_t(value[row] - std::abs(role - type) * weight);
                if (best < score) {
                    replacement = row;
                    best = score;
                }
            }
            if (replacement < 0) continue;
            uint16_t& reserve = g.squad_slots.at(range.first + replacement);
            uint16_t& field = g.squad_slots.at(range.first + pos);
            reserve = uint16_t((reserve & 0x1fff) | 0x8000);
            field = uint16_t((field & 0x1fff) | (replacement < 17 ? 0x4000 : 0));
            std::swap(field, reserve);
            if (manager >= 0) {
                std::swap_ranges(board(pos), board(pos) + 0x58, board(replacement));
                board(pos)[0x41] = formation_byte(g, table + size_t(pos));
                board(pos)[0x42] = formation_byte(g, table + 0xb + size_t(pos));
            }
            std::swap(value[pos], value[replacement]);
        }
    }
    // Indoors a human manager's first eleven play in pairs that change together: every board
    // place without a partner (+0x3f = 0xFF) takes the next free one at most one field away
    // (both +0x41 and +0x42), both note each other's index.
    if (g.indoor && manager >= 0)
        for (int i = 0; i < 10; i++) {
            if (board(i)[0x3f] != 0xFF) continue;
            for (int j = i + 1; j <= 10; j++) {
                if (board(j)[0x3f] != 0xFF) continue;
                if (std::abs(int(int8_t(board(i)[0x42])) - int8_t(board(j)[0x42])) > 1 ||
                    std::abs(int(int8_t(board(i)[0x41])) - int8_t(board(j)[0x41])) > 1)
                    continue;
                board(i)[0x3f] = uint8_t(j);
                board(j)[0x3f] = uint8_t(i);
                break;
            }
        }
    if (g.projected_foreign_rule.value_or(original_foreign_limit_enabled)) {
        const int country = match_team_region(g,team) & 0xff;
        const int allowed = match_foreign_player_limit(g,team);
        std::vector<int> foreign_rows, foreign_roles;
        for (int row = 0; row < 11; ++row) {
            const int id = g.squad_slots.at(range.first + row) & 0x0fff;
            if (id != 0x0fff && nationality(g, id) != country) {
                foreign_rows.push_back(row);
                foreign_roles.push_back(g.players.at(id).position());
            }
        }
        int tolerance = 0;
        for (int attempt = 0; foreign_rows.size() > size_t(allowed) && attempt < 200; ++attempt) {
            ++tolerance;
            if (tolerance > 100) {
                // No suitable domestic reserve: retain the original short-lineup
                // outcome rather than fielding more foreign players than allowed.
                g.squad_slots.at(range.first + foreign_rows.back()) &= 0x1fff;
                foreign_rows.pop_back(); foreign_roles.pop_back();
                continue;
            }
            for (int row = 11; row < players && value[row] != kUnavailable; ++row) {
                const int id = g.squad_slots.at(range.first + row) & 0x0fff;
                if (id == 0x0fff || nationality(g, id) != country) continue;
                const int role = g.players.at(id).position();
                const int replaced_role = foreign_roles.back();
                if ((role == 0 && replaced_role != 0 && tolerance <= 7) ||
                    std::abs(role - replaced_role) >= tolerance) continue;
                const int field = foreign_rows.back();
                foreign_rows.pop_back(); foreign_roles.pop_back();
                auto& incoming = g.squad_slots.at(range.first + row);
                auto& outgoing = g.squad_slots.at(range.first + field);
                incoming = uint16_t((incoming & 0x1fff) | 0x8000);
                outgoing = uint16_t((outgoing & 0x1fff) | (row < 17 ? 0x4000 : 0));
                std::swap(incoming, outgoing);
                std::swap(value[row], value[field]);
                if (manager >= 0) {
                    std::swap_ranges(board(row), board(row) + 0x58, board(field));
                    std::swap(board(row)[0x41], board(field)[0x41]);
                    std::swap(board(row)[0x42], board(field)[0x42]);
                }
                tolerance = 0;
                if (foreign_rows.size() <= size_t(allowed)) break;
            }
        }
    }
    // After a full line-up every player in the eleven or on the bench notes his squad slot
    // (relative to the squad start) in bits 0-4 of his card word (*PlayerCards).
    if (!first_eleven_only && !g.player_cards.empty())
        for (int s = 0; s < found; s++) {
            const uint16_t entry = g.squad_slots.at(range.first + s);
            if (entry & 0xC000) {
                uint16_t& cards = g.player_cards.at(entry & 0x0FFF);
                cards = uint16_t((cards & 0xFFE0) | s);
            }
        }
}

void auto_lineup(GameState& g, int team, int mode) {
    auto_lineup_impl(g,team,mode,g.manager_of(team));
}

void auto_lineup_parked_club_source(GameState& g, int team, int mode) {
    constexpr size_t record = 4*0xb0c+0x290;
    if (team<0 || team>=runtime_dimensions.league_teams || g.manager_teams.size()!=1 || g.manager_teams[0]!=kNoTeam ||
        !g.memory.has(::bmh::layout::CoachActive) || g.memory.u8(::bmh::layout::CoachActive)==0 ||
        !g.memory.has(::bmh::layout::ManagerCount) || g.memory.u8(::bmh::layout::ManagerCount)!=1 ||
        !g.memory.has(::bmh::layout::OriginalManagerCount) || g.memory.u8(::bmh::layout::OriginalManagerCount)<1 || g.memory.u8(::bmh::layout::OriginalManagerCount)>4 ||
        record+2>g.manager_records.size() ||
        (int(g.manager_records[0x290]) | int(g.manager_records[0x291])<<8)!=kNoTeam ||
        (int(g.manager_records[record]) | int(g.manager_records[record+1])<<8)!=team ||
        g.tactics.size()<5*0x840)
        throw std::invalid_argument("auto_lineup: invalid FindManagerForClub parked club context");
    auto_lineup_impl(g,team,mode,4);
}

void quicksort_by_key(int16_t* items, uint32_t* keys, int count, bool ascending) {
    while (count > 1) {
        uint32_t pivot = keys[count >> 1];
        int i = -1, j = count;
        for (;;) {
            if (!ascending) {
                do i++; while (pivot < keys[i]);
                do j--; while (keys[j] < pivot);
            } else {
                do i++; while (keys[i] < pivot);
                do j--; while (pivot < keys[j]);
            }
            if (j <= i) break;
            std::swap(keys[i], keys[j]);
            std::swap(items[i], items[j]);
        }
        quicksort_by_key(items, keys, i, ascending);
        items += i;
        keys += i;
        count -= i;
    }
}

bool substitute(GameState& g, int team, int off, int mode, WatcomRandom& rand) {
    if (!(team < runtime_dimensions.league_teams || team == kNoTeam)) return false;
    const int manager = g.manager_of(team);
    int used = g.team_flags.at(team) >> 5 & 7;  // substitutions so far
    if (mode == 10) {
        // a random change of a human team only when the assistant may substitute (option 1)
        if (manager >= 0 && !g.manager_option(manager, 1)) return false;
        if (used >= kMaxSubstitutions - 1) return false;
    }

    SquadRange range = squad_range(team);
    int first = int(range.first);
    int last_bench = team > 0x59 ? 16 : 17;

    // --- candidates on the bench ------------------------------------------------------------
    int16_t slot[24];
    uint32_t score[24];
    int n = 0;
    for (int s = 11; s < last_bench; s++) {
        uint16_t entry = g.squad_slots.at(first + s);
        int id = entry & 0x0FFF;
        if (id == 0x0FFF) break;
        const Player& p = g.players.at(id);
        if (!(entry & 0x4000) || (p.w364 >> 16 & 0x1F) != 0 || (p.w364 >> 12 & 1) != 0) continue;
        int type = p.position();
        if (type == 0 && mode != 20) continue;
        int base;
        if (mode < 20) {
            if (mode < 10) {
                if (mode > 7) mode = 7;  // the original clamps its parameter here for good
                int trait = int(p.w35c >> 21 & 7);
                base = 0x70 - (trait > mode ? trait - mode : mode - trait) * 16;
            } else {
                base = rand.range(0, 99);
            }
        } else {
            if (off == first + s) continue;
            if (mode != 20) {
                int t = mode - 20;
                base = 0x300 - (t > type ? t - type : type - t) * 256;
            } else {
                base = (type == 0) << 12;
            }
        }
        int strength = p.strength(false);  // Strength(w35c, 0)
        score[n] = uint32_t(type + int16_t(base + strength) * 4);
        slot[n] = int16_t(s);
        n++;
    }
    if (n == 0) return false;
    quicksort_by_key(slot, score, n);
    int on = slot[0] + first;
    int on_type = int(score[0] & 3);

    // --- the player to take off (tactical and random changes) ------------------------------
    if (mode < 20) {
        n = 0;
        for (int s = 0; s < 11; s++) {
            uint16_t entry = g.squad_slots.at(first + s);
            if (!(entry & kSlotInLineup)) continue;
            const Player& p = g.players.at(entry & 0x0FFF);
            int type = p.position();
            // The original compares the relative slot with the absolute index of the
            // substitute here, which only matters for team 0.
            if (s == on) continue;
            if (!(type != 0 || (on_type == 0 && rand.range(0, 3) == 0))) continue;
            if (!(type == 0 || on_type != 0)) continue;
            int v;
            if (mode < 10) {
                int trait = int(p.w35c >> 21 & 7);
                v = (trait > mode ? trait - mode : mode - trait) << 5;
            } else {
                v = (type > on_type ? type - on_type : on_type - type) << 6;
            }
            uint32_t junk = uint32_t(p.form() / 3 - p.technique()) - uint32_t(p.condition());
            score[n] = uint32_t(int32_t(int16_t(v + (0x14A - int16_t(junk)))));
            slot[n] = int16_t(s);
            n++;
        }
        if (n == 0) return false;
        quicksort_by_key(slot, score, n);
        off = slot[0] + first;
    }
    if (used == kMaxSubstitutions) return false;

    // --- the change itself -----------------------------------------------------------------
    used++;
    std::swap(g.squad_slots.at(on), g.squad_slots.at(off));  // SquadSlotsBoardEntriesChangePlaces
    if (manager >= 0) {
        // the two tactics board slots are swapped too
        auto board = g.tactics.begin() + long(size_t(manager) * 0x840);
        std::swap_ranges(board + long(size_t(on - first) * 0x58), board + long(size_t(on - first + 1) * 0x58),
                         board + long(size_t(off - first) * 0x58));
    }
    g.squad_slots.at(on) &= 0x1FFF;
    g.squad_slots.at(off) &= 0x1FFF;
    g.players.at(g.squad_slots.at(off) & 0x0FFF).w364 |= 0x1000;  // the substitute has played
    g.squad_slots.at(off) |= 0x8000;
    g.squad_slots.at(on) |= 0x4000;
    if (manager >= 0) {
        // human teams: the re-sort uses a formation after the attitude (mode / 2 for the
        // tactical changes, else the manager's setting, record +0xd2): option byte 0 is set
        // to 4 - attitude for the call and restored afterwards
        int attitude = mode < 9 ? mode >> 1 : int8_t(g.manager_records.at(size_t(manager) * 0xB0C + 0xd2));  // (mode as clamped above)
        if (attitude > 3) attitude = 3;
        uint8_t& option = g.manager_options.at(size_t(manager) * 5);
        const uint8_t saved = option;
        option = uint8_t(4 - attitude);
        auto_lineup(g, team, 0x65);
        option = saved;
    } else {
        auto_lineup(g, team, 0x65);
    }
    g.team_flags.at(team) = uint16_t((g.team_flags.at(team) & 0xFF1F) | used << 5);
    return true;
}

bool manual_substitution(GameState& g, int team, int field_slot, int bench_slot) {
    const SquadRange range = squad_range(team);
    if (field_slot < 0 || field_slot >= 11 || bench_slot < 11 || size_t(bench_slot) >= range.count) return false;
    const size_t off = range.first + size_t(field_slot), on = range.first + size_t(bench_slot);
    const uint16_t out = g.squad_slots.at(off), in = g.squad_slots.at(on);
    if ((out & 0x0FFF) == 0x0FFF || !(out & kSlotInLineup)) return false;
    // the substitute: on the bench (slot flag 0x4000) and not yet played in this match
    // (w364 bit 12, set when he came on - a player taken off may not come back)
    if ((in & 0x0FFF) == 0x0FFF || !(in & 0x4000) || (g.players.at(in & 0x0FFF).w364 & 0x1000)) return false;
    int used = g.team_flags.at(team) >> 5 & 7;
    if (used >= kMaxSubstitutions) return false;
    used++;
    std::swap(g.squad_slots.at(on), g.squad_slots.at(off));  // SquadSlotsBoardEntriesChangePlaces
    const int manager = g.manager_of(team);
    if (manager >= 0) {
        // the board entries change places too, but the place on the pitch stays with the slot
        auto board = g.tactics.begin() + long(size_t(manager) * 0x840);
        auto a = board + long(size_t(field_slot) * 0x58), b = board + long(size_t(bench_slot) * 0x58);
        const uint8_t x = a[0x41], y = a[0x42];
        std::swap_ranges(a, a + 0x58, b);
        a[0x41] = x, a[0x42] = y;
    }
    g.squad_slots.at(on) &= 0x1FFF;
    g.squad_slots.at(off) &= 0x1FFF;
    g.players.at(g.squad_slots.at(off) & 0x0FFF).w364 |= 0x1000;  // the substitute has played
    g.squad_slots.at(off) |= kSlotInLineup;
    g.squad_slots.at(on) |= 0x4000;
    g.team_flags.at(team) = uint16_t((g.team_flags.at(team) & 0xFF1F) | used << 5);
    return true;
}

}  // namespace bmh

namespace bmh {

bool initialize_pitch_set_piece_source(GameState& g, int manager_slot, int runtime_club) {
    if (!pitch_context_valid(g, manager_slot, runtime_club)) return false;
    const size_t rec = size_t(manager_slot) * kManagerRecordSize;
    if (uint16_t(g.manager_records[rec + 0xb0a] | uint16_t(g.manager_records[rec + 0xb0b]) << 8) != 0x3253) {
        g.manager_records[rec + 0xafc] = g.manager_records[rec + 0xafd] = 0;
        g.manager_records[rec + 0xafe] = g.manager_records[rec + 0xaff] = 0;
        g.manager_records[rec + 0xb08] = g.manager_records[rec + 0xb09] = 0;
        g.manager_records[rec + 0xb0a] = 0x53; g.manager_records[rec + 0xb0b] = 0x32;
    }
    return true;
}

bool normalize_pitch_set_piece_source(GameState& g, int manager_slot, int runtime_club) {
    if (!initialize_pitch_set_piece_source(g, manager_slot, runtime_club)) return false;
    const size_t rec = size_t(manager_slot) * kManagerRecordSize;
    const auto member = [&](uint16_t stored) {
        if (!stored || stored > 0x0fff) return false;
        const int id = int(stored) - 1;
        const auto range = squad_range(runtime_club);
        for (size_t i = 0; i < range.count; ++i)
            if ((g.squad_slots[range.first + i] & 0x0fff) == id) return true;
        return false;
    };
    for (size_t off : {size_t(0xafc), size_t(0xafe), size_t(0xb08)}) {
        uint16_t value = uint16_t(g.manager_records[rec + off] | uint16_t(g.manager_records[rec + off + 1]) << 8);
        if (!member(value)) g.manager_records[rec + off] = g.manager_records[rec + off + 1] = 0;
    }
    return true;
}

bool set_pitch_set_piece_taker_source(GameState& g, int manager_slot, int runtime_club,
                                      int selector, int row, int player_id, uint16_t slot,
                                      uint16_t expected_old_value) {
    if (!pitch_context_valid(g, manager_slot, runtime_club) || selector < 2 || selector > 4 ||
        row < 0) return false;
    const auto range = squad_range(runtime_club);
    const size_t at = range.first + size_t(row), rec = size_t(manager_slot) * kManagerRecordSize;
    if (size_t(row) >= range.count || at >= range.first + range.count || g.squad_slots[at] != slot || (slot & 0xc000) == 0 ||
        (slot & 0x0fff) != player_id || player_id < 0 || size_t(player_id) >= g.players.size() ||
        ((g.players[size_t(player_id)].w364 >> 16) & 0x1f) != 0 ||
        uint16_t(g.manager_records[rec + 0xb0a] | uint16_t(g.manager_records[rec + 0xb0b]) << 8) != 0x3253)
        return false;
    static constexpr size_t offsets[] = {0, 0, 0xafc, 0xafe, 0xb08};
    const size_t field = rec + offsets[selector];
    const uint16_t current = uint16_t(g.manager_records[field] | uint16_t(g.manager_records[field + 1]) << 8);
    if (current != expected_old_value) return false;
    const uint16_t encoded = uint16_t(player_id + 1);
    g.manager_records[field] = uint8_t(encoded); g.manager_records[field + 1] = uint8_t(encoded >> 8);
    return true;
}

bool auto_pitch_set_piece_takers_source(GameState& g, int manager_slot, int runtime_club,
                                        const std::array<uint16_t, 3>& expected_old_values) {
    if (!pitch_context_valid(g, manager_slot, runtime_club)) return false;
    const size_t rec = size_t(manager_slot) * kManagerRecordSize;
    if (uint16_t(g.manager_records[rec + 0xb0a] | uint16_t(g.manager_records[rec + 0xb0b]) << 8) != 0x3253)
        return false;
    static constexpr size_t offsets[] = {0xafc, 0xafe, 0xb08};
    for (size_t i = 0; i < 3; ++i) {
        const auto off = offsets[i];
        if (uint16_t(g.manager_records[rec + off] | uint16_t(g.manager_records[rec + off + 1]) << 8) != expected_old_values[i])
            return false;
    }
    for (const auto off : offsets) g.manager_records[rec + off] = g.manager_records[rec + off + 1] = 0;
    return true;
}

bool swap_pitch_roster_rows_source(GameState& g,int manager,int team,int first_row,int second_row,
    uint16_t first_slot,uint16_t second_slot,bool foreign_rule_enabled) {
    if(manager<0 || manager>3 || size_t(manager)>=g.manager_teams.size() || g.manager_teams[size_t(manager)]!=team ||
        team<0 || size_t(team)>=g.club_ids.size() || g.lineups_frozen || first_row<0 || second_row<0 || first_row==second_row ||
        g.tactics.size()<size_t(manager+1)*0x840 || g.manager_options.size()<size_t(manager+1)*5 || g.player_cards.size()!=g.players.size())return false;
    const auto range=squad_range(team);
    if((range.count!=16 && range.count!=24) || size_t(first_row)>=range.count || size_t(second_row)>=range.count ||
        range.first+range.count>g.squad_slots.size())return false;
    const size_t first=range.first+size_t(first_row),second=range.first+size_t(second_row);
    const auto first_id=size_t(first_slot&0xfff),second_id=size_t(second_slot&0xfff);
    if(g.squad_slots[first]!=first_slot || g.squad_slots[second]!=second_slot || first_id==0xfff || second_id==0xfff ||
        first_id>=g.players.size() || second_id>=g.players.size() ||
        ((g.players[first_id].w364>>16)&0x1f)!=0 || ((g.players[second_id].w364>>16)&0x1f)!=0 ||
        (foreign_rule_enabled && ((first_row<11)!=(second_row<11))))return false;
    const size_t board_base = size_t(manager) * 0x840;
    auto* a = g.tactics.data() + board_base + size_t(first_row) * 0x58;
    auto* b = g.tactics.data() + board_base + size_t(second_row) * 0x58;
    const uint8_t ax = a[0x41], ay = a[0x42], bx = b[0x41], by = b[0x42];
    // Original SquadSlotsBoardEntriesChangePlaces moves the player/tactics entry, then restores
    // the 0xc000 role bits to their roster rows. Preserve bit 12 and the
    // player id with the entry; keep the starter/bench flags with the slot.
    g.squad_slots[first] = uint16_t((second_slot & 0x1fff) | (first_slot & 0xc000));
    g.squad_slots[second] = uint16_t((first_slot & 0x1fff) | (second_slot & 0xc000));
    std::swap_ranges(a, a + 0x58, b);
    a[0x41] = ax; a[0x42] = ay; b[0x41] = bx; b[0x42] = by;
    // GatedByParamLive is applied to every marked row, not just the two swapped
    // players. Keep card-side row identity synchronized with the roster.
    for (size_t row = 0; row < range.count; ++row) {
        const uint16_t slot = g.squad_slots[range.first + row];
        if ((slot & 0xc000) == 0) continue;
        const size_t id = slot & 0x0fff;
        if (id >= g.players.size()) continue;
        g.player_cards[id] = uint16_t((g.player_cards[id] & 0xffe0) | uint16_t(row));
    }
    g.manager_options.at(size_t(manager) * 5) = 0; // source manual mode selects DispatchLiveOverview
    return true;
}

bool set_player_order_source(GameState& g, int manager_slot, int runtime_club, int row, int player, int aspect, int value) {
    if (manager_slot < 0 || size_t(manager_slot) >= g.manager_teams.size() || g.manager_teams[size_t(manager_slot)] != runtime_club ||
        aspect < 0 || aspect > 3 || row < 0 || row >= 24)
        return false;
    const SquadRange range = squad_range(runtime_club);
    if (size_t(row) >= range.count || range.first + size_t(row) >= g.squad_slots.size()) return false;
    const uint16_t slot = g.squad_slots[range.first + size_t(row)];
    // only a starter on the pitch can be selected in the ORDER view
    if ((slot & 0x0fff) != uint16_t(player) || !(slot & 0x8000)) return false;
    const size_t at = size_t(manager_slot) * 0x840 + size_t(row) * 0x58 + 2 + size_t(aspect);
    if (at >= g.tactics.size()) return false;
    g.tactics[at] = uint8_t(std::clamp(value, kOrderMin, kOrderMax));  // ClampInteger(&v, 2, 0x28)
    return true;
}

}  // namespace bmh
