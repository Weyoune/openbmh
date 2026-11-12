// SPDX-License-Identifier: GPL-3.0-only
// CareerSession: match preparation and the live match: tactics, line-up, live overview, interventions, pitch.
// (moved out of career_session.cpp unchanged; the class is declared in app/career_session.hpp)
#include "app/career_session_internal.hpp"

namespace bmh::app {
// Sets one tactic option (index 0-7, value within the executable's limit) for the active
// manager; touches only the manager record option bytes, never the tactics board or random
// generators. False with a German 'error' text when stale or invalid; revision plus one.
bool CareerSession::dispatch_tactics(const ui::TacticsRequest& request, uint64_t& revision,
                                    std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    error.clear();
    const int active = int(g_.memory.u8(::bmh::layout::ActiveManager));
    if (request.manager_slot != active || request.manager_slot < 0 || request.manager_slot > 3 ||
        size_t(request.manager_slot) >= g_.manager_teams.size() ||
        request.runtime_club != g_.manager_teams[size_t(request.manager_slot)] ||
        request.revision != revision) {
        error = "Die Taktikanfrage ist veraltet."; return false;
    }
    if (request.kind != ui::TacticsRequestKind::set_tactic || request.index < 0 || request.index >= 8 ||
        request.desired_value < 0 || request.desired_value > exe_.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField + request.index)) {
        error = "Ungültige Taktikeinstellung."; return false;
    }
    const bool live_option = (g_.memory.has(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3) ? g_.memory.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3) : exe_.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3)) != 0;
    if (live_option && exe_.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField2 + request.index) == 0) {
        error = "Diese Taktikeinstellung ist nicht verfügbar."; return false;
    }
    if ((request.index == 0 && request.desired_assistant_attitude != std::optional<uint8_t>(0)) ||
        (request.index != 0 && request.desired_assistant_attitude)) {
        error = "Ungültige Assistenteneinstellung."; return false;
    }
    // RerateTeam: only +d2..+d9 and (index 0) option byte +2.
    // The tactic board, player order and both RNG streams stay untouched.
    const size_t rec = size_t(request.manager_slot) * kManagerRecordSize;
    const size_t option = size_t(request.manager_slot) * 5 + 2;
    if (g_.manager_records.size() < rec + 0xb0c || g_.manager_options.size() <= option) {
        error = "Die Taktikdaten sind unvollständig."; return false;
    }
    g_.manager_records[rec + 0xd2 + size_t(request.index)] = uint8_t(request.desired_value);
    if (request.index == 0) g_.manager_options[option] = 0;
    ++revision;
    return true;
}

// Lineup requests of the active manager (refused during a calendar advance or pending offer
// dispatch). prepare_lineup builds the lineup entry on a staged copy and accepts it only when
// the first eleven squad rows form a valid authoritative lineup.
bool CareerSession::dispatch_lineup(const ui::LineupRequest& request, uint64_t& revision,
                                    std::string& error) {
    if (calendar_advance_active_ || pending_offer_dispatch_active_) {
        error = "lineup_unavailable_during_calendar_advance";
        return false;
    }
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return false; }
    error.clear();
    const int active = g_.memory.has(::bmh::layout::ActiveManager) ? int(g_.memory.u8(::bmh::layout::ActiveManager)) : -1;
    if (request.manager_slot != active || active < 0 || active > 3 ||
        size_t(active) >= g_.manager_teams.size() || request.runtime_club != g_.manager_teams[size_t(active)] ||
        request.revision != revision || revision == std::numeric_limits<uint64_t>::max() ||
        request.runtime_club < 0 || request.runtime_club >= int(g_.team_flags.size())) {
        error = "Die Aufstellungsaktion ist veraltet.";
        return false;
    }
    const SquadRange range = squad_range(request.runtime_club);
    if (range.count < 11 || range.first + range.count > g_.squad_slots.size() ||
        g_.tactics.size() < size_t(active + 1) * 0x840 ||
        g_.manager_options.size() <= size_t(active) * 5 || g_.player_cards.size() != g_.players.size()) {
        error = "Der Aufstellungszustand ist unvollständig.";
        return false;
    }
    if (request.kind == ui::LineupRequestKind::prepare_lineup) {
        GameState staged = g_;
        try {
            // PitchScreen opens through the human Int2 path first.
            // Existing assistant scoring retains its documented port limitations.
            auto_lineup(staged, request.runtime_club, 0xe4);
            prepare_lineup_entry(staged, request.runtime_club); // PrepareLineupEntry.
            open_lineup_screen(staged, active); // OpenBMH deviates from the original: supports designated set-piece record initialization and validation.
// supported rule profile NormalizePitchSetPieceSource entry initialization.
            if (!initialize_pitch_set_piece_source(staged, active, request.runtime_club))
                throw std::runtime_error("set-piece set-piece record did not validate");
            const uint32_t display=uint32_t(::bmh::layout::CareerSession_AnyOf_ExecutableField+active*2);
            if(staged.memory.has(display)&&(staged.memory.u16(display)&0x80)&&
               !normalize_pitch_set_piece_source(staged,active,request.runtime_club))
                throw std::runtime_error("set-piece reentry redraw did not validate");
        } catch (const std::exception& e) {
            error = std::string("Diese Aufstellung kann im aktuellen Spielmodus noch nicht vorbereitet werden. (") + e.what() + ")";
            return false;
        }
        // Accept only the valid authoritative eleven produced by the human path.
        std::array<bool, 0x1000> seen{};
        for (int row = 0; row < 11; ++row) {
            const uint16_t slot = staged.squad_slots.at(range.first + size_t(row));
            const int id = slot & 0x0fff;
            const auto* board = staged.tactics.data() + size_t(active) * 0x840 + size_t(row) * 0x58;
            if (id == 0x0fff || size_t(id) >= g_.players.size() || seen[size_t(id)] ||
                !(slot & 0x8000) || (slot & 0x4000) || int8_t(board[0x41]) < 0 ||
                int8_t(board[0x41]) > 19 || int8_t(board[0x42]) < 0 || int8_t(board[0x42]) > 8) {
                error = "Die Aufstellung ist noch nicht für den Anstoß vorbereitet.";
                return false;
            }
            seen[size_t(id)] = true;
        }
        g_ = std::move(staged);
        lineup_entry_manager_ = active;
        lineup_entry_club_ = request.runtime_club;
        ++revision;
        return true;
    }
    // Swap path: only allowed after prepare_lineup for the same manager and club. The two rows must
    // still hold the slots and players the UI saw; injured players and the foreign-player rule are
    // refused until their dialogs exist. The swap exchanges squad slots (keeping the role bits with
    // the row) and tactic board entries, re-syncs the card row ids and switches the manager's
    // formation automation off.
    if (request.kind != ui::LineupRequestKind::swap_players ||
        lineup_entry_manager_ != active || lineup_entry_club_ != request.runtime_club ||
        request.first_row < 0 || request.second_row < 0 || request.first_row == request.second_row ||
        size_t(request.first_row) >= range.count || size_t(request.second_row) >= range.count) {
        error = "Der Spielerwechsel ist nicht freigegeben.";
        return false;
    }
    const size_t first = range.first + size_t(request.first_row);
    const size_t second = range.first + size_t(request.second_row);
    const uint16_t first_slot = g_.squad_slots.at(first), second_slot = g_.squad_slots.at(second);
    const int first_id = (first_slot & 0x0fff) == 0x0fff ? -1 : first_slot & 0x0fff;
    const int second_id = (second_slot & 0x0fff) == 0x0fff ? -1 : second_slot & 0x0fff;
    if (first_slot != request.first_slot || second_slot != request.second_slot ||
        first_id != request.first_player || second_id != request.second_player ||
        first_id < 0 || second_id < 0 || size_t(first_id) >= g_.players.size() || size_t(second_id) >= g_.players.size()) {
        error = "Die Kaderzeilen wurden inzwischen geändert.";
        return false;
    }
    // SizeT3 requires injury/foreign-rule confirmations for special
    // swaps. Until those dialogs exist, reject that path before any writes.
    if (((g_.players[size_t(first_id)].w364 >> 16) & 0x1f) != 0 ||
        ((g_.players[size_t(second_id)].w364 >> 16) & 0x1f) != 0) {
        error = "Ein verletzter Spieler kann hier noch nicht eingewechselt werden.";
        return false;
    }
    if ((request.first_row < 11) != (request.second_row < 11) && exe_.u8(::bmh::layout::ForeignPlayerLimitEnabled) != 0) {
        error = "Dieser Wechsel benötigt eine Bestätigung der Ausländerregel.";
        return false;
    }
    const size_t board_base = size_t(active) * 0x840;
    auto* a = g_.tactics.data() + board_base + size_t(request.first_row) * 0x58;
    auto* b = g_.tactics.data() + board_base + size_t(request.second_row) * 0x58;
    const uint8_t ax = a[0x41], ay = a[0x42], bx = b[0x41], by = b[0x42];
    // Original SquadSlotsBoardEntriesChangePlaces moves the player/tactics entry, then restores
    // the 0xc000 role bits to their roster rows. Preserve bit 12 and the
    // player id with the entry; keep the starter/bench flags with the slot.
    g_.squad_slots[first] = uint16_t((second_slot & 0x1fff) | (first_slot & 0xc000));
    g_.squad_slots[second] = uint16_t((first_slot & 0x1fff) | (second_slot & 0xc000));
    std::swap_ranges(a, a + 0x58, b);
    a[0x41] = ax; a[0x42] = ay; b[0x41] = bx; b[0x42] = by;
    // GatedByParamLive is applied to every marked row, not just the two swapped
    // players. Keep card-side row identity synchronized with the roster.
    for (size_t row = 0; row < range.count; ++row) {
        const uint16_t slot = g_.squad_slots[range.first + row];
        if ((slot & 0xc000) == 0) continue;
        const size_t id = slot & 0x0fff;
        if (id >= g_.players.size()) continue;
        g_.player_cards[id] = uint16_t((g_.player_cards[id] & 0xffe0) | uint16_t(row));
    }
    g_.manager_options.at(size_t(active) * 5) = 0; // source manual mode selects DispatchLiveOverview
    ++revision;
    return true;
}

