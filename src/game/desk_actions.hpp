// SPDX-License-Identifier: GPL-3.0-only
#pragma once
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
#include "game/state.hpp"
#include "game/human_buyer_sale.hpp"
#include "game/club_player_detail.hpp"

namespace bmh {

struct MinuteReport;
class LeImage;

// The eight training camp resorts of BookTrainingCamp (names DeskActionsRuntimeField7).
constexpr int kTrainingCampResorts = 8;

// Resort names, qualities and prices are edition content, not shared rules.
// Install the loaded executable's tables after creating or loading a GameState.
void configure_training_camp_data(GameState&, const LeImage&);

// DeskDate / menu 6 friendship booking. `manager_home` selects which team is first in
// the saved pair; the manager of the home team pays the source cost. The national-team
// source-qualified overload below handles pseudo-team ratings and their W/G RNG separately.
enum class FriendlyQuoteStatus : uint8_t { quoted, invalid_manager, invalid_opponent, invalid_state };
enum class FriendlyDateStatus : uint8_t {
    available, available_with_calendar_event, current_or_past, unavailable,
    plan_full, invalid_quote, invalid_date
};
enum class FriendlyBookingStatus : uint8_t {
    booked, declined, opponent_declined, insufficient_funds, invalid_manager, invalid_opponent,
    invalid_date, date_unavailable, plan_full, stale_quote, invalid_request
};

struct FriendlyBookingQuote {
    FriendlyQuoteStatus status = FriendlyQuoteStatus::invalid_opponent;
    int manager = -1;
    int opponent = -1;
    int manager_team = -1;
    bool manager_home = true;
    int home_team = -1;
    int away_team = -1;
    int payer_manager = -1;
    int opponent_manager = -1;
    int32_t cost = 0;
    uint8_t planner_count = 0;
    bool operator==(const FriendlyBookingQuote&) const = default;
};

struct FriendlyBookingRequest {
    FriendlyBookingQuote quote;
    uint16_t season_day = 0;
    bool accept_opponent = false;
    int consenting_manager = -1;
    bool accept_cost = false;
    int approving_manager = -1;
};

struct FriendlyBookingResult : FriendlyBookingQuote {
    FriendlyBookingStatus status = FriendlyBookingStatus::invalid_request;
};

// Source-qualified menu-6 national friendly owner. The UI/session supplies the
// revision and request identity; the engine binds them to the selected runtime nation.
struct NationalFriendlyOwner {
    uint64_t request_id = 0;
    uint64_t revision = 0;
    int manager = -1;
    int runtime_team = kNoTeam;
    int menu_kind = -1;
    bool operator==(const NationalFriendlyOwner&) const = default;
};

struct NationalFriendlyQuote {
    FriendlyBookingQuote terms;
    NationalFriendlyOwner owner;
    int own_rating = 0;
    int opponent_rating = 0;
    uint32_t watcom_seed_before = 0;
    uint32_t watcom_seed_after = 0;
    uint64_t watcom_calls_before = 0;
    uint64_t watcom_calls_after = 0;
    bool operator==(const NationalFriendlyQuote&) const = default;
};

struct NationalFriendlyBookingRequest {
    NationalFriendlyQuote quote;
    NationalFriendlyOwner owner;
    uint16_t season_day = 0;
    bool accept_opponent = false;
    int consenting_manager = -1;
    bool accept_cost = false;
    int approving_manager = -1;
    bool operator==(const NationalFriendlyBookingRequest&) const = default;
};

struct NationalFriendlyBookingResult : FriendlyBookingResult {
    bool national_bonus_awarded = false;
};

// Pure source-derived quote for the pre-calendar price/venue prompt. It performs no writes
// or RNG draws. Bind this quote to the UI request ID/revision until the user chooses a day.
FriendlyBookingQuote quote_friendly_match(const GameState& g, int manager, int opponent,
                                          bool manager_home);

// Original menu-6 national quote. A successful accepted quote advances W exactly for
// QuoteNationalFriendlyMatch's ratings. Preview runs the same calculation on a copy and never advances W.
NationalFriendlyQuote quote_national_friendly_match(const GameState& g,
    const NationalFriendlyOwner&, int opponent, bool manager_home, WatcomRandom& rand);
NationalFriendlyQuote preview_national_friendly_match(const GameState& g,
    const NationalFriendlyOwner&, int opponent, bool manager_home, const WatcomRandom& rand);
FriendlyDateStatus national_friendly_match_date_status(const GameState&,
    const NationalFriendlyQuote&, const NationalFriendlyOwner&, const WatcomRandom&,
    uint16_t season_day);
NationalFriendlyBookingResult book_national_friendly_match(GameState&,
    const NationalFriendlyBookingRequest&, const WatcomRandom&, GameRandom&);

// Conservative saved-state projection for days 0..365. `available_with_calendar_event` is
// the source color class for a selectable day whose calendar word has 0x1c0 set. The
// original calendar also carries a transient, sequentially projected bit 15 that is not in
// GameState; dates with ambiguous neighboring calendar flags may therefore be over-rejected.
FriendlyDateStatus friendly_match_date_status(const GameState& g,
                                               const FriendlyBookingQuote& quote,
                                               uint16_t season_day);

// Book the accepted quote on the chosen day atomically. The quote is recomputed and must
// still match; approval is explicit. A successful home booking permanently debits the fee
// and records expense-other (+0x8a0). No RNG is used for club-vs-club bookings.
FriendlyBookingResult book_friendly_match(GameState& g, const FriendlyBookingRequest& request);

// UI-free human transfer actions (menu 17). List-entry indices address the 44-entry
// transfer table; money and contracts are stored in the normal engine state.
enum class TransferActionStatus : uint8_t {
    accepted, pending, rejected, offer_too_low, blocked_by_board, insufficient_funds, no_squad_room,
    invalid_manager, invalid_player, invalid_request, allocator_blocked, unsupported
};

struct TransferBidRequest {
    int manager = -1;
    int list_entry = -1;
    int player = -1;  // expected player identity; rejects a recycled list slot
    int32_t fee = 0;
    int32_t annual_salary = 0;
    uint8_t contract_years = 1;
    std::array<uint32_t, 7> bonuses{};
    // Exact absolute SquadSlots player-pool index observed by the selected market row.
    // The Source-qualified submit requires this explicitly; the legacy overload
    // ignores it and preserves its historical request contract.
    int source_slot = -1;
    // Poaching: list entries 40..43 and the SubmitTransferBidSourceHumanBuyer branches for them (immediate
    // decision, a human seller is asked, the DFB fee on a refusal; src/game/poach.hpp).
    bool poach = false;
    // OpenTransferEditor's loan checkbox is encoded in the high bit of the submitted
    // offer term, independently of its 1..4 year duration.
    bool loan = false;
    // DayStepRest2's buy-back row from Range2 is a distinct 40..43 Source entry;
    // it uses ordinary fee/qualification rules and is not poaching.
    bool loan_return_entry = false;
};

struct TransferActionResult {
    TransferActionStatus status = TransferActionStatus::invalid_request;
    int player = -1;
    int list_entry = -1;
    int32_t fee = 0;
    int32_t annual_salary = 0;
};

// Authenticated lender snapshot -> real borrower squad identity. This is preview
// only; the snapshot never becomes a listing/sale row.
struct TransferMarketSnapshotPreview {
    int snapshot_index=-1,displayed_row=-1,source_slot=-1,player=-1;
    bool operator==(const TransferMarketSnapshotPreview&) const = default;
};
std::optional<TransferMarketSnapshotPreview> transfer_market_snapshot_preview(
    const GameState&,int manager,int runtime_club,int snapshot_index);
// TransferMarketScreen quote identity: ordinary Native slot, or an explicitly authenticated
// lender snapshot mapped by the read-only resolver above.
struct TransferMarketPreviewRequest {
    int manager=-1,runtime_club=-1,source_slot=-1,expected_player=-1,seller=-1;
    bool purchase=false,loan=false;
    int snapshot_index=-1;
};
struct TransferMarketPreviewResult {
    bool supported=false;
    uint32_t fee=0,salary=0;
    int32_t delta=0,projected_balance=0;
    std::string reason;
};
// No Native writes or G draws. W is0 or1 (42307 fallback). The owner stages W
// and rolls it back on unsupported/exception; repeated-hover caching belongs
// to that owner, not to this primitive or a render method.
TransferMarketPreviewResult quote_transfer_market_preview_source(
    const GameState& g,const TransferMarketPreviewRequest& request,WatcomRandom& rand);

// Exact ordinary OpenTransferEditor/KB31 state owner. UI events contain calculator
// results in Native units; all quotes and Native row writes belong here.
struct TransferOfferEditorRequest {
    int manager=-1,runtime_club=-1,list_entry=-1,source_slot=-1,expected_player=-1;
    uint16_t expected_seller_word=0xffff;
    int expected_bid_slot=-1; // Source's first FFFF or matching own word; -1 if full.
    bool loan=false;
    // Poaching (Abwerben, Reset button 4): the special list entry 40..43 that
    // Range2 prepared for the player (src/game/poach.hpp); SubmitTransferBidSourceHumanBuyer then decides at once.
    bool poach=false;
    // Explicit owner for DayStepRest2 -> Range2 return offers in entries40..43.
    // This does not select the poaching branch or its DFB/188% rules.
    bool loan_return_entry=false;
};
struct TransferOfferInputProfile {
    uint32_t minimum=0,maximum=0,initial=0,source_text_address=0;
    std::array<std::optional<uint32_t>,8> presets{};
    bool supported=false,currency=false;
};
struct TransferOfferEditorState {
    bool supported=false,closed=false,existing=false,editable=false,quote_loan=false;
    // OpenTransferEditor local1c: set only after initializing a selected FFFF row.
    // Existing-own and full read-only openings keep it false until needed.
    bool initialized_empty_bid=false;
    std::string reason;
    TransferOfferEditorRequest request;
    int bid_slot=-1;
    std::optional<ClubPlayerDetailSelection> player_info_selection; // Source local40 retained at opening.
    uint8_t percent=0,term=0,years=1,mode=0;
    uint32_t fee=0,salary=0,asking_fee=0,asking_salary=0,quoted_fee=0,quoted_salary=0;
    std::array<uint8_t,7> bonuses{};
    std::array<TransferOfferInputProfile,2> money_profiles{};
    std::array<TransferOfferInputProfile,8> mode3_profiles{}; // years, seven bonuses
    std::array<uint8_t,0x58> native_entry{}; // stale event guard, owned copy
};
enum class TransferOfferEditorField : uint8_t { fee,salary,years,bonus0,bonus1,bonus2,bonus3,bonus4,bonus5,bonus6,toggle_quote_loan };
struct TransferOfferEditorEvent {
    TransferOfferEditorField field=TransferOfferEditorField::fee;
    uint32_t value=0;
    bool accepted=true; // calculator Cancel; fee/salary still redraw Source quote
};
TransferOfferEditorState open_transfer_offer_editor_source(GameState&,
    const TransferOfferEditorRequest&,GameRandom&,WatcomRandom&);
TransferOfferEditorState edit_transfer_offer_editor_source(GameState&,
    const TransferOfferEditorState&,const TransferOfferEditorEvent&,GameRandom&,WatcomRandom&);
// OpenTransferEditor TeamInfo return: 1c5b7 -> 1c186 -> 1c18b. Rescan and
// redraw the current row, preserving initial percent/G. A first empty selection
// initializes only fee/salary/term once; all seven Native bonus bytes survive.
TransferOfferEditorState resume_transfer_offer_team_info_source(GameState&,
    const TransferOfferEditorState&,WatcomRandom&);
// OpenTransferEditor:6936..6953 resolves the direct footer's actual player slot.
// Unmanaged sellers use the Native pool slot; managed sellers use the first
// masked-ID match in their real 24/16-slot squad. The opened owner retains
// Source local40; stale retained rows reject instead of silently reselecting.
ClubPlayerDetailSelection transfer_offer_player_info_selection_source(const GameState&,
    const TransferOfferEditorState&);
// After player information returns: zero return flags preserve the entire editor and quote;
// otherwise rescan/requote by rebuilding the quote without repeating the initial G draw.
TransferOfferEditorState resume_transfer_offer_player_info_source(GameState&,
    const TransferOfferEditorState&,uint8_t player_info_primary_refresh,uint8_t player_info_secondary_refresh,WatcomRandom&);
TransferOfferEditorState cancel_transfer_offer_editor_source(const GameState&,
    const TransferOfferEditorState&);
struct TransferOfferEditorSubmitResult {
    TransferOfferEditorState editor;
    TransferActionResult action;
};
TransferOfferEditorSubmitResult submit_transfer_offer_editor_source(GameState&,
    const TransferOfferEditorState&,WatcomRandom&,const HumanBuyerSaleHooks&,MinuteReport&);

// Submit a manager bid on an existing list entry. A full-value fee and salary meeting
// the demand complete an immediate agreement; lower viable offers persist for weekly review.
TransferActionResult submit_transfer_bid(GameState& g, const TransferBidRequest& request, WatcomRandom& rand);

// Explicit opt-in Source SubmitTransferBidSourceHumanBuyer ordinary bid-submit path. Unlike the legacy
// overload, the caller owns staged Native/W state, supplies the exact source
// pool slot, actual career mode is present in Native, and the session provides
// its allocator/entry ownership plus Original notice presenter. Source-approved
// AI/free-agent purchases use the exact shared settlement; other sellers retain
// a pending bid. Unsupported contexts require caller rollback.
TransferActionResult submit_transfer_bid_source_human_buyer(GameState& g,
    const TransferBidRequest& request, WatcomRandom& rand,
    const HumanBuyerSaleHooks& hooks, MinuteReport& report);

// Put one of the manager's squad rows on the list. The request consumes the original
// listing-price RNG and returns the new stable entry index through the result.
TransferActionResult list_manager_player(GameState& g, int manager, int squad_row,
                                        WatcomRandom& rand, MinuteReport& report);

enum class TrainingCampBookingBlock { none, booked_this_week, visit_limit, invalid_request };

// SetTrainingCampFocus: persist the four focus sliders (+0xc1..+0xc4) without drawing or
// applying training. Rejects an invalid manager or any value outside the original 0..15 range.
bool set_training_camp_focus(GameState& g, int manager, const std::array<uint8_t, 4>& focus);

// BookTrainingCamp controller gates, before payment and training effects.
TrainingCampBookingBlock training_camp_booking_block(const GameState& g, int manager);

// Text: price of a stay at `resort` for the manager's team - the resort's base
// price (DeskActions_ConfigureTrainingCampData_RuntimeField2 of the loaded game) plus 5 000 per player of the squad
// (QuoteNationalFriendlyMatch with 0xff, the player count). Draws nothing.
int32_t training_camp_price(const GameState& g, int manager, int resort);

// BookTrainingCamp on "Buchen": the price is booked (BookMoney2, not forced). If the money is
// there, record +0x890 (training camp expenses) grows by it, record +0x21f counts the stays
// (bit 7 = "already in a camp this week", cleared by the day step) and the camp takes
// effect (training_camp_effect). Returns false if the booking failed. The four focus
// sliders (record +0xc1..+0xc4, 0..15) are set on the screen before.
// The screen allows a booking only if bit 7 of +0x21f is clear and fewer than 3 stays
// were made (+0x21f & 0x7f < 3) - checked by the caller.
bool book_training_camp(GameState& g, int manager, int resort, WatcomRandom& rand);

// OpenBMH deviates from the original: supports designated set-piece records and the supported transfer-editor rules.
// TrainingCampEffect (with the supported rule profile patch TrainingCampEffect2 / TrainingCampEffect3 / TrainingCampEffect4): the
// effect of a training camp. The team morale rises by 12 * quality * 20 % (clamped to
// 0..47), then every player of the squad may improve in condition, technique, form,
// familiarity (tactics board +0xf) and freshness (board +0x0e bits 0-5). See the .cpp.
// Ends with the message DeskActionsRuntimeField8 (SystemIdentity, no draws).
void training_camp_effect(GameState& g, int manager, int resort, WatcomRandom& rand);

}  // namespace bmh

