// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The football almanac of a Modern career (docs/new_engine/almanach.md, data format F10).
//
// Every match the engine plays is kept: season, date, competition, matchday or cup stage,
// both clubs with their human managers, the result (extra time, penalties), referee,
// attendance, weather, both line-ups and every event with its minute (goals with the
// assistant, cards, injuries, substitutions, shoot-out kicks). The almanac only watches:
// it draws no random numbers and changes nothing the season core reads.
//
// Identities are the stable ones of the world (club and player ids), not runtime indices:
// clubs swap their runtime index on promotion, and a player index is reused for a new
// person when a player retires or a youth player is created (new_player_identity). Each
// season keeps its own tables of the clubs, players, managers and referees it mentions, so a
// finished season is self-contained and never changes again; a save holds every finished
// season as one compact binary file (history/season-<year>.bin) plus the running one.

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace bmh {
struct MatchGroup;
struct ShowEvent;
}

namespace bmh::modern {

struct ModernState;

enum class AlmanacEventKind : uint8_t {
    Goal = 0,          // player scorer, other assistant (or none), detail: chance kind (0 open
                       // play, 1 penalty / free kick, 2 follow-up, 3 corner)
    YellowCard = 1,
    RedCard = 2,
    SecondYellow = 3,  // yellow-red
    Injury = 4,
    Substitution = 5,  // player comes on, other leaves (none when the slot was empty)
    ShootoutKick = 6,  // player takes the kick, detail 1 = scored
    Miss = 7,          // a chance without a goal: player shooter, other assistant, detail kind
};

constexpr uint16_t kAlmanacNone = 0xffff;  // no player in an event or line-up entry

struct AlmanacEvent {
    AlmanacEventKind kind = AlmanacEventKind::Goal;
    uint8_t minute = 0;   // 1-based match minute (121 for the shoot-out)
    uint8_t side = 0;     // 0 home, 1 away (the acting team)
    uint8_t detail = 0;
    uint16_t player = kAlmanacNone, other = kAlmanacNone;  // indices into the season's players

    bool operator==(const AlmanacEvent&) const = default;
};

// Competition of a match: a league (0-8 in Classic order, the world's league ids in the
// season header) or a cup (the cup id of the cup table: German cup, European cups, national
// team competitions, friendlies, indoor tournament, ...), with its stage entry.
struct AlmanacMatch {
    uint32_t date = 0;          // day counter of the calendar (almanac_date)
    uint8_t type = 0;           // descriptor type: 0 league, else cup id + 1
    uint8_t league = 0xff;      // league index for type 0
    uint8_t stage = 0xff;       // cup stage entry (cup table index) for cups
    uint8_t round = 0;          // league matchday 1.., or the round within the cup stage 1..
    uint16_t home = 0, away = 0;            // indices into the season's clubs
    int8_t home_manager = -1, away_manager = -1;  // indices into the season's managers, -1 computer
    uint8_t home_goals = 0, away_goals = 0;       // after regular / extra time, without the shoot-out
    uint8_t home_shootout = 0, away_shootout = 0; // shoot-out goals (decided == 2)
    uint8_t decided = 0;        // 0 regular time, 1 extra time, 2 penalties
    uint8_t referee = 0;        // index into the season's referees (0 = none)
    uint32_t attendance = 0;
    int8_t temperature = 0;     // degrees Celsius
    uint8_t rain = 0;           // 0-7 as the engine draws it (snow when cold)
    std::array<std::vector<uint16_t>, 2> lineup;  // home, away: 11 starters, then the bench
    std::vector<AlmanacEvent> events;              // in match order

