// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "app/desktop_actions.hpp"
#include "app/action_transaction.hpp"

#include <algorithm>

namespace bmh::app {

ui::DeskRequest DesktopActions::menu(const ui::DeskState& observed, int kind) {
    ui::DeskRequest request;
    request.kind = ui::DeskRequestKind::open_menu;
    request.menu_kind = kind;
    request.manager_slot = observed.manager_slot;
    request.revision = observed.revision;
    for (const auto& item : observed.items)
        if (item.kind == kind) request.item_id = item.id;
    return request;
}

bool DeskSettingsActions::apply(uint64_t& revision, const std::function<bool()>& action) {
    return detail::apply_window(session_, revision, session_.desk_settings_checkpoint(revision),
        [&](CareerSession::DeskSettingsCheckpoint before) {
            session_.restore_desk_settings_checkpoint(std::move(before), revision);
        }, action);
}

std::optional<CareerSession::DeskSettingsProjection> DeskSettingsActions::open(
    const ui::DeskRequest& request, uint64_t& revision, std::string& error, const OpenAccept& acknowledge) {
    std::optional<CareerSession::DeskSettingsProjection> result;
    apply(revision, [&] {
        result = session_.open_desk_settings(request, revision, error);
        if (!result) return false;
        if (acknowledge) acknowledge(*result);
        return true;
    });
    return result;
}

std::optional<CareerSession::DeskSettingsDispatch> DeskSettingsActions::dispatch(
    const CareerSession::DeskSettingsAction& request, uint64_t& revision, std::string& error,
    const Accept& acknowledge) {
    std::optional<CareerSession::DeskSettingsDispatch> result;
    apply(revision, [&] {
        result = session_.dispatch_desk_settings(request, revision, error);
        if (!result) return false;
        if (acknowledge) acknowledge(*result);
        return true;
    });
    return result;
}

std::optional<CareerSession::DeskSettingsDispatch> DeskSettingsActions::write(uint64_t sequence,
    const ui::SettingsWriteRequest& request, uint64_t& revision, std::string& error, const Accept& acknowledge) {
    return dispatch(CareerSession::DeskSettingsWrite{sequence, request}, revision, error, acknowledge);
}

std::optional<CareerSession::DeskSettingsDispatch> DeskSettingsActions::file(uint64_t sequence,
    const ui::SaveLoadRequest& request, uint64_t& revision, std::string& error, const Accept& acknowledge) {
    return dispatch(CareerSession::DeskSettingsFile{sequence, request}, revision, error, acknowledge);
}

std::optional<CareerSession::DeskSettingsDispatch> DeskSettingsActions::close(uint64_t sequence,
    uint64_t owner_revision, const ui::DeskRequest& observed, uint64_t& revision, std::string& error,
    const Accept& acknowledge) {
    const CareerSession::DeskSettingsOwner owner{sequence, owner_revision, observed.manager_slot, observed.item_id};
    return dispatch(CareerSession::DeskSettingsClose{owner}, revision, error, acknowledge);
}

bool DeskSettingsActions::edit(const ui::SettingsState& observed, int row, int column, ui::SettingsEdit operation,
    uint64_t& revision, std::string& error) {
    ui::SettingsWriteRequest request;
    request.manager_slot = observed.manager_slot;
    request.revision = observed.revision;
    request.expected_raw = observed.cells;
    request.row = uint8_t(row);
    request.column = uint8_t(column);
    request.operation = operation;
    return apply(revision, [&] { return session_.dispatch_settings(request, revision, error); });
}

bool BalanceActions::apply(uint64_t& revision, const std::function<bool()>& action) {
    return detail::apply_window(session_, revision, session_.balance_checkpoint(),
        [&](CareerSession::BalanceCheckpoint before) { session_.restore_balance_checkpoint(std::move(before)); }, action);
}

std::optional<ui::BalanceState> BalanceActions::open(const ui::DeskRequest& request,
    uint64_t& revision, const Image& underlay, std::string& error, const OpenAccept& acknowledge) {
    std::optional<ui::BalanceState> result;
    apply(revision, [&] {
        result = session_.open_balance_plan(request, revision, underlay, error);
        if (!result) return false;
        if (acknowledge) acknowledge(*result);
        return true;
    });
    return result;
}

std::optional<CareerSession::BalanceDispatch> BalanceActions::dispatch(const ui::BalanceRequest& request,
    uint64_t& revision, std::string& error, const Accept& acknowledge) {
    std::optional<CareerSession::BalanceDispatch> result;
    apply(revision, [&] {
        result = session_.dispatch_balance_plan(request, revision, error);
        if (!result) return false;
        if (acknowledge) acknowledge(*result);
        return true;
    });
    return result;
}

BalancePlanDraft BalanceActions::open_draft(int manager) const { return bmh::open_balance_plan(session_.state(), manager); }
void BalanceActions::cycle_draft(BalancePlanDraft& draft) const { cycle_balance_view(session_.state(), draft); }
void BalanceActions::reset_draft(BalancePlanDraft& draft) const { reset_balance_plan(session_.state(), draft); }
BalanceEditStatus BalanceActions::edit_draft(BalancePlanDraft& draft, int row, int32_t value) const {
    return edit_balance_plan(session_.state(), draft, row, value);
}

bool BalanceActions::close_draft(const BalancePlanDraft& draft, uint64_t& revision) {
    bool warning = false;
    apply(revision, [&] {
        warning = bmh::close_balance_plan(session_.state(), draft);
        ++revision;
        return true;
    });
    return warning;
}

bool OtherGamesActions::apply(uint64_t& revision, const std::function<bool()>& action) {
    return detail::apply_window(session_, revision, session_.other_games_checkpoint(),
        [&](CareerSession::OtherGamesCheckpoint before) { session_.restore_other_games_checkpoint(std::move(before)); }, action);
}

std::optional<ui::OtherGamesState> OtherGamesActions::open(const ui::DeskRequest& request,
    uint64_t& revision, const Image& underlay, std::string& error, const OpenAccept& acknowledge) {
    std::optional<ui::OtherGamesState> result;
    apply(revision, [&] {
        result = session_.open_other_games(request, revision, underlay, error);
        if (!result) return false;
        if (acknowledge) acknowledge(*result);
        return true;
    });
    return result;
}

std::optional<CareerSession::OtherGamesDispatch> OtherGamesActions::dispatch(const ui::OtherGamesRequest& request,
    uint64_t& revision, std::string& error, const TransferOfferTeamInfoPresenter& team_info, const Accept& acknowledge) {
    std::optional<CareerSession::OtherGamesDispatch> result;
    apply(revision, [&] {
        result = session_.dispatch_other_games(request, revision, error, team_info);
        if (!result) return false;
        if (acknowledge) acknowledge(*result);
        return true;
    });
    return result;
}

bool TopScorersActions::apply(uint64_t& revision, const std::function<bool()>& action) {
    return detail::apply_window(session_, revision, session_.top_scorers_checkpoint(),
        [&](CareerSession::TopScorersCheckpoint before) { session_.restore_top_scorers_checkpoint(std::move(before)); }, action);
}

std::optional<ui::TopScorersState> TopScorersActions::open(const ui::DeskRequest& request,
    uint64_t& revision, const Image& underlay, std::string& error, const OpenAccept& acknowledge) {
    std::optional<ui::TopScorersState> result;
    apply(revision, [&] {
        result = session_.open_top_scorers(request, revision, underlay, error);
        if (!result) return false;
        ++revision; // Preserve the existing menu-20 opening revision.
        if (acknowledge) acknowledge(*result);
        return true;
    });
    return result;
}

std::optional<CareerSession::TopScorersDispatch> TopScorersActions::dispatch(const ui::TopScorersRequest& request,
    uint64_t& revision, std::string& error, const Accept& acknowledge) {
    std::optional<CareerSession::TopScorersDispatch> result;
    apply(revision, [&] {
        result = session_.dispatch_top_scorers(request, revision, error);
        if (!result) return false;
        if (acknowledge) acknowledge(*result);
        return true;
    });
    return result;
}

bool OpportunitiesActions::apply(uint64_t& revision, const std::function<bool()>& action) {
    return detail::apply_window(session_, revision, session_.opportunities_checkpoint(),
        [&](CareerSession::OpportunitiesCheckpoint before) { session_.restore_opportunities_checkpoint(std::move(before)); }, action);
}

std::optional<ui::OpportunitiesState> OpportunitiesActions::open(const ui::DeskRequest& request,
    uint64_t& revision, const Image& underlay, std::string& error, bool national_source_case,
    int stable_identity, const OpenAccept& acknowledge) {
    std::optional<ui::OpportunitiesState> result;
    apply(revision, [&] {
        result = session_.open_opportunities(request, revision, underlay, error, national_source_case, stable_identity);
        if (!result) return false;
        ++revision;
        if (acknowledge) acknowledge(*result);
        return true;
    });
    return result;
}

std::optional<CareerSession::OpportunitiesDispatch> OpportunitiesActions::dispatch(const ui::OpportunitiesRequest& request,
    uint64_t& revision, std::string& error, const Accept& acknowledge) {
    std::optional<CareerSession::OpportunitiesDispatch> result;
    apply(revision, [&] {
        result = session_.dispatch_opportunities(request, revision, error);
        if (!result) return false;
        if (acknowledge) acknowledge(*result);
        return true;
    });
    return result;
}

bool OpportunitiesActions::preference(const ui::AnalyticsRequest& request, uint64_t& revision,
    std::string& error, const std::function<void()>& acknowledge) {
    return apply(revision, [&] {
        if (!session_.apply_statistics_preference(request.owner, request.manager_slot,
            request.expected_flags, request.target_flags, revision, error)) return false;
        if (revision == UINT64_MAX) throw FormatError("statistics: revision exhausted");
        ++revision;
        if (acknowledge) acknowledge();
        return true;
    });
}

ui::OpportunitiesRequest OpportunitiesActions::close_request(const ui::OpportunitiesState& observed) {
    return {observed.identity, ui::OpportunitiesAction::close, observed.mode};
}

bool ManagerJoinActions::apply(uint64_t& revision, const std::function<bool()>& action) {
    return detail::apply_window(session_, revision, session_.manager_join_checkpoint(),
        [&](CareerSession::ManagerJoinCheckpoint before) { session_.restore_manager_join_checkpoint(std::move(before)); }, action);
}

bool ManagerJoinActions::commit(uint64_t& revision, std::string& error,
    const std::function<void(ui::CareerInitializationResult&)>& acknowledge) {
    return apply(revision, [&] {
        ui::CareerInitializationResult joined;
        if (!session_.commit_manager_join(joined, revision, error)) return false;
        if (acknowledge) acknowledge(joined);
        return true;
    });
}

bool MailboxActions::dispatch(const ui::MailboxActionRequest& request, uint64_t& revision,
    std::string& error, const std::function<void()>& acknowledge) {
    return detail::apply_native(session_, revision, [&] {
        if (!session_.dispatch_mailbox(request, revision, error)) return false;
        if (acknowledge) acknowledge();
        return true;
    });
}

bool MailboxActions::close(const ui::MailboxCloseRequest& request, uint64_t& revision,
    std::string& error, const std::function<void()>& acknowledge) {
    return detail::apply_native(session_, revision, [&] {
        if (!session_.acknowledge_mailbox_close(request, revision, error)) return false;
        if (acknowledge) acknowledge();
        return true;
    });
}

ui::MailboxActionRequest MailboxActions::request(int manager, uint64_t revision,
    ui::MailboxActionKind kind, const ui::MailboxMessage& observed) {
    ui::MailboxActionRequest request;
    request.kind = kind;
    request.manager_slot = manager;
    request.revision = revision;
    request.entry_index = observed.entry_index;
    request.expected_entry = observed.raw_entry;
    return request;
}

bool MenuLayoutActions::dispatch(const ui::SaveLoadRequest& request, uint64_t& revision,
    std::string& error, const std::function<void()>& acknowledge) {
    return detail::apply_native(session_, revision, [&] {
        if (!session_.dispatch_menu_layout(request, revision, error)) return false;
        if (acknowledge) acknowledge();
        return true;
    });
}

std::optional<EnvironmentResult> EnvironmentActions::set(const ui::EnvironmentRequest& request,
    uint64_t& revision, std::string& error, const Notice& notice, const std::function<void(bool)>& acknowledge) {
    return set_impl(request, revision, error, notice, acknowledge, false);
}

std::optional<EnvironmentResult> EnvironmentActions::set_console(const ui::EnvironmentView& observed,
    int item, int level, uint64_t& revision, std::string& error, const Notice& notice) {
    return set_impl(request(observed, item, level), revision, error, notice, {}, true);
}

std::optional<EnvironmentResult> EnvironmentActions::set_impl(const ui::EnvironmentRequest& request,
    uint64_t& revision, std::string& error, const Notice& notice, const std::function<void(bool)>& acknowledge,
    bool publish_console_result) {
    error.clear();
    std::optional<EnvironmentResult> result;
    const bool accepted = detail::apply_native(session_, revision, [&] {
        const auto& state = session_.state();
        if (request.kind != ui::EnvironmentRequestKind::set_item || request.revision != revision ||
            request.manager_slot < 0 || request.manager_slot > 3 || !state.memory.has(::bmh::layout::ActiveManager) ||
            state.memory.u8(::bmh::layout::ActiveManager) != request.manager_slot ||
            size_t(request.manager_slot) >= state.manager_teams.size() ||
            state.manager_teams[size_t(request.manager_slot)] != request.runtime_club) {
            error = "stale_environment_owner";
            return false;
        }
        auto staged = state;
        std::string message;
        result = set_environment(staged, request.manager_slot, request.item, request.level, &message);
        if (*result == EnvironmentResult::invalid) return true;
        if (publish_console_result) session_.state() = staged;
        if (!message.empty() && (!notice || !notice(staged, message))) {
            if (error.empty()) error = "environment_source_notice_aborted";
            return false;
        }
        const bool changed = *result == EnvironmentResult::changed;
        if (changed) {
            if (revision == UINT64_MAX) throw FormatError("environment revision exhausted");
            ++revision;
        }
        // The old Console called Core directly even for its handled refusals;
        // SDL only published a changed result. Preserve both caller semantics.
        if (changed || publish_console_result) session_.state() = std::move(staged);
        if (acknowledge) acknowledge(changed);
        return true;
    });
    if (!accepted) result.reset();
    return result;
}

ui::EnvironmentRequest EnvironmentActions::request(const ui::EnvironmentView& observed, int item, int level) {
    ui::EnvironmentRequest request;
    request.kind = ui::EnvironmentRequestKind::set_item;
    request.manager_slot = observed.manager_slot;
    request.runtime_club = observed.runtime_club;
    request.revision = observed.revision;
    request.owner = observed.owner;
    request.item = item;
    request.level = level;
    return request;
}

bool RescheduleActions::apply(uint64_t& revision, const std::function<bool()>& action) {
    return detail::apply_native(session_, revision, action);
}

RescheduleStatus RescheduleActions::change(GameState& state, int manager, const LeagueMatchRef& match,
    int day, ui::RescheduleActionKind kind) {
    if (kind == ui::RescheduleActionKind::move) return reschedule_league_match(state, manager, match, day);
    if (kind == ui::RescheduleActionKind::undo && undo_reschedule(state, manager, day)) return RescheduleStatus::moved;
    return RescheduleStatus::invalid_match;
}

RescheduleStatus RescheduleActions::dispatch(const ui::RescheduleAction& request, uint64_t& revision,
    const std::function<void()>& acknowledge) {
    auto status = RescheduleStatus::invalid_match;
    apply(revision, [&] {
        const auto& before = session_.state();
        const int manager = request.owner.manager_slot;
        if (request.owner.kind != ui::DeskRequestKind::open_menu || request.owner.menu_kind != 7 ||
            request.owner.revision != revision || revision == UINT64_MAX || !before.memory.has(::bmh::layout::ActiveManager) ||
            manager != int(before.memory.u8(::bmh::layout::ActiveManager))) return false;
        auto staged = before;
        const auto eligible = request.kind == ui::RescheduleActionKind::move
            ? reschedulable_matches(before, manager) : moved_matches(before, manager);
        if (std::find(eligible.begin(), eligible.end(), request.match) != eligible.end())
            status = change(staged, manager, request.match, request.target_day, request.kind);
        if (status != RescheduleStatus::moved) return false;
        session_.state() = std::move(staged);
        ++revision;
        if (acknowledge) acknowledge();
        return true;
    });
    return status;
}

RescheduleStatus RescheduleActions::move(int manager, const LeagueMatchRef& match, int day, uint64_t& revision) {
    auto status = RescheduleStatus::invalid_match;
    apply(revision, [&] {
        status = change(session_.state(), manager, match, day, ui::RescheduleActionKind::move);
        if (status != RescheduleStatus::moved) return false;
        ++revision;
        return true;
    });
    return status;
}

void RescheduleActions::undo_console(int manager, int day, uint64_t& revision) {
    apply(revision, [&] {
        // Keep the Console's existing accepted-command revision after its own
        // observed moved-match selection, including Core's Boolean result.
        (void)change(session_.state(), manager, {}, day, ui::RescheduleActionKind::undo);
        ++revision;
        return true;
    });
}

bool ContractActions::dispatch(const ui::ContractRequest& request, uint64_t& revision,
    ui::ContractActionResult& result, std::string& error, const std::function<void()>& acknowledge) {
    bool accepted = false;
    try {
        return detail::apply_native(session_, revision, [&] {
            if (!session_.dispatch_contract(request, revision, result, error)) return false;
            accepted = true;
            if (acknowledge) acknowledge();
            return true;
        });
    } catch (...) {
        // Preserve the old SDL rejection's quote cleanup and consumed-ID policy.
        if (accepted) session_.clear_contract_quote();
        throw;
    }
}

ui::ContractRequest ContractActions::bind(const ui::ContractsState& observed,
    ui::ContractActionKind kind, uint64_t request_id) {
    ui::ContractRequest request;
    request.kind = kind;
    request.request_id = request_id;
    request.revision = observed.revision;
    request.manager_slot = observed.manager_slot;
    request.runtime_club = observed.runtime_club;
    return request;
}

ui::ContractRequest ContractActions::demands(const ui::ContractsState& observed,
    const ui::ContractPlayer& player, uint64_t request_id) {
    auto request = bind(observed, ui::ContractActionKind::request_demands, request_id);
    request.row = player.row;
    request.player_id = player.player_id;
    return request;
}

ui::ContractRequest ContractActions::team_bonus(const ui::ContractsState& observed,
    int index, uint8_t percent, uint64_t request_id) {
    auto request = bind(observed, ui::ContractActionKind::submit_bonuses, request_id);
    request.proposed_bonuses = observed.team_premiums;
    request.proposed_bonuses.at(size_t(index)) = percent;
    return request;
}

ui::ContractRequest ContractActions::insurance(const ui::ContractsState& observed, int row,
    int player, uint64_t quote, uint8_t form, uint8_t months, uint64_t request_id) {
    auto request = bind(observed, ui::ContractActionKind::insure_player, request_id);
    request.row = row; request.player_id = player; request.quote_id = quote;
    request.term_column = form; request.insurance_months = months;
    return request;
}

ui::ContractRequest ContractActions::offer(const ui::ContractsState& observed, int row,
    int player, uint64_t quote, const std::array<uint8_t,7>& bonuses, int years, uint32_t salary,
    const std::array<uint32_t,4>& demands, uint64_t request_id) {
    auto request = bind(observed, years > 0 ? ui::ContractActionKind::submit_offer : ui::ContractActionKind::submit_bonuses, request_id);
    request.row = row; request.player_id = player; request.quote_id = quote;
    request.proposed_bonuses = bonuses;
    if (years > 0) {
        request.term_column = uint8_t(years - 1);
        request.demand = demands.at(size_t(years - 1));
        request.monthly_salary = salary;
    }
    return request;
}

bool FriendlyActions::apply(uint64_t& revision, const std::function<bool()>& action) {
    return detail::apply_native(session_, revision, action);
}

FriendlyBookingRequest FriendlyActions::booking_request(const ui::FriendlyMatchRequest& observed) {
    FriendlyBookingQuote quote;
    quote.status = FriendlyQuoteStatus::quoted;
    quote.manager = observed.manager_slot;
    quote.opponent = observed.opponent;
    quote.manager_team = observed.runtime_team;
    quote.manager_home = observed.manager_home;
    quote.home_team = observed.home_team;
    quote.away_team = observed.away_team;
    quote.payer_manager = observed.payer_manager;
    quote.opponent_manager = observed.opponent_manager;
    quote.cost = observed.cost;
    quote.planner_count = uint8_t(observed.planner_count);
    return {quote, uint16_t(observed.season_day), observed.accept_opponent, observed.consenting_manager,
        observed.accept_cost, observed.approving_manager};
}

ui::FriendlyBookingAck FriendlyActions::acknowledgement(const ui::FriendlyMatchRequest& request,
    const FriendlyBookingResult& outcome, uint64_t revision) const {
    ui::FriendlyBookingAck ack;
    ack.request_id = request.request_id; ack.quote_id = request.quote_id; ack.revision = revision;
    ack.accepted = true; ack.manager_slot = request.manager_slot; ack.runtime_team = request.runtime_team;
    ack.opponent = request.opponent; ack.opponent_manager = outcome.opponent_manager;
    ack.season_day = request.season_day; ack.payer_manager = outcome.payer_manager;
    ack.home_team = outcome.home_team; ack.away_team = outcome.away_team; ack.cost = outcome.cost;
    ack.planner_count = session_.state().memory.u8(::bmh::layout::FriendlyPlanCount);
    return ack;
}

FriendlyActions::Booking FriendlyActions::book(const ui::FriendlyMatchRequest& request,
    uint64_t& revision, bool national, const Accept& acknowledge) {
    Booking result;
    result.applied = apply(revision, [&] {
        if (request.kind != ui::FriendlyRequestKind::book || request.revision != revision || revision == UINT64_MAX) {
            result.result.status = FriendlyBookingStatus::stale_quote;
            return false;
        }
        if (national) {
            if (!request.national_booking) return false;
            result.result = book_national_friendly_match(session_.state(), *request.national_booking, session_.rand(), session_.game());
        } else result.result = book_friendly_match(session_.state(), booking_request(request));
        if (result.result.status != FriendlyBookingStatus::booked) return false;
        const auto next_revision = revision + 1;
        const auto ack = acknowledgement(request, result.result, next_revision);
        if (ack.planner_count != request.planner_count + 2) {
            result.planner_mismatch = true;
            return false;
        }
        if (acknowledge) acknowledge(ack);
        // Preserve the old UI caller's revision during its acknowledgement.
        revision = next_revision;
        return true;
    });
    return result;
}

FriendlyActions::Booking FriendlyActions::book_club(const ui::FriendlyMatchRequest& request,
    uint64_t& revision, const Accept& acknowledge) { return book(request, revision, false, acknowledge); }
FriendlyActions::Booking FriendlyActions::book_national(const ui::FriendlyMatchRequest& request,
    uint64_t& revision, const Accept& acknowledge) { return book(request, revision, true, acknowledge); }

FriendlyBookingResult FriendlyActions::book_console(const FriendlyBookingQuote& quote, uint16_t day,
    bool accept_opponent, int consenting_manager, bool accept_cost, int approving_manager, uint64_t& revision) {
    FriendlyBookingResult result;
    apply(revision, [&] {
        result = book_friendly_match(session_.state(),
            {quote, day, accept_opponent, consenting_manager, accept_cost, approving_manager});
        if (result.status != FriendlyBookingStatus::booked) return false;
        ++revision;
        return true;
    });
    return result;
}

FriendlyBookingQuote FriendlyActions::quote(int manager, int opponent, bool home) const {
    return quote_friendly_match(session_.state(), manager, opponent, home);
}
NationalFriendlyQuote FriendlyActions::national_quote(const NationalFriendlyOwner& owner, int opponent, bool home) {
    return quote_national_friendly_match(session_.state(), owner, opponent, home, session_.rand());
}
FriendlyDateStatus FriendlyActions::date_status(const FriendlyBookingQuote& quote, uint16_t day) const {
    return friendly_match_date_status(session_.state(), quote, day);
}
FriendlyDateStatus FriendlyActions::national_date_status(const NationalFriendlyQuote& quote,
    const NationalFriendlyOwner& owner, uint16_t day) const {
    return national_friendly_match_date_status(session_.state(), quote, owner, session_.rand(), day);
}
NationalFriendlyOwner FriendlyActions::national_owner(const ui::FriendlyMatchRequest& request) {
    return {request.quote_id, request.revision, request.manager_slot, 0x165, 6};
}

} // namespace bmh::app
