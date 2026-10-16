// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/advertising.hpp"
#include "ui/game_end.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <utility>

#include "formats/textd.hpp"
#include "game/messages.hpp"
#include "game/schedule.hpp"
#include "game/week.hpp"
#include "game/state.hpp"
#include "ui/manager.hpp"
#include "ui/setup.hpp"

namespace bmh::ui {
namespace {
constexpr size_t kManagerRecord = 0xb0c;
constexpr size_t kAdvertisingContracts = 0xa34;
constexpr size_t kContractBytes = 0x0c;
constexpr int kBoardCount = 14;

// Games sorts the unsigned 32-bit keys in descending order and swaps
// the parallel 16-bit record indices at the same time. Keep its midpoint pivot
// and partition boundaries: equal keys intentionally do not retain input order.
void original_contract_sort(std::array<uint32_t, kBoardCount>& keys,
                            std::array<int, kBoardCount>& slots,
                            int first, int count) {
    while (count > 1) {
        const uint32_t pivot = keys[size_t(first + count / 2)];
        int left = -1;  // Original ushort starts at 0xffff, then wraps to index 0.
        int right = count;
        do {
            do { ++left; } while (pivot < keys[size_t(first + left)]);
            do { --right; } while (keys[size_t(first + right)] < pivot);
            if (right <= left) break;
            std::swap(keys[size_t(first + left)], keys[size_t(first + right)]);
            std::swap(slots[size_t(first + left)], slots[size_t(first + right)]);
        } while (true);

        original_contract_sort(keys, slots, first, left);
        first += left;
        count -= left;
    }
}

uint32_t u32(const std::vector<uint8_t>& bytes, size_t at) {
    return uint32_t(bytes.at(at)) | uint32_t(bytes.at(at + 1)) << 8 |
           uint32_t(bytes.at(at + 2)) << 16 | uint32_t(bytes.at(at + 3)) << 24;
}

int32_t signed32(uint32_t value) { return std::bit_cast<int32_t>(value); }
int32_t wrap_add(int32_t a, int32_t b) { return signed32(uint32_t(a) + uint32_t(b)); }
int32_t wrap_mul(int32_t a, int32_t b) { return signed32(uint32_t(a) * uint32_t(b)); }
int32_t wrap_neg(int32_t value) { return signed32(0u - uint32_t(value)); }
int32_t mem_i32(const GameState& game, uint32_t address) {
    return signed32(game.memory.u32(address));
}
std::string original_two_digit(int value) {
    std::string result = std::to_string(value);
    if (result.size() < 2) result.insert(result.begin(), '%');
    return result;
}
std::string original_grouped_number(uint32_t value) {
    std::string digits = std::to_string(value);
    const int actual = int(digits.size());
    for (int from = actual - 3; from > 0; from -= 3) digits.insert(size_t(from), ".");
    return digits;
}

int32_t original_share_adjustment(const GameState& game, int manager, bool income) {
    // SharesAndLoans/Rec32: original LE code reads each owned fund from
    // FundBlock + k*0x1c (MOVDWORD at 4bacb), not byte stride seven. Its unit prices
    // at BMH.exe+MonthRuntimeField10 are {15,18,21,24,27,30}; owned counts use Advertising_OriginalShareAdjustment_RuntimeField + k*0x1c.
    // The outstanding-share/loan rows are saved at Loans.
    constexpr std::array<int32_t, 6> kShareUnits{15, 18, 21, 24, 27, 30};
    const uint8_t manager_count = game.memory.u8(::bmh::layout::ManagerCount); // runtime ManagerCount
    if (manager_count > 4) throw FormatError("advertising: invalid Original manager count");
    int32_t amount = 0;
    if (income) {
        for (int k = 0; k < 6; ++k) {
            const uint32_t shares = game.memory.u32(::bmh::layout::FundBlock + uint32_t(k) * 0x1c);
            const uint32_t unit = uint32_t(shares) * uint32_t(kShareUnits[size_t(k)]) / 10000;
            const uint32_t owned = game.memory.u16(::bmh::layout::Advertising_OriginalShareAdjustment_RuntimeField + uint32_t(manager) * 2 + uint32_t(k) * 0x1c);
            amount = wrap_add(amount, signed32(unit * owned));
        }
    }
    for (uint32_t other = 0; other <= manager_count; ++other) {
        if (!income) {
            const uint32_t row = other * 0x18 + uint32_t(manager) * 0x78;
            amount = wrap_add(amount, wrap_neg(mem_i32(game, ::bmh::layout::Loans + row + 8)));
        } else if (other != 0) {
            const uint32_t row = uint32_t(manager + 1) * 0x18 + (other - 1) * 0x78;
            amount = wrap_add(amount, mem_i32(game, ::bmh::layout::Loans + row + 8));
        }
    }
    return amount;
}

int32_t original_finance_adjustment(const GameState& game, int manager) {
    // DailyInterest2(0): same record arithmetic as daily interest, with all booking,
    // news, and random branches skipped. Record +0x314 balance, +0x318 deposit,
    // +0x31c credit line; -0x8e94 is -36500.
    constexpr int32_t kAnnualRate = -0x8e94;
    const size_t rec = size_t(manager) * kManagerRecord;
    const int32_t balance = int32_t(u32(game.manager_records, rec + 0x314));
    const int32_t deposit = int32_t(u32(game.manager_records, rec + 0x318));
    const int32_t credit = int32_t(u32(game.manager_records, rec + 0x31c));
    int32_t result = 0;
    if (balance < 0) {
        const int32_t with_credit = wrap_add(balance, credit);
        if (with_credit < 0) result = wrap_mul(with_credit, 0x12) / kAnnualRate;
        int32_t below_credit = wrap_neg(credit);
        if (below_credit < balance) below_credit = balance;
        result = wrap_add(result, wrap_mul(below_credit, 0xc) / kAnnualRate);
    } else {
        result = balance / kAnnualRate;
    }
    const int32_t deposit_units = wrap_mul(deposit / 10000, 20000);
    return wrap_add(result, deposit_units / kAnnualRate);
}

std::string original_currency_text(const GameState& game, int32_t value,
                                   int flags = 0x88, std::string_view suffix = ".") {
    // ConvertCurrency multiplies in 32-bit arithmetic before signed division.
    // Do the product modulo 2^32 explicitly, then reject the two undefined
    // division cases rather than invoking C++ signed-division UB.
    const int32_t multiplier = game.memory.has(::bmh::layout::CurrencyMultiplier) ?
        signed32(game.memory.u32(::bmh::layout::CurrencyMultiplier)) : 1;
    const int32_t divisor = game.memory.has(::bmh::layout::CurrencyDivisor) ?
        signed32(game.memory.u32(::bmh::layout::CurrencyDivisor)) : 1;
    if (divisor == 0) throw FormatError("advertising: invalid Original currency divisor");
    const int32_t product = wrap_mul(value, multiplier);
    if (product == std::numeric_limits<int32_t>::min() && divisor == -1)
        throw FormatError("advertising: invalid Original currency division");
    const int32_t converted = product / divisor;
    return original_to_utf8(format_number(game, converted, flags) + " EU" + std::string(suffix));
}

AdvertisingFinancePreview original_finance_preview_impl(const GameState& game, int manager) {
    if (!game.memory.has(::bmh::layout::ActiveManager) || int(game.memory.u8(::bmh::layout::ActiveManager)) != manager)
        throw FormatError("advertising: finance preview manager is not the active Original manager");
    const size_t rec = size_t(manager) * kManagerRecord;
    constexpr std::array<uint16_t, 22> kFacilityCost{
        2000,3000,3000,8000,4000,3000,2500,2500,2500,2500,2600,2600,2600,2600,
        13000,9000,9000,8000,1000,1500,1500,2000}; // BMHExeXa, BMH.exe+BalancePlanRuntimeField5
    constexpr std::array<uint16_t, 6> kFacilityIncome{0,5000,5000,4800,2400,8000};

    int32_t income = 0;
    for (int i = 0; i < 6; ++i) {
        const uint8_t level = game.manager_records.at(rec + size_t(i));
        if (level == 0) continue;
        const int facility_level = i == 0 ? ((int(level) - 1) / 0x55 + 1) : 1;
        income = wrap_add(income, facility_level * int32_t(kFacilityIncome[size_t(i)]));
    }
    for (int slot = 0; slot < 16; ++slot)
        income = wrap_add(income, signed32(u32(game.manager_records,
            rec + kAdvertisingContracts + size_t(slot) * kContractBytes)));
    income = wrap_add(income, original_share_adjustment(game, manager, true));

    // DeskDate2 computes CareerSession_PairingsRuntimeDraw_ExecutableField5 from the day counter; month_code is its
    // already-port-tested month result. DeskState_DeskDate_RuntimeField is days-per-month (Feb corrected
    // for the current leap year), used as the preview multiplier.
    uint32_t day = game.date;
    int year_phase = 3;
    while (day > uint32_t((year_phase == 4 ? 1 : 0) + 0x16c)) {
        if (year_phase == 4) { year_phase = 0; --day; }
        day -= 0x16d;
        ++year_phase;
    }
    const int month = month_code(game.date);
    if (month < 0 || month >= 13)
        throw FormatError("advertising: invalid Original finance month");
    const int month_factor = month == 2 && year_phase == 4 ? 29 :
        std::array<int, 13>{0,31,28,31,30,31,30,31,31,30,31,30,31}[size_t(month)];
    income = wrap_add(income, wrap_neg(wrap_mul(original_finance_adjustment(game, manager), month_factor)));

    // OpenBMH deviates from the original: uses the supported youth-academy cap and its saved cost fields.
// OriginalFinancePreview(0): pure amount of BMHExeXa, record +0x854/+0x858,
    // MonthlyPay flags 0x0a (salary+bonus without statistic writes), and six
    // saved supported rule profile cost words. The cap for index 1 comes from TeamLeague3.
    int32_t expenses = 0;
    for (int i = 0; i < 14; ++i) expenses = wrap_add(expenses, kFacilityCost[size_t(i)]);
    for (int i = 14; i < 22; ++i)
        expenses = wrap_add(expenses,
            int32_t(game.manager_records.at(rec + 0x2e + size_t(i)) & 7) * kFacilityCost[size_t(i)]);
    expenses = wrap_add(expenses, signed32(u32(game.manager_records, rec + 0x854)));
    expenses = wrap_add(expenses, signed32(u32(game.manager_records, rec + 0x858)));
    constexpr size_t kBoard = 0x840, kBoardSlot = 0x58;
    for (size_t slot = 0; slot < 24; ++slot) {
        const size_t board = size_t(manager) * kBoard + slot * kBoardSlot;
        if (game.tactics.at(board + 0x3d) == 0xff) continue;
        const uint32_t bonus = uint32_t(game.tactics.at(board + 0x4e)) |
            (uint32_t(game.tactics.at(board + 0x4f) & 0x7f) << 8);
        expenses = wrap_add(expenses, signed32(u32(game.tactics, board + 0x50)));
        expenses = wrap_add(expenses, int32_t(bonus));
    }
    const int team = int(u32(game.manager_records, rec + 0x290) & 0xffff);
    const int league = team > 0x164 ? 0 : team_league(team); // TeamLeague2
    const int32_t youth_cap = league == 0 ? 1500000 : league == 1 ? 1000000 :
                              league <= 3 ? 580000 : 280000;
    for (int k = 0; k < 6; ++k) {
        int32_t item = mem_i32(game, ::bmh::layout::Spending + uint32_t(manager) * 0x24 + uint32_t(k) * 4);
        if (k == 0 || k == 2) {
            // MonthlyExpenses clears these indexes unconditionally (CMP AX,3 / CMP AX,1).
            item = 0;
        } else if (k == 1 && item != 0) {
            item = u32(game.manager_records, rec + 0x880) < uint32_t(youth_cap) ? youth_cap : 0;
        }
        expenses = wrap_add(expenses, item);
    }
    expenses = wrap_add(expenses, wrap_neg(original_share_adjustment(game, manager, false)));
    expenses = wrap_add(expenses, int32_t(game.manager_records.at(rec + 0x179)) * 1000);
    if (expenses < 0) expenses = 0;
    const int32_t balance = wrap_add(income, wrap_neg(expenses));
    return {income, expenses, balance,
            original_currency_text(game, income), original_currency_text(game, expenses),
            original_currency_text(game, balance)};
}

std::string utf8(std::string_view original) { return original_to_utf8(original); }

std::string original_advertising_amount_text(const GameState& game, int32_t value) {
    const int32_t multiplier = game.memory.has(::bmh::layout::CurrencyMultiplier) ?
        signed32(game.memory.u32(::bmh::layout::CurrencyMultiplier)) : 1;
    const int32_t divisor = game.memory.has(::bmh::layout::CurrencyDivisor) ?
        signed32(game.memory.u32(::bmh::layout::CurrencyDivisor)) : 1;
    if (divisor == 0) throw FormatError("advertising: invalid Original currency divisor");
    const int32_t product = wrap_mul(value, multiplier);
    if (product == std::numeric_limits<int32_t>::min() && divisor == -1)
        throw FormatError("advertising: invalid Original currency division");
    // FormatNumber(..., 0x108, AdvertisingRuntimeField): width eight, converted currency,
    // and the standard EU suffix emitted by the 0x100 formatter flag.
    return original_to_utf8(format_number(game, product / divisor, 0x08) + " EU");
}

AdvertisingContract contract_from_game(const GameState& game, int manager, int slot) {
    const size_t at = size_t(manager) * kManagerRecord + kAdvertisingContracts +
                      size_t(slot) * kContractBytes;
    const auto& record = game.manager_records;
    AdvertisingContract value;
    value.record_slot = slot;
    value.monthly_amount = int32_t(u32(record, at));
    std::copy_n(record.begin() + static_cast<std::ptrdiff_t>(at), value.bytes.size(), value.bytes.begin());
    value.months_remaining = record.at(at + 5);
    const uint8_t flags = record.at(at + 6);
    value.extension_months = flags & 0x7f;
    value.option_marker = (flags & 0x80) != 0;
    value.sponsor_id = record.at(at + 7);
    value.active = value.monthly_amount != 0;
    if (value.active) {
        // OriginalFinancePreview3 selects the second ten-name table only for contract index 15.
        if (value.sponsor_id >= 10)
            throw FormatError("advertising: active contract has invalid sponsor identity");
        const uint32_t sponsor_text = ::bmh::layout::Advertising_FormatError_ExecutableField3 + uint32_t(value.sponsor_id) * 4 +
                                      (slot == 15 ? 10 * 4 : 0);
        value.sponsor_name = utf8(message_text(game, sponsor_text));
        if (value.sponsor_name.empty())
            throw FormatError("advertising: active contract sponsor name is unavailable");
        value.amount_text = original_advertising_amount_text(game, value.monthly_amount);
    }
    return value;
}

void copy(Image& out, const Image& src, int sx, int sy, int x, int y, int w, int h) {
    if (sx < 0 || sy < 0 || sx + w > src.width || sy + h > src.height ||
        x < 0 || y < 0 || x + w > out.width || y + h > out.height)
        throw FormatError("advertising: original VGA crop outside image");
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx)
            out.at(x + xx, y + yy) = src.pixels[size_t(sy + yy) * src.width + sx + xx];
}

void fill_rect(Image& image, int x, int y, int width, int height, uint8_t color) {
    if (x < 0 || y < 0 || width < 0 || height < 0 || x + width > image.width || y + height > image.height)
        throw FormatError("advertising: rectangle outside image");
    for (int yy = y; yy < y + height; ++yy)
        for (int xx = x; xx < x + width; ++xx)
            image.at(xx, yy) = color;
}

void rectangle(Image& image, int x, int y, int width, int height, uint8_t color) {
    for (int xx = x; xx < x + width; ++xx) {
        image.at(xx, y) = color;
        image.at(xx, y + height - 1) = color;
    }
    for (int yy = y; yy < y + height; ++yy) {
        image.at(x, yy) = color;
        image.at(x + width - 1, yy) = color;
    }
}

}

