// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/desk_actions.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

#include "formats/le.hpp"
#include "game/matchday.hpp"
#include "game/messages.hpp"
#include "game/month.hpp"
#include "game/transfer_bid.hpp"
#include "game/weekly.hpp"
#include "game/week.hpp"

namespace bmh {

namespace {

int32_t rd32(const std::vector<uint8_t>& v, size_t o) {
    return int32_t(v.at(o) | v.at(o + 1) << 8 | v.at(o + 2) << 16 | uint32_t(v.at(o + 3)) << 24);
}
void wr32(std::vector<uint8_t>& v, size_t o, int32_t x) {
    for (int i = 0; i < 4; i++) v.at(o + size_t(i)) = uint8_t(uint32_t(x) >> (8 * i));
}

// Manager record (ManagerRecords + manager * 0xb0c) offsets used here.
constexpr size_t kRecordSize = 0xb0c;
constexpr size_t kTeam = 0x290;          // club of the manager (low byte used)
constexpr size_t kFocus = 0xc1;          // four training camp sliders 0..15
constexpr size_t kStays = 0x21f;         // bits 0-6 stays, bit 7 "in a camp this week"
constexpr size_t kCampExpenses = 0x890;  // money spent on training camps
constexpr uint32_t kFriendlyTeams = ::bmh::layout::FriendlyTeams;
constexpr uint32_t kFriendlyDays = ::bmh::layout::FriendlyDays;
constexpr uint32_t kFriendlyPlanEntries = 30;
constexpr uint32_t kFriendlyPlanCount = ::bmh::layout::FriendlyPlanCount; // cup entry 30, +0x1a; counts two teams per booking
constexpr uint32_t kCalendar = ::bmh::layout::CompetitionCalendar;
constexpr const char* kOrdinarySellerBoundary =
    "seller is outside the Original ordinary club squads (clubs 0..137 or free agents only)";

// DeskActions_ConfigureTrainingCampData_RuntimeField: four quality grades per resort (1..5). The sliders use grade
// kSliderGrade[u] (DeskActionsRuntimeField), the morale grade 2 (DeskActionsRuntimeField2), the freshness grade
// 3 (DeskActionsRuntimeField3).
constexpr int kSliderGrade[4] = {1, 0, 0, 3};
// DeskActionsRuntimeField4: step by drawn index (0..23); indices 0..7 lower a value, 8.. raise it.
constexpr int8_t kStep[24] = {-3, -2, -2, -2, -1, -1, -1, -1, 0, 0, 1, 1,
                              1,  1,  2,  2,  2,  3,  3,  3,  4, 4, 5, 5};
// DeskActionsRuntimeField5: direction per value (familiarity counts the other way round).
constexpr int8_t kDirection[5] = {1, 1, 1, -1, 1};

// Scaled: a * b * 20 / 100 with unsigned byte arguments.
int scaled(int a, int b) { return (a & 0xff) * (b & 0xff) * 20 / 100; }

FriendlyBookingQuote make_friendly_quote(const GameState& g, int manager, int opponent,
                                         bool manager_home) {
    FriendlyBookingQuote out;
    out.manager = manager;
    out.opponent = opponent;
    out.manager_home = manager_home;
    if (!g.memory.has(kFriendlyPlanCount)) {
        out.status = FriendlyQuoteStatus::invalid_state;
        return out;
    }
    if (manager < 0 || size_t(manager) >= g.manager_teams.size() ||
        (size_t(manager) + 1) * kRecordSize > g.manager_records.size()) {
        out.status = FriendlyQuoteStatus::invalid_manager;
        return out;
    }
    const int own_team = g.manager_teams[size_t(manager)];
    if (own_team < 0 || own_team >= 168 || opponent < 0 || opponent >= 168 || opponent == own_team) {
        out.status = FriendlyQuoteStatus::invalid_opponent;
        return out;
    }
    out.manager_team = own_team;
    out.planner_count = g.memory.u8(kFriendlyPlanCount);
    out.home_team = manager_home ? own_team : opponent;
    out.away_team = manager_home ? opponent : own_team;
    out.payer_manager = g.manager_of(out.home_team);
    out.opponent_manager = g.manager_of(opponent);
    const int first_rating = team_average(g, own_team, 0xff).value;
    const int second_rating = team_average(g, opponent, 0xff).value;
    const uint32_t combined = uint32_t(first_rating + second_rating) * 1000;
    // DeskDate uses integer arithmetic in this order: (sum * (sum / 13000) / 7000) * 1000.
    out.cost = int32_t((uint64_t(combined) * (combined / 13000) / 7000) * 1000);
    out.status = FriendlyQuoteStatus::quoted;
    return out;
}

int national_opponent_manager(const GameState& g, const NationalFriendlyOwner& owner,
                              int opponent) {
    const int ordinary = g.manager_of(opponent);
    if (ordinary >= 0) return ordinary;
    // FindManagerForClub's second lookup: spare manager record 4 represents the
    // Bundestrainer's own association only for the first human national coach.
    if (owner.manager == 0 && !g.manager_teams.empty() && g.manager_teams[0] == kNoTeam &&
        g.memory.has(::bmh::layout::DeskActions_NationalOpponentManager_RuntimeField) && int(g.memory.u16(::bmh::layout::CupDraw_FormatError_RuntimeField3)) == opponent &&
        5 * kRecordSize <= g.manager_records.size()) return 4;
    return -1;
}

NationalFriendlyQuote make_national_friendly_quote(const GameState& g,
    const NationalFriendlyOwner& owner, int opponent, bool manager_home, WatcomRandom& rand) {
    NationalFriendlyQuote out;
    out.owner = owner;
    out.watcom_seed_before = rand.seed();
    out.watcom_calls_before = rand.calls();
    out.terms.manager = owner.manager;
    out.terms.manager_team = owner.runtime_team;
    out.terms.opponent = opponent;
    out.terms.manager_home = manager_home;
    if (!g.memory.has(kFriendlyPlanCount)) {
        out.terms.status = FriendlyQuoteStatus::invalid_state;
        return out;
    }
    if (!owner.request_id || owner.menu_kind != 6 || owner.runtime_team != kNoTeam || owner.manager < 0 ||
        size_t(owner.manager) >= g.manager_teams.size() ||
        (size_t(owner.manager) + 1) * kRecordSize > g.manager_records.size() ||
        g.manager_teams[size_t(owner.manager)] != kNoTeam) {
        out.terms.status = FriendlyQuoteStatus::invalid_manager;
        return out;
    }
    // Page 4 cannot return Germany (0); the engine boundary repeats that gate.
    if (opponent <= kNoTeam || opponent > 0x1a2 || opponent == kNoTeam) {
        out.terms.status = FriendlyQuoteStatus::invalid_opponent;
        return out;
    }

    out.terms.planner_count = g.memory.u8(kFriendlyPlanCount);
    out.terms.home_team = manager_home ? kNoTeam : opponent;
    out.terms.away_team = manager_home ? opponent : kNoTeam;
    out.terms.opponent_manager = national_opponent_manager(g, owner, opponent);
    out.terms.payer_manager = manager_home ? owner.manager : out.terms.opponent_manager;
    const auto own = team_average(g, kNoTeam, 0xff, &rand);
    const auto opposing = team_average(g, opponent, 0xff, &rand);
    out.own_rating = own.value;
    out.opponent_rating = opposing.value;
    const uint32_t combined = uint32_t(out.own_rating + out.opponent_rating) * 1000;
    out.terms.cost = int32_t((uint64_t(combined) * (combined / 13000) / 7000) * 1000);
    out.terms.status = FriendlyQuoteStatus::quoted;
    out.watcom_seed_after = rand.seed();
    out.watcom_calls_after = rand.calls();
    return out;
}

bool same_national_friendly_quote(const NationalFriendlyQuote& supplied,
    const NationalFriendlyQuote& fresh) {
    const auto& a = supplied.terms;
    const auto& b = fresh.terms;
    if (supplied.watcom_calls_after < supplied.watcom_calls_before ||
        fresh.watcom_calls_after < fresh.watcom_calls_before) return false;
    return supplied.owner == fresh.owner && a.status == FriendlyQuoteStatus::quoted &&
        fresh.watcom_seed_before == supplied.watcom_seed_before &&
        fresh.watcom_seed_after == supplied.watcom_seed_after &&
        fresh.watcom_calls_after - fresh.watcom_calls_before ==
            supplied.watcom_calls_after - supplied.watcom_calls_before &&
        supplied.own_rating == fresh.own_rating && supplied.opponent_rating == fresh.opponent_rating &&
        a.manager == b.manager && a.opponent == b.opponent && a.manager_team == b.manager_team &&
        a.manager_home == b.manager_home && a.home_team == b.home_team && a.away_team == b.away_team &&
        a.payer_manager == b.payer_manager && a.opponent_manager == b.opponent_manager &&
        a.cost == b.cost && a.planner_count == b.planner_count;
}

bool same_friendly_quote(const FriendlyBookingQuote& left, const FriendlyBookingQuote& right) {
    return left.status == FriendlyQuoteStatus::quoted && right.status == FriendlyQuoteStatus::quoted &&
           left.manager == right.manager && left.opponent == right.opponent &&
           left.manager_team == right.manager_team && left.manager_home == right.manager_home &&
           left.home_team == right.home_team && left.away_team == right.away_team &&
           left.payer_manager == right.payer_manager && left.opponent_manager == right.opponent_manager &&
           left.cost == right.cost &&
           left.planner_count == right.planner_count;
}

FriendlyDateStatus friendly_date_fields_unchecked(const GameState& g,
                                                   const FriendlyBookingQuote& quote,
                                                   uint16_t day) {
    if (quote.status != FriendlyQuoteStatus::quoted) return FriendlyDateStatus::invalid_quote;
    if (day >= 0x16e || !g.memory.has(::bmh::layout::DaysIntoSeason)) return FriendlyDateStatus::invalid_date;
    // DaysIntoSeason (today); g.memory keeps only the value of the loaded savegame
    if (int(day) <= g.season_day()) return FriendlyDateStatus::current_or_past;
    // A friendly on the next July 1 is never dispatched: TurnDriver emits the
    // season-end desk before preparing that day's competitions (is_season_end).
    // CalendarCell likewise stops its calendar loop at the next July 1. The
    // leap-year June 30 at season_day 365 remains playable; the same index in a
    // regular year is already the next season boundary.
    const uint32_t absolute_day = g.season_start + day;
    if (absolute_day > 0 && month_code(absolute_day) == 7 &&
        month_code(absolute_day - 1) != 7)
        return FriendlyDateStatus::invalid_date;
    const uint32_t calendar_at = kCalendar + uint32_t(day) * 2;
    if (!g.memory.has(calendar_at + 1) || !g.memory.has(kFriendlyDays + 0x3b) ||
        !g.memory.has(kFriendlyTeams + 0x77)) return FriendlyDateStatus::invalid_date;
    const uint16_t calendar_word = g.memory.u16(calendar_at);
    // DayConflict applies this gate to the selected day as well as its neighbors.
    // Calendar class bits alone (0x40/0x100/0x1c0) do not authorize a choice; only the
    // transient UI projection bit 15 can make such a word pass. This API never invents it.
    if (calendar_word != 0 && calendar_word != 0x4001 && (calendar_word & 0x8000) == 0)
        return FriendlyDateStatus::unavailable;
    // The saved count is a byte and Max adds two. Refuse malformed values that
    // would wrap rather than charging the manager and corrupting the planner counter.
    if (quote.planner_count > 253) return FriendlyDateStatus::plan_full;

    // CalendarCell tests the selected day and neighbors with DayConflict. Its temporary
    // calendar eligibility bit 15 is UI-local and absent from the saved GameState, so only
    // unambiguous empty/0x4001 neighbors are accepted here.
    for (int probe = std::max(0, int(day) - 1); probe <= std::min(0x16d, int(day) + 1); ++probe) {
        const uint32_t probe_at = kCalendar + uint32_t(probe) * 2;
        if (!g.memory.has(probe_at + 1)) return FriendlyDateStatus::invalid_date;
        if (probe != day) {
            const uint16_t neighbor_word = g.memory.u16(probe_at);
            if (neighbor_word != 0 && neighbor_word != 0x4001) return FriendlyDateStatus::unavailable;
        }
        for (size_t i = 0; i < kFriendlyPlanEntries; ++i) {
            if (g.memory.u16(kFriendlyDays + uint32_t(i) * 2) != uint16_t(probe)) continue;
            const uint32_t teams_at = kFriendlyTeams + uint32_t(i) * 4;
            const int first = int16_t(g.memory.u16(teams_at));
            const int second = int16_t(g.memory.u16(teams_at + 2));
            if (first == quote.manager_team || second == quote.manager_team ||
                first == quote.opponent || second == quote.opponent ||
                first == kNoTeam || second == kNoTeam) return FriendlyDateStatus::unavailable;
        }
    }
    size_t insertion = 0;
    size_t used_entries = 0;
    for (size_t i = 0; i < kFriendlyPlanEntries; ++i)
        if (g.memory.u16(kFriendlyDays + uint32_t(i) * 2) != 0) ++used_entries;
    while (insertion < kFriendlyPlanEntries) {
        const uint16_t planned_day = g.memory.u16(kFriendlyDays + uint32_t(insertion) * 2);
        if (planned_day == 0 || planned_day >= day) break;
        ++insertion;
    }
    // The original insertion would overwrite the last slot if all 30 were occupied.
    // Refuse that case rather than silently dropping a previously booked fixture.
    if (insertion == kFriendlyPlanEntries || used_entries == kFriendlyPlanEntries)
        return FriendlyDateStatus::plan_full;
    return (calendar_word & 0x01c0) != 0 ? FriendlyDateStatus::available_with_calendar_event
                                         : FriendlyDateStatus::available;
}

FriendlyDateStatus friendly_date_unchecked(const GameState& g, const FriendlyBookingQuote& quote,
                                           uint16_t day) {
    if (!same_friendly_quote(quote, make_friendly_quote(g, quote.manager, quote.opponent,
                                                        quote.manager_home)))
        return FriendlyDateStatus::invalid_quote;
    return friendly_date_fields_unchecked(g, quote, day);
}

}  // namespace

FriendlyBookingQuote quote_friendly_match(const GameState& g, int manager, int opponent,
                                          bool manager_home) {
    return make_friendly_quote(g, manager, opponent, manager_home);
}

NationalFriendlyQuote quote_national_friendly_match(const GameState& g,
    const NationalFriendlyOwner& owner, int opponent, bool manager_home, WatcomRandom& rand) {
    WatcomRandom staged = rand;
    NationalFriendlyQuote result = make_national_friendly_quote(g, owner, opponent,
                                                                 manager_home, staged);
    if (result.terms.status == FriendlyQuoteStatus::quoted) rand = staged;
    return result;
}

NationalFriendlyQuote preview_national_friendly_match(const GameState& g,
    const NationalFriendlyOwner& owner, int opponent, bool manager_home,
    const WatcomRandom& rand) {
    WatcomRandom staged = rand;
    return make_national_friendly_quote(g, owner, opponent, manager_home, staged);
}

FriendlyDateStatus national_friendly_match_date_status(const GameState& g,
    const NationalFriendlyQuote& quote, const NationalFriendlyOwner& owner,
    const WatcomRandom& rand, uint16_t season_day) {
    if (quote.owner != owner || owner.menu_kind != 6 || owner.runtime_team != kNoTeam ||
        owner.manager < 0 || size_t(owner.manager) >= g.manager_teams.size() ||
        g.manager_teams[size_t(owner.manager)] != kNoTeam ||
        rand.seed() != quote.watcom_seed_after || rand.calls() != quote.watcom_calls_after)
        return FriendlyDateStatus::invalid_quote;
    WatcomRandom verify(quote.watcom_seed_before);
    const NationalFriendlyQuote fresh = make_national_friendly_quote(g, owner,
        quote.terms.opponent, quote.terms.manager_home, verify);
    if (!same_national_friendly_quote(quote, fresh)) return FriendlyDateStatus::invalid_quote;
    return friendly_date_fields_unchecked(g, quote.terms, season_day);
}

NationalFriendlyBookingResult book_national_friendly_match(GameState& g,
    const NationalFriendlyBookingRequest& request, const WatcomRandom& watcom,
    GameRandom& game_random) {
    NationalFriendlyBookingResult out;
    const auto& quote = request.quote;
    if (quote.terms.status != FriendlyQuoteStatus::quoted || quote.owner != request.owner ||
        watcom.seed() != quote.watcom_seed_after || watcom.calls() != quote.watcom_calls_after) {
        out.status = quote.terms.status == FriendlyQuoteStatus::invalid_manager
                         ? FriendlyBookingStatus::invalid_manager
                         : quote.terms.status == FriendlyQuoteStatus::invalid_opponent
                               ? FriendlyBookingStatus::invalid_opponent
                               : quote.terms.status == FriendlyQuoteStatus::invalid_state
                                     ? FriendlyBookingStatus::invalid_request
                               : FriendlyBookingStatus::stale_quote;
        return out;
    }
    WatcomRandom verify(quote.watcom_seed_before);
    const NationalFriendlyQuote fresh = make_national_friendly_quote(g, request.owner,
        quote.terms.opponent, quote.terms.manager_home, verify);
    if (!same_national_friendly_quote(quote, fresh)) {
        out.status = fresh.terms.status == FriendlyQuoteStatus::invalid_manager
                         ? FriendlyBookingStatus::invalid_manager
                         : fresh.terms.status == FriendlyQuoteStatus::invalid_opponent
                               ? FriendlyBookingStatus::invalid_opponent
                               : FriendlyBookingStatus::stale_quote;
        return out;
    }
    static_cast<FriendlyBookingQuote&>(out) = quote.terms;

    GameState staged = g;
    const FriendlyDateStatus date_status = friendly_date_fields_unchecked(
        staged, quote.terms, request.season_day);
    if (date_status == FriendlyDateStatus::current_or_past ||
        date_status == FriendlyDateStatus::invalid_date) {
        out.status = FriendlyBookingStatus::invalid_date;
        return out;
    }
    if (date_status == FriendlyDateStatus::plan_full) {
        out.status = FriendlyBookingStatus::plan_full;
        return out;
    }
    if (date_status != FriendlyDateStatus::available &&
        date_status != FriendlyDateStatus::available_with_calendar_event) {
        out.status = date_status == FriendlyDateStatus::invalid_quote
                         ? FriendlyBookingStatus::stale_quote
                         : FriendlyBookingStatus::date_unavailable;
        return out;
    }
    if (out.opponent_manager >= 0 && out.opponent_manager != out.manager) {
        if (!request.accept_opponent) {
            out.status = FriendlyBookingStatus::opponent_declined;
            return out;
        }
        if (request.consenting_manager != out.opponent_manager) {
            out.status = FriendlyBookingStatus::invalid_request;
            return out;
        }
    }
    if (out.payer_manager >= 0) {
        if (!request.accept_cost) {
            out.status = FriendlyBookingStatus::declined;
            return out;
        }
        if (request.approving_manager != out.payer_manager) {
            out.status = FriendlyBookingStatus::invalid_request;
            return out;
        }
        if (out.cost < 0 || !book_money(staged, out.payer_manager, out.cost, false, true)) {
            out.status = FriendlyBookingStatus::insufficient_funds;
            return out;
        }
    }

    size_t insertion = 0;
    while (insertion < kFriendlyPlanEntries) {
        const uint16_t planned_day = staged.memory.u16(kFriendlyDays + uint32_t(insertion) * 2);
        if (planned_day == 0 || planned_day >= request.season_day) break;
        ++insertion;
    }
    if (insertion == kFriendlyPlanEntries) {
        out.status = FriendlyBookingStatus::plan_full;
        return out;
    }
    for (size_t i = kFriendlyPlanEntries - 1; i > insertion; --i) {
        staged.memory.set16(kFriendlyDays + uint32_t(i) * 2,
                            staged.memory.u16(kFriendlyDays + uint32_t(i - 1) * 2));
        staged.memory.set32(kFriendlyTeams + uint32_t(i) * 4,
                            staged.memory.u32(kFriendlyTeams + uint32_t(i - 1) * 4));
    }
    staged.memory.set16(kFriendlyDays + uint32_t(insertion) * 2, request.season_day);
    staged.memory.set16(kFriendlyTeams + uint32_t(insertion) * 4, uint16_t(out.home_team));
    staged.memory.set16(kFriendlyTeams + uint32_t(insertion) * 4 + 2, uint16_t(out.away_team));
    const uint32_t calendar_at = kCalendar + uint32_t(request.season_day) * 2;
    staged.memory.set16(calendar_at, uint16_t(staged.memory.u16(calendar_at) | 0x4001));
    staged.memory.set8(kFriendlyPlanCount,
        uint8_t(staged.memory.u8(kFriendlyPlanCount) + 2));

    GameRandom staged_game = game_random;
    out.national_bonus_awarded = staged_game.range(0x3c, 0x5a) < quote.opponent_rating;
    if (out.national_bonus_awarded) {
        const size_t record_byte = size_t(out.manager) * kRecordSize + 0x210;
        if (record_byte >= staged.manager_records.size()) {
            out.status = FriendlyBookingStatus::invalid_manager;
            out.national_bonus_awarded = false;
            return out;
        }
        staged.manager_records[record_byte] = uint8_t(staged.manager_records[record_byte] + 1);
    }
    out.status = FriendlyBookingStatus::booked;
    g = std::move(staged);
    game_random = staged_game;
    return out;
}

FriendlyDateStatus friendly_match_date_status(const GameState& g,
                                               const FriendlyBookingQuote& quote,
                                               uint16_t season_day) {
    return friendly_date_unchecked(g, quote, season_day);
}

FriendlyBookingResult book_friendly_match(GameState& g, const FriendlyBookingRequest& request) {
    FriendlyBookingResult out;
    const FriendlyBookingQuote fresh = make_friendly_quote(g, request.quote.manager,
                                                            request.quote.opponent,
                                                            request.quote.manager_home);
    if (!same_friendly_quote(request.quote, fresh)) {
        out.status = request.quote.status == FriendlyQuoteStatus::invalid_manager
                         ? FriendlyBookingStatus::invalid_manager
                         : request.quote.status == FriendlyQuoteStatus::invalid_opponent
                               ? FriendlyBookingStatus::invalid_opponent
                               : request.quote.status == FriendlyQuoteStatus::invalid_state
                                     ? FriendlyBookingStatus::invalid_request
                                     : FriendlyBookingStatus::stale_quote;
        return out;
    }
    static_cast<FriendlyBookingQuote&>(out) = fresh;
    const FriendlyDateStatus date_status = friendly_date_unchecked(g, fresh, request.season_day);
    if (date_status == FriendlyDateStatus::current_or_past ||
        date_status == FriendlyDateStatus::invalid_date) {
        out.status = FriendlyBookingStatus::invalid_date;
        return out;
    }
    if (date_status == FriendlyDateStatus::plan_full) {
        out.status = FriendlyBookingStatus::plan_full;
        return out;
    }
    if (date_status != FriendlyDateStatus::available &&
        date_status != FriendlyDateStatus::available_with_calendar_event) {
        out.status = date_status == FriendlyDateStatus::invalid_quote
                         ? FriendlyBookingStatus::stale_quote
                         : FriendlyBookingStatus::date_unavailable;
        return out;
    }

    if (out.opponent_manager >= 0 && out.opponent_manager != out.manager) {
        if (!request.accept_opponent) {
            out.status = FriendlyBookingStatus::opponent_declined;
            return out;
        }
        if (request.consenting_manager != out.opponent_manager) {
            out.status = FriendlyBookingStatus::invalid_request;
            return out;
        }
    }

    const int payer = out.payer_manager;
    if (payer >= 0) {
        if (!request.accept_cost) {
            out.status = FriendlyBookingStatus::declined;
            return out;
        }
        if (request.approving_manager != payer) {
            out.status = FriendlyBookingStatus::invalid_request;
            return out;
        }
        if (out.cost < 0 || !book_money(g, payer, out.cost, false, true)) {
            out.status = FriendlyBookingStatus::insufficient_funds;
            return out;
        }
    }

    size_t insertion = 0;
    while (insertion < kFriendlyPlanEntries) {
        const uint16_t planned_day = g.memory.u16(kFriendlyDays + uint32_t(insertion) * 2);
        if (planned_day == 0 || planned_day >= request.season_day) break;
        ++insertion;
    }
    for (size_t i = kFriendlyPlanEntries - 1; i > insertion; --i) {
        g.memory.set16(kFriendlyDays + uint32_t(i) * 2,
                       g.memory.u16(kFriendlyDays + uint32_t(i - 1) * 2));
        const uint32_t dest = kFriendlyTeams + uint32_t(i) * 4;
        const uint32_t src = kFriendlyTeams + uint32_t(i - 1) * 4;
        g.memory.set32(dest, g.memory.u32(src));
    }
    const uint32_t day_at = kFriendlyDays + uint32_t(insertion) * 2;
    const uint32_t teams_at = kFriendlyTeams + uint32_t(insertion) * 4;
    g.memory.set16(day_at, request.season_day);
    g.memory.set16(teams_at, uint16_t(out.home_team));
    g.memory.set16(teams_at + 2, uint16_t(out.away_team));
    const uint32_t calendar_at = kCalendar + uint32_t(request.season_day) * 2;
    g.memory.set16(calendar_at, uint16_t(g.memory.u16(calendar_at) | 0x4001));
    g.memory.set8(kFriendlyPlanCount, uint8_t(g.memory.u8(kFriendlyPlanCount) + 2));

    out.status = FriendlyBookingStatus::booked;
    return out;
}

void configure_training_camp_data(GameState& g, const LeImage& exe) {
    auto prices = g.training_camp_base_prices;
    auto quality = g.training_camp_quality;
    for (size_t resort = 0; resort < prices.size(); ++resort) {
        prices[resort] = std::bit_cast<int32_t>(exe.u32(::bmh::layout::DeskActions_ConfigureTrainingCampData_RuntimeField2 + uint32_t(resort * 4)));
        if (prices[resort] < 0) throw FormatError("training camp: negative resort base price");
        for (size_t grade = 0; grade < quality[resort].size(); ++grade) {
            const uint8_t value = exe.u8(::bmh::layout::DeskActions_ConfigureTrainingCampData_RuntimeField + uint32_t(resort * 4 + grade));
            if (value < 1 || value > 5) throw FormatError("training camp: resort quality outside 1..5");
            quality[resort][grade] = value;
        }
    }
    g.training_camp_base_prices = prices;
    g.training_camp_quality = quality;
}

int32_t training_camp_price(const GameState& g, int manager, int resort) {
    if (manager < 0 || size_t(manager) >= g.manager_teams.size() || resort < 0 || resort >= kTrainingCampResorts)
        throw FormatError("training camp: invalid manager or resort");
    const int team = int8_t(g.manager_records.at(size_t(manager) * kRecordSize + kTeam));
    return g.training_camp_base_prices[size_t(resort)] + team_average(g, team, 0xff).players * 5000;
}

bool set_training_camp_focus(GameState& g, int manager, const std::array<uint8_t, 4>& focus) {
    if (manager < 0 || size_t(manager) >= g.manager_teams.size() ||
        (size_t(manager) + 1) * kRecordSize > g.manager_records.size() ||
        std::any_of(focus.begin(), focus.end(), [](uint8_t v) { return v > 15; }) ||
        focus[3] != uint8_t(15 - (unsigned(focus[0]) + focus[1] + focus[2]) / 3)) return false;
    std::copy(focus.begin(), focus.end(), g.manager_records.begin() + size_t(manager) * kRecordSize + kFocus);
    return true;
}

TransferActionResult list_manager_player(GameState& g, int manager, int squad_row, WatcomRandom& rand,
                                        MinuteReport& report) {
    TransferActionResult out;
    if (manager < 0 || size_t(manager) >= g.manager_teams.size()) {
        out.status = TransferActionStatus::invalid_manager;
        return out;
    }
    const SquadRange range = squad_range(g.manager_teams[size_t(manager)]);
    if (squad_row < 0 || size_t(squad_row) >= range.count) return out;
    const int slot = int(range.first) + squad_row;
    const int player = g.squad_slots.at(size_t(slot)) & 0xfff;
    if (player == 0xfff) {
        out.status = TransferActionStatus::invalid_player;
        return out;
    }
    if (!list_transfer_player(g, slot, rand, report)) {
        out.status = report.unsupported ? TransferActionStatus::invalid_request : TransferActionStatus::blocked_by_board;
        return out;
    }
    out.status = TransferActionStatus::accepted;
    out.player = player;
    // The transfer-list slot follows the new pool slot. The engine uses 40 entries
    // for the normal list; entries 40-43 are reserved for matchday approaches.
    for (int i = 0; i < 40; ++i)
        if ((g.squad_slots.at(0xb70u + size_t(i)) & 0xfff) == player) { out.list_entry = i; break; }
    return out;
}

namespace {

TransferActionResult submit_transfer_bid_impl(GameState& g, const TransferBidRequest& request,
    WatcomRandom& rand, const HumanBuyerSaleHooks* source_hooks, MinuteReport* source_report) {
    TransferActionResult out;
    if (source_report && source_report->unsupported) {
        out.status = TransferActionStatus::unsupported;
        return out;
    }
    if (request.manager < 0 || size_t(request.manager) >= g.manager_teams.size()) {
        out.status = TransferActionStatus::invalid_manager;
        return out;
    }
    if (request.list_entry < 0 || request.list_entry >= 40 || request.player < 0 || request.fee <= 0 || request.annual_salary <= 0 ||
        request.contract_years < 1 || request.contract_years > 4) return out;
    const uint32_t entry = ::bmh::layout::EditorListUnsigned + uint32_t(request.list_entry) * 0x58;
    const int player = g.squad_slots.at(0xb70u + size_t(request.list_entry)) & 0xfff;
    if (player == 0xfff) {
        out.status = TransferActionStatus::invalid_player;
        return out;
    }
    if (player != request.player) {
        out.status = TransferActionStatus::invalid_request;
        return out;
    }
    const uint16_t seller_word = g.memory.u16(entry + 8);
    if (seller_word != 0xffff && (seller_word & 0x7fff) == uint16_t(g.manager_teams[size_t(request.manager)])) return out;
    const size_t rec = size_t(request.manager) * kRecordSize;
    const int32_t balance = rd32(g.manager_records, rec + 0x314);
    const int32_t credit = rd32(g.manager_records, rec + 0x31c);
    if (int64_t(balance) + credit < request.fee) {
        out.status = TransferActionStatus::blocked_by_board;
        out.player = player;
        return out;
    }
    const int buyer = g.manager_teams[size_t(request.manager)];
    const SquadRange buyer_range = squad_range(buyer);
    if (squad_count(g, buyer, 3) >= int(buyer_range.count) - 1) {
        out.status = TransferActionStatus::no_squad_room;
        out.player = player;
        return out;
    }
    const uint16_t team = uint16_t(buyer);
    for (int b = 0; b < 4; ++b)
        if (g.memory.u16(entry + uint32_t(b) * 2) == team) {
            out.status = TransferActionStatus::pending;
            out.player = player;
            return out;
        }
    int bid = 0;
    while (bid < 4 && g.memory.u16(entry + uint32_t(bid) * 2) != 0xffff) bid++;
    if (bid == 4) {
        out.status = TransferActionStatus::blocked_by_board;
        return out;
    }
    const uint32_t fee_base = (g.memory.u32(entry + 0x2c) / 10000) * 0x30d4;
    const uint32_t salary_base = ((uint32_t((request.contract_years - 1) * 20 + 100) * g.memory.u32(entry + 0x30)) / 10000) * 100;
    if (fee_base == 0 || salary_base == 0) {
        out.status = TransferActionStatus::offer_too_low;
        out.player = player;
        return out;
    }
    const uint32_t fee_pct = uint32_t(uint64_t(uint32_t(request.fee)) * 100 / fee_base);
    const uint32_t salary_pct = uint32_t(uint64_t(uint32_t(request.annual_salary)) * 100 / salary_base);
    const int seller = seller_word == 0xffff ? -1 : int(seller_word & 0x7fff);
    const int seller_manager = seller < 0 ? -1 : g.manager_of(seller);
    // This opt-in adapter starts only after the existing ordinary bid quote
    // qualifies. It ports the exact SettleHumanBuyerSale/Fail settlement and direct
    // caller tail; the earlier SubmitTransferBidSourceHumanBuyer ProjectTransferOfferList/GameMode/W negotiation itself
    // remains the existing approximation and is not claimed as Source parity.
    const bool source_direct_candidate = source_hooks && seller_manager < 0 &&
        fee_pct >= 110 && salary_pct >= g.memory.u8(entry + 0x55);
    if (source_direct_candidate) {
        if (request.list_entry >= 40 || seller < 0 || (seller >= 0x8a && seller != 0x0fff) || seller == buyer ||
            request.manager < 0 || size_t(request.manager) >= g.manager_teams.size() ||
            !g.memory.has(::bmh::layout::ActiveManager) || g.memory.u8(::bmh::layout::ActiveManager) != request.manager ||
            source_hooks->direct_mode != HumanBuyerSaleDirectMode::ordinary_ai_purchase ||
            (g.memory.has(::bmh::layout::DeskActions_EditorFail_RuntimeField2) && g.memory.u8(::bmh::layout::DeskActions_EditorFail_RuntimeField2) != 0) ||
            (source_hooks->allocator_gate != HumanBuyerSaleAllocatorGate::zero &&
             source_hooks->allocator_gate != HumanBuyerSaleAllocatorGate::blocked) ||
            !source_hooks->notice || source_hooks->before_accounts ||
            source_hooks->after_paid_accounts || buyer_range.count != 24) {
            if (source_report) source_report->fail("SubmitTransferBidSourceHumanBuyer: unsupported human-buyer direct sale runtime/context");
            out.status = TransferActionStatus::unsupported;
            out.player = player;
            return out;
        }
    }
    if (uint64_t(uint32_t(request.fee)) * 100 / fee_base < uint32_t(rand.range(0x5a, 100)) ||
        uint64_t(uint32_t(request.annual_salary)) * 100 / salary_base < g.memory.u8(entry + 0x55)) {
        out.status = TransferActionStatus::offer_too_low;
        out.player = player;
        return out;
    }
    const uint32_t checked_fee_pct = uint32_t(uint64_t(uint32_t(request.fee)) * 100 / fee_base);
    const uint32_t checked_salary_pct = uint32_t(uint64_t(uint32_t(request.annual_salary)) * 100 / salary_base);
    g.memory.set16(entry + uint32_t(bid) * 2, team);
    g.memory.set32(entry + 0x1c + uint32_t(bid) * 4, uint32_t(request.fee));
    g.memory.set32(entry + 0x0c + uint32_t(bid) * 4, uint32_t(request.annual_salary));
    g.memory.set8(entry + 0x34 + uint32_t(bid), uint8_t(request.contract_years * 12));
    for (size_t i = 0; i < request.bonuses.size(); ++i)
        g.memory.set8(entry + 0x38 + uint32_t(bid) * 7 + uint32_t(i), uint8_t(std::min<uint32_t>(request.bonuses[i], 100)));
    out.status = TransferActionStatus::pending;
    out.player = player;
    out.list_entry = request.list_entry;
    out.fee = request.fee;
    out.annual_salary = request.annual_salary;
    if (seller_manager < 0 && checked_fee_pct >= 110 && checked_salary_pct >= g.memory.u8(entry + 0x55)) {
        if (source_direct_candidate) {
            HumanBuyerSaleRequest sale;
            sale.list_entry = request.list_entry;
            sale.bid_slot = bid;
            sale.list_address = entry;
            sale.source_pool_slot = 0xb70u + size_t(request.list_entry);
            sale.expected_seller = seller;
            sale.expected_buyer = buyer;
            sale.expected_active_manager = request.manager;
            sale.raw_offer_slot = -1;
            if (!source_report) {
                out.status = TransferActionStatus::unsupported;
                return out;
            }
            HumanBuyerSaleHooks direct_hooks = *source_hooks;
            direct_hooks.before_accounts = [&](const GameState& state, const HumanBuyerSaleRequest& context) {
                // SubmitTransferBidSourceHumanBuyer enters with the active buyer and presents Original DeskActions_SubmitTransferBidImpl_ExecutableField
                // before SettleHumanBuyerSale. This runs only after the shared preflight succeeds.
                direct_hooks.notice(state, {request.manager, context.list_entry, -1,
                    ::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField, message_text(state, ::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField), 0x80});
            };
            const HumanBuyerSaleResult settled = settle_human_buyer_sale(g, sale, direct_hooks, rand, *source_report);
            if (source_report->unsupported || settled.status == HumanBuyerSaleStatus::unsupported) {
                out.status = TransferActionStatus::unsupported;
                return out;
            }
            // SubmitTransferBidSourceHumanBuyer's ordinary-list tail. No blanket bid-row or pool cleanup:
            // ReleasePlayer owns matching-pool removal on an actual move.
            // OpenTransferEditor established DeskActions_EditorFail_RuntimeField2=0 in the unsaved runtime before this
            // ordinary AI-seller branch. Do not synthesize transient EXE globals
            // in Native memory; a present marker was validated above.
            // Actual EXE VA1c447/1c7c5 clears a WORD at CareerSession_BeginFriendlyOpponentTable_ExecutableField2, not a dword.
            // Honor an explicitly represented full word without adding BSS.
            if (g.memory.has(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2) && g.memory.has(::bmh::layout::DeskActions_SubmitTransferBidImpl_RuntimeField))
                g.memory.set16(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2, 0);
            switch (settled.status) {
            case HumanBuyerSaleStatus::moved: out.status = TransferActionStatus::accepted; break;
            case HumanBuyerSaleStatus::funds_refused: out.status = TransferActionStatus::insufficient_funds; break;
            case HumanBuyerSaleStatus::allocator_blocked: out.status = TransferActionStatus::allocator_blocked; break;
            case HumanBuyerSaleStatus::no_room: out.status = TransferActionStatus::no_squad_room; break;
            case HumanBuyerSaleStatus::unsupported: out.status = TransferActionStatus::unsupported; break;
            }
        } else {
            MinuteReport legacy_report;
            if (accept_transfer_bid_immediately(g, request.list_entry, bid, rand, legacy_report))
                out.status = TransferActionStatus::accepted;
            else
                out.status = TransferActionStatus::no_squad_room;
        }
    }
    return out;
}

TransferActionResult submit_transfer_bid_source_impl(GameState& g, const TransferBidRequest& request,
    WatcomRandom& rand, const HumanBuyerSaleHooks& hooks, MinuteReport& report) {
    TransferActionResult out;
    const auto unsupported = [&](const char* reason) {
        report.fail(std::string("SubmitTransferBidSourceHumanBuyer: ") + reason);
        out.status = TransferActionStatus::unsupported;
        out.player = request.player;
        out.list_entry = request.list_entry;
        return out;
    };
    if (report.unsupported) {
        out.status = TransferActionStatus::unsupported;
        return out;
    }
    if (request.manager < 0 || size_t(request.manager) >= g.manager_teams.size() ||
        (size_t(request.manager) + 1) * kRecordSize > g.manager_records.size()) {
        out.status = TransferActionStatus::invalid_manager;
        return out;
    }
    const bool ordinary_entry = request.list_entry >= 0 && request.list_entry < 40 &&
                                !request.poach && !request.loan_return_entry;
    const bool poach_entry = request.list_entry >= 40 && request.list_entry < 44 &&
                             request.poach && !request.loan_return_entry;
    const bool return_entry = request.list_entry >= 40 && request.list_entry < 44 &&
                              request.loan_return_entry && !request.poach;
    if ((!ordinary_entry && !poach_entry && !return_entry) ||
        request.player < 0 || request.player >= 0x0fff ||
        request.fee < 0 || request.annual_salary < 0 || request.contract_years < 1 || request.contract_years > 4 ||
        std::any_of(request.bonuses.begin(), request.bonuses.end(), [](uint32_t value) { return value > 255; })) {
        out.status = TransferActionStatus::invalid_request;
        return out;
    }
    if (return_entry) {
        const uint32_t marker_address = ::bmh::layout::OfferSlots + uint32_t(request.list_entry - 40) * 4;
        const uint32_t expected_marker = uint32_t(request.player) * 0x400u + uint32_t(request.manager);
        if (!g.memory.has(marker_address + 3) || g.memory.u32(marker_address) != expected_marker) {
            out.status = TransferActionStatus::invalid_request;
            return out;
        }
    }

    const int buyer = g.manager_teams[size_t(request.manager)];
    const SquadRange buyer_range = squad_range(buyer);
    const size_t buyer_record = size_t(request.manager) * kRecordSize;
    if (buyer < 0 || buyer >= 90 || buyer_range.count != 24 ||
        (uint16_t(g.manager_records.at(buyer_record + kTeam)) |
         (uint16_t(g.manager_records.at(buyer_record + kTeam + 1)) << 8)) != uint16_t(buyer))
        return unsupported("unsupported ordinary human-buyer record or 24-slot squad");

    const uint32_t list = ::bmh::layout::EditorListUnsigned + uint32_t(request.list_entry) * 0x58;
    const size_t expected_pool_slot = 0xb70u + size_t(request.list_entry);
    if (!g.memory.has(list + 0x57) || request.source_slot < 0 ||
        size_t(request.source_slot) != expected_pool_slot || size_t(request.source_slot) >= g.squad_slots.size())
        return unsupported("missing or mismatched explicit ordinary Source pool slot");
    const int player = int(g.squad_slots[size_t(request.source_slot)] & 0x0fff);
    if (player == 0x0fff || player != request.player ||
        int(g.squad_slots[0xb70u + size_t(request.list_entry)] & 0x0fff) != player)
        return unsupported("ordinary Source list/player identity mismatch");

    if (!g.memory.has(::bmh::layout::ActiveManager) || g.memory.u8(::bmh::layout::ActiveManager) != request.manager ||
        !g.memory.has(::bmh::layout::GameMode) || (g.memory.has(::bmh::layout::DeskActions_EditorFail_RuntimeField2) && g.memory.u8(::bmh::layout::DeskActions_EditorFail_RuntimeField2) != 0) ||
        hooks.direct_mode != HumanBuyerSaleDirectMode::ordinary_ai_purchase || !hooks.notice)
        return unsupported("missing ordinary-entry, active-manager, mode, or notice ownership");
    const uint8_t career_mode = g.memory.u8(::bmh::layout::GameMode);
    if (career_mode < 1 || career_mode > 3)
        return unsupported("unsupported saved career negotiation mode");

    const uint16_t seller_word = g.memory.u16(list + 8);
    const int seller = int(seller_word & 0x7fff);
    if (seller_word == 0xffff || seller == buyer)
        return unsupported("invalid ordinary seller identity");
    // Seller words name runtime squad owners (SlotTeam), not club-name
    // offsets. The four foreign leagues are already runtime clubs90..137;
    // season initialization can reorder SSC Neapel within that range.
    // SquadRange4: clubs0..89 have24 slots, clubs90..137 have16. The
    // ordinary settlement already removes players using that actual range.
    // Higher club IDs have no ordinary squad; do not invent their settlement
    // or let ReleasePlayer reach its missing-player/internal-error21 boundary.
    if (seller != 0x0fff && seller >= 138) {
        report.fail(kOrdinarySellerBoundary);
        out.status = TransferActionStatus::unsupported;
        out.player = player; out.list_entry = request.list_entry;
        return out;
    }
    const int seller_manager = seller == 0x0fff ? -1 : g.manager_of(seller);
    if (seller_manager >= 4 || seller_manager == request.manager)
        return unsupported("unsupported seller manager identity");
    if (request.loan && seller == 0x0fff)
        return unsupported("Fail/DayStepRest2: free-agent loan return destination is not represented");

    const auto source_notice = [&](uint32_t address, std::string message, uint16_t flags) {
        hooks.notice(g, {request.manager, request.list_entry, -1, address, std::move(message), flags});
    };
    if (request.fee == 0 || request.annual_salary == 0) {
        source_notice(::bmh::layout::DeskActions_SubmitTransferOfferEditorSource_TextPointer, message_text(g, ::bmh::layout::DeskActions_SubmitTransferOfferEditorSource_TextPointer), 0x40);
        out.status = TransferActionStatus::rejected;
        out.player = player; out.list_entry = request.list_entry;
        return out;
    }
    const uint32_t team = uint32_t(buyer);
    int bid_slot = -1;
    for (int slot = 0; slot < 4; ++slot) {
        const uint16_t bidder = g.memory.u16(list + uint32_t(slot) * 2);
        if (bidder == uint16_t(team)) {
            if (bid_slot >= 0) return unsupported("duplicate own buyer bid words");
            bid_slot = slot;
        }
    }

    const auto apply_editor_values = [&](int slot, uint32_t fee) {
        const uint32_t b = uint32_t(slot);
        g.memory.set32(list + 0x1c + b * 4, fee);
        g.memory.set32(list + 0x0c + b * 4, uint32_t(request.annual_salary));
        g.memory.set8(list + 0x34 + b, uint8_t(request.contract_years * 12) |
            uint8_t(request.loan ? 0x80 : 0));
        for (size_t n = 0; n < request.bonuses.size(); ++n)
            g.memory.set8(list + 0x38 + b * 7 + uint32_t(n), uint8_t(request.bonuses[n]));
    };
    const uint32_t available = uint32_t(rd32(g.manager_records, buyer_record + 0x314)) +
                               uint32_t(rd32(g.manager_records, buyer_record + 0x31c));

    // SubmitTransferBidSourceHumanBuyer is reached only for a new buyer word. An existing own offer is
    // an editor-only update: preserve its row, apply the changed fields, and do
    // not consume any qualification W values a second time.
    if (bid_slot >= 0) {
        const uint8_t old_term = g.memory.u8(list + 0x34 + uint32_t(bid_slot));
        if ((old_term & 0x80) != uint8_t(request.loan ? 0x80 : 0))
            return unsupported("OpenTransferEditor: changing the loan class of an existing offer is unsupported");
        const uint32_t fee = uint32_t(request.fee) <= available ? uint32_t(request.fee) : 0u;
        apply_editor_values(bid_slot, fee);
        if (fee == 0) {
            source_notice(::bmh::layout::DeskActions_SubmitTransferOfferEditorSource_RuntimeField, message_text(g, ::bmh::layout::DeskActions_SubmitTransferOfferEditorSource_RuntimeField), 0x40);
            out.status = TransferActionStatus::insufficient_funds;
        } else {
            out.status = TransferActionStatus::pending;
        }
        out.player = player; out.list_entry = request.list_entry;
        out.fee = int32_t(fee); out.annual_salary = request.annual_salary;
        return out;
    }

    for (int slot = 0; slot < 4; ++slot) {
        if (g.memory.u16(list + uint32_t(slot) * 2) == 0xffff) { bid_slot = slot; break; }
    }
    if (bid_slot < 0) {
        out.status = TransferActionStatus::blocked_by_board;
        out.player = player; out.list_entry = request.list_entry;
        return out;
    }

    // OpenTransferEditor checks funds before entering SubmitTransferBidSourceHumanBuyer. Keep that parent gate
    // ahead of the inner capacity check and qualification RNG.
    if (uint32_t(request.fee) > available) {
        source_notice(::bmh::layout::DeskActions_SubmitTransferOfferEditorSource_RuntimeField, message_text(g, ::bmh::layout::DeskActions_SubmitTransferOfferEditorSource_RuntimeField), 0x40);
        out.status = TransferActionStatus::insufficient_funds;
        out.player = player; out.list_entry = request.list_entry;
        return out;
    }

    // SubmitTransferBidSourceHumanBuyer uses equality here. Weekly decisions can legitimately move this
    // count past23 while outgoing loans still reserve places (M139/#114).
    // Reject the higher counts too: accepting more players would consume the
    // returning player's slot and stop the next season. Keep Source's notice,
    // signed manager byte and refusal before bid writes or qualification RNG.
    if (!g.memory.has(::bmh::layout::DeskActions_Unsupported_RuntimeFieldUnsigned + uint32_t(request.manager)))
        return unsupported("missing bbcc8 buyer-manager capacity byte");
    const int capacity = int(int8_t(g.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeFieldUnsigned + uint32_t(request.manager)))) +
                         int(uint8_t(squad_count(g, buyer, 3)));
    if (capacity >= 0x17) {
        source_notice(::bmh::layout::DeskActions_Unsupported_ExecutableField, message_text(g, ::bmh::layout::DeskActions_Unsupported_ExecutableField), 0x40);
        out.status = TransferActionStatus::no_squad_room;
        out.player = player; out.list_entry = request.list_entry;
        return out;
    }

