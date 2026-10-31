// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/world12/fixtures.hpp"
#include "game/world12/calendar.hpp"
#include "game/world12/cups.hpp"
#include <optional>
#include "formats/fbm.hpp"
namespace bmh::world12 {
struct CalendarCursor {SeasonCalendar schedule;uint16_t next_day=0;uint64_t revision=0;bool operator==(const CalendarCursor&)const=default;};
struct CareerSnapshot {
    World world;
    SeasonResults results;
    uint32_t date=0,season=0,watcom_seed=1;
    int32_t game_seed=0;
    std::array<uint8_t,14> league_rounds{};
    std::optional<CalendarCursor> calendar;
    std::vector<KnockoutStage> cups;
    // Typed active career snapshots will extend this record as their owning
    // progression transactions are integrated. No legacy FBM stream is guessed.
    void validate()const;
    bool operator==(const CareerSnapshot&)const=default;
};
Savegame make_save(const CareerSnapshot&);
CareerSnapshot read_save(const Savegame&);
}