// Live overview dispatch (pattern open -> dispatch -> acknowledge): applies one action of the
// overview opened by open_running_overview. Requires an active calendar advance, the owner of the
// current revision and an engine that is still bound to this session with unchanged match groups
// and referees. The action variant selects the child: league family navigation or control,
// strengths, pairings or the cup other-games page. Close actions end the owner and return a
// preparation projection; a failure returns an empty optional with 'error'.
std::optional<CareerSession::LiveOverviewDispatch> CareerSession::dispatch_live_overview(
    const LiveOverviewAction& action, MatchEngine& engine, uint64_t& revision, std::string& error,
    const TransferOfferTeamInfoPresenter& team_info) {
    error.clear();
    if (!calendar_advance_active_ || !live_overview_owner_ || revision==UINT64_MAX || live_overview_owner_->request.revision!=revision ||
        &engine.state!=&g_ || &engine.game!=&game_ || &engine.rand!=&rand_ ||
        (live_overview_owner_->phase_result
            ? (!phase_result_request_ || live_overview_owner_->request.group_index!=int(phase_result_request_->group_index))
            : live_overview_owner_->request.group_index!=0) || engine.groups.empty() ||
        engine.groups.size()!=live_overview_owner_->active_groups.size() ||
        engine.groups.size()!=turn_driver_.groups.size() ||
        engine.groups.front().desc!=turn_driver_.groups.front().desc ||
        engine.groups.front().fixtures!=turn_driver_.groups.front().fixtures ||
        engine.referees!=live_overview_owner_->referees) {
        error="live overview: stale owner or no longer at the Source minute boundary";
        return {};
    }
    for(size_t i=0;i<engine.groups.size();++i)
        if(engine.groups[i].desc!=live_overview_owner_->active_groups[i].first ||
           engine.groups[i].fixtures!=live_overview_owner_->active_groups[i].second) {
            error="live overview: active Source match group changed";return {};
        }
    const auto live= *live_overview_owner_;
    // Scope guard: calendar_advance_active_ is off while the family code runs and is restored on exit.
    struct LiveBoundaryFamilyScope {
        bool& active; bool previous;
        explicit LiveBoundaryFamilyScope(bool& value) : active(value), previous(value) { active=false; }
        ~LiveBoundaryFamilyScope() { active=previous; }
    } family_scope(calendar_advance_active_);
    LiveOverviewDispatch result;result.kind=live.kind;
    try {
        if(const auto* family=std::get_if<ui::LeagueFamilyRequest>(&action)) {
            if((live.kind!=LiveOverviewKind::league_table&&live.kind!=LiveOverviewKind::league_pairings&&
                live.kind!=LiveOverviewKind::league_strengths) ||
               family->identity!=live.family_identity || family->identity.revision!=revision ||
               !league_family_owner_ || league_family_owner_->identity!=family->identity ||
               family->from_child!=league_family_owner_->child)
                throw FormatError("live overview: mismatched League family owner");
            if(family->kind==ui::LeagueFamilyRequestKind::close) {
                auto preparation=close_league_family(*family,revision,error);
                if(!preparation)throw FormatError(error.empty()?"live overview: Source family close rejected":error);
                result.closed=true;result.preparation=std::move(*preparation);live_overview_owner_.reset();return result;
            }
            if(family->kind==ui::LeagueFamilyRequestKind::control && family->from_child==1) {
                auto page=dispatch_league_table_control(*family,revision,error);
                if(!page)throw FormatError(error.empty()?"live overview: Source Table control rejected":error);
                result.table=page->page;result.family_identity=page->identity;
                result.table_phase=page->phase;result.table_dialog=page->dialog;
            } else if(family->kind==ui::LeagueFamilyRequestKind::table_range_result && family->from_child==1) {
                auto page=dispatch_league_table_range_reply(*family,revision,error);
                if(!page)throw FormatError(error.empty()?"live overview: Source Table range reply rejected":error);
                result.table=page->page;result.family_identity=page->identity;
                result.table_phase=page->phase;result.table_dialog=page->dialog;
            } else if(family->kind==ui::LeagueFamilyRequestKind::table_forecast_presented && family->from_child==1) {
                auto page=acknowledge_league_forecast_present(*family,revision,error);
                if(!page)throw FormatError(error.empty()?"live overview: Source Table forecast ACK rejected":error);
                result.table=page->page;result.family_identity=page->identity;
                result.table_phase=page->phase;result.table_dialog=page->dialog;
            } else if(family->kind==ui::LeagueFamilyRequestKind::team_info && family->from_child==1) {
                auto identity=dispatch_family_table_team_info(*family,revision,error,team_info);
                if(!identity)throw FormatError(error.empty()?"live overview: Source Table TeamInfo rejected":error);
                auto page=current_league_table_dispatch(error);
                if(!page)throw FormatError(error.empty()?"live overview: Source Table return is unavailable":error);
                result.table=page->page;result.family_identity=page->identity;
                result.table_phase=page->phase;result.table_dialog=page->dialog;
            } else if((family->kind==ui::LeagueFamilyRequestKind::select_league ||
                       family->kind==ui::LeagueFamilyRequestKind::toggle_league_group) && family->from_child==1) {
                if(!select_league_family_page(*family,revision,error))
                    throw FormatError(error.empty()?"live overview: Source Table selector rejected":error);
                auto page=current_league_table_dispatch(error);
                if(!page)throw FormatError(error.empty()?"live overview: Source Table selector projection missing":error);
                result.table=page->page;result.family_identity=page->identity;
                result.table_phase=page->phase;result.table_dialog=page->dialog;
            } else if(family->kind==ui::LeagueFamilyRequestKind::navigate &&
                      family->target_child>=1 && family->target_child<=3) {
                if(family->from_child==3) {
                    if(!suspend_strengths_for_family(*family,revision,error))
                        throw FormatError(error.empty()?"live overview: Source Strengths child return rejected":error);
                } else if(family->from_child==2) {
                    if(!suspend_pairings_for_family(*family,revision,error))
                        throw FormatError(error.empty()?"live overview: Source Pairings child return rejected":error);
                }
                auto fresh=strengths_open_request(revision);
                if(!fresh)throw FormatError("live overview: Source family has no active manager projection");
                auto identity=fresh->identity;
                identity.family_id=family->identity.family_id;
                identity.family_sequence=family->identity.family_sequence+1;
                identity.league=family->identity.league;
                identity.league_group=family->identity.league_group;
                identity.manager_default_league=family->identity.manager_default_league;
                identity.source_param2=family->identity.source_param2;
                if(!prepare_league_family_child(identity,family->target_child,revision,error))
                    throw FormatError(error.empty()?"live overview: Source family child preparation rejected":error);
                identity.expected_family_word=g_.memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(identity.manager)*2);
                if(family->target_child==1) {
                    if(!advance_league_family(*family,identity,1,revision,error))
                        throw FormatError(error.empty()?"live overview: Source Table handoff rejected":error);
                    auto page=current_league_table_dispatch(error);
                    if(!page)throw FormatError(error.empty()?"live overview: Source Table handoff projection missing":error);
                    result.kind=LiveOverviewKind::league_table;
                    result.table=page->page;result.family_identity=page->identity;
                    result.table_phase=page->phase;result.table_dialog=page->dialog;
                } else if(family->target_child==2) {
                    LeaguePairingsDraft draft{g_,game_,rand_,club_modal_runtime_,club_modal_runtime_,{}, {},
                        save_stream(g_),rand_.seed(),game_.seed(),rand_.calls(),game_.calls()};
                    for(const auto& group:engine.groups)draft.original_active_groups.emplace_back(group.desc,group.fixtures);
                    draft.original_referee_profiles=engine.referees;
                    draft.owner=open_league_pairings_source(draft.state,identity.manager,ui::league_pairings_constants(exe_));
                    if(draft.owner.league!=identity.league) {
                        // Source Move chooses the family league before its first visible page.
                        draft.owner.league=identity.league;
                        draft.owner.round=std::min<int>(draft.state.league_rounds.at(size_t(identity.league)),
                            league_matchdays(identity.league)-1);
                        draft.owner.title_round=draft.state.league_rounds.at(size_t(identity.league));
                        if(draft.owner.round>=league_matchdays(identity.league))
                            throw FormatError("live overview: selected Source Pairings league round is outside its schedule");
                    }
                    draft.active_packet=validate_normal_pairings_group(draft.state,draft.owner,draft.runtime);
                    const uint64_t child_scope=++league_pairings_scope_;
                    auto page=ui::league_pairings_from_game(draft.state,draft.owner,exe_,revision,
                        child_scope,family->identity.family_sequence+1,draft.active_packet);
                    draft.identity=page.identity;
                    draft.runtime[::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField]=exe_.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField+uint32_t(draft.owner.league));
                    pairings_runtime_draw(draft.state,draft.runtime,page,exe_);
                    league_pairings_draft_=std::move(draft);
                    if(!advance_league_family(*family,identity,2,revision,error))
                        throw FormatError(error.empty()?"live overview: Source Pairings handoff rejected":error);
                    result.kind=LiveOverviewKind::league_pairings;
                    result.pairings=std::move(page);
                    result.family_identity=league_family_owner_->identity;
                } else {
                    ui::StrengthsRequest open=*fresh;open.identity=identity;open.kind=ui::StrengthsAction::open;
                    auto page=dispatch_strengths(open,revision,error,{},true);
                    if(!page||page->closed)throw FormatError(error.empty()?"live overview: Source Strengths entry rejected":error);
                    if(!advance_league_family(*family,page->page.identity,3,revision,error))
                        throw FormatError(error.empty()?"live overview: Source Strengths handoff rejected":error);
                    result.kind=LiveOverviewKind::league_strengths;
                    result.strengths=page->page;result.family_identity=page->page.identity;
                }
            } else {
                throw FormatError("live overview: unsupported Source family transition");
            }
            if(league_family_owner_) {
                result.family_identity=league_family_owner_->identity;
                live_overview_owner_->kind=result.kind;
                live_overview_owner_->family_identity=result.family_identity;
            }
            live_overview_owner_->request.revision=revision;
            return result;
        }
        if(const auto* strengths=std::get_if<ui::StrengthsRequest>(&action)) {
            if(live.kind!=LiveOverviewKind::league_strengths || !strengths_owner_ ||
               strengths->identity!=strengths_owner_->page.identity ||
               strengths->identity.revision!=revision || strengths->identity!=live.family_identity)
                throw FormatError("live overview: mismatched Source Strengths owner");
            if(strengths->kind==ui::StrengthsAction::table || strengths->kind==ui::StrengthsAction::pairings) {
                ui::LeagueFamilyRequest navigation;
                navigation.identity=live.family_identity;
                navigation.kind=ui::LeagueFamilyRequestKind::navigate;
                navigation.from_child=3;
                navigation.target_child=strengths->kind==ui::StrengthsAction::table?1:2;
                // The opposite guard: sets calendar_advance_active_ for the nested table/pairings navigation,
                // which expects to run inside the advance, and restores the value afterwards.
                struct RestoreObserverScope { bool& flag; bool previous; explicit RestoreObserverScope(bool& f):flag(f),previous(f){flag=true;} ~RestoreObserverScope(){flag=previous;} } observer_scope(calendar_advance_active_);
                return dispatch_live_overview(LiveOverviewAction{navigation},engine,revision,error,team_info);
            }
            if(strengths->kind==ui::StrengthsAction::close) {
                ui::LeagueFamilyRequest close;close.identity=live.family_identity;
                close.kind=ui::LeagueFamilyRequestKind::close;close.from_child=3;close.target_child=0;
                auto preparation=close_league_family(close,revision,error);
                if(!preparation)throw FormatError(error.empty()?"live overview: Source Strengths close rejected":error);
                result.closed=true;result.preparation=std::move(*preparation);live_overview_owner_.reset();return result;
            }
            const auto prior=live.family_identity;
            auto page=dispatch_strengths(*strengths,revision,error,team_info,false);
            if(!page || page->closed)throw FormatError(error.empty()?"live overview: Source Strengths action rejected":error);
            if(!refresh_family_after_child(prior,3,page->page.identity,revision,error))
                throw FormatError(error.empty()?"live overview: Source Strengths ACK rejected":error);
            result.strengths=page->page;result.preparation=page->preparation;
            result.family_identity=page->page.identity;
            live_overview_owner_->family_identity=result.family_identity;
            live_overview_owner_->request.revision=revision;
            return result;
        }
        if(const auto* pairings=std::get_if<ui::LeaguePairingsRequest>(&action)) {
            if(live.kind!=LiveOverviewKind::league_pairings || !league_pairings_draft_ ||
               pairings->identity!=league_pairings_draft_->identity || pairings->identity.revision!=revision)
                throw FormatError("live overview: mismatched Source Pairings owner");
            if(pairings->event.kind==LeaguePairingsAction::close) {
                ui::LeagueFamilyRequest close;close.identity=live.family_identity;
                close.kind=ui::LeagueFamilyRequestKind::close;close.from_child=2;close.target_child=0;
                auto preparation=close_league_family(close,revision,error);
                if(!preparation)throw FormatError(error.empty()?"live overview: Source Pairings close rejected":error);
                result.closed=true;result.preparation=std::move(*preparation);live_overview_owner_.reset();return result;
            }
            auto page=dispatch_league_pairings(*pairings,revision,error,team_info);
            if(!page)throw FormatError(error.empty()?"live overview: Source Pairings action rejected":error);
            result.closed=page->closed;result.pairings=page->page;result.preparation=page->preparation;
            if(result.closed) {
                // The Source Move return is completed by the outer G/T wrapper.
                live_overview_owner_.reset();league_family_owner_.reset();
            } else {
                live_overview_owner_->request.revision=revision;
                if(league_family_owner_) {
                    live_overview_owner_->family_identity=league_family_owner_->identity;
                    result.family_identity=league_family_owner_->identity;
                }
            }
            return result;
        }
        const auto* cup=std::get_if<ui::OtherGamesRequest>(&action);
        if(!cup || live.kind!=LiveOverviewKind::cup_other_games || !other_games_owner_ ||
           cup->identity!=other_games_owner_->page.identity || cup->identity.revision!=revision)
            throw FormatError("live overview: mismatched Source cup overview owner");
        auto page=dispatch_other_games(*cup,revision,error,team_info);
        if(!page)throw FormatError(error.empty()?"live overview: Source cup overview action rejected":error);
        result.closed=page->closed;result.cup=page->page;result.preparation=page->preparation;
        if(result.closed)live_overview_owner_.reset();
        else live_overview_owner_->request.revision=revision;
        return result;
    } catch(const std::exception& e) {
        error=e.what();return {};
    }
}

