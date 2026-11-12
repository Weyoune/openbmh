// SPDX-License-Identifier: GPL-3.0-only
// CareerSession: the league family: tables, forecasts, friendly opponent table, phase results, running overview.
// (moved out of career_session.cpp unchanged; the class is declared in app/career_session.hpp)
#include "app/career_session_internal.hpp"

namespace bmh::app {
namespace {

// Builds the "first-end" round range label of the league table from a text template, patching
// the two two-digit numbers in the German or English layout.
std::string source_table_range_label(const TextDb& texts,int first,int end) {
    // DE50860 patches offsets 4/7 of its nine-byte TEXTD82 buffer. EN1e83f
    // patches 0/3 of the full " 0-34 only" string instead (the final y is live).
    const auto fixed=texts.raw(82,9);
    auto raw=league_system().english_competitions ? texts[82] : std::string(fixed.begin(),fixed.end());
    const size_t start=league_system().english_competitions ? 0 : 4;
    const size_t finish=league_system().english_competitions ? 3 : 7;
    if(raw.size()<finish+2)throw FormatError("league table: incomplete Source range label");
    auto patch_number=[&](size_t at,int value) {
        const int tens=value/10,ones=value%10;
        raw[at]=uint8_t(char(tens+'0'));raw[at+1]=uint8_t(char(ones+'0'));
    };
    patch_number(start,first+1);patch_number(finish,end);
    return raw;
}

// Clamps the table end round to the played round when it is out of range, ensures end > first
// and refreshes the retained range label; throws FormatError when the league is unknown.
void source_table_normalize_range(CareerSession::LeagueFamilyOwner::TableOwner& table,
                                 const GameState& game,int league,const TextDb& texts) {
    if(league<0||league>=int(game.league_rounds.size()))
        throw FormatError("league table: missing Source round byte");
    const int8_t raw_end=source_byte(table.end_round);
    const int8_t raw_first=source_byte(table.first_round);
    const uint8_t raw_round=game.league_rounds[size_t(league)];
    const int max_round=bmh::league_matchdays(league);
    if(max_round<int(raw_end)||raw_end<=raw_first||int(raw_round)<int(raw_end))
        table.end_round=int(game.league_rounds[size_t(league)]);
    if(source_byte(table.end_round)<=source_byte(table.first_round))
        table.end_round=table.first_round+1;
    table.retained_range_label_source_bytes=source_table_range_label(
        texts,source_byte(table.first_round),source_byte(table.end_round));
}

// True when a league-family identity still describes the live game: same active manager, club,
// club identity, date, default league and the stored settings/display-mode words.
bool source_table_identity_is_live(const GameState& game,const ui::StrengthsIdentity& id) {
    if(id.manager<0||id.manager>3||id.league<0||id.league>=bmh::kLeagueCount||
       !game.memory.has(::bmh::layout::ActiveManager)||game.memory.u8(::bmh::layout::ActiveManager)!=id.manager||
       size_t(id.manager)>=game.manager_teams.size()||game.manager_teams[size_t(id.manager)]!=id.runtime_club||
       game.date!=id.day_counter||id.runtime_club<0||size_t(id.runtime_club)>=game.club_ids.size()||
       int(game.club_ids[size_t(id.runtime_club)])!=id.club_identity||
       id.league_group>1) return false;
    const size_t record=size_t(id.manager)*kManagerRecordSize;
    if(record+0x212>game.manager_records.size()||
       id.manager_default_league!=int(game.manager_records[record+0x210])) return false;
    const uint32_t settings=::bmh::layout::ManagerSettings+uint32_t(id.manager)*2;
    const uint32_t family=::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2;
    return game.memory.has(settings+1)&&game.memory.has(family+1)&&
        game.memory.u16(settings)==id.expected_settings_word&&
        game.memory.u16(family)==id.expected_family_word;
}

// Sets the table aggregation flag bits from the clear bits 4 and 2 of the display-mode word.
void source_table_apply_entry_flags(CareerSession::LeagueFamilyOwner::TableOwner& table,uint16_t word) {
    if((word&4)==0)table.aggregation_flags=uint8_t(table.aggregation_flags|1);
    if((word&2)==0)table.aggregation_flags=uint8_t(table.aggregation_flags|2);
}

// Packs a table owner's round range, flags, display word and optional accepted engine table
// (or forecast marker) into the projection context used to render the league table page.
ui::LeagueTableProjectionContext source_table_context(
    const CareerSession::LeagueFamilyOwner::TableOwner& table,uint16_t display_word,
    std::optional<bmh::LeagueTable> accepted={},bool forecast=false) {
    ui::LeagueTableProjectionContext context;
    context.first_round=table.first_round;context.end_round=table.end_round;
    context.aggregation_flags=table.aggregation_flags;context.normal_mode=table.normal_mode;
    context.display_word_override=display_word;
    context.retained_range_label_source_bytes=table.retained_range_label_source_bytes;
    if(accepted) {context.accepted_engine_league=accepted->league;context.accepted_engine_table=std::move(accepted);}
    context.forecast=forecast;return context;
}

}  // namespace

// League family (table / pairings / strengths and the friendly opponent table): these three
// child screens are linked under one LeagueFamilyOwner carrying a family id. Pattern: each
// entry or transition validates the identity (revision, active manager slot, club, date) and
// the neighbouring child owners, snapshots the native save stream, random seeds and runtime
// bytes into the owner, and every close/suspend compares the live state with that snapshot so
// a stale or tampered request is rejected with an error text and nothing is changed.
// Child numbers: 1 = Table, 2 = Pairings, 3 = Strengths.
// Builds the identity (manager, club, date, default league, settings words) needed to open the
// strengths screen for the active manager; returns an empty optional when anything is missing.
std::optional<ui::StrengthsRequest> CareerSession::strengths_open_request(uint64_t revision) const {
    if(!g_.memory.has(::bmh::layout::ActiveManager))return {};
    const int manager=g_.memory.u8(::bmh::layout::ActiveManager);
    if(manager<0||manager>3||size_t(manager)>=g_.manager_teams.size())return {};
    const int club=g_.manager_teams[size_t(manager)];
    const uint32_t setting=::bmh::layout::ManagerSettings+uint32_t(manager)*2,family=::bmh::layout::LeagueTableDisplayModes+uint32_t(manager)*2;
    if(club<0||size_t(club)>=g_.club_ids.size()||!g_.memory.has(setting+1)||!g_.memory.has(family+1))return {};
    ui::StrengthsRequest request;
    request.identity={manager,club,int(g_.club_ids[size_t(club)]),club>356?0:team_league(club),g_.date,
        revision,0,0,g_.memory.u16(setting),g_.memory.u16(family)};
    request.identity.league_group=uint8_t(request.identity.league>=5);
    const size_t record=size_t(manager)*kManagerRecordSize;
    if(record+0x212<=g_.manager_records.size())
        request.identity.manager_default_league=int(g_.manager_records[record+0x210]);
    else return {};
    return request;
}

// Opens the family at 'child' for the identity: checks no family/TeamInfo is active, the
// revision matches, the identity belongs to the active manager and club of today (or to the
// live overview owner), and that Pairings/Strengths entries have a matching child owner.
// Allocates a family id, snapshots the state into a LeagueFamilyOwner and, for the table, builds
// the page. Returns the bound identity, or an empty optional with 'error'.
std::optional<ui::StrengthsIdentity> CareerSession::begin_league_family(
    ui::StrengthsIdentity identity,uint8_t child,uint64_t revision,std::string& error) {
    error.clear();
    if(league_family_teaminfo_active_||league_family_owner_||child<1||child>3||revision==UINT64_MAX||identity.revision!=revision||
       (identity.source_param2!=0&&!live_source_identity(identity))||identity.family_id||identity.manager<0||identity.manager>3||
       identity.league<0||identity.league>8||!g_.memory.has(::bmh::layout::ActiveManager)||g_.memory.u8(::bmh::layout::ActiveManager)!=identity.manager||
       size_t(identity.manager)>=g_.manager_teams.size()||g_.manager_teams[size_t(identity.manager)]!=identity.runtime_club||
       identity.day_counter!=g_.date||identity.runtime_club<0||size_t(identity.runtime_club)>=g_.club_ids.size()||
       int(g_.club_ids[size_t(identity.runtime_club)])!=identity.club_identity||next_league_family_id_==UINT64_MAX) {
        error="league family: invalid or nested family entry";return {};
    }
    if(child==1&&(strengths_owner_||league_pairings_draft_)) {
        error="league family: Table entry has another child owner";return {};
    }
    if(child==2&&(!league_pairings_draft_||strengths_owner_||
       league_pairings_draft_->owner.manager!=identity.manager||
       league_pairings_draft_->owner.runtime_club!=identity.runtime_club||
       league_pairings_draft_->owner.club_identity!=identity.club_identity)) {
        error="league family: Pairings entry has no matching child owner";return {};
    }
    if(child==3&&(!strengths_owner_||strengths_owner_->page.identity.manager!=identity.manager||
       strengths_owner_->page.identity.runtime_club!=identity.runtime_club||
       strengths_owner_->page.identity.revision!=revision)) {
        error="league family: Strengths entry has no matching child owner";return {};
    }
    identity.family_id=next_league_family_id_++;
    if(child==3) {
        identity=strengths_owner_->page.identity;
        identity.family_id=next_league_family_id_-1;
        strengths_owner_->page.identity=identity;
        strengths_owner_->native=save_stream(g_);
    }
    LeagueFamilyOwner owner;owner.identity=identity;owner.child=child;owner.native=save_stream(g_);
    owner.rand_seed=rand_.seed();owner.game_seed=game_.seed();owner.rand_calls=rand_.calls();
    owner.game_calls=game_.calls();owner.runtime=club_modal_runtime_;
    if(child==1) {
        LeagueFamilyOwner::TableOwner table;table.end_round=int(g_.league_rounds.at(size_t(identity.league)));
        uint16_t word=uint16_t(g_.memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(identity.manager)*2)&0xff7f);
        g_.memory.set16(::bmh::layout::LeagueTableDisplayModes+uint32_t(identity.manager)*2,word);
        identity.expected_family_word=word;owner.identity=identity;
        table.display_word_override=word;
        source_table_apply_entry_flags(table,word);
        table.normal_mode=source_runtime_byte(g_,club_modal_runtime_,exe_,::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField)&1;
        table.runtime_font_id=exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + 1);
        source_table_normalize_range(table,g_,identity.league,texts_);
        table.page=ui::league_table_from_game(g_,identity.manager,identity.league,
            source_table_context(table,table.display_word_override));
        owner.table=std::move(table);
    }
    owner.identity=identity;owner.native=save_stream(g_);owner.runtime=club_modal_runtime_;
    owner.rand_seed=rand_.seed();owner.game_seed=game_.seed();owner.rand_calls=rand_.calls();owner.game_calls=game_.calls();
    league_family_owner_=std::move(owner);
    return identity;
}