AdvertisingFinancePreview original_finance_preview(const GameState& game, int manager) {
    return original_finance_preview_impl(game, manager);
}

AdvertisingState advertising_from_game(const GameState& game, int manager_slot, uint64_t revision) {
    if (manager_slot < 0 || manager_slot > 3 || manager_slot >= int(game.manager_teams.size()))
        throw FormatError("advertising: invalid manager slot");
    const size_t record = size_t(manager_slot) * kManagerRecord;
    if (record + kAdvertisingContracts + 16 * kContractBytes > game.manager_records.size())
        throw FormatError("advertising: manager record has no complete contract block");
    if (!game.texts) throw FormatError("advertising: original TEXTD strings are unavailable");

    AdvertisingState state;
    state.manager_slot = manager_slot;
    state.revision = revision;
    state.runtime_team_id = game.manager_teams[size_t(manager_slot)];
    if (state.runtime_team_id < 0) throw FormatError("advertising: invalid manager team");
    state.club_name = utf8(game.club_name(state.runtime_team_id));
    if (record + 0x23a > game.manager_records.size() ||
        size_t(state.runtime_team_id) * 2 + 2 > game.club_heap_78.size())
        throw FormatError("advertising: manager identity is unavailable");
    const auto manager_name_begin = game.manager_records.begin() + std::ptrdiff_t(record + 0xa8);
    const auto manager_name_end = std::find(manager_name_begin, manager_name_begin + 26, uint8_t(0));
    state.manager_name = original_to_utf8(std::string_view(
        reinterpret_cast<const char*>(&*manager_name_begin), size_t(manager_name_end - manager_name_begin)));
    state.manager_portrait = game.manager_records.at(record + 0x221);
    state.notification_ack_label = utf8(message_text(game, ::bmh::layout::Advertising_FormatError_TextPointer17));
    state.spending_blocked_text = utf8(message_text(game, ::bmh::layout::Advertising_FormatError_TextPointer21));
    state.spending_funds_text = utf8(message_text(game, ::bmh::layout::Advertising_FormatError_TextPointer18));
    state.badge_sprite = (int(game.club_heap_78[size_t(state.runtime_team_id) * 2]) |
                         (int(game.club_heap_78[size_t(state.runtime_team_id) * 2 + 1]) << 8)) & 0x1ff;
    state.title = utf8(message_text(game, ::bmh::layout::Advertising_FormatError_TextPointer20));
    if (state.title.empty()) state.title = "Werbung";
    state.original_panel = !game.memory.has(::bmh::layout::SolidPanelBackground) || game.memory.u8(::bmh::layout::SolidPanelBackground) == 0;
    state.decorated_background = !game.memory.has(::bmh::layout::PlainBackground) || game.memory.u8(::bmh::layout::PlainBackground) == 0;
    state.font_gradient = !game.memory.has(::bmh::layout::TextGradientDisabled) || game.memory.u8(::bmh::layout::TextGradientDisabled) == 0;
    state.duration_label = utf8(message_text(game, ::bmh::layout::Advertising_FormatError_TextPointer8));
    state.month_suffix = utf8(message_text(game, ::bmh::layout::Advertising_FormatError_TextPointer9));
    state.option_label = utf8(message_text(game, ::bmh::layout::Advertising_FormatError_TextPointer10));
    state.terminable_label = utf8(message_text(game, ::bmh::layout::Advertising_FormatError_TextPointer11));
    state.boards_label = utf8(message_text(game, ::bmh::layout::Advertising_FormatError_TextPointer12));
    state.offer_label = utf8(message_text(game, ::bmh::layout::Advertising_FormatError_TextPointer13));
    state.bonus_label = utf8(message_text(game, ::bmh::layout::Advertising_FormatError_TextPointer14));
    state.per_month_suffix = utf8(message_text(game, ::bmh::layout::Advertising_FormatError_TextPointer15));
    state.no_offer_text = utf8(message_text(game, ::bmh::layout::Advertising_FormatError_TextPointer6));
    state.space_available_text = utf8(message_text(game, ::bmh::layout::Advertising_FormatError_TextPointer7));
    state.cancel_label = utf8(message_text(game, ::bmh::layout::Advertising_FormatError_TextPointer2));
    state.accept_label = utf8(message_text(game, ::bmh::layout::Advertising_FormatError_TextPointer3));
    state.spending_labels = {utf8(message_text(game, ::bmh::layout::Advertising_FormatError_TextPointer4)),
                             utf8(message_text(game, ::bmh::layout::Advertising_FormatError_TextPointer5)),
                             utf8(message_text(game, ::bmh::layout::Advertising_FormatError_TextPointer5))};
    state.merchandise_calculator_title = state.spending_labels[0];
    state.budget_calculator_title = state.spending_labels[1];
    for (int id = 0; id < 10; ++id) {
        state.board_sponsor_names[size_t(id)] = utf8(message_text(game, ::bmh::layout::Advertising_FormatError_ExecutableField3 + uint32_t(id) * 4));
        state.television_sponsor_names[size_t(id)] = utf8(message_text(game, ::bmh::layout::Advertising_FormatError_ExecutableField3 + uint32_t(10 + id) * 4));
    }
    for (int display = 0; display < 16; ++display) {
        state.offers[size_t(display)] = bmh::advertising_offers(game, manager_slot, display);
    }
    for (int slot = 16; slot <= 18; ++slot)
        state.spending[size_t(slot - 16)] = bmh::advertising_spending(game, manager_slot, slot);

    for (size_t display = 0; display < state.offers.size(); ++display)
        for (const auto& offer : state.offers[display].offers)
            if (offer.raw_slot >= 0 && offer.raw_slot < 10)
                state.offer_amount_text[display][size_t(offer.raw_slot)] =
                    original_advertising_amount_text(game, int32_t(offer.monthly_amount));

    std::array<AdvertisingContract, kBoardCount> boards;
    std::array<uint32_t, kBoardCount> sort_key{};
    for (int slot = 0; slot < kBoardCount; ++slot) {
        boards[size_t(slot)] = contract_from_game(game, manager_slot, slot);
        const auto& contract = boards[size_t(slot)];
        sort_key[size_t(slot)] = uint32_t(contract.monthly_amount);
        if (contract.active && !contract.option_marker) sort_key[size_t(slot)] += 1000000;
    }
    std::array<int, kBoardCount> order{};
    for (int i = 0; i < kBoardCount; ++i) order[size_t(i)] = i;
    // AdBoardOrder orders unsigned keys using the Original partition swaps,
    // including equal keys; only the owned key/index arrays change here.
    original_contract_sort(sort_key, order, 0, kBoardCount);
    state.boards.reserve(kBoardCount);
    for (int slot : order) state.boards.push_back(std::move(boards[size_t(slot)]));
    state.shirt = contract_from_game(game, manager_slot, 14);
    state.television = contract_from_game(game, manager_slot, 15);
    state.finance_preview = original_finance_preview(game, manager_slot);
    for (size_t i = 0; i < state.finance_labels.size(); ++i)
        state.finance_labels[i] = utf8(message_text(game, ::bmh::layout::Advertising_FormatError_TextPointer16 + uint32_t(i) * 4));
    // AdvertisingFromGame: its three auxiliary displays are separate from sponsor
    // contracts. Indices 17/18 read the same two amounts charged by OriginalFinancePreview.
    if (const auto weeks = game.manager_records.at(record + 0x287); weeks != 0)
        state.auxiliary_text[0] = utf8(format_number(game, weeks, 0x81) + message_text(game, ::bmh::layout::Advertising_FormatError_TextPointer19));
    for (size_t i = 1; i < state.auxiliary_text.size(); ++i)
        state.auxiliary_text[i] = original_currency_text(game,
            signed32(u32(game.manager_records, record + 0x810 + (16 + i) * 4)), 0x81);
    return state;
}