// Live intervention group (lineup changes during a running match). Pattern: begin stores a
// LiveInterventionCheckpoint (state, match groups, both random generators, revision), the
// substitution requests work against it, finish rerates the team and bumps the revision, abort
// puts everything back.
// Begin: only after minute 2, source mode 2 or 8, for a human manager whose team plays in the
// current match. Mode 2 also runs the lineup entry preparation. Returns false with a German
// 'error' text and no open intervention otherwise.
bool CareerSession::begin_live_intervention(int minute, int source_mode, int manager, int team,
        MatchEngine& engine, uint64_t& revision, std::string& error) {
    error.clear();
    if (live_intervention_ || minute <= 2 || (source_mode != 2 && source_mode != 8) ||
        revision == std::numeric_limits<uint64_t>::max() || manager < 0 || manager >= int(g_.manager_teams.size()) ||
        team < 0 || team >= int(g_.team_flags.size()) || g_.manager_teams[size_t(manager)] != team ||
        g_.manager_of(team) != manager || &engine.state != &g_ || &engine.game != &game_ || &engine.rand != &rand_) {
        error = "Der Live-Eingriff kann für diesen Manager oder Modus nicht geöffnet werden.";
        return false;
    }
    bool current_match = false;
    for (const MatchGroup& group : engine.groups) for (int match = 0; match < group.matches(); ++match) {
        const uint32_t bit = uint32_t(1) << match;
        if ((group.kickoff_void_mask & bit) || (group.void_mask() & bit)) continue;
        const int home = group.fixtures.at(size_t(match) * 2);
        const int away = group.fixtures.at(size_t(match) * 2 + 1);
        if (std::find(g_.live_matches.begin(), g_.live_matches.end(), home) != g_.live_matches.end() &&
            (team == home || team == away)) current_match = true;
    }
    if (!current_match) {
        error = "Die Mannschaft spielt in der aktuellen Partie nicht.";
        return false;
    }
    LiveInterventionCheckpoint checkpoint;
    checkpoint.state = g_;
    checkpoint.groups = engine.groups;
    checkpoint.game = engine.game;
    checkpoint.rand = engine.rand;
    checkpoint.minute = minute; checkpoint.source_mode = source_mode;
    checkpoint.manager = manager; checkpoint.team = team;
    checkpoint.initial_revision = revision; checkpoint.current_revision = revision;
    live_intervention_ = std::move(checkpoint);
    if (source_mode == 2) {
        try {
            GameState entered = g_;
            prepare_lineup_entry(entered, team); // PitchScreen's unconditional PrepareLineupEntry entry pass.
            g_ = std::move(entered);
        } catch (const std::exception& e) {
            live_intervention_.reset();
            error = std::string("Die Live-Aufstellung kann nicht geöffnet werden: ") + e.what();
            return false;
        }
    }
    if (g_.memory.has(::bmh::layout::ActiveManager)) g_.memory.set8(::bmh::layout::ActiveManager, uint8_t(manager));
    return true;
}

// Begin for an automatic intervention caused by a match event (kind 1 or 2): the event must match
// the engine's current group, match, side and teams, the manager must have the automatic
// option on, and the affected player must show the expected status. Opens the lineup entry like
// mode 2; false with 'error' otherwise.
bool CareerSession::begin_live_event_intervention(const LiveMatchEvent& event, MatchEngine& engine,
        uint64_t& revision, std::string& error) {
    error.clear();
    const int manager = event.manager_slot, team = event.team;
    if (live_intervention_ || event.minute < 1 || (event.kind != 1 && event.kind != 2) ||
        event.group_index >= engine.groups.size() || event.match_index < 0 || event.side < 0 || event.side > 1 ||
        manager < 0 || manager >= int(g_.manager_teams.size()) || team < 0 ||
        team >= int(g_.team_flags.size()) || event.player < 0 || event.player >= int(g_.players.size()) ||
        g_.manager_teams[size_t(manager)] != team || g_.manager_of(team) != manager ||
        g_.manager_option(manager, 1) || engine.live_desk_disabled || !(g_.team_morale.at(size_t(team)) & 0x80) ||
        &engine.state != &g_ || &engine.game != &game_ || &engine.rand != &rand_ ||
        revision == std::numeric_limits<uint64_t>::max()) {
        error = "Der automatische Live-Eingriff gehört nicht zu einem aktuellen Source-Ereignis.";
        return false;
    }
    const MatchGroup& group = engine.groups[event.group_index];
    if (event.match_index >= group.matches() ||
        group.fixtures.at(size_t(event.match_index) * 2 + size_t(event.side)) != team ||
        group.fixtures.at(size_t(event.match_index) * 2 + size_t(1 - event.side)) != event.opponent ||
        (group.kickoff_void_mask >> event.match_index & 1) || (group.void_mask() >> event.match_index & 1) ||
        std::find(g_.live_matches.begin(), g_.live_matches.end(), group.fixtures.at(size_t(event.match_index) * 2)) ==
            g_.live_matches.end()) {
        error = "Das Source-Ereignis gehört nicht zur aktuell gezeigten Partie.";
        return false;
    }
    // The Source event already changed this exact player/slot before opening PitchScreen.
    bool event_player_found = false;
    const auto range = squad_range(team);
    for (size_t offset = 0; offset < range.count; ++offset) {
        const size_t slot = size_t(range.first) + offset;
        const uint16_t entry = g_.squad_slots.at(slot);
        if ((entry & 0x0fff) != event.player) continue;
        if (event.kind == 2 && (g_.players.at(size_t(event.player)).w35c >> 27) != 0 && !(entry & 0x8000))
            event_player_found = true;
        if (event.kind == 1 && (g_.players.at(size_t(event.player)).w368 & 0x07000000u) != 0 && !(entry & 0x8000))
            event_player_found = true;
    }
    if (!event_player_found) {
        error = "Der Source-Spielerstatus passt nicht mehr zum automatischen Eingriff.";
        return false;
    }
    LiveInterventionCheckpoint checkpoint;
    checkpoint.state = g_; checkpoint.groups = engine.groups;
    checkpoint.game = engine.game; checkpoint.rand = engine.rand;
    checkpoint.minute = event.minute; checkpoint.source_mode = 2;
    checkpoint.manager = manager; checkpoint.team = team; checkpoint.source_event = true;
    checkpoint.event = event; checkpoint.initial_revision = revision; checkpoint.current_revision = revision;
    live_intervention_ = std::move(checkpoint);
    try {
        GameState entered = g_;
        prepare_lineup_entry(entered, team);
        g_ = std::move(entered);
    } catch (const std::exception& e) {
        live_intervention_.reset();
        error = std::string("Die automatische Live-Aufstellung kann nicht geöffnet werden: ") + e.what();
        return false;
    }
    if (g_.memory.has(::bmh::layout::ActiveManager)) g_.memory.set8(::bmh::layout::ActiveManager, uint8_t(manager));
    return true;
}

// Ends an event intervention: the open checkpoint must belong to exactly this event and the
// engine groups must be unchanged. Only consumes the owner and bumps the revision (the rerate
// belongs to the event itself).
bool CareerSession::finish_live_event_intervention(const LiveMatchEvent& event, MatchEngine& engine,
        uint64_t& revision, std::string& error) {
    error.clear();
    if (!live_intervention_ || !live_intervention_->source_event ||
        live_intervention_->event.group_index != event.group_index ||
        live_intervention_->event.match_index != event.match_index ||
        live_intervention_->event.side != event.side || live_intervention_->event.team != event.team ||
        live_intervention_->event.opponent != event.opponent ||
        live_intervention_->event.player != event.player || live_intervention_->event.kind != event.kind ||
        live_intervention_->minute != event.minute || live_intervention_->manager != event.manager_slot ||
        live_intervention_->current_revision != revision || &engine.state != &g_ ||
        &engine.game != &game_ || &engine.rand != &rand_ || revision == std::numeric_limits<uint64_t>::max()) {
        error = "Der automatische Live-Menüabschluss ist veraltet oder gehört zu einem anderen Ereignis.";
        return false;
    }
    if (event.group_index >= engine.groups.size() || event.match_index < 0 || event.side < 0 || event.side > 1) {
        error = "Das Source-Ereignis gehört nicht mehr zur aktiven Partie.";
        return false;
    }
    const MatchGroup& group = engine.groups[event.group_index];
    if (event.match_index >= group.matches() ||
        group.fixtures.at(size_t(event.match_index) * 2 + size_t(event.side)) != event.team ||
        group.fixtures.at(size_t(event.match_index) * 2 + size_t(1 - event.side)) != event.opponent ||
        (group.kickoff_void_mask >> event.match_index & 1) || (group.void_mask() >> event.match_index & 1)) {
        error = "Die aktive Partie hat sich während der automatischen Aufstellung geändert.";
        return false;
    }
    if (engine.groups.size() != live_intervention_->groups.size() ||
        group.desc != live_intervention_->groups[event.group_index].desc ||
        group.fixtures != live_intervention_->groups[event.group_index].fixtures) {
        error = "Die Matchgruppenstruktur hat sich während der Source-Aufstellung geändert.";
        return false;
    }
    // PitchScreen returns to EventImpl; its single Source rerate remains at the event's
    // existing tail, so this boundary only consumes the authenticated modal owner.
    ++revision;
    live_intervention_.reset();
    return true;
}

