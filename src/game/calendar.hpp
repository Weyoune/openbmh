// SPDX-License-Identifier: GPL-3.0-only
// Interface for the calendar epoch (start year and weekday) used for date calculations.
// calendar_epoch_from_executable validates the loaded executable, install_calendar_rules makes it
// active, calendar_epoch returns it (a synthetic default for isolated test states).
// Implemented in calendar.cpp; no GameState access and no random numbers.
#pragma once
namespace bmh {
class LeImage;
struct CalendarEpoch { int year; int weekday; };
// Read verified original date-setup operands; unknown routines are rejected.
CalendarEpoch calendar_epoch_from_executable(const LeImage& exe);
void install_calendar_rules(const LeImage& exe);
// Isolated authored states default to the established synthetic calendar.
CalendarEpoch calendar_epoch();
}