    bool operator==(const AlmanacMatch&) const = default;
};

struct AlmanacClub {
    std::string id, name;       // world club id (or team:<n> without a world), UTF-8 name
    bool operator==(const AlmanacClub&) const = default;
};
struct AlmanacPlayer {
    std::string id, first, last, nation;
    bool operator==(const AlmanacPlayer&) const = default;
};
struct AlmanacManager {
    int index = -1;             // manager slot of the career (human managers)
    std::string name;
    bool operator==(const AlmanacManager&) const = default;
};
struct AlmanacLeague {
    int index = -1;
    std::string id, name;
    bool operator==(const AlmanacLeague&) const = default;
};

// A row of a final league table (in rank order).
struct AlmanacStanding {
    uint16_t club = 0;          // index into the season's clubs
    uint8_t rank = 0;           // 1 = champion (equal ranks are possible as the engine ranks them)
    uint8_t played = 0, won = 0, drawn = 0, lost = 0;
    uint8_t goals_for = 0, goals_against = 0;
    int16_t points = 0;         // after a point deduction of a human manager
    bool operator==(const AlmanacStanding&) const = default;
};
struct AlmanacTable {
    uint8_t league = 0;         // league index (season header: id and name)
    std::vector<AlmanacStanding> rows;
    bool operator==(const AlmanacTable&) const = default;
};

// The winner of a competition, noted when it is decided: league champions from the final
// tables, cups when their last stage ends (single final: the loser is the runner-up; a final
// group, e.g. the indoor tournament: its second).
struct AlmanacWinner {
    uint8_t type = 0;           // as AlmanacMatch: 0 league, else cup id + 1
    uint8_t league = 0xff;      // league index for type 0
    uint8_t stage = 0xff;       // the last cup stage (cup table index)
    uint8_t detail = 0;         // 1 = the indoor Masters (after the five tournaments)
    uint32_t date = 0;          // day counter
    uint16_t winner = 0, runner_up = 0xffff;  // indices into the season's clubs (0xffff none)
    bool operator==(const AlmanacWinner&) const = default;
};

struct AlmanacSeason {
    int season = -1;            // seasons played before (GameState::season)
    int start_year = 0;         // calendar year the season starts in
    int display_start_year = 0; // year of day counter 0 (almanac_date)
    std::vector<AlmanacLeague> leagues;
    std::vector<AlmanacClub> clubs;
    std::vector<AlmanacPlayer> players;
    std::vector<AlmanacManager> managers;
    std::vector<std::string> referees;   // index 0 = none
    std::vector<AlmanacMatch> matches;   // in the order played
    std::vector<AlmanacTable> tables;    // final league tables, at the end of the season
    std::vector<AlmanacWinner> winners;  // in the order decided

    bool operator==(const AlmanacSeason&) const = default;

    // Index of an entry, appending it when it is new (the lookups are rebuilt after decoding).
    uint16_t club(const AlmanacClub& c);
    uint16_t player(const AlmanacPlayer& p);
    int8_t manager(const AlmanacManager& m);
    uint8_t referee(const std::string& name);
    void index();               // rebuilds the lookups from the tables

private:
    std::map<std::string, uint16_t> club_at_, player_at_;
    std::map<std::string, uint8_t> referee_at_;
};

struct Almanac {
    // Finished seasons, encoded (encode_almanac_season) and never changed again: the file
    // name in the save and its bytes.
    std::vector<std::pair<std::string, std::string>> finished;
    AlmanacSeason current;
    // Counter of the identities new_player_identity gave out (saved with the career).
    uint32_t generated_players = 0;

    bool operator==(const Almanac& o) const {
        return finished == o.finished && current == o.current && generated_players == o.generated_players;
    }
};

struct AlmanacDate { int year = 0, month = 0, day = 0; };
// The calendar date of a day counter (day 0 = 1 July of display_start_year), as the desk shows it.
AlmanacDate almanac_date(uint32_t counter, int display_start_year);

// Binary form of a season (format version 1, little-endian; docs/new_engine/almanach.md).
std::string encode_almanac_season(const AlmanacSeason& season);
// Throws FormatError when the bytes are not an almanac season of a known version.
AlmanacSeason decode_almanac_season(const std::string& bytes);

// A player index now belongs to a new person (retirement or youth player): gives it a new
// stable identity so the almanac does not merge the two careers.
void new_player_identity(ModernState& g, int player);

// --- Season results -----------------------------------------------------------------------
// The final tables of every league and their champions; the season core calls it once the
// leagues are complete, before promotion moves the clubs.
void almanac_final_tables(ModernState& g);
// A cup is decided (its last stage ended): cup id, last stage, winner and runner-up (-1 none).
void almanac_cup_winner(ModernState& g, int cup, int stage, int winner, int runner_up, bool masters = false);

// --- Recording (the Modern turn driver) ---------------------------------------------------
// One matchday as the turn driver plays it: begin after kick-off (line-ups, rounds), the
// minutes for substitutions, finish with the match engine's show events before the
// matchday's results are processed.
class AlmanacRecorder {
public:
    AlmanacRecorder(ModernState& g, const std::vector<MatchGroup>& groups);
    void after_minute(int minute);
    void finish(const std::vector<ShowEvent>& events);

private:
    struct Running {
        size_t group = 0;
        int match = 0;
        AlmanacMatch record;
        std::array<std::vector<uint16_t>, 2> eleven;  // runtime players on the pitch per side
    };
    ModernState& g_;
    const std::vector<MatchGroup>& groups_;
    std::vector<Running> running_;
    std::vector<AlmanacEvent> pending_;  // substitutions found by after_minute, per running match
    std::vector<size_t> pending_match_;

    uint16_t player_ref(int player);
    std::vector<uint16_t> on_pitch(int team) const;
};

}  // namespace bmh::modern
