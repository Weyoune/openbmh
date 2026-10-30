// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#pragma once
// OpenBMH deviates from the original: uses the supported player-generation ranges and excludes reserved calendar positions when assigning managers.
// The start of a new career (BMH.exe V2.24 + supported rule profile patch).
//
// The original starts every career at program start: CareerFirstSteps2 loads the data files
// (TEXTD.BLK, M3.MEN, M3.DAT, M3D.MAN), gives every player new values and then calls
// InitializeCareer, which sets up shares, funds, club moods, ages and skills, shows the
// options and manager dialogs (SetupScreen2) inside ConfirmManager and finally prepares the
// first season. Loading a savegame from the options screen replaces all of that.
//
// The game generator starts at 0 (CareerSession_Save_RuntimeField is not initialised), the Watcom generator
// at the clock value of srand() - recordings capture it.

#include <cstdint>
#include <utility>
#include <string>

#include "formats/m3d.hpp"
#include "game/match_engine.hpp"
#include "game/random.hpp"
#include "game/state.hpp"

namespace bmh {

// Install the nine strength ranges from the loaded edition, never a shared replacement.
void install_career_rules(const LeImage&);
std::pair<int,int> career_strength_range(int league);

// OpenBMH deviates from the original: uses the supported player-generation ranges and excludes reserved calendar positions when assigning managers.
// GeneratePlayer with the supported rule profile patch GeneratePlayer2: new values for player `id` around
// `level` (low 12 bits of `level_flags`; 0x1000 = leave the age at 0, 0x2000 = exactly
// the level instead of +-10 %):
//   - w35c bits 21-26: G(0, 7) << 3 | G(0, 7) for field players, 0x18 for goalkeepers
//     (two game draws, high part first);
//   - condition and technique G(level - level / 10, level + level / 10), each cut to
//     10..99; form G(35, 65); bits 27-31 cleared;
//   - w368: skills W(0, 0x3fff), bits 18-23 (cards) cleared;
//   - w364: age bits cleared, then (without 0x1000) G(2, 5) = 18..21 years.
void generate_player(GameState& g, int id, int level_flags, GameRandom& game, WatcomRandom& rand);

// The part of CareerFirstSteps2 after loading M3D.MAN: the 54 players of the extra slots
// (CareerSession_DataDirectory_RuntimeField4 ..) get W(20 or 70, 90) as level (the first 22 from 20), then the players
// of every league club get the level of its place in the database order of its league,
// from the league's maximum (first club) down towards its minimum (LeagueStrengthRanges pairs,
// club counts LeagueSizes), keeping the age at 0 for InitializeCareer.
void startup_players(GameState& g, GameRandom& game, WatcomRandom& rand);

// The squad list as CareerFirstSteps2 leaves it: all slots 0xfff, the database list read
// over them and its tail (from slot CareerRuntimeField + 40) moved up to slot CareerRuntimeField2
// (memmove; the four slots before keep their values).
std::vector<uint16_t> startup_squad_list(const Database& db);

}  // namespace bmh

namespace bmh {

class LeImage;

// The memory of the original after loading the data files in CareerFirstSteps2, as a GameState
// (built like a savegame: every saved block from the program's data object - zero where
// it is uninitialised - with the data of M3.MEN, M3.DAT and M3D.MAN read over it):
// manager order 0..3, option byte 0 of each manager 1, active manager 4, M3.MEN five
// times (Desktops), the first 0x160 bytes of M3.DAT four times (the other three
// parts of the file are overwritten), the nationality of the last 32 players from
// CareerRuntimeField7. The player values are those of the database; startup_players follows.
GameState startup_state(const LeImage& exe, const Database& db, const Bytes& m3_men, const Bytes& m3_dat);

// CompetitionPointers (part of startup_state): the pointers of the competition table CupTable
// (36 entries of 0x28 bytes: +0 the teams, +8 the rounds, +0xc the results) to the tables of
// the leagues, cups and tournaments.
void competition_pointers(StaticMemory& mem, const LeImage& exe);

// The memory of the English-only competitions (Football Limited, kEnglishCompetitions):
// blocks at their English addresses, which no German address uses. FA Cup results (67
// matches) and fields (68 clubs: the 20 Premier League clubs, the 48 of the qualifying
// stage, the proper rounds), the League Cup (44 clubs, three entries) and entry 35.
constexpr uint32_t kEnglishFaResults = ::bmh::layout::EnglishFaCupResults;         // 0x86 bytes, entry 0 +0x0c
constexpr uint32_t kEnglishFaField = ::bmh::layout::EnglishFaCupField;           // 68 clubs: 20 + kEnglishFaQualifyingField
constexpr uint32_t kEnglishFaQualifyingField = ::bmh::layout::EnglishFaCupQualifyingField; // the 48 of the qualifying stage (entry 0)
constexpr uint32_t kEnglishLeagueCupField = ::bmh::layout::EnglishLeagueCupField;    // 44 clubs: 20 + 24 at layout field (entry 25)
constexpr std::pair<uint32_t, uint32_t> kEnglishCompetitionBlocks[] = {
    {::bmh::layout::EnglishFaCupResults, 0x1ba},  // FA Cup: results EnglishFaCupResults, stage results EnglishFaCupStageResults, field EnglishFaCupField, proper EnglishFaCupProperField
    {::bmh::layout::EnglishLeagueCupField, 0x136},  // League Cup: fields EnglishLeagueCupField/EnglishLeagueCupFirstRoundField/EnglishLeagueCupQuarterFinalField/EnglishLeagueCupSemiFinalField, results EnglishLeagueCupFirstRoundResults/EnglishLeagueCupQuarterFinalResults/EnglishLeagueCupSemiFinalResults/EnglishLeagueCupQuarterFinalOtherLegResults
    {::bmh::layout::EnglishAdditionalCupResults, 0x48},   // entry 35: results EnglishAdditionalCupResults, field EnglishAdditionalCupField
};
void english_competition_blocks(StaticMemory& mem);

}  // namespace bmh

