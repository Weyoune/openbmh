// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/world_layout.hpp"
#include "ui/flow.hpp"

#include <algorithm>
#include <limits>

namespace bmh::ui {

void StartupFlow::show_desk_records(DeskRecordsState state,const DeskRequest& owner) {
    if(!desktop_||!engine_attached_||in_preparation_menu()||in_readonly_view()||matchday_results_||
       !desktop_->pending()||*desktop_->pending()!=owner||owner.kind!=DeskRequestKind::open_menu||
       (owner.menu_kind!=25&&owner.menu_kind!=26)||owner.manager_slot!=desktop_->state().manager_slot||
       owner.revision!=desktop_->state().revision)
        throw FormatError("desk records: mismatched menu owner");
    auto screen=std::make_unique<DeskRecordsScreen>(data_,std::move(state));
    desktop_->resolve_local_menu();desk_records_=std::move(screen);
}

FlowResult StartupFlow::finish_strengths_input(bool changed) {
    if(const auto* pending=pending_strengths()) {
        if(pending->kind==StrengthsAction::pairings||pending->kind==StrengthsAction::table||
           pending->kind==StrengthsAction::close) {
            FlowResult result{changed,FlowRequest::league_family_action};
            result.league_family_action.identity=pending->identity;
            result.league_family_action.from_child=3;
            result.league_family_action.kind=pending->kind==StrengthsAction::close
                ?LeagueFamilyRequestKind::close:LeagueFamilyRequestKind::navigate;
            result.league_family_action.target_child=pending->kind==StrengthsAction::pairings?2:
                pending->kind==StrengthsAction::table?1:0;
            return result;
        }
        FlowResult result{changed,FlowRequest::strengths_action};result.strengths_action=*pending;return result;
    }
    return {changed};
}
FlowResult StartupFlow::finish_top_scorers_input(bool changed) {
    if(const auto* pending=pending_top_scorers()) {
        FlowResult result{changed,FlowRequest::top_scorers_action};
        result.top_scorers_action=*pending;
        return result;
    }
    return {changed};
}
FlowResult StartupFlow::finish_opportunities_input(bool changed) {
    if(const auto* pending=pending_opportunities()) {
        FlowResult result{changed,FlowRequest::opportunities_action};
        result.opportunities_action=*pending;return result;
    }
    return {changed};
}
FlowResult StartupFlow::finish_statistics_menu_input(bool changed) {
    if(const auto* pending=pending_statistics_menu()) {
        FlowResult result{changed,FlowRequest::statistics_menu_action};
        result.statistics_menu_action=*pending;return result;
    }
    return {changed};
}
FlowResult StartupFlow::finish_statistics_page_input(bool changed) {
    if(statistics_page_action_) {
        FlowResult result{changed,FlowRequest::statistics_page_action};
        result.statistics_page_action=*statistics_page_action_;
        return result;
    }
    return {changed};
}
FlowResult StartupFlow::finish_statistics_rankings_input(bool changed) {
    if(const auto* pending=pending_statistics_rankings()) {
        FlowResult result{changed,FlowRequest::statistics_rankings_action};
        result.statistics_rankings_action=*pending;return result;
    }
    return {changed};
}
FlowResult StartupFlow::finish_analytics_input(bool changed) {
    if (const auto* pending = pending_analytics()) {
        FlowResult result{changed, FlowRequest::analytics_action};
        result.analytics_action = *pending;
        return result;
    }
    return {changed};
}
FlowResult StartupFlow::finish_balance_input(bool changed) {
    if(const auto* pending=pending_balance()) {
        FlowResult result{changed,FlowRequest::balance_action};result.balance_action=*pending;return result;
    }
    return {changed};
}
FlowResult StartupFlow::finish_other_games_input(bool changed) {
    if (const auto* pending = pending_other_games_action()) {
        FlowResult result{changed, FlowRequest::other_games_action};
        result.other_games_action = *pending;
        return result;
    }
    return {changed};
}
namespace {
void validate_strengths_preparation(const StrengthsIdentity& id,const DeskState& before,const PreparationState& next) {
    if(next.desktop.revision!=before.revision+1||next.desktop.revision!=id.revision||
       id.manager!=before.manager_slot||id.runtime_club!=before.runtime_club||id.day_counter!=before.day_counter||
       id.manager<0||size_t(id.manager)>=before.managers.size()||
       id.club_identity!=before.managers[size_t(id.manager)].club_identity_key)
        throw FormatError("strengths: mismatched desk acknowledgement");
    auto expected=before;expected.revision=id.revision;
    if(desk_json(expected)!=desk_json(next.desktop))throw FormatError("strengths: unrelated desk state changed");
    validate_preparation_context(next);
}
void validate_other_games_preparation(const OtherGamesIdentity& id, const DeskState& before,
                                     const PreparationState& next) {
    if (next.desktop.revision != before.revision + 1 || next.desktop.revision != id.revision ||
        id.manager_slot != before.manager_slot || id.runtime_team != before.runtime_club ||
        id.day_counter != before.day_counter || id.manager_slot < 0 ||
        size_t(id.manager_slot) >= before.managers.size() ||
        id.club_identity != int(before.managers[size_t(id.manager_slot)].club_identity_key))
        throw FormatError("other games: mismatched desk acknowledgement");
    auto expected = before;
    expected.revision = id.revision;
    if (desk_json(expected) != desk_json(next.desktop))
        throw FormatError("other games: unrelated desk state changed");
    validate_preparation_context(next);
}
}
void StartupFlow::show_strengths(StrengthsState page,PreparationState preparation,const DeskRequest& owner) {
    if(!desktop_||!engine_attached_||in_preparation_menu()||in_readonly_view()||matchday_results_||
       !desktop_->pending()||*desktop_->pending()!=owner||owner.kind!=DeskRequestKind::open_menu||
       owner.menu_kind!=2||owner.manager_slot!=desktop_->state().manager_slot||
       owner.revision!=desktop_->state().revision||page.identity.sequence!=0||!page.identity.scope)
        throw FormatError("strengths: mismatched opening acknowledgement");
    validate_strengths_preparation(page.identity,desktop_->state(),preparation);
    auto screen=std::make_unique<StrengthsScreen>(data_,std::move(page));
    auto desk=std::make_unique<DesktopScreen>(data_,preparation.desktop);
    auto next=std::make_unique<PreparationState>(std::move(preparation));
    desktop_->resolve_local_menu();desktop_=std::move(desk);preparation_=std::move(next);strengths_=std::move(screen);
    league_family_identity_=strengths_->state().identity;league_family_child_=3;
}
void StartupFlow::replace_family_child() {
    if(!league_family_identity_||!league_family_child_)
        throw FormatError("league family: no active child to replace");
    strengths_.reset();league_pairings_.reset();league_table_.reset();
    family_table_range_dialog_.reset();family_table_range_profile_.reset();
    family_table_range_stage_.reset();family_table_last_outcome_.reset();
    family_table_dispatch_pending_.reset();family_table_forecast_needs_present_=false;
    family_table_source_clock_anchor_valid_=false;family_table_reset_clock_after_present_=false;
    family_table_release_barrier_=false;
}
const LeagueFamilyRequest* StartupFlow::pending_league_family_action() const {
    if(family_table_dispatch_pending_)return &*family_table_dispatch_pending_;
    if(const auto* pairing=pending_pairings_family_action())return pairing;
    if(league_table_&&league_table_->pending_family_action())return &*league_table_->pending_family_action();
    return nullptr;
}
void StartupFlow::show_family_strengths(StrengthsState page,PreparationState preparation) {
    if(!desktop_||!engine_attached_||!league_family_identity_||
       page.identity.manager!=league_family_identity_->manager||
       page.identity.runtime_club!=league_family_identity_->runtime_club||
       page.identity.club_identity!=league_family_identity_->club_identity||
       page.identity.day_counter!=league_family_identity_->day_counter||
       preparation.desktop.revision!=page.identity.revision||
       page.identity.source_param2!=league_family_identity_->source_param2||
       page.identity.family_id!=league_family_identity_->family_id||
       page.identity.league_group!=league_family_identity_->league_group||
       page.identity.manager_default_league!=league_family_identity_->manager_default_league)
        throw FormatError("league family: mismatched Strengths transition");
    validate_strengths_preparation(page.identity,desktop_->state(),preparation);
    auto screen=std::make_unique<StrengthsScreen>(data_,page);
    auto desk=std::make_unique<DesktopScreen>(data_,preparation.desktop);
    auto next=std::make_unique<PreparationState>(std::move(preparation));
    replace_family_child();desktop_=std::move(desk);preparation_=std::move(next);
    strengths_=std::move(screen);league_family_identity_=page.identity;league_family_child_=3;
}
void StartupFlow::show_top_scorers(TopScorersState page,PreparationState preparation,const DeskRequest& owner) {
    if(!desktop_||!engine_attached_||in_preparation_menu()||in_readonly_view()||matchday_results_||
       !desktop_->pending()||*desktop_->pending()!=owner||owner.kind!=DeskRequestKind::open_menu||
       owner.menu_kind!=20||owner.manager_slot!=desktop_->state().manager_slot||
       owner.revision!=desktop_->state().revision||page.identity.manager_slot!=owner.manager_slot||
       page.identity.runtime_team!=desktop_->state().runtime_club||
       page.identity.day_counter!=desktop_->state().day_counter||
       page.identity.revision!=desktop_->state().revision+1||page.identity.sequence!=0||!page.identity.scope||
       page.identity.club_identity!=int(desktop_->state().managers.at(size_t(owner.manager_slot)).club_identity_key))
        throw FormatError("top scorers: mismatched opening acknowledgement");
    if(preparation.desktop.revision!=page.identity.revision||preparation.desktop.manager_slot!=owner.manager_slot||
       preparation.desktop.runtime_club!=desktop_->state().runtime_club||
       preparation.desktop.day_counter!=desktop_->state().day_counter)
        throw FormatError("top scorers: opening changed unrelated desk state");
    auto expected=desktop_->state();expected.revision=page.identity.revision;
    if(desk_json(expected)!=desk_json(preparation.desktop))
        throw FormatError("top scorers: opening desk acknowledgement differs beyond revision");
    validate_preparation_context(preparation);
    auto screen=std::make_unique<TopScorersScreen>(data_,std::move(page));
    auto desk=std::make_unique<DesktopScreen>(data_,preparation.desktop);
    auto next=std::make_unique<PreparationState>(std::move(preparation));
    desktop_->resolve_local_menu();desktop_=std::move(desk);preparation_=std::move(next);top_scorers_=std::move(screen);
}
void StartupFlow::acknowledge_top_scorers(TopScorersState page,PreparationState preparation) {
    const auto* pending=pending_top_scorers();
    auto expected_id=pending?pending->identity:TopScorersIdentity{};
    if(pending){++expected_id.revision;++expected_id.sequence;}
    if(!pending||!top_scorers_||!desktop_||pending->action==TopScorersAction::close||
       page.identity!=expected_id||
       page.metric!=(pending->action==TopScorersAction::set_metric?pending->metric:top_scorers_->state().metric)||
       page.scope!=(pending->action==TopScorersAction::toggle_scope?pending->scope:top_scorers_->state().scope))
        throw FormatError("top scorers: mismatched view acknowledgement");
    const auto& prior=desktop_->state();
    if(page.identity.revision!=prior.revision+1||preparation.desktop.revision!=page.identity.revision||
       preparation.desktop.manager_slot!=prior.manager_slot||preparation.desktop.runtime_club!=prior.runtime_club||
       preparation.desktop.day_counter!=prior.day_counter)
        throw FormatError("top scorers: view action changed unrelated desk state");
    auto expected=prior;expected.revision=page.identity.revision;
    if(desk_json(expected)!=desk_json(preparation.desktop))
        throw FormatError("top scorers: view action desk acknowledgement differs beyond revision");
    validate_preparation_context(preparation);
    auto screen=std::make_unique<TopScorersScreen>(data_,std::move(page));
    auto desk=std::make_unique<DesktopScreen>(data_,preparation.desktop);
    auto next=std::make_unique<PreparationState>(std::move(preparation));
    desktop_=std::move(desk);preparation_=std::move(next);top_scorers_=std::move(screen);
}
void StartupFlow::close_top_scorers(TopScorersState page,PreparationState preparation) {
    const auto* pending=pending_top_scorers();
    if(!pending||!desktop_||!top_scorers_||pending->action!=TopScorersAction::close)
        throw FormatError("top scorers: no close acknowledgement owner");
    auto expected_id=pending->identity;++expected_id.revision;++expected_id.sequence;
    if(page.identity!=expected_id||page.metric!=top_scorers_->state().metric||page.scope!=top_scorers_->state().scope)
        throw FormatError("top scorers: mismatched close acknowledgement");
    const auto& prior=desktop_->state();
    if(page.identity.revision!=prior.revision+1||preparation.desktop.revision!=page.identity.revision||
       preparation.desktop.manager_slot!=prior.manager_slot||preparation.desktop.runtime_club!=prior.runtime_club||
       preparation.desktop.day_counter!=prior.day_counter)
        throw FormatError("top scorers: close changed unrelated desk state");
    auto expected=prior;expected.revision=page.identity.revision;
    if(desk_json(expected)!=desk_json(preparation.desktop))
        throw FormatError("top scorers: close desk acknowledgement differs beyond revision");
    validate_preparation_context(preparation);
    auto desk=std::make_unique<DesktopScreen>(data_,preparation.desktop);
    auto next=std::make_unique<PreparationState>(std::move(preparation));
    desktop_=std::move(desk);preparation_=std::move(next);top_scorers_.reset();
}
void StartupFlow::reject_top_scorers(std::string reason) {
    if(!top_scorers_)throw FormatError("top scorers: missing rejection owner");
    top_scorers_->reject_action(std::move(reason));
}
void StartupFlow::show_opportunities(OpportunitiesState page,PreparationState preparation,const DeskRequest& owner) {
    const bool desktop_pending=desktop_&&desktop_->pending()&&*desktop_->pending()==owner;
    const bool returned_selector=statistics_menu_&&statistics_menu_->state().owner==owner&&desktop_&&
        !desktop_->pending()&&desktop_->state().revision==owner.revision;
    if(!desktop_||!engine_attached_||in_preparation_menu()||(in_readonly_view()&&!statistics_menu_)||matchday_results_||
       (!desktop_pending&&!returned_selector)||owner.kind!=DeskRequestKind::open_menu||owner.menu_kind!=23||
       owner.manager_slot!=desktop_->state().manager_slot||owner.revision!=desktop_->state().revision||
       page.identity.manager_slot!=owner.manager_slot||page.identity.runtime_team!=desktop_->state().runtime_club||
       page.identity.day_counter!=desktop_->state().day_counter||page.identity.revision!=desktop_->state().revision+1||
       page.identity.sequence!=0||page.identity.stable_club_identity!=int(desktop_->state().managers.at(size_t(owner.manager_slot)).club_identity_key))
        throw FormatError("opportunities: mismatched opening acknowledgement");
    if(preparation.desktop.revision!=page.identity.revision||preparation.desktop.manager_slot!=owner.manager_slot||
       preparation.desktop.runtime_club!=desktop_->state().runtime_club||preparation.desktop.day_counter!=desktop_->state().day_counter)
        throw FormatError("opportunities: opening changed unrelated desk state");
    auto expected=desktop_->state();expected.revision=page.identity.revision;
    if(desk_json(expected)!=desk_json(preparation.desktop))throw FormatError("opportunities: opening desk acknowledgement differs beyond revision");
    validate_preparation_context(preparation);
    auto screen=std::make_unique<OpportunitiesScreen>(data_,std::move(page));
    auto desk=std::make_unique<DesktopScreen>(data_,preparation.desktop);
    auto next=std::make_unique<PreparationState>(std::move(preparation));
    if(desktop_->pending())desktop_->resolve_local_menu();
    statistics_rankings_.reset();analytics_.reset();minute_statistics_.reset();transfer_history_.reset();
    alltime_table_.reset();world_cup_statistics_.reset();finance_totals_.reset();series_records_.reset();statistics_page_action_.reset();
    desktop_=std::move(desk);preparation_=std::move(next);opportunities_=std::move(screen);
    if(statistics_menu_) {
        statistics_menu_->clear_pending();
        auto parent=statistics_menu_->state().owner;parent.revision=desktop_->state().revision;
        statistics_menu_->update_owner(parent);
        if(national_friendly_picker_) {
            national_friendly_owner_.revision=parent.revision;
            national_friendly_desk_owner_.revision=parent.revision;
        }
    }
}
void StartupFlow::show_statistics_menu(const DeskRequest& owner, int transfer_history_count) {
    if(!desktop_||!engine_attached_||in_preparation_menu()||in_readonly_view()||matchday_results_||
       !desktop_->pending()||*desktop_->pending()!=owner||owner.kind!=DeskRequestKind::open_menu||
       owner.menu_kind!=23||owner.manager_slot!=desktop_->state().manager_slot||
       owner.revision!=desktop_->state().revision)
        throw FormatError("statistics menu: mismatched Source menu-23 owner");
    if(transfer_history_count<0||transfer_history_count>25)
        throw FormatError("statistics menu: invalid transfer-history count");
    auto screen=std::make_unique<StatisticsMenuScreen>(data_,StatisticsMenuState{owner,desktop_->render(),transfer_history_count,desktop_->state().gradient_text});
    statistics_menu_=std::move(screen);
}
void StartupFlow::show_alltime_table(AllTimeTableState state) {
    const bool selected=statistics_menu_&&statistics_menu_->pending()&&
        !statistics_menu_->pending()->close&&statistics_menu_->pending()->selected_index==3;
    const bool returned=statistics_menu_&&desktop_&&!desktop_->pending()&&
        !statistics_menu_->pending()&&statistics_menu_->state().owner.revision==desktop_->state().revision;
    if(!statistics_menu_||!desktop_||(!selected&&!returned)||state.owner!=statistics_menu_->state().owner||
       state.owner.manager_slot!=desktop_->state().manager_slot||state.underlay.width!=320||state.underlay.height!=240)
        throw FormatError("all-time statistics: mismatched page-3 owner");
    auto screen=std::make_unique<AllTimeTableScreen>(data_,std::move(state));
    if(desktop_->pending())desktop_->resolve_local_menu();
    statistics_menu_->clear_pending();statistics_page_action_.reset();alltime_table_=std::move(screen);
    world_cup_statistics_.reset();finance_totals_.reset();series_records_.reset();
    statistics_rankings_.reset();analytics_.reset();minute_statistics_.reset();transfer_history_.reset();
}
void StartupFlow::show_world_cup_statistics(WorldCupStatisticsProjection state) {
    const bool selected=statistics_menu_&&statistics_menu_->pending()&&
        !statistics_menu_->pending()->close&&statistics_menu_->pending()->selected_index==4;
    const bool returned=statistics_menu_&&desktop_&&!desktop_->pending()&&
        !statistics_menu_->pending()&&statistics_menu_->state().owner.revision==desktop_->state().revision;
    if(!statistics_menu_||!desktop_||(!selected&&!returned)||statistics_menu_->state().owner.manager_slot<0||
       state.can_select_opponent!=national_friendly_picker_||
       (national_friendly_picker_&&(national_friendly_owner_.sequence==0||national_friendly_owner_.desk_menu!=6||
        national_friendly_desk_owner_.kind!=DeskRequestKind::open_menu||
        national_friendly_desk_owner_.revision!=desktop_->state().revision)))
        throw FormatError("world cup statistics: mismatched read-only page-4 owner");
    auto screen=std::make_unique<WorldCupStatisticsScreen>(data_,std::move(state),statistics_menu_->render_page(4));
    if(desktop_->pending())desktop_->resolve_local_menu();
    statistics_menu_->clear_pending();statistics_page_action_.reset();world_cup_statistics_=std::move(screen);
    alltime_table_.reset();finance_totals_.reset();series_records_.reset();
    statistics_rankings_.reset();analytics_.reset();minute_statistics_.reset();transfer_history_.reset();
}

void StartupFlow::show_national_friendly_picker(WorldCupStatisticsProjection projection,
        const DeskRequest& owner, int runtime_team) {
    if (!desktop_ || !engine_attached_ || in_preparation_menu() || in_readonly_view() || matchday_results_ ||
        !desktop_->pending() || *desktop_->pending() != owner || owner.kind != DeskRequestKind::open_menu ||
        owner.menu_kind != 6 || owner.revision != desktop_->state().revision ||
        owner.manager_slot != desktop_->state().manager_slot || runtime_team != bmh::no_team() ||
        size_t(owner.manager_slot) >= desktop_->state().manager_teams.size() ||
        desktop_->state().manager_teams[size_t(owner.manager_slot)] != bmh::no_team() ||
        !projection.can_select_opponent || next_friendly_request_id_ == UINT64_MAX)
        throw FormatError("national friendly: expected the active menu-6 national caller");
    NationalFriendlyUiOwner ui_owner{6, owner.manager_slot, runtime_team,
                                     owner.revision, next_friendly_request_id_++};
    DeskRequest parent_owner=owner;
    parent_owner.menu_kind=23;
    auto parent=std::make_unique<StatisticsMenuScreen>(data_, StatisticsMenuState{parent_owner, desktop_->render(), 0, desktop_->state().gradient_text});
    auto screen=std::make_unique<WorldCupStatisticsScreen>(data_,std::move(projection),
                                                           parent->render_page(4));
    desktop_->resolve_local_menu();
    national_friendly_picker_=true;
    national_friendly_owner_=ui_owner;
    national_friendly_desk_owner_=owner;
    statistics_menu_=std::move(parent);
    world_cup_statistics_=std::move(screen);
    alltime_table_.reset();finance_totals_.reset();series_records_.reset();
}

void StartupFlow::close_national_friendly_picker() {
    if(!national_friendly_picker_)return;
    if(statistics_menu_)close_statistics_menu();
    else {
        national_friendly_picker_=false;
        national_friendly_owner_={};
        national_friendly_desk_owner_={};
        world_cup_statistics_.reset();
    }
}

void StartupFlow::show_national_friendly_calendar(FriendlyMatchState state,
        FriendlyQuoteProjection quote, NationalFriendlyBookingRequest booking) {
    const NationalFriendlyOwner engine_owner{national_friendly_owner_.sequence,
        national_friendly_owner_.revision,national_friendly_owner_.manager,
        national_friendly_owner_.runtime_team,national_friendly_owner_.desk_menu};
    if(!national_friendly_picker_||!world_cup_statistics_||!desktop_||!engine_attached_||
       booking.owner!=engine_owner||booking.quote.owner!=engine_owner||
       booking.quote.terms.status!=FriendlyQuoteStatus::quoted||
       national_friendly_desk_owner_.revision!=desktop_->state().revision||
       national_friendly_desk_owner_.manager_slot!=state.manager_slot||
       state.runtime_team!=bmh::no_team()||!state.national_team||state.revision!=desktop_->state().revision||
       state.manager_slot!=desktop_->state().manager_slot||quote.request_id!=booking.owner.request_id||
       quote.revision!=booking.owner.revision||quote.opponent!=booking.quote.terms.opponent||
       quote.manager_slot!=booking.owner.manager||state.manager_name!=original_to_utf8(desktop_->state().manager_name)||
       next_friendly_request_id_==UINT64_MAX)
        throw FormatError("national friendly: stale owner or quote before calendar");
    state.first_request_id=next_friendly_request_id_++;
    auto screen=std::make_unique<FriendlyMatchScreen>(data_,std::move(state),std::move(quote),
                                                       std::move(booking),6);
    world_cup_statistics_.reset();national_friendly_picker_=false;
    national_friendly_owner_={};national_friendly_desk_owner_={};
    statistics_menu_.reset();
    friendly_match_=std::move(screen);pending_friendly_action_.reset();
}
void StartupFlow::show_finance_totals(FinanceTotals state) {
    const bool selected=statistics_menu_&&statistics_menu_->pending()&&
        !statistics_menu_->pending()->close&&statistics_menu_->pending()->selected_index==5;
    const bool returned=statistics_menu_&&desktop_&&!desktop_->pending()&&
        !statistics_menu_->pending()&&statistics_menu_->state().owner.revision==desktop_->state().revision;
    if(!statistics_menu_||!desktop_||(!selected&&!returned)||state.manager_slot!=desktop_->state().manager_slot)
        throw FormatError("statistics finance: mismatched page-5 owner");
    auto screen=std::make_unique<FinanceTotalsScreen>(data_,std::move(state),statistics_menu_->render_page(5));
    if(desktop_->pending())desktop_->resolve_local_menu();
    statistics_menu_->clear_pending();statistics_page_action_.reset();finance_totals_=std::move(screen);
    alltime_table_.reset();world_cup_statistics_.reset();series_records_.reset();
    statistics_rankings_.reset();analytics_.reset();minute_statistics_.reset();transfer_history_.reset();
}
void StartupFlow::show_series_records(SeriesRecordsProjection state) {
    const bool selected=statistics_menu_&&statistics_menu_->pending()&&
        !statistics_menu_->pending()->close&&statistics_menu_->pending()->selected_index==6;
    const bool returned=statistics_menu_&&desktop_&&!desktop_->pending()&&
        !statistics_menu_->pending()&&statistics_menu_->state().owner.revision==desktop_->state().revision;
    if(!statistics_menu_||!desktop_||(!selected&&!returned)||state.manager_slot!=desktop_->state().manager_slot)
        throw FormatError("series records: mismatched page-6 owner");
    auto screen=std::make_unique<SeriesRecordsScreen>(data_,std::move(state),statistics_menu_->render_page(6));
    if(desktop_->pending())desktop_->resolve_local_menu();
    statistics_menu_->clear_pending();statistics_page_action_.reset();series_records_=std::move(screen);
    alltime_table_.reset();world_cup_statistics_.reset();finance_totals_.reset();
    statistics_rankings_.reset();analytics_.reset();minute_statistics_.reset();transfer_history_.reset();
}
void StartupFlow::close_statistics_page() {
    if(!in_statistics_page()||!statistics_menu_||!desktop_)
        throw FormatError("statistics page: no idle child to close");
    alltime_table_.reset();world_cup_statistics_.reset();finance_totals_.reset();series_records_.reset();
    statistics_page_action_.reset();
    close_statistics_menu();
}
void StartupFlow::show_statistics_rankings(StatisticsRankingState state) {
    const auto* pending=statistics_menu_&&statistics_menu_->pending()?&*statistics_menu_->pending():nullptr;
    const bool initial_menu=statistics_menu_&&desktop_&&desktop_->pending()&&
        statistics_menu_->state().owner==*desktop_->pending()&&
        statistics_menu_->state().owner.kind==DeskRequestKind::open_menu&&
        statistics_menu_->state().owner.menu_kind==23;
    const bool selected_history=pending&&!pending->close&&pending->selected_index==7;
    const bool returned_parent=statistics_menu_&&desktop_&&!desktop_->pending()&&!pending&&
        statistics_menu_->state().owner.kind==DeskRequestKind::open_menu&&
        statistics_menu_->state().owner.menu_kind==23&&
        statistics_menu_->state().owner.revision==desktop_->state().revision;
    if(!statistics_menu_||!desktop_||(!initial_menu&&!selected_history&&!returned_parent)||
       state.category!=0||state.first_season!=0||state.manager_slot!=desktop_->state().manager_slot||
       state.underlay.width!=320||state.underlay.height!=240)
        throw FormatError("statistics rankings: mismatched global-index-7 opening");
    auto screen=std::make_unique<StatisticsRankingsScreen>(data_,std::move(state));
    if(desktop_->pending())desktop_->resolve_local_menu();
    statistics_menu_->clear_pending();analytics_.reset();minute_statistics_.reset();transfer_history_.reset();statistics_rankings_=std::move(screen);
    alltime_table_.reset();world_cup_statistics_.reset();finance_totals_.reset();series_records_.reset();statistics_page_action_.reset();
}
void StartupFlow::show_minute_statistics(MinuteStatisticsProjection projection) {
    const auto* pending=statistics_menu_&&statistics_menu_->pending()?&*statistics_menu_->pending():nullptr;
    const bool selected_case8=pending&&!pending->close&&pending->selected_index==8;
    const bool returned_parent=statistics_menu_&&desktop_&&!desktop_->pending()&&!pending&&
        statistics_menu_->state().owner.kind==DeskRequestKind::open_menu&&
        statistics_menu_->state().owner.menu_kind==23&&
        statistics_menu_->state().owner.revision==desktop_->state().revision;
    if(!statistics_menu_||!desktop_||(!selected_case8&&!returned_parent)||
       projection.manager_slot!=desktop_->state().manager_slot)
        throw FormatError("minute statistics: mismatched global-index-8 owner");
    auto screen=std::make_unique<MinuteStatisticsScreen>(data_,std::move(projection));
    if(desktop_->pending())desktop_->resolve_local_menu();
    statistics_menu_->clear_pending();statistics_rankings_.reset();analytics_.reset();transfer_history_.reset();minute_statistics_=std::move(screen);
    alltime_table_.reset();world_cup_statistics_.reset();finance_totals_.reset();series_records_.reset();statistics_page_action_.reset();
}
void StartupFlow::show_transfer_history(TransferHistoryProjection projection,
                                       const GameState& formatting_state, int source_origin_y) {
    const auto* pending=statistics_menu_&&statistics_menu_->pending()?&*statistics_menu_->pending():nullptr;
    const bool selected_transfer=pending&&!pending->close&&pending->selected_index==1;
    const bool returned_parent=statistics_menu_&&desktop_&&!desktop_->pending()&&!pending&&
        statistics_menu_->state().owner.kind==DeskRequestKind::open_menu&&
        statistics_menu_->state().owner.menu_kind==23&&
        statistics_menu_->state().owner.revision==desktop_->state().revision;
    if(!statistics_menu_||!desktop_||(!selected_transfer&&!returned_parent)||
       projection.manager_slot!=desktop_->state().manager_slot||source_origin_y<0||source_origin_y>255)
        throw FormatError("transfer history: mismatched global-index-1 owner");
    auto screen=std::make_unique<TransferHistoryScreen>(data_,formatting_state,std::move(projection));
    if(desktop_->pending())desktop_->resolve_local_menu();
    statistics_menu_->clear_pending();statistics_rankings_.reset();analytics_.reset();minute_statistics_.reset();
    transfer_history_origin_y_=source_origin_y;
    transfer_history_=std::move(screen);
    alltime_table_.reset();world_cup_statistics_.reset();finance_totals_.reset();series_records_.reset();statistics_page_action_.reset();
}
void StartupFlow::show_analytics(AnalyticsState state) {
    const auto* pending=statistics_menu_&&statistics_menu_->pending()?&*statistics_menu_->pending():nullptr;
    const bool selected=pending&&!pending->close&&pending->selected_index==2;
    const bool returned=statistics_menu_&&desktop_&&!desktop_->pending()&&!pending&&
        statistics_menu_->state().owner.kind==DeskRequestKind::open_menu&&
        statistics_menu_->state().owner.menu_kind==23&&
        statistics_menu_->state().owner.revision==desktop_->state().revision;
    if(!statistics_menu_||!desktop_||(!selected&&!returned)||opportunities_||
       state.owner!=statistics_menu_->state().owner||state.manager_slot!=state.owner.manager_slot||
       state.manager_slot!=desktop_->state().manager_slot||state.underlay.width!=320||state.underlay.height!=240)
        throw FormatError("analytics: mismatched global-index-2 owner");
    auto screen=std::make_unique<AnalyticsScreen>(data_,std::move(state));
    if(desktop_->pending())desktop_->resolve_local_menu();
    statistics_menu_->clear_pending();statistics_rankings_.reset();minute_statistics_.reset();
    transfer_history_.reset();analytics_=std::move(screen);
    alltime_table_.reset();world_cup_statistics_.reset();finance_totals_.reset();series_records_.reset();statistics_page_action_.reset();
}
void StartupFlow::acknowledge_analytics(AnalyticsState state,PreparationState preparation) {
    const auto* pending=pending_analytics();
    if(!pending||!analytics_||!statistics_menu_||statistics_menu_->pending()||
       state.manager_slot!=analytics_->state().manager_slot||state.manager_slot!=pending->manager_slot||
       state.underlay.pixels!=analytics_->state().underlay.pixels||
       state.source_flags!=pending->target_flags||pending->owner!=analytics_->state().owner)
        throw FormatError("analytics: mismatched preference acknowledgement");
    auto expected_owner=pending->owner;
    if(expected_owner.revision==UINT64_MAX)throw FormatError("analytics: revision exhausted");
    ++expected_owner.revision;
    if(state.owner!=expected_owner||!desktop_||state.owner.manager_slot!=desktop_->state().manager_slot)
        throw FormatError("analytics: accepted owner did not advance once");
    const auto& prior=desktop_->state();
    auto expected_desk=prior;expected_desk.revision=state.owner.revision;
    if(preparation.desktop.revision!=state.owner.revision||desk_json(expected_desk)!=desk_json(preparation.desktop))
        throw FormatError("analytics: preference acknowledgement changed unrelated desk state");
    validate_preparation_context(preparation);
    auto next_screen=std::make_unique<AnalyticsScreen>(data_,std::move(state));
    auto next_desk=std::make_unique<DesktopScreen>(data_,preparation.desktop);
    auto next_preparation=std::make_unique<PreparationState>(std::move(preparation));
    statistics_menu_->update_owner(next_screen->state().owner);
    if(national_friendly_picker_) {
        national_friendly_owner_.revision=next_screen->state().owner.revision;
        national_friendly_desk_owner_.revision=next_screen->state().owner.revision;
    }
    analytics_=std::move(next_screen);desktop_=std::move(next_desk);preparation_=std::move(next_preparation);
}
void StartupFlow::close_analytics() {
    if(!analytics_)throw FormatError("analytics: no active page");
    analytics_.reset();
    if(statistics_menu_)close_statistics_menu();
}
void StartupFlow::reject_analytics() {
    if(!analytics_||!analytics_->pending())throw FormatError("analytics: no pending action");
    analytics_->reject();
}
void StartupFlow::acknowledge_statistics_rankings(StatisticsRankingState state) {
    const auto* pending=pending_statistics_rankings();
    if(!pending||pending->kind!=StatisticsRankingsRequestKind::select_category||!statistics_rankings_||
       state.category!=pending->category||state.first_season!=0||
       state.manager_slot!=statistics_rankings_->state().manager_slot||
       state.underlay.pixels!=statistics_rankings_->state().underlay.pixels)
        throw FormatError("statistics rankings: mismatched category acknowledgement");
    statistics_rankings_->acknowledge_category(std::move(state));
}
void StartupFlow::close_statistics_rankings() {
    if(!statistics_rankings_)throw FormatError("statistics rankings: no active history view");
    statistics_rankings_.reset();
}
void StartupFlow::reject_statistics_rankings() {
    if(!statistics_rankings_||!statistics_rankings_->pending())
        throw FormatError("statistics rankings: no pending action");
    statistics_rankings_->reject_action();
}
void StartupFlow::close_statistics_menu() {
    if(!statistics_menu_)throw FormatError("statistics menu: no active selector");
    if(desktop_&&desktop_->pending())desktop_->resolve_local_menu();
    statistics_menu_.reset();statistics_rankings_.reset();analytics_.reset();minute_statistics_.reset();transfer_history_.reset();
    alltime_table_.reset();world_cup_statistics_.reset();finance_totals_.reset();series_records_.reset();statistics_page_action_.reset();
    national_friendly_picker_=false;national_friendly_owner_={};national_friendly_desk_owner_={};
}
void StartupFlow::reject_statistics_menu(std::string reason) {
    if(!statistics_menu_||!statistics_menu_->pending()||reason.empty())
        throw FormatError("statistics menu: rejection requires a pending selection");
    statistics_menu_->reject(std::move(reason));
}
void StartupFlow::acknowledge_opportunities(OpportunitiesState page,PreparationState preparation) {
    const auto* pending=pending_opportunities();
    if(!pending||!desktop_||pending->action==OpportunitiesAction::close||
       page.identity.manager_slot!=pending->identity.manager_slot||page.identity.runtime_team!=pending->identity.runtime_team||
       page.identity.stable_club_identity!=pending->identity.stable_club_identity||page.identity.day_counter!=pending->identity.day_counter||
       page.identity.scope!=pending->identity.scope||page.identity.sequence!=pending->identity.sequence+1||
       page.identity.revision!=pending->identity.revision+1||page.mode!=pending->mode)
        throw FormatError("opportunities: mismatched mode acknowledgement");
    const auto& prior=desktop_->state();
    if(page.identity.revision!=prior.revision+1||preparation.desktop.revision!=page.identity.revision||
       preparation.desktop.manager_slot!=prior.manager_slot||preparation.desktop.runtime_club!=prior.runtime_club||
       preparation.desktop.day_counter!=prior.day_counter)throw FormatError("opportunities: mode action changed unrelated desk state");
    auto expected=prior;expected.revision=page.identity.revision;
    if(desk_json(expected)!=desk_json(preparation.desktop))throw FormatError("opportunities: mode desk acknowledgement differs beyond revision");
    validate_preparation_context(preparation);
    auto screen=std::make_unique<OpportunitiesScreen>(data_,std::move(page));
    auto desk=std::make_unique<DesktopScreen>(data_,preparation.desktop);
    auto next=std::make_unique<PreparationState>(std::move(preparation));desktop_=std::move(desk);preparation_=std::move(next);opportunities_=std::move(screen);
    if(statistics_menu_) {
        statistics_menu_->clear_pending();
        auto parent=statistics_menu_->state().owner;parent.revision=desktop_->state().revision;
        statistics_menu_->update_owner(parent);
    }
}
void StartupFlow::close_opportunities(OpportunitiesState page,PreparationState preparation) {
    const auto* pending=pending_opportunities();
    if(!pending||!desktop_||pending->action!=OpportunitiesAction::close)throw FormatError("opportunities: no close acknowledgement owner");
    auto id=pending->identity;++id.revision;++id.sequence;
    if(page.identity!=id||page.mode!=opportunities_->state().mode)throw FormatError("opportunities: mismatched close acknowledgement");
    const auto& prior=desktop_->state();auto expected=prior;expected.revision=page.identity.revision;
    if(page.identity.revision!=prior.revision+1||desk_json(expected)!=desk_json(preparation.desktop))
        throw FormatError("opportunities: close changed unrelated desk state");
    validate_preparation_context(preparation);
    auto desk=std::make_unique<DesktopScreen>(data_,preparation.desktop);
    auto next=std::make_unique<PreparationState>(std::move(preparation));desktop_=std::move(desk);preparation_=std::move(next);opportunities_.reset();
    if(statistics_menu_) {
        auto parent=statistics_menu_->state();
        parent.owner.revision=desktop_->state().revision;
        statistics_menu_=std::make_unique<StatisticsMenuScreen>(data_,std::move(parent));
        if(national_friendly_picker_) {
            national_friendly_owner_.revision=desktop_->state().revision;
            national_friendly_desk_owner_.revision=desktop_->state().revision;
        }
    }
}
void StartupFlow::close_opportunities_for_statistics_parent(OpportunitiesState page,PreparationState preparation) {
    if(!opportunities_||!statistics_menu_||opportunities_->pending()||!desktop_)
        throw FormatError("opportunities: parent switch requires an idle active child");
    auto id=opportunities_->state().identity;++id.revision;++id.sequence;
    if(page.identity!=id||page.mode!=opportunities_->state().mode)
        throw FormatError("opportunities: parent-switch close acknowledgement mismatch");
    const auto& prior=desktop_->state();auto expected=prior;expected.revision=page.identity.revision;
    if(page.identity.revision!=prior.revision+1||desk_json(expected)!=desk_json(preparation.desktop))
        throw FormatError("opportunities: parent-switch close changed unrelated desk state");
    validate_preparation_context(preparation);
    auto desk=std::make_unique<DesktopScreen>(data_,preparation.desktop);
    auto next=std::make_unique<PreparationState>(std::move(preparation));
    desktop_=std::move(desk);preparation_=std::move(next);opportunities_.reset();
    statistics_menu_->clear_pending();
    auto owner=statistics_menu_->state().owner;owner.revision=desktop_->state().revision;
    statistics_menu_->update_owner(owner);
    if(national_friendly_picker_) {
        national_friendly_owner_.revision=owner.revision;
        national_friendly_desk_owner_.revision=owner.revision;
    }
}
void StartupFlow::reject_opportunities(std::string reason) {
    if(!opportunities_||!opportunities_->pending())throw FormatError("opportunities: no pending action to reject");
    opportunities_->reject(std::move(reason));
}
void StartupFlow::acknowledge_strengths(StrengthsState page,PreparationState preparation) {
    const auto* pending=pending_strengths();
    if(!pending||!desktop_||pending->kind==StrengthsAction::close||
       page.identity.manager!=pending->identity.manager||page.identity.runtime_club!=pending->identity.runtime_club||
       page.identity.club_identity!=pending->identity.club_identity||page.identity.day_counter!=pending->identity.day_counter||
       page.identity.scope!=pending->identity.scope||page.identity.sequence!=pending->identity.sequence+1||
       page.identity.revision!=pending->identity.revision+1||
       page.identity.family_id!=pending->identity.family_id||
       page.identity.family_sequence!=pending->identity.family_sequence||
       page.identity.source_param2!=pending->identity.source_param2||
       page.identity.manager_default_league!=pending->identity.manager_default_league||
       (pending->kind==StrengthsAction::control&&page.identity.league!=pending->identity.league)||
       (pending->kind==StrengthsAction::team_info&&(page.identity.league!=pending->identity.league||
           page.league_group!=strengths_->state().league_group||pending->value<0||
           std::none_of(page.projection.rows.begin(),page.projection.rows.end(),[&](const auto& row) {
               return row.runtime_team==pending->value;
           })))||
       (pending->kind==StrengthsAction::select_league&&page.identity.league!=pending->value)||
       (pending->kind==StrengthsAction::toggle_group&&page.league_group!=(strengths_->state().league_group^1)))
        throw FormatError("strengths: mismatched page acknowledgement");
    validate_strengths_preparation(page.identity,desktop_->state(),preparation);
    auto screen=std::make_unique<StrengthsScreen>(data_,std::move(page));
    auto desk=std::make_unique<DesktopScreen>(data_,preparation.desktop);
    auto next=std::make_unique<PreparationState>(std::move(preparation));
    desktop_=std::move(desk);preparation_=std::move(next);strengths_=std::move(screen);
    if(league_family_identity_)league_family_identity_=strengths_->state().identity;
}
void StartupFlow::close_strengths(StrengthsState page,PreparationState preparation) {
    const auto* pending=pending_strengths();
    if(!pending||!desktop_||pending->kind!=StrengthsAction::close)
        throw FormatError("strengths: no close acknowledgement owner");
    auto expected=pending->identity;++expected.revision;++expected.sequence;
    if(page.identity!=expected)throw FormatError("strengths: mismatched close acknowledgement");
    validate_strengths_preparation(page.identity,desktop_->state(),preparation);
    auto desk=std::make_unique<DesktopScreen>(data_,preparation.desktop);
    auto next=std::make_unique<PreparationState>(std::move(preparation));
    desktop_=std::move(desk);preparation_=std::move(next);strengths_.reset();
}
void StartupFlow::reject_strengths(std::string reason) {
    if(!strengths_)throw FormatError("strengths: missing rejection owner");
    strengths_->reject_action(std::move(reason));
}

void StartupFlow::show_other_games(OtherGamesState page, PreparationState preparation,
                                   const DeskRequest& owner) {
    if (!desktop_ || !engine_attached_ || in_preparation_menu() || in_readonly_view() ||
        matchday_results_ || !desktop_->pending() || *desktop_->pending() != owner ||
        owner.kind != DeskRequestKind::open_menu || owner.menu_kind != 5 ||
        owner.manager_slot != desktop_->state().manager_slot ||
        owner.revision != desktop_->state().revision ||
        page.identity.manager_slot != owner.manager_slot ||
        page.identity.runtime_team != desktop_->state().runtime_club ||
        page.identity.day_counter != desktop_->state().day_counter ||
        page.identity.revision != owner.revision + 1 || page.identity.sequence != 0 ||
        !page.identity.scope || page.identity.expected_view_byte != 0x80 ||
        page.projection.display_mode != OtherGamesDisplayMode::detail ||
        page.source_participant_offset != 0)
        throw FormatError("other games: mismatched opening acknowledgement");
    validate_other_games_preparation(page.identity, desktop_->state(), preparation);
    auto screen = std::make_unique<OtherGamesScreen>(data_, std::move(page));
    auto desk = std::make_unique<DesktopScreen>(data_, preparation.desktop);
    auto next = std::make_unique<PreparationState>(std::move(preparation));
    desktop_->resolve_local_menu();
    desktop_ = std::move(desk);
    preparation_ = std::move(next);
    other_games_ = std::move(screen);
}

void StartupFlow::acknowledge_other_games(OtherGamesState page, PreparationState preparation) {
    const auto* pending = pending_other_games_action();
    if (!pending || !other_games_ || !desktop_ || pending->action == OtherGamesAction::close)
        throw FormatError("other games: no supported view acknowledgement owner");
    auto expected_id = pending->identity;
    ++expected_id.revision;
    ++expected_id.sequence;
    const auto& before = other_games_->state();
    const auto& old_projection = before.projection;
    const auto& next_projection = page.projection;
    uint8_t expected_view = before.identity.expected_view_byte;
    switch (pending->action) {
    case OtherGamesAction::select_competition: {
        if (pending->value < 0 || pending->value >= 8)
            throw FormatError("other games: selector action is out of range");
        const int selected = old_projection.selectors[size_t(pending->value)].competition;
        if (selected < 0 || selected == 20 || next_projection.competition != selected)
            throw FormatError("other games: selector acknowledgement chose a different competition");
        int offset = before.source_participant_offset;
        const bool paging_same_selected_id = selected == old_projection.competition &&
                                             old_projection.source_participant_count > 16;
        const bool reset_by_original_slot_comparison = !paging_same_selected_id &&
                                                       old_projection.competition != pending->value;
        if (paging_same_selected_id) {
            offset += 16;
            if (old_projection.source_participant_count <= offset) offset = 0;
        } else if (reset_by_original_slot_comparison) {
            offset = 0;
        } else {
            offset = int(expected_view & 0x7f);
        }
        expected_view = uint8_t((expected_view & 0x80) | (offset & 0x7f));
        if (next_projection.source_participant_count <= 16) expected_view |= 0x80;
        if (page.source_participant_offset != offset ||
            (reset_by_original_slot_comparison && next_projection.linked_page != 0) ||
            (!reset_by_original_slot_comparison && next_projection.linked_page != old_projection.linked_page))
            throw FormatError("other games: selector acknowledgement changed the Source cursor incorrectly");
        break;
    }
    case OtherGamesAction::select_linked_page: {
        const auto target = std::find_if(old_projection.linked_pages.begin(), old_projection.linked_pages.end(),
            [&](const auto& linked) { return linked.ordinal == pending->value; });
        const int saved_offset = int(before.identity.expected_view_byte & 0x7f);
        if (pending->value < 0 || target == old_projection.linked_pages.end() ||
            next_projection.competition != old_projection.competition ||
            next_projection.linked_page != pending->value ||
            next_projection.entry_index != target->entry_index ||
            next_projection.descriptor_competition != target->competition ||
            next_projection.round != std::min(next_projection.current_round, next_projection.rounds - 1) ||
            page.source_participant_offset != saved_offset)
            throw FormatError("other games: linked-page acknowledgement differs from saved-cursor selection");
        if (next_projection.source_participant_count <= 16) expected_view |= 0x80;
        break;
    }
    case OtherGamesAction::previous_round:
    case OtherGamesAction::next_round: {
        const int direction = pending->action == OtherGamesAction::previous_round ? -1 : 1;
        const int expected_round = (old_projection.round + direction + old_projection.rounds) % old_projection.rounds;
        if (next_projection.round != expected_round || page.source_participant_offset != 0 ||
            next_projection.competition != old_projection.competition ||
            next_projection.linked_page != old_projection.linked_page)
            throw FormatError("other games: round-arrow acknowledgement differs from wrap/reset Source behavior");
        if (next_projection.source_participant_count <= 16) expected_view |= 0x80;
        break;
    }
    case OtherGamesAction::toggle_display_mode:
        if (!old_projection.display_mode_toggle_available ||
            next_projection.display_mode == old_projection.display_mode ||
            page.source_participant_offset != before.source_participant_offset ||
            next_projection.competition != old_projection.competition ||
            next_projection.linked_page != old_projection.linked_page ||
            next_projection.round != old_projection.round)
            throw FormatError("other games: unavailable or mismatched display-mode toggle");
        expected_view ^= 0x80;
        if (next_projection.source_participant_count <= 16) expected_view |= 0x80;
        break;
    case OtherGamesAction::team_info: {
        const bool visible = std::any_of(old_projection.rows.begin(),old_projection.rows.end(),
            [&](const auto& row) { return row.home_team==pending->value||row.away_team==pending->value; });
        const bool source_forced_detail=next_projection.source_participant_count<=16&&
            next_projection.display_mode==OtherGamesDisplayMode::detail;
        if (pending->value < 0 || !visible || old_projection.unfinished_predecessor ||
            !old_projection.fixtures_available ||
            next_projection.competition != old_projection.competition ||
            next_projection.entry_index != old_projection.entry_index ||
            next_projection.descriptor_competition != old_projection.descriptor_competition ||
            next_projection.linked_page != old_projection.linked_page ||
            next_projection.round != old_projection.round ||
            (next_projection.display_mode != old_projection.display_mode&&!source_forced_detail) ||
            page.source_participant_offset != before.source_participant_offset)
            throw FormatError("other games: TeamInfo return changed the owned Source page");
        if(next_projection.source_participant_count<=16)expected_view|=0x80;
        break;
    }
    case OtherGamesAction::close:
        throw FormatError("other games: close cannot be acknowledged as a view");
    }
    expected_id.expected_view_byte = expected_view;
    if (page.identity != expected_id ||
        ((page.identity.expected_view_byte & 0x80) !=
         (next_projection.display_mode == OtherGamesDisplayMode::detail ? 0x80 : 0)))
        throw FormatError("other games: mismatched full view identity acknowledgement");
    validate_other_games_preparation(page.identity, desktop_->state(), preparation);
    auto screen = std::make_unique<OtherGamesScreen>(data_, std::move(page));
    auto desk = std::make_unique<DesktopScreen>(data_, preparation.desktop);
    auto next = std::make_unique<PreparationState>(std::move(preparation));
    desktop_ = std::move(desk);
    preparation_ = std::move(next);
    other_games_ = std::move(screen);
}

void StartupFlow::close_other_games(OtherGamesState page, PreparationState preparation) {
    const auto* pending = pending_other_games_action();
    if (!pending || !other_games_ || !desktop_ || pending->action != OtherGamesAction::close)
        throw FormatError("other games: no close acknowledgement owner");
    auto expected_id = pending->identity;
    ++expected_id.revision;
    ++expected_id.sequence;
    if (page.identity != expected_id || page.identity.expected_view_byte != other_games_->state().identity.expected_view_byte)
        throw FormatError("other games: close acknowledgement changed Source view state");
    validate_other_games_preparation(page.identity, desktop_->state(), preparation);
    auto desk = std::make_unique<DesktopScreen>(data_, preparation.desktop);
    auto next = std::make_unique<PreparationState>(std::move(preparation));
    desktop_ = std::move(desk);
    preparation_ = std::move(next);
    other_games_.reset();
}

void StartupFlow::reject_other_games(std::string reason) {
    if (!other_games_) throw FormatError("other games: missing rejection owner");
    other_games_->reject_action(std::move(reason));
}



FlowResult StartupFlow::finish_league_pairings_input(bool changed) {
    if(const auto* family=pending_pairings_family_action()) {
        FlowResult result{changed,FlowRequest::league_family_action};
        result.league_family_action=*family;return result;
    }
    if(const auto* pending=pending_league_pairings()) {
        FlowResult result{changed,FlowRequest::league_pairings_action};result.league_pairings_action=*pending;return result;
    }
    return {changed};
}
FlowResult StartupFlow::expire_friendly_question(bool release_consumed) {
    const auto* question=friendly_question_screen();
    if(!question||(!release_consumed&&question->question_held()))return {};
    const bool wait_release=!release_consumed&&question->question_release_pending();
    const auto before=state_json();
    auto request=friendly_match_->expire_question();
    friendly_question_release_barrier_=wait_release;
    if(friendly_match_->close_requested()) {
        friendly_match_.reset();pending_friendly_action_.reset();
        return {true,FlowRequest::submenu_closed};
    }
    if(request) {
        pending_friendly_action_=*request;
        next_friendly_request_id_=std::max(next_friendly_request_id_,request->request_id+1);
        FlowResult result{true,FlowRequest::friendly_action};result.friendly_action=*request;return result;
    }
    return {before!=state_json()};
}
bool StartupFlow::timed_notice_open() const {
    return (training_camp_&&training_camp_->timed_notice_open()) ||
        (bank_&&bank_->timed_notice_open()) || (advertising_&&advertising_->timed_notice_open());
}
FlowResult StartupFlow::expire_timed_notice() {
    const bool changed=training_camp_?training_camp_->expire_timed_notice():
        bank_?bank_->expire_timed_notice():advertising_?advertising_->expire_timed_notice():false;
    return {changed};
}

FlowResult StartupFlow::process_timer_expired(int pointer_x, int pointer_y) {
    // This is Source event8, not keyboard ESC or a guessed screen coordinate.
    // Timed keyboard/calculator owners must finish before their outer poll.
    if (family_table_dispatch_pending_) return {};
    if (family_table_range_dialog_)
        //BalanceFromGame event8 cancels either range stage. Its existing adapter keeps
        //key waits and the restored-frame Present before the typed reply.
        return cancel_family_table_from_source_event();
    if (league_pairings_ || in_family_table() || strengths_ || other_games_ || top_scorers_)
        return pointer_down(0, 0, true); // these routines dispatch event8 as their close action
    if (engine_attached_ && outgoing_transfer_) {
        const auto before = outgoing_transfer_->state_json();
        const auto request = outgoing_transfer_->process_timer_expired();
        PreparationRequest action;
        if (request.kind != OutgoingTransferRequestKind::none) action = request;
        return finish_preparation_input(std::move(action), before != outgoing_transfer_->state_json());
    }
    if (engine_attached_ && transfer_market_)
        //Reset event8 pages while the pointer is inside the market viewport;
        // outside it closes. Preserve the actual pointer, not a guessed click.
        return click(pointer_x, pointer_y, true);
    if (engine_attached_ && (training_ || tactics_))
        //NeedsInitialization and 18361 leave their input loops on Source event8. The normal
        //right-event path preserves pending edits and adds no mouse-UP owner.
        return click(0, 0, true);
    if (engine_attached_ && bank_) {
        //BalanceFromGame cancels the calculator;MaxSharesToBuy event8 restores its subview before
        //the outer room exits. A pending booking must keep its normal ACK.
        if (bank_->pending()) return {};
        return click(0, 0, true);
    }
    if (engine_attached_ && toto_) {
        //LeagueTableRangeDialog event8 ends an available coupon through its normal close path:
        //publish changed tips first, retain the submit ACK, then return.
        //BalanceFromGame owns the calculator's key waits and restored-frame Present.
        if (!toto_->state().available || toto_->pending()) return {};
        if (toto_->calculator_open()) return {toto_->process_timer_expired()};
        return click(0, 0, true);
    }
    if (engine_attached_ && training_camp_)
        //BookTrainingCamp event8 exits without selecting the booking event2 branch.
        //Its existing right path preserves pending focus/booking requests.
        return click(0, 0, true);
    if (engine_attached_ && friendly_match_) {
        if(friendly_question_screen())return expire_friendly_question();
        //50775(2,0) returns-1 on event8: no selected opponent or quote.
        if (!pending_friendly_action_ && friendly_opponent_picker())
            return click(0,0,true);
        //CalendarCell returns0 on event8 before26215 can add a friendly date.
        //Other Friendly stages own their own Source questions/notices.
        if (pending_friendly_action_ || !friendly_calendar_quote()) return {};
        return click(0, 0, true);
    }
    if (engine_attached_ && calendar_) {
        //CalendarCell returns0 on event8, including the reschedule caller. Keep an
        //owned command or physical release before cancelling its selection.
        if (calendar_->pending_reschedule() || reschedule_release_barrier_) return {};
        return click(0, 0, true);
    }
    if (engine_attached_ && advertising_)
        //CheckAdvertisingRenewal event8 leaves offers;BalanceFromGame cancels unconfirmed spending first.
        //The existing modal back path protects pending contracts/bookings.
        return click(0, 0, true);
    if (engine_attached_ && balance_) {
        // No physical DOWN/UP owner: preserve calculator waits and the staged
        // close warning, then let the existing app ACK publish the draft once.
        if (balance_->pending()) return {};
        return finish_balance_input(balance_->process_timer_expired());
    }
    if (engine_attached_ && environment_) {
        //Image/BypassesTwoDiscontinuedYouthGroups leaves the patched Umfeld loop on Source event8.
        //Use its existing return path; retain pending staff changes and presses.
        if (environment_->pending()) return {};
        return send_key("ESC");
    }
    if (engine_attached_ && contracts_ && !player_card_) {
        if (!contracts_->process_timer_expired(pointer_x, pointer_y)) return {};
        if (contracts_->close_requested()) {
            contracts_.reset();
            return {true, FlowRequest::contracts_closed};
        }
        FlowResult result{true};
        if (contracts_->pending()) {
            result.request = FlowRequest::contract_action;
            result.contract_action = *contracts_->pending();
        }
        return result;
    }
    if (engine_attached_ && stadium_) {
        if (stadium_->pending()) return {};
        const auto before = stadium_->state_json();
        const auto request = stadium_->process_timer_expired();
        PreparationRequest action;
        if (request.kind != StadiumRequestKind::none) action = request;
        // A local calculator cancellation restores its editor without a Core
        //command. Publish that changed frame before the next outer poll.
        return finish_preparation_input(std::move(action), before != stadium_->state_json());
    }
    if (engine_attached_ && world_cup_statistics_ && national_friendly_picker_) {
        //1e06b leaves on event8 without choosing a nation (event2 only).
        //No physical DOWN was synthesized, so create no mouse-UP barrier.
        if (national_friendly_release_barrier_ || pending_statistics_menu()) return {};
        close_national_friendly_picker();
        return {true, FlowRequest::submenu_closed};
    }
    if (engine_attached_ && statistics_menu_ && !national_friendly_picker_) {
        //20133 and its children use event8 for their existing return/close.
        //Keep each pending selection/mutation/close with its original ACK.
        if (pending_statistics_menu() || statistics_page_action_ || pending_analytics() ||
            pending_statistics_rankings() || pending_opportunities()) return {};
        return pointer_down(0, 0, true);
    }
    if (engine_attached_ && (lineup_ || pitch_))
        //PitchScreen event8 cancels a selected player before its next poll closes.
        // The existing right-event path ignores coordinates; use click rather
        // than pointer_down, since timer expiry owns no physical UP barrier.
        // A local card/statistics child returns first, before its parent poll.
        return click(0, 0, true);
    if (desktop_ && engine_attached_ && screen_name() == "desktop")
        return route_desk(desktop_->process_timer_expired());
    return {}; // other modal callers retain their own input owner
}

FlowResult StartupFlow::pointer_down(int x,int y,bool right) {
    if(pitch_hold_release_barrier_&&!right)return {};
    if(friendly_match_)return {friendly_match_->pointer_down(x,y,right)};
    if (in_toto_calculator()) return {toto_->pointer_down(x, y, right)};
    if (indoor_standings_release_barrier_) return {};
    if (indoor_standings_) {
        // SDLFlushEvents2 accepts either mouse DOWN; consume its matching UP on Desk.
        indoor_standings_.reset();
        indoor_standings_release_barrier_ = true;
        return {true, FlowRequest::submenu_closed};
    }
    if (calendar_ && calendar_->state().reschedule_manager >= 0) {
        reschedule_release_barrier_ = true;
        const bool changed = calendar_->click(x,y,right);
        if (calendar_->pending_reschedule()) {
            FlowResult result{true, FlowRequest::reschedule_action};
            result.reschedule_action = *calendar_->pending_reschedule();
            return result;
        }
        return finish_readonly_input(changed);
    }
    if(achievements_){
        const int selected=achievements_->state().selected_season;
        const auto request=achievements_->pointer_down(x,y,right);
        achievements_release_barrier_=true;
        if(request.kind==AchievementsRequestKind::close){achievements_.reset();return {true,FlowRequest::submenu_closed};}
        return {selected!=achievements_->state().selected_season};
    }
    if(desk_records_) {
        const bool changed=desk_records_->pointer_down(x,y,right);
        if(desk_records_->closed()) { desk_records_.reset();desk_records_release_barrier_=true;return {true,FlowRequest::submenu_closed}; }
        return {changed};
    }
    if (settings_readme_) {
        if (!settings_readme_->pointer_down(true)) return {};
        if (settings_readme_->close_requested()) {
            settings_readme_.reset();
            settings_readme_release_barrier_ = true;
        }
        return {true};
    }
    if (settings_ && !save_load_) {
        const bool changed = settings_->pointer_down(x,y,right);
        if (changed && settings_->release_consumed()) settings_release_barrier_ = true;
        return changed ? finish_settings_input(true) : FlowResult{};
    }
    if(world_cup_statistics_&&national_friendly_picker_) {
        const auto request=world_cup_statistics_->pointer_down(x,y,right);
        if(request.kind==WorldCupStatisticsRequestKind::close) {
            national_friendly_release_barrier_=true;
            close_national_friendly_picker();
            return {true,FlowRequest::submenu_closed};
        }
        if(request.kind==WorldCupStatisticsRequestKind::select_opponent) {
            national_friendly_release_barrier_=true;
            FlowResult result{true,FlowRequest::national_friendly_selected};
            result.national_friendly_opponent=request.runtime_team;
            return result;
        }
        return statistics_menu_ ? finish_statistics_menu_input(statistics_menu_->pointer_down(x,y,right)) : FlowResult{};
    }
    if(family_table_range_dialog_) {
        if(family_table_dispatch_pending_)return {};
        if(!family_table_range_dialog_->source_input_ready())return {};
        const bool changed=family_table_range_dialog_->pointer_down(x,y,right);
        if(changed)family_table_reset_clock_after_present_=true;
        return {changed};
    }
    if(other_games_)return finish_other_games_input(other_games_->pointer_down(x,y,right));
    if(alltime_table_) {
        if(alltime_table_->pointer_down(x,y,right)) {
            if(right) {
                statistics_page_action_=StatisticsPageAction{statistics_menu_->state().owner,3,StatisticsPageActionKind::close,-1,false};
                return finish_statistics_page_input(true);
            }
            return {true};
        }
        return statistics_menu_ ? finish_statistics_menu_input(statistics_menu_->pointer_down(x,y,right)) : FlowResult{};
    }
    if(world_cup_statistics_) {
        const auto request=world_cup_statistics_->pointer_down(x,y,right);
        if(request.kind==WorldCupStatisticsRequestKind::close) {
            statistics_page_action_=StatisticsPageAction{statistics_menu_->state().owner,4,StatisticsPageActionKind::close,-1,false};
            return finish_statistics_page_input(true);
        }
        return statistics_menu_ ? finish_statistics_menu_input(statistics_menu_->pointer_down(x,y,right)) : FlowResult{};
    }
    if(finance_totals_||series_records_) {
        if(right) {
            statistics_page_action_=StatisticsPageAction{statistics_menu_->state().owner,finance_totals_?5:6,StatisticsPageActionKind::close,-1,false};
            return finish_statistics_page_input(true);
        }
        return statistics_menu_ ? finish_statistics_menu_input(statistics_menu_->pointer_down(x,y,right)) : FlowResult{};
    }
    if(balance_) {
        const bool changed=balance_->pointer_down(x,y,right);
        if(const auto* pending=pending_balance();pending&&pending->kind==BalanceAction::confirm_close)
            balance_release_barrier_=true;
        return finish_balance_input(changed);
    }
    if(top_scorers_)return finish_top_scorers_input(top_scorers_->pointer_down(x,y,right));
    if(analytics_) {
        if(analytics_->pointer_down(x,y,right))return finish_analytics_input(true);
        if(statistics_menu_)return finish_statistics_menu_input(statistics_menu_->pointer_down(x,y,right));
        return {};
    }
    if(statistics_rankings_) {
        if(statistics_rankings_->pointer_down(x,y,right)) {
            const auto& pending=statistics_rankings_->pending();
            if(pending&&pending->kind==StatisticsRankingsRequestKind::close) {
                statistics_rankings_.reset();
                if(statistics_menu_)close_statistics_menu();
                return {true};
            }
            return finish_statistics_rankings_input(true);
        }
        if(statistics_menu_)return finish_statistics_menu_input(statistics_menu_->pointer_down(x,y,right));
        return {};
    }
    if(minute_statistics_) {
        if(right) {
            minute_statistics_->handle_source_event(8);
            minute_statistics_.reset();
            if(statistics_menu_)close_statistics_menu();
            return {true};
        }
        if(statistics_menu_)return finish_statistics_menu_input(statistics_menu_->pointer_down(x,y,false));
        return {};
    }
    if(transfer_history_) {
        if(right) {
            transfer_history_->handle_source_event(8);
            transfer_history_.reset();
            if(statistics_menu_)close_statistics_menu();
            return {true};
        }
        if(statistics_menu_)return finish_statistics_menu_input(statistics_menu_->pointer_down(x,y,false));
        return {};
    }
    if(opportunities_) {
        if(opportunities_->pointer_down(x,y,right))return finish_opportunities_input(true);
        if(statistics_menu_)return finish_statistics_menu_input(statistics_menu_->pointer_down(x,y,right));
        return {};
    }
    if(statistics_menu_)return finish_statistics_menu_input(statistics_menu_->pointer_down(x,y,right));
    if(strengths_)return finish_strengths_input(strengths_->pointer_down(x,y,right));
    if(in_family_table()) {
        bool changed=league_table_->pointer_down(x,y,right);
        if(const auto* family=pending_league_family_action()) {
            // The synchronous TeamInfo modal consumes the opening row's UP.
            // Keeping a parent barrier afterwards would swallow the next footer UP.
            family_table_release_barrier_=!right&&family->kind!=LeagueFamilyRequestKind::team_info;
            // InLeaguePairings stays in the inner loop for a same-league map click. Still
            // deliver its operation request, but preserve the last physical frame.
            if(family->kind==LeagueFamilyRequestKind::select_league&&
               family->league_id==league_table_->selected_league())changed=false;
            FlowResult result{changed,FlowRequest::league_family_action};result.league_family_action=*family;return result;
        }
        return {changed};
    }
    if (stadium_) {
        const bool changed=stadium_->pointer_down(x,y,right);
        if (stadium_->pending()) return finish_preparation_input(*stadium_->pending(),changed);
        return {changed};
    }
    if (environment_) return {environment_->pointer_down(x,y,right)};
    if(pitch_){
        const std::string before=pitch_->state_json();
        const auto request=pitch_->pointer_down(x,y,right);
        pitch_release_barrier_=pitch_->take_source_down_release_barrier();
        PreparationRequest action;if(request.kind!=PitchRequestKind::none)action=request;
        return finish_preparation_input(std::move(action),before!=pitch_->state_json());
    }
    if (league_pairings_) return finish_league_pairings_input(league_pairings_->pointer_down(x,y,right));
    if (desktop_ && screen_name() == "desktop") {
        const auto before = desktop_->state_json();
        auto result = route_desk(desktop_->pointer_down(x,y,right));
        result.changed |= before != desktop_->state_json();
        return result;
    }
    return {};
}
void StartupFlow::show_balance(BalanceState page,const DeskRequest& owner) {
    if(!desktop_||!engine_attached_||in_preparation_menu()||in_readonly_view()||balance_||matchday_results_||
       !desktop_->pending()||*desktop_->pending()!=owner||owner.kind!=DeskRequestKind::open_menu||owner.menu_kind!=28||
       owner.manager_slot!=desktop_->state().manager_slot||owner.revision!=desktop_->state().revision||
       page.identity.manager_slot!=owner.manager_slot||page.identity.runtime_club!=desktop_->state().runtime_club||
       page.identity.day_counter!=desktop_->state().day_counter||page.identity.revision!=owner.revision||
       page.identity.sequence!=0||!page.identity.scope||page.identity.club_identity!=int(desktop_->state().managers.at(size_t(owner.manager_slot)).club_identity_key))
        throw FormatError("balance: mismatched opening acknowledgement");
    auto screen=std::make_unique<BalanceScreen>(data_,std::move(page));
    desktop_->resolve_local_menu();balance_=std::move(screen);
}
void StartupFlow::acknowledge_balance(BalanceState page) {
    const auto* pending=pending_balance();
    if(!pending||pending->kind==BalanceAction::confirm_close||!balance_||!desktop_||
       page.identity.manager_slot!=pending->identity.manager_slot||page.identity.runtime_club!=pending->identity.runtime_club||
       page.identity.club_identity!=pending->identity.club_identity||page.identity.day_counter!=pending->identity.day_counter||
       page.identity.revision!=pending->identity.revision||page.identity.scope!=pending->identity.scope||
       page.identity.revision!=desktop_->state().revision||page.identity.manager_slot!=desktop_->state().manager_slot||
       page.identity.runtime_club!=desktop_->state().runtime_club||page.identity.day_counter!=desktop_->state().day_counter||
       page.identity.club_identity!=int(desktop_->state().managers.at(size_t(page.identity.manager_slot)).club_identity_key)||
       page.identity.sequence!=pending->identity.sequence+1||
       (pending->kind==BalanceAction::close)!=page.pending_close)
        throw FormatError("balance: mismatched preview acknowledgement");
    // Calculator dragging is local screen presentation state. Preserve it across
    // an acknowledged Core projection without adding it to the Core request.
    page.calculator_x=balance_->state().calculator_x;
    page.calculator_y=balance_->state().calculator_y;
    balance_=std::make_unique<BalanceScreen>(data_,std::move(page));
}
void StartupFlow::close_balance(BalanceState page,PreparationState preparation) {
    const auto* pending=pending_balance();if(!pending||!balance_||!desktop_||
       (pending->kind!=BalanceAction::close&&pending->kind!=BalanceAction::confirm_close))
        throw FormatError("balance: missing close acknowledgement owner");
    auto expected=pending->identity;++expected.revision;++expected.sequence;
    if(page.identity!=expected||pending->identity.manager_slot!=desktop_->state().manager_slot||
       pending->identity.revision!=desktop_->state().revision||pending->identity.runtime_club!=desktop_->state().runtime_club||
       pending->identity.day_counter!=desktop_->state().day_counter||
       pending->identity.club_identity!=int(desktop_->state().managers.at(size_t(pending->identity.manager_slot)).club_identity_key)||
       preparation.desktop.revision!=expected.revision||
       preparation.desktop.manager_slot!=expected.manager_slot||
       preparation.desktop.manager_teams!=desktop_->state().manager_teams||
       preparation.desktop.runtime_club!=desktop_->state().runtime_club||
       preparation.desktop.day_counter!=desktop_->state().day_counter||
       preparation.desktop.season_index!=desktop_->state().season_index||
       preparation.desktop.season_day!=desktop_->state().season_day||
       preparation.desktop.manager_name!=desktop_->state().manager_name||
       preparation.desktop.portrait!=desktop_->state().portrait||preparation.desktop.badge!=desktop_->state().badge||
       preparation.desktop.options!=desktop_->state().options)
        throw FormatError("balance: mismatched close acknowledgement");
    validate_preparation_context(preparation);
    auto desk=std::make_unique<DesktopScreen>(data_,preparation.desktop);
    auto prep=std::make_unique<PreparationState>(std::move(preparation));
    desktop_=std::move(desk);preparation_=std::move(prep);balance_.reset();
}
void StartupFlow::reject_balance(std::string reason) {
    if(!balance_||reason.empty())throw FormatError("balance: missing rejection owner");
    balance_->reject_action(std::move(reason));
}
FlowResult StartupFlow::tick_balance_source_milliseconds(uint64_t now) {
    if(!balance_||!balance_->tick_source_milliseconds(now))return {};
    return finish_balance_input(true);
}
void StartupFlow::mark_balance_presented_source_milliseconds(uint64_t now) {
    if(balance_)balance_->mark_presented_source_milliseconds(now);
}
FlowResult StartupFlow::tick_stadium_calculator_source_ticks(unsigned ticks) {
    if (!stadium_ || !stadium_->calculator_advance_source_ticks(ticks)) return {};
    PreparationRequest action;
    if (stadium_->pending()) action = *stadium_->pending();
    return finish_preparation_input(std::move(action), true);
}
void StartupFlow::mark_stadium_calculator_presented() {
    if (stadium_) stadium_->calculator_mark_presented();
}
FlowResult StartupFlow::expire_stadium_tip() {
    if (!stadium_) return {};
    const auto request = stadium_->expire_construction_tip();
    if (request.kind == StadiumRequestKind::none) return {};
    return finish_preparation_input(request, true);
}
FlowResult StartupFlow::tick_league_pairings_source_seconds(uint64_t now) {
    if (!league_pairings_ || !league_pairings_->tick_source_seconds(now)) return {};
    return finish_league_pairings_input(true);
}
void StartupFlow::mark_league_pairings_presented_source_seconds(uint64_t now) {
    if (league_pairings_) league_pairings_->mark_presented_source_seconds(now);
}
void StartupFlow::show_league_pairings(LeaguePairingsState page) {
    if(!desktop_||!engine_attached_||in_preparation_menu()||in_readonly_view()||matchday_results_||
        !desktop_->pending()||desktop_->pending()->kind!=DeskRequestKind::open_menu||desktop_->pending()->menu_kind!=1||
        page.identity.manager!=desktop_->state().manager_slot||page.identity.runtime_club!=desktop_->state().runtime_club||
        page.identity.revision!=desktop_->state().revision||page.identity.sequence!=0)
        throw FormatError("pairings: mismatched opening acknowledgement");
    auto next=std::make_unique<LeaguePairingsScreen>(data_,std::move(page));
    desktop_->resolve_local_menu();league_pairings_=std::move(next);
}
void StartupFlow::show_family_league_pairings(LeaguePairingsState page,StrengthsIdentity identity) {
    if(!desktop_||!engine_attached_||matchday_results_||
       (league_family_identity_&&(identity.manager!=league_family_identity_->manager||
        identity.runtime_club!=league_family_identity_->runtime_club||identity.club_identity!=league_family_identity_->club_identity||
        identity.day_counter!=league_family_identity_->day_counter||identity.source_param2!=league_family_identity_->source_param2||
        identity.family_id!=league_family_identity_->family_id))||
       identity.manager!=page.identity.manager||identity.runtime_club!=page.identity.runtime_club||
       identity.club_identity!=page.identity.club_identity||identity.day_counter!=desktop_->state().day_counter||
       identity.family_id==0||identity.revision!=page.identity.revision||identity.league!=page.identity.league)
        throw FormatError("league family: mismatched Pairings transition");
    if(!league_family_identity_&&(!desktop_->pending()||
       desktop_->pending()->kind!=DeskRequestKind::open_menu||desktop_->pending()->menu_kind!=1||
       identity.manager!=desktop_->state().manager_slot||identity.runtime_club!=desktop_->state().runtime_club||
       identity.revision!=desktop_->state().revision||identity.family_sequence||identity.scope||identity.sequence))
        throw FormatError("league family: invalid Pairings desktop entry");
    auto screen=std::make_unique<LeaguePairingsScreen>(data_,std::move(page),
        LeaguePairingsScreen::FamilyMode{identity});
    if(league_family_child_)replace_family_child();
    league_pairings_=std::move(screen);league_family_identity_=identity;league_family_child_=2;
    if(desktop_->pending())desktop_->resolve_local_menu();
}
void StartupFlow::acknowledge_league_pairings(LeaguePairingsState page,
                                             std::optional<StrengthsIdentity> family) {
    const auto* pending=pending_league_pairings();
    if(!pending||!desktop_||page.identity.revision!=pending->identity.revision||
        page.identity.manager!=pending->identity.manager||page.identity.runtime_club!=pending->identity.runtime_club||
        page.identity.club_identity!=pending->identity.club_identity||page.identity.scope!=pending->identity.scope||
        page.identity.sequence!=pending->identity.sequence+1)
        throw FormatError("pairings: mismatched page acknowledgement");
    if(league_family_identity_) {
        const auto& prior=*league_family_identity_;
        if(!family||league_family_child_!=2||family->manager!=prior.manager||
           family->runtime_club!=prior.runtime_club||family->club_identity!=prior.club_identity||
           family->day_counter!=prior.day_counter||family->family_id!=prior.family_id||
           family->family_sequence!=prior.family_sequence||family->source_param2!=prior.source_param2||
           family->manager_default_league!=prior.manager_default_league||
           family->revision!=page.identity.revision||family->league!=page.identity.league||
           family->scope!=page.identity.scope||family->sequence!=page.identity.sequence||
           family->league_group!=uint8_t(page.source.owner.league_group))
            throw FormatError("league family: mismatched Pairings refresh acknowledgement");
        auto next=std::make_unique<LeaguePairingsScreen>(data_,std::move(page),
            LeaguePairingsScreen::FamilyMode{*family});
        league_pairings_=std::move(next);league_family_identity_=*family;
    } else {
        if(family)throw FormatError("league family: unexpected Pairings refresh identity");
        auto next=std::make_unique<LeaguePairingsScreen>(data_,std::move(page));league_pairings_=std::move(next);
    }
}
void StartupFlow::close_league_pairings(PreparationState state) {
    const auto* pending=pending_league_pairings();
    if(!pending||pending->event.kind!=LeaguePairingsAction::close||!desktop_||!engine_attached_||
        state.desktop.revision!=pending->identity.revision+1||state.desktop.source!="engine_projection"||
        state.desktop.manager_slot!=pending->identity.manager||state.desktop.runtime_club!=pending->identity.runtime_club||
        state.desktop.manager_teams!=desktop_->state().manager_teams||
        state.desktop.day_counter!=desktop_->state().day_counter||state.desktop.season_day!=desktop_->state().season_day||
        state.desktop.season_index!=desktop_->state().season_index)
        throw FormatError("pairings: mismatched final family acknowledgement");
    validate_preparation_context(state);
    auto next_desk=std::make_unique<DesktopScreen>(data_,state.desktop);
    auto next_preparation=std::make_unique<PreparationState>(std::move(state));
    desktop_=std::move(next_desk);preparation_=std::move(next_preparation);league_pairings_.reset();
}
void StartupFlow::reject_league_pairings(std::string reason) {
    if(!league_pairings_)throw FormatError("pairings: missing rejection owner");
    league_pairings_->reject_action(std::move(reason));
}

void StartupFlow::acknowledge_transfer_market_preview(PreparationState state, TransferMarketPreview quote) {
    const auto* request = pending_transfer_market();
    if (!request || request->kind != TransferMarketRequestKind::preview_quote ||
        !desktop_ || !engine_attached_ || !state.transfer_market ||
        request->revision != desktop_->state().revision ||
        state.desktop.revision < request->revision || state.desktop.revision - request->revision > 1 ||
        quote.revision != state.desktop.revision)
        throw FormatError("transfer market: invalid preview acknowledgement context");
    auto previous = desktop_->state();
    previous.revision = state.desktop.revision;
    if (desk_json(previous) != desk_json(state.desktop))
        throw FormatError("transfer market: hover changed the desk outside its revision");
    validate_preparation_context(state);
    auto next_market = std::make_unique<TransferMarketScreen>(*transfer_market_);
    next_market->acknowledge_preview(*state.transfer_market, quote);
    auto next_desktop = std::make_unique<DesktopScreen>(data_, state.desktop);
    auto next_preparation = std::make_unique<PreparationState>(std::move(state));
    transfer_market_ = std::move(next_market);
    desktop_ = std::move(next_desktop);
    preparation_ = std::move(next_preparation);
}

void StartupFlow::show_transfer_offer(PreparationState preparation, OutgoingTransferEditorState editor) {
    const auto* request = pending_transfer_market();
    const bool poach=request && request->kind==TransferMarketRequestKind::open_poach;
    if (!request || (!poach && request->kind != TransferMarketRequestKind::open_offer) || outgoing_transfer_ ||
        !desktop_ || !engine_attached_ || !preparation.transfer_market || editor.closed ||
        editor.manager_slot != request->manager_slot || editor.runtime_club != request->runtime_club ||
        (!poach && (editor.list_entry != request->bid.list_entry || editor.source_slot != request->bid.source_slot ||
        editor.player != request->bid.player)) ||
        (poach && (editor.list_entry<40 || editor.list_entry>43 || editor.source_slot!=2928+editor.list_entry ||
                   editor.player<0 || editor.initial_action_loan)) || editor.revision != preparation.desktop.revision ||
        preparation.desktop.revision != request->revision + 1)
        throw FormatError("transfer offer: invalid opening acknowledgement");
    validate_preparation_context(preparation);
    auto next_parent = std::make_unique<TransferMarketScreen>(*transfer_market_);
    next_parent->acknowledge(*preparation.transfer_market);
    auto next_editor = std::make_unique<OutgoingTransferScreen>(data_, std::move(editor));
    auto next_desktop = std::make_unique<DesktopScreen>(data_, preparation.desktop);
    auto next_preparation = std::make_unique<PreparationState>(std::move(preparation));
    transfer_market_ = std::move(next_parent); outgoing_transfer_ = std::move(next_editor);
    desktop_ = std::move(next_desktop); preparation_ = std::move(next_preparation);
}

void StartupFlow::acknowledge_transfer_offer(PreparationState preparation, OutgoingTransferEditorState editor) {
    const auto* request = pending_transfer_offer();
    if (!request || !desktop_ || !engine_attached_ || !preparation.transfer_market ||
        request->identity.revision != desktop_->state().revision ||
        editor.revision != preparation.desktop.revision || editor.revision < request->identity.revision ||
        editor.revision - request->identity.revision > 1 || editor.manager_slot != request->identity.manager_slot ||
        editor.runtime_club != request->identity.runtime_club || editor.list_entry != request->identity.list_entry ||
        editor.source_slot != request->identity.source_slot || editor.player != request->identity.player ||
        editor.seller != request->identity.seller ||
        (request->kind != OutgoingTransferRequestKind::team_info &&
         request->kind != OutgoingTransferRequestKind::player_info && editor.bid_slot != request->identity.bid_slot))
        throw FormatError("transfer offer: stale editor acknowledgement");
    validate_preparation_context(preparation);
    auto next_desktop = std::make_unique<DesktopScreen>(data_, preparation.desktop);
    auto next_preparation = std::make_unique<PreparationState>(std::move(preparation));
    auto next_editor = std::make_unique<OutgoingTransferScreen>(*outgoing_transfer_);
    std::unique_ptr<TransferMarketScreen> next_parent;
    if (editor.closed) {
        next_parent = std::make_unique<TransferMarketScreen>(*transfer_market_);
        next_parent->refresh_after_offer(*next_preparation->transfer_market);
        next_editor.reset();
    } else next_editor->acknowledge(std::move(editor));
    if (next_parent) transfer_market_ = std::move(next_parent);
    outgoing_transfer_ = std::move(next_editor);
    desktop_ = std::move(next_desktop); preparation_ = std::move(next_preparation);
}

void StartupFlow::reject_transfer_offer(std::string reason) {
    if (!pending_transfer_offer() || reason.empty())
        throw FormatError("transfer offer: rejection requires a pending editor action");
    outgoing_transfer_->reject_action(std::move(reason));
}

Image StartupFlow::render() const {
    if (indoor_standings_) return indoor_standings_->render();
    if(achievements_)return achievements_->render();
    if (settings_readme_) return settings_readme_->render();
    if(desk_records_)return desk_records_->render();
    if(other_games_)return other_games_->render();
    if(alltime_table_)return alltime_table_->render();
    if(world_cup_statistics_)return world_cup_statistics_->render();
    if(finance_totals_)return finance_totals_->render();
    if(series_records_)return series_records_->render();
    if(balance_)return balance_->render();
    if(top_scorers_)return top_scorers_->render();
    if(analytics_)return analytics_->render();
    if(statistics_rankings_)return statistics_rankings_->render();
    if(minute_statistics_)return minute_statistics_->render(statistics_menu_->render_page(8));
    if(transfer_history_)return transfer_history_->render(statistics_menu_->render_page(1),transfer_history_origin_y_);
    if(opportunities_)return opportunities_->render();
    if(statistics_menu_)return statistics_menu_->render();
    if(strengths_)return strengths_->render();
    if(family_table_range_dialog_)return family_table_range_dialog_->render();
    if (calendar_) return calendar_->render();
    if(league_pairings_)return league_pairings_->render();
    if (joining_) return manager_ ? manager_->render() : join_setup_->render();
    if (outgoing_transfer_) return outgoing_transfer_->render();
    return player_card_ ? player_card_->render() : contracts_ ? contracts_->render() : friendly_match_ ? friendly_match_->render() : league_table_ ? league_table_->render() : mailbox_ ? mailbox_->render() : save_load_ ? save_load_->render() : settings_ ? settings_->render() : advertising_ ? advertising_->render() : environment_ ? environment_->render() : matchday_results_ ? matchday_results_->render() : player_card_ ? player_card_->render() : pitch_ ? pitch_->render() : lineup_ ? lineup_->render() : training_ ? training_->render() : training_camp_ ? training_camp_->render() : bank_ ? bank_->render() : stadium_ ? stadium_->render() : toto_ ? toto_->render() : transfer_market_ ? transfer_market_->render() : tactics_ ? tactics_->render() :
           desktop_ ? desktop_->render() : manager_ ? manager_->render() : setup_.render();
}

void StartupFlow::show_environment(EnvironmentView state, const DeskRequest& owner) {
    if (!desktop_ || !engine_attached_ || !preparation_ || in_preparation_menu() || in_readonly_view() ||
        matchday_results_ || !desktop_->pending() || *desktop_->pending() != owner ||
        owner.kind != DeskRequestKind::open_menu || owner.menu_kind != 24 ||
        owner.manager_slot != desktop_->state().manager_slot || owner.revision != desktop_->state().revision ||
        state.manager_slot != owner.manager_slot || state.runtime_club != desktop_->state().runtime_club ||
        state.revision != owner.revision || state.runtime_club < 0 || desktop_->state().national_team)
        throw FormatError("environment: expected ordinary club desk menu 24 and matching projection");
    state.owner = owner;
    desktop_->resolve_local_menu();
    auto next = std::make_unique<EnvironmentScreen>(data_, std::move(state), desktop_->render());
    environment_owner_ = owner;
    environment_ = std::move(next);
}

void StartupFlow::show_achievements(AchievementsState state,const DeskRequest& owner) {
    if(!desktop_||!engine_attached_||!desktop_->pending()||*desktop_->pending()!=owner||
       owner.kind!=DeskRequestKind::open_menu||owner.menu_kind!=4||owner.manager_slot!=desktop_->state().manager_slot||
       owner.revision!=desktop_->state().revision||state.owner!=owner||state.manager_slot!=owner.manager_slot||
       in_readonly_view()||in_preparation_menu()||matchday_results_)
        throw FormatError("achievements: expected matching ordinary desk menu 4");
    auto next=std::make_unique<AchievementsScreen>(data_,std::move(state));
    desktop_->resolve_local_menu();achievements_=std::move(next);
}

void StartupFlow::acknowledge_environment(EnvironmentView state, std::optional<PreparationState> preparation) {
    const bool changed = environment_ && environment_->pending() && state.revision == environment_->state().revision + 1;
    if (!environment_ || !environment_->pending() || !desktop_ || !preparation_ ||
        environment_->pending()->owner != environment_owner_ ||
        state.owner.manager_slot != environment_owner_.manager_slot || state.owner.menu_kind != 24 ||
        state.owner.revision != state.revision ||
        state.manager_slot != environment_owner_.manager_slot ||
        state.runtime_club != environment_->state().runtime_club ||
        (changed && (!preparation || preparation->desktop.revision != state.revision ||
            preparation->desktop.manager_slot != state.manager_slot || preparation->desktop.runtime_club != state.runtime_club ||
            preparation->desktop.day_counter != desktop_->state().day_counter ||
            preparation->desktop.options != desktop_->state().options)) ||
        (!changed && (state.revision != environment_->state().revision || preparation.has_value())))
        throw FormatError("environment: mismatched action acknowledgement");
    auto next_environment = std::make_unique<EnvironmentScreen>(*environment_);
    next_environment->acknowledge(state);
    std::unique_ptr<DesktopScreen> next_desktop;
    std::unique_ptr<PreparationState> next_preparation;
    if (changed) {
        validate_preparation_context(*preparation);
        next_desktop = std::make_unique<DesktopScreen>(data_, preparation->desktop);
        next_preparation = std::make_unique<PreparationState>(std::move(*preparation));
    }
    if (changed) { desktop_ = std::move(next_desktop); preparation_ = std::move(next_preparation); }
    environment_ = std::move(next_environment);
    environment_owner_.revision = environment_->state().revision;
}

void StartupFlow::reject_environment_action(std::string reason) {
    if (!environment_ || !environment_->pending()) throw FormatError("environment: no pending action to reject");
    environment_->reject_action(std::move(reason));
}

void StartupFlow::show_contracts(ContractsState state) {
    if (!desktop_ || !engine_attached_ || in_preparation_menu() || in_readonly_view() || matchday_results_ ||
        !desktop_->pending() || desktop_->pending()->kind != DeskRequestKind::open_menu ||
        desktop_->pending()->menu_kind != 27 || state.manager_slot != desktop_->state().manager_slot ||
        state.runtime_club != desktop_->state().runtime_club || state.revision != desktop_->state().revision)
        throw FormatError("contracts: expected active desk menu 27 and matching projection");
    state.request_id_base = contracts_generation_++ << 32;
    auto screen = std::make_unique<ContractsScreen>(data_, std::move(state));
    desktop_->resolve_local_menu();
    contracts_ = std::move(screen);
}

void StartupFlow::acknowledge_contract_quote(ContractsState state, DeskState desk, PreparationState preparation) {
    if (!contracts_ || !contracts_->pending() ||
        contracts_->pending()->kind != ContractActionKind::request_demands || !desktop_ || !engine_attached_ ||
        state.manager_slot != desktop_->state().manager_slot || state.revision != desktop_->state().revision + 1 ||
        contracts_->pending()->revision != desktop_->state().revision || desk.source != "engine_projection" ||
        desk.revision != state.revision || preparation.desktop.revision != state.revision ||
        preparation.desktop.manager_slot != state.manager_slot || desk_json(preparation.desktop) != desk_json(desk) ||
        desk.manager_teams != desktop_->state().manager_teams || desk.runtime_club != desktop_->state().runtime_club ||
        desk.day_counter != desktop_->state().day_counter || desk.season_index != desktop_->state().season_index ||
        desk.season_day != desktop_->state().season_day || desk.manager_name != desktop_->state().manager_name ||
        desk.portrait != desktop_->state().portrait || desk.badge != desktop_->state().badge ||
        desk.options != desktop_->state().options)
        throw FormatError("contracts: mismatched demand acknowledgement context");
    validate_desk_state(desk);
    validate_preparation_context(preparation);
    auto next_desktop = std::make_unique<DesktopScreen>(data_, desk);
    auto next_preparation = std::make_unique<PreparationState>(std::move(preparation));
    auto next_contracts = std::make_unique<ContractsScreen>(*contracts_);
    next_contracts->acknowledge_quote(state);
    desktop_ = std::move(next_desktop);
    preparation_ = std::move(next_preparation);
    contracts_ = std::move(next_contracts);
}

void StartupFlow::acknowledge_contract_offer(ContractsState state, DeskState desk, PreparationState preparation) {
    acknowledge_contract_mutation(std::move(state), std::move(desk), std::move(preparation), ContractActionKind::submit_offer);
}

void StartupFlow::acknowledge_contract_bonuses(ContractsState state, DeskState desk, PreparationState preparation) {
    acknowledge_contract_mutation(std::move(state), std::move(desk), std::move(preparation), ContractActionKind::submit_bonuses);
}

void StartupFlow::acknowledge_contract_insurance(ContractsState state, DeskState desk, PreparationState preparation) {
    acknowledge_contract_mutation(std::move(state), std::move(desk), std::move(preparation), ContractActionKind::insure_player);
}

void StartupFlow::acknowledge_contract_mutation(ContractsState state, DeskState desk, PreparationState preparation,
                                                ContractActionKind expected) {
    if (!contracts_ || !contracts_->pending() || contracts_->pending()->kind != expected || !desktop_ || !engine_attached_ ||
        state.manager_slot != desktop_->state().manager_slot || state.revision != desktop_->state().revision + 1 ||
        contracts_->pending()->revision != desktop_->state().revision || desk.source != "engine_projection" ||
        desk.revision != state.revision || preparation.desktop.revision != state.revision ||
        preparation.desktop.manager_slot != state.manager_slot || desk_json(preparation.desktop) != desk_json(desk) ||
        desk.manager_teams != desktop_->state().manager_teams || desk.runtime_club != desktop_->state().runtime_club ||
        desk.day_counter != desktop_->state().day_counter || desk.season_index != desktop_->state().season_index ||
        desk.season_day != desktop_->state().season_day || desk.manager_name != desktop_->state().manager_name ||
        desk.portrait != desktop_->state().portrait || desk.badge != desktop_->state().badge ||
        desk.options != desktop_->state().options)
        throw FormatError("contracts: mismatched offer acknowledgement context");
    validate_desk_state(desk);
    validate_preparation_context(preparation);
    auto next_desktop = std::make_unique<DesktopScreen>(data_, desk);
    auto next_preparation = std::make_unique<PreparationState>(std::move(preparation));
    auto next_contracts = std::make_unique<ContractsScreen>(*contracts_);
    if (expected == ContractActionKind::submit_offer) next_contracts->acknowledge_offer(state);
    else if (expected == ContractActionKind::submit_bonuses) next_contracts->acknowledge_bonus_offer(state);
    else if (expected == ContractActionKind::insure_player) next_contracts->acknowledge_insurance(state);
    else throw FormatError("contracts: invalid mutation acknowledgement kind");
    desktop_ = std::move(next_desktop);
    preparation_ = std::move(next_preparation);
    contracts_ = std::move(next_contracts);
}

void StartupFlow::reject_contract_action(std::string reason) {
    if (!contracts_ || !contracts_->pending() || reason.empty())
        throw FormatError("contracts: rejection requires a pending request");
    contracts_->reject(std::move(reason));
}

FlowResult StartupFlow::route_desk(DeskRequest action) {
    if (action.kind == DeskRequestKind::none) return {};
    if (action.kind == DeskRequestKind::end_session) return {true, FlowRequest::end_session, action};
    if (action.kind == DeskRequestKind::advance_turn) return {true, FlowRequest::kickoff, action};
    if (action.kind == DeskRequestKind::open_menu && action.menu_kind == 53 &&
        (action.value == 0 || action.value == 1)) {
        auto screen = std::make_unique<SaveLoadScreen>(data_, save_directory_,
            action.value == 0 ? SaveLoadMode::load : SaveLoadMode::save,
            action.manager_slot, action.revision, desktop_->render(), desktop_->state().gradient_text,
            SaveLoadFileKind::menu_layout, action.item_id);
        desktop_->resolve_local_menu();
        save_load_ = std::move(screen);
        return {true, FlowRequest::submenu_opened, action};
    }
    if (action.kind == DeskRequestKind::open_menu && (action.menu_kind == 10 || action.menu_kind == 11)) {
        auto screen = std::make_unique<SaveLoadScreen>(data_, save_directory_,
            action.menu_kind == 10 ? SaveLoadMode::load : SaveLoadMode::save,
            action.manager_slot, action.revision, desktop_->render(), desktop_->state().gradient_text);
        desktop_->resolve_local_menu();
        save_load_ = std::move(screen);
        return {true, FlowRequest::submenu_opened, action};
    }
    if (!preparation_ || action.kind != DeskRequestKind::open_menu ||
        (action.menu_kind != 3 && action.menu_kind != 13 && action.menu_kind != 14 && action.menu_kind != 15 && action.menu_kind != 16 && action.menu_kind != 17 && action.menu_kind != 18 && action.menu_kind != 22) ||
        (action.menu_kind == 15 && !preparation_->training_camp) ||
        (action.menu_kind == 16 && !preparation_->training))
        return {true, FlowRequest::desk_action, action};
    // Construct before consuming navigation, so a corrupt projection leaves the
    // original pending command intact. Opening a view never writes engine state.
    PreparationRequest entry;
    if (action.menu_kind == 3) {
        auto state = preparation_->lineup;
        if (engine_attached_) state.entry_prepared = false;
        auto screen = std::make_unique<LineupScreen>(data_, std::move(state));
        if (engine_attached_) { const auto request = screen->initialize(); if (request.kind != LineupRequestKind::none) entry = request; }
        if (!engine_attached_ && (screen->state().saved_display_flags & 8) &&
            preparation_->pitch && preparation_->pitch->indoor.has_value())
            pitch_ = std::make_unique<PitchScreen>(data_, screen->state(), *preparation_->pitch);
        lineup_ = std::move(screen);
    } else if (action.menu_kind == 16) {
        auto screen = std::make_unique<TrainingScreen>(data_, *preparation_->training);
        if (engine_attached_) { const auto request = screen->initialize(); if (request.kind != TrainingRequestKind::none) entry = request; }
        training_ = std::move(screen);
    } else if (action.menu_kind == 15) {
        auto screen = std::make_unique<TrainingCampScreen>(data_, *preparation_->training_camp);
        if (engine_attached_) {
            const auto request = screen->initialize_focus();
            if (request.kind != TrainingCampRequestKind::none) entry = request;
        }
        training_camp_ = std::move(screen);
    } else if (action.menu_kind == 14) {
        if (!preparation_->bank) return {true, FlowRequest::desk_action, action};
        bank_ = std::make_unique<BankScreen>(data_, *preparation_->bank);
    } else if (action.menu_kind == 22) {
        if (!preparation_->stadium) return {true, FlowRequest::desk_action, action};
        auto initial = *preparation_->stadium;
        // A closed-family ACK retains its consumed token. A new opening has no
        // owned scope until the session allocates one for this menu entry.
        if (!initial.opened) { initial.scope = 0; initial.sequence = 0; }
        auto screen = std::make_unique<StadiumScreen>(data_, std::move(initial));
        if (engine_attached_) entry = screen->initialize();
        stadium_ = std::move(screen);
    } else if (action.menu_kind == 13) {
        if (!preparation_->toto) return {true, FlowRequest::desk_action, action};
        toto_ = std::make_unique<TotoScreen>(data_, *preparation_->toto);
    } else if (action.menu_kind == 17) {
        if (!preparation_->transfer_market) return {true, FlowRequest::desk_action, action};
        transfer_market_ = std::make_unique<TransferMarketScreen>(data_, *preparation_->transfer_market);
    } else {
        tactics_ = std::make_unique<TacticsScreen>(data_, preparation_->tactics);
    }
    desktop_->resolve_local_menu();
    return {true, std::holds_alternative<std::monostate>(entry) ? FlowRequest::submenu_opened : FlowRequest::submenu_action,
            action, std::move(entry)};
}

FlowResult StartupFlow::finish_preparation_input(PreparationRequest action, bool changed) {
    if (player_card_) {
        if (player_card_->close_requested()) { player_card_.reset(); return {true, FlowRequest::submenu_closed}; }
        return {changed};
    }
    if (pitch_) {
        if (const auto* info = std::get_if<PitchRequest>(&action);
            info && info->kind == PitchRequestKind::open_player_info) {
            for (auto card : preparation_->player_cards) if (card.row == info->row && card.unsupported_record.empty()) {
                card.lineup = pitch_->state();
                auto screen = std::make_unique<PlayerCardScreen>(data_, std::move(card), pitch_->render());
                pitch_->resolve_local_navigation();
                player_card_ = std::move(screen);
                return {true, FlowRequest::submenu_opened};
            }
            pitch_->reject_action("Für diesen Spieler liegt keine unterstützte Infoansicht vor.");
            return {true};
        }
    }
    if (const auto* navigation = std::get_if<LineupRequest>(&action)) {
        if (navigation->kind == LineupRequestKind::open_pitch && preparation_->pitch &&
            preparation_->pitch->indoor.has_value()) {
            auto screen = std::make_unique<PitchScreen>(data_, lineup_->state(), *preparation_->pitch);
            auto page = screen->request_page_control(1);
            if (page.kind == PitchRequestKind::none)
                throw FormatError("pitch entry: Source Team transition could not be prepared");
            lineup_->resolve_local_navigation(LineupRequestKind::open_pitch);
            pitch_ = std::move(screen);
            pitch_entry_pending_ = true;
            FlowResult result{true,FlowRequest::submenu_action};
            result.submenu_action=page;
            return result;
        }
        if (navigation->kind == LineupRequestKind::open_player_info) {
            for (auto card : preparation_->player_cards) if (card.row == navigation->first_row && card.unsupported_record.empty()) {
                card.lineup = lineup_->state();
                // Clear hover/selection in the parent, as the native modal does.
                LineupScreen parent(data_, card.lineup);
                auto screen = std::make_unique<PlayerCardScreen>(data_, std::move(card), parent.render());
                lineup_->resolve_local_navigation(LineupRequestKind::open_player_info);
                player_card_ = std::move(screen);
                return {true, FlowRequest::submenu_opened};
            }
        }
    }
    if (pitch_ && pitch_->close_requested()) {
        const bool return_team = pitch_->return_team_requested();
        preparation_->lineup = pitch_->state();
        auto team = return_team ? std::make_unique<LineupScreen>(data_, preparation_->lineup) : nullptr;
        pitch_.reset(); lineup_ = std::move(team); pitch_entry_pending_=false;
        return {true, FlowRequest::submenu_closed};
    }
    if (training_camp_ && training_camp_->close_requested()) {
        preparation_->training_camp = training_camp_->state();
        training_camp_.reset();
        return {true, FlowRequest::submenu_closed};
    }
    if (bank_ && bank_->close_requested()) { bank_.reset(); return {true, FlowRequest::submenu_closed}; }
    if (stadium_ && stadium_->close_requested()) { stadium_.reset(); return {true, FlowRequest::submenu_closed}; }
    if (toto_ && toto_->close_requested()) { toto_.reset(); return {true, FlowRequest::submenu_closed}; }
    if (transfer_market_ && transfer_market_->close_requested()) { preparation_->transfer_market = transfer_market_->state(); transfer_market_.reset(); return {true, FlowRequest::submenu_closed}; }
    if ((lineup_ && lineup_->close_requested()) || (training_ && training_->close_requested()) ||
        (tactics_ && tactics_->close_requested())) {
        if (lineup_) preparation_->lineup = lineup_->state();
        if (training_) preparation_->training = training_->state();
        if (tactics_) preparation_->tactics = tactics_->state();
        lineup_.reset(); training_.reset(); tactics_.reset();
        return {true, FlowRequest::submenu_closed};
    }
    return {changed, std::holds_alternative<std::monostate>(action) ? FlowRequest::none : FlowRequest::submenu_action,
            {}, std::move(action)};
}

bool StartupFlow::matches_pending_lineup(const LineupRequest& request) const {
    return lineup_ && lineup_->pending() &&
           lineup_request_json(*lineup_->pending()) == lineup_request_json(request);
}

bool StartupFlow::matches_pending_pitch(const PitchRequest& request) const {
    return pitch_ && pitch_->pending() &&
           pitch_request_json(*pitch_->pending()) == pitch_request_json(request);
}

bool StartupFlow::matches_pending_stadium(const StadiumRequest& request) const {
    return stadium_ && stadium_->pending() &&
           stadium_request_json(*stadium_->pending()) == stadium_request_json(request);
}

void StartupFlow::attach_preparation_context(PreparationState state) {
    if (!desktop_ || !engine_attached_ || desktop_->pending() || in_preparation_menu() || in_readonly_view() ||
        state.desktop.source != "engine_projection" || desk_json(state.desktop) != desk_json(desktop_->state()))
        throw FormatError("preparation attachment: expected the attached idle desktop at the same revision");
    validate_preparation_context(state);
    // Validate all renderers before exposing any new context.
    LineupScreen lineup(data_, state.lineup);
    if (state.training) { TrainingScreen training(data_, *state.training); }
    if (state.training_camp) { TrainingCampScreen camp(data_, *state.training_camp); }
    if (state.bank) { BankScreen bank(data_, *state.bank); }
    if (state.toto) { TotoScreen toto(data_, *state.toto); }
    if (state.transfer_market) { TransferMarketScreen market(data_, *state.transfer_market); }
    TacticsScreen tactics(data_, state.tactics);
    if (state.pitch && state.pitch->indoor.has_value()) { PitchScreen pitch(data_, state.lineup, *state.pitch); }
    preparation_ = std::make_unique<PreparationState>(std::move(state));
}

void StartupFlow::show_matchday_results(MatchdayResults results) {
    if (!desktop_ || !engine_attached_ || desktop_->pending() || !preparation_ ||
        in_preparation_menu() || in_readonly_view() || matchday_results_)
        throw FormatError("results: expected an idle prepared engine desktop");
    auto screen = std::make_unique<MatchdayResultsScreen>(data_, std::move(results));
    matchday_results_ = std::move(screen);
}

void StartupFlow::show_league_tables(std::array<LeagueTableState, 9> tables, int initial_league) {
    if (!desktop_ || !engine_attached_ || in_preparation_menu() || in_readonly_view() || matchday_results_ ||
        !desktop_->pending() || desktop_->pending()->kind != DeskRequestKind::open_menu ||
        desktop_->pending()->menu_kind != 0)
        throw FormatError("table: expected a pending table navigation on the engine desk");
    for (const auto& table : tables) if (table.manager_slot != desktop_->state().manager_slot)
        throw FormatError("table: mismatched manager projection");
    auto screen = std::make_unique<LeagueTableScreen>(data_, std::move(tables), initial_league);
    desktop_->resolve_local_menu();
    league_table_ = std::move(screen);
}
void StartupFlow::show_family_league_tables(std::array<LeagueTableState, 9> tables,
                                            int initial_league,StrengthsIdentity identity,
                                            std::optional<PreparationState> preparation) {
    if(!desktop_||!engine_attached_||matchday_results_||identity.family_id==0||
       (league_family_identity_&&(identity.manager!=league_family_identity_->manager||identity.runtime_club!=league_family_identity_->runtime_club||
       identity.club_identity!=league_family_identity_->club_identity||identity.day_counter!=league_family_identity_->day_counter||
       identity.source_param2!=league_family_identity_->source_param2||
       identity.family_id!=league_family_identity_->family_id||
       identity.manager_default_league!=league_family_identity_->manager_default_league))||
       initial_league<0||initial_league>=9||identity.league!=initial_league)
        throw FormatError("league family: mismatched Table transition");
    if(!league_family_identity_&&(!desktop_->pending()||
       desktop_->pending()->kind!=DeskRequestKind::open_menu||desktop_->pending()->menu_kind!=0||
       identity.manager!=desktop_->state().manager_slot||identity.runtime_club!=desktop_->state().runtime_club||
       identity.day_counter!=desktop_->state().day_counter||identity.source_param2!=0||
       identity.revision!=desktop_->state().revision||identity.family_sequence||identity.scope||identity.sequence))
        throw FormatError("league family: invalid Table desktop entry");
    for(const auto& table:tables)if(table.manager_slot!=identity.manager)
        throw FormatError("league family: mismatched Table manager projection");
    auto screen=std::make_unique<LeagueTableScreen>(data_,std::move(tables),initial_league,
        LeagueTableScreen::FamilyMode{identity});
    std::unique_ptr<DesktopScreen> desk;
    std::unique_ptr<PreparationState> next;
    if(preparation) {
        validate_strengths_preparation(identity,desktop_->state(),*preparation);
        desk=std::make_unique<DesktopScreen>(data_,preparation->desktop);
        next=std::make_unique<PreparationState>(std::move(*preparation));
    } else if(identity.revision!=desktop_->state().revision) {
        throw FormatError("league family: Table acknowledgement requires a fresh desktop projection");
    }
    if(league_family_child_)replace_family_child();
    league_table_=std::move(screen);league_family_child_=1;
    league_family_identity_=identity;league_family_identity_->league=initial_league;
    if(desk) {desktop_=std::move(desk);preparation_=std::move(next);}
    if(desktop_->pending())desktop_->resolve_local_menu();
}
void StartupFlow::show_family_table_dispatch(std::array<LeagueTableState, 9> tables,
        int initial_league, StrengthsIdentity identity,
        std::optional<TableRangeDialogProfile> dialog,
        std::optional<TableDialogStage> dialog_stage, bool forecast_needs_present,
        std::optional<PreparationState> preparation,bool friendly_entry) {
    if(bool(dialog)!=bool(dialog_stage) || (dialog&&forecast_needs_present))
        throw FormatError("league family: invalid table dispatch modal profile");
    if(friendly_entry&&(league_family_identity_||friendly_table_active_||in_preparation_menu()||
                       in_readonly_view()||dialog||forecast_needs_present))
        throw FormatError("friendly table: invalid nested entry");
    if(!desktop_||!engine_attached_||matchday_results_||identity.family_id==0||
       (league_family_identity_&&(identity.manager!=league_family_identity_->manager||
        identity.runtime_club!=league_family_identity_->runtime_club||
        identity.club_identity!=league_family_identity_->club_identity||
        identity.day_counter!=league_family_identity_->day_counter||
        identity.source_param2!=league_family_identity_->source_param2||
        identity.family_id!=league_family_identity_->family_id||
        identity.manager_default_league!=league_family_identity_->manager_default_league))||
       initial_league<0||initial_league>=9||identity.league!=initial_league)
        throw FormatError("league family: mismatched Table dispatch identity");
    if(identity.manager<0||size_t(identity.manager)>=desktop_->state().managers.size()||
       identity.manager!=desktop_->state().manager_slot||identity.runtime_club!=desktop_->state().runtime_club||
       identity.club_identity!=int(desktop_->state().managers[size_t(identity.manager)].club_identity_key))
        throw FormatError("league family: invalid Table dispatch manager identity");
    if(!league_family_identity_&&(!desktop_->pending()||
       desktop_->pending()->kind!=DeskRequestKind::open_menu||desktop_->pending()->menu_kind!=(friendly_entry?6:0)||
       identity.manager!=desktop_->state().manager_slot||identity.runtime_club!=desktop_->state().runtime_club||
       identity.day_counter!=desktop_->state().day_counter||identity.source_param2!=0||
       identity.revision!=desktop_->state().revision||identity.family_sequence||identity.scope||identity.sequence))
        throw FormatError("league family: invalid Table dispatch desktop entry");
    if(league_family_identity_) {
        const auto& prior=*league_family_identity_;
        if(league_family_child_==0) {
            if(!desktop_->pending()||desktop_->pending()->kind!=DeskRequestKind::open_menu||
               desktop_->pending()->menu_kind!=(friendly_table_active_?6:0)||identity.family_sequence!=prior.family_sequence)
                throw FormatError("league family: invalid ready Table entry acknowledgement");
        } else {
            std::optional<LeagueFamilyRequest> owner_request;
            if(family_table_dispatch_pending_)owner_request=*family_table_dispatch_pending_;
            else if(league_family_child_==1&&league_table_&&league_table_->pending_family_action())
                owner_request=*league_table_->pending_family_action();
            else if(league_family_child_==2&&pending_pairings_family_action())
                owner_request=*pending_pairings_family_action();
            else if(league_family_child_==3&&pending_strengths()) {
                const auto& pending=*pending_strengths();
                LeagueFamilyRequest translated;translated.identity=pending.identity;translated.from_child=3;
                if(pending.kind==StrengthsAction::table) {
                    translated.kind=LeagueFamilyRequestKind::navigate;translated.target_child=1;
                    owner_request=translated;
                }
            }
            if(prior.family_sequence==UINT64_MAX||!owner_request||owner_request->from_child!=league_family_child_||
               owner_request->identity!=prior||
               identity.family_sequence!=prior.family_sequence+1)
                throw FormatError("league family: Table dispatch lacks the current child ACK owner");
            if(owner_request->kind==LeagueFamilyRequestKind::navigate&&owner_request->target_child!=1)
                throw FormatError("league family: child navigation does not target Table");
            if(league_family_child_!=1&&owner_request->kind!=LeagueFamilyRequestKind::navigate)
                throw FormatError("league family: non-Table child cannot authorize this Table dispatch");
            if(league_family_child_==1&&owner_request->kind!=LeagueFamilyRequestKind::control&&
               owner_request->kind!=LeagueFamilyRequestKind::select_league&&
               owner_request->kind!=LeagueFamilyRequestKind::toggle_league_group&&
               owner_request->kind!=LeagueFamilyRequestKind::team_info&&
               owner_request->kind!=LeagueFamilyRequestKind::table_range_result&&
               owner_request->kind!=LeagueFamilyRequestKind::table_forecast_presented)
                throw FormatError("league family: Table request cannot authorize a Table dispatch");
            if(league_family_child_==1&&owner_request->kind==LeagueFamilyRequestKind::control&&
               (owner_request->control_index<0||owner_request->control_index>9))
                throw FormatError("league family: invalid Table control ACK index");
            const auto selector_kind=[](LeagueFamilyRequestKind kind) {
                return kind==LeagueFamilyRequestKind::select_league||
                    kind==LeagueFamilyRequestKind::toggle_league_group;
            };
            if(league_family_child_==1&&selector_kind(owner_request->kind)&&
               owner_request->league_id!=initial_league)
                throw FormatError("league family: Table selector ACK changed its expected target league");
            if(league_family_child_==1) {
                const auto kind=owner_request->kind;
                const bool selector=selector_kind(kind);
                const uint8_t expected_group=kind==LeagueFamilyRequestKind::toggle_league_group
                    ?uint8_t(prior.league_group^1u):prior.league_group;
                if(identity.scope!=prior.scope||identity.sequence!=prior.sequence||
                   (!selector&&(identity.league!=prior.league||identity.league_group!=prior.league_group))||
                   (selector&&(identity.revision!=prior.revision||identity.league_group!=expected_group||
                               identity.league!=owner_request->league_id))||
                   (!selector&&(prior.revision==UINT64_MAX||identity.revision!=prior.revision+1)))
                    throw FormatError("league family: Table ACK changed the bound revision, group, or child scope");
                bool expected_modal=false;
                bool expected_forecast=false;
                TableDialogStage expected_stage=TableDialogStage::start;
                if(kind==LeagueFamilyRequestKind::control) {
                    expected_modal=owner_request->control_index==9;
                    expected_forecast=owner_request->control_index==3&&(prior.expected_family_word&8)==0;
                } else if(kind==LeagueFamilyRequestKind::table_range_result) {
                    if(!family_table_range_stage_||!owner_request->table_dialog_reply||
                       owner_request->control_index!=9||
                       owner_request->table_dialog_reply->stage!=*family_table_range_stage_||
                       (owner_request->table_dialog_reply->outcome!=TableDialogOutcome::accept&&
                        owner_request->table_dialog_reply->outcome!=TableDialogOutcome::cancel))
                        throw FormatError("league family: invalid Table range ACK owner");
                    expected_modal=*family_table_range_stage_==TableDialogStage::start&&
                        owner_request->table_dialog_reply->outcome==TableDialogOutcome::accept;
                    expected_stage=TableDialogStage::end;
                } else if(kind==LeagueFamilyRequestKind::table_forecast_presented) {
                    if(owner_request->control_index!=3||owner_request->table_dialog_reply||
                       !league_table_->state().forecast)
                        throw FormatError("league family: invalid Table forecast ACK owner");
                }
                if(bool(dialog)!=expected_modal||forecast_needs_present!=expected_forecast||
                   (expected_modal&&*dialog_stage!=expected_stage))
                    throw FormatError("league family: Table ACK does not match its pending stage");
            } else if(dialog||forecast_needs_present||identity.league!=prior.league||
                      identity.revision<prior.revision||identity.revision-prior.revision>1u) {
                throw FormatError("league family: child entry cannot acknowledge a Table modal or forecast");
            }
        }
    }
    for(const auto& table:tables)if(table.manager_slot!=identity.manager)
        throw FormatError("league family: mismatched Table dispatch manager projection");
    if(dialog) {
        const uint32_t expected=(*dialog_stage==TableDialogStage::start)?::bmh::layout::CareerSession_CurrentLeagueTableDispatch_ExecutableField: ::bmh::layout::CareerSession_CurrentLeagueTableDispatch_ExecutableField2;
        if(dialog->title_textd_address!=expected||dialog->minimum>dialog->maximum||
           dialog->default_value>dialog->maximum||!dialog->text_mode||
           dialog->x<0||dialog->x>207||dialog->y<0||dialog->y>104)
            throw FormatError("league family: invalid Source table range profile");
    }
    if(preparation)validate_strengths_preparation(identity,desktop_->state(),*preparation);
    else if(identity.revision!=desktop_->state().revision)
        throw FormatError("league family: Table dispatch requires a fresh desktop projection");

    // InLeaguePairings keeps the inner Table loop when the selected league is unchanged.
    // Consume the token in place so a visible KI page/body is not replaced by normal scratch.
    const auto* selector_ack=pending_league_family_action();
    const bool same_league_selector=league_family_child_==1&&selector_ack&&
        (selector_ack->kind==LeagueFamilyRequestKind::select_league||
         selector_ack->kind==LeagueFamilyRequestKind::toggle_league_group)&&
        identity.league==selector_ack->identity.league;
    if(same_league_selector) {
        if(dialog||dialog_stage||forecast_needs_present||preparation||!league_table_||
           identity.revision!=selector_ack->identity.revision)
            throw FormatError("league family: same-league selector cannot replace its retained Table frame");
        acknowledge_family_selector(*selector_ack,identity);
        return;
    }

    // Construct every potentially throwing replacement before changing the live Flow.
    auto next_table=std::make_unique<LeagueTableScreen>(data_,std::move(tables),initial_league,
        LeagueTableScreen::FamilyMode{identity,uint8_t((friendly_entry||friendly_table_active_)?2:0)});
    std::unique_ptr<LeagueTableRangeDialog> next_dialog;
    if(dialog) {
        if(!league_table_)throw FormatError("league family: range dialog requires a rendered Table underlay");
        // Both Source KB6 invocations restore the same saved Table image.
        // In particular, post-KI normal scratch is not the visible forecast.
        const auto backdrop=family_table_range_dialog_?family_table_range_dialog_->source_backdrop():
            league_table_->render();
        next_dialog=std::make_unique<LeagueTableRangeDialog>(data_,backdrop,*dialog);
    }
    std::unique_ptr<DesktopScreen> next_desk;
    std::unique_ptr<PreparationState> next_preparation;
    if(preparation) {
        next_desk=std::make_unique<DesktopScreen>(data_,preparation->desktop);
        next_preparation=std::make_unique<PreparationState>(std::move(*preparation));
    } else if(desktop_->pending()) {
        // A replacement built from the state is already idle; the old pending
        // navigation is consumed by the atomic replacement below.
        next_desk=std::make_unique<DesktopScreen>(data_,desktop_->state());
    }
    if(league_family_child_)replace_family_child();
    league_table_=std::move(next_table);league_family_child_=1;
    league_family_identity_=identity;league_family_identity_->league=initial_league;
    if(next_desk){desktop_=std::move(next_desk);if(next_preparation)preparation_=std::move(next_preparation);}
    family_table_range_dialog_=std::move(next_dialog);
    family_table_range_profile_=std::move(dialog);
    family_table_range_stage_=dialog_stage;
    family_table_last_outcome_.reset();family_table_dispatch_pending_.reset();
    family_table_forecast_needs_present_=forecast_needs_present;
    family_table_source_clock_anchor_valid_=false;family_table_reset_clock_after_present_=false;
    family_table_source_clock_anchor_=0;
    if(friendly_entry)friendly_table_active_=true;
}
void StartupFlow::acknowledge_family_selector(const LeagueFamilyRequest& old_request,
    const StrengthsIdentity& accepted_identity) {
    const auto* pending=pending_league_family_action();
    if(!in_family_table()||!league_family_identity_||!league_table_||!pending||*pending!=old_request||
       old_request.identity!=*league_family_identity_||old_request.from_child!=1||
       (old_request.kind!=LeagueFamilyRequestKind::select_league&&
        old_request.kind!=LeagueFamilyRequestKind::toggle_league_group)||
       old_request.target_child!=0||old_request.runtime_team!=-1||old_request.expected_club_identity!=-1||
       old_request.table_row_index!=-1||old_request.control_index!=-1||old_request.table_dialog_reply||
       old_request.league_id!=accepted_identity.league||
       old_request.identity.league!=accepted_identity.league||
       old_request.identity.family_sequence==UINT64_MAX||
       accepted_identity.family_sequence!=old_request.identity.family_sequence+1||
       accepted_identity.revision!=old_request.identity.revision||
       accepted_identity.scope!=old_request.identity.scope||accepted_identity.sequence!=old_request.identity.sequence||
       accepted_identity.manager!=old_request.identity.manager||
       accepted_identity.runtime_club!=old_request.identity.runtime_club||
       accepted_identity.club_identity!=old_request.identity.club_identity||
       accepted_identity.day_counter!=old_request.identity.day_counter||
       accepted_identity.source_param2!=old_request.identity.source_param2||
       accepted_identity.family_id!=old_request.identity.family_id||
       accepted_identity.manager_default_league!=old_request.identity.manager_default_league||
       accepted_identity.expected_settings_word!=old_request.identity.expected_settings_word||
       accepted_identity.league_group>1)
        throw FormatError("league family: stale or malformed same-league selector acknowledgement");
    const uint8_t expected_group=old_request.kind==LeagueFamilyRequestKind::toggle_league_group
        ?uint8_t(old_request.identity.league_group^1u):old_request.identity.league_group;
    if(accepted_identity.league_group!=expected_group)
        throw FormatError("league family: same-league selector ACK group does not match operation");
    if(family_table_range_dialog_||family_table_dispatch_pending_||family_table_forecast_needs_present_)
        throw FormatError("league family: selector ACK cannot consume a modal or pending forecast Present");
    league_table_->acknowledge_same_league_selector(old_request,accepted_identity);
    league_family_identity_=accepted_identity;
    family_table_last_outcome_.reset();
    family_table_dispatch_pending_.reset();
    family_table_source_clock_anchor_valid_=false;
    family_table_reset_clock_after_present_=false;
    family_table_source_clock_anchor_=0;
}

FlowResult StartupFlow::after_family_table_present(uint64_t absolute_source_units) {
    if(family_table_dispatch_pending_||!in_family_table())return {};
    if(family_table_range_dialog_) {
        if(family_table_reset_clock_after_present_||!family_table_source_clock_anchor_valid_) {
            family_table_source_clock_anchor_=absolute_source_units;
            family_table_source_clock_anchor_valid_=true;
            family_table_reset_clock_after_present_=false;
        }
        const bool exit_before=family_table_range_dialog_->waiting_for_exit_present();
        const auto outcome=family_table_range_dialog_->after_present();
        if(!outcome)return {!exit_before&&family_table_range_dialog_->waiting_for_exit_present()};
        family_table_last_outcome_=outcome;
        LeagueFamilyRequest request;request.identity=*league_family_identity_;
        request.kind=LeagueFamilyRequestKind::table_range_result;request.from_child=1;request.control_index=9;
        request.table_dialog_reply=TableDialogReply{*family_table_range_stage_,
            outcome->result==TableRangeDialogResult::accept?TableDialogOutcome::accept:TableDialogOutcome::cancel,
            outcome->result==TableRangeDialogResult::accept?int(outcome->value):0,outcome->x,outcome->y};
        family_table_dispatch_pending_=request;
        FlowResult result{true,FlowRequest::league_family_action};result.league_family_action=request;return result;
    }
    if(family_table_forecast_needs_present_) {
        family_table_forecast_needs_present_=false;
        LeagueFamilyRequest request;request.identity=*league_family_identity_;
        request.kind=LeagueFamilyRequestKind::table_forecast_presented;request.from_child=1;request.control_index=3;
        family_table_dispatch_pending_=request;
        FlowResult result{false,FlowRequest::league_family_action};result.league_family_action=request;return result;
    }
    return {};
}
FlowResult StartupFlow::after_toto_present(uint64_t source_units) {
    return {toto_ && toto_->after_present(source_units)};
}
FlowResult StartupFlow::tick_toto_source_units(uint64_t source_units) {
    return {toto_ && toto_->tick_source_units(source_units)};
}
FlowResult StartupFlow::tick_family_table_source_units(uint64_t absolute_source_units) {
    if(!family_table_range_dialog_||family_table_dispatch_pending_||!family_table_source_clock_anchor_valid_||
       family_table_reset_clock_after_present_)return {};
    const uint64_t elapsed=absolute_source_units>=family_table_source_clock_anchor_
        ?absolute_source_units-family_table_source_clock_anchor_:0;
    family_table_source_clock_anchor_=absolute_source_units;
    return {family_table_range_dialog_->tick_source_units(uint32_t(std::min<uint64_t>(elapsed,
        std::numeric_limits<uint32_t>::max())))};
}
FlowResult StartupFlow::cancel_family_table_from_source_event() {
    if(!family_table_range_dialog_||family_table_dispatch_pending_)return {};
    const bool changed=family_table_range_dialog_->cancel_from_source_event();
    if(changed)family_table_reset_clock_after_present_=true;
    return {changed};
}
void StartupFlow::begin_league_family(StrengthsIdentity identity) {
    if(!desktop_||!engine_attached_||league_family_identity_||identity.manager!=desktop_->state().manager_slot||
       identity.runtime_club!=desktop_->state().runtime_club||identity.day_counter!=desktop_->state().day_counter||
       identity.revision!=desktop_->state().revision||identity.source_param2!=0||identity.scope||identity.sequence||
       identity.family_id==0)
        throw FormatError("league family: invalid desktop entry identity");
    if(identity.manager<0||size_t(identity.manager)>=desktop_->state().managers.size()||
       identity.club_identity!=int(desktop_->state().managers[size_t(identity.manager)].club_identity_key))
        throw FormatError("league family: invalid manager identity");
    league_family_identity_=identity;league_family_child_=0;
}
void StartupFlow::acknowledge_friendly_own_club(const LeagueFamilyRequest& pending,StrengthsIdentity accepted) {
    const auto* action=pending_league_family_action();
    if(!friendly_table_active_||!league_table_||!league_family_identity_||!action||*action!=pending||
       pending.identity!=*league_family_identity_)
        throw FormatError("friendly table: own-club return lacks its pending row");
    league_table_->acknowledge_friendly_own_club(accepted);league_family_identity_=accepted;
    family_table_release_barrier_=false;
}
void StartupFlow::complete_friendly_opponent_table(const LeagueFamilyRequest& pending,
        FriendlyMatchState friendly,PreparationState preparation) {
    const auto* action=pending_league_family_action();
    if(!friendly_table_active_||!action||*action!=pending||
       pending.kind!=LeagueFamilyRequestKind::select_opponent||friendly.selected_opponent<0||
       friendly.selected_opponent==friendly.runtime_team||friendly.selected_league!=pending.identity.league||
       friendly.revision!=preparation.desktop.revision||
       friendly.manager_slot!=preparation.desktop.manager_slot||
       friendly.runtime_team!=preparation.desktop.runtime_club)
        throw FormatError("friendly table: mismatched opponent return acknowledgement");
    friendly.first_request_id=next_friendly_request_id_;
    auto screen=std::make_unique<FriendlyMatchScreen>(data_,std::move(friendly),6);
    auto close=pending;close.kind=LeagueFamilyRequestKind::close;
    close_league_family(close,std::move(preparation));
    friendly_match_=std::move(screen);pending_friendly_action_.reset();
}
void StartupFlow::close_league_family(const LeagueFamilyRequest& pending,PreparationState state) {
    if(pending.kind!=LeagueFamilyRequestKind::close||pending.target_child!=0||!desktop_||
       !league_family_identity_||pending.from_child!=league_family_child_||
       pending.identity.revision!=desktop_->state().revision||
       pending.identity.manager!=league_family_identity_->manager||
       pending.identity.runtime_club!=league_family_identity_->runtime_club||
       pending.identity.family_id!=league_family_identity_->family_id||
       state.desktop.revision!=desktop_->state().revision+1||
       state.desktop.source!="engine_projection"||
       state.desktop.manager_slot!=desktop_->state().manager_slot||
       state.desktop.runtime_club!=desktop_->state().runtime_club||
       state.desktop.manager_teams!=desktop_->state().manager_teams||
       state.desktop.day_counter!=desktop_->state().day_counter||
       state.desktop.season_day!=desktop_->state().season_day||
       state.desktop.season_index!=desktop_->state().season_index)
        throw FormatError("league family: mismatched final close acknowledgement");
    validate_preparation_context(state);
    auto desk=std::make_unique<DesktopScreen>(data_,state.desktop);
    auto next=std::make_unique<PreparationState>(std::move(state));
    strengths_.reset();league_pairings_.reset();league_table_.reset();
    family_table_range_dialog_.reset();family_table_range_profile_.reset();
    family_table_range_stage_.reset();family_table_last_outcome_.reset();
    family_table_dispatch_pending_.reset();family_table_forecast_needs_present_=false;
    family_table_source_clock_anchor_valid_=false;family_table_reset_clock_after_present_=false;
    desktop_=std::move(desk);preparation_=std::move(next);league_family_identity_.reset();league_family_child_=0;
    friendly_table_active_=false;
    family_table_release_barrier_=false;
}
void StartupFlow::reject_league_family(std::string reason) {
    if(reason.empty())throw FormatError("league family: rejection requires a reason");
    if(family_table_dispatch_pending_) {
        if(family_table_dispatch_pending_->kind==LeagueFamilyRequestKind::table_range_result) {
            if(!family_table_range_dialog_||!family_table_range_profile_||!family_table_range_stage_||!league_table_)
                throw FormatError("league family: missing range dialog rejection owner");
            auto profile=*family_table_range_profile_;
            if(family_table_last_outcome_&&family_table_last_outcome_->result==TableRangeDialogResult::accept)
                profile.default_value=family_table_last_outcome_->value;
            profile.x=family_table_last_outcome_?family_table_last_outcome_->x:profile.x;
            profile.y=family_table_last_outcome_?family_table_last_outcome_->y:profile.y;
            auto retry=std::make_unique<LeagueTableRangeDialog>(data_,family_table_range_dialog_->source_backdrop(),profile);
            family_table_range_dialog_=std::move(retry);family_table_range_profile_=profile;
            family_table_last_outcome_.reset();family_table_source_clock_anchor_valid_=false;
            family_table_reset_clock_after_present_=false;
        } else if(family_table_dispatch_pending_->kind==LeagueFamilyRequestKind::table_forecast_presented) {
            family_table_forecast_needs_present_=true;
        }
        family_table_dispatch_pending_.reset();
        return;
    }
    if(league_family_child_==1&&league_table_&&league_table_->pending_family_action())
        league_table_->reject_family_action(std::move(reason));
    else if(league_family_child_==2&&league_pairings_&&league_pairings_->pending_family_action())
        league_pairings_->reject_family_action(std::move(reason));
    else if(league_family_child_==3&&strengths_&&strengths_->pending())
        strengths_->reject_action(std::move(reason));
    else throw FormatError("league family: missing pending child request");
}

void StartupFlow::show_calendar(CalendarState state, const DeskRequest& owner) {
    if (!desktop_ || !engine_attached_ || in_preparation_menu() || in_readonly_view() || matchday_results_ ||
        !desktop_->pending() || *desktop_->pending() != owner || owner.kind != DeskRequestKind::open_menu ||
        (owner.menu_kind != 8 && owner.menu_kind != 7) || owner.manager_slot != desktop_->state().manager_slot ||
        owner.revision != desktop_->state().revision ||
        state.current_day < 0 || size_t(state.current_day) >= state.days.size() ||
        state.days[size_t(state.current_day)].season_day != desktop_->state().season_day ||
        state.days[size_t(state.current_day)].day_counter != desktop_->state().day_counter)
        throw FormatError("calendar: expected the exact pending navigation and current season");
    auto screen = std::make_unique<CalendarScreen>(data_, std::move(state),
        original_to_utf8(desktop_->state().manager_name), desktop_->state().decorated_background,
        desktop_->state().runtime_club, owner);
    desktop_->resolve_local_menu();
    calendar_ = std::move(screen);
}

void StartupFlow::show_indoor_standings(IndoorStandingsState state, const DeskRequest& owner) {
    if (!desktop_ || !engine_attached_ || in_preparation_menu() || in_readonly_view() || matchday_results_ ||
        !desktop_->pending() || *desktop_->pending() != owner || owner.kind != DeskRequestKind::open_menu ||
        owner.menu_kind != 9 || owner.manager_slot != desktop_->state().manager_slot ||
        owner.revision != desktop_->state().revision || state.owner != owner)
        throw FormatError("indoor standings: expected exact pending ordinary desk navigation");
    auto next = std::make_unique<IndoorStandingsScreen>(data_, std::move(state));
    desktop_->resolve_local_menu();
    indoor_standings_ = std::move(next);
}

void StartupFlow::acknowledge_reschedule(CalendarState state, DeskRequest owner, DeskState desktop,
                                         PreparationState preparation) {
    if (!calendar_ || !pending_reschedule_action() || owner.menu_kind != 7 ||
        owner.manager_slot != desktop.manager_slot || owner.revision != desktop.revision ||
        desktop.revision <= calendar_->owner().revision || preparation.desktop.revision != desktop.revision ||
        desk_json(preparation.desktop) != desk_json(desktop))
        throw FormatError("calendar: reschedule acknowledgement does not match the active request");
    validate_preparation_context(preparation);
    auto next_desktop = std::make_unique<DesktopScreen>(data_, std::move(desktop));
    auto next_preparation = std::make_unique<PreparationState>(std::move(preparation));
    calendar_->acknowledge_reschedule(std::move(state), owner);
    desktop_ = std::move(next_desktop);
    preparation_ = std::move(next_preparation);
}

void StartupFlow::reject_reschedule() {
    if (!calendar_ || !pending_reschedule_action()) throw FormatError("calendar: no pending reschedule action");
    calendar_->reject_reschedule();
}

void StartupFlow::show_friendly_match(FriendlyMatchState state, int menu_kind) {
    if (!desktop_ || !engine_attached_ || in_preparation_menu() || in_readonly_view() || matchday_results_ ||
        !desktop_->pending() || desktop_->pending()->kind != DeskRequestKind::open_menu ||
        desktop_->pending()->menu_kind != 6 || menu_kind != 6 ||
        state.manager_slot != desktop_->state().manager_slot || state.runtime_team != desktop_->state().runtime_club ||
        state.revision != desktop_->state().revision || state.manager_name != original_to_utf8(desktop_->state().manager_name))
        throw FormatError("friendly match: expected the active club-booking menu and matching engine projection");
    state.first_request_id = next_friendly_request_id_;
    auto screen = std::make_unique<FriendlyMatchScreen>(data_, std::move(state), menu_kind);
    desktop_->resolve_local_menu();
    friendly_match_ = std::move(screen);
    pending_friendly_action_.reset();
}

void StartupFlow::show_mailbox(MailboxState state) {
    if (!desktop_ || !engine_attached_ || in_preparation_menu() || in_readonly_view() || matchday_results_ ||
        !desktop_->pending() || desktop_->pending()->kind != DeskRequestKind::open_menu ||
        desktop_->pending()->menu_kind != 63 || state.manager_slot != desktop_->state().manager_slot ||
        state.revision != desktop_->state().revision)
        throw FormatError("mailbox: expected a pending message navigation on the engine desk");
    auto screen = std::make_unique<MailboxScreen>(data_, std::move(state),
        original_to_utf8(desktop_->state().manager_name), desktop_->render());
    desktop_->resolve_local_menu();
    mailbox_ = std::move(screen);
}

void StartupFlow::show_settings(SettingsState state, uint64_t owner_sequence, DeskRequest owner) {
    if (!desktop_ || !engine_attached_ || in_preparation_menu() || in_readonly_view() || matchday_results_ ||
        !desktop_->pending() || desktop_->pending()->kind != DeskRequestKind::open_menu ||
        desktop_->pending()->menu_kind != 19 || state.manager_slot != desktop_->state().manager_slot ||
        (owner_sequence ? state.revision != desktop_->state().revision + 1 :
                          state.revision != desktop_->state().revision))
        throw FormatError("settings view requires the active pending Original settings menu");
    if (owner_sequence && (owner.kind != DeskRequestKind::open_menu || owner.menu_kind != 19 ||
        owner.item_id < 0 || owner.revision + 1 != state.revision))
        throw FormatError("settings view requires its authenticated desk owner");
    auto screen = std::make_unique<SettingsScreen>(data_, std::move(state), owner.item_id);
    desktop_->resolve_local_menu();
    settings_ = std::move(screen);
    settings_owner_sequence_ = owner_sequence;
    settings_desk_owner_ = owner;
    if (owner_sequence) settings_desk_owner_.revision = state.revision;
}

void StartupFlow::show_advertising(AdvertisingState state) {
    if (!desktop_ || !engine_attached_ || in_preparation_menu() || in_readonly_view() || matchday_results_ ||
        !desktop_->pending() || desktop_->pending()->kind != DeskRequestKind::open_menu ||
        desktop_->pending()->menu_kind != 21 || state.manager_slot != desktop_->state().manager_slot ||
        state.runtime_team_id != desktop_->state().runtime_club || state.revision != desktop_->state().revision)
        throw FormatError("advertising view requires the active pending Original advertising menu");
    auto screen = std::make_unique<AdvertisingScreen>(data_, std::move(state), advertising_generation_ << 32);
    desktop_->resolve_local_menu();
    advertising_ = std::move(screen);
    ++advertising_generation_;
}

void StartupFlow::acknowledge_advertising(AdvertisingState state, DeskState desk, PreparationState preparation) {
    if (!advertising_ || !advertising_->pending() || !desktop_ || !engine_attached_)
        throw FormatError("advertising: acknowledgement requires a pending action");
    const auto request = *advertising_->pending();
    const auto& previous = desktop_->state();
    if (request.manager_slot != previous.manager_slot || request.runtime_club != previous.runtime_club ||
        request.revision != previous.revision || state.manager_slot != previous.manager_slot ||
        state.runtime_team_id != previous.runtime_club || state.revision != previous.revision + 1 ||
        desk.source != "engine_projection" || desk.revision != state.revision ||
        desk.manager_slot != previous.manager_slot || desk.runtime_club != previous.runtime_club ||
        desk.manager_teams != previous.manager_teams || desk.day_counter != previous.day_counter ||
        desk.season_index != previous.season_index || desk.season_day != previous.season_day ||
        desk.manager_name != previous.manager_name || desk.portrait != previous.portrait ||
        desk.badge != previous.badge || desk.options != previous.options ||
        desk_json(preparation.desktop) != desk_json(desk))
        throw FormatError("advertising: mismatched acknowledgement context");
    validate_desk_state(desk);
    validate_preparation_context(preparation);
    auto next_desktop = std::make_unique<DesktopScreen>(data_, desk);
    auto next_preparation = std::make_unique<PreparationState>(std::move(preparation));
    auto next_advertising = std::make_unique<AdvertisingScreen>(data_, std::move(state), advertising_generation_ << 32);
    // Only contract acceptance resumes the next offer. Spending closes its calculator.
    if (request.kind == AdvertisingActionKind::accept_offer)
        (void)next_advertising->open_offers(request.offer.display_slot);
    desktop_ = std::move(next_desktop);
    preparation_ = std::move(next_preparation);
    advertising_ = std::move(next_advertising);
    ++advertising_generation_;
}

void StartupFlow::reject_advertising_action(std::string reason) {
    if (!advertising_ || !advertising_->pending() || reason.empty())
        throw FormatError("advertising: rejection requires a pending action");
    advertising_->reject_action(std::move(reason));
}

FlowResult StartupFlow::finish_readonly_input(bool changed) {
    if (mailbox_ && mailbox_->close_requested()) {
        if (!pending_mailbox_close_)
            pending_mailbox_close_ = MailboxCloseRequest{mailbox_->state().manager_slot,
                                                         mailbox_->state().revision};
        FlowResult result{true, FlowRequest::mailbox_closed};
        result.mailbox_close = *pending_mailbox_close_;
        return result;
    }
    if ((calendar_ && calendar_->close_requested()) || (league_table_ && league_table_->close_requested()) ||
        (settings_ && settings_->close_requested()) ||
        (advertising_ && advertising_->close_requested())) {
        calendar_.reset(); league_table_.reset(); settings_.reset(); advertising_.reset();
        return {true, FlowRequest::submenu_closed};
    }
    if (changed && mailbox_ && mailbox_->pending()) {
        FlowResult result{true, FlowRequest::mailbox_action};
        result.mailbox_action = *mailbox_->pending();
        return result;
    }
    if (changed && calendar_ && calendar_->pending_reschedule()) {
        FlowResult result{true, FlowRequest::reschedule_action};
        result.reschedule_action = *calendar_->pending_reschedule();
        return result;
    }
    if (changed && settings_ && settings_->pending()) {
        FlowResult result{true, FlowRequest::settings_action};
        result.settings_action = *settings_->pending();
        return result;
    }
    if (changed && advertising_ && advertising_->pending()) {
        FlowResult result{true, FlowRequest::advertising_action};
        result.advertising_action = *advertising_->pending();
        return result;
    }
    return {changed};
}

void StartupFlow::acknowledge_mailbox(MailboxState state, PreparationState preparation) {
    if (!mailbox_ || !mailbox_->pending() || !desktop_ || !engine_attached_ ||
        state.manager_slot != desktop_->state().manager_slot ||
        state.revision != desktop_->state().revision + 1 ||
        mailbox_->pending()->revision != desktop_->state().revision ||
        mailbox_->pending()->manager_slot != state.manager_slot ||
        state.revision != preparation.desktop.revision ||
        preparation.desktop.source != "engine_projection" ||
        preparation.desktop.manager_slot != state.manager_slot ||
        preparation.desktop.runtime_club != desktop_->state().runtime_club ||
        preparation.desktop.day_counter != desktop_->state().day_counter ||
        preparation.desktop.season_index != desktop_->state().season_index ||
        preparation.desktop.season_day != desktop_->state().season_day ||
        preparation.desktop.manager_name != desktop_->state().manager_name ||
        preparation.desktop.portrait != desktop_->state().portrait ||
        preparation.desktop.badge != desktop_->state().badge ||
        preparation.desktop.options != desktop_->state().options)
        throw FormatError("mailbox: mismatched acknowledgement context");
    validate_preparation_context(preparation);
    // Validate replacements first; preserve page/mode and the pending command on failure.
    auto next_preparation = std::make_unique<PreparationState>(std::move(preparation));
    auto screen = std::make_unique<MailboxScreen>(*mailbox_);
    screen->acknowledge(std::move(state));
    desktop_->acknowledge_mailbox_parent(next_preparation->desktop);
    preparation_ = std::move(next_preparation);
    mailbox_ = std::move(screen);
}

void StartupFlow::reject_mailbox(std::string reason) {
    if (mailbox_) mailbox_->reject(std::move(reason));
}

void StartupFlow::acknowledge_settings(SettingsState state, DeskState desk, PreparationState preparation) {
    if (!settings_ || !settings_->pending() || !desktop_ || !engine_attached_ ||
        desk.source != "engine_projection" || state.manager_slot != desktop_->state().manager_slot ||
        state.manager_slot != settings_->pending()->manager_slot ||
        settings_->pending()->revision == std::numeric_limits<uint64_t>::max() ||
        state.revision != settings_->pending()->revision + 1 || desk.revision != state.revision ||
        preparation.desktop.revision != state.revision || preparation.desktop.manager_slot != state.manager_slot ||
        desk_json(preparation.desktop) != desk_json(desk) ||
        desk.manager_teams != desktop_->state().manager_teams || desk.runtime_club != desktop_->state().runtime_club ||
        desk.day_counter != desktop_->state().day_counter || desk.season_index != desktop_->state().season_index ||
        desk.season_day != desktop_->state().season_day || desk.manager_name != desktop_->state().manager_name ||
        desk.portrait != desktop_->state().portrait || desk.badge != desktop_->state().badge ||
        desk.options != desktop_->state().options)
        throw FormatError("settings: mismatched acknowledgement context");
    validate_desk_state(desk);
    validate_preparation_context(preparation);
    auto next_desktop = std::make_unique<DesktopScreen>(data_, desk);
    auto next_preparation = std::make_unique<PreparationState>(std::move(preparation));
    auto next_settings = std::make_unique<SettingsScreen>(*settings_);
    next_settings->acknowledge(std::move(state));
    settings_desk_owner_.revision = next_settings->state().revision;
    desktop_ = std::move(next_desktop);
    preparation_ = std::move(next_preparation);
    settings_ = std::move(next_settings);
}

void StartupFlow::acknowledge_settings_notice_release() {
    if (settings_ && !save_load_) {
        settings_release_barrier_ = false;
        settings_->consume_release();
    }
}

void StartupFlow::reject_settings(std::string reason) {
    if (settings_) settings_->reject(std::move(reason));
}

void StartupFlow::acknowledge_settings_file(SettingsState state, DeskState desk, PreparationState preparation) {
    if (!settings_ || !save_load_ || !save_load_->pending() ||
        save_load_->pending()->file_kind != SaveLoadFileKind::settings || !desktop_ ||
        state.manager_slot != settings_->state().manager_slot || state.revision != settings_->state().revision + 1 ||
        desk.revision != state.revision || preparation.desktop.revision != state.revision ||
        desk_json(preparation.desktop) != desk_json(desk) || desk.manager_slot != state.manager_slot ||
        desk.manager_teams != desktop_->state().manager_teams || desk.runtime_club != desktop_->state().runtime_club)
        throw FormatError("settings file acknowledgement has no pending file operation");
    auto next_settings = std::make_unique<SettingsScreen>(*settings_);
    next_settings->acknowledge_file(std::move(state));
    validate_desk_state(desk);
    validate_preparation_context(preparation);
    desktop_ = std::make_unique<DesktopScreen>(data_, desk);
    preparation_ = std::make_unique<PreparationState>(std::move(preparation));
    settings_desk_owner_.revision = next_settings->state().revision;
    save_load_->acknowledge();
    save_load_.reset();
    settings_ = std::move(next_settings);
}

void StartupFlow::reject_settings_file(std::string reason) {
    if (!settings_ || !save_load_ || !save_load_->pending() ||
        save_load_->pending()->file_kind != SaveLoadFileKind::settings)
        throw FormatError("settings file rejection has no pending file operation");
    save_load_->reject(reason);
    settings_->reject_file(std::move(reason));
}

void StartupFlow::cancel_settings_file() {
    if (settings_) settings_->cancel_file_request();
    if (save_load_) save_load_.reset();
}

void StartupFlow::acknowledge_settings_closed(SettingsState state, DeskState desk, PreparationState preparation) {
    if (!settings_ || !settings_->close_requested() || !desktop_ || !engine_attached_ ||
        state.manager_slot != desktop_->state().manager_slot || state.revision <= desktop_->state().revision ||
        desk.revision != state.revision || preparation.desktop.revision != state.revision ||
        desk_json(preparation.desktop) != desk_json(desk))
        throw FormatError("settings close acknowledgement does not match active owner");
    validate_desk_state(desk);
    validate_preparation_context(preparation);
    desktop_ = std::make_unique<DesktopScreen>(data_, std::move(desk));
    preparation_ = std::make_unique<PreparationState>(std::move(preparation));
    settings_.reset(); settings_owner_sequence_ = 0; settings_desk_owner_ = {};
    settings_release_barrier_ = false; // Main consumes a close release after the screen changed.
}

void StartupFlow::reject_settings_close(std::string reason) {
    if (settings_) settings_->reject_close(std::move(reason));
}

void StartupFlow::acknowledge_preparation(PreparationState state, std::string transfer_notice) {
    if (!engine_attached_ || !preparation_ || !in_preparation_menu() || player_card_ || state.desktop.source != "engine_projection")
        throw FormatError("preparation acknowledgement: requires an attached engine and active menu");
    validate_preparation_context(state);
    const auto& previous = preparation_->desktop;
    if (state.desktop.revision <= previous.revision || state.desktop.manager_slot != previous.manager_slot ||
        state.desktop.runtime_club != previous.runtime_club || state.desktop.day_counter != previous.day_counter ||
        state.desktop.season_index != previous.season_index || state.desktop.season_day != previous.season_day ||
        state.desktop.manager_name != previous.manager_name || state.desktop.portrait != previous.portrait ||
        state.desktop.badge != previous.badge || state.desktop.options != previous.options)
        throw FormatError("preparation acknowledgement: stale/mismatched context or unexpected time advance");
    // Prepare replacements before mutation, including dependent views. Rejected
    // acknowledgements must preserve the pending command and every old projection.
    auto desktop = std::make_unique<DesktopScreen>(data_, state.desktop);
    LineupScreen validate_lineup(data_, state.lineup);
    if (state.training) { TrainingScreen validate_training(data_, *state.training); }
    TacticsScreen validate_tactics(data_, state.tactics);
    std::unique_ptr<LineupScreen> lineup;
    std::unique_ptr<TrainingScreen> training;
    std::unique_ptr<TacticsScreen> tactics;
    std::unique_ptr<PitchScreen> pitch;
    bool next_pitch_entry_pending=pitch_entry_pending_;
    bool pitch_team_return_ack=false;
    if (pitch_) {
        if (!state.pitch) throw FormatError("pitch acknowledgement requires pitch context");
        pitch = std::make_unique<PitchScreen>(*pitch_);
        pitch->acknowledge(state.lineup, *state.pitch); state.lineup = pitch->state();
        if(next_pitch_entry_pending){
            next_pitch_entry_pending=false;
        } else {
            lineup = std::make_unique<LineupScreen>(data_, state.lineup);
        }
        if(pitch->close_requested()&&pitch->return_team_requested()){
            pitch_team_return_ack=true;
            lineup=std::make_unique<LineupScreen>(data_,state.lineup);
            pitch.reset();
        }
    } else if (lineup_) { lineup = std::make_unique<LineupScreen>(*lineup_); lineup->acknowledge(state.lineup); state.lineup = lineup->state(); }
    if (!pitch_team_return_ack && !pitch && lineup && (state.lineup.saved_display_flags & 8) && state.pitch &&
        state.pitch->indoor.has_value())
        pitch = std::make_unique<PitchScreen>(data_, state.lineup, *state.pitch);
    if (training_) { training = std::make_unique<TrainingScreen>(*training_); training->acknowledge(*state.training); }
    std::unique_ptr<TrainingCampScreen> training_camp;
    if (training_camp_) {
        if (!state.training_camp) throw FormatError("training camp acknowledgement requires camp context");
        training_camp = std::make_unique<TrainingCampScreen>(*training_camp_);
        training_camp->acknowledge(*state.training_camp);
    }
    std::unique_ptr<BankScreen> bank;
    if (bank_) {
        if (!state.bank) throw FormatError("bank acknowledgement requires bank context");
        bank = std::make_unique<BankScreen>(*bank_);
        bank->acknowledge(*state.bank);
    }
    std::unique_ptr<StadiumScreen> stadium;
    if (stadium_) {
        if (!state.stadium) throw FormatError("stadium acknowledgement requires stadium context");
        stadium = std::make_unique<StadiumScreen>(*stadium_);
        stadium->acknowledge(*state.stadium);
        if (stadium->close_requested()) stadium.reset();
    }
    std::unique_ptr<TotoScreen> toto;
    if (toto_) {
        if (!state.toto) throw FormatError("toto acknowledgement requires coupon context");
        toto = std::make_unique<TotoScreen>(*toto_);
        toto->acknowledge(*state.toto);
        if (toto->close_requested()) toto.reset();
    }
    std::unique_ptr<TransferMarketScreen> transfer_market;
    if (transfer_market_) {
        if (!state.transfer_market) throw FormatError("transfer market acknowledgement requires market context");
        transfer_market = std::make_unique<TransferMarketScreen>(*transfer_market_);
        transfer_market->acknowledge(*state.transfer_market, std::move(transfer_notice));
    }
    if (tactics_) { tactics = std::make_unique<TacticsScreen>(*tactics_); tactics->acknowledge(state.tactics); }
    auto context = std::make_unique<PreparationState>(std::move(state));
    desktop_ = std::move(desktop); preparation_ = std::move(context);
    lineup_ = std::move(lineup); training_ = std::move(training); training_camp_ = std::move(training_camp); bank_ = std::move(bank); stadium_ = std::move(stadium); toto_ = std::move(toto); transfer_market_ = std::move(transfer_market); tactics_ = std::move(tactics);
    pitch_ = std::move(pitch);
    pitch_entry_pending_=next_pitch_entry_pending;
}

void StartupFlow::acknowledge_unchanged_transfer_market() {
    if (!in_preparation_menu() || !transfer_market_)
        throw FormatError("no transfer market action context");
    transfer_market_->acknowledge_unchanged();
}

void StartupFlow::reject_preparation_action(std::string reason) {
    if (!in_preparation_menu()) throw FormatError("no preparation action context");
    if (player_card_) throw FormatError("player card has no engine action");
    if (pitch_) {
        pitch_->reject_action(std::move(reason));
        if(pitch_entry_pending_){
            pitch_.reset();
            lineup_=std::make_unique<LineupScreen>(data_,preparation_->lineup);
            pitch_entry_pending_=false;
        }
    }
    else if (lineup_) lineup_->reject_action(std::move(reason));
    else if (training_) training_->reject_action(std::move(reason));
    else if (training_camp_) training_camp_->reject_action(std::move(reason));
    else if (bank_) bank_->reject_action(std::move(reason));
    else if (stadium_) stadium_->reject_action(std::move(reason));
    else if (toto_) toto_->reject_action(std::move(reason));
    else if (transfer_market_) transfer_market_->reject_action(std::move(reason));
    else tactics_->reject_action(std::move(reason));
}

} // namespace bmh::ui
