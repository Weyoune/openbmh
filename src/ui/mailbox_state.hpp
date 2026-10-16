// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <vector>

namespace bmh { struct GameState; }

namespace bmh::ui {

// The save stores no semantic category code in a mailbox entry. Keep this explicit so a
// future screen cannot mistake a guessed category for original data.
enum class MailboxMessageKind { unclassified };

struct MailboxMessage {
    std::array<uint8_t, 0xa0> raw_entry{}; // Exact physical identity, including bounded raw text.
    // Physical mailbox slot, unique only for this projection. DropOldestMessage compacts entries
    // when old messages are discarded, so this is not a persistent message identifier.
    uint8_t entry_index = 0;
    uint32_t recipient_mask = 0;
    uint32_t kept_by_mask = 0;
    std::vector<int> recipient_managers;
    bool general_broadcast = false;
    bool kept_for_manager = false;
    uint16_t date_counter = 0; // big-endian CalendarDay copy, not a formatted calendar date
    MailboxMessageKind kind = MailboxMessageKind::unclassified;
    std::string text; // owned UTF-8 decoded from the bounded ISO 646-DE entry text
};

struct MailboxState {
    int manager_slot = -1;
    uint64_t revision = 0;
    std::vector<MailboxMessage> messages; // original reader order: ascending date counter, then physical slot
};

// Read-only counterpart of MailboxStateFromGame's per-manager filter and message ordering. It never
// marks messages read, deletes/keeps them, advances the date, or invokes game/RNG logic.
MailboxState mailbox_state_from_game(const GameState& game, int manager_slot, uint64_t revision = 0);

enum class MailboxActionKind { none, toggle_keep, delete_for_manager };
struct MailboxActionRequest {
    MailboxActionKind kind = MailboxActionKind::none;
    int manager_slot = -1;
    uint64_t revision = 0;
    int entry_index = -1;
    std::array<uint8_t, 0xa0> expected_entry{};
    bool operator==(const MailboxActionRequest&) const = default;
};

// MailboxStateFromGame clears only MailboxRecipientMask's active-manager bit after the reader
// exits; this typed close notice binds that transient acknowledgement.
struct MailboxCloseRequest {
    int manager_slot = -1;
    uint64_t revision = 0;
    bool operator==(const MailboxCloseRequest&) const = default;
};

} // namespace bmh::ui
