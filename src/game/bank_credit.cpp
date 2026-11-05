// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/bank_credit.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include "game/matchday.hpp"

namespace bmh {
namespace {
constexpr uint32_t kLoans = ::bmh::layout::Loans;
constexpr size_t kRecord = 0xb0c;
int32_t signed_word(uint32_t v) { return std::bit_cast<int32_t>(v); }
uint32_t word(int32_t v) { return std::bit_cast<uint32_t>(v); }
int32_t add(int32_t a, int32_t b) { return signed_word(word(a) + word(b)); }
int32_t sub(int32_t a, int32_t b) { return signed_word(word(a) - word(b)); }
int32_t mul(int32_t a, int32_t b) { return signed_word(word(a) * word(b)); }
int32_t divide(int32_t a, int32_t b) {
    if (!b || (a == std::numeric_limits<int32_t>::min() && b == -1))
        throw std::invalid_argument("invalid native credit division");
    return a / b;
}
uint32_t read_record(const GameState& g, size_t at) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= uint32_t(g.manager_records.at(at + i)) << (i * 8);
    return v;
}
// The original evaluates ((base / 100) * den) / 100 on the x87 stack: every step is rounded to a
// 64-bit mantissa (round to nearest even), only the final store rounds to a 53-bit double. The
// host's long double is not that format (64 bits on MSVC and on Apple silicon), which changed the
// result by 1 for loans of about a million and more. This evaluates the same chain exactly with
// integers, so the result is the same on every platform. Returns the value truncated toward zero.
struct Wide {  // 128-bit unsigned, little-endian 32-bit limbs
    std::array<uint32_t, 4> l{};
    unsigned bit_length() const {
        for (int i = 3; i >= 0; --i)
            if (l[size_t(i)]) return unsigned(i) * 32 + unsigned(32 - std::countl_zero(l[size_t(i)]));
        return 0;
    }
    bool bit(unsigned n) const { return n < 128 && (l[n / 32] >> (n % 32)) & 1u; }
    bool any_below(unsigned n) const {  // any bit below position n set
        for (unsigned i = 0; i < n && i < 128; ++i) if (bit(i)) return true;
        return false;
    }
    static Wide from(uint64_t v) { Wide w; w.l[0] = uint32_t(v); w.l[1] = uint32_t(v >> 32); return w; }
    uint64_t window(unsigned lo) const {  // the 64 bits starting at bit `lo`
        uint64_t v = 0;
        for (unsigned i = 0; i < 64; ++i) if (bit(lo + i)) v |= uint64_t(1) << i;
        return v;
    }
};
struct Extended { uint64_t m = 0; int e = 0; };  // value = m * 2^e, m has 64 bits (or is zero)
// Round `w` (plus a "something was dropped" flag) to 64 bits, nearest even; value = w * 2^exp.
Extended round_to_64(const Wide& w, bool sticky, int exp) {
    const unsigned len = w.bit_length();
    Extended r;
    if (len == 0) return r;
    if (len <= 64) {
        const unsigned up = 64 - len;
        r.m = w.window(0) << up;
        r.e = exp - int(up);
        return r;
    }
    const unsigned s = len - 64;
    r.m = w.window(s);
    const bool guard = w.bit(s - 1), rest = sticky || w.any_below(s - 1);
    r.e = exp + int(s);
    if (guard && (rest || (r.m & 1))) {
        if (++r.m == 0) { r.m = uint64_t(1) << 63; ++r.e; }
    }
    return r;
}
Extended x87_divide_small(const Extended& a, uint32_t d) {
    Wide n;                       // a.m * 2^64, divided limb by limb
    n.l[2] = uint32_t(a.m); n.l[3] = uint32_t(a.m >> 32);
    uint64_t rem = 0;
    Wide q;
    for (int i = 3; i >= 0; --i) {
        const uint64_t cur = (rem << 32) | n.l[size_t(i)];
        q.l[size_t(i)] = uint32_t(cur / d);
        rem = cur % d;
    }
    return round_to_64(q, rem != 0, a.e - 64);
}
Extended x87_multiply_small(const Extended& a, uint32_t b) {
    Wide p;
    uint64_t carry = 0;
    const uint32_t limb[2] = {uint32_t(a.m), uint32_t(a.m >> 32)};
    for (int i = 0; i < 2; ++i) {
        const uint64_t cur = uint64_t(limb[i]) * b + carry;
        p.l[size_t(i)] = uint32_t(cur);
        carry = cur >> 32;
    }
    p.l[2] = uint32_t(carry);
    return round_to_64(p, false, a.e);
}
// ((base / 100) * den) / 100 stored as a double and truncated toward zero. `zero` reports an exact
// zero before truncation (the original's "exact zero becomes 1" rule looks at the stored double).
int64_t x87_amount_chain(int32_t base, uint32_t den, bool& zero) {
    zero = base == 0 || den == 0;
    if (zero) return 0;
    const bool negative = base < 0;
    const uint64_t magnitude = negative ? uint64_t(-int64_t(base)) : uint64_t(base);
    Extended v = round_to_64(Wide::from(magnitude), false, 0);
    v = x87_divide_small(v, 100);
    v = x87_multiply_small(v, den);
    v = x87_divide_small(v, 100);
    // store as double: 53-bit mantissa, nearest even
    uint64_t m53 = v.m >> 11;
    int e = v.e + 11;
    const bool guard = (v.m >> 10) & 1, rest = (v.m & 0x3ff) != 0;
    if (guard && (rest || (m53 & 1))) ++m53;  // a carry to 2^53 is still exact
    uint64_t whole;
    if (e >= 0) {
        if (e > 10) throw std::invalid_argument("invalid native credit amount");
        whole = m53 << e;
    } else {
        whole = -e >= 64 ? 0 : m53 >> unsigned(-e);
    }
    if (whole > uint64_t(std::numeric_limits<int32_t>::max()) + (negative ? 1u : 0u))
        throw std::invalid_argument("invalid native credit amount");
    return negative ? -int64_t(whole) : int64_t(whole);
}
// Amount: signed IMUL/IDIV, x87 intermediate, QWORD store,
// exact-zero floor, then Amount2's explicit rounding toward zero.
int32_t amount(const BankCreditContext& c, uint32_t months, int32_t principal) {
    if (months > 48 || c.rate_table[months] <= 100 || c.divisor != 100.0)
        throw std::invalid_argument("invalid native credit factors");
    const int32_t factor = c.rate_table[months];
    const int32_t base = divide(mul(principal, factor), 100);
    const int32_t den = divide(10000, factor - 100);
    bool zero = false;
    int64_t result = x87_amount_chain(base, uint32_t(den), zero);
    if (zero && principal != 0) result = 1;
    return static_cast<int32_t>(result);
}
void balance(const BankCreditContext& c, BankCreditDraft& d) {
    int32_t result = add(c.cash, signed_word(d.values[0]));
    result = sub(result, signed_word(c.saved[0]));
    result = sub(result, signed_word(d.values[6]));
    result = add(result, d.refund);
    result = sub(result, d.fees);
    d.values[7] = word(sub(result, d.interest_adjustment));
}
}

