// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/world_layout.hpp"
#include "ui/incoming_transfer.hpp"
#include "ui/game_end.hpp"

#include <algorithm>
#include <sstream>

#include "formats/textd.hpp"
#include "game/state.hpp"
#include "game/weekly.hpp"
#include "ui/manager.hpp"
#include "ui/pending_offer.hpp"
#include "ui/setup.hpp"

namespace bmh::ui {
namespace {
constexpr uint32_t kTitleText = ::bmh::layout::TitleText;
constexpr uint32_t kPurchaseTitleText = ::bmh::layout::PurchaseTitleText;
constexpr uint32_t kLoanTitleText = ::bmh::layout::LoanTitleText;
constexpr uint32_t kTextBase = ::bmh::layout::TextPointerTable;
constexpr size_t kManagerRecord = 0xb0c;

std::string_view text_at(const TextDb& texts, uint32_t address) {
    if (address < kTextBase || (address - kTextBase) % 4 != 0)
        throw FormatError("incoming transfer: invalid original text address");
    return texts[(address - kTextBase) / 4];
}

void copy_region(Image& target, const Image& source, int sx, int sy, int x, int y,
                 int width, int height, int transparent_index = -1) {
    if (sx < 0 || sy < 0 || x < 0 || y < 0 || width < 0 || height < 0 ||
        sx + width > source.width || sy + height > source.height ||
        x + width > target.width || y + height > target.height)
        throw FormatError("incoming transfer: invalid original sprite bounds");
    for (int yy = 0; yy < height; ++yy) for (int xx = 0; xx < width; ++xx) {
        const auto value = source.pixels[size_t(sy + yy) * size_t(source.width) + size_t(xx + sx)];
        if (transparent_index < 0 || value != transparent_index) target.at(x + xx, y + yy) = value;
    }
}

std::string source_number(uint32_t value, int width, bool grouping = true) {
    std::string digits = std::to_string(value);
    const int actual = int(digits.size());
    const int padding = std::max(0, width - actual);
    digits.insert(0, size_t(padding), '%');
    if (grouping) {
        for (int from = actual - 3; from > 0; from -= 3)
            digits.insert(size_t(padding + from), ".");
        const int wanted = width > 3 ? (width - 4) / 3 + 1 : 0;
        const int used = actual > 3 ? (actual - 1) / 3 : 0;
        if (wanted > used) digits.insert(0, size_t(wanted - used), ' ');
    }
    return digits;
}

std::string source_money(uint32_t value, int width, uint32_t numerator, uint32_t denominator) {
    if (denominator != 0)
        value = uint32_t((uint64_t(value) * uint64_t(numerator)) / uint64_t(denominator));
    return source_number(value, width, true) + " EU";
}

int team_badge(const GameState& game, int team) {
    if (team < 0 || team >= 168 || size_t(team) * 2 + 2 > game.club_heap_78.size()) return -1;
    return (int(game.club_heap_78[size_t(team) * 2]) |
            int(game.club_heap_78[size_t(team) * 2 + 1]) << 8) & 0x1ff;
}

int manager_badge(const GameState& game, int manager) {
    if (manager < 0 || size_t(manager) >= game.manager_teams.size() ||
        size_t(manager) * kManagerRecord + 0x223 > game.manager_records.size()) return -1;
    const int team = game.manager_teams[size_t(manager)];
    if (team < 0 || size_t(team) * 2 + 2 > game.club_heap_78.size()) return -1;
    return team_badge(game, team);
}

int source_seller_team_token(const IncomingTransferDecisionState& state) {
    const int token = state.seller_team_token >= 0 ? state.seller_team_token : state.seller;
    return token >= 0 ? (token & 0x0fff) : token;
}

bool source_neutral_seller_token(int token) {
    return (token >= 90 && token <= 137) || token == 0x0fff ||
           (token >= 168 && token <= 356);
}
} // namespace

namespace {
IncomingTransferDecisionState incoming_transfer_decision_from_game_impl(
    const GameState& game, const IncomingTransferDecisionView& view, bool pending_offer_source) {
    // the outgoing offer editor also runs on the special rows 40..43 when poaching
    // (src/game/poach.hpp)
    const bool entry_valid = pending_offer_source
        ? view.list_entry >= 40 && view.list_entry < 44
        : view.list_entry >= 0 && view.list_entry < (view.source_outgoing_editor_context ? 44 : 40);
    if (!view.valid || !entry_valid ||
        view.player < 0 || size_t(view.player) >= game.players.size() ||
        (view.seller != 0x0fff && view.seller != no_team() && (view.seller < 0 || size_t(view.seller) >= game.club_ids.size())) ||
        view.seller_manager < -1 || view.viewer_manager < -1 ||
        view.preferred_bid_index < -1 || view.preferred_bid_index >= 4)
        throw FormatError("incoming transfer: invalid decision context");

    IncomingTransferDecisionState out;
    out.pending_offer_source = pending_offer_source;
    out.source_outgoing_editor_context = view.source_outgoing_editor_context;
    out.list_entry = view.list_entry;
    out.player = view.player;
    out.seller = view.seller;
    out.seller_team_token = view.seller;
    out.seller_manager = view.seller_manager;
    out.viewer_manager = view.viewer_manager;
    out.preferred_bid_index = view.preferred_bid_index;
    out.has_preferred_bid = view.has_preferred_bid;
    out.preferred_bid_passed_random_check = view.preferred_bid_passed_random_check;
    out.player_name = game.player_name(view.player);
    out.seller_name = (view.seller == 0x0fff || view.seller == no_team()) ? std::string() : game.club_name(view.seller);
    // ClubName2 derives this label from the seller team token. A club is
    // named directly; the player-pool token resolves to the original adviser.
    out.negotiator_name = (view.seller == 0x0fff || view.seller == no_team())
        ? (game.texts ? (*game.texts)[925] : "SPIELERBERATER")
        : out.seller_name;
    out.age = game.players[size_t(view.player)].age();
    out.position = game.players[size_t(view.player)].position();
    out.strength = game.players[size_t(view.player)].strength();
    out.asking_fee = view.asking_fee;
    out.asking_monthly_salary = view.asking_monthly_salary;
    out.quoted_fee = view.quoted_fee;
    out.quoted_monthly_salary = view.quoted_monthly_salary;
    if (game.memory.has(::bmh::layout::CurrencyMultiplier)) out.currency_numerator = game.memory.u32(::bmh::layout::CurrencyMultiplier);
    if (game.memory.has(::bmh::layout::CurrencyDivisor)) out.currency_denominator = game.memory.u32(::bmh::layout::CurrencyDivisor);
    out.quoted_years = view.quoted_years;
    out.notice_text = view.notice_text;
    out.notice_text_address = view.notice_text_address;
    out.offers = view.offers;
    out.seller_badge_sprite = team_badge(game, view.seller);
    const int manager = view.viewer_manager >= 0 ? view.viewer_manager : view.seller_manager;
    out.viewer_manager = manager;
    if (manager >= 0) {
        if ((!pending_offer_source && size_t(manager) >= game.manager_teams.size()) ||
            size_t(manager) * kManagerRecord + 0x223 > game.manager_records.size())
            throw FormatError("incoming transfer: invalid viewer manager");
        const int pending_team = pending_offer_source ? pending_offer_owner_team(game, manager) : -1;
        const size_t name_at = size_t(manager) * kManagerRecord + 0xa8;
        const size_t end = std::min(name_at + 26, game.manager_records.size());
        size_t nul = name_at;
        while (nul < end && game.manager_records[nul] != 0) ++nul;
        out.viewer_name.assign(reinterpret_cast<const char*>(game.manager_records.data() + name_at), nul - name_at);
        out.manager_portrait = game.manager_records[size_t(manager) * kManagerRecord + 0x221];
        // The KB31 panel uses its static manager artwork and does not draw
        // this portrait. Source notices can use portrait 12..14 as well.
        if (out.manager_portrait > ((pending_offer_source || out.source_outgoing_editor_context) ? 14 : 11))
            throw FormatError("incoming transfer: invalid viewer portrait");
        out.notice_badge_fallback=source_badge_fallback(pending_offer_source?pending_team:game.manager_teams[size_t(manager)]);
        out.badge_sprite = pending_offer_source ? team_badge(game, pending_team) : manager_badge(game, manager);
    }
    for (int i = 0; i < 4; ++i) {
        auto& offer = out.offers[size_t(i)];
        if (offer.present) {
            const bool spare_buyer = pending_offer_source && i == 0 && offer.buyer_manager == 4 &&
                pending_offer_spare_buyer(game, view.list_entry - 40, offer.buyer);
            if (offer.bid_index != i || offer.buyer < 0 || size_t(offer.buyer) >= game.club_ids.size() ||
                offer.buyer_manager < -1 || (!spare_buyer && offer.buyer_manager >= int(game.manager_teams.size())) ||
                offer.contract_months == 0 || offer.contract_months > 84)
                throw FormatError("incoming transfer: invalid raw bid slot");
            out.buyer_names[size_t(i)] = game.club_name(offer.buyer);
        } else if (offer.bid_index != -1 && offer.bid_index != i) {
            throw FormatError("incoming transfer: empty slot has mismatched identity");
        }
    }
    if (out.preferred_bid_index >= 0 && !out.offers[size_t(out.preferred_bid_index)].present)
        throw FormatError("incoming transfer: preferred bid slot is empty");
    if (pending_offer_source) {
        if (!out.offers[0].present || out.offers[0].bid_index != 0 ||
            std::any_of(out.offers.begin() + 1, out.offers.end(),
                [](const IncomingTransferOfferSlot& offer) { return offer.present; }))
            throw FormatError("incoming transfer: pending Source entry must contain only raw bid0");
    }
    return out;
}
} // namespace

IncomingTransferDecisionState incoming_transfer_decision_from_game(
    const GameState& game, const IncomingTransferDecisionView& view) {
    return incoming_transfer_decision_from_game_impl(game, view, false);
}

IncomingTransferDecisionState incoming_transfer_pending_decision_from_game(
    const GameState& game, const TransferOfferListView& source) {
    if (!source.valid || source.list_entry < 40 || source.list_entry >= 44 ||
        source.seller_manager < 0 ||
        source.offer_count != 1 || !source.offers[0].present || source.offers[0].bid_index != 0 ||
        std::any_of(source.offers.begin() + 1, source.offers.end(),
            [](const TransferOfferCandidateView& offer) { return offer.present; }))
        throw FormatError("incoming transfer: invalid DispatchClubPlayerPendingOffers special entry");
    IncomingTransferDecisionView view;
    view.valid = source.valid;
    view.list_entry = source.list_entry;
    view.player = source.player;
    view.seller = source.seller;
    view.seller_manager = source.seller_manager;
    view.viewer_manager = source.seller_manager;
    view.preferred_bid_index = source.preferred_bid_index;
    view.has_preferred_bid = source.preferred_bid_index >= 0;
    view.preferred_bid_passed_random_check = source.preferred_bid_passed_random_check;
    view.asking_fee = source.asking_fee;
    view.asking_monthly_salary = source.asking_monthly_salary;
    view.quoted_fee = source.quoted_fee;
    view.quoted_monthly_salary = source.quoted_monthly_salary;
    view.quoted_years = source.quoted_years;
    // DispatchClubPlayerPendingOffers owns pending-action notices through PendingOfferHooks::notice.
    // Its Source continuation redraws the chooser after SDLFlushEvents3, so this
    // special adapter must not create the generic retry overlay a second time.
    view.notice_text_address = 0;
    for (size_t i = 0; i < view.offers.size(); ++i) {
        const auto& from = source.offers[i];
        auto& to = view.offers[i];
        to.bid_index = from.bid_index;
        to.buyer = from.buyer;
        to.buyer_manager = from.buyer_manager;
        to.fee = from.fee;
        to.monthly_salary = from.monthly_salary;
        to.fee_reference = from.fee_reference;
        to.salary_reference = from.salary_reference;
        to.contract_months = from.contract_months;
        to.loan = from.loan;
        to.bonuses = from.bonuses;
        to.present = from.present;
        to.eligible = from.eligible;
        to.transaction_supported = from.transaction_supported;
    }
    return incoming_transfer_decision_from_game_impl(game, view, true);
}

std::string incoming_transfer_decision_json(const IncomingTransferDecisionState& state) {
    std::ostringstream out;
    out << "{\"screen\":\"incoming_transfer_decision\",\"pending_offer_source\":"
        << (state.pending_offer_source ? "true" : "false") << ",\"list_entry\":" << state.list_entry
        << ",\"title\":" << unsigned(state.title)
        << ",\"source_outgoing_editor_context\":" <<
            (state.source_outgoing_editor_context ? "true" : "false")
        << ",\"player\":" << state.player << ",\"player_name\":" << json_quote(original_to_utf8(state.player_name))
        << ",\"seller\":" << state.seller << ",\"seller_name\":" << json_quote(original_to_utf8(state.seller_name))
        << ",\"seller_team_token\":" << state.seller_team_token
        << ",\"seller_manager\":" << state.seller_manager
        << ",\"viewer_manager\":" << state.viewer_manager
        << ",\"manager_portrait\":" << state.manager_portrait
        << ",\"badge_sprite\":" << state.badge_sprite
        << ",\"seller_badge_sprite\":" << state.seller_badge_sprite
        << ",\"age\":" << state.age << ",\"position\":" << state.position
        << ",\"strength\":" << state.strength
        << ",\"notice_text_address\":" << state.notice_text_address
        << ",\"preferred_bid_index\":" << state.preferred_bid_index
        << ",\"has_preferred_bid\":" << (state.has_preferred_bid ? "true" : "false")
        << ",\"preferred_bid_passed_random_check\":"
        << (state.preferred_bid_passed_random_check ? "true" : "false")
        << ",\"negotiator_name\":" << json_quote(original_to_utf8(state.negotiator_name))
        << ",\"asking_fee\":" << state.asking_fee << ",\"asking_monthly_salary\":" << state.asking_monthly_salary
        << ",\"quoted_fee\":" << state.quoted_fee << ",\"quoted_monthly_salary\":" << state.quoted_monthly_salary
        << ",\"currency_numerator\":" << state.currency_numerator
        << ",\"currency_denominator\":" << state.currency_denominator
        << ",\"quoted_years\":" << unsigned(state.quoted_years) << ",\"offers\":[";
    for (size_t i = 0; i < state.offers.size(); ++i) {
        if (i) out << ',';
        const auto& offer = state.offers[i];
        out << "{\"bid_index\":" << offer.bid_index << ",\"present\":" << (offer.present ? "true" : "false")
            << ",\"buyer\":" << offer.buyer << ",\"buyer_name\":" << json_quote(original_to_utf8(state.buyer_names[i]))
            << ",\"buyer_manager\":" << offer.buyer_manager << ",\"fee\":" << offer.fee
            << ",\"monthly_salary\":" << offer.monthly_salary << ",\"fee_reference\":" << offer.fee_reference
            << ",\"salary_reference\":" << offer.salary_reference << ",\"contract_months\":" << unsigned(offer.contract_months)
            << ",\"loan\":" << (offer.loan ? "true" : "false") << ",\"eligible\":" << (offer.eligible ? "true" : "false")
            << ",\"transaction_supported\":" << (offer.transaction_supported ? "true" : "false")
            << ",\"bonuses\":[";
        for (size_t bonus = 0; bonus < offer.bonuses.size(); ++bonus) {
            if (bonus) out << ',';
            out << unsigned(offer.bonuses[bonus]);
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}

IncomingTransferScreen::IncomingTransferScreen(const std::filesystem::path& data,
                                               IncomingTransferDecisionState state,
                                               bool gradient_text)
    : state_(std::move(state)), exe_(load_le(asset_path(data, "BMH.exe"))),
      texts_(load_textd(asset_path(data, "TEXTD.BLK"))),
      body_(load_font(asset_path(data / "F", "BMH3.FNT"))),
      heading_(load_font(asset_path(data / "F", "BMH2.FNT"))),
      sprite_font_(data),
      background_(load_vga(asset_path(data / "KB", "129.VGA"))),
      negotiation_(load_vga(asset_path(data / "KB", "31.VGA"))),
      gradient_text_(gradient_text), widgets_(load_vga(asset_path(data / "KB", "0.VGA"))),
      faces_(load_vga(asset_path(data / "KB", "26.VGA"))) {
    const bool entry_valid = state_.pending_offer_source
        ? state_.list_entry >= 40 && state_.list_entry < 44
        : state_.list_entry >= 0 && state_.list_entry < (state_.source_outgoing_editor_context ? 44 : 40);  // poaching rows
    if (!entry_valid || state_.player < 0 || state_.player_name.empty() ||
        background_.width != 320 || background_.height != 240 || negotiation_.width != 320 || negotiation_.height != 206 ||
        state_.manager_portrait > ((state_.pending_offer_source || state_.source_outgoing_editor_context) ? 14 : 11) ||
        (state_.preferred_bid_index >= 0 && !state_.offers[size_t(state_.preferred_bid_index)].present))
        throw FormatError("incoming transfer: invalid owned view or original assets");
    if (state_.pending_offer_source && (!state_.offers[0].present || state_.offers[0].bid_index != 0 ||
        std::any_of(state_.offers.begin() + 1, state_.offers.end(),
            [](const IncomingTransferOfferSlot& offer) { return offer.present; })))
        throw FormatError("incoming transfer: pending Source view must contain only raw bid0");
    badge_atlas_=state_.notice_badge_fallback||state_.badge_sprite<0?widgets_:
        load_vga(asset_path(data/"W",std::to_string(state_.badge_sprite>>3)+".VGA"));
    // SourceBadgeFallback chooses the neutral fallback from the raw team token before
    // indexing ClubBadgeSelectors. A real selector can itself be 90..137 and must still
    // load its W atlas, so do not classify seller art by selector value.
    seller_badge_atlas_ = source_neutral_seller_token(source_seller_team_token(state_)) ||
        state_.seller_badge_sprite < 0 ? widgets_ :
        load_vga(asset_path(data / "W", std::to_string(state_.seller_badge_sprite >> 3) + ".VGA"));
    notice_active_ = !state_.notice_text.empty();
    if (!notice_active_ && state_.notice_text_address != 0) {
        state_.notice_text = text_at(texts_, state_.notice_text_address);
        notice_active_ = !state_.notice_text.empty();
    }
}

void IncomingTransferScreen::draw_body(Image& out, int x, int baseline, std::string_view text,
                                       uint8_t color) const {
    draw_top(out, x, baseline - body_.baseline, text, color);
}

void IncomingTransferScreen::draw_top(Image& out, int x, int y, std::string_view text,
                                      uint8_t color) const {
    if (text.empty()) return;
    const int top = y;
    const int width = body_.text_width(text);
    if (x < 0 || top < 0 || x + width > out.width || top + body_.height > out.height) return;
    if (!gradient_text_ || exe_.u8(::bmh::layout::TextGradientMode) != 2) { body_.draw(out, x, top, text, color); return; }
    Image mask(std::max(1, width), body_.height);
    body_.draw(mask, 0, 0, text, 1);
    int gradient = 0;
    while (gradient < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + uint32_t(gradient)) != color) ++gradient;
    for (int yy = 0; yy < body_.height; ++yy)
        for (int xx = 0; xx < width; ++xx)
            if (mask.at(xx, yy)) out.at(x + xx, top + yy) =
                gradient < 32 ? exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + uint32_t(gradient * 6 + std::min(yy, 5))) : color;
}

void IncomingTransferScreen::draw_heading(Image& out, int x, int baseline, std::string_view text,
                                          uint8_t color) const {
    const int top = baseline - heading_.baseline;
    if (x < 0 || top < 0 || x + heading_.text_width(text) > out.width || top + heading_.height > out.height) return;
    heading_.draw(out, x, top, text, color);
}

Image IncomingTransferScreen::render() const { return render_at_elapsed(0); }

void IncomingTransferScreen::begin_outcome(int row_count, int accepted_bid_index, uint8_t outcome_delay_disabled) {
    if (row_count < 0 || row_count > int(state_.offers.size()) || accepted_bid_index < -1 ||
        accepted_bid_index >= row_count ||
        (accepted_bid_index >= 0 && !state_.offers[size_t(accepted_bid_index)].present))
        throw FormatError("incoming transfer: invalid passive outcome");
    outcome_row_count_ = row_count;
    outcome_accepted_bid_index_ = accepted_bid_index;
    outcome_speed_setting_ = outcome_delay_disabled;
    outcome_dismissed_ = false;
}

uint64_t IncomingTransferScreen::outcome_duration_ms() const {
    if (outcome_row_count_ < 0) return 0;
    const int rows = outcome_accepted_bid_index_ >= 0
        ? outcome_accepted_bid_index_ + 1 : outcome_row_count_;
    return uint64_t(rows) * 8u * (outcome_speed_setting_ == 0 ? outcome_phase_ms : 0u);
}

IncomingTransferOutcomeFrame IncomingTransferScreen::outcome_frame_at(uint64_t elapsed_ms) const {
    IncomingTransferOutcomeFrame frame;
    if (outcome_row_count_ < 0 || outcome_dismissed_) return frame;
    const uint64_t phase_duration = outcome_speed_setting_ == 0 ? outcome_phase_ms : 0u;
    const uint64_t row_duration = phase_duration * 8u;
    const int resolved_rows = outcome_accepted_bid_index_ >= 0
        ? outcome_accepted_bid_index_ + 1 : outcome_row_count_;
    for (int row = 0; row < resolved_rows && row < 4; ++row) {
        const uint64_t row_start = uint64_t(row) * row_duration;
        const uint64_t row_end = row_start + row_duration;
        if (phase_duration != 0 && elapsed_ms < row_end) {
            const auto phase = uint8_t((elapsed_ms - row_start) / phase_duration);
            frame.blink_phase = phase;
            frame.marks[size_t(row)] = IncomingTransferOutcomeMark::blinking_question;
            frame.colors[size_t(row)] = exe_.u8(::bmh::layout::IncomingTransfer_OutcomeFrameAt_RuntimeField + uint32_t(phase % 2));
            break;
        }
        if (outcome_accepted_bid_index_ == row) {
            frame.marks[size_t(row)] = IncomingTransferOutcomeMark::accepted;
            frame.colors[size_t(row)] = 0x27;
            break;
        }
        frame.marks[size_t(row)] = IncomingTransferOutcomeMark::rejected;
        frame.colors[size_t(row)] = 0x6c;
    }
    frame.complete = elapsed_ms >= outcome_duration_ms();
    return frame;
}

Image IncomingTransferScreen::render_at_elapsed(uint64_t elapsed_ms) const {
    if (confirmation_mode_) {
        Image out = *confirmation_backdrop_;
        draw_notice(out);
        return out;
    }
    Image out = background_;
    // OpenTransferEditor layers KB31 at native y=0x13 over the ordinary KB129 header/footer.
    out.blit(negotiation_, 0, 0x13);
    const uint32_t title_address = state_.title == IncomingTransferTitle::player_purchase
        ? kPurchaseTitleText : state_.title == IncomingTransferTitle::player_loan
        ? kLoanTitleText : kTitleText;
    const auto title = original_to_utf8(text_at(texts_, title_address));
    sprite_font_.draw(out, SpriteFont::centered_title_x(sprite_font_.text_width(title)), 13, title, 5);
    sprite_font_.draw(out, 5, 235, original_to_utf8(state_.player_name), 5);
    auto centered = [&](int left, int right, int baseline, std::string_view text, uint8_t color) {
        const int width = body_.text_width(text);
        draw_body(out, left + (right - left) / 2 - width / 2, baseline, text, color);
    };

    // OpenTransferEditor composes the player descriptor with the Source position,
    // age and seller/team descriptor; the mode-1 text call uses baseline 25.
    const auto pos = text_at(texts_, ::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer2 + uint32_t(state_.position & 3) * 4);
    const auto years_old = text_at(texts_, ::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer6);
    const std::string detail = state_.player_name + exe_.cstring(::bmh::layout::Fltd_Layout_TextPointer) + std::string(pos) +
        exe_.cstring(::bmh::layout::Fltd_Layout_TextPointer) + source_number(uint32_t(state_.age), 2, false) + std::string(years_old) +
        exe_.cstring(::bmh::layout::CupDraw_FormatError_TextPointer) + source_number(uint32_t(state_.strength), 2) + exe_.cstring(::bmh::layout::CupDraw_FormatError_ExecutableField);
    centered(0, 319, 25, detail, 0x6a);

    // DrawnFirstBy(..., 0x40): draw the flat shadow two pixels right/down.
    auto shadow_text = [&](int x, int baseline, std::string_view text, uint8_t color) {
        if (x + 2 + body_.text_width(text) <= out.width && baseline + 2 < out.height)
            body_.draw(out, x + 2, baseline + 2 - body_.baseline, text, 0x40);
        draw_body(out, x, baseline, text, color);
    };
    shadow_text(6, 39, text_at(texts_, ::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer7), 0x6a);
    if (!state_.negotiator_name.empty()) shadow_text(6, 46, state_.negotiator_name, 0x5f);
    shadow_text(170, 39, text_at(texts_, ::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer8), 0x6a);
    shadow_text(170, 46, state_.player_name, 0x5f);
    const bool source_neutral_badge =
        source_neutral_seller_token(source_seller_team_token(state_));
    if (source_neutral_badge) {
        // SourceBadgeFallback passes seller|0x8000. These Source token families route
        // through FormatError(type 14): frame first, then the neutral KB0 tile.
        for (int y = 50; y <= 89; ++y) out.at(101, y) = 0x46;
        for (int x = 101; x <= 140; ++x) out.at(x, 50) = 0x46;
        for (int x = 101; x <= 140; ++x) out.at(x, 89) = 0x40;
        for (int y = 50; y <= 89; ++y) out.at(140, y) = 0x40;
        copy_region(out, widgets_, 8, 0, 102, 51, 38, 38, 0xff);
    } else if (state_.seller_badge_sprite >= 0) {
        const int sprite = state_.seller_badge_sprite;
        if (seller_badge_atlas_.width >= (sprite % 8 + 1) * 38)
            copy_region(out, seller_badge_atlas_, (sprite & 7) * 38, 0, 102, 51, 38, 38);
    }

    // OpenTransferEditor's seller view uses the single quoted line at baseline 32;
    // the separate "Angebot" strings are only drawn when param_3 == -1.
    std::string quote = std::string(text_at(texts_, ::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer12)) +
        source_money(state_.quoted_fee, 1, state_.currency_numerator, state_.currency_denominator) +
        std::string(text_at(texts_, ::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer13)) +
        source_money(state_.quoted_monthly_salary, 1, state_.currency_numerator, state_.currency_denominator) +
        std::string(text_at(texts_, ::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer14)) + source_number(state_.quoted_years, 1, false) +
        std::string(text_at(texts_, ::bmh::layout::Contracts_Out_ExecutableField15)) + (state_.quoted_years == 1 ? "" : exe_.cstring(::bmh::layout::Fltd_Layout_TextPointer3)) +
        exe_.cstring(::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer);
    centered(0, 319, 32, quote, 0x6a);
    shadow_text(7, 123, std::string(text_at(texts_, ::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer10)) +
        source_money(state_.asking_fee, 7, state_.currency_numerator, state_.currency_denominator), 0x6a);
    shadow_text(170, 123, std::string(text_at(texts_, ::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer11)) +
        source_money(state_.asking_monthly_salary, 5, state_.currency_numerator, state_.currency_denominator), 0x6a);
    centered(2, 166, 139, text_at(texts_, ::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer5), 0x6a);

    for (size_t i = 0; i < state_.offers.size(); ++i) {
        const auto& offer = state_.offers[i];
        if (!offer.present) {
            draw_body(out, 4, 0x99 + int(i) * 0x11, text_at(texts_, ::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer9), 0x67);
            continue;
        }
        const int years = (int(offer.contract_months) - 1) / 12 + 1;
        const uint8_t offer_color = offer.loan ? 0x69 : 0x6c;
        const std::string first = state_.buyer_names[i] + exe_.cstring(::bmh::layout::Fltd_Layout_TextPointer2) +
            source_number(uint32_t(years), 1, false) + texts_[1327];
        const std::string second = source_money(offer.fee, 7, state_.currency_numerator, state_.currency_denominator) + texts_[1328] +
            source_money(offer.monthly_salary, 5, state_.currency_numerator, state_.currency_denominator) + texts_[1329];
        draw_body(out, 4, 0x95 + int(i) * 0x11, first, offer_color);
        draw_body(out, 4, 0x9d + int(i) * 0x11, second, offer_color);
    }
    if (outcome_row_count_ >= 0 && !outcome_dismissed_) {
        const auto outcome = outcome_frame_at(elapsed_ms);
        const auto question = exe_.cstring(::bmh::layout::IncomingTransfer_RenderAtElapsed_ExecutableField);
        const auto rejected = text_at(texts_, ::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer4);
        const auto accepted = text_at(texts_, ::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer3);
        for (size_t row = 0; row < outcome.marks.size(); ++row) {
            std::string_view text;
            switch (outcome.marks[row]) {
            case IncomingTransferOutcomeMark::blinking_question: text = question; break;
            case IncomingTransferOutcomeMark::rejected: text = rejected; break;
            case IncomingTransferOutcomeMark::accepted: text = accepted; break;
            case IncomingTransferOutcomeMark::none: break;
            }
            // Source draws directly into the same framebuffer. Its final dark
            // question glyphs remain wherever NEIN/OK does not overwrite them.
            if (outcome.marks[row] == IncomingTransferOutcomeMark::rejected ||
                outcome.marks[row] == IncomingTransferOutcomeMark::accepted)
                draw_body(out, 0xb2, 0x98 + int(row) * 0x11, question, exe_.u8(::bmh::layout::IncomingTransfer_RenderAtElapsed_RuntimeField));
            if (!text.empty()) draw_body(out, 0xb2, 0x98 + int(row) * 0x11, text,
                                         outcome.colors[row]);
        }
    }
    if (notice_active_) draw_notice(out);
    return out;
}

void IncomingTransferScreen::show_confirmation(uint32_t original_text_address, const Image& backdrop) {
    if (backdrop.width != 320 || backdrop.height != 240 || state_.viewer_manager < 0 ||
        state_.manager_portrait < 0 || state_.badge_sprite < 0)
        throw FormatError("incoming transfer: invalid confirmation context");
    state_.notice_text_address = original_text_address;
    state_.notice_text = text_at(texts_, original_text_address);
    notice_active_ = true;
    confirmation_mode_ = true;
    confirmation_dismissed_ = false;
    confirmation_backdrop_ = backdrop;
}

bool IncomingTransferScreen::dismiss_notice() {
    if (confirmation_mode_ || !notice_active_) return false;
    notice_active_ = false;
    return true;
}

std::optional<int> IncomingTransferScreen::click(int x, int y, bool right, uint64_t elapsed_ms) {
    if (confirmation_mode_) {
        if (!right && x >= 95 && x <= 220 && y >= 166 && y <= 176)
            confirmation_dismissed_ = true;
        return std::nullopt;
    }
    if (outcome_row_count_ >= 0) {
        // SDLFlushEvents2 is reached only after all result rows have settled.
        // The caller passes the same deterministic elapsed clock as render_at_elapsed.
        if (!outcome_dismissed_ && right && outcome_frame_at(elapsed_ms).complete)
            outcome_dismissed_ = true;
        return std::nullopt;
    }
    if (notice_active_) { dismiss_notice(); return std::nullopt; }
    if (answer_) return std::nullopt;
    if (right) { answer_ = -1; return answer_; }
    // IncomingTransferScreen: InsideInclusive(4,0x8f,0xbe,0xd3,-1,0), then
    // row=(mouse_y-0x8f)/0x11; the raw record slot is the same row index.
    // InsideInclusive includes x=190 and y=211. The last bottom-edge pixel
    // computes raw row 4, so keep it inert rather than indexing past four slots.
    if (x < 4 || x > 190 || y < 0x8f || y >= 0xd3) return std::nullopt;
    const int slot = (y - 0x8f) / 0x11;
    if (slot < 0 || slot >= int(state_.offers.size()) || !state_.offers[size_t(slot)].present ||
        (!state_.pending_offer_source && !state_.offers[size_t(slot)].transaction_supported))
        return std::nullopt;
    answer_ = slot;
    return answer_;
}

std::optional<int> IncomingTransferScreen::key(std::string_view name) {
    if (confirmation_mode_ || outcome_row_count_ >= 0) return std::nullopt;
    if (notice_active_) { dismiss_notice(); return std::nullopt; }
    if (answer_) return std::nullopt;
    if (name == "ESC" || name == "ESCAPE") { answer_ = -1; return answer_; }
    return std::nullopt;
}

std::string IncomingTransferScreen::state_json() const {
    auto json = incoming_transfer_decision_json(state_);
    if (answer_) {
        const auto at = json.rfind('}');
        json.insert(at, std::string(",\"answer\":") + std::to_string(*answer_));
    }
    if (notice_active_) {
        const auto at = json.rfind('}');
        json.insert(at, std::string(",\"notice_active\":true,\"notice_text\":") +
                        json_quote(original_to_utf8(state_.notice_text)));
    }
    if (outcome_row_count_ >= 0) {
        const auto at = json.rfind('}');
        json.insert(at, std::string(",\"outcome_row_count\":") + std::to_string(outcome_row_count_) +
                        ",\"outcome_accepted_bid_index\":" +
                        std::to_string(outcome_accepted_bid_index_) +
                        ",\"outcome_speed_setting\":" + std::to_string(outcome_speed_setting_) +
                        ",\"outcome_dismissed\":" + (outcome_dismissed_ ? "true" : "false"));
    }
    if (confirmation_mode_) {
        json.insert(json.rfind('}'), std::string(",\"confirmation_mode\":true,\"confirmation_dismissed\":") +
            (confirmation_dismissed_ ? "true" : "false"));
    }
    return json;
}

void IncomingTransferScreen::draw_notice(Image& out) const {
    if (state_.viewer_manager < 0 || state_.manager_portrait < 0 || state_.badge_sprite < 0) return;
    TaxDialogProjection notice;
    notice.manager_name=state_.viewer_name;
    notice.portrait=state_.manager_portrait;
    notice.badge_sprite=state_.badge_sprite;
    notice.badge_fallback=state_.notice_badge_fallback;
    notice.body=state_.notice_text;
    notice.source_manager_flags=confirmation_mode_?0x80:0x40;
    draw_source_dialog(out,{widgets_,faces_,&badge_atlas_,body_,heading_,exe_,
        sprite_font_,texts_},notice,gradient_text_);
}

} // namespace bmh::ui
