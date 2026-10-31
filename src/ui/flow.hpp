// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <memory>
#include <optional>
#include <cstdint>
#include <string>

#include "ui/manager.hpp"
#include "ui/desktop.hpp"
#include "ui/preparation.hpp"
#include "ui/save_load.hpp"
#include "ui/stadium.hpp"
#include "ui/matchday_results_screen.hpp"
#include "ui/league_table.hpp"
#include "ui/league_table_range_dialog.hpp"
#include "ui/league_pairings.hpp"
#include "ui/mailbox_screen.hpp"
#include "ui/settings.hpp"
#include "ui/advertising.hpp"
#include "ui/friendly_match.hpp"
#include "ui/national_friendly_ui.hpp"
#include "ui/calendar.hpp"
#include "ui/strengths.hpp"
#include "ui/top_scorers.hpp"
#include "ui/desk_records.hpp"
#include "ui/opportunities.hpp"
#include "ui/statistics_rankings.hpp"
#include "ui/analytics.hpp"
#include "ui/minute_statistics.hpp"
#include "ui/transfer_history.hpp"
#include "ui/contracts.hpp"
#include "ui/outgoing_transfer.hpp"
#include "ui/balance_plan.hpp"
#include "ui/other_games.hpp"
#include "ui/alltime_table.hpp"
#include "ui/world_cup_statistics.hpp"
#include "ui/finance_totals.hpp"
#include "ui/series_records.hpp"
#include "ui/environment.hpp"
#include "ui/indoor_standings.hpp"
#include "ui/achievements.hpp"