// Ends a normal intervention: rerates the team on a staged engine copy; on success the staged
// state, groups and generators replace the live ones and the revision advances, on a rerate
// failure or exhausted revision the checkpoint is restored. False with a German 'error' text.
bool CareerSession::finish_live_intervention(int minute, int source_mode, int manager, int team,
        MatchEngine& engine, uint64_t& revision, std::string& error) {
    error.clear();
    if (!live_intervention_ || live_intervention_->minute != minute ||
        live_intervention_->source_mode != source_mode || live_intervention_->manager != manager ||
        live_intervention_->team != team || live_intervention_->current_revision != revision ||
        &engine.state != &g_ || &engine.game != &game_ || &engine.rand != &rand_) {
        error = "Der Live-Menüabschluss ist veraltet oder gehört zu einem anderen Eingriff.";
        return false;
    }
    const LiveInterventionCheckpoint& checkpoint = *live_intervention_;
    GameState staged_state = g_;
    std::vector<MatchGroup> staged_groups = engine.groups;
    GameRandom staged_game = engine.game;
    WatcomRandom staged_rand = engine.rand;
    MatchEngine staged_engine(staged_state, staged_groups, staged_game, staged_rand, engine.referees);
    staged_engine.indoor = engine.indoor;
    staged_engine.alternate_event_roll = engine.alternate_event_roll;
    staged_engine.match_events_disabled = engine.match_events_disabled;
    staged_engine.live_scene_visible = engine.live_scene_visible;
    staged_engine.live_desk_disabled = engine.live_desk_disabled;
    staged_engine.month = engine.month;
    staged_engine.trace = engine.trace;
    staged_engine.show = engine.show;
    staged_engine.leave_screens = engine.leave_screens;
    staged_engine.buttons = engine.buttons;
    MinuteReport report;
    staged_engine.rerate_team(team, report);
    if (report.unsupported) {
        g_ = checkpoint.state;
        engine.groups = checkpoint.groups;
        engine.game = checkpoint.game;
        engine.rand = checkpoint.rand;
        revision = checkpoint.initial_revision;
        live_intervention_.reset();
        error = "Die Mannschaft kann nach dem Live-Eingriff nicht neu bewertet werden: " + report.reason;
        return false;
    }
    g_ = std::move(staged_state);
    engine.groups = std::move(staged_groups);
    engine.game = staged_game;
    engine.rand = staged_rand;
    if (revision == std::numeric_limits<uint64_t>::max()) {
        g_ = checkpoint.state;
        engine.groups = checkpoint.groups;
        engine.game = checkpoint.game;
        engine.rand = checkpoint.rand;
        revision = checkpoint.initial_revision;
        live_intervention_.reset();
        error = "Die Live-Menürevisionsnummer ist erschöpft.";
        return false;
    }
    ++revision;
    live_intervention_.reset();
    return true;
}

// Abandons the matching open intervention and restores state, groups, random generators and the
// revision from its checkpoint. Does nothing if no matching intervention is open.
void CareerSession::abort_live_intervention(int minute, int source_mode, int manager, int team,
        MatchEngine& engine, uint64_t& revision) {
    if (!live_intervention_ || live_intervention_->minute != minute ||
        live_intervention_->source_mode != source_mode || live_intervention_->manager != manager ||
        live_intervention_->team != team || &engine.state != &g_ ||
        &engine.game != &game_ || &engine.rand != &rand_) return;
    g_ = std::move(live_intervention_->state);
    if (live_intervention_->source_event && engine.groups.size() == live_intervention_->groups.size()) {
        // Preserve MatchGroup element addresses held by MatchEngine::event/side;
        // side reacquires its record byte pointers immediately after this callback.
        for (size_t i = 0; i < engine.groups.size(); ++i)
            engine.groups[i] = std::move(live_intervention_->groups[i]);
    } else {
        engine.groups = std::move(live_intervention_->groups);
    }
    engine.game = live_intervention_->game;
    engine.rand = live_intervention_->rand;
    revision = live_intervention_->initial_revision;
    live_intervention_.reset();
}

// Applies one swap of the open live intervention (mode 2 only): checks owner, revision, manager,
// team and that the rows still hold the shown slots and players, whether it is a same-zone
// exchange or a field/bench substitution, and the injury status of both players (some notices
// are reported as "source_notice" codes in 'error'). False with 'error' on any failure.
bool CareerSession::dispatch_live_substitution(const ui::LineupRequest& request, int minute,
        int source_mode, MatchEngine& engine, uint64_t& revision, std::string& error) {
    error.clear();
    // PitchScreen stays open for repeated actions; EndPhase rerates once after
    // event 8 closes either live menu.
    if (source_mode != 2 || request.kind != ui::LineupRequestKind::swap_players ||
        (minute <= 2 && (!live_intervention_ || !live_intervention_->source_event))) {
        error = "Der Eingriff ist in dieser Spielminute nicht verfügbar.";
        return false;
    }
    const int active = request.manager_slot;
    const int team = request.runtime_club;
    if (!live_intervention_ || live_intervention_->minute != minute ||
        live_intervention_->source_mode != source_mode || live_intervention_->manager != active ||
        live_intervention_->team != team || live_intervention_->current_revision != revision ||
        request.revision != revision || revision == std::numeric_limits<uint64_t>::max() ||
        active < 0 || active > 3 || request.manager_slot != active || team < 0 ||
        size_t(active) >= g_.manager_teams.size() || g_.manager_teams[size_t(active)] != team ||
        g_.manager_of(team) != active || team >= int(g_.team_flags.size()) ||
        &engine.state != &g_ || &engine.game != &game_ || &engine.rand != &rand_) {
        error = "Der Eingriff ist veraltet oder gehört nicht zum aktiven Manager.";
        return false;
    }
    const bool current_match = std::any_of(engine.groups.begin(), engine.groups.end(), [this, team](const MatchGroup& group) {
            for (int match = 0; match < group.matches(); ++match) {
                const uint32_t bit = uint32_t(1) << match;
                if ((group.kickoff_void_mask & bit) || (group.void_mask() & bit)) continue;
                const int home = group.fixtures.at(size_t(match) * 2);
                const int away = group.fixtures.at(size_t(match) * 2 + 1);
                if (std::find(g_.live_matches.begin(), g_.live_matches.end(), home) != g_.live_matches.end() &&
                    (team == home || team == away)) return true;
            }
            return false;
        });
    if (!current_match) {
        error = "Die Mannschaft spielt in der aktuellen Partie nicht.";
        return false;
    }
    const auto range = squad_range(team);
    const bool first_field = request.first_row >= 0 && request.first_row < 11;
    const bool second_field = request.second_row >= 0 && request.second_row < 11;
    const bool same_zone_exchange = first_field == second_field;
    if (request.first_row < 0 || request.second_row < 0 ||
        size_t(request.first_row) >= range.count || size_t(request.second_row) >= range.count ||
        request.first_row == request.second_row ||
        range.first + range.count > g_.squad_slots.size()) {
        error = "Die Spielerzeilen sind für den Live-Tausch ungültig.";
        return false;
    }
    const uint16_t outgoing = g_.squad_slots[range.first + size_t(request.first_row)];
    const uint16_t incoming = g_.squad_slots[range.first + size_t(request.second_row)];
    const uint16_t same_zone_role = first_field ? 0x8000 : 0x4000;
    if (outgoing != request.first_slot || incoming != request.second_slot ||
        (outgoing & 0x0fff) != request.first_player ||
        (incoming & 0x0fff) != request.second_player ||
        (same_zone_exchange ? ((outgoing & 0xc000) != same_zone_role || (incoming & 0xc000) != same_zone_role) :
            (first_field ? (!(outgoing & 0x8000) || !(incoming & 0x4000)) :
                           (!(incoming & 0x8000) || !(outgoing & 0x4000)))) ||
        request.first_player < 0 || request.second_player < 0) {
        error = "Die Kaderauswahl hat sich inzwischen geändert.";
        return false;
    }
    if (size_t(request.first_player) >= g_.players.size() || size_t(request.second_player) >= g_.players.size()) {
        error = "Die Spielerkennung liegt außerhalb des Kaders.";
        return false;
    }
    const Player& first_player = g_.players.at(size_t(request.first_player));
    const Player& second_player = g_.players.at(size_t(request.second_player));
    const auto injury_status = [](const Player& p) { return int(p.w364 >> 16 & 0x1f); };
    // PitchScreen checks both selected players before it distinguishes same-zone
    // tactical exchanges from field/bench substitutions.
    if (((first_player.w35c >> 27) == 0 && injury_status(first_player) != 0) ||
        ((second_player.w35c >> 27) == 0 && injury_status(second_player) != 0)) {
        error = "source_notice:0xaf13c";
        return false;
    }
    if (!same_zone_exchange) {
        const int field_row = first_field ? request.first_row : request.second_row;
        const int bench_row = first_field ? request.second_row : request.first_row;
        const int field_player = first_field ? request.first_player : request.second_player;
        const int bench_player = first_field ? request.second_player : request.first_player;
        const Player& field = g_.players.at(size_t(field_player));
        const Player& bench = g_.players.at(size_t(bench_player));
        const int field_status = int(field.w364 >> 16 & 0x1f);
        const int bench_status = int(bench.w364 >> 16 & 0x1f);
        if (field_status != 0 && (field.w35c >> 27) == 0) {
            error = "source_notice:0xaf140";
            return false;
        }
        if (bench_status != 0) {
            error = "source_notice:0xaf144";
            return false;
        }
        const int used = int(g_.team_flags.at(size_t(team)) >> 5 & 7);
        const int max_substitutions = source_runtime_byte(g_,club_modal_runtime_,exe_,::bmh::layout::CareerSession_DispatchLiveSubstitution_ExecutableField);
        if (used >= max_substitutions) {
            error = "source_notice:0xaf150";
            return false;
        }
        const bool foreign_limit = g_.memory.has(::bmh::layout::ForeignPlayerLimitEnabled) ? g_.memory.u8(::bmh::layout::ForeignPlayerLimitEnabled) != 0 : exe_.u8(::bmh::layout::ForeignPlayerLimitEnabled) != 0;
        if (foreign_limit) {
            int foreign_on_field = 0;
            for (size_t row = 0; row < range.count; ++row) {
                const uint16_t slot = g_.squad_slots.at(range.first + row);
                if (!(slot & 0x8000) || (slot & 0x0fff) == 0x0fff) continue;
                if (nationality(g_, slot & 0x0fff) != 0) ++foreign_on_field;
            }
            foreign_on_field -= nationality(g_, field_player) != 0;
            foreign_on_field += nationality(g_, bench_player) != 0;
            if (foreign_on_field >= 4) {
                error = "source_notice:0xaf148";
                return false;
            }
        }
        if (bench.w364 & 0x1000) {
            error = "source_notice:0xaf14c";
            return false;
        }
    }

    // Source updates the squad before leaving the child menu; BuildSideRecords3 belongs
    // to the single parent-menu completion callback.
    GameState staged_state = g_;
    if (same_zone_exchange) {
        const size_t first = range.first + size_t(request.first_row);
        const size_t second = range.first + size_t(request.second_row);
        const size_t board = size_t(active) * 0x840;
        if (board + (size_t(std::max(request.first_row, request.second_row)) + 1) * 0x58 >
            staged_state.tactics.size()) {
            error = "Die taktischen Spielerdaten sind unvollständig.";
            return false;
        }
        auto* a = staged_state.tactics.data() + board + size_t(request.first_row) * 0x58;
        auto* b = staged_state.tactics.data() + board + size_t(request.second_row) * 0x58;
        const uint8_t ax = a[0x41], ay = a[0x42], bx = b[0x41], by = b[0x42];
        staged_state.squad_slots[first] = uint16_t((incoming & 0x1fff) | (outgoing & 0xc000));
        staged_state.squad_slots[second] = uint16_t((outgoing & 0x1fff) | (incoming & 0xc000));
        std::swap_ranges(a, a + 0x58, b);
        a[0x41] = ax; a[0x42] = ay; b[0x41] = bx; b[0x42] = by;
        // GatedByParamLive (the desk-card remap) is gated by param_2==0; a live
        // marked exchange changes the selected rows but leaves player_cards alone.
    } else {
        const int field_row = first_field ? request.first_row : request.second_row;
        const int bench_row = first_field ? request.second_row : request.first_row;
        const auto staged_range = squad_range(team);
        const size_t field_at = staged_range.first + size_t(field_row);
        const size_t bench_at = staged_range.first + size_t(bench_row);
        const uint16_t field_slot = staged_state.squad_slots.at(field_at);
        const uint16_t bench_slot = staged_state.squad_slots.at(bench_at);
        const int used = int(staged_state.team_flags.at(size_t(team)) >> 5 & 7);
        if (!(field_slot & 0x8000) || !(bench_slot & 0x4000) ||
            (bench_slot & 0x0fff) == 0x0fff || used >= source_runtime_byte(g_,club_modal_runtime_,exe_,::bmh::layout::CareerSession_DispatchLiveSubstitution_ExecutableField) ||
            (staged_state.players.at(bench_slot & 0x0fff).w364 & 0x1000)) {
            error = "Die Auswechslung erfüllt die Source-Bedingungen nicht.";
            return false;
        }
        const size_t first = std::min(field_at, bench_at), second = std::max(field_at, bench_at);
        const size_t board = size_t(active) * 0x840;
        if (board + (size_t(bench_row) + 1) * 0x58 > staged_state.tactics.size()) {
            error = "Die taktischen Spielerdaten sind unvollständig.";
            return false;
        }
        auto* a = staged_state.tactics.data() + board + size_t(field_row) * 0x58;
        auto* b = staged_state.tactics.data() + board + size_t(bench_row) * 0x58;
        const uint8_t fx = a[0x41], fy = a[0x42], bx = b[0x41], by = b[0x42];
        b[0] = uint8_t(minute); // EndPhase passes minute + 1 as PitchScreen param_2; source stores param_2 - 1.
        staged_state.squad_slots[first] = bench_slot;
        staged_state.squad_slots[second] = field_slot;
        std::swap_ranges(a, a + 0x58, b);
        a[0x41] = fx; a[0x42] = fy; b[0x41] = bx; b[0x42] = by;
        staged_state.squad_slots[bench_at] &= 0x1fff;
        staged_state.squad_slots[field_at] &= 0x1fff;
        staged_state.players.at(bench_slot & 0x0fff).w364 |= 0x1000;
        staged_state.squad_slots[field_at] |= 0x8000;
        staged_state.squad_slots[bench_at] |= 0x4000;
        staged_state.team_flags.at(size_t(team)) = uint16_t(
            (staged_state.team_flags.at(size_t(team)) & 0xff1f) | uint16_t(used + 1) << 5);
    }
    g_ = std::move(staged_state);
    ++revision;
    live_intervention_->current_revision = revision;
    return true;
}