BankCreditContext bank_credit_context(const GameState& g, const LeImage& exe, int manager, int counterparty) {
    BankCreditContext c;
    if (manager < 0 || size_t(manager) >= g.manager_teams.size()
        || g.manager_records.size() < (size_t(manager) + 1) * kRecord) return c;
    // case 5: another human manager lends (row lender + 1 of the borrower's matrix)
    if (counterparty < 0 || counterparty > 4 || counterparty - 1 == manager ||
        (counterparty > 0 && size_t(counterparty - 1) >= g.manager_teams.size())) return c;
    c.counterparty = counterparty;
    const uint32_t at = kLoans + uint32_t(manager) * 0x78 + uint32_t(counterparty) * 0x18;
    try {
        for (size_t i = 0; i < c.saved.size(); ++i) c.saved[i] = g.memory.u32(at + uint32_t(i) * 4);
        for (size_t i = 0; i < c.rate_table.size(); ++i) c.rate_table[i] = exe.u8(::bmh::layout::BankCredit_BankCreditContext_RuntimeField3 + uint32_t(i));
        const uint64_t bits = uint64_t(exe.u32(::bmh::layout::BankCredit_BankCreditContext_RuntimeField)) | uint64_t(exe.u32(::bmh::layout::BankCredit_BankCreditContext_RuntimeField2)) << 32;
        c.divisor = std::bit_cast<double>(bits);
        const size_t rec = size_t(manager) * kRecord;
        c.cash = signed_word(read_record(g, rec + 0x314));
        c.overdraft = signed_word(read_record(g, rec + 0x31c));
        if (counterparty > 0) {
            const size_t lender = size_t(counterparty - 1) * kRecord;
            c.lender_liquidity = add(signed_word(read_record(g, lender + 0x314)), signed_word(read_record(g, lender + 0x31c)));
        }
        c.available = c.divisor == 100.0 && c.saved[3] <= 48 && c.saved[4] <= 48;
        for (auto factor : c.rate_table) c.available = c.available && factor > 100;
    } catch (const std::out_of_range&) { return {}; }
    return c;
}