namespace bmh {

// The stock exchange screen MaxSharesToBuy (bank button). The twelve shares are entries of
// 0x5c bytes in GameState::stocks (StockExchange): +0x00 long-term value (u32, price * 100),
// +0x04 + manager * 4 the amount the manager paid for his shares (u32), +0x16 trend byte,
// +0x4e current price (u16), +0x50 + manager * 2 shares held (u16), +0x59 age of the trend.
constexpr int kStocks = 12;

// Largest number of shares of `stock` the manager may buy: (money + credit line) / price,
// with at most 9999 shares of one company.
int max_shares_to_buy(const GameState& g, int manager, int stock);

// Buying `count` shares: the price times count plus 1.5 % fee is booked (BookMoney2, not
// forced) - but only while the paid amount of all shares held stays below 5 % of the
// money (record +0x314 / 20), else the message Bank_OriginalToUtf8_TextPointer8 ("too much") and nothing
// happens (returns false). Record +0x324 (stock balance) falls by the amount.
bool buy_shares(GameState& g, int manager, int stock, int count);

// Selling `count` shares at the current price: the paid amount shrinks by its average per
// share, the money (forced booking) and record +0x324 rise.
void sell_shares(GameState& g, int manager, int stock, int count);

// The "Tip" button (costs 5 000, booked forced with statistics): 0 (sell) .. 4 (buy),
// from the trend byte and the price against the long-term value, then with chance 1/3
// moved by -2, 0 or +2 (WatcomRandom(0, 2) twice). The text is Bank_FormatError_RuntimeField19[tip].
int stock_tip(GameState& g, int manager, int stock, WatcomRandom& rand);

}  // namespace bmh

