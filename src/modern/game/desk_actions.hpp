// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/desk_actions.hpp"
// OpenBMH deviates from the original: supports designated set-piece records and the supported transfer-editor rules.
// Actions a human manager can take at his desk that change the game state and draw random
// numbers (BMH.exe V2.24 + supported rule profile patch). The screens themselves belong to the UI; these
// functions are what happens when the manager confirms.

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "game/random.hpp"
#include "modern/game/modern_state.hpp"
#include "game/human_buyer_sale.hpp"
#include "modern/game/club_player_detail.hpp"

namespace bmh::modern {


// The eight training camp resorts of BookTrainingCamp (names DeskActionsRuntimeField7).
constexpr int kTrainingCampResorts = 8;

// Resort names, qualities and prices are edition content, not shared rules.
// Install the loaded executable's tables after creating or loading a ModernState.
void configure_training_camp_data(ModernState&, const LeImage&);

// DeskDate / menu 6 friendship booking. `manager_home` selects which team is first in
// the saved pair; the manager of the home team pays the source cost. The national-team
// source-qualified overload below handles pseudo-team ratings and their W/G RNG separately.




// Source-qualified menu-6 national friendly owner. The UI/session supplies the
// revision and request identity; the engine binds them to the selected runtime nation.




// Pure source-derived quote for the pre-calendar price/venue prompt. It performs no writes
// or RNG draws. Bind this quote to the UI request ID/revision until the user chooses a day.
FriendlyBookingQuote quote_friendly_match(const ModernState& g, int manager, int opponent,
                                          bool manager_home);

// Original menu-6 national quote. A successful accepted quote advances W exactly for
// QuoteNationalFriendlyMatch's ratings. Preview runs the same calculation on a copy and never advances W.
NationalFriendlyQuote quote_national_friendly_match(const ModernState& g,
    const NationalFriendlyOwner&, int opponent, bool manager_home, WatcomRandom& rand);
NationalFriendlyQuote preview_national_friendly_match(const ModernState& g,
    const NationalFriendlyOwner&, int opponent, bool manager_home, const WatcomRandom& rand);
FriendlyDateStatus national_friendly_match_date_status(const ModernState&,
    const NationalFriendlyQuote&, const NationalFriendlyOwner&, const WatcomRandom&,
    uint16_t season_day);
NationalFriendlyBookingResult book_national_friendly_match(ModernState&,
    const NationalFriendlyBookingRequest&, const WatcomRandom&, GameRandom&);

// Conservative saved-state projection for days 0..365. `available_with_calendar_event` is
// the source color class for a selectable day whose calendar word has 0x1c0 set. The
// original calendar also carries a transient, sequentially projected bit 15 that is not in
// ModernState; dates with ambiguous neighboring calendar flags may therefore be over-rejected.
FriendlyDateStatus friendly_match_date_status(const ModernState& g,
                                               const FriendlyBookingQuote& quote,
                                               uint16_t season_day);

// Book the accepted quote on the chosen day atomically. The quote is recomputed and must
// still match; approval is explicit. A successful home booking permanently debits the fee
// and records expense-other (+0x8a0). No RNG is used for club-vs-club bookings.
FriendlyBookingResult book_friendly_match(ModernState& g, const FriendlyBookingRequest& request);

// UI-free human transfer actions (menu 17). List-entry indices address the 44-entry
// transfer table; money and contracts are stored in the normal engine state.



// Authenticated lender snapshot -> real borrower squad identity. This is preview
// only; the snapshot never becomes a listing/sale row.
std::optional<TransferMarketSnapshotPreview> transfer_market_snapshot_preview(
    const ModernState&,int manager,int runtime_club,int snapshot_index);
// TransferMarketScreen quote identity: ordinary Native slot, or an explicitly authenticated
// lender snapshot mapped by the read-only resolver above.
// No Native writes or G draws. W is0 or1 (42307 fallback). The owner stages W
// and rolls it back on unsupported/exception; repeated-hover caching belongs
// to that owner, not to this primitive or a render method.
TransferMarketPreviewResult quote_transfer_market_preview_source(
    const ModernState& g,const TransferMarketPreviewRequest& request,WatcomRandom& rand);

// Exact ordinary OpenTransferEditor/KB31 state owner. UI events contain calculator
// results in Native units; all quotes and Native row writes belong here.
TransferOfferEditorState open_transfer_offer_editor_source(ModernState&,
    const TransferOfferEditorRequest&,GameRandom&,WatcomRandom&);
TransferOfferEditorState edit_transfer_offer_editor_source(ModernState&,
    const TransferOfferEditorState&,const TransferOfferEditorEvent&,GameRandom&,WatcomRandom&);
// OpenTransferEditor TeamInfo return: 1c5b7 -> 1c186 -> 1c18b. Rescan and
// redraw the current row, preserving initial percent/G. A first empty selection
// initializes only fee/salary/term once; all seven Native bonus bytes survive.
TransferOfferEditorState resume_transfer_offer_team_info_source(ModernState&,
    const TransferOfferEditorState&,WatcomRandom&);
// OpenTransferEditor:6936..6953 resolves the direct footer's actual player slot.
// Unmanaged sellers use the Native pool slot; managed sellers use the first
// masked-ID match in their real 24/16-slot squad. The opened owner retains
// Source local40; stale retained rows reject instead of silently reselecting.
ClubPlayerDetailSelection transfer_offer_player_info_selection_source(const ModernState&,
    const TransferOfferEditorState&);
// After player information returns: zero return flags preserve the entire editor and quote;
// otherwise rescan/requote by rebuilding the quote without repeating the initial G draw.
TransferOfferEditorState resume_transfer_offer_player_info_source(ModernState&,
    const TransferOfferEditorState&,uint8_t player_info_primary_refresh,uint8_t player_info_secondary_refresh,WatcomRandom&);
TransferOfferEditorState cancel_transfer_offer_editor_source(const ModernState&,
    const TransferOfferEditorState&);
TransferOfferEditorSubmitResult submit_transfer_offer_editor_source(ModernState&,
    const TransferOfferEditorState&,WatcomRandom&,const HumanBuyerSaleHooks&,MinuteReport&);

// Submit a manager bid on an existing list entry. A full-value fee and salary meeting
// the demand complete an immediate agreement; lower viable offers persist for weekly review.
TransferActionResult submit_transfer_bid(ModernState& g, const TransferBidRequest& request, WatcomRandom& rand);

// Explicit opt-in Source SubmitTransferBidSourceHumanBuyer ordinary bid-submit path. Unlike the legacy
// overload, the caller owns staged Native/W state, supplies the exact source
// pool slot, actual career mode is present in Native, and the session provides
// its allocator/entry ownership plus Original notice presenter. Source-approved
// AI/free-agent purchases use the exact shared settlement; other sellers retain
// a pending bid. Unsupported contexts require caller rollback.
TransferActionResult submit_transfer_bid_source_human_buyer(ModernState& g,
    const TransferBidRequest& request, WatcomRandom& rand,
    const HumanBuyerSaleHooks& hooks, MinuteReport& report);

// Put one of the manager's squad rows on the list. The request consumes the original
// listing-price RNG and returns the new stable entry index through the result.
TransferActionResult list_manager_player(ModernState& g, int manager, int squad_row,
                                        WatcomRandom& rand, MinuteReport& report);


// SetTrainingCampFocus: persist the four focus sliders (+0xc1..+0xc4) without drawing or
// applying training. Rejects an invalid manager or any value outside the original 0..15 range.
bool set_training_camp_focus(ModernState& g, int manager, const std::array<uint8_t, 4>& focus);

// BookTrainingCamp controller gates, before payment and training effects.
TrainingCampBookingBlock training_camp_booking_block(const ModernState& g, int manager);

// Text: price of a stay at `resort` for the manager's team - the resort's base
// price (DeskActions_ConfigureTrainingCampData_RuntimeField2 of the loaded game) plus 5 000 per player of the squad
// (QuoteNationalFriendlyMatch with 0xff, the player count). Draws nothing.
int32_t training_camp_price(const ModernState& g, int manager, int resort);

// BookTrainingCamp on "Buchen": the price is booked (BookMoney2, not forced). If the money is
// there, record +0x890 (training camp expenses) grows by it, record +0x21f counts the stays
// (bit 7 = "already in a camp this week", cleared by the day step) and the camp takes
// effect (training_camp_effect). Returns false if the booking failed. The four focus
// sliders (record +0xc1..+0xc4, 0..15) are set on the screen before.
// The screen allows a booking only if bit 7 of +0x21f is clear and fewer than 3 stays
// were made (+0x21f & 0x7f < 3) - checked by the caller.
bool book_training_camp(ModernState& g, int manager, int resort, WatcomRandom& rand);

// OpenBMH deviates from the original: supports designated set-piece records and the supported transfer-editor rules.
// TrainingCampEffect (with the supported rule profile patch TrainingCampEffect2 / TrainingCampEffect3 / TrainingCampEffect4): the
// effect of a training camp. The team morale rises by 12 * quality * 20 % (clamped to
// 0..47), then every player of the squad may improve in condition, technique, form,
// familiarity (tactics board +0xf) and freshness (board +0x0e bits 0-5). See the .cpp.
// Ends with the message DeskActionsRuntimeField8 (SystemIdentity, no draws).
void training_camp_effect(ModernState& g, int manager, int resort, WatcomRandom& rand);

}  // namespace bmh::modern