    apply_editor_values(bid_slot, uint32_t(request.fee));
    g.memory.set16(list + uint32_t(bid_slot) * 2, uint16_t(team));
    const TransferBidRatingRequest rating_request{request.source_slot, request.list_entry, bid_slot,
                                                   buyer, player};
    const TransferBidRating rating = rate_transfer_bid_source(g, rating_request, rand, report);
    if (!rating.supported || report.unsupported) {
        out.status = TransferActionStatus::unsupported;
        out.player = player; out.list_entry = request.list_entry;
        return out;
    }

    // Poaching (rows 40..43) is decided at once: a refusal is final (rejected), the player
    // is easier to convince in mode III (W90..96 instead of W120..190), a human seller is
    // asked (PresentManagerDialog: yes 0xa0, no 0) and the club refuses less often (W92..98).
    if (request.poach && !hooks.confirm) return unsupported("missing poaching PresentManagerDialog question owner");
    uint32_t salary_threshold = 0;
    switch (career_mode) {
    case 1: salary_threshold = 100; break;
    case 2: salary_threshold = uint32_t(rand.range(90, 110)); break;
    case 3: salary_threshold = uint32_t(request.poach ? rand.range(90, 96) : rand.range(120, 190)); break;
    default: return unsupported("unsupported saved career negotiation mode");
    }
    if (rating.salary_pct < salary_threshold) {
        source_notice(::bmh::layout::DeskActions_Unsupported_ExecutableField3, g.player_name(player) + message_text(g, ::bmh::layout::DeskActions_Unsupported_ExecutableField3), 0x80);
        out.status = request.poach ? TransferActionStatus::rejected : TransferActionStatus::pending;
        out.player = player; out.list_entry = request.list_entry;
        out.fee = request.fee; out.annual_salary = request.annual_salary;
        return out;
    }

