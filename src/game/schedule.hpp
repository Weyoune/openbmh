// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// League fixtures and results.
//
// Team ids
//   Every club has an index into all per-club tables (names, stadiums, squads, ...).
//   Leagues are contiguous ranges of these ids: league 0 is ids 0..17, league 1 the next
//   18 ids and so on (sizes in kLeagueSizes, LeagueSizes; team -> league is
//   TeamLeague2). When a new game starts the clubs of a league are shuffled by swapping
//   all their table entries (swap function around 0x685xx), so a team's position inside
//   its league is its "draw position" for the fixture list. The master database order is
//   therefore NOT the order used by a running game; always take names from the savegame.
//
// Fixtures (GenerateFixtures)
//   The fixture list is not stored. For each league size there is a fixed table of
//   "round strings" loaded from the executable: round r of the first half of the season is a
//   string of letters, each consecutive pair "xy" is one match, home team = draw position
//   x - 'a', away team = y - 'a'. The second half repeats the rounds with home and away
//   swapped, and so on for leagues that play more than twice (the round index wraps).
//
// Results (block LeagueResults, 0xD5C bytes)
//   results[league * 380 + (matchday - 1) * 10 + match] = home_goals << 4 | away_goals
//   Room for 9 leagues x 38 matchdays x 10 matches. 0xCC marks a match not yet played
//   (the whole block is filled with 0xCC at season start, FUN at layout field in the dump);
//   the code also tests (value & 0xCF) against 0xCC/0xCD/0xCE for postponed matches.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace bmh {

class LeImage;
// Round strings are installation content. Sparse authored caches are supported
// for isolated callers; real sessions install the tables from their executable.
using FixtureTableCache = std::map<int, std::vector<std::string>>;
// Validates every supplied table before replacing the current cache. Throws
// std::invalid_argument for malformed rows, team ids, sizes or round counts.
void set_fixture_tables(FixtureTableCache tables);
void install_fixture_tables(const LeImage& exe);

// Number of teams per league, LeagueSizes of the loaded game: the German versions have 5
// leagues of 18 and 4 of 12; the English Football Limited 20 20 20 20 10 and the same four
// international leagues of 12 (both 138 league teams, so the squad layout is the same).
// The German sizes are the default; set_league_sizes() installs those of the loaded game
// (app::CareerSession does it from the EXE table). One game per process.
constexpr int kLeagueCount = 9;
inline uint8_t kLeagueSizes[kLeagueCount] = {18, 18, 18, 18, 18, 12, 12, 12, 12};
// Installs the league sizes of the loaded game; throws std::invalid_argument unless they
// add up to 138 (the league teams of every version) and every league has a fixture table (20, 18, 16, 12 or 10 teams).
void set_league_sizes(const uint8_t (&sizes)[kLeagueCount]);

// The calendar groups of the loaded game: competition bits 0-7 play the leagues
// kGroupLeague[bit] .. + kGroupLeagues[bit] - 1 (CalendarFirstLeague / CalendarLeagueCounts), and a league
// plays on calendar bit kLeagueBit[league] (LeagueCalendarBits). German: 3. Liga A and B share
// bit 2; English: every division has its own bit and the 10-team fifth league none (0xff
// in kLeagueBit, not played). Set with the league sizes from the EXE tables.
inline uint8_t kGroupLeague[8] = {0, 1, 2, 4, 5, 6, 7, 8};
inline uint8_t kGroupLeagues[8] = {1, 1, 2, 1, 1, 1, 1, 1};
inline uint8_t kLeagueBit[kLeagueCount] = {0, 1, 2, 2, 3, 4, 4, 4, 4};
void set_league_groups(const uint8_t (&first)[8], const uint8_t (&count)[8]);

