// SPDX-License-Identifier: GPL-3.0-only
#include "modern/game/stadium_blocks.hpp"
#include "modern/game/modern_state.hpp"

#include "modern/game/matchday.hpp"

#include <algorithm>
#include <limits>

namespace bmh::modern {
namespace {
constexpr size_t kRecordSize = 0xb0c;
constexpr size_t kManagerClub = 0x290;
constexpr size_t kBuild = 0x34;
constexpr size_t kFlags = 0x44;
constexpr size_t kCapacity = 0x4c;
constexpr size_t kFutureCapacity = 0x54;
constexpr uint8_t kSeat = 1;
constexpr uint8_t kRoof = 2;
constexpr uint8_t kKnownFeatures = kSeat | kRoof;

bool slot_index(StadiumBlockSlot slot, size_t& out) {
    const auto raw = static_cast<uint8_t>(slot);
    if (raw >= 8) return false;
    out = raw;
    return true;
}

uint16_t record_u16(const std::vector<uint8_t>& records, size_t at) {
    return uint16_t(records.at(at)) | uint16_t(records.at(at + 1)) << 8;
}

std::optional<uint32_t> checked_component(uint32_t price_per_1000, uint32_t places,
                                          uint32_t multiplier, uint32_t divisor) {
    const uint64_t units = places / 1000;
    const uint64_t multiplied = uint64_t(price_per_1000) * units * multiplier;
    if (multiplied > uint64_t(std::numeric_limits<int32_t>::max())) return std::nullopt;
    return uint32_t(multiplied / divisor);
}

bool valid_resolved_offer(const StandBlockOffer& offer, const StadiumBlockExpansionQuote& quote) {
    if (offer.percent < 6 || offer.percent > 14 || offer.base_price != quote.base_price ||
        offer.base_days != quote.base_weeks || offer.base_days < 0 || offer.base_days > 255 ||
        offer.offered_price != uint32_t(uint64_t(quote.base_price) * uint32_t(100 + offer.percent) / 100) ||
        offer.price > uint32_t(std::numeric_limits<int32_t>::max()))
        return false;
    switch (offer.decision) {
        case StandBlockOfferDecision::Declined:
            return offer.price == offer.base_price && offer.construction_days == offer.base_days;
        case StandBlockOfferDecision::Accepted:
            // PrepareStandBlockOffer forms base_price * (100 + percent) in a signed
            // 32-bit intermediate before dividing by 100. Do not accept a
            // mathematically representable final uint32 when that Source
            // intermediate would overflow. Declined offers do not use it.
            return uint64_t(quote.base_price) * uint32_t(100 + offer.percent) <=
                       uint64_t(std::numeric_limits<int32_t>::max()) &&
                   offer.price == offer.offered_price &&
                   offer.construction_days <= offer.base_days - (offer.base_days >> 3) &&
                   offer.construction_days >= offer.base_days - (offer.base_days >> 1);
        case StandBlockOfferDecision::Pending:
        default:
            return false;
    }
}
} // namespace

std::optional<StadiumBlockExpansionQuote> quote_stadium_block_expansion(
    const ModernState& g, int manager, StadiumBlockSlot slot, uint32_t total_target_places,
    uint8_t desired_features, int company_price_percent,
    const StadiumBlockExpansionConstants& constants) {
    size_t block = 0;
    if (!slot_index(slot, block) || manager < 0 ||
        size_t(manager) >= g.manager_teams.size() ||
        (size_t(manager) + 1) * kRecordSize > g.manager_records.size())
        return std::nullopt;
    const int club = g.manager_teams[size_t(manager)];
    const size_t rec = size_t(manager) * kRecordSize;
    if (club < 0 || club == no_team() || record_u16(g.manager_records, rec + kManagerClub) != uint16_t(club))
        return std::nullopt;

    const uint8_t build_weeks = g.manager_records.at(rec + kBuild + block);
    const uint8_t raw_flags = g.manager_records.at(rec + kFlags + block);
    const uint8_t current_capacity = g.manager_records.at(rec + kCapacity + block);
    const uint8_t future_capacity = g.manager_records.at(rec + kFutureCapacity + block);
    const uint32_t maximum = constants.max_places[block];
    const uint32_t unit_price = constants.price_per_1000[block];
    const uint8_t duration = constants.weeks[block];

    // Active construction and idle unknown bits are not editable A–H source states.
    if (build_weeks != 0 || (raw_flags & uint8_t(~kKnownFeatures)) != 0 ||
        (desired_features & uint8_t(~kKnownFeatures)) != 0 ||
        (desired_features & uint8_t(raw_flags & kKnownFeatures)) != uint8_t(raw_flags & kKnownFeatures) ||
        company_price_percent < 0 || company_price_percent > 427 || unit_price == 0 ||
        maximum == 0 || maximum > 255000 || maximum % 1000 != 0 || duration == 0)
        return std::nullopt;

    const uint32_t current_places = uint32_t(current_capacity) * 1000;
    if (total_target_places < current_places || total_target_places > maximum ||
        total_target_places / 1000 > 255)
        return std::nullopt;
    const uint32_t new_places = total_target_places - current_places;
    const auto new_cost = checked_component(unit_price, new_places, 1, 1);
    if (!new_cost) return std::nullopt;
    const uint32_t seats_places = (raw_flags & kSeat) != 0 ? new_places : total_target_places;
    const uint32_t roof_places = (raw_flags & kRoof) != 0 ? new_places : total_target_places;
    const auto seats_cost = (desired_features & kSeat) != 0
        ? checked_component(unit_price, seats_places, 45, 100) : std::optional<uint32_t>(0);
    const auto roof_cost = (desired_features & kRoof) != 0
        ? checked_component(unit_price, roof_places, 30, 100) : std::optional<uint32_t>(0);
    if (!seats_cost || !roof_cost) return std::nullopt;

    const uint64_t sum = uint64_t(*new_cost) + *seats_cost + *roof_cost;
    if (sum > uint64_t(std::numeric_limits<int32_t>::max()) ||
        sum * uint32_t(company_price_percent) > uint64_t(std::numeric_limits<int32_t>::max()))
        return std::nullopt;
    const uint32_t price = uint32_t(sum * uint32_t(company_price_percent) / 100);

    StadiumBlockExpansionQuote quote;
    quote.manager = manager;
    quote.manager_club = club;
    quote.slot = slot;
    quote.current_build_weeks = build_weeks;
    quote.current_raw_flags = raw_flags;
    quote.current_capacity_thousands = current_capacity;
    quote.current_future_capacity_thousands = future_capacity;
    quote.total_target_places = total_target_places;
    quote.desired_features = desired_features;
    quote.company_price_percent = company_price_percent;
    quote.price_per_1000 = unit_price;
    quote.max_places = maximum;
    quote.new_places = new_places;
    quote.new_places_cost = *new_cost;
    quote.seats_cost = *seats_cost;
    quote.roof_cost = *roof_cost;
    quote.undiscounted_cost = uint32_t(sum);
    quote.base_price = price;
    quote.base_weeks = duration;
    return quote;
}

bool build_stadium_block_expansion(
    ModernState& g, int manager, StadiumBlockSlot slot,
    const StadiumBlockExpansionConstants& constants,
    const StadiumBlockExpansionQuote& quote, const StandBlockOffer& resolved_offer,
    WatcomRandom& rand, const ConstructionNoticeHook& payment_notice,
    const SeasonTicketCompensationNoticeHook& compensation_notice) {
    if (manager != quote.manager || slot != quote.slot || quote.base_price == 0 ||
        !valid_resolved_offer(resolved_offer, quote))
        return false;
    const auto fresh = modern::quote_stadium_block_expansion(g, manager, slot, quote.total_target_places,
        quote.desired_features, quote.company_price_percent, constants);
    if (!fresh || *fresh != quote) return false;

    // Sponsor is always false for Original A–H construction. In particular this
    // preserves +0x171 and prevents pay_construction's sponsor-only W draws.
    if (!modern::pay_construction(g, manager, resolved_offer.price, false, rand, payment_notice)) return false;

    const size_t rec = size_t(manager) * kRecordSize;
    const size_t block = static_cast<uint8_t>(slot);
    g.manager_records.at(rec + kBuild + block) = resolved_offer.construction_days;
    g.manager_records.at(rec + kFlags + block) = uint8_t(
        g.manager_records.at(rec + kFlags + block) | uint8_t(quote.desired_features << 4));
    g.manager_records.at(rec + kFutureCapacity + block) = uint8_t(quote.total_target_places / 1000);
    modern::season_ticket_compensation_source(g, manager, compensation_notice);
    return true;
}

} // namespace bmh::modern
