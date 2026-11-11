// SPDX-License-Identifier: GPL-3.0-only
// CareerSession: the club information dialogs (club, player list, player detail) and their services.
// (moved out of career_session.cpp unchanged; the class is declared in app/career_session.hpp)
#include "app/career_session_internal.hpp"

namespace bmh::app {
// Club-information modal family (enter, change category, player redraw, national action,
// pending offers): each member refuses to run while another modal owner is open and clears
// 'error' first. The ones that change state work on copies and assign them back only after
// success.
// Enter: opens the club info entry for 'team'; nullopt plus 'error' on an invalid team.
std::optional<ClubInfoEntry> CareerSession::enter_club_information(int team, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return std::nullopt; }
    error.clear();
    try { return prepare_club_info_entry(g_, team); }
    catch (const std::invalid_argument& e) { error = e.what(); return std::nullopt; }
}

// Switches the club info category for 'team'; nullopt plus 'error' when it is invalid.
std::optional<uint8_t> CareerSession::change_club_information_category(int team, int requested,
                                                                    std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return std::nullopt; }
    error.clear();
    try { return set_club_info_category(g_, team, requested); }
    catch (const std::invalid_argument& e) { error = e.what(); return std::nullopt; }
}

// Redraws a player detail card; random generators advance only if the redraw succeeded.
std::optional<ClubPlayerDetailRedraw> CareerSession::redraw_club_player_detail(
    const ClubPlayerDetailSelection& selection, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return std::nullopt; }
    error.clear();
    auto next_game = game_;
    auto next_rand = rand_;
    try {
        auto redraw = club_player_detail_redraw(g_, selection, exe_, next_game, next_rand);
        game_ = next_game;
        rand_ = next_rand;
        return redraw;
    } catch (const std::invalid_argument& e) {
        error = e.what();
        return std::nullopt;
    }
}

// Applies the national-team add/remove action of a player card on a staged copy; the copy is
// committed only when the engine reports no reason. Returns whether the action was applied.
std::optional<bool> CareerSession::dispatch_club_player_national_action(
    const ClubPlayerDetailSelection& selection, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return std::nullopt; }
    error.clear();
    auto next_state = g_;
    auto next_rand = rand_;
    MinuteReport report;
    try {
        const bool applied = club_player_detail_national_action(next_state, selection, next_rand, report);
        if (!report.reason.empty()) { error = report.reason; return std::nullopt; }
        g_ = std::move(next_state);
        rand_ = next_rand;
        return applied; // Source still refreshes the lineup when an add finds a full squad.
    } catch (const std::invalid_argument& e) {
        error = e.what();
        return std::nullopt;
    }
}

// Processes the pending transfer offers of a player card. Rejects a stale revision or a nested
// pending-offer/market dispatch, works on copies and commits them when at least one offer was
// processed. The revision is bumped here unless a calendar advance is active: then the outer
// driver publishes the revision when it returns.
std::optional<PendingOfferResult> CareerSession::dispatch_club_player_pending_offers(
    const ClubPlayerDetailSelection& selection, uint64_t expected_revision,
    const PendingOfferHooks& hooks, uint64_t& revision, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return std::nullopt; }
    error.clear();
    if (expected_revision != revision || pending_offer_dispatch_active_ || market_dispatch_active_ ||
        revision == std::numeric_limits<uint64_t>::max()) {
        error = "pending offer: stale or nested player-card action";
        return std::nullopt;
    }
    auto next_state = g_;
    auto next_game = game_;
    auto next_rand = rand_;
    pending_offer_dispatch_active_ = true;
    struct Reset { bool& active; ~Reset() { active = false; } } reset{pending_offer_dispatch_active_};
    MinuteReport report;
    try {
        const auto result = process_player_card_pending_offers(
            next_state, selection, next_game, next_rand, pending_hooks_for_dispatch(hooks), report);
        if (report.unsupported || !result.completed) {
            error = report.reason.empty() ? "pending offer: manual decision incomplete" : report.reason;
            return std::nullopt;
        }
        if (result.processed != 0) {
            g_ = std::move(next_state);
            game_ = next_game;
            rand_ = next_rand;
            // A nested cup modal belongs to the current calendar transaction;
            // its outer driver publishes that revision when advancement returns.
            if (!calendar_advance_active_) ++revision;
        }
        return result;
    } catch (const std::exception& e) {
        error = e.what();
        return std::nullopt;
    }
}

