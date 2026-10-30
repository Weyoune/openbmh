// SPDX-License-Identifier: GPL-3.0-only
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