std::string advertising_state_json(const AdvertisingState& state, int selected_contract,
                                   bool close_requested, int selected_offer, bool offer_open) {
    std::ostringstream out;
    out << "{\"screen\":\"advertising\",\"read_only\":false,\"revision\":" << state.revision
        << ",\"manager_slot\":" << state.manager_slot
        << ",\"runtime_team_id\":" << state.runtime_team_id
        << ",\"club_name\":" << json_quote(state.club_name)
        << ",\"title\":" << json_quote(state.title)
        << ",\"original_panel\":" << (state.original_panel ? "true" : "false")
        << ",\"decorated_background\":" << (state.decorated_background ? "true" : "false")
        << ",\"finance_preview\":{\"income\":" << state.finance_preview.income
        << ",\"expenses\":" << state.finance_preview.expenses
        << ",\"balance\":" << state.finance_preview.balance
        << ",\"income_text\":" << json_quote(state.finance_preview.income_text)
        << ",\"expenses_text\":" << json_quote(state.finance_preview.expenses_text)
        << ",\"balance_text\":" << json_quote(state.finance_preview.balance_text)
        << ",\"source\":\"FinanceTotalsFromGame3(0)/OriginalFinancePreview(0)\"}"
        << ",\"spending_views\":[";
    for (size_t i = 0; i < state.spending.size(); ++i) {
        if (i) out << ',';
        const auto& v = state.spending[i];
        out << "{\"manager\":" << v.manager << ",\"target_slot\":" << v.target_slot
            << ",\"current_amount\":" << v.current_amount << ",\"maximum_amount\":" << v.maximum_amount
            << ",\"delivery_weeks\":" << unsigned(v.delivery_weeks)
            << ",\"available\":" << (v.available ? "true" : "false") << '}';
    }
    out << "],\"notification_text\":" << json_quote(state.notification_text)
        << ",\"notification_warning\":" << (state.notification_warning ? "true" : "false")
        << ",\"selected_contract\":" << selected_contract
        << ",\"offer_open\":" << (offer_open ? "true" : "false")
        << ",\"selected_offer\":" << selected_offer
        << ",\"close_requested\":" << (close_requested ? "true" : "false")
        << ",\"boards\":[";
    auto write_contract = [&](const AdvertisingContract& value) {
        out << "{\"record_slot\":" << value.record_slot << ",\"sponsor_id\":" << value.sponsor_id
            << ",\"sponsor_name\":" << json_quote(value.sponsor_name)
            << ",\"amount_text\":" << json_quote(value.amount_text)
            << ",\"active\":" << (value.active ? "true" : "false")
            << ",\"monthly_amount\":" << value.monthly_amount
            << ",\"months_remaining\":" << unsigned(value.months_remaining)
            << ",\"extension_months\":" << unsigned(value.extension_months)
            << ",\"option_marker\":" << (value.option_marker ? "true" : "false") << '}';
    };
    for (size_t i = 0; i < state.boards.size(); ++i) {
        if (i) out << ',';
        write_contract(state.boards[i]);
    }
    out << "],\"shirt\":";
    write_contract(state.shirt);
    out << ",\"television\":";
    write_contract(state.television);
    out << ",\"offer_views\":[";
    for (size_t i = 0; i < state.offers.size(); ++i) {
        if (i) out << ',';
        const auto& view = state.offers[i];
        out << "{\"display_slot\":" << view.display_slot << ",\"contract_slot\":" << view.contract_slot
            << ",\"pool_group\":" << view.pool_group << ",\"free_boards\":" << view.free_boards << ",\"status\":"
            << unsigned(view.status) << ",\"offers\":[";
        for (size_t j = 0; j < view.offers.size(); ++j) {
            if (j) out << ',';
            const auto& offer = view.offers[j];
            out << "{\"raw_slot\":" << offer.raw_slot << ",\"amount\":" << offer.monthly_amount
                << ",\"bundle_units\":" << unsigned(offer.bundle_units)
                << ",\"months\":" << unsigned(offer.months) << ",\"sponsor_id\":" << unsigned(offer.sponsor_id)
                << ",\"present\":" << (offer.present ? "true" : "false")
                << ",\"can_accept\":" << (offer.can_accept ? "true" : "false") << '}';
        }
        out << "]}";
    }
    out << ']';
    out << '}';
    return out.str();
}