// Wraps dispatch_live_substitution: applied, a "source_notice" status carrying the original
// notice address parsed from the error text, or rejected with the error text.
LiveSubstitutionResult CareerSession::dispatch_live_substitution_result(
        const ui::LineupRequest& request, int minute, int source_mode,
        MatchEngine& engine, uint64_t& revision) {
    std::string error;
    if (dispatch_live_substitution(request, minute, source_mode, engine, revision, error))
        return {LiveSubstitutionStatus::applied, 0, request.manager_slot, {}};
    constexpr std::string_view prefix = "source_notice:";
    if (error.compare(0, prefix.size(), prefix) == 0) {
        uint32_t address = 0;
        try { address = uint32_t(std::stoul(error.substr(prefix.size()), nullptr, 16)); }
        catch (...) { return {LiveSubstitutionStatus::rejected, 0, request.manager_slot,
            "Invalid internal Source notice address."}; }
        return {LiveSubstitutionStatus::source_notice, address, request.manager_slot, {}};
    }
    return {LiveSubstitutionStatus::rejected, 0, request.manager_slot, std::move(error)};
}

// Sets one tactic option during the open live intervention (source mode 8, after minute 2): the
// intervention owner, revision, manager, team and current match must match, the value must be in
// range and available. Only the manager record bytes change; revision plus one and the open
// intervention's current revision follows. False with a German 'error' text otherwise.
bool CareerSession::dispatch_live_tactic(const ui::TacticsRequest& request, int minute,
        int source_mode, MatchEngine& engine, uint64_t& revision, std::string& error) {
    error.clear();
    if (minute <= 2 || source_mode != 8 || request.kind != ui::TacticsRequestKind::set_tactic) {
        error = "Die Taktikänderung ist in dieser Spielminute nicht verfügbar.";
        return false;
    }
    const int manager = request.manager_slot;
    const int team = request.runtime_club;
    if (!live_intervention_ || live_intervention_->minute != minute ||
        live_intervention_->source_mode != source_mode || live_intervention_->manager != manager ||
        live_intervention_->team != team || live_intervention_->current_revision != revision ||
        request.revision != revision || revision == std::numeric_limits<uint64_t>::max() ||
        manager < 0 || manager >= int(g_.manager_teams.size()) || team < 0 ||
        team >= int(g_.team_flags.size()) || team != g_.manager_teams[size_t(manager)] ||
        g_.manager_of(team) != manager || &engine.state != &g_ || &engine.game != &game_ || &engine.rand != &rand_) {
        error = "Die Taktikanfrage ist veraltet oder gehört nicht zum aktiven Manager.";
        return false;
    }
    bool selected_match = false;
    for (const MatchGroup& group : engine.groups) {
        for (int match = 0; match < group.matches(); ++match) {
            const uint32_t bit = uint32_t(1) << match;
            if ((group.kickoff_void_mask & bit) || (group.void_mask() & bit)) continue;
            const int home = group.fixtures.at(size_t(match) * 2);
            const int away = group.fixtures.at(size_t(match) * 2 + 1);
            if (std::find(g_.live_matches.begin(), g_.live_matches.end(), home) != g_.live_matches.end() &&
                (team == home || team == away)) selected_match = true;
        }
    }
    if (!selected_match) {
        error = "Die Mannschaft spielt nicht in der aktuell ausgewählten Partie.";
        return false;
    }
    if (request.index < 0 || request.index >= 8 || request.desired_value < 0 ||
        request.desired_value > exe_.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField + request.index)) {
        error = "Ungültige Taktikeinstellung.";
        return false;
    }
    const bool live_option = (g_.memory.has(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3) ? g_.memory.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3) : exe_.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3)) != 0;
    if (live_option && exe_.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField2 + request.index) == 0) {
        error = "Diese Taktikeinstellung ist nicht verfügbar.";
        return false;
    }
    if ((request.index == 0 && request.desired_assistant_attitude != std::optional<uint8_t>(0)) ||
        (request.index != 0 && request.desired_assistant_attitude)) {
        error = "Ungültige Assistenteneinstellung.";
        return false;
    }
    const size_t record = size_t(manager) * kManagerRecordSize;
    const size_t option = size_t(manager) * 5 + 2;
    if (g_.manager_records.size() < record + 0xb0c || g_.manager_options.size() <= option) {
        error = "Die Taktikdaten sind unvollständig.";
        return false;
    }
    // RerateTeam commits each click to the manager record while the menu stays
    // open; EndPhase invokes BuildSideRecords3 exactly once after menu exit.
    g_.manager_records[record + 0xd2 + size_t(request.index)] = uint8_t(request.desired_value);
    if (request.index == 0) g_.manager_options[option] = 0;
    ++revision;
    live_intervention_->current_revision = revision;
    return true;
}

