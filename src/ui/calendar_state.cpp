// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/calendar_state.hpp"

#include <limits>
#include <stdexcept>
#include <string>

#include "formats/le.hpp"
#include "game/matchday_setup.hpp"
#include "game/state.hpp"
#include "game/reschedule.hpp"

namespace bmh::ui {
namespace {
constexpr uint32_t kCalendar = ::bmh::layout::CompetitionCalendar;
constexpr int kMaxSeasonDays = 366;

void require_memory(const GameState& game, uint32_t address) {
    if (!game.memory.has(address)) throw FormatError("calendar: missing saved memory at " + std::to_string(address));
}
}

CalendarState calendar_from_game(const GameState& game, const LeImage& executable) {
    if (!executable.contains(::bmh::layout::CalendarState_CalendarFromGame_ExecutableField) || !executable.contains(::bmh::layout::CalendarState_CalendarFromGame_ExecutableField2))
        throw FormatError("calendar: missing executable month lengths");
    require_memory(game, ::bmh::layout::CalendarState_FormatError_RuntimeField3);
    require_memory(game, ::bmh::layout::DaysIntoSeason);
    const uint32_t native_start = game.memory.u16(::bmh::layout::CalendarState_FormatError_RuntimeField3);
    const int native_current = game.memory.u16(::bmh::layout::DaysIntoSeason);
    if (game.date < native_start || game.date - native_start != uint32_t(native_current))
        throw FormatError("calendar: saved native season cursor disagrees with current date");
    const DeskDate first_native_date = desk_date(native_start, executable);
    if (first_native_date.month != 7 || first_native_date.day < 1 || first_native_date.day > 7)
        throw FormatError("calendar: native season start is outside the first July week");
    const int leading_days = first_native_date.day - 1;
    if (native_current < 0 || native_current >= kMaxSeasonDays)
        throw FormatError("calendar: native season cursor is out of range");
    if (native_start < uint32_t(leading_days))
        throw FormatError("calendar: display calendar anchor underflow");
    const uint32_t display_start = native_start - uint32_t(leading_days);
    const int current_display = native_current + leading_days;

    // CalendarCell reads 0x16e saved calendar words. Keep the whole source table
    // available even when this particular year uses only its first 365 entries.
    for (int day = 0; day < kMaxSeasonDays; ++day) {
        const uint64_t address = uint64_t(kCalendar) + uint64_t(day) * 2 + 1;
        if (address > std::numeric_limits<uint32_t>::max())
            throw FormatError("calendar: calendar address overflow");
        require_memory(game, uint32_t(address));
    }
    require_memory(game, ::bmh::layout::CalendarState_FormatError_RuntimeField2);
    require_memory(game, ::bmh::layout::CalendarState_FormatError_RuntimeField);
    require_memory(game, ::bmh::layout::CompetitionSelection);
    for (int league = 0; league < 9; ++league) {
        const uint32_t address = ::bmh::layout::LeagueCalendarBits + uint32_t(league);
        if (!executable.contains(address))
            throw FormatError("calendar: missing executable league calendar class");
        if (executable.u8(address) > 31)
            throw FormatError("calendar: invalid executable league calendar class");
    }

    CalendarState state;
    state.season_start_counter = display_start;
    state.leading_days = leading_days;
    if (current_display >= kMaxSeasonDays)
        throw FormatError("calendar: current date is outside the display calendar");

    std::array<int, 9> ordinals{};
    state.days.reserve(kMaxSeasonDays);
    for (int offset = 0; offset <= kMaxSeasonDays; ++offset) {
        const uint64_t absolute = uint64_t(display_start) + uint64_t(offset);
        if (absolute > std::numeric_limits<uint32_t>::max())
            throw FormatError("calendar: day counter overflow");
        const auto day_counter = uint32_t(absolute);
        const DeskDate date = desk_date(day_counter, executable);
        if (offset > 0 && date.month == 7 && date.day == 1) break;
        if (offset == kMaxSeasonDays)
            throw FormatError("calendar: season did not reach the next July 1");

        const int native_day = offset - leading_days;
        const uint16_t raw = native_day >= 0 ? game.memory.u16(kCalendar + uint32_t(native_day) * 2) : 0;
        const uint32_t competitions = native_day >= 0 ? day_competitions(game, raw) : 0;
        CalendarDayProjection projection;
        projection.season_day = native_day;
        projection.day_counter = day_counter;
        projection.date = date;
        projection.raw_entry = raw;
        projection.competitions = competitions;
        projection.current_day = offset == current_display;

        for (int league = 0; league < 9; ++league) {
            const uint32_t mask = uint32_t(1) << executable.u8(::bmh::layout::LeagueCalendarBits + uint32_t(league));
            if ((competitions & mask) != 0) {
                if (ordinals[size_t(league)] == std::numeric_limits<int>::max())
                    throw FormatError("calendar: league matchday ordinal overflow");
                projection.league_matchdays[size_t(league)] = ++ordinals[size_t(league)];
            }
        }
        state.days.push_back(projection);
    }

    if (state.days.size() != 365 && state.days.size() != 366)
        throw FormatError("calendar: unsupported season length");
    if (current_display >= int(state.days.size()))
        throw FormatError("calendar: current date is outside the season calendar");
    state.current_day = current_display;
    return state;
}

CalendarState reschedule_calendar_from_game(const GameState& game, const LeImage& executable, int manager) {
    CalendarState state = calendar_from_game(game, executable);
    state.reschedule_manager = manager;
    for (const auto& match : reschedulable_matches(game, manager)) {
        RescheduleOption option;
        option.match = match;
        option.target_days = reschedule_target_days(game, manager, match);
        const int display_day = match.day + state.leading_days;
        if (match.day >= 0 && display_day >= 0 && size_t(display_day) < state.days.size())
            state.days[size_t(display_day)].reschedule_source = true;
        for (const int day : option.target_days)
            if (day >= 0 && day + state.leading_days >= 0 &&
                size_t(day + state.leading_days) < state.days.size())
                state.days[size_t(day + state.leading_days)].reschedule_target = true;
        state.reschedule_options.push_back(std::move(option));
    }
    state.moved_matches = moved_matches(game, manager);
    for (const auto& match : state.moved_matches) {
        const int display_day = match.day + state.leading_days;
        if (match.day >= 0 && display_day >= 0 && size_t(display_day) < state.days.size())
            state.days[size_t(display_day)].reschedule_undo = true;
    }
    return state;
}

} // namespace bmh::ui