AdvertisingScreen::AdvertisingScreen(const std::filesystem::path& data, AdvertisingState state,
                                     uint64_t request_id_seed)
    : state_(std::move(state)), exe_(load_le(asset_path(data, "BMH.exe"))),
      texts_(load_textd(asset_path(data, "TEXTD.BLK"))),
      font_(load_font(asset_path(data / "F", "BMH3.FNT"))),
      large_font_(load_font(asset_path(data / "F", "BMH2.FNT"))),
      sprite_font_(data),
      desktop_(load_vga(asset_path(data / "KB", "129.VGA"))),
      art_(load_vga(asset_path(data / "KB", "4.VGA"))),
      logos_(load_vga(asset_path(data / "KB", "34.VGA"))),
      panel_(load_vga(asset_path(data / "KB", "56.VGA"))),
      widgets_(load_vga(asset_path(data / "KB", "0.VGA"))),
      calculator_(load_vga(asset_path(data / "KB", "6.VGA"))),
      faces_(load_vga(asset_path(data / "KB", "26.VGA"))), next_request_id_(request_id_seed) {
    if (state_.manager_slot < 0 || state_.manager_slot > 3 || state_.runtime_team_id < 0 || state_.boards.size() != kBoardCount ||
        desktop_.width != 320 || desktop_.height != 240 || art_.width < 320 || art_.height < 197 ||
        logos_.width < 110 || logos_.height < 231 || panel_.width < 314 || panel_.height < 203 ||
        widgets_.width != 77 || widgets_.height != 112 || calculator_.width < 113 ||
        calculator_.height < 150 || faces_.width < 228 || faces_.height < 76 ||
        request_id_seed == 0 || state_.manager_portrait < 0 || state_.manager_portrait > 11 ||
        state_.badge_sprite < 0 || state_.badge_sprite > 0x1ff)
        throw FormatError("advertising screen: invalid owned state or original assets");
    (void)utf8_to_original(state_.title);
    (void)utf8_to_original(state_.club_name);
    (void)utf8_to_original(state_.manager_name);
    (void)utf8_to_original(state_.notification_text);
    (void)utf8_to_original(state_.notification_ack_label);
    (void)utf8_to_original(state_.spending_blocked_text);
    (void)utf8_to_original(state_.spending_funds_text);
    (void)utf8_to_original(state_.merchandise_calculator_title);
    (void)utf8_to_original(state_.budget_calculator_title);
    if (source_badge_fallback(state_.runtime_team_id)) badge_atlas_ = widgets_;
    else badge_atlas_ = load_vga(asset_path(data / "W", std::to_string(state_.badge_sprite >> 3) + ".VGA"));
    for (const auto& value : {state_.finance_preview.income_text,
                              state_.finance_preview.expenses_text,
                              state_.finance_preview.balance_text})
        (void)utf8_to_original(value);
    for (const auto& value : state_.finance_labels) (void)utf8_to_original(value);
    for (const auto& value : state_.auxiliary_text) (void)utf8_to_original(value);
    for (const auto& value : {state_.duration_label, state_.month_suffix, state_.option_label,
                              state_.terminable_label, state_.boards_label, state_.offer_label,
                              state_.bonus_label, state_.per_month_suffix, state_.no_offer_text,
                              state_.space_available_text, state_.cancel_label, state_.accept_label})
        (void)utf8_to_original(value);
    for (const auto& value : state_.board_sponsor_names) (void)utf8_to_original(value);
    for (const auto& value : state_.television_sponsor_names) (void)utf8_to_original(value);
    for (const auto& value : state_.spending_labels) (void)utf8_to_original(value);
    for (size_t i = 0; i < state_.spending.size(); ++i) {
        const auto& view = state_.spending[i];
        if (view.manager != state_.manager_slot || view.target_slot != 16 + int(i))
            throw FormatError("advertising screen: invalid spending projection identity");
    }
    for (size_t i = 0; i < state_.offers.size(); ++i) {
        const auto& view = state_.offers[i];
        if (view.display_slot != int(i) || view.manager != state_.manager_slot || view.offers.empty())
            throw FormatError("advertising screen: invalid Original offer projection");
        if (view.contract_slot < 0 || view.contract_slot > 15 ||
            view.contract_bytes.size() != 12 || view.offers.size() != (i == 15 ? 5u : 10u))
            throw FormatError("advertising screen: malformed Original offer list");
    }
    std::array<bool, kBoardCount> seen{};
    const auto validate_contract = [](const AdvertisingContract& value, int slot) {
        if (value.record_slot != slot || value.active != (value.monthly_amount != 0) ||
            (value.active && (value.sponsor_id < 0 || value.sponsor_id >= 10 || value.sponsor_name.empty())))
            throw FormatError("advertising screen: invalid owned contract identity");
        (void)utf8_to_original(value.sponsor_name);
        (void)utf8_to_original(value.amount_text);
    };
    for (const auto& value : state_.boards) {
        if (value.record_slot < 0 || value.record_slot >= kBoardCount || seen[size_t(value.record_slot)])
            throw FormatError("advertising screen: duplicate or invalid board identity");
        seen[size_t(value.record_slot)] = true;
        validate_contract(value, value.record_slot);
    }
    validate_contract(state_.shirt, 14);
    validate_contract(state_.television, 15);
}

