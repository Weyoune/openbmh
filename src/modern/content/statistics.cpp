// SPDX-License-Identifier: GPL-3.0-only
#include "modern/content/statistics.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <variant>
#include <vector>

#include "formats/executable_layout.hpp"
#include "game/messages.hpp"
#include "modern/game/almanac.hpp"

namespace bmh::modern {

namespace {

using J = cli::Json;
using A = J::Array;
using O = J::Object;

J error(const char* code) { return O{{"ok", false}, {"error", code}}; }

std::string season_label(int start_year) {
    const int next = (start_year + 1) % 100;
    return std::to_string(start_year) + "/" + (next < 10 ? "0" : "") + std::to_string(next);
}

std::string iso_date(uint32_t counter, int display_start_year) {
    const AlmanacDate d = almanac_date(counter, display_start_year);
    char out[16];
    std::snprintf(out, sizeof out, "%04d-%02d-%02d", d.year, d.month, d.day);
    return out;
}

// Every season of the career: the finished ones decoded, then the running one.
std::vector<AlmanacSeason> seasons_of(const ModernState& g) {
    std::vector<AlmanacSeason> out;
    for (const auto& [name, bytes] : g.almanac.finished) out.push_back(decode_almanac_season(bytes));
    if (g.almanac.current.season >= 0) out.push_back(g.almanac.current);
    return out;
}

// A competition as the statistics name it: leagues by the world's league id, cups by
// their cup id, the indoor Masters apart from the single tournaments.
struct Competition {
    std::string id, name, type;
    int order = 0;  // leagues first (by index), then cups (by cup id)
};

std::string cup_name(const ModernState& g, int cup) {
    // The indoor tournament's text names the venue of the next tournament (with a
    // placeholder); the statistics list all tournaments under one name.
    if (cup == 0xb) return "Hallenturnier";
    std::string name;
    try {
        if (g.texts) name = almanac_utf8(message_text(g, ::bmh::layout::DeskState_FormatError_ExecutableField3 + uint32_t(cup + 1) * 4));
    } catch (const std::exception&) {
        name.clear();
    }
    return name.empty() ? "Pokal " + std::to_string(cup) : name;
}

Competition league_competition(const AlmanacSeason& s, int league) {
    Competition c;
    c.type = "league";
    c.order = league;
    for (const auto& l : s.leagues)
        if (l.index == league) c.id = l.id, c.name = l.name;
    if (c.id.empty()) c.id = "league:" + std::to_string(league);
    if (c.name.empty()) c.name = "Liga " + std::to_string(league + 1);
    return c;
}

Competition cup_competition(const ModernState& g, int cup, bool masters) {
    Competition c;
    c.type = "cup";
    c.order = 100 + cup * 2 + (masters ? 1 : 0);
    c.id = "cup:" + std::to_string(cup) + (masters ? ":masters" : "");
    c.name = masters ? "Hallen-Masters" : cup_name(g, cup);
    return c;
}

Competition competition_of(const ModernState& g, const AlmanacSeason& s, const AlmanacMatch& m) {
    return m.type == 0 ? league_competition(s, m.league) : cup_competition(g, m.type - 1, false);
}

Competition competition_of(const ModernState& g, const AlmanacSeason& s, const AlmanacWinner& w) {
    return w.type == 0 ? league_competition(s, w.league) : cup_competition(g, w.type - 1, w.detail == 1);
}

J club_json(const AlmanacSeason& s, uint16_t club) {
    if (club >= s.clubs.size()) return nullptr;
    return O{{"id", s.clubs[club].id}, {"name", s.clubs[club].name}};
}

std::string player_name(const AlmanacPlayer& p) { return p.first.empty() ? p.last : p.first + " " + p.last; }

const J* field(const J& request, const char* key) {
    const auto* o = request.object();
    if (!o) return nullptr;
    const auto it = o->find(key);
    return it == o->end() ? nullptr : &it->second;
}

J competitions(const ModernState& g, const std::vector<AlmanacSeason>& seasons) {
    std::map<std::string, std::pair<Competition, std::vector<std::string>>> found;  // id -> seasons
    auto note = [&](const Competition& c, const std::string& season) {
        auto& entry = found[c.id];
        entry.first = c;
        if (entry.second.empty() || entry.second.back() != season) entry.second.push_back(season);
    };
    for (const auto& s : seasons) {
        const std::string label = season_label(s.start_year);
        for (const auto& m : s.matches) note(competition_of(g, s, m), label);
        for (const auto& w : s.winners) note(competition_of(g, s, w), label);
    }
    std::vector<std::pair<Competition, std::vector<std::string>>> list;
    for (auto& [id, entry] : found) list.push_back(std::move(entry));
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first.order < b.first.order; });
    A out;
    for (const auto& [c, labels] : list) {
        A season_list;
        for (const auto& l : labels) season_list.push_back(l);
        out.push_back(O{{"id", c.id}, {"name", c.name}, {"type", c.type}, {"seasons", std::move(season_list)}});
    }
    return O{{"ok", true}, {"competitions", std::move(out)}};
}