    uint32_t fee_pct = career_mode == 3 ? rating.fee_pct : 200u;
    if (request.poach && seller_manager >= 0) {
        // "Sind Sie mit dem Angebot von <fee> einverstanden?" to the selling manager
        const std::string question = message_text(g, ::bmh::layout::DeskActions_Unsupported_TextPointer) +
            format_amount(g, std::bit_cast<int32_t>(g.memory.u32(list + 0x1c + uint32_t(bid_slot) * 4))) +
            message_text(g, ::bmh::layout::DeskActions_Unsupported_TextPointer2);
        fee_pct = hooks.confirm(g, seller_manager, question) ? 0xa0u : 0u;
    }
    bool dfb_fee = false;
    if (uint32_t(rand.range(92, request.poach ? 98 : 150)) >= fee_pct) {
        if (request.poach) {
            // The club refuses; as the player agreed, the DFB sets the fee to 188 % of his
            // market value (AddToSquad with 0xbc) and the buyer may pay it (PresentManagerDialog).
            uint32_t fee = 0, salary = 0;
            const bool loan = (g.memory.u8(list + 0x34 + uint32_t(bid_slot)) & 0x80) != 0;
            market_value(g, request.source_slot, loan ? 1 : 0, 0xbc, fee, salary, 1, 0, rand);
            g.memory.set32(list + 0x1c + uint32_t(bid_slot) * 4, fee);
            const std::string question = message_text(g, ::bmh::layout::DeskActions_Unsupported_TextPointer4) + format_amount(g, std::bit_cast<int32_t>(fee)) +
                                         message_text(g, ::bmh::layout::DeskActions_Unsupported_TextPointer5);
            out.player = player; out.list_entry = request.list_entry;
            out.fee = std::bit_cast<int32_t>(fee); out.annual_salary = request.annual_salary;
            if (!hooks.confirm(g, request.manager, question)) {
                out.status = TransferActionStatus::rejected;
                return out;
            }
            dfb_fee = true;
        } else {
            if (seller_word == 0x0fff) {
                source_notice(::bmh::layout::DeskActions_Unsupported_ExecutableField2, message_text(g, ::bmh::layout::DeskActions_Unsupported_ExecutableField2), 0x80);
            } else {
                source_notice(::bmh::layout::DeskActions_Unsupported_TextPointer3, g.club_name(seller) + message_text(g, ::bmh::layout::DeskActions_Unsupported_TextPointer3), 0x80);
            }
            out.status = TransferActionStatus::pending;
            out.player = player; out.list_entry = request.list_entry;
            out.fee = request.fee; out.annual_salary = request.annual_salary;
            return out;
        }
    }

