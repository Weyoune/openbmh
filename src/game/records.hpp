// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Sizes of the fixed records of the original game state, in bytes. Until 1.0.12 every file that walks
// these records kept its own copy of the numbers; they are named here once. The values are those of
// the original (the save file is built from these records), so they are constants, not part of the
// world layout: a world with more managers or clubs has more records, not larger ones.

#include <cstddef>

namespace bmh {

// One manager record (five of them: managers 0-3 and the spare record 4 the national coach swaps with).
constexpr size_t kManagerRecordSize = 0xB0C;
// The tactics board inside a manager record: 24 slots of kTacticsBoardSlotSize bytes.
constexpr size_t kTacticsBoardSize = 0x840;
constexpr size_t kTacticsBoardSlotSize = 0x58;
// The league record of a club (*ClubSeasonRecords), one per club with league records.
constexpr size_t kClubRecordSize = 0x4a;
// One league's part of the results block (LeagueResults, schedule.hpp): 38 matchdays x 10 matches,
// whatever the size of the league.
constexpr size_t kLeagueResultsStride = 0x17C;

}  // namespace bmh