// Opens the table used to pick a friendly-match opponent (child 1 with friendly_opponent set).
// Needs the strengths request of the active manager and no other child owner; the table
// display word is initialised for that picker. Any failure restores the family checkpoint.
std::optional<ui::StrengthsIdentity> CareerSession::begin_friendly_opponent_table(
        uint64_t revision,std::string& error) {
    error.clear();
    const auto request=strengths_open_request(revision);
    if(!request||request->identity.runtime_club>=world_layout().first_foreign_cup()||request->identity.source_param2||
       league_family_owner_||strengths_owner_||league_pairings_draft_) {
        error="friendly table: unavailable club entry";return {};
    }
    const auto checkpoint=league_family_checkpoint();
    try {
        auto identity=request->identity;
        // Desktop case6 initializes these before its direct FriendlyMatchScreen(2,0).
        const uint32_t word_at=::bmh::layout::LeagueTableDisplayModes+uint32_t(identity.manager)*2;
        const auto mode=exe_.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField+uint32_t(identity.league));
        uint16_t word=uint16_t((g_.memory.u16(word_at)&0xfc0f)|0x0170);
        if(mode)word|=0x80;
        g_.memory.set16(word_at,word);
        club_modal_runtime_[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField2]=uint8_t(identity.league);
        club_modal_runtime_[::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField]=0;identity.league_group=0;
        club_modal_runtime_[::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField]=mode;club_modal_runtime_[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField]=0;
        club_modal_runtime_[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2]=0;
        identity.expected_family_word=word;
        const auto bound=begin_league_family(identity,1,revision,error);
        if(!bound)throw FormatError(error.empty()?"friendly table: owner entry rejected":error);
        league_family_owner_->friendly_opponent=true;
        return bound;
    } catch(const std::exception& e) {
        restore_league_family_checkpoint(checkpoint);error=e.what();return {};
    }
}

// Checks a select_opponent request against the owned table (identity, revision, row, club
// identity key) and returns the runtime team of the chosen row, or an empty optional with 'error'.
// Does not change state.
std::optional<int> CareerSession::resolve_friendly_table_opponent(
        const ui::LeagueFamilyRequest& request,uint64_t revision,std::string& error) const {
    error.clear();
    if(!league_family_owner_||!league_family_owner_->friendly_opponent||
       league_family_owner_->identity!=request.identity||request.identity.revision!=revision||
       request.kind!=ui::LeagueFamilyRequestKind::select_opponent||request.from_child!=1||
       request.target_child||request.control_index!=-1||request.league_id!=-1||request.table_dialog_reply) {
        error="friendly table: stale opponent selection";return {};
    }
    const auto dispatch=current_league_table_dispatch(error);
    if(!dispatch||dispatch->phase!=TableDispatchPhase::ready)return {};
    const auto& table=*league_family_owner_->table;
    const int row=request.table_row_index;
    if(row<0||size_t(row)>=dispatch->page.rows.size()||size_t(row)>=table.page.rows.size()) {
        error="friendly table: opponent row outside the owned page";return {};
    }
    const auto& visible=dispatch->page.rows[size_t(row)];
    const auto& normal=table.page.rows[size_t(row)];
    if(visible.runtime_team_id!=request.runtime_team||visible.club_identity_key!=request.expected_club_identity||
       visible.runtime_team_id<0||size_t(visible.runtime_team_id)>=g_.club_ids.size()||
       visible.club_identity_key!=int(g_.club_ids[size_t(visible.runtime_team_id)])||
       normal.rank!=row+1||normal.runtime_team_id<0||normal.runtime_team_id>=world_layout().first_foreign_cup()||
       size_t(normal.runtime_team_id)>=g_.club_ids.size()||
       normal.club_identity_key!=int(g_.club_ids[size_t(normal.runtime_team_id)])) {
        error="friendly table: opponent row identity changed";return {};
    }
    // After a forecast Present FriendlyMatchScreen resolves TableRuntimeField6's restored normal ranks.
    return normal.runtime_team_id;
}

// The player picked their own club in the opponent table: restores the normal-mode bit of the
// display word, advances the family sequence and keeps the picker open. Empty optional on a
// stale or different-club request.
std::optional<ui::StrengthsIdentity> CareerSession::acknowledge_friendly_own_club(
        const ui::LeagueFamilyRequest& request,uint64_t revision,std::string& error) {
    const auto team=resolve_friendly_table_opponent(request,revision,error);
    if(!team||*team!=request.identity.runtime_club||request.identity.family_sequence==UINT64_MAX) {
        if(error.empty())error="friendly table: invalid own-club return";
        return {};
    }
    auto& owner=*league_family_owner_;auto next=owner.identity;
    uint16_t word=uint16_t((next.expected_family_word&0x0f7f)|(owner.table->normal_mode?0x80:0));
    auto state=g_;state.memory.set16(::bmh::layout::LeagueTableDisplayModes+uint32_t(next.manager)*2,word);
    auto native=save_stream(state);
    ++next.family_sequence;next.expected_family_word=word;
    g_=std::move(state);owner.native=std::move(native);owner.identity=next;
    // Keep the visible forecast/body and normal ranks: this is the inner loop.
    return next;
}

// Leaves the opponent table: validates a select_opponent (not the own club) or close request,
// restores the display word, validates the new preparation projection and drops the family
// owner while advancing the revision. Failure restores the checkpoint and returns empty.
std::optional<ui::PreparationState> CareerSession::finish_friendly_opponent_table(
        const ui::LeagueFamilyRequest& request,uint64_t& revision,std::string& error) {
    error.clear();
    if(!league_family_owner_||!league_family_owner_->friendly_opponent||
       request.identity!=league_family_owner_->identity||request.identity.revision!=revision||
       revision==UINT64_MAX||request.from_child!=1||request.target_child) {
        error="friendly table: stale return";return {};
    }
    if(request.kind==ui::LeagueFamilyRequestKind::select_opponent) {
        const auto team=resolve_friendly_table_opponent(request,revision,error);
        if(!team||*team==request.identity.runtime_club) {
            if(error.empty())error="friendly table: own club keeps the picker open";
            return {};
        }
    } else if(request.kind!=ui::LeagueFamilyRequestKind::close||!canonical_table_envelope(request,-1)) {
        error="friendly table: invalid return envelope";return {};
    }
    const auto dispatch=current_league_table_dispatch(error);
    if(!dispatch||dispatch->phase!=TableDispatchPhase::ready)return {};
    const auto checkpoint=league_family_checkpoint();
    try {
        const uint32_t word_at=::bmh::layout::LeagueTableDisplayModes+uint32_t(request.identity.manager)*2;
        uint16_t word=uint16_t(g_.memory.u16(word_at)&0x0f7f);
        if(league_family_owner_->table->normal_mode)word|=0x80;
        g_.memory.set16(word_at,word);
        auto preparation=preparation_projection(revision+1);
        ui::validate_preparation_context(preparation);
        ui::DesktopScreen validate(data_directory_,preparation.desktop);
        // Direct table return: no LeagueScreenShown family teardown / stir_random.
        league_family_owner_.reset();++revision;
        return preparation;
    } catch(const std::exception& e) {
        restore_league_family_checkpoint(checkpoint);error=e.what();return {};
    }
}

// Moves the family from one child screen to another: the request, owner and 'next' identity must
// agree (same family id, manager, club, day; sequence increased by one; revision at most one
// ahead). Replaces the owner with a fresh snapshot, building the table page when the target is
// the table. Returns false with 'error' when the transition is stale or mismatched.
bool CareerSession::advance_league_family(const ui::LeagueFamilyRequest& request,
    ui::StrengthsIdentity next,uint8_t target,uint64_t revision,std::string& error) {
    error.clear();
    if(league_family_teaminfo_active_||!league_family_owner_||league_family_owner_->friendly_opponent||league_family_owner_->child!=request.from_child||
       league_family_owner_->identity!=request.identity||request.identity.family_id==0||
       request.kind!=ui::LeagueFamilyRequestKind::navigate||target<1||target>3||target!=request.target_child||
       revision<request.identity.revision||revision-request.identity.revision>1u||
       next.revision!=revision||next.family_id!=request.identity.family_id||
       next.manager!=request.identity.manager||next.runtime_club!=request.identity.runtime_club||
       next.club_identity!=request.identity.club_identity||next.day_counter!=request.identity.day_counter||
       next.source_param2!=request.identity.source_param2||next.manager_default_league!=request.identity.manager_default_league||
       request.identity.family_sequence==UINT64_MAX||next.family_sequence!=request.identity.family_sequence+1||
       next.league<0||next.league>8||
       (request.from_child==1&&league_family_owner_->table&&
        league_family_owner_->table->phase!=TableDispatchPhase::ready)) {
        error="league family: stale or mismatched child transition";return false;
    }
    auto owner=*league_family_owner_;owner.identity=next;owner.child=target;owner.native=save_stream(g_);
    owner.rand_seed=rand_.seed();owner.game_seed=game_.seed();owner.rand_calls=rand_.calls();
    owner.game_calls=game_.calls();owner.runtime=club_modal_runtime_;
    owner.table.reset();
    if(target==1) {
        LeagueFamilyOwner::TableOwner table;table.end_round=int(g_.league_rounds.at(size_t(next.league)));
        uint16_t word=uint16_t(g_.memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(next.manager)*2)&0xff7f);
        g_.memory.set16(::bmh::layout::LeagueTableDisplayModes+uint32_t(next.manager)*2,word);
        next.expected_family_word=word;owner.identity=next;
        table.display_word_override=word;
        source_table_apply_entry_flags(table,word);
        table.normal_mode=source_runtime_byte(g_,club_modal_runtime_,exe_,::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField)&1;
        table.runtime_font_id=exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + 1);
        source_table_normalize_range(table,g_,next.league,texts_);
        table.page=ui::league_table_from_game(g_,next.manager,next.league,
            source_table_context(table,table.display_word_override));
        owner.table=std::move(table);
    }
    owner.identity=next;owner.native=save_stream(g_);owner.runtime=club_modal_runtime_;
    owner.rand_seed=rand_.seed();owner.game_seed=game_.seed();owner.rand_calls=rand_.calls();owner.game_calls=game_.calls();
    league_family_owner_=std::move(owner);return true;
}