    out.player = player; out.list_entry = request.list_entry;
    out.fee = std::bit_cast<int32_t>(g.memory.u32(list + 0x1c + uint32_t(bid_slot) * 4));
    out.annual_salary = request.annual_salary;
    if ((hooks.allocator_gate != HumanBuyerSaleAllocatorGate::zero &&
         hooks.allocator_gate != HumanBuyerSaleAllocatorGate::blocked) ||
        hooks.before_accounts || hooks.after_paid_accounts) {
        return unsupported("unknown direct allocator gate or caller-owned settlement hook");
    }
    HumanBuyerSaleRequest sale;
    sale.list_entry = request.list_entry;
    sale.bid_slot = bid_slot;
    sale.list_address = list;
    sale.source_pool_slot = size_t(request.source_slot);
    sale.expected_seller = seller;
    sale.expected_buyer = buyer;
    sale.expected_active_manager = request.manager;
    sale.raw_offer_slot = -1;
    sale.poach = request.poach;
    sale.loan_return_entry = request.loan_return_entry;
    HumanBuyerSaleHooks direct_hooks = hooks;
    // "Sie einigen sich mit dem Spieler..." (DeskActions_SubmitTransferBidImpl_ExecutableField); a poached player: "Ihr erstes Angebot wird
    // angenommen!" (DeskActions_SubmitTransferOfferEditorSource_ExecutableField); after the DFB fee the original goes to the accounts directly
    const uint32_t accepted_text = request.poach ? ::bmh::layout::DeskActions_SubmitTransferOfferEditorSource_ExecutableField : ::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField;
    if (!dfb_fee)
        direct_hooks.before_accounts = [&](const GameState& state, const HumanBuyerSaleRequest& context) {
            direct_hooks.notice(state, {request.manager, context.list_entry, -1,
                accepted_text, message_text(state, accepted_text), 0x80});
        };
    const HumanBuyerSaleResult settled = settle_human_buyer_sale(g, sale, direct_hooks, rand, report);
    if (report.unsupported || settled.status == HumanBuyerSaleStatus::unsupported) {
        out.status = TransferActionStatus::unsupported;
        return out;
    }

    // SubmitTransferBidSourceHumanBuyer's normal direct tail: matching-pool removal is performed only
    // by ReleasePlayer on a move; clear only already represented transient storage.
    if (g.memory.has(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2) && g.memory.has(::bmh::layout::DeskActions_SubmitTransferBidImpl_RuntimeField)) g.memory.set16(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2, 0);
    if (g.memory.has(::bmh::layout::DeskActions_EditorFail_RuntimeField2)) g.memory.set8(::bmh::layout::DeskActions_EditorFail_RuntimeField2, 0);
    switch (settled.status) {
    case HumanBuyerSaleStatus::moved: out.status = TransferActionStatus::accepted; break;
    case HumanBuyerSaleStatus::funds_refused: out.status = TransferActionStatus::insufficient_funds; break;
    case HumanBuyerSaleStatus::allocator_blocked: out.status = TransferActionStatus::allocator_blocked; break;
    case HumanBuyerSaleStatus::no_room: out.status = TransferActionStatus::no_squad_room; break;
    case HumanBuyerSaleStatus::unsupported: out.status = TransferActionStatus::unsupported; break;
    }
    return out;
}

} // namespace

std::optional<TransferMarketSnapshotPreview> transfer_market_snapshot_preview(
    const GameState& g,int manager,int runtime_club,int index) {
    if(manager<0||manager>=4||size_t(manager)>=g.manager_teams.size()||
       g.manager_teams[size_t(manager)]!=runtime_club||runtime_club<0||runtime_club>=138||index<0||index>=12)
        return std::nullopt;
    const auto owner=squad_range(runtime_club);
    int count=0,ordinal=-1;
    for(int i=0;i<12;++i) {
        const uint32_t id=::bmh::layout::Career_CareerManagersEnd_RuntimeField+uint32_t(i)*2,who=::bmh::layout::ClubInfo_FormatError_RuntimeField2+uint32_t(i);
        if(!g.memory.has(id)||!g.memory.has(id+1)||!g.memory.has(who))return std::nullopt;
        if(g.memory.u16(id)!=0x0fff&&g.memory.u8(who)==manager) {
            if(i==index)ordinal=count;
            ++count;
        }
    }
    if(ordinal<0||size_t(count)>owner.count)return std::nullopt;
    const int player=g.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField+uint32_t(index)*2);
    if(player<0||size_t(player)>=g.players.size()||player==0x0fff)return std::nullopt;
    for(int i=0;i<12;++i)if(i!=index&&g.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField+uint32_t(i)*2)==player)return std::nullopt;
    const uint32_t board=::bmh::layout::ClubInfo_FormatError_RuntimeField+uint32_t(index)*0x58;
    for(uint32_t n=0;n<0x58;++n)if(!g.memory.has(board+n))return std::nullopt;
    int borrower=-1;
    for(int i=0;i<200;++i) {
        const uint32_t at=::bmh::layout::Career_RemoveJoinLoans_RuntimeField+uint32_t(i)*4;
        if(!g.memory.has(at)||!g.memory.has(at+3))return std::nullopt;
        const uint32_t loan=g.memory.u32(at);
        if(!loan||int(loan&0xfff)!=player)continue;
        // Require exactly one genuine ledger and its authenticated owner.
        if(borrower!=-1||int(loan>>12&0x3ff)!=runtime_club)return std::nullopt;
        borrower=int(loan>>22&0x3ff);
        if(borrower<0||borrower>=138||borrower==runtime_club)return std::nullopt;
    }
    if(borrower<0)return std::nullopt;
    const auto range=squad_range(borrower);
    if(!range.count||range.first+range.count>g.squad_slots.size())return std::nullopt;
    int source=-1;
    for(size_t row=0;row<range.count;++row)if(int(g.squad_slots[range.first+row]&0xfff)==player) {
        if(source!=-1)return std::nullopt;
        source=int(range.first+row);
    }
    if(source<0)return std::nullopt;
    return TransferMarketSnapshotPreview{index,int(owner.count)-count+ordinal,source,player};
}

TransferMarketPreviewResult quote_transfer_market_preview_source(
    const GameState& g,const TransferMarketPreviewRequest& request,WatcomRandom& rand) {
    TransferMarketPreviewResult result;
    const auto unsupported=[&](const char* reason){result.reason=reason;return result;};
    if(request.manager<0||size_t(request.manager)>=g.manager_teams.size()||request.manager>=4||
       !g.memory.has(::bmh::layout::ActiveManager)||g.memory.u8(::bmh::layout::ActiveManager)!=request.manager)
        return unsupported("market preview: stale active manager");
    const size_t record=size_t(request.manager)*kRecordSize;
    if(record+0x318>g.manager_records.size()||request.runtime_club<0||
       g.manager_teams[size_t(request.manager)]!=request.runtime_club)
        return unsupported("market preview: stale runtime club/record");
    const uint16_t native_team=uint16_t(g.manager_records[record+0x290])|
        uint16_t(uint16_t(g.manager_records[record+0x291])<<8);
    if(native_team!=request.runtime_club)
        return unsupported("market preview: Native record disagrees with runtime club");
    if(request.source_slot<0||size_t(request.source_slot)>=g.squad_slots.size()||
       request.expected_player<0||request.expected_player==0x0fff||
       size_t(request.expected_player)>=g.players.size()||
       (g.squad_slots[size_t(request.source_slot)]&0x0fff)!=request.expected_player)
        return unsupported("market preview: stale Source player/slot");
    if(request.purchase) {
        if(request.snapshot_index!=-1)return unsupported("market preview: snapshot is not a purchase row");
        if(request.source_slot<2928||request.source_slot>=2928+40)
            return unsupported("market preview: purchase is not an ordinary market row");
        const uint32_t seller_at=::bmh::layout::EditorList+uint32_t(request.source_slot-2928)*0x58+8;
        if(!g.memory.has(seller_at)||!g.memory.has(seller_at+1))
            return unsupported("market preview: missing Source seller word");
        const uint16_t raw=g.memory.u16(seller_at);
        // Existing UI's -1 identity represents only an observed FULLWORD FFFF.
        // Canonical0FFF remains4095; a high-bit seller flag is masked by18c22.
        const int seller=raw==0xffff?-1:int(raw&0x7fff);
        if(request.seller!=seller)return unsupported("market preview: stale Source seller");
    } else if(request.snapshot_index>=0) {
        const auto identity=transfer_market_snapshot_preview(g,request.manager,request.runtime_club,request.snapshot_index);
        if(!identity||identity->source_slot!=request.source_slot||identity->player!=request.expected_player||request.seller!=request.runtime_club)
            return unsupported("market preview: stale lender snapshot/borrower identity");
        // Intentional correction: Original19c1c/TransferMarketScreen uses the displayed tail
        // row, which can be empty0FFF. Value the authenticated borrower slot
        // with the unchanged42307 formula; do not synthesize a Native slot.
    } else {
        if(request.snapshot_index!=-1)return unsupported("market preview: invalid snapshot index");
        if(request.seller!=request.runtime_club||request.runtime_club>=138)
            return unsupported("market preview: outgoing club is not an ordinary owned squad");
        const auto range=squad_range(request.runtime_club);
        if(range.count==0||range.first+range.count>g.squad_slots.size()||
           !g.memory.has(::bmh::layout::Career_CareerManagersEnd_RuntimeField+23)||!g.memory.has(::bmh::layout::ClubInfo_FormatError_RuntimeField2+11))
            return unsupported("market preview: incomplete outgoing/loan roster");
        size_t borrowed=0;
        for(uint32_t n=0;n<12;++n) {
            if(!g.memory.has(::bmh::layout::Career_CareerManagersEnd_RuntimeField+n*2)||!g.memory.has(::bmh::layout::Career_CareerManagersEnd_RuntimeField+n*2+1)||!g.memory.has(::bmh::layout::ClubInfo_FormatError_RuntimeField2+n))
                return unsupported("market preview: incomplete outgoing loan ownership");
            if(g.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField+n*2)!=0x0fff&&g.memory.u8(::bmh::layout::ClubInfo_FormatError_RuntimeField2+n)==request.manager)++borrowed;
        }
        if(borrowed>range.count||size_t(request.source_slot)<range.first||
           size_t(request.source_slot)>=range.first+range.count-borrowed)
            return unsupported("market preview: outgoing Source slot is not an ordinary roster row");
    }
    // EXE raw23922 (MarketValue): NEG32; 2393a IDIV ecx(10000),
    // 2393c IMUL32, 23959 ADD32. Salary is computed before42307's loan quarter.
    market_value(g,request.source_slot,int(request.loan),100,result.fee,result.salary,1,0,rand);
    const uint32_t sign=request.purchase?0u-result.fee:result.fee;
    const int32_t quotient=std::bit_cast<int32_t>(sign)/10000;
    const uint32_t rounded=uint32_t(quotient)*10000u;
    result.delta=std::bit_cast<int32_t>(rounded);
    result.projected_balance=std::bit_cast<int32_t>(std::bit_cast<uint32_t>(rd32(g.manager_records,record+0x314))+rounded);
    result.supported=true;
    return result;
}

