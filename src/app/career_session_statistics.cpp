// SPDX-License-Identifier: GPL-3.0-only
// CareerSession: strengths, top scorers, opportunities, other games, balance plan and league pairings.
// (moved out of career_session.cpp unchanged; the class is declared in app/career_session.hpp)
#include "app/career_session_internal.hpp"

namespace bmh::app {
namespace {

// Fills every rank with 1 and returns 0 unless the first league's round counter is in the
// range [2, matchdays of league 0); inside that range it returns 9 for team ids >= 138 and 0
// for all other teams.
int national_opportunities_rank(const GameState& game,int team,std::array<uint8_t,24>& ranks) {
    ranks.fill(1);
    const int rounds=game.league_rounds.empty()?0:int8_t(game.league_rounds.at(0));
    if(rounds<2||rounds>=league_matchdays(0))return 0;
    return team>=world_layout().league_teams?9:0;
}

// Copies the executable's presentation switches (decorated background, solid panel, text
// gradient, cached backdrop) onto the balance page state.
void apply_balance_source_style(ui::BalanceState& page,const GameState& state,
                                const std::map<uint32_t,uint8_t>& runtime,const LeImage& exe) {
    page.decorated=source_runtime_byte(state,runtime,exe,::bmh::layout::PlainBackground)==0;
    page.original_panel=source_runtime_byte(state,runtime,exe,::bmh::layout::SolidPanelBackground)==0;
    page.gradient=source_runtime_byte(state,runtime,exe,::bmh::layout::TextGradientDisabled)==0 &&
        source_runtime_byte(state,runtime,exe,::bmh::layout::TextGradientMode)==2;
    page.cached_backdrop=source_runtime_u32(state,runtime,exe,::bmh::layout::CareerSession_ApplyBalanceSourceStyle_ExecutableField)!=0;
}

// True when 'team' belongs to one of the human managers or to the national team record of a
// national coach; throws FormatError when the manager count is missing or invalid.
bool other_games_source_manager_owned(const GameState& g,int team) {
    if(team<0||!g.memory.has(::bmh::layout::ManagerCount))
        throw FormatError("other games: missing Source manager count for club detail redraw");
    const size_t count=g.memory.u8(::bmh::layout::ManagerCount);
    if(count>4||count>g.manager_teams.size())
        throw FormatError("other games: invalid Source manager count for club detail redraw");
    for(size_t manager=0;manager<count;++manager)
        if(g.manager_teams[manager]==team)return true;
    constexpr size_t national_record=4 * kManagerRecordSize+0x290;
    if(!g.manager_teams.empty()&&g.manager_teams.front()==kNoTeam&&
       g.manager_records.size()>=national_record+2)
        return int(uint16_t(g.manager_records[national_record]|
               uint16_t(g.manager_records[national_record+1])<<8))==team;
    return false;
}

// Replays the side effects of drawing the other-games page: five ordered team-average
// calculations per displayed club (some of which draw random numbers) and the scratch bytes
// they leave.
void other_games_source_redraw_effects(GameState& g,WatcomRandom& rand,
    std::map<uint32_t,uint8_t>& runtime,const OtherGamesProjection& projection,
    const LeImage& exe) {
    const auto put=[&](uint32_t address,uint8_t value) {
        if(g.memory.has(address))g.memory.set8(address,value);
        else runtime[address]=value;
    };
    constexpr std::array<uint16_t,5> source_flags{0x0f1,0x0f2,0x0f4,0x0f8,0x0ff};
    for(const auto& row:projection.rows) {
        for(const int team:{row.home_team,row.away_team}) {
            // Void6 emits a fixed foreign-strength label without calling QuoteNationalFriendlyMatch
            // for a non-manager international club. All other displayed rows make
            // five ordered Source average calls; overseas manager clubs use W there.
            if(team>=exe.u16(::bmh::layout::NoTeamSquadStart)&&
               !other_games_source_manager_owned(g,team))continue;
            for(const uint16_t flags:source_flags) {
                const auto value=team_average(g,team,flags,&rand);
                // QuoteNationalFriendlyMatch resets/updates CareerSession_FormatError_ExecutableField3 only on the squad-backed branch;
                // TeamRating's foreign-rating branch leaves the previous scratch intact.
                if(team<exe.u16(::bmh::layout::NoTeamSquadStart)||team==kNoTeam) {
                    put(::bmh::layout::CareerSession_FormatError_ExecutableField3,uint8_t(value.best_half_sum));
                    put(::bmh::layout::CareerSession_FormatError_ExecutableField4,uint8_t(value.best_half_sum>>8));
                }
            }
        }
    }
}

// True when two other-games checkpoints agree on state, random generators, runtime bytes,
// selection and owner identity, i.e. nothing observable changed in between.
bool same_other_games_live_baseline(const CareerSession::OtherGamesCheckpoint& before,
                                    const CareerSession::OtherGamesCheckpoint& after) {
    if(save_stream(before.state)!=save_stream(after.state)||
       before.game.seed()!=after.game.seed()||before.game.calls()!=after.game.calls()||
       before.rand.seed()!=after.rand.seed()||before.rand.calls()!=after.rand.calls()||
       before.runtime!=after.runtime||
       before.selection.competition!=after.selection.competition||
       before.selection.linked_page!=after.selection.linked_page||
       before.next_scope!=after.next_scope||before.owner.has_value()!=after.owner.has_value())
        return false;
    if(!before.owner)return true;
    const auto& a=*before.owner;const auto& b=*after.owner;
    return a.page.identity==b.page.identity&&a.desk_owner==b.desk_owner&&
        a.round==b.round&&a.participant_offset==b.participant_offset&&
        a.display_mode==b.display_mode&&a.native==b.native&&a.runtime==b.runtime&&
        a.rand_seed==b.rand_seed&&a.game_seed==b.game_seed&&
        a.rand_calls==b.rand_calls&&a.game_calls==b.game_calls&&
        a.page.source_participant_offset==b.page.source_participant_offset&&
        a.page.projection.competition==b.page.projection.competition&&
        a.page.projection.entry_index==b.page.projection.entry_index&&
        a.page.projection.linked_page==b.page.projection.linked_page&&
        a.page.projection.round==b.page.projection.round&&
        a.page.projection.display_mode==b.page.projection.display_mode;
}

}  // namespace

// Strengths / top scorers / opportunities / other games group: modal desk pages that follow
// open -> dispatch -> acknowledge. Each open_* validates the desk request (menu kind, revision,
// active manager slot, desk stage, no other modal owner) and stores an owner that snapshots the
// native stream and the random counters; each dispatch_* refuses a stale identity or a changed
// snapshot, applies one action on a copy, bumps revision and sequence and returns the new page
// plus the desktop projection. Failures return an empty optional with 'error' and change nothing.
// National-coach desk identity: while a national coach manages the national team the manager's
// team is the national slot (kNoTeam or 0x165); the club identity key is then taken from the
// spare manager record 4 (offset 0x290) instead of the club table.
// Strengths dispatch: one entry point for open, control, league select, group toggle, team info
// and close of the strengths page (also used as a child of the league family).
std::optional<CareerSession::StrengthsDispatch> CareerSession::dispatch_strengths(
    const ui::StrengthsRequest& request,uint64_t& revision,std::string& error,
    const TransferOfferTeamInfoPresenter& team_info,bool family_transition) {
    error.clear();const auto& id=request.identity;const bool opening=request.kind==ui::StrengthsAction::open;
    // The turn driver marks the claimed turn as Failed until it completes (a guard against replaying
    // a half-played turn), so inside the running matchday the stage is not a desk. The Stärken page
    // of the live overview (G key, results page after a phase) belongs to that running turn.
    const bool running_turn=live_overview_owner_.has_value()&&turn_driver_.stage==TurnDriver::Stage::Failed;
    if(league_family_teaminfo_active_||strengths_dispatch_active_||revision==UINT64_MAX||id.revision!=revision||id.sequence==UINT64_MAX||
       id.manager<0||id.manager>3||!g_.memory.has(::bmh::layout::ActiveManager)||g_.memory.u8(::bmh::layout::ActiveManager)!=id.manager||
       size_t(id.manager)>=g_.manager_teams.size()||id.runtime_club!=g_.manager_teams[size_t(id.manager)]||
       id.runtime_club<0||size_t(id.runtime_club)>=g_.club_ids.size()||
       id.club_identity!=int(g_.club_ids[size_t(id.runtime_club)])||id.day_counter!=g_.date||
       id.league<0||id.league>8||career_complete()||
       (turn_driver_.stage!=TurnDriver::Stage::MatchdayDesks&&turn_driver_.stage!=TurnDriver::Stage::SeasonEndDesk&&!running_turn)||
       pending_offer_dispatch_active_||market_dispatch_active_||calendar_advance_active_||league_pairings_draft_||
       league_pairings_dispatch_active_||transfer_offer_editor_||manager_join_||contract_quote_||
       stadium_.investment||stadium_.tip||stadium_.periphery_quote||top_scorers_owner_||top_scorers_dispatch_active_ || stadium_.block_expansion||other_games_owner_||other_games_dispatch_active_) {
        error="strengths: stale or unavailable owner";return {};
    }
    const size_t rec=size_t(id.manager)*kManagerRecordSize;
    const uint32_t setting=::bmh::layout::ManagerSettings+uint32_t(id.manager)*2,family=::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2;
    if(rec+0xb0c>g_.manager_records.size()||
       (int(g_.manager_records[rec+0x290])|int(g_.manager_records[rec+0x291])<<8)!=id.runtime_club||
       !g_.memory.has(setting+1)||!g_.memory.has(family+1)||
       id.expected_settings_word!=g_.memory.u16(setting)||id.expected_family_word!=g_.memory.u16(family)) {
        error="strengths: inconsistent manager or saved settings";return {};
    }
    if(opening) {
        const int initial=id.runtime_club>356?0:team_league(id.runtime_club);
        if(strengths_owner_||id.scope||id.sequence||(!family_transition&&id.league!=initial)||
           (family_transition&&((id.source_param2!=0&&!live_source_identity(id))||id.league_group>1||id.manager_default_league<0||id.manager_default_league>8))||
           next_strengths_scope_==UINT64_MAX||
           request.value!=-1||request.control!=ui::StrengthsControl::condition) {
            error="strengths: invalid entry";return {};
        }
    } else if(!strengths_owner_||id!=strengths_owner_->page.identity||
              save_stream(g_)!=strengths_owner_->native||
              rand_.seed()!=strengths_owner_->rand_seed||rand_.calls()!=strengths_owner_->rand_calls||
              game_.seed()!=strengths_owner_->game_seed||game_.calls()!=strengths_owner_->game_calls) {
        error="strengths: stale modal snapshot";return {};
    }
    // The process timer's automatic Source event8 belongs to an unported modal
    // context. Ordinary desk entry initializes this runtime timer to zero.
    const auto byte=[&](uint32_t at) {if(g_.memory.has(at))return g_.memory.u8(at);
        const auto found=club_modal_runtime_.find(at);return found==club_modal_runtime_.end()?uint8_t(0):found->second;};
    const uint32_t start=uint32_t(byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField2))|uint32_t(byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField3))<<8|
        uint32_t(byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField4))<<16|uint32_t(byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField5))<<24;
    if(opening&&start!=0&&byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField)<200) {
        error="strengths: active process-timer context unsupported";return {};
    }
    if(request.kind==ui::StrengthsAction::pairings||request.kind==ui::StrengthsAction::table) {
        error="strengths: navigation target not implemented";return {};
    }
    if(request.kind==ui::StrengthsAction::team_info&&!team_info) {
        error="strengths: TeamInfo presenter unavailable";return {};
    }
    strengths_dispatch_active_=true;
    // Re-entrancy flag: set while this dispatch runs (callbacks may re-enter), cleared on every exit.
    struct Reset { bool& active; ~Reset(){active=false;} } reset{strengths_dispatch_active_};
    try {
        StrengthsDispatch result;
        if(request.kind==ui::StrengthsAction::team_info) {
            if(request.control!=ui::StrengthsControl::condition||request.value<0)
                throw FormatError("strengths: malformed TeamInfo target");
            // Keep an owned value copy across the external callback; callbacks
            // can re-enter the public API and replace/reset modal ownership.
            const auto current=strengths_owner_->page;
            const auto target=std::find_if(current.projection.rows.begin(),current.projection.rows.end(),
                [&](const auto& row){return row.runtime_team==request.value;});
            if(target==current.projection.rows.end())
                throw FormatError("strengths: TeamInfo target is not a visible team");

            // Reset2 opens ClubInfoFromGame from the selected runtime row and
            // resumes this same Strengths loop after the nested modal returns.
            // Stage the presenter against the live page, then commit only if it
            // did not re-enter or mutate the retained Strengths owner.
            GameState next=g_;GameRandom next_game=game_;WatcomRandom next_rand=rand_;
            auto next_runtime=club_modal_runtime_;
            const auto live_before=strengths_checkpoint();const auto old_revision=revision;
            const auto owner_identity=current.identity;
            const auto same_strengths_checkpoint=[&](const StrengthsCheckpoint& before,
                                                      const StrengthsCheckpoint& after) {
                const bool owner_matches=before.owner.has_value()==after.owner.has_value()&&
                    (!before.owner||(before.owner->page==after.owner->page&&
                        before.owner->native==after.owner->native&&
                        before.owner->rand_seed==after.owner->rand_seed&&
                        before.owner->game_seed==after.owner->game_seed&&
                        before.owner->rand_calls==after.owner->rand_calls&&
                        before.owner->game_calls==after.owner->game_calls));
                return save_stream(before.state)==save_stream(after.state)&&
                    same_strengths_runtime_state(before.state,after.state)&&
                    before.game.seed()==after.game.seed()&&before.game.calls()==after.game.calls()&&
                    before.rand.seed()==after.rand.seed()&&before.rand.calls()==after.rand.calls()&&
                    before.runtime==after.runtime&&before.next_scope==after.next_scope&&owner_matches&&
                    before.family==after.family&&before.next_family_id==after.next_family_id;
            };
            const Image backdrop=ui::StrengthsScreen(data_directory_,current).render();
            auto services=staged_club_modal_services(next,next_game,next_rand,next_runtime);
            const bool accepted=team_info(services,request.value,backdrop);
            const auto live_after=strengths_checkpoint();
            if(revision!=old_revision||!same_strengths_checkpoint(live_before,live_after)||
               !live_after.owner||live_after.owner->page.identity!=owner_identity)
                throw FormatError("strengths: live owner changed during TeamInfo presenter");
            if(!accepted)throw FormatError("strengths: TeamInfo presenter stopped");
            if(next.date!=g_.date||next.manager_teams!=g_.manager_teams||
               !next.memory.has(::bmh::layout::ActiveManager)||next.memory.u8(::bmh::layout::ActiveManager)!=id.manager||
               next.memory.u16(setting)!=g_.memory.u16(setting)||next.memory.u16(family)!=g_.memory.u16(family)||
               rec+0x292>next.manager_records.size()||
               (int(next.manager_records[rec+0x290])|int(next.manager_records[rec+0x291])<<8)!=id.runtime_club)
                throw FormatError("strengths: TeamInfo changed the retained desk or family owner");
            // Reset2 retains local_68 across a nested ClubInfoFromGame and
            // redraws the same league. The accepted club-entry injury cleanup
            // can change lineup bits, so do not recompute this same-instance cache.
            ui::StrengthsProjectionContext context;
            context.cached_age=current.projection.age_scratch;
            context.retain_validated_owner_age=true;
            auto projection=ui::strengths_from_game(next,id.manager,id.league,context);
            if(projection.rows.empty())throw FormatError("strengths: empty TeamInfo return projection");
            if(current.projection.age_scratch.league!=id.league||
               current.projection.age_scratch.tenths.size()!=projection.rows.size())
                throw FormatError("strengths: retained TeamInfo age scratch does not match its owner");
            next.memory.set16(setting,projection.words.post_draw_settings_word);
            result.page=current;
            result.page.identity.revision=revision+1;
            result.page.identity.sequence=id.sequence+1;
            result.page.identity.expected_settings_word=next.memory.u16(setting);
            result.page.identity.expected_family_word=next.memory.u16(family);
            result.page.projection=std::move(projection);
            ui::StrengthsScreen validate(data_directory_,result.page);
            result.preparation=preparation_projection(next,revision+1);
            next_runtime[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField2]=uint8_t(id.league);
            next_runtime[::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField]=uint8_t(current.league_group);
            next_runtime[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField]=1;next_runtime[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2]=0;
            next_runtime[::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField]=exe_.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField+uint32_t(id.league));
            const auto last=team_average(next,result.page.projection.rows.back().runtime_team,
                result.page.projection.words.effective_average_flags);
            next_runtime[::bmh::layout::CareerSession_FormatError_ExecutableField3]=uint8_t(last.best_half_sum);
            next_runtime[::bmh::layout::CareerSession_FormatError_ExecutableField4]=uint8_t(last.best_half_sum>>8);
            StrengthsOwner owner{result.page,save_stream(next),next_rand.seed(),next_game.seed(),
                next_rand.calls(),next_game.calls(),next_runtime};
            g_=std::move(next);game_=next_game;rand_=next_rand;
            club_modal_runtime_=std::move(next_runtime);strengths_owner_=std::move(owner);
            ++revision;return result;
        }
        if(request.kind==ui::StrengthsAction::close) {
            if(request.value!=-1||request.control!=ui::StrengthsControl::condition)
                throw FormatError("strengths: malformed close");
            result.closed=true;result.page=strengths_owner_->page;
            ++result.page.identity.revision;++result.page.identity.sequence;
            result.preparation=preparation_projection(g_,revision+1);
            strengths_owner_.reset();++revision;return result;
        }
        GameState next=g_;auto runtime=club_modal_runtime_;ui::StrengthsProjectionContext context;
        int league=id.league,group=opening?(family_transition?id.league_group:0):strengths_owner_->page.league_group;
        if(opening)next.memory.set16(family,ui::strengths_family_word_source(next.memory.u16(family),exe_.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField+uint32_t(league))));
        else if(request.kind==ui::StrengthsAction::control) {
            if(request.value!=-1)throw FormatError("strengths: malformed control");
            const auto word=ui::strengths_control_word_source(id.expected_settings_word,request.control);
            if(!word||*word==id.expected_settings_word)throw FormatError("strengths: unavailable or unchanged control");
            next.memory.set16(setting,*word);
            if(request.control==ui::StrengthsControl::lineup)context.recompute_age_as=ui::StrengthsAgeBasis::header_toggle_bit15;
            else context.cached_age=strengths_owner_->page.projection.age_scratch;
        } else if(request.kind==ui::StrengthsAction::select_league) {
            if(request.control!=ui::StrengthsControl::condition||request.value<0||request.value>8||request.value==league)
                throw FormatError("strengths: invalid league selection");
            league=request.value;
        } else if(request.kind==ui::StrengthsAction::toggle_group) {
            if(request.value!=-1||request.control!=ui::StrengthsControl::condition||g_.memory.u8(::bmh::layout::CalendarState_FormatError_RuntimeField2)!=0)
                throw FormatError("strengths: unavailable group selection");
            group^=1;league=group?int(exe_.u8(::bmh::layout::CareerSession_FormatError_RuntimeField4)):int(g_.manager_records[rec+0x210]);
            if(league<0||league>8)throw FormatError("strengths: invalid manager league");
            if(league==id.league)context.cached_age=strengths_owner_->page.projection.age_scratch;
        } else if(!opening)throw FormatError("strengths: unknown action");
        auto projection=ui::strengths_from_game(next,id.manager,league,context);
        next.memory.set16(setting,projection.words.post_draw_settings_word);
        result.page.identity={id.manager,id.runtime_club,id.club_identity,league,id.day_counter,revision+1,
            opening?next_strengths_scope_:id.scope,opening?0:id.sequence+1,
            next.memory.u16(setting),next.memory.u16(family)};
        result.page.identity.source_param2=id.source_param2;
        result.page.identity.league_group=uint8_t(group);
        result.page.identity.manager_default_league=id.manager_default_league;
        result.page.identity.family_id=id.family_id;
        result.page.identity.family_sequence=id.family_sequence;
        result.page.projection=std::move(projection);result.page.league_group=group;
        result.page.decorated=g_.memory.u8(::bmh::layout::PlainBackground)==0;result.page.original_panel=g_.memory.u8(::bmh::layout::SolidPanelBackground)==0;
        result.page.gradient=g_.memory.u8(::bmh::layout::TextGradientDisabled)==0&&exe_.u8(::bmh::layout::TextGradientMode)==2;
        result.page.secondary_leagues=g_.memory.u8(::bmh::layout::CalendarState_FormatError_RuntimeField2)==0;
        result.page.title=message_text(next,::bmh::layout::CareerSession_FormatError_TextPointer5)+ui::utf8_to_original(result.page.projection.league_name);
        const uint8_t raw_round=next.league_rounds.at(size_t(league));const int round=(raw_round&0x7f)+1;
        result.page.subtitle=round>league_matchdays(league)?message_text(next,::bmh::layout::CareerSession_FormatError_TextPointer3):
            message_text(next,::bmh::layout::CareerSession_FormatError_TextPointer2+uint32_t((raw_round&0x80)!=0)*4)+format_number(next,round,1)+
            exe_.cstring(::bmh::layout::CareerSession_FormatError_TextPointer)+message_text(next,::bmh::layout::CareerSession_FormatError_TextPointer4);
        ui::StrengthsScreen validate(data_directory_,result.page);
        result.preparation=preparation_projection(next,revision+1);
        // Runtime drawing scratch belongs to the session process, not the FBM.
        runtime[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField2]=uint8_t(league);runtime[::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField]=uint8_t(group);runtime[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField]=1;runtime[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2]=0;
        runtime[::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField]=exe_.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField+uint32_t(league));
        const auto last=team_average(next,result.page.projection.rows.back().runtime_team,result.page.projection.words.effective_average_flags);
        runtime[::bmh::layout::CareerSession_FormatError_ExecutableField3]=uint8_t(last.best_half_sum);runtime[::bmh::layout::CareerSession_FormatError_ExecutableField4]=uint8_t(last.best_half_sum>>8);
        StrengthsOwner owner{result.page,save_stream(next),rand_.seed(),game_.seed(),rand_.calls(),game_.calls(),runtime};
        g_=std::move(next);club_modal_runtime_=std::move(runtime);strengths_owner_=std::move(owner);
        if(opening)++next_strengths_scope_;
        ++revision;return result;
    } catch(const std::exception& e) {error=e.what();return {};}
}