void AdvertisingScreen::text(Image& image, int x, int baseline, std::string_view value,
                             uint8_t color, int max_width, bool use_gradient) const {
    std::string bytes;
    try { bytes = utf8_to_original(value); }
    catch (const FormatError&) {
        for (unsigned char c : value) bytes += c < 128 ? char(c) : '?';
    }
    while (!bytes.empty() && font_.text_width(bytes) > max_width) bytes.pop_back();
    const int top = baseline - font_.baseline;
    if (x < 0 || top < 0 || x + font_.text_width(bytes) > image.width ||
        top + font_.height > image.height) return;
    if (!use_gradient || !state_.font_gradient || exe_.u8(::bmh::layout::TextGradientMode) != 2 || color == 0x40) {
        font_.draw(image, x, top, bytes, color);
        return;
    }
    // DrawnFirstBy -> Mask: preserve the FNT3 mask and fill each
    // foreground scanline with the EXE lookup color; no shadow for param7=-1.
    unsigned slot = 0;
    while (slot < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + slot) != color) ++slot;
    int pen_x = x;
    for (unsigned char ch : bytes) {
        const auto& glyph = font_.glyph(ch);
        for (int row = 0; row < font_.height; ++row) {
            const uint8_t ink = exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + slot * 6 + row);
            if (!ink) continue;
            for (int col = 0; col < glyph.bit_width; ++col) {
                const int bit = glyph.bit_offset + col;
                if (font_.bitmap[size_t(row) * font_.modulo + bit / 8] & (0x80 >> (bit % 8))) {
                    const int px = pen_x + col;
                    if (px >= 0 && px < image.width) image.at(px, top + row) = ink;
                }
            }
        }
        pen_x += glyph.advance;
    }
}

