// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/messages.hpp"
#include "game/schedule.hpp"

#include <algorithm>
#include <cstdio>

namespace bmh {

namespace {

uint32_t rd32(const std::vector<uint8_t>& v, size_t o) {
    return uint32_t(v.at(o) | v.at(o + 1) << 8 | v.at(o + 2) << 16) | uint32_t(v.at(o + 3)) << 24;
}
void wr32(std::vector<uint8_t>& v, size_t o, uint32_t x) {
    for (int i = 0; i < 4; i++) v.at(o + size_t(i)) = uint8_t(x >> (8 * i));
}

}  // namespace

void post_message(GameState& g, int64_t recipients, const std::vector<uint8_t>& text) {
    std::vector<uint8_t>& box = g.mailbox;
    if (box.size() < kMailboxEntries * kMailboxEntry) return;  // no mailbox loaded
    size_t index = 0;
    while (index < kMailboxEntries && rd32(box, index * kMailboxEntry) != 0) index++;
    size_t at;
    if (index == kMailboxEntries) {
        // DropOldestMessage: drop the oldest entry, unread ones count as newer
        uint32_t best = 0x7FFFFFFF, key = 0;
        size_t oldest = 0;
        for (size_t k = 0; k < kMailboxEntries; k++) {
            const size_t e = k * kMailboxEntry;
            if (rd32(box, e + 4) != 0) key = 0xF0000;
            key |= uint32_t(box[e + 8]) << 8 | box[e + 9];
            if (key < best) best = key, oldest = k;
        }
        if (oldest != kMailboxEntries - 1)
            std::copy(box.begin() + long((oldest + 1) * kMailboxEntry), box.end(), box.begin() + long(oldest * kMailboxEntry));
        at = (kMailboxEntries - 1) * kMailboxEntry;
        box[at] = 0;
    } else {
        at = index * kMailboxEntry;
    }
    uint32_t mask;
    if (recipients == -1) mask = (0xFFFFFFFFu >> (32 - g.manager_teams.size())) | 0x80000000u;
    else if (recipients & 0x80000000) mask = uint32_t(recipients);
    else mask = 1u << uint32_t(recipients);
    // PostMessage ORs the recipient mask into MailboxRecipientMask. It is a runtime
    // global absent from normal FBMs; add_block deliberately keeps it out of
    // save serialization while preserving the Source address and bit layout.
    g.memory.add_block(::bmh::layout::MailboxRecipientMask, 4);
    g.memory.set32(::bmh::layout::MailboxRecipientMask, g.memory.u32(::bmh::layout::MailboxRecipientMask) | mask);
    wr32(box, at, mask);
    wr32(box, at + 4, 0);
    const uint16_t date = uint16_t(std::min<uint32_t>(g.date, 0xFFFF));
    box[at + 8] = uint8_t(date >> 8);
    box[at + 9] = uint8_t(date);
    for (size_t i = 0; i < kMailboxText; i++) box[at + 10 + i] = i < text.size() ? text[i] : 0;
}

void post_text(GameState& g, int64_t recipients, size_t text_id) {
    if (!g.texts) return;
    post_message(g, recipients, g.texts->raw(text_id, kMailboxText));
}

void post_composed(GameState& g, int64_t recipients, const std::string& text) {
    post_message(g, recipients, std::vector<uint8_t>(text.begin(), text.end()));
}

int compare_mailbox(const std::vector<uint8_t>& ours, const std::vector<uint8_t>& theirs, std::string* first) {
    int diffs = 0;
    for (size_t k = 0; k < kMailboxEntries && (k + 1) * kMailboxEntry <= std::min(ours.size(), theirs.size()); k++) {
        const size_t e = k * kMailboxEntry;
        bool same = std::equal(ours.begin() + long(e), ours.begin() + long(e + 10), theirs.begin() + long(e));
        for (size_t i = 10; i < kMailboxEntry && same; i++) {
            if (ours[e + i] != theirs[e + i]) same = false;
            if (theirs[e + i] == 0) break;  // end of the text
        }
        if (rd32(theirs, e) == 0 && rd32(ours, e) == 0) same = true;  // both empty
        if (!same) {
            if (diffs == 0 && first) {
                char b[64];
                std::snprintf(b, sizeof b, "entry %zu", k);
                *first = b;
            }
            diffs++;
        }
    }
    return diffs;
}

}  // namespace bmh