// Opens the top scorers page (desk menu 20) for the active manager; refuses while the process
// timer context is active. For a national coach the club identity comes from the spare record.
// Returns the first page (goals, league scope) or an empty optional with 'error'.
std::optional<ui::TopScorersState> CareerSession::open_top_scorers(
    const ui::DeskRequest& request, uint64_t revision, const Image& underlay, std::string& error) {
    error.clear();
    if (top_scorers_dispatch_active_ || top_scorers_owner_ || revision == UINT64_MAX ||
        next_top_scorers_scope_ == UINT64_MAX || request.kind != ui::DeskRequestKind::open_menu ||
        request.menu_kind != 20 || request.revision != revision || request.manager_slot < 0 ||
        request.manager_slot > 3 || !g_.memory.has(::bmh::layout::ActiveManager) ||
        g_.memory.u8(::bmh::layout::ActiveManager) != request.manager_slot ||
        size_t(request.manager_slot) >= g_.manager_teams.size() || career_complete() ||
        (turn_driver_.stage != TurnDriver::Stage::MatchdayDesks &&
         turn_driver_.stage != TurnDriver::Stage::SeasonEndDesk) ||
        pending_offer_dispatch_active_ || market_dispatch_active_ || calendar_advance_active_ ||
        league_pairings_draft_ || league_pairings_dispatch_active_ || strengths_owner_ ||
        strengths_dispatch_active_ || balance_draft_ || balance_dispatch_active_ || transfer_offer_editor_ ||
        manager_join_ || contract_quote_ || stadium_.investment || stadium_.tip || stadium_.periphery_quote || stadium_.block_expansion) {
        error = "top scorers: stale or unavailable menu-20 owner";
        return {};
    }
    const auto source_byte = [&](uint32_t address) {
        if (g_.memory.has(address)) return g_.memory.u8(address);
        const auto it = club_modal_runtime_.find(address);
        return it == club_modal_runtime_.end() ? exe_.u8(address) : it->second;
    };
    const uint32_t timer_start=uint32_t(source_byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField2))|uint32_t(source_byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField3))<<8|
        uint32_t(source_byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField4))<<16|uint32_t(source_byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField5))<<24;
    const unsigned timer_duration=source_byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField);
    if (timer_start != 0 && timer_duration < 200) {
        error = "top scorers: active process-timer context unsupported";
        return {};
    }
    try {
        const int manager = request.manager_slot;
        const int team = g_.manager_teams[size_t(manager)];
        const size_t manager_record = size_t(manager) * kManagerRecordSize;
        if (manager_record + 0x292 > g_.manager_records.size() ||
            uint16_t(g_.manager_records[manager_record + 0x290] |
                     uint16_t(g_.manager_records[manager_record + 0x291]) << 8) != uint16_t(team))
            throw FormatError("top scorers: manager record does not own the active team");
        int stable_team = team;
        if (team == kNoTeam) {
            // The live ManagerRecord4Club owner is mirrored by the typed spare manager record 4.
            const size_t backup = 4 * 0xb0c + 0x290;
            if (manager != 0 || backup + 2 > g_.manager_records.size())
                throw FormatError("top scorers: unsupported national-coach owner");
            stable_team = int(uint16_t(g_.manager_records[backup] |
                                       uint16_t(g_.manager_records[backup + 1]) << 8));
        } else if (team < 0 || team >= world_layout().league_teams) {
            throw FormatError("top scorers: manager team is outside Source league scope");
        }
        if (stable_team < 0 || stable_team >= 138 || size_t(stable_team) >= g_.club_ids.size())
            throw FormatError("top scorers: missing stable club identity");
        const int league = team > 0x164 ? 0 : team_league(team);
        if (league < 0 || league > 8)
            throw FormatError("top scorers: manager has no supported Source league");

        ui::TopScorersIdentity identity;
        identity.manager_slot = manager;
        identity.runtime_team = team;
        identity.club_identity = int(g_.club_ids[size_t(stable_team)]);
        identity.league = league;
        identity.day_counter = uint32_t(g_.date);
        identity.revision = revision + 1;
        identity.scope = next_top_scorers_scope_;
        identity.sequence = 0;
        auto page = ui::top_scorers_state_from_game(g_, identity,
            ui::TopScorersMetric::goals, ui::TopScorersScope::league, underlay);
        page.decorated_background = source_byte(::bmh::layout::PlainBackground) == 0;
        page.gradient_text = source_byte(::bmh::layout::TextGradientDisabled) == 0 && source_byte(::bmh::layout::TextGradientMode) == 2;
        page.cached_backdrop = source_byte(::bmh::layout::CareerSession_ApplyBalanceSourceStyle_ExecutableField) != 0 || source_byte(::bmh::layout::CareerSession_FormatError_ExecutableField5) != 0 ||
                               source_byte(::bmh::layout::CareerSession_FormatError_ExecutableField6) != 0 || source_byte(::bmh::layout::CareerSession_FormatError_ExecutableField7) != 0;
        TopScorersOwner owner{page, save_stream(g_), rand_.seed(), game_.seed(), rand_.calls(), game_.calls()};
        top_scorers_owner_ = std::move(owner);
        ++next_top_scorers_scope_;
        ++revision;
        return page;
    } catch (const std::exception& e) {
        error = e.what();
        return {};
    }
}