namespace bmh::modern {

// The stock exchange screen MaxSharesToBuy (bank button). The twelve shares are entries of
// 0x5c bytes in GameState::stocks (StockExchange): +0x00 long-term value (u32, price * 100),
// +0x04 + manager * 4 the amount the manager paid for his shares (u32), +0x16 trend byte,
// +0x4e current price (u16), +0x50 + manager * 2 shares held (u16), +0x59 age of the trend.
constexpr int kStocks = 12;

// Largest number of shares of `stock` the manager may buy: (money + credit line) / price,
// with at most 9999 shares of one company.
int max_shares_to_buy(const ModernState& g, int manager, int stock);

// Buying `count` shares: the price times count plus 1.5 % fee is booked (BookMoney2, not
// forced) - but only while the paid amount of all shares held stays below 5 % of the
// money (record +0x314 / 20), else the message Bank_OriginalToUtf8_TextPointer8 ("too much") and nothing
// happens (returns false). Record +0x324 (stock balance) falls by the amount.
bool buy_shares(ModernState& g, int manager, int stock, int count);

// Selling `count` shares at the current price: the paid amount shrinks by its average per
// share, the money (forced booking) and record +0x324 rise.
void sell_shares(ModernState& g, int manager, int stock, int count);

// The "Tip" button (costs 5 000, booked forced with statistics): 0 (sell) .. 4 (buy),
// from the trend byte and the price against the long-term value, then with chance 1/3
// moved by -2, 0 or +2 (WatcomRandom(0, 2) twice). The text is Bank_FormatError_RuntimeField19[tip].
int stock_tip(ModernState& g, int manager, int stock, WatcomRandom& rand);

}  // namespace bmh::modern

