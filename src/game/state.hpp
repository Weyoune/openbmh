// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// In-memory game state used by the game logic.
//
// The original keeps its state in many global tables; GameState collects the ones the
// ported logic needs, decoded into typed vectors. It is filled from a savegame
// (SaveState) and will later be written back the same way.

#include <cstdint>
#include <array>
#include <functional>
#include <string>
#include <vector>

#include "formats/m3d.hpp"
#include "formats/savestate.hpp"
#include "formats/textd.hpp"
#include "game/memory.hpp"
#include "game/random.hpp"
#include "game/reality_motion.hpp"

namespace bmh {

struct TransferOfferListView;
struct TransferDecisionEvent;
struct HumanBuyerSaleHooks;

// Special team id used by the original for "no club" / the player pool (0x165).
constexpr int kNoTeam = 0x165;

// Squad slot values (u16 in table *SquadSlots):
//   bits 0-11  player id, 0xFFF = empty slot
//   bit  15    player is in the starting line-up
//   bits 12-14 further flags (not yet decoded)
constexpr uint16_t kEmptySlot = 0x0FFF;
constexpr uint16_t kSlotInLineup = 0x8000;

struct GameState {
    std::vector<uint16_t> squad_slots;  // *SquadSlots, all squads back to back (see squad_range)
    std::vector<Player> players;        // *PlayerConditionTechniqueForm / *PlayerSkillWords / *PlayerIdentityWords, names not filled
    std::vector<uint8_t> team_morale;   // *TeamMorale, per team: bits 0-5 morale / 2, bits 6-7 flags
    // *TeamFlags, per team u16. Bits 8-9 and 10-11 hold two formation numbers (0-3); bit 4
    // selects which one is used (see formation()). Other bits not yet decoded.
    std::vector<uint16_t> team_flags;
    // *PlayerBonuses, per player: bits 0-6 a bonus value (0-50 used, raises the player's rating by
    // up to 25 %; probably experience/routine), bit 7 a flag.
    std::vector<uint8_t> player_bonus;
    // Block Formations = contents of M3.DAT: formation tables, 2 sets (outdoor, indoor) of
    // 0xB0 bytes = 8 formations x (11 x positions, 11 y positions). The engine uses 4.
    std::vector<uint8_t> formations;
    // Team id managed by each human manager (manager record +0x290, CupDraw_FormatError_RuntimeField2), one entry
    // per human manager (ManagerCount).
    std::vector<int> manager_teams;
    bool indoor = false;  // OpenBMH deviates from the original: retains designated set-piece selections in the supported manager-record extension.
// CareerSessionRuntimeField: indoor tournament in progress
    // Block ManagerRecords: 5 manager records of 0xB0C bytes (raw). Known fields: +0x290 team id,
    // +0x8ae staff value used by the club doctor, and from the supported rule profile patch the designated
    // takers (player id + 1, u16) +0xafc penalty, +0xafe penalty follow-up, +0xb08 free
    // kick, valid only if +0xb0a holds the marker 0x3253.
    std::vector<uint8_t> manager_records;
    // Block TacticsBoards: tactics board of each human manager, 0x840 bytes = 24 slots of 0x58
    // bytes. Slot byte +0x41 = depth x, +0x42 = lane y (signed; x >= 31 and y >= 31 are
    // positions between the grid lines), +0x43 = a flag read by the injury code.
    std::vector<uint8_t> tactics;
    // Block ManagerAutomation: 5 option bytes per human manager. Byte 1 (StateRuntimeField2): the assistant
    // coach may substitute; byte 2 (StateRuntimeField3): the assistant coach sets the attitude.
    std::vector<uint8_t> manager_options;
    bool manager_option(int manager, int index) const {
        size_t at = size_t(manager) * 5 + size_t(index);
        return at < manager_options.size() && manager_options[at] != 0;
    }
    // OpenBMH deviates from the original: retains designated set-piece selections in the supported manager-record extension.
// CurrencySuffixText: the currency after a formatted amount, from the loaded EXE (supported rule profile " EU",
    // CD and CLASSIC " DM", the English version " &" - the pound sign of its font). Set by the
    // career session; " EU" until then.
    std::string currency_suffix = " EU";
    // Runtime-only resort content, loaded for every career and saved-game session.
    // Direct engine callers supply their own content before using training camps.
    std::array<int32_t, 8> training_camp_base_prices{};
    std::array<std::array<uint8_t, 4>, 8> training_camp_quality{};
    // Runtime-only copy of the executable credit factors (EXE +BankCredit_BankCreditContext_RuntimeField3), used by
    // ChangeBankCreditInterest during weekly manager offers. Never serialized into FBM.
    std::array<uint8_t, 49> credit_rate_table{};
    double credit_divisor = 0;
    bool credit_rate_table_available = false;
    int level = 0;  // Difficulty: game level; above 3 the tactics board rates players
    // ChanceRain / ChanceTemperature: rain and temperature (descriptor +0x50 / +0x30) of the
    // match whose chance the minute loop handled last. player_value reads them (bad weather
    // and heat cost players whose condition and technique differ), also when the side
    // records of the next matchday are built - the values stay from the last chance.
    // Run-time globals, not saved.
    int chance_rain = 0;
    int chance_temperature = 0;
    // MatchEngineRuntimeField17: the shooter of the last real chance (0xFFF none); the penalty
    // shoot-out still reads it. Run-time global, not saved.
    int last_shooter = 0xFFF;
    // MatchEngineRuntimeField: quality of the last player picked (PickTaker), also read later.
    int last_pick_quality = 0;
    // LiveSceneVisible: the live scene is shown this matchday (1 - option CareerSession_DispatchLiveTactic_RuntimeField3, set
    // when the matchday starts). Run-time global, not saved.
    bool live_scene = false;
    RealityResidue reality; // unsaved MatchEngine_Minute_ExecutableField2/MatchEngine_Minute_ExecutableField/MatchEngine_Minute_ExecutableField3/MatchEngine_Minute_ExecutableField4 and previous grid
    // StateRuntimeField4 / MatchdaySetupRuntimeField12: home teams of the matches chosen for the live display at
    // kick-off (AutoLineup, at most 4). Run-time globals, not saved.
    std::vector<int> live_matches;
    // ResetSportschauSelection: unsaved bd125 words / bd139 bytes, including insertion slot9.
    // Reset clears priorities only; home words deliberately survive between days.
    std::array<uint16_t, 10> sportschau_homes{};
    std::array<uint8_t, 10> sportschau_priorities{};
    bool sportschau_selection_available = false;
    uint16_t sportschau_club_threshold = 90; // executable TeamsWithSquads, supplied by CareerSession
    // MonthRuntimeField7: alternates 0/1 each time a computer club is looked for to put a
    // player on the transfer list (Int5). Run-time static, starts at 0.
    int list_toggle = 0;
    // LineupRuntimeField: set while the players age at the season change (CareerSeasonSetup3); the
    // automatic line-up (Int2) does nothing then.
    bool lineups_frozen = false;
    // InstallDefaultHooks(row): the contracts screen a human manager gets in the month change when
    // the contract of the player in board row `row` has ended. Returns 1 when that contract
    // was renewed (the screen may change other rows too), 0 when the player goes, -1 to stop
    // (MinuteReport). Without it the month change reports the contract as not supported.
    std::function<int(GameState&, int manager, int row)> contract_screen;
    // DayStepRest2's borrower decision when an expiring loan can be kept: 1 = yes,
    // 0 = no, negative = no dialog owner / abort before the month transaction commits.
    std::function<int(GameState&, int manager, const std::string& question)> loan_return_screen;
    // DayStepRest2's positive Month_ReturnLoans_TextPointer6 branch enters OpenTransferEditor on a temporary
    // special row. The runtime owner must provide the real editor/settlement path.
    std::function<int(GameState&, GameRandom&, WatcomRandom&, int manager,
                      int source_slot, int list_entry, const HumanBuyerSaleHooks&)> loan_return_offer_screen;
    // How a human manager answers the JA/NEIN questions of the weekly offers (Bool,
    // kinds 5, 8 and 10-15): false = NEIN.
    bool offer_answer = false;
    // Human transfer negotiation at the weekly list deadline. The callback receives the
    // seller-manager slot, player, buyer club, fee and annual wage; absent a controller,
    // an explicitly human-owned selling club declines rather than silently transferring.
    std::function<bool(GameState&, int seller_manager, int player, int buyer, int32_t fee, int32_t salary)> transfer_decision;
    // Optional synchronous human-seller selector for the original multi-bid screen
    // (IncomingTransferScreen). Return a stable raw bid slot 0..3 or -1 to decline. The legacy
    // bool callback above remains the default path when this presenter is absent.
    std::function<int(const GameState&, const TransferOfferListView&)> transfer_offer_selector;
    // Read-only presentation notification for IncomingTransferScreen's automatic decision
    // animation and the subsequent successful-transfer confirmation. It never
    // supplies a game decision; ranking, acceptance and transfer remain in Core.
    std::function<void(const TransferDecisionEvent&)> transfer_decision_observer;
    // DeskActions_EditorFail_RuntimeField2 gates the original explicit-decline notice in IncomingTransferScreen.
    // Runtime context only; it is not a save field.
    bool transfer_notice_suppressed = false;
    // PoolsOpenFlag == 0xcc: the pools coupon of the desk (BuildPoolsCoupon) is not evaluated
    // yet. The desk sets it (also again after loading a game there); EvaluatePoolsCoupon(0) at the
    // end of the next second half evaluates the coupon and overwrites the byte, so later
    // matchdays leave the results alone.
    bool pools_open = true;
    // DeskRuntimeField5 / SeasonRuntimeField27: the autosave counter of the desk (DeskDate): months
    // seen at the first manager's desk and the month code last seen. Run-time statics:
    // a freshly started game has 0/0, so its first desk already counts as a new month.
    int autosave_months = 0;
    int autosave_month = 0;

