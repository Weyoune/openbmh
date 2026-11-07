// SPDX-License-Identifier: GPL-3.0-only
// Implementation of the club and league window facades declared in window_actions.hpp. Each
// method validates the UI request against the session state, runs it on the CareerSession
// inside an apply() transaction (state, RNG and revision are restored on rejection or an
// exception) and hands the result to the front end's acknowledgement callback. Errors come
// back through the std::string& error parameter; see controller.cpp for the overall model.
#include "formats/executable_layout.hpp"
#include "app/window_actions.hpp"
#include "app/action_transaction.hpp"
#include "game/save.hpp"
#include "game/lineup.hpp"

#include <algorithm>
#include <limits>

namespace bmh::app {
using detail::apply_window;

// Runs `action` as a transaction on the league family state. Returns true if the action
// succeeded; if it returns false or throws, the checkpoint and the revision are restored
// (an exception is rethrown).
bool LeagueFamilyActions::apply(uint64_t& revision, const std::function<bool()>& action) {
    auto before = session_.league_family_checkpoint();
    const auto old_revision = revision;
    try {
        if (action()) return true;
    } catch (...) {
        session_.restore_league_family_checkpoint(std::move(before));
        revision = old_revision;
        throw;
    }
    session_.restore_league_family_checkpoint(std::move(before));
    revision = old_revision;
    return false;
}

// Opens the league pairings screen from the desk (menu kind 1, using the desk item of
// kind 1). Returns its state, or nothing with `error` set if the session refused.
std::optional<ui::LeaguePairingsState> LeagueFamilyActions::open_pairings(
    const ui::DeskState& observed, uint64_t& revision, std::string& error) {
    ui::DeskRequest request;
    request.kind = ui::DeskRequestKind::open_menu;
    request.menu_kind = 1;
    request.manager_slot = observed.manager_slot;
    request.revision = observed.revision;
    for (const auto& item : observed.items)
        if (item.kind == 1) request.item_id = item.id;
    std::optional<ui::LeaguePairingsState> result;
    apply(revision, [&] {
        result = session_.open_league_pairings(request, revision, error);
        return result.has_value();
    });
    return result;
}

// Sends one pairings screen event (kind and value) to the session inside a transaction.
std::optional<CareerSession::LeaguePairingsDispatch> LeagueFamilyActions::pairings(
    const ui::LeaguePairingsIdentity& observed, LeaguePairingsAction kind, int value,
    uint64_t& revision, std::string& error) {
    ui::LeaguePairingsRequest request;
    request.identity = observed;
    request.event.kind = kind;
    request.event.value = value;
    std::optional<CareerSession::LeaguePairingsDispatch> result;
    apply(revision, [&] {
        result = session_.dispatch_league_pairings(request, revision, error);
        return result.has_value();
    });
    return result;
}

// Builds the desk request that opens menu 1 (pairings) for the manager of `identity`.
ui::DeskRequest LeagueFamilyActions::pairings_entry(const ui::StrengthsIdentity& identity,
    uint64_t revision) {
    return {ui::DeskRequestKind::open_menu, identity.manager, 0, 1, -1, revision};
}

// Builds the pairings request that selects `league`.
ui::LeaguePairingsRequest LeagueFamilyActions::pairings_selection(
    const ui::LeaguePairingsIdentity& identity, int league) {
    return {identity, {LeaguePairingsAction::select_league, league, -1}};
}

// Turns a fresh strengths request into an `open` request bound to `identity`.
ui::StrengthsRequest LeagueFamilyActions::strengths_entry(const ui::StrengthsRequest& fresh,
    const ui::StrengthsIdentity& identity) {
    auto request = fresh;
    request.identity = identity;
    request.kind = ui::StrengthsAction::open;
    return request;
}

// Runs `action` as a transaction on the transfer state. Before it, the game state, random
// generators, offer editor, poach state, club modal runtime and revision are copied; they
// are all restored if the action returns false or throws (then rethrown).
bool TransferActions::apply(uint64_t& revision, const std::function<bool()>& action) {
    auto state = session_.g_;
    const auto game = session_.game_;
    const auto rand = session_.rand_;
    auto owner = session_.transfer_offer_editor_;
    auto poach = session_.poach_;
    const auto owner_revision = session_.transfer_offer_revision_;
    auto runtime = session_.club_modal_runtime_;
    const auto old_revision = revision;
    // Puts every saved piece back into the session.
    const auto restore = [&] {
        session_.g_ = std::move(state);
        session_.game_ = game;
        session_.rand_ = rand;
        session_.transfer_offer_editor_ = std::move(owner);
        session_.poach_ = std::move(poach);
        session_.transfer_offer_revision_ = owner_revision;
        session_.club_modal_runtime_ = std::move(runtime);
        revision = old_revision;
    };
    try {
        if (action()) return true;
    } catch (...) {
        restore();
        throw;
    }
    restore();
    return false;
}

// Handles a transfer market request. In the normal interactive policy the session does
// the work (with the sale hooks and the review callback). The other policy is an older
// path that only allows listing a player and submitting a bid; it bumps the revision
// only if the state or random stream really changed. `acknowledge` is called on success.
std::optional<TransferActions::MarketDispatch> TransferActions::market(
    const ui::TransferMarketRequest& request, const HumanBuyerSaleHooks& hooks,
    uint64_t& revision, std::string& error, HumanBuyerMarketEntryPolicy policy,
    const MarketAccept& acknowledge,
    const std::function<int(const GameState&, const TransferOfferListView&)>& review) {
    std::optional<MarketDispatch> result;
    // Body of the transaction; returning false rolls everything back.
    apply(revision, [&] {
        const auto old_revision = revision;
        if (policy == HumanBuyerMarketEntryPolicy::normal_interactive) {
            const auto action = session_.dispatch_transfer_market(request, hooks, revision, error, review);
            if (!action) return false;
            result = MarketDispatch{*action, revision != old_revision};
        } else {
            // Preserve the legacy caller's threshold draw even for a low offer.
            // This path predates the interactive Source sale and has its own
            // revision semantics; moving it must not merge the two policies.
            error = rejection(request, revision, policy);
            if (!error.empty()) return false;
            if (request.kind != ui::TransferMarketRequestKind::list_player &&
                request.kind != ui::TransferMarketRequestKind::submit_bid) {
                error = "Diese Transferaktion ist nicht verfügbar.";
                return false;
            }
            const auto before = save_stream(session_.state());
            const auto game_calls = session_.game().calls();
            const auto rand_calls = session_.rand().calls();
            TransferActionResult action;
            if (request.kind == ui::TransferMarketRequestKind::list_player) {
                MinuteReport report;
                action = list_manager_player(session_.state(), request.manager_slot,
                    request.squad_row, session_.rand(), report);
                if (report.unsupported) throw FormatError("unsupported transfer listing");
            } else {
                action = submit_transfer_bid(session_.state(), request.bid, session_.rand());
            }
            const bool changed = session_.game().calls() != game_calls ||
                session_.rand().calls() != rand_calls || save_stream(session_.state()) != before;
            if (changed) ++revision;
            result = MarketDispatch{action, changed};
        }
        if (acknowledge) acknowledge(*result);
        return true;
    });
    return result;
}

// Checks a market request against the current state. Returns a German error text if the
// request is stale or invalid, or an empty string if it may proceed. Bonus values may be
// at most 255 (interactive policy) or 100 (legacy policy).
std::string TransferActions::rejection(const ui::TransferMarketRequest& request,
    uint64_t revision, HumanBuyerMarketEntryPolicy policy) const {
    const auto& state = session_.state();
    if (request.manager_slot < 0 || size_t(request.manager_slot) >= state.manager_teams.size() ||
        request.manager_slot > 3 || !state.memory.has(::bmh::layout::ActiveManager) ||
        state.memory.u8(::bmh::layout::ActiveManager) != request.manager_slot ||
        request.runtime_club != state.manager_teams[size_t(request.manager_slot)] ||
        request.revision != revision || revision == std::numeric_limits<uint64_t>::max())
        return "Dieses Transferangebot gehört zu einem veralteten Spielstand.";
    if (request.kind == ui::TransferMarketRequestKind::submit_bid) {
        if (request.bid.manager != request.manager_slot || request.squad_row != -1 ||
            request.expected_player != -1 ||
            std::any_of(request.bid.bonuses.begin(), request.bid.bonuses.end(), [policy](uint32_t value) {
                return value > (policy == HumanBuyerMarketEntryPolicy::normal_interactive ? 255u : 100u);
            })) return "Die Angaben zum Transferangebot sind ungültig.";
    } else if (request.kind == ui::TransferMarketRequestKind::list_player) {
        const auto range = squad_range(request.runtime_club);
        if (request.squad_row < 0 || size_t(request.squad_row) >= range.count ||
            range.first + size_t(request.squad_row) >= state.squad_slots.size() ||
            request.expected_player < 0 || request.expected_player >= 0xfff ||
            (state.squad_slots[range.first + size_t(request.squad_row)] & 0xfff) != request.expected_player)
            return "Dieser Spieler gehört nicht mehr zur gewählten Kaderposition.";
    } else if (request.kind != ui::TransferMarketRequestKind::open_offer &&
               request.kind != ui::TransferMarketRequestKind::preview_quote &&
               request.kind != ui::TransferMarketRequestKind::open_poach &&
               request.kind != ui::TransferMarketRequestKind::review_offers &&
               request.kind != ui::TransferMarketRequestKind::store_search_profile)
        return "Diese Transferaktion ist nicht verfügbar.";
    return {};
}

// Opens the transfer offer editor for a market request (one transaction).
std::optional<CareerSession::TransferOfferEditorDispatch> TransferActions::open_offer(
    const ui::TransferMarketRequest& request, uint64_t& revision, std::string& error,
    const OfferAccept& acknowledge) {
    std::optional<CareerSession::TransferOfferEditorDispatch> result;
    apply(revision, [&] {
        result = session_.open_transfer_offer(request, revision, error);
        if (!result) return false;
        if (acknowledge) acknowledge(*result);
        return true;
    });
    return result;
}

// Asks the session for a preview (quote) of a market request without committing it.
std::optional<CareerSession::TransferMarketPreviewDispatch> TransferActions::preview(
    const ui::TransferMarketRequest& request, uint64_t& revision, std::string& error,
    const PreviewAccept& acknowledge) {
    std::optional<CareerSession::TransferMarketPreviewDispatch> result;
    apply(revision, [&] {
        result = session_.preview_transfer_market(request, revision, error);
        if (!result) return false;
        if (acknowledge) acknowledge(*result);
        return true;
    });
    return result;
}

// Sends an action of the outgoing offer editor to the session; the team and player
// information presenters are used when it opens those screens.
std::optional<CareerSession::TransferOfferEditorDispatch> TransferActions::offer(
    const ui::OutgoingTransferRequest& request, const HumanBuyerSaleHooks& hooks,
    uint64_t& revision, std::string& error, const TransferOfferTeamInfoPresenter& team_info,
    const TransferOfferPlayerInfoPresenter& player_info, const OfferAccept& acknowledge) {
    std::optional<CareerSession::TransferOfferEditorDispatch> result;
    apply(revision, [&] {
        result = session_.dispatch_transfer_offer(request, hooks, revision, error, team_info, player_info);
        if (!result) return false;
        if (acknowledge) acknowledge(*result);
        return true;
    });
    return result;
}

// Starts a poaching attempt for the given manager and squad slot.
std::optional<CareerSession::PoachDispatch> TransferActions::poach(int manager, int slot,
    uint64_t& revision, std::string& error) {
    std::optional<CareerSession::PoachDispatch> result;
    apply(revision, [&] {
        result = session_.open_poach(manager, slot, revision, error);
        return result.has_value();
    });
    return result;
}

// Builds the request to list the squad player `item` on the market (as a loan if `loan`),
// bound to the observed state's manager, club and revision.
ui::TransferMarketRequest TransferActions::listing(const ui::TransferMarketState& observed,
    const ui::TransferMarketSquadItem& item, bool loan) {
    ui::TransferMarketRequest request;
    request.kind = ui::TransferMarketRequestKind::list_player;
    request.offer_is_loan = loan;
    request.manager_slot = observed.manager_slot;
    request.runtime_club = observed.runtime_club;
    request.revision = observed.revision;
    request.squad_row = item.row;
    request.expected_player = item.player;
    request.selected_players.push_back({-1, observed.roster.squad_start_offset + item.row,
        item.player, observed.runtime_club, item.row});
    return request;
}

// Builds the request to open an offer for the market entry `item`; an existing own bid
// is used as the starting point.
ui::TransferMarketRequest TransferActions::opening(const ui::TransferMarketState& observed,
    const ui::TransferMarketItem& item, bool loan) {
    ui::TransferMarketRequest request;
    request.kind = ui::TransferMarketRequestKind::open_offer;
    request.offer_is_loan = loan;
    request.manager_slot = observed.manager_slot;
    request.runtime_club = observed.runtime_club;
    request.revision = observed.revision;
    if (item.own_bid) request.bid = *item.own_bid;
    request.bid.manager = observed.manager_slot;
    request.bid.list_entry = item.list_entry;
    request.bid.player = item.player;
    request.bid.source_slot = item.source_slot;
    request.selected_players.push_back({item.list_entry, item.source_slot, item.player, item.seller, -1});
    return request;
}

// Builds a request from the offer editor's current state plus the changed field and value.
ui::OutgoingTransferRequest TransferActions::editor(const ui::OutgoingTransferEditorState& observed,
    ui::OutgoingTransferRequestKind kind, ui::OutgoingTransferField field, uint32_t value) {
    ui::OutgoingTransferRequest request;
    request.kind = kind;
    request.identity = {observed.manager_slot, observed.runtime_club, observed.revision,
        observed.list_entry, observed.source_slot, observed.player, observed.seller, observed.bid_slot};
    request.field = field;
    request.value = value;
    request.quote_loan = observed.quote_loan;
    request.fee = observed.fee;
    request.monthly_salary = observed.monthly_salary;
    request.duration_years = observed.duration_years;
    request.bonuses = observed.bonuses;
    request.quoted_fee = observed.panel.quoted_fee;
    request.quoted_monthly_salary = observed.panel.quoted_monthly_salary;
    request.quoted_years = observed.panel.quoted_years;
    return request;
}

// Runs `action` as a transaction restoring the bank checkpoint on failure.
bool BankActions::apply(uint64_t& revision, const std::function<bool()>& action) {
    return apply_window(session_, revision, session_.bank_checkpoint(),
        [&](CareerSession::BankCheckpoint before) { session_.restore_bank_checkpoint(std::move(before)); }, action);
}

// Sends a bank request to the session; `acknowledge` is called when it was accepted.
bool BankActions::dispatch(const ui::BankRequest& request, uint64_t& revision, std::string& error,
    const std::function<void()>& acknowledge) {
    return apply(revision, [&] {
        if (!session_.dispatch_bank(request, revision, error)) return false;
        if (acknowledge) acknowledge();
        return true;
    });
}

// Creates a bank request of `kind` bound to the observed manager, club and revision.
ui::BankRequest BankActions::bind(const ui::BankState& observed, ui::BankRequestKind kind) {
    ui::BankRequest request;
    request.kind = kind;
    request.manager_slot = observed.manager_slot;
    request.runtime_club = observed.runtime_club;
    request.revision = observed.revision;
    return request;
}

// Request to move `amount` to the deposit account.
ui::BankRequest BankActions::savings(const ui::BankState& observed, int32_t amount) {
    auto request = bind(observed, ui::BankRequestKind::move_to_deposit);
    request.amount = amount;
    return request;
}

// Request for an investment action on asset index `asset` with `count` units.
ui::BankRequest BankActions::investment(const ui::BankState& observed, ui::BankRequestKind kind,
    int asset, int count) {
    auto request = bind(observed, kind);
    request.asset_index = asset;
    request.count = count;
    return request;
}

// Request for a bank credit: carries the saved credit, the player's edits, whether the
// fees are accepted and the counterparty.
ui::BankRequest BankActions::credit(const ui::BankState& observed, const BankCreditContext& context,
    const BankCreditDraft& draft, bool accept_fees, int counterparty) {
    auto request = bind(observed, ui::BankRequestKind::bank_credit);
    request.expected_credit = context.saved;
    request.credit_edits = draft.edits;
    request.accept_credit_fees = accept_fees;
    request.credit_counterparty = counterparty;
    return request;
}

// Runs `action` as a transaction restoring the stadium checkpoint on failure.
bool StadiumActions::apply(uint64_t& revision, const std::function<bool()>& action) {
    return apply_window(session_, revision, session_.stadium_checkpoint(),
        [&](CareerSession::StadiumCheckpoint before) { session_.restore_stadium_checkpoint(std::move(before)); }, action);
}

// Sends a stadium request to the session with the notice hooks; `acknowledge` is called
// when it was accepted.
bool StadiumActions::dispatch(const ui::StadiumRequest& request, uint64_t& revision, std::string& error,
    const ConstructionNoticeHook& construction, const SeasonTicketCompensationNoticeHook& compensation,
    const std::function<void()>& acknowledge) {
    return apply(revision, [&] {
        if (!session_.dispatch_stadium(request, revision, error, construction, compensation)) return false;
        if (acknowledge) acknowledge();
        return true;
    });
}

// Creates a stadium request bound to the observed club, revision, scope and sequence.
ui::StadiumRequest StadiumActions::request(const ui::StadiumState& observed, ui::StadiumRequestKind kind,
    int object, int option, int32_t price, bool accept_tip) {
    ui::StadiumRequest request;
    request.kind = kind;
    request.manager_slot = observed.manager_slot;
    request.runtime_club = observed.runtime_club;
    request.stable_club_identity = observed.stable_club_identity;
    request.revision = observed.revision;
    request.scope = observed.scope;
    request.sequence = observed.sequence;
    request.object = object;
    request.option = option;
    request.price = price;
    request.accept_tip = accept_tip;
    return request;
}

// Sets the ticket price for the observed desk's club; the stable club identity is filled
// in when the club index is valid.
bool DeskActions::ticket_price(const ui::DeskState& observed, int price,
    uint64_t& revision, std::string& error) {
    TicketPriceRequest request;
    request.identity.manager_slot = observed.manager_slot;
    request.identity.runtime_club = observed.runtime_club;
    if (observed.runtime_club >= 0 && size_t(observed.runtime_club) < session_.state().club_ids.size())
        request.identity.stable_club_identity = int(session_.state().club_ids[size_t(observed.runtime_club)]);
    request.identity.revision = observed.revision;
    request.price = price;
    return session_.dispatch_ticket_price(request, revision, error);
}

// Runs `action` as a transaction restoring the lineup checkpoint on failure.
bool LineupActions::apply(uint64_t& revision, const std::function<bool()>& action) {
    return apply_window(session_, revision, session_.lineup_checkpoint(),
        [&](CareerSession::LineupCheckpoint before) { session_.restore_lineup_checkpoint(std::move(before)); }, action);
}

// Sends a lineup request to the session; `acknowledge` is called when accepted.
bool LineupActions::lineup(const ui::LineupRequest& request, uint64_t& revision, std::string& error,
    const std::function<void()>& acknowledge) {
    return apply(revision, [&] {
        if (!session_.dispatch_lineup(request, revision, error)) return false;
        if (acknowledge) acknowledge();
        return true;
    });
}

// Sends a pitch request to the session; `acknowledge` is called when accepted.
bool LineupActions::pitch(const ui::PitchRequest& request, uint64_t& revision, std::string& error,
    const std::function<void()>& acknowledge) {
    return apply(revision, [&] {
        if (!session_.dispatch_pitch(request, revision, error)) return false;
        if (acknowledge) acknowledge();
        return true;
    });
}

// Sends a pitch swap request to the session as one transaction.
bool LineupActions::pitch_swap(const ui::PitchRequest& request, uint64_t& revision, std::string& error) {
    return apply(revision, [&] { return session_.dispatch_pitch_swap(request, revision, error); });
}

// Changes one order value (`aspect`) of a player. Only valid for the observed revision;
// on success the revision is increased.
bool LineupActions::order(const ui::LineupState& observed, const ui::LineupPlayer& player,
    int aspect, int value, uint64_t& revision) {
    return apply(revision, [&] {
        if (observed.revision != revision || !set_player_order_source(session_.state(), observed.manager_slot,
            observed.runtime_club, player.row, player.player_id, aspect, value)) return false;
        ++revision;
        return true;
    });
}

// Tries the requested pitch move on a copy of the state and tells whether the player ends
// up paired with `second_row` (requested), with another player (other_pair) or whether the
// move is invalid.
LineupActions::PairTarget LineupActions::pair_target(const ui::PitchRequest& request, int second_row) const {
    auto preview = session_.state();
    if (!move_pitch_player_source(preview, request.manager_slot, request.runtime_club,
        request.row, request.player_id, request.slot, request.old_depth, request.old_lane,
        request.new_depth, request.new_lane)) return PairTarget::invalid;
    const size_t board = size_t(request.manager_slot) * 0x840;
    return preview.tactics.at(board + size_t(request.row) * 0x58 + 0x3f) == second_row &&
        preview.tactics.at(board + size_t(second_row) * 0x58 + 0x3f) == request.row
        ? PairTarget::requested : PairTarget::other_pair;
}

// Creates a lineup request that prepares the lineup of the observed state.
ui::LineupRequest LineupActions::prepare(const ui::LineupState& observed) {
    ui::LineupRequest request;
    request.kind = ui::LineupRequestKind::prepare_lineup;
    request.manager_slot = observed.manager_slot;
    request.runtime_club = observed.runtime_club;
    request.revision = observed.revision;
    return request;
}

// Creates a request to swap two players of the lineup.
ui::LineupRequest LineupActions::swap(const ui::LineupState& observed,
    const ui::LineupPlayer& first, const ui::LineupPlayer& second) {
    auto request = prepare(observed);
    request.kind = ui::LineupRequestKind::swap_players;
    request.first_row = first.row; request.second_row = second.row;
    request.first_player = first.player_id; request.second_player = second.player_id;
    request.first_slot = first.slot; request.second_slot = second.slot;
    return request;
}

// Creates a pitch request of `kind` bound to the observed lineup. The observed live mode
// must be known, otherwise a FormatError is thrown.
ui::PitchRequest LineupActions::bind_pitch(const ui::PreparationState& observed, ui::PitchRequestKind kind) {
    if (!observed.pitch || !observed.pitch->indoor.has_value())
        throw FormatError("pitch: missing observed live mode");
    ui::PitchRequest request;
    request.kind = kind;
    request.manager_slot = observed.lineup.manager_slot;
    request.runtime_club = observed.lineup.runtime_club;
    request.revision = observed.lineup.revision;
    request.indoor = observed.pitch->indoor.value();
    return request;
}

// Creates a request to swap two players on the pitch (including their current positions).
ui::PitchRequest LineupActions::swap_pitch(const ui::PreparationState& observed,
    const ui::LineupPlayer& first, const ui::LineupPlayer& second) {
    auto request = bind_pitch(observed, ui::PitchRequestKind::swap_players);
    request.row = first.row; request.player_id = first.player_id; request.slot = first.slot;
    request.old_depth = int8_t(first.board[0x41]); request.old_lane = int8_t(first.board[0x42]);
    request.second_row = second.row; request.second_player = second.player_id; request.second_slot = second.slot;
    return request;
}

// Creates a request to switch the pitch page (toggles the 0x000c bits of the display word).
ui::PitchRequest LineupActions::page(const ui::PreparationState& observed) {
    auto request = bind_pitch(observed, ui::PitchRequestKind::select_pitch_page);
    request.expected_mode = uint8_t(observed.pitch->mode);
    request.expected_display_word = observed.pitch->native_display_word;
    request.expected_set_piece_values = observed.pitch->set_piece_takers;
    request.expected_set_piece_signature = observed.pitch->set_piece_signature;
    request.page_control = 1;
    request.new_display_word = uint16_t(request.expected_display_word ^ 0x000c);
    return request;
}

// Creates a request to load or save a formation in `slot`; saving copies the position
// bytes of the 11 players on the pitch.
ui::PitchRequest LineupActions::formation(const ui::PreparationState& observed,
    ui::PitchRequestKind kind, int slot) {
    auto request = bind_pitch(observed, kind);
    request.old_weights = request.new_weights = observed.pitch->automatic_weights;
    request.formation = slot;
    if (kind == ui::PitchRequestKind::save_formation) {
        for (size_t row = 0; row < 11; ++row) {
            const auto& player = observed.lineup.players.at(row);
            request.formation_positions[row] = player.board[0x41];
            request.formation_positions[11 + row] = player.board[0x42];
        }
    }
    return request;
}

// Creates a request to move a player to the given depth and lane.
ui::PitchRequest LineupActions::move(const ui::PreparationState& observed, const ui::LineupPlayer& player,
    int depth, int lane) {
    auto request = bind_pitch(observed, ui::PitchRequestKind::move_player);
    request.old_weights = request.new_weights = observed.pitch->automatic_weights;
    request.row = player.row; request.player_id = player.player_id; request.slot = player.slot;
    request.old_depth = int8_t(player.board[0x41]); request.old_lane = int8_t(player.board[0x42]);
    request.new_depth = depth; request.new_lane = lane;
    return request;
}

// Creates a request that advances one of the three automatic weights (index 0..2) by one
// step; each weight is a 2-bit value that wraps around after 3.
ui::PitchRequest LineupActions::weight(const ui::PreparationState& observed, int index) {
    if (index < 0 || index > 2) throw FormatError("pitch: invalid automatic weight index");
    auto request = bind_pitch(observed, ui::PitchRequestKind::set_automatic_weight);
    request.old_weights = request.new_weights = observed.pitch->automatic_weights;
    request.weight_index = index;
    const int shift = 2 * index;
    const int next = (((request.old_weights >> shift) & 3) + 1) & 3;
    request.new_weights = uint8_t((request.old_weights & ~(3 << shift)) | (next << shift));
    return request;
}

} // namespace bmh::app