namespace bmh::modern {

// The stadium screen ConstructionPricePercent (stadium button) - the parts that draw numbers or book.
// The manager record holds 14 periphery objects (+0x00..+0x0d condition, +0x1e..+0x2b
// construction weeks), eight Facility/Maintain investments (+0x0e.. condition,
// +0x2c.. construction weeks, +0x3c.. current/pending level), and eight stand blocks
// (+0x34.. construction weeks, +0x44.. seat/roof flags, +0x4c.. current capacity in
// thousands, +0x54.. future capacity). These are distinct Source branches.

// On opening: the game generator restarts from the saved seed CareerSession_Invalid_RuntimeField2 and draws the
// building company's discount, DeskActionsRuntimeField9 = 100 - G(r / 100, r / 100 + 5) with r =
// record +0x8b4 (int16). Returns the price percentage (100 - discount).
int construction_price_percent(const ModernState& g, int manager, GameRandom& game);

// The offer for periphery object `object` (0..13) when it is clicked: the game generator
// restarts from CareerSession_Invalid_RuntimeField2, draws G(0, 100) once per object before it and then the
// price percentage G(85, 115); price = base (StadiumPriceTable) * percentage / 1 000 000 *
// 10 000.
int32_t periphery_offer(const ModernState& g, int object, GameRandom& game);

// PayConstruction: pays a construction. With `sponsor` and a sponsor share (record +0x171)
// the price is reduced once - by ((share >> 5) + 3) * 10 % below a limit of ((share >> 2)
// & 7) * 325 000 + 26 000, else by WatcomRandom(2, 4) * 5 % ("Der Sponsor fühlt sich
// ausgenutzt") - and the share is cleared. Booked with statistics, not forced.

// PayConstruction presents the sponsor notice before clearing+171. A genuine
// BookMoney2 refusal presents funds after that clearing. Presenter exceptions
// propagate; the caller owns the whole transaction checkpoint/rollback.
bool pay_construction(ModernState& g, int manager, uint32_t price, bool sponsor, WatcomRandom& rand,
                      const ConstructionNoticeHook& notice = {});

// "Ja" on the offer: pay_construction(price, sponsor) and the construction time of the
// object (CareerSession_DispatchStadium_RuntimeField2[object * 5]) is entered at record +0x1e + object.
bool build_periphery(ModernState& g, int manager, int object, uint32_t price, WatcomRandom& rand,
                     const ConstructionNoticeHook& notice = {});



// PrepareStandBlockOffer, before its Yes/No prompt: consumes exactly W(6,14) and returns the
// percentage and quoted price. The quote is a value; displaying it consumes no more RNG.
// The path has not been compared with a matching original recording.
StandBlockOffer prepare_stand_block_offer(uint32_t base_price, int construction_days, WatcomRandom& rand);

// Resolve the PrepareStandBlockOffer answer once. No leaves the base price/time and draws nothing;
// Yes applies the quoted price and consumes exactly W(days>>3, days>>1) to shorten time.
// Repeated resolution is ignored, so a duplicated input cannot consume another draw.
bool resolve_stand_block_offer(StandBlockOffer& offer, bool accept, WatcomRandom& rand);

// ConstructionPricePercent choices 0..7: TEXTD734..741. Choices 8..15 are separate stand
// expansion editors and must never use the Facility/Maintain record arithmetic.
// Pure projection. Busy or terminal investments have no options. Invalid owner,
// constants or a nonzero idle high nibble raise FormatError. Heating/VIP offer
// one tier unless (raw status &7)==2, including raw0. Ordinary tiers start at
// the full raw status byte and stop before5; bit3 yields no ordinary options.
// target_level is the accepted level after weekly completion's &7 narrowing.
std::vector<StadiumInvestmentOption> stadium_investment_options_source(
    const ModernState&, int manager, StadiumInvestment, int construction_percent,
    const StadiumInvestmentConstants&);
// Revalidate the owned option against the current record and trusted Original
// constants before payment. Both resolved tip answers build: Declined means no
// tip, not cancel. Source sponsor draws/consumption can precede payment refusal;
// only successful payment writes +2c/+3c. No actual block capacity/flags
// (+34/+44/+54), GameRandom or weekly compensation is touched.
bool build_stadium_investment_source(ModernState&, int manager, StadiumInvestment,
    int construction_percent, const StadiumInvestmentConstants&,
    const StadiumInvestmentOption&, const StandBlockOffer& resolved_tip, WatcomRandom&,
    const ConstructionNoticeHook& notice = {});

// LeagueTableRangeDialog: persist the active 11-match pools coupon. The stake sits at
// Coupon + manager*0x14 and picks at +8..+0x12. Money is only charged by
// settle_pools_coupon at matchday end; this confirmation itself draws no RNG.
// BuildPoolsCoupon makes the screen unavailable unless all eleven fixtures exist.
// BalanceFromGame bounds stake to 10,000..400,000 (zero clears it).
bool submit_pools_coupon(ModernState& g, int manager, uint32_t stake,
                         const std::array<uint8_t, 11>& tips);

}  // namespace bmh::modern