Image AdvertisingScreen::render() const {
    Image image = state_.decorated_background ? desktop_ : Image(320, 240);
    if (!state_.decorated_background) {
        fill_rect(image, 0, 4, 320, 13, 0x44);
        fill_rect(image, 1, 5, 318, 11, 0x39);
        fill_rect(image, 0, 228, 320, 10, 0x44);
        fill_rect(image, 1, 229, 318, 8, 0x39);
    }
    // CheckAdvertisingRenewal draws KB/4.VGA's 320x53 slot strip from source (0,0x90) to (0,0x13).
    copy(image, art_, 0, 0x90, 0, 0x13, 0x140, 0x35);
    // CheckAdvertisingRenewal restores the five fixed art panels from KB/4.VGA.
    for (uint32_t i = 0; i < 5; ++i)
        copy(image, art_, exe_.u8(::bmh::layout::Advertising_Render_RuntimeField5 + i), exe_.u8(::bmh::layout::Advertising_Render_RuntimeField6 + i),
             exe_.u16(::bmh::layout::Advertising_Render_RuntimeField + i * 2), exe_.u16(::bmh::layout::Advertising_Render_RuntimeField2 + i * 2),
             exe_.u16(::bmh::layout::Advertising_Render_RuntimeField3 + i * 2), exe_.u16(::bmh::layout::Advertising_Render_RuntimeField4 + i * 2));

    // LoadGameEndTaxAnimationAssets draws its original panel from KB/56.VGA, or palette 0x42
    // when SolidPanelBackground is set. Border is retained through the original crop.
    if (state_.original_panel) {
        copy(image, panel_, 82, 130, 83, 150, 231, 73);
    } else {
        fill_rect(image, 82, 149, 232, 74, 0x42);
    }
    // PaintsFourBorderEdges orders left, top, bottom, right; the later dark edges
    // overwrite their shared corners, including (82,223).
    for (int y = 149; y <= 223; ++y) image.at(82, y) = 0x46;
    for (int x = 82; x <= 314; ++x) image.at(x, 149) = 0x46;
    for (int x = 82; x <= 314; ++x) image.at(x, 223) = 0x40;
    for (int y = 149; y <= 223; ++y) image.at(314, y) = 0x40;

    const auto brand = [&](const AdvertisingContract& contract, bool television)
        -> std::optional<std::pair<int, int>> {
        if (!contract.active) return std::nullopt;
        const int sx = television ? 32 : 0;
        const int sy = contract.sponsor_id * 22;
        if (sx + 32 <= logos_.width && sy + 22 <= logos_.height)
            return std::pair{sx, sy};
        throw FormatError("advertising screen: sponsor logo outside KB/34.VGA");
    };
    const auto& exe = exe_;
    for (int i = 0; i < kBoardCount; ++i) {
        const auto& contract = state_.boards[size_t(i)];
        const int x = exe.u16(::bmh::layout::Advertising_Click_RuntimeField3 + uint32_t(i) * 2);
        const int y = exe.u16(::bmh::layout::Advertising_Click_RuntimeField4 + uint32_t(i) * 2);
        const int width = exe.u16(::bmh::layout::Advertising_Click_RuntimeField5 + uint32_t(i) * 2);
        const int height = exe.u16(::bmh::layout::Advertising_Click_RuntimeField6 + uint32_t(i) * 2);
        if (contract.active) {
            const auto logo = brand(contract, false).value();
            copy(image, logos_, logo.first, logo.second, x, y, std::min(32, width), std::min(22, height));
        } else {
            fill_rect(image, x, y, width, height, 0x57); // OriginalFinancePreview2: empty slot color
        }
    }

    // Lower source panels are distinct original layouts for jersey and TV rights.
    // Their locations/asset crops are kept out of the board-index hitbox API because
    // clicks there originally enter negotiation rather than merely select a record.
    if (state_.shirt.active) {
        const int id = state_.shirt.sponsor_id;
        const int sx = id <= 2 ? 32 : 71;
        const int sy = id <= 2 ? 0x6e + id * 0x1f : (id - 3) * 0x1f;
        copy(image, logos_, sx, sy, exe.u16(::bmh::layout::Advertising_FormatError_RuntimeField), exe.u16(::bmh::layout::Advertising_FormatError_RuntimeField3), 36, 26);
    }
    if (state_.television.active) {
        const auto logo = brand(state_.television, true).value();
        copy(image, logos_, logo.first, logo.second, exe.u16(::bmh::layout::Advertising_FormatError_RuntimeField2), exe.u16(::bmh::layout::Advertising_FormatError_RuntimeField4), 31, 22);
    }

    // PlainWindowFrameUsedBy: Mode 5 for both title and managed club, restoring Mode 3 afterwards.
    sprite_font_.draw(image, SpriteFont::centered_title_x(sprite_font_.text_width(state_.title)), 13, state_.title);
    sprite_font_.draw(image, 5, 235, state_.club_name);

    // AdvertisingFromGame centers between the Original hitbox edges. Its index16
    // weeks label sits at y+16; currency labels 17/18 sit at y+8.
    for (size_t i = 0; i < state_.auxiliary_text.size(); ++i) {
        if (state_.auxiliary_text[i].empty()) continue;
        const uint32_t at = uint32_t(16 + i) * 2;
        const int left = exe_.u16(::bmh::layout::Advertising_Click_RuntimeField3 + at), width = exe_.u16(::bmh::layout::Advertising_Click_RuntimeField5 + at);
        const auto bytes = utf8_to_original(state_.auxiliary_text[i]);
        text(image, left + width / 2 - font_.text_width(bytes) / 2,
             exe_.u16(::bmh::layout::Advertising_Click_RuntimeField4 + at) + (i == 0 ? 16 : 8), state_.auxiliary_text[i],
             i == 0 ? 0x5f : 0x6c, width);
    }

    text(image, 95, 214, state_.finance_labels[0], 0x6c);
    text(image, 137, 214, exe_.cstring(::bmh::layout::Advertising_FormatError_TextPointer), 0x6b);
    text(image, 95, 222, state_.finance_labels[1], 0x6c);
    text(image, 137, 222, exe_.cstring(::bmh::layout::Advertising_FormatError_TextPointer), 0x6b);
    text(image, 219, 218, state_.finance_labels[2], 0x6c);

    // Utf8ToOriginal/Utf8ToOriginal2 right-align 0x188-formatted amounts to x=198
    // at baselines 214/222; the balance starts at x=254, baseline 218.
    const auto draw_finance = [&](std::string_view value, int right, int baseline) {
        std::string bytes;
        try { bytes = utf8_to_original(value); }
        catch (const FormatError&) { for (unsigned char c : value) bytes += c < 128 ? char(c) : '?'; }
        text(image, right - font_.text_width(bytes), baseline, value, 0x6b, 198);
    };
    draw_finance(state_.finance_preview.income_text, 198, 214);
    draw_finance(state_.finance_preview.expenses_text, 198, 222);
    text(image, 254, 218, state_.finance_preview.balance_text, 0x6b, 64);

    if (selected_contract_ >= 0) {
        const auto& contract = selected_contract_ < kBoardCount
            ? state_.boards[size_t(selected_contract_)]
            : selected_contract_ == 14 ? state_.shirt : state_.television;
        if (selected_contract_ < 16) {
            const int i = selected_contract_;
            rectangle(image, exe.u16(::bmh::layout::Advertising_Click_RuntimeField3 + uint32_t(i) * 2), exe.u16(::bmh::layout::Advertising_Click_RuntimeField4 + uint32_t(i) * 2),
                      exe.u16(::bmh::layout::Advertising_Click_RuntimeField5 + uint32_t(i) * 2), exe.u16(::bmh::layout::Advertising_Click_RuntimeField6 + uint32_t(i) * 2), 0x27);
        }
        const auto& offer_list = state_.offers[size_t(selected_contract_)];
        const bmh::AdvertisingOfferCandidate* offer = nullptr;
        if (offer_open_ && selected_offer_ >= 0)
            for (const auto& candidate : offer_list.offers)
                if (candidate.raw_slot == selected_offer_) { offer = &candidate; break; }
        const bool show_offer = offer && offer->present && offer->monthly_amount != 0;
        const bool show_contract = !offer_open_ && contract.active;
        int sponsor_id = show_offer ? int(offer->sponsor_id) : contract.sponsor_id;
        std::string sponsor_name = show_offer
            ? ((offer_list.pool_group == 2 ? state_.television_sponsor_names : state_.board_sponsor_names)
                   [size_t(std::clamp(sponsor_id, 0, 9))])
            : contract.sponsor_name;
        if (show_offer || show_contract) {
            text(image, 156, 159, sponsor_name, 0x5f, 154);
            if (sponsor_id >= 0 && sponsor_id < 10) {
                const int sx = offer_list.pool_group == 2 ? 32 : 0;
                const int sy = sponsor_id * 22;
                rectangle(image, 101, 160, 34, 24, 0x75);
                copy(image, logos_, sx, sy, 102, 161, 32, 22);
                if (offer_open_) rectangle(image, 101, 160, 34, 24, 0x27);
            }
            const int months = show_offer ? int(offer->months) : int(contract.months_remaining);
            const uint8_t extension = show_offer ? uint8_t(offer->bytes[6] & 0x7f) :
                                                     uint8_t(contract.bytes[6] & 0x7f);
            const std::string duration_value = original_to_utf8(exe_.cstring(::bmh::layout::Advertising_FormatError_ExecutableField)) +
                original_two_digit(months) +
                (extension != 0 ? original_to_utf8(exe_.cstring(::bmh::layout::Advertising_FormatError_ExecutableField2)) +
                                  original_two_digit(months + extension)
                                : std::string()) + state_.month_suffix +
                ((months > 1 || extension != 0) ? original_to_utf8(exe_.cstring(::bmh::layout::Advertising_String_ExecutableField)) : std::string());
            text(image, 156, 169, state_.duration_label, 0x6c, 34);
            text(image, 191, 169, duration_value, 0x6c, 100);
            if (offer_list.pool_group == 0) {
                int board_count = show_offer ? int(offer->bundle_units) : 0;
                if (show_contract) {
                    const uint8_t group_id = contract.bytes[4];
                    for (const auto& board : state_.boards)
                        if (board.active && board.bytes[4] == group_id) ++board_count;
                }
                text(image, 156, 177, state_.boards_label, 0x6c, 34);
                text(image, 191, 177, original_to_utf8(exe_.cstring(::bmh::layout::Advertising_FormatError_ExecutableField)) + original_two_digit(board_count), 0x6c, 100);
            }
            text(image, 156, 185, show_offer ? state_.offer_label : state_.bonus_label, 0x6c, 34);
            text(image, 191, 185, original_to_utf8(exe_.cstring(::bmh::layout::Advertising_FormatError_ExecutableField)) + (show_offer
                     ? state_.offer_amount_text[size_t(selected_contract_)][size_t(selected_offer_)]
                     : contract.amount_text), 0x6c, 100);
            if (!offer_open_ && contract.active && contract.months_remaining == 0 &&
                contract.extension_months != 0)
                text(image, 275, 169, state_.option_label, 0x6c, 39);
            else if (!offer_open_ && contract.active && contract.option_marker)
                text(image, 275, 169, state_.terminable_label, 0x6c, 39);
        } else if (offer_open_) {
            text(image, 156, 169, state_.no_offer_text, 0x6c, 154);
            if (offer && offer->sponsor_id < 10) {
                const auto& names = offer_list.pool_group == 2 ? state_.television_sponsor_names : state_.board_sponsor_names;
                text(image, 156, 159, names[offer->sponsor_id], 0x5f, 154);
                copy(image, logos_, offer_list.pool_group == 2 ? 32 : 0,
                     int(offer->sponsor_id) * 22, 102, 161, 32, 22);
                rectangle(image, 101, 160, 34, 24, 0x27);
            }
        } else if (!contract.active) {
            text(image, 156, 169, state_.space_available_text, 0x6c, 154);
        }
        if (offer_open_) {
            // OriginalFinancePreview3 copies the two original raw-slot arrows from KB/34.VGA.
            copy(image, logos_, 32, 203, 138, 158, 11, 14);

            copy(image, logos_, 32, 217, 138, 174, 11, 14);

            button(image, 89, 191, 66, state_.cancel_label);
            button(image, 164, 191, 66, state_.accept_label);
        }
    }
    if (selected_spending_slot_ >= 16 && selected_spending_slot_ <= 18) {
        const uint32_t i = uint32_t(selected_spending_slot_);
        rectangle(image, exe_.u16(::bmh::layout::Advertising_Click_RuntimeField3 + i * 2), exe_.u16(::bmh::layout::Advertising_Click_RuntimeField4 + i * 2),
                  exe_.u16(::bmh::layout::Advertising_Click_RuntimeField5 + i * 2), exe_.u16(::bmh::layout::Advertising_Click_RuntimeField6 + i * 2), 0x27);
    }
    if (spending_calculator_open_ && spending_backdrop_) image = *spending_backdrop_;
    if (notification_open()) draw_notification(image);
    else if (spending_calculator_open_) draw_spending_calculator(image);
    return image;
}

void AdvertisingScreen::button(Image& image, int x, int y, int width, std::string_view label) const {
    // FormatError3's normal KB/0 bevel. Source promotes any wide button shorter
    // than ten pixels to a ten-pixel button before tiling its centre.
    constexpr int height = 10;
    copy(image, widgets_, 24, 74, x, y, 31, height);
    int used = 31;
    while (width - used > 20) {
        const int count = std::min(8, width - used - 20);
        copy(image, widgets_, 24, 84, x + used, y, count, height);
        used += count;
    }
    copy(image, widgets_, 45 - (width - used), 94, x + used, y, width - used, height);
    if (!label.empty())
        sprite_font_.draw(image, x + width / 2 - sprite_font_.text_width(label) / 2, y + 7,
                          label, exe_.u8(::bmh::layout::Advertising_Button_RuntimeField));
}

void AdvertisingScreen::pattern_rect(Image& out, int x, int y, int width, int height) const {
    for (int yy = 0; yy < height; ++yy)
        for (int xx = 0; xx < width; ++xx)
            out.at(x + xx, y + yy) = exe_.u8(::bmh::layout::Advertising_PatternRect_RuntimeField + size_t(yy & 15) * 16 + size_t(xx & 15));
}

