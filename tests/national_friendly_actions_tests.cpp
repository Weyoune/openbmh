// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/desk_actions.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace {
using namespace bmh;
constexpr size_t kRecord = 0xb0c;
constexpr uint32_t kPlannerDays = ::bmh::layout::FriendlyDays;
constexpr uint32_t kPlannerTeams = ::bmh::layout::FriendlyTeams;
constexpr uint32_t kCalendar = ::bmh::layout::CompetitionCalendar;
constexpr uint32_t kPlanCount = ::bmh::layout::FriendlyPlanCount;

void check(bool condition, const char* why) {
    if (!condition) throw std::runtime_error(std::string("national friendly: ") + why);
}

void put32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes.at(at + size_t(i)) = uint8_t(value >> (i * 8));
}
uint32_t get32(const std::vector<uint8_t>& bytes, size_t at) {
    return uint32_t(bytes.at(at)) | uint32_t(bytes.at(at + 1)) << 8 |
           uint32_t(bytes.at(at + 2)) << 16 | uint32_t(bytes.at(at + 3)) << 24;
}

GameState fixture() {
    GameState g;
    g.manager_teams = {kNoTeam};
    g.manager_records.assign(5 * kRecord, 0);
    g.manager_records[0x290] = uint8_t(kNoTeam);
    g.manager_records[0x291] = uint8_t(kNoTeam >> 8);
    put32(g.manager_records, 0x314, 5'000'000);
    put32(g.manager_records, 4 * kRecord + 0x314, 5'000'000); // spare association manager 4
    g.squad_slots.assign(3096, kEmptySlot);
    g.foreign_strength.assign(251, 0x80);
    g.memory.add_block(kPlanCount, 1);
    g.memory.set8(kPlanCount, 0);
    g.memory.add_block(kPlannerDays, 30 * 2);
    g.memory.add_block(kPlannerTeams, 30 * 4);
    g.memory.add_block(kCalendar, 0x16e * 2);
    g.memory.add_block(::bmh::layout::DaysIntoSeason, 2);
    g.memory.add_block(::bmh::layout::CupDraw_FormatError_RuntimeField3, 2);
    g.date = 50;
    g.season_start = 0;
    g.level = 1;
    return g;
}

NationalFriendlyOwner owner() { return {77, 12, 0, kNoTeam, 6}; }

NationalFriendlyQuote accepted_quote(GameState& g, WatcomRandom& rand, int opponent = 0x16c,
                                     bool manager_home = true) {
    return quote_national_friendly_match(g, owner(), opponent, manager_home, rand);
}

uint16_t available_day(const GameState& g, const NationalFriendlyQuote& quote,
                       const WatcomRandom& rand) {
    for (uint16_t day = 51; day < 0x16e; ++day) {
        const auto status = national_friendly_match_date_status(g, quote, owner(), rand, day);
        if (status == FriendlyDateStatus::available ||
            status == FriendlyDateStatus::available_with_calendar_event) return day;
    }
    throw std::runtime_error("national friendly: fixture has no free source date");
}