namespace bmh {

// The stadium screen ConstructionPricePercent (stadium button) - the parts that draw numbers or book.
// The manager record holds 14 periphery objects (+0x00..+0x0d condition, +0x1e..+0x2b
// construction weeks), eight Facility/Maintain investments (+0x0e.. condition,
// +0x2c.. construction weeks, +0x3c.. current/pending level), and eight stand blocks
// (+0x34.. construction weeks, +0x44.. seat/roof flags, +0x4c.. current capacity in
// thousands, +0x54.. future capacity). These are distinct Source branches.

// On opening: the game generator restarts from the saved seed CareerSession_Invalid_RuntimeField2 and draws the
// building company's discount, DeskActionsRuntimeField9 = 100 - G(r / 100, r / 100 + 5) with r =
// record +0x8b4 (int16). Returns the price percentage (100 - discount).
int construction_price_percent(const GameState& g, int manager, GameRandom& game);

// The offer for periphery object `object` (0..13) when it is clicked: the game generator
// restarts from CareerSession_Invalid_RuntimeField2, draws G(0, 100) once per object before it and then the
// price percentage G(85, 115); price = base (StadiumPriceTable) * percentage / 1 000 000 *
// 10 000.
int32_t periphery_offer(const GameState& g, int object, GameRandom& game);

// PayConstruction: pays a construction. With `sponsor` and a sponsor share (record +0x171)
// the price is reduced once - by ((share >> 5) + 3) * 10 % below a limit of ((share >> 2)
// & 7) * 325 000 + 26 000, else by WatcomRandom(2, 4) * 5 % ("Der Sponsor fühlt sich
// ausgenutzt") - and the share is cleared. Booked with statistics, not forced.
enum class ConstructionNoticeKind { SponsorContribution, SponsorExploited, InsufficientFunds };
struct ConstructionNotice {
    int manager = -1;
    ConstructionNoticeKind kind = ConstructionNoticeKind::InsufficientFunds;
    uint32_t gross_price = 0, subsidy = 0;
    int percent = 0;
};
using ConstructionNoticeHook = std::function<void(const GameState&, const ConstructionNotice&)>;

// PayConstruction presents the sponsor notice before clearing+171. A genuine
// BookMoney2 refusal presents funds after that clearing. Presenter exceptions
// propagate; the caller owns the whole transaction checkpoint/rollback.
bool pay_construction(GameState& g, int manager, uint32_t price, bool sponsor, WatcomRandom& rand,
                      const ConstructionNoticeHook& notice = {});

// "Ja" on the offer: pay_construction(price, sponsor) and the construction time of the
// object (CareerSession_DispatchStadium_RuntimeField2[object * 5]) is entered at record +0x1e + object.
bool build_periphery(GameState& g, int manager, int object, uint32_t price, WatcomRandom& rand,
                     const ConstructionNoticeHook& notice = {});

enum class StandBlockOfferDecision : uint8_t { Pending, Declined, Accepted };

struct StandBlockOffer {
    int percent = 0;
    uint32_t base_price = 0;
    uint32_t offered_price = 0;  // quoted +percent price; the amount shown before Yes/No
    uint32_t price = 0;          // amount to pay: base price until Accepted
    int base_days = 0;
    int construction_days = 0;   // original duration until Accepted
    StandBlockOfferDecision decision = StandBlockOfferDecision::Pending;
};

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
enum class StadiumInvestment : uint8_t {
    Floodlights = 0, Scoreboard, PitchHeating, VipLounge, Pitch, Comfort, Condition, Safety
};
struct StadiumInvestmentConstants {
    std::array<uint32_t, 8> price{};  // EXE CareerSession_DispatchStadium_RuntimeField[choice], not CareerSession_DispatchStadium_RuntimeField[choice+8]
    std::array<uint8_t, 8> weeks{};   // EXE CareerSession_DispatchStadium_RuntimeField2[(choice+14)*5]
};
struct StadiumInvestmentOption {
    uint8_t current_status = 0, current_level = 0, increment = 0, target_level = 0;
    uint32_t base_price = 0;
    uint8_t base_weeks = 0;
    bool operator==(const StadiumInvestmentOption&) const = default;
};
// Pure projection. Busy or terminal investments have no options. Invalid owner,
// constants or a nonzero idle high nibble raise FormatError. Heating/VIP offer
// one tier unless (raw status &7)==2, including raw0. Ordinary tiers start at
// the full raw status byte and stop before5; bit3 yields no ordinary options.
// target_level is the accepted level after weekly completion's &7 narrowing.
std::vector<StadiumInvestmentOption> stadium_investment_options_source(
    const GameState&, int manager, StadiumInvestment, int construction_percent,
    const StadiumInvestmentConstants&);
// Revalidate the owned option against the current record and trusted Original
// constants before payment. Both resolved tip answers build: Declined means no
// tip, not cancel. Source sponsor draws/consumption can precede payment refusal;
// only successful payment writes +2c/+3c. No actual block capacity/flags
// (+34/+44/+54), GameRandom or weekly compensation is touched.
bool build_stadium_investment_source(GameState&, int manager, StadiumInvestment,
    int construction_percent, const StadiumInvestmentConstants&,
    const StadiumInvestmentOption&, const StandBlockOffer& resolved_tip, WatcomRandom&,
    const ConstructionNoticeHook& notice = {});

// LeagueTableRangeDialog: persist the active 11-match pools coupon. The stake sits at
// Coupon + manager*0x14 and picks at +8..+0x12. Money is only charged by
// settle_pools_coupon at matchday end; this confirmation itself draws no RNG.
// BuildPoolsCoupon makes the screen unavailable unless all eleven fixtures exist.
// BalanceFromGame bounds stake to 10,000..400,000 (zero clears it).
bool submit_pools_coupon(GameState& g, int manager, uint32_t stake,
                         const std::array<uint8_t, 11>& tips);

}  // namespace bmh