J winners(const ModernState& g, const std::vector<AlmanacSeason>& seasons, const std::string& only) {
    std::map<std::string, std::pair<Competition, A>> found;
    for (const auto& s : seasons) {
        std::map<std::string, int> edition;  // several indoor tournaments in a season
        for (const auto& w : s.winners) {
            const Competition c = competition_of(g, s, w);
            if (!only.empty() && c.id != only) continue;
            auto& entry = found[c.id];
            entry.first = c;
            entry.second.push_back(O{{"season", season_label(s.start_year)}, {"year", s.start_year},
                                     {"edition", ++edition[c.id]}, {"date", iso_date(w.date, s.display_start_year)},
                                     {"winner", club_json(s, w.winner)},
                                     {"runner_up", w.runner_up == 0xffff ? J(nullptr) : club_json(s, w.runner_up)}});
        }
    }
    std::vector<std::pair<Competition, A>> list;
    for (auto& [id, entry] : found) list.push_back(std::move(entry));
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first.order < b.first.order; });
    A out;
    for (auto& [c, rows] : list)
        out.push_back(O{{"id", c.id}, {"name", c.name}, {"type", c.type}, {"winners", std::move(rows)}});
    return O{{"ok", true}, {"competitions", std::move(out)}};
}

struct PlayerRow {
    std::string id, name, club_id, club_name;
    int appearances = 0, goals = 0, assists = 0, yellow = 0, second_yellow = 0, red = 0;
    uint64_t last = 0;  // order of the player's last match (his club is the club of that match)
};

