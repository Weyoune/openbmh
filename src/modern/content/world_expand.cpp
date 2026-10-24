// SPDX-License-Identifier: GPL-3.0-only
#include "modern/content/world_expand.hpp"
// OpenBMH deviates from the original: Modern worlds have league sizes, clubs and players of their own; players whose nation has no single-byte nationality code carry a substitute foreign code.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <stdexcept>

#include "formats/executable_layout.hpp"
#include "game/schedule.hpp"
#include "game/table.hpp"
#include "import/detail.hpp"
#include "modern/game/world_calendar.hpp"

namespace bmh::modern {

namespace {

using obw::detail::fail;

// MODERN_DEBUG=1 traces the expansion stages on stderr.
void stage(const char* name) {
    static const bool on = std::getenv("MODERN_DEBUG") != nullptr;
    if (on) std::fprintf(stderr, "expand: %s\n", name);
}

constexpr int kSquadSlots = 24;

// Decodes one UTF-8 code point; returns U+FFFD for malformed input.
char32_t next_code_point(const std::string& s, size_t& i) {
    const auto byte = [&](size_t k) { return k < s.size() ? uint8_t(s[k]) : uint8_t(0); };
    const uint8_t b = byte(i);
    if (b < 0x80) return ++i, b;
    if ((b >> 5) == 6 && (byte(i + 1) >> 6) == 2) {
        const char32_t c = char32_t(b & 0x1f) << 6 | (byte(i + 1) & 0x3f);
        i += 2;
        return c;
    }
    if ((b >> 4) == 14 && (byte(i + 1) >> 6) == 2 && (byte(i + 2) >> 6) == 2) {
        const char32_t c = char32_t(b & 0x0f) << 12 | char32_t(byte(i + 1) & 0x3f) << 6 | (byte(i + 2) & 0x3f);
        i += 3;
        return c;
    }
    ++i;
    while (i < s.size() && (byte(i) >> 6) == 2) ++i;
    return 0xfffd;
}

// Letters of Latin Extended-A and similar that have no single-byte form: their base letter.
char base_letter(char32_t c) {
    static const std::map<char32_t, char> letters = {
        {0x0106, 'C'}, {0x0107, 'c'}, {0x010c, 'C'}, {0x010d, 'c'}, {0x0110, 'D'}, {0x0111, 'd'},
        {0x0118, 'E'}, {0x0119, 'e'}, {0x011a, 'E'}, {0x011b, 'e'}, {0x011e, 'G'}, {0x011f, 'g'},
        {0x0130, 'I'}, {0x0131, 'i'}, {0x0141, 'L'}, {0x0142, 'l'}, {0x0143, 'N'}, {0x0144, 'n'},
        {0x0147, 'N'}, {0x0148, 'n'}, {0x0150, 'O'}, {0x0151, 'o'}, {0x0158, 'R'}, {0x0159, 'r'},
        {0x015a, 'S'}, {0x015b, 's'}, {0x015e, 'S'}, {0x015f, 's'}, {0x0160, 'S'}, {0x0161, 's'},
        {0x0162, 'T'}, {0x0163, 't'}, {0x0164, 'T'}, {0x0165, 't'}, {0x016e, 'U'}, {0x016f, 'u'},
        {0x0170, 'U'}, {0x0171, 'u'}, {0x0179, 'Z'}, {0x017a, 'z'}, {0x017b, 'Z'}, {0x017c, 'z'},
        {0x017d, 'Z'}, {0x017e, 'z'}, {0x0219, 's'}, {0x0218, 'S'}, {0x021b, 't'}, {0x021a, 'T'},
        {0x2019, '\''}, {0x2013, '-'}, {0x2014, '-'}};
    const auto it = letters.find(c);
    return it == letters.end() ? '?' : it->second;
}

std::string upper(const std::string& legacy) {
    std::string out = legacy;
    for (char& ch : out) {
        if (ch >= 'a' && ch <= 'z') ch = char(ch - 'a' + 'A');
        else if (ch == '{') ch = '[';   // ä -> Ä
        else if (ch == '|') ch = '\\';  // ö -> Ö
        else if (ch == '}') ch = ']';   // ü -> Ü
        else if (uint8_t(ch) >= 0xe0 && uint8_t(ch) != 0xf7) ch = char(uint8_t(ch) - 0x20);  // Latin-1
    }
    return out;
}

// Appends a zero-terminated name to a pool and returns its offset (pools use u16 offsets).
uint16_t append_name(std::vector<uint8_t>& pool, const std::string& name) {
    if (pool.size() + name.size() + 1 > 0xffff) fail("name pool full (64 KB)");
    const auto at = uint16_t(pool.size());
    pool.insert(pool.end(), name.begin(), name.end());
    pool.push_back(0);
    return at;
}

// Copies per-team records of `width` bytes into the new id order; ids without an old record
// copy the record of their template id (the new id of an original club of the same league).
std::vector<uint8_t> remap_records(const std::vector<uint8_t>& old, size_t width, int old_count, int new_count,
                                   const std::vector<int>& to_new, const std::vector<int>& template_of) {
    std::vector<uint8_t> out(size_t(new_count) * width, 0);
    std::vector<bool> filled(size_t(new_count), false);
    for (int t = 0; t < old_count && size_t(t + 1) * width <= old.size(); ++t) {
        const int n = to_new[size_t(t)];
        if (n < 0 || n >= new_count) continue;
        std::copy_n(old.begin() + long(size_t(t) * width), width, out.begin() + long(size_t(n) * width));
        filled[size_t(n)] = true;
    }
    for (int n = 0; n < new_count; ++n)
        if (!filled[size_t(n)] && template_of[size_t(n)] >= 0 && filled[size_t(template_of[size_t(n)])])
            std::copy_n(out.begin() + long(size_t(template_of[size_t(n)]) * width), width,
                        out.begin() + long(size_t(n) * width));
    return out;
}

std::vector<uint16_t> remap_words(const std::vector<uint16_t>& old, int new_count, const std::vector<int>& to_new,
                                  const std::vector<int>& template_of) {
    std::vector<uint8_t> bytes;
    for (const uint16_t w : old) bytes.push_back(uint8_t(w)), bytes.push_back(uint8_t(w >> 8));
    const auto out = remap_records(bytes, 2, int(old.size()), new_count, to_new, template_of);
    std::vector<uint16_t> words;
    for (size_t i = 0; i + 1 < out.size(); i += 2) words.push_back(uint16_t(out[i] | out[i + 1] << 8));
    return words;
}

void remap_memory(StaticMemory& mem, uint32_t address, size_t width, int old_count, int new_count,
                  const std::vector<int>& to_new, const std::vector<int>& template_of, bool grow) {
    if (!mem.has(address)) return;
    std::vector<uint8_t> old;
    for (int i = 0; i < old_count * int(width) && mem.has(address + uint32_t(i)); ++i) old.push_back(mem.u8(address + uint32_t(i)));
    const auto out = remap_records(old, width, old_count, new_count, to_new, template_of);
    if (grow) mem.grow_block(address, out.size());
    for (size_t i = 0; i < out.size() && mem.has(address + uint32_t(i)); ++i) mem.set8(address + uint32_t(i), out[i]);
}

WorldLayout modern_layout(int league_clubs, int german_clubs) {
    WorldLayout w;
    w.league_teams = league_clubs;
    w.german_teams = german_clubs;
    w.squad_start.clear();
    w.squad_size.clear();
    for (int t = 0; t < league_clubs; ++t) {
        w.squad_start.push_back(uint32_t(t) * kSquadSlots);
        w.squad_size.push_back(kSquadSlots);
    }
    w.transfer_list = uint32_t(league_clubs) * kSquadSlots;
    w.free_agents = w.transfer_list + 44;
    w.foreign_offers = w.free_agents + 80;
    w.pool_end = w.foreign_offers + 20;
    w.total_slots = w.pool_end + w.no_team_slots + 54;
    return w;
}

}  // namespace

std::string legacy_encoding(const std::string& utf8) {
    std::string out;
    for (size_t i = 0; i < utf8.size();) {
        const char32_t c = next_code_point(utf8, i);
        switch (c) {
            case U'Ä': out += '['; break;
            case U'Ö': out += '\\'; break;
            case U'Ü': out += ']'; break;
            case U'ä': out += '{'; break;
            case U'ö': out += '|'; break;
            case U'ü': out += '}'; break;
            case U'ß': out += '~'; break;
            default:
                if (c < 0x80 && c != '[' && c != '\\' && c != ']' && c != '{' && c != '|' && c != '}' && c != '~')
                    out += char(c);
                else if (c >= 0xc0 && c <= 0xff)
                    out += char(uint8_t(c));  // Latin-1 letters, as the Classic pools store them
                else
                    out += base_letter(c);
        }
    }
    return out;
}

void install_world_dimensions(const World& world) {
    int league_clubs = 0, german_clubs = 0;
    uint8_t sizes[kLeagueCount] = {};
    for (const WorldLeague& l : world.leagues) {
        sizes[l.index] = uint8_t(l.members.size());
        league_clubs += int(l.members.size());
        if (l.index < 5) german_clubs += int(l.members.size());
    }
    WorldLayout layout = modern_layout(league_clubs, german_clubs);
    // The league sizes are checked against the layout and need the per-league fixture tables.
    kLeagueTablesPerLeague = true;
    set_world_layout(layout);
    set_league_sizes(sizes);
    kNewcomerFirst = world_layout().first_amateur();
}

ExpandReport expand_to_world(ModernState& g, const World& world) {
    const WorldLayout old = world_layout();
    if (old.league_teams != 138 || old.total_teams() != 419) fail("the world expands a Classic start state only");
    ExpandReport report;

    // --- team ids: league clubs in the order of the world's leagues ----------------------
    int league_clubs = 0;
    uint8_t sizes[kLeagueCount] = {};
    for (const WorldLeague& l : world.leagues) {
        sizes[l.index] = uint8_t(l.members.size());
        league_clubs += int(l.members.size());
    }
    const int added = league_clubs - old.league_teams;
    if (added < 0) fail("a Modern world cannot have fewer league clubs than Classic");
    const int new_total = old.total_teams() + added;
    std::vector<int> to_new(size_t(old.total_teams()), -1);       // Classic id -> new id
    std::vector<int> template_of(size_t(new_total), -1);          // new id -> template new id
    std::vector<const WorldClub*> club_at(size_t(new_total), nullptr);
    for (int t = old.league_teams; t < old.total_teams(); ++t) to_new[size_t(t)] = t + added;
    int id = 0;
    for (const WorldLeague& l : world.leagues) {
        int template_id = -1;
        const int first = id;
        for (const std::string& member : l.members) {
            const WorldClub* c = world.club(member);
            if (!c) fail("unknown league member " + member);
            if (c->legacy_index >= old.league_teams) fail("club " + member + " is not a Classic league club");
            if (c->legacy_index >= 0) {
                if (to_new[size_t(c->legacy_index)] >= 0) fail("Classic club in two leagues: " + member);
                to_new[size_t(c->legacy_index)] = id;
                if (template_id < 0) template_id = id;
            }
            club_at[size_t(id)] = c;
            ++id;
        }
        for (int n = first; n < id; ++n)
            if (!club_at[size_t(n)] || club_at[size_t(n)]->legacy_index < 0) template_of[size_t(n)] = template_id;
        if (template_id < 0 && id > first) fail("league without an original club: " + l.id);
    }
    for (int t = 0; t < old.league_teams; ++t)
        if (to_new[size_t(t)] < 0) fail("Classic league club " + std::to_string(t) + " is missing from the world's leagues");
    for (const WorldClub& c : world.clubs)
        if (c.legacy_index >= old.league_teams && c.legacy_index < old.total_teams())
            club_at[size_t(to_new[size_t(c.legacy_index)])] = &c;
    report.league_clubs = league_clubs;
    report.added_clubs = added;

    stage("team ids");
    // --- per-team tables -----------------------------------------------------------------
    const int records = old.first_foreign_cup() + added;  // clubs with records (league + amateurs)
    g.team_morale = remap_records(g.team_morale, 1, old.total_teams(), new_total, to_new, template_of);
    g.team_flags = remap_words(g.team_flags, new_total, to_new, template_of);
    g.club_heap_78 = remap_records(g.club_heap_78, 2, old.total_teams(), new_total, to_new, template_of);
    g.club_heap_bc = remap_records(g.club_heap_bc, 2, old.first_foreign_cup(), records, to_new, template_of);
    g.club_heap_b8 = remap_records(g.club_heap_b8, 1, old.first_foreign_cup(), records, to_new, template_of);
    if (!g.club_table.empty())
        g.club_table = remap_records(g.club_table, 0x4a, old.first_foreign_cup(), records, to_new, template_of);
    {
        std::vector<int> no_template(template_of.size(), -1);
        g.position_history = remap_records(g.position_history, 0x1a, old.league_teams, league_clubs, to_new, no_template);
        std::vector<uint8_t> results;
        const size_t managers = g.manager_results.size() / 0x690;
        for (size_t m = 0; m < managers; ++m) {
            const std::vector<uint8_t> one(g.manager_results.begin() + long(m * 0x690),
                                           g.manager_results.begin() + long((m + 1) * 0x690));
            const auto moved = remap_records(one, 10, old.first_foreign_cup(), records, to_new, no_template);
            results.insert(results.end(), moved.begin(), moved.end());
        }
        g.manager_results = std::move(results);
    }
    // Club names: Classic clubs keep their pool entries; added clubs get one.
    std::vector<uint16_t> club_ids(size_t(new_total), 0);
    for (int t = 0; t < old.total_teams() && size_t(t) < g.club_ids.size(); ++t)
        club_ids[size_t(to_new[size_t(t)])] = g.club_ids[size_t(t)];
    for (int n = 0; n < new_total; ++n)
        if (template_of[size_t(n)] >= 0 && club_at[size_t(n)])
            club_ids[size_t(n)] = append_name(g.club_names, upper(legacy_encoding(club_at[size_t(n)]->name)));
    g.club_ids = std::move(club_ids);
    remap_memory(g.memory, ::bmh::layout::ClubShortNameOffsets, 1, old.first_foreign_cup(), records, to_new, template_of, true);
    for (int n = 0; n < records; ++n)  // added clubs: the short name is the start of the name
        if (template_of[size_t(n)] >= 0 && club_at[size_t(n)])
            g.memory.set8(::bmh::layout::ClubShortNameOffsets + uint32_t(n),
                          uint8_t(std::min<size_t>(legacy_encoding(club_at[size_t(n)]->name).size(), 16) - 1));
    {   // Card counters per league club (fair-play table): 2 bytes each, room behind the block.
        std::vector<int> no_template(template_of.size(), -1);
        remap_memory(g.memory, ::bmh::layout::DeskRecords_Teams_RuntimeField, 2, old.league_teams, league_clubs, to_new,
                     no_template, true);
    }
    // Kit selectors are only moved on promotion; the block has no room for more clubs.
    remap_memory(g.memory, ::bmh::layout::ClubKitSelectors, 1, old.total_teams(), old.total_teams(), to_new, template_of, false);

    stage("per-team tables");
    // --- players -------------------------------------------------------------------------
    std::map<std::string, int> player_index;
    std::vector<const WorldPlayer*> player_at;
    for (const WorldPlayer& p : world.players)
        if (p.legacy) {
            if (p.legacy_index < 0 || size_t(p.legacy_index) >= g.players.size()) fail("invalid original player " + p.id);
            player_index[p.id] = p.legacy_index;
        }
    int next_player = int(g.players.size());
    for (const WorldPlayer& p : world.players)
        if (!p.legacy) player_index[p.id] = next_player++;
    if (next_player > 0xFFE) fail("too many players for 12-bit player ids");
    player_at.assign(size_t(next_player), nullptr);
    for (const WorldPlayer& p : world.players) player_at[size_t(player_index[p.id])] = &p;
    const size_t original_players = g.players.size();
    g.players.resize(size_t(next_player));
    g.player_name_offsets.resize(size_t(next_player), 0);
    g.player_cards.resize(size_t(next_player), 0);
    if (g.player_bonus.size() < size_t(next_player) + 0x36) g.player_bonus.resize(size_t(next_player) + 0x36, 0);
    for (size_t i = original_players; i < size_t(next_player); ++i) {
        const WorldPlayer& p = *player_at[i];
        const auto clamp7 = [](int v) { return uint32_t(std::clamp(v, 0, 127)); };
        const auto skill = [](int v) { return uint32_t(std::clamp(v, 0, 3)); };
        int nationality = 31;
        if (const auto it = world.nationality.find(p.nation); it != world.nationality.end() && it->second < 32)
            nationality = it->second;
        else
            ++report.fallback_nationalities;
        const int age = std::clamp(p.age < 0 ? 24 : p.age, 16, 47);
        if (p.age >= 0) {  // a birth date: the career start keeps this age (balance.md)
            if (g.world_age_known.size() < size_t(next_player)) g.world_age_known.resize(size_t(next_player), false);
            g.world_age_known[i] = true;
        }
        Player& pl = g.players[i];
        pl.w35c = clamp7(p.form) | clamp7(p.technique) << 7 | clamp7(p.condition) << 14;
        pl.w368 = skill(p.teamplay) | skill(p.speed) << 2 | skill(p.shooting) << 4 | skill(p.passing) << 6 |
                  skill(p.tackling) << 8 | skill(p.free_kick) << 10 | skill(p.corner) << 12 | uint32_t(nationality) << 27;
        pl.w364 = uint32_t(p.position & 3) | uint32_t(age - 16) << 21;
        g.player_bonus[i] = uint8_t(std::clamp(p.bonus, 0, 50));
        g.player_name_offsets[i] = append_name(g.player_names, upper(legacy_encoding(p.last)));
        ++report.added_players;
    }

    stage("players");
    // --- layout, squads, league sizes ----------------------------------------------------
    WorldLayout layout = modern_layout(league_clubs, old.german_teams);
    std::vector<uint16_t> slots(layout.total_slots, kEmptySlot);
    // The pools behind the squads keep their Classic contents and order: transfer list, free
    // agents, foreign offers, the no-club squad and the 54 reserve players the career start
    // generates (the world lists those as players without a club).
    std::vector<bool> pooled(size_t(next_player), false);
    for (uint32_t s = old.transfer_list; s < old.total_slots && s < g.squad_slots.size(); ++s) {
        const uint16_t v = g.squad_slots[s];
        slots[layout.transfer_list + (s - old.transfer_list)] = v;
        if ((v & 0x0FFF) != 0x0FFF && size_t(v & 0x0FFF) < pooled.size()) pooled[size_t(v & 0x0FFF)] = true;
    }
    std::map<std::string, std::vector<std::pair<int, int>>> squads;  // club id -> (order, player)
    std::vector<int> free_agents;  // players the world adds without a club
    for (const WorldPlayer& p : world.players) {
        const int pid = player_index[p.id];
        if (p.team.empty()) {
            if (!pooled[size_t(pid)]) free_agents.push_back(pid);
            continue;
        }
        squads[p.team].push_back({p.squad_order < 0 ? 999 : p.squad_order, pid});
    }
    for (int t = 0; t < league_clubs; ++t) {
        auto& squad = squads[club_at[size_t(t)]->id];
        std::stable_sort(squad.begin(), squad.end());
        if (squad.size() > size_t(kSquadSlots)) fail("squad larger than 24 players: " + club_at[size_t(t)]->id);
        for (size_t k = 0; k < squad.size(); ++k) slots[layout.squad_start[size_t(t)] + k] = uint16_t(squad[k].second);
    }
    for (const auto& [club, squad] : squads) {
        const WorldClub* c = world.club(club);
        if (!c || std::find(club_at.begin(), club_at.begin() + league_clubs, c) == club_at.begin() + league_clubs)
            for (const auto& entry : squad) free_agents.push_back(entry.second);  // players of squadless clubs
    }
    size_t free_slot = layout.free_agents;
    for (const int pid : free_agents) {
        while (free_slot < layout.foreign_offers && slots[free_slot] != kEmptySlot) ++free_slot;
        if (free_slot == layout.foreign_offers) fail("no free-agent slot left for the world's players without a club");
        slots[free_slot++] = uint16_t(pid);
    }
    report.free_agents = int(free_agents.size());
    {   // Every player holds at most one slot (squad, pool or reserve).
        std::vector<int> seen(size_t(next_player), -1);
        for (size_t s = 0; s < slots.size(); ++s) {
            const int pid = slots[s] & 0x0FFF;
            if (pid == 0x0FFF) continue;
            if (size_t(pid) >= seen.size()) fail("slot " + std::to_string(s) + " names an unknown player");
            if (seen[size_t(pid)] >= 0)
                fail("player " + std::to_string(pid) + " in slots " + std::to_string(seen[size_t(pid)]) + " and " +
                     std::to_string(s));
            seen[size_t(pid)] = int(s);
        }
    }
    g.squad_slots = std::move(slots);

    stage("squads");
    install_world_dimensions(world);

    stage("layout and league sizes");
    // --- league dates of the international group (calendar bit 4) -------------------------
    const WorldLeague* dated = nullptr;
    for (const WorldLeague& l : world.leagues)
        if (l.index >= 5 && (!dated || l.dates.size() > dated->dates.size())) dated = &l;
    if (dated && !dated->dates.empty()) {
        std::vector<int> days;
        for (const std::string& d : dated->dates) {
            const int day = season_day_of(d, world.start_year);
            if (day < 0) fail("invalid league date " + d);
            days.push_back(day);
        }
        if (int(days.size()) < league_matchdays(dated->index)) fail("fewer league dates than matchdays: " + dated->id);
        for (std::string& moved : install_league_dates(g, 4, days).moved) report.notes.push_back(std::move(moved));
    }

    stage("league dates");
    // --- Modern names and identities -----------------------------------------------------
    g.world_club_id.assign(size_t(new_total), {});
    g.world_club_name.assign(size_t(new_total), {});
    g.world_club_short.assign(size_t(new_total), {});
    g.world_league_id.clear(), g.world_league_name.clear();
    for (const WorldLeague& l : world.leagues) {
        if (size_t(l.index) >= g.world_league_id.size()) {
            g.world_league_id.resize(size_t(l.index) + 1);
            g.world_league_name.resize(size_t(l.index) + 1);
        }
        g.world_league_id[size_t(l.index)] = l.id;
        g.world_league_name[size_t(l.index)] = l.name;
    }
    for (int n = 0; n < new_total; ++n)
        if (const WorldClub* c = club_at[size_t(n)]) {
            g.world_club_id[size_t(n)] = c->id;
            g.world_club_name[size_t(n)] = c->name;
            g.world_club_short[size_t(n)] = c->short_name.empty() ? c->name : c->short_name;
        }
    g.world_player_id.assign(size_t(next_player), {});
    g.player_first.assign(size_t(next_player), {});
    g.player_last.assign(size_t(next_player), {});
    g.player_nation.assign(size_t(next_player), {});
    for (size_t i = 0; i < player_at.size(); ++i)
        if (const WorldPlayer* p = player_at[i]) {
            g.world_player_id[i] = p->id;
            g.player_first[i] = p->first;
            g.player_last[i] = p->last;
            g.player_nation[i] = p->nation;
        }
    // Referees the data set added (data format F9): their names go to the referee pool and
    // their name offsets behind the Classic ones in MasterDatabaseMetadata (u16 per referee).
    // install_world_referees adds the matching property bytes to the database.
    for (const WorldReferee& r : world.referees) {
        if (r.legacy_index >= 0) continue;
        const uint16_t at = append_name(g.referee_names, legacy_encoding(r.name));
        g.master_database_metadata.push_back(uint8_t(at));
        g.master_database_metadata.push_back(uint8_t(at >> 8));
        ++report.added_referees;
    }
    if (g.master_database_metadata.size() / 2 > 0xff) fail("more than 255 referees");
    return report;
}

void install_world_referees(std::vector<uint8_t>& properties, const World& world) {
    for (const WorldReferee& r : world.referees)
        if (r.legacy_index < 0) properties.push_back(r.property_byte());
}

}  // namespace bmh::modern

namespace bmh::modern {

void install_training_camps(ModernState& g, const World& world) {
    const CampCatalogue& catalogue = world.camps ? *world.camps : default_camp_catalogue();
    if (catalogue.camps.empty()) obw::detail::fail("the training camp catalogue is empty");
    g.camp_maps = catalogue.maps;
    g.training_camps = catalogue.camps;
}

}  // namespace bmh::modern