// Applies one top scorers action: close, metric cycle (goals -> assists -> scorer -> goals) or
// scope toggle. Rejects a stale, replayed or modified owner; commits the new page on success.
std::optional<CareerSession::TopScorersDispatch> CareerSession::dispatch_top_scorers(
    const ui::TopScorersRequest& request, uint64_t& revision, std::string& error) {
    error.clear();
    if (stadium_.block_expansion || top_scorers_dispatch_active_ || !top_scorers_owner_ || revision == UINT64_MAX ||
        request.identity != top_scorers_owner_->page.identity || request.identity.revision != revision ||
        request.identity.sequence == UINT64_MAX || request.identity.expected_settings_word != 0 ||
        request.identity.expected_family_word != 0 || save_stream(g_) != top_scorers_owner_->native ||
        rand_.seed() != top_scorers_owner_->rand_seed || rand_.calls() != top_scorers_owner_->rand_calls ||
        game_.seed() != top_scorers_owner_->game_seed || game_.calls() != top_scorers_owner_->game_calls) {
        error = "top scorers: stale, replayed or modified owner";
        return {};
    }
    top_scorers_dispatch_active_ = true;
    struct Reset { bool& active; ~Reset() { active = false; } } reset{top_scorers_dispatch_active_};
    try {
        TopScorersDispatch result;
        const auto& current = top_scorers_owner_->page;
        const auto& identity = request.identity;
        if (identity.manager_slot < 0 || identity.manager_slot > 3 ||
            size_t(identity.manager_slot) >= g_.manager_teams.size() ||
            g_.manager_teams[size_t(identity.manager_slot)] != identity.runtime_team ||
            !g_.memory.has(::bmh::layout::ActiveManager) || g_.memory.u8(::bmh::layout::ActiveManager) != identity.manager_slot ||
            uint32_t(g_.date) != identity.day_counter || identity.league != current.projection.league)
            throw FormatError("top scorers: desk owner changed");
        const size_t manager_record = size_t(identity.manager_slot) * kManagerRecordSize;
        if (manager_record + 0x292 > g_.manager_records.size() ||
            uint16_t(g_.manager_records[manager_record + 0x290] |
                     uint16_t(g_.manager_records[manager_record + 0x291]) << 8) !=
                uint16_t(identity.runtime_team))
            throw FormatError("top scorers: manager record changed");
        const size_t backup = 4 * 0xb0c + 0x290;
        const int identity_team = identity.runtime_team == kNoTeam ?
            (backup + 2 <= g_.manager_records.size() ?
                int(uint16_t(g_.manager_records[backup] |
                             uint16_t(g_.manager_records[backup + 1]) << 8)) : -1) : identity.runtime_team;
        if (identity_team < 0 || identity_team >= world_layout().league_teams ||
            size_t(identity_team) >= g_.club_ids.size() ||
            int(g_.club_ids[size_t(identity_team)]) != identity.club_identity)
            throw FormatError("top scorers: stable club identity changed");

        if (request.action == ui::TopScorersAction::open)
            throw FormatError("top scorers: open is not a modal action");
        if (request.action == ui::TopScorersAction::close) {
            if (request.metric != current.metric || request.scope != current.scope)
                throw FormatError("top scorers: malformed close request");
            result.closed = true;
            result.page = current;
        } else {
            ui::TopScorersMetric metric = current.metric;
            ui::TopScorersScope scope = current.scope;
            if (request.action == ui::TopScorersAction::set_metric) {
                const auto next_metric = current.metric == ui::TopScorersMetric::goals ?
                    ui::TopScorersMetric::assists : current.metric == ui::TopScorersMetric::assists ?
                    ui::TopScorersMetric::scorer : ui::TopScorersMetric::goals;
                if (request.scope != current.scope || request.metric != next_metric)
                    throw FormatError("top scorers: metric request is not the Source cycle successor");
                metric = request.metric;
            } else if (request.action == ui::TopScorersAction::toggle_scope) {
                if (request.metric != current.metric || request.scope == current.scope)
                    throw FormatError("top scorers: invalid scope request");
                scope = request.scope;
            } else {
                throw FormatError("top scorers: unknown action");
            }
            result.page = ui::top_scorers_state_from_game(g_, identity, metric, scope, current.underlay);
            result.page.decorated_background = current.decorated_background;
            result.page.gradient_text = current.gradient_text;
            result.page.cached_backdrop = current.cached_backdrop;
        }
        result.page.identity.revision = revision + 1;
        result.page.identity.sequence = identity.sequence + 1;
        result.preparation = preparation_projection(g_, revision + 1);
        if (result.closed) top_scorers_owner_.reset();
        else {
            top_scorers_owner_->page = result.page;
            top_scorers_owner_->native = save_stream(g_);
            top_scorers_owner_->rand_seed = rand_.seed();
            top_scorers_owner_->game_seed = game_.seed();
            top_scorers_owner_->rand_calls = rand_.calls();
            top_scorers_owner_->game_calls = game_.calls();
        }
        ++revision;
        return result;
    } catch (const std::exception& e) {
        error = e.what();
        return {};
    }
}