void check_quote_rng_and_cancel() {
    GameState g = fixture();
    WatcomRandom rand(0x12121212);
    const WatcomRandom before = rand;
    const auto preview = preview_national_friendly_match(g, owner(), 0x16c, true, rand);
    check(preview.terms.status == FriendlyQuoteStatus::quoted, "preview quotes valid national owner");
    check(rand.seed() == before.seed() && rand.calls() == before.calls(), "preview leaves W untouched");
    check(preview.watcom_calls_after - preview.watcom_calls_before == 4,
          "foreign nation rating consumes four original W draws");

    auto invalid_owner = owner();
    invalid_owner.runtime_team = 9;
    const auto invalid = quote_national_friendly_match(g, invalid_owner, 0x16c, true, rand);
    check(invalid.terms.status == FriendlyQuoteStatus::invalid_manager &&
          rand.seed() == before.seed() && rand.calls() == before.calls(),
          "invalid owner consumes no W");
    invalid_owner = owner();
    invalid_owner.menu_kind = 23;
    const auto wrong_menu = quote_national_friendly_match(g, invalid_owner, 0x16c, true, rand);
    check(wrong_menu.terms.status == FriendlyQuoteStatus::invalid_manager &&
          rand.seed() == before.seed() && rand.calls() == before.calls(),
          "ordinary menu 23 cannot enter national selection scope");
    const auto invalid_nation = quote_national_friendly_match(g, owner(), 0x165, true, rand);
    check(invalid_nation.terms.status == FriendlyQuoteStatus::invalid_opponent &&
          rand.seed() == before.seed() && rand.calls() == before.calls(),
          "zero/own nation opponent consumes no W");

    const auto quote = accepted_quote(g, rand);
    check(quote.terms.status == FriendlyQuoteStatus::quoted &&
          quote.terms.opponent == 0x16c && quote.terms.manager_team == kNoTeam,
          "accepted quote binds selected nation without changing own team");
    const uint32_t rating_sum = uint32_t(quote.own_rating + quote.opponent_rating) * 1000;
    const int32_t source_cost = int32_t((uint64_t(rating_sum) * (rating_sum / 13000) / 7000) * 1000);
    check(quote.terms.cost == source_cost, "quote uses Original integer cost operation order");
    check(rand.seed() == quote.watcom_seed_after && rand.calls() == quote.watcom_calls_after &&
          rand.calls() == before.calls() + 4, "accepted quote consumes W once");

    const uint16_t day = available_day(g, quote, rand);
    NationalFriendlyBookingRequest cancel;
    cancel.quote = quote;
    cancel.owner = owner();
    cancel.season_day = day;
    cancel.approving_manager = 0;
    GameRandom game(1234), game_before = game;
    const auto state_before = g;
    const auto result = book_national_friendly_match(g, cancel, rand, game);
    check(result.status == FriendlyBookingStatus::declined, "payer can decline after accepted quote");
    check(g.memory.u8(kPlanCount) == state_before.memory.u8(kPlanCount) &&
          g.manager_records == state_before.manager_records, "decline leaves career unchanged");
    check(game.seed() == game_before.seed() && game.calls() == game_before.calls(),
          "decline consumes no G");
}

void check_success_and_failure_atomicity() {
    GameState g = fixture();
    WatcomRandom rand(0x76543210);
    auto quote = accepted_quote(g, rand);
    const uint16_t day = available_day(g, quote, rand);
    NationalFriendlyBookingRequest request;
    request.quote = quote;
    request.owner = owner();
    request.season_day = day;
    request.accept_cost = true;
    request.approving_manager = 0;

    GameRandom game(0x12345678), expected = game;
    const bool expected_bonus = expected.range(60, 90) < quote.opponent_rating;
    const uint8_t old_league_byte = g.manager_records[0x210];
    const auto old_money = get32(g.manager_records, 0x314);
    const auto result = book_national_friendly_match(g, request, rand, game);
    check(result.status == FriendlyBookingStatus::booked, "valid national match books");
    check(result.national_bonus_awarded == expected_bonus, "bonus uses one source G(60,90) draw");
    check(game.seed() == expected.seed() && game.calls() == expected.calls(), "successful booking commits one G draw");
    check(g.manager_records[0x210] == uint8_t(old_league_byte + unsigned(expected_bonus)),
          "successful booking applies source record +0x210 bonus byte");
    check(get32(g.manager_records, 0x314) == old_money - uint32_t(quote.terms.cost),
          "home national coach pays exact quoted fee");
    check(g.memory.u16(kPlannerDays) == day && g.memory.u16(kPlannerTeams) == kNoTeam &&
          g.memory.u16(kPlannerTeams + 2) == quote.terms.opponent &&
          g.memory.u8(kPlanCount) == 2, "booking inserts one source pair and increments count by two");
    check(g.manager_teams[0] == kNoTeam, "booking does not replace the national owner's team");

    GameState poor = fixture();
    put32(poor.manager_records, 0x314, 0);
    put32(poor.manager_records, 0x31c, 0);
    WatcomRandom poor_rand(0x76543210);
    auto poor_quote = accepted_quote(poor, poor_rand);
    NationalFriendlyBookingRequest poor_request = request;
    poor_request.quote = poor_quote;
    poor_request.season_day = available_day(poor, poor_quote, poor_rand);
    GameRandom poor_game(9876), poor_before = poor_game;
    const auto poor_memory = poor.memory;
    const auto poor_records = poor.manager_records;
    const auto poor_result = book_national_friendly_match(poor, poor_request, poor_rand, poor_game);
    check(poor_result.status == FriendlyBookingStatus::insufficient_funds,
          "insufficient payer funds reject booking");
    check(poor.memory.u8(kPlanCount) == poor_memory.u8(kPlanCount) &&
          poor.manager_records == poor_records, "funds rejection has no partial Native writes");
    check(poor_game.seed() == poor_before.seed() && poor_game.calls() == poor_before.calls(),
          "funds rejection consumes no G");
}

