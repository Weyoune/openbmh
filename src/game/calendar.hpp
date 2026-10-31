// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <span>
namespace bmh {
class LeImage;
struct CalendarEpoch { int year; int weekday; };
// Read verified original date-setup operands; unknown routines are rejected.
CalendarEpoch calendar_epoch_from_executable(const LeImage& exe);
// Read the verified DOS FLTD date-setup operands directly. The translated
// data image does not contain the original English instruction addresses.
CalendarEpoch calendar_epoch_from_fltd(std::span<const uint8_t> executable);
void install_calendar_rules(const LeImage& exe);
// Isolated authored states default to the established synthetic calendar.
CalendarEpoch calendar_epoch();
}