// Statistics menu (desk menu 23): flips the manager's statistics preference flags. Needs the open
// menu owner request of the current revision and the expected current flag byte; allows exactly
// one metric bit (below 0x40) or the pair 0xc0 to change. Returns false with 'error' otherwise.
bool CareerSession::apply_statistics_preference(const ui::DeskRequest& owner, int manager_slot,
                                                uint8_t expected_flags, uint8_t target_flags,
                                                uint64_t& revision, std::string& error) {
    constexpr uint32_t flags_base = ::bmh::layout::Flags;
    if (owner.kind != ui::DeskRequestKind::open_menu || owner.menu_kind != 23 ||
        owner.manager_slot != manager_slot || manager_slot < 0 || manager_slot >= 4 ||
        owner.revision != revision || !g_.memory.has(::bmh::layout::ActiveManager) ||
        g_.memory.u8(::bmh::layout::ActiveManager) != manager_slot || size_t(manager_slot) >= g_.manager_teams.size() ||
        career_complete() || opportunities_active() ||
        (turn_driver_.stage != TurnDriver::Stage::MatchdayDesks &&
         turn_driver_.stage != TurnDriver::Stage::SeasonEndDesk)) {
        error = "statistics: stale menu owner";
        return false;
    }
    const uint32_t address = flags_base + uint32_t(manager_slot) * 3;
    if (!g_.memory.has(address) || g_.memory.u8(address) != expected_flags) {
        error = "statistics: stale Source preference";
        return false;
    }
    const uint8_t delta = uint8_t(expected_flags ^ target_flags);
    const bool one_metric = delta != 0 && (delta & uint8_t(delta - 1)) == 0 && delta < 0x40;
    const bool absolute_pair = delta == 0xc0;
    if ((!one_metric && !absolute_pair) || target_flags != uint8_t(expected_flags ^ delta)) {
        error = "statistics: invalid Source preference operation";
        return false;
    }
    g_.memory.set8(address, target_flags);
    error.clear();
    return true;
}

// Opens the opportunities page (desk menu 23) for the active manager. 'national_source_case'
// selects the national-team variant (team 0x165, league 0, identity supplied by the caller as
// 'stable_identity'). Needs the 320x240 desktop underlay. Empty optional with 'error' on failure.
std::optional<ui::OpportunitiesState> CareerSession::open_opportunities(
    const ui::DeskRequest& request,uint64_t revision,const Image& underlay,std::string& error,
    bool national_source_case,int stable_identity) {
    error.clear();
    if(opportunities_dispatch_active_||opportunities_owner_||revision==UINT64_MAX||next_opportunities_scope_==UINT64_MAX||
       request.kind!=ui::DeskRequestKind::open_menu||request.menu_kind!=23||request.revision!=revision||
       request.manager_slot<0||request.manager_slot>3||!g_.memory.has(::bmh::layout::ActiveManager)||g_.memory.u8(::bmh::layout::ActiveManager)!=request.manager_slot||
       size_t(request.manager_slot)>=g_.manager_teams.size()||career_complete()||
       (turn_driver_.stage!=TurnDriver::Stage::MatchdayDesks&&turn_driver_.stage!=TurnDriver::Stage::SeasonEndDesk)||
       pending_offer_dispatch_active_||market_dispatch_active_||calendar_advance_active_||league_pairings_draft_||
       league_pairings_dispatch_active_||strengths_owner_||strengths_dispatch_active_||top_scorers_owner_||
        top_scorers_dispatch_active_||balance_draft_||balance_dispatch_active_||other_games_owner_||other_games_dispatch_active_||
       transfer_offer_editor_||manager_join_||contract_quote_||stadium_.investment||stadium_.tip||stadium_.periphery_quote||
       stadium_.block_expansion) { error="opportunities: stale or unavailable menu-23 owner"; return {}; }
    try {
        const int manager=request.manager_slot,team=g_.manager_teams[size_t(manager)];
        const int league=national_source_case?0:team_league(team);
        if(national_source_case) {
            if(team!=kNoTeam||league!=0)throw FormatError("opportunities: invalid national menu-6 owner");
            if(stable_identity<0)throw FormatError("opportunities: missing national Desk identity");
        } else if(team<0||team>=138||size_t(team)>=g_.club_ids.size()||league<0||league>=9)
            throw FormatError("opportunities: unsupported team/league");
        if(underlay.width!=320||underlay.height!=240)throw FormatError("opportunities: expected captured 320x240 desktop");
        const uint32_t choice_at=::bmh::layout::CareerSession_FormatError_ExecutableField+uint32_t(manager)*2;
        if(!g_.memory.has(choice_at+1))throw FormatError("opportunities: Source choice word missing");
        uint16_t mode=g_.memory.u16(choice_at);if(mode==0){mode=1;g_.memory.set16(choice_at,mode);}
        if(mode!=1&&mode!=2&&mode!=4&&mode!=8)throw FormatError("opportunities: invalid stored Source mode");
        ui::OpportunitiesState page;page.identity={manager,team,national_source_case?stable_identity:int(g_.club_ids[size_t(team)]),uint32_t(g_.date),revision+1,next_opportunities_scope_,0};
        page.mode=mode;page.league_size=league_system().sizes[league];page.underlay=underlay;
        page.gradient_text=g_.memory.has(::bmh::layout::TextGradientDisabled)&&g_.memory.u8(::bmh::layout::TextGradientDisabled)==0&&g_.memory.has(::bmh::layout::TextGradientMode)&&g_.memory.u8(::bmh::layout::TextGradientMode)==2;
        page.current_rank=national_source_case?national_opportunities_rank(g_,team,page.ranks):table_prediction(g_,team,mode,page.ranks,nullptr);
        opportunities_owner_=OpportunitiesOwner{page,save_stream(g_),game_,rand_,national_source_case};++next_opportunities_scope_;
        return page;
    } catch(const std::exception& e){error=e.what();return {};}
}

// Closes the opportunities page or selects one of its modes (1, 2, 4 or 8) and recomputes the
// rank table; stale or modified owners are rejected.
std::optional<CareerSession::OpportunitiesDispatch> CareerSession::dispatch_opportunities(
    const ui::OpportunitiesRequest& request,uint64_t& revision,std::string& error) {
    error.clear();
    if(opportunities_dispatch_active_||!opportunities_owner_||revision==UINT64_MAX||
       request.identity!=opportunities_owner_->page.identity||request.identity.revision!=revision||
       request.identity.sequence==UINT64_MAX||save_stream(g_)!=opportunities_owner_->native||
       game_.seed()!=opportunities_owner_->game.seed()||game_.calls()!=opportunities_owner_->game.calls()||
       rand_.seed()!=opportunities_owner_->rand.seed()||rand_.calls()!=opportunities_owner_->rand.calls()) {
        error="opportunities: stale, replayed or modified owner";return {};
    }
    opportunities_dispatch_active_=true;struct Reset{bool& active;~Reset(){active=false;}} reset{opportunities_dispatch_active_};
    try {
        const auto current=opportunities_owner_->page; // Value snapshot across all validation.
        const bool national_source_case=opportunities_owner_->national_source_case;
        if(current.identity.manager_slot<0||current.identity.manager_slot>3||
           size_t(current.identity.manager_slot)>=g_.manager_teams.size()||
           g_.manager_teams[size_t(current.identity.manager_slot)]!=current.identity.runtime_team||
           !g_.memory.has(::bmh::layout::ActiveManager)||g_.memory.u8(::bmh::layout::ActiveManager)!=current.identity.manager_slot||
           uint32_t(g_.date)!=current.identity.day_counter||
           size_t(current.identity.runtime_team)>=g_.club_ids.size()||
           (!national_source_case&&int(g_.club_ids[size_t(current.identity.runtime_team)])!=current.identity.stable_club_identity)||
           (national_source_case&&(current.identity.runtime_team!=kNoTeam||
                                  current.identity.stable_club_identity<0)))
            throw FormatError("opportunities: desk owner changed");
        OpportunitiesDispatch result;result.page=current;
        if(request.action==ui::OpportunitiesAction::close){
            if(request.mode!=current.mode)throw FormatError("opportunities: malformed close");
            result.closed=true;
        } else if(request.action==ui::OpportunitiesAction::select_mode){
            if(request.mode!=1&&request.mode!=2&&request.mode!=4&&request.mode!=8)
                throw FormatError("opportunities: unsupported Source mode");
            const uint32_t choice_at=::bmh::layout::CareerSession_FormatError_ExecutableField+uint32_t(current.identity.manager_slot)*2;
            if(!g_.memory.has(choice_at+1))throw FormatError("opportunities: Source choice word missing");
            g_.memory.set16(choice_at,uint16_t(request.mode));
            result.page.mode=request.mode;
            result.page.current_rank=national_source_case?
                national_opportunities_rank(g_,current.identity.runtime_team,result.page.ranks):
                table_prediction(g_,current.identity.runtime_team,request.mode,result.page.ranks,nullptr);
        } else throw FormatError("opportunities: unknown action");
        result.page.identity.revision=revision+1;result.page.identity.sequence=request.identity.sequence+1;
        result.preparation=preparation_projection(g_,revision+1);
        if(result.closed)opportunities_owner_.reset();
        else opportunities_owner_=OpportunitiesOwner{result.page,save_stream(g_),game_,rand_,national_source_case};
        ++revision;return result;
    } catch(const std::exception& e){error=e.what();return {};}
}