// True when the identity (its source_param2 holds the match minute) belongs to the currently
// open live overview: same manager, team, minute and day, and for a phase-result viewer the
// same match group.
bool CareerSession::live_source_identity(const ui::StrengthsIdentity& id) const {
    return live_overview_owner_ &&
        live_overview_owner_->kind != LiveOverviewKind::cup_other_games &&
        live_overview_owner_->request.manager_slot == id.manager &&
        live_overview_owner_->request.runtime_team == id.runtime_club &&
        live_overview_owner_->request.minute >= 0 && live_overview_owner_->request.minute <= 255 &&
        id.source_param2 == uint8_t(live_overview_owner_->request.minute) &&
        id.day_counter == uint32_t(g_.date) &&
        (live_overview_owner_->request.group_index == 0 ||
         (live_overview_owner_->phase_result && phase_result_request_ &&
          live_overview_owner_->request.group_index == int(phase_result_request_->group_index)));
}

// Final close of the family: the long condition re-verifies identity, child owner and that the
// live state, random generators and runtime bytes still equal the owner's snapshot. On success
// it commits staged Pairings changes (if any), draws the random stir unless a phase-result
// viewer owns that draw, resets the child owners and bumps the revision. Returns the new
// preparation projection, or an empty optional with 'error' and no change.
std::optional<ui::PreparationState> CareerSession::close_league_family(
    const ui::LeagueFamilyRequest& request,uint64_t& revision,std::string& error) {
    error.clear();const auto& id=request.identity;
    if(league_family_teaminfo_active_||request.kind!=ui::LeagueFamilyRequestKind::close||request.target_child!=0||
       request.from_child<1||request.from_child>3||revision==UINT64_MAX||id.revision!=revision||
       id.manager<0||id.manager>3||!g_.memory.has(::bmh::layout::ActiveManager)||g_.memory.u8(::bmh::layout::ActiveManager)!=id.manager||
       size_t(id.manager)>=g_.manager_teams.size()||id.runtime_club!=g_.manager_teams[size_t(id.manager)]||
       id.day_counter!=g_.date||id.runtime_club<0||size_t(id.runtime_club)>=g_.club_ids.size()||
       id.club_identity!=int(g_.club_ids[size_t(id.runtime_club)])||
       (id.source_param2!=0&&!live_source_identity(id))||
       !league_family_owner_||league_family_owner_->friendly_opponent||league_family_owner_->child!=request.from_child||
       league_family_owner_->identity!=id||
       strengths_dispatch_active_||league_pairings_dispatch_active_||
       (request.from_child==1&&(strengths_owner_||league_pairings_draft_))||
       (request.from_child==2&&(!league_pairings_draft_||strengths_owner_||
          save_stream(g_)!=league_pairings_draft_->original_stream||
          rand_.seed()!=league_pairings_draft_->original_rand_seed||game_.seed()!=league_pairings_draft_->original_game_seed||
          rand_.calls()!=league_pairings_draft_->original_rand_calls||game_.calls()!=league_pairings_draft_->original_game_calls||
          club_modal_runtime_!=league_pairings_draft_->original_runtime))||
       (request.from_child==3&&(!strengths_owner_||league_pairings_draft_||
          strengths_owner_->page.identity!=id||save_stream(g_)!=strengths_owner_->native||
          rand_.seed()!=strengths_owner_->rand_seed||game_.seed()!=strengths_owner_->game_seed||
          rand_.calls()!=strengths_owner_->rand_calls||game_.calls()!=strengths_owner_->game_calls||
          club_modal_runtime_!=strengths_owner_->runtime))||
       (request.from_child==1&&(save_stream(g_)!=league_family_owner_->native||
          rand_.seed()!=league_family_owner_->rand_seed||game_.seed()!=league_family_owner_->game_seed||
          rand_.calls()!=league_family_owner_->rand_calls||game_.calls()!=league_family_owner_->game_calls||
          club_modal_runtime_!=league_family_owner_->runtime||!league_family_owner_->table||
          league_family_owner_->table->phase!=TableDispatchPhase::ready))) {
        error="league family: stale or unavailable final close";return {};
    }
    try {
        GameState next_state=g_;GameRandom next_game=game_;WatcomRandom next_rand=rand_;
        auto next_runtime=club_modal_runtime_;
        auto next_pairings=league_pairings_draft_;
        if(request.from_child==2) {
            next_state=std::move(next_pairings->state);next_game=next_pairings->game;
            next_rand=next_pairings->rand;next_runtime=std::move(next_pairings->runtime);
            const uint32_t pairings_word=::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(id.manager);
            if(!next_state.memory.has(pairings_word))throw FormatError("league family: missing Pairings return word");
            next_state.memory.set8(pairings_word,uint8_t((next_state.memory.u8(pairings_word)&0xf0)|1));
        }
        // Phase viewers return to the pending engine ACK, which owns this draw.
        // Keyboard/desk family callers still leave through their existing draw.
        if(!live_overview_owner_ || !live_overview_owner_->phase_result)
            stir_random(next_game,next_rand);
        auto preparation=preparation_projection(next_state,revision+1);
        ui::validate_preparation_context(preparation);
        ui::DesktopScreen validate(data_directory_,preparation.desktop);
        g_=std::move(next_state);game_=next_game;rand_=next_rand;
        club_modal_runtime_=std::move(next_runtime);
        if(request.from_child==2)league_pairings_draft_.reset();
        if(request.from_child==3)strengths_owner_.reset();
        league_family_owner_.reset();
        ++revision;return preparation;
    } catch(const std::exception& e) {error=e.what();return {};}
}

// Leaves the Strengths child towards Table or Pairings without a full close: checks the owner
// snapshot is unchanged, then drops the strengths owner. False with 'error' when stale.
bool CareerSession::suspend_strengths_for_family(const ui::LeagueFamilyRequest& request,
    uint64_t revision,std::string& error) {
    error.clear();const auto& id=request.identity;
    if(league_family_teaminfo_active_||request.kind!=ui::LeagueFamilyRequestKind::navigate||request.from_child!=3||
       (request.target_child!=1&&request.target_child!=2)||!strengths_owner_||strengths_dispatch_active_||
       id.revision!=revision||strengths_owner_->page.identity!=id||save_stream(g_)!=strengths_owner_->native||
       rand_.seed()!=strengths_owner_->rand_seed||rand_.calls()!=strengths_owner_->rand_calls||
       game_.seed()!=strengths_owner_->game_seed||game_.calls()!=strengths_owner_->game_calls||
       !league_family_owner_||league_family_owner_->child!=3||league_family_owner_->identity!=id) {
        error="league family: stale Strengths child transition";return false;
    }
    // A nonzero Reset2 selector returns to LeagueScreenShown without a child close.
    strengths_owner_.reset();return true;
}

// Leaves the Pairings child towards Table or Strengths: after the same snapshot checks it
// commits only the staged page/redraw changes of the draft and drops the draft. False with
// 'error' when stale or the return word is missing.
bool CareerSession::suspend_pairings_for_family(const ui::LeagueFamilyRequest& request,
    uint64_t revision,std::string& error) {
    error.clear();const auto& id=request.identity;
    if(league_family_teaminfo_active_||request.kind!=ui::LeagueFamilyRequestKind::navigate||request.from_child!=2||
       (request.target_child!=1&&request.target_child!=3)||!league_pairings_draft_||
       league_pairings_dispatch_active_||strengths_owner_||id.revision!=revision||
       id.manager!=league_pairings_draft_->owner.manager||
       id.runtime_club!=league_pairings_draft_->owner.runtime_club||
       id.club_identity!=league_pairings_draft_->owner.club_identity||
       (id.source_param2!=0&&!live_source_identity(id))||
       save_stream(g_)!=league_pairings_draft_->original_stream||
       rand_.seed()!=league_pairings_draft_->original_rand_seed||game_.seed()!=league_pairings_draft_->original_game_seed||
       rand_.calls()!=league_pairings_draft_->original_rand_calls||game_.calls()!=league_pairings_draft_->original_game_calls||
       club_modal_runtime_!=league_pairings_draft_->original_runtime||!league_family_owner_||
       league_family_owner_->child!=2||league_family_owner_->identity!=id) {
        error="league family: stale Pairings child transition";return false;
    }
    // A nonzero Move selector returns to LeagueScreenShown. Commit only its staged
    // page/redraw changes; never invoke edit_league_pairings_source(close).
    GameState next_state=std::move(league_pairings_draft_->state);
    const uint32_t pairings_word=::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(id.manager);
    if(!next_state.memory.has(pairings_word)) {error="league family: missing Pairings return word";return false;}
    next_state.memory.set8(pairings_word,uint8_t((next_state.memory.u8(pairings_word)&0xf0)|1));
    g_=std::move(next_state);game_=league_pairings_draft_->game;
    rand_=league_pairings_draft_->rand;club_modal_runtime_=std::move(league_pairings_draft_->runtime);
    league_pairings_draft_.reset();return true;
}

// Prepares the manager mode word and runtime bytes before a child screen is entered, after
// validating the identity and that no other child owner or dispatch is active. False with
// 'error' otherwise.
bool CareerSession::prepare_league_family_child(const ui::StrengthsIdentity& id,uint8_t child,
    uint64_t revision,std::string& error) {
    error.clear();
    if(league_family_teaminfo_active_||(league_family_owner_&&league_family_owner_->friendly_opponent)||child<1||child>3||revision==UINT64_MAX||id.revision!=revision||
       (id.source_param2!=0&&!live_source_identity(id))||
       id.manager<0||id.manager>3||id.league<0||id.league>8||!g_.memory.has(::bmh::layout::ActiveManager)||
       g_.memory.u8(::bmh::layout::ActiveManager)!=id.manager||size_t(id.manager)>=g_.manager_teams.size()||
       g_.manager_teams[size_t(id.manager)]!=id.runtime_club||g_.date!=id.day_counter||
       id.runtime_club<0||size_t(id.runtime_club)>=g_.club_ids.size()||
       int(g_.club_ids[size_t(id.runtime_club)])!=id.club_identity||strengths_dispatch_active_||
       league_pairings_dispatch_active_||strengths_owner_||league_pairings_draft_) {
        error="league family: stale child entry";return false;
    }
    if((id.family_id==0&&league_family_owner_)||
       (id.family_id!=0&&(!league_family_owner_||league_family_owner_->identity.family_id!=id.family_id||
          league_family_owner_->identity.manager!=id.manager||league_family_owner_->identity.runtime_club!=id.runtime_club||
          league_family_owner_->identity.club_identity!=id.club_identity||league_family_owner_->identity.day_counter!=id.day_counter||
          league_family_owner_->identity.source_param2!=id.source_param2||
          league_family_owner_->identity.family_sequence==UINT64_MAX||
          league_family_owner_->identity.family_sequence+1!=id.family_sequence))) {
        error="league family: missing owner binding for child entry";return false;
    }
    const size_t record=size_t(id.manager)*kManagerRecordSize;
    if(record+0x212>g_.manager_records.size()||id.manager_default_league!=int(g_.manager_records[record+0x210])) {
        error="league family: stale manager default league";return false;
    }
    const uint32_t word=::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2;
    if(!g_.memory.has(word+1)) {error="league family: missing manager mode word";return false;}
    g_.memory.set16(word,ui::strengths_family_word_source(g_.memory.u16(word),exe_.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField+uint32_t(id.league))));
    club_modal_runtime_[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField2]=uint8_t(id.league);
    club_modal_runtime_[::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField]=id.league_group;
    club_modal_runtime_[::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField]=exe_.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField+uint32_t(id.league));
    // Uint8T2's selector is child-specific: Table/Pairings use 0, Strengths 1.
    // This is presentation context only; it must not be inferred from the prior page.
    club_modal_runtime_[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField]=uint8_t(child==3);club_modal_runtime_[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2]=0;
    return true;
}