// Pitch (lineup screen) group: all pitch actions (swap, move player, weights, formations,
// set piece editor and takers, player orders, marking, page switches). A swap is forwarded to
// dispatch_lineup. Everything else is checked against the display word, the saved set piece
// signature and the expected old values in the request, applied on a staged copy of the state
// and committed with a revision bump. False with a German 'error' text or a short code otherwise.
bool CareerSession::dispatch_pitch(const ui::PitchRequest& request, uint64_t& revision,
                                    std::string& error) {
    if (request.kind == ui::PitchRequestKind::swap_players) {
        if (request.runtime_club < 0 || request.runtime_club >= int(g_.team_flags.size()) ||
            size_t(request.runtime_club + 1) * 2 > g_.club_heap_78.size() ||
            request.indoor != g_.indoor) {
            error = "Der Hallen-/Außenmodus der Spielfeldanfrage ist veraltet.";
            return false;
        }
        ui::LineupRequest lineup;
        lineup.kind = ui::LineupRequestKind::swap_players;
        lineup.manager_slot = request.manager_slot; lineup.runtime_club = request.runtime_club;
        lineup.revision = request.revision;
        lineup.first_row = request.row; lineup.first_player = request.player_id; lineup.first_slot = request.slot;
        lineup.second_row = request.second_row; lineup.second_player = request.second_player;
        lineup.second_slot = request.second_slot;
        return dispatch_lineup(lineup, revision, error);
    }
    if (request.kind != ui::PitchRequestKind::move_player &&
        request.kind != ui::PitchRequestKind::set_automatic_weight &&
        request.kind != ui::PitchRequestKind::apply_formation &&
        request.kind != ui::PitchRequestKind::save_formation &&
        request.kind != ui::PitchRequestKind::open_set_piece_editor &&
        request.kind != ui::PitchRequestKind::close_set_piece_editor &&
        request.kind != ui::PitchRequestKind::set_piece_taker &&
        request.kind != ui::PitchRequestKind::auto_set_piece_takers &&
        request.kind != ui::PitchRequestKind::set_player_order &&
        request.kind != ui::PitchRequestKind::select_pitch_page &&
        request.kind != ui::PitchRequestKind::set_player_marking) {
        error = "Diese Spielfeldaktion ist noch nicht verfügbar.";
        return false;
    }
    // PitchRequest has runtime ownership and a revision, not a caller-supplied
    // stable-club token. Cross-check the live mapping and record just as for the
    // semantic desk setters, including their tracked phase/modal exclusions.
    const int count = g_.memory.has(::bmh::layout::ManagerCount) ? int(g_.memory.u8(::bmh::layout::ManagerCount)) : -1;
    if (count < 1 || count > 4 || g_.manager_teams.size() != size_t(count) ||
        request.manager_slot < 0 || request.manager_slot >= count) {
        error = "Die Managerdaten der Spielfeldaktion sind unvollständig.";
        return false;
    }
    DeskSettingIdentity identity{request.manager_slot, request.runtime_club, std::nullopt, request.revision};
    if (request.runtime_club >= 0 && size_t(request.runtime_club) < g_.club_ids.size())
        identity.stable_club_identity = int(g_.club_ids[size_t(request.runtime_club)]);
    // PitchScreen exposes the prepared Pitch view during initial setup as well as
    // during manager desks. A verified line-up owner in the still-empty turn loop
    // is sufficient authority there; ordinary automation/ticket settings remain
    // desk-only. Keep all nested desk mutations blocked while allowing this Source
    // line-up window before the first desk has been created.
    // True when the request belongs to the open live lineup intervention (same manager, team and
    // revision); such requests are authorised by that owner instead of by the desk phase.
    const bool live_context = live_intervention_ && live_intervention_->source_mode == 2 &&
        request.manager_slot == live_intervention_->manager &&
        request.runtime_club == live_intervention_->team &&
        request.revision == live_intervention_->current_revision && request.revision == revision;
    const bool desk_phase = turn_driver_.stage == TurnDriver::Stage::MatchdayDesks ||
        turn_driver_.stage == TurnDriver::Stage::SeasonEndDesk;
    // True during the very first lineup window before the first desk exists (driver stage Empty).
    const bool initial_lineup_phase = turn_driver_.stage == TurnDriver::Stage::Empty &&
        lineup_entry_manager_ == request.manager_slot && lineup_entry_club_ == request.runtime_club;
    if (!desk_phase && !initial_lineup_phase && !live_context) { error = "desk_setting_unavailable"; return false; }
    if (!live_context && (pending_offer_dispatch_active_ || market_dispatch_active_ || calendar_advance_active_ ||
        transfer_offer_editor_ || manager_join_ || contract_quote_ || stadium_.investment ||
        stadium_.tip || stadium_.periphery_quote)) {
        error = "desk_setting_unavailable"; return false;
    }
    if (live_context) {
        const int manager = live_intervention_->manager;
        const int team = live_intervention_->team;
        const size_t rec = size_t(manager) * kManagerRecordSize;
        if (request.indoor != g_.indoor || manager < 0 || manager >= int(g_.manager_teams.size()) ||
            g_.manager_teams[size_t(manager)] != team || g_.manager_of(team) != manager ||
            size_t(team) >= g_.club_ids.size() || rec + 0xb0c > g_.manager_records.size() ||
            (int(g_.manager_records[rec + 0x290]) | int(g_.manager_records[rec + 0x291]) << 8) != team) {
            error = "Die Spielfeldaktion gehört nicht zum aktiven Live-Manager."; return false;
        }
    } else if (!validate_desk_setting_owner(identity, revision, error, false)) return false;
    const size_t badge = size_t(request.runtime_club) * 2;
    const int mode = g_.memory.has(::bmh::layout::GameMode) ? int(g_.memory.u8(::bmh::layout::GameMode)) : int(exe_.u8(::bmh::layout::GameMode));
    if (request.runtime_club >= int(exe_.u16(::bmh::layout::LeagueClubCount)) || g_.lineups_frozen ||
        mode < 1 || mode > 3 || g_.manager_options[size_t(request.manager_slot) * 5] > 8 ||
        request.indoor != g_.indoor ||
        (!live_context && (lineup_entry_manager_ != request.manager_slot ||
        lineup_entry_club_ != request.runtime_club)) || badge + 1 >= g_.club_heap_78.size()) {
        error = "Diese Spielfeldaktion ist nicht für die vorbereitete Aufstellung freigegeben.";
        return false;
    }
    if ((request.kind == ui::PitchRequestKind::apply_formation ||
         request.kind == ui::PitchRequestKind::save_formation) && request.new_weights != request.old_weights) {
        error = "Die Gewichtung der Aufstellungsanfrage wurde geändert.";
        return false;
    }
    const auto selected_live_opponent = [&](int opponent) {
        if (!live_context || opponent < 0 || opponent >= int(exe_.u16(::bmh::layout::LeagueClubCount))) return false;
        return std::any_of(live_intervention_->groups.begin(), live_intervention_->groups.end(),
            [&](const MatchGroup& group) {
                for (int match = 0; match < group.matches(); ++match) {
                    const uint32_t bit = uint32_t(1) << match;
                    if ((group.kickoff_void_mask & bit) || (group.void_mask() & bit)) continue;
                    const int home = group.fixtures.at(size_t(match) * 2);
                    const int away = group.fixtures.at(size_t(match) * 2 + 1);
                    if (std::find(g_.live_matches.begin(), g_.live_matches.end(), home) != g_.live_matches.end() &&
                        ((home == request.runtime_club && away == opponent) ||
                         (away == request.runtime_club && home == opponent))) return true;
                }
                return false;
            });
    };
    const auto selected_source_opponent = [&](int opponent) {
        if (live_context) return selected_live_opponent(opponent);
        const auto prepared = prepared_lineup_opponent(request.manager_slot,
            request.runtime_club, request.revision);
        return prepared && *prepared == opponent;
    };
    if (request.kind == ui::PitchRequestKind::select_pitch_page && request.page_control == 4 &&
        (request.indoor || g_.indoor || !selected_source_opponent(request.opponent_team))) {
        error = "Die Manndeckungsseite ist für diese Partie nicht verfügbar.";
        return false;
    }
    if (request.kind == ui::PitchRequestKind::set_player_marking &&
        (request.indoor || g_.indoor || !selected_source_opponent(request.opponent_team))) {
        error = "Die Manndeckung gehört nicht zur aktuellen Außenspielpartie.";
        return false;
    }
    try {
        GameState staged = g_;
        bool accepted = false;
        const size_t manager_rec = size_t(request.manager_slot) * kManagerRecordSize;
        static constexpr size_t set_piece_offsets[] = {0xafc,0xafe,0xb08};
        std::array<uint16_t,3> current_set_piece{};
        for(size_t i=0;i<3;++i){const size_t at=manager_rec+set_piece_offsets[i];
            current_set_piece[i]=uint16_t(staged.manager_records.at(at)|uint16_t(staged.manager_records.at(at+1))<<8);}
        const uint32_t display_at=uint32_t(::bmh::layout::CareerSession_AnyOf_ExecutableField+request.manager_slot*2);
        const bool editor_action=request.kind==ui::PitchRequestKind::open_set_piece_editor||
            request.kind==ui::PitchRequestKind::close_set_piece_editor||
            request.kind==ui::PitchRequestKind::set_piece_taker||
            request.kind==ui::PitchRequestKind::auto_set_piece_takers;
        if(request.kind==ui::PitchRequestKind::select_pitch_page&&
           (!staged.memory.has(display_at)||request.expected_display_word!=staged.memory.u16(display_at)||
            request.expected_set_piece_values!=current_set_piece||
            request.expected_set_piece_signature!=uint16_t(staged.manager_records.at(manager_rec+0xb0a)|uint16_t(staged.manager_records.at(manager_rec+0xb0b))<<8)||
            request.expected_mode!=mode||request.indoor!=staged.indoor||request.page_control<0||request.page_control>4)){
            error="Der Seitenwechsel am Spielfeld ist veraltet oder ungültig.";return false;
        }
        if (request.kind == ui::PitchRequestKind::set_player_marking) {
            if (!staged.memory.has(display_at) ||
                request.expected_display_word != staged.memory.u16(display_at) ||
                (request.expected_display_word & 0x0040) == 0 || staged.indoor ||
                request.opponent_team < 0 || !selected_source_opponent(request.opponent_team) ||
                size_t(request.opponent_team) >= staged.team_flags.size()) {
                error = "Die Manndeckungsanfrage ist veraltet oder nicht auf der Source-Seite aktiv.";
                return false;
            }
            const SquadRange own = squad_range(request.runtime_club);
            const SquadRange opponent = squad_range(request.opponent_team);
            const size_t own_board = size_t(request.manager_slot) * 0x840;
            if (own.count < 11 || opponent.count < 11 || own.first + 11 > staged.squad_slots.size() ||
                opponent.first + 11 > staged.squad_slots.size() || own_board + 11 * 0x58 > staged.tactics.size()) {
                error = "Die Mannschaftstaktik enthält keine vollständigen Spielerzeilen.";
                return false;
            }
            if (request.opponent_row < 1 || request.opponent_row >= 11 ||
                staged.squad_slots.at(opponent.first + size_t(request.opponent_row)) != request.opponent_slot ||
                (request.opponent_slot & 0xc000) != 0x8000 ||
                (request.opponent_slot & 0x0fff) != request.opponent_player ||
                request.opponent_player < 0 || size_t(request.opponent_player) >= staged.players.size()) {
                error = "Der ausgewählte Gegenspieler gehört nicht mehr zur gegnerischen Feldaufstellung.";
                return false;
            }
            bool clear = request.row == -1 && request.player_id == -1;
            if (clear) {
                if (request.expected_marking_player_id != uint16_t(request.opponent_player)) {
                    error = "Die zu löschende Manndeckung ist veraltet.";
                    return false;
                }
            } else {
                if (request.row < 0 || request.row >= 11 ||
                    staged.squad_slots.at(own.first + size_t(request.row)) != request.slot ||
                    (request.slot & 0xc000) != 0x8000 || (request.slot & 0x0fff) != request.player_id ||
                    request.player_id < 0 || size_t(request.player_id) >= staged.players.size()) {
                    error = "Der ausgewählte eigene Feldspieler gehört nicht mehr zur Aufstellung.";
                    return false;
                }
                const size_t own_row_at = own_board + size_t(request.row) * 0x58;
                const uint16_t current = uint16_t(staged.tactics.at(own_row_at + 0x44) |
                    uint16_t(staged.tactics.at(own_row_at + 0x45)) << 8);
                if (current != request.expected_marking_player_id) {
                    error = "Die bestehende Manndeckung wurde inzwischen geändert.";
                    return false;
                }
            }
        }
        if(editor_action && (!staged.memory.has(display_at)||request.expected_set_piece_values!=current_set_piece||
           request.expected_display_word!=staged.memory.u16(display_at)||request.expected_mode!=mode||request.indoor!=staged.indoor||
           request.expected_set_piece_signature!=uint16_t(staged.manager_records.at(manager_rec+0xb0a)|uint16_t(staged.manager_records.at(manager_rec+0xb0b))<<8)||
           uint16_t(staged.manager_records.at(manager_rec+0xb0a)|uint16_t(staged.manager_records.at(manager_rec+0xb0b))<<8)!=0x3253)){
            error="Der Schützenauftrag ist veraltet oder unvollständig.";return false;
        }
        const bool editor_open=staged.memory.has(display_at)&&(staged.memory.u16(display_at)&0x80)!=0;
        // The low-80 native bit survives Padded when the whole Pitch view
        // returns. Screen-local editor runtime is reset on each new view, so a
        // Source Schütze entry is valid even when the retained word already has
        // bit80; mutations and editor-page exits still require that active bit.
        if((request.kind==ui::PitchRequestKind::close_set_piece_editor||
            request.kind==ui::PitchRequestKind::set_piece_taker||
            request.kind==ui::PitchRequestKind::auto_set_piece_takers)&&!editor_open){
            error="Der Schützenauftrag passt nicht zum aktuellen Anzeigestatus.";return false;
        }
        if(request.kind==ui::PitchRequestKind::set_player_order){
            if(request.expected_mode!=mode||request.expected_display_word!=staged.memory.u16(display_at)||
               request.expected_set_piece_values!=current_set_piece||
               request.expected_set_piece_signature!=uint16_t(staged.manager_records.at(manager_rec+0xb0a)|uint16_t(staged.manager_records.at(manager_rec+0xb0b))<<8)||
               request.row<0||request.row>=11||request.order_aspect<0||request.order_aspect>3||
               mode==1||!staged.memory.has(display_at)||(staged.memory.u16(display_at)&0x0070)!=0x0020||
               request.new_order_value<2||request.new_order_value>40||
               request.new_order_value==request.expected_order_value){
                error="Der Anweisungsauftrag ist ungültig.";return false;
            }
            const size_t row_at=size_t(request.manager_slot)*0x840+size_t(request.row)*0x58;
            if(row_at+6>staged.tactics.size()||
               (staged.squad_slots.at(squad_range(request.runtime_club).first+size_t(request.row))!=request.slot)||
               (request.slot&0x0fff)!=request.player_id||
               staged.tactics[row_at+2+size_t(request.order_aspect)]!=request.expected_order_value){
                error="Die Anweisungszeile wurde inzwischen geändert.";return false;
            }
        }
        switch (request.kind) {
        case ui::PitchRequestKind::set_automatic_weight:
            accepted = set_pitch_automatic_weight_source(staged, request.manager_slot, request.runtime_club,
                request.weight_index, request.old_weights, request.new_weights);
            break;
        case ui::PitchRequestKind::move_player:
            accepted = move_pitch_player_source(staged, request.manager_slot, request.runtime_club,
                request.row, request.player_id, request.slot, request.old_depth, request.old_lane,
                request.new_depth, request.new_lane);
            break;
        case ui::PitchRequestKind::apply_formation:
            accepted = apply_pitch_formation_source(staged, request.manager_slot, request.runtime_club,
                request.formation, request.old_weights);
            break;
        case ui::PitchRequestKind::save_formation:
            accepted = save_pitch_formation_source(staged, request.manager_slot, request.runtime_club,
                request.formation, request.old_weights, request.formation_positions);
            break;
        case ui::PitchRequestKind::open_set_piece_editor: {
            const uint16_t expected=uint16_t((request.expected_display_word&0xff00)|
                ((request.expected_display_word&0x008f)|0x0080));
            accepted=request.new_display_word==expected&&
                normalize_pitch_set_piece_source(staged,request.manager_slot,request.runtime_club);
            if(accepted)staged.memory.set16(display_at,expected);
            break;
        }
        case ui::PitchRequestKind::close_set_piece_editor: {
            const uint16_t expected=uint16_t(request.expected_display_word&~uint16_t(0x80));
            accepted=(request.expected_display_word&0x80)!=0&&request.new_display_word==expected;
            if(accepted)staged.memory.set16(display_at,expected);
            break;
        }
        case ui::PitchRequestKind::set_piece_taker:
            accepted=request.set_piece_selector>=2&&request.set_piece_selector<=4&&
                request.expected_set_piece_value==current_set_piece[size_t(request.set_piece_selector-2)]&&
                set_pitch_set_piece_taker_source(staged,request.manager_slot,request.runtime_club,
                    request.set_piece_selector,request.row,request.player_id,request.slot,request.expected_set_piece_value);
            break;
        case ui::PitchRequestKind::auto_set_piece_takers:
            accepted=auto_pitch_set_piece_takers_source(staged,request.manager_slot,request.runtime_club,
                request.expected_set_piece_values);
            break;
        case ui::PitchRequestKind::set_player_order:
            accepted=set_player_order_source(staged,request.manager_slot,request.runtime_club,request.row,
                request.player_id,request.order_aspect,request.new_order_value);
            break;
        case ui::PitchRequestKind::set_player_marking: {
            const size_t board = size_t(request.manager_slot) * 0x840;
            for (size_t row = 0; row < 11; ++row) {
                const size_t at = board + row * 0x58 + 0x44;
                const uint16_t marker = uint16_t(staged.tactics.at(at) | uint16_t(staged.tactics.at(at + 1)) << 8);
                if (marker == uint16_t(request.opponent_player)) {
                    staged.tactics[at] = 0xff;
                    staged.tactics[at + 1] = 0x0f;
                }
            }
            if (request.row >= 0) {
                const size_t at = board + size_t(request.row) * 0x58 + 0x44;
                staged.tactics[at] = uint8_t(request.opponent_player);
                staged.tactics[at + 1] = uint8_t(uint16_t(request.opponent_player) >> 8);
            }
            accepted = true;
            break;
        }
        case ui::PitchRequestKind::select_pitch_page: {
            const uint16_t old=request.expected_display_word;
            uint16_t next=old;
            switch(request.page_control){
            case 0: next=uint16_t((old&~uint16_t(0x80))^0x0003); break; // Data toggles its source word.
            case 1: next=uint16_t(old^0x000c); break; // Team is the original inner-loop toggle.
            case 2: next=uint16_t(((old&~uint16_t(0x0080))&0x008f)|0x0010); break;
            case 3: next=uint16_t(((old&~uint16_t(0x0080))&0x008f)|0x0020); break;
            case 4: next=uint16_t(((old&~uint16_t(0x0080))&0x008f)|0x0040); break;
            default: break;
            }
            accepted=staged.memory.has(display_at)&&request.new_display_word==next&&
                !((request.page_control==3)&&mode==1);
            if(accepted)staged.memory.set16(display_at,next);
            break;
        }
        default: break;
        }
        if (!accepted) {
            error = "Die Spielfeldaktion ist veraltet oder in dieser Aufstellung nicht verfügbar.";
            return false;
        }
        g_ = std::move(staged);
    } catch (const std::exception&) {
        // Keep pitch requests transactional if a malformed native projection throws.
        error = "Diese Aufstellung kann im aktuellen Spielmodus noch nicht übernommen werden.";
        return false;
    }
    ++revision;
    if (live_context) live_intervention_->current_revision = revision;
    error.clear();
    return true;
}