// Opens the other-games overview (desk menu 5): validates the desk request and runs the entry
// changes on a staged copy (season-dependent selector byte, selector 12 insertion or removal),
// then builds the page for the remembered competition. The copy replaces the live state only
// once the page and owner exist. Empty optional with 'error' on failure.
std::optional<ui::OtherGamesState> CareerSession::open_other_games(
    const ui::DeskRequest& request, uint64_t& revision, const Image& underlay, std::string& error) {
    error.clear();
    if (other_games_dispatch_active_ || other_games_owner_ || revision == UINT64_MAX ||
        next_other_games_scope_ == UINT64_MAX || request.kind != ui::DeskRequestKind::open_menu ||
        request.menu_kind != 5 || request.revision != revision || request.manager_slot < 0 ||
        request.manager_slot > 3 || !g_.memory.has(::bmh::layout::ActiveManager) ||
        g_.memory.u8(::bmh::layout::ActiveManager) != request.manager_slot ||
        size_t(request.manager_slot) >= g_.manager_teams.size() || career_complete() ||
        (turn_driver_.stage != TurnDriver::Stage::MatchdayDesks &&
         turn_driver_.stage != TurnDriver::Stage::SeasonEndDesk) ||
        pending_offer_dispatch_active_ || market_dispatch_active_ || calendar_advance_active_ ||
        league_pairings_draft_ || league_pairings_dispatch_active_ || strengths_owner_ ||
        strengths_dispatch_active_ || top_scorers_owner_ || top_scorers_dispatch_active_ || other_games_owner_ || other_games_dispatch_active_ ||
        balance_draft_ || balance_dispatch_active_ || transfer_offer_editor_ || manager_join_ ||
        contract_quote_ || stadium_.investment || stadium_.tip || stadium_.periphery_quote || stadium_.block_expansion) {
        error = "other games: stale or unavailable menu-5 owner";
        return {};
    }
    try {
        if (!g_.memory.has(::bmh::layout::ActiveSelectors + 7) || !g_.memory.has(::bmh::layout::ActiveManager) ||
            !g_.memory.has(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField + uint32_t(request.manager_slot)) || !g_.memory.has(::bmh::layout::FriendlyPlanCount))
            throw FormatError("other games: missing Source overview state");

        GameState next = g_;
        // CupGroupOverviewEffects's exact entry transaction. CareerSession_FormatError_RuntimeField3 aliases selector slot four, so its
        // season write precedes the first-hit selector-12 insertion/removal.
        next.memory.set8(::bmh::layout::CareerSession_FormatError_RuntimeField3, uint8_t(7 - (uint16_t(next.season) % 4)));
        int active = next.memory.u8(::bmh::layout::ActiveManager);
        if (active > 3) {
            active = 0;
            next.memory.set8(::bmh::layout::ActiveManager, 0);
        }
        next.memory.set8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField + uint32_t(active), 0x80);
        if (next.memory.u8(::bmh::layout::FriendlyPlanCount) == 0) {
            end_competition(next.memory, 12);
        } else {
            for (uint32_t i = 0; i < 8; ++i) {
                const uint8_t id = next.memory.u8(::bmh::layout::ActiveSelectors + i);
                if (id == 12) break;
                if (id == 0x14) { next.memory.set8(::bmh::layout::ActiveSelectors + i, 12); break; }
            }
        }

        const int manager = active;
        if (size_t(manager) >= next.manager_teams.size())
            throw FormatError("other games: active manager slot is outside typed manager list");
        const int team = next.manager_teams[size_t(manager)];
        const size_t record = size_t(manager) * kManagerRecordSize;
        if (record + 0x292 > next.manager_records.size() || team < 0 || team >= 0x190 ||
            uint16_t(next.manager_records[record + 0x290] |
                     uint16_t(next.manager_records[record + 0x291]) << 8) != uint16_t(team))
            throw FormatError("other games: manager record does not own the active team");
        int stable_team = team;
        if (team > 0x164) {
            const size_t backup = 4 * 0xb0c + 0x290;
            if (manager != 0 || backup + 2 > next.manager_records.size())
                throw FormatError("other games: unsupported national-coach owner");
            stable_team = int(uint16_t(next.manager_records[backup] |
                                       uint16_t(next.manager_records[backup + 1]) << 8));
        }
        if (stable_team < 0 || stable_team >= 138 || size_t(stable_team) >= next.club_ids.size())
            throw FormatError("other games: missing stable manager club identity");

        const uint8_t source_view = next.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField + uint32_t(manager));
        const OtherGamesDisplayMode mode = (source_view & 0x80)
            ? OtherGamesDisplayMode::detail : OtherGamesDisplayMode::compact;
        const int local_cursor = source_view & 0x7f;
        if (local_cursor % 16 != 0)
            throw FormatError("other games: malformed Source local page cursor");
        ui::OtherGamesIdentity identity;
        identity.manager_slot = manager;
        identity.runtime_team = team;
        identity.club_identity = int(next.club_ids[size_t(stable_team)]);
        identity.day_counter = uint32_t(next.date);
        identity.revision = revision + 1;
        identity.scope = next_other_games_scope_;
        identity.sequence = 0;
        identity.expected_view_byte = source_view;

        auto page = ui::other_games_state_from_game(next,identity,texts_,exe_,
            other_games_selection_.competition,other_games_selection_.linked_page,-1,mode,
            local_cursor,true,underlay,club_modal_runtime_);
        if (page.projection.display_mode != mode) {
            // Source forces local20=detail on redraw if this round cannot use the toggle.
            if (page.projection.source_participant_count <= 16 && mode != OtherGamesDisplayMode::detail) {
                next.memory.set8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField + uint32_t(manager),uint8_t(source_view | 0x80));
                identity.expected_view_byte = uint8_t(source_view | 0x80);
                page.identity = identity;
            } else {
                throw FormatError("other games: malformed Source local page cursor");
            }
        }
        page.source_participant_offset = local_cursor;
        OtherGamesOwner owner;
        owner.page = page;
        owner.desk_owner = request;
        owner.round = page.projection.round;
        owner.participant_offset = local_cursor;
        owner.display_mode = page.projection.display_mode;
        owner.native = save_stream(next);
        owner.runtime = club_modal_runtime_;
        owner.rand_seed = rand_.seed(); owner.game_seed = game_.seed();
        owner.rand_calls = rand_.calls(); owner.game_calls = game_.calls();
        g_ = std::move(next);
        other_games_owner_ = std::move(owner);
        ++next_other_games_scope_;
        ++revision;
        return page;
    } catch (const std::exception& e) {
        error = e.what();
        return {};
    }
}