namespace bmh::modern {

// Source-backed advertising contracts (DeskActionsOperation/CheckAdvertisingRenewal). Offers are
// generated elsewhere and live in the manager's saved +0x8cc pool. Projection
// is read-only: it never generates offers or consumes RNG.





// A group of identical offers may require several boards. Empty boards and rows
// marked replaceable by source flag +6 bit 7 count toward capacity.
AdvertisingOfferList advertising_offers(const ModernState& g, int manager, int display_slot);

// Accept an existing offer atomically. Board rows are source-sorted before the
// bundle is installed; the group byte at +4 receives AdGroupId and that counter
// advances once. Shirt/TV offers replace their corresponding slot. This commit
// books no immediate cash and draws no RNG. Option-extension offers are excluded.
AdvertisingOfferResult accept_advertising_offer(ModernState& g,
                                                  const AdvertisingOfferRequest& request);

// CheckAdvertisingRenewal: a click on a contract whose months ran out (sponsor set, months 0 - the
// month change posted "kann verlängert werden", Month_MonthChange_TextPointer). The sponsor's demand (word
// +8, the club attraction it wants) is scaled by W(95, 105) % (WatcomRandom); if it stays
// below the club's attraction (ClubAttraction) the manager is asked "Möchten Sie den Vertrag
// mit <sponsor> verlängern ?" (DeskActions_CheckAdvertisingRenewal_TextPointer/DeskActions_CheckAdvertisingRenewal_TextPointer2), otherwise the sponsor declines ("Die Firma
// <sponsor> wird den Vertrag nicht verlängern.", DeskActions_CheckAdvertisingRenewal_TextPointer3..DeskActions_CheckAdvertisingRenewal_TextPointer5, a mailbox message) and the
// place becomes free. Step one draws the random number and settles the refusal; step two
// applies the manager's answer: yes = the old duration again (byte +6 & 0x7f), no = free.
// Either way byte +6 is cleared and the other boards of the same bundle (group byte +4)
// get a copy of the result.
AdvertisingRenewal check_advertising_renewal(ModernState& g, int manager, int display_slot, WatcomRandom& rand);
bool answer_advertising_renewal(ModernState& g, int manager, int contract_slot, bool renew);

// CheckAdvertisingRenewal advertising spend fields (manager record +0x850/+0x854/+0x858,
// delivery countdown +0x287). Projection is read-only; only goods purchasing draws.




AdvertisingSpendingView advertising_spending(const ModernState& g, int manager, int target_slot);
AdvertisingSpendingResult set_advertising_spending(ModernState& g,
                                                    const AdvertisingSpendingRequest& request,
                                                    WatcomRandom& rand);

}  // namespace bmh::modern