J player_stats(const ModernState& g, const std::vector<AlmanacSeason>& seasons, const std::string& competition,
               const std::string& season, const std::string& sort, size_t limit) {
    std::map<std::string, PlayerRow> rows;
    uint64_t order = 0;
    for (const auto& s : seasons) {
        if (!season.empty() && season_label(s.start_year) != season) continue;
        for (const auto& m : s.matches) {
            ++order;
            if (competition_of(g, s, m).id != competition) continue;
            auto row = [&](uint16_t player, int side) -> PlayerRow* {
                if (player >= s.players.size()) return nullptr;
                const AlmanacPlayer& p = s.players[player];
                PlayerRow& r = rows[p.id];
                r.id = p.id, r.name = player_name(p);
                if (order >= r.last) {
                    const uint16_t club = side ? m.away : m.home;
                    r.club_id = s.clubs.at(club).id, r.club_name = s.clubs.at(club).name, r.last = order;
                }
                return &r;
            };
            for (int side = 0; side < 2; ++side) {
                const auto& lineup = m.lineup[size_t(side)];
                for (size_t k = 0; k < std::min<size_t>(11, lineup.size()); ++k)
                    if (PlayerRow* r = row(lineup[k], side)) ++r->appearances;
            }
            for (const auto& e : m.events) {
                switch (e.kind) {
                    case AlmanacEventKind::Goal:
                        if (PlayerRow* r = row(e.player, e.side)) ++r->goals;
                        if (PlayerRow* r = row(e.other, e.side)) ++r->assists;
                        break;
                    case AlmanacEventKind::YellowCard:
                        if (PlayerRow* r = row(e.player, e.side)) ++r->yellow;
                        break;
                    case AlmanacEventKind::SecondYellow:
                        if (PlayerRow* r = row(e.player, e.side)) ++r->second_yellow;
                        break;
                    case AlmanacEventKind::RedCard:
                        if (PlayerRow* r = row(e.player, e.side)) ++r->red;
                        break;
                    case AlmanacEventKind::Substitution:
                        if (PlayerRow* r = row(e.player, e.side)) ++r->appearances;
                        break;
                    default:
                        break;
                }
            }
        }
    }
    auto key = [&](const PlayerRow& r) {
        if (sort == "assists") return r.assists;
        if (sort == "yellow") return r.yellow;
        if (sort == "red") return r.red + r.second_yellow;
        return r.goals;
    };
    std::vector<PlayerRow> list;
    for (auto& [id, r] : rows)
        if (key(r) > 0) list.push_back(std::move(r));
    std::sort(list.begin(), list.end(), [&](const PlayerRow& a, const PlayerRow& b) {
        if (key(a) != key(b)) return key(a) > key(b);
        if (a.appearances != b.appearances) return a.appearances < b.appearances;  // fewer games ranks first
        return a.name < b.name;
    });
    A out;
    int rank = 0, previous = -1;
    for (size_t i = 0; i < list.size() && (limit == 0 || i < limit); ++i) {
        const PlayerRow& r = list[i];
        if (key(r) != previous) rank = int(i) + 1, previous = key(r);
        out.push_back(O{{"rank", rank}, {"player", O{{"id", r.id}, {"name", r.name}}},
                        {"club", O{{"id", r.club_id}, {"name", r.club_name}}}, {"appearances", r.appearances},
                        {"goals", r.goals}, {"assists", r.assists}, {"yellow", r.yellow},
                        {"second_yellow", r.second_yellow}, {"red", r.red}});
    }
    return O{{"ok", true}, {"competition", competition}, {"season", season.empty() ? J(nullptr) : J(season)},
             {"sort", sort}, {"players", std::move(out)}};
}

}  // namespace

cli::Json query_statistics(const ModernState& g, const cli::Json& request) {
    const J* kind = field(request, "kind");
    if (!kind || !kind->string()) return error("invalid_statistics_request");
    try {
        const auto seasons = seasons_of(g);
        if (*kind->string() == "competitions") return competitions(g, seasons);
        if (*kind->string() == "winners") {
            const J* only = field(request, "competition");
            if (only && !only->string()) return error("invalid_statistics_request");
            return winners(g, seasons, only ? *only->string() : std::string());
        }
        if (*kind->string() == "player_stats") {
            const J* competition = field(request, "competition");
            const J* season = field(request, "season");
            const J* sort = field(request, "sort");
            const J* limit = field(request, "limit");
            if (!competition || !competition->string() || (season && !season->string() && !std::holds_alternative<std::nullptr_t>(season->value)) ||
                (sort && !sort->string()) || (limit && (!limit->integer() || *limit->integer() < 0)))
                return error("invalid_statistics_request");
            const std::string order = sort ? *sort->string() : "goals";
            if (order != "goals" && order != "assists" && order != "yellow" && order != "red")
                return error("invalid_statistics_request");
            return player_stats(g, seasons, *competition->string(), season && season->string() ? *season->string() : "",
                                order, limit ? size_t(*limit->integer()) : 50);
        }
        return error("invalid_statistics_request");
    } catch (const std::exception&) {
        return error("statistics_unavailable");
    }
}

}  // namespace bmh::modern