// Pitch request during a live intervention: needs the matching open intervention (mode 2) and
// current revision. Formation and weight controls exist only in the initial preparation and are
// refused here; the rest goes to dispatch_pitch.
bool CareerSession::dispatch_live_pitch(const ui::PitchRequest& request, int minute, int source_mode,
        MatchEngine& engine, uint64_t& revision, std::string& error) {
    if (!live_intervention_ || live_intervention_->minute != minute ||
        live_intervention_->source_mode != source_mode || source_mode != 2 ||
        live_intervention_->manager != request.manager_slot ||
        live_intervention_->team != request.runtime_club ||
        live_intervention_->current_revision != revision || request.revision != revision ||
        &engine.state != &g_ || &engine.game != &game_ || &engine.rand != &rand_) {
        error = "Die Spielfeldaktion gehört zu keinem aktiven Live-Menü.";
        return false;
    }
    if (request.kind == ui::PitchRequestKind::swap_players) {
        ui::LineupRequest swap;
        swap.kind = ui::LineupRequestKind::swap_players;
        swap.manager_slot = request.manager_slot; swap.runtime_club = request.runtime_club;
        swap.revision = request.revision;
        swap.first_row = request.row; swap.first_player = request.player_id; swap.first_slot = request.slot;
        swap.second_row = request.second_row; swap.second_player = request.second_player;
        swap.second_slot = request.second_slot;
        return dispatch_live_substitution(swap, minute, source_mode, engine, revision, error);
    }
    // PitchScreen's live param_2 is nonzero: these formation and weight controls
    // exist only in its param_2==0 initial-preparation path.
    if (request.kind == ui::PitchRequestKind::set_automatic_weight ||
        request.kind == ui::PitchRequestKind::apply_formation ||
        request.kind == ui::PitchRequestKind::save_formation) {
        error = "Diese Aufstellungsoption ist außerhalb der Vorbereitungsphase nicht verfügbar.";
        return false;
    }
    return dispatch_pitch(request, revision, error);
}

// Result-style wrapper: swaps return the substitution result (with source notices), other pitch
// actions return applied or rejected with the error text.
LiveSubstitutionResult CareerSession::dispatch_live_pitch_result(const ui::PitchRequest& request,
        int minute, int source_mode, MatchEngine& engine, uint64_t& revision) {
    if (request.kind == ui::PitchRequestKind::swap_players) {
        ui::LineupRequest swap;
        swap.kind = ui::LineupRequestKind::swap_players;
        swap.manager_slot = request.manager_slot; swap.runtime_club = request.runtime_club;
        swap.revision = request.revision;
        swap.first_row = request.row; swap.first_player = request.player_id; swap.first_slot = request.slot;
        swap.second_row = request.second_row; swap.second_player = request.second_player;
        swap.second_slot = request.second_slot;
        return dispatch_live_substitution_result(swap, minute, source_mode, engine, revision);
    }
    std::string error;
    const bool applied = dispatch_live_pitch(request, minute, source_mode, engine, revision, error);
    return {applied ? LiveSubstitutionStatus::applied : LiveSubstitutionStatus::rejected,
        0, request.manager_slot, std::move(error)};
}

// Desk-time swap only; any other pitch kind is refused.
bool CareerSession::dispatch_pitch_swap(const ui::PitchRequest& request, uint64_t& revision,
                                        std::string& error) {
    if (request.kind != ui::PitchRequestKind::swap_players) {
        error = "Diese Spielfeldaktion ist kein Spielerwechsel.";
        return false;
    }
    return dispatch_pitch(request, revision, error);
}

// Preparation (match day) projection of the live state at 'revision'; read-only.
ui::PreparationState CareerSession::preparation_projection(uint64_t revision) const {
    return preparation_projection(g_, revision);
}