// Installs (or clears) the turn driver hook that fires at the end of a match phase. The wrapper
// records the current request in phase_result_request_ for the duration of the call, only
// accepts the driver's own groups and state, and requires that any viewer is closed again before
// the engine continues.
void CareerSession::set_phase_results(decltype(TurnDriver::phase_results) hook) {
    if (!hook) { turn_driver_.phase_results = {}; return; }
    turn_driver_.phase_results = [this, hook=std::move(hook)](const PhaseResultRequest& request,
            const std::vector<MatchGroup>& groups, const GameState& state) {
        if (phase_result_request_ || &groups != &turn_driver_.groups || &state != &g_ ||
            request.group_index >= groups.size())
            throw FormatError("phase results: invalid synchronous Source boundary");
        phase_result_request_ = &request;
        // Clears the borrowed request pointer when the hook returns or throws.
        struct Reset { const PhaseResultRequest*& request; ~Reset() { request = nullptr; } } reset{phase_result_request_};
        hook(request, groups, state);
        if (live_overview_owner_ && live_overview_owner_->phase_result)
            throw FormatError("phase results: viewer must close before the engine ACK");
    };
}

// Live overview group (phase-result viewer, live league table / pairings opened from a running
// match): only valid inside a calendar advance (calendar_advance_active_) with the driver
// waiting at a phase boundary. Pattern: open -> dispatch -> acknowledge; the owner snapshots the
// state and random counters when it opens and the close must find them unchanged.
// Opens the viewer at a phase boundary: copies the current group results into a staged state
// (no simulation, no random draws) and opens the overview on it; on failure the previous state
// is kept. Returns an empty optional with 'error' when no request is pending or a viewer is open.
std::optional<CareerSession::LiveOverviewProjection> CareerSession::open_phase_results(
    uint64_t& revision, const Image& underlay, std::string& error) {
    error.clear();
    if (!phase_result_request_ || !calendar_advance_active_ || live_overview_owner_ ||
        phase_result_request_->group_index >= turn_driver_.groups.size()) {
        error = "phase results: no current Source request or viewer already active"; return {};
    }
    try {
        // Native result arrays and process descriptors share pointers in the
        // Original. The Port owns separate vectors; publish their actual scores
        // at this viewer boundary, without simulation or random draws. This also
        // makes Source search/filter/forecast/TeamInfo read the current results.
        GameState entered = g_;
        for (const auto& group : turn_driver_.groups) {
            const uint32_t id = le32(group.desc, 0xcc);
            if (id < 0x15) {
                const size_t at = size_t(id)*kLeagueResultsStride + size_t(entered.league_rounds.at(id))*10;
                for (size_t m = 0; m < group.results.size(); ++m)
                    entered.results.at(at+m) = group.results[m];
            } else store_cup_results(entered, group);
        }
        const int manager = entered.memory.u8(::bmh::layout::ActiveManager);
        if (manager < 0 || size_t(manager) >= entered.manager_teams.size())
            throw FormatError("phase results: unavailable active Source manager");
        LiveOverviewRequest request;
        request.key_slot = 1; request.minute = phase_result_request_->phase_end_minute;
        request.group_index = int(phase_result_request_->group_index);
        request.manager_slot = manager; request.runtime_team = entered.manager_teams[size_t(manager)];
        request.revision = revision; request.sequence = next_live_overview_scope_;
        const auto checkpoint = g_;
        g_ = std::move(entered);
        MatchEngine context{g_, turn_driver_.groups, game_, rand_, turn_driver_.referees};
        auto page = open_running_overview(request, context, revision, underlay, error, true);
        if (!page) g_ = checkpoint;
        return page;
    } catch (const std::exception& e) { error = e.what(); return {}; }
}

// Forwards a viewer action while the phase-result owner of the pending request is open; empty
// optional with 'error' if the viewer is missing or belongs to another group.
std::optional<CareerSession::LiveOverviewDispatch> CareerSession::dispatch_phase_results(
    const LiveOverviewAction& action, uint64_t& revision, std::string& error,
    const TransferOfferTeamInfoPresenter& team_info) {
    if (!phase_result_request_ || !live_overview_owner_ || !live_overview_owner_->phase_result ||
        live_overview_owner_->request.group_index != int(phase_result_request_->group_index)) {
        error = "phase results: stale or unavailable Source viewer"; return {};
    }
    MatchEngine context{g_, turn_driver_.groups, game_, rand_, turn_driver_.referees};
    return dispatch_live_overview(action, context, revision, error, team_info);
}

// Opens the overview from a key press during a running match (not a phase-result viewer).
std::optional<CareerSession::LiveOverviewProjection> CareerSession::open_live_overview(
    const LiveOverviewRequest& request, MatchEngine& engine, uint64_t& revision,
    const Image& underlay, std::string& error) {
    return open_running_overview(request, engine, revision, underlay, error, false);
}

