// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Reading the recorder's JSON lines (runtime verification) - shared by the test tools.
//
// Every line is one flat JSON object: minute snapshots ({"minute":n,...,"groups":[...]}),
// post-match snapshots ({"post":n,...}) and state dumps ({"state":{...}}) with the game
// tables as hex strings.
#include <cstdint>
#include <string>
#include <vector>

#include "formats/binary.hpp"
#include "game/match_engine.hpp"
#include "game/state.hpp"

namespace bmh::capture {

// Value of `key` (string contents or number text); searching starts at `from`.
inline std::string field(const std::string& line, const std::string& key, size_t from = 0) {
    size_t p = line.find("\"" + key + "\":", from);
    if (p == std::string::npos) return "";
    p += key.size() + 3;
    if (line[p] == '"') {
        size_t e = line.find('"', p + 1);
        return line.substr(p + 1, e - p - 1);
    }
    size_t e = line.find_first_of(",}]", p);
    return line.substr(p, e - p);
}

inline Bytes unhex(const std::string& s) {
    Bytes b;
    for (size_t i = 0; i + 1 < s.size(); i += 2) b.push_back(uint8_t(std::stoi(s.substr(i, 2), nullptr, 16)));
    return b;
}

inline std::vector<uint32_t> u32s(const Bytes& b) {
    std::vector<uint32_t> v;
    for (size_t i = 0; i + 3 < b.size(); i += 4) v.push_back(le32(b, i));
    return v;
}

inline std::vector<uint16_t> u16s(const Bytes& b) {
    std::vector<uint16_t> v;
    for (size_t i = 0; i + 1 < b.size(); i += 2) v.push_back(le16(b, i));
    return v;
}

// GameState from a state dump. Tables the dump does not contain stay empty.
inline GameState state_from_dump(const std::string& line) {
    GameState g;
    g.squad_slots = u16s(unhex(field(line, "squad_slots")));
    auto a = u32s(unhex(field(line, "w35c"))), b = u32s(unhex(field(line, "w368"))), c = u32s(unhex(field(line, "w364")));
    for (size_t i = 0; i < a.size(); i++) g.players.push_back(Player{"", a[i], b[i], c[i]});
    g.team_morale = unhex(field(line, "team_morale"));
    g.team_flags = u16s(unhex(field(line, "team_flags")));
    g.player_bonus = unhex(field(line, "player_bonus"));
    g.formations = unhex(field(line, "formations"));
    g.manager_records = unhex(field(line, "managers"));
    int count = std::stoi(field(line, "manager_count"));
    for (int m = 0; m < count; m++) g.manager_teams.push_back(int16_t(le16(g.manager_records, m * 0xB0C + 0x290)));
    g.indoor = field(line, "indoor") != "0";
    // newer recordings (post-match tables)
    g.player_cards = u16s(unhex(field(line, "player_cards")));
    g.club_table = unhex(field(line, "club_table"));
    g.tactics = unhex(field(line, "tactics"));
    g.manager_options = unhex(field(line, "manager_options"));
    if (!field(line, "bdaf8").empty()) g.development_balance = std::stoi(field(line, "bdaf8"));
    if (!field(line, "a3d4c").empty()) g.trend_rotation = std::stoi(field(line, "a3d4c"));
    if (!field(line, "a3fd3").empty()) g.list_toggle = std::stoi(field(line, "a3fd3"));
    if (!field(line, "bd8fc").empty()) g.autosave_months = std::stoi(field(line, "bd8fc"));
    if (!field(line, "bdb75").empty()) g.autosave_month = std::stoi(field(line, "bdb75"));
    if (!field(line, "level").empty()) g.level = std::stoi(field(line, "level"));
    return g;
}

// Field of one match group: search only between this group's start and the next group.
inline std::string group_field(const std::string& l, const std::string& key, size_t start) {
    size_t end = l.find("{\"type\":", start + 1);
    std::string v = field(l, key, start);
    size_t at = l.find("\"" + key + "\":", start);
    return (at == std::string::npos || (end != std::string::npos && at > end)) ? std::string() : v;
}

// The match groups of a minute (or post-match baseline) snapshot.
inline std::vector<MatchGroup> groups_from_snapshot(const std::string& line) {
    std::vector<MatchGroup> groups;
    size_t p = line.find("\"groups\":[");
    while ((p = line.find("{\"type\":", p)) != std::string::npos) {
        MatchGroup g;
        g.desc = unhex(group_field(line, "desc", p));
        Bytes fx = unhex(group_field(line, "fixtures", p));
        for (size_t k = 0; k + 1 < fx.size(); k += 2) g.fixtures.push_back(le16(fx, k));
        g.results = unhex(group_field(line, "results", p));
        g.records = unhex(group_field(line, "sides", p));
        g.cup_results = u16s(unhex(group_field(line, "cup0", p)));
        g.cup_other_leg = u16s(unhex(group_field(line, "cup1", p)));
        for (uint32_t s : u32s(unhex(group_field(line, "spectators", p)))) g.spectators.push_back(s);
        g.end_minute = unhex(group_field(line, "end_minutes", p));
        g.end_minute.resize(g.matches(), 0);
        if (g.desc.size() >= 0xd0 && g.desc.at(0) != 0) {
            // cups: the descriptor holds guest pointers (cup table entry, result arrays);
            // the port keeps LE data addresses
            const uint32_t entry = le32(g.desc, 0xcc);
            if (entry >= kGuestDataBase) {
                const uint32_t e = entry - kGuestDataBase;
                for (int i = 0; i < 4; i++) g.desc.at(0xcc + size_t(i)) = uint8_t(e >> (8 * i));
            }
            for (int leg = 0; leg < 2; leg++) {
                const uint32_t r = le32(g.desc, 0x80 + size_t(leg) * 4);
                if (r >= kGuestDataBase) g.cup_result_address[leg] = r - kGuestDataBase;
            }
        }
        groups.push_back(g);
        p++;
    }
    return groups;
}

}  // namespace bmh::capture