// Returns the opponent team of the manager's prepared lineup entry (from the captured desk call
// arguments, confirmed against the desk projection), or nothing when the manager is not the
// active prepared one, the day has no ordinary opponent or any check fails. Read-only.
std::optional<int> CareerSession::prepared_lineup_opponent(int manager_slot, int runtime_club,
                                                            uint64_t revision) const {
    if (turn_driver_.stage != TurnDriver::Stage::MatchdayDesks || g_.indoor ||
        manager_slot < 0 || manager_slot >= int(g_.manager_teams.size()) ||
        manager_slot != lineup_entry_manager_ || runtime_club != lineup_entry_club_ ||
        !g_.memory.has(::bmh::layout::ActiveManager) ||
        int(g_.memory.u8(::bmh::layout::ActiveManager)) != manager_slot ||
        g_.manager_teams[size_t(manager_slot)] != runtime_club ||
        runtime_club == kNoTeam || runtime_club < 0 ||
        runtime_club >= int(exe_.u16(::bmh::layout::LeagueClubCount)) || g_.manager_of(runtime_club) != manager_slot ||
        size_t(manager_slot) >= source_desk_callargs_.size())
        return std::nullopt;

    const ui::DeskMatchCallargs& source = source_desk_callargs_[size_t(manager_slot)];
    const int opponent = source.opponent;
    if (opponent < 0 || opponent == runtime_club || opponent >= int(exe_.u16(::bmh::layout::LeagueClubCount)) ||
        source.competition == 32 || source.competition == 33 || source.competition == 34)
        return std::nullopt;
    uint32_t competitions = day_competitions(g_, turn_driver_.calendar_entry);
    if (competitions == 0x800000) competitions = 0;
    const int default_competition = competitions != 0 ? 34 : 33;
    GameState manager_view = g_;
    manager_view.memory.set8(::bmh::layout::ActiveManager, uint8_t(manager_slot));
    const ui::DeskState projected = ui::desk_from_game(manager_view, exe_, revision,
        &turn_driver_.groups, default_competition);
    if (projected.manager_slot != manager_slot || projected.runtime_club != runtime_club ||
        !projected.next_opponent || *projected.next_opponent != opponent ||
        !projected.next_competition || *projected.next_competition != source.competition)
        return std::nullopt;
    const SquadRange rival_roster = squad_range(opponent);
    if (rival_roster.count < 11 || rival_roster.first + rival_roster.count > g_.squad_slots.size())
        return std::nullopt;

    // The captured DayCompetitions2 callargs already encode Source's group order and
    // overwrite rules. Confirm that exact pair still exists in this Matchday;
    // never substitute another fixture if it has become void or unsupported.
    bool current_pair = false;
    for (const MatchGroup& group : turn_driver_.groups) {
        if (group.fixtures.size() < size_t(group.matches()) * 2) continue;
        const int type = group.desc.empty() ? -1 : group.desc.at(0);
        int mapped_competition = -1, league = -1;
        if (type == 0) {
            if (group.desc.size() < 0xd0) continue;
            league = int(le32(group.desc, 0xcc));
            if (league >= 0 && league < 9)
                mapped_competition = int(exe_.u8(::bmh::layout::LeagueCalendarBits + uint32_t(league)));
        } else if (type > 0) {
            mapped_competition = type + 4;
        }
        for (int match = 0; match < group.matches(); ++match) {
            const int home = group.fixtures.at(size_t(match) * 2);
            const int away = group.fixtures.at(size_t(match) * 2 + 1);
            if (!((home == runtime_club && away == opponent) ||
                  (away == runtime_club && home == opponent))) continue;
            bool valid = mapped_competition == source.competition;
            const uint32_t bit = uint32_t(1) << match;
            valid = valid && !(group.kickoff_void_mask & bit) && !(group.void_mask() & bit);
            if (valid && type == 0) {
                bool has_result = false;
                uint8_t raw_result = 0xcc;
                if (size_t(match) < group.results.size()) {
                    has_result = true;
                    raw_result = group.results[size_t(match)];
                } else if (league >= 0 && league < int(g_.league_rounds.size())) {
                    const int round = int(g_.league_rounds[size_t(league)]);
                    const size_t result_at = size_t(league) * kLeagueResultsStride + size_t(round) * 10 + size_t(match);
                    if (round >= 0 && result_at < g_.results.size()) {
                        has_result = true;
                        raw_result = g_.results[result_at];
                    }
                }
                if (has_result && raw_result != 0xcc) valid = false;
            }
            // MatchdayNeedsManagerDesks overwrites the per-manager callargs in group order.
            // The last matching pair therefore decides whether this exact
            // callarg is still a supported, unplayed fixture.
            current_pair = valid;
        }
    }
    return current_pair ? std::optional<int>(opponent) : std::nullopt;
}

// Builds the preparation projection of 'state' and layers the session-owned extras on top: the
// desktop, the lineup "entry prepared" flag and pitch extras, the bank notices and the stadium
// page including its open dialogs (construction tip, periphery quote, block expansion editor)
// when the stadium owner belongs to this exact manager, club and revision. Read-only.
// Throws FormatError when the manager's league cannot be found.
ui::PreparationState CareerSession::preparation_projection(const GameState& state, uint64_t revision) const {
    auto result = ui::preparation_from_game(state, exe_, revision);
    result.desktop = desktop_projection(state, revision);
    if (lineup_entry_manager_ == result.lineup.manager_slot &&
        lineup_entry_club_ == result.lineup.runtime_club) {
        result.lineup.entry_prepared = true;
        if (result.pitch) {
            const auto opponent = prepared_lineup_opponent(result.lineup.manager_slot,
                result.lineup.runtime_club, revision);
            if (opponent)
                result.pitch = ui::pitch_extras_from_game(state, exe_, revision, *opponent);
        }
    }
    if (result.bank) {
        result.bank->last_tip = last_stock_tip_;
        result.bank->property_notice = last_property_notice_;
        result.bank->credit_notice = last_credit_notice_;
    }
    if (result.stadium) {
        const int team = result.stadium->runtime_club;
        const int stable = int(state.club_ids.at(size_t(team)));
        const bool owned = stadium_.manager == result.stadium->manager_slot &&
            stadium_.club == team && stadium_.stable_club == stable && stadium_.revision == revision;
        *result.stadium = ui::stadium_from_game(state, exe_, revision, owned ? stadium_.price_percent : 100);
        auto& stadium = *result.stadium;
        stadium.source = result.desktop.source;
        stadium.club_name = result.desktop.club_name;
        stadium.stable_club_identity = stable;
        stadium.plain_background = source_runtime_byte(state, club_modal_runtime_, exe_, ::bmh::layout::PlainBackground) != 0;
        stadium.decorated_stadium_header = !stadium.plain_background;
        stadium.compact_dialog_frame = source_runtime_byte(state, club_modal_runtime_, exe_, ::bmh::layout::CompactDialogFrame) != 0;
        stadium.solid_panel_background = source_runtime_byte(state, club_modal_runtime_, exe_, ::bmh::layout::SolidPanelBackground) != 0;
        stadium.gradient_text = source_runtime_byte(state, club_modal_runtime_, exe_, ::bmh::layout::TextGradientDisabled) == 0 &&
            source_runtime_byte(state, club_modal_runtime_, exe_, ::bmh::layout::TextGradientMode) == 2;
        stadium.source_level = state.level;
        stadium.season_tickets = uint16_t(state.manager_records.at(size_t(stadium.manager_slot) * kManagerRecordSize + 0x2ac)) |
            uint16_t(state.manager_records.at(size_t(stadium.manager_slot) * kManagerRecordSize + 0x2ad)) << 8;
        const int source_team = int16_t(uint16_t(state.manager_records.at(size_t(stadium.manager_slot) * kManagerRecordSize + 0x290)) |
            uint16_t(state.manager_records.at(size_t(stadium.manager_slot) * kManagerRecordSize + 0x291)) << 8);
        const int source_league = source_team > 0x164 ? 0 : team_league(source_team);
        if (source_league < 0 || size_t(source_league) >= state.league_rounds.size())
            throw FormatError("stadium source league is outside the live league table");
        stadium.live_league_round = state.league_rounds.at(size_t(source_league));
        stadium.ticket_refund_rate = std::bit_cast<int32_t>(exe_.u32(::bmh::layout::CareerSession_FormatError_RuntimeField5 + uint32_t(source_league) * 4));
        stadium.investments_enabled = state.memory.u8(::bmh::layout::GameMode) != 1;
        for (size_t i = 0; i < stadium.investments.size(); ++i) {
            stadium.investments[i].original_label = texts_[734 + i];
            if (!stadium.investments_enabled) stadium.investments[i].available = false;
        }
        if (owned) {
            stadium.opened = stadium_.opened;
            stadium.scope = stadium_.scope; stadium.sequence = stadium_.sequence;
            if (stadium_.data_open)
                stadium.data = ui::stadium_data_from_game(state, exe_, stadium.manager_slot, stadium_.data_flags);
            stadium.selected_investment = stadium_.investment;
            if (stadium_.option_index >= 0) stadium.selected_option = stadium_.option_index;
            stadium.investment_text_variant = stadium_.text_variant;
            stadium.action_feedback = stadium_.feedback;
            if (stadium_.tip) {
                PendingOfferQuestion question;
                question.manager = stadium_.manager; question.source_flags = 0x204;
                question.original_message = message_text(state, ::bmh::layout::CareerSession_FormatError_TextPointer8) +
                    format_number(state, stadium_.tip->percent, 1) + message_text(state, ::bmh::layout::CareerSession_FormatError_TextPointer9);
                stadium.construction_tip_dialog = ui::pending_offer_question_dialog(state, question);
                stadium.construction_tip_price = stadium_.tip->offered_price;
            }
            if (stadium_.periphery_quote) {
                stadium.quoted_object = stadium_.periphery_quote->first;
                stadium.quoted_price = stadium_.periphery_quote->second;
                PendingOfferQuestion question;
                question.manager = stadium_.manager;
                question.source_flags = 1; // ConstructionPricePercent -> PresentManagerDialog: ordinary JA/NEIN question.
                question.original_message = message_text(state, ::bmh::layout::CareerSession_FormatError_TextPointer6) +
                    format_number(state, stadium.quoted_price, 0x106) + message_text(state, ::bmh::layout::CareerSession_FormatError_TextPointer7);
                stadium.periphery_quote_dialog = ui::pending_offer_question_dialog(state, question);
                stadium.periphery_quote_dialog->compact_dialog_frame = stadium.compact_dialog_frame;
            }
            if (stadium_.block_expansion) {
                const auto& draft = *stadium_.block_expansion;
                ui::StadiumBlockExpansionEditor editor;
                editor.quote = draft.quote;
                editor.total_target_places = draft.total_target_places;
                editor.desired_features = draft.desired_features;
                editor.header_text_variant = draft.header_text_variant;
                editor.offer = draft.tip;
                const uint32_t current = uint32_t(draft.quote.current_capacity_thousands) * 1000;
                const uint32_t added = draft.total_target_places - current;
                editor.current_places_text = format_number(state, int32_t(current), 5);
                editor.new_places_text = format_number(state, int32_t(added), 5);
                editor.target_places_text = format_number(state, int32_t(draft.total_target_places), 5);
                const auto cost_text = [&](uint32_t amount) {
                    return format_number(state, int32_t(amount), 0x181) + exe_.cstring(::bmh::layout::CareerSession_FormatNumber_ExecutableField);
                };
                editor.per_thousand_cost_text = format_number(state, int32_t(draft.quote.price_per_1000), 0x181);
                editor.new_places_cost_text = cost_text(draft.quote.new_places_cost);
                editor.seats_cost_text = cost_text(draft.quote.seats_cost);
                editor.roof_cost_text = cost_text(draft.quote.roof_cost);
                editor.total_cost_text = cost_text(draft.quote.base_price);
                editor.discount_text = exe_.cstring(::bmh::layout::CareerSession_FormatNumber_TextPointer) +
                    format_number(state, 100 - stadium_.price_percent, 1) + exe_.cstring(::bmh::layout::CareerSession_FormatNumber_ExecutableField2);
                stadium.block_expansion = std::move(editor);
            }
            if (stadium_.block_expansion && stadium_.block_expansion->tip) {
                PendingOfferQuestion question;
                question.manager = stadium_.manager;
                question.source_flags = 0x204;
                question.original_message = message_text(state, ::bmh::layout::CareerSession_FormatError_TextPointer8) +
                    format_number(state, stadium_.block_expansion->tip->percent, 1) +
                    message_text(state, ::bmh::layout::CareerSession_FormatError_TextPointer9);
                stadium.construction_tip_dialog = ui::pending_offer_question_dialog(state, question);
                stadium.construction_tip_dialog->compact_dialog_frame = stadium.compact_dialog_frame;
                stadium.construction_tip_price = stadium_.block_expansion->tip->offered_price;
            }
        }
    }
    return result;
}

}  // namespace bmh::app
