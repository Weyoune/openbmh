// SPDX-License-Identifier: GPL-3.0-only
#include "modern/game/almanac.hpp"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <string_view>

#include "formats/binary.hpp"
#include "game/match_engine.hpp"
#include "game/presentation.hpp"
#include "game/schedule.hpp"
#include "game/table.hpp"
#include "modern/game/cup.hpp"
#include "modern/game/matchday.hpp"
#include "modern/game/modern_state.hpp"

namespace bmh::modern {

namespace {

constexpr size_t kManagerRecord = 0xB0C;
// Version 2 adds the final tables and the winners; version 1 seasons are still read.
constexpr uint32_t kVersion = 2;

// Original single-byte text (ISO 646-DE) to UTF-8, as the interfaces show it.
std::string utf8(std::string_view original) {
    static constexpr std::string_view german[] = {"Ä", "Ö", "Ü", "ä", "ö", "ü", "ß"};
    static constexpr std::string_view codes = "[\\]{|}~";
    std::string out;
    for (char c : original) {
        const size_t i = codes.find(c);
        if (i != std::string_view::npos) out += german[i];
        else if (static_cast<unsigned char>(c) < 128) out += c;
        else out += '?';
    }
    return out;
}

const std::string& entry(const std::vector<std::string>& names, size_t i) {
    static const std::string none;
    return i < names.size() ? names[i] : none;
}

AlmanacClub club_of(const ModernState& g, int team) {
    AlmanacClub c;
    c.id = entry(g.world_club_id, size_t(team));
    if (c.id.empty()) c.id = "team:" + std::to_string(team);
    c.name = entry(g.world_club_name, size_t(team));
    if (c.name.empty()) c.name = utf8(g.club_name(team));
    return c;
}

AlmanacManager manager_of(const ModernState& g, int manager) {
    AlmanacManager m;
    m.index = manager;
    const size_t at = size_t(manager) * kManagerRecord + 0xa8;  // the manager's name (30 bytes)
    for (size_t i = 0; i < 30 && at + i < g.manager_records.size() && g.manager_records[at + i]; ++i)
        m.name.push_back(char(g.manager_records[at + i]));
    m.name = utf8(m.name);
    return m;
}

std::string referee_name(const ModernState& g, int referee) {
    const size_t at = size_t(referee) * 2;
    if (at + 1 >= g.master_database_metadata.size()) return "#" + std::to_string(referee);
    const size_t offset = size_t(g.master_database_metadata[at] | g.master_database_metadata[at + 1] << 8);
    std::string name;
    for (size_t i = offset; i < g.referee_names.size() && g.referee_names[i]; ++i) name.push_back(char(g.referee_names[i]));
    return utf8(name);
}

AlmanacSeason& current_season(ModernState& g) {
    Almanac& a = g.almanac;
    if (a.current.season != g.season) {
        if (a.current.season >= 0 && !a.current.matches.empty())
            a.finished.emplace_back("history/season-" + std::to_string(a.current.start_year) + ".bin",
                                    encode_almanac_season(a.current));
        a.current = AlmanacSeason{};
        a.current.season = g.season;
        a.current.display_start_year = g.display_start_year;
        a.current.start_year = g.display_start_year + g.season;
        a.current.referees.push_back({});  // index 0: no referee
        for (size_t l = 0; l < g.world_league_id.size(); ++l)
            a.current.leagues.push_back({int(l), g.world_league_id[l], entry(g.world_league_name, l)});
        a.current.index();
    }
    return a.current;
}

// --- binary form -----------------------------------------------------------------------

struct Writer {
    std::string out;
    void u8(uint32_t v) { out.push_back(char(v & 0xff)); }
    void u16(uint32_t v) { u8(v); u8(v >> 8); }
    void u32(uint32_t v) { u16(v); u16(v >> 16); }
    void str(const std::string& s) {
        if (s.size() > 0xffff) throw FormatError("almanac: text too long");
        u16(uint32_t(s.size()));
        out += s;
    }
};

struct Reader {
    const std::string& in;
    size_t at = 0;
    uint32_t u8() {
        if (at >= in.size()) throw FormatError("almanac: truncated season");
        return uint8_t(in[at++]);
    }
    uint32_t u16() { const uint32_t lo = u8(); return lo | u8() << 8; }
    uint32_t u32() { const uint32_t lo = u16(); return lo | u16() << 16; }
    std::string str() {
        const size_t n = u16();
        if (at + n > in.size()) throw FormatError("almanac: truncated season");
        std::string s = in.substr(at, n);
        at += n;
        return s;
    }
};

}  // namespace

uint16_t AlmanacSeason::club(const AlmanacClub& c) {
    const auto it = club_at_.find(c.id);
    if (it != club_at_.end()) return it->second;
    if (clubs.size() >= 0xffff) throw FormatError("almanac: too many clubs in a season");
    clubs.push_back(c);
    return club_at_[c.id] = uint16_t(clubs.size() - 1);
}

uint16_t AlmanacSeason::player(const AlmanacPlayer& p) {
    const auto it = player_at_.find(p.id);
    if (it != player_at_.end()) return it->second;
    if (players.size() >= kAlmanacNone) throw FormatError("almanac: too many players in a season");
    players.push_back(p);
    return player_at_[p.id] = uint16_t(players.size() - 1);
}

int8_t AlmanacSeason::manager(const AlmanacManager& m) {
    for (size_t i = 0; i < managers.size(); ++i)
        if (managers[i] == m) return int8_t(i);
    if (managers.size() >= 0x7f) throw FormatError("almanac: too many managers in a season");
    managers.push_back(m);
    return int8_t(managers.size() - 1);
}

uint8_t AlmanacSeason::referee(const std::string& name) {
    const auto it = referee_at_.find(name);
    if (it != referee_at_.end()) return it->second;
    if (referees.size() >= 0xff) throw FormatError("almanac: too many referees in a season");
    referees.push_back(name);
    return referee_at_[name] = uint8_t(referees.size() - 1);
}

void AlmanacSeason::index() {
    club_at_.clear(), player_at_.clear(), referee_at_.clear();
    for (size_t i = 0; i < clubs.size(); ++i) club_at_[clubs[i].id] = uint16_t(i);
    for (size_t i = 0; i < players.size(); ++i) player_at_[players[i].id] = uint16_t(i);
    for (size_t i = 1; i < referees.size(); ++i) referee_at_[referees[i]] = uint8_t(i);
}

AlmanacDate almanac_date(uint32_t counter, int display_start_year) {
    // As the desk counts (desk_date): day 0 is 1 July, the fourth year of the cycle is a
    // leap year (the first February the calendar reaches is in the first leap year).
    static constexpr int kDays[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    AlmanacDate d;
    int cycle = 3, years = 0;
    while (counter > uint32_t(364 + (cycle == 4))) {
        if (cycle == 4) { cycle = 0; --counter; }
        ++years, counter -= 365, ++cycle;
    }
    d.month = 7;
    auto month_days = [&] { return d.month == 2 ? 28 + (cycle == 4) : kDays[d.month]; };
    while (counter > uint32_t(month_days())) {
        counter -= uint32_t(month_days());
        if (++d.month == 13) d.month = 1, ++years;
    }
    d.day = int(counter) + 1;
    if (d.day > month_days()) {
        d.day -= month_days();
        if (++d.month == 13) d.month = 1, ++years;
    }
    d.year = display_start_year + years;
    return d;
}

std::string encode_almanac_season(const AlmanacSeason& s) {
    Writer w;
    w.out = "OBAL";
    w.u16(kVersion);
    w.u16(uint32_t(int16_t(s.season))), w.u16(uint32_t(s.start_year)), w.u16(uint32_t(s.display_start_year));
    w.u16(uint32_t(s.leagues.size()));
    for (const auto& l : s.leagues) w.u8(uint32_t(l.index)), w.str(l.id), w.str(l.name);
    w.u16(uint32_t(s.clubs.size()));
    for (const auto& c : s.clubs) w.str(c.id), w.str(c.name);
    w.u32(uint32_t(s.players.size()));
    for (const auto& p : s.players) w.str(p.id), w.str(p.first), w.str(p.last), w.str(p.nation);
    w.u8(uint32_t(s.managers.size()));
    for (const auto& m : s.managers) w.u8(uint32_t(uint8_t(int8_t(m.index)))), w.str(m.name);
    w.u16(uint32_t(s.referees.size()));
    for (const auto& r : s.referees) w.str(r);
    w.u32(uint32_t(s.matches.size()));
    for (const AlmanacMatch& m : s.matches) {
        w.u32(m.date);
        w.u8(m.type), w.u8(m.league), w.u8(m.stage), w.u8(m.round);
        w.u16(m.home), w.u16(m.away);
        w.u8(uint8_t(m.home_manager)), w.u8(uint8_t(m.away_manager));
        w.u8(m.home_goals), w.u8(m.away_goals), w.u8(m.home_shootout), w.u8(m.away_shootout);
        w.u8(m.decided), w.u8(m.referee);
        w.u32(m.attendance);
        w.u8(uint8_t(m.temperature)), w.u8(m.rain);
        for (const auto& side : m.lineup) {
            if (side.size() > 0xff) throw FormatError("almanac: line-up too long");
            w.u8(uint32_t(side.size()));
            for (uint16_t p : side) w.u16(p);
        }
        if (m.events.size() > 0xffff) throw FormatError("almanac: too many events in a match");
        w.u16(uint32_t(m.events.size()));
        for (const AlmanacEvent& e : m.events)
            w.u8(uint32_t(e.kind)), w.u8(e.minute), w.u8(e.side), w.u8(e.detail), w.u16(e.player), w.u16(e.other);
    }
    w.u8(uint32_t(s.tables.size()));
    for (const AlmanacTable& t : s.tables) {
        w.u8(t.league), w.u8(uint32_t(t.rows.size()));
        for (const AlmanacStanding& row : t.rows) {
            w.u16(row.club), w.u8(row.rank), w.u8(row.played), w.u8(row.won), w.u8(row.drawn), w.u8(row.lost);
            w.u8(row.goals_for), w.u8(row.goals_against), w.u16(uint32_t(uint16_t(row.points)));
        }
    }
    w.u16(uint32_t(s.winners.size()));
    for (const AlmanacWinner& x : s.winners)
        w.u8(x.type), w.u8(x.league), w.u8(x.stage), w.u8(x.detail), w.u32(x.date), w.u16(x.winner), w.u16(x.runner_up);
    return std::move(w.out);
}

AlmanacSeason decode_almanac_season(const std::string& bytes) {
    if (bytes.size() < 6 || bytes.compare(0, 4, "OBAL") != 0) throw FormatError("almanac: not an almanac season");
    Reader r{bytes, 4};
    const uint32_t version = r.u16();
    if (version < 1 || version > kVersion) throw FormatError("almanac: unknown season version");
    AlmanacSeason s;
    s.season = int16_t(r.u16()), s.start_year = int(r.u16()), s.display_start_year = int(r.u16());
    for (uint32_t n = r.u16(); n > 0; --n) {
        AlmanacLeague l;
        l.index = int(r.u8()), l.id = r.str(), l.name = r.str();
        s.leagues.push_back(std::move(l));
    }
    for (uint32_t n = r.u16(); n > 0; --n) {
        AlmanacClub c;
        c.id = r.str(), c.name = r.str();
        s.clubs.push_back(std::move(c));
    }
    for (uint32_t n = r.u32(); n > 0; --n) {
        AlmanacPlayer p;
        p.id = r.str(), p.first = r.str(), p.last = r.str(), p.nation = r.str();
        s.players.push_back(std::move(p));
    }
    for (uint32_t n = r.u8(); n > 0; --n) {
        AlmanacManager m;
        m.index = int8_t(r.u8()), m.name = r.str();
        s.managers.push_back(std::move(m));
    }
    for (uint32_t n = r.u16(); n > 0; --n) s.referees.push_back(r.str());
    for (uint32_t n = r.u32(); n > 0; --n) {
        AlmanacMatch m;
        m.date = r.u32();
        m.type = uint8_t(r.u8()), m.league = uint8_t(r.u8()), m.stage = uint8_t(r.u8()), m.round = uint8_t(r.u8());
        m.home = uint16_t(r.u16()), m.away = uint16_t(r.u16());
        m.home_manager = int8_t(r.u8()), m.away_manager = int8_t(r.u8());
        m.home_goals = uint8_t(r.u8()), m.away_goals = uint8_t(r.u8());
        m.home_shootout = uint8_t(r.u8()), m.away_shootout = uint8_t(r.u8());
        m.decided = uint8_t(r.u8()), m.referee = uint8_t(r.u8());
        m.attendance = r.u32();
        m.temperature = int8_t(r.u8()), m.rain = uint8_t(r.u8());
        for (auto& side : m.lineup)
            for (uint32_t k = r.u8(); k > 0; --k) side.push_back(uint16_t(r.u16()));
        for (uint32_t k = r.u16(); k > 0; --k) {
            AlmanacEvent e;
            e.kind = AlmanacEventKind(r.u8());
            e.minute = uint8_t(r.u8()), e.side = uint8_t(r.u8()), e.detail = uint8_t(r.u8());
            e.player = uint16_t(r.u16()), e.other = uint16_t(r.u16());
            if (uint8_t(e.kind) > uint8_t(AlmanacEventKind::Miss)) throw FormatError("almanac: unknown event kind");
            m.events.push_back(e);
        }
        if (m.home >= s.clubs.size() || m.away >= s.clubs.size()) throw FormatError("almanac: unknown club in a match");
        s.matches.push_back(std::move(m));
    }
    if (version >= 2) {
        for (uint32_t n = r.u8(); n > 0; --n) {
            AlmanacTable t;
            t.league = uint8_t(r.u8());
            for (uint32_t k = r.u8(); k > 0; --k) {
                AlmanacStanding row;
                row.club = uint16_t(r.u16());
                row.rank = uint8_t(r.u8()), row.played = uint8_t(r.u8()), row.won = uint8_t(r.u8());
                row.drawn = uint8_t(r.u8()), row.lost = uint8_t(r.u8());
                row.goals_for = uint8_t(r.u8()), row.goals_against = uint8_t(r.u8());
                row.points = int16_t(r.u16());
                if (row.club >= s.clubs.size()) throw FormatError("almanac: unknown club in a table");
                t.rows.push_back(row);
            }
            s.tables.push_back(std::move(t));
        }
        for (uint32_t n = r.u16(); n > 0; --n) {
            AlmanacWinner x;
            x.type = uint8_t(r.u8()), x.league = uint8_t(r.u8()), x.stage = uint8_t(r.u8()), x.detail = uint8_t(r.u8());
            x.date = r.u32();
            x.winner = uint16_t(r.u16()), x.runner_up = uint16_t(r.u16());
            if (x.winner >= s.clubs.size() || (x.runner_up != 0xffff && x.runner_up >= s.clubs.size()))
                throw FormatError("almanac: unknown club among the winners");
            s.winners.push_back(x);
        }
    }
    if (r.at != bytes.size()) throw FormatError("almanac: trailing bytes after the season");
    s.index();
    return s;
}

void almanac_final_tables(ModernState& g) try {
    AlmanacSeason& season = current_season(g);
    season.tables.clear();
    std::erase_if(season.winners, [](const AlmanacWinner& x) { return x.type == 0; });
    for (int league = 0; league < kLeagueCount; ++league) {
        const LeagueTable t = compute_league_table(g, league, 0, league_matchdays(league), 7, 1);
        const int first = league_first_team(league);
        AlmanacTable table;
        table.league = uint8_t(league);
        for (int i = 0; i < t.size; ++i) {
            AlmanacStanding row;
            row.club = season.club(club_of(g, first + i));
            row.rank = uint8_t(t.rank(i) + 1);
            row.played = uint8_t(t.games(i)), row.won = t.wins[size_t(i)], row.lost = t.defeats[size_t(i)];
            row.drawn = uint8_t(std::max(0, t.games(i) - t.wins[size_t(i)] - t.defeats[size_t(i)]));
            row.goals_for = t.goals_for[size_t(i)], row.goals_against = t.goals_against[size_t(i)];
            row.points = t.points[size_t(i)];
            table.rows.push_back(row);
        }
        std::stable_sort(table.rows.begin(), table.rows.end(),
                         [](const AlmanacStanding& a, const AlmanacStanding& b) { return a.rank < b.rank; });
        if (!table.rows.empty()) {
            AlmanacWinner champion;
            champion.league = uint8_t(league);
            champion.date = g.date;
            champion.winner = table.rows[0].club;
            if (table.rows.size() > 1) champion.runner_up = table.rows[1].club;
            season.winners.push_back(champion);
        }
        season.tables.push_back(std::move(table));
    }
} catch (const std::exception& e) {
    std::fprintf(stderr, "almanac: final tables not recorded: %s\n", e.what());
}

void almanac_cup_winner(ModernState& g, int cup, int stage, int winner, int runner_up, bool masters) try {
    const int teams = world_layout().total_teams();
    if (winner < 0 || winner >= teams) return;
    AlmanacSeason& season = current_season(g);
    AlmanacWinner x;
    x.type = uint8_t(cup + 1);
    x.stage = uint8_t(stage);
    x.detail = masters ? 1 : 0;
    x.date = g.date;
    x.winner = season.club(club_of(g, winner));
    if (runner_up >= 0 && runner_up < teams) x.runner_up = season.club(club_of(g, runner_up));
    season.winners.push_back(x);
} catch (const std::exception& e) {
    std::fprintf(stderr, "almanac: winner not recorded: %s\n", e.what());
}

void new_player_identity(ModernState& g, int player) {
    if (player < 0 || size_t(player) >= g.players.size()) return;
    if (g.world_player_id.size() < g.players.size()) g.world_player_id.resize(g.players.size());
    // The new person keeps the name of the index, as in the original (its name pool entry).
    g.world_player_id[size_t(player)] = "player:gen-" + std::to_string(++g.almanac.generated_players);
}

// --- recording -------------------------------------------------------------------------

AlmanacRecorder::AlmanacRecorder(ModernState& g, const std::vector<MatchGroup>& groups) : g_(g), groups_(groups) {
    AlmanacSeason& season = current_season(g);
    for (size_t gi = 0; gi < groups.size(); ++gi) {
        const MatchGroup& group = groups[gi];
        if (group.desc.size() < 0xd0) continue;
        for (int m = 0; m < group.matches(); ++m) {
            if (group.void_mask() >> m & 1) continue;
            const size_t f = size_t(m) * 2;
            if (f + 1 >= group.fixtures.size()) continue;
            const int home = group.fixtures[f], away = group.fixtures[f + 1];
            const int teams = world_layout().total_teams();
            if (home < 0 || away < 0 || home >= teams || away >= teams || home == no_team() || away == no_team()) continue;
            Running r;
            r.group = gi, r.match = m;
            AlmanacMatch& rec = r.record;
            rec.date = g.date;
            rec.type = group.desc[0];
            const uint32_t id = le32(group.desc, 0xcc);
            if (rec.type == 0) {
                rec.league = uint8_t(id);
                if (id < g.league_rounds.size()) rec.round = uint8_t(g.league_rounds[id] + 1);
            } else if (id >= kCupTable && id < kCupTable + kCupEntries * kCupEntrySize && (id - kCupTable) % kCupEntrySize == 0) {
                rec.stage = uint8_t(modern::cup_entry_index(id));
                rec.round = uint8_t(modern::cup_round(g.memory, rec.stage) + 1);
            }
            rec.home = season.club(club_of(g, home));
            rec.away = season.club(club_of(g, away));
            if (const int hm = g.manager_of(home); hm >= 0) rec.home_manager = season.manager(manager_of(g, hm));
            if (const int am = g.manager_of(away); am >= 0) rec.away_manager = season.manager(manager_of(g, am));
            rec.temperature = int8_t(group.desc.at(0x30 + size_t(m)));
            rec.rain = group.desc.at(0x50 + size_t(m));
            if (const int ref = group.desc.at(0x10 + size_t(m)); ref > 0) rec.referee = season.referee(referee_name(g, ref));
            for (int side = 0; side < 2; ++side) {
                const int team = side ? away : home;
                if (team >= league_clubs()) continue;  // only league clubs have squads
                const SquadRange range = squad_range(team);
                for (size_t s = 0; s < range.count; ++s) {
                    const uint16_t e = g.squad_slots.at(range.first + s);
                    const int player = e & 0x0fff;
                    if (player == 0x0fff) continue;
                    if (s < 11 || (e & 0x4000)) rec.lineup[size_t(side)].push_back(player_ref(player));
                }
                r.eleven[size_t(side)] = on_pitch(team);
            }
            running_.push_back(std::move(r));
        }
    }
}

uint16_t AlmanacRecorder::player_ref(int player) {
    AlmanacPlayer p;
    p.id = entry(g_.world_player_id, size_t(player));
    if (p.id.empty()) p.id = "player:legacy-" + std::to_string(player);
    p.first = entry(g_.player_first, size_t(player));
    p.last = entry(g_.player_last, size_t(player));
    if (p.last.empty()) p.last = utf8(g_.player_name(player));
    p.nation = entry(g_.player_nation, size_t(player));
    return g_.almanac.current.player(p);
}

// The players on the pitch: the first 11 slots that still carry a line-up flag (a player
// sent off or injured loses it; a substitute takes the slot of the player he replaces).
std::vector<uint16_t> AlmanacRecorder::on_pitch(int team) const {
    std::vector<uint16_t> out;
    if (team < 0 || team >= league_clubs()) return out;
    const SquadRange range = squad_range(team);
    for (size_t s = 0; s < std::min<size_t>(11, range.count); ++s) {
        const uint16_t e = g_.squad_slots.at(range.first + s);
        if ((e & 0x0fff) != 0x0fff && (e & 0xf000)) out.push_back(uint16_t(e & 0x0fff));
    }
    std::sort(out.begin(), out.end());
    return out;
}

void AlmanacRecorder::after_minute(int minute) {
    for (size_t i = 0; i < running_.size(); ++i) {
        Running& r = running_[i];
        const MatchGroup& group = groups_.at(r.group);
        for (int side = 0; side < 2; ++side) {
            const int team = group.fixtures.at(size_t(r.match) * 2 + size_t(side));
            std::vector<uint16_t> now = on_pitch(team);
            auto& before = r.eleven[size_t(side)];
            if (now == before) continue;
            std::vector<uint16_t> came, left;
            std::set_difference(now.begin(), now.end(), before.begin(), before.end(), std::back_inserter(came));
            std::set_difference(before.begin(), before.end(), now.begin(), now.end(), std::back_inserter(left));
            // A player still in the first 11 slots left without a substitute (sent off or
            // injured); the others went to the bench for the players who came on.
            const SquadRange range = squad_range(team);
            std::vector<uint16_t> replaced;
            for (uint16_t p : left) {
                bool in_eleven = false;
                for (size_t s = 0; s < std::min<size_t>(11, range.count) && !in_eleven; ++s)
                    in_eleven = (g_.squad_slots.at(range.first + s) & 0x0fff) == p;
                if (!in_eleven) replaced.push_back(p);
            }
            for (size_t k = 0; k < came.size(); ++k) {
                AlmanacEvent e;
                e.kind = AlmanacEventKind::Substitution;
                e.minute = uint8_t(std::clamp(minute, 0, 255));
                e.side = uint8_t(side);
                e.player = player_ref(came[k]);
                if (k < replaced.size()) e.other = player_ref(replaced[k]);
                pending_.push_back(e);
                pending_match_.push_back(i);
            }
            before = std::move(now);
        }
    }
}

void AlmanacRecorder::finish(const std::vector<ShowEvent>& shown) {
    std::vector<std::vector<AlmanacEvent>> events(running_.size());
    std::map<std::pair<size_t, int>, size_t> by_match;
    for (size_t i = 0; i < running_.size(); ++i) by_match[{running_[i].group, running_[i].match}] = i;
    auto ref = [&](int player) { return player >= 0 && player < 0x0fff ? player_ref(player) : kAlmanacNone; };
    // Goals, misses, cards and injuries: the event log of each group (Matches), which the
    // engine keeps for every match. A pair of words per entry: player + code * 0x1000, then
    // team * 0x80 + value (the minute; for the second pair of a chance the distance).
    //   0 goal, 1 miss: followed by the assistant pair, code 2 open play, 7 penalty / free
    //     kick, 8 follow-up, 9 corner
    //   3 yellow card, 4 second yellow, 5 red card, 6 injury
    std::map<std::pair<size_t, int>, size_t> by_team;  // (group, team) -> running match
    for (size_t i = 0; i < running_.size(); ++i)
        for (int side = 0; side < 2; ++side)
            by_team[{running_[i].group, groups_.at(running_[i].group).fixtures.at(size_t(running_[i].match) * 2 + size_t(side))}] = i;
    for (size_t gi = 0; gi < groups_.size(); ++gi) {
        const auto& log = groups_[gi].log;
        for (size_t w = 0; w + 1 < log.size(); w += 2) {
            const int code = log[w] >> 12, player = log[w] & 0x0fff;
            const int team = log[w + 1] >> 7, value = log[w + 1] & 0x7f;
            const bool chance = code == 0 || code == 1;
            int assistant = 0x0fff, detail = 0;
            if (chance && w + 3 < log.size()) {
                const int follow = log[w + 2] >> 12;
                assistant = log[w + 2] & 0x0fff;
                detail = follow == 2 ? 0 : std::clamp(follow - 6, 0, 3);
                w += 2;
            }
            const auto it = by_team.find({gi, team});
            if (it == by_team.end()) continue;
            AlmanacEvent e;
            e.minute = uint8_t(value);
            e.side = groups_[gi].fixtures.at(size_t(running_[it->second].match) * 2) == team ? 0 : 1;
            e.player = ref(player);
            switch (code) {
                case 0: case 1:
                    e.kind = code == 0 ? AlmanacEventKind::Goal : AlmanacEventKind::Miss;
                    e.detail = uint8_t(detail);
                    e.other = ref(assistant);
                    break;
                case 3: e.kind = AlmanacEventKind::YellowCard; break;
                case 4: e.kind = AlmanacEventKind::SecondYellow; break;
                case 5: e.kind = AlmanacEventKind::RedCard; break;
                case 6: e.kind = AlmanacEventKind::Injury; break;
                default: continue;
            }
            events[it->second].push_back(e);
        }
    }
    // The kicks of a shoot-out (the log has none): the show events of the match engine.
    for (const ShowEvent& s : shown) {
        if (s.what != Show::PenaltyKick) continue;
        const auto it = by_match.find({size_t(s.group), int(s.match)});
        if (it == by_match.end()) continue;
        AlmanacEvent e;
        e.kind = AlmanacEventKind::ShootoutKick;
        e.minute = uint8_t(std::clamp<int>(s.minute, 0, 255));
        e.side = s.side;
        e.detail = s.flag ? 1 : 0;
        e.player = ref(s.player);
        events[it->second].push_back(e);
    }
    // Substitutions after the shown events of their minute (an injury, then its substitute).
    for (size_t k = 0; k < pending_.size(); ++k) {
        auto& list = events[pending_match_[k]];
        const auto at = std::upper_bound(list.begin(), list.end(), pending_[k].minute,
                                         [](uint8_t minute, const AlmanacEvent& e) { return minute < e.minute; });
        list.insert(at, pending_[k]);
    }
    AlmanacSeason& season = g_.almanac.current;
    for (size_t i = 0; i < running_.size(); ++i) {
        Running& r = running_[i];
        const MatchGroup& group = groups_.at(r.group);
        const int m = r.match;
        if (group.results.empty() && (size_t(m) >= group.cup_results.size() || group.cup_results[size_t(m)] == 0xCCCC)) continue;
        if (!group.results.empty() && group.results.at(size_t(m)) == 0xCC) continue;
        int home = 0, away = 0;
        if (!modern::match_score(group, m, home, away)) continue;
        AlmanacMatch& rec = r.record;
        rec.events = std::move(events[i]);
        if (group.results.empty()) rec.decided = uint8_t(group.cup_results[size_t(m)] >> 10 & 3);
        // After a shoot-out the stored score includes its goals (CupTie2): the goals of the
        // match are those of the log, the rest were scored in the shoot-out.
        if (rec.decided == 2) {
            int goals[2] = {0, 0};
            for (const AlmanacEvent& e : rec.events)
                if (e.kind == AlmanacEventKind::Goal) ++goals[e.side];
            rec.home_shootout = uint8_t(std::max(0, home - goals[0]));
            rec.away_shootout = uint8_t(std::max(0, away - goals[1]));
        }
        rec.home_goals = uint8_t(std::max(0, home - rec.home_shootout));
        rec.away_goals = uint8_t(std::max(0, away - rec.away_shootout));
        if (size_t(m) < group.spectators.size()) rec.attendance = group.spectators[size_t(m)];
        season.matches.push_back(std::move(rec));
    }
}

}  // namespace bmh::modern
