// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#pragma once
// The managers' mailbox (heap block *ManagerMailbox, saved in the savegame).
//
// 40 entries of 0xA0 bytes (PostMessage):
//   +0x00  u32 recipients: bit n = human manager n, bit 31 = a general message
//   +0x04  u32 read by (bit per manager)
//   +0x08  date (CalendarDay) as big-endian u16
//   +0x0a  0x96 bytes of text, copied blindly from the source - for texts taken straight
//          from TEXTD.BLK the following texts come along; for composed messages the rest
//          of the caller's buffer (not reproducible, see compare_mailbox)
// When the box is full the oldest entry (read ones first) is dropped (DropOldestMessage).

#include <cstdint>
#include <string>
#include <vector>

#include "formats/le.hpp"
#include "game/state.hpp"

namespace bmh {

constexpr size_t kMailboxEntries = 40;
constexpr size_t kMailboxEntry = 0xA0;
constexpr size_t kMailboxText = 0x96;

// PostMessage. recipients: a manager index, -1 = all managers, or a mask with bit 31 set.
void post_message(GameState& g, int64_t recipients, const std::vector<uint8_t>& text);
// A text of TEXTD.BLK as the original posts it (0x96 bytes from its start).
void post_text(GameState& g, int64_t recipients, size_t text_id);
// A composed message (only the text itself is known; the rest of the entry is zero).
void post_composed(GameState& g, int64_t recipients, const std::string& text);

// Differences between two mailboxes, comparing the text of each entry only up to its NUL
// (the bytes after it may be stack contents of the original).
int compare_mailbox(const std::vector<uint8_t>& ours, const std::vector<uint8_t>& theirs, std::string* first = nullptr);

}  // namespace bmh

namespace bmh {

// Text id of a pointer variable of the text table TextPointerTable (e.g. Weekly_AdvertisingOffers_TextPointer).
constexpr size_t text_id(uint32_t pointer_variable) { return (pointer_variable - ::bmh::layout::TextPointerTable) / 4; }

}  // namespace bmh

namespace bmh {

// Text of a pointer variable of the text table (TEXTD.BLK), empty without texts.
std::string message_text(const GameState& g, uint32_t pointer_variable);
// The next NUL-delimited string in the same loaded text block; empty if absent.
std::string message_text_following(const GameState& g, uint32_t pointer_variable);

// Contract warning loaded from the selected game image.
inline std::string kContractEndsSoonText;
inline std::string kMastersQualificationText;
inline std::string kMastersWinnerText;
inline std::string kIndoorInvitationText;
inline std::string kChampionshipBonusText;
inline std::string kFriendlyPayerPrefixText;

// Installs the six texts above from the game image (the English game uses its own wording).
// Both career sessions call it at start; standalone engine tools (week_check, the season sims)
// must call it too, or these messages lose their text (#14: the indoor entry bonus mail).
void install_message_texts(const LeImage& exe);

// FormatNumber(buf, value, 0x101, ""): an amount of money as the messages show it: the
// value converted with CurrencyMultiplier / CurrencyDivisor, digits grouped by dots, then " EU".
std::string format_amount(const GameState& g, int32_t value);

// FormatNumber(buf, value, flags, ""): bits 0-5 the field width, bit 0x40 no dot grouping,
// bit 0x100 an amount of money (converted, " EU" appended).
std::string format_number(const GameState& g, int32_t value, int flags);

}  // namespace bmh