// Shared opener. The long guard requires an active calendar advance, no other modal owner or
// dispatch, a request that matches the driver's groups, the live state objects and the active
// manager (revision and scope included). key_slot 0 opens the league table, 1 the league
// pairings. Returns an empty optional with 'error' when the request is stale, nested or
// unauthenticated.
std::optional<CareerSession::LiveOverviewProjection> CareerSession::open_running_overview(
    const LiveOverviewRequest& request, MatchEngine& engine, uint64_t& revision,
    const Image& underlay, std::string& error, bool phase_result) {
    error.clear();
    if (!calendar_advance_active_ || live_overview_owner_ || request.key_slot < 0 || request.key_slot > 1 ||
        request.minute < 0 || request.minute > 255 || request.group_index < 0 ||
        size_t(request.group_index) >= engine.groups.size() ||
        (phase_result ? (!phase_result_request_ || request.group_index != int(phase_result_request_->group_index) ||
                        request.minute != phase_result_request_->phase_end_minute || request.key_slot != 1)
                      : request.group_index != 0) || engine.groups.empty() ||
        engine.groups.size() != turn_driver_.groups.size() || turn_driver_.groups.empty() ||
        request.manager_slot < 0 || request.manager_slot >= int(g_.manager_teams.size()) ||
        request.revision != revision || revision == UINT64_MAX ||
        next_live_overview_scope_ == UINT64_MAX || &engine.state != &g_ || &engine.game != &game_ ||
        &engine.rand != &rand_ || !g_.memory.has(::bmh::layout::ActiveManager) ||
        g_.memory.u8(::bmh::layout::ActiveManager) != request.manager_slot || request.runtime_team < 0 ||
        size_t(request.runtime_team) >= g_.team_flags.size() ||
        g_.manager_teams[size_t(request.manager_slot)] != request.runtime_team ||
        g_.manager_of(request.runtime_team) != request.manager_slot ||
        engine.groups.front().desc != turn_driver_.groups.front().desc ||
        engine.groups.front().fixtures != turn_driver_.groups.front().fixtures ||
        strengths_owner_ || strengths_dispatch_active_ || league_family_owner_ || league_pairings_draft_ ||
        league_pairings_dispatch_active_ || other_games_owner_ || other_games_dispatch_active_ ||
        live_intervention_ || balance_draft_ || balance_dispatch_active_ || top_scorers_owner_ ||
        top_scorers_dispatch_active_ || pending_offer_dispatch_active_ || market_dispatch_active_ ||
        transfer_offer_editor_ || manager_join_ || contract_quote_ ||
        stadium_.investment || stadium_.tip || stadium_.periphery_quote || stadium_.block_expansion) {
        error = "live overview: stale, nested or unauthenticated Source key request";
        return {};
    }
    const MatchGroup& active = engine.groups[size_t(request.group_index)];
    if (active.desc.size() < 0xd0 || active.matches() <= 0 ||
        (!phase_result && active.void_mask() == ((uint32_t(1) << active.matches()) - 1))) {
        error = "live overview: missing active Source match descriptor";
        return {};
    }


    // Scope guard: turns calendar_advance_active_ off while the family screens run and restores the
    // previous value on exit, so nested family code does not treat itself as inside the advance.
    struct LiveBoundaryFamilyScope {
        bool& active;
        bool previous;
        explicit LiveBoundaryFamilyScope(bool& value) : active(value), previous(value) { active = false; }
        ~LiveBoundaryFamilyScope() { active = previous; }
    } family_scope(calendar_advance_active_);
    const uint64_t next_revision = revision + 1;
    const auto family_checkpoint = league_family_checkpoint();
    const auto pairings_checkpoint = league_pairings_checkpoint();
    const auto other_checkpoint = other_games_checkpoint();
    const auto scope_checkpoint = next_live_overview_scope_;
    try {
        LiveOverviewProjection result;
        source_runtime_set_byte(g_,club_modal_runtime_,::bmh::layout::CareerSession_LiveBoundaryFamilyScope_ExecutableField,0);
        result.owner = request;
        result.owner.revision = next_revision;
        result.owner.sequence = next_live_overview_scope_;
        const uint32_t competition = le32(active.desc, 0xcc);
        LiveOverviewOwner live;
        live.phase_result = phase_result;
        live.request = result.owner;
        live.source_competition = int(competition);
        live.original_native = save_stream(g_);
        live.original_rand_seed = rand_.seed(); live.original_game_seed = game_.seed();
        live.original_rand_calls = rand_.calls(); live.original_game_calls = game_.calls();
        for (const auto& group : engine.groups) live.active_groups.emplace_back(group.desc, group.fixtures);
        live.referees = engine.referees;
        live_overview_owner_ = live;

        if (active.desc[0] == 0) {
            if (competition > 8)
                throw FormatError("live overview: active Source descriptor is not a league");
            result.kind = request.key_slot == 0 ? LiveOverviewKind::league_table : LiveOverviewKind::league_pairings;
            auto strengths = strengths_open_request(next_revision);
            if (!strengths) throw FormatError("live overview: active manager has no Source league identity");
            strengths->identity.league = int(competition);
            strengths->identity.league_group = uint8_t(competition >= 5);
            strengths->identity.sequence = next_live_overview_scope_;
            strengths->identity.source_param2 = uint8_t(request.minute);
            if (request.key_slot == 0) {
                auto family = begin_league_family(strengths->identity, 1, next_revision, error);
                if (!family) throw FormatError(error.empty() ? "live overview: Source Table family entry rejected" : error);
                result.family_identity = *family;
                live.family_identity = *family;
                auto table = current_league_table_dispatch(error);
                if (!table) throw FormatError(error.empty() ? "live overview: Source Table projection unavailable" : error);
                result.table = table->page;
            } else {
                LeaguePairingsDraft draft{g_,game_,rand_,club_modal_runtime_,club_modal_runtime_,{}, {},
                    save_stream(g_),rand_.seed(),game_.seed(),rand_.calls(),game_.calls()};
                for (const auto& group : engine.groups) draft.original_active_groups.emplace_back(group.desc,group.fixtures);
                draft.original_referee_profiles = engine.referees;
                draft.owner = open_league_pairings_source(draft.state,request.manager_slot,ui::league_pairings_constants(exe_));
                if (draft.owner.league != int(competition)) {
                    draft.owner.league=int(competition);
                    draft.owner.round=std::min<int>(draft.state.league_rounds.at(size_t(competition)),
                        league_matchdays(int(competition))-1);
                    draft.owner.title_round=draft.state.league_rounds.at(size_t(competition));
                    if(draft.owner.round>=league_matchdays(int(competition)))
                        throw FormatError("live overview: Source Pairings round is outside its league schedule");
                }
                draft.active_packet = validate_normal_pairings_group(draft.state,draft.owner,draft.runtime);
                const uint64_t child_scope = ++league_pairings_scope_;
                auto page = ui::league_pairings_from_game(draft.state,draft.owner,exe_,next_revision,child_scope,next_live_overview_scope_,draft.active_packet);
                draft.identity = page.identity;
                draft.runtime[::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField] = exe_.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField + uint32_t(draft.owner.league));
                pairings_runtime_draw(draft.state,draft.runtime,page,exe_);
                league_pairings_draft_ = std::move(draft);
                auto family = begin_league_family(strengths->identity,2,next_revision,error);
                if (!family) throw FormatError(error.empty() ? "live overview: Source Pairings family entry rejected" : error);
                result.family_identity = *family;
                live.family_identity = *family;
                result.pairings = std::move(page);
            }
        } else {
            // Cup descriptor (not a league): the overview becomes an other-games detail page bound to the
            // active cup entry, found by trying linked-page ordinals until the cup entry and competition
            // id match. Anything else throws and is rolled back by the catch block below.
            if (request.key_slot != 0 && request.key_slot != 1)
                throw FormatError("live overview: invalid cup overview key slot");
            const int entry_index = cup_entry_index(competition);
            // CupGroupOverviewEffects owns this unsaved traversal cursor even in a freshly
            // loaded career. Its absence in an FBM is not an invalid cup entry.
            if (phase_result && !g_.memory.has(::bmh::layout::CareerSession_FormatError_RuntimeField8)) g_.memory.add_block(::bmh::layout::CareerSession_FormatError_RuntimeField8, 1);
            if (entry_index < 0 || entry_index >= kCupEntries || !g_.memory.has(cup_entry_address(entry_index)+0x1e) ||
                !g_.memory.has(::bmh::layout::CareerSession_FormatError_RuntimeField8) || !g_.memory.has(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(request.manager_slot)))
                throw FormatError("live overview: active cup descriptor is outside the Source linked table");
            // CupGroupOverviewEffects receives the cup-entry ID at entry+18. Match descriptor
            // +6 is part of its end-minute pointer, not a competition selector.
            const int selector = int16_t(g_.memory.u16(competition + 0x18));
            const uint8_t old_view = g_.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(request.manager_slot));
            if (!phase_result) {
                g_.memory.set8(::bmh::layout::CareerSession_FormatError_RuntimeField3,uint8_t(7-(uint16_t(g_.season)%4)));
                g_.memory.set8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(request.manager_slot),uint8_t(old_view|0x80));
            }
            // CupGroupOverviewEffects resolves this exact active descriptor through FindCup(selector, ordinal).
            const int competition_id = int16_t(g_.memory.u16(cup_entry_address(entry_index)+0x18));
            int stable_team = request.runtime_team;
            if (stable_team > 0x164) {
                const size_t backup = 4 * 0xb0c + 0x290;
                if (request.manager_slot != 0 || backup + 2 > g_.manager_records.size())
                    throw FormatError("live overview: unsupported national cup owner");
                stable_team = int(uint16_t(g_.manager_records[backup] | uint16_t(g_.manager_records[backup+1])<<8));
            }
            if (stable_team < 0 || stable_team >= 138 || size_t(stable_team) >= g_.club_ids.size())
                throw FormatError("live overview: missing stable cup owner identity");
            ui::OtherGamesIdentity identity;
            identity.manager_slot=request.manager_slot; identity.runtime_team=request.runtime_team;
            identity.club_identity=int(g_.club_ids[size_t(stable_team)]); identity.day_counter=uint32_t(g_.date);
            identity.revision=next_revision; identity.scope=next_live_overview_scope_; identity.sequence=next_live_overview_scope_;
            identity.expected_view_byte=g_.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(request.manager_slot));
            std::optional<ui::OtherGamesState> page;
            int linked_page=-1;
            for(int ordinal=0;ordinal<kCupEntries;++ordinal) {
                try {
                    auto candidate=ui::other_games_state_from_game(g_,identity,texts_,exe_,selector,ordinal,-1,
                        OtherGamesDisplayMode::detail,int(g_.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(request.manager_slot))&0x7f),
                        false,underlay,club_modal_runtime_);
                    if(candidate.projection.entry_index==entry_index&&
                       candidate.projection.descriptor_competition==competition_id) {
                        linked_page=ordinal;page=std::move(candidate);break;
                    }
                } catch(const std::exception&) {}
            }
            if (!page || linked_page<0)
                throw FormatError("live overview: Source cup pointer resolved a different active descriptor");
            g_.memory.set8(::bmh::layout::CareerSession_FormatError_RuntimeField8,uint8_t(linked_page));
            page->source_participant_offset=int(g_.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(request.manager_slot))&0x7f);
            OtherGamesOwner owner;
            owner.page=*page; owner.round=page->projection.round; owner.participant_offset=page->source_participant_offset;
            owner.display_mode=page->projection.display_mode; owner.native=save_stream(g_); owner.runtime=club_modal_runtime_;
            owner.rand_seed=rand_.seed(); owner.game_seed=game_.seed(); owner.rand_calls=rand_.calls(); owner.game_calls=game_.calls();
            owner.desk_owner.kind=ui::DeskRequestKind::open_menu; owner.desk_owner.menu_kind=5;
            owner.desk_owner.manager_slot=request.manager_slot; owner.desk_owner.revision=next_revision;
            other_games_owner_=std::move(owner);
            other_games_selection_={selector,linked_page};
            result.kind=LiveOverviewKind::cup_other_games; result.cup=std::move(*page);
        }
        live.kind=result.kind;
        live.original_native=save_stream(g_); live.original_rand_seed=rand_.seed(); live.original_game_seed=game_.seed();
        live.original_rand_calls=rand_.calls(); live.original_game_calls=game_.calls();
        // Commit point: the owner now holds its final kind, family identity and revision; the scope
        // counter and the revision are advanced only here, after every child owner was opened.
        live_overview_owner_->kind=live.kind;
        live_overview_owner_->family_identity=live.family_identity;
        live_overview_owner_->request.revision=next_revision;
        ++next_live_overview_scope_;
        revision=next_revision;
        return result;
    } catch(const std::exception& e) {
        // Failure path: restore the family, pairings and other-games checkpoints, drop the overview
        // owner and put the scope counter back, so a failed open leaves no trace.
        restore_league_family_checkpoint(family_checkpoint);
        restore_league_pairings_checkpoint(pairings_checkpoint);
        restore_other_games_checkpoint(other_checkpoint);
        live_overview_owner_.reset();
        next_live_overview_scope_=scope_checkpoint;
        error=e.what();
        return {};
    }
}

// League table dispatch group (controls, range dialog, forecast acknowledgement, league
// selector, team info). Pattern: validate the request envelope, identity, owner and that the
// live state still equals the owner snapshot; take a checkpoint; apply the change to the
// table owner; refresh the snapshot and identity (sequence + 1, revision + 1); on any failure
// restore the checkpoint and the old revision and return an empty optional with 'error'.
// Projects the table page (or the pending range dialog profile) of the owned table child;
// read-only. Empty optional with 'error' when no table owner exists or the state drifted.
std::optional<CareerSession::LeagueTableDispatch> CareerSession::current_league_table_dispatch(
    std::string& error) const {
    error.clear();
    if(!league_family_owner_||league_family_owner_->child!=1||!league_family_owner_->table||
       !source_table_identity_is_live(g_,league_family_owner_->identity)||
       strengths_owner_||league_pairings_draft_) {
        error="league table: no active Source family table owner";return {};
    }
    try {
        const auto& owner=*league_family_owner_;const auto& table=*owner.table;
        const uint32_t settings_at=::bmh::layout::ManagerSettings+uint32_t(owner.identity.manager)*2;
        const uint32_t family_at=::bmh::layout::LeagueTableDisplayModes+uint32_t(owner.identity.manager)*2;
        if(save_stream(g_)!=owner.native||rand_.seed()!=owner.rand_seed||rand_.calls()!=owner.rand_calls||
           game_.seed()!=owner.game_seed||game_.calls()!=owner.game_calls||club_modal_runtime_!=owner.runtime||
           !g_.memory.has(settings_at+1)||!g_.memory.has(family_at+1)||
           g_.memory.u16(settings_at)!=owner.identity.expected_settings_word||
           g_.memory.u16(family_at)!=owner.identity.expected_family_word) {
            error="league table: owned Source state changed before projection";return {};
        }
        const uint16_t word=table.display_word_override;
        LeagueTableDispatch result;
        result.identity=owner.identity;result.phase=table.phase;
        if(table.phase==TableDispatchPhase::range_start||table.phase==TableDispatchPhase::range_end) {
            const int league=owner.identity.league,maximum=bmh::league_matchdays(league);
            ui::TableRangeDialogProfile profile;
            profile.title_textd_address=table.phase==TableDispatchPhase::range_start?::bmh::layout::CareerSession_CurrentLeagueTableDispatch_ExecutableField: ::bmh::layout::CareerSession_CurrentLeagueTableDispatch_ExecutableField2;
            profile.minimum=table.phase==TableDispatchPhase::range_start?1:uint32_t(source_byte(table.pending_start_round)+1);
            profile.maximum=uint32_t(maximum);
            profile.default_value=table.phase==TableDispatchPhase::range_start?1:
                std::max<uint32_t>(1,std::min<uint32_t>(uint32_t(g_.league_rounds.at(size_t(league))),profile.maximum));
            const bool gradient=source_runtime_byte(g_,club_modal_runtime_,exe_,::bmh::layout::TextGradientDisabled)==0&&
                table.runtime_font_id==3;
            profile.text_mode=gradient?ui::TableRangeTextMode::source_gradient:ui::TableRangeTextMode::flat;
            profile.x=table.dialog_x;profile.y=table.dialog_y;result.dialog=profile;
        }
        result.page=table.forecast_page?*table.forecast_page:table.page;
        if(result.page.manager_slot!=owner.identity.manager||result.page.league_id!=owner.identity.league||
           result.page.rows.empty())
            throw FormatError("league table: owned active page is missing or mismatched");
        return result;
    } catch(const std::exception& e) {error=e.what();return {};}
}

