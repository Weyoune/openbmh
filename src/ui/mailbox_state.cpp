// SPDX-License-Identifier: GPL-3.0-only
#include "ui/mailbox_state.hpp"

#include <algorithm>
#include <utility>

#include "formats/binary.hpp"
#include "game/messages.hpp"
#include "game/state.hpp"

namespace bmh::ui {
namespace {
uint32_t read_u32_le(const std::vector<uint8_t>& bytes, size_t at) {
    return uint32_t(bytes.at(at)) | uint32_t(bytes.at(at + 1)) << 8 |
           uint32_t(bytes.at(at + 2)) << 16 | uint32_t(bytes.at(at + 3)) << 24;
}
}

MailboxState mailbox_state_from_game(const GameState& game, int manager_slot, uint64_t revision) {
    // Original game has four human-manager slots. Bit 31 is a broadcast marker, not a manager.
    if (manager_slot < 0 || size_t(manager_slot) >= game.manager_teams.size() || manager_slot >= 4)
        throw FormatError("mailbox: invalid manager slot");
    constexpr size_t required = kMailboxEntries * kMailboxEntry;
    if (game.mailbox.size() < required) throw FormatError("mailbox: missing or truncated mailbox block");

    MailboxState out;
    out.manager_slot = manager_slot;
    out.revision = revision;
    const uint32_t manager_bit = uint32_t(1) << manager_slot;
    for (size_t index = 0; index < kMailboxEntries; ++index) {
        const size_t entry = index * kMailboxEntry;
        const uint32_t recipients = read_u32_le(game.mailbox, entry);
        if ((recipients & manager_bit) == 0) continue; // MailboxStateFromGame filters on this bit.

        MailboxMessage message;
        std::copy_n(game.mailbox.begin() + entry, message.raw_entry.size(), message.raw_entry.begin());
        message.entry_index = uint8_t(index);
        message.recipient_mask = recipients;
        message.kept_by_mask = read_u32_le(game.mailbox, entry + 4);
        message.general_broadcast = (recipients & 0x80000000u) != 0;
        message.kept_for_manager = (message.kept_by_mask & manager_bit) != 0;
        message.date_counter = uint16_t(uint16_t(game.mailbox[entry + 8]) << 8 |
                                         game.mailbox[entry + 9]);
        for (size_t manager = 0; manager < game.manager_teams.size() && manager < 31; ++manager)
            if ((recipients & (uint32_t(1) << manager)) != 0)
                message.recipient_managers.push_back(int(manager));

        const size_t text_at = entry + 10;
        size_t text_length = 0;
        while (text_length < kMailboxText && game.mailbox[text_at + text_length] != 0)
            ++text_length;
        std::string original_text(reinterpret_cast<const char*>(game.mailbox.data() + text_at), text_length);
        // RealityReporterPanel2 treats both ASCII space and 0xa0 as word separators. The latter is
        // an original layout byte, not a standalone UTF-8 character; normalize it before
        // converting the owned projection, preserving the same visible word boundary.
        std::replace(original_text.begin(), original_text.end(), char(0xa0), ' ');
        message.text = iso646de_to_utf8(original_text.data(), original_text.size());
        out.messages.push_back(std::move(message));
    }

    // MailboxStateFromGame packs (date high byte << 24) | (date low byte << 8) | physical slot
    // and Games compares this as unsigned ascending. Decoded date then slot is equivalent.
    std::sort(out.messages.begin(), out.messages.end(), [](const auto& a, const auto& b) {
        if (a.date_counter != b.date_counter) return a.date_counter < b.date_counter;
        return a.entry_index < b.entry_index;
    });
    return out;
}

} // namespace bmh::ui