namespace bmh::modern {

// The funds of the same screen (block FundBlock in GameState::memory, six entries of 0x1c
// bytes): +0x00 price of a unit ((1 << k) * 100 000), +0x04 + manager * 4 the amount the
// manager paid (u32), +0x14 + manager * 2 units held (u16, at most 10 per fund).
constexpr int kFunds = 6;

// Units of fund `fund` the manager can buy: (money + credit line) / (price * 1.05), at most
// 10 minus the units held.
int max_fund_units(const ModernState& g, int manager, int fund);

// Buying `count` units: price * count plus 5 % fee is booked (not forced) - record +0x324
// falls by it, record +0x30c rises by the fee and by price * count / 9. Every fund but
// the first costs a surcharge as well: count * G(price / 40, price / 10), paid (forced) and
// added to +0x30c, with the message CareerSession_Int32T_TextPointer2. Returns the surcharge.
int32_t buy_fund_units(ModernState& g, int manager, int fund, int count, GameRandom& game);

// Selling `count` units at the price: the paid amount shrinks by its average per unit, the
// money (forced) and +0x324 rise, +0x30c falls by price * count / 9.
void sell_fund_units(ModernState& g, int manager, int fund, int count);

// The fixed-term deposit (record +0x318): a positive amount moves money from the account
// to the deposit, a negative one back (both booked forced; +0x324 changes by the same).
// The deposit earns interest in daily_interest.
void move_to_deposit(ModernState& g, int manager, int32_t amount);

// OpenBMH deviates from the original: supports designated set-piece records and the supported transfer-editor rules.
// NormalizePitchSetPieceSource (supported rule profile patch), when the line-up screen opens (PitchScreen, desk menu 3 and
// before matches): a manager record without the set-piece marker 0x3253 at +0xb0a gets it,
// with the takers cleared (+0xafc..+0xaff = 0, +0xb08 = designated-taker signature).
void open_lineup_screen(ModernState& g, int manager);

}  // namespace bmh::modern
