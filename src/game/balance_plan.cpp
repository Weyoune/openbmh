// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/environment.hpp"
#include "game/balance_plan.hpp"

#include <algorithm>
#include <cmath>

#include "game/prediction.hpp"
#include "game/schedule.hpp"
#include "game/table.hpp"
#include "game/week.hpp"

namespace bmh {

namespace {

constexpr size_t kRecordSize = 0xb0c;
constexpr uint32_t kPlanTicketPrice = ::bmh::layout::PlanTicketPrice;  // one byte per manager
// BalancePlanRuntimeField: months left in the business year by calendar month (July: 12, June: 1)
constexpr int8_t kMonthsLeft[13] = {0, 6, 5, 4, 3, 2, 1, 12, 11, 10, 9, 8, 7};
// BalancePlanRuntimeField2: which rows can be changed; BalancePlanRuntimeField3: the class of the maximum
// (BalancePlanRuntimeField4: 30 million, 25, 100000, 2 million)
constexpr uint8_t kEditable[0x16] = {1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1};
constexpr uint8_t kMaximumClass[0x16] = {0, 0, 0, 0, 3, 0, 0, 0, 0, 3, 3, 3, 3, 0, 3, 3, 0, 0, 0, 1, 2, 1};
constexpr int32_t kMaximum[4] = {30000000, 25, 100000, 2000000};
// BalancePlanRuntimeField5: monthly running costs of the stadium facilities (BMHExeXa)
constexpr uint16_t kFacilityCost[22] = {2000, 3000, 3000, 8000, 4000, 3000, 2500, 2500, 2500, 2500, 2600,
                                        2600, 2600, 2600, 13000, 9000, 9000, 8000, 1000, 1500, 1500, 2000};
// BalancePlanRuntimeField6: monthly income of the first six facilities (FacilityIncome)
constexpr uint16_t kFacilityIncome[6] = {0, 5000, 5000, 4800, 2400, 8000};
// BalancePlanRuntimeField7: catering income per visitor of the facilities 0-13 (BookMoney)
constexpr uint8_t kPerVisitor[14] = {0, 10, 10, 3, 5, 15, 3, 3, 3, 3, 2, 2, 2, 2};
// Rec32: price units of the six funds (BMH.exe+MonthRuntimeField10)
constexpr int32_t kShareUnits[6] = {15, 18, 21, 24, 27, 30};

int32_t rec32(const GameState& g, int m, size_t o) {
    const size_t at = size_t(m) * kRecordSize + o;
    const auto& r = g.manager_records;
    return int32_t(uint32_t(r.at(at)) | uint32_t(r.at(at + 1)) << 8 | uint32_t(r.at(at + 2)) << 16 | uint32_t(r.at(at + 3)) << 24);
}
void set_rec32(GameState& g, int m, size_t o, int32_t v) {
    for (int i = 0; i < 4; i++) g.manager_records.at(size_t(m) * kRecordSize + o + size_t(i)) = uint8_t(uint32_t(v) >> (8 * i));
}
uint8_t rec8(const GameState& g, int m, size_t o) { return g.manager_records.at(size_t(m) * kRecordSize + o); }
int32_t mem32(const GameState& g, uint32_t a) { return g.memory.has(a + 3) ? int32_t(g.memory.u32(a)) : 0; }
int32_t wrap(int64_t v) { return int32_t(uint32_t(uint64_t(v))); }

// Add32: the level of a facility (facility 0: its size step)
uint32_t facility_level(const GameState& g, int m, int k) {
    const uint8_t v = rec8(g, m, size_t(k));
    return k == 0 ? uint32_t((v - 1) / 0x55 + 1) & 0xff : 1;
}

// DailyInterest2(0): the interest of one day without booking it - negative is income
// (balance 1 %, fixed deposit 2 % a year), positive an expense (overdraft 12 %, beyond the
// credit line 18 %). The divisor is -36500.
int32_t daily_interest_preview(const GameState& g, int m) {
    constexpr int32_t kAnnual = -0x8e94;
    const int32_t balance = rec32(g, m, 0x314), deposit = rec32(g, m, 0x318), credit = rec32(g, m, 0x31c);
    int32_t result = 0;
    if (balance < 0) {
        const int32_t with_credit = wrap(int64_t(balance) + credit);
        if (with_credit < 0) result = wrap(int64_t(with_credit) * 0x12) / kAnnual;
        int32_t below = wrap(-int64_t(credit));
        if (below < balance) below = balance;
        result = wrap(int64_t(result) + wrap(int64_t(below) * 0xc) / kAnnual);
    } else {
        result = balance / kAnnual;
    }
    return wrap(int64_t(result) + wrap(int64_t(deposit / 10000) * 20000) / kAnnual);
}

// SharesAndLoans(manager, flags, months): flags 1 the dividends of the manager's funds and
// the loans he gave to other managers (income), 2 the loans he took (expense, negative);
// flag 4 multiplies by the months (loans: at most their remaining months).
int32_t shares_and_loans(const GameState& g, int m, int flags, int months) {
    int32_t sum = 0;
    if (flags & 1) {
        for (int k = 0; k < 6; k++) {
            const uint32_t unit = g.memory.u32(::bmh::layout::FundBlock + uint32_t(k) * 0x1c) * uint32_t(kShareUnits[k]) / 10000;
            sum = wrap(int64_t(sum) + int64_t(unit * g.memory.u16(::bmh::layout::Advertising_OriginalShareAdjustment_RuntimeField + uint32_t(m) * 2 + uint32_t(k) * 0x1c)));
        }
        if (flags & 4) sum = wrap(int64_t(sum) * months);
    }
    const int managers = g.memory.u8(::bmh::layout::ManagerCount);
    for (int c = 0; c <= managers; c++) {
        uint32_t row = uint32_t(c) * 0x18 + uint32_t(m) * 0x78;
        int32_t amount = mem32(g, ::bmh::layout::Loans + row + 8);
        if (amount != 0 && (flags & 2)) {
            if (!(flags & 4)) {
                sum = wrap(int64_t(sum) - amount);
            } else {
                int left = uint8_t(g.memory.u8(::bmh::layout::Loans + row + 0xc) - g.memory.u8(::bmh::layout::Loans + row + 0x10));
                sum = wrap(int64_t(sum) - int64_t(std::min(left, months)) * amount);
            }
        }
        if (c == 0) continue;
        row = uint32_t(m + 1) * 0x18 + uint32_t(c - 1) * 0x78;
        amount = mem32(g, ::bmh::layout::Loans + row + 8);
        if (amount != 0 && (flags & 1)) {
            if (!(flags & 4)) {
                sum = wrap(int64_t(sum) + amount);
            } else {
                int left = uint8_t(g.memory.u8(::bmh::layout::Loans + row + 0xc) - g.memory.u8(::bmh::layout::Loans + row + 0x10));
                sum = wrap(int64_t(sum) + int64_t(std::min(left, months)) * amount);
            }
        }
    }
    return sum;
}

// MonthlyPay(manager, 0xb): a month of the squad's pay - bonus base (+0x4e), salary
// (+0x50) and the bonus each player can expect (ContractOffer with the fitness correction)
// for the predicted final rank (Void5 mode 4). Changes scratch tables: `g` is a copy.
int32_t monthly_pay(GameState& g, int m, uint8_t flags = 0xb) {
    const int team = g.manager_teams.at(size_t(m));
    std::array<uint8_t, 24> buf{};
    const int predicted = table_prediction(g, team, 4, buf);
    const int rank = predicted == 0 ? 0xb : predicted + 1;
    const SquadRange range = squad_range(team);
    int32_t sum = 0;
    for (int s = 0; s < 0x18; s++) {
        const size_t slot = size_t(m) * 0x840 + size_t(s) * 0x58;
        if (g.tactics.at(slot + 0x3d) == 0xff) continue;
        const int32_t bonus = int32_t((g.tactics.at(slot + 0x4e) | g.tactics.at(slot + 0x4f) << 8) & 0x7fff);
        const int32_t salary = int32_t(uint32_t(g.tactics.at(slot + 0x50)) | uint32_t(g.tactics.at(slot + 0x51)) << 8 |
                                       uint32_t(g.tactics.at(slot + 0x52)) << 16 | uint32_t(g.tactics.at(slot + 0x53)) << 24);
        // a board row whose squad slot is empty (0xfff) makes the original read a player
        // beyond the table; the port counts no expected bonus for it
        const size_t player = g.squad_slots.at(range.first + size_t(s)) & 0xfff;
        const int32_t expected = (flags & 1) && player < g.players.size() ? int32_t(expected_bonus(g, m, rank, s, int(player), true)) : 0;
        sum = wrap(int64_t(sum) + ((flags & 2) ? salary : 0) +
                   ((flags & 1) ? expected : 0) + ((flags & 8) ? bonus : 0));
    }
    return sum;
}

// BMHExeXa: the facilities' monthly running costs
int32_t facility_costs(const GameState& g, int m) {
    int32_t sum = 0;
    for (int i = 0; i < 0xe; i++) sum += kFacilityCost[i];
    for (int i = 0xe; i < 0x16; i++) sum += (rec8(g, m, 0x2e + size_t(i)) & 7) * kFacilityCost[i];
    return sum;
}

// FacilityIncome: the facilities' monthly income
int32_t facility_income(const GameState& g, int m) {
    int32_t sum = 0;
    for (int k = 0; k < 6; k++)
        if (rec8(g, m, size_t(k)) != 0) sum += int32_t(facility_level(g, m, k)) * kFacilityIncome[k];
    return sum;
}

// BookMoney(manager, gate, 1): kiosks and restaurants of one home game, without the
// random factor
uint32_t catering_per_game(const GameState& g, int m, int32_t gate) {
    uint32_t sum = 0;
    for (int k = 0; k < 14; k++)
        if (rec8(g, m, size_t(k)) != 0) sum += kPerVisitor[k] * uint32_t(gate) * facility_level(g, m, k) / 100;
    return sum;
}

// The redraw of BalanceFromGame2 (with its continuation Redraw and the patch at
// Redraw2): recomputes the items if `refresh` asks for it, then the spectator income
// and the totals.
void redraw(const GameState& source, BalancePlanDraft& d) {
    const int m = d.manager;
    const int view = int(d.view);
    auto& r = d.rows;
    r[3] = view < 2 ? rec32(source, m, 0x868) : 0;
    int32_t home = r[kBalanceLeagueHomeGames] + r[kBalanceOtherHomeGames];  // local_48
    if (view == 0) home = 0;
    if (view < 2) {
        const int month = month_code(source.date);
        d.months_left = month >= 1 && month <= 12 ? kMonthsLeft[month] : 0;
        d.days_left = 0x16d - source.season_day();
        const bool first_of_month = source.date == 0 || month_code(source.date - 1) != month;
        if (source.season_day() != 0 && month == 7 && first_of_month) d.months_left = d.days_left = 0;
        home -= int8_t(rec8(source, m, 0x28c)) + int8_t(rec8(source, m, 0x28d)) + int8_t(rec8(source, m, 0x28e));
        if (home < 0) home = 0;
    } else {
        d.months_left = 12;
        d.days_left = 0x16d;
    }
    const int months = int8_t(d.months_left);
    if (d.refresh != 0) {
        if (view < 2) {
            for (int i = 0; i < kBalanceItems; i++) r[size_t(i)] = rec32(source, m, 0x85c + size_t(i) * 4);
            r[7] = wrap(int64_t(rec32(source, m, 0x33c)) + rec32(source, m, 0x334) + rec32(source, m, 0x338));
        } else {
            std::fill(r.begin(), r.begin() + kBalanceItems, 0);
        }
        if (view != 0 && months != 0)
            for (int i = 0; i < kBalanceItems; i++) r[size_t(i)] = wrap(int64_t(r[size_t(i)]) + d.adjustments[size_t(i)] * 12 / months);
        if (d.refresh != 2) r[kBalanceAverageGate] = r[kBalanceGateSoFar];
        if (view != 0) {
            GameState g = source;  // scratch copy, see the header
            const int32_t interest = wrap(int64_t(int16_t(d.days_left)) * daily_interest_preview(g, m));
            if (interest < 1) r[5] = wrap(int64_t(r[5]) - interest);
            else r[0x10] = wrap(int64_t(r[0x10]) + interest);
            r[5] = wrap(int64_t(r[5]) + shares_and_loans(g, m, 5, months));
            r[0x10] = wrap(int64_t(r[0x10]) - shares_and_loans(g, m, 6, months));
            // advertising: shirt (+0xadc) and TV (+0xae8) contracts, the 14 boards (+0xa34);
            // free boards are expected to bring 80 % of the average board
            r[0] = wrap(int64_t(r[0]) + int64_t(rec32(g, m, 0xadc)) * months);
            r[2] = wrap(int64_t(r[2]) + int64_t(months) * rec32(g, m, 0xae8));
            int boards = 0;
            for (int s = 0; s < 0xe; s++) {
                const int32_t v = rec32(g, m, 0xa34 + size_t(s) * 0xc);
                if (v != 0) boards++;
                r[1] = wrap(int64_t(r[1]) + int64_t(v) * months);
            }
            if (g.memory.u8(::bmh::layout::GameMode) == 1 || (g.memory.u8(::bmh::layout::CareerDuration) == 1 && g.date == 0)) boards = 0xe;
            if (boards != 0 && boards < 0xe) {
                int32_t missing = (r[1] / boards) * (0xe - boards);
                if (months != 0) missing /= months;
                r[1] = wrap(int64_t(r[1]) + int64_t(months) * ((int64_t(missing) * 0x50) / 100));
            }
            r[7] = wrap(int64_t(r[7]) + int64_t(monthly_pay(g, m)) * months);
            r[8] = wrap(int64_t(r[8]) + int64_t(facility_costs(g, m)) * months);
            r[4] = wrap(int64_t(r[4]) + int64_t(facility_income(g, m)) * months);
            const uint32_t catering = catering_per_game(g, m, r[kBalanceAverageGate]);
            r[4] = wrap(int64_t(r[4]) + int64_t((catering * uint32_t(home)) / 12) * months);
            const uint32_t costs = ::bmh::layout::Spending + uint32_t(m) * 0x24;
            for (int k = 0; k < 3; ++k) {
                const auto value = environment_monthly_cost(g, m, k);
                const auto raw = mem32(g, costs + uint32_t(k) * 4);
                const int count = k == kYouthAcademy && raw > kEnvironmentStep[k] * 6 ? 1 : months;
                r[9] = wrap(int64_t(r[9]) + int64_t(count) * value);
            }
            for (int k = 0; k < 3; k++) r[size_t(10 + k)] = wrap(int64_t(r[size_t(10 + k)]) + int64_t(months) * mem32(g, costs + 0xc + uint32_t(k) * 4));
            for (int k = 0; k < 2; k++) r[size_t(14 + k)] = wrap(int64_t(r[size_t(14 + k)]) + int64_t(months) * rec32(g, m, 0x854 + size_t(k) * 4));
        }
        d.refresh = 0;
    }
    // the view "real" leaves out the season tickets of the remaining league home games
    int32_t season_tickets = 0;
    if (view == 1) {
        int32_t sold = int32_t(uint16_t(rec8(source, m, 0x2ac) | rec8(source, m, 0x2ad) << 8));
        if (r[kBalanceAverageGate] < sold) sold = r[kBalanceAverageGate];
        int32_t games = r[kBalanceLeagueHomeGames] - int8_t(rec8(source, m, 0x28c));
        if (games < 0) games = 0;
        season_tickets = wrap(int64_t((sold * 0x46) / 100) * r[kBalanceTicketPrice] * games);
    }
    r[3] = wrap(int64_t(r[3]) + (int64_t(home) * r[kBalanceTicketPrice] * r[kBalanceAverageGate] - season_tickets));
    double total = 0;
    for (int i = 0; i < kBalanceItems; i++) {
        if (r[size_t(i)] < 0) r[size_t(i)] = 0;
        total += double(r[size_t(i)]) * (i < 7 ? 1 : -1);
    }
    d.depreciation = view < 2 ? rec32(source, m, 0x30c) : 0;
    d.total = total - double(d.depreciation);
}

}  // namespace

// StadiumRequiredAttendance: Source's monthly preview differs from booked month_change totals.
// Prediction may touch scratch tables; both pay previews run on an owned copy.
int32_t stadium_required_attendance(const GameState& source, int manager) {
    if (manager < 0 || manager > 4) throw FormatError("stadium attendance: invalid manager");
    GameState g = source;
    const int team = g.manager_teams.at(size_t(manager));
    const int league = team > (kNoTeam - 1) ? 0 : team_league(team);
    if (league < 0 || league >= kLeagueCount)
        throw FormatError("stadium attendance: manager has no Source league");
    const int home_games = league_matchdays(league) / 2;
    if (home_games <= 0) throw FormatError("stadium attendance: invalid Source league home count");
    const int m = manager;
    int32_t costs = wrap(int64_t(facility_costs(g, m)) + rec32(g, m, 0x854) + rec32(g, m, 0x858));
    costs = wrap(int64_t(costs) + monthly_pay(g, m, 0xa));
    for (int k = 0; k < 6; ++k) {
        const int32_t value = environment_monthly_cost(g, m, k);
        costs = wrap(int64_t(costs) + value);
    }
    costs = wrap(int64_t(costs) - shares_and_loans(g, m, 2, 0) + int(rec8(g, m, 0x179)) * 1000);
    if (costs < 0) costs = 0;
    int32_t income = facility_income(g, m);
    for (int i = 0; i < 16; ++i) income = wrap(int64_t(income) + rec32(g, m, 0xa34 + size_t(i) * 12));
    constexpr int days[13] = {0,31,28,31,30,31,30,31,31,30,31,30,31}; // DeskState_DeskDate_RuntimeField
    const int month = month_code(g.date);
    if (month < 1 || month > 12) throw FormatError("stadium attendance: invalid Source month");
    income = wrap(int64_t(income) + shares_and_loans(g, m, 1, 0) -
                  int64_t(daily_interest_preview(g, m)) * days[month]);
    const int32_t bonus = monthly_pay(g, m, 9);
    int32_t needed = wrap(int64_t(wrap(int64_t(costs) - income)) * 12 + bonus) / home_games;
    const int price = rec8(g, m, 0x26e);
    if (price != 0) needed /= price;
    return std::max(needed, int32_t(0));
}

BalancePlanDraft open_balance_plan(const GameState& g, int manager) {
    BalancePlanDraft d;
    d.manager = manager;
    const int m = manager;
    d.view = BalanceView(std::clamp(int(int8_t(rec8(g, m, 0x310))), 0, 2));
    const int league = team_league(g.manager_teams.at(size_t(m)));
    auto& r = d.rows;
    r[kBalanceLeagueHomeGames] = league < kLeagueCount ? league_matchdays(league) / 2 : 0;
    r[kBalanceOtherHomeGames] = rec32(g, m, 0x2f8);
    r[kBalanceAverageGate] = rec32(g, m, 0x2fc);
    uint8_t ticket = g.memory.has(kPlanTicketPrice + uint32_t(m)) ? g.memory.u8(kPlanTicketPrice + uint32_t(m)) : 0;
    if (ticket == 0) ticket = rec8(g, m, 0x26e);
    r[kBalanceTicketPrice] = ticket;
    r[kBalanceAccount] = rec32(g, m, 0x314);
    // the average gate of the last league home games (+0x340, count +0x283)
    int32_t sum = 0;
    const int games = rec8(g, m, 0x283);
    for (int i = 0; i < games; i++) sum = wrap(int64_t(sum) + rec32(g, m, 0x340 + size_t(i) * 4));
    r[kBalanceGateSoFar] = games != 0 ? sum / games : sum;
    if (rec32(g, m, 0x300) != 0) r[kBalanceAverageGate] = rec32(g, m, 0x304) / rec32(g, m, 0x300);
    for (int i = 0; i < kBalanceItems; i++) d.adjustments[size_t(i)] = rec32(g, m, 0x2b0 + size_t(i) * 4);
    d.refresh = 2;
    redraw(g, d);
    return d;
}

void cycle_balance_view(const GameState& g, BalancePlanDraft& d) {
    d.view = BalanceView((int(d.view) + 1) % 3);
    d.refresh = d.rows[kBalanceAverageGate] != 0 ? 2 : 1;
    redraw(g, d);
}

void reset_balance_plan(const GameState& g, BalancePlanDraft& d) {
    d.adjustments.fill(0);
    d.refresh = 1;
    redraw(g, d);
}

int32_t balance_row_maximum(const BalancePlanDraft& d, int row) {
    if (row < 0 || row >= 0x16 || !kEditable[row]) return -1;
    (void)d;  // the input dialog BalanceFromGame only offers other step sizes for the gate
    return kMaximum[kMaximumClass[row]];
}

BalanceEditStatus edit_balance_plan(const GameState& g, BalancePlanDraft& d, int row, int32_t value) {
    if (row < 0 || row >= 0x16 || !kEditable[row]) return BalanceEditStatus::not_editable;
    if (d.view == BalanceView::so_far) return BalanceEditStatus::view_so_far;
    if (value < 0 || value > balance_row_maximum(d, row)) return BalanceEditStatus::out_of_range;
    if (row < kBalanceItems)
        d.adjustments[size_t(row)] = wrap(int64_t(d.adjustments[size_t(row)]) +
                                          wrap(int64_t(int8_t(d.months_left)) * (int64_t(value) - d.rows[size_t(row)])) / 12);
    d.rows[size_t(row)] = value;
    if (row > 0x11) d.refresh = 2;
    redraw(g, d);
    return BalanceEditStatus::changed;
}

bool close_balance_plan(GameState& g, const BalancePlanDraft& d) {
    const int m = d.manager;
    const bool warning = double(rec32(g, m, 0x314)) + d.total < -4000000.0;  // BalancePlanRuntimeField8
    for (int i = 0; i < kBalanceItems; i++) set_rec32(g, m, 0x2b0 + size_t(i) * 4, d.adjustments[size_t(i)]);
    set_rec32(g, m, 0x308, int32_t(std::lround(d.total)));
    if (g.memory.has(kPlanTicketPrice + uint32_t(m))) g.memory.set8(kPlanTicketPrice + uint32_t(m), uint8_t(d.rows[kBalanceTicketPrice]));
    set_rec32(g, m, 0x2f8, d.rows[kBalanceOtherHomeGames]);
    set_rec32(g, m, 0x2fc, d.rows[kBalanceAverageGate]);
    set_rec32(g, m, 0x310, int32_t(d.view));
    return warning;
}

}  // namespace bmh