namespace bmh {

// InitializeCareer up to the options dialog (SetupScreen2, called first in ConfirmManager(0)):
//   - funds (price (1 << k) * 100 000) and shares (value k * 2000 + 10 000, price
//     k * 20 + 100);
//   - stir_random; G(0, 1) chooses which of the two computer manager names (Career_NewCareerMarketsAndMoods_ExecutableField /
//     aeafc) the records 0 and 1 get, records 2 and 3 get Career_NewCareerMarketsAndMoods_ExecutableField3 / Career_NewCareerMarketsAndMoods_ExecutableField4 (needs
//     g.texts; skipped without);
//   - 30 days of share prices (stock_market);
//   - Titles (900 bytes) 0xff, Career_NewCareerMarketsAndMoods_RuntimeField2 = 1, 2, 4, 8, the transfer list's 44
//     entries empty (player 0xfff);
//   - club moods (reset_club_moods, career mode);
//   - every club player (squad slots 0 .. CareerRuntimeField) gets an age W(4, 16) and new
//     skills W(0, 0x3fff);
//   - the 54 extra players leave for the free agents (release_player, MonthChange).
// Returns false if a step is not supported (report).
bool new_career_before_dialog(GameState& g, GameRandom& game, WatcomRandom& rand, MinuteReport& report);
// Its two parts: up to the club moods, and the ages, skills and extra players.
void new_career_markets_and_moods(GameState& g, GameRandom& game, WatcomRandom& rand);
bool new_career_players(GameState& g, GameRandom& game, WatcomRandom& rand, MinuteReport& report);

}  // namespace bmh

namespace bmh {

// A manager as confirmed in his dialog (ManagerScreen2; the UI's ManagerChoice): the dialog
// writes name (record +0xa8), club (+0x290), start league 0..4 (+0x210) and portrait
// (+0x221) into the record and returns the strength 0 (lower third) .. 2 (upper third).
struct CareerManager {
    std::string name;  // ISO 646-DE bytes, at most 24
    int team = 0;      // club id at the moment of the confirmation
    int league = 0;    // start league 0..4
    int strength = 1;  // 0 lower, 1 middle, 2 upper third
    int portrait = 0;  // 0..11
};

// ConfirmManager before the first dialog: manager slot 4 (the national team's) gets the
// training plan 2 and 1 000 000 money.
void career_managers_begin(GameState& g, bool adding = false);

// StartingSponsors initializes the three sponsor contracts for mode I, or for a
// one-year career before season day 0. The date check uses GameState.date,
// because the serialized CalendarDay mirror can be stale during startup/reset.
void starting_sponsors(GameState& g, int manager);

// OpenBMH deviates from the original: uses the supported player-generation ranges and excludes reserved calendar positions when assigning managers.
// The body of ConfirmManager's loop for manager `m` (`adding` = a manager joins a running
// game, param_1 != 0): the record is prepared and the dialog's values entered, then
//   - the club moves to its place: in a league other than the chosen start league it
//     changes places with a club of that league (G(first, first + 12), supported rule profile ReservedPlace2
//     / ReservedPlace skip clubs of humans and reserved places), which gets new values
//     (CareerAddManager2(club, 0)); in the first league with a club place m + 1 (m + 9 when
//     adding; supported rule profile CareerAddManager4 skips reserved and human places);
//   - the club's players get condition and technique around the chosen strength
//     (CareerAddManager2, W(level - 6, level + 6));
//   - record values: morale flag, board patience, stadium (CareerAddManager3, StartingSponsors),
//     training plan, ratings, money (4 000 000 - level * 200 000; 50 000 000 at level 0),
//     credit line, the first 20 players (or the whole squad when adding) on the tactics
//     board with a new age W(4, 9), and an automatic line-up.
// The optional file of CareerAddManager (bonus codes) is not read. Needs the program image
// for its tables (Career_BuildStadium_RuntimeField4, Career_BuildStadium_RuntimeField, Career_BuildStadium_RuntimeField2).
bool career_add_manager(GameState& g, const LeImage& exe, int m, const CareerManager& choice, bool adding,
                        GameRandom& game, WatcomRandom& rand, MinuteReport& report);

// ConfirmManager after the loop: clear CareerSession_FormatError_RuntimeField6; only a new career empties the twelve
// human loan entries Career_CareerManagersEnd_RuntimeField. Joining preserves unrelated entries.
void career_managers_end(GameState& g, bool adding);

}  // namespace bmh