namespace bmh::ui {

// Value boundary for the future engine adapter. No RNG, RAM or save writes.
struct NewCareerRequest {
    SetupOptions options;
    std::vector<ManagerChoice> managers;
};
struct InitializedManager { int slot = -1, master_club = -1, runtime_club = -1; };
struct CareerInitializationResult {
    DeskState desktop;
    std::vector<InitializedManager> managers;
    PreparationState preparation;
};
enum class FlowRequest { none, load_game, manager_confirmed, new_career, kickoff, desk_action, end_session, submenu_opened, submenu_closed, contracts_closed, submenu_action, file_action, results_closed, mailbox_action, mailbox_closed, settings_action, settings_close, settings_file_action, friendly_action, national_friendly_selected, contract_action, advertising_action, environment_action, join_options, join_manager, join_cancel, league_pairings_action, strengths_action, top_scorers_action, balance_action, other_games_action, league_family_action, opportunities_action, statistics_menu_action, statistics_rankings_action, analytics_action, statistics_page_action, reschedule_action };
enum class StatisticsPageActionKind : uint8_t { close, alltime, world_cup };
struct StatisticsPageAction { DeskRequest owner{}; int page=-1; StatisticsPageActionKind kind=StatisticsPageActionKind::close; int nation=-1; bool relative=false; bool operator==(const StatisticsPageAction&) const = default; };
struct FlowResult {
    bool changed = false;
    FlowRequest request = FlowRequest::none;
    DeskRequest desk_action{};
    PreparationRequest submenu_action{};
    SaveLoadRequest file_action{};
    MailboxActionRequest mailbox_action{};
    MailboxCloseRequest mailbox_close{};
    SettingsWriteRequest settings_action{};
    uint64_t settings_owner_sequence = 0;
    FriendlyMatchRequest friendly_action{};
    int national_friendly_opponent = -1;
    ContractRequest contract_action{};
    AdvertisingRequest advertising_action{};
    EnvironmentRequest environment_action{};
    SetupOptions join_options{};
    ManagerChoice join_manager{};
    LeaguePairingsRequest league_pairings_action{};
    StrengthsRequest strengths_action{};
    TopScorersRequest top_scorers_action{};
    OpportunitiesRequest opportunities_action{};
    StatisticsMenuRequest statistics_menu_action{};
    StatisticsRankingsRequest statistics_rankings_action{};
    AnalyticsRequest analytics_action{};
    StatisticsPageAction statistics_page_action{};
    BalanceRequest balance_action{};
    OtherGamesRequest other_games_action{};
    LeagueFamilyRequest league_family_action{};
    RescheduleAction reschedule_action{};
};

class StartupFlow {
public:
    explicit StartupFlow(const std::filesystem::path& data, std::filesystem::path save_directory = {},
        SetupLeagueSymbol symbol=SetupLeagueSymbol::source)
        : data_(data), save_directory_(save_directory.empty() ? data : std::move(save_directory)), setup_symbol_(symbol),setup_(data,symbol) {}
    FlowResult click(int x, int y, bool right = false, bool double_click = false);
    FlowResult pointer_down(int x,int y,bool right=false);
    FlowResult process_timer_expired(int pointer_x = 0, int pointer_y = 0);
    bool timed_notice_open() const;
    FlowResult expire_timed_notice();
    // The synchronous SDL question presenter consumes the original nation-click UP.
    // Call only after that modal has returned; right-DOWN close keeps its barrier.
    void acknowledge_national_friendly_modal_release() { national_friendly_release_barrier_=false; }
    FlowResult expire_stadium_tip();
    bool in_stadium() const { return bool(stadium_); }
    bool in_pitch() const { return bool(pitch_); }
    std::optional<Image> pitch_formation_save_feedback(bool restored) const;
    FlowResult pitch_held_left(int x, int y, uint64_t source_ticks);
    void consume_orphaned_pitch_release(){pitch_release_barrier_=false;}
    const StadiumScreen* stadium() const { return stadium_.get(); }
    const TotoScreen* toto() const { return toto_.get(); }
    FlowResult hold(int x, int y, int milliseconds);
    FlowResult move_action(int x, int y);
    bool move(int x, int y) { return move_action(x, y).changed; }
    bool text(std::string_view value);
    bool key(std::string_view value) { return send_key(value).changed; }
    FlowResult send_key(std::string_view value);
    bool resume_manager_selection(SelectionContext context);
    void show_manager_join(std::vector<ManagerChoice> existing, SelectionContext context);
    void resume_manager_join(SelectionContext context);
    void acknowledge_manager_join(CareerInitializationResult result);
    void reject_manager_join(std::string reason);
    const DeskRequest* join_origin() const { return joining_ ? &join_origin_ : nullptr; }
    const SetupOptions* pending_join_options() const { return join_options_pending_ ? &join_setup_->options() : nullptr; }
    const ManagerChoice* pending_join_manager() const { return join_choice_pending_ ? &join_choices_.back() : nullptr; }
    Image render() const;
    void load_reference(const std::filesystem::path& save, std::optional<bool> indoor = std::nullopt);
    void attach_initialized_career(CareerInitializationResult result);
    void acknowledge_desk(DeskState state);
    void reject_desk_action(std::string reason);
    void acknowledge_desk_notice(const DeskRequest& owner);
    void acknowledge_saved_game(DeskState desk, PreparationState preparation);
    // Complete an authenticated menu-layout operation while preserving the active career.
    void acknowledge_menu_layout(DeskState desk, PreparationState preparation);
    void reject_file_action(std::string reason);
    void attach_loaded_game(DeskState desk, PreparationState preparation);
    const SaveLoadRequest* pending_file() const {
        return save_load_ && save_load_->pending() ? &*save_load_->pending() : nullptr;
    }
    void attach_preparation_context(PreparationState state);
    void show_matchday_results(MatchdayResults results);
    bool in_results_view() const { return bool(matchday_results_); }
    bool in_settings() const { return bool(settings_ && !save_load_); }
    bool settings_readme_release_pending() const { return settings_readme_release_barrier_; }
    void show_league_tables(std::array<LeagueTableState, 9> tables, int initial_league);
    void show_family_league_tables(std::array<LeagueTableState, 9> tables, int initial_league,
        StrengthsIdentity identity, std::optional<PreparationState> preparation = {});
    void show_family_table_dispatch(std::array<LeagueTableState, 9> tables, int initial_league,
        StrengthsIdentity identity, std::optional<TableRangeDialogProfile> dialog = {},
        std::optional<TableDialogStage> dialog_stage = {}, bool forecast_needs_present = false,
        std::optional<PreparationState> preparation = {}, bool friendly_entry = false);
    // Accept a same-league internal selector operation without replacing its visible Table frame.
    void acknowledge_family_selector(const LeagueFamilyRequest& old_request,
                                     const StrengthsIdentity& accepted_identity);
    void begin_league_family(StrengthsIdentity identity);
    bool friendly_table_active() const { return friendly_table_active_; }
    void acknowledge_friendly_own_club(const LeagueFamilyRequest&,StrengthsIdentity);
    void complete_friendly_opponent_table(const LeagueFamilyRequest&,FriendlyMatchState,PreparationState);
    void close_league_family(const LeagueFamilyRequest&, PreparationState);
    void reject_league_family(std::string reason);
    const std::optional<StrengthsIdentity>& league_family_identity() const { return league_family_identity_; }
    uint8_t league_family_child() const { return league_family_child_; }
    void replace_family_child();
    const LeagueFamilyRequest* pending_league_family_action() const;
    FlowResult after_family_table_present(uint64_t absolute_source_units);
    FlowResult tick_family_table_source_units(uint64_t absolute_source_units);
    FlowResult cancel_family_table_from_source_event();
    bool in_family_table_range_dialog() const { return bool(family_table_range_dialog_); }
    bool family_table_range_input_ready() const {
        return family_table_range_dialog_ && !family_table_dispatch_pending_ &&
            family_table_range_dialog_->source_input_ready();
    }
    bool in_toto_calculator() const { return toto_ && toto_->calculator_open(); }
    bool toto_calculator_input_ready() const { return toto_ && toto_->calculator_input_ready(); }
    FlowResult after_toto_present(uint64_t source_units);
    FlowResult tick_toto_source_units(uint64_t source_units);
    void show_league_pairings(LeaguePairingsState);
    void show_family_league_pairings(LeaguePairingsState, StrengthsIdentity);
    void acknowledge_league_pairings(LeaguePairingsState, std::optional<StrengthsIdentity> family = {});
    void close_league_pairings(PreparationState);
    void reject_league_pairings(std::string reason);
    bool in_league_pairings() const {return bool(league_pairings_);}
    bool in_family_table() const { return league_table_ && league_family_identity_ && league_family_child_==1; }
    FlowResult tick_league_pairings_source_seconds(uint64_t now);
    void mark_league_pairings_presented_source_seconds(uint64_t now);
    const LeaguePairingsScreen* league_pairings() const {return league_pairings_.get();}
    const LeaguePairingsRequest* pending_league_pairings() const {
        return league_pairings_&&league_pairings_->pending()?&*league_pairings_->pending():nullptr;
    }
    const LeagueFamilyRequest* pending_pairings_family_action() const {
        return league_pairings_&&league_pairings_->pending_family_action()?&*league_pairings_->pending_family_action():nullptr;
    }
    void show_mailbox(MailboxState state);
    // Source's MailboxRecipientMask popup is consumed after the native desk draw, without a
    // synthetic DeskRequest. The app polls the Session latch only at an idle desk.
    void show_notified_mailbox(MailboxState state);
    bool in_mailbox() const { return bool(mailbox_); }
    const MailboxActionRequest* pending_mailbox() const { return mailbox_ && mailbox_->pending() ? &*mailbox_->pending() : nullptr; }
    const MailboxCloseRequest* pending_mailbox_close() const { return pending_mailbox_close_ ? &*pending_mailbox_close_ : nullptr; }
    const BankRequest* pending_bank_action() const {
        return bank_ && bank_->pending() ? &*bank_->pending() : nullptr;
    }
    const TransferMarketRequest* pending_transfer_market() const {
        return transfer_market_ && transfer_market_->pending() ? &*transfer_market_->pending() : nullptr;
    }
    const OutgoingTransferRequest* pending_transfer_offer() const {
        return outgoing_transfer_ && outgoing_transfer_->pending() ? &*outgoing_transfer_->pending() : nullptr;
    }
    void show_transfer_offer(PreparationState, OutgoingTransferEditorState);
    void acknowledge_transfer_offer(PreparationState, OutgoingTransferEditorState);
    void reject_transfer_offer(std::string reason);
    void acknowledge_mailbox(MailboxState state, PreparationState preparation);
    void acknowledge_mailbox_close(const MailboxCloseRequest&, DeskState, PreparationState);
    void reject_mailbox_close(std::string reason);
    void reject_mailbox(std::string reason);
    const SettingsWriteRequest* pending_settings() const { return settings_ && settings_->pending() ? settings_->pending() : nullptr; }
    void acknowledge_settings(SettingsState state, DeskState desk, PreparationState preparation);
    void reject_settings(std::string reason);
    void acknowledge_settings_notice_release();
    void acknowledge_settings_file(SettingsState state, DeskState desk, PreparationState preparation);
    void reject_settings_file(std::string reason);
    void cancel_settings_file();
    void acknowledge_settings_closed(SettingsState, DeskState, PreparationState);
    void reject_settings_close(std::string reason);
    void show_settings(SettingsState state, uint64_t owner_sequence = 0, DeskRequest owner = {});
    uint64_t settings_owner_sequence() const { return settings_owner_sequence_; }
    const DeskRequest& settings_desk_owner() const { return settings_desk_owner_; }
    uint64_t settings_owner_revision() const { return settings_ ? settings_->state().revision : settings_desk_owner_.revision; }
    void show_advertising(AdvertisingState state);
    const AdvertisingRequest* pending_advertising_action() const {
        return advertising_ && advertising_->pending() ? &*advertising_->pending() : nullptr;
    }
    void acknowledge_advertising(AdvertisingState, DeskState, PreparationState);
    void reject_advertising_action(std::string reason);
    void show_environment(EnvironmentView, const DeskRequest& owner);
    const EnvironmentScreen* environment() const { return environment_.get(); }
    const EnvironmentRequest* pending_environment() const {
        return environment_ && environment_->pending() ? &*environment_->pending() : nullptr;
    }
    void acknowledge_environment(EnvironmentView, std::optional<PreparationState> preparation = {});
    void reject_environment_action(std::string reason);
    bool in_environment() const { return bool(environment_); }
    void show_achievements(AchievementsState, const DeskRequest& owner);
    bool in_achievements() const { return bool(achievements_); }
    const AchievementsScreen* achievements() const { return achievements_.get(); }
    void show_strengths(StrengthsState, PreparationState, const DeskRequest& owner);
    void show_family_strengths(StrengthsState, PreparationState);
    void acknowledge_strengths(StrengthsState, PreparationState);
    void close_strengths(StrengthsState, PreparationState);
    void reject_strengths(std::string reason);
    bool in_strengths() const { return bool(strengths_); }
    const StrengthsScreen* strengths() const { return strengths_.get(); }
    const StrengthsRequest* pending_strengths() const {
        return strengths_&&strengths_->pending()?&*strengths_->pending():nullptr;
    }
    void show_top_scorers(TopScorersState, PreparationState, const DeskRequest& owner);
    void acknowledge_top_scorers(TopScorersState, PreparationState);
    void close_top_scorers(TopScorersState, PreparationState);
    void reject_top_scorers(std::string reason);
    bool in_top_scorers() const { return bool(top_scorers_); }
    const TopScorersScreen* top_scorers() const { return top_scorers_.get(); }
    const TopScorersRequest* pending_top_scorers() const {
        return top_scorers_ && top_scorers_->pending() ? &*top_scorers_->pending() : nullptr;
    }
    void show_desk_records(DeskRecordsState, const DeskRequest& owner);
    bool in_desk_records() const { return bool(desk_records_); }
    const DeskRecordsScreen* desk_records() const { return desk_records_.get(); }
    bool desk_records_points_active() const { return desk_records_&&desk_records_->points_active(); }
    bool advance_desk_records_point() { return desk_records_&&desk_records_->advance_point(); }
    bool finish_desk_records_points() { return desk_records_&&desk_records_->finish_points(); }
    void show_opportunities(OpportunitiesState, PreparationState, const DeskRequest& owner);
    void show_statistics_menu(const DeskRequest& owner, int transfer_history_count = 0);
    void show_alltime_table(AllTimeTableState);
    void show_world_cup_statistics(WorldCupStatisticsProjection);
    void show_finance_totals(FinanceTotals);
    void show_series_records(SeriesRecordsProjection);
    FlowResult finish_statistics_page_input(bool changed);
    const StatisticsPageAction* pending_statistics_page_action() const { return statistics_page_action_ ? &*statistics_page_action_ : nullptr; }
    void close_statistics_page();
    bool in_statistics_page() const { return bool(alltime_table_||world_cup_statistics_||finance_totals_||series_records_); }
    void show_statistics_rankings(StatisticsRankingState state);
    void show_analytics(AnalyticsState state);
    void acknowledge_analytics(AnalyticsState state, PreparationState preparation);
    void close_analytics();
    void reject_analytics();
    bool in_analytics() const { return bool(analytics_); }
    const AnalyticsScreen* analytics() const { return analytics_.get(); }
    const AnalyticsRequest* pending_analytics() const { return analytics_ && analytics_->pending() ? &*analytics_->pending() : nullptr; }
    void show_minute_statistics(MinuteStatisticsProjection projection);
    void show_transfer_history(TransferHistoryProjection projection, const GameState& formatting_state,
                               int source_origin_y);
    void acknowledge_statistics_rankings(StatisticsRankingState state);
    void close_statistics_rankings();
    void reject_statistics_rankings();
    bool in_statistics_rankings() const { return bool(statistics_rankings_); }
    bool in_minute_statistics() const { return bool(minute_statistics_); }
    bool in_transfer_history() const { return bool(transfer_history_); }
    const TransferHistoryScreen* transfer_history() const { return transfer_history_.get(); }
    const StatisticsRankingsScreen* statistics_rankings() const { return statistics_rankings_.get(); }
    const StatisticsRankingsRequest* pending_statistics_rankings() const {
        return statistics_rankings_ && statistics_rankings_->pending() ? &*statistics_rankings_->pending() : nullptr;
    }
    void close_statistics_menu();
    void reject_statistics_menu(std::string reason);
    bool in_statistics_menu() const { return bool(statistics_menu_); }
    const StatisticsMenuScreen* statistics_menu() const { return statistics_menu_.get(); }
    const StatisticsMenuRequest* pending_statistics_menu() const {
        return statistics_menu_ && statistics_menu_->pending() ? &*statistics_menu_->pending() : nullptr;
    }
    void acknowledge_opportunities(OpportunitiesState, PreparationState);
    void close_opportunities(OpportunitiesState, PreparationState);
    void close_opportunities_for_statistics_parent(OpportunitiesState, PreparationState);
    void reject_opportunities(std::string reason);
    bool in_opportunities() const { return bool(opportunities_); }
    const OpportunitiesScreen* opportunities() const { return opportunities_.get(); }
    const OpportunitiesRequest* pending_opportunities() const {
        return opportunities_ && opportunities_->pending() ? &*opportunities_->pending() : nullptr;
    }
    void show_balance(BalanceState, const DeskRequest& owner);
    void acknowledge_balance(BalanceState);
    void close_balance(BalanceState, PreparationState);
    void reject_balance(std::string reason);
    bool in_balance() const { return bool(balance_); }
    const BalanceScreen* balance() const { return balance_.get(); }
    const BalanceRequest* pending_balance() const { return balance_&&balance_->pending()?&*balance_->pending():nullptr; }
    void show_other_games(OtherGamesState, PreparationState, const DeskRequest& owner);
    void acknowledge_other_games(OtherGamesState, PreparationState);
    void close_other_games(OtherGamesState, PreparationState);
    void reject_other_games(std::string reason);
    bool in_other_games() const { return bool(other_games_); }
    const OtherGamesScreen* other_games() const { return other_games_.get(); }
    const OtherGamesRequest* pending_other_games_action() const {
        return other_games_ && other_games_->pending() ? &*other_games_->pending() : nullptr;
    }
    FlowResult tick_balance_source_milliseconds(uint64_t now);
    void mark_balance_presented_source_milliseconds(uint64_t now);
    FlowResult tick_stadium_calculator_source_ticks(unsigned ticks);
    void mark_stadium_calculator_presented();
    void show_calendar(CalendarState state, const DeskRequest& owner);
    const RescheduleAction* pending_reschedule_action() const {
        return calendar_ && calendar_->pending_reschedule() ? &*calendar_->pending_reschedule() : nullptr;
    }
    void acknowledge_reschedule(CalendarState state, DeskRequest owner, DeskState desktop,
                                PreparationState preparation);
    void reject_reschedule();
    bool source_mouse_wait_release_pending() const {
        return indoor_standings_release_barrier_ || reschedule_release_barrier_ || achievements_release_barrier_ || desk_records_release_barrier_;
    }
    const CalendarScreen* calendar() const { return calendar_.get(); }
    void show_indoor_standings(IndoorStandingsState state, const DeskRequest& owner);
    const IndoorStandingsScreen* indoor_standings() const { return indoor_standings_.get(); }
    bool in_indoor_standings() const { return bool(indoor_standings_); }
    bool in_reschedule_calendar() const { return calendar_ && calendar_->state().reschedule_manager >= 0; }
    void show_friendly_match(FriendlyMatchState state, int menu_kind = 6);
    const LeagueTableScreen* friendly_opponent_picker() const {
        return friendly_table_active_ ? league_table_.get()
            : friendly_match_ ? friendly_match_->opponent_table() : nullptr;
    }
    const FriendlyMatchScreen* friendly_question_screen() const {
        return friendly_match_ && friendly_match_->active_question() && !pending_friendly_action_
            ? friendly_match_.get() : nullptr;
    }
    FlowResult expire_friendly_question(bool release_consumed = false);
    bool friendly_question_release_pending() const { return friendly_question_release_barrier_; }
    const FriendlyQuoteProjection* friendly_calendar_quote() const {
        return friendly_match_ && friendly_match_->stage() == FriendlyStage::calendar && friendly_match_->quote()
            ? &*friendly_match_->quote() : nullptr;
    }
    void show_national_friendly_picker(WorldCupStatisticsProjection, const DeskRequest& owner,
                                       int runtime_team);
    const NationalFriendlyUiOwner* national_friendly_owner() const {
        return national_friendly_picker_ ? &national_friendly_owner_ : nullptr;
    }
    const DeskRequest* national_friendly_desk_owner() const {
        return national_friendly_picker_ ? &national_friendly_desk_owner_ : nullptr;
    }
    bool national_friendly_family_active() const { return national_friendly_picker_; }
    void close_national_friendly_picker();
    void show_national_friendly_calendar(FriendlyMatchState, FriendlyQuoteProjection,
                                         NationalFriendlyBookingRequest);
    const FriendlyMatchRequest* pending_friendly_action() const {
        return friendly_match_ && pending_friendly_action_ ? &*pending_friendly_action_ : nullptr;
    }
    bool acknowledge_friendly_quote(FriendlyQuoteProjection quote);
    bool acknowledge_friendly_booking(FriendlyBookingAck ack);
    bool acknowledge_friendly_booking(FriendlyBookingAck ack, DeskState desk, PreparationState preparation);
    void reject_friendly_action(std::string reason);
    void show_contracts(ContractsState state);
    const ContractRequest* pending_contract_action() const { return contracts_ ? contracts_->pending() : nullptr; }
    void acknowledge_contract_quote(ContractsState state, DeskState desk, PreparationState preparation);
    void acknowledge_contract_offer(ContractsState state, DeskState desk, PreparationState preparation);
    void acknowledge_contract_bonuses(ContractsState state, DeskState desk, PreparationState preparation);
    void acknowledge_contract_insurance(ContractsState state, DeskState desk, PreparationState preparation);
    void reject_contract_action(std::string reason);
    bool in_readonly_view() const { return bool(desk_records_ || achievements_ || indoor_standings_ || other_games_ || strengths_ || top_scorers_ || opportunities_ || statistics_menu_ || statistics_rankings_ || analytics_ || minute_statistics_ || transfer_history_ || league_pairings_ || calendar_ || league_table_ || mailbox_ || settings_ || advertising_ || alltime_table_ || world_cup_statistics_ || finance_totals_ || series_records_); }
    void acknowledge_preparation(PreparationState state, std::string transfer_notice = {});
    void reject_preparation_action(std::string reason);
    void acknowledge_unchanged_transfer_market();
    void acknowledge_transfer_market_preview(PreparationState, TransferMarketPreview);
    bool matches_pending_lineup(const LineupRequest&) const;
    bool matches_pending_pitch(const PitchRequest&) const;
    bool matches_pending_stadium(const StadiumRequest&) const;
    bool in_preparation_menu() const { return bool(other_games_ || balance_ || player_card_ || pitch_ || lineup_ || training_ || training_camp_ || bank_ || stadium_ || environment_ || toto_ || transfer_market_ || tactics_ || save_load_ || friendly_match_ || contracts_); }
    bool file_info_press(int x, int y) { return save_load_ && save_load_->info_press(x, y); }
    const DesktopScreen* desktop() const { return desktop_.get(); }
    std::string screen_name() const;
    std::string state_json() const;
    const SetupOptions& options() const { return setup_.options(); }
    const NewCareerRequest* pending_career() const { return pending_.get(); }
    const ManagerChoice* pending_manager() const { return awaiting_context_ ? &confirmed_.back() : nullptr; }
private:
    FlowResult finish_statistics_rankings_input(bool changed);
    FlowResult finish_analytics_input(bool changed);
    bool joining_ = false, join_options_pending_ = false, join_choice_pending_ = false;
    DeskRequest join_origin_;
    std::unique_ptr<SetupScreen> join_setup_;
    std::vector<ManagerChoice> join_choices_;
    uint64_t join_context_revision_ = 0;
    void acknowledge_contract_mutation(ContractsState, DeskState, PreparationState, ContractActionKind);
    std::filesystem::path data_;
    std::filesystem::path save_directory_;
    SetupLeagueSymbol setup_symbol_;
    SetupScreen setup_;
    std::unique_ptr<ManagerScreen> manager_;
    std::vector<ManagerChoice> confirmed_;
    std::unique_ptr<NewCareerRequest> pending_;
    std::unique_ptr<DesktopScreen> desktop_;
    std::unique_ptr<MatchdayResultsScreen> matchday_results_;
    std::unique_ptr<CalendarScreen> calendar_;
    std::unique_ptr<IndoorStandingsScreen> indoor_standings_;
    bool indoor_standings_release_barrier_ = false;
    std::unique_ptr<StrengthsScreen> strengths_;
    std::unique_ptr<TopScorersScreen> top_scorers_;
    std::unique_ptr<DeskRecordsScreen> desk_records_;
    bool desk_records_release_barrier_ = false;
    std::unique_ptr<OpportunitiesScreen> opportunities_;
    std::unique_ptr<StatisticsMenuScreen> statistics_menu_;
    std::unique_ptr<StatisticsRankingsScreen> statistics_rankings_;
    std::unique_ptr<AnalyticsScreen> analytics_;
    std::unique_ptr<MinuteStatisticsScreen> minute_statistics_;
    std::unique_ptr<TransferHistoryScreen> transfer_history_;
    std::unique_ptr<AllTimeTableScreen> alltime_table_;
    std::unique_ptr<WorldCupStatisticsScreen> world_cup_statistics_;
    bool national_friendly_picker_ = false;
    bool national_friendly_release_barrier_ = false;
    NationalFriendlyUiOwner national_friendly_owner_{};
    DeskRequest national_friendly_desk_owner_{};
    std::unique_ptr<FinanceTotalsScreen> finance_totals_;
    std::unique_ptr<SeriesRecordsScreen> series_records_;
    std::optional<StatisticsPageAction> statistics_page_action_;
    int transfer_history_origin_y_ = 0;
    std::unique_ptr<LeagueTableScreen> league_table_;
    std::unique_ptr<LeagueTableRangeDialog> family_table_range_dialog_;
    std::optional<TableRangeDialogProfile> family_table_range_profile_;
    std::optional<TableDialogStage> family_table_range_stage_;
    std::optional<TableRangeDialogOutcome> family_table_last_outcome_;
    std::optional<LeagueFamilyRequest> family_table_dispatch_pending_;
    bool family_table_forecast_needs_present_ = false;
    bool family_table_source_clock_anchor_valid_ = false;
    bool family_table_reset_clock_after_present_ = false;
    uint64_t family_table_source_clock_anchor_ = 0;
    std::optional<StrengthsIdentity> league_family_identity_;
    bool friendly_table_active_ = false;
    uint8_t league_family_child_ = 0;
    bool family_table_release_barrier_ = false;
    std::unique_ptr<LeaguePairingsScreen> league_pairings_;
    std::unique_ptr<BalanceScreen> balance_;
    std::unique_ptr<OtherGamesScreen> other_games_;
    std::unique_ptr<MailboxScreen> mailbox_;
    std::optional<MailboxCloseRequest> pending_mailbox_close_;
    std::unique_ptr<SettingsScreen> settings_;
    std::unique_ptr<SettingsReadmeScreen> settings_readme_;
    uint64_t settings_owner_sequence_ = 0;
    DeskRequest settings_desk_owner_{};
    bool settings_release_barrier_ = false;
    bool settings_readme_release_barrier_ = false;
    std::unique_ptr<AdvertisingScreen> advertising_;
    uint64_t advertising_generation_ = 1;
    std::unique_ptr<FriendlyMatchScreen> friendly_match_;
    bool friendly_question_release_barrier_ = false;
    std::unique_ptr<ContractsScreen> contracts_;
    std::unique_ptr<EnvironmentScreen> environment_;
    std::unique_ptr<AchievementsScreen> achievements_;
    DeskRequest environment_owner_{};
    uint64_t contracts_generation_ = 1;
    std::optional<FriendlyMatchRequest> pending_friendly_action_;
    uint64_t next_friendly_request_id_ = 1;
    std::unique_ptr<PreparationState> preparation_;
    std::unique_ptr<LineupScreen> lineup_;
    std::unique_ptr<TrainingScreen> training_;
    std::unique_ptr<TrainingCampScreen> training_camp_;
    std::unique_ptr<BankScreen> bank_;
    std::unique_ptr<StadiumScreen> stadium_;
    std::unique_ptr<TotoScreen> toto_;
    std::unique_ptr<TransferMarketScreen> transfer_market_;
    std::unique_ptr<OutgoingTransferScreen> outgoing_transfer_;
    std::unique_ptr<SaveLoadScreen> save_load_;
    std::unique_ptr<TacticsScreen> tactics_;
    std::unique_ptr<PitchScreen> pitch_;
    bool pitch_entry_pending_ = false;
    std::unique_ptr<PlayerCardScreen> player_card_;
    FlowResult route_desk(DeskRequest);
    FlowResult finish_preparation_input(PreparationRequest, bool changed);
    FlowResult finish_file_input(bool changed);
    FlowResult finish_settings_input(bool changed);
    FlowResult finish_readonly_input(bool changed);
    FlowResult finish_league_pairings_input(bool changed);
    FlowResult finish_strengths_input(bool changed);
    FlowResult finish_top_scorers_input(bool changed);
    FlowResult finish_opportunities_input(bool changed);
    FlowResult finish_statistics_menu_input(bool changed);
    FlowResult finish_balance_input(bool changed);
    FlowResult finish_other_games_input(bool changed);
    bool engine_attached_ = false;
    bool balance_release_barrier_ = false;
    bool achievements_release_barrier_ = false;
    bool pitch_release_barrier_ = false;
    bool pitch_hold_release_barrier_ = false;
    bool reschedule_release_barrier_ = false;
    bool awaiting_context_ = false;
    std::vector<int> confirmed_runtime_teams_;
    uint64_t roster_revision_ = 0;
    std::string roster_source_ = "master_database";
};

}  // namespace bmh::ui
