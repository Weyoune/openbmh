// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/bank.hpp"
#include "ui/game_end.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstdlib>
#include <sstream>

#include "formats/vga.hpp"
#include "game/desk_actions.hpp"
#include "game/messages.hpp"
#include "game/state.hpp"
#include "ui/setup.hpp"

namespace bmh::ui {
namespace {
std::string money(int64_t value) {
    const bool negative = value < 0;
    std::string result = std::to_string(negative ? -value : value);
    for (int pos = int(result.size()) - 3; pos > 0; pos -= 3) result.insert(size_t(pos), ".");
    if (negative) result.insert(result.begin(), '-');
    return result;
}
void rect(Image& image, int x, int y, int w, int h, uint8_t color) {
    for (int yy = std::max(0, y); yy < std::min(image.height, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(image.width, x + w); ++xx) image.at(xx, yy) = color;
}
void line(Image& image, int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        if (x0 >= 0 && y0 >= 0 && x0 < image.width && y0 < image.height) image.at(x0, y0) = color;
        if (x0 == x1 && y0 == y1) break;
        const int twice = 2 * err;
        if (twice >= dy) { err += dy; x0 += sx; }
        if (twice <= dx) { err += dx; y0 += sy; }
    }
}
uint16_t rd16(const std::vector<uint8_t>& v, size_t at) {
    return uint16_t(v.at(at) | uint16_t(v.at(at + 1)) << 8);
}
int32_t rd32(const std::vector<uint8_t>& v, size_t at) {
    return int32_t(uint32_t(v.at(at)) | uint32_t(v.at(at + 1)) << 8 | uint32_t(v.at(at + 2)) << 16 |
                   uint32_t(v.at(at + 3)) << 24);
}
int32_t wrap_i32(uint32_t value) { return std::bit_cast<int32_t>(value); }
int32_t wrap_sum(int32_t a, int32_t b) { return wrap_i32(uint32_t(a) + uint32_t(b)); }
void copy_region(Image& target, const Image& source, int sx, int sy, int x, int y,
                 int width, int height, bool transparent = false) {
    for (int yy = 0; yy < height; ++yy) for (int xx = 0; xx < width; ++xx) {
        const int tx = x + xx, ty = y + yy, px = sx + xx, py = sy + yy;
        if (tx < 0 || ty < 0 || tx >= target.width || ty >= target.height ||
            px < 0 || py < 0 || px >= source.width || py >= source.height) continue;
        const uint8_t color = source.pixels[size_t(py) * size_t(source.width) + size_t(px)];
        if (!transparent || color != 0xff) target.at(tx, ty) = color;
    }
}
}

BankState bank_from_game(const GameState& g, const LeImage& exe, uint64_t revision) {
    const int manager = int(g.memory.u8(::bmh::layout::ActiveManager));
    if (manager < 0 || size_t(manager) >= g.manager_teams.size()) throw FormatError("bank: invalid active manager");
    const size_t record = size_t(manager) * 0xb0c;
    if (record + 0x328 > g.manager_records.size()) throw FormatError("bank: truncated manager record");
    BankState state;
    state.manager_slot = manager;
    state.runtime_club = g.manager_teams.at(size_t(manager));
    state.revision = revision;
    state.gradient_text = g.memory.u8(::bmh::layout::TextGradientDisabled) == 0;
    const auto read = [&](size_t at) {
        return int32_t(uint32_t(g.manager_records.at(at)) | uint32_t(g.manager_records.at(at + 1)) << 8 |
                       uint32_t(g.manager_records.at(at + 2)) << 16 | uint32_t(g.manager_records.at(at + 3)) << 24);
    };
    state.balance = read(record + 0x314); state.deposit = read(record + 0x318);
    state.credit_line = read(record + 0x31c); state.investment_balance = read(record + 0x324);
    state.credit = bank_credit_context(g, exe, manager);
    // MaxSharesToBuy iterates the configured human-manager count (ManagerCount),
    // not the larger live vector which may include the hidden national coach.
    if (!g.memory.has(::bmh::layout::ManagerCount)) throw FormatError("bank: missing configured manager count");
    const int configured_managers = int(g.memory.u8(::bmh::layout::ManagerCount));
    if (configured_managers < 1 || configured_managers > 4 ||
        size_t(configured_managers) > g.manager_teams.size() || manager >= configured_managers)
        throw FormatError("bank: unsupported configured manager count");
    for (int slot = 0; slot < configured_managers && state.credit_lenders.size() < 3; ++slot) {
        if (slot == manager) continue;
        const size_t lender_record = size_t(slot) * 0xb0c;
        if (lender_record + 0x314 > g.manager_records.size())
            throw FormatError("bank: truncated lender manager record");
        const auto lender_end = std::find(g.manager_records.begin() + lender_record + 0xa8,
            g.manager_records.begin() + lender_record + 0xa8 + 26, uint8_t(0));
        BankState::CreditLender lender;
        lender.manager_slot = slot;
        lender.counterparty = slot + 1;
        lender.manager_name.assign(g.manager_records.begin() + lender_record + 0xa8, lender_end);
        lender.credit = bank_credit_context(g, exe, manager, lender.counterparty);
        state.credit_lenders.push_back(std::move(lender));
    }
    state.credit_currency = {g.memory.has(::bmh::layout::CurrencyMultiplier) ? int32_t(g.memory.u32(::bmh::layout::CurrencyMultiplier)) : 1,
                             g.memory.has(::bmh::layout::CurrencyDivisor) ? int32_t(g.memory.u32(::bmh::layout::CurrencyDivisor)) : 1};
    const auto currency = [&](int32_t amount, int flags) {
        std::string value = format_number(g, amount, flags);
        // FormatNumber appends currency (CurrencySuffixText) and the caller's Bank_FormatError_ExecutableField suffix.
        value += exe.cstring(::bmh::layout::Bank_FormatError_ExecutableField);
        return original_to_utf8(value);
    };
    state.balance_text = currency(state.balance, 0x188);
    state.deposit_text = currency(state.deposit, 0x188);
    state.account_text = currency(state.balance, 0x101);
    if (g.stocks.size() < 12u * 0x5c) throw FormatError("bank: truncated stock table");
    for (size_t i = 0; i < state.stocks.size(); ++i) {
        const size_t at = i * 0x5c;
        auto& stock = state.stocks[i];
        stock.long_term_value = rd32(g.stocks, at);
        stock.paid = rd32(g.stocks, at + 4 + size_t(manager) * 4);
        stock.price = rd16(g.stocks, at + 0x4e);
        stock.previous_price = rd16(g.stocks, at + 0x4c);
        stock.max_to_buy = stock.price == 0 ? 0 : max_shares_to_buy(g, manager, int(i));
        stock.owned = rd16(g.stocks, at + 0x50 + size_t(manager) * 2);
        stock.trend = g.stocks.at(at + 0x16);
        stock.trend_age = g.stocks.at(at + 0x59);
        for (size_t day = 0; day < stock.history.size(); ++day)
            stock.history[day] = rd16(g.stocks, at + 0x14 + day * 2);
        uint16_t maximum = 0, minimum = 2000;
        for (const uint16_t value : stock.history) {
            maximum = std::max(maximum, value);
            minimum = std::min(minimum, value);
        }
        stock.market_text = {original_to_utf8(format_number(g, stock.price, 0x44)),
            original_to_utf8(format_number(g, stock.previous_price, 0x44)),
            original_to_utf8(format_number(g, maximum, 0x44)),
            original_to_utf8(format_number(g, minimum, 0x44))};
    }
    for (size_t i = 0; i < state.funds.size(); ++i) {
        const uint32_t at = ::bmh::layout::FundBlock + uint32_t(i) * 0x1c;
        auto& fund = state.funds[i];
        fund.price = g.memory.u32(at);
        fund.paid = g.memory.u32(at + 4 + uint32_t(manager) * 4);
        fund.owned = g.memory.u16(at + 0x14 + uint32_t(manager) * 2);
        fund.max_to_buy = fund.price == 0 ? 0 : max_fund_units(g, manager, int(i));
        fund.name = original_to_utf8(message_text(g, ::bmh::layout::Bank_OriginalToUtf8_TextPointer3 + uint32_t(i) * 4));
        fund.unit_price_text = currency(wrap_i32(fund.price), 0x108);
        fund.average_paid_text = currency(fund.owned ? wrap_i32(fund.paid / fund.owned) : 0, 0x108);
        fund.quantity_text = original_to_utf8(format_number(g, fund.owned, 0x82) + exe.cstring(::bmh::layout::Bank_OriginalToUtf8_ExecutableField2));
        fund.one_quantity_text = original_to_utf8(format_number(g, 1, 0x82) + exe.cstring(::bmh::layout::Bank_OriginalToUtf8_ExecutableField2));
        const uint32_t suffix = exe.u32(::bmh::layout::Bank_OriginalToUtf8_RuntimeField + uint32_t(i) * 8);
        fund.singular_suffix = original_to_utf8(exe.cstring(suffix));
        fund.plural_suffix = original_to_utf8(exe.cstring(exe.u32(::bmh::layout::Bank_OriginalToUtf8_RuntimeField + uint32_t(i) * 8 + 4)));
    }
    // MaxSharesToBuy case 1 / HighscoreScore. The seven rows are account,
    // passbook, investment holdings, shares, outstanding borrowing, net worth,
    // and the separate overdraft limit. All inputs are copied/read-only.
    state.finance_values[0] = state.balance;
    state.finance_values[1] = state.deposit;
    int32_t investment_value = 0, investment_gain = 0;
    for (size_t i = 0; i < state.funds.size(); ++i) {
        const uint32_t price = g.memory.u32(::bmh::layout::FundBlock + uint32_t(i) * 0x1c);
        const uint32_t held = g.memory.u16(::bmh::layout::Advertising_OriginalShareAdjustment_RuntimeField + uint32_t(manager) * 2 + uint32_t(i) * 0x1c);
        const int32_t value = wrap_i32(price * held);
        investment_value = wrap_sum(investment_value, value);
        const int32_t paid = wrap_i32(g.memory.u32(::bmh::layout::Bank_OriginalToUtf8_RuntimeField5 + uint32_t(i) * 0x1c + uint32_t(manager) * 4));
        if (held) investment_gain = wrap_sum(investment_gain, wrap_i32(uint32_t(value) - uint32_t(paid)));
    }
    state.finance_values[2] = investment_value;
    state.finance_gains[0] = investment_gain;
    int32_t stock_value = 0, stock_gain = 0;
    for (size_t i = 0; i < state.stocks.size(); ++i) {
        const int32_t value = wrap_i32(uint32_t(state.stocks[i].price) * state.stocks[i].owned);
        stock_value = wrap_sum(stock_value, value);
        if (state.stocks[i].owned) stock_gain = wrap_sum(stock_gain, wrap_i32(uint32_t(value) - uint32_t(state.stocks[i].paid)));
    }
    state.finance_values[3] = stock_value;
    state.finance_gains[1] = stock_gain;
    int32_t debts = 0, debt_assets = 0;
    for (uint32_t loan = 0; loan < 5; ++loan) {
        const uint32_t at = ::bmh::layout::Analytics_SourceAssetMaskE_RuntimeField + uint32_t(manager) * 0x78 + loan * 0x18;
        const uint32_t borrowed = g.memory.u32(at);
        const uint32_t repaid = g.memory.u32(at + 0x10);
        debts = wrap_sum(debts, wrap_i32(borrowed - repaid));
        debt_assets = wrap_sum(debt_assets, wrap_i32(borrowed));
    }
    state.finance_values[4] = debts;
    int32_t net = state.balance;
    net = wrap_sum(net, state.deposit);
    net = wrap_sum(net, investment_value);
    net = wrap_sum(net, stock_value);
    net = wrap_sum(net, wrap_i32(0u - uint32_t(debt_assets)));
    state.finance_values[5] = net;
    state.finance_values[6] = state.credit_line;
    for (size_t i = 0; i < state.finance_value_text.size(); ++i)
        state.finance_value_text[i] = currency(state.finance_values[i], 0x188);
    for (size_t i = 0; i < state.finance_gains.size(); ++i) {
        const int32_t gain = state.finance_gains[i];
        const uint32_t sign_address = gain < 0 ? ::bmh::layout::Bank_OriginalToUtf8_ExecutableField5 : gain > 0 ? ::bmh::layout::Bank_OriginalToUtf8_ExecutableField6 : ::bmh::layout::Bank_OriginalToUtf8_ExecutableField7;
        const int32_t magnitude = gain < 0 ? wrap_i32(0u - uint32_t(gain)) : gain;
        state.finance_gain_text[i] = original_to_utf8(exe.cstring(sign_address) +
            format_number(g, magnitude, 0x101) + exe.cstring(::bmh::layout::Bank_OriginalToUtf8_ExecutableField3));
        // 0x101 appends; Source preserves market value and adds signed gain in parentheses.
        if (state.finance_values[i + 2] != 0)
            state.finance_value_text[i + 2] += original_to_utf8(exe.cstring(::bmh::layout::Bank_OriginalToUtf8_ExecutableField4)) + state.finance_gain_text[i];
    }
    const auto initial_credit = bank_credit_draft(state.credit);
    for (size_t i = 0; i < state.credit_values.size(); ++i)
        state.credit_values[i] = wrap_i32(initial_credit.values[i]);
    state.credit_values[0] = wrap_i32(initial_credit.values[0] - initial_credit.values[6]);
    state.credit_values[4] = wrap_i32(state.credit.saved[3] - state.credit.saved[4]);
    state.credit_values[5] = wrap_i32(state.credit.saved[5]);
    for (size_t i = 0; i < state.credit_values.size(); ++i) {
        state.credit_labels[i] = original_to_utf8(message_text(g, ::bmh::layout::Bank_OriginalToUtf8_TextPointer4 + uint32_t(i) * 4));
        std::string value = exe.cstring(::bmh::layout::Bank_OriginalToUtf8_TextPointer2);
        value += format_number(g, state.credit_values[i], 8);
        value += exe.cstring(::bmh::layout::Bank_OriginalToUtf8_ExecutableField2);
        if (i == 3 || i == 4) {
            value += message_text(g, ::bmh::layout::Bank_OriginalToUtf8_TextPointer7);
            if (state.credit_values[i] != 1) value += exe.cstring(::bmh::layout::Bank_OriginalToUtf8_TextPointer);
        } else {
            value += message_text(g, ::bmh::layout::Bank_OriginalToUtf8_TextPointer6);
        }
        state.credit_value_text[i] = original_to_utf8(value);
    }
    for (size_t i = 0; i < state.funds.size(); ++i) {
        const auto& fund = state.funds[i];
        if (!fund.owned) continue;
        state.finance_fund_labels[i] = original_to_utf8(format_number(g, fund.owned, 0x82) + exe.cstring(::bmh::layout::Bank_OriginalToUtf8_ExecutableField2) +
            message_text(g, ::bmh::layout::Bank_OriginalToUtf8_TextPointer3 + uint32_t(i) * 4) +
            exe.cstring(exe.u32(::bmh::layout::Bank_OriginalToUtf8_RuntimeField + uint32_t(i) * 8 + (fund.owned > 1 ? 4 : 0))));
        state.finance_fund_prices[i] = original_to_utf8("(" + format_number(g, wrap_i32(fund.price), 0x108) + exe.cstring(::bmh::layout::Bank_OriginalToUtf8_ExecutableField3));
    }
    for (size_t i = 0; i < state.stocks.size(); ++i) {
        const auto& stock = state.stocks[i];
        if (!stock.owned) continue;
        state.finance_stock_labels[i] = original_to_utf8(format_number(g, stock.owned, 0x84) + exe.cstring(::bmh::layout::Bank_OriginalToUtf8_ExecutableField2) +
            message_text(g, ::bmh::layout::Bank_FormatError_ExecutableField2 + uint32_t(i) * 4));
        state.finance_stock_prices[i] = original_to_utf8("(" + format_number(g, stock.price, 4) + exe.cstring(::bmh::layout::Bank_OriginalToUtf8_ExecutableField3));
    }
    const auto record_end = std::find(g.manager_records.begin() + record + 0xa8,
        g.manager_records.begin() + record + 0xa8 + 26, uint8_t(0));
    state.manager_name.assign(g.manager_records.begin() + record + 0xa8, record_end);
    if (state.runtime_club < 0 || size_t(state.runtime_club) >= g.club_ids.size()) throw FormatError("bank: invalid runtime club");
    const size_t name_at = g.club_ids.at(size_t(state.runtime_club));
    if (name_at >= g.club_names.size()) throw FormatError("bank: invalid club name offset");
    const auto name_end = std::find(g.club_names.begin() + name_at, g.club_names.end(), uint8_t(0));
    state.club_name.assign(g.club_names.begin() + name_at, name_end);
    state.manager_portrait = g.manager_records.at(record + 0x221);
    state.badge_sprite = rd16(g.club_heap_78, size_t(state.runtime_club) * 2) & 0x1ff;
    state.investment_limit_text = original_to_utf8(message_text(g, ::bmh::layout::Bank_OriginalToUtf8_TextPointer8));
    state.source = "engine_projection";
    return state;
}

std::string bank_state_json(const BankState& s) {
    std::ostringstream out;
    out << "{\"screen\":\"bank\",\"source\":" << json_quote(s.source)
        << ",\"manager_slot\":" << s.manager_slot << ",\"runtime_club\":" << s.runtime_club
        << ",\"manager_name\":" << json_quote(original_to_utf8(s.manager_name))
        << ",\"club_name\":" << json_quote(original_to_utf8(s.club_name))
        << ",\"revision\":" << s.revision << ",\"balance\":" << s.balance
        << ",\"deposit\":" << s.deposit << ",\"credit_line\":" << s.credit_line
        << ",\"investment_balance\":" << s.investment_balance << ",\"finance_values\":[";
    for (size_t i = 0; i < s.finance_values.size(); ++i) { if (i) out << ','; out << s.finance_values[i]; }
    out << "],\"finance_gains\":[" << s.finance_gains[0] << ',' << s.finance_gains[1]
        << "],\"credit_values\":[";
    for (size_t i = 0; i < s.credit_values.size(); ++i) { if (i) out << ','; out << s.credit_values[i]; }
    out << "],\"credit_available\":" << (s.credit.available ? "true" : "false")
        << ",\"credit_counterparty\":" << s.credit_counterparty << ",\"credit_lenders\":[";
    for (size_t i = 0; i < s.credit_lenders.size(); ++i) {
        if (i) out << ',';
        const auto& lender = s.credit_lenders[i];
        out << "{\"manager_slot\":" << lender.manager_slot << ",\"counterparty\":" << lender.counterparty
            << ",\"manager_name\":" << json_quote(original_to_utf8(lender.manager_name))
            << ",\"available\":" << (lender.credit.available ? "true" : "false") << '}';
    }
    out << "],\"credit_saved\":[";
    for (size_t i = 0; i < s.credit.saved.size(); ++i) { if (i) out << ','; out << s.credit.saved[i]; }
    out << "],\"stocks\":[";
    for (size_t i = 0; i < s.stocks.size(); ++i) {
        if (i) out << ',';
        const auto& stock = s.stocks[i];
        out << "{\"long_term_value\":" << stock.long_term_value << ",\"paid\":" << stock.paid
            << ",\"price\":" << stock.price << ",\"owned\":" << stock.owned
            << ",\"max_to_buy\":" << stock.max_to_buy << ",\"trend\":" << stock.trend
            << ",\"trend_age\":" << stock.trend_age << '}';
    }
    out << "],\"funds\":[";
    for (size_t i = 0; i < s.funds.size(); ++i) {
        if (i) out << ',';
        const auto& fund = s.funds[i];
        out << "{\"price\":" << fund.price << ",\"paid\":" << fund.paid << ",\"owned\":" << fund.owned
            << ",\"max_to_buy\":" << fund.max_to_buy << ",\"name\":" << json_quote(fund.name)
            << ",\"unit_price_text\":" << json_quote(fund.unit_price_text)
            << ",\"average_paid_text\":" << json_quote(fund.average_paid_text)
            << ",\"quantity_text\":" << json_quote(fund.quantity_text)
            << ",\"one_quantity_text\":" << json_quote(fund.one_quantity_text) << '}';
    }
    out << "],\"property_notice\":";
    if (s.property_notice) {
        out << "{\"asset_index\":" << s.property_notice->asset_index
            << ",\"additional_cost\":" << s.property_notice->additional_cost
            << ",\"text\":" << json_quote(s.property_notice->text) << '}';
    } else out << "null";
    out << ",\"credit_notice\":";
    if (s.credit_notice) out << "{\"refund\":" << s.credit_notice->refund << ",\"text\":"
        << json_quote(s.credit_notice->text) << '}';
    else out << "null";
    out << ",\"last_tip\":" << (s.last_tip ? std::to_string(*s.last_tip) : "null") << '}';
    return out.str();
}
std::string bank_request_json(const BankRequest& r) {
    std::ostringstream out;
    const char* kind = r.kind == BankRequestKind::move_to_deposit ? "move_to_deposit" :
        r.kind == BankRequestKind::buy_shares ? "buy_shares" : r.kind == BankRequestKind::sell_shares ? "sell_shares" :
        r.kind == BankRequestKind::stock_tip ? "stock_tip" : r.kind == BankRequestKind::buy_fund ? "buy_fund" :
        r.kind == BankRequestKind::sell_fund ? "sell_fund" :
        r.kind == BankRequestKind::bank_credit ? "bank_credit" : "none";
    out << "{\"kind\":\"" << kind
        << "\",\"manager_slot\":" << r.manager_slot << ",\"runtime_club\":" << r.runtime_club
        << ",\"revision\":" << r.revision << ",\"amount\":" << r.amount
        << ",\"asset_index\":" << r.asset_index << ",\"count\":" << r.count;
    if (r.kind == BankRequestKind::bank_credit) {
        out << ",\"expected_credit\":[";
        for (size_t i = 0; i < r.expected_credit.size(); ++i) { if (i) out << ','; out << r.expected_credit[i]; }
        out << "],\"credit_edits\":[";
        for (size_t i = 0; i < r.credit_edits.size(); ++i) {
            if (i) out << ',';
            out << "{\"field\":" << r.credit_edits[i].field << ",\"value\":" << r.credit_edits[i].value << '}';
        }
        out << "],\"accept_credit_fees\":" << (r.accept_credit_fees ? "true" : "false")
            << ",\"credit_counterparty\":" << r.credit_counterparty;
    }
    out << '}';
    return out.str();
}

BankScreen::BankScreen(const std::filesystem::path& data, BankState state)
    : state_(std::move(state)), background_(load_vga(asset_path(data / "KB", "23.VGA"))),
      deposit_frame_(load_vga(asset_path(data / "KB", "65.VGA"))),
      finance_frame_(load_vga(asset_path(data / "KB", "5.VGA"))),
      credit_lender_frame_(load_vga(asset_path(data / "KB", "64.VGA"))),
      property_art_(load_vga(asset_path(data / "KB", "7.VGA"))),
      calculator_art_(load_vga(asset_path(data / "KB", "6.VGA"))),
      widgets_(load_vga(asset_path(data / "KB", "0.VGA"))),
      faces_(load_vga(asset_path(data / "KB", "26.VGA"))),
      exe_(load_le(asset_path(data, "BMH.exe"))), texts_(load_textd(asset_path(data, "TEXTD.BLK"))),
      font_(load_font(asset_path(data / "F", "BMH3.FNT"))),
      large_font_(load_font(asset_path(data / "F", "BMH2.FNT"))), sprite_font_(data) {
    if (background_.width != 320 || background_.height != 240 || state_.manager_slot < 0 || state_.manager_slot > 3)
        throw FormatError("bank: unsupported background or state");
    if (state_.credit_counterparty != 0 || state_.credit_lenders.size() > 3)
        throw FormatError("bank: invalid initial credit lender state");
    int previous_slot = -1;
    for (const auto& lender : state_.credit_lenders) {
        if (lender.manager_slot <= previous_slot || lender.manager_slot < 0 || lender.manager_slot > 3 ||
            lender.manager_slot == state_.manager_slot || lender.counterparty != lender.manager_slot + 1 ||
            lender.credit.counterparty != lender.counterparty)
            throw FormatError("bank: invalid Source-ordered credit lender alternative");
        previous_slot = lender.manager_slot;
    }
    if (state_.credit.counterparty != 0)
        throw FormatError("bank: initial editor context must be the bank counterparty");
    // MaxSharesToBuy patches the KB/5 offscreen buffer once per available
    // lender. Subviews restore this frame; the room itself only receives the
    // first KB/64 pass below.
    if (!state_.credit_lenders.empty()) {
        if (credit_lender_frame_.width < 249 || credit_lender_frame_.height < 160 ||
            finance_frame_.width < 212 || finance_frame_.height < 142)
            throw FormatError("bank: truncated credit lender assets");
        for (size_t ordinal = 0; ordinal < state_.credit_lenders.size(); ++ordinal) {
            const size_t table = ordinal;
            const int sx = exe_.u8(::bmh::layout::Bank_FormatError_RuntimeField5 + table), sy = exe_.u8(::bmh::layout::Bank_FormatError_RuntimeField6 + table);
            const int dx = int(exe_.u8(::bmh::layout::Bank_FormatError_RuntimeField9 + table)) - 0x31;
            const int dy = int(exe_.u8(::bmh::layout::Bank_FormatError_RuntimeField10 + table)) - 0x2d;
            const int width = exe_.u8(::bmh::layout::Bank_FormatError_RuntimeField13 + table), height = exe_.u8(::bmh::layout::Bank_FormatError_RuntimeField14 + table);
            copy_region(finance_frame_, credit_lender_frame_, sx, sy, dx, dy, width, height);
        }
    }
    bank_credit_ = state_.credit;
    if (state_.manager_portrait < 0 || state_.manager_portrait > 11 || state_.badge_sprite < 0 || state_.badge_sprite > 0x1ff)
        throw FormatError("bank: invalid manager portrait or badge");
    if (source_badge_fallback(state_.runtime_club)) badge_atlas_ = widgets_;
    else badge_atlas_ = load_vga(asset_path(data / "W", std::to_string(state_.badge_sprite >> 3) + ".VGA"));
    // A retained notice belongs to the last transaction checkpoint; only the
    // matching buy_fund ACK may surface it in this screen instance.
    property_notice_open_ = false;
}
void BankScreen::restore_bank_credit() {
    state_.credit = bank_credit_;
    state_.credit_counterparty = 0;
    credit_draft_ = bank_credit_draft(state_.credit);
    refresh_credit_rows();
}
void BankScreen::text_at(Image& out, int x, int y, std::string_view value, uint8_t color) const {
    if (x < 0 || y < 0 || y + 6 > out.height || x + font_.text_width(value) > out.width) return;
    font_.draw(out, x, y, value, color);
}
void BankScreen::original_text(Image& out, int x, int baseline, std::string_view value, uint8_t color) const {
    const std::string bytes = utf8_to_original(value);
    const int top = baseline - large_font_.baseline;
    if (x < 0 || top < 0 || top + large_font_.height > out.height ||
        x + large_font_.text_width(bytes) > out.width) return;
    large_font_.draw(out, x, top, bytes, color);
}
void BankScreen::body_baseline(Image& out, int x, int baseline, std::string_view value, uint8_t color) const {
    body_baseline_raw(out, x, baseline, utf8_to_original(value), color, true);
}
void BankScreen::body_baseline_raw(Image& out, int x, int baseline, std::string_view bytes,
                                   uint8_t color, bool gradient) const {
    const std::string owned(bytes);
    const int top = baseline - font_.baseline;
    if (x < 0 || top < 0 || top + font_.height > out.height ||
        x + font_.text_width(owned) > out.width) return;
    if (!gradient || !state_.gradient_text || exe_.u8(::bmh::layout::TextGradientMode) != 2 || color == 0x40) {
        font_.draw(out, x, top, owned, color);
        return;
    }
    unsigned slot = 0;
    while (slot < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + slot) != color) ++slot;
    if (slot == 32) { font_.draw(out, x, top, owned, color); return; }
    int pen_x = x;
    for (unsigned char ch : owned) {
        const auto& glyph = font_.glyph(ch);
        for (int row = 0; row < font_.height; ++row) {
            const uint8_t ink = exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + slot * 6 + size_t(row));
            if (!ink) continue;
            for (int col = 0; col < glyph.bit_width; ++col) {
                const int bit = glyph.bit_offset + col;
                if (font_.bitmap[size_t(row) * font_.modulo + size_t(bit / 8)] & (0x80 >> (bit % 8))) {
                    const int px = pen_x + col;
                    if (px >= 0 && px < out.width) out.at(px, top + row) = ink;
                }
            }
        }
        pen_x += glyph.advance;
    }
}
void BankScreen::open_calculator(bool withdrawing) {
    calculator_stock_count_ = false;
    calculator_property_count_ = false;
    withdrawing_ = withdrawing;
    calculator_backdrop_ = render();
    calculator_open_ = true;
    calculator_max_ = uint32_t(std::clamp<int64_t>(
        withdrawing ? int64_t(state_.deposit) : int64_t(state_.balance), 0, 99999999));
    calculator_value_ = calculator_initial_ = 0;
    calculator_presets_ = {10, 100, 1000, 10000, 100000, 1000000, 0, calculator_max_};
    for (size_t i = 0; i < 6; ++i)
        if (calculator_presets_[i] > calculator_max_) calculator_presets_[i] = UINT32_MAX;
}
void BankScreen::open_stock_calculator(bool selling) {
    const auto& selected = state_.stocks.at(size_t(selected_stock_));
    if ((selling ? int(selected.owned) : selected.max_to_buy) <= 0) return;
    withdrawing_ = selling;
    calculator_backdrop_ = render();
    calculator_open_ = true;
    calculator_stock_count_ = true;
    calculator_property_count_ = false;
    const auto& stock = state_.stocks.at(size_t(selected_stock_));
    calculator_max_ = uint32_t(std::max(0, selling ? int(stock.owned) : stock.max_to_buy));
    calculator_initial_ = selling ? stock.owned : 0;
    calculator_value_ = calculator_initial_;
    calculator_presets_ = {20, 40, 60, 80, 100, 120, 0, calculator_max_};
    for (size_t i = 0; i < 6; ++i)
        if (calculator_presets_[i] > calculator_max_) calculator_presets_[i] = UINT32_MAX;
}
void BankScreen::open_property_calculator(bool selling) {
    const auto& selected = state_.funds.at(size_t(selling ? std::max(0, selected_owned_fund_) : selected_fund_));
    const int maximum = selling ? int(selected.owned) : selected.max_to_buy;
    if (maximum <= 0) return;
    withdrawing_ = selling;
    calculator_backdrop_ = render();
    calculator_open_ = true;
    calculator_stock_count_ = false;
    calculator_property_count_ = true;
    calculator_max_ = uint32_t(maximum);
    calculator_initial_ = 0;
    calculator_value_ = 0;
    calculator_presets_ = {1, 2, 3, 4, 5, 6, 0, calculator_max_};
    for (size_t i = 0; i < 6; ++i)
        if (calculator_presets_[i] > calculator_max_) calculator_presets_[i] = UINT32_MAX;
}
void BankScreen::open_credit_calculator(int field) {
    const auto bounds = bank_credit_bounds(state_.credit, credit_draft_, field);
    if (!bounds.available || bounds.maximum < bounds.minimum) return;
    calculator_stock_count_ = false;
    calculator_property_count_ = false;
    calculator_credit_field_ = field;
    calculator_minimum_ = bounds.minimum;
    calculator_backdrop_ = render();
    calculator_open_ = true;
    calculator_max_ = bounds.maximum;
    calculator_initial_ = std::min(bounds.initial, bounds.maximum);
    calculator_value_ = calculator_initial_;
    if (field == 3) {
        calculator_presets_ = {6, 12, 18, 24, 30, 36, bounds.minimum, bounds.maximum};
    } else if (field == 2) {
        // Source BalanceFromGame's -1 mode centres six presets around the
        // annuity (floor to the selected step, then three increments either
        // side). Source retains one running value: it clamps that value to
        // the minimum before each cell, stores it, then increments the
        // clamped value. For small repayments the step is one; otherwise it
        // is 1000.
        const uint32_t step = calculator_minimum_ < 100000 ? 1u : 1000u;
        const uint32_t center = (calculator_initial_ / step) * step;
        const uint32_t increment = ((calculator_initial_ / step) / 10) * step;
        int64_t running = int64_t(center) - int64_t(3) * increment;
        for (size_t i = 0; i < 6; ++i) {
            running = std::max<int64_t>(running, calculator_minimum_);
            calculator_presets_[i] = running > calculator_max_ ? UINT32_MAX : uint32_t(running);
            running += increment;
        }
        calculator_presets_[6] = calculator_minimum_;
        calculator_presets_[7] = calculator_max_;
    } else if (field == 0 || field == 6) {
        const uint32_t span = calculator_max_ >= calculator_minimum_ ? calculator_max_ - calculator_minimum_ : 0;
        const uint32_t step7 = span / 7;
        uint32_t factor = 1;
        while (factor <= step7 / 10) factor *= 10;
        const uint32_t step = (step7 / factor) * factor;
        const uint32_t midpoint = calculator_minimum_ + step7;
        const uint32_t first = ((midpoint + factor / 2) / factor) * factor;
        for (size_t i = 0; i < 6; ++i) {
            const uint64_t preset = uint64_t(first) + uint64_t(i) * step;
            calculator_presets_[i] = preset > UINT32_MAX ? UINT32_MAX : uint32_t(preset);
        }
        calculator_presets_[6] = calculator_minimum_;
        calculator_presets_[7] = calculator_max_;
    } else {
        calculator_presets_ = {10, 100, 1000, 10000, 100000, 1000000, calculator_minimum_, calculator_max_};
    }
    for (size_t i = 0; i < 6; ++i)
        if (calculator_presets_[i] < calculator_minimum_ || calculator_presets_[i] > calculator_max_)
            calculator_presets_[i] = UINT32_MAX;
}
void BankScreen::refresh_credit_rows() {
    const auto& v = credit_draft_.values;
    state_.credit_values[0] = wrap_i32(v[0] - v[6]);
    state_.credit_values[1] = wrap_i32(v[1]);
    state_.credit_values[2] = wrap_i32(v[2]);
    state_.credit_values[3] = wrap_i32(v[3]);
    state_.credit_values[4] = credit_draft_.initial_display
        ? wrap_i32(state_.credit.saved[3] - state_.credit.saved[4]) : wrap_i32(v[3]);
    state_.credit_values[5] = credit_draft_.initial_display
        ? wrap_i32(state_.credit.saved[5]) : wrap_i32(v[5]);
    state_.credit_values[6] = wrap_i32(v[6]);
    state_.credit_values[7] = wrap_i32(v[7]);
    for (size_t i = 0; i < state_.credit_values.size(); ++i) {
        std::string value = exe_.cstring(::bmh::layout::Bank_OriginalToUtf8_TextPointer2) + format_number(GameState{}, state_.credit_values[i], 8);
        value += exe_.cstring(::bmh::layout::Bank_OriginalToUtf8_ExecutableField2);
        if (i == 3 || i == 4) {
            value += texts_[text_id(::bmh::layout::Bank_OriginalToUtf8_TextPointer7)];
            if (state_.credit_values[i] != 1) value += exe_.cstring(::bmh::layout::Bank_OriginalToUtf8_TextPointer);
        } else value += texts_[text_id(::bmh::layout::Bank_OriginalToUtf8_TextPointer6)];
        state_.credit_value_text[i] = original_to_utf8(value);
    }
}
BankRequest BankScreen::finish_credit() {
    if (!credit_open_ || pending_) return {};
    if (credit_draft_.edits.empty()) {
        credit_open_ = false;
        restore_bank_credit();
        credit_fee_confirmation_open_ = false;
        feedback_.clear();
        return {};
    }
    if (state_.credit.counterparty == 0 &&
        wrap_sum(credit_draft_.fees, credit_draft_.interest_adjustment) != 0 && !credit_fees_accepted_) {
        credit_fee_confirmation_open_ = true;
        return {};
    }
    BankRequest request;
    request.kind = BankRequestKind::bank_credit;
    request.manager_slot = state_.manager_slot;
    request.runtime_club = state_.runtime_club;
    request.revision = state_.revision;
    request.expected_credit = state_.credit.saved;
    request.credit_edits = credit_draft_.edits;
    request.accept_credit_fees = credit_fees_accepted_;
    request.credit_counterparty = state_.credit_counterparty;
    pending_ = request;
    feedback_.clear();
    return request;
}
void BankScreen::draw_calculator(Image& out) const {
    // BalanceFromGame uses the 113x136 source crop from KB6 at the caller's
    // a449d/a449f origin, then draws the source text and 23 calculator cells.
    copy_region(out, calculator_art_, 0, 0, 103, 55, 113, 136);
    rect(out, 105, 191, 113, 2, 0x40);
    rect(out, 216, 57, 2, 136, 0x40);
    std::string title = texts_.strings.size() > 838 ? texts_[838] : std::string{};
    title += state_.account_text.empty() ? money(state_.balance) : state_.account_text;
    const int title_width = font_.text_width(title);
    body_baseline_raw(out, 103 + 8 + (105 - 8) / 2 - title_width / 2,
                      55 + 14, title, 0x62, true);
    for (int i = 0; i < 15; ++i) {
        const int x = 103 + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + size_t(i) * 2);
        const int y = 55 + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + size_t(i));
        copy_region(out, calculator_art_, 0, 136, x, y, 15, 14);
        body_baseline_raw(out, x + 5, y + 9, std::string(1, "0123456789DRCJN"[i]), 0x62, false);
    }
    for (int i = 0; i < 8; ++i) {
        if (calculator_presets_[size_t(i)] == UINT32_MAX) continue;
        const int x = 103 + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + size_t(i + 15) * 2);
        const int y = 55 + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + size_t(i + 15));
        const std::string value = money(calculator_presets_[size_t(i)]);
        const int width = font_.text_width(value);
        body_baseline_raw(out, x + 47 - width, y + 12, value, 0x62, true);
    }
    const std::string value = money(calculator_value_);
    const int value_width = font_.text_width(value);
    body_baseline_raw(out, 103 + 106 - value_width, 55 + 110, value, 0x00, true);
}
void BankScreen::draw_manager_dialog(Image& out, std::string_view message, bool with_ack_button) const {
    TaxDialogProjection notice;
    notice.manager_name=state_.manager_name;
    notice.portrait=state_.manager_portrait;
    notice.badge_sprite=state_.badge_sprite;
    notice.badge_fallback=source_badge_fallback(state_.runtime_club);
    notice.body=utf8_to_original(message);
    notice.source_manager_flags=with_ack_button?0x80:0x40;
    if(credit_fee_confirmation_open_&&!with_ack_button)notice.kind=TaxDialogKind::question;
    draw_source_dialog(out,{widgets_,faces_,badge_atlas_?&*badge_atlas_:nullptr,
        font_,large_font_,exe_,sprite_font_,texts_},notice,state_.gradient_text);
}
void BankScreen::draw_investment_warning(Image& out) const {
    draw_manager_dialog(out, state_.investment_limit_text, false);
}
void BankScreen::draw_property_notice(Image& out) const {
    if (state_.property_notice) draw_manager_dialog(out, state_.property_notice->text, true);
}
void BankScreen::draw_credit_notice(Image& out) const {
    if (state_.credit_notice) draw_manager_dialog(out, state_.credit_notice->text, true);
}
void BankScreen::draw_credit_fee_confirmation(Image& out) const {
    const auto textd = [&](uint32_t address) {
        if (address < ::bmh::layout::TextPointerTable || (address - ::bmh::layout::TextPointerTable) % 4) return std::string{};
        const size_t index = (address - ::bmh::layout::TextPointerTable) / 4;
        return index < texts_.strings.size() ? original_to_utf8(texts_[index]) : std::string{};
    };
    const int32_t total = wrap_sum(credit_draft_.fees, credit_draft_.interest_adjustment);
    GameState formatting;
    formatting.memory.add_block(::bmh::layout::CurrencyMultiplier, 8);
    formatting.memory.set32(::bmh::layout::CurrencyMultiplier, uint32_t(state_.credit_currency[0]));
    formatting.memory.set32(::bmh::layout::CurrencyDivisor, uint32_t(state_.credit_currency[1]));
    const std::string amount = format_number(formatting, total, 0x101);
    draw_manager_dialog(out, textd(::bmh::layout::Bank_DrawCreditFeeConfirmation_ExecutableField) + amount + textd(::bmh::layout::Bank_DrawCreditFeeConfirmation_ExecutableField2), false);

}
void BankScreen::draw_credit_lender_selector(Image& out) const {
    if (state_.credit_lenders.size() > 3 ||
        (!state_.credit_lenders.empty() &&
         (credit_lender_frame_.width < 249 || credit_lender_frame_.height < 160)))
        throw FormatError("bank: truncated credit lender assets");
    for (size_t ordinal = 0; ordinal < state_.credit_lenders.size(); ++ordinal) {
        const size_t table = ordinal;
        const int sx1 = exe_.u8(::bmh::layout::Bank_FormatError_RuntimeField3 + table), sy1 = exe_.u8(::bmh::layout::Bank_FormatError_RuntimeField4 + table);
        const int dx1 = exe_.u8(::bmh::layout::Bank_FormatError_RuntimeField7 + table), dy1 = exe_.u8(::bmh::layout::Bank_FormatError_RuntimeField8 + table);
        const int w1 = exe_.u8(::bmh::layout::Bank_FormatError_RuntimeField11 + table), h1 = exe_.u8(::bmh::layout::Bank_FormatError_RuntimeField12 + table);
        // FormatError2 routes the 0xc0 argument through the opaque VGA blit;
        // the final 0xff is the destination plane mask, not a transparent key.
        copy_region(out, credit_lender_frame_, sx1, sy1, dx1, dy1, w1, h1);
    }
}
void BankScreen::draw_property_card(Image& out, int property, int y, bool owned) const {
    if (property < 0 || property >= int(state_.funds.size())) return;
    const auto& item = state_.funds[size_t(property)];
    const int count = owned ? int(item.owned) : 1;
    if (owned && count <= 0) return;
    const int source_y = (property / 2) * 0x29;
    const int source_x = (property % 2) * 0x43;
    if (property_art_.width >= source_x + 0x43 && property_art_.height >= source_y + 0x29)
        copy_region(out, property_art_, source_x, source_y, 0xae, y - 8, 0x43, 0x29);
    const std::string suffix = count > 1 ? item.plural_suffix : item.singular_suffix;
    const std::string quantity = count == 1 ? (item.one_quantity_text.empty() ? "1." : item.one_quantity_text) :
        (item.quantity_text.empty() ? std::to_string(count) + "." : item.quantity_text);
    body_baseline(out, 0x37, y + 6, quantity + item.name + suffix, 0x6a);
    const auto address_text = [&](uint32_t address) {
        if (address < ::bmh::layout::TextPointerTable || (address - ::bmh::layout::TextPointerTable) % 4) return std::string{};
        const size_t index = (address - ::bmh::layout::TextPointerTable) / 4;
        return index < texts_.strings.size() ? original_to_utf8(texts_[index]) : std::string{};
    };
    const std::string price = owned ? item.average_paid_text : item.unit_price_text;
    body_baseline(out, 0x37, y + 15, address_text(::bmh::layout::Bank_DrawPropertyCard_TextPointer2) + price, 0x6a);
    body_baseline(out, 0x37, y + 24, address_text(::bmh::layout::Bank_DrawPropertyCard_TextPointer3), 0x6a);
    body_baseline_raw(out, 0x5f, y + 24, exe_.cstring(::bmh::layout::Bank_DrawPropertyCard_TextPointer) + utf8_to_original(item.unit_price_text), 0x6a, true);
}
uint32_t BankScreen::accept_calculator() {
    const uint32_t value = (credit_open_ && calculator_credit_field_ >= 0)
        ? std::max(calculator_minimum_, calculator_value_) : calculator_value_;
    calculator_open_ = false;
    calculator_backdrop_.reset();
    if (credit_open_ && calculator_credit_field_ >= 0) {
        if (!edit_bank_credit(state_.credit, credit_draft_, calculator_credit_field_, value)) {
            calculator_credit_field_ = -1;
            return value;
        }
        credit_draft_.initial_display = false;
        credit_selected_row_ = calculator_credit_field_ == 0 ? 0 : calculator_credit_field_ == 1 ? 1 :
            calculator_credit_field_ == 2 ? 2 : calculator_credit_field_ == 3 ? 3 : 6;
        refresh_credit_rows();
        calculator_credit_field_ = -1;
        return value;
    }
    amount_text_ = value ? std::to_string(value) : std::string{};
    return value;
}
Image BankScreen::render() const {
    Image out = calculator_open_ && calculator_backdrop_ ? *calculator_backdrop_ : background_;
    if (!(calculator_open_ && calculator_backdrop_)) {
        // PlainWindowFrameUsedBy (bank room): title in KB/54 mode 5 and active manager
        // name in the lower title strip. KB/23 contains only the backdrop.
        const std::string title = texts_.strings.size() > 1272 ? original_to_utf8(texts_[1272]) : std::string{};
        sprite_font_.draw(out, SpriteFont::centered_title_x(sprite_font_.text_width(title)), 13, title, 5);
        sprite_font_.draw(out, 5, 235, original_to_utf8(state_.manager_name), 5);
        // MaxSharesToBuy's first KB/64 pass is part of the bank-room base. The
        // modal overlays are drawn afterward, leaving the crop's visible
        // outer edges around each subview. A calculator instead restores its
        // captured backdrop, which already contains this pass.
        draw_credit_lender_selector(out);
    }
    if (calculator_open_) { draw_calculator(out); return out; }
    if (finance_open_) {
        if (finance_frame_.width < 212 || finance_frame_.height < 142) throw FormatError("bank: truncated finance view asset");
        copy_region(out, finance_frame_, 0, 0, 49, 45, 212, 142);
        const auto db = [&](uint32_t address) -> std::string {
            if (address < ::bmh::layout::TextPointerTable || (address - ::bmh::layout::TextPointerTable) % 4) return {};
            const size_t index = (address - ::bmh::layout::TextPointerTable) / 4;
            if (index >= texts_.strings.size()) return {};
            return original_to_utf8(texts_[index]);
        };
        original_text(out, 111, 54, db(::bmh::layout::Bank_OriginalToUtf8_TextPointer5), 0x6a);
        // MaxSharesToBuy case 1: the thin rule follows the title at y=0x37.
        line(out, 51, 55, 258, 55, 0x6a);
        int held_row = 0;
        for (size_t i = 0; i < state_.funds.size(); ++i) if (state_.funds[i].owned) {
            body_baseline(out, 53, 85 + held_row * 6, state_.finance_fund_labels[i], 0x6a);
            body_baseline(out, 169, 85 + held_row * 6, state_.finance_fund_prices[i], 0x6a);
            ++held_row;
        }
        held_row = 0;
        for (size_t i = 0; i < state_.stocks.size(); ++i) if (state_.stocks[i].owned) {
            const int x = held_row < 6 ? 53 : 156;
            const int y = 131 + (held_row % 6) * 6;
            body_baseline(out, x, y, state_.finance_stock_labels[i], 0x6a);
            body_baseline(out, x + 70, y, state_.finance_stock_prices[i], 0x6a);
            ++held_row;
        }
        for (int i = 0; i < 7; ++i) {
            const int x = exe_.u8(::bmh::layout::Bank_OriginalToUtf8_RuntimeField2 + size_t(i));
            const int y = exe_.u8(::bmh::layout::Bank_OriginalToUtf8_RuntimeField3 + size_t(i));
            const uint8_t shade = exe_.u8(::bmh::layout::Bank_OriginalToUtf8_RuntimeField4 + size_t(i));
            body_baseline(out, x, y, db(::bmh::layout::Bank_OriginalToUtf8_RuntimeField6 + uint32_t(i) * 4), 0x6a);
            body_baseline_raw(out, x + 42, y, exe_.cstring(::bmh::layout::Bank_OriginalToUtf8_ExecutableField), 0x6a, true);
            const int value_x = x + ((i == 4 || i == 6) ? 44 : 49);
            const std::string& value = state_.finance_value_text[size_t(i)];
            body_baseline_raw(out, value_x, y, utf8_to_original(value), shade, i == 6);

        }
        return out;
    }
    if (credit_open_) {
        if (finance_frame_.width < 212 || finance_frame_.height < 142)
            throw FormatError("bank: truncated credit view asset");
        copy_region(out, finance_frame_, 0, 0, 49, 45, 212, 142);
        std::string lender_name;
        if (state_.credit_counterparty != 0) {
            const auto lender = std::find_if(state_.credit_lenders.begin(), state_.credit_lenders.end(),
                [&](const auto& candidate) { return candidate.counterparty == state_.credit_counterparty; });
            if (lender != state_.credit_lenders.end()) lender_name = lender->manager_name;
        }
        const std::string title = texts_[text_id(::bmh::layout::Bank_FormatError_TextPointer)] +
            (state_.credit_counterparty == 0 ? texts_[text_id(::bmh::layout::Bank_FormatError_TextPointer6)] : lender_name);
        const std::string title_utf8 = original_to_utf8(title);
        original_text(out, 154 - large_font_.text_width(utf8_to_original(title_utf8)) / 2, 54, title_utf8, 0x6a);
        line(out, 51, 55, 258, 55, 0x6a);
        for (int row = 0; row < 8; ++row) {
            const int baseline = 77 + row * 9;
            body_baseline(out, 53, baseline, state_.credit_labels[size_t(row)], 0x6a);
            if (row == 7 || state_.credit_values[1] != 0)
                body_baseline(out, 142, baseline, state_.credit_value_text[size_t(row)], 0x6a);
        }
        const auto draw_button = [&](int x, uint32_t text_address) {
            constexpr int y = 174, button_width = 64;
            rect(out, x + 2, y + 10, button_width, 2, 0x40); rect(out, x + button_width, y + 2, 2, 10, 0x40);
            copy_region(out, widgets_, 24, 74, x, y, 31, 10);
            int at = 31, remaining = button_width - 31;
            while (remaining > 20) { const int n = std::min(8, remaining - 20); copy_region(out, widgets_, 24, 84, x + at, y, n, 10); at += n; remaining -= n; }
            if (remaining > 0) copy_region(out, widgets_, 45 - remaining, 94, x + at, y, remaining, 10);
            const size_t id = text_id(text_address);
            if (id < texts_.strings.size()) {
                const std::string label = original_to_utf8(texts_[id]);
                sprite_font_.draw(out, x + (button_width - sprite_font_.text_width(label)) / 2, y + 7,
                                  label, exe_.u8(::bmh::layout::Advertising_Button_RuntimeField));
            }
        };
        draw_button(60, ::bmh::layout::Bank_FormatError_RuntimeField18 + 4 * 4);
        draw_button(185, ::bmh::layout::Bank_FormatError_RuntimeField18 + 5 * 4);
        if (credit_notice_open_) draw_credit_notice(out);
        else if (credit_fee_confirmation_open_) draw_credit_fee_confirmation(out);
        return out;
    }
    if (!exchange_open_ && !deposit_open_ && !property_open_ && !credit_open_) {
        // Original room (MaxSharesToBuy case 0): access is by the baked Source
        // hotspots below; the host-only ASCII shortcut legend is omitted.
        return out;
    }
    if (property_open_) {
        if (finance_frame_.width < 212 || finance_frame_.height < 142 ||
            property_art_.width < 134 || property_art_.height < 123)
            throw FormatError("bank: truncated original property assets");
        copy_region(out, finance_frame_, 0, 0, 49, 45, 212, 142);
        const auto db = [&](uint32_t address) -> std::string {
            if (address < ::bmh::layout::TextPointerTable || (address - ::bmh::layout::TextPointerTable) % 4) return {};
            const size_t index = (address - ::bmh::layout::TextPointerTable) / 4;
            return index < texts_.strings.size() ? original_to_utf8(texts_[index]) : std::string{};
        };
        const std::string title = db(::bmh::layout::Bank_FormatError_TextPointer4);
        const int title_width = large_font_.text_width(utf8_to_original(title));
        original_text(out, 49 + (261 - 49) / 2 - title_width / 2, 54, title, 0x6a);
        line(out, 51, 55, 258, 55, 0x6a);
        body_baseline(out, 52, 63, db(::bmh::layout::Bank_FormatError_TextPointer5), 0x5f);
        body_baseline(out, 52, 115, db(::bmh::layout::Bank_FormatError_ExecutableField3), 0x5f);
        for (const int y : {61, 113}) {
            rect(out, 172, y + 1, 71, 45, 0x6a);
            rect(out, 173, y + 2, 69, 43, 0x62);
        }
        if (selected_owned_fund_ >= 0 && selected_owned_fund_ < int(state_.funds.size()) &&
            state_.funds[size_t(selected_owned_fund_)].owned)
            draw_property_card(out, selected_owned_fund_, 72, true);
        draw_property_card(out, selected_fund_, 124, false);
        const auto source_button = [&](int x, std::string_view label) {
            constexpr int width = 64, y = 174;
            rect(out, x + 2, y + 10, width, 2, 0x40);
            rect(out, x + width, y + 2, 2, 10, 0x40);
            copy_region(out, widgets_, 24, 74, x, y, 31, 10);
            int at = 31, remaining = width - 31;
            while (remaining > 20) {
                const int n = std::min(8, remaining - 20);
                copy_region(out, widgets_, 24, 84, x + at, y, n, 10);
                at += n; remaining -= n;
            }
            if (remaining > 0) copy_region(out, widgets_, 45 - remaining, 94, x + at, y, remaining, 10);
            sprite_font_.draw(out, x + (width - sprite_font_.text_width(label)) / 2, y + 7,
                              label, exe_.u8(::bmh::layout::Advertising_Button_RuntimeField));
        };
        source_button(60, original_to_utf8(texts_[191]));
        source_button(185, original_to_utf8(texts_[192]));
        if (property_notice_open_) draw_property_notice(out);
        return out;
    }
    if (exchange_open_) {
        if (finance_frame_.width < 212 || finance_frame_.height < 142)
            throw FormatError("bank: truncated stock-market frame");
        // MaxSharesToBuy case 0 copies KB5 as the opaque case background.
        copy_region(out, finance_frame_, 0, 0, 49, 45, 212, 142);
        const auto db = [&](uint32_t address) -> std::string_view {
            if (address < ::bmh::layout::TextPointerTable || (address - ::bmh::layout::TextPointerTable) % 4) return {};
            const size_t index = (address - ::bmh::layout::TextPointerTable) / 4;
            return index < texts_.strings.size() ? std::string_view(texts_[index]) : std::string_view{};
        };
        body_baseline(out, 59, 52, db(::bmh::layout::Bank_FormatError_TextPointer2), 0x5f);
        body_baseline(out, 51, 168, db(::bmh::layout::Bank_FormatError_TextPointer3), 0x5f);
        for (size_t i = 0; i < state_.stocks.size(); ++i) {
            const auto& stock = state_.stocks[i];
            const int y = 68 + int(i) * 8;
            const std::string_view name = db(::bmh::layout::Bank_FormatError_ExecutableField2 + uint32_t(i) * 4);
            body_baseline(out, 51, y, name, exe_.u8(::bmh::layout::Bank_FormatError_RuntimeField17 + (int(i) == selected_stock_)));
            const int x0 = 103;
            for (int column = 0; column < 4; ++column) {
                const int x = x0 + column * 22;
                rect(out, x - 1, y - 7, 21, 9, 0x67);
                line(out, x, y, x + 18, y, 0x65);
                rect(out, x, y - 5, 19, 5, 0x62);
                body_baseline(out, x, y, stock.market_text[size_t(column)], 0x27);
            }
            for (int marker = 0; marker < 2; ++marker) {
                const int x = 190 + marker * 4;
                rect(out, x, y - 5, 3, 3, 100);
                if (!stock.owned) continue;
                const uint32_t average = uint32_t(stock.paid) / stock.owned;
                if (!(marker == 0 ? average < stock.price : stock.price <= average)) continue;
                rect(out, x, y - 5, 3, 3, exe_.u8(::bmh::layout::Bank_FormatError_RuntimeField + size_t(marker) * 2));
                rect(out, x, y - 5, 2, 2, exe_.u8(::bmh::layout::Bank_FormatError_RuntimeField2 + size_t(marker) * 2));
                out.at(x, y - 5) = 0x27; // PlotsCallerCurrentPen plots the caller's current pen.
            }

        }
        const auto& stock = state_.stocks.at(size_t(selected_stock_));
        const int16_t quote_offset = int16_t(uint16_t(stock.price - uint16_t(0x118)));
        auto graph_point = [&](int day) {
            return 170 - int(stock.history[size_t(day)]) / 2 + int(quote_offset) / 2;
        };
        rect(out, 198, 48, 61, 124, 100);
        rect(out, 199, 49, 59, 122, 0x6a);
        // Abs starts its major-axis accumulator at zero (not half
        // a pixel). Abs4 clips endpoints before drawing its three strokes.
        const auto source_line = [&](int x0, int y0, int x1, int y1, uint8_t color) {
            const int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
            const int sx = x1 < x0 ? -1 : 1, sy = y1 < y0 ? -1 : 1;
            int error = 0;
            const auto plot = [&] { if (x0 >= 0 && x0 < out.width && y0 >= 0 && y0 < out.height) out.at(x0,y0) = color; };
            plot();
            for (int step = 0; step < std::max(dx,dy); ++step) {
                if (dy < dx) { x0 += sx; error += dy; if (error >= dx) { error -= dx; y0 += sy; } }
                else { y0 += sy; error += dx; if (error >= dy) { error -= dy; x0 += sx; } }
                plot();
            }
        };
        for (int day = 1; day < int(stock.history.size()); ++day) {
            int x = day * 2 + 199, previous_x = x - 2;
            int previous_y = graph_point(day - 1) + 49, y = graph_point(day) + 49;
            if (!((previous_y < 171 || y < 171) && (previous_y > 48 || y > 48))) continue;
            if (y > 170) { x += (previous_x - x) * (170 - y) / (previous_y - y); y = 170; }
            if (y < 49) { x += (previous_x - x) * (49 - y) / (previous_y - y); y = 49; }
            if (previous_y > 170) { previous_x += (x - previous_x) * (170 - previous_y) / (y - previous_y); previous_y = 170; }
            if (previous_y < 49) { previous_x += (x - previous_x) * (49 - previous_y) / (y - previous_y); previous_y = 49; }
            if (!(previous_x >= 199 && previous_x <= 258 && previous_y >= 49 && previous_y <= 170 &&
                  x >= 199 && x <= 258 && y >= 49 && y <= 170)) continue;
            source_line(previous_x, previous_y, x, y, exe_.u8(::bmh::layout::Bank_FormatError_RuntimeField15));
            source_line(previous_x, previous_y + 1, x, y + 1, exe_.u8(::bmh::layout::Bank_FormatError_RuntimeField15));
            source_line(previous_x, previous_y + 2, x, y + 2, exe_.u8(::bmh::layout::Bank_FormatError_RuntimeField16));
        }
        if (visible_tip_) body_baseline(out, 200, 167, db(::bmh::layout::Bank_FormatError_RuntimeField19 + uint32_t(*visible_tip_) * 4), 0xf7);
        auto source_button = [&](int x, std::string_view label) {
            constexpr int width = 64, y = 174;
            rect(out, x + 2, y + 10, width, 2, 0x40); rect(out, x + width, y + 2, 2, 10, 0x40);
            copy_region(out, widgets_, 24, 74, x, y, 31, 10);
            int at = 31, remain = width - 31;
            while (remain > 20) { const int n = std::min(8, remain - 20); copy_region(out, widgets_, 24, 84, x + at, y, n, 10); at += n; remain -= n; }
            if (remain > 0) copy_region(out, widgets_, 45 - remain, 94, x + at, y, remain, 10);
            sprite_font_.draw(out, x + (width - sprite_font_.text_width(label)) / 2, y + 7,
                              label, exe_.u8(::bmh::layout::Advertising_Button_RuntimeField));
        };
        source_button(60, original_to_utf8(texts_[191]));
        source_button(185, original_to_utf8(texts_[192]));
        if (investment_warning_open_) draw_investment_warning(out);
        return out;
    }
    // Case 3 first restores KB5 like every bank subview, then places KB65.
    copy_region(out, finance_frame_, 0, 0, 49, 45, 212, 142);
    // MaxSharesToBuy case 3: original scene KB/23 plus exact Sparbuch overlay KB/65.
    if (deposit_frame_.width < 200 || deposit_frame_.height < 123) throw FormatError("bank: truncated original deposit frame");
    for (int y = 0; y < 123; ++y) for (int x = 0; x < 200; ++x)
        out.at(55 + x, 48 + y) = deposit_frame_.pixels[size_t(y) * deposit_frame_.width + x];
    // The Sparbuch page always reports the fixed deposit. The withdrawal
    // calculator is an input overlay and does not change the displayed row.
    body_baseline(out, 195, 82, state_.deposit_text.empty() ? money(state_.deposit) : state_.deposit_text, 0x00);
    auto source_button = [&](int x, std::string_view label) {
        const int width = 64, y = 174;
        rect(out, x + 2, y + 10, width, 2, 0x40);
        rect(out, x + width, y + 2, 2, 10, 0x40);
        copy_region(out, widgets_, 24, 74, x, y, 31, 10);
        int at = 31, remaining = width - 31;
        while (remaining > 20) {
            const int n = std::min(8, remaining - 20);
            copy_region(out, widgets_, 24, 84, x + at, y, n, 10);
            at += n; remaining -= n;
        }
        if (remaining > 0) copy_region(out, widgets_, 45 - remaining, 94, x + at, y, remaining, 10);
        sprite_font_.draw(out, x + (width - sprite_font_.text_width(label)) / 2, y + 7, label,
                          exe_.u8(::bmh::layout::Advertising_Button_RuntimeField));
    };
    source_button(60, original_to_utf8(texts_[193]));
    source_button(185, original_to_utf8(texts_[194]));
    return out;
}
BankRequest BankScreen::submit() {
    if (pending_) return {};
    if (property_open_ && calculator_property_count_ && calculator_value_ == 0) return {};
    uint32_t value = 0;
    const auto [end, ec] = std::from_chars(amount_text_.data(), amount_text_.data() + amount_text_.size(), value);
    const uint32_t maximum = (exchange_open_ || property_open_) ? 9999u : 99999999u;
    if (exchange_open_ && calculator_stock_count_ && ec == std::errc{} &&
        end == amount_text_.data() + amount_text_.size() && value == 0) {
        feedback_.clear();
        return {};
    }
    if (property_open_ && calculator_property_count_ && ec == std::errc{} &&
        end == amount_text_.data() + amount_text_.size() && value == 0) {
        feedback_.clear();
        return {};
    }
    if (amount_text_.empty() || ec != std::errc{} || end != amount_text_.data() + amount_text_.size() || value == 0 || value > maximum) {
        feedback_ = "UNGUELTIGER BETRAG"; return {};
    }
    if (exchange_open_) {
        const auto& stock = state_.stocks.at(size_t(selected_stock_));
        const uint32_t stock_max = uint32_t(std::max(0, selling_shares_ ? int(stock.owned) : stock.max_to_buy));
        if (value > stock_max) {
            feedback_ = "STUECKZAHL NICHT VERFUEGBAR"; return {};
        }
        BankRequest request{selling_shares_ ? BankRequestKind::sell_shares : BankRequestKind::buy_shares,
            state_.manager_slot, state_.runtime_club, state_.revision, 0, selected_stock_, int(value)};
        pending_ = request; feedback_.clear(); return request;
    }
    if (property_open_ && calculator_property_count_) {
        const int property = selling_shares_ ? selected_owned_fund_ : selected_fund_;
        if (property < 0 || property >= int(state_.funds.size())) return {};
        const auto& selected = state_.funds[size_t(property)];
        if (selling_shares_ ? value > selected.owned : value > uint32_t(std::max(0, selected.max_to_buy))) {
            feedback_ = "STUECKZAHL NICHT VERFUEGBAR"; return {};
        }
        BankRequest request{selling_shares_ ? BankRequestKind::sell_fund : BankRequestKind::buy_fund,
            state_.manager_slot, state_.runtime_club, state_.revision, 0, property, int(value)};
        pending_ = request; feedback_.clear(); return request;
    }
    const int64_t available = withdrawing_ ? int64_t(state_.deposit) : int64_t(state_.balance);
    if (int64_t(value) > std::max<int64_t>(0, available)) { feedback_ = "BETRAG NICHT VERFUEGBAR"; return {}; }
    BankRequest request{BankRequestKind::move_to_deposit, state_.manager_slot, state_.runtime_club,
                        state_.revision, withdrawing_ ? -int32_t(value) : int32_t(value)};
    pending_ = request; feedback_.clear(); return request;
}
BankRequest BankScreen::click(int x, int y, bool right) {
    if (credit_notice_open_) {
        if (right || (x >= 95 && x <= 220 && y >= 166 && y <= 176)) {
            credit_notice_open_ = false;
            state_.credit_notice.reset();
            credit_open_ = false; credit_draft_ = {}; credit_fees_accepted_ = false;
            restore_bank_credit();
        }
        return {};
    }
    if (credit_fee_confirmation_open_) {
        if (right || (x >= 175 && x <= 250 && y >= 166 && y <= 176)) {
            credit_fee_confirmation_open_ = false;
            credit_open_ = false;
            credit_draft_ = {};
            credit_fees_accepted_ = false;
            restore_bank_credit();
            return {};
        }
        if (x >= 62 && x <= 137 && y >= 166 && y <= 176) {
            credit_fee_confirmation_open_ = false;
            credit_fees_accepted_ = true;
            return finish_credit();
        }
        return {};
    }
    if (property_notice_open_) {
        if (right || (x >= 95 && x <= 220 && y >= 166 && y <= 176)) {
            property_notice_open_ = false;
            state_.property_notice.reset();
        }
        return {};
    }
    if (investment_warning_open_) { investment_warning_open_ = false; return {}; }
    if (pending_) return {};
    if (calculator_open_) {
        if (right) { calculator_open_ = false; calculator_backdrop_.reset(); calculator_credit_field_ = -1; amount_text_.clear(); return {}; }
        for (int i = 0; i < 15; ++i) {
            const int key_x = 103 + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + size_t(i) * 2);
            const int key_y = 55 + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + size_t(i));
            if (x < key_x || x >= key_x + 16 || y < key_y || y >= key_y + 14) continue;
            if (i < 10) { text(std::to_string(i)); return {}; }
            if (i == 10) { calculator_value_ /= 10; return {}; }
            if (i == 11) { calculator_value_ = calculator_initial_; return {}; }
            if (i == 12) { calculator_value_ = 0; return {}; }
            if (i == 13) { accept_calculator(); return credit_open_ ? BankRequest{} : submit(); }
            if (i == 14) { calculator_open_ = false; calculator_backdrop_.reset(); calculator_credit_field_ = -1; amount_text_.clear(); return {}; }
        }
        for (int i = 0; i < 8; ++i) {
            const int key_x = 103 + exe_.u16(::bmh::layout::Advertising_Click_RuntimeField + size_t(i + 15) * 2);
            const int key_y = 55 + exe_.u8(::bmh::layout::Advertising_Click_RuntimeField2 + size_t(i + 15));
            if (calculator_presets_[size_t(i)] != UINT32_MAX && x >= key_x && x < key_x + 53 &&
                y >= key_y && y < key_y + 14) { calculator_value_ = calculator_presets_[size_t(i)]; return {}; }
        }
        return {};
    }
    if (right) {
        if (deposit_open_ || finance_open_ || exchange_open_ || property_open_ || credit_open_) {
            const bool leaving_credit = credit_open_;
            deposit_open_ = finance_open_ = exchange_open_ = property_open_ = credit_open_ = false;
            if (leaving_credit) { credit_draft_ = {}; credit_fees_accepted_ = false; credit_fee_confirmation_open_ = false; restore_bank_credit(); }
            amount_text_.clear(); feedback_.clear(); return {};
        }
        close_requested_ = true; return {};
    }
    if (x < 8 && y > 220) { close_requested_ = true; return {}; }
    if (exchange_open_) {
        if (y > 61 && y < 157) {
            const int selected = (y - 61) >> 3;
            if (selected != selected_stock_) visible_tip_.reset();
            selected_stock_ = selected; amount_text_.clear(); return {};
        }
        if (x > 50 && x < 200 && y > 162 && y < 167) {
            BankRequest request{BankRequestKind::stock_tip, state_.manager_slot, state_.runtime_club,
                                state_.revision, 0, selected_stock_, 0};
            pending_ = request; return request;
        }
        if (y >= 174 && y < 183) {
            if (x >= 60 && x < 124) { selling_shares_ = false; amount_text_.clear(); open_stock_calculator(false); return {}; }
            if (x >= 185 && x < 249) { selling_shares_ = true; amount_text_.clear(); open_stock_calculator(true); return {}; }
        }
        return {};
    }
    if (property_open_) {
        if (y >= 174 && y <= 184) {
            if (x >= 60 && x <= 124) { selling_shares_ = false; amount_text_.clear(); open_property_calculator(false); return {}; }
            if (x >= 185 && x <= 249) { selling_shares_ = true; amount_text_.clear(); open_property_calculator(true); return {}; }
        }
        if (x > 171 && x < 243 && y >= 61 && y <= 106) {
            std::array<int, 6> owned{}; int count = 0;
            for (int i = 0; i < int(state_.funds.size()); ++i)
                if (state_.funds[size_t(i)].owned) owned[size_t(count++)] = i;
            if (count) {
                int position = 0;
                for (; position < count && owned[size_t(position)] != selected_owned_fund_; ++position) {}
                selected_owned_fund_ = owned[size_t((position + 1) % count)];
            }
            return {};
        }
        if (x > 171 && x < 243 && y >= 113 && y <= 158) {
            selected_fund_ = (selected_fund_ + 1) % int(state_.funds.size());
            return {};
        }
        return {};
    }
    if (credit_open_) {
        const std::vector<int> editable_rows = state_.credit_counterparty == 0
            ? std::vector<int>{0, 1, 3, 6} : std::vector<int>{0, 1, 2, 3, 6};
        for (const int row : editable_rows) {
            if (y >= 70 + row * 9 && y <= 79 + row * 9) {
                credit_selected_row_ = row;
                const int field = row == 0 ? 0 : row == 1 ? 1 : row == 2 ? 2 : row == 3 ? 3 : 6;
                open_credit_calculator(field);
                return {};
            }
        }
        // MaxSharesToBuy's bottom action confirms the current draft.
        if (y >= 174 && y <= 184 && x >= 60 && x <= 124) return finish_credit();
        if (y >= 174 && y <= 184 && x >= 185 && x <= 249) {
            credit_open_ = false; credit_draft_ = {}; credit_fees_accepted_ = false; restore_bank_credit(); return {};
        }
        return {};
    }
    if (!deposit_open_ && !finance_open_ && !exchange_open_ && !property_open_) {
        // MaxSharesToBuy's six native bank hitbox slots (Bank_FinishCredit_RuntimeField/Bank_FinishCredit_RuntimeField2/Bank_FinishCredit_RuntimeField3/Bank_FinishCredit_RuntimeField4).
        // Stock, financial overview, Immobilien and KASSE are mapped here.
        for (int i = 0; i < 5; ++i) {
            const int left = exe_.u16(::bmh::layout::Bank_FinishCredit_RuntimeField + size_t(i) * 2);
            const int right_edge = exe_.u16(::bmh::layout::Bank_FinishCredit_RuntimeField2 + size_t(i) * 2);
            const int top = exe_.u8(::bmh::layout::Bank_FinishCredit_RuntimeField3 + size_t(i));
            const int bottom = exe_.u8(::bmh::layout::Bank_FinishCredit_RuntimeField4 + size_t(i));
            if (x < left || x > right_edge || y < top || y > bottom) continue;
            if (i == 0) { exchange_open_ = true; selected_stock_ = 11; visible_tip_.reset(); }
            else if (i == 1) finance_open_ = true;
            else if (i == 2) {
                property_open_ = true;
                selected_fund_ = 0;
                selected_owned_fund_ = -1;
                for (int property = 0; property < int(state_.funds.size()); ++property)
                    if (state_.funds[size_t(property)].owned) { selected_owned_fund_ = property; break; }
            }
            else if (i == 3) deposit_open_ = true;
            else if (i == 4) { restore_bank_credit(); credit_open_ = true; credit_fees_accepted_ = false; credit_selected_row_ = -1; credit_draft_ = bank_credit_draft(state_.credit); refresh_credit_rows(); }
            return {};
        }
        // Source tests the five fixed bank hotspots first, then the lender
        // hitboxes mapped into slots 5..7, with inclusive edges.
        for (size_t i = 0; i < state_.credit_lenders.size(); ++i) {
            const size_t table = i + 5;
            const int left = exe_.u8(::bmh::layout::Bank_FinishCredit_RuntimeField5 + table), top = exe_.u8(::bmh::layout::Bank_FinishCredit_RuntimeField6 + table);
            if (x < left || x > left + 0x11 || y < top || y > top + 0x11) continue;
            const auto& lender = state_.credit_lenders[i];
            state_.credit = lender.credit;
            state_.credit_counterparty = lender.counterparty;
            credit_open_ = true;
            credit_fees_accepted_ = false;
            credit_fee_confirmation_open_ = false;
            credit_selected_row_ = -1;
            credit_draft_ = bank_credit_draft(state_.credit);
            refresh_credit_rows();
            return {};
        }
        return {};
    }
    if (finance_open_) { finance_open_ = false; return {}; }
    if (deposit_open_ && y >= 174 && y < 186) {
        if (x >= 60 && x < 124) open_calculator(false);
        else if (x >= 185 && x < 250) open_calculator(true);
    }
    return {};
}
BankRequest BankScreen::key(std::string_view key) {
    if (credit_notice_open_) {
        if (key == "ENTER" || key == "J" || key == "ESC" || key == "ESCAPE") {
            credit_notice_open_ = false; state_.credit_notice.reset(); credit_open_ = false;
            credit_draft_ = {}; credit_fees_accepted_ = false; restore_bank_credit();
        }
        return {};
    }
    if (credit_fee_confirmation_open_) {
        if (key == "Y" || key == "J" || key == "ENTER") {
            credit_fee_confirmation_open_ = false; credit_fees_accepted_ = true; return finish_credit();
        }
        if (key == "N" || key == "ESC" || key == "ESCAPE") {
            credit_fee_confirmation_open_ = false; credit_open_ = false; credit_draft_ = {}; credit_fees_accepted_ = false; restore_bank_credit();
        }
        return {};
    }
    if (property_notice_open_) {
        if (key == "ESC" || key == "ESCAPE" || key == "ENTER" || key == "J") {
            property_notice_open_ = false;
            state_.property_notice.reset();
        }
        return {};
    }
    if (investment_warning_open_) { investment_warning_open_ = false; return {}; }
    if (pending_) return {};
    if (calculator_open_) {
        if (key == "ESC" || key == "ESCAPE" || key == "N") { calculator_open_ = false; calculator_backdrop_.reset(); calculator_credit_field_ = -1; amount_text_.clear(); return {}; }
        if (key == "ENTER" || key == "J") { accept_calculator(); return credit_open_ ? BankRequest{} : submit(); }
        if (key == "BACKSPACE" || key == "D") { calculator_value_ /= 10; return {}; }
        if (key == "CLEAR" || key == "SPACE" || key == "C") { calculator_value_ = 0; return {}; }
        if (key == "R") { calculator_value_ = calculator_initial_; return {}; }
        if (key.size() == 1 && key[0] >= '0' && key[0] <= '9') text(key);
        return {};
    }
    if (key == "ESC" || key == "ESCAPE") {
        if (deposit_open_ || finance_open_ || exchange_open_ || property_open_ || credit_open_) {
            const bool leaving_credit = credit_open_;
            deposit_open_ = finance_open_ = exchange_open_ = property_open_ = credit_open_ = false; amount_text_.clear(); feedback_.clear(); credit_draft_ = {}; credit_fees_accepted_ = false;
            if (leaving_credit) restore_bank_credit();
        } else close_requested_ = true;
        return {};
    }
    if (key == "BACKSPACE" && (deposit_open_ || exchange_open_ || property_open_)) {
        deposit_open_ = false; exchange_open_ = false; property_open_ = false; amount_text_.clear(); feedback_.clear(); return {};
    }
    if (!deposit_open_ && !exchange_open_ && !finance_open_ && !property_open_ && !credit_open_ && (key == "B" || key == "b")) { exchange_open_ = true; selected_stock_ = 11; visible_tip_.reset(); return {}; }
    if (!deposit_open_ && !exchange_open_ && !finance_open_ && !property_open_ && !credit_open_ && (key == "F" || key == "f")) { deposit_open_ = true; return {}; }
    if (!deposit_open_ && !exchange_open_ && !finance_open_ && !property_open_ && !credit_open_ && (key == "I" || key == "i")) {
        property_open_ = true; selected_fund_ = 0; selected_owned_fund_ = -1;
        for (int property = 0; property < int(state_.funds.size()); ++property)
            if (state_.funds[size_t(property)].owned) { selected_owned_fund_ = property; break; }
        return {};
    }
    if (credit_open_) {
        if (key == "UP" || key == "DOWN") {
            const std::vector<int> rows = state_.credit_counterparty == 0
                ? std::vector<int>{0, 1, 3, 6, 7} : std::vector<int>{0, 1, 2, 3, 6, 7};
            auto it = std::find(rows.begin(), rows.end(), credit_selected_row_);
            int at = it == rows.end() ? 0 : int(it - rows.begin());
            at = (at + (key == "DOWN" ? 1 : int(rows.size()) - 1)) % int(rows.size());
            credit_selected_row_ = rows[size_t(at)];
        }
        if (key == "N") { credit_open_ = false; credit_draft_ = {}; credit_fees_accepted_ = false; restore_bank_credit(); return {}; }
        return {};
    }
    if (key == "ENTER") return {};
    if (key == "BACKSPACE") { if (!amount_text_.empty()) amount_text_.pop_back(); return {}; }
    return {};
}
bool BankScreen::text(std::string_view value) {
    if (pending_ || investment_warning_open_ || property_notice_open_ || value.empty() ||
        ((exchange_open_ || property_open_ || deposit_open_) && !calculator_open_)) return false;
    if (std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < '0' || c > '9'; })) return false;
    if (calculator_open_) {
        bool changed = false;
        for (unsigned char c : value) {
            const uint32_t digit = c - '0';
            if (calculator_value_ <= (calculator_max_ - std::min(calculator_max_, digit)) / 10) {
                calculator_value_ = calculator_value_ * 10 + digit;
                if (calculator_value_ > calculator_max_) calculator_value_ = calculator_max_;
                changed = true;
            } else if (calculator_value_ < calculator_max_) {
                calculator_value_ = calculator_max_; changed = true;
            }
        }
        amount_text_ = calculator_value_ ? std::to_string(calculator_value_) : std::string{};
        return changed;
    }
    const auto old = amount_text_;
    for (const auto c : value) if (amount_text_.size() < 8) amount_text_.push_back(c);
    return amount_text_ != old;
}
void BankScreen::acknowledge(BankState updated) {
    if (!pending_) throw FormatError("bank: acknowledgment requires a pending action");
    if (updated.credit.counterparty != 0)
        throw FormatError("bank: fresh ACK bank context must remain counterparty zero");
    const bool credit_ack = pending_ && pending_->kind == BankRequestKind::bank_credit;
    bmh::BankCreditContext next_bank_credit = updated.credit;
    if (credit_ack) {
        if (pending_->credit_counterparty != state_.credit_counterparty)
            throw FormatError("bank: pending credit counterparty changed");
        const int accepted_counterparty = pending_->credit_counterparty;
        if (updated.credit_counterparty != 0 && updated.credit_counterparty != accepted_counterparty)
            throw FormatError("bank: fresh credit projection names a different counterparty");
        if (accepted_counterparty == 0) {
            next_bank_credit = updated.credit;
            updated.credit_counterparty = 0;
        } else {
            const auto lender = std::find_if(updated.credit_lenders.begin(), updated.credit_lenders.end(),
                [&](const auto& candidate) { return candidate.counterparty == accepted_counterparty; });
            if (lender == updated.credit_lenders.end() || lender->manager_slot + 1 != accepted_counterparty ||
                lender->credit.counterparty != accepted_counterparty)
                throw FormatError("bank: fresh projection omitted accepted credit lender");
            next_bank_credit = updated.credit;
            updated.credit = lender->credit;
            updated.credit_counterparty = accepted_counterparty;
        }
        const bool expects_notice = credit_draft_.refund > 0;
        if (expects_notice != updated.credit_notice.has_value() ||
            (expects_notice && updated.credit_notice->refund != credit_draft_.refund))
            throw FormatError("bank: missing or mismatched credit refund notice");
    } else if (updated.credit_notice) {
        throw FormatError("bank: unexpected credit notice");
    }
    const bool expects_property_notice = pending_ && pending_->kind == BankRequestKind::buy_fund &&
        pending_->asset_index > 0 && pending_->asset_index < int(state_.funds.size());
    if (expects_property_notice) {
        if (!updated.property_notice || updated.property_notice->asset_index != pending_->asset_index)
            throw FormatError("bank: missing or mismatched property purchase notice");
    } else if (updated.property_notice) {
        throw FormatError("bank: unexpected property purchase notice");
    }
    bool changed = updated.balance != state_.balance || updated.deposit != state_.deposit || updated.last_tip != state_.last_tip;
    changed = changed || updated.credit.saved != state_.credit.saved;
    for (size_t i = 0; i < updated.stocks.size(); ++i)
        changed = changed || updated.stocks[i].owned != state_.stocks[i].owned || updated.stocks[i].paid != state_.stocks[i].paid;
    for (size_t i = 0; i < updated.funds.size(); ++i)
        changed = changed || updated.funds[i].owned != state_.funds[i].owned || updated.funds[i].paid != state_.funds[i].paid;
    if (!pending_ || updated.manager_slot != state_.manager_slot || updated.runtime_club != state_.runtime_club ||
        updated.revision <= state_.revision || !changed)
        throw FormatError("bank: expected a changed projection for the same manager");
    visible_tip_ = pending_->kind == BankRequestKind::stock_tip ? updated.last_tip : std::nullopt;
    // LAB_0004c592 rebuilds the sorted held-type list and preserves its local
    // position, rather than the previous raw type, unless the position wraps.
    int owned_position = 0;
    if (property_open_) {
        for (int property = 0; property < selected_owned_fund_; ++property)
            if (state_.funds[size_t(property)].owned) ++owned_position;
    }
    state_ = std::move(updated);
    bank_credit_ = next_bank_credit;
    property_notice_open_ = state_.property_notice.has_value();
    credit_notice_open_ = credit_ack && state_.credit_notice.has_value();
    if (credit_notice_open_) {
        credit_draft_ = bank_credit_draft(state_.credit);
        refresh_credit_rows();
    }
    if (credit_ack && !credit_notice_open_) {
        credit_open_ = false;
        credit_draft_ = {};
        credit_fees_accepted_ = false;
        restore_bank_credit();
    }
    if (property_open_) {
        std::array<int, 6> held{};
        int count = 0;
        for (int property = 0; property < int(state_.funds.size()); ++property)
            if (state_.funds[size_t(property)].owned) held[size_t(count++)] = property;
        if (owned_position >= count) owned_position = 0;
        selected_owned_fund_ = count ? held[size_t(owned_position)] : -1;
    }
    pending_.reset(); amount_text_.clear(); feedback_.clear();
    calculator_open_ = false; calculator_backdrop_.reset();
}
void BankScreen::reject_action(std::string reason) {
    if (reason == "credit_no_change") {
        pending_.reset();
        credit_open_ = false; credit_draft_ = {}; credit_fees_accepted_ = false; restore_bank_credit(); feedback_.clear(); return;
    }
    if (!pending_ || reason.empty()) throw FormatError("bank: rejection requires a pending request and reason");
    pending_.reset();
    investment_warning_open_ = reason == "investment_limit" && exchange_open_;
    feedback_ = reason == "insufficient_funds" ? "KONTO NICHT GEDECKT" : "AKTION ABGELEHNT";
}
std::string BankScreen::state_json() const {
    auto json = bank_state_json(state_); json.pop_back();
    return json + ",\"withdrawing\":" + (withdrawing_ ? "true" : "false") + ",\"amount_input\":" +
        json_quote(amount_text_) + ",\"deposit_open\":" + (deposit_open_ ? "true" : "false") +
        ",\"finance_open\":" + (finance_open_ ? "true" : "false") +
        ",\"credit_open\":" + (credit_open_ ? "true" : "false") +
        ",\"credit_selected_row\":" + std::to_string(credit_selected_row_) +
        ",\"credit_fee_confirmation_open\":" + (credit_fee_confirmation_open_ ? "true" : "false") +
        ",\"credit_notice_open\":" + (credit_notice_open_ ? "true" : "false") +
        ",\"credit_calculator_field\":" + std::to_string(calculator_credit_field_) +
        ",\"credit_fees_accepted\":" + (credit_fees_accepted_ ? "true" : "false") +
        ",\"credit_edit_count\":" + std::to_string(credit_draft_.edits.size()) +
        ",\"investment_warning_open\":" + (investment_warning_open_ ? "true" : "false") +
        ",\"calculator_open\":" + (calculator_open_ ? "true" : "false") +
        ",\"calculator_value\":" + std::to_string(calculator_value_) +
        ",\"exchange_open\":" + (exchange_open_ ? "true" : "false") + ",\"property_open\":" + (property_open_ ? "true" : "false") +
        ",\"funds_open\":" + (property_open_ ? "true" : "false") +
        ",\"property_notice_open\":" + (property_notice_open_ ? "true" : "false") +
        ",\"selected_property_catalog\":" + std::to_string(selected_fund_) +
        ",\"selected_owned_property\":" + std::to_string(selected_owned_fund_) + ",\"selected_stock\":" +
        std::to_string(selected_stock_) + ",\"selling_shares\":" + (selling_shares_ ? "true" : "false") +
        ",\"pending_action\":" + (pending_ ? bank_request_json(*pending_) : "null") +
        ",\"feedback\":" + (feedback_.empty() ? "null" : json_quote(feedback_)) + '}';
}
} // namespace bmh::ui