bool change_bank_credit_interest(GameState& g, int manager, uint8_t new_rate) {
    if (manager < 0 || size_t(manager) >= g.manager_teams.size() || !g.credit_rate_table_available)
        return false;
    const uint32_t at = kLoans + uint32_t(manager) * 0x78;
    for (uint32_t i = 0; i < 24; ++i) if (!g.memory.has(at + i)) return false;
    try {
        const int32_t principal = signed_word(g.memory.u32(at));
        const int32_t term = signed_word(g.memory.u32(at + 12));
        const int32_t elapsed = signed_word(g.memory.u32(at + 16));
        const int32_t remaining = sub(principal, mul(divide(principal, term), elapsed));
        const int32_t adjusted = divide(remaining, 100);
        const int32_t next_principal = mul(adjusted, 100 - int32_t(new_rate));
        const int32_t next_term = sub(term, elapsed);
        BankCreditContext c;
        c.rate_table = g.credit_rate_table;
        c.divisor = g.credit_divisor;
        const uint32_t payment = word(amount(c, uint32_t(next_term), next_principal));
        g.memory.set32(at, word(next_principal));
        g.memory.set32(at + 12, word(next_term));
        g.memory.set32(at + 16, 0);
        g.memory.set32(at + 20, 0);
        g.memory.set32(at + 8, payment);
        g.memory.set32(at + 4, payment * uint32_t(next_term));
        return true;
    } catch (const std::exception&) { return false; }
}

BankCreditDraft bank_credit_draft(const BankCreditContext& c) {
    BankCreditDraft d;
    for (size_t i = 0; i < c.saved.size(); ++i) d.values[i] = c.saved[i];
    // Source clears the working paid field after its first rendering. The UI
    // still uses the saved field for that initial display.
    d.values[5] = 0;
    d.values[7] = word(c.cash);
    return d;
}

BankCreditBounds bank_credit_bounds(const BankCreditContext& c, const BankCreditDraft& d, int field) {
    if (!c.available || (field != 0 && field != 1 && field != 3 && field != 6 && !(field == 2 && c.counterparty))
        || (field == 6 && !c.saved[0])) return {};
    try {
        const auto& v = d.values;
        int32_t accrued = 0;
        if (v[0]) {
            const uint32_t elapsed = v[4] ? v[4] : 1;
            accrued = sub(mul(amount(c, elapsed, signed_word(v[0])), signed_word(elapsed)),
                signed_word(v[5]));
        }
        const int32_t liquidity = add(c.cash, c.overdraft);
        int32_t lo = 0, hi = 0;
        if (field == 0) {
            lo = 1000;
            hi = sub(12000000, accrued);
            // case 5: no more than the lender can pay out (balance + credit line)
            if (c.counterparty && c.lender_liquidity < hi) hi = sub(c.lender_liquidity, accrued);
        }
        if (field == 2) {
            // case 5 only: the monthly payment within 20 % of the annuity (Amount
            // with the term and the net principal); the input starts at the annuity of
            // the whole principal
            const int32_t rate = amount(c, v[3], signed_word(v[0] - v[6]));
            lo = sub(rate, rate / 5);
            hi = add(rate, rate / 5);
            if (lo >= hi || lo < 0) return {};
            return {word(lo), word(hi), word(amount(c, v[3], signed_word(v[0]))), true};
        }
        if (field == 1) { lo = c.cash < accrued ? sub(accrued, liquidity) : liquidity; hi = accrued; }
        if (field == 3) {
            lo = 1;
            hi = amount(c, v[3], signed_word(v[0] - v[6])) == 0 ? 0 : 48;
        }
        if (field == 6) { hi = liquidity < accrued ? liquidity : signed_word(v[0]); }
        // Source checks availability before overriding the principal bounds.
        if (lo >= hi) return {};
        if (field == 0 && lo < signed_word(c.saved[0])) { lo = signed_word(c.saved[0]); hi = 12000000; }
        if (lo < 0 || hi < 0 || lo >= hi) return {};
        return {word(lo), word(hi), word(lo - (lo == 1)), true};
    } catch (const std::invalid_argument&) { return {}; }
}