    // --- matchday bookkeeping (see matchday.hpp) ------------------------------------------
    // *ClubSeasonRecords: league record per club, 0x4a bytes (168 clubs):
    //   +0x00  7 streaks x 3 bytes (all, home, away): 0 wins, 1 defeats, 2 draws,
    //          3 without a win, 4 unbeaten, 5 without conceding, 6 without scoring
    //   +0x15  8 record results (home goals << 4 | away goals), see matchday.cpp
    //   +0x1e  points, +0x20 goals for, +0x22 goals against, +0x24 wins, +0x26 draws,
    //          +0x28 defeats (u16 each)
    //   +0x2a  8 x u16 opponent of the record (club_ids of the opponent)
    //   +0x3a  8 x u16 date of the record (CalendarDay, 0xFFFF at most)
    std::vector<uint8_t> club_table;
    std::vector<uint16_t> club_ids;       // *ClubIdentities, per club (a name/identity number)
    // Further heap tables per club, so far only moved when clubs change leagues
    // (ClubsChangePlaces): *ClubAuxiliaryWords (u16 per club, 168), *ClubBadgeSelectors (u16 per team, 419), *ClubAuxiliaryBytes (168 bytes)
    std::vector<uint8_t> club_heap_bc, club_heap_78, club_heap_b8;
    // *MasterDatabaseMetadata (100 bytes from M3D.MAN) and the savegame trailer pools 2 (referees, *ba370) and
    // 3 (stadiums, *ba360): not used by the engine, kept for writing savegames.
    std::vector<uint8_t> master_database_metadata, referee_names, stadium_names;
    std::vector<uint16_t> player_cards;   // *PlayerCards, per player: card counters per competition
    uint32_t date = 0;                    // CalendarDay: day counter
    // DaysIntoSeason (day of the season, the calendar index) moves with the day counter within
    // a season: season_day() = date - season_start.
    uint32_t season_start = 0;
    // *TournamentName (0x24 bytes, the text slot 590 rewritten at run time): the name of the
    // indoor tournament, "In " followed by the longest word of the host club's name.
    std::vector<uint8_t> tournament_name;
    int season_day() const { return int(date) - int(season_start); }
    int32_t development_balance = 0;      // PlayerDevelopmentBalance: sum of attribute changes (steers trends)
    int trend_rotation = 0;               // PlayerTrendRotation: next team trend handed out (0-2)
    // ManagerMatchHistory: per human manager and opponent club the last results, 5 per venue
    // (0x690 bytes per manager = 168 clubs x 2 venues x 5, home | away << 4 as seen by
    // the manager's club).
    std::vector<uint8_t> manager_results;
    // ManagerRatings: per manager 6 competitions x the last 10 match ratings (0-200); their
    // weighted average is the manager's performance (record +0x8aa).
    std::vector<uint8_t> manager_ratings;
    // *LeagueRankHistory: table position history, 13 u16 per team, three 5-bit positions each;
    // LeagueRoundCounters: number of positions recorded per league (9 leagues).
    std::vector<uint8_t> position_history;
    std::vector<uint8_t> league_rounds;
    // Block LeagueResults: season results, 9 leagues x 38 matchdays x 10 matches (schedule.hpp).
    std::vector<uint8_t> results;
    int season = 0;  // SeasonIndex: seasons played (index of the per-season manager records)
    // Block StockExchange: 12 shares of 0x5c bytes (price, history, trend) for the stock market.
    std::vector<uint8_t> stocks;
    // ForeignClubStrength: strength byte of each club without a squad (ids from 168 on, the
    // foreign clubs): bits 5-7 the level (table RatingRuntimeField2), bits 1-3 strong lines, bit 0 style.
    std::vector<uint8_t> foreign_strength;
    // The other static tables of the savegame, addressed like in the original (cups, ...).
    StaticMemory memory;
    // *ManagerMailbox: the managers' mailbox (40 entries of 0xA0 bytes, see messages.hpp).
    std::vector<uint8_t> mailbox;
    // TEXTD.BLK for the texts of messages (not part of the savegame; may be null).
    const TextDb* texts = nullptr;
    // *ba374: the club name pool (trailer pool 0 of the savegame); *ClubIdentities (club_ids)
    // holds each club's offset into it.
    std::vector<uint8_t> club_names;
    // ClubName2: name of a club (0xFFF gives the text NoClubText, needs `texts`).
    std::string club_name(int team) const;
    // *PlayerNameOffsets / *ba36c: offset of each player's name in the player name pool (pool 1).
    std::vector<uint16_t> player_name_offsets;
    std::vector<uint8_t> player_names;
    // PlayerName for a player id (0xFFF gives the text NoPlayerText, needs `texts`).
    std::string player_name(int player) const;