// Applies one action of the other-games overview against the owner snapshot (a team info
// presenter may be supplied); empty optional with 'error' when the request or owner is stale.
std::optional<CareerSession::OtherGamesDispatch> CareerSession::dispatch_other_games(
    const ui::OtherGamesRequest& request, uint64_t& revision, std::string& error,
    const TransferOfferTeamInfoPresenter& team_info) {
    error.clear();
    if (stadium_.block_expansion || other_games_dispatch_active_ || !other_games_owner_ || revision == UINT64_MAX ||
        request.identity != other_games_owner_->page.identity || request.identity.revision != revision ||
        request.identity.sequence == UINT64_MAX || save_stream(g_) != other_games_owner_->native ||
        club_modal_runtime_ != other_games_owner_->runtime || rand_.seed() != other_games_owner_->rand_seed ||
        rand_.calls() != other_games_owner_->rand_calls || game_.seed() != other_games_owner_->game_seed ||
        game_.calls() != other_games_owner_->game_calls) {
        error = "other games: stale, replayed or modified owner";
        return {};
    }
    other_games_dispatch_active_ = true;
    struct Reset { bool& active; ~Reset() { active = false; } } reset{other_games_dispatch_active_};
    try {
        // Keep value snapshots across any external presenter call. The presenter
        // may capture this CareerSession and invoke another public action while
        // the modal dispatch is active.
        const auto current = other_games_owner_->page;
        const auto id = request.identity;
        const auto desk = other_games_owner_->desk_owner;
        const int owned_round=other_games_owner_->round;
        const int owned_participant_offset=other_games_owner_->participant_offset;
        const auto owned_display_mode=other_games_owner_->display_mode;
        if (id.manager_slot != desk.manager_slot || id.manager_slot < 0 || id.manager_slot > 3 ||
            size_t(id.manager_slot) >= g_.manager_teams.size() ||
            g_.manager_teams[size_t(id.manager_slot)] != id.runtime_team ||
            g_.memory.u8(::bmh::layout::ActiveManager) != id.manager_slot || uint32_t(g_.date) != id.day_counter ||
            g_.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField + uint32_t(id.manager_slot)) != id.expected_view_byte)
            throw FormatError("other games: desk owner or view byte changed");
        const size_t rec = size_t(id.manager_slot) * kManagerRecordSize;
        if (rec + 0x292 > g_.manager_records.size() ||
            uint16_t(g_.manager_records[rec + 0x290] | uint16_t(g_.manager_records[rec + 0x291]) << 8) !=
                uint16_t(id.runtime_team))
            throw FormatError("other games: manager record changed");
        int stable_team = id.runtime_team;
        if (id.runtime_team > 0x164) {
            const size_t backup = 4 * 0xb0c + 0x290;
            if (id.manager_slot != 0 || backup + 2 > g_.manager_records.size())
                throw FormatError("other games: unsupported national owner");
            stable_team = int(uint16_t(g_.manager_records[backup] | uint16_t(g_.manager_records[backup + 1]) << 8));
        }
        if (stable_team < 0 || stable_team >= 138 || size_t(stable_team) >= g_.club_ids.size() ||
            int(g_.club_ids[size_t(stable_team)]) != id.club_identity)
            throw FormatError("other games: stable club identity changed");
        // Team info branch: the presenter runs against a staged copy (state, both random generators,
        // runtime bytes) and the live session must come back unchanged; only then is the staged copy
        // committed together with the redrawn page and the owner snapshot is refreshed.
        if (request.action == ui::OtherGamesAction::team_info) {
            if (request.value < 0 || current.projection.unfinished_predecessor ||
                !current.projection.fixtures_available)
                throw FormatError("other games: invalid team-information target");
            bool target_visible = false;
            for (const auto& row:current.projection.rows) {
                target_visible |= row.home_team==request.value||row.away_team==request.value;
            }
            if (!target_visible) throw FormatError("other games: team-information target is not visible");
            if (!team_info) throw FormatError("other games: TeamInfo presenter unavailable");

            GameState next=g_;
            GameRandom next_game=game_;
            WatcomRandom next_rand=rand_;
            auto next_runtime=club_modal_runtime_;
            // Source CupGroupOverviewEffects passes the accepted Other Games drawing as the
            // immediate parent's backdrop. The presenter's mutations are staged
            // with the same Native, W and process-runtime owners.
            const Image backdrop=ui::OtherGamesScreen(data_directory_,current).render();
            auto services=staged_club_modal_services(next,next_game,next_rand,next_runtime);
            const auto live_baseline=other_games_checkpoint();
            const auto revision_baseline=revision;
            const bool presenter_accepted=team_info(services,request.value,backdrop);

            // Validate the live session before consulting the owner again or
            // committing staged presenter changes. A captured callback that
            // mutated Native, either RNG, runtime, selection, scope or owner is
            // rejected; Controller restores its pre-dispatch checkpoint.
            if(revision!=revision_baseline||
               !same_other_games_live_baseline(live_baseline,other_games_checkpoint()))
                throw FormatError("other games: live session changed during TeamInfo presenter");
            if(!presenter_accepted)
                throw FormatError("other games: TeamInfo presenter stopped");
            if(!other_games_owner_||other_games_owner_->page.identity!=id||
               ui::OtherGamesScreen(data_directory_,other_games_owner_->page).render().pixels!=backdrop.pixels)
                throw FormatError("other games: live page changed during TeamInfo presenter");

            if(next.date!=g_.date||next.manager_teams!=g_.manager_teams||
               next.memory.u8(::bmh::layout::ActiveManager)!=id.manager_slot||
               next.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(id.manager_slot))!=id.expected_view_byte)
                throw FormatError("other games: TeamInfo changed its outer desk or saved page owner");
            const size_t active_record=size_t(id.manager_slot)*kManagerRecordSize;
            if(active_record+0x292>next.manager_records.size()||
               uint16_t(next.manager_records[active_record+0x290]|
                   uint16_t(next.manager_records[active_record+0x291])<<8)!=uint16_t(id.runtime_team))
                throw FormatError("other games: TeamInfo changed the active manager record");
            const int retained_team=id.runtime_team>0x164
                ? int(uint16_t(next.manager_records[4*0xb0c+0x290]|
                    uint16_t(next.manager_records[4*0xb0c+0x291])<<8)) : id.runtime_team;
            if(retained_team<0||retained_team>=138||size_t(retained_team)>=next.club_ids.size()||
               int(next.club_ids[size_t(retained_team)])!=id.club_identity)
                throw FormatError("other games: TeamInfo changed the stable desk club identity");

            // LAB_000226dc clears the input event, then local_50 skips the
            // entry-only stage/cursor/round setup and redraws the retained view.
            const auto put_runtime=[&](uint32_t address,uint8_t value) {
                if(next.memory.has(address))next.memory.set8(address,value);
                else next_runtime[address]=value;
            };
            put_runtime(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2,0);
            ui::OtherGamesIdentity returned_id=id;
            returned_id.revision=revision+1;
            returned_id.sequence=id.sequence+1;
            auto page=ui::other_games_state_from_game(next,returned_id,texts_,exe_,
                current.projection.competition,current.projection.linked_page,
                owned_round,owned_display_mode,
                owned_participant_offset,false,current.underlay,next_runtime);
            if(page.projection.competition!=current.projection.competition||
               page.projection.entry_index!=current.projection.entry_index||
               page.projection.descriptor_competition!=current.projection.descriptor_competition||
               page.projection.linked_page!=current.projection.linked_page||
               page.projection.round!=current.projection.round)
                throw FormatError("other games: TeamInfo return changed the retained Source selection");
            if(page.projection.source_participant_count<=16) {
                const uint32_t view_at=::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(id.manager_slot);
                const uint8_t before_view=next.memory.has(view_at)
                    ?next.memory.u8(view_at):id.expected_view_byte;
                const uint8_t forced=uint8_t(before_view|0x80);
                put_runtime(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(id.manager_slot),forced);
                returned_id.expected_view_byte=forced;
                page=ui::other_games_state_from_game(next,returned_id,texts_,exe_,
                    current.projection.competition,current.projection.linked_page,
                    owned_round,OtherGamesDisplayMode::detail,
                    owned_participant_offset,false,current.underlay,next_runtime);
            }
            page.source_participant_offset=owned_participant_offset;
            other_games_source_redraw_effects(next,next_rand,next_runtime,page.projection,exe_);
            OtherGamesDispatch result;
            result.page=std::move(page);
            result.preparation=preparation_projection(next,revision+1);
            ui::OtherGamesScreen validate_page(data_directory_,result.page);
            ui::DesktopScreen validate_desktop(data_directory_,result.preparation.desktop);

            // Commit only after the nested presenter and both return projections
            // have succeeded. The modal scope stays owned; only its action token
            // advances, matching StartupFlow's full-identity ACK.
            g_=std::move(next);game_=next_game;rand_=next_rand;
            club_modal_runtime_=std::move(next_runtime);
            other_games_owner_->page=result.page;
            other_games_owner_->round=result.page.projection.round;
            other_games_owner_->participant_offset=result.page.source_participant_offset;
            other_games_owner_->display_mode=result.page.projection.display_mode;
            other_games_owner_->native=save_stream(g_);
            other_games_owner_->runtime=club_modal_runtime_;
            other_games_owner_->rand_seed=rand_.seed();other_games_owner_->game_seed=game_.seed();
            other_games_owner_->rand_calls=rand_.calls();other_games_owner_->game_calls=game_.calls();
            ++revision;
            return result;
        }

        OtherGamesDispatch result;
        if (request.action == ui::OtherGamesAction::close) {
            if (request.value != -1) throw FormatError("other games: malformed close request");
            result.closed = true;
            result.page = current;
        } else {
            GameState next = g_;
            const int manager = id.manager_slot;
            const uint32_t view_at = ::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField + uint32_t(manager);
            uint8_t view = next.memory.u8(view_at);
            int competition = other_games_selection_.competition;
            int linked_page = other_games_selection_.linked_page;
            int round = other_games_owner_->round;
            int local_cursor = other_games_owner_->participant_offset;
            auto mode = other_games_owner_->display_mode;
            bool source_new_selection = false;
            // Page navigation actions (selector, linked page, previous/next round, display toggle): each is
            // validated against what the current page offers, then the page is rebuilt on a staged copy.
            switch (request.action) {
            case ui::OtherGamesAction::select_competition: {
                if (current.projection.display_mode != OtherGamesDisplayMode::detail || request.value < 0 ||
                    request.value >= int(current.projection.selectors.size()))
                    throw FormatError("other games: invalid selector slot");
                const auto& selector = current.projection.selectors[size_t(request.value)];
                if (!selector.active) throw FormatError("other games: inactive selector slot");
                const int old_competition=competition;
                if (old_competition == selector.competition &&
                    current.projection.source_participant_count > 16) {
                    local_cursor += 16;
                    if (local_cursor >= current.projection.source_participant_count) local_cursor = 0;
                    const uint8_t updated = uint8_t((view & 0x80) | (local_cursor & 0x7f));
                    next.memory.set8(view_at,updated);
                    view = updated;
                } else {
                    // CupGroupOverviewEffects compares the selected ID with the clicked SLOT before it
                    // stores the selector ID. These are intentionally different values.
                    if (old_competition != request.value) {
                        local_cursor = 0;
                        linked_page = 0;
                        const uint8_t updated = uint8_t(view & 0x80);
                        next.memory.set8(view_at,updated);
                        view = updated;
                    }
                    competition = selector.competition;
                    local_cursor = int(view & 0x7f);
                    round = -1;
                    source_new_selection = old_competition != competition;
                }
                break;
            }
            case ui::OtherGamesAction::select_linked_page:
                if (current.projection.display_mode != OtherGamesDisplayMode::detail || request.value < 0 ||
                    request.value >= int(current.projection.linked_pages.size()) ||
                    current.projection.linked_pages[size_t(request.value)].ordinal != request.value)
                    throw FormatError("other games: invalid linked-page ordinal");
                linked_page = request.value;
                round = -1;
                local_cursor = int(view & 0x7f);
                break;
            case ui::OtherGamesAction::previous_round:
            case ui::OtherGamesAction::next_round:
                if (request.value != -1 || !current.projection.round_arrows_visible)
                    throw FormatError("other games: invalid round action");
                round = request.action == ui::OtherGamesAction::previous_round
                    ? (round + current.projection.rounds - 1) % current.projection.rounds
                    : (round + 1) % current.projection.rounds;
                local_cursor = 0;
                break;
            case ui::OtherGamesAction::toggle_display_mode:
                if (request.value != -1 || !current.projection.display_mode_toggle_available ||
                    current.projection.unfinished_predecessor || !current.projection.fixtures_available)
                    throw FormatError("other games: display toggle is unavailable");
                view ^= 0x80;
                next.memory.set8(view_at,view);
                mode = (view & 0x80) ? OtherGamesDisplayMode::detail : OtherGamesDisplayMode::compact;
                break;
            case ui::OtherGamesAction::team_info:
            case ui::OtherGamesAction::close:
                throw FormatError("other games: malformed action");
            default:
                throw FormatError("other games: unknown action");
            }
            if (current.projection.source_participant_count <= 16) {
                const uint8_t updated = uint8_t(view | 0x80);
                if (updated != view) next.memory.set8(view_at,updated);
                view = updated; mode = OtherGamesDisplayMode::detail;
            }
            ui::OtherGamesIdentity next_id = id;
            next_id.revision = revision + 1;
            next_id.sequence = id.sequence + 1;
            next_id.expected_view_byte = view;
            auto page = ui::other_games_state_from_game(next,next_id,texts_,exe_,competition,linked_page,
                round,mode,local_cursor,source_new_selection,current.underlay,club_modal_runtime_);
            if (page.projection.source_participant_count <= 16 && mode != OtherGamesDisplayMode::detail) {
                const uint8_t updated = uint8_t(view | 0x80);
                next.memory.set8(view_at,updated);
                next_id.expected_view_byte = updated;
                page.identity = next_id;
                mode = OtherGamesDisplayMode::detail;
            }
            page.source_participant_offset = local_cursor;
            result.page = std::move(page);
            result.preparation = preparation_projection(next,revision+1);
            g_ = std::move(next);
            other_games_selection_ = {competition,linked_page};
            other_games_owner_->page = result.page;
            other_games_owner_->round = result.page.projection.round;
            other_games_owner_->participant_offset = local_cursor;
            other_games_owner_->display_mode = mode;
            other_games_owner_->native = save_stream(g_);
            other_games_owner_->runtime = club_modal_runtime_;
            other_games_owner_->rand_seed = rand_.seed(); other_games_owner_->game_seed = game_.seed();
            other_games_owner_->rand_calls = rand_.calls(); other_games_owner_->game_calls = game_.calls();
        }
        if (result.closed) {
            result.page.identity.revision = revision + 1;
            result.page.identity.sequence = id.sequence + 1;
            result.preparation = preparation_projection(g_,revision+1);
            other_games_owner_.reset();
        }
        ++revision;
        return result;
    } catch (const std::exception& e) {
        error = e.what();
        return {};
    }
}