namespace {
constexpr uint32_t kEditorList=::bmh::layout::EditorList;
uint32_t editor_list(const TransferOfferEditorRequest& r){return kEditorList+uint32_t(r.list_entry)*0x58;}
int editor_slot(const GameState& g,const TransferOfferEditorRequest& r) {
    const uint32_t at=editor_list(r);
    for(int slot=0;slot<4;++slot) {
        const uint16_t buyer=g.memory.u16(at+uint32_t(slot)*2);
        if(buyer==0xffff||buyer==r.runtime_club)return slot;
    }
    return -1;
}
std::string editor_identity(const GameState& g,const TransferOfferEditorRequest& r) {
    if(r.manager<0||r.manager>=4||size_t(r.manager)>=g.manager_teams.size()||
       !g.memory.has(::bmh::layout::ActiveManager)||g.memory.u8(::bmh::layout::ActiveManager)!=r.manager||
       g.manager_teams[size_t(r.manager)]!=r.runtime_club||r.runtime_club<0||r.runtime_club>=138)
        return "OpenTransferEditor: stale ordinary manager/team";
    const size_t record=size_t(r.manager)*kRecordSize;
    if(record+kRecordSize>g.manager_records.size()||
       (uint16_t(g.manager_records[record+0x290])|uint16_t(uint16_t(g.manager_records[record+0x291])<<8))!=r.runtime_club)
        return "OpenTransferEditor: inconsistent Native buyer record";
    // ordinary rows 0..39; poaching uses the special rows 40..43 (and only those)
    const bool ordinary_entry = r.list_entry >= 0 && r.list_entry < 40 && !r.poach && !r.loan_return_entry;
    const bool poach_entry = r.list_entry >= 40 && r.list_entry < 44 && r.poach && !r.loan_return_entry;
    const bool return_entry = r.list_entry >= 40 && r.list_entry < 44 && r.loan_return_entry && !r.poach;
    if((!ordinary_entry&&!poach_entry&&!return_entry)||r.source_slot!=2928+r.list_entry||
       size_t(r.source_slot)>=g.squad_slots.size()||r.expected_player<0||r.expected_player>=0x0fff||
       size_t(r.expected_player)>=g.players.size()||
       int(g.squad_slots[size_t(r.source_slot)]&0x0fff)!=r.expected_player)
        return "OpenTransferEditor: stale ordinary Source slot/player";
    if(return_entry) {
        const uint32_t marker=::bmh::layout::OfferSlots+uint32_t(r.list_entry-40)*4;
        if(!g.memory.has(marker+3)||g.memory.u32(marker)!=uint32_t(r.expected_player)*0x400u+uint32_t(r.manager))
            return "OpenTransferEditor: stale DayStepRest2 return-offer marker";
    }
    const uint32_t list=editor_list(r);
    for(uint32_t n=0;n<0x58;++n)if(!g.memory.has(list+n))return "OpenTransferEditor: missing Native list row";
    if(g.memory.u16(list+8)!=r.expected_seller_word)return "OpenTransferEditor: stale raw seller word";
    const int seller=int(r.expected_seller_word&0x7fff);
    if(r.expected_seller_word==0xffff||seller==r.runtime_club)
        return "OpenTransferEditor: targeted/invalid seller is outside ordinary editor";
    if(seller!=0x0fff&&seller>=138)return kOrdinarySellerBoundary;
    // OpenTransferEditor: the original refuses a loan from a free agent (seller 0x0fff) with the message
    // DeskActionsRuntimeField6 (SDLFlushEvents3) - a rule of the game, not a gap: the code names it
    if(r.loan&&r.expected_seller_word==0x0fff)return "free_agent_loan_refused";
    const int seller_manager=seller==0x0fff?-1:g.manager_of(seller);
    if(seller_manager>=0) {
        const auto range=squad_range(seller);bool found=false;
        if(range.first+range.count>g.squad_slots.size())return "OpenTransferEditor: incomplete human seller squad";
        for(size_t n=range.first;n<range.first+range.count;++n)
            if(int(g.squad_slots[n]&0x0fff)==r.expected_player){found=true;break;}
        if(!found)return "OpenTransferEditor: human seller player missing (Sourceerror10)";
    }
    if(!g.memory.has(::bmh::layout::GameMode)||g.memory.u8(::bmh::layout::GameMode)<1||g.memory.u8(::bmh::layout::GameMode)>3)
        return "OpenTransferEditor: missing supported Native negotiation mode";
    return {};
}
TransferOfferEditorState editor_fail(TransferOfferEditorState state,const std::string& reason) {
    state.supported=false;state.reason=reason;return state;
}
TransferOfferInputProfile editor_profile(uint32_t minimum,uint32_t encoded_max,uint32_t first,
        uint32_t multiplier,uint32_t increment,uint32_t initial,uint32_t text) {
    TransferOfferInputProfile p;p.minimum=minimum;p.currency=(encoded_max&0x40000000u)!=0;
    uint32_t maximum=encoded_max^(p.currency?0x40000000u:0u);
    const auto signed32=[](uint32_t n){return std::bit_cast<int32_t>(n);};
    if(signed32(maximum)<signed32(initial))initial=maximum;
    if(first==0xffffffffu) {
        if(signed32(initial)<=0||multiplier==0)return p; // Sourceauto loop would not terminate.
        const uint32_t rounded=uint32_t(signed32(initial)/int32_t(multiplier))*multiplier;
        uint32_t step=multiplier;
        for(;;) {
            if(step==0)return p;
            increment=step*uint32_t((signed32(initial)/int32_t(step))/10);
            if(increment!=0)break;
            step/=10;
        }
        first=rounded-increment*3u;multiplier=1;
    }
    if(signed32(initial)>99999999)initial=99999999;
    if(signed32(maximum)>99999999)maximum=99999999;
    if(signed32(maximum)<signed32(minimum))return p;
    p.maximum=maximum;p.initial=initial;p.source_text_address=text;
    for(size_t n=0;n<8;++n) {
        if(signed32(first)<signed32(minimum))first=minimum;
        if(n==6)first=minimum;
        if(n==7)first=maximum;
        if(signed32(first)<=signed32(maximum))p.presets[n]=first;
        first=first*multiplier+increment;
    }
    p.supported=true;return p;
}
void editor_project(const GameState& g,TransferOfferEditorState& state) {
    const uint32_t list=editor_list(state.request);
    state.existing=state.bid_slot>=0&&g.memory.u16(list+uint32_t(state.bid_slot)*2)==state.request.runtime_club;
    state.editable=state.bid_slot>=0;
    if(state.editable) {
        const uint32_t bid=uint32_t(state.bid_slot);
        state.fee=g.memory.u32(list+0x1c+bid*4);state.salary=g.memory.u32(list+0x0c+bid*4);
        state.term=g.memory.u8(list+0x34+bid);state.years=uint8_t((int(state.term&127)-1)/12+1);
        for(uint32_t n=0;n<7;++n)state.bonuses[n]=g.memory.u8(list+0x38+bid*7+n);
    } else {state.years=1;state.term=0;state.fee=0;state.salary=0;state.bonuses.fill(0);}
    // RateTransferBidSource: explicit byte years and Source32 unsigned products.
    const uint8_t year_index=state.editable?uint8_t((int(state.term&127)-1)/12):uint8_t(1);
    state.asking_fee=(g.memory.u32(list+0x2c)/10000u)*12500u;
    if(state.editable&&(state.term&128))state.asking_fee>>=2;
    state.asking_salary=((uint32_t(year_index)*20u+100u)*g.memory.u32(list+0x30)/10000u)*100u;
    state.money_profiles[0]=editor_profile(0,state.asking_fee*2u|0x40000000u,0xffffffffu,10000,0,state.asking_fee,::bmh::layout::DeskActions_EditorProject_RuntimeField);
    state.money_profiles[1]=editor_profile(0,0x40030d40u,0xffffffffu,500,0,state.asking_salary,::bmh::layout::DeskActions_EditorProject_ExecutableField2);
    state.mode3_profiles[0]=editor_profile(1,state.request.loan?2u:4u,1,1,1,state.years,::bmh::layout::DeskActions_EditorProject_ExecutableField);
    for(size_t n=0;n<7;++n) {
        const uint32_t first=n<3?1u:50u-uint32_t(n-3)*10u;
        state.mode3_profiles[n+1]=editor_profile(0,n<3?20u:1000u,first,1,first,state.bonuses[n],::bmh::layout::Contracts_Out_ExecutableField4+uint32_t(n)*4);
    }
    for(uint32_t n=0;n<0x58;++n)state.native_entry[n]=g.memory.u8(list+n);
}
std::string editor_current(const GameState& g,const TransferOfferEditorState& state) {
    if(!state.supported||state.closed)return "OpenTransferEditor: editor is unsupported or closed";
    if(const auto reason=editor_identity(g,state.request);!reason.empty())return reason;
    if(g.memory.u8(::bmh::layout::GameMode)!=state.mode)return "OpenTransferEditor: Native mode changed during editor";
    const uint32_t list=editor_list(state.request);
    for(uint32_t n=0;n<0x58;++n)if(g.memory.u8(list+n)!=state.native_entry[n])return "OpenTransferEditor: stale Native editor entry";
    return {};
}
void editor_initialize_empty_bid(GameState& g,TransferOfferEditorState& state) {
    if(state.initialized_empty_bid||state.bid_slot<0)return;
    const uint32_t list=editor_list(state.request),bid=uint32_t(state.bid_slot);
    if(g.memory.u16(list+bid*2)!=0xffff)return;
    // BacLocalFirstEmptySelectionOnlyBuyer..1bac7 / local1c: the first empty selection only. Buyer and
    // all seven stale bonus bytes remain unchanged; loan is the original action.
    g.memory.set32(list+0x0c+bid*4,0);g.memory.set32(list+0x1c+bid*4,0);
    g.memory.set8(list+0x34+bid,uint8_t(24|(state.request.loan?128:0)));
    state.initialized_empty_bid=true;
}
void editor_quote(const GameState& g,TransferOfferEditorState& state,WatcomRandom& rand) {
    uint32_t fee=0,salary=0;
    market_value(g,state.request.source_slot,int(state.quote_loan),state.percent,fee,salary,state.years,0,rand);
    // OpenBMH deviates from the original: uses the supported transfer-fee arithmetic and designated set-piece fields.
// supported rule profile MakeRequest ->PlayerCardMarketValue ->422f4; unsigned Source32 fee addition.
    state.quoted_fee=((fee+(fee>>2))/10000u)*10000u;
    state.quoted_salary=(salary/100u)*100u;
}
} // namespace

TransferOfferEditorState open_transfer_offer_editor_source(GameState& g,
    const TransferOfferEditorRequest& request,GameRandom& game,WatcomRandom& rand) {
    TransferOfferEditorState state;state.request=request;state.quote_loan=request.loan;
    if(const auto reason=editor_identity(g,request);!reason.empty())return editor_fail(state,reason);
    state.bid_slot=editor_slot(g,request);
    if(state.bid_slot!=request.expected_bid_slot)return editor_fail(state,"OpenTransferEditor: stale first free/own raw bid slot");
    if(!g.memory.has(::bmh::layout::CareerSession_Invalid_RuntimeField2)||!g.memory.has(::bmh::layout::DeskActions_OpenTransferOfferEditorSource_RuntimeField))
        return editor_fail(state,"OpenTransferEditor: missing saved opening G seed");
    state.mode=g.memory.u8(::bmh::layout::GameMode);state.supported=true;
    // InitializesOrdinaryEntryMarkerAbsenceStaysAbsent initializes the ordinary entry marker; absence stays absent.
    if(g.memory.has(::bmh::layout::DeskActions_EditorFail_RuntimeField2))g.memory.set8(::bmh::layout::DeskActions_EditorFail_RuntimeField2,0);
    const uint32_t seed=g.memory.u32(::bmh::layout::CareerSession_Invalid_RuntimeField2);
    // CareerSession_Save_RuntimeField is the Source transient G state, not a serialized Native field.
    // GameRandom owns it; mirror only when an explicit memory block exists.
    const bool mirror_g=g.memory.has(::bmh::layout::CareerSession_Save_RuntimeField)&&g.memory.has(::bmh::layout::DeskActions_EditorFail_RuntimeField);
    game.set_seed(std::bit_cast<int32_t>(seed));
    if(mirror_g)g.memory.set32(::bmh::layout::CareerSession_Save_RuntimeField,seed);
    state.percent=uint8_t(game.range(92,108));
    if(mirror_g)g.memory.set32(::bmh::layout::CareerSession_Save_RuntimeField,std::bit_cast<uint32_t>(game.seed()));
    editor_initialize_empty_bid(g,state);
    editor_project(g,state);
    state.player_info_selection=transfer_offer_player_info_selection_source(g,state);
    editor_quote(g,state,rand);return state;
}
TransferOfferEditorState edit_transfer_offer_editor_source(GameState& g,
    const TransferOfferEditorState& previous,const TransferOfferEditorEvent& event,GameRandom& game,WatcomRandom& rand) {
    (void)game;auto state=previous;
    if(const auto reason=editor_current(g,state);!reason.empty())return editor_fail(state,reason);
    if(event.field==TransferOfferEditorField::toggle_quote_loan) {
        state.quote_loan=!state.quote_loan;editor_quote(g,state,rand);return state;
    }
    const unsigned field=unsigned(event.field);
    if(!state.editable||field>unsigned(TransferOfferEditorField::bonus6))return editor_fail(state,"OpenTransferEditor: noneditable field");
    if(field>=2&&state.mode!=3)return editor_fail(state,"OpenTransferEditor: years/bonuses require Native mode3");
    if(!event.accepted) {
        if(field<2)editor_quote(g,state,rand);
        return state;
    }
    const auto& profile=field<2?state.money_profiles[field]:state.mode3_profiles[field-2];
    if(!profile.supported||event.value<profile.minimum||event.value>profile.maximum)
        return editor_fail(state,"OpenTransferEditor: calculator value outside Source field bounds");
    const uint32_t list=editor_list(state.request),bid=uint32_t(state.bid_slot);
    if(field==0)g.memory.set32(list+0x1c+bid*4,event.value);
    else if(field==1)g.memory.set32(list+0x0c+bid*4,event.value);
    else if(field==2)g.memory.set8(list+0x34+bid,uint8_t(event.value*12u)|(state.request.loan?128:0));
    else {const unsigned bonus=field-3;g.memory.set8(list+0x38+bid*7+bonus,uint8_t(bonus<3?event.value:event.value/5u));}
    editor_project(g,state);editor_quote(g,state,rand);return state;
}
TransferOfferEditorState resume_transfer_offer_team_info_source(GameState& g,
    const TransferOfferEditorState& previous,WatcomRandom& rand) {
    auto state=previous;
    if(!state.supported||state.closed)return editor_fail(state,"OpenTransferEditor: closed TeamInfo owner");
    if(const auto reason=editor_identity(g,state.request);!reason.empty())return editor_fail(state,reason);
    if(g.memory.u8(::bmh::layout::GameMode)!=state.mode)return editor_fail(state,"OpenTransferEditor: TeamInfo changed Native negotiation mode");
    // 1c18b rescans first FFFF/own. local1c may still be zero after an
    // existing-own/read-only opening; initialize the first empty choice once.
    // This resume never repeats CareerSession_Save_RuntimeField reset or the opening G draw.
    state.bid_slot=editor_slot(g,state.request);
    state.request.expected_bid_slot=state.bid_slot;
    editor_initialize_empty_bid(g,state);
    editor_project(g,state);editor_quote(g,state,rand);
    return state;
}
ClubPlayerDetailSelection transfer_offer_player_info_selection_source(const GameState& g,
    const TransferOfferEditorState& owner) {
    if(const auto reason=editor_current(g,owner);!reason.empty())throw std::invalid_argument(reason);
    const auto& r=owner.request;
    const int seller=r.expected_seller_word&0x7fff;
    // Literal68ca0 count and Nationalcoach special owner4, independently of
    // buyer identity and of any retained pending offer for the same player.
    if(!g.memory.has(::bmh::layout::ManagerCount))throw std::invalid_argument("OpenTransferEditor: missing Source manager count");
    const size_t count=g.memory.u8(::bmh::layout::ManagerCount);
    if(count>4||count>g.manager_teams.size())throw std::invalid_argument("OpenTransferEditor: invalid Source manager count");
    int manager=-1;
    for(size_t m=0;m<count;++m)if(g.manager_teams[m]==seller){manager=int(m);break;}
    if(manager<0&&!g.manager_teams.empty()&&g.manager_teams[0]==kNoTeam) {
        const size_t at=4*kRecordSize+0x290;
        if(at+2>g.manager_records.size())throw std::invalid_argument("OpenTransferEditor: missing Nationalcoach owner4");
        if((uint16_t(g.manager_records[at])|(uint16_t(g.manager_records[at+1])<<8))==seller)manager=4;
    }
    if(owner.player_info_selection) {
        const auto& retained=*owner.player_info_selection;
        if(retained.player_id!=r.expected_player||
           (manager<0&&!r.poach&&(retained.origin!=ClubPlayerDetailOrigin::market_pool||retained.market_pool_slot!=r.source_slot))||
           ((manager>=0||r.poach)&&(retained.origin!=ClubPlayerDetailOrigin::squad||retained.runtime_team!=seller)))
            throw std::invalid_argument("OpenTransferEditor: retained local40 owner changed");
        (void)club_player_detail_context(g,retained);
        return retained;
    }
    ClubPlayerDetailSelection selection;
    selection.player_id=r.expected_player;
    // A poaching row (40..43) is outside the market pool the detail screen knows; its
    // player is shown at his club instead, like a managed seller's player.
    if(manager<0&&!r.poach&&!r.loan_return_entry) {
        selection.origin=ClubPlayerDetailOrigin::market_pool;
        selection.market_pool_slot=r.source_slot;
        selection.rendered_row=r.list_entry; // Logical entry only; Native identity is explicit.
    } else {
        const auto range=squad_range(seller);
        if(range.first+range.count>g.squad_slots.size())throw std::invalid_argument("OpenTransferEditor: truncated seller squad");
        size_t row=0;
        while(row<range.count&&(g.squad_slots[range.first+row]&0x0fff)!=r.expected_player)++row;
        if(row==range.count)throw std::invalid_argument("OpenTransferEditor: managed seller player missing (Sourceerror10)");
        selection.runtime_team=seller;
        selection.physical_row=int(row);selection.rendered_row=int(row);
    }
    (void)club_player_detail_context(g,selection); // Bounds and actual board/global tables before RNG.
    return selection;
}
TransferOfferEditorState resume_transfer_offer_player_info_source(GameState& g,
    const TransferOfferEditorState& previous,uint8_t player_info_primary_refresh,uint8_t player_info_secondary_refresh,WatcomRandom& rand) {
    if(player_info_primary_refresh||player_info_secondary_refresh)return resume_transfer_offer_team_info_source(g,previous,rand);
    // Sourcegoto1c5b7 does not rescan, project profiles, initialize a row or
    // requote. A changed raw row cannot safely fit this port's typed editor
    // owner on that branch: reject it explicitly and discard modal staging.
    if(const auto reason=editor_current(g,previous);!reason.empty())return editor_fail(previous,reason);
    return previous;
}
TransferOfferEditorState cancel_transfer_offer_editor_source(const GameState& g,const TransferOfferEditorState& previous) {
    auto state=previous;if(const auto reason=editor_current(g,state);!reason.empty())return editor_fail(state,reason);
    state.closed=true;return state;
}
TransferOfferEditorSubmitResult submit_transfer_offer_editor_source(GameState& g,
    const TransferOfferEditorState& previous,WatcomRandom& rand,const HumanBuyerSaleHooks& hooks,MinuteReport& report) {
    TransferOfferEditorSubmitResult result;result.editor=previous;
    auto& state=result.editor;auto& action=result.action;
    action.player=state.request.expected_player;action.list_entry=state.request.list_entry;
    const auto unsupported=[&](const std::string& why){state=editor_fail(state,why);report.fail(why);action.status=TransferActionStatus::unsupported;return result;};
    if(report.unsupported)return unsupported(report.reason);
    if(const auto reason=editor_current(g,state);!reason.empty())return unsupported(reason);
    if(!state.editable){action.status=TransferActionStatus::blocked_by_board;return result;}
    if(!hooks.notice)return unsupported("OpenTransferEditor: missing Source notice presenter");
    const uint32_t list=editor_list(state.request),bid=uint32_t(state.bid_slot);
    const auto notice=[&](uint32_t address){hooks.notice(g,{state.request.manager,state.request.list_entry,-1,address,message_text(g,address),0x40});};
    if(state.fee==0||state.salary==0){notice(::bmh::layout::DeskActions_SubmitTransferOfferEditorSource_TextPointer);action.status=TransferActionStatus::rejected;return result;}
    const size_t rec=size_t(state.request.manager)*kRecordSize;
    const uint32_t available=uint32_t(rd32(g.manager_records,rec+0x314))+uint32_t(rd32(g.manager_records,rec+0x31c));
    bool own_exists=false;for(uint32_t n=0;n<4;++n)own_exists|=g.memory.u16(list+n*2)==state.request.runtime_club;
    if(state.fee>available) {
        notice(::bmh::layout::DeskActions_SubmitTransferOfferEditorSource_RuntimeField);if(own_exists){g.memory.set32(list+0x1c+bid*4,0);editor_project(g,state);}
        action.status=TransferActionStatus::insufficient_funds;return result;
    }
    if(own_exists) {action.status=TransferActionStatus::pending;action.fee=std::bit_cast<int32_t>(state.fee);action.annual_salary=std::bit_cast<int32_t>(state.salary);return result;}
    // Editor writes may retain Source stale bonuses outside their interactive
    // controls; qualifier reads all7 as bytes, without a <=100 clamp.
    TransferBidRequest request;request.manager=state.request.manager;request.list_entry=state.request.list_entry;
    request.player=state.request.expected_player;request.source_slot=state.request.source_slot;
    request.fee=int32_t(state.fee);request.annual_salary=int32_t(state.salary);request.contract_years=state.years;
    request.poach=state.request.poach;request.loan=state.request.loan;
    request.loan_return_entry=state.request.loan_return_entry;
    for(size_t n=0;n<7;++n)request.bonuses[n]=state.bonuses[n];
    bool entered_accounts=false;const auto present=hooks.notice;auto owned=hooks;
    owned.notice=[present,&entered_accounts](const GameState& staged,const HumanBuyerSaleNotice& event){
        if(event.source_text_address==::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField||event.source_text_address==::bmh::layout::DeskActions_SubmitTransferOfferEditorSource_ExecutableField)entered_accounts=true;
        present(staged,event);
    };
    action=submit_transfer_bid_source_human_buyer(g,request,rand,owned,report);
    if(report.unsupported||action.status==TransferActionStatus::unsupported)return unsupported(report.reason.empty()?"OpenTransferEditor: unsupported Source qualification/settlement":report.reason);
    if(entered_accounts)state.closed=true;
    // a poaching offer is decided at once: SubmitTransferBidSourceHumanBuyer never leaves a pending row behind
    else if(state.request.poach&&action.status!=TransferActionStatus::insufficient_funds&&
            action.status!=TransferActionStatus::no_squad_room)state.closed=true;
    else if(action.status==TransferActionStatus::pending){editor_project(g,state);editor_quote(g,state,rand);}
    return result;
}

