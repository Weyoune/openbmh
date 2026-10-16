// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// OpenBMH deviates from the original: uses the enhanced assistant-training rules, retaining the selected skill until it reaches its maximum.
// Between the matchdays: the day processing (BMH.exe V2.24 + supported rule profile patch).
//
// "Weiter" (DailyInterest) handles the event of the current day (AdvanceTurn, e.g. a
// matchday) and then advances day by day (DaysIntoSeason = day of the season, CalendarDay =
// day counter) until the calendar (block CompetitionCalendar) has the next event. Every day runs
// DayStepRest:
//   - date + 1, DailyInterest2(1): interest on every human manager's account
//   - training and weekly processing on the Source weekday boundary
//   - month_change and the national coach's temporary club handback
// day_step_rest in turn.cpp owns those steps; DailyInterest3 advances shares.

#include <cstdint>

#include "game/random.hpp"
#include "game/state.hpp"

namespace bmh {

// DailyInterest2(1): daily interest for the four manager slots. Balance (record +0x314)
// earns 1 % a year, the fixed deposit (+0x318) 2 %; an overdraft costs 12 % a year, beyond
// the credit line (+0x31c) 18 % (booked to +0x89c). Income goes to +0x870. A balance over
// 100 million triggers a random extra booking (WatcomRandom 10000-20000 thousand).
// Every day step starts with it, so it also does what DailyInterest does right before the day
// step: bit 7 of record +0x21f (a training camp visit booked today, BookTrainingCamp) is
// cleared for every manager.
void daily_interest(GameState& g, WatcomRandom& rand);

}  // namespace bmh

namespace bmh {

// DeskDate2 (weekday part): CareerSession_PairingsRuntimeDraw_ExecutableField6 of a day counter.
int weekday_code(uint32_t date);

// FamiliarityDecay (training days): the players of human managers slowly forget their places
// on the tactics board (slot byte 0x0f), faster when condition < technique and when older.
void familiarity_decay(GameState& g, GameRandom& game, WatcomRandom& rand);

// TrainSquad: training of one human manager's squad (on training days). At the end, with
// the training automatic on (option byte 4 bit 0, CareerSessionRuntimeField3), auto_training.
void train_squad(GameState& g, int manager, GameRandom& game, WatcomRandom& rand);

// OpenBMH deviates from the original: uses the enhanced assistant-training rules, retaining the selected skill until it reaches its maximum.
// Initialize (+ the supported rule profile patch Position / WeekOperation): the assistant's training
// plan. Option byte 4 (CareerSessionRuntimeField3) holds what he decides - without bit 7 it becomes 0x9f
// (everything), without bit 0 nothing. The plan (record +0xc5, 13 bytes, 1 light 2 normal
// 3 hard) starts from the manager's own:
//   bit 1, bytes 0-3: the team value (QuoteNationalFriendlyMatch 0x1f/0x2f/0x4f/0x8f) of keepers,
//          defence, midfield, attack in percent of the whole team (0xff); the two weakest
//          below 98 % train hard, the two strongest then light - sorted ascending,
//   bit 2, bytes 4-5: condition (0xf1) against technique (0xf2): the weaker one hard when
//          they differ by more than one,
//   bit 2, bytes 6-12: the skills 6..0 of w368: 50 + players at 0 - players at 3, sorted
//          descending; the three most needed (over 52) hard, their opposite numbers light.
// Per player of the squad on the tactics board:
//   bit 3, +0x0a bits 0-1: the intensity after the familiarity +0x0f (below 31 hard, above
//          60 light),
//   bit 4, +0x0a bits 2-4: the skill to train (supported rule profile): kept unless it is at 3 already,
//          else the first not yet at 3 in the order of the position (keeper 7,6,3,4,5;
//          defence 3,6,4,7,5; midfield 4,6,5,7,3; attack 5,6,4,7,3 - index k = skill 7 - k).
void auto_training(GameState& g, int manager);

// DailyInterest3: the daily moves of the 12 shares (block StockExchange).
void stock_market(GameState& g, GameRandom& game);

}  // namespace bmh

namespace bmh {

// DeskDate2 (month part): CareerSession_PairingsRuntimeDraw_ExecutableField5 (1-12) of a day counter. Day 0 is in July; the
// year has 365 days, every fourth one 366 (February 29).
int month_code(uint32_t date);

}  // namespace bmh
