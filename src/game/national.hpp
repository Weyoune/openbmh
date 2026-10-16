// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The national team (BMH.exe V2.24 + supported rule profile patch), team id 0x165 (kNoTeam), squad slots
// 0xc00-0xc17 (LargeSquadClubCount).
//
// With the world game on (CompetitionSelection bit 1) the calendar holds national team days (entry
// 0x4006, DayCompetitions -> national-only flag). On such a day AdvanceTurn calls NationalTeamDay, which
// nominates the squad: the manager slots are swapped so that slot 0 is a spare one
// (SwapManagerSlots(4, 0); a Bundestrainer instead takes slot 0 over with the national team,
// SwapManagerSlots2), the 3 goalkeepers, 5 defenders, 7 midfielders and 5 forwards
// (NationalRuntimeField) of the highest strength among the available German players of all clubs
// join the national squad with a fresh tactics board entry (PendingOfferAnswer), the national team
// gets its line-up (Int2(0x165, 0xe4)) and the slots are swapped back. Without a
// Bundestrainer the day then prepares an empty matchday (no desk, no match).

#include "game/random.hpp"
#include "game/state.hpp"

namespace bmh {

struct MinuteReport;

// SwapManagerSlots(a, b): manager slots a and b change places - the manager records (0xb0c
// bytes), the tactics boards (0x840) and the desktop layouts (block Desktops, 0x360).
void swap_manager_slots(GameState& g, int a, int b);

// PendingOfferAnswer(manager, from_slot, -1, squad_start): the player of the absolute squad slot
// `from_slot` gets the first free slot of the squad starting at `squad_start` (24 slots;
// the slot value is copied with its flags) and a fresh entry on the tactics board of
// manager slot `manager`: neutral settings, familiarity W(40, 60), the four attribute
// bytes W(2, 39) each (1 for the first two places) and the salary of a 2-year contract
// at W(95, 105) percent (AddToSquad). Returns the new absolute slot, -1 if the squad is full.
int add_to_squad(GameState& g, int manager, int from_slot, int squad_start, WatcomRandom& rand);
// The board part of PendingOfferAnswer (also called with a given place `index`): the entry of
// the player in absolute squad slot `slot` on the tactics board of `manager`.
void new_board_entry(GameState& g, int manager, int index, int slot, WatcomRandom& rand);

// NationalTeamDay: the national team day (see above). Returns whether a Bundestrainer manages
// the national team (CoachActive) - then his desk follows (AdvanceTurn: local_28 = 4).
bool national_team_day(GameState& g, WatcomRandom& rand, MinuteReport& report);

// The Bundestrainer (national coach), see national.cpp. SwapManagerSlots2: notes the number of
// managers (OriginalManagerCount) and `active_manager` (ClubPlayerDetail_InvalidArgument_RuntimeField3 = ActiveManager); a manager
// flagged in his record (+0x21c), every manager in the world game without leagues
// (CompetitionSelection == 2) and the only manager of a one-manager game takes the national team over:
// the national team's record (slot 4) gets his name and portrait, the number of managers
// becomes 1, slots 4 and 0 change places, slot 0 manages team 0x165 and copies +0x218 from
// slot 4, the national team gets its line-up (Int2(0x165, 0xe4)) and
// CoachActive = 1.
// `source_season_day` is the 16-bit value visible in DaysIntoSeason at this exact Source call
// site. It is not always `g.season_day()`: DailyInterest advances the counter before
// quiet-day rest but after rest on the completed-event path.
void national_coach_take_over(GameState& g, int active_manager, uint16_t source_season_day);
// NationalCoachHandBack: with CoachActive set the slots change places again, the number of managers
// is restored from OriginalManagerCount and CoachActive cleared.
void national_coach_hand_back(GameState& g);
// CupDraw_FormatError_RuntimeField2 == 0x165: slot 0 manages the national team.
bool national_coach_active(const GameState& g);
// NationalCoachAfterLoad (saving and loading): the savegame holds the slots swapped back with
// CoachActive still 1; afterwards (and so after loading) SwapManagerSlots2 takes over again.
void national_coach_after_load(GameState& g);
// The state as NationalCoachAfterLoad writes it (for the savegame writer and comparisons).
GameState national_coach_save_form(const GameState& g);

}  // namespace bmh