void check_stale_and_source_association_payer() {
    GameState g = fixture();
    WatcomRandom rand(0xabcdef01);
    auto quote = accepted_quote(g, rand);
    const uint16_t day = available_day(g, quote, rand);
    NationalFriendlyBookingRequest stale;
    stale.quote = quote;
    stale.owner = owner();
    ++stale.owner.revision;
    stale.season_day = day;
    stale.accept_cost = true;
    stale.approving_manager = 0;
    GameRandom game(11), before = game;
    const auto bytes = g.manager_records;
    check(book_national_friendly_match(g, stale, rand, game).status == FriendlyBookingStatus::stale_quote,
          "revision change invalidates quote");
    check(g.manager_records == bytes && game.seed() == before.seed() && game.calls() == before.calls(),
          "stale quote is atomic and consumes no G");

    NationalFriendlyBookingRequest stale_w;
    stale_w.quote = quote;
    stale_w.owner = owner();
    stale_w.season_day = day;
    stale_w.accept_cost = true;
    stale_w.approving_manager = 0;
    WatcomRandom changed_w = rand;
    changed_w.rand();
    check(book_national_friendly_match(g, stale_w, changed_w, game).status ==
              FriendlyBookingStatus::stale_quote,
          "changed W anchor invalidates accepted quote");
    check(g.manager_records == bytes && game.seed() == before.seed() && game.calls() == before.calls(),
          "W-stale quote leaves Native/G unchanged");

    GameState unowned_away = fixture();
    WatcomRandom away_rand(0xabcdef01);
    auto away_quote = accepted_quote(unowned_away, away_rand, 0x16c, false);
    check(away_quote.terms.opponent_manager == -1 && away_quote.terms.payer_manager == -1,
          "unowned national away side has no source consent owner or payer");
    NationalFriendlyBookingRequest away_request;
    away_request.quote = away_quote;
    away_request.owner = owner();
    away_request.season_day = available_day(unowned_away, away_quote, away_rand);
    const uint32_t away_owner_money = get32(unowned_away.manager_records, 0x314);
    GameRandom away_game(88);
    check(book_national_friendly_match(unowned_away, away_request, away_rand, away_game).status ==
              FriendlyBookingStatus::booked,
          "unowned national away fixture follows source no-manager path");
    check(get32(unowned_away.manager_records, 0x314) == away_owner_money,
          "unowned away side is not charged to the national coach");

    GameState home_association = fixture();
    home_association.memory.set16(::bmh::layout::CupDraw_FormatError_RuntimeField3, 0x16c);
    WatcomRandom home_rand(0xabcdef01);
    auto home_quote = accepted_quote(home_association, home_rand, 0x16c, true);
    NationalFriendlyBookingRequest home_request;
    home_request.quote = home_quote;
    home_request.owner = owner();
    home_request.season_day = available_day(home_association, home_quote, home_rand);
    home_request.accept_cost = true;
    home_request.approving_manager = 0;
    GameRandom home_game(44), home_before = home_game;
    const auto home_records = home_association.manager_records;
    check(book_national_friendly_match(home_association, home_request, home_rand, home_game).status ==
              FriendlyBookingStatus::opponent_declined,
          "mapped opponent consent is requested even when the national manager pays");
    check(home_association.manager_records == home_records && home_game.seed() == home_before.seed() &&
          home_game.calls() == home_before.calls(), "opponent refusal changes neither Native nor G");

    GameState association = fixture();
    association.memory.set16(::bmh::layout::CupDraw_FormatError_RuntimeField3, 0x16c);
    WatcomRandom association_rand(0xabcdef01);
    auto association_quote = accepted_quote(association, association_rand, 0x16c, false);
    check(association_quote.terms.opponent_manager == 4 &&
          association_quote.terms.payer_manager == 4,
          "legacy routine maps current national association to spare manager 4");
    const uint16_t association_day = available_day(association, association_quote, association_rand);
    NationalFriendlyBookingRequest association_request;
    association_request.quote = association_quote;
    association_request.owner = owner();
    association_request.season_day = association_day;
    association_request.accept_opponent = true;
    association_request.consenting_manager = 4;
    association_request.accept_cost = true;
    association_request.approving_manager = 4;
    GameRandom association_game(44);
    const auto result = book_national_friendly_match(association, association_request,
                                                      association_rand, association_game);
    check(result.status == FriendlyBookingStatus::booked,
          (std::string("association manager consent/payment rejected with status ") +
           std::to_string(int(result.status))).c_str());
    check(get32(association.manager_records, 4 * kRecord + 0x314) < 5'000'000,
          "spare association manager 4 pays the source fee");
}
}

int main() {
    check_quote_rng_and_cancel();
    check_success_and_failure_atomicity();
    check_stale_and_source_association_payer();
}