TransferActionResult submit_transfer_bid(GameState& g, const TransferBidRequest& request, WatcomRandom& rand) {
    return submit_transfer_bid_impl(g, request, rand, nullptr, nullptr);
}

TransferActionResult submit_transfer_bid_source_human_buyer(GameState& g,
    const TransferBidRequest& request, WatcomRandom& rand,
    const HumanBuyerSaleHooks& hooks, MinuteReport& report) {
    return submit_transfer_bid_source_impl(g, request, rand, hooks, report);
}

TrainingCampBookingBlock training_camp_booking_block(const GameState& g, int manager) {
    if (manager < 0 || size_t(manager) >= g.manager_teams.size() ||
        (size_t(manager) + 1) * kRecordSize > g.manager_records.size())
        return TrainingCampBookingBlock::invalid_request;
    const uint8_t stays = g.manager_records.at(size_t(manager) * kRecordSize + kStays);
    if (stays & 0x80) return TrainingCampBookingBlock::booked_this_week;
    if ((stays & 0x7f) >= 3) return TrainingCampBookingBlock::visit_limit;
    return TrainingCampBookingBlock::none;
}

bool book_training_camp(GameState& g, int manager, int resort, WatcomRandom& rand) {
    if (resort < 0 || resort >= kTrainingCampResorts ||
        training_camp_booking_block(g, manager) != TrainingCampBookingBlock::none) return false;
    const int32_t cost = training_camp_price(g, manager, resort);
    if (!book_money(g, manager, cost, false)) return false;
    const size_t rec = size_t(manager) * kRecordSize;
    wr32(g.manager_records, rec + kCampExpenses, rd32(g.manager_records, rec + kCampExpenses) + cost);
    uint8_t& stays = g.manager_records.at(rec + kStays);
    stays = uint8_t((stays + 1) | 0x80);
    training_camp_effect(g, manager, resort, rand);
    return true;
}

void training_camp_effect(GameState& g, int manager, int resort, WatcomRandom& rand) {
    const size_t rec = size_t(manager) * kRecordSize;
    const int team = int8_t(g.manager_records.at(rec + kTeam));
    const auto& grade = g.training_camp_quality[size_t(resort)];

    // Morale (bits 0-5) + 12 * grade * 20 %, clamped to 0..0x2f (ClampSignedWord with a word
    // sum of the sign extended byte); the flag bits 6-7 are kept.
    uint8_t& tm = g.team_morale.at(size_t(team));
    const int morale = std::clamp(int(tm & 0x3f) + int(int16_t(scaled(12, grade[2]))), 0, 0x2f);
    tm = uint8_t((tm & 0xc0) | uint8_t(morale));

    // The maximum step of each value: the slider times its grade (stored as a byte), the
    // familiarity one 7 lower, the freshness one from the fourth grade.
    int8_t delta[5];
    for (int u = 0; u < 4; u++)
        delta[u] = int8_t(scaled(g.manager_records.at(rec + kFocus + size_t(u)), grade[kSliderGrade[u]]));
    delta[3] = int8_t(delta[3] - 7);
    delta[4] = int8_t(scaled(12, grade[3]));

    const SquadRange range = squad_range(team);
    for (size_t s = range.first; s < range.first + range.count; s++) {
        const uint16_t id = g.squad_slots.at(s) & 0x0fff;
        if (id == kEmptySlot) continue;
        Player& p = g.players.at(id);
        uint8_t* board = &g.tactics.at(size_t(manager) * 0x840 + (s - range.first) * 0x58);
        // Chance to train: 100 % up to age 29 (raw age 13), then 10 % less per year -
        // computed in 8 bits, so from raw age 25 on the value wraps above 100 (always).
        const uint8_t age = uint8_t(p.w364 >> 21 & 0x1f);
        const uint8_t chance = age < 14 ? 100 : uint8_t(100 - uint8_t((age - 14) * 10));
        int8_t v[5] = {int8_t(p.w35c >> 14 & 0x7f), int8_t(p.w35c >> 7 & 0x7f), int8_t(p.w35c & 0x7f),
                       int8_t(board[0xf]), int8_t(board[0xe] & 0x3f)};
        for (int i = 0; i < 5; i++) {
            const int lo = delta[i] < 0 ? 0 : 8;
            if (rand.range(0, 100) <= chance) {
                // values below 83..87 (88 in the better resorts 6 and 7) can still improve
                const int limit = rand.range(0x53, 0x57) + (resort >= 6 ? 1 : 0);
                if (v[i] < limit) {
                    const int k = rand.range(lo, delta[i] + 8);
                    v[i] = int8_t(v[i] + int8_t(kStep[k] * kDirection[i]));
                }
            }
            v[i] = std::clamp<int8_t>(v[i], 0, 0x63);  // Int8T
        }
        p.w35c = (p.w35c & 0xffe00000u) | uint32_t(v[0]) << 14 | uint32_t(v[1]) << 7 | uint32_t(v[2]);
        board[0xf] = uint8_t(v[3]);
        board[0xe] = uint8_t((board[0xe] & 0xc0) | std::min<uint8_t>(uint8_t(v[4]), 0x3f));
    }
}

}  // namespace bmh

namespace bmh {

namespace {

constexpr size_t kStockSize = 0x5c;
constexpr size_t kMoney = 0x314, kCredit = 0x31c, kStockBalance = 0x324;

uint16_t rd16(const std::vector<uint8_t>& v, size_t o) { return uint16_t(v.at(o) | v.at(o + 1) << 8); }
void wr16(std::vector<uint8_t>& v, size_t o, int x) {
    v.at(o) = uint8_t(x);
    v.at(o + 1) = uint8_t(x >> 8);
}

}  // namespace

int max_shares_to_buy(const GameState& g, int manager, int stock) {
    const size_t rec = size_t(manager) * kRecordSize, s = size_t(stock) * kStockSize;
    const int price = rd16(g.stocks, s + 0x4e);
    int n = (rd32(g.manager_records, rec + kMoney) + rd32(g.manager_records, rec + kCredit)) / price;
    const int held = rd16(g.stocks, s + 0x50 + size_t(manager) * 2);
    if (n + held > 9999) n = 9999 - held;
    return n;
}

bool buy_shares(GameState& g, int manager, int stock, int count) {
    const size_t rec = size_t(manager) * kRecordSize, s = size_t(stock) * kStockSize;
    // the paid amounts of all shares the manager holds (computed when the screen is drawn)
    int32_t portfolio = 0;
    for (int k = 0; k < kStocks; k++)
        if (rd16(g.stocks, size_t(k) * kStockSize + 0x50 + size_t(manager) * 2) != 0)
            portfolio += rd32(g.stocks, size_t(k) * kStockSize + 4 + size_t(manager) * 4);
    int32_t cost = rd16(g.stocks, s + 0x4e) * count;
    if (portfolio + cost >= rd32(g.manager_records, rec + kMoney) / 20) return false;
    wr16(g.stocks, s + 0x50 + size_t(manager) * 2, rd16(g.stocks, s + 0x50 + size_t(manager) * 2) + count);
    cost += cost * 15 / 1000;
    book_money(g, manager, cost, false);
    wr32(g.manager_records, rec + kStockBalance, rd32(g.manager_records, rec + kStockBalance) - cost);
    const size_t paid = s + 4 + size_t(manager) * 4;
    wr32(g.stocks, paid, rd32(g.stocks, paid) + cost);
    return true;
}

void sell_shares(GameState& g, int manager, int stock, int count) {
    const size_t rec = size_t(manager) * kRecordSize, s = size_t(stock) * kStockSize;
    const size_t paid = s + 4 + size_t(manager) * 4, held = s + 0x50 + size_t(manager) * 2;
    wr32(g.stocks, paid, int32_t(uint32_t(rd32(g.stocks, paid)) - uint32_t(rd32(g.stocks, paid)) / rd16(g.stocks, held) * uint32_t(count)));
    wr16(g.stocks, held, rd16(g.stocks, held) - count);
    const int32_t income = count * rd16(g.stocks, s + 0x4e);
    book_money(g, manager, -income, true);
    wr32(g.manager_records, rec + kStockBalance, rd32(g.manager_records, rec + kStockBalance) + income);
}

int stock_tip(GameState& g, int manager, int stock, WatcomRandom& rand) {
    const size_t s = size_t(stock) * kStockSize;
    int tip = int(g.stocks.at(s + 0x16)) - 1;
    if (g.stocks.at(s + 0x59) < 3) tip = 0;
    const int value = int16_t(uint32_t(rd32(g.stocks, s)) / 100);
    const int price = rd16(g.stocks, s + 0x4e);
    if (price < value - 5) tip++;
    if (value + 5 < price) tip--;
    if (price < value - 30) tip = 2;
    if (value + 30 < price) tip = -2;
    tip += 2;
    if (rand.range(0, 2) == 0) tip += rand.range(0, 2) * 2 - 2;
    tip = std::clamp(tip, 0, 4);
    book_money(g, manager, 5000, true, true);
    return tip;
}

}  // namespace bmh

