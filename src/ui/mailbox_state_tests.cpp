// SPDX-License-Identifier: GPL-3.0-only
#include "ui/mailbox_state.hpp"

#include <functional>
#include <iostream>
#include <stdexcept>

#include "formats/binary.hpp"
#include "game/messages.hpp"
#include "game/state.hpp"
#include "ui/manager.hpp"

using namespace bmh;
using namespace bmh::ui;

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void rejected(const std::function<void()>& action) {
    try { action(); } catch (const FormatError&) { return; }
    throw std::runtime_error("expected rejected mailbox input");
}
void put32(std::vector<uint8_t>& data, size_t at, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) data.at(at + i) = uint8_t(v >> (8 * i));
}
GameState fixture() {
    GameState g;
    g.manager_teams = {12, 13};
    g.mailbox.assign(kMailboxEntries * kMailboxEntry, 0);
    return g;
}
void message(GameState& g, size_t index, uint32_t to, uint32_t read, uint16_t date,
             const std::string& text) {
    const size_t at = index * kMailboxEntry;
    put32(g.mailbox, at, to); put32(g.mailbox, at + 4, read);
    g.mailbox.at(at + 8) = uint8_t(date >> 8); g.mailbox.at(at + 9) = uint8_t(date);
    for (size_t i = 0; i < text.size() && i < kMailboxText; ++i)
        g.mailbox.at(at + 10 + i) = uint8_t(text[i]);
}
}

int main() {
    try {
        auto g = fixture();
        message(g, 5, 0x80000003u, 0x2u, 0x0123, "Gr}~e"); // ISO 646-DE: } = ü, ~ = ß
        message(g, 2, 0x1u, 0x1u, 0x0123, "Fr}hst}ck");
        message(g, 7, 0x2u, 0, 0x0122, "only manager one");
        message(g, 9, 0x80000000u, 0, 0x0121, "general bit without manager bits");
        message(g, 11, 0x1u, 0x1u, 0x8000, "unsigned late-range date");
        message(g, 12, 0x1u, 0, 0x0123, ""); // recipient bit makes an empty-text item visible
        std::string bounded(kMailboxText, 'x');
        bounded.back() = char(0xa0);
        message(g, 13, 0x1u, 0, 0x0123, bounded);
        message(g, 14, 0, 0, 0x0001, "recipient-free text is ignored");
        message(g, 15, 0x1u, 0, 0x0124, std::string("K\xa0}nig"));
        const auto original = g.mailbox;

        const auto manager0 = mailbox_state_from_game(g, 0);
        check(manager0.manager_slot == 0 && manager0.messages.size() == 6,
              "filter by manager recipient bit, not general bit alone");
        check(manager0.messages[0].entry_index == 2 && manager0.messages[1].entry_index == 5 &&
              manager0.messages[2].entry_index == 12 && manager0.messages[3].entry_index == 13 &&
              manager0.messages[4].entry_index == 15 && manager0.messages[5].entry_index == 11,
              "original mailbox date and physical-slot ordering");
        check(manager0.messages[0].date_counter == 0x0123 &&
              manager0.messages[0].text == "Frühstück", "big-endian date and owned UTF-8 content");
        check(manager0.messages[1].general_broadcast && manager0.messages[1].recipient_managers.size() == 2 &&
              manager0.messages[1].recipient_managers[0] == 0 && manager0.messages[1].recipient_managers[1] == 1,
              "general broadcast and explicit recipient-manager mask");
        check(manager0.messages[0].kept_for_manager && !manager0.messages[1].kept_for_manager &&
              manager0.messages[0].kept_by_mask == 1, "keep-marker state remains a projection");
        check(manager0.messages[2].text.empty() && manager0.messages[3].text.size() == kMailboxText &&
              manager0.messages[3].text.back() == ' ',
              "empty and unterminated bounded text spans");
        check(manager0.messages[4].text == "K ünig" &&
              utf8_to_original(manager0.messages[4].text) == "K }nig",
              "0xa0 becomes an ASCII word separator before owned UTF-8 conversion");
        for (const auto& item : manager0.messages) (void)utf8_to_original(item.text);
        check(manager0.messages[0].kind == MailboxMessageKind::unclassified,
              "mailbox bytes do not invent a semantic message category");
        const auto manager1 = mailbox_state_from_game(g, 1);
        check(manager1.messages.size() == 2 && manager1.messages[0].entry_index == 7 &&
              manager1.messages[1].entry_index == 5, "same global mailbox projects per manager");
        check(g.mailbox == original, "projection never changes mailbox bytes");
        check(manager0.messages[0].text == "Frühstück", "projected strings own decoded storage");
        g.mailbox[2 * kMailboxEntry + 10] = 'X';
        check(manager0.messages[0].text == "Frühstück", "owned text survives source mailbox mutation");

        rejected([&] { (void)mailbox_state_from_game(g, -1); });
        rejected([&] { (void)mailbox_state_from_game(g, 2); });
        rejected([&] { (void)mailbox_state_from_game(g, 4); });
        auto truncated = fixture(); truncated.mailbox.resize(kMailboxEntry);
        rejected([&] { (void)mailbox_state_from_game(truncated, 0); });
        std::cout << "Read-only mailbox projection passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