void AdvertisingScreen::source_frame(Image& out, int x, int y, int width, int height, bool shadow) const {
    if (shadow) { fill_rect(out, x + 2, y + height, width, 2, 0x40); fill_rect(out, x + width, y + 2, 2, height, 0x40); }
    pattern_rect(out, x + 4, y + 4, width - 7, height - 7);
    copy(out, widgets_, 0, 0, x, y, 4, 4);
    copy(out, widgets_, 4, 0, x + width - 4, y, 4, 4);
    copy(out, widgets_, 0, 4, x, y + height - 4, 4, 4);
    copy(out, widgets_, 4, 4, x + width - 4, y + height - 4, 4, 4);
    for (int at = 4; at < width - 4; at += 74) {
        const int n = std::min(74, width - 4 - at);
        copy(out, widgets_, 0, 104, x + at, y, n, 4);
        copy(out, widgets_, 0, 108, x + at, y + height - 4, n, 4);
    }
    for (int at = 4; at < height - 4; at += 96) {
        const int n = std::min(96, height - 4 - at);
        copy(out, widgets_, 0, 8, x, y + at, 4, n);
        copy(out, widgets_, 4, 8, x + width - 4, y + at, 4, n);
    }
}

void AdvertisingScreen::draw_spending_calculator(Image& out) const {
    constexpr int x0 = 103, y0 = 55;
    if (calculator_.width < 113 || calculator_.height < 150) return;
    copy(out, calculator_, 0, 0, x0, y0, 113, 136);
    fill_rect(out, x0 + 113, y0 + 2, 2, 136, 0x40);
    fill_rect(out, x0 + 2, y0 + 136, 113, 2, 0x40);
    const std::string& title = selected_spending_slot_ == 16 ? state_.merchandise_calculator_title :
                                                               state_.budget_calculator_title;
    const std::string bytes = utf8_to_original(title);
    const int title_width = font_.text_width(bytes);
    text(out, x0 + 8 + (0x69 - 8) / 2 - title_width / 2, y0 + 14, title, 98, 98);
    constexpr std::string_view keys = "0123456789DRCJN";
    for (int i = 0; i < 15; ++i) {
        const int x = x0 + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + size_t(i) * 2);
        const int y = y0 + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + size_t(i));
        copy(out, calculator_, 0, 136, x, y, 15, 14);
        // Render calls the raw font renderer for key legends.
        text(out, x + 5, y + 9, keys.substr(size_t(i), 1), 98, 15, false);
    }
    for (int i = 0; i < 8; ++i) {
        const int x = x0 + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + size_t(i + 15) * 2);
        const int y = y0 + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + size_t(i + 15));
        const std::string value = original_grouped_number(spending_presets_[size_t(i)]);
        text(out, x + 47 - font_.text_width(utf8_to_original(value)), y + 12, value, 98, 53);
    }
    const std::string value = original_grouped_number(spending_input_);
    text(out, x0 + 106 - font_.text_width(utf8_to_original(value)), y0 + 110, value, 0, 57);
}

void AdvertisingScreen::draw_notification(Image& out) const {
    TaxDialogProjection notice;
    notice.manager_name=utf8_to_original(state_.manager_name);
    notice.portrait=state_.manager_portrait;
    notice.badge_sprite=state_.badge_sprite;
    notice.badge_fallback=source_badge_fallback(state_.runtime_team_id);
    notice.body=utf8_to_original(state_.notification_text);
    notice.source_manager_flags=state_.notification_warning?0x40:0x80;
    draw_source_dialog(out,{widgets_,faces_,badge_atlas_?&*badge_atlas_:nullptr,
        font_,large_font_,exe_,sprite_font_,texts_},notice,state_.font_gradient);
}

bool AdvertisingScreen::dismiss_notification() {
    if (!notification_open()) return false;
    notification_dismissed_ = true;
    return true;
}

void AdvertisingScreen::open_spending_calculator(int display_slot) {
    if (display_slot < 16 || display_slot > 18) return;
    const auto& view = state_.spending[size_t(display_slot - 16)];
    selected_spending_slot_ = display_slot;
    if (!view.available) {
        // Source reports the merchandise-delivery gate with its campaign
        // notice. Other invalid projections have no evidenced user message.
        if (display_slot == 16 && view.delivery_weeks != 0 && !state_.spending_blocked_text.empty()) {
            state_.notification_text = state_.spending_blocked_text;
            state_.notification_warning = true;
            notification_dismissed_ = false;
        }
        return;
    }
    spending_maximum_ = view.maximum_amount;
    // The original calculator's maximum-key path clamps a legacy stored value
    // above the allowed range when it opens (BalanceFromGame).
    spending_input_ = std::min(view.current_amount, spending_maximum_);
    spending_initial_ = spending_input_;
    if (display_slot == 16) {
        for (size_t i = 0; i < 6; ++i) spending_presets_[i] = 50000u * uint32_t(i + 1);
    } else {
        for (size_t i = 0; i < 6; ++i) spending_presets_[i] = 5000u * uint32_t(i + 1);
    }
    spending_presets_[6] = 0;
    spending_presets_[7] = view.maximum_amount;
    spending_backdrop_ = render();
    spending_calculator_open_ = true;
}

void AdvertisingScreen::close_spending_calculator() {
    spending_calculator_open_ = false;
    spending_backdrop_.reset();
}

bool AdvertisingScreen::accept_spending_calculator() {
    if (!spending_calculator_open_ || pending_ || selected_spending_slot_ < 16 || selected_spending_slot_ > 18)
        return false;
    const auto& view = state_.spending[size_t(selected_spending_slot_ - 16)];
    if (!view.available || spending_input_ > spending_maximum_) return false;
    if ((selected_spending_slot_ == 16 && spending_input_ == 0) ||
        (selected_spending_slot_ != 16 && spending_input_ == view.current_amount)) {
        close_spending_calculator();
        return true;
    }
    if (next_request_id_ == 0 || next_request_id_ == UINT64_MAX)
        throw FormatError("advertising: UI request id exhausted");
    AdvertisingRequest request;
    request.request_id = next_request_id_++;
    request.manager_slot = state_.manager_slot;
    request.runtime_club = state_.runtime_team_id;
    request.revision = state_.revision;
    request.kind = AdvertisingActionKind::set_spending;
    request.spending.manager = state_.manager_slot;
    request.spending.target_slot = selected_spending_slot_;
    request.spending.amount = spending_input_;
    request.spending.expected_amount = view.current_amount;
    request.spending.expected_delivery_weeks = view.delivery_weeks;
    pending_ = request;
    return true;
}

bool AdvertisingScreen::text(std::string_view input) {
    if (!spending_calculator_open_ || pending_) return false;
    bool changed = false;
    for (char c : input) {
        if (c == '\b' || c == 127) {
            spending_input_ /= 10; changed = true;
        } else if (c >= '0' && c <= '9') {
            const uint64_t digit = uint64_t(c - '0');
            spending_input_ = uint32_t(std::min<uint64_t>(spending_maximum_,
                uint64_t(spending_input_) * 10 + digit));
            changed = true;
        }
    }
    return changed;
}

void AdvertisingScreen::begin_offer_browser(int display_slot) {
    selected_contract_ = display_slot;
    offer_open_ = true;
    selected_offer_ = -1;
    const auto& offers = state_.offers[size_t(display_slot)].offers;
    uint32_t highest = 0;
    for (auto it = offers.rbegin(); it != offers.rend(); ++it) {
        if (it->present && it->monthly_amount > highest) {
            highest = it->monthly_amount;
            selected_offer_ = it->raw_slot;
        }
    }
    feedback_.clear();
}

bool AdvertisingScreen::open_offers(int display_slot) {
    if (display_slot < 0 || display_slot >= 16 || pending_ || close_requested_) return false;
    if (state_.offers[size_t(display_slot)].status == bmh::AdvertisingOfferStatus::unsupported_extension)
        return false;
    const auto& offers = state_.offers[size_t(display_slot)].offers;
    const bool any_offer = std::any_of(offers.begin(), offers.end(), [](const auto& candidate) {
        return candidate.present && candidate.monthly_amount != 0;
    });
    if (!any_offer) return false;
    begin_offer_browser(display_slot);
    return true;
}

