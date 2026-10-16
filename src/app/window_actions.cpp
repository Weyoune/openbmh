// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "app/window_actions.hpp"
#include "app/action_transaction.hpp"
#include "game/save.hpp"
#include "game/lineup.hpp"

#include <algorithm>
#include <limits>

namespace bmh::app {
using detail::apply_window;

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

ui::DeskRequest LeagueFamilyActions::pairings_entry(const ui::StrengthsIdentity& identity,
    uint64_t revision) {
    return {ui::DeskRequestKind::open_menu, identity.manager, 0, 1, -1, revision};
}

ui::LeaguePairingsRequest LeagueFamilyActions::pairings_selection(
    const ui::LeaguePairingsIdentity& identity, int league) {
    return {identity, {LeaguePairingsAction::select_league, league, -1}};
}

ui::StrengthsRequest LeagueFamilyActions::strengths_entry(const ui::StrengthsRequest& fresh,
    const ui::StrengthsIdentity& identity) {
    auto request = fresh;
    request.identity = identity;
    request.kind = ui::StrengthsAction::open;
    return request;
}

bool TransferActions::apply(uint64_t& revision, const std::function<bool()>& action) {
    auto state = session_.g_;
    const auto game = session_.game_;
    const auto rand = session_.rand_;
    auto owner = session_.transfer_offer_editor_;
    auto poach = session_.poach_;
    const auto owner_revision = session_.transfer_offer_revision_;
    auto runtime = session_.club_modal_runtime_;
    const auto old_revision = revision;
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

std::optional<TransferActions::MarketDispatch> TransferActions::market(
    const ui::TransferMarketRequest& request, const HumanBuyerSaleHooks& hooks,
    uint64_t& revision, std::string& error, HumanBuyerMarketEntryPolicy policy,
    const MarketAccept& acknowledge) {
    std::optional<MarketDispatch> result;
    apply(revision, [&] {
        const auto old_revision = revision;
        if (policy == HumanBuyerMarketEntryPolicy::normal_interactive) {
            const auto action = session_.dispatch_transfer_market(request, hooks, revision, error);
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
               request.kind != ui::TransferMarketRequestKind::open_poach)
        return "Diese Transferaktion ist nicht verfügbar.";
    return {};
}

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

std::optional<CareerSession::PoachDispatch> TransferActions::poach(int manager, int slot,
    uint64_t& revision, std::string& error) {
    std::optional<CareerSession::PoachDispatch> result;
    apply(revision, [&] {
        result = session_.open_poach(manager, slot, revision, error);
        return result.has_value();
    });
    return result;
}

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

bool BankActions::apply(uint64_t& revision, const std::function<bool()>& action) {
    return apply_window(session_, revision, session_.bank_checkpoint(),
        [&](CareerSession::BankCheckpoint before) { session_.restore_bank_checkpoint(std::move(before)); }, action);
}

bool BankActions::dispatch(const ui::BankRequest& request, uint64_t& revision, std::string& error,
    const std::function<void()>& acknowledge) {
    return apply(revision, [&] {
        if (!session_.dispatch_bank(request, revision, error)) return false;
        if (acknowledge) acknowledge();
        return true;
    });
}

ui::BankRequest BankActions::bind(const ui::BankState& observed, ui::BankRequestKind kind) {
    ui::BankRequest request;
    request.kind = kind;
    request.manager_slot = observed.manager_slot;
    request.runtime_club = observed.runtime_club;
    request.revision = observed.revision;
    return request;
}

ui::BankRequest BankActions::savings(const ui::BankState& observed, int32_t amount) {
    auto request = bind(observed, ui::BankRequestKind::move_to_deposit);
    request.amount = amount;
    return request;
}

ui::BankRequest BankActions::investment(const ui::BankState& observed, ui::BankRequestKind kind,
    int asset, int count) {
    auto request = bind(observed, kind);
    request.asset_index = asset;
    request.count = count;
    return request;
}

ui::BankRequest BankActions::credit(const ui::BankState& observed, const BankCreditContext& context,
    const BankCreditDraft& draft, bool accept_fees, int counterparty) {
    auto request = bind(observed, ui::BankRequestKind::bank_credit);
    request.expected_credit = context.saved;
    request.credit_edits = draft.edits;
    request.accept_credit_fees = accept_fees;
    request.credit_counterparty = counterparty;
    return request;
}

bool StadiumActions::apply(uint64_t& revision, const std::function<bool()>& action) {
    return apply_window(session_, revision, session_.stadium_checkpoint(),
        [&](CareerSession::StadiumCheckpoint before) { session_.restore_stadium_checkpoint(std::move(before)); }, action);
}

bool StadiumActions::dispatch(const ui::StadiumRequest& request, uint64_t& revision, std::string& error,
    const ConstructionNoticeHook& construction, const SeasonTicketCompensationNoticeHook& compensation,
    const std::function<void()>& acknowledge) {
    return apply(revision, [&] {
        if (!session_.dispatch_stadium(request, revision, error, construction, compensation)) return false;
        if (acknowledge) acknowledge();
        return true;
    });
}

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

bool LineupActions::apply(uint64_t& revision, const std::function<bool()>& action) {
    return apply_window(session_, revision, session_.lineup_checkpoint(),
        [&](CareerSession::LineupCheckpoint before) { session_.restore_lineup_checkpoint(std::move(before)); }, action);
}

bool LineupActions::lineup(const ui::LineupRequest& request, uint64_t& revision, std::string& error,
    const std::function<void()>& acknowledge) {
    return apply(revision, [&] {
        if (!session_.dispatch_lineup(request, revision, error)) return false;
        if (acknowledge) acknowledge();
        return true;
    });
}

bool LineupActions::pitch(const ui::PitchRequest& request, uint64_t& revision, std::string& error,
    const std::function<void()>& acknowledge) {
    return apply(revision, [&] {
        if (!session_.dispatch_pitch(request, revision, error)) return false;
        if (acknowledge) acknowledge();
        return true;
    });
}

bool LineupActions::pitch_swap(const ui::PitchRequest& request, uint64_t& revision, std::string& error) {
    return apply(revision, [&] { return session_.dispatch_pitch_swap(request, revision, error); });
}

bool LineupActions::order(const ui::LineupState& observed, const ui::LineupPlayer& player,
    int aspect, int value, uint64_t& revision) {
    return apply(revision, [&] {
        if (observed.revision != revision || !set_player_order_source(session_.state(), observed.manager_slot,
            observed.runtime_club, player.row, player.player_id, aspect, value)) return false;
        ++revision;
        return true;
    });
}

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

ui::LineupRequest LineupActions::prepare(const ui::LineupState& observed) {
    ui::LineupRequest request;
    request.kind = ui::LineupRequestKind::prepare_lineup;
    request.manager_slot = observed.manager_slot;
    request.runtime_club = observed.runtime_club;
    request.revision = observed.revision;
    return request;
}

ui::LineupRequest LineupActions::swap(const ui::LineupState& observed,
    const ui::LineupPlayer& first, const ui::LineupPlayer& second) {
    auto request = prepare(observed);
    request.kind = ui::LineupRequestKind::swap_players;
    request.first_row = first.row; request.second_row = second.row;
    request.first_player = first.player_id; request.second_player = second.player_id;
    request.first_slot = first.slot; request.second_slot = second.slot;
    return request;
}

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

ui::PitchRequest LineupActions::swap_pitch(const ui::PreparationState& observed,
    const ui::LineupPlayer& first, const ui::LineupPlayer& second) {
    auto request = bind_pitch(observed, ui::PitchRequestKind::swap_players);
    request.row = first.row; request.player_id = first.player_id; request.slot = first.slot;
    request.old_depth = int8_t(first.board[0x41]); request.old_lane = int8_t(first.board[0x42]);
    request.second_row = second.row; request.second_player = second.player_id; request.second_slot = second.slot;
    return request;
}

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

ui::PitchRequest LineupActions::move(const ui::PreparationState& observed, const ui::LineupPlayer& player,
    int depth, int lane) {
    auto request = bind_pitch(observed, ui::PitchRequestKind::move_player);
    request.old_weights = request.new_weights = observed.pitch->automatic_weights;
    request.row = player.row; request.player_id = player.player_id; request.slot = player.slot;
    request.old_depth = int8_t(player.board[0x41]); request.old_lane = int8_t(player.board[0x42]);
    request.new_depth = depth; request.new_lane = lane;
    return request;
}

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