namespace bmh {

namespace {

// StadiumPriceTable: base price of the 14 periphery objects.
constexpr int32_t kPeripheryPrice[14] = {2500000, 1700000, 2200000, 1000000, 1100000, 1500000, 280000,
                                         280000,  280000,  280000,  350000,  350000,  350000,  350000};
// CareerSession_DispatchStadium_RuntimeField2[object * 5]: construction time in days.
constexpr uint8_t kPeripheryDays[14] = {45, 30, 28, 40, 30, 20, 6, 6, 6, 6, 5, 5, 5, 5};

}  // namespace

int construction_price_percent(const GameState& g, int manager, GameRandom& game) {
    game.set_seed(int32_t(g.memory.u32(::bmh::layout::CareerSession_Invalid_RuntimeField2)));
    const int r = int16_t(rd16(g.manager_records, size_t(manager) * kRecordSize + 0x8b4)) / 100;
    return 100 - game.range(r, r + 5);
}

int32_t periphery_offer(const GameState& g, int object, GameRandom& game) {
    game.set_seed(int32_t(g.memory.u32(::bmh::layout::CareerSession_Invalid_RuntimeField2)));
    for (int i = 0; i < object; i++) game.range(0, 100);
    const int percent = game.range(0x55, 0x73);
    return kPeripheryPrice[object] * percent / 1000000 * 10000;
}

bool pay_construction(GameState& g, int manager, uint32_t price, bool sponsor, WatcomRandom& rand,
                      const ConstructionNoticeHook& notice) {
    const size_t rec = size_t(manager) * kRecordSize;
    uint8_t& share = g.manager_records.at(rec + 0x171);
    ConstructionNotice details{manager, ConstructionNoticeKind::InsufficientFunds, price, 0, 0};
    if (sponsor && price <= uint32_t(rd32(g.manager_records, rec + kMoney)) && share != 0) {
        if (price < uint32_t((share >> 2) & 7) * 0x4f588 + 26000) {
            details.kind = ConstructionNoticeKind::SponsorContribution;
            details.percent = int((share >> 5) + 3) * 10;
            details.subsidy = uint32_t(details.percent) * price / 100;
        } else {
            details.kind = ConstructionNoticeKind::SponsorExploited;
            const int sponsor_roll = rand.range(2, 4);
            details.percent = sponsor_roll * 5;
            details.subsidy = price * uint32_t(sponsor_roll) * 5 / 100;
        }
        price -= details.subsidy;
        // PayConstruction waits for the notice while the original share is still live.
        if (notice) notice(g, details);
        share = 0;
    }
    const bool paid = book_money(g, manager, int32_t(price), false, true);
    if (!paid && notice) {
        details.kind = ConstructionNoticeKind::InsufficientFunds;
        notice(g, details);
    }
    return paid;
}

bool build_periphery(GameState& g, int manager, int object, uint32_t price, WatcomRandom& rand,
                     const ConstructionNoticeHook& notice) {
    if (!pay_construction(g, manager, price, true, rand, notice)) return false;
    g.manager_records.at(size_t(manager) * kRecordSize + 0x1e + size_t(object)) = kPeripheryDays[object];
    return true;
}

StandBlockOffer prepare_stand_block_offer(uint32_t base_price, int construction_days, WatcomRandom& rand) {
    const int percent = rand.range(6, 14);  // PrepareStandBlockOffer draws before showing the prompt
    const uint32_t offered = uint32_t(uint64_t(base_price) * uint32_t(100 + percent) / 100);
    return {percent, base_price, offered, base_price, construction_days, construction_days,
            StandBlockOfferDecision::Pending};
}

bool resolve_stand_block_offer(StandBlockOffer& offer, bool accept, WatcomRandom& rand) {
    if (offer.decision != StandBlockOfferDecision::Pending || offer.percent < 6 || offer.percent > 14 ||
        offer.construction_days < 0 || offer.construction_days > 255 || offer.base_days != offer.construction_days ||
        offer.price != offer.base_price || offer.offered_price !=
            uint32_t(uint64_t(offer.base_price) * uint32_t(100 + offer.percent) / 100))
        return false;
    offer.decision = accept ? StandBlockOfferDecision::Accepted : StandBlockOfferDecision::Declined;
    if (!accept) return false;
    offer.price = offer.offered_price;
    offer.construction_days -= rand.range(offer.base_days >> 3, offer.base_days >> 1);
    return true;
}

std::vector<StadiumInvestmentOption> stadium_investment_options_source(
    const GameState& g, int manager, StadiumInvestment investment, int construction_percent,
    const StadiumInvestmentConstants& constants) {
    const size_t choice = size_t(investment);
    if (manager < 0 || size_t(manager) >= g.manager_teams.size() ||
        (size_t(manager) + 1) * kRecordSize > g.manager_records.size() || choice >= 8 ||
        construction_percent < 1 || construction_percent > 427 || constants.price[choice] == 0 ||
        constants.weeks[choice] == 0)
        throw FormatError("stadium: invalid investment owner or constants");
    const size_t rec = size_t(manager) * kRecordSize;
    if (g.manager_teams[size_t(manager)] < 0 || g.manager_teams[size_t(manager)] == kNoTeam ||
        rd16(g.manager_records, rec + kTeam) != g.manager_teams[size_t(manager)])
        throw FormatError("stadium: investment manager club differs from record");
    if (g.manager_records.at(rec + 0x2c + choice) != 0) return {};
    const uint8_t status = g.manager_records.at(rec + 0x3c + choice);
    const uint8_t current = status & 7;
    const bool binary = choice == 2 || choice == 3;
    if ((status & 0xf0) != 0)
        throw FormatError("stadium: invalid idle investment level");
    std::vector<StadiumInvestmentOption> options;
    // ConstructionPricePercent tests the masked terminal level, but its ordinary sprite loop
    // starts at the full raw byte. Bit3 cannot manufacture an ordinary tier.
    const int count = binary ? (current == 2 ? 0 : 1) : std::max(0, std::min(3, 4 - int(status)));
    for (int increment = 1; increment <= count; ++increment) {
        // ConstructionPricePercent uses a 32-bit multiplied base/discount before /100. The
        // authentic tables fit; reject malformed constants instead of wrapping.
        const uint64_t multiplied = uint64_t(constants.price[choice]) * uint32_t(increment) *
                                    uint32_t(construction_percent);
        if (multiplied > std::numeric_limits<uint32_t>::max())
            throw FormatError("stadium: investment price multiplication overflows Native width");
        const uint32_t price = uint32_t(multiplied) / 100;
        if (price == 0 || price > uint32_t(INT32_MAX))
            throw FormatError("stadium: investment price outside Native money range");
        options.push_back({status, current, uint8_t(increment), uint8_t((status + increment) & 7),
                           price, constants.weeks[choice]});
    }
    return options;
}

bool build_stadium_investment_source(GameState& g, int manager, StadiumInvestment investment,
    int construction_percent, const StadiumInvestmentConstants& constants,
    const StadiumInvestmentOption& option, const StandBlockOffer& offer, WatcomRandom& rand,
    const ConstructionNoticeHook& notice) {
    try {
        const auto options = stadium_investment_options_source(
            g, manager, investment, construction_percent, constants);
        if (std::find(options.begin(), options.end(), option) == options.end() ||
            offer.base_price != option.base_price || offer.base_days != option.base_weeks ||
            offer.percent < 6 || offer.percent > 14 ||
            offer.offered_price != uint32_t(uint64_t(option.base_price) * uint32_t(100 + offer.percent) / 100) ||
            offer.price > uint32_t(INT32_MAX) ||
            (offer.decision != StandBlockOfferDecision::Declined &&
             offer.decision != StandBlockOfferDecision::Accepted) ||
            (offer.decision == StandBlockOfferDecision::Declined &&
             (offer.price != option.base_price || offer.construction_days != option.base_weeks)) ||
            (offer.decision == StandBlockOfferDecision::Accepted &&
             (offer.price != offer.offered_price ||
              offer.construction_days > option.base_weeks - (option.base_weeks >> 3) ||
              offer.construction_days < option.base_weeks - (option.base_weeks >> 1))))
            return false;
    } catch (const FormatError&) {
        return false;
    }
    // Validation alone converts FormatError into a neutral rejection. Payment
    // and presenters must remain outside that catch so App can roll back a
    // failed notice instead of acknowledging it as a consumed funds refusal.
    if (!pay_construction(g, manager, offer.price, true, rand, notice)) return false;
    const size_t rec = size_t(manager) * kRecordSize;
    const size_t choice = size_t(investment);
    g.manager_records.at(rec + 0x2c + choice) = uint8_t(offer.construction_days);
    uint8_t& status = g.manager_records.at(rec + 0x3c + choice);
    status = uint8_t(status | uint8_t(uint8_t(status + option.increment) << 4));
    return true;
}

}  // namespace bmh

namespace bmh {

namespace {

constexpr uint32_t kFundBlock = ::bmh::layout::FundBlock;
constexpr uint32_t kFundSize = 0x1c;
constexpr size_t kAssets = 0x30c, kDeposit = 0x318;

}  // namespace

int max_fund_units(const GameState& g, int manager, int fund) {
    const size_t rec = size_t(manager) * kRecordSize;
    const uint32_t e = kFundBlock + uint32_t(fund) * kFundSize;
    const uint32_t price = g.memory.u32(e);
    const uint32_t each = price + price / 20;
    // (unsigned division as in the original)
    int n = int(uint32_t(rd32(g.manager_records, rec + kMoney) + rd32(g.manager_records, rec + kCredit)) / each);
    const int room = 10 - g.memory.u16(e + 0x14 + uint32_t(manager) * 2);
    return std::min(n, room);
}

int32_t buy_fund_units(GameState& g, int manager, int fund, int count, GameRandom& game) {
    const size_t rec = size_t(manager) * kRecordSize;
    const uint32_t e = kFundBlock + uint32_t(fund) * kFundSize;
    const uint32_t held = e + 0x14 + uint32_t(manager) * 2, paid = e + 4 + uint32_t(manager) * 4;
    auto add = [&](size_t o, int32_t x) { wr32(g.manager_records, rec + o, rd32(g.manager_records, rec + o) + x); };
    g.memory.set16(held, uint16_t(g.memory.u16(held) + count));
    const int32_t price = int32_t(g.memory.u32(e));
    const int32_t fee = count * price / 20;
    add(kAssets, fee);
    const int32_t total = count * price + fee;
    add(kStockBalance, -total);
    book_money(g, manager, total, false);
    add(kAssets, count * price / 9);
    g.memory.set32(paid, g.memory.u32(paid) + uint32_t(total));
    if (fund == 0) return 0;
    const int32_t surcharge = count * game.range(price / 40, price / 10);
    add(kAssets, surcharge);
    book_money(g, manager, surcharge, true);
    return surcharge;
}

void sell_fund_units(GameState& g, int manager, int fund, int count) {
    const size_t rec = size_t(manager) * kRecordSize;
    const uint32_t e = kFundBlock + uint32_t(fund) * kFundSize;
    const uint32_t held = e + 0x14 + uint32_t(manager) * 2, paid = e + 4 + uint32_t(manager) * 4;
    g.memory.set32(paid, g.memory.u32(paid) - g.memory.u32(paid) / g.memory.u16(held) * uint32_t(count));
    g.memory.set16(held, uint16_t(g.memory.u16(held) - count));
    const int32_t income = count * int32_t(g.memory.u32(e));
    book_money(g, manager, -income, true);
    wr32(g.manager_records, rec + kStockBalance, rd32(g.manager_records, rec + kStockBalance) + income);
    wr32(g.manager_records, rec + kAssets, rd32(g.manager_records, rec + kAssets) - income / 9);
}

void move_to_deposit(GameState& g, int manager, int32_t amount) {
    const size_t rec = size_t(manager) * kRecordSize;
    book_money(g, manager, amount, true);
    wr32(g.manager_records, rec + kStockBalance, rd32(g.manager_records, rec + kStockBalance) - amount);
    wr32(g.manager_records, rec + kDeposit, rd32(g.manager_records, rec + kDeposit) + amount);
}

void open_lineup_screen(GameState& g, int manager) {
    uint8_t* rec = &g.manager_records.at(size_t(manager) * 0xb0c);
    if ((rec[0xb0a] | rec[0xb0b] << 8) == 0x3253) return;
    for (int i = 0; i < 4; i++) rec[0xafc + i] = 0;
    rec[0xb08] = 0, rec[0xb09] = 0, rec[0xb0a] = 0x53, rec[0xb0b] = 0x32;
}

}  // namespace bmh

namespace bmh {

bool submit_pools_coupon(GameState& g, int manager, uint32_t stake,
                         const std::array<uint8_t, 11>& tips) {
    constexpr size_t kRecord = 0xb0c;
    constexpr uint32_t kCoupon = ::bmh::layout::Coupon;
    constexpr uint32_t kCouponBytes = 5 * 0x14;
    if (manager < 0 || size_t(manager) >= g.manager_teams.size() ||
        (size_t(manager) + 1) * kRecord > g.manager_records.size() ||
        !g.memory.has(kCoupon) || !g.memory.has(kCoupon + kCouponBytes - 1) ||
        !g.memory.has(::bmh::layout::DeskActions_SubmitPoolsCoupon_RuntimeField) || !g.memory.has(::bmh::layout::DeskActions_SubmitPoolsCoupon_RuntimeField + 43) ||
        g.memory.u8(::bmh::layout::CareerSession_DispatchToto_RuntimeField) != 11 ||
        (stake != 0 && (stake < 10'000 || stake > 400'000)) ||
        std::any_of(tips.begin(), tips.end(), [](uint8_t pick) { return pick > 2; }))
        return false;

    const uint32_t at = kCoupon + uint32_t(manager) * 0x14;
    g.memory.set32(at, stake);
    for (uint32_t i = 0; i < tips.size(); ++i) g.memory.set8(at + 8 + i, tips[i]);
    return true;
}

}  // namespace bmh

