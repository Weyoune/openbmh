// SPDX-License-Identifier: GPL-3.0-only
#include "modern/content/projections.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <variant>
#include <vector>

#include "game/schedule.hpp"
#include "game/table.hpp"
#include "modern/content/statistics.hpp"
#include "modern/game/almanac.hpp"
#include "modern/game/matchday.hpp"

namespace bmh::modern {

namespace {

using J = cli::Json;
using A = J::Array;
using O = J::Object;

constexpr size_t kRecord = 0xb0c;
constexpr size_t kResultsPerLeague = 0x17c;  // 38 rounds x 10 matches

J error(const char* code) { return O{{"ok", false}, {"error", code}}; }

const J* field(const J& request, const char* key) {
    const auto* o = request.object();
    if (!o) return nullptr;
    const auto it = o->find(key);
    return it == o->end() ? nullptr : &it->second;
}

const std::string& entry(const std::vector<std::string>& v, size_t i) {
    static const std::string none;
    return i < v.size() ? v[i] : none;
}

// The forms shared by every answer (the parts of a later complete JSON state):
// club {id, name, short}, player {id, name}, league {index, id, name}.
J club_json(const ModernState& g, int team) {
    std::string id = entry(g.world_club_id, size_t(team)), name = entry(g.world_club_name, size_t(team)),
                short_name = entry(g.world_club_short, size_t(team));
    if (id.empty()) id = "team:" + std::to_string(team);
    if (name.empty()) name = almanac_utf8(g.club_name(team));
    if (short_name.empty()) short_name = name;
    return O{{"id", id}, {"name", name}, {"short", short_name}};
}

std::string player_name(const ModernState& g, int player) {
    const std::string& first = entry(g.player_first, size_t(player));
    std::string last = entry(g.player_last, size_t(player));
    if (last.empty()) last = almanac_utf8(g.player_name(player));
    return first.empty() ? last : first + " " + last;
}

J league_json(const ModernState& g, int league) {
    std::string id = entry(g.world_league_id, size_t(league)), name = entry(g.world_league_name, size_t(league));
    if (id.empty()) id = "league:" + std::to_string(league);
    if (name.empty()) name = "Liga " + std::to_string(league + 1);
    return O{{"index", league}, {"id", id}, {"name", name}};
}

// A league by index (number) or by the world's league id (string); -1 if unknown.
int league_of_request(const ModernState& g, const J* value) {
    if (!value) return -1;
    if (value->integer()) {
        const int64_t l = *value->integer();
        return l >= 0 && l < kLeagueCount ? int(l) : -1;
    }
    if (value->string()) {
        for (int l = 0; l < kLeagueCount; ++l)
            if (std::get<O>(league_json(g, l).value).at("id").string() &&
                *std::get<O>(league_json(g, l).value).at("id").string() == *value->string())
                return l;
    }
    return -1;
}

std::string iso_date(const ModernState& g) {
    const AlmanacDate d = almanac_date(g.date, g.display_start_year);
    char out[16];
    std::snprintf(out, sizeof out, "%04d-%02d-%02d", d.year, d.month, d.day);
    return out;
}

std::string season_label(const ModernState& g) {
    const int start = g.display_start_year + g.season, next = (start + 1) % 100;
    return std::to_string(start) + "/" + (next < 10 ? "0" : "") + std::to_string(next);
}

int32_t money_of(const ModernState& g, int manager) {
    const size_t at = size_t(manager) * kRecord + 0x314;
    if (at + 3 >= g.manager_records.size()) return 0;
    return int32_t(uint32_t(g.manager_records[at]) | uint32_t(g.manager_records[at + 1]) << 8 |
                   uint32_t(g.manager_records[at + 2]) << 16 | uint32_t(g.manager_records[at + 3]) << 24);
}

std::string manager_name(const ModernState& g, int manager) {
    std::string name;
    const size_t at = size_t(manager) * kRecord + 0xa8;
    for (size_t i = 0; i < 30 && at + i < g.manager_records.size() && g.manager_records[at + i]; ++i)
        name.push_back(char(g.manager_records[at + i]));
    return almanac_utf8(name);
}

J status(const ModernState& g, int manager) {
    if (manager < 0 || size_t(manager) >= g.manager_teams.size()) return error("invalid_manager");
    const int team = g.manager_teams[size_t(manager)];
    O out{{"ok", true}, {"manager", O{{"index", manager}, {"name", manager_name(g, manager)}}},
          {"date", iso_date(g)}, {"season", season_label(g)}, {"money", int64_t(money_of(g, manager))}};
    if (team >= 0 && team < league_clubs()) {
        const int league = team_league(team);
        out["club"] = club_json(g, team);
        out["league"] = league_json(g, league);
        out["place"] = int(table_position(g, team)) + 1;
    } else {
        out["club"] = nullptr;  // e.g. at the national coach's desk
    }
    return out;
}

J leagues(const ModernState& g) {
    A out;
    for (int l = 0; l < kLeagueCount; ++l) {
        J league = league_json(g, l);
        auto& o = std::get<O>(league.value);
        o["clubs"] = int(kLeagueSizes[l]);
        o["matchdays"] = league_matchdays(l);
        o["played"] = l < int(g.league_rounds.size()) ? int(g.league_rounds[size_t(l)]) : 0;
        out.push_back(std::move(league));
    }
    return O{{"ok", true}, {"leagues", std::move(out)}};
}

J table(const ModernState& g, int league) {
    const int played = league < int(g.league_rounds.size()) ? int(g.league_rounds[size_t(league)]) : 0;
    const LeagueTable t = compute_league_table(g, league, 0, played, 7, 1);
    const int first = league_first_team(league);
    std::vector<int> order(size_t(t.size));
    for (int i = 0; i < t.size; ++i) order[size_t(i)] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return t.rank(a) < t.rank(b); });
    A rows;
    for (int i : order) {
        const int games = t.games(i), won = t.wins[size_t(i)], lost = t.defeats[size_t(i)];
        rows.push_back(O{{"rank", t.rank(i) + 1}, {"club", club_json(g, first + i)}, {"played", games}, {"won", won},
                         {"drawn", std::max(0, games - won - lost)}, {"lost", lost},
                         {"goals_for", int(t.goals_for[size_t(i)])}, {"goals_against", int(t.goals_against[size_t(i)])},
                         {"points", int(t.points[size_t(i)])}});
    }
    return O{{"ok", true}, {"league", league_json(g, league)}, {"played", played}, {"rows", std::move(rows)}};
}