// Balance plan group (desk menu 28): the plan is edited in a draft (BalanceDraft) that holds a
// copy of the state, random generators and runtime bytes. Pattern: open -> edit actions on the
// draft -> close; closing may stage a warning that needs a confirmation, and only the final
// close or confirmation moves the draft into the live state and bumps the revision.
// Opens the balance plan for the active manager's club; empty optional with 'error' on a stale,
// nested or unsupported opening (any other modal owner or dispatch counts as nested).
std::optional<ui::BalanceState> CareerSession::open_balance_plan(
    const ui::DeskRequest& request,uint64_t revision,const Image& underlay,std::string& error) {
    error.clear();
    if(request.kind!=ui::DeskRequestKind::open_menu||request.menu_kind!=28||request.revision!=revision||
       revision==UINT64_MAX||balance_draft_||balance_dispatch_active_||strengths_owner_||strengths_dispatch_active_||
       top_scorers_owner_||top_scorers_dispatch_active_ || stadium_.block_expansion||other_games_owner_||other_games_dispatch_active_||
       league_pairings_draft_||league_pairings_dispatch_active_||market_dispatch_active_||pending_offer_dispatch_active_||
       calendar_advance_active_||transfer_offer_editor_||manager_join_||request.manager_slot<0||request.manager_slot>=4||
       size_t(request.manager_slot)>=g_.manager_teams.size()||!g_.memory.has(::bmh::layout::ActiveManager)||
       g_.memory.u8(::bmh::layout::ActiveManager)!=request.manager_slot) {
        error="balance: stale, nested or unsupported opening";return {};
    }
    try {
        const int slot=request.manager_slot, runtime_club=g_.manager_teams[size_t(slot)];
        if(runtime_club<0||size_t(runtime_club)>=g_.club_ids.size()||g_.date>UINT32_MAX||next_balance_scope_==UINT64_MAX)
            throw FormatError("balance: invalid manager identity");
        BalanceDraft next{g_,game_,rand_,club_modal_runtime_,club_modal_runtime_,{},save_stream(g_),
            rand_.seed(),game_.seed(),rand_.calls(),game_.calls(),false};
        next.notice_gate={source_runtime_byte(g_,club_modal_runtime_,exe_,::bmh::layout::CompactDialogFrame),
            source_runtime_byte(g_,club_modal_runtime_,exe_,::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField2),
            source_runtime_byte(g_,club_modal_runtime_,exe_,::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField)};
        GameState opening=g_;
        if(!opening.memory.has(::bmh::layout::PlanTicketPrice))opening.memory.add_block(::bmh::layout::PlanTicketPrice,4);
        for(int i=0;i<4;++i)opening.memory.set8(::bmh::layout::PlanTicketPrice+uint32_t(i),
            source_runtime_byte(g_,club_modal_runtime_,exe_,::bmh::layout::PlanTicketPrice+uint32_t(i)));
        auto draft=bmh::open_balance_plan(opening,slot);
        if(!g_.memory.has(::bmh::layout::PlanTicketPrice+uint32_t(slot)))
            next.runtime[::bmh::layout::PlanTicketPrice+uint32_t(slot)]=uint8_t(draft.rows[kBalanceTicketPrice]);
        ui::BalanceIdentity id{slot,runtime_club,int(g_.club_ids[size_t(runtime_club)]),uint32_t(g_.date),revision,next_balance_scope_,0};
        next.page=ui::balance_from_game(g_,draft,exe_,id,underlay);
        apply_balance_source_style(next.page,g_,club_modal_runtime_,exe_);
        next.page.read_only_notice_enabled=next.notice_gate.enabled();
        next.page.read_only_notice.compact_dialog_frame=next.notice_gate.compact_dialog_frame!=0;
        next.page.close_notice.compact_dialog_frame=next.notice_gate.compact_dialog_frame!=0;
        ui::BalanceScreen validate(data_directory_,next.page);
        auto accepted_page=next.page;
        auto accepted_runtime=next.runtime;
        next.original_runtime=accepted_runtime;
        balance_draft_=std::move(next);
        club_modal_runtime_=std::move(accepted_runtime);
        ++next_balance_scope_;
        return accepted_page;
    } catch(const std::exception& e) {error=e.what();return {};}
}

// Applies one balance action to a copy of the draft: edit a row, cycle the view, take actual
// values, close, or confirm a staged warning close. Rejects a stale identity or a live state
// that differs from the draft's origin. Only a closing action commits the draft and bumps the
// revision; a staged warning keeps the draft open. Empty optional with 'error' on failure.
std::optional<CareerSession::BalanceDispatch> CareerSession::dispatch_balance_plan(
    const ui::BalanceRequest& request,uint64_t& revision,std::string& error) {
    error.clear();
    if(stadium_.block_expansion||balance_dispatch_active_||!balance_draft_||request.identity!=balance_draft_->page.identity||
       request.identity.revision!=revision||request.identity.sequence==UINT64_MAX||revision==UINT64_MAX||
       save_stream(g_)!=balance_draft_->original_stream||rand_.seed()!=balance_draft_->original_rand_seed||
       game_.seed()!=balance_draft_->original_game_seed||rand_.calls()!=balance_draft_->original_rand_calls||
       game_.calls()!=balance_draft_->original_game_calls||club_modal_runtime_!=balance_draft_->original_runtime) {
        error="balance: stale or nested owner action";return {};
    }
    auto next=*balance_draft_;
    balance_dispatch_active_=true;struct Reset {bool& x;~Reset(){x=false;}} reset{balance_dispatch_active_};
    try {
        auto& identity=next.page.identity;
        const int manager=identity.manager_slot;
        if(manager<0||manager>=4||size_t(manager)>=g_.manager_teams.size()||
           g_.manager_teams[size_t(manager)]!=identity.runtime_club||
           int(g_.club_ids[size_t(identity.runtime_club)])!=identity.club_identity||g_.date!=identity.day_counter||
           !g_.memory.has(::bmh::layout::ActiveManager)||g_.memory.u8(::bmh::layout::ActiveManager)!=manager)
            throw FormatError("balance: manager identity changed");
        BalanceDispatch result;
        if(request.kind==ui::BalanceAction::confirm_close) {
            if(!next.page.pending_close||!next.warning)throw FormatError("balance: no staged warning close");
            if(request.row!=-1||request.value)throw FormatError("balance: malformed confirmation");
            const auto before=identity;identity.revision=revision+1;identity.sequence=before.sequence+1;
            next.page.pending_close=false;next.page.close_notice={};
            result.closed=true;result.page=next.page;
            result.preparation=preparation_projection(next.state,revision+1);
            ui::validate_preparation_context(result.preparation);
            ui::DesktopScreen validate(data_directory_,result.preparation.desktop);
            g_=std::move(next.state);game_=next.game;rand_=next.rand;club_modal_runtime_=std::move(next.runtime);
            balance_draft_.reset();++revision;
            return result;
        }
        if(next.page.pending_close)throw FormatError("balance: warning confirmation is pending");
        auto draft=next.page.draft;
        switch(request.kind) {
        case ui::BalanceAction::edit_row: {
            if(request.row<0||request.row>=kBalanceRows)throw FormatError("balance: invalid row");
            const auto status=edit_balance_plan(next.state,draft,request.row,request.value);
            if(status!=BalanceEditStatus::changed)throw FormatError("balance: edit rejected");
            break;
        }
        case ui::BalanceAction::cycle_view:
            if(request.row!=-1||request.value)throw FormatError("balance: malformed view action");
            cycle_balance_view(next.state,draft);break;
        case ui::BalanceAction::take_actuals:
            if(request.row!=-1||request.value)throw FormatError("balance: malformed reset action");
            reset_balance_plan(next.state,draft);break;
        case ui::BalanceAction::close: {
            if(request.row!=-1||request.value)throw FormatError("balance: malformed close action");
            const auto original_identity=identity;
            GameState closing=next.state;
            if(!closing.memory.has(::bmh::layout::PlanTicketPrice))closing.memory.add_block(::bmh::layout::PlanTicketPrice,4);
            for(int i=0;i<4;++i)closing.memory.set8(::bmh::layout::PlanTicketPrice+uint32_t(i),
                source_runtime_byte(next.state,next.runtime,exe_,::bmh::layout::PlanTicketPrice+uint32_t(i)));
            const bool dfb_warning=bmh::close_balance_plan(closing,draft);
            next.state.manager_records=std::move(closing.manager_records);
            const uint32_t ticket_address=::bmh::layout::PlanTicketPrice+uint32_t(manager);
            if(next.state.memory.has(ticket_address))next.state.memory.set8(ticket_address,closing.memory.u8(ticket_address));
            else next.runtime[ticket_address]=closing.memory.u8(ticket_address);
            next.notice_gate={source_runtime_byte(next.state,next.runtime,exe_,::bmh::layout::CompactDialogFrame),
                source_runtime_byte(next.state,next.runtime,exe_,::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField2),
                source_runtime_byte(next.state,next.runtime,exe_,::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField)};
            next.warning=dfb_warning&&next.notice_gate.enabled();
            next.page.pending_close=next.warning;
            next.page.close_notice=next.warning?next.page.close_notice:ui::TaxDialogProjection{};
            next.page.draft=draft;
            next.page.identity.sequence=original_identity.sequence+1;
            if(next.warning) {
                auto& notice=next.page.close_notice;
                notice.kind=ui::TaxDialogKind::acknowledgement;
                notice.source_manager_flags=uint8_t(manager)|0x40;
                notice.compact_dialog_frame=next.notice_gate.compact_dialog_frame!=0;
                notice.body=message_text(next.state,::bmh::layout::BalancePlan_BalanceFromGame_TextPointer2);
                const size_t record=size_t(manager)*kManagerRecordSize;
                if(record+0x223>next.state.manager_records.size())throw FormatError("balance: missing manager display identity");
                const size_t name_at=record+0xa8;size_t name_end=name_at;
                while(name_end<name_at+30&&next.state.manager_records[name_end])++name_end;
                notice.manager_name.assign(reinterpret_cast<const char*>(next.state.manager_records.data()+name_at),name_end-name_at);
                notice.portrait=next.state.manager_records[record+0x221];
                const int club=next.state.manager_teams[size_t(manager)];
                notice.badge_fallback=original_badge_fallback(club,&exe_);
                if(!notice.badge_fallback&&club>=0&&size_t(club)*2+2<=next.state.club_heap_78.size())
                    notice.badge_sprite=(int(next.state.club_heap_78[size_t(club)*2])|
                        int(next.state.club_heap_78[size_t(club)*2+1])<<8)&0x1ff;
                else notice.badge_fallback=true;
                result.warning=true;result.page=next.page;balance_draft_=std::move(next);return result;
            }
            next.page.pending_close=false;
            next.page.identity.revision=revision+1;
            next.page.identity.sequence=original_identity.sequence+1;
            result.closed=true;result.page=next.page;
            result.preparation=preparation_projection(next.state,revision+1);
            ui::validate_preparation_context(result.preparation);
            ui::DesktopScreen validate(data_directory_,result.preparation.desktop);
            g_=std::move(next.state);game_=next.game;rand_=next.rand;club_modal_runtime_=std::move(next.runtime);
            balance_draft_.reset();++revision;return result;
        }
        default: throw FormatError("balance: unsupported action");
        }
        const int calculator_x=next.page.calculator_x,calculator_y=next.page.calculator_y;
        next.page.draft=draft;next.page.identity.sequence=identity.sequence+1;
        next.page=ui::balance_from_game(next.state,draft,exe_,next.page.identity,next.page.underlay);
        next.page.calculator_x=calculator_x;next.page.calculator_y=calculator_y;
        apply_balance_source_style(next.page,next.state,next.runtime,exe_);
        next.notice_gate={source_runtime_byte(next.state,next.runtime,exe_,::bmh::layout::CompactDialogFrame),
            source_runtime_byte(next.state,next.runtime,exe_,::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField2),
            source_runtime_byte(next.state,next.runtime,exe_,::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField)};
        next.page.read_only_notice_enabled=next.notice_gate.enabled();
        next.page.read_only_notice.compact_dialog_frame=next.notice_gate.compact_dialog_frame!=0;
        next.page.close_notice.compact_dialog_frame=next.notice_gate.compact_dialog_frame!=0;
        ui::BalanceScreen validate(data_directory_,next.page);
        result.page=next.page;balance_draft_=std::move(next);return result;
    } catch(const std::exception& e) {error=e.what();return {};}
}