    static GameState from_save(const SaveState& save, const DbCounts& counts = {});

    // Human manager index of a team, or -1 for computer teams (FindManagerForClub, without the
    // special case for manager slot 4).
    int manager_of(int team) const;
    // Formation number (0-3) a computer team plays with (used by FormationPosition).
    int formation(int team) const;
    // Formation position of line-up slot `slot` (0-10): x = depth (0 goalkeeper .. ~21
    // forward line), y = lane (0-8, 4 = centre). Human teams read their tactics board.
    void formation_position(int team, int slot, int& x, int& y) const;
};

// Location of a team's squad inside squad_slots (SquadRange4):
//   teams 0..89 (big squads)   : 24 slots at team * 24
//   teams 90..137 (small)      : 16 slots at team * 16 + 90 * 8
//   kNoTeam                    : 24 slots at 3072
struct SquadRange {
    size_t first = 0;
    size_t count = 0;
};
SquadRange squad_range(int team);

// Selection and value flags of team_average (the "param_2" of QuoteNationalFriendlyMatch).
namespace avg {
constexpr uint16_t CONDITION = 0x001;  // KO
constexpr uint16_t TECHNIQUE = 0x002;  // TE
constexpr uint16_t FORM = 0x004;       // FO
constexpr uint16_t MORALE = 0x008;     // team morale (same value for every player)
constexpr uint16_t GOALKEEPER = 0x010; // position filters, bit (position + 4)
constexpr uint16_t DEFENDER = 0x020;
constexpr uint16_t MIDFIELDER = 0x040;
constexpr uint16_t FORWARD = 0x080;
constexpr uint16_t ALL_POSITIONS = 0x0F0;
constexpr uint16_t LINEUP_ONLY = 0x100;  // only slots flagged kSlotInLineup
constexpr uint16_t NO_DIVIDE = 0x200;    // return the sum instead of the per-player average
constexpr uint16_t FIT_ONLY = 0x400;     // skip players with w364 bits 16-20 != 0 (injured?)
}  // namespace avg

struct TeamAverage {
    int value = 1;         // result, 1..99 unless NO_DIVIDE
    int players = 0;       // number of players that matched the filter (at least 1)
    int best_half_sum = 0; // max over players of (KO + TE) / 2 for the selected attributes (CareerSession_FormatError_ExecutableField3)
};

// Port of QuoteNationalFriendlyMatch for league teams (id < 168) and kNoTeam.
//
// With CONDITION and/or TECHNIQUE the result is the average of the selected attributes,
// scaled by form and morale if those flags are set as well:
//     value = mean(KO, TE) * (100 + (avg_form - 50) / 8) / 100 * (100 + (avg_morale - 50) / 8) / 100
// With only FORM and/or MORALE the result is the respective average (both: their mean).
// Quirk kept from the original: if all four position bits and LINEUP_ONLY are set
// (flags & 0x1F0 == 0x1F0), the divisor is fixed to 11 players instead of the real count.
// Teams without squads (ids from 168 on) take their value from TeamRating with the
// rating profile 2, which draws from `rand` (required for them); players is then 0.
TeamAverage team_average(const GameState& state, int team, uint16_t flags, WatcomRandom* rand = nullptr);

}  // namespace bmh