J fixtures(const ModernState& g, int league, int matchday) {
    const int matchdays = league_matchdays(league);
    if (matchday < 1 || matchday > matchdays) return error("invalid_matchday");
    A out;
    const auto fx = league_fixtures(league, matchday);
    for (size_t m = 0; m < fx.size(); ++m) {
        const size_t at = size_t(league) * kResultsPerLeague + size_t(matchday - 1) * 10 + m;
        const uint8_t r = at < g.results.size() ? g.results[at] : kNotPlayed;
        O match{{"home", club_json(g, fx[m].home)}, {"away", club_json(g, fx[m].away)}};
        if (r == kNotPlayed) {
            match["result"] = nullptr, match["status"] = "open";
        } else if (r == 0xcd) {
            match["result"] = nullptr, match["status"] = "postponed";
        } else {
            match["result"] = A{J(int(r >> 4)), J(int(r & 0xf))}, match["status"] = "played";
        }
        out.push_back(std::move(match));
    }
    return O{{"ok", true}, {"league", league_json(g, league)}, {"matchday", matchday}, {"matches", std::move(out)}};
}

J squad(const ModernState& g, int team) {
    if (team < 0 || team >= league_clubs()) return error("invalid_club");
    static constexpr const char* kPositions[4] = {"gk", "def", "mid", "fwd"};
    static constexpr const char* kSkills[7] = {"teamplay", "speed", "shooting", "passing", "tackling", "free_kick", "corner"};
    A players;
    const SquadRange r = squad_range(team);
    for (size_t s = r.first; s < r.first + r.count; ++s) {
        const uint16_t e = g.squad_slots.at(s);
        const int id = e & 0x0fff;
        if (id == 0x0fff) continue;
        const Player& p = g.players.at(size_t(id));
        O skills;
        for (int k = 0; k < 7; ++k) skills[kSkills[k]] = int(p.w368 >> (2 * k) & 3);
        std::string pid = entry(g.world_player_id, size_t(id));
        if (pid.empty()) pid = "player:legacy-" + std::to_string(id);
        const uint32_t absence = p.w364 >> 16 & 0x1f;  // weeks out; bit 4: banned instead of injured
        players.push_back(O{{"slot", int(s - r.first)}, {"player", O{{"id", pid}, {"name", player_name(g, id)}}},
                            {"nation", entry(g.player_nation, size_t(id))}, {"position", kPositions[p.position()]},
                            {"age", p.age()}, {"technique", p.technique()}, {"condition", p.condition()},
                            {"form", p.form()}, {"strength", p.strength()}, {"skills", std::move(skills)},
                            {"lineup", (e & 0x8000) ? "start" : (e & 0x4000) ? "bench" : "none"},
                            {"injured_weeks", (absence & 0x10) ? 0 : int(absence)},
                            {"banned_weeks", (absence & 0x10) ? int(absence & 0xf) : 0}});
    }
    return O{{"ok", true}, {"club", club_json(g, team)}, {"players", std::move(players)}};
}

}  // namespace

cli::Json query_projection(const ModernState& g, int manager, const cli::Json& request) {
    const J* kind = field(request, "kind");
    if (!kind || !kind->string()) return error("invalid_request");
    const std::string& k = *kind->string();
    try {
        if (k == "status") return status(g, manager);
        if (k == "leagues") return leagues(g);
        if (k == "table" || k == "fixtures") {
            const int league = league_of_request(g, field(request, "league"));
            if (league < 0) return error("invalid_league");
            if (k == "table") return table(g, league);
            const J* md = field(request, "matchday");
            if (md && !md->integer()) return error("invalid_request");
            const int played = league < int(g.league_rounds.size()) ? int(g.league_rounds[size_t(league)]) : 0;
            // without a matchday: the next one to play (the last one after the season)
            return fixtures(g, league, md ? int(*md->integer()) : std::min(played + 1, league_matchdays(league)));
        }
        if (k == "squad") {
            const J* club = field(request, "club");
            int team = manager >= 0 && size_t(manager) < g.manager_teams.size() ? g.manager_teams[size_t(manager)] : -1;
            if (club) {
                team = -1;
                if (club->integer()) team = int(*club->integer());
                else if (club->string())
                    for (size_t t = 0; t < g.world_club_id.size(); ++t)
                        if (g.world_club_id[t] == *club->string()) team = int(t);
            }
            return squad(g, team);
        }
        if (k == "competitions" || k == "winners" || k == "player_stats") return query_statistics(g, request);
        return error("invalid_request");
    } catch (const std::exception&) {
        return error("unavailable");
    }
}

}  // namespace bmh::modern