// Applies one of the table's ten controls (index 0-9) to the owned table. Requires a ready
// table, a canonical request envelope and an unchanged snapshot; revision advances by one.
std::optional<CareerSession::LeagueTableDispatch> CareerSession::dispatch_league_table_control(
    const ui::LeagueFamilyRequest& request,uint64_t& revision,std::string& error) {
    error.clear();const auto& id=request.identity;
    if(league_family_teaminfo_active_||request.kind!=ui::LeagueFamilyRequestKind::control||
       request.from_child!=1||!canonical_table_envelope(request,request.control_index)||
       id.revision!=revision||request.control_index<0||request.control_index>9||
       !source_table_identity_is_live(g_,id)||
       !league_family_owner_||league_family_owner_->child!=1||league_family_owner_->identity!=id||
       !league_family_owner_->table||league_family_owner_->table->phase!=TableDispatchPhase::ready||
       strengths_owner_||league_pairings_draft_||
       (id.source_param2!=0&&!live_source_identity(id))||id.family_sequence==UINT64_MAX||revision==UINT64_MAX||
       save_stream(g_)!=league_family_owner_->native||rand_.seed()!=league_family_owner_->rand_seed||
       rand_.calls()!=league_family_owner_->rand_calls||game_.seed()!=league_family_owner_->game_seed||
       game_.calls()!=league_family_owner_->game_calls||club_modal_runtime_!=league_family_owner_->runtime||
       strengths_owner_||league_pairings_draft_) {
        error="league table: stale or unavailable control request";return {};
    }
    const auto checkpoint=league_family_checkpoint();
    const uint64_t old_revision=revision;
    try {
        auto& family=*league_family_owner_;auto& table=*family.table;
        const uint32_t word_at=::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2;
        if(!g_.memory.has(word_at+1))throw FormatError("league table: manager display word is unavailable");
        uint16_t word=g_.memory.u16(word_at);
        const int league=id.league,matchdays=bmh::league_matchdays(league);
        const auto update_page=[&](std::optional<bmh::LeagueTable> engine={},bool forecast=false) {
            table.display_word_override=word;
            const auto context=source_table_context(table,word,std::move(engine),forecast);
            table.page=ui::league_table_from_game(g_,id.manager,league,context);
        };
        // Controls 0-2 set the aggregation flags and mode bits, 3 computes a forecast table (shown
        // after the present acknowledgement), 4-6 toggle display bits (at least one stays set), 7
        // toggles the normal mode, 8 resets the round range to the whole season and 9 starts the
        // two-step range dialog.
        switch(request.control_index) {
        case 0: table.aggregation_flags=7;word=uint16_t((word&0x0ff0)|1);break;
        case 1: table.aggregation_flags=1;word=uint16_t((word&0x0ff0)|2);break;
        case 2: table.aggregation_flags=2;word=uint16_t((word&0x0ff0)|4);break;
        case 3: {
            if((word&8)==0) {
                const int sort_extra=0x0e - int((word>>3)&0x0e);
                std::array<uint8_t,10> gap{};
                for(size_t i=0;i<gap.size();++i)gap[i]=exe_.u8(::bmh::layout::CareerSession_DispatchLeagueTableControl_RuntimeField+uint32_t(i));
                const auto forecast=bmh::compute_league_table_forecast(g_,league,0,matchdays,
                    table.aggregation_flags,table.normal_mode,sort_extra,gap,game_);
                table.forecast_game_after=forecast.game_after;
                word=uint16_t((word|8)&0x0f7f);
                table.display_word_override=uint16_t((word&0x0cf7)+0x0108);
                table.forecast_present_pending=true;
                table.phase=TableDispatchPhase::forecast_pending_present;
                const auto context=source_table_context(table,table.display_word_override,forecast.table,true);
                table.forecast_page=ui::league_table_from_game(g_,id.manager,league,context);
            }
            break;
        }
        case 4: case 5: case 6: {
            const uint16_t bit=uint16_t(0x10u<<(request.control_index-4));word^=bit;
            if((word&0x70)==0)word=uint16_t(word|0x70);
            break;
        }
        case 7:
            table.normal_mode^=1;
            if(club_modal_runtime_.count(::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField))club_modal_runtime_[::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField]=uint8_t(table.normal_mode);
            else if(g_.memory.has(::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField))g_.memory.set8(::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField,uint8_t(table.normal_mode));
            break;
        case 8:
            table.first_round=0;table.end_round=matchdays;
            word=uint16_t((word&0x0cff)|0x0100);break;
        case 9: {
            table.previous_start_round=table.first_round;table.previous_end_round=table.end_round;
            table.pending_start_round=table.first_round;
            table.runtime_font_id=exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + 1);
            if(g_.memory.has(::bmh::layout::CareerSession_DispatchLeagueTableControl_RuntimeField2))g_.memory.set8(::bmh::layout::CareerSession_DispatchLeagueTableControl_RuntimeField2,table.runtime_font_id);
            else club_modal_runtime_[::bmh::layout::CareerSession_DispatchLeagueTableControl_RuntimeField2]=table.runtime_font_id;
            table.phase=TableDispatchPhase::range_start;
            break;
        }
        default: throw FormatError("league table: unsupported control");
        }
        if(request.control_index!=3&&request.control_index!=9) {
            table.forecast_page.reset();table.forecast_game_after.reset();table.forecast_present_pending=false;
            word=uint16_t(word&0x0f7f);
            g_.memory.set16(word_at,word);
            update_page();
        }
        if(request.control_index==3&&table.phase!=TableDispatchPhase::forecast_pending_present) {
            word=uint16_t(word&0x0f7f);
            g_.memory.set16(word_at,word);update_page();
        }
        auto next=id;next.revision=revision+1;next.family_sequence=id.family_sequence+1;
        next.expected_settings_word=g_.memory.u16(::bmh::layout::ManagerSettings+uint32_t(id.manager)*2);
        next.expected_family_word=g_.memory.u16(word_at);
        family.identity=next;family.native=save_stream(g_);family.rand_seed=rand_.seed();
        family.game_seed=game_.seed();family.rand_calls=rand_.calls();family.game_calls=game_.calls();
        family.runtime=club_modal_runtime_;revision++;
        auto result=current_league_table_dispatch(error);
        if(!result)throw FormatError(error.empty()?"league table: dispatch projection failed":error);
        return result;
    } catch(const std::exception& e) {
        restore_league_family_checkpoint(checkpoint);revision=old_revision;error=e.what();return {};
    }
}

// Handles the answer of the range dialog (control 9): start value first, then end value.
// Accept stores the value, cancel restores the previous range; positions and bounds of the
// reply are validated. The table returns to ready after the second step or a cancel.
std::optional<CareerSession::LeagueTableDispatch> CareerSession::dispatch_league_table_range_reply(
    const ui::LeagueFamilyRequest& request,uint64_t& revision,std::string& error) {
    error.clear();const auto& id=request.identity;
    if(league_family_teaminfo_active_||request.kind!=ui::LeagueFamilyRequestKind::table_range_result||
       request.from_child!=1||request.target_child!=0||request.control_index!=9||
       request.runtime_team!=-1||request.expected_club_identity!=-1||request.table_row_index!=-1||
       request.league_id!=-1||id.revision!=revision||id.family_sequence==UINT64_MAX||revision==UINT64_MAX||
       !source_table_identity_is_live(g_,id)||
       !request.table_dialog_reply||!league_family_owner_||league_family_owner_->child!=1||
       league_family_owner_->identity!=id||!league_family_owner_->table||
       (league_family_owner_->table->phase!=TableDispatchPhase::range_start&&
        league_family_owner_->table->phase!=TableDispatchPhase::range_end)||
       save_stream(g_)!=league_family_owner_->native||rand_.seed()!=league_family_owner_->rand_seed||
       rand_.calls()!=league_family_owner_->rand_calls||game_.seed()!=league_family_owner_->game_seed||
       game_.calls()!=league_family_owner_->game_calls||club_modal_runtime_!=league_family_owner_->runtime) {
        error="league table: stale or unavailable range reply";return {};
    }
    const auto checkpoint=league_family_checkpoint();
    const uint64_t old_revision=revision;
    try {
        auto& family=*league_family_owner_;auto& table=*family.table;
        const auto& reply=*request.table_dialog_reply;
        const auto phase=table.phase;
        const auto expected=phase==TableDispatchPhase::range_start?ui::TableDialogStage::start:ui::TableDialogStage::end;
        if((reply.outcome!=ui::TableDialogOutcome::accept&&reply.outcome!=ui::TableDialogOutcome::cancel)||
           reply.stage!=expected||reply.x<0||reply.x>207||reply.y<0||reply.y>104)
            throw FormatError("league table: wrong dialog stage or Source position");
        const int maximum=bmh::league_matchdays(id.league);
        if(reply.outcome==ui::TableDialogOutcome::accept) {
            const int minimum=phase==TableDispatchPhase::range_start?1:int(source_byte(table.pending_start_round))+1;
            if(reply.value<minimum||reply.value>maximum)
                throw FormatError("league table: accepted range value is outside Source bounds");
        }
        table.dialog_x=reply.x;table.dialog_y=reply.y;
        uint16_t word=g_.memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2);
        if(phase==TableDispatchPhase::range_start) {
            if(reply.outcome==ui::TableDialogOutcome::accept) {
                table.pending_start_round=reply.value-1;table.phase=TableDispatchPhase::range_end;
            } else table.phase=TableDispatchPhase::ready;
        } else {
            if(reply.outcome==ui::TableDialogOutcome::accept) {
                table.first_round=table.pending_start_round;table.end_round=reply.value;
            } else {
                table.first_round=table.previous_start_round;table.end_round=table.previous_end_round;
            }
            word=uint16_t((word&0x0cff)|0x0200);
            source_table_apply_entry_flags(table,word);
            source_table_normalize_range(table,g_,id.league,texts_);
            table.phase=TableDispatchPhase::ready;
        }
        if(table.phase==TableDispatchPhase::ready) {
            word=uint16_t(word&0x0f7f);
            g_.memory.set16(::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2,word);
            table.display_word_override=word;table.forecast_page.reset();table.forecast_game_after.reset();
            table.page=ui::league_table_from_game(g_,id.manager,id.league,source_table_context(table,word));
        }
        auto next=id;next.revision=revision+1;next.family_sequence=id.family_sequence+1;
        next.expected_settings_word=g_.memory.u16(::bmh::layout::ManagerSettings+uint32_t(id.manager)*2);
        next.expected_family_word=g_.memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2);
        family.identity=next;family.native=save_stream(g_);family.rand_seed=rand_.seed();
        family.game_seed=game_.seed();family.rand_calls=rand_.calls();family.game_calls=game_.calls();
        family.runtime=club_modal_runtime_;revision++;
        auto result=current_league_table_dispatch(error);
        if(!result)throw FormatError(error.empty()?"league table: range projection failed":error);
        return result;
    } catch(const std::exception& e) {restore_league_family_checkpoint(checkpoint);revision=old_revision;error=e.what();return {};}
}