namespace bmh {

// Source-backed advertising contracts (DeskActionsOperation/CheckAdvertisingRenewal). Offers are
// generated elsewhere and live in the manager's saved +0x8cc pool. Projection
// is read-only: it never generates offers or consumes RNG.
enum class AdvertisingOfferStatus : uint8_t {
    available, accepted, invalid_manager, invalid_display_slot, invalid_state, no_offers,
    unavailable_target, unsupported_extension, insufficient_boards, stale_offer,
    malformed_offer, invalid_request
};

struct AdvertisingOfferCandidate {
    int raw_slot = -1;                       // saved pool index, 0..9 (TV: 0..4)
    std::array<uint8_t, 12> bytes{};          // complete source offer record
    uint32_t monthly_amount = 0;              // DWORD +0
    uint8_t bundle_units = 0;                 // byte +4 (DWORD word 1, low byte)
    uint8_t months = 0;                       // byte +5
    uint8_t sponsor_id = 0xff;                // byte +7
    bool present = false;
    bool can_accept = false;
};

struct AdvertisingOfferList {
    AdvertisingOfferStatus status = AdvertisingOfferStatus::invalid_request;
    int manager = -1;
    int display_slot = -1;                   // 0..13 boards, 14 shirt, 15 television
    int contract_slot = -1;                   // physical manager-record row backing display_slot
    std::array<uint8_t, 12> contract_bytes{};  // exact selected row for request binding
    int pool_group = -1;                     // 0 boards, 1 shirt, 2 television
    int free_boards = 0;
    std::vector<AdvertisingOfferCandidate> offers;
};

struct AdvertisingOfferRequest {
    int manager = -1;
    int display_slot = -1;                   // screen row after source sort (0..15)
    int contract_slot = -1;                   // physical row identity from the UI projection
    std::array<uint8_t, 12> expected_contract{};
    int raw_slot = -1;
    std::array<uint8_t, 12> expected_offer{}; // binds UI choice to the projected offer
    bool operator==(const AdvertisingOfferRequest&) const = default;
};

struct AdvertisingOfferResult {
    AdvertisingOfferStatus status = AdvertisingOfferStatus::invalid_request;
    int manager = -1;
    int display_slot = -1;
    int contract_slot = -1;
    int raw_slot = -1;
};

// A group of identical offers may require several boards. Empty boards and rows
// marked replaceable by source flag +6 bit 7 count toward capacity.
AdvertisingOfferList advertising_offers(const GameState& g, int manager, int display_slot);

// Accept an existing offer atomically. Board rows are source-sorted before the
// bundle is installed; the group byte at +4 receives AdGroupId and that counter
// advances once. Shirt/TV offers replace their corresponding slot. This commit
// books no immediate cash and draws no RNG. Option-extension offers are excluded.
AdvertisingOfferResult accept_advertising_offer(GameState& g,
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
struct AdvertisingRenewal {
    enum class Status : uint8_t { invalid, not_expired, declined_by_sponsor, offered } status = Status::invalid;
    int contract_slot = -1;  // 0-13 boards, 14 shirt, 15 TV
    std::string question;    // the original question text (offered)
    std::string notice;      // the sponsor's refusal (declined_by_sponsor)
};
AdvertisingRenewal check_advertising_renewal(GameState& g, int manager, int display_slot, WatcomRandom& rand);
bool answer_advertising_renewal(GameState& g, int manager, int contract_slot, bool renew);

// CheckAdvertisingRenewal advertising spend fields (manager record +0x850/+0x854/+0x858,
// delivery countdown +0x287). Projection is read-only; only goods purchasing draws.
struct AdvertisingSpendingView {
    int manager = -1;
    int target_slot = -1; // 16 merchandise, 17 press budget, 18 fan-work budget
    uint32_t current_amount = 0;
    uint32_t maximum_amount = 0;
    uint8_t delivery_weeks = 0;
    bool available = false;
};

enum class AdvertisingSpendingStatus : uint8_t {
    accepted, no_change, insufficient_funds, delivery_pending,
    invalid_manager, invalid_target, invalid_amount, stale_request, invalid_state
};

struct AdvertisingSpendingRequest {
    int manager = -1;
    int target_slot = -1;
    uint32_t amount = 0;
    uint32_t expected_amount = 0;
    uint8_t expected_delivery_weeks = 0;
    bool operator==(const AdvertisingSpendingRequest&) const = default;
};

struct AdvertisingSpendingResult {
    AdvertisingSpendingStatus status = AdvertisingSpendingStatus::invalid_state;
    bool changed = false;
    uint32_t message_address = 0;
};

AdvertisingSpendingView advertising_spending(const GameState& g, int manager, int target_slot);
AdvertisingSpendingResult set_advertising_spending(GameState& g,
                                                    const AdvertisingSpendingRequest& request,
                                                    WatcomRandom& rand);

}  // namespace bmh

namespace bmh {

// The funds of the same screen (block FundBlock in GameState::memory, six entries of 0x1c
// bytes): +0x00 price of a unit ((1 << k) * 100 000), +0x04 + manager * 4 the amount the
// manager paid (u32), +0x14 + manager * 2 units held (u16, at most 10 per fund).
constexpr int kFunds = 6;

// Units of fund `fund` the manager can buy: (money + credit line) / (price * 1.05), at most
// 10 minus the units held.
int max_fund_units(const GameState& g, int manager, int fund);

// Buying `count` units: price * count plus 5 % fee is booked (not forced) - record +0x324
// falls by it, record +0x30c rises by the fee and by price * count / 9. Every fund but
// the first costs a surcharge as well: count * G(price / 40, price / 10), paid (forced) and
// added to +0x30c, with the message CareerSession_Int32T_TextPointer2. Returns the surcharge.
int32_t buy_fund_units(GameState& g, int manager, int fund, int count, GameRandom& game);

// Selling `count` units at the price: the paid amount shrinks by its average per unit, the
// money (forced) and +0x324 rise, +0x30c falls by price * count / 9.
void sell_fund_units(GameState& g, int manager, int fund, int count);

// The fixed-term deposit (record +0x318): a positive amount moves money from the account
// to the deposit, a negative one back (both booked forced; +0x324 changes by the same).
// The deposit earns interest in daily_interest.
void move_to_deposit(GameState& g, int manager, int32_t amount);

// OpenBMH deviates from the original: supports designated set-piece records and the supported transfer-editor rules.
// NormalizePitchSetPieceSource (supported rule profile patch), when the line-up screen opens (PitchScreen, desk menu 3 and
// before matches): a manager record without the set-piece marker 0x3253 at +0xb0a gets it,
// with the takers cleared (+0xafc..+0xaff = 0, +0xb08 = designated-taker signature).
void open_lineup_screen(GameState& g, int manager);

}  // namespace bmh