// Seeds the process-only byte map used by the club modals with their start values from the
// executable; these values are never written to a save.
void CareerSession::initialize_club_modal_runtime() {
    // Setup clears the boot-only dialog mode before either creating or loading
    // a career. This process flag is deliberately absent from native saves.
    club_modal_runtime_.emplace(layout::CompactDialogFrame, 0);
    // LE object4 has bb cdd/de/e1 in its zero-initialized virtual tail;
    // CareerSession_InitializeClubModalRuntime_ExecutableField is file-backed zero. These are explicit process-start values,
    // carried by this runtime owner across modal entry/return, not FBM bytes.
    for(uint32_t address:{::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableFieldUnsigned,::bmh::layout::CareerSession_DispatchPoachUi_ExecutableFieldUnsigned,::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableField2Unsigned,::bmh::layout::CareerSession_InitializeClubModalRuntime_ExecutableFieldUnsigned})
        club_modal_runtime_.emplace(address,exe_.u8(address));
}

// Collects the executable bytes and rating tables the Sportschau screen reads.
SportschauSourceOptions CareerSession::sportschau_source_options() const {
    return {source_runtime_byte(g_, club_modal_runtime_, exe_, ::bmh::layout::CareerSession_SportschauSourceOptions_ExecutableField),
            source_runtime_byte(g_, club_modal_runtime_, exe_, ::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableField2), exe_.u16(::bmh::layout::TeamsWithSquads),
            sportschau_rating_tables(exe_)};
}

// Builds the service callbacks of the club modal on a given (staged) state, generators and
// runtime byte map, so the modal can run on a copy and be committed or dropped by the caller.
ClubModalServices CareerSession::staged_club_modal_services(GameState& state,
    GameRandom& game,WatcomRandom& rand,std::map<uint32_t,uint8_t>& runtime) {
    ClubModalServices services;
    auto* g=&state; auto* gr=&game; auto* wr=&rand; auto* bytes=&runtime;
    services.state=[g]() -> GameState& { return *g; };
    services.enter_club=[g](int team) { return prepare_club_info_entry(*g,team); };
    services.change_category=[g](int team,int category) { return set_club_info_category(*g,team,category); };
    services.redraw_player=[this,g,gr,wr](const ClubPlayerDetailSelection& selection) {
        auto next_game=*gr; auto next_rand=*wr;
        auto result=club_player_detail_redraw(*g,selection,exe_,next_game,next_rand);
        *gr=next_game; *wr=next_rand; return result;
    };
    services.national_action=[g,wr](const ClubPlayerDetailSelection& selection) {
        auto next=*g; auto next_rand=*wr; MinuteReport report;
        const bool applied=club_player_detail_national_action(next,selection,next_rand,report);
        if(!report.reason.empty())throw FormatError(report.reason);
        *g=std::move(next); *wr=next_rand; return applied;
    };
    services.pending_offers=[this,g,gr,wr](const ClubPlayerDetailSelection& selection,const PendingOfferHooks& hooks) {
        auto next=*g; auto next_game=*gr; auto next_rand=*wr; MinuteReport report;
        auto result=process_player_card_pending_offers(next,selection,next_game,next_rand,
            pending_hooks_for_dispatch(hooks),report);
        if(report.unsupported||!result.completed)
            throw FormatError(report.reason.empty()?"TeamInfo: nested pending decision incomplete":report.reason);
        *g=std::move(next); *gr=next_game; *wr=next_rand; return result;
    };
    services.runtime_byte=[this,g,bytes](uint32_t address) {
        if(g->memory.has(address))return g->memory.u8(address);
        if(const auto at=bytes->find(address);at!=bytes->end())return at->second;
        // Explicit original initialization supplies the transient input owner.
        // Never serialize an absent BSS byte or infer Native absence as zero.
        return exe_.u8(address);
    };
    services.set_runtime_byte=[g,bytes](uint32_t address,uint8_t value) {
        if(g->memory.has(address))g->memory.set8(address,value);
        else (*bytes)[address]=value;
    };
    return services;
}

// Same services bound to the live state (non-transactional); pending offers go through
// dispatch_club_player_pending_offers. Throws when another modal owner is open.
ClubModalServices CareerSession::live_club_modal_services(uint64_t& revision) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) throw FormatError("strengths_active");
    auto services=staged_club_modal_services(g_,game_,rand_,club_modal_runtime_);
    services.transactional=false;
    // Existing cup/club caller keeps its separately owned pending transaction.
    services.pending_offers=[this,&revision](const ClubPlayerDetailSelection& selection,const PendingOfferHooks& hooks) {
        std::string error;
        auto result=dispatch_club_player_pending_offers(selection,revision,hooks,revision,error);
        if(!result)throw FormatError(error);
        return *result;
    };
    return services;
}

}  // namespace bmh::app