void AdvertisingScreen::submit_selected_offer() {
    if (pending_ || !offer_open_ || selected_contract_ < 0 || selected_contract_ >= 16 || selected_offer_ < 0)
        return;
    const auto& view = state_.offers[size_t(selected_contract_)];
    const auto candidate = std::find_if(view.offers.begin(), view.offers.end(), [&](const auto& value) {
        return value.raw_slot == selected_offer_;
    });
    if (candidate == view.offers.end() || !candidate->present || !candidate->can_accept ||
        candidate->monthly_amount == 0 || view.contract_slot < 0 || view.contract_slot >= 16) return;
    if (next_request_id_ == 0 || next_request_id_ == UINT64_MAX)
        throw FormatError("advertising: UI request id exhausted");
    bmh::AdvertisingOfferRequest core;
    core.manager = state_.manager_slot;
    core.display_slot = selected_contract_;
    core.contract_slot = view.contract_slot;
    core.expected_contract = view.contract_bytes;
    core.raw_slot = selected_offer_;
    core.expected_offer = candidate->bytes;
    AdvertisingRequest request;
    request.request_id = next_request_id_++;
    request.manager_slot = state_.manager_slot;
    request.runtime_club = state_.runtime_team_id;
    request.revision = state_.revision;
    request.kind = AdvertisingActionKind::accept_offer;
    request.offer = core;
    pending_ = request;
}

bool AdvertisingScreen::reject_action(std::string reason) {
    if (!pending_ || reason.empty()) return false;
    (void)utf8_to_original(reason);
    if (pending_->kind == AdvertisingActionKind::set_spending) close_spending_calculator();
    pending_.reset();
    feedback_ = std::move(reason);
    return true;
}

bool AdvertisingScreen::click(int x, int y) {
    if (close_requested_ || pending_ || x < 0 || y < 0 || x >= 320 || y >= 240) return false;
    if (notification_open()) return dismiss_notification();
    if (spending_calculator_open_) {
        constexpr int x0 = 103, y0 = 55;
        for (int i = 0; i < 15; ++i) {
            const int key_x = x0 + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + size_t(i) * 2);
            const int key_y = y0 + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + size_t(i));
            if (x < key_x || x >= key_x + 16 || y < key_y || y >= key_y + 14) continue;
            if (i < 10) return text(std::string(1, char('0' + i)));
            if (i == 10) { spending_input_ /= 10; return true; }
            if (i == 11) { spending_input_ = spending_initial_; return true; }
            if (i == 12) { spending_input_ = 0; return true; }
            if (i == 13) return accept_spending_calculator();
            if (i == 14) { close_spending_calculator(); return true; }
        }
        for (int i = 0; i < 8; ++i) {
            const int preset_x = x0 + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + size_t(i + 15) * 2);
            const int preset_y = y0 + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + size_t(i + 15));
            if (x >= preset_x && x < preset_x + 53 && y >= preset_y && y < preset_y + 14) {
                spending_input_ = spending_presets_[size_t(i)];
                return true;
            }
        }
        return false;
    }
    if (offer_open_) {
        if (x >= 89 && x <= 154 && y >= 191 && y <= 200) {
            offer_open_ = false;
            selected_offer_ = -1;
            feedback_.clear();
            return true;
        }
        if (x >= 164 && x <= 229 && y >= 191 && y <= 200) {
            submit_selected_offer();
            return pending_.has_value();
        }
        const int max_raw = state_.offers[size_t(selected_contract_)].pool_group == 2 ? 4 : 9;
        if (x >= 137 && x <= 150) {
            if (y >= 158 && y <= 171 && selected_offer_ > 0) { --selected_offer_; return true; }
            if (y >= 174 && y <= 187 && selected_offer_ >= 0 && selected_offer_ < max_raw) {
                ++selected_offer_; return true;
            }
        }
        return false;
    }
    for (int i = 0; i < 16; ++i) {
        const int left = exe_.u16(::bmh::layout::Advertising_Click_RuntimeField3 + uint32_t(i) * 2);
        const int top = exe_.u16(::bmh::layout::Advertising_Click_RuntimeField4 + uint32_t(i) * 2);
        const int right = left + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField5 + uint32_t(i) * 2);
        const int bottom = top + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField6 + uint32_t(i) * 2);
        if (x >= left && x < right && y >= top && y < bottom) {
            const auto& projected = state_.offers[size_t(i)];
            const bool renewal_due = projected.contract_slot >= 0 && projected.contract_slot < 16 &&
                (projected.contract_bytes[6] & 0x7f) != 0 && projected.contract_bytes[5] == 0;
            if (renewal_due) {
                if (next_request_id_ == 0 || next_request_id_ == UINT64_MAX)
                    throw FormatError("advertising: UI request id exhausted");
                AdvertisingRequest request;
                request.request_id = next_request_id_++;
                request.manager_slot = state_.manager_slot;
                request.runtime_club = state_.runtime_team_id;
                request.revision = state_.revision;
                request.kind = AdvertisingActionKind::check_renewal;
                request.renewal_display_slot = i;
                request.renewal_contract_slot = projected.contract_slot;
                request.renewal_expected_contract = projected.contract_bytes;
                pending_ = request;
            } else if (!open_offers(i)) {
                selected_contract_ = i;
                selected_offer_ = -1;
                offer_open_ = false;
            }
            return true;
        }
    }
    for (int i = 16; i <= 18; ++i) {
        const int left = exe_.u16(::bmh::layout::Advertising_Click_RuntimeField3 + uint32_t(i) * 2);
        const int top = exe_.u16(::bmh::layout::Advertising_Click_RuntimeField4 + uint32_t(i) * 2);
        const int right = left + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField5 + uint32_t(i) * 2);
        const int bottom = top + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField6 + uint32_t(i) * 2);
        if (x >= left && x < right && y >= top && y < bottom) {
            selected_contract_ = -1;
            selected_spending_slot_ = i;
            open_spending_calculator(i);
            return true;
        }
    }
    return false;
}

bool AdvertisingScreen::key(std::string_view value) {
    if (close_requested_ || pending_) return false;
    if (notification_open()) {
        if (value == "ENTER" || value == "ESC" || value == "KLICK") return dismiss_notification();
        return false;
    }
    if (spending_calculator_open_) {
        if (value == "ENTER" || value == "J") return accept_spending_calculator();
        if (value == "ESC" || value == "N") { close_spending_calculator(); return true; }
        if (value == "D" || value == "BACKSPACE") { spending_input_ /= 10; return true; }
        if (value == "R") { spending_input_ = spending_initial_; return true; }
        if (value == "C" || value == "CLEAR" || value == "SPACE") { spending_input_ = 0; return true; }
        return text(value);
    }
    if (value == "ESC") {
        if (offer_open_) { offer_open_ = false; selected_offer_ = -1; return true; }
        close_requested_ = true;
        return true;
    }
    return false;
}

std::string AdvertisingScreen::state_json() const {
    std::string result = advertising_state_json(state_, selected_contract_, close_requested_, selected_offer_, offer_open_);
    if (!result.empty() && result.back() == '}') result.pop_back();
    result += ",\"spending_calculator_open\":" +
        std::string(spending_calculator_open_ ? "true" : "false") +
        ",\"selected_spending_slot\":" + std::to_string(selected_spending_slot_) +
        ",\"spending_input\":" + std::to_string(spending_input_) +
        ",\"notification_open\":" + std::string(notification_open() ? "true" : "false");
    result += ",\"pending_action\":";
    if (!pending_) result += "null";
    else {
        const auto& req = *pending_;
        result += "{\"request_id\":" + std::to_string(req.request_id) +
            ",\"manager_slot\":" + std::to_string(req.manager_slot) +
            ",\"runtime_club\":" + std::to_string(req.runtime_club) +
            ",\"revision\":" + std::to_string(req.revision) +
            ",\"kind\":" + std::to_string(unsigned(req.kind));
        if (req.kind == AdvertisingActionKind::check_renewal) {
            result += ",\"display_slot\":" + std::to_string(req.renewal_display_slot) +
                ",\"contract_slot\":" + std::to_string(req.renewal_contract_slot);
        } else if (req.kind == AdvertisingActionKind::set_spending) {
            result += ",\"target_slot\":" + std::to_string(req.spending.target_slot) +
                ",\"amount\":" + std::to_string(req.spending.amount) +
                ",\"expected_amount\":" + std::to_string(req.spending.expected_amount) +
                ",\"expected_delivery_weeks\":" + std::to_string(req.spending.expected_delivery_weeks);
        } else {
            result += ",\"display_slot\":" + std::to_string(req.offer.display_slot) +
                ",\"contract_slot\":" + std::to_string(req.offer.contract_slot) +
                ",\"raw_slot\":" + std::to_string(req.offer.raw_slot);
        }
        result += '}';
    }
    result += ",\"feedback\":" + (feedback_.empty() ? std::string("null") : json_quote(feedback_)) + '}';
    return result;
}

} // namespace bmh::ui