// The forecast page has been shown: adopts the random generator state computed with the
// forecast, returns the table to ready and rebuilds the normal page.
std::optional<CareerSession::LeagueTableDispatch> CareerSession::acknowledge_league_forecast_present(
    const ui::LeagueFamilyRequest& request,uint64_t& revision,std::string& error) {
    error.clear();const auto& id=request.identity;
    if(league_family_teaminfo_active_||request.kind!=ui::LeagueFamilyRequestKind::table_forecast_presented||
       request.from_child!=1||request.target_child!=0||request.control_index!=3||
       request.runtime_team!=-1||request.expected_club_identity!=-1||request.table_row_index!=-1||
       request.league_id!=-1||request.table_dialog_reply||
       id.revision!=revision||id.family_sequence==UINT64_MAX||revision==UINT64_MAX||
       !source_table_identity_is_live(g_,id)||
       !league_family_owner_||league_family_owner_->child!=1||league_family_owner_->identity!=id||
       !league_family_owner_->table||league_family_owner_->table->phase!=TableDispatchPhase::forecast_pending_present||
       !league_family_owner_->table->forecast_present_pending||!league_family_owner_->table->forecast_game_after||
       save_stream(g_)!=league_family_owner_->native||rand_.seed()!=league_family_owner_->rand_seed||
       rand_.calls()!=league_family_owner_->rand_calls||game_.seed()!=league_family_owner_->game_seed||
       game_.calls()!=league_family_owner_->game_calls||club_modal_runtime_!=league_family_owner_->runtime) {
        error="league table: stale or unavailable forecast-present acknowledgement";return {};
    }
    const auto checkpoint=league_family_checkpoint();
    const uint64_t old_revision=revision;
    try {
        auto& family=*league_family_owner_;auto& table=*family.table;
        game_=*table.forecast_game_after;
        const uint32_t word_at=::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2;
        uint16_t word=uint16_t(table.display_word_override&0x0ff7);
        g_.memory.set16(word_at,word);
        table.display_word_override=word;table.forecast_present_pending=false;
        table.forecast_game_after.reset();table.phase=TableDispatchPhase::ready;
        const int sort_extra=0x0e - int((word>>3)&0x0e);
        const auto normal=bmh::compute_league_table(g_,id.league,0,table.end_round,
            table.aggregation_flags,table.normal_mode+sort_extra);
        table.page=ui::league_table_from_game(g_,id.manager,id.league,
            source_table_context(table,word,normal,false));
        auto next=id;next.revision=revision+1;next.family_sequence=id.family_sequence+1;
        next.expected_settings_word=g_.memory.u16(::bmh::layout::ManagerSettings+uint32_t(id.manager)*2);
        next.expected_family_word=word;
        family.identity=next;family.native=save_stream(g_);family.rand_seed=rand_.seed();
        family.game_seed=game_.seed();family.rand_calls=rand_.calls();family.game_calls=game_.calls();
        family.runtime=club_modal_runtime_;revision++;
        auto result=current_league_table_dispatch(error);
        if(!result)throw FormatError(error.empty()?"league table: forecast ACK projection failed":error);
        return result;
    } catch(const std::exception& e) {restore_league_family_checkpoint(checkpoint);revision=old_revision;error=e.what();return {};}
}

// Switches the table to another league (map click) or toggles the league group, after
// checking the target is listed in the bound league map. Works on staged copies of the state,
// runtime bytes and owner and commits them only on success; returns false with 'error' otherwise.
bool CareerSession::select_league_family_page(const ui::LeagueFamilyRequest& request,
    uint64_t revision,std::string& error) {
    error.clear();const auto& id=request.identity;
    const bool map_select=request.kind==ui::LeagueFamilyRequestKind::select_league;
    const bool toggle=request.kind==ui::LeagueFamilyRequestKind::toggle_league_group;
    if(league_family_teaminfo_active_||(!map_select&&!toggle)||request.from_child!=1||
       !canonical_table_envelope(request,-1,true)||id.revision!=revision||request.league_id<0||request.league_id>8||
       !source_table_identity_is_live(g_,id)||strengths_owner_||league_pairings_draft_||
       !league_family_owner_||league_family_owner_->child!=1||league_family_owner_->identity!=id||
       !league_family_owner_->table||league_family_owner_->table->phase!=TableDispatchPhase::ready||
       save_stream(g_)!=league_family_owner_->native||rand_.seed()!=league_family_owner_->rand_seed||
       rand_.calls()!=league_family_owner_->rand_calls||game_.seed()!=league_family_owner_->game_seed||
       game_.calls()!=league_family_owner_->game_calls||club_modal_runtime_!=league_family_owner_->runtime||
       id.family_sequence==UINT64_MAX) {
        error="league family: stale Table league selector";return false;
    }
    const uint8_t old_group=id.league_group;
    const uint8_t next_group=toggle?uint8_t(old_group^1):old_group;
    const size_t record=size_t(id.manager)*kManagerRecordSize;
    const auto checkpoint=league_family_checkpoint();
    try {
        if(toggle) {
            if(!g_.memory.has(::bmh::layout::CalendarState_FormatError_RuntimeField2)||g_.memory.u8(::bmh::layout::CalendarState_FormatError_RuntimeField2)!=0||
               request.league_id!=(next_group?int(exe_.u8(::bmh::layout::CareerSession_FormatError_RuntimeField4)):int(g_.manager_records[record+0x210])))
                throw FormatError("league family: group toggle target or Source availability is invalid");
        } else {
            bool listed=false;
            for(int row=0;row<4;++row) {
                const int count=exe_.u8(::bmh::layout::CalendarLeagueCounts+uint32_t(old_group*4+row));
                const int first=old_group?int(exe_.u8(::bmh::layout::CareerSession_FormatError_RuntimeField4))+row:
                    int(exe_.u8(::bmh::layout::CalendarFirstLeague+uint32_t(row)));
                if(first+count>int(bmh::kLeagueCount))
                    throw FormatError("league family: Source map row exceeds the league table");
                for(int cell=0;cell<count;++cell)
                    if(first+cell==request.league_id)listed=true;
            }
            if(!listed)throw FormatError("league family: selected league is not in the bound Source map");
        }
        GameState next=g_;auto next_runtime=club_modal_runtime_;auto next_owner=*league_family_owner_;
        const uint32_t word_at=::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2;
        if(!next.memory.has(word_at+1))throw FormatError("league family: missing manager mode word");
        const bool league_changed=request.league_id!=id.league;
        next_runtime[::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField]=next_group;
        if(league_changed) {
            next_runtime[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField2]=uint8_t(request.league_id);
            next_runtime[::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField]=exe_.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField+uint32_t(request.league_id));
        }
        uint16_t word=uint16_t(next.memory.u16(word_at)&0x0f7f);
        if(next_runtime[::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField]!=0)word=uint16_t(word|0x0080);
        if(league_changed)word=uint16_t(word&0xff7f); // FriendlyMatchScreen outer redraw only.
        next.memory.set16(word_at,word);
        auto& identity=next_owner.identity;identity.league=request.league_id;
        identity.league_group=next_group;
        identity.expected_family_word=word;identity.family_sequence=id.family_sequence+1;
        auto& table=*next_owner.table;
        if(league_changed) {
            table.normal_mode=next_runtime[::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField]&1;
            table.display_word_override=word;table.forecast_page.reset();table.forecast_game_after.reset();
            table.forecast_present_pending=false;
            table.page=ui::league_table_from_game(next,id.manager,request.league_id,
                source_table_context(table,word));
        }
        // Same-league map/toggle stay inside FriendlyMatchScreen's event loop. Keep
        // the current body, forecast, range locals and display override intact.
        next_owner.native=save_stream(next);next_owner.runtime=next_runtime;
        next_owner.rand_seed=rand_.seed();next_owner.rand_calls=rand_.calls();
        next_owner.game_seed=game_.seed();next_owner.game_calls=game_.calls();
        g_=std::move(next);club_modal_runtime_=std::move(next_runtime);
        league_family_owner_=std::move(next_owner);
        return true;
    } catch(const std::exception& e) {
        restore_league_family_checkpoint(checkpoint);error=e.what();return false;
    }
}