// League pairings group (desk menu 1): like the balance plan it works on a draft copy
// (LeaguePairingsDraft) that remembers the active match groups and referee profiles of the turn
// driver, so a draft is only valid while those are unchanged.
// Opens the pairings page for the manager; empty optional with 'error' on a stale, nested or
// unsupported opening.
std::optional<ui::LeaguePairingsState> CareerSession::open_league_pairings(
    const ui::DeskRequest& request,uint64_t revision,std::string& error) {
    error.clear();
    if(league_family_teaminfo_active_||request.kind!=ui::DeskRequestKind::open_menu||request.menu_kind!=1||request.revision!=revision||
        revision==std::numeric_limits<uint64_t>::max()||strengths_owner_||strengths_dispatch_active_||balance_draft_||balance_dispatch_active_||
        top_scorers_owner_||top_scorers_dispatch_active_ || stadium_.block_expansion||other_games_owner_||other_games_dispatch_active_||league_pairings_draft_||league_pairings_dispatch_active_||
        market_dispatch_active_||pending_offer_dispatch_active_||calendar_advance_active_||transfer_offer_editor_||
        manager_join_||request.manager_slot<0||request.manager_slot>=4) {
        error="pairings: stale, nested or unsupported opening";return std::nullopt;
    }
    try {
        LeaguePairingsDraft next{g_,game_,rand_,club_modal_runtime_,club_modal_runtime_,{}, {},
            save_stream(g_),rand_.seed(),game_.seed(),rand_.calls(),game_.calls()};
        for(const auto& group:turn_driver_.groups)
            next.original_active_groups.emplace_back(group.desc,group.fixtures);
        next.original_referee_profiles=turn_driver_.referees;
        next.owner=open_league_pairings_source(next.state,request.manager_slot,ui::league_pairings_constants(exe_));
        next.active_packet=validate_normal_pairings_group(next.state,next.owner,next.runtime);
        const uint64_t scope=++league_pairings_scope_;
        auto page=ui::league_pairings_from_game(next.state,next.owner,exe_,revision,scope,0,next.active_packet);
        ui::LeaguePairingsScreen validate(data_directory_,page);
        next.identity=page.identity;
        next.runtime[::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField]=exe_.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField+uint32_t(next.owner.league));
        pairings_runtime_draw(next.state,next.runtime,page,exe_);
        league_pairings_draft_=std::move(next);return page;
    } catch(const std::exception& e) {error=e.what();return std::nullopt;}
}

// Refuses to continue while the original's process timer is running (not implemented), then asks
// the pairings source for the active-match packet of the running groups and records the active
// group and phase in the owner. Returns the packet, or nothing when no match group is active.
std::optional<LeaguePairingsActivePacket> CareerSession::validate_normal_pairings_group(
    const GameState& state,LeaguePairingsOwner& owner,
    const std::map<uint32_t,uint8_t>& runtime) const {
    //Sample synthesizes event8 only for a running process timer. These globals
    //are not serialized Native pointers. The normal port starts them at zero.
    const auto byte=[&](uint32_t address) {if(state.memory.has(address))return state.memory.u8(address);
        const auto at=runtime.find(address);return at==runtime.end()?uint8_t(0):at->second;};
    const unsigned duration=byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField);
    const uint32_t started=uint32_t(byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField2))|(uint32_t(byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField3))<<8)|
        (uint32_t(byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField4))<<16)|(uint32_t(byte(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField5))<<24);
    if(duration<200&&started!=0)
        throw FormatError("pairings: active process timer presentation is not implemented");
    auto packet=league_pairings_active_packet_source(state,owner,turn_driver_.groups,turn_driver_.referees);
    owner.active_group=packet?packet->group_index:-1;
    if(!packet)owner.active_phase=false;
    if(packet)validate_league_pairings_active_packet_source(state,owner,*packet);
    return packet;
}

// Applies one pairings event (page change, computer tip, team info, close) to a copy of the
// draft. The live state must still equal the draft's origin (live_matches below). On close the
// draft is committed and the revision advances; otherwise a new page is returned and the draft
// refreshed. Empty optional with 'error' when stale, nested or a presenter stops.
std::optional<CareerSession::LeaguePairingsDispatch> CareerSession::dispatch_league_pairings(
    const ui::LeaguePairingsRequest& request,uint64_t& revision,std::string& error,
    const TransferOfferTeamInfoPresenter& team_info) {
    if (league_family_teaminfo_active_ || balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return std::nullopt; }
    error.clear();
    // True while the live state, random counters, runtime bytes, match groups and referee profiles
    // still equal what the draft started from.
    const auto live_matches=[&] {
        if(!league_pairings_draft_)return false;
        const auto& d=*league_pairings_draft_;
        if(d.original_active_groups.size()!=turn_driver_.groups.size()||
            d.original_referee_profiles!=turn_driver_.referees)return false;
        for(size_t n=0;n<turn_driver_.groups.size();++n)
            if(d.original_active_groups[n].first!=turn_driver_.groups[n].desc||
                d.original_active_groups[n].second!=turn_driver_.groups[n].fixtures)return false;
        return revision==d.identity.revision&&save_stream(g_)==d.original_stream&&
            rand_.seed()==d.original_rand_seed&&game_.seed()==d.original_game_seed&&
            rand_.calls()==d.original_rand_calls&&game_.calls()==d.original_game_calls&&
            club_modal_runtime_==d.original_runtime;
    };
    if(balance_draft_||balance_dispatch_active_||!league_pairings_draft_||league_pairings_dispatch_active_||top_scorers_owner_||top_scorers_dispatch_active_||other_games_owner_||other_games_dispatch_active_||market_dispatch_active_||
        pending_offer_dispatch_active_||calendar_advance_active_||
        request.identity!=league_pairings_draft_->identity||!live_matches()||
        request.identity.sequence==std::numeric_limits<uint64_t>::max()||
        revision==std::numeric_limits<uint64_t>::max()) {
        error="pairings: stale or nested family action";return std::nullopt;
    }
    if(request.event.kind==LeaguePairingsAction::team_info&&!team_info) {
        error="pairings: TeamInfo presenter unavailable";return std::nullopt;
    }
    auto next=*league_pairings_draft_;
    league_pairings_dispatch_active_=true;
    struct Reset {bool& value;~Reset(){value=false;}} reset{league_pairings_dispatch_active_};
    try {
        auto bound_owner=next.owner;
        const auto bound_packet=validate_normal_pairings_group(next.state,bound_owner,next.runtime);
        if(bound_packet!=next.active_packet||bound_owner.active_phase!=next.owner.active_phase||
            bound_owner.active_group!=next.owner.active_group)
            throw FormatError("pairings: stale staged active presentation packet");
        next.owner=edit_league_pairings_source(next.state,next.owner,request.event,
            ui::league_pairings_constants(exe_),next.game,next.rand);
        if(request.event.kind==LeaguePairingsAction::computer_tip) {
            const auto put_scratch=[&](uint32_t address,uint8_t value) {
                if(next.state.memory.has(address))next.state.memory.set8(address,value);
                else next.runtime[address]=value;
            };
            // ComputeLeagueTableForecast's final 17295(0x3fc) clears the strength scratch before
            // the ordinary list redraw repopulates it from visible away rows.
            put_scratch(::bmh::layout::CareerSession_FormatError_ExecutableField3,0);put_scratch(::bmh::layout::CareerSession_FormatError_ExecutableField4,0);
        }
        if(!next.owner.closed)next.active_packet=validate_normal_pairings_group(next.state,next.owner,next.runtime);
        if(request.event.kind==LeaguePairingsAction::team_info) {
            const auto parent=ui::league_pairings_from_game(next.state,next.owner,exe_,revision,
                next.identity.scope,next.identity.sequence,next.active_packet);
            auto services=staged_club_modal_services(next.state,next.game,next.rand,next.runtime);
            if(!team_info(services,request.event.value,ui::LeaguePairingsScreen(data_directory_,parent).render())) {
                error="pairings: TeamInfo presenter stopped";return std::nullopt;
            }
            const auto returned_packet=validate_normal_pairings_group(next.state,next.owner,next.runtime);
            if(returned_packet!=next.active_packet)
                throw FormatError("pairings: active presentation packet changed during TeamInfo");
        }
        if(!live_matches()) {error="pairings: live state changed during modal";return std::nullopt;}
        LeaguePairingsDispatch result;result.closed=next.owner.closed;
        if(result.closed) {
            result.preparation=preparation_projection(next.state,revision+1);
            ui::validate_preparation_context(result.preparation);
            ui::DesktopScreen validate(data_directory_,result.preparation.desktop);
            // Move returns with event8;53435 stirs once and leaves.
            next.runtime[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2]=8;
            g_=std::move(next.state);game_=next.game;rand_=next.rand;club_modal_runtime_=std::move(next.runtime);
            league_pairings_draft_.reset();++revision;
        } else {
            result.page=ui::league_pairings_from_game(next.state,next.owner,exe_,revision,
                next.identity.scope,next.identity.sequence+1,next.active_packet);
            ui::LeaguePairingsScreen validate(data_directory_,result.page);
            pairings_runtime_draw(next.state,next.runtime,result.page,exe_);
            next.identity=result.page.identity;league_pairings_draft_=std::move(next);
        }
        return result;
    } catch(const std::exception& e) {error=e.what();return std::nullopt;}
}

}  // namespace bmh::app