// The promotion ladder of the loaded game (Void / English ClubName), set with
// the league sizes from the EXE tables:
// - kLeagueRelegation[league]: relegation places (CareerSession_InstallLeagueSizes_RuntimeField2; German 3 4 2 2 3, English
//   4 4 4 4 4),
// - kGroupBelow[group] .. + kGroupBelowCount[group] - 1: the league(s) a promotion group
//   takes its promoted clubs from (CareerSession_InstallLeagueSizes_RuntimeField5 / CareerSession_InstallLeagueSizes_RuntimeField4; German 1 | 2 | 4 with the
//   two Regionalligen in one group, English 1 | 2 | 3),
// - kLeagueSuffix[league]: the letter after a league's name (CareerSession_InstallLeagueSizes_RuntimeField3, two bytes per
//   league; German A/B for the Regionalligen, English none),
// - kNewcomerFirst / kNewcomerCount: the clubs the lowest league is refilled from
//   (CareerSession_DispatchLineup_RuntimeField / CareerSession_InstallLeagueSizes_RuntimeField6; German the 30 amateur clubs 138-167; the English game
//   draws from its 12 further English clubs 138-149 - its later ids have no name - so the
//   session counts only named clubs).
inline uint8_t kLeagueRelegation[kLeagueCount] = {3, 4, 2, 2, 3, 0, 0, 0, 0};
// European places per league for the table prediction (CareerSession_InstallLeagueSizes_RuntimeField; German 0 3 2 2 4 - league
// 0 uses 4 in the prediction -, English 0 4 4 4 4)
inline uint8_t kLeagueEurope[kLeagueCount] = {0, 3, 2, 2, 4, 0, 0, 0, 0};
inline uint8_t kGroupBelow[8] = {1, 2, 4, 5, 6, 7, 8, 0};
inline uint8_t kGroupBelowCount[8] = {1, 2, 1, 1, 1, 1, 1, 0};
inline char kLeagueSuffix[kLeagueCount] = {0, 0, 'A', 'B', 0, 0, 0, 0, 0};
inline int kNewcomerFirst = 0x8a, kNewcomerCount = 0x1e;

// The English competition structure (Football Limited): an FA Cup of 68 clubs with a
// qualifying stage (cup entries 0 and 28) and a League Cup (entries 25-27). Its cup memory
// lies partly in blocks of its own (career.hpp, english_competition_blocks). Set with the
// league sizes: a first league of 20 clubs is the English structure.
inline bool kEnglishCompetitions = false;

// First team id of a league (sum of the sizes of all previous leagues).
int league_first_team(int league);
// League of a team id, or kLeagueCount for teams outside league play (TeamLeague2).
int team_league(int team);
// The round-string table a league plays with (its team count): the original points leagues
// 0-4 at the table of the first league and 5-8 at the 12-team table (DE CareerFirstSteps2, EN
// LeagueTableTeams). The same as the league size except for the English fifth league (10 teams),
// which belongs to no calendar group and is never played.
int league_table_teams(int league);

struct Fixture {
    int home;  // team id
    int away;  // team id
};

// Matches of a league matchday (1-based), home/away as team ids. `rounds` is the number of
// rounds per half season, normally team count - 1 (the original passes matchdays / 2).
std::vector<Fixture> league_fixtures(int league, int matchday);

// Raw fixture generator, a direct port of GenerateFixtures:
//   teams      - league size, selects the round-string table
//   matchday   - 1-based
//   rounds     - rounds per half season (normally teams - 1)
//   first_team - id of the league's first team, added to every draw position
std::vector<Fixture> generate_fixtures(int teams, int matchday, int rounds, int first_team);

constexpr uint8_t kNotPlayed = 0xCC;
constexpr size_t kResultsSize = 0xD5C;

struct Result {
    bool played = false;
    int home_goals = 0;
    int away_goals = 0;
};

// Decodes one result byte of block LeagueResults.
inline Result decode_result(uint8_t v) {
    if ((v & 0xCF) >= 0xCC) return {};
    return {true, v >> 4, v & 0x0F};
}

// Index into the results block for a league match.
inline size_t result_index(int league, int matchday, int match) {
    return size_t(league) * 0x17C + size_t(matchday - 1) * 10 + size_t(match);
}

}  // namespace bmh
