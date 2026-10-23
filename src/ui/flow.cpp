// SPDX-License-Identifier: GPL-3.0-only
#include "ui/flow.hpp"
#include "game/world_layout.hpp"
#include "ui/source_hold.hpp"

#include <algorithm>
#include <sstream>
#include <set>

namespace bmh::ui {
FlowResult StartupFlow::move_action(int x, int y) {
    if (indoor_standings_ || indoor_standings_release_barrier_) return {};
    if(world_cup_statistics_)return {world_cup_statistics_->move(x,y)};
    if(achievements_)return {achievements_->move(x,y)};
    if(family_table_dispatch_pending_)return {};
    if(family_table_range_dialog_) {
        const bool changed=family_table_range_dialog_->move(x,y);
        return {changed};
    }
    if(other_games_)return {other_games_->move(x,y)};
    if(balance_)return {balance_->move(x,y)};
    if(top_scorers_)return {top_scorers_->move(x,y)};
    if(analytics_)return {};
    if(statistics_rankings_)return {statistics_rankings_->move(x,y)};
    if(transfer_history_)return {};
    if(opportunities_)return {opportunities_->move(x,y)};
    if(statistics_menu_)return {};
    if(strengths_)return {strengths_->move(x,y)};
    if(in_family_table())return {league_table_->move(x,y)};
    if (calendar_) return {calendar_->move(x,y)};
    if (friendly_match_) return {friendly_match_->move(x,y)};
    if(league_pairings_)return {league_pairings_->move(x,y)};
    if(stadium_)return {stadium_->pointer_move(x,y)};
    if(environment_)return {};
    if (outgoing_transfer_) return {};
    if (transfer_market_ && !player_card_) {
        const auto before = transfer_market_->state_json();
        const auto request = transfer_market_->move(x, y);
        if (request.kind != TransferMarketRequestKind::none)
            return finish_preparation_input(request, true);
        return {before != transfer_market_->state_json()};
    }
    if (desktop_ && screen_name() == "desktop") return {desktop_->pointer_move(x,y)};
    const bool changed = joining_
        ? manager_ && !join_options_pending_ && !join_choice_pending_ && manager_->move(x, y)
        : contracts_ && !player_card_ ? contracts_->move(x, y)
        : matchday_results_ || in_readonly_view() || friendly_match_ ? false
        : player_card_ ? false : pitch_ ? pitch_->move(x, y)
        : lineup_ ? lineup_->move(x, y) : training_camp_ ? training_camp_->move(x, y)
        : toto_ ? toto_->move(x, y)
        : bank_ || stadium_ ? false
        : !desktop_ && manager_ && !pending_ && !awaiting_context_ && manager_->move(x, y);
    return {changed};
}


namespace {
void validate_confirmed_manager_projection(const std::vector<ManagerChoice>& choices,
                                           const std::vector<InitializedManager>& managers,
                                           const DeskState& desk) {
    validate_desk_state(desk);
    if (choices.size() != size_t(desk.options.managers) || managers.size() != choices.size())
        throw FormatError("career attachment: manager count does not match confirmed setup");
    std::vector<const InitializedManager*> by_slot(choices.size(), nullptr);
    std::set<int> chosen_master_clubs, chosen_portraits;
    for (const auto& choice : choices) {
        if (!chosen_master_clubs.insert(choice.master_club).second ||
            !chosen_portraits.insert(choice.portrait).second)
            throw FormatError("career attachment: confirmed choices contain duplicate club/portrait");
    }
    for (const auto& manager : managers) {
        if (manager.slot < 0 || size_t(manager.slot) >= by_slot.size() || by_slot[size_t(manager.slot)])
            throw FormatError("career attachment: sparse or duplicate manager slot");
        by_slot[size_t(manager.slot)] = &manager;
    }
    for (size_t slot = 0; slot < choices.size(); ++slot) {
        const auto& choice = choices[slot];
        const auto* manager = by_slot[slot];
        const auto& identity = desk.managers[slot];
        if (!manager || choice.slot != int(slot) || manager->master_club != choice.master_club ||
            manager->runtime_club != desk.manager_teams[slot] || identity.name != choice.name ||
            identity.portrait != choice.portrait || identity.league != choice.start_league)
            throw FormatError("career attachment: manager identity does not match confirmed slot mapping");
    }
    const auto& active = choices[size_t(desk.manager_slot)];
    if (desk.manager_name != active.name || desk.portrait != active.portrait ||
        desk.league != active.start_league)
        throw FormatError("career attachment: active manager identity does not match confirmed choice");
}
void validate_confirmed_desk(const std::vector<ManagerChoice>& choices,
                             const std::vector<int>& runtime_teams,
                             const std::vector<DeskManagerIdentity>& previous,
                             bool previous_national_coach_takeover,
                             const std::vector<DeskManagerIdentity>& previous_national_roster,
                             bool allow_turn_handoff, const DeskState& desk) {
    validate_desk_state(desk);
    const bool previous_national = previous.size() == 1 && previous[0].runtime_club == no_team() &&
        (desk.options.world || previous_national_coach_takeover);
    const bool incoming_national = desk.national_team &&
        (desk.options.world || desk.national_coach_takeover);
    const bool national_transition = allow_turn_handoff && (previous_national || incoming_national);
    if (!allow_turn_handoff && previous_national_coach_takeover &&
        previous_national_roster.size() != desk.national_return_roster.size())
        throw FormatError("desk acknowledgement: national return roster changed unexpectedly");
    if (!allow_turn_handoff && previous_national_coach_takeover &&
        !std::equal(previous_national_roster.begin(), previous_national_roster.end(),
                    desk.national_return_roster.begin(), [](const auto& before, const auto& after) {
            return before.club_identity_key == after.club_identity_key && before.name == after.name &&
                   before.portrait == after.portrait;
        }))
        throw FormatError("desk acknowledgement: national return roster changed unexpectedly");
    if (national_transition && incoming_national) {
        if (desk.managers.size() != 1)
            throw FormatError("desk acknowledgement: invalid temporary national roster size");
        const auto& active = desk.managers[0];
        const auto& baseline = previous_national && !previous_national_roster.empty()
            ? previous_national_roster : previous;
        if ((desk.national_coach_takeover && desk.national_return_roster.size() != baseline.size()) ||
            (!desk.national_return_roster.empty() &&
             !std::equal(baseline.begin(), baseline.end(), desk.national_return_roster.begin(),
                         [](const auto& before, const auto& after) {
                return before.club_identity_key == after.club_identity_key && before.name == after.name &&
                       before.portrait == after.portrait;
            })) || std::none_of(baseline.begin(), baseline.end(), [&](const auto& manager) {
                return manager.club_identity_key == active.club_identity_key && manager.name == active.name &&
                       manager.portrait == active.portrait;
            }))
            throw FormatError("desk acknowledgement: national coach is not from the confirmed roster");
    } else if (national_transition && previous_national) {
        const auto& baseline = previous_national_roster.empty() ? previous : previous_national_roster;
        if (desk.managers.size() != baseline.size() ||
            !std::equal(desk.managers.begin(), desk.managers.end(), baseline.begin(), [](const auto& after, const auto& before) {
                return before.club_identity_key == after.club_identity_key && before.name == after.name &&
                       before.portrait == after.portrait;
            }))
            throw FormatError("desk acknowledgement: handback does not restore the confirmed manager roster");
    } else if (previous.size() != desk.managers.size()) {
        throw FormatError("desk acknowledgement: manager identity baseline changed");
    }
    if (!national_transition || (!incoming_national && !previous_national)) {
        for (size_t slot=0; slot<desk.managers.size(); ++slot) {
            const auto& before=previous[slot]; const auto& after=desk.managers[slot];
            if (before.club_identity_key != after.club_identity_key || before.name != after.name ||
                before.portrait != after.portrait)
                throw FormatError("desk acknowledgement: manager identity changed unexpectedly");
        }
    }
    if (!allow_turn_handoff && runtime_teams != desk.manager_teams)
        throw FormatError("desk acknowledgement: manager mapping changed unexpectedly");
    if (choices.empty()) return;
    const size_t choice_count = desk.national_team && desk.national_coach_takeover
        ? desk.national_return_roster.size() : size_t(desk.options.managers);
    if (choices.size() != choice_count || desk.manager_slot < 0 ||
        size_t(desk.manager_slot) >= size_t(desk.options.managers))
        throw FormatError("desk acknowledgement: manager roster/mapping changed unexpectedly");
    if (desk.national_team) {
        if (std::none_of(choices.begin(), choices.end(), [&](const ManagerChoice& choice) {
                return desk.manager_name == choice.name && desk.portrait == choice.portrait;
            }))
            throw FormatError("desk acknowledgement: temporary national coach is not a confirmed manager");
    } else {
        const auto& active = choices[size_t(desk.manager_slot)];
        if (desk.manager_name != active.name || desk.portrait != active.portrait)
            throw FormatError("desk acknowledgement: active slot does not match its confirmed manager");
    }
}
}

void StartupFlow::show_manager_join(std::vector<ManagerChoice> existing, SelectionContext context) {
    if (!desktop_ || !engine_attached_ || joining_ || in_preparation_menu() || in_readonly_view() || matchday_results_ ||
        !desktop_->pending() || desktop_->pending()->kind != DeskRequestKind::open_menu ||
        desktop_->pending()->menu_kind != 29 || desktop_->state().national_team ||
        existing.size() != desktop_->state().managers.size() || context.revision != desktop_->state().revision)
        throw FormatError("join: expected the running career's new-manager request");
    for (size_t slot=0; slot<existing.size(); ++slot) {
        const auto& choice=existing[slot]; const auto& identity=desktop_->state().managers[slot];
        if (choice.slot != int(slot) || choice.name != identity.name || choice.portrait != identity.portrait ||
            choice.start_league != identity.league || choice.selection_club != identity.runtime_club ||
            choice.selection_club < 0 || size_t(choice.selection_club) >= context.club_order.size() ||
            context.club_order[size_t(choice.selection_club)] != choice.master_club)
            throw FormatError("join: existing identity does not match the engine roster");
    }
    auto setup=std::make_unique<SetupScreen>(data_, desktop_->state().options);
    join_origin_=*desktop_->pending(); join_choices_=std::move(existing);
    join_context_revision_=context.revision; join_setup_=std::move(setup);
    manager_.reset(); joining_=true; join_options_pending_=join_choice_pending_=false;
}

void StartupFlow::resume_manager_join(SelectionContext context) {
    if (!joining_ || (!join_options_pending_ && !join_choice_pending_) ||
        context.revision < join_context_revision_ ||
        (join_choice_pending_ && context.revision == join_context_revision_) ||
        join_choices_.size() >= size_t(join_setup_->options().managers))
        throw FormatError("join: stale or incomplete manager-selection context");
    auto next=std::make_unique<ManagerScreen>(data_,int(join_choices_.size()),join_choices_,context);
    manager_=std::move(next); join_context_revision_=context.revision;
    join_options_pending_=join_choice_pending_=false;
}

void StartupFlow::acknowledge_manager_join(CareerInitializationResult result) {
    if (!joining_ || (!join_options_pending_ && !join_choice_pending_) || !desktop_ ||
        !desktop_->pending() || *desktop_->pending() != join_origin_ ||
        result.desktop.source != "engine_projection" || result.desktop.revision != join_origin_.revision+1 ||
        result.desktop.options != join_setup_->options() ||
        result.desktop.manager_slot != desktop_->state().manager_slot ||
        result.desktop.day_counter != desktop_->state().day_counter ||
        desk_json(result.desktop) != desk_json(result.preparation.desktop))
        throw FormatError("join: acknowledgement does not match the pending selection");
    validate_confirmed_manager_projection(join_choices_,result.managers,result.desktop);
    for (size_t slot=0; slot<desktop_->state().managers.size(); ++slot) {
        const auto& before=desktop_->state().managers[slot]; const auto& after=result.desktop.managers[slot];
        if (before.club_identity_key != after.club_identity_key || before.name != after.name || before.portrait != after.portrait)
            throw FormatError("join: existing manager identity changed");
    }
    // All renderers must accept the new projection before exposing it to the player.
    StartupFlow candidate(data_,save_directory_);
    candidate.desktop_=std::make_unique<DesktopScreen>(data_,result.desktop);
    candidate.engine_attached_=true; candidate.attach_preparation_context(std::move(result.preparation));
    auto next_choices=join_choices_;
    auto next_teams=result.desktop.manager_teams;
    desktop_=std::move(candidate.desktop_); preparation_=std::move(candidate.preparation_);
    confirmed_=std::move(next_choices); confirmed_runtime_teams_=std::move(next_teams);
    manager_.reset(); join_setup_.reset(); join_choices_.clear();
    joining_=join_options_pending_=join_choice_pending_=false;
}

void StartupFlow::reject_manager_join(std::string reason) {
    if (!joining_) return;
    if (reason.empty()) desktop_->resolve_local_menu();
    else desktop_->reject_action(std::move(reason));
    manager_.reset(); join_setup_.reset(); join_choices_.clear();
    joining_=join_options_pending_=join_choice_pending_=false;
}

std::optional<Image> StartupFlow::pitch_formation_save_feedback(bool restored)const{
    return pitch_?pitch_->formation_save_feedback(restored):std::nullopt;
}

FlowResult StartupFlow::pitch_held_left(int x,int y,uint64_t source_ticks){
    if(!pointer_source_hold_ready(source_ticks)||!pitch_||player_card_||
       pitch_release_barrier_||pitch_hold_release_barrier_||
       x<240||x>=320||y<112||y>=208)return {};
    const auto before=pitch_->state_json();const auto request=pitch_->save_formation(x,y);
    PreparationRequest action;
    if(request.kind!=PitchRequestKind::none){
        action=request;
        // PitchScreen redraws through SDLFlushEvents after saving: the old held input
        // cannot write another template, including on its eventual release.
        pitch_hold_release_barrier_=true;
    }
    return finish_preparation_input(std::move(action),before!=pitch_->state_json());
}

FlowResult StartupFlow::hold(int x,int y,int milliseconds){
    if(milliseconds<0||milliseconds>60000)throw FormatError("hold duration must be 0..60000 ms");
    if(pitch_hold_release_barrier_){pitch_hold_release_barrier_=false;return {};}
    if(pitch_release_barrier_){pitch_release_barrier_=false;return {};}
    if(balance_release_barrier_){balance_release_barrier_=false;return {};}
    if(other_games_)return finish_other_games_input(other_games_->release(x,y));
    if(balance_)return finish_balance_input(balance_->release(x,y));
    if(top_scorers_)return finish_top_scorers_input(top_scorers_->release(x,y));
    if(opportunities_)return finish_opportunities_input(opportunities_->release(x,y));
    if(strengths_)return finish_strengths_input(strengths_->release(x,y));
    if(milliseconds<2000||!pitch_||player_card_||x<240||x>=320||y<112||y>=208)return click(x,y);
    const auto before=state_json();const auto request=pitch_->save_formation(x,y);
    PreparationRequest action;
    if(request.kind!=PitchRequestKind::none)action=request;
    return finish_preparation_input(std::move(action),before!=state_json());
}

FlowResult StartupFlow::click(int x, int y, bool right, bool double_click) {
    if(pitch_hold_release_barrier_&&!right){pitch_hold_release_barrier_=false;return {};}
    if(friendly_question_release_barrier_&&!right) {
        friendly_question_release_barrier_=false;return {};
    }
    if (indoor_standings_release_barrier_) {
        indoor_standings_release_barrier_ = false;
        return {};
    }
    if (indoor_standings_) return {};
    if(desk_records_release_barrier_){desk_records_release_barrier_=false;return {};}
    if(desk_records_)return {desk_records_->pointer_up(x,y,right)};
    if(family_table_dispatch_pending_)return {};
    if (reschedule_release_barrier_) { reschedule_release_barrier_ = false; return {}; }
    if(achievements_release_barrier_){achievements_release_barrier_=false;return {};}
    if(achievements_)return {};
    if (settings_readme_release_barrier_) {
        settings_readme_release_barrier_ = false;
        return {};
    }
    if (settings_readme_) return {}; // Mouse UP is consumed by the viewer's wait.
    if (settings_release_barrier_) {
        settings_release_barrier_ = false;
        if (settings_) settings_->consume_release();
        return {};
    }
    if(national_friendly_release_barrier_){national_friendly_release_barrier_=false;return {};}
    if(pitch_release_barrier_){pitch_release_barrier_=false;return {};}
    if(family_table_range_dialog_) {
        const bool changed=family_table_range_dialog_->pointer_up(x,y,right);
        // A consumed release ends drag ownership; it does not start a new
        // DecodeTaxAnimationScript2 wait or restart one already anchored at its pressed frame.
        return {changed};
    }
    if(family_table_release_barrier_){family_table_release_barrier_=false;return {};}
    if(balance_release_barrier_){balance_release_barrier_=false;return {};}
    if(other_games_)return right?FlowResult{}:finish_other_games_input(other_games_->release(x,y));
    if(world_cup_statistics_&&national_friendly_picker_)
        return statistics_menu_&&!right ? finish_statistics_menu_input(statistics_menu_->release(x,y)) : FlowResult{};
    if(alltime_table_) {
        const bool changed=alltime_table_->release(x,y);
        if(const auto& request=alltime_table_->pending();request) {
            statistics_page_action_=StatisticsPageAction{request->owner,3,
                request->kind==AllTimeRequestKind::close?StatisticsPageActionKind::close:StatisticsPageActionKind::alltime,
                request->nation,request->relative};
            return finish_statistics_page_input(true);
        }
        if(changed)return {true};
        return statistics_menu_ && !right ? finish_statistics_menu_input(statistics_menu_->release(x,y)) : FlowResult{};
    }
    if(world_cup_statistics_||finance_totals_||series_records_)
        return statistics_menu_ && !right ? finish_statistics_menu_input(statistics_menu_->release(x,y)) : FlowResult{};
    if(balance_)return right?FlowResult{}:finish_balance_input(balance_->release(x,y));
    if(top_scorers_)return right?FlowResult{}:finish_top_scorers_input(top_scorers_->release(x,y));
    if(analytics_) {
        if(analytics_->pending())return {};
        if(statistics_menu_)return right?FlowResult{}:finish_statistics_menu_input(statistics_menu_->release(x,y));
        return {};
    }
    if(statistics_rankings_) {
        if(statistics_rankings_->release(x,y))return finish_statistics_rankings_input(true);
        if(statistics_menu_)return right?FlowResult{}:finish_statistics_menu_input(statistics_menu_->release(x,y));
        return {};
    }
    if(minute_statistics_) {
        if(statistics_menu_)return right?FlowResult{}:finish_statistics_menu_input(statistics_menu_->release(x,y));
        return {};
    }
    if(transfer_history_) {
        if(statistics_menu_)return right?FlowResult{}:finish_statistics_menu_input(statistics_menu_->release(x,y));
        return {};
    }
    if(opportunities_) {
        if(opportunities_->release(x,y))return finish_opportunities_input(true);
        if(statistics_menu_)return right?FlowResult{}:finish_statistics_menu_input(statistics_menu_->release(x,y));
        return {};
    }
    if(statistics_menu_)return right?FlowResult{}:finish_statistics_menu_input(statistics_menu_->release(x,y));
    if(strengths_)return right?FlowResult{}:finish_strengths_input(strengths_->release(x,y));
    if (stadium_) {
        const auto request = stadium_->release(x,y,right);
        PreparationRequest action;
        if (request.kind != StadiumRequestKind::none) action = request;
        // A release also restores the pressed button or changes the local view.
        return finish_preparation_input(std::move(action), true);
    }
    if (environment_) {
        (void)environment_->click(x,y,right);
        if (environment_->close_requested()) { environment_.reset(); return {true,FlowRequest::submenu_closed}; }
        if (environment_->pending()) {
            FlowResult result{true,FlowRequest::environment_action};
            result.environment_action=*environment_->pending(); return result;
        }
        return {};
    }
    // Familyclose is Source event8 on DOWN; an orphan UP has no action.
    if(league_pairings_)return right?FlowResult{}:finish_league_pairings_input(league_pairings_->release(x,y));
    if (joining_) {
        if (join_options_pending_ || join_choice_pending_) return {};
        if (!manager_) {
            auto input = join_setup_->click(x,y,right);
            if (input.request == SetupRequest::new_game) {
                join_options_pending_ = true;
                FlowResult result{true,FlowRequest::join_options};
                result.desk_action = join_origin_; result.join_options = join_setup_->options();
                return result;
            }
            return {input.changed};
        }
        const bool changed = manager_->click(x,y,right);
        if (manager_->view() != ManagerView::accepted) return {changed};
        join_choices_.push_back(manager_->choice()); join_choice_pending_ = true;
        FlowResult result{true,FlowRequest::join_manager};
        result.desk_action = join_origin_; result.join_manager = join_choices_.back();
        return result;
    }
    if (contracts_ && !player_card_) {
        const bool changed = contracts_->click(x, y, right);
        if (contracts_->close_requested()) { contracts_.reset(); return {true, FlowRequest::contracts_closed}; }
        if (contracts_->pending()) {
            FlowResult result{true, FlowRequest::contract_action};
            result.contract_action = *contracts_->pending();
            return result;
        }
        const int info_row=contracts_->take_player_info_request();
        if(info_row>=0 && preparation_) for(auto card:preparation_->player_cards) {
            if(card.row==info_row && card.unsupported_record.empty()) {
                player_card_=std::make_unique<PlayerCardScreen>(data_,std::move(card),contracts_->render());
                return {true,FlowRequest::submenu_opened};
            }
        }
        return {changed};
    }
    if (friendly_match_) {
        const auto before = state_json();
        auto request = friendly_match_->click(x, y, right);
        if (friendly_match_->close_requested()) {
            friendly_match_.reset(); pending_friendly_action_.reset();
            return {true, FlowRequest::submenu_closed};
        }
        if (request) {
            pending_friendly_action_ = *request;
            next_friendly_request_id_ = std::max(next_friendly_request_id_, request->request_id + 1);
            FlowResult result{true, FlowRequest::friendly_action};
            result.friendly_action = *request;
            return result;
        }
        return {before != state_json()};
    }
    if (calendar_) {
        const bool changed = calendar_->click(x,y,right);
        if (calendar_->pending_reschedule()) {
            FlowResult result{true, FlowRequest::reschedule_action};
            result.reschedule_action = *calendar_->pending_reschedule();
            return result;
        }
        return finish_readonly_input(changed);
    }
    if (league_table_) {
        const bool changed = right ? league_table_->key("ESC") : league_table_->click(x, y);
        if(const auto* family=pending_league_family_action()) {
            FlowResult result{changed,FlowRequest::league_family_action};
            result.league_family_action=*family;return result;
        }
        return finish_readonly_input(changed);
    }
    if (mailbox_) return finish_readonly_input(mailbox_->click(x, y, right));
    if (settings_ && !save_load_) return finish_settings_input(settings_->click(x, y, right));
    if (advertising_) return finish_readonly_input(right ? advertising_->key("ESC") : advertising_->click(x, y));
    if (matchday_results_) {
        const bool changed = matchday_results_->click(x, y);
        if (matchday_results_->close_requested()) {
            matchday_results_.reset();
            return {true, FlowRequest::results_closed};
        }
        return {changed};
    }
    if (save_load_) {
        const auto before = state_json();
        const bool changed = save_load_->click(x, y, right);
        return finish_file_input(changed || before != state_json());
    }
    if (in_preparation_menu()) {
        const auto before = state_json();
        const bool statistics_before = player_card_ && player_card_->in_statistics();
        PreparationRequest action;
        if (player_card_) player_card_->click(x, y, right);
        else if (pitch_) { auto r = pitch_->click(x, y, right); if (r.kind != PitchRequestKind::none) action = r; }
        else if (lineup_) { auto r = lineup_->click(x, y, right); if (r.kind != LineupRequestKind::none) action = r; }
        else if (training_) { auto r = training_->click(x, y, right); if (r.kind != TrainingRequestKind::none) action = r; }
        else if (training_camp_) { auto r = training_camp_->click(x, y, right); if (r.kind != TrainingCampRequestKind::none) action = r; }
        else if (bank_) { auto r = bank_->click(x, y, right); if (r.kind != BankRequestKind::none) action = r; }
        else if (stadium_) { auto r = stadium_->click(x, y, right); if (r.kind != StadiumRequestKind::none) action = r; }
        else if (environment_) { auto r=environment_->click(x,y,right); if(environment_->close_requested()) { environment_.reset(); return {true,FlowRequest::submenu_closed}; } if(r.kind!=EnvironmentRequestKind::none) { FlowResult result{true,FlowRequest::environment_action}; result.environment_action=r; return result; } }
        else if (toto_) { auto r = toto_->click(x, y, right); if (r.kind != TotoRequestKind::none) action = r; }
        else if (outgoing_transfer_) { auto r = outgoing_transfer_->click(x, y, right, double_click); if (r.kind != OutgoingTransferRequestKind::none) action = r; }
        else if (transfer_market_) { auto r = transfer_market_->click(x, y, right); if (r.kind != TransferMarketRequestKind::none) action = r; }
        else if (tactics_) { auto r = tactics_->click(x, y, right); if (r.kind != TacticsRequestKind::none) action = r; }
        auto result = finish_preparation_input(std::move(action), before != state_json());
        if (player_card_ && statistics_before != player_card_->in_statistics())
            result.request = player_card_->in_statistics() ? FlowRequest::submenu_opened : FlowRequest::submenu_closed;
        return result;
    }
    if (desktop_) {
        const auto before = desktop_->state_json();
        auto result = route_desk(desktop_->pointer_up(x, y, right));
        result.changed |= before != desktop_->state_json();
        return result;
    }
    if (pending_ || awaiting_context_) return {}; // One request, then wait for an engine adapter.
    if (!manager_) {
        auto r = setup_.click(x, y, right);
        if (r.request == SetupRequest::load_game) {
            save_load_ = std::make_unique<SaveLoadScreen>(data_, save_directory_, SaveLoadMode::load, -1, 0, setup_.render());
            return {true, FlowRequest::submenu_opened};
        }
        if (r.request == SetupRequest::new_game) {
            manager_ = std::make_unique<ManagerScreen>(data_, 0);
            return {true, FlowRequest::none};
        }
        return {r.changed, FlowRequest::none};
    }
    bool changed = manager_->click(x, y, right);
    if (manager_->view() != ManagerView::accepted) return {changed, FlowRequest::none};
    confirmed_.push_back(manager_->choice());
    if (int(confirmed_.size()) == setup_.options().managers) {
        pending_ = std::make_unique<NewCareerRequest>(NewCareerRequest{setup_.options(), confirmed_});
        return {true, FlowRequest::new_career};
    }
    awaiting_context_ = true;
    return {true, FlowRequest::manager_confirmed};
}

FlowResult StartupFlow::send_key(std::string_view value) {
    if (indoor_standings_ || indoor_standings_release_barrier_) return {};
    if(achievements_)return {}; // AchievementsFromGame waits for its Source mouse event.
    if (settings_readme_) return {}; // SDLFlushEvents2 waits for mouse input, not ESC.
    if(desk_records_)return {desk_records_->key_input(value)};
    if(family_table_dispatch_pending_)return {};
    if(family_table_range_dialog_) {
        if(!family_table_range_dialog_->source_input_ready())return {};
        const bool changed=family_table_range_dialog_->key(value);
        if(changed)family_table_reset_clock_after_present_=true;
        return {changed};
    }
    if(balance_)return finish_balance_input(balance_->key(value));
    if(other_games_)return {}; // CupGroupOverviewEffects has no keyboard control mapping.
    if(top_scorers_)return {}; // Copy3 has no keyboard control mapping.
    if(opportunities_)return finish_opportunities_input(opportunities_->key(value));
    if(statistics_menu_)return {};
    if(strengths_)return {}; // Original has no keyboard control mapping.
    if (calendar_) return finish_readonly_input(calendar_->key(value));
    if(league_pairings_)return {}; // No invented ESC or keyboard arrows.
    if (joining_) {
        if (manager_ && manager_->view() == ManagerView::name_edit) return {manager_->key(value)};
        if (value == "ESC") {
            FlowResult result{true,FlowRequest::join_cancel}; result.desk_action = join_origin_; return result;
        }
        return {manager_ && !join_options_pending_ && !join_choice_pending_ && manager_->key(value)};
    }
    if (contracts_ && !player_card_) {
        const bool changed = contracts_->key(value);
        if (contracts_->close_requested()) { contracts_.reset(); return {true, FlowRequest::contracts_closed}; }
        if (contracts_->pending()) {
            FlowResult result{true, FlowRequest::contract_action};
            result.contract_action = *contracts_->pending();
            return result;
        }
        return {changed};
    }
    if (friendly_match_) {
        const auto before = state_json();
        auto request = friendly_match_->key(value);
        if (friendly_match_->close_requested()) {
            friendly_match_.reset(); pending_friendly_action_.reset();
            return {true, FlowRequest::submenu_closed};
        }
        if (request) {
            pending_friendly_action_ = *request;
            next_friendly_request_id_ = std::max(next_friendly_request_id_, request->request_id + 1);
            FlowResult result{true, FlowRequest::friendly_action};
            result.friendly_action = *request;
            return result;
        }
        return {before != state_json()};
    }
    if (league_table_) return finish_readonly_input(league_table_->key(value));
    if (mailbox_) return finish_readonly_input(mailbox_->key(value));
    if (settings_ && !save_load_) return finish_settings_input(settings_->key(value));
    if (advertising_) return finish_readonly_input(advertising_->key(value));
    if (environment_) { environment_->key(value); if(environment_->close_requested()) { environment_.reset(); return {true,FlowRequest::submenu_closed}; } return {}; }
    if (matchday_results_) {
        const bool changed = matchday_results_->key(value);
        if (matchday_results_->close_requested()) {
            matchday_results_.reset();
            return {true, FlowRequest::results_closed};
        }
        return {changed};
    }
    if (save_load_) {
        const auto before = state_json();
        const bool changed = save_load_->key(value);
        return finish_file_input(changed || before != state_json());
    }
    if (in_preparation_menu()) {
        const auto before = state_json();
        const bool statistics_before = player_card_ && player_card_->in_statistics();
        PreparationRequest action;
        if (player_card_) player_card_->key(value);
        else if (pitch_) { auto r = pitch_->key(value); if (r.kind != PitchRequestKind::none) action = r; }
        else if (lineup_) { auto r = lineup_->key(value); if (r.kind != LineupRequestKind::none) action = r; }
        else if (training_) { auto r = training_->key(value); if (r.kind != TrainingRequestKind::none) action = r; }
        else if (training_camp_) { auto r = training_camp_->key(value); if (r.kind != TrainingCampRequestKind::none) action = r; }
        else if (bank_) { auto r = bank_->key(value); if (r.kind != BankRequestKind::none) action = r; }
        else if (stadium_) { auto r = stadium_->key(value); if (r.kind != StadiumRequestKind::none) action = r; }
        else if (toto_) { auto r = toto_->key(value); if (r.kind != TotoRequestKind::none) action = r; }
        else if (outgoing_transfer_) { auto r = outgoing_transfer_->key(value); if (r.kind != OutgoingTransferRequestKind::none) action = r; }
        else if (transfer_market_) { auto r = transfer_market_->key(value); if (r.kind != TransferMarketRequestKind::none) action = r; }
        else if (tactics_) { auto r = tactics_->key(value); if (r.kind != TacticsRequestKind::none) action = r; }
        auto result = finish_preparation_input(std::move(action), before != state_json());
        if (player_card_ && statistics_before != player_card_->in_statistics())
            result.request = player_card_->in_statistics() ? FlowRequest::submenu_opened : FlowRequest::submenu_closed;
        return result;
    }
    if (desktop_) {
        return route_desk(desktop_->key(value));
    }
    return {manager_ && !pending_ && !awaiting_context_ && manager_->key(value)};
}

bool StartupFlow::text(std::string_view value) {
    if(desk_records_)return desk_records_->text_input(value);
    if(family_table_dispatch_pending_)return false;
    if(family_table_range_dialog_) {
        if(!family_table_range_dialog_->source_input_ready())return false;
        const bool changed=family_table_range_dialog_->text(value);
        if(changed)family_table_reset_clock_after_present_=true;
        return changed;
    }
    if(balance_)return balance_->text(value);
    if (joining_) return manager_ && !join_options_pending_ && !join_choice_pending_ && manager_->text(value);
    if (contracts_ && !player_card_) return contracts_->text(value);
    if (advertising_) return advertising_->text(value);
    // A retained Settings parent must not block its active file dialog.
    if (save_load_) return save_load_->text(value);
    if (matchday_results_ || in_readonly_view() || friendly_match_) return false;
    return bank_ ? bank_->text(value) : toto_ ? toto_->text(value) : outgoing_transfer_ ? outgoing_transfer_->text(value) : transfer_market_ ? transfer_market_->text(value) :
        stadium_ ? stadium_->text(value) : !desktop_ && manager_ && !pending_ && !awaiting_context_ && manager_->text(value);
}

FlowResult StartupFlow::finish_file_input(bool changed) {
    if (save_load_->close_requested()) {
        const bool settings_file = save_load_->file_kind() == SaveLoadFileKind::settings;
        save_load_.reset();
        if (settings_file && settings_) settings_->cancel_file_request();
        return {true, FlowRequest::submenu_closed};
    }
    if (save_load_->pending()) {
        FlowResult result{true, save_load_->pending()->file_kind == SaveLoadFileKind::settings ?
            FlowRequest::settings_file_action : FlowRequest::file_action};
        result.file_action = *save_load_->pending();
        result.settings_owner_sequence = settings_owner_sequence_;
        return result;
    }
    return {changed};
}

FlowResult StartupFlow::finish_settings_input(bool changed) {
    if (!settings_) return {changed};
    if (settings_->readme_requested()) {
        settings_->consume_readme_request();
        settings_readme_ = std::make_unique<SettingsReadmeScreen>(data_, settings_->state().decorated_background);
        return {true};
    }
    if (settings_->close_requested()) {
        FlowResult result{true, FlowRequest::settings_close};
        result.settings_owner_sequence = settings_owner_sequence_;
        return result;
    }
    if (settings_->pending_file()) {
        if (!save_load_) {
            const auto request = *settings_->pending_file();
            save_load_ = std::make_unique<SaveLoadScreen>(data_, save_directory_,
                request.action == SaveLoadAction::load ? SaveLoadMode::load : SaveLoadMode::save,
                request.manager_slot, request.revision, settings_->render(), true,
                SaveLoadFileKind::settings, request.origin_item_id);
            return {true, FlowRequest::submenu_opened};
        }
    }
    if (settings_->pending()) {
        FlowResult result{true, FlowRequest::settings_action};
        result.settings_action = *settings_->pending();
        result.settings_owner_sequence = settings_owner_sequence_;
        return result;
    }
    return {changed};
}

void StartupFlow::load_reference(const std::filesystem::path& save, std::optional<bool> indoor) {
    if (manager_ || desktop_) throw FormatError("reference load requires a fresh startup flow");
    auto state = std::make_unique<PreparationState>(load_preparation_reference(save, load_le(asset_path(data_, "BMH.exe"))));
    if (state->pitch && indoor) state->pitch->indoor = indoor;
    auto desktop = std::make_unique<DesktopScreen>(data_, state->desktop);
    desktop_ = std::move(desktop); preparation_ = std::move(state);
}

void StartupFlow::attach_initialized_career(CareerInitializationResult result) {
    if (!pending_ || desktop_) throw FormatError("career attachment requires a pending new-career request");
    validate_desk_profile(pending_->options);
    const auto& state = result.desktop;
    if (state.source != "engine_projection" || state.options != pending_->options)
        throw FormatError("career attachment: engine result does not match confirmed setup");
    validate_confirmed_manager_projection(pending_->managers, result.managers, state);
    const auto runtime_teams = state.manager_teams;
    auto desktop = std::make_unique<DesktopScreen>(data_, std::move(result.desktop));
    desktop_ = std::move(desktop); pending_.reset(); engine_attached_ = true;
    confirmed_runtime_teams_ = runtime_teams;
}

void StartupFlow::acknowledge_desk(DeskState state) {
    if (!desktop_ || !engine_attached_ || in_preparation_menu() || in_readonly_view() || state.source != "engine_projection")
        throw FormatError("desk acknowledgement requires an attached engine projection");
    const bool turn_handoff = desktop_->pending() && desktop_->pending()->kind == DeskRequestKind::advance_turn;
    validate_confirmed_desk(confirmed_, confirmed_runtime_teams_, desktop_->state().managers,
                            desktop_->state().national_coach_takeover, desktop_->state().national_return_roster,
                            turn_handoff, state);
    desktop_->acknowledge(std::move(state));
    confirmed_runtime_teams_ = desktop_->state().manager_teams;
    preparation_.reset(); // Require a fresh same-revision context after any desk command.
}

void StartupFlow::show_notified_mailbox(MailboxState state) {
    if (!desktop_ || !engine_attached_ || desktop_->pending() || in_preparation_menu() ||
        in_readonly_view() || matchday_results_ || !desktop_->state().mailbox_notification_pending ||
        desktop_->state().national_team || state.manager_slot != desktop_->state().manager_slot ||
        state.revision != desktop_->state().revision)
        throw FormatError("mailbox: automatic notification requires the idle active club desk");
    mailbox_ = std::make_unique<MailboxScreen>(data_, std::move(state),
        original_to_utf8(desktop_->state().manager_name), desktop_->render());
}

void StartupFlow::acknowledge_mailbox_close(const MailboxCloseRequest& request, DeskState desk,
                                             PreparationState preparation) {
    if (!mailbox_ || !pending_mailbox_close_ || *pending_mailbox_close_ != request || !desktop_ ||
        !engine_attached_ || request.manager_slot != desktop_->state().manager_slot ||
        request.manager_slot != mailbox_->state().manager_slot || request.revision != desktop_->state().revision ||
        request.revision != mailbox_->state().revision || desk.source != "engine_projection" ||
        desk.revision != request.revision || preparation.desktop.revision != request.revision ||
        desk.manager_slot != request.manager_slot || desk.runtime_club != desktop_->state().runtime_club ||
        desk.day_counter != desktop_->state().day_counter || desk.mailbox_notification_pending)
        throw FormatError("mailbox: mismatched notification close acknowledgement");
    auto expected = desktop_->state();
    expected.mailbox_notification_pending = false;
    if (desk_json(expected) != desk_json(desk) || desk_json(desk) != desk_json(preparation.desktop))
        throw FormatError("mailbox: close acknowledgement changed unrelated desk state");
    validate_preparation_context(preparation);
    auto next_preparation = std::make_unique<PreparationState>(std::move(preparation));
    desktop_->acknowledge_mailbox_parent(std::move(desk));
    preparation_ = std::move(next_preparation);
    mailbox_.reset();
    pending_mailbox_close_.reset();
}

void StartupFlow::reject_mailbox_close(std::string reason) {
    if (!pending_mailbox_close_ || !mailbox_) throw FormatError("mailbox: no pending close to reject");
    pending_mailbox_close_.reset();
    mailbox_->cancel_close_request();
    mailbox_->reject(std::move(reason));
}

void StartupFlow::acknowledge_desk_notice(const DeskRequest& owner) {
    if(!desktop_||!desktop_->pending()||*desktop_->pending()!=owner||
       owner.kind!=DeskRequestKind::open_menu||owner.menu_kind!=4)
        throw FormatError("desk notice: mismatched achievements owner");
    desktop_->resolve_local_menu();
}

void StartupFlow::reject_desk_action(std::string reason) {
    if (!desktop_) throw FormatError("no desktop action context");
    desktop_->reject_action(std::move(reason));
}

bool StartupFlow::acknowledge_friendly_quote(FriendlyQuoteProjection quote) {
    if (!friendly_match_ || !pending_friendly_action_ ||
        pending_friendly_action_->kind != FriendlyRequestKind::quote ||
        !friendly_match_->acknowledge_quote(std::move(quote))) return false;
    pending_friendly_action_.reset();
    return true;
}

bool StartupFlow::acknowledge_friendly_booking(FriendlyBookingAck ack) {
    if (!friendly_match_ || !pending_friendly_action_ ||
        pending_friendly_action_->kind != FriendlyRequestKind::book || ack.accepted ||
        !friendly_match_->acknowledge_booking(ack)) return false;
    pending_friendly_action_.reset();
    return true;
}

bool StartupFlow::acknowledge_friendly_booking(FriendlyBookingAck ack, DeskState desk,
                                               PreparationState preparation) {
    if (!friendly_match_ || !pending_friendly_action_ ||
        pending_friendly_action_->kind != FriendlyRequestKind::book || !ack.accepted || !desktop_ || !engine_attached_)
        return false;
    if (desk.source != "engine_projection" || desk.revision != desktop_->state().revision + 1 ||
        desk.revision != ack.revision || desk.manager_slot != desktop_->state().manager_slot ||
        desk.manager_teams != desktop_->state().manager_teams ||
        preparation.desktop.revision != desk.revision || preparation.desktop.manager_slot != desk.manager_slot ||
        preparation.desktop.manager_teams != desk.manager_teams ||
        preparation.desktop.runtime_club != desk.runtime_club || preparation.desktop.source != desk.source)
        return false;
    validate_confirmed_desk(confirmed_, confirmed_runtime_teams_, desktop_->state().managers,
                            desktop_->state().national_coach_takeover, desktop_->state().national_return_roster,
                            false, desk);
    validate_desk_state(desk);
    validate_preparation_context(preparation);
    auto next_desktop = std::make_unique<DesktopScreen>(data_, desk);
    auto next_preparation = std::make_unique<PreparationState>(std::move(preparation));
    if (!friendly_match_->acknowledge_booking(ack)) return false;
    desktop_ = std::move(next_desktop);
    preparation_ = std::move(next_preparation);
    confirmed_runtime_teams_ = desktop_->state().manager_teams;
    pending_friendly_action_.reset();
    // Source menu6 exits after Max succeeds; it has no success dialog.
    friendly_match_.reset();
    return true;
}

void StartupFlow::reject_friendly_action(std::string reason) {
    if (!friendly_match_ || !pending_friendly_action_ || reason.empty())
        throw FormatError("friendly match rejection requires a pending request");
    friendly_match_->reject(std::move(reason));
    pending_friendly_action_.reset();
}

void StartupFlow::acknowledge_saved_game(DeskState desk, PreparationState preparation) {
    if (!save_load_ || !save_load_->pending() || save_load_->pending()->file_kind != SaveLoadFileKind::game ||
        save_load_->pending()->action != SaveLoadAction::save ||
        !desktop_ || !engine_attached_ || desk.source != "engine_projection" ||
        desk.manager_slot != save_load_->pending()->manager_slot || desk.revision <= save_load_->pending()->revision ||
        preparation.desktop.revision != desk.revision || preparation.desktop.manager_slot != desk.manager_slot)
        throw FormatError("save/load: saved-game acknowledgement does not match the pending save");
    validate_confirmed_desk(confirmed_, confirmed_runtime_teams_, desktop_->state().managers,
                            desktop_->state().national_coach_takeover, desktop_->state().national_return_roster,
                            false, desk);
    validate_desk_state(preparation.desktop);
    if (preparation.desktop.manager_teams != desk.manager_teams || preparation.desktop.runtime_club != desk.runtime_club)
        throw FormatError("save/load: save projection manager mapping mismatch");
    if (desk.managers.size() != desktop_->state().managers.size())
        throw FormatError("save/load: manager roster changed while saving");
    for (size_t i=0;i<desk.managers.size();++i)
        if (desk.managers[i].name != desktop_->state().managers[i].name ||
            desk.managers[i].portrait != desktop_->state().managers[i].portrait)
            throw FormatError("save/load: manager identity changed while saving");
    auto next_desktop = std::make_unique<DesktopScreen>(data_, desk);
    auto next_preparation = std::make_unique<PreparationState>(std::move(preparation));
    save_load_->acknowledge(); save_load_.reset();
    desktop_ = std::move(next_desktop); preparation_ = std::move(next_preparation);
}

void StartupFlow::reject_file_action(std::string reason) {
    if (!save_load_) throw FormatError("save/load: no file dialog");
    save_load_->reject(std::move(reason));
}

void StartupFlow::acknowledge_menu_layout(DeskState desk, PreparationState preparation) {
    if (!save_load_ || !save_load_->pending() ||
        save_load_->pending()->file_kind != SaveLoadFileKind::menu_layout ||
        !desktop_ || !engine_attached_ ||
        desk.source != "engine_projection" || preparation.desktop.source != desk.source ||
        desk.manager_slot != save_load_->pending()->manager_slot ||
        desk.revision != save_load_->pending()->revision + 1 ||
        preparation.desktop.revision != desk.revision ||
        preparation.desktop.manager_slot != desk.manager_slot)
        throw FormatError("save/load: menu-layout acknowledgement does not match the pending operation");
    const DeskState& before = desktop_->state();
    if (save_load_->pending()->origin_item_id < 0 || save_load_->pending()->origin_item_id >= 54 ||
        !before.menu_layout_shortcut ||
        before.menu_layout_shortcut->id != save_load_->pending()->origin_item_id)
        throw FormatError("save/load: menu-layout origin no longer matches the active desktop record");
    validate_confirmed_desk(confirmed_, confirmed_runtime_teams_, before.managers,
                            before.national_coach_takeover, before.national_return_roster, false, desk);
    validate_desk_state(desk);
    validate_desk_state(preparation.desktop);
    if (desk.options != before.options || desk.runtime_club != before.runtime_club ||
        desk.manager_teams != before.manager_teams || desk.managers != before.managers ||
        desk.manager_slot != before.manager_slot || desk.league != before.league ||
        desk.portrait != before.portrait || desk.badge != before.badge ||
        desk.day_counter != before.day_counter || desk.season_index != before.season_index ||
        desk.season_day != before.season_day || desk.date.year != before.date.year ||
        desk.date.month != before.date.month || desk.date.day != before.date.day ||
        desk.date.weekday != before.date.weekday ||
        desk.national_team != before.national_team ||
        desk.national_coach_takeover != before.national_coach_takeover ||
        desk.national_return_roster != before.national_return_roster ||
        preparation.desktop.manager_teams != desk.manager_teams ||
        preparation.desktop.runtime_club != desk.runtime_club ||
        preparation.desktop.managers != desk.managers || preparation.desktop.options != desk.options)
        throw FormatError("save/load: menu-layout operation changed career ownership or calendar");
    auto expected = before;
    expected.revision = desk.revision;
    expected.source = desk.source;
    expected.items = desk.items;
    expected.mailbox_shortcut = desk.mailbox_shortcut;
    expected.menu_layout_shortcut = desk.menu_layout_shortcut;
    if (desk_json(expected) != desk_json(desk) || desk_json(desk) != desk_json(preparation.desktop))
        throw FormatError("save/load: menu-layout acknowledgement changed state outside the active menu design");
    validate_preparation_context(preparation);
    auto next_desktop = std::make_unique<DesktopScreen>(data_, std::move(desk));
    auto next_preparation = std::make_unique<PreparationState>(std::move(preparation));
    save_load_->acknowledge(); save_load_.reset();
    desktop_ = std::move(next_desktop); preparation_ = std::move(next_preparation);
}

void StartupFlow::attach_loaded_game(DeskState desk, PreparationState preparation) {
    if (!save_load_ || !save_load_->pending() || save_load_->pending()->action != SaveLoadAction::load ||
        save_load_->pending()->file_kind != SaveLoadFileKind::game ||
        desk.source != "engine_projection" || preparation.desktop.source != desk.source ||
        desk.revision <= save_load_->pending()->revision || preparation.desktop.manager_slot != desk.manager_slot ||
        preparation.desktop.revision != desk.revision)
        throw FormatError("save/load: loaded-game projection does not match the pending load");
    validate_desk_state(desk);
    validate_desk_state(preparation.desktop);
    if (preparation.desktop.manager_teams != desk.manager_teams || preparation.desktop.runtime_club != desk.runtime_club)
        throw FormatError("save/load: loaded-game manager mapping mismatch");
    auto next_desktop = std::make_unique<DesktopScreen>(data_, desk);
    auto next_preparation = std::make_unique<PreparationState>(std::move(preparation));
    save_load_->acknowledge(); save_load_.reset();
    desktop_ = std::move(next_desktop); preparation_ = std::move(next_preparation);
    manager_.reset(); pending_.reset(); confirmed_.clear(); awaiting_context_ = false;
    confirmed_runtime_teams_ = desk.manager_teams;
    engine_attached_ = true;
}

bool StartupFlow::resume_manager_selection(SelectionContext context) {
    if (!awaiting_context_ || context.revision <= roster_revision_ || context.club_order.empty())
        throw FormatError("manager selection: expected a newer engine/reference club-order context");
    // Construct before mutation so a bad context leaves the pending request intact.
    auto next = std::make_unique<ManagerScreen>(data_, int(confirmed_.size()), confirmed_, context);
    roster_revision_ = context.revision; roster_source_ = context.source;
    manager_ = std::move(next); awaiting_context_ = false;
    return true;
}

std::string StartupFlow::screen_name() const {
    if (indoor_standings_) return "indoor_standings";
    if(achievements_)return "achievements";
    if (settings_readme_) return "settings_readme";
    if(desk_records_)return desk_records_->kind()==DeskRecordKind::fair_play?"fair_play":"highscore";
    if(other_games_)return "other_games";
    if(balance_)return "balance_plan";
    if(top_scorers_)return "top_scorers";
    if(alltime_table_)return "alltime_table";
    if(world_cup_statistics_)return national_friendly_picker_?"national_friendly_picker":"world_cup_statistics";
    if(finance_totals_)return "finance_totals";
    if(series_records_)return "series_records";
    if(analytics_)return "analytics";
    if(statistics_rankings_)return "statistics_rankings";
    if(minute_statistics_)return "minute_statistics";
    if(transfer_history_)return "transfer_history";
    if(opportunities_)return "opportunities";
    if(statistics_menu_)return "statistics_menu";
    if(strengths_)return "strengths";
    if (calendar_) return calendar_->state().reschedule_manager >= 0 ? "reschedule_calendar" : "calendar";
    if(league_pairings_)return "league_pairings";
    if (joining_) return manager_ ? manager_->screen_name() : "joining_options";
    if (outgoing_transfer_) return "outgoing_transfer";
    if (friendly_table_active_) return "friendly_match";
    return player_card_ ? (player_card_->in_statistics() ? "player_statistics" : "player_card") : contracts_ ? "contracts" : friendly_match_ ? "friendly_match" : league_table_ ? "league_table" : mailbox_ ? "mailbox" : save_load_ ? "save_load" : settings_ ? "settings" : advertising_ ? "advertising" : environment_ ? "environment" : matchday_results_ ? "matchday_results" : player_card_ ? (player_card_->in_statistics() ? "player_statistics" : "player_card") : pitch_ ? "pitch" : lineup_ ? "lineup" : training_ ? "training" : training_camp_ ? "training_camp" : bank_ ? "bank" : stadium_ ? "stadium" : toto_ ? "toto" : transfer_market_ ? "transfer_market" : tactics_ ? "tactics" : desktop_ ? "desktop" : pending_ ? "career_pending" : awaiting_context_ ? "manager_pending" : manager_ ? manager_->screen_name() : "setup";
}
std::string StartupFlow::state_json() const {
    if (indoor_standings_) return indoor_standings_->state_json();
    if(achievements_)return "{\"screen\":\"achievements\",\"manager\":"+
        std::to_string(achievements_->state().manager_slot)+",\"season_count\":"+
        std::to_string(achievements_->state().season_count)+",\"selected_season\":"+
        std::to_string(achievements_->state().selected_season)+"}";
    if (settings_readme_) {
        return "{\"screen\":\"settings_readme\",\"available\":" +
            std::string(settings_readme_->file_available() ? "true" : "false") +
            ",\"displayed_bytes\":" + std::to_string(settings_readme_->displayed_bytes()) +
            ",\"close_input\":\"left_or_right_mouse_down\"}";
    }
    if (joining_) {
        std::ostringstream out;
        out << "{\"screen\":" << json_quote(screen_name()) << ",\"joining_manager\":true,\"engine_attached\":true,\"revision\":"
            << join_origin_.revision << ",\"options\":" << setup_json(join_setup_->options())
            << ",\"confirmed_count\":" << join_choices_.size();
        if (manager_) out << ",\"manager\":" << manager_json(manager_->choice());
        out << "}"; return out.str();
    }
    const auto with_attachment = [&](std::string value) {
        const std::string field = "\"engine_attached\":false";
        const auto at = value.find(field);
        if (engine_attached_ && at != std::string::npos) value.replace(at, field.size(), "\"engine_attached\":true");
        return value;
    };
    if (contracts_ && !player_card_) return contracts_->state_json();
    if (environment_) return environment_->state_json();
    if (outgoing_transfer_) return outgoing_transfer_->state_json();
    if(other_games_)return other_games_->state_json();
    if(desk_records_)return std::string("{\"screen\":\"")+
        (desk_records_->kind()==DeskRecordKind::fair_play?"fair_play":"highscore")+"\",\"close_input\":\"original_wait\"}";
    if(top_scorers_)return top_scorers_->state_json();
    if(world_cup_statistics_&&national_friendly_picker_)
        return "{\"screen\":\"national_friendly_picker\",\"manager\":"+
            std::to_string(national_friendly_owner_.manager)+",\"runtime_team\":"+
            std::to_string(national_friendly_owner_.runtime_team)+",\"revision\":"+
            std::to_string(national_friendly_owner_.revision)+",\"request_id\":"+
            std::to_string(national_friendly_owner_.sequence)+"}";
    if(world_cup_statistics_)return "{\"screen\":\"world_cup_statistics\"}";
    if(analytics_)return "{\"screen\":\"analytics\",\"flags\":"+std::to_string(analytics_->state().source_flags)+"}";
    if(statistics_rankings_)return "{\"screen\":\"statistics_rankings\",\"category\":"+std::to_string(statistics_rankings_->state().category)+",\"first_season\":"+std::to_string(statistics_rankings_->state().first_season)+"}";
    if(minute_statistics_)return "{\"screen\":\"minute_statistics\",\"manager\":"+std::to_string(minute_statistics_->projection().manager_slot)+"}";
    if(transfer_history_)return "{\"screen\":\"transfer_history\",\"manager\":"+std::to_string(transfer_history_->projection().manager_slot)+",\"rows\":"+std::to_string(transfer_history_->projection().rows.size())+"}";
    if(opportunities_)return opportunities_->state_json();
    if(statistics_menu_)return statistics_menu_->state_json();
    if(strengths_)return strengths_->state_json();
    if(balance_)return balance_->state_json();
    if(league_pairings_)return league_pairings_->state_json();
    if (save_load_ && settings_) return save_load_->state_json();
    if (in_readonly_view()) {
        auto result = calendar_ ? calendar_->state_json() : league_table_ ? league_table_->state_json() : mailbox_ ? mailbox_->state_json() : settings_ ? settings_->state_json() : advertising_->state_json();
        result.pop_back();
        return result + ",\"revision\":" + std::to_string(desktop_->state().revision) +
            ",\"engine_attached\":" + (engine_attached_ ? "true" : "false") +
            ",\"desktop_context\":" + with_attachment(desk_json(desktop_->state())) + "}";
    }
    if (friendly_match_) {
        auto result = friendly_match_->state_json();
        result.pop_back();
        return result + ",\"engine_attached\":" + (engine_attached_ ? "true" : "false") +
            ",\"desktop_context\":" + desk_json(desktop_->state()) + "}";
    }
    if (matchday_results_) return matchday_results_->state_json();
    if (save_load_) return save_load_->state_json();
    if (in_preparation_menu()) {
        auto result = player_card_ ? player_card_->state_json() : pitch_ ? pitch_->state_json() : lineup_ ? lineup_->state_json() : training_ ? training_->state_json() : training_camp_ ? training_camp_->state_json() : bank_ ? bank_->state_json() : stadium_ ? stadium_->state_json() : toto_ ? toto_->state_json() : transfer_market_ ? transfer_market_->state_json() : tactics_->state_json();
        result.pop_back();
        result += ",\"pitch_context\":";
        result += !preparation_->pitch || !preparation_->pitch->indoor ? "null" : (*preparation_->pitch->indoor ? "\"indoor\"" : "\"outdoor\"");
        if (lineup_ && !pitch_ && !player_card_ && lineup_->pending()) {
            const auto& action = *lineup_->pending();
            std::string reason;
            if (action.kind == LineupRequestKind::open_pitch)
                reason = !preparation_->pitch || !preparation_->pitch->indoor ? "missing_transient_pitch_context" : "indoor_pitch_not_supported";
            if (action.kind == LineupRequestKind::open_player_info) {
                reason = "missing_player_card_context";
                for (const auto& card : preparation_->player_cards) if (card.row == action.first_row)
                    reason = card.unsupported_record;
            }
            if (!reason.empty()) result += ",\"unavailable_navigation\":" + json_quote(reason);
        }
        return result + ",\"engine_attached\":" + (engine_attached_ ? "true" : "false") +
               ",\"desktop_context\":" + with_attachment(desk_json(desktop_->state())) + "}";
    }
    if (desktop_) {
        return with_attachment(desktop_->state_json());
    }
    std::ostringstream out;
    out << "{\"screen\":" << json_quote(screen_name()) << ",\"options\":" << setup_json(setup_.options())
        << ",\"confirmed_managers\":[";
    for (size_t i = 0; i < confirmed_.size(); ++i) {
        if (i) out << ',';
        out << manager_json(confirmed_[i]);
    }
    out << ']';
    if (manager_) out << ",\"manager\":" << manager_json(manager_->choice());
    out << ",\"roster_revision\":" << roster_revision_ << ",\"roster_source\":" << json_quote(roster_source_)
        << ",\"engine_attached\":false}";
    return out.str();
}

}  // namespace bmh::ui