namespace bmh {

std::string message_text(const GameState& g, uint32_t pointer_variable) {
    return g.texts ? (*g.texts)[text_id(pointer_variable)] : std::string();
}

std::string message_text_following(const GameState& g, uint32_t pointer_variable) {
    if (!g.texts) return {};
    const size_t id = text_id(pointer_variable);
    if (id >= g.texts->offsets.size()) return {};
    const auto& bytes = g.texts->blob;
    size_t at = g.texts->offsets[id];
    if (at >= bytes.size()) return {};
    while (at < bytes.size() && bytes[at] != 0) ++at;
    if (at == bytes.size()) return {};
    ++at;
    std::string text;
    while (at < bytes.size() && bytes[at] != 0) text.push_back(char(bytes[at++]));
    return text;
}

std::string format_number(const GameState& g, int32_t value, int flags) {
    if (flags & 0x100) {
        // ConvertCurrency: currency conversion (1/1 unless the savegame says otherwise)
        const int32_t mul = g.memory.has(::bmh::layout::CurrencyMultiplier) ? int32_t(g.memory.u32(::bmh::layout::CurrencyMultiplier)) : 1;
        const int32_t div = g.memory.has(::bmh::layout::CurrencyDivisor) ? int32_t(g.memory.u32(::bmh::layout::CurrencyDivisor)) : 1;
        if (div != 0) value = value * mul / div;
    }
    // FormatGroupedDigits: digits from the right; with grouping (bit 0x40 clear) a dot before the
    // 4th, 8th and 12th character; padded to the width (bits 0-5), leading blanks for the
    // dots a wide field reserves
    const bool group = !(flags & 0x40);
    const int width = flags & 0x3F;
    int blanks = group && width > 3 ? (width - 4) / 3 + 1 : 0;
    const bool negative = value < 0;
    uint32_t v = negative ? uint32_t(-int64_t(value)) : uint32_t(value);
    std::string reversed;
    int dots = 0;
    while (v != 0) {
        const size_t n = reversed.size();
        if (group && (n == 3 || n == 7 || n == 11)) reversed.push_back('.'), blanks--, dots++;
        reversed.push_back(char('0' + v % 10));
        v /= 10;
    }
    if (!group) blanks = 0;
    while (int(reversed.size()) < width + dots - (negative ? 1 : 0)) reversed.push_back(reversed.empty() ? '0' : '%');
    for (int i = 0; i < blanks; i++) reversed.push_back(' ');
    if (negative) reversed.push_back('-');
    std::string s(reversed.rbegin(), reversed.rend());
    if (flags & 0x100) {
        // FLTD FormatAmount uses the pound prefix at CurrencyPrefixText.
        if (g.currency_suffix == " &") s = "& " + s;
        else s += g.currency_suffix;  // CurrencySuffixText
    }
    return s;
}

std::string format_amount(const GameState& g, int32_t value) { return format_number(g, value, 0x101); }

void install_message_texts(const LeImage& exe) {
    kContractEndsSoonText = exe.cstring(kEnglishCompetitions ? ::bmh::layout::EnglishContractExpiryWarningText : ::bmh::layout::ContractExpiryWarningText);
    kMastersQualificationText = kEnglishCompetitions ? "Masters entry confirmed. Your participation bonus has been credited." : exe.cstring(::bmh::layout::MastersQualificationText);
    kMastersWinnerText = kEnglishCompetitions ? "Masters title won. Your prize money has been credited." : exe.cstring(::bmh::layout::MastersWinnerText);
    kIndoorInvitationText = kEnglishCompetitions ? "Indoor event entry bonus credited for " : exe.cstring(::bmh::layout::IndoorInvitationText);
    kChampionshipBonusText = kEnglishCompetitions ? "League title bonus credited." : exe.cstring(::bmh::layout::ChampionshipBonusText);
    kFriendlyPayerPrefixText = kEnglishCompetitions ? "Your share: " : exe.cstring(::bmh::layout::NationalFriendlyPayerPrefixText);
}

}  // namespace bmh