// Opens the club information of a table row through the injected presenter, on a staged copy.
// Checks the visible row identity and that the presenter left the live family untouched;
// then either keeps the poach selection (card returned) or redraws the table and publishes the
// copy. Returns the refreshed identity, or an empty optional with 'error' (state restored).
std::optional<ui::StrengthsIdentity> CareerSession::dispatch_family_table_team_info(
    const ui::LeagueFamilyRequest& request,uint64_t& revision,std::string& error,
    const TransferOfferTeamInfoPresenter& presenter,
    const std::optional<ClubPlayerDetailSelection>* poach_selection) {
    error.clear();const auto& id=request.identity;
    if((poach_selection&&!poach_table_selection_)||(league_family_owner_&&league_family_owner_->friendly_opponent)||league_family_teaminfo_active_||!presenter||request.kind!=ui::LeagueFamilyRequestKind::team_info||request.from_child!=1||
       request.target_child!=0||request.control_index!=-1||request.league_id!=-1||request.table_dialog_reply||
       id.revision!=revision||id.family_sequence==UINT64_MAX||!source_table_identity_is_live(g_,id)||
       !league_family_owner_||league_family_owner_->child!=1||league_family_owner_->identity!=id||
       !league_family_owner_->table||league_family_owner_->table->phase!=TableDispatchPhase::ready||
       strengths_owner_||league_pairings_draft_||
       save_stream(g_)!=league_family_owner_->native||rand_.seed()!=league_family_owner_->rand_seed||
       game_.seed()!=league_family_owner_->game_seed||rand_.calls()!=league_family_owner_->rand_calls||
       game_.calls()!=league_family_owner_->game_calls||club_modal_runtime_!=league_family_owner_->runtime||
       revision==UINT64_MAX) {
        error="league family: stale Table TeamInfo request";return {};
    }
    const auto checkpoint=league_family_checkpoint();
    const uint64_t old_revision=revision;
    try {
        const auto& table_owner=*league_family_owner_->table;
        const ui::LeagueTableState& visible=table_owner.forecast_page?*table_owner.forecast_page:table_owner.page;
        if(request.table_row_index<0||size_t(request.table_row_index)>=visible.rows.size())
            throw FormatError("league family: Table TeamInfo row index is outside the visible page");
        const auto& visible_row=visible.rows[size_t(request.table_row_index)];
        if(visible_row.runtime_team_id!=request.runtime_team||
           visible_row.club_identity_key!=request.expected_club_identity||
           request.runtime_team<0||size_t(request.runtime_team)>=g_.club_ids.size()||
           int(g_.club_ids[size_t(request.runtime_team)])!=request.expected_club_identity)
            throw FormatError("league family: Table TeamInfo visible row identity is stale");
        if(size_t(request.table_row_index)>=table_owner.page.rows.size()||
           table_owner.page.rows[size_t(request.table_row_index)].rank!=request.table_row_index+1)
            throw FormatError("league family: normal working rank is unavailable for Table TeamInfo");
        const auto& selected_row=table_owner.page.rows[size_t(request.table_row_index)];
        const int selected_team=selected_row.runtime_team_id;
        if(selected_team<0||size_t(selected_team)>=g_.club_ids.size()||
           selected_row.club_identity_key!=int(g_.club_ids[size_t(selected_team)]))
            throw FormatError("league family: normal working rank has a stale club identity");
        const auto& before=checkpoint;
        GameState next=g_;GameRandom next_game=game_;WatcomRandom next_rand=rand_;
        auto next_runtime=club_modal_runtime_;
        auto tables=ui::league_tables_from_game(g_,id.manager);
        if(size_t(id.league)>=tables.size()||visible.manager_slot!=id.manager||visible.league_id!=id.league)
            throw FormatError("league family: visible Table backdrop does not match the active owner");
        tables[size_t(id.league)]=visible;
        const Image backdrop=ui::LeagueTableScreen(data_directory_,tables,id.league,
            ui::LeagueTableScreen::FamilyMode{id,uint8_t(poach_selection!=nullptr)}).render();
        if(poach_selection) {
            const uint32_t word_at=::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2;
            uint16_t word=uint16_t(next.memory.u16(word_at)&0x0f7f);
            if(source_runtime_byte(next,next_runtime,exe_,::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField)!=0)word|=0x80;
            next.memory.set16(word_at,word);
        }
        auto services=staged_club_modal_services(next,next_game,next_rand,next_runtime);
        const auto enter=services.enter_club;int entry_calls=0;
        services.enter_club=[enter,&entry_calls,selected=selected_team](int team) {
            if(team!=selected)throw FormatError("league family: TeamInfo entered a different club");
            ++entry_calls;return enter(team);
        };
        league_family_teaminfo_active_=true;
        // Marks the family as inside TeamInfo while the presenter runs; cleared on every exit.
        struct Reset { bool& active;~Reset(){active=false;} } reset{league_family_teaminfo_active_};
        const bool accepted=presenter(services,selected_team,backdrop);
        const auto after=league_family_checkpoint();
        const bool live_unchanged=same_strengths_runtime_state(before.state,after.state)&&
            save_stream(before.state)==save_stream(after.state)&&before.runtime==after.runtime&&
            before.game.seed()==after.game.seed()&&before.game.calls()==after.game.calls()&&
            before.rand.seed()==after.rand.seed()&&before.rand.calls()==after.rand.calls()&&
            before.family==after.family&&before.next_family_id==after.next_family_id&&
            before.next_strengths_scope==after.next_strengths_scope&&
            !before.strengths&&!after.strengths&&!before.pairings&&!after.pairings;
        // The club information enters the club again on every redraw (view toggle, statistics, player
        // card), so one entry is the minimum; requiring exactly one threw away the whole visit, also the
        // Nein/Radio/Video setting made in it.
        if(!live_unchanged||!accepted||entry_calls<1)
            throw FormatError("league family: TeamInfo presenter changed owner or did not enter the selected club");
        const size_t record=size_t(id.manager)*kManagerRecordSize;
        if(next.date!=g_.date||next.manager_teams!=g_.manager_teams||
           !next.memory.has(::bmh::layout::ActiveManager)||next.memory.u8(::bmh::layout::ActiveManager)!=id.manager||record+0x292>next.manager_records.size()||
           (int(next.manager_records[record+0x290])|int(next.manager_records[record+0x291])<<8)!=id.runtime_club||
           size_t(id.runtime_club)>=next.club_ids.size()||int(next.club_ids[size_t(id.runtime_club)])!=id.club_identity||
           record+0x212>next.manager_records.size()||
           int(next.manager_records[record+0x210])!=id.manager_default_league)
            throw FormatError("league family: TeamInfo changed active manager or date");
        auto refreshed=id;refreshed.revision=revision+1;refreshed.family_sequence=id.family_sequence+1;
        refreshed.expected_settings_word=next.memory.u16(::bmh::layout::ManagerSettings+uint32_t(id.manager)*2);
        const uint32_t family_word_at=::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2;
        if(poach_selection && *poach_selection) {
            const auto chosen=club_player_detail_context(next,**poach_selection);
            if(chosen.selection.runtime_team!=selected_team || chosen.source_slot<0 ||
               chosen.source_slot>=0xb70 || chosen.human_loan)
                throw FormatError("poach: card returned a different ordinary squad player");
            refreshed.expected_family_word=next.memory.u16(family_word_at);
            // A successful param1=1 selection leaves50775 here. Its common
            // low80 write survives; there is no next outer Table redraw.
            g_=std::move(next);game_=next_game;rand_=next_rand;club_modal_runtime_=std::move(next_runtime);
            league_family_owner_->identity=refreshed;league_family_owner_->native=save_stream(g_);
            league_family_owner_->rand_seed=rand_.seed();league_family_owner_->game_seed=game_.seed();
            league_family_owner_->rand_calls=rand_.calls();league_family_owner_->game_calls=game_.calls();
            league_family_owner_->runtime=club_modal_runtime_;++revision;return refreshed;
        }
        const uint16_t drawn_word=uint16_t(next.memory.u16(family_word_at)&0xff7f);
        next.memory.set16(family_word_at,drawn_word);
        refreshed.expected_family_word=drawn_word;
        auto next_table_owner=*league_family_owner_->table;
        source_table_apply_entry_flags(next_table_owner,next.memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(id.manager)*2));
        source_table_normalize_range(next_table_owner,next,refreshed.league,texts_);
        next_table_owner.forecast_page.reset();next_table_owner.forecast_game_after.reset();
        next_table_owner.forecast_present_pending=false;next_table_owner.phase=TableDispatchPhase::ready;
        next_table_owner.display_word_override=refreshed.expected_family_word;
        auto next_tables=ui::league_tables_from_game(next,id.manager);
        next_tables[size_t(refreshed.league)]=ui::league_table_from_game(next,id.manager,refreshed.league,
            source_table_context(next_table_owner,next_table_owner.display_word_override));
        ui::LeagueTableScreen validate(data_directory_,std::move(next_tables),refreshed.league,
            ui::LeagueTableScreen::FamilyMode{refreshed});
        next_table_owner.page=validate.state();
        auto preparation=preparation_projection(next,revision+1);
        ui::validate_preparation_context(preparation);ui::DesktopScreen desk(data_directory_,preparation.desktop);
        g_=std::move(next);game_=next_game;rand_=next_rand;club_modal_runtime_=std::move(next_runtime);
        league_family_owner_->identity=refreshed;league_family_owner_->native=save_stream(g_);
        league_family_owner_->table=std::move(next_table_owner);
        league_family_owner_->rand_seed=rand_.seed();league_family_owner_->game_seed=game_.seed();
        league_family_owner_->rand_calls=rand_.calls();league_family_owner_->game_calls=game_.calls();
        league_family_owner_->runtime=club_modal_runtime_;++revision;return refreshed;
    } catch(const std::exception& e) {restore_league_family_checkpoint(checkpoint);revision=old_revision;error=e.what();return {};}
}

// After a child screen acknowledged its page, re-binds the family owner to the child's new
// identity (same family id and sequence, revision at most one ahead) and refreshes its snapshot.
// False with 'error' when the identity or child owner is stale.
bool CareerSession::refresh_family_after_child(const ui::StrengthsIdentity& prior,uint8_t child,
    ui::StrengthsIdentity next,uint64_t revision,std::string& error) {
    error.clear();
    if(league_family_teaminfo_active_||child<1||child>3||!league_family_owner_||league_family_owner_->child!=child||
       league_family_owner_->identity!=prior||prior.family_id==0||next.family_id!=prior.family_id||
       next.family_sequence!=prior.family_sequence||next.revision!=revision||revision<prior.revision||
       revision-prior.revision>1||
       next.manager!=prior.manager||next.runtime_club!=prior.runtime_club||next.club_identity!=prior.club_identity||
       next.day_counter!=prior.day_counter||next.source_param2!=prior.source_param2||
       next.manager_default_league!=prior.manager_default_league||next.league<0||next.league>8||
       !g_.memory.has(::bmh::layout::ActiveManager)||g_.memory.u8(::bmh::layout::ActiveManager)!=next.manager||g_.date!=next.day_counter||
       size_t(next.manager)>=g_.manager_teams.size()||g_.manager_teams[size_t(next.manager)]!=next.runtime_club||
       next.runtime_club<0||size_t(next.runtime_club)>=g_.club_ids.size()||
       int(g_.club_ids[size_t(next.runtime_club)])!=next.club_identity) {
        error="league family: child acknowledgement identity is stale";return false;
    }
    next.expected_settings_word=g_.memory.u16(::bmh::layout::ManagerSettings+uint32_t(next.manager)*2);
    next.expected_family_word=g_.memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(next.manager)*2);
    if(child==3&&(!strengths_owner_||strengths_owner_->page.identity!=next)) {
        error="league family: Strengths child acknowledgement lost its owner";return false;
    }
    if(child==2&&(!league_pairings_draft_||league_pairings_draft_->identity.manager!=next.manager||
       league_pairings_draft_->identity.runtime_club!=next.runtime_club||league_pairings_draft_->identity.league!=next.league)) {
        error="league family: Pairings child acknowledgement lost its owner";return false;
    }
    league_family_owner_->identity=next;league_family_owner_->native=save_stream(g_);
    league_family_owner_->rand_seed=rand_.seed();league_family_owner_->game_seed=game_.seed();
    league_family_owner_->rand_calls=rand_.calls();league_family_owner_->game_calls=game_.calls();
    league_family_owner_->runtime=club_modal_runtime_;return true;
}

}  // namespace bmh::app
