// SPDX-License-Identifier: GPL-3.0-only
#include "game/schedule.hpp"
#include "game/world_layout.hpp"
#include "formats/executable_layout.hpp"
#include "formats/le.hpp"

#include <stdexcept>
#include <utility>

namespace bmh {

namespace {

// Fixture content belongs to the selected installation. Explicit callers install
// the loaded tables before scheduling; no copied or generated fallback is used.
FixtureTableCache fixture_tables;

const char* round_string(int teams, int round) {
    const auto table = fixture_tables.find(teams);
    if (table == fixture_tables.end())
        throw std::invalid_argument("fixture table is not installed for this league size");
    if (round < 1 || size_t(round) > table->second.size())
        throw std::out_of_range("fixture round out of range");
    return table->second[size_t(round - 1)].c_str();
}

}  // namespace

void set_fixture_tables(FixtureTableCache tables) {
    if (tables.empty()) throw std::invalid_argument("fixture tables are empty");
    for (const auto& [teams, rounds] : tables) {
        if (teams != 4 && teams != 6 && teams != 12 && teams != 16 && teams != 18 && teams != 20)
            throw std::invalid_argument("unsupported fixture table size");
        if (rounds.size() != size_t(teams - 1))
            throw std::invalid_argument("fixture table has an invalid round count");
        for (const auto& round : rounds) {
            if (round.size() != size_t(teams))
                throw std::invalid_argument("fixture round has an invalid length");
            uint32_t seen = 0;
            for (unsigned char ch : round) {
                const int team = int(ch) - 'a';
                if (team < 0 || team >= teams || (seen & (uint32_t(1) << team)))
                    throw std::invalid_argument("fixture round has an invalid or duplicate team");
                seen |= uint32_t(1) << team;
            }
        }
    }
    fixture_tables.swap(tables);
}

void install_fixture_tables(const LeImage& exe) {
    struct Binding { int teams; uint32_t table; };
    static constexpr Binding german[] = {
        {4, layout::CareerSession_RuntimeError_RuntimeField},
        {6, layout::CareerSession_RuntimeError_RuntimeField2},
        {12, layout::CareerSession_RuntimeError_RuntimeField3},
        {16, layout::CareerSession_RuntimeError_ExecutableField3},
        {18, layout::CareerSession_RuntimeError_ExecutableField},
        {20, layout::CareerSession_RuntimeError_ExecutableField2},
    };
    static constexpr Binding english[] = {
        {4, layout::EnglishFourTeamFixtureTable},
        {6, layout::EnglishSixTeamFixtureTable},
        {12, layout::EnglishTwelveTeamFixtureTable},
        {20, layout::EnglishTwentyTeamFixtureTable},
    };
    FixtureTableCache tables;
    const auto read = [&](const auto& bindings) {
        for (const auto& [teams, table] : bindings) {
            if (!exe.contains(table) || !exe.contains(table + uint32_t(teams * 4 - 1)))
                throw std::invalid_argument("fixture pointer table is outside the loaded image");
            const uint32_t empty = exe.u32(table);
            if (!exe.contains(empty) || exe.u8(empty) != 0)
                throw std::invalid_argument("fixture table has no empty initial round");
            auto& rounds = tables[teams];
            for (int round = 1; round < teams; ++round) {
                const uint32_t at = exe.u32(table + uint32_t(round * 4));
                if (!exe.contains(at) || uint64_t(at) + uint64_t(teams) >= uint64_t(exe.base()) + exe.size())
                    throw std::invalid_argument("fixture round is outside the loaded image");
                if (exe.u8(at + uint32_t(teams)) != 0)
                    throw std::invalid_argument("fixture round has no terminating NUL");
                std::string text;
                for (int i = 0; i < teams; ++i) text.push_back(char(exe.u8(at + uint32_t(i))));
                rounds.push_back(std::move(text));
            }
        }
    };
    if (exe.u8(layout::LeagueSizes) == 20) read(english);
    else read(german);
    set_fixture_tables(std::move(tables));
}

int league_first_team(int league) {
    int first = 0;
    for (int l = 0; l < league; l++) first += kLeagueSizes[l];
    return first;
}

int team_league(int team) {
    int end = 0;
    for (int l = 0; l < kLeagueCount; l++) {
        end += kLeagueSizes[l];
        if (team < end) return l;
    }
    return kLeagueCount;
}

std::vector<Fixture> generate_fixtures(int teams, int matchday, int rounds, int first_team) {
    // GenerateFixtures: reduce the matchday into the first half, toggling home/away for every
    // half season that is skipped.
    bool swap = false;
    while (matchday > rounds) {
        matchday -= rounds;
        swap = !swap;
    }
    const char* s = round_string(teams, matchday);
    std::vector<Fixture> out;
    // The original loops (rounds + 1) / 2 times, which equals teams / 2 for a full league.
    for (int m = 0; m < (rounds + 1) / 2; m++) {
        int a = s[2 * m] - 'a', b = s[2 * m + 1] - 'a';
        if (swap) std::swap(a, b);
        out.push_back({first_team + a, first_team + b});
    }
    return out;
}

int league_table_teams(int league) {
    if (kLeagueTablesPerLeague) return kLeagueSizes[league];
    return league < 5 ? kLeagueSizes[0] : kLeagueSizes[5];
}

void set_league_sizes(const uint8_t (&sizes)[kLeagueCount]) {
    int total = 0;
    for (int l = 0; l < kLeagueCount; l++) total += sizes[l];
    const auto has_table = [](int teams) { return teams == 20 || teams == 18 || teams == 16 || teams == 12; };
    if (total != world_layout().league_teams || !has_table(sizes[0]) || !has_table(sizes[5]))
        throw std::invalid_argument("league sizes do not describe 138 league teams with fixture tables");
    for (int l = 0; l < kLeagueCount; l++) {
        if (kLeagueTablesPerLeague ? !has_table(sizes[l]) : sizes[l] < 2 || sizes[l] > (l < 5 ? sizes[0] : sizes[5]))
            throw std::invalid_argument("a league is larger than its fixture table");
    }
    for (int l = 0; l < kLeagueCount; l++) kLeagueSizes[l] = sizes[l];
}

void set_league_groups(const uint8_t (&first)[8], const uint8_t (&count)[8]) {
    uint8_t bit[kLeagueCount];
    for (int l = 0; l < kLeagueCount; l++) bit[l] = 0xff;
    for (int b = 0; b < 8; b++) {
        if (first[b] + count[b] > kLeagueCount) throw std::invalid_argument("calendar groups outside the leagues");
        // (bits 4-7 are the four international leagues, all on calendar bit 4)
        for (int l = first[b]; l < first[b] + count[b]; l++) bit[l] = uint8_t(b < 4 ? b : 4);
    }
    for (int b = 0; b < 8; b++) kGroupLeague[b] = first[b], kGroupLeagues[b] = count[b];
    for (int l = 0; l < kLeagueCount; l++) kLeagueBit[l] = bit[l];
}

std::vector<Fixture> league_fixtures(int league, int matchday) {
    const int teams = kLeagueSizes[league];
    return generate_fixtures(league_table_teams(league), matchday, teams - 1, league_first_team(league));
}

}  // namespace bmh