namespace bmh {
namespace {
constexpr size_t kAdRecord = 0xb0c;
constexpr size_t kAdPool = 0x8cc;
constexpr size_t kAdContracts = 0xa34;
constexpr size_t kAdContractBytes = 0x0c;
constexpr uint32_t kAdGroupId = ::bmh::layout::AdGroupId;

uint32_t ad_u32(const std::vector<uint8_t>& bytes, size_t at) {
    return uint32_t(bytes.at(at)) | uint32_t(bytes.at(at + 1)) << 8 |
           uint32_t(bytes.at(at + 2)) << 16 | uint32_t(bytes.at(at + 3)) << 24;
}

bool ad_replaceable(const std::vector<uint8_t>& bytes, size_t at) {
    return ad_u32(bytes, at) == 0 || (bytes.at(at + 6) & 0x80) != 0;
}

struct AdBoardOrder {
    std::array<uint32_t, 14> keys{};
    std::array<int, 14> slots{};
};

void ad_sort_order(AdBoardOrder& order, int first, int count) {
    while (count > 1) {
        const uint32_t pivot = order.keys[size_t(first + count / 2)];
        int left = -1;
        int right = count;
        do {
            do { ++left; } while (pivot < order.keys[size_t(first + left)]);
            do { --right; } while (order.keys[size_t(first + right)] < pivot);
            if (right <= left) break;
            std::swap(order.keys[size_t(first + left)], order.keys[size_t(first + right)]);
            std::swap(order.slots[size_t(first + left)], order.slots[size_t(first + right)]);
        } while (true);
        ad_sort_order(order, first, left);
        first += left;
        count -= left;
    }
}

AdBoardOrder ad_board_order(const std::vector<uint8_t>& records, size_t rec) {
    AdBoardOrder order;
    for (int slot = 0; slot < 14; ++slot) {
        const size_t at = rec + kAdContracts + size_t(slot) * kAdContractBytes;
        const uint32_t amount = ad_u32(records, at);
        uint32_t key = amount;
        if (amount != 0 && (records.at(at + 6) & 0x80) == 0) key += 1'000'000u;
        order.keys[size_t(slot)] = key;
        order.slots[size_t(slot)] = slot;
    }
    ad_sort_order(order, 0, 14);
    return order;
}

bool ad_manager_record_valid(const GameState& g, int manager) {
    return manager >= 0 && size_t(manager) < g.manager_teams.size() &&
           (size_t(manager) + 1) * kAdRecord <= g.manager_records.size();
}

bool ad_extension_requires_unsupported_prompt(const std::vector<uint8_t>& records, size_t at) {
    return (records.at(at + 6) & 0x7f) != 0 && records.at(at + 5) == 0;
}
}  // namespace

AdvertisingOfferList advertising_offers(const GameState& g, int manager, int display_slot) {
    AdvertisingOfferList out;
    out.manager = manager;
    out.display_slot = display_slot;
    if (!ad_manager_record_valid(g, manager)) {
        out.status = AdvertisingOfferStatus::invalid_manager;
        return out;
    }
    if (display_slot < 0 || display_slot > 15) {
        out.status = AdvertisingOfferStatus::invalid_display_slot;
        return out;
    }
    const size_t rec = size_t(manager) * kAdRecord;
    const int pool_group = display_slot < 14 ? 0 : (display_slot == 14 ? 1 : 2);
    const int pool_count = pool_group == 2 ? 5 : 10;
    const size_t pool = rec + kAdPool + size_t(pool_group) * 0x78;
    if (pool + size_t(pool_count) * kAdContractBytes > rec + kAdRecord ||
        rec + kAdContracts + 16 * kAdContractBytes > g.manager_records.size()) {
        out.status = AdvertisingOfferStatus::invalid_state;
        return out;
    }
    out.pool_group = pool_group;

    const AdBoardOrder order = ad_board_order(g.manager_records, rec);
    if (display_slot < 14) {
        out.contract_slot = order.slots[size_t(display_slot)];
        for (int slot = 0; slot < 14; ++slot) {
            const size_t at = rec + kAdContracts + size_t(slot) * kAdContractBytes;
            if (ad_replaceable(g.manager_records, at)) ++out.free_boards;
        }
    } else {
        out.contract_slot = display_slot;
    }
    const size_t target = rec + kAdContracts + size_t(out.contract_slot) * kAdContractBytes;
    std::copy_n(g.manager_records.begin() + std::ptrdiff_t(target), kAdContractBytes,
                out.contract_bytes.begin());
    if (ad_extension_requires_unsupported_prompt(g.manager_records, target)) {
        out.status = AdvertisingOfferStatus::unsupported_extension;
    } else if (pool_group != 0 && !ad_replaceable(g.manager_records, target)) {
        out.status = AdvertisingOfferStatus::unavailable_target;
    } else {
        out.status = AdvertisingOfferStatus::available;
    }

    bool has_offer = false;
    const bool selected_target_available = pool_group == 0 ||
        ad_replaceable(g.manager_records, rec + kAdContracts + size_t(out.contract_slot) * kAdContractBytes);
    for (int raw = 0; raw < pool_count; ++raw) {
        const size_t at = pool + size_t(raw) * kAdContractBytes;
        AdvertisingOfferCandidate candidate;
        candidate.raw_slot = raw;
        std::copy_n(g.manager_records.begin() + std::ptrdiff_t(at), kAdContractBytes,
                    candidate.bytes.begin());
        candidate.monthly_amount = ad_u32(g.manager_records, at);
        candidate.bundle_units = g.manager_records.at(at + 4);
        candidate.months = g.manager_records.at(at + 5);
        candidate.sponsor_id = g.manager_records.at(at + 7);
        candidate.present = candidate.monthly_amount != 0;
        has_offer |= candidate.present;
        const bool valid_offer = candidate.sponsor_id < 10 &&
                                 (pool_group != 0 || (candidate.bundle_units > 0 && candidate.bundle_units <= 14));
        const bool enough_boards = pool_group != 0 || candidate.bundle_units <= out.free_boards;
        bool insertion_targets_available = true;
        if (pool_group == 0 && candidate.bundle_units <= 14) {
            for (int i = 0; i < candidate.bundle_units; ++i) {
                const int raw_target = order.slots[size_t(13 - i)];
                const size_t at = rec + kAdContracts + size_t(raw_target) * kAdContractBytes;
                if (!ad_replaceable(g.manager_records, at)) insertion_targets_available = false;
            }
        }
        candidate.can_accept = candidate.present && valid_offer && enough_boards &&
                               selected_target_available && insertion_targets_available &&
                               out.status == AdvertisingOfferStatus::available;
        out.offers.push_back(candidate);
    }
    if (out.status == AdvertisingOfferStatus::available && !has_offer)
        out.status = AdvertisingOfferStatus::no_offers;
    return out;
}

AdvertisingOfferResult accept_advertising_offer(GameState& g,
                                                 const AdvertisingOfferRequest& request) {
    AdvertisingOfferResult result;
    result.manager = request.manager;
    result.display_slot = request.display_slot;
    result.contract_slot = request.contract_slot;
    result.raw_slot = request.raw_slot;
    if (!ad_manager_record_valid(g, request.manager)) {
        result.status = AdvertisingOfferStatus::invalid_manager;
        return result;
    }
    const AdvertisingOfferList view = advertising_offers(g, request.manager, request.display_slot);
    if (view.status != AdvertisingOfferStatus::available) {
        result.status = view.status;
        return result;
    }
    if (view.contract_slot != request.contract_slot || request.contract_slot < 0 ||
        request.contract_slot > 15 || request.raw_slot < 0) {
        result.status = AdvertisingOfferStatus::invalid_request;
        return result;
    }
    const size_t rec = size_t(request.manager) * kAdRecord;
    const size_t target = rec + kAdContracts + size_t(request.contract_slot) * kAdContractBytes;
    const auto order = ad_board_order(g.manager_records, rec);
    if (request.display_slot < 14 && order.slots[size_t(request.display_slot)] != request.contract_slot) {
        result.status = AdvertisingOfferStatus::stale_offer;
        return result;
    }
    if (request.expected_contract != [&] {
            std::array<uint8_t, 12> bytes{};
            std::copy_n(g.manager_records.begin() + std::ptrdiff_t(target), kAdContractBytes, bytes.begin());
            return bytes;
        }()) {
        result.status = AdvertisingOfferStatus::stale_offer;
        return result;
    }

    const int pool_group = view.pool_group;
    const int pool_count = pool_group == 2 ? 5 : 10;
    if (request.raw_slot >= pool_count) {
        result.status = AdvertisingOfferStatus::invalid_request;
        return result;
    }
    const size_t pool = rec + kAdPool + size_t(pool_group) * 0x78 + size_t(request.raw_slot) * kAdContractBytes;
    std::array<uint8_t, 12> offer{};
    std::copy_n(g.manager_records.begin() + std::ptrdiff_t(pool), kAdContractBytes, offer.begin());
    if (offer != request.expected_offer || ad_u32(g.manager_records, pool) == 0) {
        result.status = AdvertisingOfferStatus::stale_offer;
        return result;
    }
    const uint8_t sponsor = offer[7];
    const uint8_t bundle = offer[4];
    if (sponsor >= 10 || (pool_group == 0 && (bundle == 0 || bundle > 14))) {
        result.status = AdvertisingOfferStatus::malformed_offer;
        return result;
    }
    if (pool_group == 0 && bundle > view.free_boards) {
        result.status = AdvertisingOfferStatus::insufficient_boards;
        return result;
    }
    if (pool_group == 0 && !g.memory.has(kAdGroupId)) {
        result.status = AdvertisingOfferStatus::invalid_state;
        return result;
    }

    // Prepare all writes in owned bytes first. Source AdBoardOrder sorts descending and
    // moves option-marked rows without the +1,000,000 active-contract priority.
    std::vector<uint8_t> next = g.manager_records;
    {
        const auto staged_order = ad_board_order(next, rec);
        std::array<std::array<uint8_t, 12>, 14> sorted{};
        for (int i = 0; i < 14; ++i) {
            const size_t from = rec + kAdContracts + size_t(staged_order.slots[size_t(i)]) * kAdContractBytes;
            std::copy_n(next.begin() + std::ptrdiff_t(from), kAdContractBytes, sorted[size_t(i)].begin());
        }
        for (int i = 0; i < 14; ++i) {
            const size_t to = rec + kAdContracts + size_t(i) * kAdContractBytes;
            std::copy(sorted[size_t(i)].begin(), sorted[size_t(i)].end(), next.begin() + std::ptrdiff_t(to));
        }
    }
    const size_t sorted_target = rec + kAdContracts + size_t(request.display_slot) * kAdContractBytes;
    if (pool_group != 0 && !ad_replaceable(next, sorted_target)) {
        result.status = AdvertisingOfferStatus::unavailable_target;
        return result;
    }
    if (ad_extension_requires_unsupported_prompt(next, sorted_target)) {
        result.status = AdvertisingOfferStatus::unsupported_extension;
        return result;
    }
    if (pool_group == 0) {
        const uint8_t group_id = g.memory.u8(kAdGroupId);
        for (int i = 0; i < bundle; ++i) {
            const size_t to = rec + kAdContracts + size_t(13 - i) * kAdContractBytes;
            if (!ad_replaceable(next, to)) {
                result.status = AdvertisingOfferStatus::unavailable_target;
                return result;
            }
            std::copy(offer.begin(), offer.end(), next.begin() + std::ptrdiff_t(to));
            next[to + 4] = group_id;
        }
        std::vector<uint8_t> before_resort = next;
        const auto final_order = ad_board_order(before_resort, rec);
        std::array<std::array<uint8_t, 12>, 14> sorted{};
        for (int i = 0; i < 14; ++i) {
            const size_t from = rec + kAdContracts + size_t(final_order.slots[size_t(i)]) * kAdContractBytes;
            std::copy_n(before_resort.begin() + std::ptrdiff_t(from), kAdContractBytes, sorted[size_t(i)].begin());
        }
        for (int i = 0; i < 14; ++i) {
            const size_t to = rec + kAdContracts + size_t(i) * kAdContractBytes;
            std::copy(sorted[size_t(i)].begin(), sorted[size_t(i)].end(), next.begin() + std::ptrdiff_t(to));
        }
    } else {
        std::copy(offer.begin(), offer.end(), next.begin() + std::ptrdiff_t(sorted_target));
    }
    for (size_t i = 0; i < 4; ++i) next[pool + i] = 0; // Source clears the offer amount only.

    std::copy(next.begin() + std::ptrdiff_t(rec), next.begin() + std::ptrdiff_t(rec + kAdRecord),
              g.manager_records.begin() + std::ptrdiff_t(rec));
    if (pool_group == 0) g.memory.set8(kAdGroupId, uint8_t(g.memory.u8(kAdGroupId) + 1));
    result.status = AdvertisingOfferStatus::accepted;
    return result;
}

AdvertisingSpendingView advertising_spending(const GameState& g, int manager, int target_slot) {
    AdvertisingSpendingView view;
    view.manager = manager;
    view.target_slot = target_slot;
    if (!ad_manager_record_valid(g, manager)) return view;
    if (target_slot < 16 || target_slot > 18) return view;
    const size_t rec = size_t(manager) * kAdRecord;
    const size_t field = rec + (target_slot == 16 ? 0x850 : target_slot == 17 ? 0x854 : 0x858);
    if (field + 4 > g.manager_records.size() || !g.memory.has(::bmh::layout::ActiveManager) ||
        int(g.memory.u8(::bmh::layout::ActiveManager)) != manager) return view;
    view.current_amount = ad_u32(g.manager_records, field);
    view.maximum_amount = target_slot == 16 ? 350000u : 35000u;
    if (target_slot == 16) view.delivery_weeks = g.manager_records.at(rec + 0x287);
    // BalanceFromGame clamps a legacy current value to the widget maximum; it does
    // not make an otherwise available field uneditable.
    view.available = target_slot != 16 || view.delivery_weeks == 0;
    return view;
}

AdvertisingSpendingResult set_advertising_spending(GameState& g,
                                                    const AdvertisingSpendingRequest& request,
                                                    WatcomRandom& rand) {
    AdvertisingSpendingResult result;
    const auto reject = [&](AdvertisingSpendingStatus status) {
        result.status = status;
        return result;
    };
    if (!ad_manager_record_valid(g, request.manager) || !g.memory.has(::bmh::layout::ActiveManager) ||
        int(g.memory.u8(::bmh::layout::ActiveManager)) != request.manager)
        return reject(AdvertisingSpendingStatus::invalid_manager);
    if (request.target_slot < 16 || request.target_slot > 18)
        return reject(AdvertisingSpendingStatus::invalid_target);
    const size_t rec = size_t(request.manager) * kAdRecord;
    const size_t field = rec + (request.target_slot == 16 ? 0x850 :
                                request.target_slot == 17 ? 0x854 : 0x858);
    if (field + 4 > g.manager_records.size()) return reject(AdvertisingSpendingStatus::invalid_state);
    const uint32_t current = ad_u32(g.manager_records, field);
    uint8_t delivery = 0;
    if (request.target_slot == 16) {
        if (rec + 0x288 > g.manager_records.size()) return reject(AdvertisingSpendingStatus::invalid_state);
        delivery = g.manager_records.at(rec + 0x287);
    }
    if (current != request.expected_amount || delivery != request.expected_delivery_weeks)
        return reject(AdvertisingSpendingStatus::stale_request);
    const uint32_t maximum = request.target_slot == 16 ? 350000u : 35000u;
    if (request.amount > maximum)
        return reject(AdvertisingSpendingStatus::invalid_amount);
    if (request.target_slot == 16 && delivery != 0)
        return reject(AdvertisingSpendingStatus::delivery_pending);

    if (request.target_slot == 16) {
        // BalanceFromGame may return zero; the merchandise branch then performs no writes.
        if (request.amount == 0) return reject(AdvertisingSpendingStatus::no_change);
        GameState staged = g;
        WatcomRandom staged_rand = rand;
        if (!book_money(staged, request.manager, int32_t(request.amount), false, true)) {
            // CheckAdvertisingRenewal clears +0x850 on insufficient funds but does not draw delivery RNG.
            wr32(staged.manager_records, field, 0);
            g.manager_records = std::move(staged.manager_records);
            result.status = AdvertisingSpendingStatus::insufficient_funds;
            result.changed = current != 0;
            result.message_address = ::bmh::layout::Advertising_FormatError_TextPointer18;
            return result;
        }
        wr32(staged.manager_records, field, int32_t(request.amount));
        staged.manager_records.at(rec + 0x287) = uint8_t(staged_rand.range(6, 9));
        g.manager_records = std::move(staged.manager_records);
        rand = staged_rand;
        result.status = AdvertisingSpendingStatus::accepted;
        result.changed = true;
        result.message_address = ::bmh::layout::DeskActions_Reject_ExecutableField;
        return result;
    }

    if (current == request.amount) return reject(AdvertisingSpendingStatus::no_change);
    wr32(g.manager_records, field, int32_t(request.amount));
    result.status = AdvertisingSpendingStatus::accepted;
    result.changed = true;
    return result;
}

namespace {

void wr32_ad(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.at(at + size_t(i)) = uint8_t(v >> (8 * i));
}

// The end of a renewal (CheckAdvertisingRenewal): byte +6 cleared, the other boards of the same
// bundle (group byte +4, non-empty) take a copy of the contract (renewed or freed).
void settle_advertising_renewal(GameState& g, size_t rec, int contract_slot) {
    std::vector<uint8_t>& r = g.manager_records;
    const size_t at = rec + kAdContracts + size_t(contract_slot) * kAdContractBytes;
    r.at(at + 6) = 0;
    if (contract_slot >= 14) return;
    for (int other = 0; other < 14; ++other) {
        if (other == contract_slot) continue;
        const size_t o = rec + kAdContracts + size_t(other) * kAdContractBytes;
        if (r.at(o + 4) == r.at(at + 4) && ad_u32(r, o) != 0)
            std::copy_n(r.begin() + std::ptrdiff_t(at), kAdContractBytes, r.begin() + std::ptrdiff_t(o));
    }
}

}  // namespace

AdvertisingRenewal check_advertising_renewal(GameState& g, int manager, int display_slot, WatcomRandom& rand) {
    AdvertisingRenewal out;
    if (!ad_manager_record_valid(g, manager) || display_slot < 0 || display_slot > 15) return out;
    const size_t rec = size_t(manager) * kAdRecord;
    out.contract_slot = display_slot < 14 ? ad_board_order(g.manager_records, rec).slots[size_t(display_slot)] : display_slot;
    const size_t at = rec + kAdContracts + size_t(out.contract_slot) * kAdContractBytes;
    std::vector<uint8_t>& r = g.manager_records;
    if (!ad_extension_requires_unsupported_prompt(r, at)) {
        out.status = AdvertisingRenewal::Status::not_expired;
        return out;
    }
    // the sponsor name: board/shirt firms from Advertising_FormatError_ExecutableField3, TV stations ten entries later
    const uint32_t name = ::bmh::layout::Advertising_FormatError_ExecutableField3 + uint32_t((out.contract_slot == 15 ? 10 : 0) + std::min<int>(r.at(at + 7), 9)) * 4;
    const int demand = int(uint32_t(r.at(at + 8)) | uint32_t(r.at(at + 9)) << 8);
    const int scaled = demand * int(rand.range(0x5f, 0x69)) / 100;
    if (scaled < club_attraction(g, g.manager_teams.at(size_t(manager)))) {
        out.status = AdvertisingRenewal::Status::offered;
        out.question = message_text(g, ::bmh::layout::DeskActions_CheckAdvertisingRenewal_TextPointer) + message_text(g, name) + message_text(g, ::bmh::layout::DeskActions_CheckAdvertisingRenewal_TextPointer2);
        return out;
    }
    out.status = AdvertisingRenewal::Status::declined_by_sponsor;
    out.notice = message_text(g, ::bmh::layout::DeskActions_CheckAdvertisingRenewal_TextPointer3) + message_text(g, name) + message_text(g, ::bmh::layout::DeskActions_CheckAdvertisingRenewal_TextPointer4) + message_text(g, ::bmh::layout::DeskActions_CheckAdvertisingRenewal_TextPointer5);
    post_composed(g, manager, out.notice);
    wr32_ad(r, at, 0);
    settle_advertising_renewal(g, rec, out.contract_slot);
    return out;
}

bool answer_advertising_renewal(GameState& g, int manager, int contract_slot, bool renew) {
    if (!ad_manager_record_valid(g, manager) || contract_slot < 0 || contract_slot > 15) return false;
    const size_t rec = size_t(manager) * kAdRecord;
    const size_t at = rec + kAdContracts + size_t(contract_slot) * kAdContractBytes;
    std::vector<uint8_t>& r = g.manager_records;
    if (!ad_extension_requires_unsupported_prompt(r, at)) return false;
    if (renew) r.at(at + 5) = uint8_t(r.at(at + 6) & 0x7f);
    else wr32_ad(r, at, 0);
    settle_advertising_renewal(g, rec, contract_slot);
    return true;
}

}  // namespace bmh
