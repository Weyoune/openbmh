// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "modern/game/modern_state.hpp"
#include "modern/game/environment.hpp"
#include "modern/game/matchday.hpp"
#include "modern/game/matchday_setup.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>

#include "modern/game/cup.hpp"
#include "game/schedule.hpp"
#include "game/sportschau_presentation.hpp"
#include "game/table.hpp"

namespace bmh::modern {

namespace {

// Competition class of each descriptor type (MatchEngineRuntimeField9): 0 league (cards count per
// team), 1-4 cup competitions with their own card counters, 0xFF = none.
constexpr uint8_t kCompetitionClass[24] = {0, 1, 2, 2, 2, 3, 3, 3, 3, 255, 0, 255,
                                           4, 255, 255, 255, 255, 255, 255, 1, 0, 0, 0, 0};
// Clubs per league (LeagueSizes): five leagues of 18, four regional leagues of 12.
// (the clubs per league are kLeagueSizes, schedule.hpp)
// Three points for a win in every league (CareerSession_BeginFriendlyOpponentTable_RuntimeField, index 9 = "no league" reads the
// following table byte 18, also non-zero).
constexpr bool kThreePoints[10] = {true, true, true, true, true, true, true, true, true, true};
constexpr size_t kClubRecord = 0x4a;
constexpr size_t kManagerRecord = 0xB0C;
constexpr size_t kBoard = 0x840, kBoardSlot = 0x58;

uint32_t rd32(const std::vector<uint8_t>& v, size_t o) {
    return uint32_t(v.at(o) | v.at(o + 1) << 8 | v.at(o + 2) << 16 | uint32_t(v.at(o + 3)) << 24);
}
void wr32(std::vector<uint8_t>& v, size_t o, uint32_t x) {
    for (int i = 0; i < 4; i++) v.at(o + i) = uint8_t(x >> (8 * i));
}
uint16_t rd16(const std::vector<uint8_t>& v, size_t o) { return uint16_t(v.at(o) | v.at(o + 1) << 8); }
void wr16(std::vector<uint8_t>& v, size_t o, uint16_t x) {
    v.at(o) = uint8_t(x);
    v.at(o + 1) = uint8_t(x >> 8);
}

int competition_class(int type) { return type >= 0 && type < 24 ? kCompetitionClass[type] : 255; }

// TeamLeague2: league (0-8) of a club id, 9 for clubs outside the leagues.
int league_of(int team) {
    if (team > 0x164) return 0;
    int end = 0;
    for (int l = 0; l < 9; l++) {
        end += kLeagueSizes[l];
        if (team < end) return l;
    }
    return 9;
}

// ClubRecord: record result `idx` of a club. The value of a result is
// home * home_weight - away * away_weight; `lower` keeps the smallest instead of the
// largest value. The stored result uses away_weight 1 if it was negative.
void club_record(ModernState& g, int club, int idx, int home, int away, bool lower, int home_weight, int away_weight, int opponent) {
    size_t base = size_t(club) * kClubRecord;
    uint8_t& r = g.club_table.at(base + 0x15 + idx);
    int old_value = (r >> 4) * home_weight - (r & 0x0F) * away_weight;
    int new_value = home * home_weight - away * away_weight;
    if (!lower ? new_value <= old_value : old_value <= new_value) return;
    if (away_weight < 0) away_weight = 1;
    r = uint8_t((uint8_t(home * home_weight) << 4) + uint8_t(away * away_weight));
    wr16(g.club_table, base + 0x2a + idx * 2, g.club_ids.at(opponent));
    wr16(g.club_table, base + 0x3a + idx * 2, uint16_t(std::min<uint32_t>(g.date, 0xFFFF)));
}

}  // namespace

bool match_score(const MatchGroup& g, int m, int& home, int& away) {
    if (!g.results.empty()) {
        home = g.results.at(m) >> 4;
        away = g.results.at(m) & 0x0F;
        return true;
    }
    if (g.cup_results.size() == size_t(g.matches())) {
        home = g.cup_results[m] >> 5 & 0x1F;
        away = g.cup_results[m] & 0x1F;
        return true;
    }
    return false;
}

int sportschau_source_setting_index(const ModernState& state, const MatchGroup& group) {
    const uint32_t id = le32(group.desc, 0xcc);
    if (id <= 0x14) return id < 5 ? int(id) : std::min(int(id) + 12, 0x13);
    const uint32_t source = id + 0x18;
    return state.memory.has(source) ? int(uint8_t(state.memory.u8(source) + 5)) : -1;
}

SportschauFixtureView sportschau_fixture_view(const ModernState& state, const MatchGroup& group,
                                               int group_index, int fixture_index,
                                               const std::vector<ShowEvent>& show_events) {
    SportschauFixtureView view;
    if (group_index < 0 || group_index > 0xff || fixture_index < 0 || fixture_index > 0xff ||
        size_t(fixture_index * 2 + 1) >= group.fixtures.size())
        return view;

    view.group = uint8_t(group_index);
    view.fixture = uint8_t(fixture_index);
    view.competition_type = group.desc.at(0);
    view.fixture_flags = group.desc.at(1);
    view.source_footer_variant = state.indoor ? 2 : ((group.desc.at(1) & 8) != 0 ? 1 : 0);
    view.has_replay_progression_mode = state.memory.has(::bmh::layout::ReplayProgressionMode);
    if (view.has_replay_progression_mode) view.replay_progression_mode = state.memory.u8(::bmh::layout::ReplayProgressionMode);
    view.has_replay_chance_mode = state.memory.has(::bmh::layout::ReplayChanceMode);
    if (view.has_replay_chance_mode) view.replay_chance_mode = state.memory.u8(::bmh::layout::ReplayChanceMode);
    view.source_selection_available = state.sportschau_selection_available;
    view.competition_id = le32(group.desc, 0xcc);
    view.source_setting_index = int16_t(modern::sportschau_source_setting_index(state, group));
    view.home_team = int16_t(group.fixtures.at(size_t(fixture_index * 2)));
    view.away_team = int16_t(group.fixtures.at(size_t(fixture_index * 2 + 1)));
    view.source_selected = view.source_selection_available && modern::sportschau_selected(state, view.home_team);
    if (size_t(fixture_index) < group.end_minute.size())
        view.source_played_minute = group.end_minute[size_t(fixture_index)];
    auto as_bytes = [](const std::string& s) {
        return std::vector<uint8_t>(s.begin(), s.end());
    };
    auto set_team = [&](int team, std::vector<uint8_t>& full, std::vector<uint8_t>& short_name,
                        bool& short_name_available,
                        bool& requires_font_fit, bool& requires_longest_word, uint16_t& badge_word) {
        const std::string name = state.club_name(team);
        full = as_bytes(name);
        if (team >= 0 && size_t(team) * 2 + 1 < state.club_heap_78.size())
            badge_word = uint16_t(state.club_heap_78[size_t(team) * 2] |
                                  uint16_t(state.club_heap_78[size_t(team) * 2 + 1]) << 8);
        if (team >= 0 && team < first_foreign_cup()) {
            requires_font_fit = true;
            if (state.memory.has(::bmh::layout::ClubShortNameOffsets + uint32_t(team))) {
                const uint8_t source = state.memory.u8(::bmh::layout::ClubShortNameOffsets + uint32_t(team));
                const size_t offset = source >> 4;
                const size_t length = (source & 0x0f) + 1;
                if (offset <= full.size()) {
                    short_name_available = true;
                    const size_t count = std::min(length, full.size() - offset);
                    short_name.assign(full.begin() + std::ptrdiff_t(offset),
                                      full.begin() + std::ptrdiff_t(offset + count));
                }
            }
        } else {
            short_name = full;
            short_name_available = !short_name.empty();
            requires_font_fit = true;
            requires_longest_word = true;
        }
    };
    set_team(view.home_team, view.home_name, view.home_short_name, view.home_short_name_available,
             view.home_name_requires_font_fit, view.home_name_requires_longest_word, view.home_badge_word);
    set_team(view.away_team, view.away_name, view.away_short_name, view.away_short_name_available,
             view.away_name_requires_font_fit, view.away_name_requires_longest_word, view.away_badge_word);
    int home = 0, away = 0;
    view.score_known = modern::match_score(group, fixture_index, home, away);
    if (view.score_known) {
        view.home_goals = uint8_t(home);
        view.away_goals = uint8_t(away);
    }

    uint16_t ordinal = 0;
    for (size_t i = 0; i + 1 < group.log.size(); i += 2) {
        const uint16_t code_player = group.log[i];
        const uint16_t team_value = group.log[i + 1];
        const int team = team_value >> 7;
        if (team != view.home_team && team != view.away_team) continue;
        SportschauLogEvent event;
        event.group = view.group;
        event.fixture = view.fixture;
        event.event_ordinal = ordinal++;
        event.source_word_index = uint16_t(i);
        event.source_code_player_word = code_player;
        event.source_team_value_word = team_value;
        event.source_code = uint8_t(code_player >> 12);
        event.player_id = uint16_t(code_player & 0x0fff);
        event.team_id = int16_t(team);
        event.source_value = uint8_t(team_value & 0x7f);
        event.consumes_following_pair = event.source_code == 0 || event.source_code == 1;
        event.has_following_word = i + 2 < group.log.size();
        event.has_following_pair = i + 3 < group.log.size();
        if (event.has_following_word) {
            event.following_code_player_word = group.log[i + 2];
            const int player = event.following_code_player_word & 0x0fff;
            if (player != 0x0fff) event.following_player_name = as_bytes(state.player_name(player));
        }
        if (event.has_following_pair) event.following_team_value_word = group.log[i + 3];
        if (event.player_id != 0x0fff) event.player_name = as_bytes(state.player_name(event.player_id));
        view.log_events.push_back(std::move(event));
    }

    for (const ShowEvent& event : show_events)
        if (event.group == view.group && event.match == view.fixture) view.show_events.push_back(event);
    return view;
}

bool book_money(ModernState& g, int manager, int32_t amount, bool force, bool statistics) {
    if (g.level == 0) return true;
    size_t rec = size_t(manager) * kManagerRecord;
    const uint32_t balance = rd32(g.manager_records, rec + 0x314);
    const uint32_t credit = rd32(g.manager_records, rec + 0x31c);
    // BookMoney2 adds the Native dwords before a signed comparison and subtracts
    // the amount in the same 32-bit register width. Preserve that wrap without
    // C++ signed-overflow undefined behavior at the account limits.
    if (!force && amount >= 0 && std::bit_cast<int32_t>(balance + credit) < amount) return false;
    wr32(g.manager_records, rec + 0x314, balance - uint32_t(amount));
    if (statistics) {
        if (amount < 0)
            wr32(g.manager_records, rec + 0x874, rd32(g.manager_records, rec + 0x874) - uint32_t(amount));
        else
            wr32(g.manager_records, rec + 0x8a0, rd32(g.manager_records, rec + 0x8a0) + uint32_t(amount));
    }
    return true;
}

uint32_t pay_bonus(ModernState& g, int manager, int board_slot, int index, bool pay) {
    size_t slot = size_t(manager) * kBoard + size_t(board_slot) * kBoardSlot;
    uint32_t rate = g.tactics.at(slot + 0x12 + index);
    if (rate == 0) return 0;
    uint32_t factor = index > 2 ? 5 : 1;
    uint32_t amount = factor * (rate * rd32(g.tactics, slot + 0x50)) / 100;
    if (pay) {
        size_t rec = size_t(manager) * kManagerRecord;
        uint32_t tax = g.manager_records.at(rec + 0x172);
        if (tax != 0) amount -= ((tax >> 5 & 7) + 3) * amount * 10 / 100;
        modern::book_money(g, manager, int32_t(amount), true);
        wr32(g.manager_records, rec + 0x334, rd32(g.manager_records, rec + 0x334) + amount);
    }
    return amount;
}

void player_statistic(ModernState& g, int type, int player, int manager, int slot, int kind, int minute) {
    int cls = (type == 0xb && manager >= 0 && !g.manager_teams.empty() && g.manager_teams[0] == no_team())
                  ? 3
                  : int8_t(competition_class(type));
    if (cls < 0 || cls >= 4) return;
    Player& p = g.players.at(player);
    const uint32_t w364 = p.w364;
    const size_t board = manager >= 0 ? size_t(manager) * kBoard + size_t(slot) * kBoardSlot : 0;
    if (kind == 0) {  // appearance
        if (cls == 3 && (g.player_bonus.at(player) & 0x7F) < 0x7F) g.player_bonus.at(player)++;
        if (manager >= 0) {
            modern::pay_bonus(g, manager, slot, 2, true);
            if (cls < 3) g.tactics.at(board + 7 + cls)++;
            wr16(g.tactics, board + 0x48, uint16_t(rd16(g.tactics, board + 0x48) + 1));
        }
        if (cls == 0) p.w364 = (w364 & 0x03FFFFFFu) | ((w364 >> 26) + 1) << 26;  // league games
    } else if (kind == 1) {  // goal
        if (manager >= 0) {
            modern::pay_bonus(g, manager, slot, 0, true);
            wr16(g.tactics, board + 0x4a, uint16_t(rd16(g.tactics, board + 0x4a) + 1));
            if (cls == 0) g.manager_records.at(size_t(manager) * kManagerRecord + 0x1ae + std::min(minute, 89) / 5)++;
        }
        // league goals in bits 2-7 (a 64th goal carries into bit 8, as in the original)
        if (cls == 0) p.w364 = (w364 & 0xFFFFFF03u) | ((((w364 >> 2) & 0x3F) << 2) + 4);
    } else if (kind == 2) {  // assist
        if (manager >= 0) wr16(g.tactics, board + 0x4c, uint16_t(rd16(g.tactics, board + 0x4c) + 1));
        if (cls == 0) p.w368 = (p.w368 & 0xFF03FFFFu) | ((((p.w368 >> 18) & 0x3F) + 1) << 18);
    }
}

void finish_match(ModernState& g, MatchGroup& grp, int m, int minute, GameRandom& game) {
    if (grp.end_minute.at(m) == 0) grp.end_minute.at(m) = uint8_t(minute);
    for (int side = 0; side < 2; side++) {
        int manager = g.manager_of(grp.fixtures.at(m * 2 + side));
        if (manager < 0) continue;
        int home = 0, away = 0;
        modern::match_score(grp, m, home, away);
        int own = side ? away : home, opp = side ? home : away;
        int points = 0;
        if (own == opp) points = 1;
        if (int8_t(own) > int8_t(opp)) points = 2;
        for (int slot = 0; slot < 24; slot++) {
            size_t at = size_t(manager) * kBoard + size_t(slot) * kBoardSlot;
            int start = int8_t(g.tactics.at(at + 0x10));  // minute the player came on
            for (int i = 0; i < points; i++) modern::pay_bonus(g, manager, slot, 1, true);
            if (start >= 126) continue;
            // Getting used to the place: a quarter of the minutes played (half in the
            // competitions 5 and 7), at most 99.
            int8_t delta = int8_t((minute - start) / 4);
            if (grp.desc.at(0) == 5 || grp.desc.at(0) == 7) delta = int8_t(delta / 2);
            if (g.indoor) start = int8_t(start) >> 2;
            int used = int8_t(g.tactics.at(at + 0xf)) + delta;
            g.tactics.at(at + 0xf) = uint8_t(std::clamp(used, 0, 99));
            // The player's own place (bytes 0/1) moves one step towards the board place.
            if (game.range(0, 90) < minute - start) {
                for (int k = 0; k < 2; k++) {
                    uint8_t& cur = g.tactics.at(at + k);
                    uint8_t target = g.tactics.at(at + 0x41 + k);
                    if (cur < target) cur++;
                    if (cur > target) cur--;
                }
            }
        }
    }
}

void update_club_table(ModernState& g, int home, int away, int hg, int ag) {
    if (g.indoor || g.club_table.empty()) return;
    const size_t H = size_t(home) * kClubRecord, A = size_t(away) * kClubRecord;
    auto add16 = [&](size_t o, int v) { wr16(g.club_table, o, uint16_t(rd16(g.club_table, o) + v)); };
    // Only the first league and the regional leagues keep points in this table (the
    // condition tests the home club only).
    const bool league = home < kLeagueSizes[0] || (home >= german_clubs() && home < league_clubs());
    const int win_points = kThreePoints[league_of(home)] ? 3 : 2;
    if (league) {
        add16(H + 0x20, hg);
        add16(H + 0x22, ag);
        add16(A + 0x20, ag);
        add16(A + 0x22, hg);
    }
    uint8_t h[21], a[21];  // streaks: t * 3 + (0 all, 1 home, 2 away)
    std::copy_n(g.club_table.begin() + H, 21, h);
    std::copy_n(g.club_table.begin() + A, 21, a);
    club_record(g, home, 2, hg, ag, false, 1, 0, away);   // most goals scored at home
    club_record(g, away, 7, hg, ag, false, 1, 0, home);   // most goals conceded away
    club_record(g, home, 3, hg, ag, false, 0, -1, away);  // most goals conceded at home
    club_record(g, away, 6, hg, ag, false, 0, -1, home);  // most goals scored away
    auto bump = [](uint8_t* s, int t, int j) { s[t * 3 + j]++, s[t * 3]++; };
    auto clear = [](uint8_t* s, int t, int j) { s[t * 3 + j] = 0, s[t * 3] = 0; };
    // streak 5: without conceding, 6: without scoring
    if (ag == 0) bump(h, 5, 1), bump(a, 6, 2);
    else clear(h, 5, 1), clear(a, 6, 2);
    if (hg == 0) bump(h, 6, 1), bump(a, 5, 2);
    else clear(h, 6, 1), clear(a, 5, 2);
    if (ag < hg) {  // home win
        if (league) {
            add16(H + 0x1e, win_points);
            add16(H + 0x24, 1);
            add16(A + 0x28, 1);
        }
        bump(h, 0, 1);
        for (int t = 1; t < 4; t++) clear(h, t, 1);
        club_record(g, home, 0, hg, ag, false, 1, 1, away);  // biggest home win
        club_record(g, away, 5, hg, ag, false, 1, 1, home);  // biggest away defeat
        bump(h, 4, 1);
        clear(a, 0, 2), clear(a, 2, 2), clear(a, 4, 2);
        bump(a, 1, 2), bump(a, 3, 2);
    } else if (hg == ag) {  // draw
        if (league) {
            add16(H + 0x1e, 1);
            add16(A + 0x1e, 1);
            add16(H + 0x26, 1);
            add16(A + 0x26, 1);
        }
        clear(h, 0, 1), clear(h, 1, 1);
        for (int t = 2; t < 5; t++) bump(h, t, 1);
        clear(a, 0, 2), clear(a, 1, 2);
        for (int t = 2; t < 5; t++) bump(a, t, 2);
    } else {  // away win
        if (league) {
            add16(A + 0x1e, win_points);
            add16(H + 0x28, 1);
            add16(A + 0x24, 1);
        }
        bump(a, 0, 2);
        for (int t = 1; t < 4; t++) clear(a, t, 2);
        clear(h, 0, 1), clear(h, 2, 1), clear(h, 4, 1);
        bump(a, 4, 2);
        bump(h, 1, 1), bump(h, 3, 1);
        club_record(g, home, 1, hg, ag, true, 1, 1, away);  // biggest home defeat
        club_record(g, away, 4, hg, ag, true, 1, 1, home);  // biggest away win
    }
    std::copy_n(h, 21, g.club_table.begin() + H);
    std::copy_n(a, 21, g.club_table.begin() + A);
    // Human managers keep their club's best streaks (manager record +0x5e).
    for (int side = 0; side < 2; side++) {
        int club = side ? away : home;
        const uint8_t* s = side ? a : h;
        for (size_t k = 0; k < g.manager_teams.size(); k++) {
            if (g.manager_teams[k] != club) continue;
            for (int i = 0; i < 21; i++) {
                uint8_t& best = g.manager_records.at(k * kManagerRecord + 0x5e + i);
                if (best < s[i]) best = s[i];
            }
        }
    }
}

void update_morale(ModernState& g, int team, int own_goals, int opp_goals, int side, WatcomRandom& rand) {
    uint8_t& morale = g.team_morale.at(team);
    const int value = morale & 0x3F;
    int sign = 1;
    int diff = int16_t(side + (own_goals - opp_goals) + (5 - value / 5));
    int step = std::abs(25 - value) / 5;
    if (diff < 0 || (diff == 0 && value > 25)) {
        sign = -1;
        diff = -diff;
    }
    // Far from the middle (25) morale moves slowly towards the extremes.
    if ((sign == 1 && value > 12) || (sign == -1 && value < 37)) step = 5 - step;
    if (int16_t(step) == 0) step = 1;
    diff = std::clamp(diff, 1, 3);
    int range = int16_t(step * diff);
    if (range > 12) range = 12;
    if (range == 0) return;
    int v = int16_t(value + sign * int16_t(rand.range(1, range)));
    v = std::clamp(v, 0, 0x2f);
    morale = uint8_t(v | (morale & 0xC0));
}

void develop_players(ModernState& g, const MatchGroup& grp, int m, GameRandom& game, WatcomRandom& rand) {
    const int type = grp.desc.at(0);
    for (int side = 0; side < 2; side++) {
        const int team = grp.fixtures.at(m * 2 + side);
        if (!(team < first_foreign_cup() || team == no_team())) continue;
        const uint8_t cls = (type == 0xb && team == no_team()) ? 3 : uint8_t(competition_class(type));
        const int manager = g.manager_of(team);
        const SquadRange range = squad_range(team);

        // 1 in 11: the team gets the next trend of the rotation (none/down, neutral, up);
        // the rotation is steered by the balance of all attribute changes so far.
        if (game.range(0, 10) == 0) {
            if (g.development_balance < -50) g.trend_rotation = 2;
            if (g.development_balance > 50) g.trend_rotation = 0;
            uint16_t& f = g.team_flags.at(team);
            f = uint16_t(((f & 0xFFF8) + g.trend_rotation * 2) & 0xFFFF);
            if (++g.trend_rotation == 3) g.trend_rotation = 0;
        }
        const int8_t trend = int8_t(((g.team_flags.at(team) >> 1) & 3) - 1);
        int capped = 0;  // local_30: attributes that hit 99 (carries over between players)

        for (size_t s = range.first; s < range.first + range.count; s++) {
            const int player = g.squad_slots.at(s) & 0x0FFF;
            if (player == 0xFFF) break;
            Player& p = g.players.at(player);
            const int odds = manager >= 0 ? 6 : 3;

            if (trend != 0 && game.range(0, 3) == 0) {
                int8_t d = trend;
                if (rand.range(0, 4) == 0) d = int8_t(-trend);
                // form (shift 0, kept within 35-65), technique (7), condition (14)
                uint32_t w = p.w35c;
                for (int sh = 0; sh < 15; sh += 7) {
                    if (sh >= 7 && capped == 0 && game.range(0, odds) != 0) continue;
                    const int8_t old = int8_t((w >> sh) & 0x7F);
                    int8_t v = int8_t(old + d);
                    if (capped != 0 || game.range(0, 1) != 0) v = int8_t(v + d);
                    if (sh != 0) {
                        if (v < 0) v = 0;
                        if (v > 99) {
                            // Quirk of the original: the first attribute to pass 99 restarts
                            // the loop - 99 is written to the form and technique and
                            // condition are processed (again).
                            if (capped == 0) sh = 0;
                            v = 99;
                            capped++;
                        } else if (capped != 0) {
                            capped--;
                        }
                    } else {
                        v = int8_t(std::clamp<int>(v, 0x23, 0x41));
                    }
                    if (sh != 0) g.development_balance += v - old;
                    w = (w & ~(0x7Fu << sh)) + (uint32_t(int32_t(v)) << sh);
                }
                p.w35c = w;
                // the seven skills follow the team trend (1 in 9 each)
                uint32_t k = p.w368;
                for (int sh = 0; sh < 13; sh += 2) {
                    if (game.range(0, 8) != 0) continue;
                    int v = int((p.w368 >> sh) & 3) + trend;
                    v = std::clamp(v, 0, 3);
                    k = (k & ~(3u << sh)) + (uint32_t(v) << sh);
                }
                p.w368 = k;
            }
            if (p.w364 & 0x1000)  // played in this match
                modern::player_statistic(g, type, player, manager, int(s - range.first), 0, 0x5a);
            if (p.w35c >> 27) continue;  // injured players are handled elsewhere

            if (cls != 0xFF) {
                const int duration = (p.w364 >> 16) & 0xF;
                if (duration != 0 && !(p.w364 & 0x100000)) {
                    // injury (not a ban) heals by one match; in cups the card counter restarts
                    if (cls != 0) {
                        if (p.w368 >> 24 & 1) p.w368 &= 0xF8FFFFFFu;
                        else p.w368 = (p.w368 & 0xF8FC3FFFu) | 0x4000u;
                    }
                    p.w364 = (p.w364 & 0xFFE0FFFFu) | uint32_t(duration - 1) << 16;
                } else if (cls == 0) {
                    // league: every fifth yellow card (booked in this match) bans for one match
                    int yellow = (p.w368 >> 14) & 0xF;
                    if ((g.squad_slots.at(s) & 0x1000) && yellow > 4 && yellow % 5 == 0) p.w364 |= 0x10000u;
                }
            }
            g.squad_slots.at(s) &= 0xCFFF;  // clear the booked/substituted slot flags
            if (cls == 0) continue;

            // Cups: move the card counters of this competition into the player's card word
            // and load those of the next competition (the w368 card bits are shared).
            if (p.w368 >> 24 & 1) p.w368 |= 0xC000u;
            uint16_t& cards = g.player_cards.at(player);
            switch (cls) {
            case 1: p.w364 = (p.w364 & 0xFFFFFCFFu) | ((p.w368 >> 14) & 3) << 8; break;
            case 2: p.w364 = (p.w364 & 0xFFFFF3FFu) | ((p.w368 >> 14) & 3) << 10; break;
            case 3: cards = uint16_t((cards & 0x3FFF) | (p.w368 & 0xC000)); break;
            case 4: cards = uint16_t((cards & 0xCFFF) | ((p.w368 >> 14) & 3) << 12); break;
            default: break;
            }
            p.w368 = (p.w368 & 0xF8FC3FFFu) | uint32_t((cards >> 5) & 0xF) << 14 | uint32_t((cards >> 9) & 7) << 24;
            if (p.w35c >> 27 == 0) p.w364 = (p.w364 & 0xFFE0FFFFu) | ((p.w364 >> 13) & 7) << 16;
        }
    }
}

void process_group(ModernState& g, const MatchGroup& grp, GameRandom& game, WatcomRandom& rand, MinuteReport& report,
                   int group_index, const SportschauFixtureHook& sportschau,
                   const std::vector<ShowEvent>* show_events, const SportschauReplayHooks* replay) {
    auto owned = grp;
    modern::process_group(g, owned, game, rand, report, group_index, sportschau, show_events, replay, nullptr);
}

void process_group(ModernState& g, MatchGroup& grp, GameRandom& game, WatcomRandom& rand, MinuteReport& report,
                   int group_index, const SportschauFixtureHook& sportschau,
                   const std::vector<ShowEvent>* show_events, const SportschauReplayHooks* replay,
                   const SportschauRatingTables* ratings) {
    const int type = grp.desc.at(0);
    auto store_awards = [&] {
        if (group_index < 0 || group_index >= 6) return;
        const uint32_t base = ::bmh::layout::MatchdayGroupDescriptors + uint32_t(group_index) * 0xd0;
        for (size_t i = 0; i < 11; ++i) {
            if (g.memory.has(base + 0x70 + uint32_t(i)))
                g.memory.set8(base + 0x70 + uint32_t(i), grp.desc.at(0x70 + i));
            if (g.memory.has(base + 0x9c + uint32_t(i * 2 + 1)))
                g.memory.set16(base + 0x9c + uint32_t(i * 2), le16(grp.desc, 0x9c + i * 2));
        }
    };
    if (ratings) {
        // FinishEvent/BuildSideRecords reset eleven score bytes, retaining old slot ids.
        std::fill_n(grp.desc.begin() + 0x70, 11, uint8_t(0));
        store_awards();
    }
    // The original walks the fixtures with a pointer (puVar2) that only moves on in the
    // morale step of a played match. After a match that was not played, the club records and
    // the morale of every following match are therefore booked on the teams of the match
    // before it (the player development and the manager records use the match index).
    size_t cursor = 0;
    for (int m = 0; m < grp.matches(); m++) {
        // The original keeps local_70 (kickoff_void_mask) separately from the descriptor:
        // desc+0xb8 also gains ended-match bits as the minute loop runs. Its fixture pointer
        // advances only for played matches, so a void fixture must not advance `cursor`.
        if (grp.kickoff_void_mask & (1u << m)) continue;
        // A postponed league match can also retain its 0xCD result byte.
        if (!grp.results.empty() && (grp.results.at(size_t(m)) & 0xCC) == 0xCC) continue;
        int home = 0, away = 0;
        if (!modern::match_score(grp, m, home, away)) {
            report.fail("matchday processing needs the score");
            return;
        }
        const int ht = grp.fixtures.at(cursor), at = grp.fixtures.at(cursor + 1);
        cursor += 2;
        if (type == 0 || type == 10) modern::update_club_table(g, ht, at, home, away);
        // MatchdayIntro sits between table updates and morale. Core owns its enabled
        // replay scene draws; the old fixture callback remains a pure observer.
        if ((sportschau || replay || ratings) && group_index >= 0) {
            const auto view = sportschau_fixture_view(g, grp, group_index, m,
                show_events ? *show_events : std::vector<ShowEvent>{});
            if (replay) present_sportschau_replay(g, grp, view, game, rand, *replay);
            if (ratings) { rate_sportschau_players(g, grp, view, rand, *ratings); store_awards(); }
            if (sportschau) sportschau(g, view);
        }
        modern::update_morale(g, ht, home, away, 0, rand);
        modern::update_morale(g, at, away, home, 1, rand);
        modern::manager_match_record(g, grp, m, home, away, report);
        if (report.unsupported) return;
        modern::develop_players(g, grp, m, game, rand);
    }
}

void stir_random(GameRandom& game, WatcomRandom& rand) {
    int n = int16_t(rand.range(100, 300));
    for (int i = 0; i < n; i++) game.range(0, 1);
}

bool league_screen_shown(const std::vector<uint8_t>& buttons, int param_1, int param_2, int league) {
    if (param_2 == 0 || param_2 >= 200) return true;  // called from the desk: always shown
    int kind = param_1 - 1;
    if (param_1 == 2 && (param_2 == 90 || param_2 == 120)) kind = 2;
    int row = league;
    if (league > 4) row = std::min(league + 12, 19);
    if (param_2 == 45 && kind == 0) return false;
    return buttons.at(size_t(kind + row * 9)) == 0;
}


int table_position(const ModernState& g, int team) {
    if (team >= league_clubs()) return 2;
    int league = league_of(team);
    int rounds = league < int(g.league_rounds.size()) ? g.league_rounds[league] : 0;
    if (rounds == 0) return -1;
    unsigned u = unsigned(rounds - 1);
    int word = int16_t(rd16(g.position_history, (size_t(u / 3) + size_t(team) * 13) * 2));
    return (word >> ((u % 3) * 5)) & 0x1F;
}

void rate_manager(ModernState& g, int manager, int rating, int competition) {
    uint8_t* hist = g.manager_ratings.data() + size_t(competition) * 10 + size_t(manager) * 0x3c;
    std::copy_backward(hist, hist + 9, hist + 10);  // Int16T: shift the last 9 up
    hist[0] = uint8_t(rating);
    int sum = 0;
    for (int s = 0; s < 10; s++) sum = int16_t(sum + ((100 - 10 * s) * hist[s]) / 100);
    wr16(g.manager_records, size_t(manager) * kManagerRecord + 0x8aa + size_t(competition) * 2, uint16_t(sum));
}

void manager_match_record(ModernState& g, const MatchGroup& grp, int m, int home_goals, int away_goals, MinuteReport& report) {
    const int type = grp.desc.at(0);
    for (size_t k = 0; k < g.manager_teams.size(); k++) {
        const size_t rec = k * kManagerRecord;
        int side = -1, opponent = 0;
        for (int s = 0; s < 2; s++)
            if (g.manager_teams[k] == grp.fixtures.at(m * 2 + s)) {
                side = s;
                opponent = grp.fixtures.at(m * 2 + 1 - s);
                break;
            }
        if (side < 0) continue;
        // opponent id as the original keeps it: its low byte (0xFE for ids >= 256)
        const int opp = opponent < 0x100 ? opponent : 0xFE;
        // last results (home | away << 4) and fixtures, newest first
        std::copy_backward(g.manager_records.begin() + rec + 0x212, g.manager_records.begin() + rec + 0x217,
                           g.manager_records.begin() + rec + 0x218);
        std::copy_backward(g.manager_records.begin() + rec + 0x292, g.manager_records.begin() + rec + 0x2a6,
                           g.manager_records.begin() + rec + 0x2aa);
        g.manager_records.at(rec + 0x212) = uint8_t(home_goals + (away_goals << 4));
        for (int i = 0; i < 2; i++) wr16(g.manager_records, rec + 0x292 + i * 2, uint16_t(grp.fixtures.at(m * 2 + i)));
        int own = side ? away_goals : home_goals, against = side ? home_goals : away_goals;
        int rating = side ? 110 : 90;  // an away game counts more
        if (uint8_t(own) > uint8_t(against)) {
            // league win bonus from the sponsor (record +0x173 thousands, less for
            // higher +0x210 levels)
            int bonus = g.manager_records.at(rec + 0x173);
            if (type == 0 && bonus != 0) {
                int level = g.manager_records.at(rec + 0x210);
                if (level > 2) level--;
                int32_t amount = (4 - level) * bonus * 1000;
                if (g.level != 0) {
                    wr32(g.manager_records, rec + 0x314, rd32(g.manager_records, rec + 0x314) + uint32_t(amount));
                    wr32(g.manager_records, rec + 0x874, rd32(g.manager_records, rec + 0x874) + uint32_t(amount));
                }
            }
            rating += 20;
        }
        if (uint8_t(own) < uint8_t(against)) rating -= 20;
        // MatchdayRuntimeField (u16 every 4 bytes): competitions that rate the manager. A league match
        // counts the table place; a cup match its weight plus twice the rounds played in the
        // stage and in every earlier one (link +0x15). All go into the league history (0).
        static const uint16_t kRatingWeight[14] = {1, 12, 16, 20, 18, 0, 0, 0, 0, 14, 1, 0, 12, 10};
        const uint16_t weight = type < 14 ? kRatingWeight[type] : 0;
        if (weight != 0) {
            if (type != 0) {
                int add = weight;
                uint32_t e = le32(grp.desc, 0xcc);
                for (int guard = 0; guard < 64; guard++) {
                    add += g.memory.u8(e + 0x1d) * 2;
                    const int8_t previous = int8_t(g.memory.u8(e + 0x15));
                    if (previous == -1) break;
                    e = modern::cup_entry_address(previous);
                }
                rating += int16_t(add);
            } else {
                int league = league_of(g.manager_teams[k]);
                int size = league < 9 ? kLeagueSizes[league] : 0;
                rating += (size - modern::table_position(g, g.manager_teams[k])) * 4;
            }
            rating = std::clamp(int16_t(rating), int16_t(0), int16_t(200));
            modern::rate_manager(g, int(k), rating, 0);
        }
        if (opp < first_foreign_cup()) {
            size_t at = k * 0x690 + size_t(opp) * 10 + (side == 1 ? 5 : 0);
            std::copy_backward(g.manager_results.begin() + at, g.manager_results.begin() + at + 4,
                               g.manager_results.begin() + at + 5);
            g.manager_results.at(at) = uint8_t(own + (against << 4));
        }
    }
}


// ManagerOf(manager, 0, value): the manager's position of this season.
static void store_season_position(ModernState& g, int manager, int value) {
    int season = std::min(g.season, 0x31);
    g.manager_records.at(size_t(manager) * kManagerRecord + 0xda + size_t(season) * 3) = uint8_t(value);
}

// StoreStrengthHistory: strength and position history of a human manager's club (38 entries).
static void store_strength_history(ModernState& g, int manager) {
    const size_t rec = size_t(manager) * kManagerRecord;
    const int team = g.manager_teams.at(manager);
    int n = g.manager_records.at(rec + 0x284);
    if (n < 0x26) {
        g.manager_records.at(rec + 0x284) = uint8_t(n + 1);
    } else {
        std::copy(g.manager_records.begin() + rec + 0x223, g.manager_records.begin() + rec + 0x248,
                  g.manager_records.begin() + rec + 0x222);
        std::copy(g.manager_records.begin() + rec + 0x249, g.manager_records.begin() + rec + 0x26e,
                  g.manager_records.begin() + rec + 0x248);
        n = 0x25;
    }
    g.manager_records.at(rec + 0x222 + n) = uint8_t(team_average(g, team, 0xFF).value);
    int pos = modern::table_position(g, team);
    if (pos == -1) pos = (kLeagueSizes[g.manager_records.at(rec + 0x210)] - 1) >> 1;
    g.manager_records.at(rec + 0x248 + n) = uint8_t(pos);
}

void advance_league_rounds(ModernState& g, uint32_t competitions, WatcomRandom& rand, MinuteReport& report,
                           const CompetitionWinnerHook& winner) {
    // CalendarFirstLeague / CalendarLeagueCounts: first league and league count of competition bits 0-7
    const uint8_t* kFirst = kGroupLeague;  // (schedule.hpp: the groups of the loaded game)
    const uint8_t* kCount = kGroupLeagues;
    for (int bit = 0; bit < 19; bit++) {
        if (!(competitions >> bit & 1)) continue;
        if (bit >= 5) {
            report.fail("cup round bookkeeping (AdvanceCup)");
            return;
        }
        const int end = bit < 4 ? bit + 1 : 8;
        for (int c = bit; c < end; c++) {
            for (int league = kFirst[c]; league < kFirst[c] + kCount[c]; league++) {
                if (league == 0) {
                    for (size_t m = 0; m < g.manager_teams.size(); m++) {
                        modern::advance_youth_teams(g, int(m), rand);
                        const size_t rec = m * kManagerRecord;
                        if (g.league_rounds.at(0) == 0x10) wr32(g.manager_records, rec + 0x320, rd32(g.manager_records, rec + 0x308));
                        uint8_t& d = g.manager_records.at(rec + 0x220);  // point deduction countdown
                        if (d >> 5 == 1) d = (d & 0x1F) < 2 ? 0 : uint8_t((d & 0xE0) | ((d & 0x1F) - 1));
                    }
                }
                const int round = g.league_rounds.at(league);
                g.league_rounds.at(league) = uint8_t(round + 1);
                LeagueTable t = compute_league_table(g, league, 0, league_matchdays(league), 7, kThreePoints[league] ? 1 : 0);
                const int first = league_first_team(league);
                for (int i = 0; i < t.size; i++) {
                    const int team = first + i;
                    const int rank = t.rank(i);
                    size_t at = (size_t(round / 3) + size_t(team) * 13) * 2;
                    wr16(g.position_history, at, uint16_t(rd16(g.position_history, at) | rank << ((round % 3) * 5)));
                    const int manager = g.manager_of(team);
                    if (manager >= 0) {
                        int base = first;
                        if (league_first_team(3) <= base) base -= 18;
                        store_season_position(g, manager, base + rank + 1);
                        store_strength_history(g, manager);
                    }
                }
                // OnceSecondDivisionLeagueHasPlayedAll, once the second division (league 2) has played all its rounds
                // (in the last competition of the loop, after each of its leagues): the
                // clubs placed 1-2 in leagues 2 and 3 (TablePosition, positions counted from 0)
                // go into the team list of the promotion play-off (Teams), its results
                // (MatchdayGroupWorkspace) are cleared and competition MatchdayRuntimeField2 (0xd) starts
                if (c + 1 == end && g.league_rounds.at(2) == league_matchdays(2)) {
                    uint32_t n = 0;
                    for (int l = 2; l < 4; l++)
                        for (int team = league_first_team(l); team < league_first_team(l) + kLeagueSizes[l]; team++) {
                            const uint8_t pos = uint8_t(modern::table_position(g, team));
                            if (pos != 0 && pos < 3) g.memory.set16(::bmh::layout::Teams + n++ * 2, uint16_t(team));
                        }
                    for (uint32_t i = 0; i < 0x20; i++) g.memory.set8(::bmh::layout::MatchdayGroupWorkspace + i, 0xcc);
                    for (uint32_t i = 0; i < 8; i++) {  // AddCompetition(0xd)
                        if (int8_t(g.memory.u8(::bmh::layout::ActiveSelectors + i)) == 0xd) break;
                        if (g.memory.u8(::bmh::layout::ActiveSelectors + i) == 0x14) {
                            g.memory.set8(::bmh::layout::ActiveSelectors + i, 0xd);
                            break;
                        }
                    }
                }
                // the first division is over: its leader (OnTableJust(0, 0) on the table just
                // computed) is champion - the winner screen (CompetitionWinnerScreen(0, 0), clears
                // CareerSession_BeginFriendlyOpponentTable_ExecutableField2), the list of title holders (ListTitleHoldersAf(club, 0)), a human
                // champion's record (+0x8a4) and Cup_AdvanceCup_RuntimeField6
                if (league == 0 && g.league_rounds.at(0) == league_matchdays(0)) {
                    int champion = -1;
                    for (int i = 0; i < t.size && champion < 0; i++)
                        if (t.rank(i) == 0) champion = first + i;
                    if (champion < 0) champion = 0;  // (OnTableJust returns 0 when none is found)
                    if (g.memory.u8(::bmh::layout::Cup_AdvanceCup_RuntimeField) == 0 && winner)
                        winner(g, {0, uint16_t(champion)});
                    if (g.memory.u8(::bmh::layout::Cup_AdvanceCup_RuntimeField) == 0 && g.memory.has(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2)) g.memory.set8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2, 0);
                    g.memory.set16(::bmh::layout::Titles + uint32_t(std::min<int>(g.season, 0x31)) * 2, uint16_t(champion));
                    const int manager = g.manager_of(champion);
                    if (manager >= 0) {
                        const size_t at = size_t(manager) * kManagerRecord + 0x8a4;
                        wr16(g.manager_records, at, uint16_t(rd16(g.manager_records, at) + 1));
                    }
                    g.memory.set16(::bmh::layout::Cup_AdvanceCup_RuntimeField6, uint16_t(champion));
                }
            }
        }
    }
}

}  // namespace bmh::modern
