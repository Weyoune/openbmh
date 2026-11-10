// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The dimensions of a game world: team id ranges and the squad slot table.
//
// The original game fixes them (and so does every supported edition, German and English):
//   ids   0..137  the 138 league clubs: 90 clubs of the five domestic leagues with 24 squad slots,
//                 48 clubs of the four international leagues with 16
//   ids 138..167  30 amateur clubs (cup newcomers), without squads
//   ids 168..356  189 foreign cup clubs without squads (a strength byte each)
//   id  357       the pseudo team "no club" (kNoTeam, 0x165): national team / player pool, 24 slots
//   ids 358..418  61 national teams
// and behind the squads the three player pools (transfer list, free agents, offers from abroad).
//
// Until 1.0.12 these numbers stood as literals all over the engine (138, 168, 0x165, 90 * 24, ...).
// WorldLayout names them in one place. OpenBMH Classic never installs another layout:
// classic_world_layout() returns exactly the original numbers, so every function that asks the
// layout computes what it computed before. A later edition with more leagues (OpenBMH Extended)
// installs its own layout instead of patching literals.
//
// One game per process, like the league tables of schedule.hpp.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bmh {

struct WorldLayout {
    // Team id ranges, in this order: league clubs, amateur clubs, squadless foreign cup clubs,
    // the pseudo team "no club", national teams.
    int league_teams = 138;       // ids 0 .. league_teams-1 have squads
    int domestic_teams = 90;      // the first league clubs (the five domestic leagues)
    int amateur_teams = 30;       // ids league_teams .. +amateur_teams-1, no squads
    int foreign_cup_teams = 189;  // squadless foreign clubs (27 nations x 7)
    int national_teams = 62;      // "no club" (the first of them) and the 61 national teams

    // Squad slot table: first slot and slot count per league club, then the pools.
    std::vector<uint32_t> squad_start;   // per league club
    std::vector<uint8_t> squad_size;     // per league club
    uint32_t transfer_list = 0xB70;      // 44 slots: players on the transfer list
    uint32_t free_agents = 0xB9C;        // 80 slots: players without a club
    uint32_t foreign_offers = 0xBEC;     // 20 slots: players offered from abroad
    uint32_t pool_end = 0xC00;           // end of the pools = first slot of "no club"
    uint32_t no_team_slots = 24;         // the squad of "no club"
    uint32_t total_slots = 3150;         // including the reserve behind "no club"

    int first_amateur() const { return league_teams; }
    // The first team without a squad of its own that is a foreign cup club (Classic: 168); the
    // clubs before it are "the clubs with league records" (league clubs + amateurs).
    int first_foreign_cup() const { return league_teams + amateur_teams; }
    int no_team() const { return first_foreign_cup() + foreign_cup_teams; }
    int total_teams() const { return no_team() + national_teams; }
    bool has_squad(int team) const { return (team >= 0 && team < league_teams) || team == no_team(); }
};

// The original dimensions (V2.24 and the supported German and English editions).
WorldLayout classic_world_layout();

// The layout of the running game (Classic unless another one was installed).
const WorldLayout& world_layout();

// Installs a layout; throws std::invalid_argument when its ranges or its slot table are inconsistent.
void set_world_layout(WorldLayout layout);

// The league structure of a game world: sizes, calendar groups, promotion ladder, cup structure.
//
// The German versions have 5 leagues of 18 and 4 of 12; the English Football Limited 20 20 20 20 10
// and the same four international leagues of 12 (both 138 league teams, so the squad layout is the
// same). Until 1.0.12 these tables were separate mutable globals of schedule.hpp (kLeagueSizes,
// kGroupLeague, ...). The values and every use are unchanged; the German ones are the default.
//
// Still fixed (open for an edition with more leagues): the number of leagues (kLeagueCount, the size
// of several tables of the save file), eight calendar groups, and the results block of schedule.hpp.
constexpr int kLeagueCount = 9;

struct LeagueSystem {
    // Number of teams per league (LeagueSizes of the loaded game). Leagues are contiguous id ranges.
    uint8_t sizes[kLeagueCount] = {18, 18, 18, 18, 18, 12, 12, 12, 12};

    // The calendar groups: competition bits 0-7 play the leagues group_first[bit] .. + group_count[bit] - 1
    // (CalendarFirstLeague / CalendarLeagueCounts), and a league plays on calendar bit calendar_bit[league]
    // (LeagueCalendarBits). German: 3. Liga A and B share bit 2; English: every division has its own
    // bit and the 10-team fifth league none (0xff in calendar_bit, not played).
    uint8_t group_first[8] = {0, 1, 2, 4, 5, 6, 7, 8};
    uint8_t group_count[8] = {1, 1, 2, 1, 1, 1, 1, 1};
    uint8_t calendar_bit[kLeagueCount] = {0, 1, 2, 2, 3, 4, 4, 4, 4};

    // The promotion ladder (Void / English ClubName):
    // - relegation[league]: relegation places (CareerSession_InstallLeagueSizes_RuntimeField2; German 3 4 2 2 3,
    //   English 4 4 4 4 4),
    // - below_first[group] .. + below_count[group] - 1: the league(s) a promotion group takes its promoted
    //   clubs from (CareerSession_InstallLeagueSizes_RuntimeField5 / CareerSession_InstallLeagueSizes_RuntimeField4;
    //   German 1 | 2 | 4 with the two Regionalligen in one group, English 1 | 2 | 3),
    // - suffix[league]: the letter after a league's name (CareerSession_InstallLeagueSizes_RuntimeField3, two
    //   bytes per league; German A/B for the Regionalligen, English none),
    // - newcomer_first / newcomer_count: the clubs the lowest league is refilled from
    //   (CareerSession_DispatchLineup_RuntimeField / CareerSession_InstallLeagueSizes_RuntimeField6; German the 30
    //   amateur clubs 138-167; the English game draws from its 12 further English clubs 138-149 - its later
    //   ids have no name - so the session counts only named clubs).
    uint8_t relegation[kLeagueCount] = {3, 4, 2, 2, 3, 0, 0, 0, 0};
    // European places per league for the table prediction (CareerSession_InstallLeagueSizes_RuntimeField;
    // German 0 3 2 2 4 - league 0 uses 4 in the prediction -, English 0 4 4 4 4)
    uint8_t europe[kLeagueCount] = {0, 3, 2, 2, 4, 0, 0, 0, 0};
    uint8_t below_first[8] = {1, 2, 4, 5, 6, 7, 8, 0};
    uint8_t below_count[8] = {1, 2, 1, 1, 1, 1, 1, 0};
    char suffix[kLeagueCount] = {0, 0, 'A', 'B', 0, 0, 0, 0, 0};
    int newcomer_first = 0x8a, newcomer_count = 0x1e;

    // The English competition structure (Football Limited): an FA Cup of 68 clubs with a qualifying
    // stage (cup entries 0 and 28) and a League Cup (entries 25-27). Its cup memory lies partly in
    // blocks of its own (career.hpp, english_competition_blocks). A first league of 20 clubs is the
    // English structure.
    bool english_competitions = false;
};

// The league structure of the running game. Read through league_system(); the installer of a game
// version (app::CareerSession, set_league_sizes / set_league_groups of schedule.hpp) writes through
// installed_league_system(). One game per process.
inline LeagueSystem g_league_system;
inline const LeagueSystem& league_system() { return g_league_system; }
inline LeagueSystem& installed_league_system() { return g_league_system; }

}  // namespace bmh