bool edit_bank_credit(const BankCreditContext& c, BankCreditDraft& draft, int field, uint32_t value) {
    const auto bounds = bank_credit_bounds(c, draft, field);
    if (!bounds.available || value < bounds.minimum || value > bounds.maximum) return false;
    BankCreditDraft d = draft;
    try {
        if (!d.interest_adjustment) {
            d.principal_paid = c.saved[3]
                ? mul(signed_word(c.saved[4]), divide(signed_word(c.saved[0]), signed_word(c.saved[3])))
                : mul(signed_word(c.saved[0]), signed_word(c.saved[4]));
        }
        d.values[field] = value;
        // fees only for the bank (case 4)
        d.fees = !c.counterparty && c.saved[0] && (d.values[6] || d.values[3] != c.saved[3]) ? 5000 : 0;
        if (!d.interest_adjustment && (field == 0 || field == 3 || field == 6)) {
            const int32_t term = c.saved[3] ? signed_word(c.saved[3]) : 1;
            const int32_t saved_p = signed_word(c.saved[0]);
            const int32_t remaining = sub(saved_p, d.principal_paid);
            const int32_t interest = sub(sub(amount(c, 1, saved_p), saved_p),
                sub(amount(c, 1, remaining), remaining));
            d.interest_adjustment = divide(mul(interest, signed_word(c.saved[3] - c.saved[4])), term);
            d.refund = mul(signed_word(c.saved[4]), divide(saved_p, term));
            d.values[4] = d.values[5] = 0;
            d.principal_paid = 0;
        }
        if (field != 2 && field != 3 && field != 6) d.values[3] = 48;
        const int32_t net = signed_word(d.values[0] - d.values[6]);
        // a monthly payment set in case 5 stays; every other edit computes the annuity
        if (field != 2) d.values[2] = net ? word(amount(c, d.values[3], net)) : 0;
        if (d.fees) d.fees = add(d.fees, sub(amount(c, 1, signed_word(d.values[6])), signed_word(d.values[6])));
        d.values[1] = d.values[2] * d.values[3];
        d.initial_display = false;
        d.edits.push_back({field, value});
        balance(c, d);
    } catch (const std::invalid_argument&) { return false; }
    draft = std::move(d);
    return true;
}

BankCreditResult commit_bank_credit(GameState& g, const LeImage& exe, int manager,
    const std::array<uint32_t, 6>& expected, const std::vector<BankCreditEdit>& edits, bool accept_fees,
    int counterparty) {
    if (manager < 0 || size_t(manager) >= g.manager_teams.size()) return {BankCreditStatus::invalid_manager};
    const auto c = bank_credit_context(g, exe, manager, counterparty);
    if (!c.available) return {BankCreditStatus::invalid_state};
    if (c.saved != expected) return {BankCreditStatus::stale_request};
    if (edits.empty()) return {BankCreditStatus::no_change};
    if (edits.size() > 4096) return {BankCreditStatus::invalid_request};
    auto d = bank_credit_draft(c);
    for (const auto& edit : edits)
        if (!edit_bank_credit(c, d, edit.field, edit.value)) return {BankCreditStatus::invalid_request};
    // only the bank charges (and asks for) fees and the interest adjustment
    const int32_t fees = c.counterparty ? 0 : add(d.fees, d.interest_adjustment);
    const int lender = c.counterparty - 1;
    if (fees && !accept_fees) return {BankCreditStatus::confirmation_required, fees, d.refund};
    auto resulting_record = c.saved;
    if (!d.values[1]) resulting_record.fill(0);
    else {
        for (size_t i = 0; i < 5; ++i) resulting_record[i] = d.values[i];
        resulting_record[0] -= d.values[6];
        if (signed_word(resulting_record[1]) < signed_word(resulting_record[0]))
            resulting_record[1] += resulting_record[0];
        if (signed_word(resulting_record[0]) < 1) resulting_record[0] = 1;
        if (d.refund) resulting_record[4] = resulting_record[5] = 0;
    }
    if (!fees && !d.refund && c.saved[0] - d.values[0] + d.values[6] == 0
        && resulting_record == c.saved) return {BankCreditStatus::no_change};
    GameState staged = g;
    if (fees) book_money(staged, manager, fees, true, false);
    if (d.refund) {
        d.values[4] = 0;
        // the repaid part comes back from the bank or from the lender
        if (lender >= 0) book_money(staged, lender, d.refund, true, false);
        book_money(staged, manager, sub(0, d.refund), true, false);
    }
    const int32_t principal_change = signed_word(c.saved[0] - d.values[0] + d.values[6]);
    book_money(staged, manager, principal_change, true, false);
    if (lender >= 0) book_money(staged, lender, sub(0, principal_change), true, false);
    const uint32_t at = kLoans + uint32_t(manager) * 0x78 + uint32_t(c.counterparty) * 0x18;
    if (!d.values[1]) {
        for (uint32_t i = 0; i < 6; ++i) staged.memory.set32(at + i * 4, 0);
    } else {
        d.values[0] -= d.values[6];
        if (signed_word(d.values[1]) < signed_word(d.values[0])) d.values[1] += d.values[0];
        if (signed_word(d.values[0]) < 1) d.values[0] = 1;
        for (uint32_t i = 0; i < 5; ++i) staged.memory.set32(at + i * 4, d.values[i]);
        if (d.refund) staged.memory.set32(at + 20, 0);
    }
    g = std::move(staged);
    return {BankCreditStatus::accepted, fees, d.refund};
}
} // namespace bmh
