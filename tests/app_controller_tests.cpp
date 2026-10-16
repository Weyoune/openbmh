#include "test_inputs.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <tuple>

#include "app/controller.hpp"
#include "app/window_actions.hpp"
#include "ui/manager.hpp"
#include "ui/contracts.hpp"
#include "game/month.hpp"
#include "game/desk_actions.hpp"
#include "game/weekly.hpp"
#include "ui/transfer_market.hpp"
#include "ui/club_player_detail.hpp"
#include "ui/league_table.hpp"
#include "ui/live_matchday.hpp"
#include "ui/live_set_piece.hpp"
#include "ui/desk_records.hpp"
#include "ui/game_end_alternate.hpp"
#include "ui/desktop.hpp"
#include "ui/settings.hpp"
#include "ui/environment.hpp"
#include "ui/season_entrants.hpp"
#include "formats/textd.hpp"
#include "game/messages.hpp"
#include "game/lineup.hpp"
#include "game/save.hpp"
#include "game/schedule.hpp"
#include "game/national.hpp"
#include "game/matchday.hpp"
#include "game/matchday_setup.hpp"
#include "game/reschedule.hpp"
#include "game/highscore.hpp"

using namespace bmh;
using namespace bmh::ui;

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

uint8_t session_runtime_byte(const GameState& state,const std::map<uint32_t,uint8_t>& runtime,uint32_t address) {
    if(state.memory.has(address))return state.memory.u8(address);
    const auto found=runtime.find(address);
    if(found==runtime.end())throw std::runtime_error("expected live Source runtime byte is absent");
    return found->second;
}

int32_t signed_u32(const std::vector<uint8_t>& bytes, size_t at) {
    return int32_t(uint32_t(bytes.at(at)) | uint32_t(bytes.at(at + 1)) << 8 |
                   uint32_t(bytes.at(at + 2)) << 16 | uint32_t(bytes.at(at + 3)) << 24);
}

void check_poach_ui_components(const std::filesystem::path& data) {
    StartupFlow flow(data);
    flow.click(150,215);flow.click(5,225,true);flow.click(5,225,true);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision=0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    app::CareerSession::PoachPlayerSelector selector;
    const auto serve=[&](const FlowResult& result) {
        check(app::serve_flow_request(flow,result,session,data,data,0x1789abcd,revision,playback,audio_session,false,
            {},{},{},{},{},{},{},{},{},app::PendingOfferEntryPolicy::legacy_unpresented,{},
            app::HumanBuyerMarketEntryPolicy::normal_interactive,{},{},{},{},{},{},selector).empty(),
            "poach uses the production controller");
    };
    serve(flow.click(90,169));
    check(session && flow.screen_name()=="desktop","poach starts in an actual career");
    auto& state=session->state();
    state.memory.set16(::bmh::layout::Savestate_Layout_ExecutableField3,0x1f0);
    check(state.memory.has(::bmh::layout::GameMode)&&state.memory.u8(::bmh::layout::GameMode)==3,"actual Mode III exposes Original Abwerben");
    flow.attach_preparation_context(session->preparation_projection(revision));
    const auto& menu=*std::find_if(flow.desktop()->state().items.begin(),flow.desktop()->state().items.end(),
        [](const DeskItem& item){return item.kind==17&&!item.disabled;});
    serve(flow.click(menu.x+menu.width/2,menu.y+menu.height/2));
    const auto exe=load_le(asset_path(data,"BMH.exe"));
    const auto texts=load_textd(asset_path(data,"TEXTD.BLK"));
    const int px=exe.u16(::bmh::layout::TransferMarket_Click_RuntimeField + 3*6)+2,py=exe.u16(::bmh::layout::TransferMarket_Click_RuntimeField2+3*6)+2;
    const auto open=[&] {
        auto result=flow.click(px,py);
        check(result.request==FlowRequest::submenu_action &&
            std::get<TransferMarketRequest>(result.submenu_action).kind==TransferMarketRequestKind::open_poach,
            "Original EXE Abwerben button emits its typed selection request");
        return result;
    };
    auto pending=open();
    const auto native=save_stream(state);const auto old_g=session->game();const auto old_w=session->rand();
    const auto old_runtime=session->club_modal_runtime_checkpoint();const auto old_rev=revision;
    selector=[&](app::CareerSession&,app::ClubModalServices& staged,int,uint64_t&,const Image&) -> std::optional<ClubPlayerDetailSelection> {
        staged.state().date++;
        state.date++;
        return std::nullopt;
    };
    serve(pending);
    check(save_stream(state)==native && state.date==flow.desktop()->state().day_counter && revision==old_rev &&
        session->game().seed()==old_g.seed()&&session->game().calls()==old_g.calls()&&
        session->rand().seed()==old_w.seed()&&session->rand().calls()==old_w.calls()&&
        session->club_modal_runtime_checkpoint()==old_runtime&&!session->transfer_offer_owner(),
        "reentrant selection/cancel restores session Native, date, G/W, runtime and revision");
    int selected_team=-1,selected_player=-1,physical=-1;
    for(int team=0;team<138&&selected_team<0;++team) {
        if(team==state.manager_teams[0])continue;
        const auto range=squad_range(team);
        for(size_t row=0;row<range.count;++row) {
            const int id=state.squad_slots.at(range.first+row)&0xfff;
            if(id==0xfff || (state.players.at(size_t(id)).w364&3)<2)continue;
            auto trial=state;auto w=session->rand();
            if(start_poach(trial,0,int(range.first+row),w).status==PoachStatus::ready) {
                selected_team=team;selected_player=id;physical=int(row);break;
            }
        }
    }
    check(selected_team>=0,"actual career has a Source-poachable ordinary forward");
    selector=[&](app::CareerSession& selector_session,app::ClubModalServices&,int manager,uint64_t& selector_revision,const Image& backdrop)
        -> std::optional<ClubPlayerDetailSelection> {
        check(manager==0&&backdrop.width==320,"poach owns its actual Transfer backdrop");
        const auto entry_g=selector_session.game(),live_g=session->game();
        const auto entry_w=selector_session.rand(),live_w=session->rand();
        check(entry_g.seed()==live_g.seed()&&entry_g.calls()==live_g.calls()&&
            entry_w.seed()==live_w.seed()&&entry_w.calls()==live_w.calls()&&
            selector_session.state().memory.u16(::bmh::layout::ManagerSettings)==state.memory.u16(::bmh::layout::ManagerSettings),
            "standalone50775 entry does not invoke53435 entry writes or family-close RNG");
        std::string error;
        auto page=selector_session.current_league_table_dispatch(error);check(bool(page),"selection owns the reused Table controls");
        if(page->identity.league!=team_league(selected_team)) {
            LeagueFamilyRequest map;map.identity=page->identity;map.from_child=1;
            map.kind=LeagueFamilyRequestKind::select_league;map.league_id=team_league(selected_team);
            check(selector_session.select_league_family_page(map,selector_revision,error),"Source map changes selection league");
            page=selector_session.current_league_table_dispatch(error);check(bool(page),"selected league has an owned page");
        }
        if(page->page.source_normal_mode==0) {
            LeagueFamilyRequest toggle;toggle.identity=page->identity;toggle.from_child=1;
            toggle.kind=LeagueFamilyRequestKind::control;toggle.control_index=7;
            page=selector_session.dispatch_league_table_control(toggle,selector_revision,error);
            check(bool(page),"selection reuses the real Source normal-mode control");
        }
        auto tables=league_tables_from_game(selector_session.state(),manager);
        tables[size_t(page->identity.league)]=page->page;
        LeagueTableScreen table(data,std::move(tables),page->identity.league,LeagueTableScreen::FamilyMode{page->identity,1});
        const auto row=std::find_if(page->page.rows.begin(),page->page.rows.end(),[&](const auto& r){return r.runtime_team_id==selected_team;});
        check(row!=page->page.rows.end(),"chosen club belongs to actual visible Source league");
        check(table.pointer_down(30,29+int(row-page->page.rows.begin())*7+3)&&table.pending_family_action(),
            "physical Table row DOWN requests the owned club");
        std::optional<ClubPlayerDetailSelection> chosen;
        const auto returned=selector_session.dispatch_family_table_team_info(*table.pending_family_action(),selector_revision,error,
            [&](app::ClubModalServices& staged,int team,const Image& table_backdrop) {
                check(team==selected_team&&staged.runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField)==2,"selected Source rank resolves actual club in poach mode");
                const auto entry=staged.enter_club(team);
                const auto info=club_info_from_game(staged.state(),team,entry,exe,texts);
                const auto player=std::find_if(info.players.begin(),info.players.end(),[&](const auto& p){return p.physical_row==physical&&p.player_id==selected_player;});
                check(player!=info.players.end(),"selected identity belongs to the actual Source club list");
                ClubPlayerDetailSelection selection{team,selected_player,player->rendered_row,physical,-1};
                auto redraw=staged.redraw_player(selection);
                const int slot=redraw.context.source_slot;
                ClubPlayerDetailScreen card(data,club_player_detail_from_game(staged.state(),std::move(redraw),exe),table_backdrop);
                check(!card.held_left_ticks(25)&&card.held_left_ticks(26),"Original long press requires more than25 ticks");
                const auto request=card.take_request();
                check(request.kind==ClubPlayerDetailRequestKind::close&&request.source_return==slot+1,
                    "Source card returns the concrete absolute squad slot plus one");
                chosen=request.selection;return true;
            },&chosen);
        if(!returned) throw std::runtime_error(error);
        check(chosen&&(selector_session.state().memory.u16(::bmh::layout::LeagueTableDisplayModes)&0x80),
            "valid50775 player selection preserves common low80 without an extra Table redraw");
        return chosen;
    };
    pending=open();
    const auto before_offer=save_stream(state);const auto before_offer_g=session->game();
    const auto before_offer_w=session->rand();const auto before_offer_runtime=session->club_modal_runtime_checkpoint();
    const auto before_offer_rev=revision;
    std::string reject_error;
    check(!session->dispatch_poach_ui(std::get<TransferMarketRequest>(pending.submenu_action),flow.render(),selector,
        [](const app::CareerSession::PoachDispatch& result,PreparationState) {
            check(bool(result.editor),"rollback test reached the real prepared poach editor");
            throw FormatError("controlled UI acknowledgement rejection");
        },revision,reject_error),"failed UI acknowledgement rejects prepared poach");
    check(save_stream(state)==before_offer&&revision==before_offer_rev&&!session->transfer_offer_owner()&&
        poach_attempts_left(state,0)==5&&session->game().seed()==before_offer_g.seed()&&
        session->game().calls()==before_offer_g.calls()&&session->rand().seed()==before_offer_w.seed()&&
        session->rand().calls()==before_offer_w.calls()&&session->club_modal_runtime_checkpoint()==before_offer_runtime&&
        flow.pending_transfer_market(),"failed UI ACK restores special rows, owner, attempts, revision, runtime and G/W");
    serve(pending);
    check(flow.screen_name()=="outgoing_transfer"&&session->transfer_offer_owner()&&
        session->transfer_offer_owner()->request.poach&&session->transfer_offer_owner()->request.list_entry>=40&&
        session->transfer_offer_owner()->request.list_entry<=43&&revision==old_rev+1,
        "typed player selection opens the actual special-row Original offer owner");
    check(poach_attempts_left(state,0)==5,"opening the editor does not prematurely count the attempt");
    const auto cancel_offer=flow.click(260,219);
    {
        const auto native=save_stream(state);const auto g=session->game();const auto w=session->rand();
        const auto rev=revision;bool acknowledged=false;std::string error;
        try {
            app::TransferActions(*session).offer(std::get<OutgoingTransferRequest>(cancel_offer.submenu_action),
                {},revision,error,{},{},[&](app::CareerSession::TransferOfferEditorDispatch& result) {
                    acknowledged=result.editor.closed;
                    session->game().range(0,10);session->rand().rand();
                    throw std::runtime_error("rejected poach cancel ACK");
                });
        } catch(const std::runtime_error&) {}
        check(acknowledged && save_stream(state)==native && revision==rev && session->transfer_offer_owner() &&
            session->game().seed()==g.seed() && session->game().calls()==g.calls() &&
            session->rand().seed()==w.seed() && session->rand().calls()==w.calls(),
            "shared offer rollback restores the suspended poach after a closing ACK fails");
    }
    serve(cancel_offer);
    check(flow.screen_name()=="transfer_market"&&!session->transfer_offer_owner()&&poach_attempts_left(state,0)==4,
        "physical Original editor cancel returns to Transfer and counts/frees the poach attempt");
    const auto completed_rev=revision;serve(pending);
    check(revision==completed_rev&&poach_attempts_left(state,0)==4,"consumed selection cannot reopen or count twice");
    selector=[&](app::CareerSession& staged,app::ClubModalServices&,int,uint64_t&,const Image&)
        -> std::optional<ClubPlayerDetailSelection> {
        std::string error;const auto page=staged.current_league_table_dispatch(error);
        check(bool(page),"cancel owns the real selection Table");
        auto tables=league_tables_from_game(staged.state(),0);tables[size_t(page->identity.league)]=page->page;
        LeagueTableScreen table(data,std::move(tables),page->identity.league,LeagueTableScreen::FamilyMode{page->identity,1});
        check(table.pointer_down(1,1,true)&&table.pending_family_action(),"selection right-DOWN posts Source close");
        check(staged.close_poach_table_selection(*table.pending_family_action(),error),"standalone selection close accepts its owner");
        return std::nullopt;
    };
    const auto cancel_g=session->game();const auto cancel_w=session->rand();
    serve(open());
    check(flow.screen_name()=="transfer_market"&&revision==completed_rev+1&&!session->transfer_offer_owner()&&
        poach_attempts_left(state,0)==4&&session->game().seed()==cancel_g.seed()&&session->game().calls()==cancel_g.calls()&&
        session->rand().seed()==cancel_w.seed()&&session->rand().calls()==cancel_w.calls(),
        "Source selection cancel returns to Transfer without an attempt or family-close W/G stir");
    const auto cancelled_revision=revision;
    // Source loan mode rejects before asking the player selector to run.
    flow.click(200,207); // no marks: toggle quote loan mode
    bool selected_again=false;
    selector=[&](app::CareerSession&,app::ClubModalServices&,int,uint64_t&,const Image&) -> std::optional<ClubPlayerDetailSelection> {selected_again=true;return std::nullopt;};
    const auto before_loan=save_stream(state);const auto loan_w=session->rand();
    serve(open());
    check(!selected_again&&revision==cancelled_revision&&save_stream(state)==before_loan&&session->rand().calls()==loan_w.calls(),
        "loan-mode prohibition precedes Table selection and preserves attempts/Native/W");
}

void check_transfer_market_controller(const std::filesystem::path& data) {
    StartupFlow flow(data);
    flow.click(150, 215); flow.click(5, 225, true); flow.click(5, 225, true);
    auto started = flow.click(90, 169);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    const auto serve = [&](const FlowResult& result) {
        check(app::serve_flow_request(flow, result, session, data, data, 0x1789abcd,
            revision, playback, audio_session, false).empty(), "transfer flow uses production App controller");
    };
    serve(started);
    check(session && flow.screen_name() == "desktop", "transfer test begins in an actual initialized career");
    auto& game = session->state();
    MinuteReport market_report;
    new_transfer_list_entries(game, session->rand(), market_report);
    check(!market_report.unsupported, "real transfer pool is initialized by the engine API");
    // Declared saved view control: Source position classes enabled and ordinary
    // market/roster split, rather than the removed free-form sell screen.
    game.memory.set16(::bmh::layout::Savestate_Layout_ExecutableField3, 0x1f0);
    flow.attach_preparation_context(session->preparation_projection(revision));

    const auto menu = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
        [](const DeskItem& item) { return item.kind == 17 && !item.disabled; });
    check(menu != flow.desktop()->state().items.end(), "actual desktop offers menu 17");
    const auto opened = flow.click(menu->x + menu->width / 2, menu->y + menu->height / 2);
    check(opened.request == FlowRequest::submenu_opened && flow.screen_name() == "transfer_market",
          "menu 17 opens the real transfer screen");
    serve(opened);

    const auto initial = transfer_market_from_game(game, revision);
    {
        app::TransferActions common(*session);
        const auto native=save_stream(game);const auto g=session->game();const auto w=session->rand();
        const auto rev=revision;const auto runtime=session->club_modal_runtime_checkpoint();
        auto observed=initial;++observed.revision;
        std::string error;
        check(!common.market(app::TransferActions::listing(observed,observed.squad.front(),false),{},revision,error),
              "shared transfer boundary rejects a stale observed listing");
        check(!common.market(app::TransferActions::listing(observed,observed.squad.front(),false),{},revision,error,
                  app::HumanBuyerMarketEntryPolicy::legacy_unpresented),
              "shared legacy transfer path rejects the same stale identity before an engine draw");
        check(!common.apply(revision,[&] {
            game.date++;session->rand().rand();session->game().range(0,10);++revision;
            auto changed=runtime;changed[::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField]=1;
            session->restore_club_modal_runtime(std::move(changed));
            return false;
        }) && save_stream(game)==native && revision==rev &&
            session->game().seed()==g.seed() && session->game().calls()==g.calls() &&
            session->rand().seed()==w.seed() && session->rand().calls()==w.calls() &&
            session->club_modal_runtime_checkpoint()==runtime && !session->transfer_offer_owner(),
            "shared transfer rejection restores Native, both RNGs, revision, runtime and owner");
    }
    int listing_row = -1;
    TransferActionResult expected_listing;
    for (int row = std::min(17, int(initial.squad.size()) - 1); row >= 11; --row) {
        const auto& player = initial.squad[size_t(row)];
        if (player.player < 0 || player.already_listed) continue;
        auto trial_state = game;
        auto trial_rand = session->rand();
        MinuteReport trial_report;
        const auto trial = list_manager_player(trial_state, initial.manager_slot, player.row,
                                               trial_rand, trial_report);
        if (trial.status == TransferActionStatus::accepted && !trial_report.unsupported) {
            listing_row = player.row;
            expected_listing = trial;
            break;
        }
    }
    check(listing_row >= 0, "live manager roster has a listable reserve player");
    int list_y = -1, baseline = 36;
    for (size_t n = 0; n < initial.roster.players.size(); ++n) {
        const auto& player = initial.roster.players[n];
        if (player.borrowed) continue;
        if (n && (player.displayed_slot & 0xc000) != (initial.roster.players[n-1].displayed_slot & 0xc000))
            baseline += 2;
        if (player.player_id < 0) continue;
        if (player.physical_row == listing_row) { list_y = baseline - 3; break; }
        baseline += 7;
    }
    check(list_y >= 29 && list_y < 215, "listable player is visible at its Original roster coordinate");
    const auto roster_preview = flow.move_action(20, list_y);
    check(roster_preview.request == FlowRequest::submenu_action &&
          std::get<TransferMarketRequest>(roster_preview.submenu_action).kind == TransferMarketRequestKind::preview_quote,
          "Source roster hover sends its owned outgoing value preview");
    // Hover belongs to the normal Source owner even while the compatibility
    // controller below retains the already-published listing path.
    check(app::serve_flow_request(flow, roster_preview, session, data, data, 0x1789abcd,
        revision, playback, audio_session, false, {}, {}, {}, {}, {}, {}, {}, {}, {},
        app::PendingOfferEntryPolicy::legacy_unpresented, {},
        app::HumanBuyerMarketEntryPolicy::normal_interactive).empty(), "real roster preview uses the production controller");
    check(!flow.pending_transfer_market(), "Source roster preview acknowledged before mark/action");
    flow.click(20, list_y);
    auto listing_request = flow.click(200, 207);
    check(listing_request.request == FlowRequest::submenu_action &&
          std::holds_alternative<TransferMarketRequest>(listing_request.submenu_action) &&
          std::get<TransferMarketRequest>(listing_request.submenu_action).kind == TransferMarketRequestKind::list_player &&
          std::get<TransferMarketRequest>(listing_request.submenu_action).squad_row == listing_row &&
          std::get<TransferMarketRequest>(listing_request.submenu_action).selected_players.size() == 1,
          "selecting a roster player sends a typed list-player request");
    const auto list_action = std::get<TransferMarketRequest>(listing_request.submenu_action);
    const size_t squad_slot = squad_range(list_action.runtime_club).first + size_t(list_action.squad_row);
    const uint16_t listed_player = game.squad_slots.at(squad_slot) & 0x0fff;
    const auto before_forgery = save_stream(game);
    const auto rand_seed = session->rand().seed();
    const auto rand_calls = session->rand().calls();
    const uint64_t before_listing_revision = revision;

    auto forged_revision = listing_request;
    std::get<TransferMarketRequest>(forged_revision.submenu_action).revision--;
    serve(forged_revision);
    check(save_stream(game) == before_forgery && revision == before_listing_revision &&
          session->rand().seed() == rand_seed && session->rand().calls() == rand_calls &&
          flow.pending_transfer_market(),
          "stale list envelope is inert and leaves the valid pending request available");

    auto altered_manager_list = list_action;
    altered_manager_list.manager_slot++;
    auto forged_manager_list = listing_request;
    forged_manager_list.submenu_action = altered_manager_list;
    serve(forged_manager_list);
    check(save_stream(game) == before_forgery && revision == before_listing_revision &&
          session->rand().seed() == rand_seed && session->rand().calls() == rand_calls &&
          flow.pending_transfer_market(), "forged list manager is inert");

    const uint16_t old_slot = game.squad_slots.at(squad_slot);
    const int replacement = (listed_player + 1) % int(game.players.size());
    game.squad_slots.at(squad_slot) = uint16_t((old_slot & 0xf000) | uint16_t(replacement));
    const auto stale_player_state = save_stream(game);
    serve(listing_request);
    check(save_stream(game) == stale_player_state && revision == before_listing_revision &&
          session->rand().seed() == rand_seed && session->rand().calls() == rand_calls &&
          !flow.pending_transfer_market(), "stale roster player is rejected before engine/RNG mutation");
    game.squad_slots.at(squad_slot) = old_slot;

    auto retry_listing = flow.click(200, 207);
    check(retry_listing.request == FlowRequest::submenu_action, "rejected listing can be resubmitted from current roster");
    auto expected_state = game;
    auto expected_rand = session->rand();
    MinuteReport expected_report;
    expected_listing = list_manager_player(expected_state, list_action.manager_slot, list_action.squad_row,
                                           expected_rand, expected_report);
    check(expected_listing.status == TransferActionStatus::accepted && !expected_report.unsupported,
          "independent reference listing succeeds");
    const auto expected_list_bytes = save_stream(expected_state);
    serve(retry_listing);
    check(revision == before_listing_revision + 1 && save_stream(game) == expected_list_bytes &&
          session->rand().seed() == expected_rand.seed() && session->rand().calls() == expected_rand.calls() &&
          !flow.pending_transfer_market() && flow.screen_name() == "transfer_market",
          "App ACK matches independent list-manager engine state and consumes one revision");
    const auto listed_projection = transfer_market_from_game(game, revision);
    check(std::any_of(listed_projection.items.begin(), listed_projection.items.end(), [&](const auto& item) {
              return item.player == listed_player && item.own_listing;
          }) && flow.state_json().find("Der Spieler steht jetzt auf der Transferliste.") != std::string::npos,
          "listing returns owned listing and visible confirmation to the UI");
    const auto listed_snapshot = save_stream(game);
    const auto listed_seed = session->rand().seed();
    const auto listed_calls = session->rand().calls();
    const uint64_t listed_revision = revision;
    serve(retry_listing);
    check(save_stream(game) == listed_snapshot && revision == listed_revision &&
          session->rand().seed() == listed_seed && session->rand().calls() == listed_calls,
          "consumed list request replay is inert");

    // The old KB23 editor/legacy heuristic is no longer the ordinary GUI route.
    // Keep its controller capability checks on real Source KB31 events, with
    // the Core owner as the integration reference (not an independent EXE oracle).
    check(game.memory.u8(::bmh::layout::GameMode) == 3, "actual startup creates saved Source Mode III for qualification");
    flow.send_key("ESC");
    flow.attach_preparation_context(session->preparation_projection(revision));
    const auto market_menu = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
        [](const DeskItem& item) { return item.kind == 17 && !item.disabled; });
    check(market_menu != flow.desktop()->state().items.end(), "refreshed desktop retains market capability");
    flow.click(market_menu->x + market_menu->width/2, market_menu->y + market_menu->height/2);
    std::vector<uint32_t> presented;
    HumanBuyerSaleHooks presenters;
    presenters.allocator_gate = HumanBuyerSaleAllocatorGate::zero;
    presenters.direct_mode = HumanBuyerSaleDirectMode::ordinary_ai_purchase;
    presenters.notice = [&](const GameState&, const HumanBuyerSaleNotice& notice) { presented.push_back(notice.source_text_address); };
    const auto serve_source = [&](const FlowResult& result) {
        check(app::serve_flow_request(flow, result, session, data, data, 0x1789abcd,
            revision, playback, audio_session, false, {}, {}, {}, {}, {}, {}, {}, {}, {},
            app::PendingOfferEntryPolicy::legacy_unpresented, presenters,
            app::HumanBuyerMarketEntryPolicy::normal_interactive).empty(), "KB31 events use the production controller");
    };
    const auto visible_entries = [&] {
        std::vector<int> out; const auto text = flow.state_json();
        const auto at = text.find("\"visible_market_entries\":[");
        check(at != std::string::npos, "Source market exposes its exact visible list identities");
        const auto end = text.find(']', at); size_t cursor = at + std::string("\"visible_market_entries\":[").size();
        while (cursor < end) { if (text[cursor] == ',' || text[cursor] == ' ') { ++cursor; continue; }
            size_t used = 0; out.push_back(std::stoi(text.substr(cursor,end-cursor), &used)); cursor += used; }
        return out;
    };
    const auto source_input = [&](const TransferMarketItem& item, const GameState& state) {
        const uint32_t list = ::bmh::layout::EditorList + uint32_t(item.list_entry)*0x58; int bid = -1;
        for (int n = 0; n < 4; ++n) if (state.memory.u16(list+uint32_t(n)*2) == 0xffff ||
            state.memory.u16(list+uint32_t(n)*2) == initial.runtime_club) { bid=n; break; }
        return TransferOfferEditorRequest{initial.manager_slot,initial.runtime_club,item.list_entry,
            item.source_slot,item.player,state.memory.u16(list+8),bid,false};
    };
    struct OfferChoice { TransferMarketItem item; uint32_t fee=0, salary=0; uint32_t expected_notice=0; };
    const auto choose_offer = [&](uint32_t wanted_notice) {
        const auto market = transfer_market_from_game(game,revision);
        for (const auto& item : market.items) {
            if (item.own_listing || item.own_bid_pending || item.asking_fee <= 0 || !item.salary_reference) continue;
            for (unsigned pct : {1u,100u,150u,200u}) {
                auto trial = game; auto trial_g = session->game(); auto trial_w = session->rand();
                auto owner = open_transfer_offer_editor_source(trial,source_input(item,trial),trial_g,trial_w);
                if (!owner.supported || !owner.editable) continue;
                const uint32_t fee = wanted_notice == ::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField ?
                    std::min(owner.money_profiles[0].maximum, uint32_t(item.asking_fee)*pct/100u) : 1u;
                const uint32_t salary = wanted_notice == ::bmh::layout::DeskActions_Unsupported_ExecutableField3 ? 1u : owner.money_profiles[1].maximum;
                owner=edit_transfer_offer_editor_source(trial,owner,{TransferOfferEditorField::fee,fee},trial_g,trial_w);
                owner=edit_transfer_offer_editor_source(trial,owner,{TransferOfferEditorField::salary,salary},trial_g,trial_w);
                if (!owner.supported) continue;
                MinuteReport report; std::vector<uint32_t> notices; auto hooks=presenters;
                hooks.notice=[&](const GameState&,const HumanBuyerSaleNotice& n){notices.push_back(n.source_text_address);};
                const auto result=submit_transfer_offer_editor_source(trial,owner,trial_w,hooks,report);
                if (report.unsupported || notices.empty()) continue;
                if (wanted_notice == ::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField && (notices.front()!=::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField || result.action.status!=TransferActionStatus::accepted)) continue;
                if (wanted_notice != ::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField && notices.front()!=wanted_notice) continue;
                return OfferChoice{item,fee,salary,wanted_notice};
            }
        }
        throw std::runtime_error("no genuine Source market candidate has requested qualification outcome");
    };
    const auto request_bid = [&](const OfferChoice& choice) {
        check(flow.screen_name()=="transfer_market", "real Source parent available before opening");
        int row=-1;
        for (int page=0;page<7 && row<0;++page) { const auto entries=visible_entries();
            const auto found=std::find(entries.begin(),entries.end(),choice.item.list_entry);
            if (found!=entries.end()) row=int(found-entries.begin());
            else flow.click(250,40,true);
        }
        check(row>=0,"purchase reaches its Source six-row page through actual navigation");
        const int y=32+row*15;
        serve_source(flow.move_action(220,y));
        flow.click(220,y); // mark only after the Source preview is acknowledged.
        const auto opened=flow.click(200,207);
        check(opened.request==FlowRequest::submenu_action &&
              std::get<TransferMarketRequest>(opened.submenu_action).kind==TransferMarketRequestKind::open_offer,
              "Source market action emits typed KB31 opening");
        const auto& open=std::get<TransferMarketRequest>(opened.submenu_action);
        check(open.bid.player==choice.item.player && open.bid.list_entry==choice.item.list_entry &&
              open.bid.source_slot==choice.item.source_slot && open.selected_players.size()==1,
              "physical opening preserves selected player/slot/manager identity");
        {
            const auto native=save_stream(game);const auto g=session->game();const auto w=session->rand();
            const auto rev=revision;const auto runtime=session->club_modal_runtime_checkpoint();
            bool acknowledged=false;std::string error;
            try {
                app::TransferActions(*session).open_offer(open,revision,error,
                    [&](app::CareerSession::TransferOfferEditorDispatch&) {
                        acknowledged=bool(session->transfer_offer_owner());
                        game.date++;session->rand().rand();session->game().range(0,10);
                        throw std::runtime_error("rejected transfer frontend ACK");
                    });
            } catch(const std::runtime_error&) {}
            check(acknowledged && save_stream(game)==native && revision==rev &&
                session->game().seed()==g.seed() && session->game().calls()==g.calls() &&
                session->rand().seed()==w.seed() && session->rand().calls()==w.calls() &&
                session->club_modal_runtime_checkpoint()==runtime && !session->transfer_offer_owner(),
                "shared transfer opening restores complete state when the frontend ACK throws");
        }
        serve_source(opened);
        check(flow.screen_name()=="outgoing_transfer" && session->transfer_offer_owner(),"controller opens actual KB31 owner");
        const auto field=[&](int x,uint32_t value) {
            flow.click(x,123); check(flow.text(std::to_string(value)),"actual KB6 accepts entered offer value");
            const auto edit=flow.send_key("ENTER");
            check(edit.request==FlowRequest::submenu_action &&
                  std::get<OutgoingTransferRequest>(edit.submenu_action).kind==OutgoingTransferRequestKind::quote_change,
                  "calculator emits a typed single-field Source commit");
            serve_source(edit);
        };
        field(40,choice.fee); field(200,choice.salary);
        const auto submit=flow.click(180,219);
        check(submit.request==FlowRequest::submenu_action && std::holds_alternative<OutgoingTransferRequest>(submit.submenu_action),
              "live KB31 Angebot emits the typed submit request");
        const auto& event=std::get<OutgoingTransferRequest>(submit.submenu_action);
        check(event.identity.manager_slot==initial.manager_slot && event.identity.player==choice.item.player &&
              event.identity.list_entry==choice.item.list_entry && event.identity.source_slot==choice.item.source_slot &&
              event.fee==choice.fee && event.monthly_salary==choice.salary,
              "KB31 submit preserves Native identity and entered fee/salary");
        return submit;
    };
    const auto retry_submit=[&] { const auto r=flow.click(180,219);
        check(r.request==FlowRequest::submenu_action && flow.pending_transfer_offer(),"rejected KB31 submit remains physically retryable");return r; };
    const auto stable=[&](const std::vector<uint8_t>& bytes,const GameRandom& g,const WatcomRandom& w,uint64_t rev) {
        return save_stream(game)==bytes && revision==rev && session->game().seed()==g.seed() && session->game().calls()==g.calls() &&
            session->rand().seed()==w.seed() && session->rand().calls()==w.calls();
    };
    const auto check_submit=[&](const FlowResult& submitted,const OfferChoice& choice) {
        auto expected=game;auto expected_g=session->game();auto expected_w=session->rand();
        MinuteReport report;std::vector<uint32_t> notices;auto hooks=presenters;
        hooks.notice=[&](const GameState&,const HumanBuyerSaleNotice& n){notices.push_back(n.source_text_address);};
        const auto reference=submit_transfer_offer_editor_source(expected,*session->transfer_offer_owner(),expected_w,hooks,report);
        check(!report.unsupported && !notices.empty() && notices.front()==choice.expected_notice,
              "current Source owner reference retains the meaningful qualification outcome");
        const auto rev=revision;const auto balance=signed_u32(game.manager_records,0x314);presented.clear();
        serve_source(submitted);
        check(save_stream(game)==save_stream(expected) && session->game().seed()==expected_g.seed() &&
              session->game().calls()==expected_g.calls() && session->rand().seed()==expected_w.seed() &&
              session->rand().calls()==expected_w.calls() && revision==rev+1 && presented==notices && !flow.pending_transfer_offer(),
              "actual controller submit matches complete Core Native/G/W/notice sequence once");
        const auto after=save_stream(game);const auto after_g=session->game();const auto after_w=session->rand();const auto after_rev=revision;
        serve_source(submitted);
        check(stable(after,after_g,after_w,after_rev),"consumed offer request replay cannot consume a second draw or commit");
        const auto projection=transfer_market_from_game(game,revision);
        if (reference.action.status==TransferActionStatus::accepted) {
            const auto range=squad_range(initial.runtime_club);bool bought=false;
            for(size_t n=0;n<range.count;++n)bought|=(game.squad_slots[range.first+n]&0xfff)==choice.item.player;
            check(reference.editor.closed && flow.screen_name()=="transfer_market" && bought &&
                  signed_u32(game.manager_records,0x314)<balance &&
                  std::none_of(projection.items.begin(),projection.items.end(),[&](const auto& i){return i.player==choice.item.player;}),
                  "immediate Source agreement charges cash, moves roster and refreshes owned market");
        } else {
            check(!reference.editor.closed && flow.screen_name()=="outgoing_transfer" &&
                  signed_u32(game.manager_records,0x314)==balance &&
                  std::any_of(projection.items.begin(),projection.items.end(),[&](const auto& i){return i.player==choice.item.player && i.own_bid_pending;}),
                  "Source low/auction-pending bid retains Native offer without charging money or moving player");
            serve_source(flow.click(260,219));
            check(flow.screen_name()=="transfer_market", "physical Cancel returns retained bid to refreshed Source market");
        }
    };
    const auto low=choose_offer(::bmh::layout::DeskActions_Unsupported_ExecutableField3);auto low_request=request_bid(low);
    auto forged=low_request;std::get<OutgoingTransferRequest>(forged.submenu_action).identity.manager_slot++;
    const auto before=save_stream(game);const auto before_g=session->game();const auto before_w=session->rand();const auto before_rev=revision;
    serve_source(forged);
    check(stable(before,before_g,before_w,before_rev)&&flow.pending_transfer_offer(),"forged bid manager envelope is inert before Native/RNG");
    auto forged_offer_revision=low_request;std::get<OutgoingTransferRequest>(forged_offer_revision.submenu_action).identity.revision--;
    serve_source(forged_offer_revision);
    check(stable(before,before_g,before_w,before_rev)&&flow.pending_transfer_offer(),"stale bid revision envelope preserves valid physical pending request");
    game.memory.set8(::bmh::layout::ActiveManager,1);const auto stale_manager=save_stream(game);
    serve_source(low_request);
    check(stable(stale_manager,before_g,before_w,before_rev)&&!flow.pending_transfer_offer(),"stale actual active manager rejects before Native/G/W mutation");
    game.memory.set8(::bmh::layout::ActiveManager,0);
    low_request=retry_submit();
    const size_t pool=size_t(low.item.source_slot);const uint16_t pool_before=game.squad_slots.at(pool);
    int replacement_player=-1;for(uint16_t slot:game.squad_slots){const int id=slot&0xfff;if(id!=0xfff&&id!=low.item.player&&size_t(id)<game.players.size()){replacement_player=id;break;}}
    check(replacement_player>=0,"genuine career contains a distinct replacement player");
    game.squad_slots[pool]=uint16_t((pool_before&0xf000)|replacement_player);const auto stale_player=save_stream(game);
    serve_source(low_request);
    check(stable(stale_player,before_g,before_w,before_rev)&&!flow.pending_transfer_offer(),"recycled market identity rejects before Native/RNG/revision");
    game.squad_slots[pool]=pool_before;
    check_submit(retry_submit(),low);
    // A high salary plus a negligible fee reaches the ordinary auction refusal,
    // distinct from the preceding low-salary short circuit; both retain a bid.
    OfferChoice pending;
    try { pending=choose_offer(::bmh::layout::DeskActions_Unsupported_ExecutableField2); } catch(const std::runtime_error&) { pending=choose_offer(::bmh::layout::DeskActions_Unsupported_TextPointer3); }
    check_submit(request_bid(pending),pending);
    const auto accepted=choose_offer(::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField);
    check_submit(request_bid(accepted),accepted);

}

void check_mailbox_actions(const std::filesystem::path& data) {
    StartupFlow flow(data);
    flow.click(150, 215); flow.click(5, 225, true); flow.click(5, 225, true);
    const auto career = flow.click(90, 169);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    const auto serve = [&](const FlowResult& result) {
        check(app::serve_flow_request(flow, result, session, data, data, 0x12345678,
            revision, playback, audio_session, false).empty(), "mailbox actions use production controller");
    };
    serve(career);
    check(bool(session), "mailbox fixture creates a career");
    auto& game = session->state();
    check(game.memory.has(::bmh::layout::MailboxRecipientMask) &&
          (game.memory.u32(::bmh::layout::MailboxRecipientMask) & (uint32_t(1) << flow.desktop()->state().manager_slot)),
          "Source message posting sets the active manager format field latch");
    const auto notification_mask = game.memory.u32(::bmh::layout::MailboxRecipientMask);
    const auto notification_save = save_stream(game);
    const auto quickbar_revision = revision;
    const auto quickbar_game_calls = session->game().calls(), quickbar_watcom_calls = session->rand().calls();
    const auto edge_motion = flow.move_action(100, 239);
    check(edge_motion.request == FlowRequest::none &&
          flow.desktop()->state_json().find("\"shortcut_bar_visible\":true") != std::string::npos &&
          save_stream(game) == notification_save && revision == quickbar_revision &&
          session->game().calls() == quickbar_game_calls && session->rand().calls() == quickbar_watcom_calls,
          "production Flow lower-edge motion shows quickbar without controller, save or RNG changes");
    if (!flow.desktop()->state().shortcut_bar_always)
        check(edge_motion.changed, "physical lower-edge motion requests the hidden quickbar SDL redraw");
    check(!flow.move_action(120, 239).changed,
          "production Flow consecutive bottom-edge samples preserve the shown bar");
    flow.click(120, 239);
    check(flow.desktop()->state_json().find("\"shortcut_bar_visible\":true") != std::string::npos,
          "physical click after lower-edge motion does not retoggle the quickbar");
    flow.click(10, 215);  // Toggle legacy routine's presentation-local bin mode.
    check(flow.desktop()->state_json().find("\"bin_mode\":true") != std::string::npos,
          "mailbox parent fixture enters the local bin mode without a Native write");
    auto notification = session->pending_mailbox_notification(revision);
    check(notification && notification->manager_slot == flow.desktop()->state().manager_slot,
          "only the active ordinary manager receives a pending automatic mailbox projection");
    flow.show_notified_mailbox(std::move(*notification));
    check(flow.screen_name() == "mailbox" && !flow.desktop()->pending(),
          "format field auto-opens the reader without fabricating a desk click");
    const auto close_notification = flow.send_key("ESC");
    check(close_notification.request == FlowRequest::mailbox_closed &&
          close_notification.mailbox_close.revision == revision,
          "mailbox close returns an owner-bound notification acknowledgement");
    serve(close_notification);
    const uint32_t active_bit = uint32_t(1) << flow.desktop()->state().manager_slot;
    check(!session->pending_mailbox_notification(revision) &&
          game.memory.u32(::bmh::layout::MailboxRecipientMask) == (notification_mask & ~active_bit) &&
          revision == close_notification.mailbox_close.revision && save_stream(game) == notification_save,
          "reader close clears only the active transient bit without revision or FBM writes");
    check(flow.desktop()->state_json().find("\"bin_mode\":true") != std::string::npos,
          "returning from the reader preserves the desk's local bin mode");
    flow.click(10, 215);
    game.memory.set8(::bmh::layout::DeskState_FormatError_RuntimeField3, 0);
    game.mailbox.assign(kMailboxEntries * kMailboxEntry, 0);
    post_composed(game, 0, "Gemeinsame Nachricht");
    game.mailbox[0] = 3; game.mailbox[3] = 0x80; // Manager 0+1 and Original broadcast marker.
    post_composed(game, 0, "Private Nachricht");
    const auto price = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
        [](const DeskItem& i) { return i.kind == 55 && i.open; });
    check(price != flow.desktop()->state().items.end(), "mailbox fixture has price widget");
    serve(flow.click(price->x + 39, price->y + 10));
    serve(flow.click(183, 220));
    check(flow.screen_name() == "mailbox", "mailbox opens in production App");
    const auto date = game.date;
    const auto gs = session->game().seed(); const auto ws = session->rand().seed();
    const auto gc = session->game().calls(), wc = session->rand().calls();
    const auto stable = [&] {
        check(game.date == date && session->game().seed() == gs && session->rand().seed() == ws &&
              session->game().calls() == gc && session->rand().calls() == wc,
              "article actions preserve calendar and both RNG streams");
    };
    auto expected = game;
    const auto before = revision;
    serve(flow.click(85, 205)); // Merken mode.
    const auto keep = flow.click(60, 70);
    check(keep.request == FlowRequest::mailbox_action && keep.mailbox_action.entry_index == 0 &&
          flow.pending_mailbox() && !flow.send_key("ESC").changed,
          "article request is bound to modal revision and owns input until ACK");
    auto wrong_context = session->preparation_projection(revision + 1);
    ++wrong_context.desktop.season_index;
    bool context_rejected = false;
    try { flow.acknowledge_mailbox(mailbox_state_from_game(game, 0, revision + 1), wrong_context); }
    catch (const FormatError&) { context_rejected = true; }
    check(context_rejected && flow.pending_mailbox() && flow.desktop()->state().revision == revision,
          "mailbox acknowledgement cannot advance calendar or replace another context");
    auto forged = keep; forged.mailbox_action.kind = MailboxActionKind::delete_for_manager;
    serve(forged);
    check(revision == before && save_stream(game) == save_stream(expected) && flow.pending_mailbox(),
          "forged event neither writes nor consumes the valid pending article action");
    serve(keep); expected.mailbox[4] ^= 1;
    check(revision == before + 1 && save_stream(game) == save_stream(expected) && !flow.pending_mailbox() &&
          flow.desktop()->state().revision == revision && flow.screen_name() == "mailbox",
          "keep changes only active manager bit and refreshes all modal/parent context");
    serve(keep);
    check(revision == before + 1 && save_stream(game) == save_stream(expected), "consumed action replay is inert");
    serve(flow.click(110, 205));
    serve(flow.click(60, 70));
    check(revision == before + 1 && save_stream(game) == save_stream(expected) && !flow.pending_mailbox(),
          "kept message cannot be deleted and rejection restores usable input");
    serve(flow.click(85, 205)); serve(flow.click(60, 70)); expected.mailbox[4] ^= 1;
    check(revision == before + 2 && save_stream(game) == save_stream(expected), "unkeep toggles only manager keep bit");
    serve(flow.click(110, 205)); serve(flow.click(60, 70)); expected.mailbox[0] = 2;
    check(revision == before + 3 && save_stream(game) == save_stream(expected) &&
          mailbox_state_from_game(game, 0).messages.size() == 1,
          "delete removes only this manager recipient and preserves other manager, broadcast, text and date");
    // Physical entries can be recycled without an App revision change. Compare all 160 bytes.
    const auto stale = flow.click(60, 70);
    check(stale.request == FlowRequest::mailbox_action && stale.mailbox_action.entry_index == 1,
          "remaining article retains physical entry identity");
    game.mailbox[kMailboxEntry + 159] ^= 1;
    expected = game;
    serve(stale);
    check(revision == before + 3 && save_stream(game) == save_stream(expected) && !flow.pending_mailbox(),
          "recycled or changed entry is rejected even when only an unused tail byte changed");
    serve(flow.send_key("ESC"));
    check(flow.screen_name() == "desktop" && flow.desktop()->state().revision == revision,
          "mailbox closes to usable authoritative parent");
    serve(flow.click(183, 220)); serve(flow.click(110, 205)); serve(flow.click(60, 70));
    expected.mailbox[kMailboxEntry] = 0;
    check(revision == before + 4 && save_stream(game) == save_stream(expected) &&
          flow.state_json().find("\"message_count\":0") != std::string::npos &&
          flow.state_json().find("\"page_count\":1") != std::string::npos,
          "last deletion shows an explicit empty owned page without synthetic messages");
    stable();
    serve(flow.send_key("ESC"));

    // Exercise the Original's asymmetric recipient-word + 1 five-entry guard.
    game.mailbox.assign(kMailboxEntries * kMailboxEntry, 0);
    for (int i = 0; i < 6; ++i) post_composed(game, 0, "Grenze");
    for (int i = 0; i < 5; ++i) game.mailbox[size_t(i) * kMailboxEntry + 4] = 1;
    auto request = [&](int slot, int manager, MailboxActionKind kind) {
        MailboxActionRequest r{kind, manager, revision, slot};
        std::copy_n(game.mailbox.begin() + size_t(slot) * kMailboxEntry, kMailboxEntry, r.expected_entry.begin());
        return r;
    };
    expected = game;
    std::string error;
    const auto cap_revision = revision;
    check(!session->dispatch_mailbox(request(5, 0, MailboxActionKind::toggle_keep), revision, error) &&
          !session->dispatch_mailbox(request(0, 0, MailboxActionKind::toggle_keep), revision, error) &&
          revision == cap_revision && save_stream(game) == save_stream(expected),
          "Original manager-zero five-kept guard blocks both new keep and unkeep without writes");
    auto stale_rev = request(5, 0, MailboxActionKind::delete_for_manager); --stale_rev.revision;
    check(!session->dispatch_mailbox(stale_rev, revision, error), "stale revision is rejected");
    auto wrong_manager = request(5, 1, MailboxActionKind::delete_for_manager);
    check(!session->dispatch_mailbox(wrong_manager, revision, error), "inactive manager request is rejected");
    game.manager_teams.push_back(game.manager_teams.front());
    game.memory.set8(::bmh::layout::ActiveManager, 1);
    for (int i = 0; i < 6; ++i) game.mailbox[size_t(i) * kMailboxEntry] = 2;
    for (int i = 0; i < 5; ++i) game.mailbox[size_t(i) * kMailboxEntry + 4] = 2;
    expected = game; expected.mailbox[5 * kMailboxEntry + 4] = 2;
    check(session->dispatch_mailbox(request(5, 1, MailboxActionKind::toggle_keep), revision, error) &&
          revision == cap_revision + 1 && save_stream(game) == save_stream(expected),
          "Original manager-one recipient-plus-one exception permits a sixth kept entry");
    game.mailbox[5 * kMailboxEntry + 4] = 0;
    game.mailbox[5 * kMailboxEntry + 3] = 0x80;
    expected = game; expected.mailbox[5 * kMailboxEntry] = 0; expected.mailbox[5 * kMailboxEntry + 3] = 0;
    check(session->dispatch_mailbox(request(5, 1, MailboxActionKind::delete_for_manager), revision, error) &&
          save_stream(game) == save_stream(expected), "last manager deletion clears an orphaned broadcast marker");
    stable();
}

void check_strengths_semantic_and_controller(const std::filesystem::path& data) {
    const auto exe=load_le(asset_path(data,"BMH.exe"));
    StartupFlow flow(data);
    flow.click(150,215);flow.click(5,225,true);flow.click(5,225,true);
    std::unique_ptr<app::CareerSession> session;uint64_t revision=0;
    openbmh::audio::Playback playback;std::optional<uint64_t> audio_session;
    const auto serve=[&](const FlowResult& result) {
        return app::serve_flow_request(flow,result,session,data,data,0x1789abcd,revision,playback,audio_session,false);
    };
    check(serve(flow.click(90,169)).empty()&&session,"strengths starts a genuine career");
    const auto w=session->rand();const auto g=session->game();
    const auto rng_neutral=[&] {check(session->rand().seed()==w.seed()&&session->rand().calls()==w.calls()&&
        session->game().seed()==g.seed()&&session->game().calls()==g.calls(),"strengths never draws either RNG");};
    const int manager=session->state().memory.u8(::bmh::layout::ActiveManager);
    const uint32_t settings=::bmh::layout::ManagerSettings+uint32_t(manager)*2,family=::bmh::layout::LeagueTableDisplayModes+uint32_t(manager)*2;
    // Source entry uses bit10 before it is cleared by render normalization.
    session->state().memory.set16(settings,0x0400);session->state().memory.set16(family,0xaabe);
    const auto open=session->strengths_open_request(revision);
    check(bool(open),"semantic strengths fixture has an active manager");
    const int team_info_fixture_team=league_first_team(open->identity.league);
    const auto injured_range=squad_range(team_info_fixture_team);
    const int injured_player=session->state().squad_slots.at(injured_range.first)&0x0fff;
    check(injured_player!=kEmptySlot,"nested TeamInfo fixture has a first-row player");
    session->state().squad_slots.at(injured_range.first)|=kSlotInLineup;
    session->state().players.at(size_t(injured_player)).w364|=1u<<16;
    const GameState baseline=session->state();
    check(bool(open)&&!flow.in_strengths(),"semantic strengths entry has no UI/MEN owner requirement");
    const auto neutral=[&](StrengthsRequest request) {
        const auto native=save_stream(session->state());const auto rev=revision;std::string error;
        check(!session->dispatch_strengths(request,revision,error)&&!error.empty()&&revision==rev&&
            save_stream(session->state())==native,"forged strengths identity rejects with whole Native/revision neutral");rng_neutral();
    };
    auto wrong=*open;++wrong.identity.manager;neutral(wrong);
    wrong=*open;++wrong.identity.runtime_club;neutral(wrong);
    wrong=*open;++wrong.identity.club_identity;neutral(wrong);
    wrong=*open;++wrong.identity.day_counter;neutral(wrong);
    wrong=*open;++wrong.identity.revision;neutral(wrong);
    wrong=*open;++wrong.identity.expected_settings_word;neutral(wrong);
    wrong=*open;++wrong.identity.expected_family_word;neutral(wrong);
    std::string error;const auto before_revision=revision;
    auto page=session->dispatch_strengths(*open,revision,error);
    check(page&&revision==before_revision+1&&page->page.identity.sequence==0&&page->page.identity.scope,
        "semantic opening commits exactly one ownership revision and establishes private scope");
    auto expected=baseline;expected.memory.set16(settings,0x01ff);
    expected.memory.set16(family,uint16_t(0xa97e|(exe.u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField+uint32_t(open->identity.league))?0x80:0)));
    check(save_stream(session->state())==save_stream(expected),"opening changes exactly format field and b602c words");
    check(page->page.projection.words.raw_settings_word==0x0400&&page->page.projection.age_scratch.lineup_only&&
        page->page.projection.age_scratch.basis==StrengthsAgeBasis::entry_or_league_change_bit10,
        "Source opening keeps bit10-derived age despite persisted bit10 clearing");rng_neutral();neutral(*open);
    auto id=page->page.identity;
    // A valid public Bank transaction used to bypass the Strengths owner. It
    // must refuse before touching cash/deposit, and become usable after close.
    BankRequest deposit;deposit.kind=BankRequestKind::move_to_deposit;
    deposit.manager_slot=manager;deposit.runtime_club=id.runtime_club;
    deposit.revision=revision;deposit.amount=1;
    const auto owned_native=save_stream(session->state());const auto owned_revision=revision;
    check(!session->dispatch_bank(deposit,revision,error)&&error=="strengths_active"&&
        revision==owned_revision&&save_stream(session->state())==owned_native,
        "valid direct Bank request cannot invalidate active Strengths owner");rng_neutral();
    bool facade_refused=false;
    try {(void)session->live_club_modal_services(revision);}catch(const FormatError&){facade_refused=true;}
    check(facade_refused&&save_stream(session->state())==owned_native,
        "new live mutable modal capability is unavailable during Strengths ownership");
    // Mutable state() is a trusted Engine/fixture escape hatch. Its unexpected
    // Native mutation must be detected before the next owned view transition.
    const size_t cash_byte=size_t(manager)*0xb0c+0x314;
    const auto cash=session->state().manager_records.at(cash_byte);
    session->state().manager_records.at(cash_byte)^=1;
    neutral({id,StrengthsAction::control});
    session->state().manager_records.at(cash_byte)=cash;
    wrong={id,StrengthsAction::control};++wrong.identity.scope;neutral(wrong);
    wrong={id,StrengthsAction::control};++wrong.identity.sequence;neutral(wrong);
    wrong={id,StrengthsAction::control,static_cast<StrengthsControl>(255),-1};neutral(wrong);
    const auto action=[&](StrengthsAction kind,StrengthsControl control=StrengthsControl::condition,int value=-1) {
        const auto old_revision=revision;
        auto result=session->dispatch_strengths({id,kind,control,value},revision,error);
        check(result&&revision==old_revision+1,"owned semantic strengths action commits exactly once");
        id=result->page.identity;rng_neutral();return *result;
    };
    const auto age=page->page.projection.age_scratch.tenths;
    const auto filter=action(StrengthsAction::control);
    expected.memory.set16(settings,0x00fe);
    check(save_stream(session->state())==save_stream(expected)&&filter.page.projection.age_scratch.tenths==age&&
        filter.page.projection.age_scratch.lineup_only,"filter retains owned ages and only writes Source settings");
    const auto lineup=action(StrengthsAction::control,StrengthsControl::lineup);
    expected.memory.set16(settings,0x80fe);
    check(save_stream(session->state())==save_stream(expected)&&lineup.page.projection.words.raw_settings_word&0x8000&&
        lineup.page.projection.age_scratch.basis==StrengthsAgeBasis::header_toggle_bit15,
        "header toggles saved bit15 and refreshes age with its own Source basis");
    const auto header_frame=StrengthsScreen(data,lineup.page).render().pixels;
    const auto reset=action(StrengthsAction::control,StrengthsControl::reset);
    expected.memory.set16(settings,0x01ff);
    check(save_stream(session->state())==save_stream(expected)&&reset.page.projection.age_scratch.lineup_only&&
        reset.page.projection.age_scratch.basis==StrengthsAgeBasis::header_toggle_bit15&&
        StrengthsScreen(data,reset.page).render().pixels!=header_frame,
        "reset removes bit15 yet retains header ages and changes Original header/button drawing");
    action(StrengthsAction::control,StrengthsControl::age);
    expected.memory.set16(settings,0x0200);
    check(save_stream(session->state())==save_stream(expected),"age sort Source transition clears only the specified word bits");
    neutral({id,StrengthsAction::control,StrengthsControl::condition,-1});
    action(StrengthsAction::control,StrengthsControl::age);expected.memory.set16(settings,0x01ff);
    const int first_navigation=id.league;
    for(int n=0;n<9;++n) {
        const int league=(first_navigation+1+n)%9;
        page=action(StrengthsAction::select_league,StrengthsControl::condition,league);
        check(page&&page->page.projection.rows.size()==size_t(kLeagueSizes[league])&&
            page->page.projection.age_scratch.basis==StrengthsAgeBasis::entry_or_league_change_bit10&&
            !page->page.projection.age_scratch.lineup_only,"all nine ordinary leagues refresh age from current bit10");
        if(league>=5)check(page->page.projection.league_name==original_to_utf8(session->state().club_name(0x165+league-4)),
            "foreign ordinary leagues use actual country names without domestic suffix-table lookup");
        for(const auto& row:page->page.projection.rows)check(row.runtime_team>=league_first_team(league)&&
            row.runtime_team<league_first_team(league)+kLeagueSizes[league],"sorted rows retain authoritative runtime club identity");
    }
    neutral({id,StrengthsAction::team_info,StrengthsControl::condition,0});
    neutral({id,StrengthsAction::pairings});neutral({id,StrengthsAction::table});
    const auto lineup_for_team=action(StrengthsAction::control,StrengthsControl::lineup);
    check(lineup_for_team.page.projection.age_scratch.lineup_only,
        "TeamInfo fixture reaches the Source lineup-only age-cache mode");
    const int team=team_info_fixture_team;
    const auto visible=std::find_if(page->page.projection.rows.begin(),page->page.projection.rows.end(),
        [&](const auto& row){return row.runtime_team==team;});
    check(visible!=page->page.projection.rows.end(),"TeamInfo fixture targets a visible runtime-team row");
    const auto team_action=StrengthsRequest{id,StrengthsAction::team_info,StrengthsControl::condition,team};
    const auto team_native=save_stream(session->state());const auto team_rev=revision;
    bool presenter_called=false;
    const app::TransferOfferTeamInfoPresenter stopped=[&](app::ClubModalServices&,int target,const Image& underlay) {
        presenter_called=true;check(target==team&&underlay.width==320&&underlay.height==240,
            "Strengths TeamInfo receives selected runtime club and exact parent frame");return false;
    };
    check(!session->dispatch_strengths(team_action,revision,error,stopped)&&presenter_called&&
        revision==team_rev&&save_stream(session->state())==team_native&&session->strengths_owner()->page.identity==id,
        "stopped nested TeamInfo rolls back staged state and retains the exact Strengths owner");rng_neutral();
    bool nested_rejected=false;
    const app::TransferOfferTeamInfoPresenter reentrant=[&](app::ClubModalServices&,int,const Image&) {
        std::string nested_error;auto nested=session->dispatch_strengths({id,StrengthsAction::close},revision,nested_error);
        nested_rejected=!nested&&!nested_error.empty();return true;
    };
    auto team_return=session->dispatch_strengths(team_action,revision,error,reentrant);
    check(team_return&&nested_rejected&&revision==team_rev+1&&save_stream(session->state())==team_native,
        "reentrant nested dispatch is refused while the valid outer TeamInfo transaction succeeds");
    id=team_return->page.identity;page=team_return;rng_neutral();
    const app::TransferOfferTeamInfoPresenter accepted=[&](app::ClubModalServices& services,int target,const Image&) {
        check(target==team,"accepted TeamInfo keeps the selected runtime-team identity");
        const auto entry=services.enter_club(target);
        check(!entry.displayed_slots.empty(),"TeamInfo fixture executes the original club-entry cleanup/projection");
        return true;
    };
    const auto source_effect_request=StrengthsRequest{id,StrengthsAction::team_info,StrengthsControl::condition,team};
    const auto source_effect_before=save_stream(session->state());const auto source_effect_revision=revision;
    team_return=session->dispatch_strengths(source_effect_request,revision,error,accepted);
    GameState expected_team_info=session->state();
    const auto expected_team_entry=prepare_club_info_entry(expected_team_info,team);
    check(!expected_team_entry.displayed_slots.empty(),"TeamInfo fixture executes known Original injury cleanup");
    const bool returned_team_visible=team_return&&std::any_of(team_return->page.projection.rows.begin(),
        team_return->page.projection.rows.end(),[&](const auto& row){return row.runtime_team==team;});
    check(team_return&&revision==source_effect_revision+1&&team_return->page.identity.manager==id.manager&&
        team_return->page.identity.runtime_club==id.runtime_club&&team_return->page.identity.league==id.league&&
        team_return->page.identity.scope==id.scope&&team_return->page.identity.sequence==id.sequence+1&&
        returned_team_visible&&
        team_return->page.projection.age_scratch==lineup_for_team.page.projection.age_scratch,
        "TeamInfo returns through a fresh projection to the same Strengths league and manager");
    check(save_stream(session->state())==save_stream(expected_team_info)&&
        session->state().squad_slots.at(injured_range.first)!=uint16_t(baseline.squad_slots.at(injured_range.first)|kSlotInLineup),
        "successful nested TeamInfo commits the real injury cleanup and retains source age cache");
    id=team_return->page.identity;page=team_return;rng_neutral();
    const auto close_native=save_stream(session->state());action(StrengthsAction::close);
    check(!session->strengths_owner()&&save_stream(session->state())==close_native,
        "semantic close abandons scope without Source normalization/family writes");
    neutral({id,StrengthsAction::close});
    deposit.revision=revision;auto deposit_expected=session->state();
    move_to_deposit(deposit_expected,manager,deposit.amount);
    const auto deposit_revision=revision;
    check(session->dispatch_bank(deposit,revision,error)&&revision==deposit_revision+1&&
        save_stream(session->state())==save_stream(deposit_expected),
        "same genuine direct Bank request resumes after accepted Strengths close");rng_neutral();

    // Restore the real desktop at the advanced ownership revision, then exercise
    // actual controller pending equality and physical down/held-release routes.
    flow=StartupFlow(data);session.reset();revision=0;audio_session.reset();
    flow.click(150,215);flow.click(5,225,true);flow.click(5,225,true);
    check(serve(flow.click(90,169)).empty()&&session,"physical strengths fixture starts a separate genuine career");
    const auto item=std::find_if(flow.desktop()->state().items.begin(),flow.desktop()->state().items.end(),
        [](const DeskItem& item){return item.kind==2&&!item.disabled;});
    check(item!=flow.desktop()->state().items.end(),"real desktop has enabled Strengths menu2");
    const int x=item->x+item->width/2,y=item->y+item->height/2;
    const auto opened=flow.click(x,y);auto forged=opened;++forged.desk_action.item_id;
    const auto native_before=save_stream(session->state());const auto rev_before=revision;
    check(serve(forged).empty()&&!flow.in_strengths()&&flow.desktop()->pending()&&
        save_stream(session->state())==native_before&&revision==rev_before,"forged menu2 result cannot consume real UI owner");
    check(serve(opened).empty()&&flow.in_strengths()&&revision==rev_before+1,"production controller opens acknowledged Strengths screen");
    check(serve(opened).empty()&&revision==rev_before+1,"opening replay cannot allocate another scope");
    const auto down=flow.pointer_down(205,218);
    check(down.changed&&down.request==FlowRequest::none&&flow.strengths()->state().identity.revision==revision,
        "Source button down presents held state without Native/revision commit");
    const auto held=flow.render().pixels;
    check(flow.move(319,239)&&flow.click(205,218).request==FlowRequest::none&&flow.render().pixels!=held,
        "leaving held button cancels its release action");
    flow.pointer_down(5,178);const auto request=flow.hold(5,178,2200);
    check(request.request==FlowRequest::strengths_action&&flow.pending_strengths(),"long hold follows same single release route");
    forged=request;++forged.strengths_action.identity.scope;
    const auto pending_native=save_stream(session->state());const auto pending_revision=revision;
    check(serve(forged).empty()&&flow.pending_strengths()&&revision==pending_revision&&
        save_stream(session->state())==pending_native,"forged external action cannot dispatch another UI pending request");
    auto bad_ack=flow.strengths()->state();++bad_ack.identity.revision;++bad_ack.identity.sequence;++bad_ack.identity.scope;
    bool thrown=false;try {flow.acknowledge_strengths(bad_ack,session->preparation_projection(revision+1));}
    catch(const FormatError&) {thrown=true;}
    check(thrown&&flow.pending_strengths()&&revision==pending_revision,"wrong ACK scope leaves pending UI and live session untouched");
    check(serve(request).empty()&&revision==pending_revision+1&&!flow.pending_strengths(),"owned release receives strict ACK exactly once");
    const auto accepted_native=save_stream(session->state());
    const auto close_revision=revision;auto close_game=session->game();auto close_rand=session->rand();
    stir_random(close_game,close_rand);
    check(serve(request).empty()&&revision==pending_revision+1&&save_stream(session->state())==accepted_native,
        "accepted physical request replay cannot write twice");
    const auto close=flow.pointer_down(0,0,true);
    check(close.request==FlowRequest::league_family_action&&
        close.league_family_action.kind==LeagueFamilyRequestKind::close&&serve(close).empty()&&!flow.in_strengths()&&
        flow.screen_name()=="desktop","rightDOWN family close returns to desktop");
    check(revision==close_revision+1,"family close commits one revision");
    check(save_stream(session->state())==accepted_native,"family close keeps Native bytes");
    check(session->game().seed()==close_game.seed()&&session->game().calls()==close_game.calls()&&
        session->rand().seed()==close_rand.seed()&&session->rand().calls()==close_rand.calls(),
        "family close matches exactly one Source stir");
    check(flow.click(0,0,true).request==FlowRequest::none,"orphan rightUP does not reopen an action");

    const auto save_dir=std::filesystem::temp_directory_path()/"openbmh-strengths-save-app-test";
    std::filesystem::create_directories(save_dir);const auto save=save_dir/"STRENGTH.FBM";
    std::filesystem::remove(save);
    check(session->save_game(save,manager,false,error),"changed Source criteria save through actual CareerSession");
    app::CareerSession reload(data,0x1789abcd,save);
    check(reload.state().memory.u16(settings)==session->state().memory.u16(settings)&&
        reload.state().memory.u16(family)==session->state().memory.u16(family)&&
        reload.rand().calls()==0&&reload.game().calls()==0,"fresh actual Native load retains both settings without W/G");
    std::filesystem::remove_all(save_dir);
}

void check_strengths_controller_ack_rollback(const std::filesystem::path& data) {
    const auto temp=std::filesystem::temp_directory_path()/"openbmh-strengths-ack-assets";
    std::filesystem::remove_all(temp);std::filesystem::create_directories(temp/"KB");
    for(const auto& entry:std::filesystem::directory_iterator(data)) {
        if(entry.path().filename()=="KB")continue;
        if(entry.is_regular_file())
            std::filesystem::copy_file(entry.path(),temp/entry.path().filename());
    }
    // Regular copies keep the ACK fault isolated without Windows symlink privileges.
    // Startup, desktop and eagerly projected submenus also need fonts and badges.
    for(const char* directory : {"KB","F","W"}) {
        std::filesystem::create_directories(temp/directory);
        for(const auto& entry:std::filesystem::directory_iterator(data/directory)) {
            if(!entry.is_regular_file())continue;
            std::filesystem::copy_file(entry.path(),temp/directory/entry.path().filename());
        }
    }
    StartupFlow flow(temp);flow.click(150,215);flow.click(5,225,true);flow.click(5,225,true);
    std::unique_ptr<app::CareerSession> session;uint64_t revision=0;
    openbmh::audio::Playback playback;std::optional<uint64_t> audio_session;
    const auto serve=[&](const FlowResult& result) {return app::serve_flow_request(flow,result,session,data,data,
        0x1789abcd,revision,playback,audio_session,false);};
    check(serve(flow.click(90,169)).empty()&&session,"ACK rollback fixture uses genuine career and separate UI asset path");
    const int manager=session->state().memory.u8(::bmh::layout::ActiveManager);session->state().memory.set16(::bmh::layout::ManagerSettings+uint32_t(manager)*2,0x0400);
    const auto before=session->strengths_checkpoint();const auto native=save_stream(session->state());const auto rev=revision;
    const auto item=std::find_if(flow.desktop()->state().items.begin(),flow.desktop()->state().items.end(),
        [](const DeskItem& item){return item.kind==2&&!item.disabled;});
    check(item!=flow.desktop()->state().items.end(),"ACK fixture has genuine menu2");
    const auto request=flow.click(item->x+item->width/2,item->y+item->height/2);
    // Session projection validates authentic assets; only the separate Flow ACK
    // renderer fails after the semantic transaction has accepted its settings.
    const auto original=read_file(temp/"KB"/"56.VGA");
    const auto write_asset=[&](const Bytes& bytes) {
        std::ofstream out(temp/"KB"/"56.VGA",std::ios::binary|std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
        check(bool(out),"bounded test asset write succeeds");
    };
    write_asset(Bytes{});
    check(!serve(request).empty()&&!flow.in_strengths()&&!session->strengths_owner()&&revision==rev&&
        save_stream(session->state())==native&&session->rand().seed()==before.rand.seed()&&
        session->rand().calls()==before.rand.calls()&&session->game().seed()==before.game.seed()&&
        session->game().calls()==before.game.calls()&&session->strengths_checkpoint().next_scope==before.next_scope&&
        session->club_modal_runtime_checkpoint()==before.runtime,
        "postcommit UI ACK renderer exception rolls back settings, scope/cache/revision/runtime and both RNGs");
    write_asset(original);
    const auto retry=flow.click(item->x+item->width/2,item->y+item->height/2);
    check(serve(retry).empty()&&flow.in_strengths()&&flow.strengths()->state().identity.scope==before.next_scope,
        "retry after ACK failure receives the original unconsumed scope");
    check(serve(flow.pointer_down(0,0,true)).empty(),"rollback fixture closes owned scope");
    std::filesystem::remove_all(temp);
}

void check_indoor_standings_controller(const std::filesystem::path& data) {
    StartupFlow flow(data);
    flow.click(150, 215); flow.click(5, 225, true); flow.click(5, 225, true);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    const auto serve = [&](const FlowResult& result) {
        check(app::serve_flow_request(flow, result, session, data, data, 0x12345678,
            revision, playback, audio_session, false).empty(), "indoor standings use the production controller");
    };
    serve(flow.click(90, 169));
    check(session && flow.screen_name() == "desktop", "indoor standings start at a real career desk");
    auto& game = session->state();
    game.memory.set8(::bmh::layout::Cup_AdvanceCup_RuntimeField4, 1);
    for (int team = 0; team < 40; ++team) game.memory.set16(::bmh::layout::Cup_AdvanceCup_ExecutableField + uint32_t(team * 2), uint16_t(40 - team));
    game.memory.set16(::bmh::layout::Cup_AdvanceCup_RuntimeField6, 0); game.memory.set16(::bmh::layout::Season_CareerSeasonSetup_RuntimeField7, 0); game.memory.set16(::bmh::layout::Cup_AdvanceCup_RuntimeField7, 1);
    const auto native = save_stream(game);
    const auto w = session->rand(); const auto g = session->game(); const auto rev = revision;
    const auto menu = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
        [](const DeskItem& item) { return item.kind == 9 && !item.disabled; });
    check(menu != flow.desktop()->state().items.end(), "ordinary desk exposes its Hallenwertung item");
    const int x = menu->x + menu->width / 2, y = menu->y + menu->height / 2;
    const auto open = flow.click(x, y);
    check(open.request == FlowRequest::desk_action && open.desk_action.menu_kind == 9,
          "indoor table retains its exact pending desk owner");
    serve(open);
    check(flow.screen_name() == "indoor_standings" && flow.indoor_standings() && !flow.desktop()->pending(),
          "menu 9 opens the actual Source ranking rather than the unavailable-menu fallback");
    const auto& rows = flow.indoor_standings()->state().rows;
    check(rows.size() == 16 && rows[0].team == 0 && rows[0].points == 40 && rows[15].team == 15,
          "Source ranks the forty saved Masters totals and presents the top sixteen");
    check(rows[7].rank_color == 0x51 && rows[8].rank_color == 0xf7,
          "duplicate title holders do not consume an extra qualification place");
    (void)flow.render();
    check(!flow.send_key("ESC").changed && flow.screen_name() == "indoor_standings",
          "Source mouse wait does not invent an ESC binding");
    serve(open); // Replayed owner has already been consumed.
    check(flow.screen_name() == "indoor_standings", "replayed navigation cannot reopen the owned table");
    check(flow.pointer_down(x, y, true).request == FlowRequest::submenu_closed && flow.screen_name() == "desktop",
          "Original mouse DOWN returns to the retained desk");
    check(!flow.click(x, y, true).changed && !flow.desktop()->pending(),
          "the matching mouse UP is consumed and cannot act on the underlying desk");
    check(save_stream(game) == native && revision == rev && session->rand().seed() == w.seed() &&
          session->rand().calls() == w.calls() && session->game().seed() == g.seed() &&
          session->game().calls() == g.calls(), "ranking/render/close preserve Native, revision and both RNGs");
    game.memory.set8(::bmh::layout::Cup_AdvanceCup_RuntimeField4, 0);
    game.indoor = true;
    const auto empty = indoor_standings_from_game(game, load_le(asset_path(data, "BMH.exe")),
        load_textd(asset_path(data, "TEXTD.BLK")), open.desk_action);
    check(empty.rows.empty() && empty.footer.empty() && !empty.strength_text.empty() && !empty.prize_text[0].empty(),
          "before the first tournament no ranking is fabricated; indoor prize information remains available");
}

void check_calendar_readonly_controller(const std::filesystem::path& data) {
    StartupFlow flow(data);
    flow.click(150,215); flow.click(5,225,true); flow.click(5,225,true);
    const auto career = flow.click(90,169);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    const auto serve = [&](const FlowResult& result) {
        check(app::serve_flow_request(flow,result,session,data,data,0x12345678,
            revision,playback,audio_session,false).empty(), "calendar uses the production App controller");
    };
    serve(career);
    check(session && flow.screen_name() == "desktop", "calendar fixture begins on the actual career desk");
    auto& game = session->state();
    const auto native = save_stream(game);
    const auto rev = revision;
    const auto w = session->rand(); const auto g = session->game();
    const auto unchanged = [&] {
        check(save_stream(game) == native && revision == rev &&
            session->rand().seed() == w.seed() && session->rand().calls() == w.calls() &&
            session->game().seed() == g.seed() && session->game().calls() == g.calls(),
            "calendar open/hover/select/redraw/close/replay preserve full Native, revision and both RNGs");
    };
    const auto calendar_item = std::find_if(flow.desktop()->state().items.begin(),flow.desktop()->state().items.end(),
        [](const DeskItem& item) { return item.kind == 8 && !item.disabled; });
    check(calendar_item != flow.desktop()->state().items.end(), "actual desktop has an enabled Original calendar item");
    const int x = calendar_item->x + calendar_item->width / 2;
    const int y = calendar_item->y + calendar_item->height / 2;
    const auto open = [&] { return flow.click(x,y); };
    const auto request = open();
    check(request.request == FlowRequest::desk_action && request.desk_action.menu_kind == 8 &&
        flow.desktop()->pending() && !flow.calendar(), "menu8 awaits actual controller acknowledgement");
    auto forged = request; ++forged.desk_action.item_id;
    serve(forged);
    check(!flow.calendar() && !flow.desktop()->pending(), "forged calendar request does not consume a different menu owner");
    unchanged();
    serve(request);
    check(!flow.calendar(), "replayed rejected calendar navigation has no pending owner");
    unchanged();
    auto stale = open(); ++stale.desk_action.revision;
    serve(stale);
    check(!flow.calendar(), "stale calendar revision rejects before projection"); unchanged();
    auto wrong_manager = open(); ++wrong_manager.desk_action.manager_slot;
    serve(wrong_manager);
    check(!flow.calendar(), "calendar navigation binds the exact active manager"); unchanged();

    // Keep the pending UI request genuine but alter only the live stable identity.
    const auto identity_request = open();
    const int club = flow.desktop()->state().runtime_club;
    const auto identity = game.club_ids.at(size_t(club));
    game.club_ids.at(size_t(club)) = uint16_t(identity + 1);
    const auto stale_identity_native = save_stream(game);
    serve(identity_request);
    check(!flow.calendar() && save_stream(game) == stale_identity_native && revision == rev &&
        session->rand().calls() == w.calls() && session->game().calls() == g.calls(),
        "a pending calendar request cannot cross live stable-club identity replacement");
    game.club_ids.at(size_t(club)) = identity;
    unchanged();
    const auto opening = open();
    auto desk_pixels = flow.render().pixels;
    serve(opening);
    check(flow.calendar() && flow.screen_name() == "calendar" && flow.in_readonly_view() &&
        !flow.desktop()->pending(), "menu8 opens a real calendar instead of the parent unsupported route");
    unchanged();
    const auto state = flow.calendar()->state();
    check((state.days.size() == 365 || state.days.size() == 366) &&
        state.days.front().date.month == 7 && state.days.front().date.day == 1 &&
        state.days.back().date.month == 6 && state.days.back().date.day == 30,
        "calendar screen owns the complete projected July-June year");
    const auto first = calendar_cell(state.days.front());
    check(first.x == 28 && first.y == 29 + state.days.front().date.weekday * 8 &&
        first.width == 11 && first.height == 8,
        "Source July1 hitbox uses vertical weekdays and eleven by eight pixels");
    const auto current = calendar_cell(state.days[size_t(state.current_day)]);
    check(flow.move(current.x + 2,current.y + 2) && flow.calendar()->selected_day() == state.current_day &&
        flow.render().pixels.size() == 320 * 240 && !flow.move(current.x + 2,current.y + 2),
        "physical hover selects current day locally and duplicate motion is inert");
    check(!flow.text("ignored") && !flow.send_key("RIGHT").changed,
        "calendar owns text and arrow input without advancing the career");
    check(flow.move(0,0) && flow.calendar()->selected_day() == -1,
        "leaving Original day cells restores the unhighlighted calendar");
    const auto free = std::find_if(state.days.begin(),state.days.end(),[](const CalendarDayProjection& day) {
        return day.competitions == 0;
    });
    check(free != state.days.end(), "genuine calendar contains a free day");
    const auto free_cell = calendar_cell(*free);
    serve(flow.click(free_cell.x + 2,free_cell.y + 2));
    check(flow.calendar() && flow.calendar()->selected_day() == free->season_day,
        "a free day click remains read-only and does not book or advance");
    const auto redraw = flow.render().pixels;
    check(flow.render().pixels == redraw, "calendar redraw is deterministic");
    unchanged();
    serve(opening);
    check(flow.calendar(), "consumed open replay cannot replace a live calendar"); unchanged();
    serve(flow.click(0,0,true));
    check(!flow.calendar() && flow.screen_name() == "desktop" && flow.render().pixels == desk_pixels,
        "right event8 returns to the exact parent desk without an engine revision");
    unchanged();
    // Source local30 selects only the manager's domestic class (plus bit14).
    int active_class = flow.desktop()->state().league;
    if (active_class > 2) --active_class;
    serve(open());
    const auto own_state = flow.calendar()->state();
    const auto own = std::find_if(own_state.days.begin(),own_state.days.end(),[&](const CalendarDayProjection& day) {
        return active_class >= 0 && active_class < 4 && (day.competitions & (uint32_t(1) << active_class));
    });
    check(own != own_state.days.end(), "genuine calendar contains an active domestic league day");
    const auto own_cell = calendar_cell(*own);
    flow.move(own_cell.x + 2,own_cell.y + 2);
    const auto close = flow.click(own_cell.x + 2,own_cell.y + 2);
    check(close.request == FlowRequest::submenu_closed && !flow.calendar(),
        "Original own-league left event2 closes only the view, without kickoff or rescheduling");
    serve(close); unchanged();
    serve(open()); serve(flow.send_key("ESC"));
    check(!flow.calendar() && flow.screen_name() == "desktop", "calendar escape returns to the same desk");
    unchanged();

    const auto move_menu = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
        [](const DeskItem& item) { return item.kind == 7 && !item.disabled; });
    check(move_menu != flow.desktop()->state().items.end(), "the real desk exposes enabled Original menu 7");
    serve(flow.click(move_menu->x + move_menu->width / 2, move_menu->y + move_menu->height / 2));
    check(flow.screen_name() == "reschedule_calendar" && flow.calendar() &&
        !flow.calendar()->state().reschedule_options.empty(),
        "menu 7 opens its Source mode-2 calendar with actual reschedulable home fixtures");
    const auto option = flow.calendar()->state().reschedule_options.front();
    check(!option.target_days.empty(), "selected Original home fixture has at least one legal target");
    const auto source_cell = calendar_cell(flow.calendar()->state().days[size_t(option.match.day)]);
    const auto select = flow.pointer_down(source_cell.x + 2, source_cell.y + 2);
    check(select.request == FlowRequest::none && flow.calendar()->state().reschedule_options.size() > 0,
        "first date click selects a fixture without changing Native state");
    check(flow.click(source_cell.x + 2, source_cell.y + 2).request == FlowRequest::none,
        "matching physical release is consumed after Source date selection");
    const int target_day = option.target_days.front();
    const auto target_cell = calendar_cell(flow.calendar()->state().days[size_t(target_day)]);
    const auto move = flow.pointer_down(target_cell.x + 2, target_cell.y + 2);
    check(move.request == FlowRequest::reschedule_action && move.reschedule_action.kind == RescheduleActionKind::move,
        "second date click emits a typed move request for the selected Source fixture");
    const size_t result_at = size_t(option.match.match) + size_t(option.match.round) * 10 + size_t(option.match.league) * 0x17c;
    const auto before_move_revision = revision;
    serve(move);
    check(flow.click(target_cell.x + 2, target_cell.y + 2).request == FlowRequest::none,
        "matching release cannot act a second time after the move acknowledgement");
    check(revision == before_move_revision + 1 && flow.calendar()->state().moved_matches.size() == 1 &&
        game.results.at(result_at) == 0xce,
        "controller applies the existing Engine reschedule transaction and acknowledges the moved fixture");
    const auto target_again = calendar_cell(flow.calendar()->state().days[size_t(target_day)]);
    const auto undo = flow.pointer_down(target_again.x + 2, target_again.y + 2);
    check(undo.request == FlowRequest::reschedule_action && undo.reschedule_action.kind == RescheduleActionKind::undo,
        "clicking a moved match date emits the Source take-back action");
    serve(undo);
    check(flow.click(target_again.x + 2, target_again.y + 2).request == FlowRequest::none,
        "matching release after take-back is consumed");
    check(revision == before_move_revision + 2 && game.results.at(result_at) == 0xcc &&
        flow.calendar()->state().moved_matches.empty(),
        "take-back restores the original unplayed fixture and removes its postponed entry");
    serve(flow.pointer_down(0, 0, true));
    check(flow.click(0, 0, true).request == FlowRequest::none, "menu 7 close release is consumed");
    check(!flow.calendar() && flow.screen_name() == "desktop",
        "right event8 closes menu 7 and returns to the refreshed desk");
}

void check_readonly_views(const std::filesystem::path& data) {
    StartupFlow flow(data);
    flow.click(150, 215); flow.click(5, 225, true); flow.click(5, 225, true);
    const auto career = flow.click(90, 169);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    const auto serve = [&](const FlowResult& result) {
        const auto error=app::serve_flow_request(flow, result, session, data, data, 0x12345678,
            revision, playback, audio_session, false);
        if(!error.empty())throw std::runtime_error("read-only navigation uses the production App controller: "+error);
    };
    serve(career);
    check(session && flow.screen_name() == "desktop", "read-only fixture reaches an actual career desk");
    auto& game = session->state();
    // Use a valid saved option which always displays the Original compact drawer bar.
    game.memory.set8(::bmh::layout::DeskState_FormatError_RuntimeField3, 0);
    for (int i = 0; i < 4; ++i) post_composed(game, 0, "Neue Nachricht " + std::to_string(i));
    const auto current = flow.desktop()->state();
    // A real acknowledged widget change refreshes the projected saved option and mailbox shortcut.
    const auto& items = current.items;
    const auto price = std::find_if(items.begin(), items.end(), [](const DeskItem& i) { return i.kind == 55 && i.open; });
    check(price != items.end(), "fixture has the Original open admission drawer");
    serve(flow.click(price->x + 4 + 35, price->y + 10));
    check(flow.desktop()->state().mailbox_shortcut &&
          flow.desktop()->state().mailbox_shortcut->id == 44 && flow.desktop()->state().mailbox_shortcut->kind == 63,
          "Original mailbox shortcut projects layout item 44 as drawer kind 63");
    auto saved = save_stream(game);
    auto date = game.date;
    auto rev = revision;
    auto gs = session->game().seed();
    auto ws = session->rand().seed();
    auto gc = session->game().calls(), wc = session->rand().calls();
    auto desk_pixels = flow.render().pixels;
    const auto unchanged = [&] {
        check(save_stream(game) == saved && revision == rev && game.date == date &&
              session->game().seed() == gs && session->rand().seed() == ws &&
              session->game().calls() == gc && session->rand().calls() == wc,
              "view opening, paging, rendering and closing preserve every saved byte, revision, date and both RNG streams");
    };
    const auto table = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
        [](const DeskItem& i) { return i.kind == 0 && !i.disabled; });
    check(table != flow.desktop()->state().items.end(), "table is enabled on the real desk");
    auto expected_table_entry=game;
    const int table_manager=flow.desktop()->state().manager_slot;
    const uint16_t table_family_before=game.memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(table_manager)*2);
    // legacy routine establishes the family bits; legacy routine clears the low mode bit
    // at its first draw. Runtime selector context is checked below separately.
    const uint16_t expected_table_word=uint16_t(((table_family_before&0xfc0f)|0x0170)&0xff7f);
    expected_table_entry.memory.set16(::bmh::layout::LeagueTableDisplayModes+uint32_t(table_manager)*2,expected_table_word);
    serve(flow.click(table->x + 10, table->y + 10));
    check(flow.screen_name() == "league_table" && flow.in_readonly_view() && !flow.desktop()->pending(),
          "table menu opens the Source control view through the engine ACK");
    std::string table_dispatch_error;
    const auto table_dispatch=session->current_league_table_dispatch(table_dispatch_error);
    check(save_stream(game)==save_stream(expected_table_entry)&&
          game.memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(table_manager)*2)==expected_table_word&&
          table_dispatch&&table_dispatch_error.empty()&&table_dispatch->page.source_controls_projection&&
          session->club_modal_runtime_checkpoint().at(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField)==0,
          "Table entry changes only the Source family word and binds the Table selector context");
    saved=save_stream(game);date=game.date;rev=revision;gs=session->game().seed();ws=session->rand().seed();
    gc=session->game().calls();wc=session->rand().calls();
    check(flow.render().pixels.size() == 320 * 240 && !flow.text("x") && !flow.move(15, 15),
          "read-only view renders and owns all input");
    serve(flow.send_key("LEFT")); serve(flow.send_key("RIGHT"));
    const auto behind = flow.click(290, 100);
    check(behind.request == FlowRequest::none && flow.screen_name() == "league_table",
          "modal table input cannot trigger the underlying kickoff icon");
    unchanged();
    serve(flow.pointer_down(5,225,true));
    check(flow.screen_name() == "desktop" && flow.render().pixels.size()==320*240,
          "Table right-click performs the Original family close and returns to a usable desk");
    check(revision==rev+1,"Table family close commits one final Source return revision");
    saved=save_stream(game);date=game.date;rev=revision;gs=session->game().seed();ws=session->rand().seed();
    gc=session->game().calls();wc=session->rand().calls();
    desk_pixels=flow.render().pixels;
    const auto mailbox = flow.click(183, 220);
    check(mailbox.request == FlowRequest::desk_action && mailbox.desk_action.item_id == 44 &&
          mailbox.desk_action.menu_kind == 63, "Original compact-bar message hitbox resolves item ID rather than menu kind 44");
    serve(mailbox);
    check(flow.screen_name() == "mailbox" && flow.in_readonly_view() && !flow.desktop()->pending(),
          "messages open through the actual App controller");
    check(flow.render().pixels.size() == 320 * 240 && flow.state_json().find("\"read_only\":false") != std::string::npos,
          "message view exposes its action scope");
    serve(flow.click(60, 205)); serve(flow.click(260, 205));
    serve(flow.click(85, 205)); // Selecting an action mode alone is local.
    serve(flow.click(110, 205)); // No article was selected.
    serve(flow.hold(290, 100, 2000));
    unchanged();
    serve(flow.click(0, 0, true));
    check(flow.screen_name() == "desktop" && flow.render().pixels == desk_pixels,
          "right-click closes the mailbox without changing or clicking the parent desk");
    unchanged();
    const auto settings = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
        [](const DeskItem& i) { return i.kind == 19 && !i.disabled; });
    check(settings != flow.desktop()->state().items.end(), "Original settings icon is enabled");
    const auto open_settings = flow.click(settings->x + 10, settings->y + 10);
    serve(open_settings);
    check(flow.screen_name() == "settings" && flow.in_readonly_view() && !flow.desktop()->pending(),
          "settings opens through production controller with owned raw settings");
    saved=save_stream(game);date=game.date;rev=revision;gs=session->game().seed();ws=session->rand().seed();
    gc=session->game().calls();wc=session->rand().calls();
    check(flow.render().pixels.size() == 320 * 240 && !flow.text("x") && !flow.move(70, 27),
          "settings view owns all input");
    serve(flow.click(64, 27));
    serve(flow.click(27, 206)); // Original button 1 opens the .EIN load picker.
    check(flow.screen_name() == "save_load" && flow.state_json().find("\"file_kind\":\"settings\"") != std::string::npos,
          "settings Load opens the existing picker with the Original .EIN filter");
    serve(flow.send_key("ESC"));
    check(flow.screen_name() == "settings", "cancelling the settings picker restores its parent settings page");
    serve(flow.click(27, 219)); // Original button 2 opens the .EIN save picker.
    check(flow.screen_name() == "save_load" && flow.state_json().find("\"mode\":\"save\"") != std::string::npos &&
          flow.state_json().find("\"file_kind\":\"settings\"") != std::string::npos,
          "settings Save opens the same picker in save mode with the .EIN filter");
    serve(flow.click(120, 214));
    check(flow.text("EIN-CHECK") && flow.state_json().find("EIN-CHECK") != std::string::npos,
          "retained settings parent permits typing into the active EIN filename field");
    serve(flow.send_key("ESC"));
    check(flow.screen_name() == "settings", "cancelling the save picker preserves the settings parent");
    serve(flow.hold(290, 100, 2000)); serve(flow.send_key("RIGHT"));
    unchanged();
    const auto settings_pixels = flow.render().pixels;
    const auto ignored_escape = flow.send_key("ESC");
    check(!ignored_escape.changed && ignored_escape.request == FlowRequest::none &&
          flow.screen_name() == "settings" && flow.render().pixels == settings_pixels,
          "Source settings ignores ESC and retains the exact settings view");
    unchanged();
    serve(flow.pointer_down(310, 230, true));
    check(flow.screen_name() == "desktop" && flow.render().pixels == desk_pixels,
          "Source right-button press returns settings to the exact parent pixels");
    saved=save_stream(game);date=game.date;rev=revision;gs=session->game().seed();ws=session->rand().seed();
    gc=session->game().calls();wc=session->rand().calls();
    serve(open_settings);
    check(flow.screen_name() == "desktop" && !flow.desktop()->pending(),
          "replayed consumed settings navigation is inert");
    unchanged();
    const auto ad = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
        [](const DeskItem& i) { return i.kind == 21 && !i.disabled; });
    check(ad != flow.desktop()->state().items.end(), "Original advertising icon is enabled");
    serve(flow.click(ad->x + 10, ad->y + 10));
    check(flow.screen_name() == "advertising" && flow.in_readonly_view() && !flow.desktop()->pending(),
          "advertising opens through production controller");
    check(flow.render().pixels.size() == 320 * 240 && !flow.text("x") && !flow.move(1, 1),
          "advertising owns modal input");
    serve(flow.click(15, 30)); serve(flow.hold(290, 100, 2000));
    unchanged();
    serve(flow.click(0, 0, true));
    check(flow.screen_name() == "advertising", "right-click cancels the nested spending calculator first");
    unchanged();
    serve(flow.click(0, 0, true));
    check(flow.screen_name() == "desktop" && flow.render().pixels == desk_pixels,
          "advertising right-click closes only its modal");
    unchanged();
}

void check_desk_records_controller(const std::filesystem::path& data) {
    const auto save_dir=std::filesystem::temp_directory_path()/
        ("openbmh-desk-records-controller-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(save_dir);
    StartupFlow flow(data,save_dir);
    flow.click(150,215);flow.click(5,225,true);flow.click(5,225,true);
    std::unique_ptr<app::CareerSession> session;uint64_t revision=0;
    openbmh::audio::Playback playback;std::optional<uint64_t> audio_session;
    const auto serve=[&](const FlowResult& result) {
        return app::serve_flow_request(flow,result,session,data,save_dir,0x44524543,revision,playback,audio_session,false);
    };
    check(serve(flow.click(90,169)).empty()&&session&&flow.screen_name()=="desktop",
          "menu 26 verification starts through a real App career/controller");
    auto& game=session->state();
    const auto menu26=[&]() {
        const auto found=std::find_if(flow.desktop()->state().items.begin(),flow.desktop()->state().items.end(),
            [](const DeskItem& item){return item.kind==26&&!item.disabled;});
        check(found!=flow.desktop()->state().items.end(),"Source menu 26 is enabled on the active desktop");
        return flow.click(found->x+found->width/2,found->y+found->height/2);
    };
    game.memory.set16(::bmh::layout::CareerSession_FormatError_RuntimeField7,0);
    check(serve(menu26()).empty()&&flow.screen_name()=="highscore"&&flow.desk_records()&&
          flow.desk_records()->is_alternate_calculator()&&std::filesystem::is_empty(save_dir),
          "zero signature dispatches menu 26 into the actual alternate score calculator");
    const auto calculator=flow.render().pixels;
    check(serve(flow.send_key("ENTER")).empty()&&flow.desk_records_points_active(),
          "accepted calculator enters the Source point presentation phase");
    unsigned frames=0;while(flow.advance_desk_records_point())++frames;
    check(frames==ui::GameEndAlternateScoreScreen::initial&&flow.finish_desk_records_points()&&
          flow.render().pixels!=calculator,
          "controller-owned calculator presents every point and retains the completed points image");
    check(serve(flow.pointer_down(1,1)).empty()&&flow.screen_name()=="desktop",
          "the points result waits for a distinct mouse-down and returns to its desk");
    serve(flow.click(1,1)); // Consume the paired UP release barrier.

    constexpr uint16_t signature=0x4938;
    game.memory.set16(::bmh::layout::CareerSession_FormatError_RuntimeField7,signature);
    const auto exe=load_le(asset_path(data,"BMH.exe"));
    const auto texts=load_textd(asset_path(data,"TEXTD.BLK"));
    const auto ranked_path=highscore_file(save_dir,signature,texts.strings.at(889));
    const auto owner_slot=flow.desktop()->state().manager_slot;
    check(owner_slot>=0&&size_t(owner_slot)<game.manager_teams.size()&&!game.manager_teams.empty(),
          "controller fixture has a valid active manager slot");
    const int foreign_slot=game.manager_teams.size()>1
        ? int((size_t(owner_slot)+1)%game.manager_teams.size())
        : (owner_slot==0?1:0);
    check(foreign_slot!=owner_slot,"controller fixture can select a distinct foreign manager slot");
    game.memory.set8(::bmh::layout::ActiveManager,uint8_t(foreign_slot));
    const auto stale_request=menu26();
    const auto rejected=serve(stale_request);
    check(!rejected.empty()&&!std::filesystem::exists(ranked_path)&&flow.screen_name()=="desktop",
          "wrong active-manager ownership is rejected before ranked highscore file persistence");
    game.memory.set8(::bmh::layout::ActiveManager,uint8_t(owner_slot));
    check(serve(menu26()).empty()&&flow.screen_name()=="highscore"&&flow.desk_records()&&
          !flow.desk_records()->is_alternate_calculator()&&std::filesystem::exists(ranked_path)&&
          std::filesystem::file_size(ranked_path)==360,
          "ranked highscore constructs its Original screen before writing the 360-byte table");
    std::filesystem::remove_all(save_dir);
}

void check_advertising_controller(const std::filesystem::path& data) {
    StartupFlow flow(data);
    flow.click(150, 215); flow.click(5, 225, true); flow.click(5, 225, true);
    const auto started = flow.click(90, 169);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    const auto serve = [&](const FlowResult& request) {
        check(app::serve_flow_request(flow, request, session, data, data, 0x1789abcd,
            revision, playback, audio_session, false).empty(), "advertising uses the production App controller");
    };
    serve(started);
    check(session && flow.screen_name() == "desktop", "advertising test initializes an actual career");
    auto& game = session->state();
    const int manager = int(game.memory.u8(::bmh::layout::ActiveManager));
    const size_t record = size_t(manager) * 0xb0c;
    std::fill_n(game.manager_records.begin() + std::ptrdiff_t(record + 0x8cc), 3 * 0x78, 0);
    std::fill_n(game.manager_records.begin() + std::ptrdiff_t(record + 0xa34), 16 * 12, 0);
    const size_t pool = record + 0x8cc + 3 * 12;
    const uint32_t amount = 10904;
    for (int i = 0; i < 4; ++i) game.manager_records[pool + size_t(i)] = uint8_t(amount >> (8 * i));
    game.manager_records[pool + 4] = 2;
    game.manager_records[pool + 5] = 24;
    game.manager_records[pool + 7] = 3;
    game.memory.set8(::bmh::layout::AdGroupId, 31);
    const auto original_bytes = save_stream(game);
    const auto original_watcom = session->rand();
    const auto original_game_rng = session->game();
    const auto original_revision = revision;
    const auto unchanged = [&] {
        check(save_stream(game) == original_bytes && revision == original_revision &&
            session->rand().seed() == original_watcom.seed() && session->rand().calls() == original_watcom.calls() &&
            session->game().seed() == original_game_rng.seed() && session->game().calls() == original_game_rng.calls(),
            "advertising navigation, cancel and rejected requests do not mutate state or either RNG");
    };
    serve(flow.send_key("W"));
    check(flow.screen_name() == "advertising", "Original W opens advertising through the controller");
    serve(flow.click(40, 30));
    (void)flow.render();
    unchanged();
    serve(flow.click(110, 195));
    check(!flow.pending_advertising_action() && flow.screen_name() == "advertising",
          "ABBRECHEN closes only the offer browser without an engine request");
    unchanged();
    serve(flow.click(40, 30));
    const auto accept = [&] {
        auto action = flow.click(190, 195);
        check(action.request == FlowRequest::advertising_action && flow.pending_advertising_action(),
              "Original ANNEHMEN emits one manager/revision-bound offer request");
        return action;
    };
    for (int kind = 0; kind < 6; ++kind) {
        auto action = accept();
        auto forged = action;
        auto& r = forged.advertising_action;
        if (kind == 0) ++r.manager_slot;
        if (kind == 1) ++r.runtime_club;
        if (kind == 2) ++r.revision;
        if (kind == 3) ++r.request_id;
        if (kind == 4) r.offer.expected_offer[11] ^= 1;
        if (kind == 5) r.offer.expected_contract[10] ^= 1;
        serve(forged);
        check(!flow.pending_advertising_action(), "forged advertising envelope is rejected");
        unchanged();
    }
    auto stale = accept();
    game.manager_records[pool + 11] ^= 1;
    const auto stale_bytes = save_stream(game);
    serve(stale);
    check(save_stream(game) == stale_bytes && revision == original_revision &&
          !flow.pending_advertising_action(), "changed native offer is rejected atomically before acceptance");
    game.manager_records[pool + 11] ^= 1;
    unchanged();

    // Both frontends use this application boundary. It must keep the observed
    // offer's identity, not silently refresh a stale selection from live state.
    auto observed = advertising_from_game(game, manager, revision);
    ++observed.revision;
    std::string shared_error;
    check(!session->accept_advertising_selection(observed, 0, 3, 0x700000001ULL,
              revision, shared_error) && !shared_error.empty(),
          "shared advertising selection rejects a stale observed revision");
    unchanged();
    const auto interrupted = accept();
    const auto shared_last_id = session->advertising_checkpoint().last_request_id;
    bool acknowledgement_failed = false;
    try {
        session->apply_advertising(interrupted.advertising_action, revision, shared_error,
            [&](const app::CareerSession::AdvertisingActionResult&) {
                session->rand().rand();
                session->game().range(0, 10);
                throw std::runtime_error("shared advertising acknowledgement rejected");
            });
    } catch (const std::runtime_error&) { acknowledgement_failed = true; }
    check(acknowledgement_failed && session->advertising_checkpoint().last_request_id == shared_last_id,
          "shared advertising rollback restores the consumed request id after failed acknowledgement");
    unchanged();
    flow.reject_advertising_action("shared acknowledgement test");

    // Force the authoritative desk ACK to fail after the Core acceptance has succeeded.
    const auto failed_ack = accept();
    const size_t name_byte = record + 0xa8;
    const auto old_name = game.manager_records[name_byte];
    game.manager_records[name_byte] = old_name == 'X' ? 'Y' : 'X';
    const auto rollback_bytes = save_stream(game);
    const auto last_id = session->advertising_checkpoint().last_request_id;
    serve(failed_ack);
    check(save_stream(game) == rollback_bytes && revision == original_revision &&
          session->advertising_checkpoint().last_request_id == last_id &&
          session->rand().calls() == original_watcom.calls() && session->game().calls() == original_game_rng.calls(),
          "failed advertising ACK restores native state, revision and consumed request id");
    game.manager_records[name_byte] = old_name;
    unchanged();

    auto action = accept();
    auto expected = game;
    check(accept_advertising_offer(expected, action.advertising_action.offer).status == AdvertisingOfferStatus::accepted,
          "independent source commit accepts the two-board offer");
    const auto expected_bytes = save_stream(expected);
    const int32_t cash_before = signed_u32(game.manager_records, record + 0x314);
    serve(action);
    check(save_stream(game) == expected_bytes && revision == original_revision + 1 &&
          !flow.pending_advertising_action() && flow.screen_name() == "advertising" &&
          flow.desktop()->state().revision == revision &&
          signed_u32(game.manager_records, record + 0x314) == cash_before &&
          session->rand().seed() == original_watcom.seed() && session->rand().calls() == original_watcom.calls() &&
          session->game().seed() == original_game_rng.seed() && session->game().calls() == original_game_rng.calls(),
          "accepted advertisement matches Core bytes, refreshes desk once, and draws neither RNG nor immediate cash");
    const auto projection = advertising_from_game(game, manager, revision);
    check(projection.finance_preview.income == advertising_from_game(expected, manager, revision).finance_preview.income,
          "accepted advertising finance preview is coherent with the committed contracts");
    const auto committed_bytes = save_stream(game);
    const auto committed_revision = revision;
    serve(action);
    std::string error;
    check(!session->dispatch_advertising(action.advertising_action, revision, error) &&
          save_stream(game) == committed_bytes && revision == committed_revision,
          "replayed accepted advertisement cannot consume a second revision or another offer");
}

void check_environment_controller(const std::filesystem::path& data) {
    StartupFlow flow(data);
    flow.click(150, 215); flow.click(5, 225, true); flow.click(5, 225, true);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    const auto serve = [&](const FlowResult& request) {
        return app::serve_flow_request(flow, request, session, data, data, 0x1789abcd,
            revision, playback, audio_session, false);
    };
    check(serve(flow.click(90, 169)).empty() && session && flow.screen_name() == "desktop",
          "environment route starts with an actual initialized career");
    const auto menu = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
        [](const DeskItem& item) { return item.kind == 24 && !item.disabled; });
    check(menu != flow.desktop()->state().items.end(), "ordinary desk exposes the Original environment menu");
    check(serve(flow.click(menu->x + menu->width / 2, menu->y + menu->height / 2)).empty() &&
          flow.in_environment(), "menu 24 opens its source environment projection");

    const auto exe = load_le(asset_path(data, "BMH.exe"));
    const int x = exe.u8(::bmh::layout::Environment_FormatError_RuntimeField + 3) + 51, y = exe.u8(::bmh::layout::Environment_FormatError_RuntimeField2 + 3) + 5;
    (void)flow.pointer_down(x, y);
    auto action = flow.click(x, y);
    check(action.request == FlowRequest::environment_action && flow.pending_environment(),
          "Original tile release creates one typed environment action");
    const auto native_before = save_stream(session->state());
    const auto watcom_before = session->rand();
    const auto game_rng_before = session->game();
    const auto rev_before = revision;
    auto forged = action;
    ++forged.environment_action.manager_slot;
    check(serve(forged) == "stale_environment_owner" && revision == rev_before &&
          save_stream(session->state()) == native_before &&
          session->rand().seed() == watcom_before.seed() && session->rand().calls() == watcom_before.calls() &&
          session->game().seed() == game_rng_before.seed() && session->game().calls() == game_rng_before.calls() &&
          !flow.pending_environment(), "forged owner is rejected before Native or either RNG changes");

    (void)flow.pointer_down(x, y);
    action = flow.click(x, y);
    check(action.request == FlowRequest::environment_action && serve(action).empty() &&
          revision == rev_before + 1 && flow.in_environment() && !flow.pending_environment() &&
          flow.environment()->state().values.level[3] == 6 &&
          flow.environment()->state().values.monthly[3] == 30000,
          "valid environment transaction commits once and keeps the source screen open");
    (void)flow.pointer_down(10, 10, true);
    const auto close = flow.click(10, 10, true);
    check(close.request == FlowRequest::submenu_closed && !flow.in_environment() && flow.screen_name() == "desktop",
          "matched right-button close returns to the same desk without a turn advance");
}

void check_advertising_renewal_controller(const std::filesystem::path& data) {
    StartupFlow flow(data);
    flow.click(150, 215); flow.click(5, 225, true); flow.click(5, 225, true);
    const auto start = flow.click(90, 169);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    check(app::serve_flow_request(flow, start, session, data, data, 0x1789abcd,
        revision, playback, audio_session, false).empty(), "renewal flow starts a real session");
    auto& game = session->state();
    const int manager = int(game.memory.u8(::bmh::layout::ActiveManager));
    const size_t rec = size_t(manager) * 0xb0c;
    const size_t at = rec + 0xa34 + 14 * 0x0c;
    std::fill_n(game.manager_records.begin() + std::ptrdiff_t(at), 12, 0);
    game.manager_records[at + 5] = 0;
    game.manager_records[at + 6] = 12;
    game.manager_records[at + 7] = 3;
    // A low demand deterministically produces the original question for this club.
    game.manager_records[at + 8] = 0;
    game.manager_records[at + 9] = 0;
    const auto exe = load_le(asset_path(data, "BMH.exe"));
    const auto serve = [&](const FlowResult& request,
                           app::AdvertisingRenewalPresenter presenter = {}) {
        return app::serve_flow_request(flow, request, session, data, data, 0x1789abcd,
            revision, playback, audio_session, false,
            {}, {}, {}, {}, {}, {}, {}, {}, {}, app::PendingOfferEntryPolicy::legacy_unpresented,
            {}, app::HumanBuyerMarketEntryPolicy::legacy_unpresented, {}, {}, {}, {}, {},
            std::move(presenter));
    };
    const auto opened = flow.send_key("W");
    check(serve(opened).empty() && flow.screen_name() == "advertising",
          "expired contract is projected through the production advertising Flow");
    const int x = int(exe.u16(::bmh::layout::Advertising_Click_RuntimeField3 + 14 * 2)) + 1;
    const int y = int(exe.u16(::bmh::layout::Advertising_Click_RuntimeField4 + 14 * 2)) + 1;
    const auto click = flow.click(x, y);
    check(click.request == FlowRequest::advertising_action &&
          click.advertising_action.kind == AdvertisingActionKind::check_renewal &&
          click.advertising_action.renewal_display_slot == 14 &&
          click.advertising_action.renewal_contract_slot == 14,
          "expired-row click emits one typed display-to-physical renewal request");
    const auto old_w_calls = session->rand().calls();
    const auto old_revision = revision;
    bool presented = false;
    const auto result = serve(click, [&](const GameState& staged, const AdvertisingRenewal& q,
                                         const Image& backdrop) {
        presented = q.status == AdvertisingRenewal::Status::offered &&
                    !q.question.empty() && backdrop.width == 320 && backdrop.height == 240 &&
                    staged.manager_records.at(at + 6) == 12;
        return app::CareerSession::AdvertisingRenewalPresentation::renew;
    });
    check(result.empty() && presented && revision == old_revision + 1 &&
          session->rand().calls() == old_w_calls + 1 && !flow.pending_advertising_action() &&
          game.manager_records.at(at + 5) == 12 && game.manager_records.at(at + 6) == 0 &&
          flow.screen_name() == "advertising",
          "production Session/Flow stages Source question, accepts once, and refreshes the advertising screen");
    const auto bytes = save_stream(game);
    const auto draws = session->rand().calls();
    const auto rev = revision;
    check(serve(click, [&](const GameState&, const AdvertisingRenewal&, const Image&) {
                    return app::CareerSession::AdvertisingRenewalPresentation::renew; }).empty() &&
          save_stream(game) == bytes && revision == rev && session->rand().calls() == draws,
          "replayed renewal request cannot answer or draw a second time");
}

void check_advertising_spending_controller(const std::filesystem::path& data) {
    StartupFlow flow(data);
    flow.click(150, 215); flow.click(5, 225, true); flow.click(5, 225, true);
    const auto start = flow.click(90, 169);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    const auto saves = std::filesystem::temp_directory_path() / "openbmh-advertising-spending-controller";
    std::filesystem::create_directories(saves);
    const auto serve = [&](const FlowResult& action) {
        check(app::serve_flow_request(flow, action, session, data, saves, 0x987654,
            revision, playback, audio_session, false).empty(), "spending uses production controller");
    };
    serve(start);
    check(session && session->state().level != 0, "spending fixture starts a financial career");
    auto& initial_game = session->state();
    const int manager = int(initial_game.memory.u8(::bmh::layout::ActiveManager));
    const size_t record = size_t(manager) * 0xb0c;
    const auto write = [&](size_t at, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) initial_game.manager_records[record + at + i] = uint8_t(value >> (i * 8));
    };
    write(0x314, 500000); write(0x31c, 0);
    write(0x850, 0); write(0x854, 0); write(0x858, 0);
    initial_game.manager_records[record + 0x287] = 0;
    // Load the controlled account through the production file controller so all views agree.
    auto copied_rand = session->rand();
    write_savegame(saves / "AUTOSAVE.FBM", make_savegame(initial_game, copied_rand));
    const auto exe_load = load_le(asset_path(data, "BMH.exe"));
    serve(flow.send_key(std::string(1, char(exe_load.u8(::bmh::layout::Desktop_Key_RuntimeField + 10)))));
    serve(flow.send_key("ENTER"));
    check(flow.screen_name() == "desktop", "spending fixture uses a real native load");
    auto& g = session->state();
    serve(flow.send_key("W"));
    const auto exe = load_le(asset_path(data, "BMH.exe"));
    const auto open = [&](int target) {
        serve(flow.click(exe.u16(::bmh::layout::Advertising_Click_RuntimeField3 + unsigned(target) * 2) + 2,
                         exe.u16(::bmh::layout::Advertising_Click_RuntimeField4 + unsigned(target) * 2) + 2));
    };
    const auto submit = [&] {
        auto action = flow.send_key("ENTER");
        check(action.request == FlowRequest::advertising_action &&
              action.advertising_action.kind == AdvertisingActionKind::set_spending &&
              flow.pending_advertising_action(), "calculator emits an owned spending request");
        return action;
    };
    const auto before = save_stream(g);
    const auto w = session->rand(); const auto gr = session->game();
    open(17); flow.text("5000"); (void)flow.render();
    serve(flow.send_key("ESC"));
    check(save_stream(g) == before && session->rand().calls() == w.calls() &&
          session->game().calls() == gr.calls(), "calculator input and cancel are read-only");
    open(17); flow.text("5000");
    auto monthly = submit();
    auto forged = monthly; ++forged.advertising_action.spending.expected_amount;
    serve(forged);
    check(save_stream(g) == before && !flow.pending_advertising_action(),
          "forged monthly request cannot mutate state");
    open(17); flow.text("5000"); monthly = submit();
    serve(monthly);
    check(signed_u32(g.manager_records, record + 0x854) == 5000 &&
          signed_u32(g.manager_records, record + 0x314) == 500000 &&
          session->rand().calls() == w.calls() && session->game().calls() == gr.calls(),
          "monthly publicity budget changes only the persisted budget without cash or RNG");
    const auto monthly_bytes = save_stream(g); const auto monthly_revision = revision;
    serve(monthly);
    check(save_stream(g) == monthly_bytes && revision == monthly_revision,
          "monthly spending replay is inert");
    open(18); flow.text("10000"); serve(submit());
    check(signed_u32(g.manager_records, record + 0x858) == 10000 &&
          signed_u32(g.manager_records, record + 0x314) == 500000,
          "fanwork writes its separate monthly budget");
    open(16); flow.text("50000");
    auto merchandise = submit();
    const auto merchandise_before = save_stream(g); const auto revision_before = revision;
    const auto id_before = session->advertising_checkpoint().last_request_id;
    const auto name = g.manager_records[record + 0xa8];
    g.manager_records[record + 0xa8] = name == 'X' ? 'Y' : 'X';
    const auto changed_name = save_stream(g);
    serve(merchandise);
    check(save_stream(g) == changed_name && revision == revision_before &&
          session->advertising_checkpoint().last_request_id == id_before &&
          session->rand().seed() == w.seed() && session->rand().calls() == w.calls(),
          "failed merchandise ACK rolls back cash, delivery, consumed request and delivery RNG");
    g.manager_records[record + 0xa8] = name;
    check(save_stream(g) == merchandise_before, "merchandise fixture is restored for retry");
    auto expected_rand = w; const int weeks = expected_rand.range(6, 9);
    open(16); flow.text("50000"); merchandise = submit(); serve(merchandise);
    check(signed_u32(g.manager_records, record + 0x850) == 50000 &&
          signed_u32(g.manager_records, record + 0x314) == 450000 &&
          g.manager_records[record + 0x287] == weeks &&
          session->rand().seed() == expected_rand.seed() && session->rand().calls() == expected_rand.calls() &&
          session->game().seed() == gr.seed() && session->game().calls() == gr.calls(),
          "merchandise books once and uses precisely the original Watcom delivery draw");
    const auto accepted = save_stream(g); const auto accepted_revision = revision;
    serve(merchandise);
    check(save_stream(g) == accepted && revision == accepted_revision &&
          session->rand().calls() == expected_rand.calls(), "merchandise replay cannot charge or draw twice");
    // Close the original notification; opening merchandise while delivery is pending is inert.
    serve(flow.send_key("ENTER")); open(16);
    check(!flow.pending_advertising_action() && save_stream(g) == accepted &&
          session->rand().calls() == expected_rand.calls(), "pending delivery cannot start a second order");
}

void check_original_bank_controller(const std::filesystem::path& data) {
    StartupFlow flow(data);
    flow.click(150, 215); flow.click(5, 225, true); flow.click(5, 225, true);
    const auto start = flow.click(90, 169);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback; std::optional<uint64_t> audio_session;
    const auto saves = std::filesystem::temp_directory_path() / "openbmh-original-bank-controller";
    std::filesystem::create_directories(saves);
    const auto serve = [&](const FlowResult& action) {
        check(app::serve_flow_request(flow, action, session, data, saves, 0x782345,
            revision, playback, audio_session, false).empty(), "bank uses production controller");
    };
    serve(start);
    auto& initial = session->state();
    const size_t rec = size_t(initial.memory.u8(::bmh::layout::ActiveManager)) * 0xb0c;
    const auto write = [&](GameState& g, size_t field, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) g.manager_records[rec + field + i] = uint8_t(value >> (8 * i));
    };
    write(initial, 0x314, 500000); write(initial, 0x318, 25000); write(initial, 0x324, 0);
    auto copied = session->rand(); write_savegame(saves / "AUTOSAVE.FBM", make_savegame(initial, copied));
    const auto exe = load_le(asset_path(data, "BMH.exe"));
    serve(flow.send_key(std::string(1, char(exe.u8(::bmh::layout::Desktop_Key_RuntimeField + 10)))));
    serve(flow.send_key("ENTER")); check(flow.screen_name() == "desktop", "bank fixture is loaded natively");
    auto& g = session->state();
    const auto before = save_stream(g); const auto w = session->rand(); const auto gr = session->game();
    auto finance_fixture = g;
    const unsigned manager = finance_fixture.memory.u8(::bmh::layout::ActiveManager);
    for (unsigned i = 0; i < 6; ++i) {
        const auto at = ::bmh::layout::FundBlockUnsigned + i * 0x1c;
        finance_fixture.memory.set16(at + 0x14 + manager * 2, i == 0 ? 2 : 0);
        finance_fixture.memory.set32(at + 4 + manager * 4, i == 0 ? 1500 : i == 1 ? 777 : 0);
        if (i == 0) finance_fixture.memory.set32(at, 1000);
    }
    for (unsigned i = 0; i < 12; ++i) {
        const size_t at = i * 0x5c;
        auto put16 = [&](size_t field, uint16_t value) {
            finance_fixture.stocks.at(at + field) = uint8_t(value);
            finance_fixture.stocks.at(at + field + 1) = uint8_t(value >> 8);
        };
        put16(0x50 + manager * 2, i == 0 ? 3 : 0);
        for (unsigned b = 0; b < 4; ++b)
            finance_fixture.stocks.at(at + 4 + manager * 4 + b) = uint8_t((i == 0 ? 250u : i == 1 ? 888u : 0u) >> (8 * b));
        if (i == 0) put16(0x4e, 100);
    }
    for (unsigned i = 0; i < 5; ++i) {
        const auto at = ::bmh::layout::Analytics_SourceAssetMaskE_RuntimeFieldUnsigned + manager * 0x78 + i * 0x18;
        finance_fixture.memory.set32(at, i == 0 ? 1000 : 0);
        finance_fixture.memory.set32(at + 0x10, i == 0 ? 200 : 0);
    }
    const auto finance_stream = save_stream(finance_fixture);
    const auto finance = bank_from_game(finance_fixture, exe, revision);
    check(finance.finance_values[2] == 2000 && finance.finance_values[3] == 300 &&
          finance.finance_gains[0] == 500 && finance.finance_gains[1] == 50 &&
          finance.finance_values[4] == 800 && finance.finance_values[5] == 526300 &&
          finance.finance_value_text[2].find("(+500 EU)") != std::string::npos &&
          finance.finance_value_text[3].find("(+50 EU)") != std::string::npos &&
          !finance.finance_fund_labels[0].empty() && finance.finance_fund_labels[1].empty() &&
          !finance.finance_stock_labels[0].empty() && finance.finance_stock_labels[1].empty() &&
          save_stream(finance_fixture) == finance_stream,
          "financial projection distinguishes market values, gains and Source loan-mask semantics read-only");
    serve(flow.send_key(std::string(1, char(exe.u8(::bmh::layout::Desktop_Key_RuntimeField + 14)))));
    check(flow.screen_name() == "bank", "original desk key opens bank");
    const auto hit = [&](int index) {
        serve(flow.click(exe.u16(::bmh::layout::Bank_FinishCredit_RuntimeField + index * 2) + 2, exe.u8(::bmh::layout::Bank_FinishCredit_RuntimeField3 + index) + 2));
    };
    hit(1); (void)flow.render(); serve(flow.send_key("ESC"));
    check(flow.screen_name() == "bank" && save_stream(g) == before && session->rand().calls() == w.calls(),
          "finance overview and local close are read-only");
    hit(3); serve(flow.click(80, 178)); flow.text("5000"); (void)flow.render();
    serve(flow.send_key("ESC"));
    check(save_stream(g) == before && !flow.pending_bank_action(), "calculator cancel is inert");
    const auto submit = [&] {
        auto action = flow.send_key("ENTER");
        check(action.request == FlowRequest::submenu_action && std::holds_alternative<BankRequest>(action.submenu_action) &&
              flow.pending_bank_action(), "bank calculator emits an owned pending request");
        return action;
    };
    serve(flow.click(80, 178)); flow.text("5000"); auto deposit = submit();
    auto forged = deposit; ++std::get<BankRequest>(forged.submenu_action).amount; serve(forged);
    check(save_stream(g) == before && !flow.pending_bank_action(), "forged bank amount cannot mutate account");
    serve(flow.click(80, 178)); flow.text("5000"); deposit = submit();
    const auto old_revision = revision; const auto name = g.manager_records[rec + 0xa8];
    g.manager_records[rec + 0xa8] = name == 'X' ? 'Y' : 'X'; const auto changed_name = save_stream(g);
    serve(deposit);
    check(save_stream(g) == changed_name && revision == old_revision &&
          session->rand().seed() == w.seed() && session->rand().calls() == w.calls() &&
          session->game().seed() == gr.seed() && session->game().calls() == gr.calls(),
          "failed bank ACK rolls back all state and both RNGs");
    g.manager_records[rec + 0xa8] = name;
    serve(flow.click(80, 178)); flow.text("5000"); deposit = submit(); serve(deposit);
    GameState expected = g;
    write(expected, 0x314, 500000); write(expected, 0x318, 25000); write(expected, 0x324, 0);
    check(save_stream(expected) == before && signed_u32(g.manager_records, rec + 0x314) == 495000 &&
          signed_u32(g.manager_records, rec + 0x318) == 30000 &&
          signed_u32(g.manager_records, rec + 0x324) == -5000,
          "deposit changes only the three Source account fields, without statistics");
    const auto deposited = save_stream(g); const auto deposited_revision = revision; serve(deposit);
    check(save_stream(g) == deposited && revision == deposited_revision, "bank request replay is inert");
    serve(flow.click(205, 178)); flow.text("2000"); auto withdrawal = submit();
    check(std::get<BankRequest>(withdrawal.submenu_action).amount == -2000, "withdrawal uses opposite sign");
    serve(withdrawal);
    check(signed_u32(g.manager_records, rec + 0x314) == 497000 &&
          signed_u32(g.manager_records, rec + 0x318) == 28000 &&
          signed_u32(g.manager_records, rec + 0x324) == -3000 &&
          session->rand().seed() == w.seed() && session->rand().calls() == w.calls() &&
          session->game().seed() == gr.seed() && session->game().calls() == gr.calls(),
          "withdrawal reverses account fields without RNG");
    // Source exchange selection, count calculator and guarded production booking.
    serve(flow.send_key("BACKSPACE")); serve(flow.send_key("B"));
    const auto exchange_before = save_stream(g); const auto exchange_w = session->rand();
    const auto exchange_gr = session->game();
    serve(flow.click(55, 64)); (void)flow.render(); serve(flow.click(80, 178));
    flow.text("1"); serve(flow.send_key("ESC"));
    check(save_stream(g) == exchange_before && session->rand().calls() == exchange_w.calls() &&
          session->game().calls() == exchange_gr.calls(), "stock selection and calculator cancel are inert");
    check(!flow.text("1") && flow.send_key("ENTER").request == FlowRequest::none,
          "closed stock calculator cannot submit typed or canceled input");
    serve(flow.click(80, 178));
    flow.text(std::to_string(std::max(1, max_shares_to_buy(g, int(g.memory.u8(::bmh::layout::ActiveManager)), 0) / 2)));
    const auto rejected_portfolio = submit();
    serve(rejected_portfolio);
    check(save_stream(g) == exchange_before && session->rand().seed() == exchange_w.seed() &&
          session->rand().calls() == exchange_w.calls() && session->game().seed() == exchange_gr.seed() &&
          session->game().calls() == exchange_gr.calls() &&
          flow.state_json().find("\"investment_warning_open\":true") != std::string::npos,
          "Source portfolio rejection shows board warning without booking or RNG");
    (void)flow.render(); serve(flow.click(80, 178));
    check(flow.state_json().find("\"investment_warning_open\":false") != std::string::npos &&
          !flow.pending_bank_action(), "warning click dismisses only the notice");
    serve(flow.click(80, 178)); flow.text("1"); auto shares = submit();
    const auto share_request = std::get<BankRequest>(shares.submenu_action);
    check(share_request.kind == BankRequestKind::buy_shares && share_request.asset_index == 0 &&
          share_request.count == 1 && share_request.amount == 0, "Original purchase binds selected stock and count");
    auto forged_stock = shares; std::get<BankRequest>(forged_stock.submenu_action).asset_index = 1;
    serve(forged_stock);
    check(save_stream(g) == exchange_before && !flow.pending_bank_action(), "forged stock selection is rejected");
    serve(flow.click(80, 178)); flow.text("1"); shares = submit();
    g.manager_records[rec + 0xa8] = name == 'X' ? 'Y' : 'X'; const auto stock_changed_name = save_stream(g);
    const auto stock_revision = revision; serve(shares);
    check(save_stream(g) == stock_changed_name && revision == stock_revision &&
          session->rand().seed() == exchange_w.seed() && session->game().seed() == exchange_gr.seed(),
          "failed purchase ACK restores account and holdings");
    g.manager_records[rec + 0xa8] = name;
    const auto held_before = uint16_t(g.stocks.at(0x50) | uint16_t(g.stocks.at(0x51)) << 8);
    const auto price = uint16_t(g.stocks.at(0x4e) | uint16_t(g.stocks.at(0x4f)) << 8);
    const auto cash_before = signed_u32(g.manager_records, rec + 0x314);
    serve(flow.click(80, 178)); flow.text("1"); shares = submit(); serve(shares);
    check(uint16_t(g.stocks.at(0x50) | uint16_t(g.stocks.at(0x51)) << 8) == held_before + 1 &&
          signed_u32(g.manager_records, rec + 0x314) == cash_before - price - price * 15 / 1000 &&
          session->rand().seed() == exchange_w.seed() && session->rand().calls() == exchange_w.calls() &&
          session->game().seed() == exchange_gr.seed() && session->game().calls() == exchange_gr.calls(),
          "purchase applies Source price and integer fee without RNG");
    const auto purchased = save_stream(g); const auto purchased_revision = revision; serve(shares);
    check(save_stream(g) == purchased && revision == purchased_revision, "stock purchase replay is inert");
    serve(flow.click(205, 178)); serve(flow.send_key("CLEAR")); flow.text("1");
    const auto sale = submit();
    check(std::get<BankRequest>(sale.submenu_action).kind == BankRequestKind::sell_shares &&
          std::get<BankRequest>(sale.submenu_action).count == 1, "Original sell calculator submits count");
    serve(sale);
    check(uint16_t(g.stocks.at(0x50) | uint16_t(g.stocks.at(0x51)) << 8) == held_before &&
          signed_u32(g.manager_records, rec + 0x314) == cash_before - price * 15 / 1000,
          "sale returns market proceeds and preserves purchase fee");
    // Existing paid stock tip must also roll back its hidden result and conditional RNG on failed ACK.
    auto tip = flow.click(80, 165);
    check(tip.request == FlowRequest::submenu_action && flow.pending_bank_action(), "stock-tip fixture is pending");
    const auto tip_before = session->bank_checkpoint();
    g.manager_records[rec + 0xa8] = name == 'X' ? 'Y' : 'X'; const auto tip_changed_name = save_stream(g);
    serve(tip);
    check(save_stream(g) == tip_changed_name && session->bank_checkpoint().last_tip == tip_before.last_tip &&
          session->rand().seed() == tip_before.rand.seed() && session->rand().calls() == tip_before.rand.calls() &&
          session->game().seed() == tip_before.game.seed() && session->game().calls() == tip_before.game.calls(),
          "failed paid-advice ACK restores hidden advice, account and both RNGs");
    g.manager_records[rec + 0xa8] = name;
    serve(flow.send_key("BACKSPACE")); serve(flow.send_key("I"));
    int fund = -1;
    for (int i = 1; i < 6; ++i)
        if (max_fund_units(g, int(g.memory.u8(::bmh::layout::ActiveManager)), i) > 0) { fund = i; break; }
    check(fund > 0, "fund rollback fixture can purchase a surcharge-bearing asset");
    for (int i = 0; i < fund; ++i) serve(flow.click(205, 130));
    serve(flow.click(80, 178)); flow.text("1");
    auto fund_action = flow.send_key("ENTER");
    check(fund_action.request == FlowRequest::submenu_action && flow.pending_bank_action(),
          "fund purchase has an owned pending request");
    const auto fund_before = session->bank_checkpoint(); const auto fund_revision = revision;
    auto source_copy = g; auto source_rng = session->game();
    (void)buy_fund_units(source_copy, int(g.memory.u8(::bmh::layout::ActiveManager)), fund, 1, source_rng);
    check(source_rng.calls() > fund_before.game.calls(), "selected Source purchase actually consumes GameRandom");
    g.manager_records[rec + 0xa8] = name == 'X' ? 'Y' : 'X'; const auto fund_changed_name = save_stream(g);
    serve(fund_action);
    check(save_stream(g) == fund_changed_name && revision == fund_revision &&
          session->bank_checkpoint().last_tip == fund_before.last_tip &&
          session->bank_checkpoint().property_notice == fund_before.property_notice &&
          session->rand().seed() == fund_before.rand.seed() && session->rand().calls() == fund_before.rand.calls() &&
          session->game().seed() == fund_before.game.seed() && session->game().calls() == fund_before.game.calls(),
          "failed asset-purchase ACK rolls back consumed GameRandom, full state and revision");
    g.manager_records[rec + 0xa8] = name;
    const auto property_before = session->bank_checkpoint();
    const auto property_cash = signed_u32(g.manager_records, rec + 0x314);
    const uint32_t property_at = ::bmh::layout::FundBlockUnsigned + uint32_t(fund) * 0x1c;
    const int32_t property_price = int32_t(g.memory.u32(property_at));
    const uint16_t property_held = g.memory.u16(property_at + 0x14 + uint32_t(manager) * 2);
    auto expected_draw = session->game();
    const int32_t extra = expected_draw.range(property_price / 40, property_price / 10);
    serve(flow.click(80, 178)); flow.text("1"); auto property_buy = submit(); serve(property_buy);
    const auto projected_notice = session->preparation_projection(revision).bank->property_notice;
    check(projected_notice && projected_notice->asset_index == fund && projected_notice->additional_cost == extra &&
          !projected_notice->text.empty() && g.memory.u16(property_at + 0x14 + uint32_t(manager) * 2) == property_held + 1 &&
          signed_u32(g.manager_records, rec + 0x314) == property_cash - property_price - property_price / 20 - extra &&
          session->game().seed() == expected_draw.seed() && session->game().calls() == expected_draw.calls() &&
          session->rand().seed() == property_before.rand.seed() && session->rand().calls() == property_before.rand.calls(),
          "property purchase returns actual renovation cost and exactly one Source GameRandom draw");
    const auto property_bought = save_stream(g); const auto property_revision = revision;
    serve(property_buy);
    check(save_stream(g) == property_bought && revision == property_revision,
          "property purchase replay cannot charge a second renovation fee");
    auto notice_ack = flow.send_key("ENTER");
    check(notice_ack.request == FlowRequest::none, "renovation notice is a local ACK of the already booked purchase");
    serve(notice_ack);
    check(save_stream(g) == property_bought && revision == property_revision && session->game().seed() == expected_draw.seed() &&
          session->game().calls() == expected_draw.calls(), "notice ACK leaves saved state and RNG unchanged");
    // A later failed ACK must restore the previous genuine notice, not merely clear it.
    serve(flow.click(80,178)); flow.text("1"); auto second_property = submit();
    const auto prior_notice = session->bank_checkpoint();
    g.manager_records[rec + 0xa8] = name == 'X' ? 'Y' : 'X'; const auto changed_property_name = save_stream(g);
    serve(second_property);
    check(save_stream(g) == changed_property_name && revision == property_revision &&
          session->bank_checkpoint().property_notice == prior_notice.property_notice &&
          session->game().seed() == prior_notice.game.seed() && session->game().calls() == prior_notice.game.calls() &&
          session->rand().seed() == prior_notice.rand.seed() && session->rand().calls() == prior_notice.rand.calls(),
          "failed later property ACK restores prior notice, all booking fields and both RNGs");
    g.manager_records[rec + 0xa8] = name;
    serve(flow.click(205,178)); flow.text("1"); auto property_sale = submit(); serve(property_sale);
    check(g.memory.u16(property_at + 0x14 + uint32_t(manager) * 2) == property_held &&
          signed_u32(g.manager_records, rec + 0x314) == property_cash - property_price / 20 - extra &&
          !session->bank_checkpoint().property_notice && session->game().seed() == prior_notice.game.seed() &&
          session->game().calls() == prior_notice.game.calls(), "property sale returns market proceeds without a renovation draw");
    serve(flow.send_key("ESC")); serve(flow.click(70,100));
    const auto credit_view = session->bank_checkpoint(); const auto credit_view_stream=save_stream(g);
    serve(flow.click(150,74)); serve(flow.send_key("CLEAR")); flow.text("100000"); serve(flow.send_key("ENTER"));
    check(save_stream(g)==credit_view_stream && !flow.pending_bank_action(),
          "credit calculator only edits a local draft without booking");
    serve(flow.click(210,178));
    check(save_stream(g)==credit_view_stream && !flow.pending_bank_action(),
          "Source ABBRUCH discards the entire local credit draft");
    serve(flow.click(70,100)); serve(flow.click(150,74)); serve(flow.send_key("CLEAR")); flow.text("100000");
    serve(flow.send_key("ENTER")); auto credit_request=flow.click(80,178);
    check(credit_request.request==FlowRequest::submenu_action && flow.pending_bank_action()
          && std::get<BankRequest>(credit_request.submenu_action).kind==BankRequestKind::bank_credit,
          "Source OK creates one owned credit request");
    const auto envelope_before=save_stream(g); const auto envelope_revision=revision;
    auto forged_credit=credit_request;
    ++std::get<BankRequest>(forged_credit.submenu_action).credit_edits.front().value;
    serve(forged_credit);
    check(save_stream(g)==envelope_before && revision==envelope_revision && !flow.pending_bank_action(),
          "altered supplied credit edits are rejected before dispatching the owned request");
    credit_request=flow.click(80,178);
    auto forged_counterparty=credit_request;
    std::get<BankRequest>(forged_counterparty.submenu_action).credit_counterparty=manager+1;
    serve(forged_counterparty);
    check(save_stream(g)==envelope_before && revision==envelope_revision && !flow.pending_bank_action()
          && session->game().seed()==credit_view.game.seed() && session->game().calls()==credit_view.game.calls()
          && session->rand().seed()==credit_view.rand.seed() && session->rand().calls()==credit_view.rand.calls(),
          "altered supplied credit counterparty is rejected before booking the owned bank request");
    credit_request=flow.click(80,178);
    const auto credit_rev=revision;
    g.manager_records[rec+0xa8]=name=='X'?'Y':'X'; const auto changed_credit_name=save_stream(g);
    serve(credit_request);
    check(save_stream(g)==changed_credit_name && revision==credit_rev
          && session->bank_checkpoint().credit_notice==credit_view.credit_notice
          && session->game().seed()==credit_view.game.seed() && session->game().calls()==credit_view.game.calls()
          && session->rand().seed()==credit_view.rand.seed() && session->rand().calls()==credit_view.rand.calls(),
          "failed credit ACK rolls back loan/cash/notice/revision and both RNGs");
    g.manager_records[rec+0xa8]=name;
    auto accepted_credit=flow.click(80,178); serve(accepted_credit);
    const uint32_t bank_loan=::bmh::layout::Loans+uint32_t(manager)*0x78;
    check(g.memory.u32(bank_loan)==100000 && g.memory.u32(bank_loan+8)==2624
          && signed_u32(g.manager_records,rec+0x314)==property_cash-property_price/20-extra+100000,
          "production controller accepts Source credit and installment record once");
    const auto credit_committed=save_stream(g); const auto credit_committed_rev=revision;
    serve(accepted_credit);
    check(save_stream(g)==credit_committed && revision==credit_committed_rev,
          "owned credit request replay cannot make a second disbursement");
    serve(flow.click(70,100)); serve(flow.click(150,128)); flow.text("20000");
    serve(flow.send_key("ENTER")); auto fee_question=flow.click(80,178);
    check(fee_question.request==FlowRequest::none && !flow.pending_bank_action()
          && flow.state_json().find("\"credit_fee_confirmation_open\":true")!=std::string::npos,
          "repayment requires the original JA/NEIN fee question before Core dispatch");
    serve(flow.click(210,170));
    check(save_stream(g)==credit_committed && revision==credit_committed_rev,
          "NEIN discards repayment and fees without a mutation");
    serve(flow.click(70,100)); serve(flow.click(150,128)); flow.text("20000");
    serve(flow.send_key("ENTER")); serve(flow.click(80,178)); auto repayment=flow.click(90,170); serve(repayment);
    check(g.memory.u32(bank_loan)==80000 && g.memory.u32(bank_loan+8)==2099
          && signed_u32(g.manager_records,rec+0x314)==property_cash-property_price/20-extra+74800,
          "JA books Source partial repayment and fees through production controller");
}

void check_friendly_match_controller(const std::filesystem::path& data) {
    StartupFlow flow(data);
    flow.click(150, 215); flow.click(5, 225, true); flow.click(5, 225, true);
    auto started = flow.click(90, 169);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    const auto serve = [&](const FlowResult& result) {
        check(app::serve_flow_request(flow, result, session, data, data, 0x20101004,
            revision, playback, audio_session, false).empty(), "friendly flow uses the production App controller");
    };
    serve(started);
    check(session && flow.screen_name() == "desktop", "friendly fixture reaches an initialized engine desk");
    auto& game = session->state();
    const auto menu = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
        [](const DeskItem& item) { return item.kind == 6 && !item.disabled; });
    check(menu != flow.desktop()->state().items.end(), "Original club desk exposes friendly-booking kind 6");
    auto opened = flow.click(menu->x + menu->width / 2, menu->y + menu->height / 2);
    check(opened.request == FlowRequest::desk_action, "kind 6 produces the real pending desk navigation");
    serve(opened);
    check(flow.screen_name() == "friendly_match" && !flow.desktop()->pending(),
          "controller attaches authoritative tables and opens the friendly screen");
    const int friendly_x=menu->x+menu->width/2, friendly_y=menu->y+menu->height/2;
    const auto picker_w=session->rand();
    const auto picker_g=session->game();
    const auto picker_revision=revision;
    auto expected_picker=game;
    const uint32_t picker_word=::bmh::layout::LeagueTableDisplayModes+uint32_t(flow.desktop()->state().manager_slot)*2;
    uint16_t returned_word=uint16_t(expected_picker.memory.u16(picker_word)&0x0f7f);
    if(flow.friendly_opponent_picker()->state().source_normal_mode)returned_word|=0x80;
    expected_picker.memory.set16(picker_word,returned_word);
    check(flow.friendly_table_active() && flow.friendly_opponent_picker()!=nullptr,
          "friendly picker has its direct Source table owner");
    const auto expired=flow.process_timer_expired(0,0);
    check(expired.request==FlowRequest::league_family_action,
          "Source event8 emits an identity-bound opponent-table close");
    serve(expired);
    check(
          flow.screen_name()=="desktop" && !flow.friendly_opponent_picker() &&
          !flow.pending_friendly_action() && save_stream(game)==save_stream(expected_picker) && revision==picker_revision+1 &&
          !session->league_family_owner() &&
          session->rand().calls()==picker_w.calls() && session->game().calls()==picker_g.calls(),
          "format field event8 returns with only its common format word and no quote or random draw");
    serve(flow.click(friendly_x,friendly_y));

    const int manager = flow.desktop()->state().manager_slot;
    const int own_team = game.manager_teams.at(size_t(manager));
    const auto leagues = league_tables_from_game(game, manager);
    int opponent = -1, opponent_row = -1;
    for (const auto& table : leagues) {
        const auto own = std::find_if(table.rows.begin(), table.rows.end(),
            [&](const LeagueTableRow& row) { return row.runtime_team_id == own_team; });
        if (own == table.rows.end()) continue;
        for (size_t row = 0; row < table.rows.size(); ++row) {
            if (table.rows[row].runtime_team_id == own_team) continue;
            opponent = table.rows[row].runtime_team_id;
            opponent_row = int(row);
            break;
        }
        if (opponent >= 0) break;
    }
    check(opponent >= 0 && opponent_row >= 0, "active league has a selectable club opponent");
    const auto rng_game = session->game().seed();
    const auto rng_rand = session->rand().seed();
    const auto calls_game = session->game().calls(), calls_rand = session->rand().calls();
    const int opponent_y = 0x23 - 4 + opponent_row * 7 + 2;
    const auto selected=flow.pointer_down(300,opponent_y);
    check(selected.request==FlowRequest::league_family_action &&
          selected.league_family_action.kind==LeagueFamilyRequestKind::select_opponent,
          "physical table row carries an identity-bound opponent selection");
    serve(selected);
    auto before_quote=save_stream(game);
    uint64_t before_quote_revision=revision;
    check(!session->league_family_owner()&&!flow.friendly_table_active(),
          "direct opponent return releases the table before venue/quote");
    check(flow.click(99,171,true).request==FlowRequest::none &&
          flow.send_key("ESC").request==FlowRequest::none &&
          flow.state_json().find("\"stage\":\"venue\"")!=std::string::npos &&
          !flow.pending_friendly_action() && save_stream(game)==before_quote &&
          session->game().calls()==calls_game && session->rand().calls()==calls_rand,
          "Original venue ignores right/key input without dispatch, writes or RNG draws");
    auto quote_action = flow.click(99, 171);
    check(quote_action.request == FlowRequest::friendly_action &&
          quote_action.friendly_action.kind == FriendlyRequestKind::quote,
          "home venue emits a distinct pending quote request");
    const auto pending_quote_json=flow.state_json();
    check(!flow.process_timer_expired(0,0).changed && flow.state_json()==pending_quote_json &&
          flow.pending_friendly_action() && !flow.friendly_opponent_picker(),
          "opponent deadline cannot consume an owned quote request in the next stage");
    auto stale_quote = quote_action;
    --stale_quote.friendly_action.revision;
    serve(stale_quote);
    check(flow.pending_friendly_action() && save_stream(game) == before_quote && revision == before_quote_revision &&
          session->game().seed() == rng_game && session->rand().seed() == rng_rand &&
          session->game().calls() == calls_game && session->rand().calls() == calls_rand,
          "stale quote envelope is inert and does not consume the valid pending request");
    serve(quote_action);
    check(flow.state_json().find("\"stage\":\"cost_confirmation\"") != std::string::npos &&
          !flow.pending_friendly_action() && save_stream(game) == before_quote && revision == before_quote_revision &&
          session->game().calls() == calls_game && session->rand().calls() == calls_rand,
          "production quote ACK is read-only and clears exactly one request");
    serve(quote_action);
    check(save_stream(game) == before_quote && revision == before_quote_revision,
          "replayed quote ACK cannot change state or revision");
    check(flow.click(213,171,true).request==FlowRequest::none &&
          flow.send_key("ESC").request==FlowRequest::none &&
          flow.state_json().find("\"stage\":\"cost_confirmation\"")!=std::string::npos &&
          save_stream(game)==before_quote && revision==before_quote_revision &&
          session->game().calls()==calls_game && session->rand().calls()==calls_rand,
          "Original fee question ignores right/Escape and retains its read-only quote");

    check(flow.click(213, 171).request == FlowRequest::submenu_closed &&
          flow.screen_name()=="desktop" && !flow.pending_friendly_action() &&
          save_stream(game) == before_quote && revision == before_quote_revision &&
          session->game().calls() == calls_game && session->rand().calls() == calls_rand,
          "Source fee decline ends menu6 without booking, write or RNG draw");

    serve(flow.click(friendly_x,friendly_y));
    check(flow.screen_name()=="friendly_match","a fresh desk request opens a new friendly caller after decline");
    serve(flow.pointer_down(40,opponent_y));
    before_quote=save_stream(game);before_quote_revision=revision;
    quote_action = flow.click(99, 171);
    serve(quote_action);
    check(flow.state_json().find("\"stage\":\"cost_confirmation\"") != std::string::npos,
          "the same opponent can be quoted again after a local decline");
    flow.click(99, 171); // accept the displayed quote; no booking yet
    check(flow.state_json().find("\"stage\":\"calendar\"") != std::string::npos &&
          save_stream(game) == before_quote && revision == before_quote_revision,
          "fee acceptance only opens the local calendar");
    const auto engine_quote = quote_friendly_match(game, manager, opponent, true);
    int book_day = -1;
    for (int day = game.season_day() + 1; day <= 365; ++day) {
        const auto status = friendly_match_date_status(game, engine_quote, uint16_t(day));
        if (status == FriendlyDateStatus::available ||
            status == FriendlyDateStatus::available_with_calendar_event) { book_day = day; break; }
    }
    check(book_day >= 0, "the authoritative calendar projection has a future available date");
    const auto exe = load_le(asset_path(data, "BMH.exe"));
    const auto cell = friendly_calendar_cell(exe, game.season_start, book_day);
    auto book_action = flow.click(cell.x + 1, cell.y + 1);
    check(book_action.request == FlowRequest::friendly_action &&
          book_action.friendly_action.kind == FriendlyRequestKind::book &&
          book_action.friendly_action.quote_id == quote_action.friendly_action.request_id &&
          book_action.friendly_action.season_day == book_day &&
          book_action.friendly_action.request_id != quote_action.friendly_action.request_id &&
          book_action.friendly_action.cost == engine_quote.cost,
          "calendar selection emits a new booking ID bound to the frozen quote");
    auto forged_book = book_action;
    ++forged_book.friendly_action.cost;
    const auto before_book = save_stream(game);
    serve(forged_book);
    check(save_stream(game) == before_book && revision == before_quote_revision &&
          flow.pending_friendly_action(), "forged cost is rejected before the engine booking");
    const int planner_before = game.memory.u8(::bmh::layout::FriendlyPlanCount);
    serve(book_action);
    check(revision == before_quote_revision + 1 && game.memory.u8(::bmh::layout::FriendlyPlanCount) == planner_before + 2 &&
          save_stream(game) != before_book && flow.screen_name() == "desktop" &&
          session->game().calls() == calls_game && session->rand().calls() == calls_rand &&
          !flow.pending_friendly_action(),
          "successful core booking acknowledges once and returns directly to Source desk without RNG");
    const auto accepted_bytes = save_stream(game);
    const auto accepted_revision = revision;
    serve(book_action);
    check(save_stream(game) == accepted_bytes && revision == accepted_revision,
          "replayed booking cannot book or charge the manager twice");
    check(!flow.desktop()->pending() && !flow.pending_friendly_action() &&
          !flow.friendly_calendar_quote(), "successful booking leaves no child or pending parent request");
    std::puts("friendly App controller: quote, decline, calendar and single booking OK");
}

void check_semantic_desk_settings(const std::filesystem::path& data) {
    StartupFlow setup(data);
    setup.click(150,215); setup.click(5,225,true); setup.click(5,225,true); setup.click(90,169);
    app::CareerSession session(data,0x12345678);
    std::string error;
    check(session.confirm_manager(setup.pending_career()->options,setup.pending_career()->managers[0],error),
          "semantic setting fixture confirms an actual manager");
    auto& game=session.state();
    uint64_t revision=0;
    const auto identity=[&] {
        const int manager=game.memory.u8(::bmh::layout::ActiveManager), club=game.manager_teams.at(size_t(manager));
        return app::DeskSettingIdentity{manager,club,int(game.club_ids.at(size_t(club))),revision};
    };
    const auto reject=[&](const auto& dispatch,const char* reason) {
        const auto before=save_stream(game); const auto r=revision;
        const auto w=session.rand(); const auto g=session.game();
        check(!dispatch()&&!error.empty()&&revision==r&&save_stream(game)==before&&
              session.rand().seed()==w.seed()&&session.rand().calls()==w.calls()&&
              session.game().seed()==g.seed()&&session.game().calls()==g.calls(),reason);
    };
    const auto accept=[&](const auto& dispatch,const GameState& expected,const char* reason) {
        const auto r=revision; const auto w=session.rand(); const auto g=session.game();
        check(dispatch()&&error.empty()&&revision==r+1&&save_stream(game)==save_stream(expected)&&
              session.rand().seed()==w.seed()&&session.rand().calls()==w.calls()&&
              session.game().seed()==g.seed()&&session.game().calls()==g.calls(),reason);
    };
    // During setup ActiveManager is the spare record4, not an active human desk.
    // Bind the confirmed slot explicitly so dispatch tests the pre-desk guard.
    const int confirmed_club=game.manager_teams.at(0);
    auto price=app::TicketPriceRequest{{0,confirmed_club,int(game.club_ids.at(size_t(confirmed_club))),revision},17};
    reject([&]{return session.dispatch_ticket_price(price,revision,error);},
           "typed setting cannot edit a career before its first desk");
    check(error=="desk_setting_unavailable","pre-desk failure is the actual phase guard");
    CareerInitializationResult init;
    check(session.finish(init,error),"semantic setting fixture reaches a real desk");
    revision=init.desktop.revision;
    const int manager=game.memory.u8(::bmh::layout::ActiveManager); const size_t rec=size_t(manager)*0xb0c;
    const auto default_desk=session.desktop_projection(revision);
    check(std::none_of(default_desk.items.begin(),default_desk.items.end(),[](const auto& item) {
        return item.kind==64&&item.open&&!item.disabled;
    }),"parent failure uses the actual default closed automation drawer");
    int automation_item=-1,price_item=-1;
    for(int i=0;i<54;++i) {
        const auto at=::bmh::layout::DesktopsUnsigned+uint32_t(manager)*0x360+uint32_t(i)*16;
        const auto kind=game.memory.u8(at+12);
        if(kind==64)automation_item=i;
        if(kind==55) {
            price_item=i;
            game.memory.set8(at+13,uint8_t(game.memory.u8(at+13)&~0x80));
        }
    }
    check(automation_item>=0&&price_item>=0,"Original MEN contains both setting drawers");
    DeskRequest legacy{DeskRequestKind::change_option,manager,automation_item,64,1,revision};
    reject([&]{return session.dispatch_desk_widget(legacy,revision,error);},
           "original SDL dispatcher still rejects default closed automation");
    check(error=="stale_or_closed_desk_widget","closed widget fails at the retained SDL item guard");
    for(int value:{8,25,17}) {
        price={identity(),value}; auto expected=game; expected.manager_records[rec+0x26e]=uint8_t(value);
        accept([&]{return session.dispatch_ticket_price(price,revision,error);},expected,
               "typed price works with a closed drawer and changes only admission, never cash or credit");
    }
    reject([&]{return session.dispatch_ticket_price(price,revision,error);},"stale price replay is wholly neutral");
    legacy={DeskRequestKind::change_ticket_price,manager,price_item,55,12,revision};
    reject([&]{return session.dispatch_desk_widget(legacy,revision,error);},"typed price does not unlock the SDL drawer");
    for(int value:{7,26}) {
        price={identity(),value};
        reject([&]{return session.dispatch_ticket_price(price,revision,error);},"typed price rejects outside Original endpoints");
    }
    for(int variant=0;variant<5;++variant) {
        price={identity(),12};
        if(variant==0)--price.identity.revision;
        if(variant==1)price.identity.manager_slot=manager+1;
        if(variant==2)++price.identity.runtime_club;
        if(variant==3)++*price.identity.stable_club_identity;
        if(variant==4)price.identity.stable_club_identity.reset();
        reject([&]{return session.dispatch_ticket_price(price,revision,error);},"typed identity rejects stale, nonactive, wrong club or missing stable owner");
    }
    const auto saved_team=game.manager_records[rec+0x290];
    game.manager_records[rec+0x290]^=1;price={identity(),12};
    reject([&]{return session.dispatch_ticket_price(price,revision,error);},"typed owner validates the live Native manager record");
    game.manager_records[rec+0x290]=saved_team;
    auto automation=app::AutomationRequest{identity(),app::DeskAutomation::Substitution,true,std::nullopt};
    for(int index=1;index<=4;++index) {
        game.manager_options[size_t(manager)*5+size_t(index)]=0xa4;
        for(bool enabled:{true,true,false}) {
            automation={identity(),static_cast<app::DeskAutomation>(index),enabled,std::nullopt};
            auto expected=game;expected.manager_options[size_t(manager)*5+size_t(index)]=uint8_t(0xa4|int(enabled));
            accept([&]{return session.dispatch_automation(automation,revision,error);},expected,
                   "typed automation sets an explicit boolean idempotently and preserves every high bit");
        }
    }
    reject([&]{return session.dispatch_automation(automation,revision,error);},"automation replay cannot toggle again");
    automation={identity(),app::DeskAutomation::Substitution,true,3};
    reject([&]{return session.dispatch_automation(automation,revision,error);},"ordinary automation rejects a stray formation parameter");
    automation={identity(),static_cast<app::DeskAutomation>(5),true,std::nullopt};
    reject([&]{return session.dispatch_automation(automation,revision,error);},"typed automation rejects unknown kind");
    game.manager_records[rec+0x218]=0x84; game.manager_records[rec+0x219]=0x02;
    game.manager_options[size_t(manager)*5]=0;
    for(int slot:{3,8,0}) {
        automation={identity(),app::DeskAutomation::Formation,slot!=0,slot?std::optional<int>(slot):std::nullopt};
        auto expected=game;expected.manager_options[size_t(manager)*5]=uint8_t(slot);
        accept([&]{return session.dispatch_automation(automation,revision,error);},expected,
               "typed formation selects an available outdoor slot explicitly or disables it");
    }
    for(auto slot: {std::optional<int>{},std::optional<int>{0},std::optional<int>{2},std::optional<int>{9}}) {
        automation={identity(),app::DeskAutomation::Formation,true,slot};
        reject([&]{return session.dispatch_automation(automation,revision,error);},"formation on requires an available saved slot1..8");
    }
    game.indoor=true;
    automation={identity(),app::DeskAutomation::Formation,true,3};
    reject([&]{return session.dispatch_automation(automation,revision,error);},"indoor formation uses its own Native mask");
    automation={identity(),app::DeskAutomation::Formation,true,2};
    auto expected=game;expected.manager_options[size_t(manager)*5]=2;
    accept([&]{return session.dispatch_automation(automation,revision,error);},expected,"indoor saved slot2 is available");
    automation={identity(),app::DeskAutomation::Formation,false,2};
    reject([&]{return session.dispatch_automation(automation,revision,error);},"formation off cannot carry a selected slot");
    game.indoor=false;
    game.manager_options[size_t(manager)*5]=9;
    automation={identity(),app::DeskAutomation::Formation,false,std::nullopt};
    reject([&]{return session.dispatch_automation(automation,revision,error);},"malformed current formation is rejected neutrally");
    game.manager_options[size_t(manager)*5]=0;
    // A consistent controlled national owner verifies the exact357 identity domain.
    const int original_club=game.manager_teams[size_t(manager)];
    const auto original_competitions=game.memory.u32(::bmh::layout::CompetitionSelection);
    const auto original_squad=game.squad_slots;
    game.memory.set32(::bmh::layout::CompetitionSelection,2);
    for(int row=0;row<24;++row)
        game.squad_slots[3072+size_t(row)]=uint16_t(row|(row<11?0x8000:row<16?0x4000:0));
    game.manager_teams[size_t(manager)]=kNoTeam;
    game.manager_records[rec+0x290]=uint8_t(kNoTeam);game.manager_records[rec+0x291]=uint8_t(kNoTeam>>8);
    const auto national_desk=session.desktop_projection(revision);
    check(national_desk.national_team&&national_desk.runtime_club==kNoTeam&&
          national_desk.managers.at(size_t(manager)).club_identity_key==*identity().stable_club_identity,
          "national357 stable identity matches the actual production desk projection");
    price={identity(),12};
    reject([&]{return session.dispatch_ticket_price(price,revision,error);},"national owner cannot change admission");
    automation={identity(),app::DeskAutomation::Training,true,std::nullopt};
    reject([&]{return session.dispatch_automation(automation,revision,error);},"national owner cannot enable training");
    for(int index=0;index<4;++index) {
        automation={identity(),static_cast<app::DeskAutomation>(index),true,index==0?std::optional<int>(3):std::nullopt};
        expected=game;expected.manager_options[size_t(manager)*5+size_t(index)]=index==0?3:uint8_t(game.manager_options[size_t(manager)*5+size_t(index)]|1);
        accept([&]{return session.dispatch_automation(automation,revision,error);},expected,
               "national formation, substitution, tactics and marking remain allowed");
    }
    game.manager_teams[size_t(manager)]=original_club;
    game.manager_records[rec+0x290]=uint8_t(original_club);game.manager_records[rec+0x291]=uint8_t(original_club>>8);
    game.memory.set32(::bmh::layout::CompetitionSelection,original_competitions);game.squad_slots=original_squad;
    const uint64_t before_overflow=revision;revision=UINT64_MAX;price={identity(),12};
    reject([&]{return session.dispatch_ticket_price(price,revision,error);},"revision overflow is rejected before Native mutation");
    revision=before_overflow;
    const auto before_pairings=session.league_pairings_checkpoint();
    const auto page=session.open_league_pairings(DeskRequest{DeskRequestKind::open_menu,manager,0,1,-1,revision},revision,error);
    check(page.has_value(),"actual Source pairings draft opens for phase ownership test");
    price={identity(),12};
    reject([&]{return session.dispatch_ticket_price(price,revision,error);},"typed setter cannot mutate a staged pairings owner");
    automation={identity(),app::DeskAutomation::Substitution,false,std::nullopt};
    reject([&]{return session.dispatch_automation(automation,revision,error);},"automation obeys the same staged owner boundary");
    session.restore_league_pairings_checkpoint(before_pairings);
    // Genuine quote ownership excludes unrelated typed writes. A neutral legacy
    // periphery scene remains usable because the console has no close command.
    StadiumRequest stadium{StadiumRequestKind::open,manager,original_club,-1,revision};
    check(session.dispatch_stadium(stadium,revision,error),"actual legacy stadium scene opens for ownership test");
    const auto neutral_stadium=session.stadium_checkpoint();
    const auto neutral_revision=revision;
    int offered=-1;
    for(int object=0;object<14;++object) {
        stadium={StadiumRequestKind::quote_periphery,manager,original_club,object,revision};
        if(session.dispatch_stadium(stadium,revision,error)) {offered=object;break;}
    }
    check(offered>=0,"actual Source periphery quote is available in the initialized career");
    const auto quote=session.preparation_projection(revision).stadium;
    check(quote&&quote->quoted_object==offered&&quote->quoted_price>0,"projection binds the owned live periphery quote");
    price={identity(),12};
    reject([&]{return session.dispatch_ticket_price(price,revision,error);},"owned periphery quote blocks unrelated admission writes");
    automation={identity(),app::DeskAutomation::Substitution,false,std::nullopt};
    reject([&]{return session.dispatch_automation(automation,revision,error);},"owned periphery quote blocks unrelated automation writes");
    stadium={StadiumRequestKind::decline_periphery,manager,original_club,offered,revision,quote->quoted_price};
    check(session.dispatch_stadium(stadium,revision,error),"genuine decline consumes the quote but keeps neutral legacy scene");
    automation={identity(),app::DeskAutomation::Substitution,false,std::nullopt};
    expected=game;expected.manager_options[size_t(manager)*5+1]&=uint8_t(~1u);
    accept([&]{return session.dispatch_automation(automation,revision,error);},expected,
           "typed setting works after legacy quote decline without a nonexistent console close");
    session.restore_stadium_checkpoint(neutral_stadium);revision=neutral_revision;
    const auto ownership_checkpoint=session.stadium_checkpoint();
    stadium={StadiumRequestKind::select_investment,manager,original_club,1,revision};
    stadium.stable_club_identity=identity().stable_club_identity;
    stadium.scope=neutral_stadium.stadium.scope;stadium.sequence=neutral_stadium.stadium.sequence;
    check(session.dispatch_stadium(stadium,revision,error),"actual investment selection establishes its modal owner");
    price={identity(),12};
    reject([&]{return session.dispatch_ticket_price(price,revision,error);},"owned investment selection blocks an unrelated typed setter");
    const auto selected=session.preparation_projection(revision).stadium;
    check(selected&&!selected->investments[1].options.empty(),"selected investment exposes authentic tier options");
    stadium={StadiumRequestKind::select_investment_option,manager,original_club,1,revision,
             int32_t(selected->investments[1].options.front().base_price),0};
    stadium.stable_club_identity=identity().stable_club_identity;
    stadium.scope=selected->scope;stadium.sequence=selected->sequence;
    check(session.dispatch_stadium(stadium,revision,error),"actual investment tier selection is accepted");
    stadium.kind=StadiumRequestKind::confirm_investment;stadium.revision=revision;
    stadium.sequence=session.preparation_projection(revision).stadium->sequence;
    check(session.dispatch_stadium(stadium,revision,error),"actual immutable construction tip owns the next answer");
    automation={identity(),app::DeskAutomation::Substitution,false,std::nullopt};
    reject([&]{return session.dispatch_automation(automation,revision,error);},"owned construction tip blocks unrelated automation");
    session.restore_stadium_checkpoint(ownership_checkpoint);revision=neutral_revision;
    const auto directory=std::filesystem::temp_directory_path()/("openbmh-semantic-settings-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    check(session.save_game(directory/"SETTINGS.FBM",manager,false,error),"actual accepted settings save through production serialization");
    app::CareerSession loaded(data,0x12345678,directory/"SETTINGS.FBM");
    check(save_stream(loaded.state())==save_stream(game)&&loaded.rand().calls()==0&&loaded.game().calls()==0,
          "fresh production CareerSession load preserves every Native byte with no RNG draws");
    const auto loaded_desk=loaded.desktop_projection(revision);
    check(loaded_desk.ticket_price==17&&loaded_desk.automation[0]==3,
          "fresh loaded projection contains accepted admission and explicit formation");
    std::filesystem::remove_all(directory);
}

void check_desk_widgets(const std::filesystem::path& data) {
    StartupFlow flow(data);
    flow.click(150, 215); flow.click(5, 225, true); flow.click(5, 225, true);
    flow.click(90, 169);
    auto session = std::make_unique<app::CareerSession>(data, 0x12345678);
    std::string error;
    check(session->confirm_manager(flow.pending_career()->options, flow.pending_career()->managers[0], error),
          "widget fixture confirms an actual manager");
    CareerInitializationResult init;
    check(session->finish(init, error), "widget fixture reaches a real career desk");
    uint64_t revision = init.desktop.revision;
    auto& game = session->state();
    // A valid saved menu layout exposing the two already implemented drawers.
    // The default layout keeps the automation drawer in a folder.
    game.memory.set8(::bmh::layout::Desktops + 5, 1);
    for (int i = 1; i <= 2; ++i) {
        const uint32_t at = ::bmh::layout::Desktops + uint32_t(i) * 16;
        game.memory.set16(at, 20);
        game.memory.set8(at + 2, uint8_t(i == 1 ? 80 : 130));
        game.memory.set8(at + 3, uint8_t(i == 1 ? 98 : 173));
        game.memory.set8(at + 4, uint8_t(i == 1 ? 36 : 38));
        game.memory.set8(at + 5, uint8_t(i == 1 ? 2 : 0));
        game.memory.set8(at + 12, uint8_t(i == 1 ? 55 : 64));
        game.memory.set8(at + 13, 0x80);
    }
    init.desktop = session->desktop_projection(revision);
    init.preparation = session->preparation_projection(revision);
    auto preparation = init.preparation;
    flow.attach_initialized_career(std::move(init));
    flow.attach_preparation_context(std::move(preparation));
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    const auto execute = [&](const FlowResult& request) {
        check(request.request == FlowRequest::desk_action, "drawer click routes an engine desk action");
        check(app::serve_flow_request(flow, request, session, data, data, 0x12345678,
                                     revision, playback, audio_session, false).empty(),
              "production controller acknowledges a desk widget");
        check(!flow.desktop()->pending(), "desk widget ACK clears its pending request");
    };
    const auto records = game.manager_records;
    const auto options = game.manager_options;
    const auto board = game.tactics;
    const auto squad = game.squad_slots;
    const auto date = game.date;
    const auto gs = session->game().seed();
    const auto ws = session->rand().seed();
    const auto gc = session->game().calls(), wc = session->rand().calls();
    const auto rev = revision;
    const auto price = flow.click(20 + 4 + 86, 90);
    check(price.desk_action.value == 25, "rightmost original price hitbox maps to 25");
    check(flow.click(20 + 4 + 35, 90).request == FlowRequest::none, "pending widget blocks a duplicate");
    execute(price);
    auto changed_records = records;
    changed_records[0x26e] = 25;
    check(game.manager_records == changed_records && game.manager_options == options && revision == rev + 1,
          "price changes only the active manager's admission byte");
    execute(flow.click(20 + 4 + 35, 90));
    check(game.manager_records[0x26e] == 8, "leftmost original price hitbox maps to 8");
    check(flow.click(20 + 4 + 34, 90).request == FlowRequest::none &&
          flow.click(20 + 4 + 87, 90).request == FlowRequest::none,
          "outside price endpoints remain read-only");
    for (int index : {1, 2, 3, 4}) {
        const auto before = game.manager_options;
        execute(flow.click(20 + 4 + index * 33 + 10, 140));
        auto expected = before;
        expected[size_t(index)] ^= 1;
        check(game.manager_options == expected, "automation toggles only its Original bit including training high flags");
    }
    const auto masks = std::pair{game.manager_records[0x218], game.manager_records[0x219]};
    game.manager_records[0x218] = 0x84; // Outdoor: only formations 3 and 8.
    game.manager_records[0x219] = 0x02; // Indoor: only formation 2.
    game.manager_options[0] = 0;
    for (int value : {3, 8, 0}) {
        execute(flow.click(20 + 4 + 10, 140));
        check(game.manager_options[0] == value, "outdoor automation skips unavailable stored formations and wraps to off");
    }
    game.indoor = true;
    for (int value : {2, 0}) {
        execute(flow.click(20 + 4 + 10, 140));
        check(game.manager_options[0] == value, "indoor automation uses the separate saved formation mask");
    }
    game.indoor = false;
    game.manager_records[0x218] = masks.first;
    game.manager_records[0x219] = masks.second;
    const auto unchanged = [&](const DeskRequest& request) {
        const auto before_records = game.manager_records;
        const auto before_options = game.manager_options;
        const auto before_revision = revision;
        check(!session->dispatch_desk_widget(request, revision, error) &&
              game.manager_records == before_records && game.manager_options == before_options &&
              revision == before_revision, "invalid desk widget rejects before all state mutation");
    };
    DeskRequest request{DeskRequestKind::change_option, 0, 2, 64, 1, revision};
    for (int variant = 0; variant < 7; ++variant) {
        auto invalid = request;
        if (variant == 0) --invalid.revision;
        if (variant == 1) invalid.manager_slot = 1;
        if (variant == 2) invalid.item_id = 53;
        if (variant == 3) invalid.menu_kind = 55;
        if (variant == 4) invalid.value = 5;
        if (variant == 5) invalid.value = -1;
        if (variant == 6) invalid.kind = DeskRequestKind::change_layout;
        unchanged(invalid);
    }
    for (int value : {7, 26}) unchanged(DeskRequest{DeskRequestKind::change_ticket_price, 0, 1, 55, value, revision});
    game.memory.set8(::bmh::layout::Desktops + 2 * 16 + 13, 0);
    unchanged(request);
    game.memory.set8(::bmh::layout::Desktops + 2 * 16 + 13, 0x80);
    game.memory.set16(::bmh::layout::Desktops + 2 * 16, 20 | 0x1000);
    unchanged(request);
    game.memory.set16(::bmh::layout::Desktops + 2 * 16, 20);
    game.manager_options[0] = 9;
    request.value = 0; unchanged(request);
    game.manager_options[0] = 0;
    // National coaches keep lineup/attitude/marking automation, while admission
    // and training controls are unavailable in legacy routine/legacy routine.
    game.manager_teams[0] = kNoTeam;
    game.manager_records[0x290] = uint8_t(kNoTeam);
    game.manager_records[0x291] = uint8_t(kNoTeam >> 8);
    game.memory.set32(::bmh::layout::CompetitionSelection, 2);
    for (int row = 0; row < 24; ++row)
        game.squad_slots[3072 + size_t(row)] = uint16_t(row | (row < 11 ? 0x8000 : row < 16 ? 0x4000 : 0));
    const auto national = session->desktop_projection(revision);
    check(national.national_team, "widget fixture projects a national coach");
    DesktopScreen national_screen(data, national);
    check(national_screen.click(20 + 4 + 35, 90).kind == DeskRequestKind::none,
          "national admission control does not emit a price request");
    unchanged(DeskRequest{DeskRequestKind::change_ticket_price, 0, 1, 55, 10, revision});
    request.value = 4;
    unchanged(request);
    game.manager_teams[0] = flow.desktop()->state().runtime_club;
    game.manager_records[0x290] = uint8_t(game.manager_teams[0]);
    game.manager_records[0x291] = uint8_t(game.manager_teams[0] >> 8);
    game.memory.set32(::bmh::layout::CompetitionSelection, 1);
    game.squad_slots = squad;
    check(session->game().seed() == gs && session->rand().seed() == ws &&
          session->game().calls() == gc && session->rand().calls() == wc &&
          game.date == date && game.tactics == board && game.squad_slots == squad,
          "every drawer action preserves both RNG streams, date, board and squad");
}

void check_league_only_national_coach_handoff(const std::filesystem::path& data) {
    StartupFlow flow(data);
    flow.click(150, 215); // League-only setup; world competition remains disabled.
    flow.click(5, 225, true);
    flow.click(5, 225, true);
    const auto started = flow.click(90, 169);
    check(started.request == FlowRequest::new_career, "league-only coach fixture requests a new career");

    app::CareerSession session(data, 0x4c756e61);
    std::string error;
    const auto& career = *flow.pending_career();
    check(session.confirm_manager(career.options, career.managers[0], error),
          "league-only coach fixture confirms one manager");
    CareerInitializationResult initialized;
    check(session.finish(initialized, error), "league-only coach fixture reaches its first desk");
    const auto& preparation_desk = initialized.preparation.desktop;
    check(initialized.desktop.next_competition == preparation_desk.next_competition &&
          initialized.desktop.next_opponent == preparation_desk.next_opponent &&
          initialized.desktop.next_competition_label == preparation_desk.next_competition_label &&
          initialized.desktop.next_competition_detail == preparation_desk.next_competition_detail &&
          initialized.desktop.next_opponent_name == preparation_desk.next_opponent_name &&
          initialized.desktop.next_opponent_badge == preparation_desk.next_opponent_badge &&
          initialized.desktop.next_opponent_badge_fallback == preparation_desk.next_opponent_badge_fallback,
          "fresh mode-I career desk and preparation projection share the current driver fixture context");
    uint64_t revision = initialized.desktop.revision;
    check(initialized.desktop.options.league && !initialized.desktop.options.world &&
          initialized.desktop.manager_teams.size() == 1,
          "fixture keeps the ordinary league-only profile");
    auto preparation = initialized.preparation;
    flow.attach_initialized_career(std::move(initialized));
    flow.attach_preparation_context(std::move(preparation));

    auto& game = session.state();
    const int home_team = game.manager_teams.at(0);
    const int home_key = flow.desktop()->state().managers.at(0).club_identity_key;
    national_coach_take_over(game, 1, uint16_t(game.date - game.season_start)); // Source legacy routine temporary assignment.
    check(game.manager_teams.at(0) == kNoTeam && game.memory.u8(::bmh::layout::CoachActive) == 1,
          "Source takeover selects the national side and marks its temporary context");
    ++revision;
    const auto national = session.desktop_projection(revision);
    const auto national_preparation = session.preparation_projection(revision);
    check(national_preparation.lineup.runtime_club == kNoTeam &&
          national_preparation.lineup.players.size() == 24 &&
          !national_preparation.training && !national_preparation.pitch &&
          !national_preparation.bank && !national_preparation.stadium,
          "temporary league-only national coach projects the complete national preparation context");
    check(national.national_team && national.national_coach_takeover &&
          !national.options.world && national.manager_slot == 0,
          "league-only temporary national projection is accepted without changing setup options");
    auto item = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
        [](const DeskItem& candidate) { return candidate.kind == 12 && !candidate.disabled; });
    check(item != flow.desktop()->state().items.end(), "league-only desk exposes its normal advance control");
    auto kickoff = flow.click(item->x + item->width / 2, item->y + item->height / 2);
    check(kickoff.request == FlowRequest::kickoff, "national transition follows a normal desk handoff");
    flow.acknowledge_desk(national);
    check(flow.desktop()->state().national_team &&
          flow.desktop()->state().managers.at(0).club_identity_key == home_key,
          "national ACK preserves the stable home-club identity");

    national_coach_hand_back(game); // Source legacy routine restores the club slot.
    ++revision;
    const auto returned = session.desktop_projection(revision);
    const auto returned_preparation = session.preparation_projection(revision);
    check(returned_preparation.lineup.runtime_club == home_team &&
          returned_preparation.training && returned_preparation.pitch &&
          returned_preparation.bank && returned_preparation.stadium,
          "source handback restores club preparation drawers");
    item = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
        [](const DeskItem& candidate) { return candidate.kind == 12 && !candidate.disabled; });
    check(item != flow.desktop()->state().items.end(), "national desk still exposes its advance control");
    kickoff = flow.click(item->x + item->width / 2, item->y + item->height / 2);
    check(kickoff.request == FlowRequest::kickoff, "club handback follows a normal desk handoff");
    flow.acknowledge_desk(returned);
    check(!flow.desktop()->state().national_team &&
          flow.desktop()->state().runtime_club == home_team &&
          flow.desktop()->state().managers.at(0).club_identity_key == home_key &&
          !flow.desktop()->state().options.world,
          "handback restores the same club without enabling world competition");

    const GameState saved = game;
    game.manager_teams[0] = kNoTeam;
    game.memory.set8(::bmh::layout::CoachActive, 0);
    bool rejected = false;
    try { (void)session.desktop_projection(revision + 1); }
    catch (const FormatError&) { rejected = true; }
    game = saved;
    check(rejected, "team 0x165 without world mode or Source takeover evidence remains rejected");
}

void check_two_manager_national_handoff(const std::filesystem::path& data) {
    StartupFlow flow(data);
    check(flow.click(260, 135).changed && flow.options().managers == 2,
          "two-manager fixture selects two managers in the original setup");
    check(flow.click(150, 215).changed && !flow.options().world && flow.options().league,
          "two-manager fixture keeps league mode and disables world mode");
    flow.click(5, 225, true); // Original new-career entry.
    flow.click(5, 225, true); // Manager choice to confirmation.
    auto first_choice = flow.click(90, 169);
    check(first_choice.request == FlowRequest::manager_confirmed,
          "first manager enters the engine-confirmed multi-manager path");

    std::unique_ptr<app::CareerSession> session;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    uint64_t revision = 0;
    constexpr uint32_t seed = 0x27710102;
    const auto serve = [&](const FlowResult& result) {
        check(app::serve_flow_request(flow, result, session, data, data, seed,
                                      revision, playback, audio_session, false).empty(),
              "production controller accepts the multi-manager setup request");
    };
    serve(first_choice);
    check(flow.screen_name() == "manager_selection", "engine-confirmed second-manager selection is shown");
    flow.click(5, 225, true);
    auto career_request = flow.click(90, 169);
    check(career_request.request == FlowRequest::new_career && flow.pending_career() &&
          flow.pending_career()->managers.size() == 2,
          "two distinct confirmed managers complete new-career setup");
    serve(career_request);
    check(session && flow.screen_name() == "desktop" && session->state().manager_teams.size() == 2,
          "production controller attaches the full two-manager career");

    auto& game = session->state();
    const DeskState home = flow.desktop()->state();
    check(home.manager_teams.size() == 2 && home.manager_teams[0] != home.manager_teams[1] &&
          home.options.league && !home.options.world,
          "league-only desk begins with two different clubs and unchanged competition flags");
    const auto original_options = home.options;
    const auto original_roster = home.managers;
    for (size_t manager = 0; manager < 2; ++manager)
        game.manager_records[manager * 0xb0c + 0x21c] = 0;
    // Source legacy routine recognizes the manager-specific Bundestrainer flag.
    game.manager_records[1 * 0xb0c + 0x21c] = 1;
    national_coach_take_over(game, 2, uint16_t(game.date - game.season_start));
    check(game.manager_teams.size() == 1 && game.manager_teams[0] == kNoTeam &&
          game.memory.u8(::bmh::layout::OriginalManagerCount) == 2 && game.memory.u8(::bmh::layout::CoachActive) == 1,
          "Source takeover temporarily reduces the active manager count and retains its two-manager context");
    const uint64_t national_revision = revision + 1;
    const DeskState national = session->desktop_projection(national_revision);
    check(national.options.managers == 1 && national.options.league && !national.options.world &&
          national.national_coach_takeover && national.national_return_roster.size() == 2,
          "national desk projects one visible coach plus the Source-backed return roster");
    check(national.managers[0].club_identity_key == original_roster[1].club_identity_key &&
          national.managers[0].name == original_roster[1].name &&
          national.national_return_roster[0].club_identity_key == original_roster[0].club_identity_key &&
          national.national_return_roster[1].club_identity_key == original_roster[1].club_identity_key,
          "the actual slot-1 Bundestrainer and both home clubs remain correctly identified");

    const auto national_save = std::filesystem::temp_directory_path() /
        ("openbmh-national-multi-" + std::to_string(session->session_id()) + ".fbm");
    std::string save_error;
    check(session->save_game(national_save, 0, true, save_error),
          "temporary national state saves through the production save path");
    app::CareerSession loaded(data, 0x27710102, national_save);
    const auto loaded_national = loaded.desktop_projection(1);
    check(loaded.state().manager_teams.size() == 1 && loaded.state().manager_teams[0] == kNoTeam &&
          loaded.state().memory.u8(::bmh::layout::OriginalManagerCount) == 2 && loaded.state().memory.u8(::bmh::layout::CoachActive) == 1 &&
          loaded_national.national_coach_takeover && loaded_national.national_return_roster.size() == 2 &&
          loaded_national.managers[0].club_identity_key == original_roster[1].club_identity_key &&
          loaded_national.national_return_roster[0].club_identity_key == original_roster[0].club_identity_key &&
          loaded_national.national_return_roster[1].club_identity_key == original_roster[1].club_identity_key,
          "save/reload re-applies the Source takeover and reconstructs both original home clubs");
    std::error_code remove_error;
    std::filesystem::remove(national_save, remove_error);
    check(!remove_error, "temporary national save fixture is removed");

    auto continue_item = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
        [](const DeskItem& item) { return item.kind == 12 && !item.disabled; });
    check(continue_item != flow.desktop()->state().items.end(), "two-manager club desk has its normal advance control");
    auto advance = flow.click(continue_item->x + continue_item->width / 2,
                              continue_item->y + continue_item->height / 2);
    check(advance.request == FlowRequest::kickoff, "club-to-national ACK is gated by a turn handoff");
    DeskState forged_national = national;
    ++forged_national.national_return_roster[0].club_identity_key;
    bool rejected = false;
    try { flow.acknowledge_desk(forged_national); } catch (const FormatError&) { rejected = true; }
    check(rejected && flow.desktop()->pending(),
          "forged hidden-manager identity cannot consume the pending transition");
    flow.acknowledge_desk(national);
    revision = national_revision;
    check(flow.desktop()->state().managers.size() == 1 &&
          flow.desktop()->state().options.duration == original_options.duration &&
          flow.desktop()->state().options.difficulty == original_options.difficulty &&
          flow.desktop()->state().options.mode == original_options.mode &&
          flow.desktop()->state().options.league == original_options.league &&
          flow.desktop()->state().options.world == original_options.world,
          "temporary desk keeps all career options except Source manager count");

    continue_item = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
        [](const DeskItem& item) { return item.kind == 12 && !item.disabled; });
    check(continue_item != flow.desktop()->state().items.end(), "temporary national desk permits its next turn");
    advance = flow.click(continue_item->x + continue_item->width / 2,
                         continue_item->y + continue_item->height / 2);
    check(advance.request == FlowRequest::kickoff, "national-to-national ACK uses the normal turn request");
    const DeskState next_national = session->desktop_projection(national_revision + 1);
    DeskState forged_hidden_roster = next_national;
    ++forged_hidden_roster.national_return_roster[1].club_identity_key;
    rejected = false;
    try { flow.acknowledge_desk(forged_hidden_roster); } catch (const FormatError&) { rejected = true; }
    check(rejected && flow.desktop()->pending(),
          "national-to-national ACK rejects a changed hidden multi-manager roster");
    flow.acknowledge_desk(next_national);
    revision = national_revision + 1;

    DesktopScreen national_screen(data, next_national);
    const auto lineup_item = std::find_if(next_national.items.begin(), next_national.items.end(),
        [](const DeskItem& item) { return item.kind == 3 && !item.disabled; });
    check(lineup_item != next_national.items.end(), "national desk exposes its ordinary lineup entry");
    check(national_screen.click(lineup_item->x + lineup_item->width / 2,
                                lineup_item->y + lineup_item->height / 2).kind == DeskRequestKind::open_menu,
          "national lineup creates a non-turn desktop request");
    DeskState forged_save_ack = next_national;
    ++forged_save_ack.revision;
    ++forged_save_ack.national_return_roster[0].club_identity_key;
    rejected = false;
    try { national_screen.acknowledge(forged_save_ack); } catch (const FormatError&) { rejected = true; }
    check(rejected && national_screen.pending(),
          "non-turn desktop ACK rejects a changed hidden return roster");

    national_coach_hand_back(game);
    check(game.manager_teams.size() == 2 && game.memory.u8(::bmh::layout::CoachActive) == 0,
          "Source legacy routine restores both original manager slots");
    const uint64_t home_revision = revision + 1;
    const DeskState restored = session->desktop_projection(home_revision);
    continue_item = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
        [](const DeskItem& item) { return item.kind == 12 && !item.disabled; });
    check(continue_item != flow.desktop()->state().items.end(), "national desk has its normal handback control");
    advance = flow.click(continue_item->x + continue_item->width / 2,
                         continue_item->y + continue_item->height / 2);
    check(advance.request == FlowRequest::kickoff, "national-to-club ACK is gated by a turn handoff");
    DeskState forged_return = restored;
    ++forged_return.managers[0].club_identity_key;
    rejected = false;
    try { flow.acknowledge_desk(forged_return); } catch (const FormatError&) { rejected = true; }
    check(rejected && flow.desktop()->pending(),
          "handback to a changed club roster is rejected without consuming the turn request");
    flow.acknowledge_desk(restored);
    check(flow.desktop()->state().manager_teams == home.manager_teams &&
          flow.desktop()->state().options == original_options &&
          flow.desktop()->state().managers.size() == 2 &&
          flow.desktop()->state().managers[0].club_identity_key == original_roster[0].club_identity_key &&
          flow.desktop()->state().managers[1].club_identity_key == original_roster[1].club_identity_key,
          "handback restores the same two clubs in the same order without changing setup options");
}

void check_settings_writer(const std::filesystem::path& data) {
    StartupFlow flow(data);
    flow.click(150, 215); flow.click(5, 225, true); flow.click(5, 225, true);
    auto result = flow.click(90, 169);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    const auto serve = [&](const FlowResult& r) {
        check(app::serve_flow_request(flow, r, session, data, data, 0x76543210,
            revision, playback, audio_session, false).empty(), "settings uses production session/controller");
    };
    serve(result);
    check(session && flow.screen_name() == "desktop", "settings writer fixture reaches a real career desk");
    auto& game = session->state();
    for (size_t i = 0; i < 180; ++i) game.memory.set8(::bmh::layout::LeaguePresentationOptions + uint32_t(i), 0);
    // Keep the desk's already-acknowledged setup projection authoritative:
    // these two bytes feed DeskState::options and must not be rewritten behind
    // the open DesktopScreen merely to make unrelated setting cells available.
    game.memory.set8(::bmh::layout::CareerSession_DispatchDesktopLayout_RuntimeField, 1); // C6: drawer handles hidden until enabled.
    game.memory.set8(::bmh::layout::DeskState_FormatError_RuntimeField3, 1); // E6: shortcut bar is not forced visible.
    game.memory.set8(::bmh::layout::SeasonEntrants_SeasonEntrantsFromGame_TextPointer, 1); // J7: season entrant pages hidden.
    for (uint32_t at : {::bmh::layout::Career_EuropeanClub_ExecutableFieldUnsigned, ::bmh::layout::Career_EuropeanClub_ExecutableField2Unsigned, ::bmh::layout::Career_EuropeanClub_ExecutableField3Unsigned}) {
        if (!game.memory.has(at)) game.memory.add_block(at, 1);
        game.memory.set8(at, 0);
    }
    for (uint32_t at : {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField25Unsigned, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField14Unsigned, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField19Unsigned}) {
        if (!game.memory.has(at)) game.memory.add_block(at, 1);
        if (!game.memory.has(at + 1)) game.memory.add_block(at + 1, 1);
        game.memory.set16(at, 0);
    }
    game.memory.set8(::bmh::layout::MatchEngine_EventImpl_ExecutableField, 0); // K5, consumed by MatchEngine::button(data-field offset).
    game.memory.set8(::bmh::layout::MatchEngine_Side_ExecutableField, 1); // B8, current match-engine setting with CareerSession_DispatchSettings_RuntimeField upper bound 4.
    const auto settings_item = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
        [](const DeskItem& i) { return i.kind == 19 && !i.disabled; });
    check(settings_item != flow.desktop()->state().items.end(), "settings writer fixture has the source menu");
    serve(flow.click(settings_item->x + 10, settings_item->y + 10));
    check(flow.screen_name() == "settings", "settings writer modal opens in production flow");

    const auto flag_game_seed = session->game().seed();
    const auto flag_w_seed = session->rand().seed();
    const auto flag_game_calls = session->game().calls();
    const auto flag_w_calls = session->rand().calls();
    const auto flip_consumed_flag = [&](int row, int column, uint32_t address) {
        const uint8_t old = game.memory.u8(address);
        const uint64_t old_revision = revision;
        const auto click = flow.click(67 + column * 24 + 5, 25 + row * 10 + 5);
        check(click.request == FlowRequest::settings_action &&
              click.settings_action.row == row && click.settings_action.column == column &&
              click.settings_action.operation == SettingsEdit::toggle_binary,
              "consumed display setting maps to its exact Original cell action");
        serve(click);
        const auto feedback=flow.state_json();
        const auto why="accepted consumed display setting changes one source byte and one revision: cell="+
            std::string(1,char('A'+row))+std::to_string(column+1)+" revision="+std::to_string(revision)+
            " expected="+std::to_string(old_revision+1)+" byte="+std::to_string(game.memory.u8(address))+
            " expected="+std::to_string(old^1)+" "+feedback.substr(0,feedback.find(",\"cells\""));
        check(revision == old_revision + 1 && game.memory.u8(address) == uint8_t(old ^ 1), why.c_str());
    };
    flip_consumed_flag(2, 5, ::bmh::layout::CareerSession_DispatchDesktopLayout_RuntimeField);
    check(flow.desktop()->state().drawer_handles == (game.memory.u8(::bmh::layout::CareerSession_DispatchDesktopLayout_RuntimeField) == 0),
          "C6 acknowledgement refreshes the desk drawer-handle projection");
    flip_consumed_flag(3, 5, ::bmh::layout::CareerSession_DispatchDesktopLayout_RuntimeField2);
    check(flow.desktop()->state().layout_snap == (game.memory.u8(::bmh::layout::CareerSession_DispatchDesktopLayout_RuntimeField2) == 0),
          "D6 acknowledgement refreshes the native desktop raster projection");
    flip_consumed_flag(3, 5, ::bmh::layout::CareerSession_DispatchDesktopLayout_RuntimeField2);
    flip_consumed_flag(4, 5, ::bmh::layout::DeskState_FormatError_RuntimeField3);
    check(flow.desktop()->state().shortcut_bar_always == (game.memory.u8(::bmh::layout::DeskState_FormatError_RuntimeField3) == 0),
          "E6 acknowledgement refreshes the always-visible shortcut-bar projection");
    flip_consumed_flag(9, 6, ::bmh::layout::SeasonEntrants_SeasonEntrantsFromGame_TextPointer);
    const auto flag_exe = load_le(asset_path(data, "BMH.exe"));
    const auto flag_texts = load_textd(asset_path(data, "TEXTD.BLK"));
    check(season_entrants_from_game(game, flag_exe, flag_texts).size() == 3,
          "J7 acknowledgement reaches the existing three-page entrant projection");
    flip_consumed_flag(9, 6, ::bmh::layout::SeasonEntrants_SeasonEntrantsFromGame_TextPointer);
    check(season_entrants_from_game(game, flag_exe, flag_texts).empty(),
          "J7 suppresses the existing entrant projection when its Original flag is set");
    // Restore each flag before the next edit: Source B7 off dynamically locks C7.
    flip_consumed_flag(2, 6, ::bmh::layout::Controller_FormatError_RuntimeField2); // C7: existing intro-animation gate.
    flip_consumed_flag(2, 6, ::bmh::layout::Controller_FormatError_RuntimeField2);
    flip_consumed_flag(5, 6, ::bmh::layout::Controller_FormatError_RuntimeField3); // F7: existing fixture-report gate.
    flip_consumed_flag(5, 6, ::bmh::layout::Controller_FormatError_RuntimeField3);
    flip_consumed_flag(1, 6, ::bmh::layout::Controller_FormatError_RuntimeField); // B7: existing post-matchday intro gate.
    flip_consumed_flag(1, 6, ::bmh::layout::Controller_FormatError_RuntimeField);
    flip_consumed_flag(4, 5, ::bmh::layout::DeskState_FormatError_RuntimeField3);
    flip_consumed_flag(2, 5, ::bmh::layout::CareerSession_DispatchDesktopLayout_RuntimeField);
    check(session->game().seed() == flag_game_seed && session->rand().seed() == flag_w_seed &&
          session->game().calls() == flag_game_calls && session->rand().calls() == flag_w_calls,
          "C6/D6/E6/J7 setting edits and ACKs consume neither random stream");

    const auto date = game.date;
    const auto game_seed = session->game().seed();
    const auto watcom_seed = session->rand().seed();
    const auto game_calls = session->game().calls();
    const auto watcom_calls = session->rand().calls();
    const GameState before = game;
    const uint64_t before_revision = revision;
    const auto toggle = flow.click(67 + 4 * 24 + 12, 24 + 10 * 10 + 5); // K5: one click, no hold semantics.
    check(toggle.request == FlowRequest::settings_action && toggle.settings_action.row == 10 &&
          toggle.settings_action.column == 4 && toggle.settings_action.operation == SettingsEdit::toggle_binary &&
          flow.pending_settings() && game.memory.u8(::bmh::layout::MatchEngine_EventImpl_ExecutableField) == 0,
          "cell click creates one pending source-shaped action without mutating state");
    auto forged = toggle; forged.settings_action.expected_raw[0] ^= 1;
    serve(forged);
    check(flow.pending_settings() && revision == before_revision && save_stream(game) == save_stream(before),
          "forged action identity leaves the pending request and memory untouched");
    auto wrong_tail = toggle.settings_action;
    wrong_tail.revision = revision;
    wrong_tail.expected_raw[179] ^= 1;
    const auto tail_before = save_stream(game);
    std::string tail_error;
    check(!session->dispatch_settings(wrong_tail, revision, tail_error) && revision == before_revision &&
          save_stream(game) == tail_before,
          "stale last byte of the full 180-cell snapshot rejects without a write");
    auto forged_ack = settings_from_game(game, toggle.settings_action.manager_slot, revision + 1,
        load_le(ui::asset_path(data, "BMH.exe")));
    forged_ack.cells[size_t(toggle.settings_action.row) * 9 + toggle.settings_action.column] ^= 1;
    auto ack_desk = session->desktop_projection(revision + 1);
    auto ack_preparation = session->preparation_projection(revision + 1);
    ++ack_preparation.desktop.season_day;
    bool bad_ack_rejected = false;
    try { flow.acknowledge_settings(std::move(forged_ack), std::move(ack_desk), std::move(ack_preparation)); }
    catch (const FormatError&) { bad_ack_rejected = true; }
    check(bad_ack_rejected && flow.pending_settings() && revision == before_revision &&
          save_stream(game) == tail_before,
          "forged preparation context cannot consume a pending settings ACK");
    serve(toggle);
    auto expected = before;
    expected.memory.set8(::bmh::layout::MatchEngine_EventImpl_ExecutableField, 1);
    check(revision == before_revision + 1 && save_stream(game) == save_stream(expected) &&
          flow.screen_name() == "settings" && !flow.pending_settings() &&
          flow.desktop()->state().revision == revision &&
          session->game().seed() == game_seed && session->rand().seed() == watcom_seed &&
          session->game().calls() == game_calls && session->rand().calls() == watcom_calls && game.date == date,
          "accepted click changes exactly its consumed save byte and ACK revision; no RNG/date/exit effect");

    const auto numeric = flow.click(67 + 7 * 24 + 5, 25 + 1 * 10 + 5); // left half of B8 increments once.
    check(numeric.request == FlowRequest::settings_action && numeric.settings_action.row == 1 &&
          numeric.settings_action.column == 7 && numeric.settings_action.operation == SettingsEdit::increment,
          "B8 click maps to one source-supported increment request");
    serve(numeric);
    expected.memory.set8(::bmh::layout::MatchEngine_Side_ExecutableField, 2);
    check(revision == before_revision + 2 && save_stream(game) == save_stream(expected) &&
          session->game().seed() == game_seed && session->rand().seed() == watcom_seed &&
          session->game().calls() == game_calls && session->rand().calls() == watcom_calls && game.date == date,
          "supported numeric step changes exactly B8 under the source table, without RNG/date changes");
    const auto i9_plus = flow.click(67 + 8 * 24 + 5, 25 + 8 * 10 + 5);
    check(i9_plus.request == FlowRequest::settings_action && i9_plus.settings_action.row == 8 &&
          i9_plus.settings_action.column == 8 && i9_plus.settings_action.operation == SettingsEdit::increment,
          "I9 maps to the Original last-column increment action");
    serve(i9_plus);
    expected.memory.set8(::bmh::layout::CupDraw_FormatError_RuntimeField, 2); // I9, raw value displayed at half scale.
    check(revision == before_revision + 3 && save_stream(game) == save_stream(expected) &&
          session->game().seed() == game_seed && session->rand().seed() == watcom_seed &&
          session->game().calls() == game_calls && session->rand().calls() == watcom_calls && game.date == date,
          "I9 increment changes the exact source byte by two without RNG/date changes");
    const auto i9_minus = flow.click(67 + 8 * 24 + 15, 25 + 8 * 10 + 5);
    check(i9_minus.request == FlowRequest::settings_action &&
          i9_minus.settings_action.operation == SettingsEdit::decrement,
          "I9 right half maps to the Original decrement action");
    serve(i9_minus);
    expected.memory.set8(::bmh::layout::CupDraw_FormatError_RuntimeField, 0);
    check(revision == before_revision + 4 && save_stream(game) == save_stream(expected),
          "I9 decrement returns to zero in the exact two-unit Source step");
    serve(toggle);
    check(revision == before_revision + 4 && save_stream(game) == save_stream(expected),
          "duplicate consumed settings response is inert");
    std::string error;
    auto stale = toggle.settings_action;
    check(!session->dispatch_settings(stale, revision, error) && revision == before_revision + 4 &&
          save_stream(game) == save_stream(expected), "stale settings revision is rejected without writes");
    auto wrong_manager = toggle.settings_action;
    wrong_manager.revision = revision;
    wrong_manager.manager_slot = 1;
    for (size_t i = 0; i < wrong_manager.expected_raw.size(); ++i)
        wrong_manager.expected_raw[i] = game.memory.u8(::bmh::layout::LeaguePresentationOptions + uint32_t(i));
    check(!session->dispatch_settings(wrong_manager, revision, error) && revision == before_revision + 4 &&
          save_stream(game) == save_stream(expected), "inactive/nonexistent manager is rejected without writes");
    auto wrong_operation = toggle.settings_action;
    wrong_operation.revision = revision;
    wrong_operation.expected_raw = {};
    for (size_t i = 0; i < wrong_operation.expected_raw.size(); ++i)
        wrong_operation.expected_raw[i] = game.memory.u8(::bmh::layout::LeaguePresentationOptions + uint32_t(i));
    wrong_operation.operation = SettingsEdit::increment; // K5 is binary.
    check(!session->dispatch_settings(wrong_operation, revision, error) &&
          revision == before_revision + 4 && save_stream(game) == save_stream(expected),
          "operation mismatch is rejected without changing memory or revision");

    const auto unsupported = flow.click(67 + 8 * 24 + 5, 25 + 9 * 10 + 5); // J9, no current Source presentation consumer.
    check(unsupported.request == FlowRequest::settings_action &&
          unsupported.settings_action.row == 9 && unsupported.settings_action.operation == SettingsEdit::increment,
          "left half maps to one increment action; no implicit held-step loop");
    serve(unsupported);
    check(!flow.pending_settings() && revision == before_revision + 4 && save_stream(game) == save_stream(expected),
          "unconsumed Original numeric field is safely rejected");
    const auto unsupported_minus = flow.click(67 + 8 * 24 + 15, 25 + 9 * 10 + 5);
    check(unsupported_minus.request == FlowRequest::settings_action &&
          unsupported_minus.settings_action.operation == SettingsEdit::decrement,
          "right half maps to one decrement action");
    serve(unsupported_minus);
    check(revision == before_revision + 4 && save_stream(game) == save_stream(expected),
          "unconsumed minus action leaves the exact state unchanged");

    const auto unsupported_o3 = flow.click(67 + 2 * 24 + 5, 25 + 14 * 10 + 5);
    check(unsupported_o3.request == FlowRequest::settings_action &&
          unsupported_o3.settings_action.row == 14 && unsupported_o3.settings_action.column == 2,
          "O3 maps to its exact grid identity");
    serve(unsupported_o3);
    check(revision == before_revision + 4 && save_stream(game) == save_stream(expected),
          "unconsumed middle rows/columns are rejected by the bounded source whitelist");

    // Exact Q5 date boundary: the Original handler blocks inclusive days 110..230.
    for (size_t i = 0; i < 180; ++i) game.memory.set8(::bmh::layout::LeaguePresentationOptions + uint32_t(i), 0);
    game.memory.set8(::bmh::layout::GameMode, 3); game.memory.set32(::bmh::layout::CompetitionSelection, 3);
    game.date = game.season_start + 110;
    const auto q5_request = [&] {
        ui::SettingsWriteRequest r;
        r.manager_slot = int(game.memory.u8(::bmh::layout::ActiveManager)); r.revision = revision;
        for (size_t i = 0; i < r.expected_raw.size(); ++i) r.expected_raw[i] = game.memory.u8(::bmh::layout::LeaguePresentationOptions + uint32_t(i));
        r.row = 16; r.column = 4; r.operation = SettingsEdit::toggle_binary; return r;
    };
    auto q5 = q5_request();
    const auto q5_before = save_stream(game);
    check(!session->dispatch_settings(q5, revision, error) && revision == before_revision + 4 &&
          save_stream(game) == q5_before && game.date == game.season_start + 110,
          "Q5 is rejected on day 110 with no memory/revision/date change");
    game.date = game.season_start + 230;
    q5 = q5_request();
    const auto q5_end_before = save_stream(game);
    check(!session->dispatch_settings(q5, revision, error) && revision == before_revision + 4 &&
          save_stream(game) == q5_end_before,
          "Q5 is also rejected on inclusive day 230");

    game.memory.set8(::bmh::layout::GameMode, 1); // B5 is dynamically locked outside mode III.
    ui::SettingsWriteRequest locked{};
    locked.manager_slot = int(game.memory.u8(::bmh::layout::ActiveManager)); locked.revision = revision;
    for (size_t i = 0; i < locked.expected_raw.size(); ++i) locked.expected_raw[i] = game.memory.u8(::bmh::layout::LeaguePresentationOptions + uint32_t(i));
    locked.row = 1; locked.column = 4; locked.operation = SettingsEdit::toggle_binary;
    const auto lock_before = save_stream(game);
    check(!session->dispatch_settings(locked, revision, error) && revision == before_revision + 4 &&
          save_stream(game) == lock_before,
          "recomputed Original mode lock rejects B5 without a saved write");

    game.memory.set8(::bmh::layout::GameMode, 3);
    game.memory.set8(::bmh::layout::MatchEngine_Side_ExecutableField, 4); // B8 upper bound from CareerSession_DispatchSettings_RuntimeField row B.
    ui::SettingsWriteRequest at_limit{};
    at_limit.manager_slot = int(game.memory.u8(::bmh::layout::ActiveManager)); at_limit.revision = revision;
    for (size_t i = 0; i < at_limit.expected_raw.size(); ++i) at_limit.expected_raw[i] = game.memory.u8(::bmh::layout::LeaguePresentationOptions + uint32_t(i));
    at_limit.row = 1; at_limit.column = 7; at_limit.operation = SettingsEdit::increment;
    const auto limit_before = save_stream(game);
    check(!session->dispatch_settings(at_limit, revision, error) && revision == before_revision + 4 &&
          save_stream(game) == limit_before,
          "source table upper bound is a no-op, with no write or revision bump");

    game.memory.set8(::bmh::layout::MatchEngine_EventImpl_ExecutableField, 0x80); // projected lock bit may be stale; raw source byte is not writable.
    ui::SettingsWriteRequest stale_lock{};
    stale_lock.manager_slot = int(game.memory.u8(::bmh::layout::ActiveManager)); stale_lock.revision = revision;
    for (size_t i = 0; i < stale_lock.expected_raw.size(); ++i)
        stale_lock.expected_raw[i] = game.memory.u8(::bmh::layout::LeaguePresentationOptions + uint32_t(i));
    stale_lock.row = 10; stale_lock.column = 4; stale_lock.operation = SettingsEdit::toggle_binary;
    const auto stale_lock_before = save_stream(game);
    check(!session->dispatch_settings(stale_lock, revision, error) && revision == before_revision + 4 &&
          save_stream(game) == stale_lock_before,
          "recomputed unlocked display does not authorize writing a raw cell still >=2");

    ui::SettingsWriteRequest overflow{};
    overflow.manager_slot = int(game.memory.u8(::bmh::layout::ActiveManager)); overflow.revision = std::numeric_limits<uint64_t>::max();
    for (size_t i = 0; i < overflow.expected_raw.size(); ++i)
        overflow.expected_raw[i] = game.memory.u8(::bmh::layout::LeaguePresentationOptions + uint32_t(i));
    overflow.row = 10; overflow.column = 4; overflow.operation = SettingsEdit::toggle_binary;
    uint64_t max_revision = std::numeric_limits<uint64_t>::max();
    const auto overflow_before = save_stream(game);
    check(!session->dispatch_settings(overflow, max_revision, error) &&
          max_revision == std::numeric_limits<uint64_t>::max() && save_stream(game) == overflow_before,
          "revision UINT64_MAX cannot wrap or write");

    // The Source's ninth column retains its fixed ±2 rule, including odd
    // values. J9 below stays gated because its consumer is not yet ported.
    app::CareerSession i9_session(data, 0x4d323849);
    i9_session.state() = before; // Copy the initialized career, not the empty session shell.
    auto& i9_game = i9_session.state();
    const int i9_manager = int(i9_game.memory.u8(::bmh::layout::ActiveManager));
    uint64_t i9_revision = 1;
    const uint32_t i9_address = ::bmh::layout::LeaguePresentationOptions + 8 * 9 + 8;
    const auto i9_request = [&](SettingsEdit operation) {
        ui::SettingsWriteRequest request{};
        request.manager_slot = i9_manager; request.revision = i9_revision;
        for (size_t i = 0; i < request.expected_raw.size(); ++i)
            request.expected_raw[i] = i9_game.memory.u8(::bmh::layout::LeaguePresentationOptions + uint32_t(i));
        request.row = 8; request.column = 8; request.operation = operation;
        return request;
    };
    i9_game.memory.set8(i9_address, 197);
    auto odd_plus = i9_request(SettingsEdit::increment);
    check(i9_session.dispatch_settings(odd_plus, i9_revision, error) &&
          i9_revision == 2 && i9_game.memory.u8(i9_address) == 199,
          "Source I9 increment adds two and preserves an odd raw value below 198");
    auto odd_minus = i9_request(SettingsEdit::decrement);
    check(i9_session.dispatch_settings(odd_minus, i9_revision, error) &&
          i9_revision == 3 && i9_game.memory.u8(i9_address) == 197,
          "Source I9 decrement subtracts two without normalizing odd values");
    i9_game.memory.set8(i9_address, 1);
    auto at_minimum = i9_request(SettingsEdit::decrement);
    const auto minimum_before = save_stream(i9_game);
    check(!i9_session.dispatch_settings(at_minimum, i9_revision, error) &&
          i9_revision == 3 && save_stream(i9_game) == minimum_before,
          "Source I9 decrement is a no-op at raw value one");
    i9_game.memory.set8(i9_address, 198);
    auto at_maximum = i9_request(SettingsEdit::increment);
    const auto maximum_before = save_stream(i9_game);
    check(!i9_session.dispatch_settings(at_maximum, i9_revision, error) &&
          i9_revision == 3 && save_stream(i9_game) == maximum_before,
          "Source I9 increment is a no-op at raw value 0xc6");
    auto other_column9 = i9_request(SettingsEdit::increment);
    other_column9.row = 9;
    for (size_t i = 0; i < other_column9.expected_raw.size(); ++i)
        other_column9.expected_raw[i] = i9_game.memory.u8(::bmh::layout::LeaguePresentationOptions + uint32_t(i));
    check(!i9_session.dispatch_settings(other_column9, i9_revision, error) && i9_revision == 3,
          "other ninth-column cells remain unavailable until their consumer is ported");
}

void check_live_delay_settings_writer(const std::filesystem::path& data,const std::filesystem::path& save_path) {
    app::CareerSession session(data,8128,save_path);auto& game=session.state();
    const int manager=int(game.memory.u8(::bmh::layout::ActiveManager));uint64_t revision=1;std::string error;
    const auto exe=load_le(ui::asset_path(data,"BMH.exe"));const auto w=session.rand();const auto g=session.game();
    const std::array<std::pair<int,int>,13> cells{{{3,7},{4,7},{5,7},{6,7},{1,8},{2,8},{3,8},{4,8},{5,8},{6,8},{10,8},{12,8},{15,8}}};
    for(const auto [row,col]:cells) {
        auto state=settings_from_game(game,manager,revision,exe);const size_t index=size_t(row*9+col);
        const auto old=state.cells[index];check(old<200,"real WEEK196 exposes the consumed speed/delay cell");
        const int limit=col==7?exe.u8(::bmh::layout::CareerSession_DispatchSettings_RuntimeField+uint32_t(row)):198;
        const bool increase=old<limit;const int delta=(col==8?2:1)*(increase?1:-1);
        check(increase||old>(col==8?1:0),"real cell supports one legal Source edit without a fixture setter");
        SettingsScreen screen(data,state);const auto pixels=screen.render().pixels;
        check(screen.click(67+col*24+(increase?5:15),25+row*10+5)&&screen.pending(),
              "Original numeric cell creates the correct one-step UI request");
        const auto request=*screen.pending();const auto before=game;const auto old_revision=revision;
        const auto accepted=session.dispatch_settings(request,revision,error);
        const auto name=std::string(1,char('A'+row))+std::to_string(col+1);
        const auto why="consumed Original "+name+" is writable: "+error;
        check(accepted,why.c_str());
        auto expected=before;expected.memory.set8(::bmh::layout::LeaguePresentationOptions+uint32_t(index),uint8_t(int(old)+delta));
        check(revision==old_revision+1&&save_stream(game)==save_stream(expected)&&
              session.rand().seed()==w.seed()&&session.rand().calls()==w.calls()&&
              session.game().seed()==g.seed()&&session.game().calls()==g.calls(),
              "speed/delay edit changes exactly its Source byte/revision and neither RNG stream");
        screen.acknowledge(settings_from_game(game,manager,revision,exe));
        check(screen.render().pixels!=pixels&&!screen.pending(),"accepted Source numeric value reaches the Original cell renderer");
        const auto native=save_stream(game);
        check(!session.dispatch_settings(request,revision,error)&&save_stream(game)==native,
              "replayed delay request cannot apply a second edit");
    }
}

void check_settings_refusal_controller(const std::filesystem::path& data,
        const std::filesystem::path& input) {
    const auto saves=std::filesystem::temp_directory_path()/
        ("openbmh-settings-notice-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(saves);
    std::filesystem::copy_file(input,saves/"AUTOSAVE.FBM");
    std::unique_ptr<app::CareerSession> session;
    StartupFlow flow(data,saves);uint64_t revision=0;
    openbmh::audio::Playback playback;std::optional<uint64_t> audio_session;
    const auto serve=[&](const FlowResult& r) {
        check(app::serve_flow_request(flow,r,session,data,saves,8128,revision,playback,audio_session,false).empty(),
              "genuine Settings refusal uses the App controller");
    };
    serve(flow.click(190,215));
    serve(flow.click(100,45));
    serve(flow.click(100,45));
    check(session && flow.screen_name()=="desktop","normal FBM load reaches the real career desktop");
    auto& game=session->state();
    const auto& items=flow.desktop()->state().items;
    const auto settings=std::find_if(items.begin(),items.end(),[](const DeskItem& i){return i.kind==19&&!i.disabled;});
    check(settings!=items.end(),"loaded career has the genuine Settings desk item");
    serve(flow.click(settings->x+10,settings->y+10));
    check(flow.in_settings(), "loaded career opens the authenticated desk Settings owner");
    check(game.league_rounds.at(5)!=0 && game.memory.u8(::bmh::layout::CalendarState_FormatError_RuntimeField2)==0,
          "genuine WEEK196 reaches R5 rejection without Native setters");
    serve(flow.click(168, 200)); // R5 off is allowed after secondary rounds start.
    const auto denied = flow.pointer_down(168, 200);
    check(denied.request == FlowRequest::settings_action && flow.pending_settings(),
          "R5 refusal originates at the owned mouse-down boundary");
    const auto stopped = save_stream(game); const auto stopped_revision = revision;
    int notices = 0;
    const app::EnvironmentNoticePresenter present_notice = [&](const GameState& shown,
            TaxDialogProjection view, const Image& backdrop) {
        ++notices;
        check(view.source_manager_flags == uint8_t(denied.settings_action.manager_slot | 0x40) &&
              view.source_flags == 0 && view.body == message_text(game, ::bmh::layout::CareerSession_DispatchSettings_TextPointer2) &&
              backdrop.pixels == flow.render().pixels && save_stream(shown) == stopped,
              "Source R5 text, timed manager flag and actual parent backdrop reach the shared renderer");
        check(flow.pending_settings() != nullptr, "request stays owned while its synchronous notice is open");
        flow.acknowledge_settings_notice_release();
        return true;
    };
    const auto serve_notice = [&](const FlowResult& r) {
        return app::serve_flow_request(flow, r, session, data, saves, 8128,
            revision, playback, audio_session, false,
            {}, {}, {}, {}, {}, {}, {}, {}, {}, app::PendingOfferEntryPolicy::legacy_unpresented, {}, app::HumanBuyerMarketEntryPolicy::legacy_unpresented, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, present_notice);
    };
    auto forged_notice = denied; ++forged_notice.settings_owner_sequence;
    check(serve_notice(forged_notice).empty() && notices == 0 && flow.pending_settings(),
          "wrong owner sequence cannot open a refusal dialog or consume the genuine request");
    const auto notice_error=serve_notice(denied);
    const auto notice_check="refusal preserves Native/revision and consumes once: error="+notice_error+
        " notices="+std::to_string(notices)+" pending="+std::to_string(bool(flow.pending_settings()))+
        " Native="+std::to_string(save_stream(game)==stopped)+" revision="+std::to_string(revision)+
        " expected="+std::to_string(stopped_revision)+" state="+flow.state_json();
    check(notice_error.empty() && notices == 1 && !flow.pending_settings() &&
          save_stream(game) == stopped && revision == stopped_revision,
          notice_check.c_str());
    check(serve_notice(denied).empty() && notices == 1,
          "replaying a consumed refusal cannot show a second notice");
    serve(flow.pointer_down(310,230,true));
    check(flow.screen_name()=="desktop", "fresh right DOWN closes Settings after the notice release");
    std::filesystem::remove_all(saves);
}

void check_gameplay_switches_writer(const std::filesystem::path& data, const std::filesystem::path& save_path) {
    app::CareerSession session(data,8128,save_path);auto& state=session.state();
    uint64_t revision=1;std::string error;const auto w=session.rand();const auto g=session.game();
    const auto exe=load_le(ui::asset_path(data,"BMH.exe"));
    // Controlled settings/round boundary over a real loaded career. Original
    // mode and link gates are tested separately below, rather than bypassed.
    state.memory.set8(::bmh::layout::GameMode,3);state.memory.set8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3,0);
    for(uint32_t at:{::bmh::layout::RealityMatch_FormatError_ExecutableFieldUnsigned,::bmh::layout::OutcomeDelayDisabledUnsigned,::bmh::layout::MatchdaySetup_ConsiderSportschauFixture_RuntimeFieldUnsigned,::bmh::layout::CalendarState_FormatError_RuntimeField2Unsigned})state.memory.set8(at,0);
    const auto request=[&](int row,int col) {
        auto projection=settings_from_game(state,int(state.memory.u8(::bmh::layout::ActiveManager)),revision,exe);
        return SettingsWriteRequest{projection.manager_slot,revision,projection.cells,uint8_t(row),uint8_t(col),SettingsEdit::toggle_binary};
    };
    for(const auto [row,col]:std::array<std::pair<int,int>,3>{{{2,4},{6,5},{16,6}}}) {
        auto r=request(row,col);const auto before=state;const auto old_revision=revision;
        check(session.dispatch_settings(r,revision,error),"existing C5/G6/Q7 consumer is writable");
        auto expected=before;expected.memory.set8(::bmh::layout::LeaguePresentationOptions+uint32_t(row*9+col),1);
        check(save_stream(state)==save_stream(expected)&&revision==old_revision+1,"switch changes exactly its saved Source byte");
        check(!session.dispatch_settings(r,revision,error)&&save_stream(state)==save_stream(expected),"stale switch cannot apply twice");
        r=request(row,col);check(session.dispatch_settings(r,revision,error)&&save_stream(state)==save_stream(before),"switch reverses without unrelated Native edits");
    }
    check(state.league_rounds.size()>5,"loaded format field round exists");
    state.league_rounds[5]=1;
    check(day_competitions(state,0x10)==0x10,"secondary-league calendar entry initially enabled");
    auto r=request(17,4);check(session.dispatch_settings(r,revision,error)&&day_competitions(state,0x10)==0,
                             "R5 off reaches real calendar competition consumer after season start");
    const auto stopped=save_stream(state);const auto stopped_revision=revision;r=request(17,4);
    check(!session.dispatch_settings(r,revision,error)&&save_stream(state)==stopped&&revision==stopped_revision,
          "Source rejects re-enable after bb4c6 started, with no mutation");
    check(error==original_to_utf8(message_text(state,::bmh::layout::CareerSession_DispatchSettings_TextPointer2)),"R5 rejection uses the Original warning text");
    check(settings_source_refusal(state,r,revision)==::bmh::layout::CareerSession_DispatchSettings_TextPointer2,"started R5 refusal selects legacy routine aeb54");
    auto stale_notice=r; ++stale_notice.revision;
    check(!settings_source_refusal(state,stale_notice,revision),"stale revision cannot create a notice");
    stale_notice=r; stale_notice.expected_raw[0]^=1;
    check(!settings_source_refusal(state,stale_notice,revision),"stale Native snapshot cannot create a notice");
    const auto old_date=state.date;state.memory.set8(::bmh::layout::CalendarState_FormatError_RuntimeField,0);
    for(int day:{109,110,230,231}) {
        state.date=state.season_start+day;auto q5=request(16,4);
        check(bool(settings_source_refusal(state,q5,revision))==(day>=110&&day<=230),
              "Q5 refusal has the inclusive Original 110..230 season-day boundary");
        if(day>=110&&day<=230) {
            const auto before=save_stream(state);
            check(!session.dispatch_settings(q5,revision,error)&&save_stream(state)==before&&
                  error==original_to_utf8(message_text(state,::bmh::layout::CareerSession_DispatchSettings_TextPointer)),
                  "Q5 refusal preserves Native bytes and uses Original aeb50 text");
        }
    }
    state.date=old_date;
    state.league_rounds[5]=0;r=request(17,4);
    check(session.dispatch_settings(r,revision,error)&&day_competitions(state,0x10)==0x10,
          "R5 can re-enable before first secondary-league round");
    state.memory.set8(::bmh::layout::GameMode,2);r=request(17,4);const auto locked=save_stream(state);
    check(!session.dispatch_settings(r,revision,error)&&save_stream(state)==locked,"Original mode lock survives R5 connection");
    state.memory.set8(::bmh::layout::GameMode,3);state.memory.set8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3,1);r=request(2,4);const auto numbers_locked=save_stream(state);
    check(!session.dispatch_settings(r,revision,error)&&save_stream(state)==numbers_locked,"C5 remains locked when Reality is disabled");
    check(session.rand().seed()==w.seed()&&session.rand().calls()==w.calls()&&session.game().seed()==g.seed()&&
          session.game().calls()==g.calls(),"all switch and rejection paths preserve both RNG streams");
}

void check_loaded_settings_writer(const std::filesystem::path& data, const std::filesystem::path& save_path) {
    app::CareerSession session(data, 0x4c756e61, save_path);
    auto& game = session.state();
    const int manager = int(game.memory.u8(::bmh::layout::ActiveManager));
    const uint64_t initial_revision = 19;
    const auto exe = load_le(ui::asset_path(data, "BMH.exe"));
    auto state = settings_from_game(game, manager, initial_revision, exe);
    constexpr size_t l7 = size_t(11 * 9 + 6);
    check(state.cells[l7] <= 1 && state.display_cells && !((*state.display_cells)[l7] & 0x80),
          "WEEK196 L7 is an enabled source-backed panel toggle");
    SettingsScreen screen(data, state);
    const auto pixels_before = screen.render().pixels;
    const GameState state_before = game;
    const auto saved_before = save_stream(game);
    const auto game_seed = session.game().seed();
    const auto watcom_seed = session.rand().seed();
    const auto game_calls = session.game().calls(), watcom_calls = session.rand().calls();
    check(screen.click(67 + 6 * 24 + 5, 25 + 11 * 10 + 5) && screen.pending() &&
          screen.pending()->row == 11 && screen.pending()->column == 6,
          "actual save UI emits L7 request for its exact cell");
    auto request = *screen.pending();
    uint64_t revision = initial_revision;
    std::string error;
    check(session.dispatch_settings(request, revision, error) && revision == initial_revision + 1,
          "actual WEEK196 CareerSession accepts the L7 panel toggle");
    auto updated = settings_from_game(game, manager, revision, exe);
    screen.acknowledge(std::move(updated));
    const auto pixels_after = screen.render().pixels;
    bool panel_changed = false;
    for (int y = 24 + 11 * 10; y < 24 + 11 * 10 + 9; ++y)
        for (int x = 67 + 6 * 24; x < 67 + 6 * 24 + 23; ++x)
            panel_changed |= pixels_before[size_t(y) * 320 + x] != pixels_after[size_t(y) * 320 + x];
    check(panel_changed && game.memory.u8(::bmh::layout::SolidPanelBackground) == (request.expected_raw[l7] ^ 1) &&
          revision == initial_revision + 1 && session.game().seed() == game_seed &&
          session.rand().seed() == watcom_seed && session.game().calls() == game_calls &&
          session.rand().calls() == watcom_calls,
          "native L7 rendering follows ACK while engine RNG remains untouched");
    auto expected = state_before;
    // The actual in-memory save projection differs only at L7; each writer call
    // mutates a single byte and the existing serializer preserves all others.
    expected.memory.set8(::bmh::layout::SolidPanelBackground, uint8_t(request.expected_raw[l7] ^ 1));
    check(save_stream(game) != saved_before && save_stream(game) == save_stream(expected),
          "WEEK196 writer changes only the acknowledged raw cell");
    const auto after_l7 = save_stream(game);
    check(!session.dispatch_settings(request, revision, error) && revision == initial_revision + 1 &&
          save_stream(game) == after_l7,
          "replayed WEEK196 action is stale after its current-revision ACK");

    // A stale hidden lock bit must not become writable just because entry-time
    // projection strips dynamic bit 7 in its local display copy.
    game.memory.set8(::bmh::layout::SolidPanelBackground, 0x80);
    ui::SettingsWriteRequest legacy = request;
    legacy.revision = revision;
    legacy.expected_raw[l7] = 0x80;
    const auto legacy_before = save_stream(game);
    check(!session.dispatch_settings(legacy, revision, error) && revision == initial_revision + 1 &&
          save_stream(game) == legacy_before,
          "WEEK196 raw >=2 remains blocked despite projected availability");
}

void check_pitch_actions_semantic(const std::filesystem::path& data) {
    StartupFlow setup(data);
    setup.click(150,215); setup.click(5,225,true); setup.click(5,225,true); setup.click(90,169);
    app::CareerSession session(data,0x12345678);
    std::string error;
    check(session.confirm_manager(setup.pending_career()->options,setup.pending_career()->managers[0],error),
          "Pitch fixture confirms a genuine manager");
    CareerInitializationResult init;
    check(session.finish(init,error),"Pitch fixture reaches an actual career desk");
    uint64_t revision=init.desktop.revision;
    auto& game=session.state();
    const int manager=game.memory.u8(::bmh::layout::ActiveManager),team=game.manager_teams.at(size_t(manager));
    const size_t rec=size_t(manager)*0xb0c,board=size_t(manager)*0x840,option=size_t(manager)*5;
    const auto range=squad_range(team);
    const auto request=[&](PitchRequestKind kind) {
        PitchRequest r;r.kind=kind;r.manager_slot=manager;r.runtime_club=team;r.revision=revision;
        r.indoor=game.indoor;r.expected_mode=game.memory.u8(::bmh::layout::GameMode);
        r.expected_set_piece_signature=uint16_t(game.manager_records.at(rec+0xb0a)|uint16_t(game.manager_records.at(rec+0xb0b))<<8);
        r.old_weights=r.new_weights=game.manager_records.at(rec+0x288);return r;
    };
    const auto weight=[&](int index) {
        auto r=request(PitchRequestKind::set_automatic_weight);r.weight_index=index;
        const int shift=index*2,next=(((r.old_weights>>shift)&3)+1)&3;
        r.new_weights=uint8_t((r.old_weights&~(3<<shift))|(next<<shift));return r;
    };
    const auto reject=[&](const PitchRequest& r,const char* reason) {
        const auto bytes=save_stream(game);const auto before_revision=revision;
        const auto w=session.rand();const auto g=session.game();
        const auto entry=session.lineup_checkpoint();
        check(!session.dispatch_pitch(r,revision,error)&&!error.empty()&&revision==before_revision&&
              save_stream(game)==bytes&&session.rand().seed()==w.seed()&&session.rand().calls()==w.calls()&&
              session.game().seed()==g.seed()&&session.game().calls()==g.calls()&&
              session.lineup_checkpoint().manager==entry.manager&&session.lineup_checkpoint().club==entry.club,reason);
    };
    const auto accept=[&](const PitchRequest& r,const GameState& expected,const char* reason) {
        const auto before_revision=revision;const auto w=session.rand();const auto g=session.game();
        check(session.dispatch_pitch(r,revision,error)&&error.empty()&&revision==before_revision+1&&
              save_stream(game)==save_stream(expected)&&session.rand().seed()==w.seed()&&session.rand().calls()==w.calls()&&
              session.game().seed()==g.seed()&&session.game().calls()==g.calls()&&
              session.preparation_projection(revision).lineup.entry_prepared,reason);
    };
    reject(weight(0),"semantic Pitch refuses before authoritative lineup entry");
    game.manager_records[rec+0xafc]=0xff;game.manager_records[rec+0xafd]=0xff;
    game.manager_records[rec+0xafe]=0xff;game.manager_records[rec+0xaff]=0xff;
    game.manager_records[rec+0xb08]=0xff;game.manager_records[rec+0xb09]=0xff;
    game.manager_records[rec+0xb0a]=0;game.manager_records[rec+0xb0b]=0;
    LineupRequest entry{LineupRequestKind::prepare_lineup,manager,team,revision};
    check(session.dispatch_lineup(entry,revision,error),"genuine Source lineup preparation establishes Pitch ownership");
    check(game.manager_records[rec+0xafc]==0&&game.manager_records[rec+0xafd]==0&&
          game.manager_records[rec+0xafe]==0&&game.manager_records[rec+0xaff]==0&&
          game.manager_records[rec+0xb08]==0&&game.manager_records[rec+0xb09]==0&&
          uint16_t(game.manager_records[rec+0xb0a]|uint16_t(game.manager_records[rec+0xb0b])<<8)==0x3253,
          "entry initializes invalid supported edition signature and all three AUTO takers source-faithfully");
    const int roster_id=game.squad_slots.at(range.first)&0x0fff;
    const uint16_t roster_encoded=uint16_t(roster_id+1);
    game.manager_records[rec+0xafc]=uint8_t(roster_encoded);game.manager_records[rec+0xafd]=uint8_t(roster_encoded>>8);
    game.manager_records[rec+0xafe]=0xff;game.manager_records[rec+0xaff]=0x0f;
    LineupRequest reentry{LineupRequestKind::prepare_lineup,manager,team,revision};
    check(session.dispatch_lineup(reentry,revision,error)&&
          uint16_t(game.manager_records[rec+0xafc]|uint16_t(game.manager_records[rec+0xafd])<<8)==roster_encoded&&
          game.manager_records[rec+0xafe]==0xff&&game.manager_records[rec+0xaff]==0x0f,
          "closed-editor re-entry preserves signature-valid values until the Original editor redraw");
    // Declared native fixture controls: modeIII and reserved high weight bits.
    // No player identity, roster or ownership mapping is fabricated.
    game.memory.set8(::bmh::layout::GameMode,3);
    game.manager_records[rec+0x288]|=0xc0;
    for(int index=0;index<3;++index) {
        for(int cycle=0;cycle<4;++cycle) {
            const auto r=weight(index);auto expected=game;expected.manager_records[rec+0x288]=r.new_weights;
            accept(r,expected,"Source weight cycle changes only one whole Native byte without optimization or RNG");
            reject(r,"consumed Pitch revision makes weight replay entirely neutral");
        }
    }
    for(int variant=0;variant<6;++variant) {
        auto r=weight(0);
        if(variant==0)++r.manager_slot;
        if(variant==1)++r.runtime_club;
        if(variant==2)--r.revision;
        if(variant==3)r.old_weights^=0x80;
        if(variant==4)r.new_weights^=0x80;
        if(variant==5)r.weight_index=3;
        reject(r,"Pitch weight binds active manager, club, revision, full old byte and exact Source cycle");
    }
    const auto record_team=game.manager_records[rec+0x290];game.manager_records[rec+0x290]^=1;
    reject(weight(0),"Pitch cross-checks manager record290 against live manager_teams");
    game.manager_records[rec+0x290]=record_team;
    const auto active=game.memory.u8(::bmh::layout::ActiveManager);game.memory.set8(::bmh::layout::ActiveManager,uint8_t(manager+1));
    reject(weight(0),"actual active-manager handoff invalidates old Pitch ownership");game.memory.set8(::bmh::layout::ActiveManager,active);
    const auto manager_count=game.memory.u8(::bmh::layout::ManagerCount);game.memory.set8(::bmh::layout::ManagerCount,0);
    reject(weight(0),"Pitch requires the current Original manager count and complete ownership table");game.memory.set8(::bmh::layout::ManagerCount,manager_count);
    game.indoor=true;auto wrong_mode=weight(0);wrong_mode.indoor=false;
    reject(wrong_mode,"stale outdoor-stamped request is refused against the live indoor owner");game.indoor=false;
    // Assistant-manager entry can leave badge bit9 different from transient
    // format field. Actions bind the live mode separately and retain this byte.
    game.club_heap_78[size_t(team)*2+1]|=2;
    {
        const auto mismatch_request=weight(0);auto mismatch_expected=game;
        mismatch_expected.manager_records[rec+0x288]=mismatch_request.new_weights;
        accept(mismatch_request,mismatch_expected,"outdoor assistant projection accepts a Source-valid badge/mode mismatch without normalizing it");
    }
    game.lineups_frozen=true;reject(weight(0),"frozen lineup phase refuses new Pitch mutation");game.lineups_frozen=false;
    const auto automatic=game.manager_options[option];game.manager_options[option]=9;
    reject(weight(0),"Pitch refuses malformed current automatic-formation state");game.manager_options[option]=automatic;
    const auto before_overflow=revision;revision=UINT64_MAX;reject(weight(0),"Pitch revision cannot overflow");revision=before_overflow;
    game.memory.set8(::bmh::layout::GameMode,1);reject(weight(0),"Source modeI suppresses automatic weights");game.memory.set8(::bmh::layout::GameMode,3);

    // Controlled valid outdoor geometry on the actual nominated eleven allows
    // independent exact-coordinate expectations, including the Source19/8 edge.
    const std::array<std::pair<uint8_t,uint8_t>,11> positions{{{0,4},{4,0},{4,2},{4,4},{4,6},
        {8,0},{8,2},{8,4},{12,0},{12,3},{12,6}}};
    for(size_t row=0;row<11;++row) {
        game.tactics[board+row*0x58+0x41]=positions[row].first;
        game.tactics[board+row*0x58+0x42]=positions[row].second;
        game.tactics[board+row*0x58+0x3f]=0xff;
    }
    for(size_t row=11;row<24;++row)game.tactics[board+row*0x58+0x3f]=0xff;
    game.manager_options[option]=3;
    const auto move=[&](int row,int depth,int lane) {
        auto r=request(PitchRequestKind::move_player);r.row=row;r.slot=game.squad_slots.at(range.first+size_t(row));
        r.player_id=r.slot&0xfff;r.old_depth=int8_t(game.tactics.at(board+size_t(row)*0x58+0x41));
        r.old_lane=int8_t(game.tactics.at(board+size_t(row)*0x58+0x42));r.new_depth=depth;r.new_lane=lane;return r;
    };
    auto r=move(0,4,0);auto expected=game;
    std::swap(expected.tactics[board+0x41],expected.tactics[board+0x58+0x41]);
    std::swap(expected.tactics[board+0x42],expected.tactics[board+0x58+0x42]);expected.manager_options[option]=0;
    accept(r,expected,"occupied Pitch target swaps coordinate bytes and AA only, preserving player/squad/card identities");
    reject(move(0,5,3),"Source outdoor adjacency refuses diagonal target beside another starter");
    reject(move(0,20,8),"Source depth20 lies outside the accepted grid");
    reject(move(0,19,9),"Source lane9 lies outside the accepted grid");
    auto stale=move(0,19,8);++stale.old_depth;reject(stale,"move binds authoritative old coordinates");
    stale=move(0,19,8);++stale.player_id;reject(stale,"move binds authoritative player identity");
    stale=move(0,19,8);stale.slot^=0x1000;reject(stale,"move binds full roster slot including high identity flags");
    stale=move(11,19,8);reject(stale,"bench row cannot become a persistent Pitch placement");
    const int id=game.squad_slots[range.first]&0xfff;const auto injury=game.players.at(size_t(id)).w364;
    game.players[size_t(id)].w364|=1u<<16;reject(move(0,19,8),"injured starter placement refuses before writes");game.players[size_t(id)].w364=injury;
    game.tactics[board+0x3f]=1;game.tactics[board+0x58+0x3f]=0;
    game.tactics[board+2*0x58+0x3f]=1;game.tactics[board+3*0x58+0x3f]=0;
    game.tactics[board+4*0x58+0x3f]=5;game.tactics[board+5*0x58+0x3f]=4;
    r=move(0,19,8);expected=game;
    for(int row:{0,1,3})expected.tactics[board+size_t(row)*0x58+0x3f]=0xff;
    // Outdoor local_60 remains ff; old partner1 and references-to-1 are not a second cleanup owner.
    expected.tactics[board+0x41]=19;expected.tactics[board+0x42]=8;expected.manager_options[option]=0;
    accept(r,expected,"free depth19 lane8 placement clears only relevant Source partner references");
    reject(move(0,19,8),"same-coordinate typed placement remains a neutral no-op");

    // Compare apply/save to the pre-existing Source optimizer, not to the new
    // wrappers: table/mask/AA are independently staged first in this oracle.
    game.manager_records[rec+0x288]=0xc0;
    int formation=-1;
    for(int f=0;f<8;++f)if(game.manager_records[rec+0x218]&(1<<f)){formation=f;break;}
    check(formation>=0,"actual manager has an available Source formation");
    r=request(PitchRequestKind::apply_formation);r.formation=formation;expected=game;
    expected.manager_options[option]=uint8_t(formation+1);auto_lineup(expected,team,0xe4);
    accept(r,expected,"apply commits AA then real Source optimizer with complete Native equality and no RNG");
    r=request(PitchRequestKind::apply_formation);r.formation=8;reject(r,"invalid formation index refuses neutrally");
    r=request(PitchRequestKind::apply_formation);r.formation=formation;r.new_weights^=1;
    reject(r,"apply cannot smuggle a weight change through its acknowledged packet");
    const auto mask=game.manager_records[rec+0x218];game.manager_records[rec+0x218]=0;
    r=request(PitchRequestKind::apply_formation);r.formation=formation;reject(r,"unavailable formation mask refuses neutrally");game.manager_records[rec+0x218]=mask;
    game.memory.set8(::bmh::layout::GameMode,1);
    r=request(PitchRequestKind::apply_formation);r.formation=formation;expected=game;
    expected.manager_options[option]=uint8_t(formation+1);auto_lineup(expected,team,0xe4);
    accept(r,expected,"modeI keeps Original formation application available");game.memory.set8(::bmh::layout::GameMode,3);
    const auto save_request=[&](int f) {
        auto q=request(PitchRequestKind::save_formation);q.formation=f;
        for(size_t row=0;row<11;++row){q.formation_positions[row]=game.tactics.at(board+row*0x58+0x41);
            q.formation_positions[11+row]=game.tactics.at(board+row*0x58+0x42);}return q;
    };
    r=save_request(4);r.formation_positions[0]^=1;reject(r,"save formation cannot accept forged submitted geometry");
    reject(save_request(3),"ordinary saved-formation writes exclude built-in slots");
    const auto nominated=game.squad_slots[range.first];game.squad_slots[range.first]&=uint16_t(~0x8000u);
    reject(save_request(4),"saving requires eleven currently nominated starters");game.squad_slots[range.first]=nominated;
    for(int f:{4,7}) {
        r=save_request(f);expected=game;
        std::copy(r.formation_positions.begin(),r.formation_positions.end(),expected.formations.begin()+size_t(manager)*0x160+size_t(f)*22);
        expected.manager_records[rec+0x218]|=uint8_t(1<<f);expected.manager_options[option]=uint8_t(f+1);
        auto_lineup(expected,team,0xe4);
        accept(r,expected,"save writes exact22 bytes/mask/AA before optimizer and preserves unrelated Native");
    }
    const auto level=game.level;const auto weights=game.manager_records[rec+0x288];
    game.level=6;game.manager_records[rec+0x288]=uint8_t((weights&~12u)|4u);
    r=request(PitchRequestKind::apply_formation);r.formation=formation;
    expected=game;expected.manager_options[option]=uint8_t(formation+1);auto_lineup(expected,team,0xe4);
    accept(r,expected,"level-6 detail score commits through Source formation application");
    r=save_request(4);expected=game;
    std::copy(r.formation_positions.begin(),r.formation_positions.end(),expected.formations.begin()+size_t(manager)*0x160+size_t(4)*22);
    expected.manager_records[rec+0x218]|=uint8_t(1<<4);expected.manager_options[option]=5;
    auto_lineup(expected,team,0xe4);
    accept(r,expected,"level-6 detail score commits through Source saved formation path");
    // Weight-only edits remain independent of the optimizer.
    r=weight(0);expected=game;expected.manager_records[rec+0x288]=r.new_weights;
    accept(r,expected,"weight-only edit remains available alongside detailed scoring");
    game.level=level;game.manager_records[rec+0x288]=weights;

    const auto pairings=session.league_pairings_checkpoint();
    check(session.open_league_pairings(DeskRequest{DeskRequestKind::open_menu,manager,0,1,-1,revision},revision,error).has_value(),
          "actual pairings draft establishes an unrelated owned phase");
    reject(weight(0),"semantic Pitch refuses while Source pairings owns a staged state");session.restore_league_pairings_checkpoint(pairings);
    StadiumRequest stadium{StadiumRequestKind::open,manager,team,-1,revision};
    check(session.dispatch_stadium(stadium,revision,error),"genuine legacy periphery scene opens for modal ownership test");
    int offered=-1;
    for(int object=0;object<14;++object){stadium={StadiumRequestKind::quote_periphery,manager,team,object,revision};
        if(session.dispatch_stadium(stadium,revision,error)){offered=object;break;}}
    check(offered>=0,"actual Source quote supplies a genuine unrelated modal owner");
    reject(weight(0),"owned periphery quote prevents unrelated Pitch mutation");
    const auto quote=session.preparation_projection(revision).stadium;
    stadium={StadiumRequestKind::decline_periphery,manager,team,offered,revision,quote->quoted_price};
    check(session.dispatch_stadium(stadium,revision,error),"actual decline releases the unrelated modal quote");
    r=weight(0);expected=game;expected.manager_records[rec+0x288]=r.new_weights;
    accept(r,expected,"neutral legacy stadium-open alone does not block semantic Pitch after quote decline");

    const auto directory=std::filesystem::temp_directory_path()/("openbmh-pitch-actions-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    check(session.save_game(directory/"PITCH.FBM",manager,false,error),"accepted Pitch state uses actual production Native save");
    app::CareerSession loaded(data,0x12345678,directory/"PITCH.FBM");
    check(save_stream(loaded.state())==save_stream(game)&&loaded.rand().calls()==0&&loaded.game().calls()==0,
          "fresh real Native load preserves full accepted Pitch state without RNG draws");
    check(loaded.state().formations==game.formations&&loaded.state().manager_records==game.manager_records&&
          loaded.state().manager_options==game.manager_options&&loaded.state().tactics==game.tactics&&
          loaded.state().player_cards==game.player_cards,"fresh Native restores formation22/mask/AA/board/card effects");

    // Controlled live-mode cross-product against the same genuine initialized
    // career: mode is deliberately switched only in this test fixture, while
    // manager, runtime club, revision, board, and all RNG state remain owned.
    // The indoor formation/mask are disjoint from their outdoor counterparts.
    game.indoor=true;
    game.club_heap_78.at(size_t(team)*2+1)|=0x02;
    game.manager_records.at(rec+0x218)=0x80;
    game.manager_records.at(rec+0x219)=0x01;
    const size_t indoor_table=size_t(manager)*0x160+0xb0;
    const auto indoor_positions=std::array<uint8_t,22>{0,1,2,3,4,5,6,7,8,9,10,4,4,4,4,4,4,4,4,4,4,4};
    std::copy(indoor_positions.begin(),indoor_positions.end(),game.formations.begin()+std::ptrdiff_t(indoor_table));
    auto stale_mode=request(PitchRequestKind::apply_formation);stale_mode.indoor=false;stale_mode.formation=0;
    reject(stale_mode,"outdoor-stamped request cannot mutate the live indoor table");
    auto indoor_apply=request(PitchRequestKind::apply_formation);indoor_apply.formation=0;
    expected=game;expected.manager_options[option]=1;auto_lineup(expected,team,0xe4);
    accept(indoor_apply,expected,"session accepts an explicitly indoor request against mask+0x219 and table+0xb0");
    check(game.manager_records.at(rec+0x218)==0x80&&game.manager_records.at(rec+0x219)==0x01&&
          std::equal(indoor_positions.begin(),indoor_positions.end(),game.formations.begin()+std::ptrdiff_t(indoor_table)),
          "indoor AA commit leaves outdoor mask and formation storage untouched");
    const uint32_t display_at=uint32_t(::bmh::layout::CareerSession_AnyOf_ExecutableField+manager*2);
    check(game.memory.has(display_at)&&
          uint16_t(game.manager_records.at(rec+0xb0a)|uint16_t(game.manager_records.at(rec+0xb0b))<<8)==0x3253,
          "Source pitch entry initializes the supported edition signature and manager display owner");
    const auto designated=[&](){return std::array<uint16_t,3>{
        uint16_t(game.manager_records.at(rec+0xafc)|uint16_t(game.manager_records.at(rec+0xafd))<<8),
        uint16_t(game.manager_records.at(rec+0xafe)|uint16_t(game.manager_records.at(rec+0xaff))<<8),
        uint16_t(game.manager_records.at(rec+0xb08)|uint16_t(game.manager_records.at(rec+0xb09))<<8)};};
    // Assistant entry can preserve an outdoor badge bit in the live indoor
    // mode. Original pitch actions do not use this bit as an indoor guard.
    game.club_heap_78.at(size_t(team)*2+1)&=uint8_t(~0x02u);
    auto editor=request(PitchRequestKind::open_set_piece_editor);
    editor.expected_set_piece_values=designated();editor.expected_display_word=game.memory.u16(display_at);
    editor.new_display_word=uint16_t((editor.expected_display_word&0xff00)|((editor.expected_display_word&0x008f)|0x0080));
    expected=game;expected.memory.set16(display_at,editor.new_display_word);
    expected.manager_records[rec+0xafe]=expected.manager_records[rec+0xaff]=0;
    accept(editor,expected,"Schütze redraw normalizes the forged nonmember at the Source editor boundary");
    int bench_eligible=-1;
    for(size_t row=11;row<range.count;++row){const auto slot=game.squad_slots.at(range.first+row);const int id=slot&0x0fff;
        if((slot&0xc000)&&id<int(game.players.size())&&((game.players[size_t(id)].w364>>16)&0x1f)==0){bench_eligible=int(row);break;}}
    check(bench_eligible>=11,"actual prepared roster has a marked healthy bench candidate for the Source top list");
    {
        auto r=request(PitchRequestKind::set_piece_taker);r.expected_set_piece_values=designated();
        r.expected_display_word=game.memory.u16(display_at);r.set_piece_selector=2;r.row=bench_eligible;
        r.slot=game.squad_slots.at(range.first+size_t(bench_eligible));r.player_id=r.slot&0x0fff;
        r.expected_set_piece_value=designated()[0];expected=game;
        const size_t off=rec+0xafc;const uint16_t encoded=uint16_t(r.player_id+1);
        expected.manager_records[off]=uint8_t(encoded);expected.manager_records[off+1]=uint8_t(encoded>>8);
        accept(r,expected,"Source top-list accepts an actual marked healthy bench row and persists its player_id+1");
    }
    int eligible=-1;
    for(int row=0;row<11;++row){const auto slot=game.squad_slots.at(range.first+size_t(row));const int id=slot&0x0fff;
        if((slot&0xc000)&&id<int(game.players.size())&&((game.players[size_t(id)].w364>>16)&0x1f)==0){eligible=row;break;}}
    check(eligible>=0,"Source-prepared lineup supplies a healthy marked taker candidate");
    for(int selector=2;selector<=4;++selector){
        auto r=request(PitchRequestKind::set_piece_taker);r.expected_set_piece_values=designated();
        r.expected_display_word=game.memory.u16(display_at);r.set_piece_selector=selector;r.row=eligible;
        r.slot=game.squad_slots.at(range.first+size_t(eligible));r.player_id=r.slot&0x0fff;
        r.expected_set_piece_value=designated()[size_t(selector-2)];
        expected=game;const size_t off=rec+(selector==2?0xafc:selector==3?0xafe:0xb08);
        const uint16_t encoded=uint16_t(r.player_id+1);expected.manager_records[off]=uint8_t(encoded);expected.manager_records[off+1]=uint8_t(encoded>>8);
        accept(r,expected,"supported edition selector writes player ID+1 to its exact Source manager offset");
    }
    auto auto_takers=request(PitchRequestKind::auto_set_piece_takers);
    auto_takers.expected_set_piece_values=designated();auto_takers.expected_display_word=game.memory.u16(display_at);
    expected=game;for(const size_t off:{size_t(0xafc),size_t(0xafe),size_t(0xb08)})
        expected.manager_records[rec+off]=expected.manager_records[rec+off+1]=0;
    accept(auto_takers,expected,"supported edition AUTO resets all three encoded takers without consuming either RNG");
    auto close_editor=request(PitchRequestKind::close_set_piece_editor);
    close_editor.expected_set_piece_values=designated();close_editor.expected_display_word=game.memory.u16(display_at);
    close_editor.new_display_word=uint16_t(close_editor.expected_display_word&~uint16_t(0x80));
    expected=game;expected.memory.set16(display_at,close_editor.new_display_word);
    accept(close_editor,expected,"switching from Schütze restores the ordinary page and clears only the transient low-80 bit");
    reject(indoor_apply,"consumed indoor pitch revision rejects replay without Native, RNG, or owner-checkpoint changes");
    r=weight(0);expected=game;expected.manager_records[rec+0x288]=r.new_weights;
    accept(r,expected,"indoor assistant projection retains a Source-valid badge/mode mismatch through another action");
    std::filesystem::remove_all(directory);
}

void check_pitch_actions_controller(const std::filesystem::path& data) {
    StartupFlow flow(data);std::unique_ptr<app::CareerSession> session;
    openbmh::audio::Playback playback;std::optional<uint64_t> audio_session;uint64_t revision=0;
    const auto serve=[&](const FlowResult& result){check(app::serve_flow_request(flow,result,session,data,data,0x12345678,
        revision,playback,audio_session,false).empty(),"Pitch uses shared production Flow/Controller");};
    serve(flow.click(150,215));serve(flow.click(5,225,true));serve(flow.click(5,225,true));serve(flow.click(90,169));
    check(session&&flow.screen_name()=="desktop","physical Pitch fixture initializes a genuine career");
    session->state().memory.set8(::bmh::layout::GameMode,3); // declared modeIII fixture for enabled weight controls
    flow.attach_preparation_context(session->preparation_projection(revision));
    const auto& items=flow.desktop()->state().items;
    const auto menu=std::find_if(items.begin(),items.end(),[](const auto& item){return item.kind==3&&!item.disabled;});
    check(menu!=items.end(),"actual desktop offers lineup entry");
    serve(flow.click(menu->x+menu->width/2,menu->y+menu->height/2));
    const uint32_t display_at=uint32_t(::bmh::layout::CareerSession_AnyOf_ExecutableField+2*session->state().memory.u8(::bmh::layout::ActiveManager));
    const uint16_t before_pitch_entry=session->state().memory.u16(display_at);
    auto pitch_entry=flow.click(80,219);
    check(pitch_entry.request==FlowRequest::submenu_action&&
          std::get<PitchRequest>(pitch_entry.submenu_action).page_control==1&&
          std::get<PitchRequest>(pitch_entry.submenu_action).new_display_word==uint16_t(before_pitch_entry^0x000c),
          "Lineup OpenPitch is a typed Source word transition, not a local screen-only switch");
    const auto& pitch_entry_request=std::get<PitchRequest>(pitch_entry.submenu_action);
    std::string pitch_entry_error;
    const bool pitch_entry_accepted=session->dispatch_pitch(pitch_entry_request,revision,pitch_entry_error);
    check(pitch_entry_accepted,(std::string("Session rejected Lineup-to-Pitch Source transition: ")+pitch_entry_error).c_str());
    flow.acknowledge_preparation(session->preparation_projection(revision));
    check(flow.screen_name()=="pitch","accepted Lineup-to-Pitch ACK selects the Pitch presenter");
    check(session->preparation_projection(revision).lineup.entry_prepared,
          "accepted Lineup-to-Pitch transition retains the prepared lineup");
    check(session->state().memory.u16(display_at)==uint16_t(before_pitch_entry^0x000c),
          "accepted Lineup-to-Pitch transition stores the Source XOR 0x0c word");
    auto down=flow.pointer_down(200,180);
    check(down.request==FlowRequest::submenu_action&&std::get<PitchRequest>(down.submenu_action).kind==PitchRequestKind::open_set_piece_editor,
          "Source Schütze entry acts on physical DOWN and creates one typed owner request");
    serve(down);
    check(flow.click(200,180).request==FlowRequest::none,"matching UP is consumed after immediate Schütze ACK");
    down=flow.pointer_down(260,118);serve(down);
    check(flow.click(260,118).request==FlowRequest::none,"selector button selection is local to DOWN and its UP is neutral");
    auto live_projection=session->preparation_projection(revision).lineup;
    int healthy_row=-1,baseline=36;
    for(size_t row=0;row<live_projection.players.size();++row){const auto& p=live_projection.players[row];
        if(row&&((p.slot&0xc000)!=(live_projection.players[row-1].slot&0xc000)))baseline+=2;
        if(p.player_id>=0&&(p.slot&0xc000)&&((p.attributes.w364>>16)&31)==0){healthy_row=int(row);break;}
        baseline+=7;
    }
    check(healthy_row>=0,"actual lineup has a marked healthy taker row");
    down=flow.pointer_down(20,baseline-5);
    check(down.request==FlowRequest::submenu_action&&std::get<PitchRequest>(down.submenu_action).kind==PitchRequestKind::set_piece_taker,
          "Source marked healthy row writes its selected player only on DOWN");
    serve(down);check(flow.click(20,baseline-5).request==FlowRequest::none,"taker ACK owns its matching UP");
    check(flow.state_json().find("\"shooter_selection\":1")!=std::string::npos,
          "accepted production ACK resets copied presenter selection to Source neutral mode");
    down=flow.pointer_down(260,215);
    check(down.request==FlowRequest::submenu_action&&std::get<PitchRequest>(down.submenu_action).kind==PitchRequestKind::auto_set_piece_takers,
          "Source AUTO action dispatches on physical DOWN");
    serve(down);check(flow.click(260,215).request==FlowRequest::none,"AUTO ACK consumes matching UP");
    const bool view_was_a=flow.state_json().find("\"view\":\"data_a\"")!=std::string::npos;
    down=flow.pointer_down(200,115);
    check(down.request==FlowRequest::submenu_action&&std::get<PitchRequest>(down.submenu_action).kind==PitchRequestKind::select_pitch_page,
          "Data page transition uses a typed Source display-word action");
    serve(down);check(flow.click(200,115).request==FlowRequest::none&&
          flow.state_json().find(view_was_a?"\"view\":\"data_b\"":"\"view\":\"data_a\"")!=std::string::npos,
          "Data page ACK changes the owned view once and survives transactional Flow reconstruction");
    down=flow.pointer_down(200,180);serve(down);flow.click(200,180);
    down=flow.pointer_down(200,126);
    check(down.request==FlowRequest::submenu_action&&std::get<PitchRequest>(down.submenu_action).page_control==1,
          "Pitch Team-return binds the Source low-word operation even without the Schützeneditor");
    const auto before_team_return=session->state().memory.u16(display_at);
    check(std::get<PitchRequest>(down.submenu_action).expected_display_word==before_team_return&&
          std::get<PitchRequest>(down.submenu_action).new_display_word==uint16_t(before_team_return^0x000c),
          "Team-return payload authenticates the exact current word and XOR 0x0c target");
    serve(down);flow.click(200,126);
    check(flow.screen_name()=="pitch"&&session->state().memory.u16(display_at)==uint16_t(before_team_return^0x000c),
          "accepted Team toggle keeps the full-sheet page owned by the Pitch presenter");
    const auto sheet_select=flow.click(20,32);
    check(sheet_select.changed&&flow.state_json().find("\"selected_row\":0")!=std::string::npos,
          "full Team sheet keeps the original selectable player rows interactive");
    check(flow.click(20,32).changed,"clicking the selected full-sheet row clears its local selection");
    serve(flow.click(80,219));
    check(flow.screen_name()=="pitch","re-entering Pitch again uses the Source transition");
    down=flow.pointer_down(200,151);
    check(down.request==FlowRequest::submenu_action&&std::get<PitchRequest>(down.submenu_action).page_control==3,
          "Anweis page uses its native page-selection ACK rather than a local overlay toggle");
    serve(down);flow.click(200,151);
    check(flow.state_json().find("\"order_panel_open\":true")!=std::string::npos,
          "accepted Anweis ACK preserves page state across Flow copy construction");
    down=flow.pointer_down(200,180);serve(down);flow.click(200,180);
    const auto editor_word_before= session->state().memory.u16(::bmh::layout::CareerSession_AnyOf_ExecutableField+2*session->state().memory.u8(::bmh::layout::ActiveManager));
    down=flow.pointer_down(200,180);
    check(down.request==FlowRequest::none,
          "logical control 5 while the supported edition editor is already open follows Source redraw-only behavior");
    flow.click(200,180);
    check(session->state().memory.u16(::bmh::layout::CareerSession_AnyOf_ExecutableField+2*session->state().memory.u8(::bmh::layout::ActiveManager))==editor_word_before&&
          flow.state_json().find("\"shooter_editor_open\":true")!=std::string::npos,
          "active logical control 5 preserves the editor and native display word");
    down=flow.pointer_down(200,140);
    check(down.request==FlowRequest::submenu_action&&std::get<PitchRequest>(down.submenu_action).page_control==2,
          "Aufst page selection is a typed Source page transition");
    serve(down);flow.click(200,140);
    check(flow.state_json().find("\"shooter_editor_open\":false")!=std::string::npos&&
          (session->state().memory.u16(::bmh::layout::CareerSession_AnyOf_ExecutableField+2*session->state().memory.u8(::bmh::layout::ActiveManager))&0x80)==0,
          "accepted Aufst page transition exits the screen-local editor and clears its Source low-80 bit");
    const auto before=save_stream(session->state());const auto before_revision=revision;
    const auto w=session->rand();const auto g=session->game();
    const auto action=flow.click(245,212);
    check(action.request==FlowRequest::submenu_action&&std::holds_alternative<PitchRequest>(action.submenu_action)&&
          std::get<PitchRequest>(action.submenu_action).kind==PitchRequestKind::set_automatic_weight,
          "actual Pitch hitbox emits the previously unsupported weight request");
    const auto request=std::get<PitchRequest>(action.submenu_action);
    auto forged=action;++std::get<PitchRequest>(forged.submenu_action).runtime_club;serve(forged);
    check(revision==before_revision&&save_stream(session->state())==before&&flow.matches_pending_pitch(request),
          "forged external FlowResult cannot bypass full real pending ownership");
    forged=action;std::get<PitchRequest>(forged.submenu_action).indoor=true;
    serve(forged);
    check(revision==before_revision&&save_stream(session->state())==before&&flow.matches_pending_pitch(request),
          "forged indoor mode cannot pass outdoor owner ACK and preserves session/pending revision");
    check(flow.click(245,212).request==FlowRequest::none,"pending Pitch mutation blocks duplicate physical input");
    auto expected=session->state();expected.manager_records[size_t(request.manager_slot)*0xb0c+0x288]=request.new_weights;
    serve(action);
    check(revision==before_revision+1&&save_stream(session->state())==save_stream(expected)&&!flow.matches_pending_pitch(request)&&
          session->rand().seed()==w.seed()&&session->rand().calls()==w.calls()&&session->game().seed()==g.seed()&&session->game().calls()==g.calls(),
          "physical Pitch weight commits exactly once through source projection and strict pending ACK");
    serve(action);check(revision==before_revision+1&&save_stream(session->state())==save_stream(expected),
          "consumed physical Pitch request replay is inert");
    const auto rollback=flow.click(272,212);check(rollback.request==FlowRequest::submenu_action,"second actual weight request is pending");
    const auto normal_date=session->state().date;++session->state().date;
    const auto rollback_bytes=save_stream(session->state());const auto rollback_revision=revision;
    const auto entry=session->lineup_checkpoint();serve(rollback);
    check(revision==rollback_revision&&save_stream(session->state())==rollback_bytes&&
          !flow.matches_pending_pitch(std::get<PitchRequest>(rollback.submenu_action))&&
          session->lineup_checkpoint().manager==entry.manager&&session->lineup_checkpoint().club==entry.club&&
          session->rand().seed()==w.seed()&&session->rand().calls()==w.calls()&&session->game().seed()==g.seed()&&session->game().calls()==g.calls(),
          "failed actual Pitch ACK rolls back complete Native/revision/prepared-entry and consumes pending rejection");
    session->state().date=normal_date;
    // The valid follow-up proves restored ownership is usable, not merely that
    // the bytes happened to compare equal after the failed projection.
    const auto retry=flow.click(272,212);check(retry.request==FlowRequest::submenu_action,"Pitch remains usable after ACK rollback");
    serve(retry);check(revision==rollback_revision+1&&!flow.matches_pending_pitch(std::get<PitchRequest>(retry.submenu_action)),
          "restored entry accepts the next physical Pitch mutation");
    const auto accept=[&](const FlowResult& result,const GameState& expected,const char* reason) {
        check(result.request==FlowRequest::submenu_action&&std::holds_alternative<PitchRequest>(result.submenu_action),
              "physical Pitch interaction emits a typed pending request");
        const auto request=std::get<PitchRequest>(result.submenu_action);const auto before_revision=revision;
        serve(result);
        check(revision==before_revision+1&&save_stream(session->state())==save_stream(expected)&&
              !flow.matches_pending_pitch(request)&&session->rand().seed()==w.seed()&&session->rand().calls()==w.calls()&&
              session->game().seed()==g.seed()&&session->game().calls()==g.calls(),reason);
        serve(result);check(revision==before_revision+1&&save_stream(session->state())==save_stream(expected),
              "accepted physical Pitch action cannot replay");
    };
    auto projected=session->preparation_projection(revision).lineup;
    const size_t board=size_t(projected.manager_slot)*0x840;
    int selected=-1,depth=-1,lane=-1;
    for(int row=0;row<11&&selected<0;++row) {
        if((projected.players.at(size_t(row)).attributes.w364>>16)&31)continue;
        for(int x=19;x>=0&&selected<0;--x)for(int y=8;y>=0&&selected<0;--y) {
            bool free=true;
            for(int other=0;other<11;++other) {
                const auto& p=projected.players.at(size_t(other));
                const int dx=std::abs(x-int8_t(p.board[0x41])),dy=std::abs(y-int8_t(p.board[0x42]));
                if(other==row) {if(dx==0&&dy==0)free=false;}
                else if(!((p.attributes.w364>>16)&31)&&dx<2&&dy<2)free=false;
            }
            if(free){selected=row;depth=x;lane=y;}
        }
    }
    check(selected>=0,"genuine prepared Pitch has a fit starter and free Source grid cell");
    check(flow.click(20,36+selected*7).request==FlowRequest::none,"physical player selection is local only");
    const auto moved=flow.click(5+depth*8,120+lane*10);
    check(moved.request==FlowRequest::submenu_action&&std::get<PitchRequest>(moved.submenu_action).kind==PitchRequestKind::move_player,
          "physical grid placement emits the previously unsupported move request");
    expected=session->state();
    for(size_t row=0;row<24;++row)if(int(row)==selected||expected.tactics[board+row*0x58+0x3f]==selected)
        expected.tactics[board+row*0x58+0x3f]=0xff;
    expected.tactics[board+size_t(selected)*0x58+0x41]=uint8_t(depth);
    expected.tactics[board+size_t(selected)*0x58+0x42]=uint8_t(lane);
    expected.manager_options[size_t(projected.manager_slot)*5]=0;
    accept(moved,expected,"physical free move ACK confirms Source coordinate/reference cleanup with no roster exchange");
    const auto extras=*session->preparation_projection(revision).pitch;
    int formation=-1;for(int f=0;f<8;++f)if(extras.formation_mask&(1<<f)){formation=f;break;}
    check(formation>=0,"genuine physical Pitch offers an available formation");
    const auto applied=flow.click(245+(formation%2)*40,113+(formation/2)*24);
    check(applied.request==FlowRequest::submenu_action&&std::get<PitchRequest>(applied.submenu_action).kind==PitchRequestKind::apply_formation,
          "physical panel click emits the previously unsupported apply request");
    expected=session->state();expected.manager_options[size_t(projected.manager_slot)*5]=uint8_t(formation+1);
    auto_lineup(expected,projected.runtime_club,0xe4);
    accept(applied,expected,"physical apply reaches Source optimizer and ACKs its complete Native result");
    const auto before_hold=save_stream(session->state());const auto before_hold_revision=revision;
    const auto before_hold_json=flow.state_json();
    for(const auto& point:std::vector<std::pair<int,int>>{{239,166},{320,166},{250,111},{250,208},{250,209},{250,210}})
        check(flow.pitch_held_left(point.first,point.second,26).request==FlowRequest::none&&
              flow.state_json()==before_hold_json&&save_stream(session->state())==before_hold&&revision==before_hold_revision,
              "held API excludes outer bounds and phantom ninth/tenth slots below the eight Source fields");
    flow.pointer_down(245,161);
    check(flow.pitch_held_left(245,161,25).request==FlowRequest::none&&
          save_stream(session->state())==before_hold&&revision==before_hold_revision,
          "held own field cannot save before the Source26th BIOS tick");
    const auto saved=flow.pitch_held_left(245,161,26);
    check(saved.request==FlowRequest::submenu_action&&std::get<PitchRequest>(saved.submenu_action).kind==PitchRequestKind::save_formation,
          "physical own-panel hold emits the previously unsupported save request");
    const auto save_request=std::get<PitchRequest>(saved.submenu_action);check(save_request.formation==4,"hold selects own slot4");
    const auto feedback_native=save_stream(session->state());const auto feedback_json=flow.state_json();
    const auto feedback_revision=revision;const auto feedback_w=session->rand();const auto feedback_g=session->game();
    check(flow.pitch_formation_save_feedback(false)&&flow.pitch_formation_save_feedback(true)&&
          save_stream(session->state())==feedback_native&&flow.state_json()==feedback_json&&revision==feedback_revision&&
          session->rand().seed()==feedback_w.seed()&&session->rand().calls()==feedback_w.calls()&&
          session->game().seed()==feedback_g.seed()&&session->game().calls()==feedback_g.calls(),
          "both pending save-feedback frames leave Native, Flow, revision and both RNGs unchanged");
    expected=session->state();
    std::copy(save_request.formation_positions.begin(),save_request.formation_positions.end(),
        expected.formations.begin()+size_t(projected.manager_slot)*0x160+4*22);
    expected.manager_records[size_t(projected.manager_slot)*0xb0c+0x218]|=0x10;
    expected.manager_options[size_t(projected.manager_slot)*5]=5;auto_lineup(expected,projected.runtime_club,0xe4);
    accept(saved,expected,"physical save binds authoritative22 bytes, updates mask/AA, optimizes and ACKs once");
    check(!flow.pitch_formation_save_feedback(false),"production save ACK removes transient feedback");
    const auto held_native=save_stream(session->state());const auto held_revision=revision;
    check(flow.pitch_held_left(245,161,90).request==FlowRequest::none&&
          flow.click(290,120).request==FlowRequest::none&&
          revision==held_revision&&save_stream(session->state())==held_native&&
          session->rand().seed()==w.seed()&&session->rand().calls()==w.calls()&&
          session->game().seed()==g.seed()&&session->game().calls()==g.calls(),
          "continued hold and its release over a different preset never dispatch or consume RNG twice");
    const auto next_hold=flow.pitch_held_left(245,161,26);
    check(next_hold.request==FlowRequest::submenu_action,"next hold is accepted after its prior release");
    flow.reject_preparation_action("declared rejection probe");
    check(flow.click(290,120).request==FlowRequest::none&&save_stream(session->state())==held_native,
          "rejected held save still owns its physical release without applying another preset");
    check(flow.pitch_held_left(245,161,26).request==FlowRequest::submenu_action,
          "own-field hold is usable again after rejection and release");
    flow.reject_preparation_action("declared close-before-release probe");
    serve(flow.pointer_down(306,228,true));
    check(flow.screen_name()=="desktop","right DOWN can close Pitch while its left release remains outstanding");
    flow.consume_orphaned_pitch_release(); // Main's right UP returned to another screen.
    const auto returned_native=save_stream(session->state());const auto returned_revision=revision;
    const auto desk_item=flow.desktop()->state().items.front();
    check(flow.click(desk_item.x,desk_item.y).request==FlowRequest::none&&
          save_stream(session->state())==returned_native&&revision==returned_revision&&
          flow.screen_name()=="desktop",
          "late left UP cannot click through to the returned desktop after right close");
}

void check_loaded_lineup(const std::filesystem::path& data, const std::filesystem::path& save_path) {
    app::CareerSession session(data, 0x4c756e61, save_path);
    uint64_t revision = 19;
    const int manager = int(session.state().memory.u8(::bmh::layout::ActiveManager));
    const int team = session.state().manager_teams.at(size_t(manager));
    const auto range = squad_range(team);
    check(team == 45 && range.count == 24, "WEEK196 fixture has the expected manager roster");
    size_t populated = 0;
    while (populated < range.count && (session.state().squad_slots[range.first + populated] & 0x0fff) != 0x0fff)
        ++populated;
    check(populated == 16, "WEEK196 contains 16 contiguous players and empty reserve rows");

    LineupRequest entry;
    entry.kind = LineupRequestKind::prepare_lineup;
    entry.manager_slot = manager; entry.runtime_club = team; entry.revision = revision;
    std::string error;
    check(session.dispatch_lineup(entry, revision, error) && error.empty(),
          "loaded CareerSession acknowledges an already prepared lineup");
    const auto source_opponent = session.prepared_lineup_opponent(manager, team, revision);
    const auto prepared_projection = session.preparation_projection(revision);
    check(source_opponent && *source_opponent == 52 && prepared_projection.pitch &&
          prepared_projection.pitch->opponent_team == 52 &&
          !prepared_projection.pitch->opponent_players.empty(),
          "genuine WEEK196 prepared desk carries its Source-resolved Aachen opponent into Pitch");
    check(!session.prepared_lineup_opponent(manager, team + 1, revision) &&
          !session.prepared_lineup_opponent(manager + 1, team, revision),
          "prepared opponent is unavailable to wrong-club or wrong-manager callers");
    if (source_opponent && prepared_projection.pitch) {
        const auto& pitch = *prepared_projection.pitch;
        PitchRequest page;
        page.kind = PitchRequestKind::select_pitch_page;
        page.manager_slot = manager; page.runtime_club = team; page.revision = revision;
        page.indoor = false; page.page_control = 4; page.opponent_team = *source_opponent;
        page.expected_display_word = pitch.native_display_word;
        page.expected_mode = uint8_t(pitch.mode);
        page.new_display_word = uint16_t(((page.expected_display_word & ~uint16_t(0x80)) & 0x008f) | 0x0040);
        page.old_weights = page.new_weights = pitch.automatic_weights;
        page.expected_set_piece_values = pitch.set_piece_takers;
        page.expected_set_piece_signature = pitch.set_piece_signature;
        auto forged = page; forged.opponent_team = (*source_opponent + 1) % int(session.state().team_flags.size());
        const auto before_forged = save_stream(session.state());
        const auto game_before_forged = session.game();
        const auto rand_before_forged = session.rand();
        check(!session.dispatch_pitch(forged, revision, error) && revision == entry.revision + 1 &&
              save_stream(session.state()) == before_forged &&
              session.game().seed() == game_before_forged.seed() && session.game().calls() == game_before_forged.calls() &&
              session.rand().seed() == rand_before_forged.seed() && session.rand().calls() == rand_before_forged.calls(),
              "ordinary Pitch rejects a caller-selected rival without changing the prepared session");
        auto stale_page = page; --stale_page.revision;
        check(!session.dispatch_pitch(stale_page, revision, error) && revision == entry.revision + 1 &&
              save_stream(session.state()) == before_forged,
              "ordinary Pitch rejects a stale-revision page request without writing");
        check(session.dispatch_pitch(page, revision, error) &&
              session.state().memory.u16(::bmh::layout::CareerSession_AnyOf_ExecutableFieldUnsigned + uint32_t(manager) * 2) == page.new_display_word,
              "ordinary Pitch selects the original Manndeckung page for the Source desk rival");
        const auto refreshed = session.preparation_projection(revision).pitch;
        check(refreshed && refreshed->opponent_team == *source_opponent &&
              !refreshed->opponent_players.empty(),
              "ordinary Pitch ACK retains the authenticated rival player rows");
    }
    const GameState before = session.state();
    GameState injured = before;
    injured.manager_options.at(size_t(manager) * 5) = 0;
    const size_t board = size_t(manager) * 0x840;
    const uint8_t starter_x = injured.tactics[board + 4 * 0x58 + 0x41];
    const uint8_t starter_y = injured.tactics[board + 4 * 0x58 + 0x42];
    const uint8_t bench_x = injured.tactics[board + 15 * 0x58 + 0x41];
    const uint8_t bench_y = injured.tactics[board + 15 * 0x58 + 0x42];
    const int injured_id = injured.squad_slots.at(range.first + 4) & 0x0fff;
    const int replacement_id = injured.squad_slots.at(range.first + 15) & 0x0fff;
    injured.players.at(size_t(injured_id)).w364 |= 1u << 16;
    const auto unchanged = save_stream(injured);
    auto_lineup(injured, team, 0x65);
    check(save_stream(injured) == unchanged, "manual 0x65 keeps its source early-return semantics");
    auto_lineup(injured, team, 100);
    check((injured.squad_slots.at(range.first + 4) & 0x0fff) == replacement_id &&
          (injured.squad_slots.at(range.first + 15) & 0x0fff) == injured_id,
          "manual 100 normalization moves the last fit player into the injured starter row");
    check(injured.tactics[board + 4 * 0x58 + 0x41] == starter_x &&
          injured.tactics[board + 4 * 0x58 + 0x42] == starter_y &&
          injured.tactics[board + 15 * 0x58 + 0x41] == bench_x &&
          injured.tactics[board + 15 * 0x58 + 0x42] == bench_y,
          "injury re-sort leaves formation coordinates attached to their row");
    check((injured.squad_slots.at(range.first + 4) & 0xc000) == 0x8000 &&
          (injured.squad_slots.at(range.first + 15) & 0xc000) == 0 &&
          (injured.player_cards.at(size_t(replacement_id)) & 0x1f) == 4,
          "manual normalization rebuilds starter flags and card identity");
    GameState wrong_mode = before;
    wrong_mode.manager_options.at(size_t(manager) * 5) = 0;
    wrong_mode.indoor = !wrong_mode.indoor;
    GameState formation_zero = wrong_mode;
    formation_zero.manager_options.at(size_t(manager) * 5) = 1; // assistant formation 0 reference
    const size_t badge_at = size_t(team) * 2;
    const uint16_t original_badge = uint16_t(formation_zero.club_heap_78.at(badge_at) |
                                              uint16_t(formation_zero.club_heap_78.at(badge_at + 1)) << 8);
    const uint16_t indoor_badge = uint16_t((original_badge & 0x01ff) | (unsigned(formation_zero.indoor) << 9));
    formation_zero.club_heap_78[badge_at] = uint8_t(indoor_badge);
    formation_zero.club_heap_78[badge_at + 1] = uint8_t(indoor_badge >> 8);
    auto_lineup(wrong_mode, team, 100);
    auto_lineup(formation_zero, team, 100);
    formation_zero.manager_options.at(size_t(manager) * 5) = 0;
    check(wrong_mode.club_heap_78.at(badge_at) == uint8_t(indoor_badge) &&
          wrong_mode.club_heap_78.at(badge_at + 1) == uint8_t(indoor_badge >> 8) &&
          save_stream(wrong_mode) == save_stream(formation_zero),
          "manual crest/mode mismatch source-normalizes to the ordinary formation-0 assistant path");
    check(wrong_mode.manager_options.at(size_t(manager) * 5) == 0,
          "mode-transition fallback preserves the manager's manual-AA setting");

    LineupRequest swap;
    swap.kind = LineupRequestKind::swap_players;
    swap.manager_slot = manager; swap.runtime_club = team; swap.revision = revision;
    swap.first_row = 0; swap.second_row = 11;
    swap.first_slot = before.squad_slots.at(range.first);
    swap.second_slot = before.squad_slots.at(range.first + 11);
    swap.first_player = swap.first_slot & 0x0fff;
    swap.second_player = swap.second_slot & 0x0fff;
    const auto game_calls = session.game().calls(), random_calls = session.rand().calls();
    check(session.dispatch_lineup(swap, revision, error) && error.empty(),
          "loaded CareerSession accepts one identity-bound field/bench swap");
    const uint16_t first = before.squad_slots.at(range.first), second = before.squad_slots.at(range.first + 11);
    check(session.state().manager_options.at(size_t(manager) * 5) == 0 &&
          session.state().squad_slots.at(range.first) == uint16_t((second & 0x1fff) | (first & 0xc000)) &&
          session.state().squad_slots.at(range.first + 11) == uint16_t((first & 0x1fff) | (second & 0xc000)) &&
          (session.state().player_cards.at(size_t(swap.first_player)) & 0x1f) == 11 &&
          (session.state().player_cards.at(size_t(swap.second_player)) & 0x1f) == 0,
          "accepted swap moves players but keeps starter/bench flags on their roster rows");
    check(session.game().calls() == game_calls && session.rand().calls() == random_calls,
          "pre-kickoff swap consumes no RNG");

    bool saw_fixture = false, order_preserved = false, live_overview_roundtrip = false;
    session.set_live_minute([&](int minute, MatchEngine& engine, std::vector<MatchGroup>& groups, GameState& state) {
        if (!live_overview_roundtrip && minute >= 3 && !groups.empty() && groups[0].desc.size() > 0xcf &&
            groups[0].desc[0] == 0 && std::any_of(groups.begin(),groups.end(),[&](const MatchGroup& group) {
                return std::find(group.fixtures.begin(),group.fixtures.end(),team)!=group.fixtures.end();
            })) {
            const int group_index = 0;
            app::CareerSession::LiveOverviewRequest live;
            live.key_slot=0; live.minute=minute; live.group_index=group_index;
            live.manager_slot=manager; live.runtime_team=team; live.revision=revision;
            live.sequence=0xfeed; // Deliberately not a predicted Session scope.
            auto opened=session.open_live_overview(live,engine,revision,Image{},error);
            if(!opened)throw std::runtime_error("actual WEEK196 live G key failed to open: "+error);
            check(opened->kind==app::CareerSession::LiveOverviewKind::league_table && opened->table.has_value(),
                  "real kickoff observer opens the Source G table for the first league descriptor");
            ui::LeagueFamilyRequest close;
            close.identity=opened->family_identity; close.kind=ui::LeagueFamilyRequestKind::close;
            close.from_child=1; close.target_child=0;
            auto closed=session.dispatch_live_overview(app::CareerSession::LiveOverviewAction{close},engine,revision,error);
            if(!closed)throw std::runtime_error("actual WEEK196 live G key failed to close: "+error);
            check(closed->closed && closed->preparation.has_value() && error.empty(),
                  "real kickoff observer closes the Source G family and resumes the same minute");
            live_overview_roundtrip=true;
        }
        if (saw_fixture) return;
        for (const auto& group : groups) {
            if (std::find(group.fixtures.begin(), group.fixtures.end(), team) == group.fixtures.end()) continue;
            saw_fixture = true;
            const auto squad = squad_range(team);
            order_preserved = (state.squad_slots.at(squad.first) & 0x0fff) == (second & 0x0fff) &&
                              (state.squad_slots.at(squad.first + 11) & 0x0fff) == (first & 0x0fff);
        }
    });
    int turns = 0;
    for (; turns < 8 && (!saw_fixture || turns < 2); ++turns) {
        ui::DeskRequest request;
        request.kind = ui::DeskRequestKind::advance_turn;
        request.manager_slot = int(session.state().memory.u8(::bmh::layout::ActiveManager));
        request.revision = revision;
        const auto advanced = session.dispatch_kickoff(request, revision, error);
        if (advanced.kind == TurnDeskKind::Failed)
            throw std::runtime_error("loaded production kickoff after swap: " + error);
    }
    check(turns >= 2, "two real CareerSession turns complete after the manual swap");
    check(saw_fixture && order_preserved,
          "the swapped row order reaches a played manager fixture after engine preparation");
    check(live_overview_roundtrip,
          "genuine WEEK196 set_live_minute observer accepts and closes G while calendar advance is active");
}

void check_loaded_live_settings(const std::filesystem::path& data,const std::filesystem::path& save_path) {
    app::CareerSession session(data,0x4c756e61,save_path);
    uint64_t revision=0;std::string error;
    const int manager=int(session.state().memory.u8(::bmh::layout::ActiveManager));
    const int team=session.state().manager_teams.at(size_t(manager));
    check(team==45,"live settings fixture is the genuine WEEK196 manager-45 save");
    bool exercised=false;
    session.set_live_minute([&](int minute,MatchEngine& engine,std::vector<MatchGroup>& groups,GameState&) {
        if(exercised||minute<3||groups.empty())return;
        const auto before=session.state();
        const auto game_seed=session.game().seed();const auto game_calls=session.game().calls();
        const auto rand_seed=session.rand().seed();const auto rand_calls=session.rand().calls();
        app::CareerSession::LiveSettingsRequest request;
        request.minute=minute;request.manager_slot=manager;request.runtime_team=team;request.revision=revision;
        auto opened=session.open_live_settings(request,engine,revision,error);
        if(!opened)throw std::runtime_error("genuine WEEK196 S settings failed to open: "+error);
        const size_t k5=10*9+4;
        const auto entry_runtime=session.club_modal_runtime_checkpoint();
        check(opened->state.cells[k5]==before.memory.u8(::bmh::layout::LeaguePresentationOptions+uint32_t(k5))&&
              opened->state.display_cells.has_value()&&
              session_runtime_byte(session.state(),entry_runtime,::bmh::layout::CareerSession_LiveBoundaryFamilyScope_ExecutableField)==0,
              "live S entry preserves K5 raw value, presents availability and clears the Source redraw latch");
        for(size_t i=0;i<180;++i)if(i!=5&&i!=6)
            check(session.state().memory.u8(::bmh::layout::LeaguePresentationOptions+uint32_t(i))==(*opened->state.display_cells)[i],
                  "live S entry applies Source availability flags");
        const auto ein_root=std::filesystem::temp_directory_path()/
            ("openbmh-live-settings-ein-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(ein_root);
        const auto ein=ein_root/"LIVE.EIN";
        ui::SettingsScreen screen(data,opened->state);
        check(screen.click(10,214)&&screen.pending_file(),"live S Original control creates a typed .EIN save request");
        auto live_save=*screen.pending_file();live_save.path=ein;
        auto file_saved=session.dispatch_live_settings(app::CareerSession::LiveSettingsAction{
            app::CareerSession::LiveSettingsFile{opened->owner.sequence,live_save}},engine,revision,error);
        if(!file_saved)throw std::runtime_error("live S .EIN save failed: "+error);
        opened->owner=file_saved->owner;screen.acknowledge_file(file_saved->state);
        check(std::filesystem::file_size(ein)==180,"live S .EIN save writes exactly 180 raw settings bytes");
        auto stale=opened->owner;++stale.sequence;
        const auto unchanged=save_stream(session.state());
        auto rejected=session.dispatch_live_settings(app::CareerSession::LiveSettingsAction{
            app::CareerSession::LiveSettingsCloseRequest{stale}},engine,revision,error);
        check(!rejected&&save_stream(session.state())==unchanged,"live S rejects forged owner without writes");
        check(screen.click(175,129)&&screen.pending(),"Original K5 hitbox produces typed settings write");
        const auto before_generic=save_stream(session.state());
        auto generic=*screen.pending();
        check(!session.dispatch_settings(generic,revision,error)&&save_stream(session.state())==before_generic,
              "generic settings API cannot bypass the active live S owner");
        app::CareerSession::LiveSettingsWrite write{opened->owner.sequence,*screen.pending()};
        auto ack=session.dispatch_live_settings(app::CareerSession::LiveSettingsAction{write},engine,revision,error);
        if(!ack)throw std::runtime_error("live S K5 write failed: "+error);
        check(engine.buttons.size()==180&&engine.buttons[k5]==session.state().memory.u8(::bmh::layout::LeaguePresentationOptions+uint32_t(k5)),
              "live S ACK refreshes the MatchEngine button cache");
        screen.acknowledge(ack->state);
        check(screen.click(10,201)&&screen.pending_file(),"live S Original control creates a typed .EIN load request");
        auto live_load=*screen.pending_file();live_load.path=ein;
        auto file_loaded=session.dispatch_live_settings(app::CareerSession::LiveSettingsAction{
            app::CareerSession::LiveSettingsFile{ack->owner.sequence,live_load}},engine,revision,error);
        if(!file_loaded)throw std::runtime_error("live S .EIN load failed: "+error);
        screen.acknowledge_file(file_loaded->state);
        check(session.state().memory.u8(::bmh::layout::LeaguePresentationOptions+uint32_t(k5))==before.memory.u8(::bmh::layout::LeaguePresentationOptions+uint32_t(k5))&&
              engine.buttons[k5]==session.state().memory.u8(::bmh::layout::LeaguePresentationOptions+uint32_t(k5)),
              "live S .EIN load restores K5 and refreshes the running MatchEngine buttons");
        auto closed=session.dispatch_live_settings(app::CareerSession::LiveSettingsAction{
            app::CareerSession::LiveSettingsCloseRequest{file_loaded->owner}},engine,revision,error);
        if(!closed)throw std::runtime_error("genuine WEEK196 S settings failed to close: "+error);
        const auto& memory=session.state().memory;
        check(closed->closed&&memory.u8(::bmh::layout::LeaguePresentationOptions+uint32_t(k5))==before.memory.u8(::bmh::layout::LeaguePresentationOptions+uint32_t(k5)),
              "live S .EIN load restores the Original K5 value before close");
        const auto runtime=session.club_modal_runtime_checkpoint();
        check(session.state().live_scene==uint8_t(1-(memory.u8(::bmh::layout::LeaguePresentationOptions+13)&1))&&
              session_runtime_byte(session.state(),runtime,::bmh::layout::CareerSession_DispatchLiveSubstitution_ExecutableField)==memory.u8(::bmh::layout::CareerSession_FormatError_RuntimeField2)&&
              session_runtime_byte(session.state(),runtime,::bmh::layout::CareerSession_FormatError_ExecutableField8)==1,
              "live S close applies Original scene/substitution/redraw latches");
        uint32_t scene_code=0;
        for(uint32_t i=0;i<6;++i)scene_code=scene_code*10+memory.u8(::bmh::layout::CareerSession_FormatError_RuntimeField + (i+14)*9);
        const uint32_t saved_scene_code=uint32_t(session_runtime_byte(session.state(),runtime,::bmh::layout::CareerSession_FormatError_RuntimeField9))|
            uint32_t(session_runtime_byte(session.state(),runtime,::bmh::layout::FixtureControllerValueByteOne))<<8|
            uint32_t(session_runtime_byte(session.state(),runtime,::bmh::layout::FixtureControllerValueByteTwo))<<16|
            uint32_t(session_runtime_byte(session.state(),runtime,::bmh::layout::FixtureControllerValueByteThree))<<24;
        check(saved_scene_code==scene_code,"live S close derives the six-cell Source scene code");
        check(session.game().seed()==game_seed&&session.game().calls()==game_calls&&
              session.rand().seed()==rand_seed&&session.rand().calls()==rand_calls,
              "live S settings consume no game RNG");
        std::filesystem::remove_all(ein_root);
        exercised=true;
    });
    for(int turn=0;turn<8&&!exercised;++turn) {
        ui::DeskRequest request;request.kind=ui::DeskRequestKind::advance_turn;
        request.manager_slot=manager;request.revision=revision;
        const auto result=session.dispatch_kickoff(request,revision,error);
        if(result.kind==TurnDeskKind::Failed)throw std::runtime_error("WEEK196 live settings kickoff failed: "+error);
    }
    check(exercised,"actual WEEK196 set_live_minute callback exercised S settings lifecycle");
}

void check_settings_files(const std::filesystem::path& data,const std::filesystem::path& save_path) {
    app::CareerSession session(data,0x4c756e61,save_path);
    auto& game=session.state();
    uint64_t revision=7;std::string error;
    const int manager=int(session.state().memory.u8(::bmh::layout::ActiveManager));
    auto desk=session.desktop_projection(revision);
    const auto source=std::find_if(desk.items.begin(),desk.items.end(),[](const ui::DeskItem& item) {
        return item.kind==19&&!item.disabled;
    });
    check(source!=desk.items.end(),"genuine desk fixture exposes the enabled Original menu-19 settings item");
    if(source==desk.items.end())return;
    ui::DeskRequest request{ui::DeskRequestKind::open_menu,manager,source->id,19,-1,revision};
    auto projection=session.open_desk_settings(request,revision,error);
    if(!projection)throw std::runtime_error("Original desk settings open failed: "+error);
    auto wrong=request;wrong.item_id=(source->id+1)%54;wrong.revision=revision;
    check(!session.open_desk_settings(wrong,revision,error),"settings owner rejects a different desk record");

    const auto root=std::filesystem::temp_directory_path()/
        ("openbmh-settings-files-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto ein=root/"ORIGINAL.EIN";
    const GameState baseline=session.state();
    const auto game_seed=session.game().seed();const auto game_calls=session.game().calls();
    const auto rand_seed=session.rand().seed();const auto rand_calls=session.rand().calls();
    const auto file_request=[&](ui::SaveLoadAction action,const std::filesystem::path& path,bool overwrite=false) {
        ui::SaveLoadRequest file;file.action=action;file.manager_slot=manager;file.revision=revision;
        file.path=path;file.overwrite=overwrite;file.file_kind=ui::SaveLoadFileKind::settings;
        file.origin_item_id=projection->owner.source_item_id;return file;
    };
    app::CareerSession::DeskSettingsFile save{projection->owner.sequence,
        file_request(ui::SaveLoadAction::save,ein)};
    auto saved=session.dispatch_desk_settings(app::CareerSession::DeskSettingsAction{save},revision,error);
    if(!saved)throw std::runtime_error("Original .EIN save failed: "+error);
    projection->owner=saved->owner;
    check(std::filesystem::file_size(ein)==180,"Original .EIN is exactly 0xb4 raw bytes");
    const Bytes payload=read_file(ein);
    for(size_t i=0;i<180;++i)check(payload[i]==baseline.memory.u8(::bmh::layout::LeaguePresentationOptions+uint32_t(i)),
        "Original .EIN save preserves the active raw settings block");

    ui::SettingsScreen screen(data,saved->state);
    const size_t k5=10*9+4;
    check(screen.click(175,129)&&screen.pending(),"Original K5 yields a semantic setting action");
    const auto unchanged=save_stream(session.state());
    auto generic=*screen.pending();
    check(!session.dispatch_settings(generic,revision,error)&&save_stream(session.state())==unchanged,
          "generic settings dispatcher cannot bypass active desk-menu owner");
    auto write=session.dispatch_desk_settings(app::CareerSession::DeskSettingsAction{
        app::CareerSession::DeskSettingsWrite{projection->owner.sequence,*screen.pending()}},revision,error);
    if(!write)throw std::runtime_error("Original desk K5 write failed: "+error);
    projection->owner=write->owner;
    check(session.state().memory.u8(::bmh::layout::LeaguePresentationOptions+uint32_t(k5))!=baseline.memory.u8(::bmh::layout::LeaguePresentationOptions+uint32_t(k5)),
          "Original settings dispatcher toggles K5 once");

    const GameState mutated=session.state();const auto runtime=session.club_modal_runtime_checkpoint();
    const uint64_t mutated_revision=revision;
    auto checkpoint=session.desk_settings_checkpoint(revision);
    const auto bad=root/"SHORT.EIN";{std::ofstream out(bad,std::ios::binary);out.write("bad",3);}
    auto invalid=session.dispatch_desk_settings(app::CareerSession::DeskSettingsAction{
        app::CareerSession::DeskSettingsFile{projection->owner.sequence,file_request(ui::SaveLoadAction::load,bad)}},revision,error);
    check(!invalid&&revision==mutated_revision&&save_stream(session.state())==save_stream(mutated),
          "wrong-length .EIN load is rejected atomically");
    auto loaded=session.dispatch_desk_settings(app::CareerSession::DeskSettingsAction{
        app::CareerSession::DeskSettingsFile{projection->owner.sequence,file_request(ui::SaveLoadAction::load,ein)}},revision,error);
    if(!loaded)throw std::runtime_error("Original .EIN load failed: "+error);
    projection->owner=loaded->owner;
    check(save_stream(session.state())==save_stream(baseline),
          "Original .EIN load restores settings and leaves unrelated native state unchanged");
    check(session.game().seed()==game_seed&&session.game().calls()==game_calls&&
          session.rand().seed()==rand_seed&&session.rand().calls()==rand_calls&&runtime==session.club_modal_runtime_checkpoint(),
          "ordinary .EIN save/load consumes no RNG and preserves runtime latches");

    session.restore_desk_settings_checkpoint(std::move(checkpoint),revision);
    check(revision==mutated_revision&&save_stream(session.state())==save_stream(mutated)&&
          session.club_modal_runtime_checkpoint()==runtime,
          "settings checkpoint restores GameState, runtime and revision after child ACK failure");
    projection->owner.revision=revision;
    auto reloaded=session.dispatch_desk_settings(app::CareerSession::DeskSettingsAction{
        app::CareerSession::DeskSettingsFile{projection->owner.sequence,file_request(ui::SaveLoadAction::load,ein)}},revision,error);
    if(!reloaded)throw std::runtime_error(".EIN load after restored owner failed: "+error);
    auto closed=session.dispatch_desk_settings(app::CareerSession::DeskSettingsAction{
        app::CareerSession::DeskSettingsClose{reloaded->owner}},revision,error);
    if(!closed||!closed->closed)throw std::runtime_error("Original settings close failed: "+error);

    // legacy routine snapshots byte H8 (CareerSession_DispatchStrengths_ExecutableField) before the settings menu. Its close
    // path adjusts the signed countdown only from that entry value, including
    // across a failed child-ACK checkpoint restore. H9 (Main_FormatError_RuntimeField4) is independent.
    const uint32_t h8=::bmh::layout::LeaguePresentationOptions+7*9+7;
    const uint32_t h9=h8+1;
    const auto set_timer=[&](uint32_t remaining) {
        for(uint32_t i=0;i<4;++i) {
            if(!game.memory.has(::bmh::layout::CareerSession_FormatError_ExecutableField2+i))game.memory.add_block(::bmh::layout::CareerSession_FormatError_ExecutableField2+i,1);
            game.memory.set8(::bmh::layout::CareerSession_FormatError_ExecutableField2+i,uint8_t(remaining>>(8*i)));
        }
    };
    const auto timer_value=[&] { return game.memory.u32(::bmh::layout::CareerSession_FormatError_ExecutableField2); };
    const auto open_timer_settings=[&](uint8_t initial,uint32_t remaining) {
        game.memory.set8(h8,initial);game.memory.set8(h9,0xff);set_timer(remaining);
        auto owner_request=request;owner_request.revision=revision;
        auto opened=session.open_desk_settings(owner_request,revision,error);
        if(!opened)throw std::runtime_error("H8 settings open failed: "+error);
        return *opened;
    };
    const auto edit_h8=[&](const app::CareerSession::DeskSettingsProjection& opened,
                           ui::SettingsEdit operation) {
        ui::SettingsWriteRequest write{manager,revision,opened.state.cells,7,7,operation};
        auto result=session.dispatch_desk_settings(app::CareerSession::DeskSettingsAction{
            app::CareerSession::DeskSettingsWrite{opened.owner.sequence,write}},revision,error);
        if(!result)throw std::runtime_error("H8 settings write failed: "+error);
        return *result;
    };
    const auto close_timer_settings=[&](const app::CareerSession::DeskSettingsOwner& owner) {
        auto result=session.dispatch_desk_settings(app::CareerSession::DeskSettingsAction{
            app::CareerSession::DeskSettingsClose{owner}},revision,error);
        if(!result||!result->closed)throw std::runtime_error("H8 settings close failed: "+error);
    };
    const auto before_rng=std::pair{session.rand().seed(),session.rand().calls()};
    auto zero_to_one=open_timer_settings(0,0x12345678);
    check(zero_to_one.state.cells[7*9+7]==0&&zero_to_one.state.cells[7*9+8]==0xff,
          "H8 is a byte cell and adjacent H9 remains independent");
    auto zero_write=edit_h8(zero_to_one,ui::SettingsEdit::increment);
    close_timer_settings(zero_write.owner);
    check(game.memory.u8(h8)==1&&game.memory.u8(h9)==0xff&&timer_value()==0x12345678,
          "changing H8 from zero does not shift the process countdown");

    auto to_zero=open_timer_settings(1,0x12345678);
    auto zero_result=edit_h8(to_zero,ui::SettingsEdit::decrement);
    close_timer_settings(zero_result.owner);
    check(game.memory.u8(h8)==0&&timer_value()==1,
          "closing settings with H8 zero sets the Source countdown to one");

    auto delta_owner=open_timer_settings(1,1000);
    auto h8_checkpoint=session.desk_settings_checkpoint(revision);
    auto transient_zero=edit_h8(delta_owner,ui::SettingsEdit::decrement);
    (void)transient_zero;
    const uint64_t changed_revision=revision;
    session.restore_desk_settings_checkpoint(std::move(h8_checkpoint),revision);
    check(revision+1==changed_revision&&game.memory.u8(h8)==1&&timer_value()==1000,
          "settings child rollback restores H8, countdown and owner revision");
    auto delta_result=edit_h8(delta_owner,ui::SettingsEdit::increment);
    close_timer_settings(delta_result.owner);
    check(game.memory.u8(h8)==2&&game.memory.u8(h9)==0xff&&timer_value()==1060,
          "H8 nonzero delta adjusts the 32-bit Source countdown by signed minutes times sixty");
    check(session.rand().seed()==before_rng.first&&session.rand().calls()==before_rng.second,
          "H8 edit and close consume no game randomness");
    std::filesystem::remove_all(root);
}

void check_contract_controller(const std::filesystem::path& data) {
    const auto save_directory = std::filesystem::temp_directory_path() /
        ("openbmh-contract-original-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(save_directory);
    StartupFlow flow(data, save_directory);
    flow.click(150, 215); flow.click(5, 225, true); flow.click(5, 225, true);
    const auto started = flow.click(90, 169);
    std::unique_ptr<app::CareerSession> session;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    uint64_t revision = 0;
    const auto serve = [&](const FlowResult& input) {
        check(app::serve_flow_request(flow, input, session, data, save_directory, 0x48c0ffee,
              revision, playback, audio_session, false).empty(), "Original contract inputs use the production controller");
    };
    serve(started);
    check(session && flow.screen_name() == "desktop", "Original contract workflow starts on a live desk");
    const auto menu = [&](int kind) {
        const auto it = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
            [&](const DeskItem& item) { return item.kind == kind && !item.disabled; });
        check(it != flow.desktop()->state().items.end(), "required desk menu is available");
        return flow.click(it->x + it->width / 2, it->y + it->height / 2);
    };
    serve(menu(27));
    check(flow.screen_name() == "contracts", "menu 27 opens the Original roster");
    const auto roster_bytes = save_stream(session->state());
    const auto roster_rand = session->rand();
    const auto roster_game = session->game();
    const auto roster_revision = revision;
    const auto expired_roster = flow.process_timer_expired(61, 33);
    check(expired_roster.request == FlowRequest::contracts_closed && flow.screen_name() == "desktop",
          "40cec event8 outside the name column returns the roster to its desk");
    serve(expired_roster);
    check(save_stream(session->state()) == roster_bytes && revision == roster_revision &&
          session->rand().seed() == roster_rand.seed() && session->rand().calls() == roster_rand.calls() &&
          session->game().seed() == roster_game.seed() && session->game().calls() == roster_game.calls(),
          "roster deadline handback changes no saved bytes, revision or either RNG stream");
    serve(menu(27));
    const auto info_bytes=save_stream(session->state());const auto info_rand=session->rand();const auto info_revision=revision;
    const auto info=flow.click(20,33);
    check(info.request==FlowRequest::submenu_opened && flow.screen_name()=="player_card", "Original left-click opens the existing player info card");
    check(!flow.process_timer_expired(61, 33).changed && flow.screen_name() == "player_card",
          "roster timer does not steal the player-info child input owner");
    check(flow.send_key("ESC").request==FlowRequest::submenu_closed && flow.screen_name()=="contracts" &&
          save_stream(session->state())==info_bytes && revision==info_revision && session->rand().seed()==info_rand.seed() &&
          session->rand().calls()==info_rand.calls(), "closing player info returns to the contract roster without game or RNG effects");
    const int manager = int(session->state().memory.u8(::bmh::layout::ActiveManager));
    auto roster = contracts_from_game(session->state(), manager, revision);
    std::vector<ContractPlayer> negotiable;
    for (const auto& p : roster.players) {
        auto g = session->state(); auto rng = session->rand();
        const auto demands = contract_demands(g, manager, p.row, true, rng);
        if (demands[0] > 100 && demands[0] < 200000) negotiable.push_back(p);
    }
    check(negotiable.size() >= 2, "two players can be used for independent acceptance and refusal");
    const auto visual_row = [&](int row) {
        return int(std::distance(roster.players.begin(), std::find_if(roster.players.begin(), roster.players.end(),
            [&](const ContractPlayer& p) { return p.row == row; })));
    };
    const auto talk = [&](int row) {
        // legacy routine: right click in the Original name column opens the talk.
        const auto result = flow.click(20, 32 + visual_row(row) * 7, true);
        check(result.request == FlowRequest::contract_action &&
              result.contract_action.kind == ContractActionKind::request_demands,
              "right-clicking the Original row requests its salary demands");
        return result;
    };
    const auto calculator = [&](uint32_t value) {
        check(flow.click(76, 132).changed, "Original C calculator key clears the entry");
        if (value) check(flow.text(std::to_string(value)), "calculator accepts an explicit numeric value");
        return flow.click(94, 132); // Original J, not an invented salary field.
    };
    const auto offer = [&](int row, uint8_t term, uint32_t salary) {
        const int top = row < 13 ? 130 : 22;
        check(flow.click(100 + int(term) * 60, top + 52).changed,
              "clicking a term opens the Original salary calculator");
        check(calculator(salary).changed, "Original J confirms the local offer");
        const auto result = flow.click(125, top + 80);
        check(result.request == FlowRequest::contract_action && result.contract_action.kind == ContractActionKind::submit_offer &&
              result.contract_action.monthly_salary == salary && result.contract_action.term_column == term,
              "ANGEBOT MACHEN sends the selected monthly offer");
        return result;
    };
    const int row = negotiable[0].row;
    { // Source-level contract mutations, independent of UI widgets.
        const auto initial = roster;
        const auto& game = session->state();
        const auto player = negotiable[0];
        auto probe_game = game; auto probe_rand = session->rand();
        const auto expected_demands = contract_demands(probe_game, manager, row, true, probe_rand);
    {
        const size_t board = size_t(initial.manager_slot) * 0x840 + size_t(row) * 0x58;
        std::array<uint8_t, 7> proposed{};
        for (size_t i = 0; i < proposed.size(); ++i) {
            const uint8_t current = game.tactics.at(board + 0x12 + i);
            proposed[i] = uint8_t(current + (i < 3 ? (current < 20) : (current < 200)));
        }
        auto accepted_with_bonus = game;
        auto bonus_rng = session->rand();
        const bool accepted = contract_offer(accepted_with_bonus, initial.manager_slot, row, 0,
            expected_demands[0], expected_demands[0], false, false, bonus_rng, &proposed);
        check(accepted, "a full-demand offer with source-range bonus proposals is accepted");
        for (size_t i = 0; i < proposed.size(); ++i)
            check(accepted_with_bonus.tactics.at(board + 0x12 + i) == proposed[i],
                  "accepted offer commits the seven proposed bonus rates");

        auto declined_with_bonus = game;
        auto decline_rng = session->rand();
        const auto before_bonus = save_stream(declined_with_bonus);
        const bool declined = contract_offer(declined_with_bonus, initial.manager_slot, row, 0, 1,
            expected_demands[0], false, false, decline_rng, &proposed);
        check(!declined, "a one-EU offer with bonus proposals is refused");
        for (size_t i = 0; i < proposed.size(); ++i)
            check(declined_with_bonus.tactics.at(board + 0x12 + i) == game.tactics.at(board + 0x12 + i),
                  "refusal leaves the current bonus rates unchanged");
        check(save_stream(declined_with_bonus) != before_bonus,
              "a genuine refusal still applies the original mood/refusal effects");

        auto lower_bonus_game = game;
        lower_bonus_game.tactics.at(board + 0x12) = 5;
        auto lower_proposal = proposed;
        lower_proposal[0] = 0;
        auto lower_rng = session->rand();
        check(contract_offer(lower_bonus_game, initial.manager_slot, row, 0,
              expected_demands[0], expected_demands[0], false, false, lower_rng, &lower_proposal) &&
              lower_bonus_game.tactics.at(board + 0x12) == 0,
              "salary negotiation may lower a proposed bonus and commits it only on acceptance");

        auto bonus_only = game;
        auto requested_rates = proposed;
        check(contract_bonuses(bonus_only, initial.manager_slot, row, requested_rates),
              "individual bonus page commits a source-range monotone increase");
        const auto changed_bonus_bytes = save_stream(bonus_only);
        check(!contract_bonuses(bonus_only, initial.manager_slot, row, requested_rates) &&
              save_stream(bonus_only) == changed_bonus_bytes,
              "unchanged bonus submission is a no-op rather than a second commit");
        auto decrease_rates = requested_rates;
        decrease_rates[0] = uint8_t(requested_rates[0] - 1);
        check(!contract_bonuses(bonus_only, initial.manager_slot, row, decrease_rates) &&
              save_stream(bonus_only) == changed_bonus_bytes,
              "bonus-only page rejects reductions atomically");

        auto team_bonus = game;
        std::array<uint8_t, 7> team_rates{};
        const SquadRange team_range = squad_range(initial.runtime_club);
        std::vector<size_t> occupied_rows;
        for (size_t r = 0; r < team_range.count; ++r)
            if ((team_bonus.squad_slots.at(team_range.first + r) & 0x0fff) != 0x0fff)
                occupied_rows.push_back(r);
        check(occupied_rows.size() >= 2, "team bonus fixture has at least two occupied player rows");
        const size_t first_board = size_t(initial.manager_slot) * 0x840 + occupied_rows[0] * 0x58;
        const size_t second_board = size_t(initial.manager_slot) * 0x840 + occupied_rows[1] * 0x58;
        team_bonus.tactics.at(first_board + 0x12) = 5;
        team_bonus.tactics.at(second_board + 0x12) = 12;
        team_rates[0] = 9;
        check(contract_bonuses(team_bonus, initial.manager_slot, -1, team_rates) &&
              team_bonus.tactics.at(first_board + 0x12) == 9 &&
              team_bonus.tactics.at(second_board + 0x12) == 12,
              "team proposal is checked against the current minimum and raises only lower rows");
        const auto heterogeneous_after = save_stream(team_bonus);
        check(!contract_bonuses(team_bonus, initial.manager_slot, -1, team_rates) &&
              save_stream(team_bonus) == heterogeneous_after,
              "team proposal equal to the floor is an inert no-op when no row needs a raise");

        team_bonus = game;
        team_rates = {};
        for (size_t i = 0; i < team_rates.size(); ++i) {
            uint8_t maximum = 0;
            for (size_t r = 0; r < team_range.count; ++r)
                if ((team_bonus.squad_slots.at(team_range.first + r) & 0x0fff) != 0x0fff)
                    maximum = std::max(maximum, team_bonus.tactics.at(size_t(initial.manager_slot) * 0x840 + r * 0x58 + 0x12 + i));
            team_rates[i] = uint8_t(std::min<int>(i < 3 ? 20 : 200, maximum + (maximum < (i < 3 ? 20 : 200))));
        }
        const auto before_team_rates = save_stream(team_bonus);
        const bool team_changed = contract_bonuses(team_bonus, initial.manager_slot, -1, team_rates);
        check(team_changed && save_stream(team_bonus) != before_team_rates,
              "team bonus page commits monotone rates across the manager squad");
        for (size_t r = 0; r < team_range.count; ++r) {
            if ((team_bonus.squad_slots.at(team_range.first + r) & 0x0fff) == 0x0fff) continue;
            for (size_t i = 0; i < team_rates.size(); ++i)
                check(team_bonus.tactics.at(size_t(initial.manager_slot) * 0x840 + r * 0x58 + 0x12 + i) <= team_rates[i],
                      "team bonus page never lowers a player's existing rate");
        }
    }
    {
        const size_t board = size_t(initial.manager_slot) * 0x840 + size_t(row) * 0x58;
        auto insured = game;
        insured.tactics.at(board + 0x0b) &= uint8_t(~0x60);
        insured.tactics.at(board + 0x43) = 0;
        insured.players.at(size_t(player.player_id)).w35c &= 0x07ffffffu;
        const uint32_t salary = uint32_t(insured.tactics.at(board + 0x50)) |
            (uint32_t(insured.tactics.at(board + 0x51)) << 8) |
            (uint32_t(insured.tactics.at(board + 0x52)) << 16) |
            (uint32_t(insured.tactics.at(board + 0x53)) << 24);
        const uint16_t expected_monthly_fee = uint16_t((uint32_t(salary) / 100) * 10);
        const auto insurance = contract_insurance(insured, initial.manager_slot, row, 0, 60);
        check(insurance.accepted && insurance.monthly_fee == expected_monthly_fee &&
              insured.tactics.at(board + 0x43) == 60,
              "insurance stores 60 months, class A, and the source monthly fee without RNG input");
        const auto insured_bytes = save_stream(insured);
        const auto repeated_insurance = contract_insurance(insured, initial.manager_slot, row, 1, 30);
        check(!repeated_insurance.accepted && save_stream(insured) == insured_bytes,
              "insurance can only be selected once and duplicate selection is inert");
    }
    }
    auto demand_request = flow.process_timer_expired(20, 32 + visual_row(row) * 7);
    check(demand_request.request == FlowRequest::contract_action &&
          demand_request.contract_action.kind == ContractActionKind::request_demands &&
          demand_request.contract_action.row == row,
          "40cec event8 over the actual player row enters its normal demand request");
    const auto pending_quote_json = flow.state_json();
    check(!flow.process_timer_expired(61, 33).changed && flow.state_json() == pending_quote_json,
          "a pending demand keeps its ACK rather than being closed or redispatched by the timer");
    auto expected = session->state(); auto rng = session->rand();
    auto demands = contract_demands(expected, manager, row, true, rng);
    const auto before_quote = save_stream(session->state()); const auto quote_revision = revision;
    serve(demand_request);
    check(save_stream(session->state()) == before_quote && revision == quote_revision + 1 &&
          session->rand().seed() == rng.seed() && session->rand().calls() == rng.calls(),
          "Original talk entry has exactly the demand RNG effects and no saved-byte changes");
    const auto cancel_bytes = save_stream(session->state());
    const auto cancel_rand = session->rand();
    const auto cancel_game = session->game();
    const auto cancel_revision = revision;
    const int timer_top = row < 13 ? 130 : 22;
    check(flow.click(100, timer_top + 52).changed && flow.click(76, 132).changed && flow.text("17000"),
          "real contract calculator accepts an unconfirmed salary draft");
    const auto expired_calculator = flow.process_timer_expired(61, 33);
    check(expired_calculator.changed && expired_calculator.request == FlowRequest::none &&
          !flow.pending_contract_action() && flow.state_json().find("\"page\":1") != std::string::npos,
          "calculator deadline returns to its talk with no salary or insurance command");
    check(flow.click(285, timer_top + 80).changed && flow.click(30, timer_top + 27).changed &&
          flow.click(76, 132).changed && flow.text("1") && flow.click(94, 132).changed &&
          !flow.pending_contract_action(), "one bonus is confirmed locally without submitting the negotiation");
    const auto expired_talk = flow.process_timer_expired(61, 33);
    check(expired_talk.changed && expired_talk.request == FlowRequest::none &&
          flow.screen_name() == "contracts" && !flow.pending_contract_action(),
          "41232 premium deadline returns to roster locally without submitting or fabricating an ACK");
    check(save_stream(session->state()) == cancel_bytes && revision == cancel_revision &&
          session->rand().seed() == cancel_rand.seed() && session->rand().calls() == cancel_rand.calls() &&
          session->game().seed() == cancel_game.seed() && session->game().calls() == cancel_game.calls(),
          "expired negotiation preserves exact saved bytes, revision and both RNG streams");
    // Re-enter normally so the existing acceptance/refusal suite keeps testing
    // a live quote, independently of the cancelled generation above.
    demand_request = talk(row);
    expected = session->state(); rng = session->rand();
    demands = contract_demands(expected, manager, row, true, rng);
    serve(demand_request);
    {
        // contracts_from_game deliberately starts with no selection. The ACK must
        // restore the player row carried by the pending request before offer render.
        ContractsScreen quote_screen(data, roster);
        for (const auto point : {std::pair{61, 33}, std::pair{20, 0}, std::pair{20, 210}}) {
            ContractsScreen outside(data, roster);
            check(outside.process_timer_expired(point.first, point.second) && outside.close_requested() &&
                  !outside.pending(), "name-column boundary, header and empty row close without a contract request");
            check(!outside.process_timer_expired(20, 33), "a closed roster cannot create a late demand request");
        }
        const auto row_request = quote_screen.click(20, 32 + visual_row(row) * 7, true);
        check(row_request && quote_screen.pending() && quote_screen.pending()->row == row,
              "contracts projection emits the selected row in its demand request");
        auto incoming = contracts_from_game(session->state(), manager, quote_revision + 2);
        incoming.demands = demands;
        incoming.quote_id = 1;
        incoming.quote_row = row;
        quote_screen.acknowledge_quote(incoming);
        check(incoming.selected_row == -1 && quote_screen.state().selected_row == row,
              "quote ACK restores selected_row from the pending request, not the fresh projection default");
        check(quote_screen.render().width == 320,
              "quote ACK with an unselected fresh projection renders its player-offer page");
        const int top = row < 13 ? 130 : 22;
        check(quote_screen.click(100, top + 52), "timer test opens the unconfirmed salary calculator");
        check(quote_screen.click(76, 132) && quote_screen.text("17000"), "unconfirmed calculator draft is entered");
        check(quote_screen.process_timer_expired(61, 33) && !quote_screen.pending() &&
              quote_screen.state().demands == incoming.demands && !quote_screen.close_requested() &&
              quote_screen.state_json().find("\"page\":1") != std::string::npos,
              "4a13a deadline restores the talk without submitting the unconfirmed salary");
        check(quote_screen.process_timer_expired(61, 33) && !quote_screen.pending() &&
              quote_screen.state_json().find("\"page\":0") != std::string::npos,
              "next expired parent poll returns to roster without a command");
    }
    ui::ContractActionResult duplicate_result; std::string error;
    const auto duplicate_revision = revision; const auto duplicate_seed = session->rand().seed();
    check(!session->dispatch_contract(demand_request.contract_action, revision, duplicate_result, error) &&
          revision == duplicate_revision && save_stream(session->state()) == before_quote && session->rand().seed() == duplicate_seed,
          "replayed quote requests are inert");

    // A failure after Core execution must restore all game and RNG state.
    const auto failed_ack = offer(row, 0, 1);
    const size_t name_byte = size_t(manager) * 0xb0c + 0xa8;
    const auto original_name = session->state().manager_records[name_byte];
    session->state().manager_records[name_byte] = original_name == 'X' ? 'Y' : 'X';
    const auto rollback_bytes = save_stream(session->state()); const auto rollback_revision = revision;
    const auto rollback_watcom = session->rand(); const auto rollback_game = session->game();
    serve(failed_ack);
    check(save_stream(session->state()) == rollback_bytes && revision == rollback_revision &&
          session->rand().seed() == rollback_watcom.seed() && session->rand().calls() == rollback_watcom.calls() &&
          session->game().seed() == rollback_game.seed() && session->game().calls() == rollback_game.calls(),
          "failed authoritative ACK rolls back saved bytes, both RNG streams and revision");
    session->state().manager_records[name_byte] = original_name;
    check(!session->dispatch_contract(failed_ack.contract_action, revision, duplicate_result, error),
          "failed ACK invalidates its negotiation quote");
    flow.send_key("ESC");
    if (flow.screen_name() == "desktop") serve(menu(27));
    demand_request = talk(row);
    expected = session->state(); rng = session->rand();
    demands = contract_demands(expected, manager, row, true, rng);
    serve(demand_request);

    // Insurance is its own Original action, with no RNG; the salary quote remains valid.
    const int top = row < 13 ? 130 : 22;
    check(flow.click(220, top + 67).changed, "insurance type C opens the Original coverage calculator");
    const auto insurance = calculator(12);
    check(insurance.request == FlowRequest::contract_action && insurance.contract_action.kind == ContractActionKind::insure_player &&
          insurance.contract_action.insurance_months == 12 && insurance.contract_action.term_column == 2,
          "insurance calculator generates an independent coverage request");
    const auto pending_insurance_json = flow.state_json();
    check(!flow.process_timer_expired(61, 33).changed && flow.state_json() == pending_insurance_json,
          "confirmed insurance keeps its pending command until the normal ACK");
    expected = session->state(); rng = session->rand();
    check(contract_insurance(expected, manager, row, 2, 12).accepted, "Source insurance fixture is eligible");
    serve(insurance);
    check(save_stream(session->state()) == save_stream(expected) && session->rand().seed() == rng.seed() &&
          session->rand().calls() == rng.calls(), "insurance ACK has exactly Source writes and no RNG");
    const auto insurance_revision = revision;
    const auto accepted_request = offer(row, 0, demands[0]);
    check(accepted_request.contract_action.quote_id == insurance.contract_action.quote_id &&
          accepted_request.contract_action.revision == insurance_revision,
          "insurance retains the negotiation quote at its new revision");
    const auto pending_offer_json = flow.state_json();
    check(!flow.process_timer_expired(61, 33).changed && flow.state_json() == pending_offer_json &&
          flow.pending_contract_action() && *flow.pending_contract_action() == accepted_request.contract_action,
          "already submitted salary and premiums keep the exact pending offer and normal ACK");
    const auto frozen_offer_bytes=save_stream(session->state());
    const auto frozen_offer_rand=session->rand();const auto frozen_offer_revision=revision;
    for(int forgery=0;forgery<5;++forgery) {
        auto request=accepted_request.contract_action;
        if(forgery==0) --request.revision;
        else if(forgery==1) ++request.quote_id;
        else if(forgery==2) ++request.player_id;
        else if(forgery==3) ++request.demand;
        else request.proposed_bonuses[0]=21;
        ContractActionResult result;std::string error;
        check(!session->dispatch_contract(request,revision,result,error) && !error.empty() &&
              save_stream(session->state())==frozen_offer_bytes && revision==frozen_offer_revision &&
              session->rand().seed()==frozen_offer_rand.seed() && session->rand().calls()==frozen_offer_rand.calls(),
              "stale and forged offers preserve the valid pending quote, exact state and RNG");
    }
    expected = session->state(); rng = session->rand();
    check(contract_offer(expected, manager, row, 0, demands[0], demands[0], false, false, rng,
          &accepted_request.contract_action.proposed_bonuses), "full demand is an authentic acceptance");
    serve(accepted_request);
    check(save_stream(session->state()) == save_stream(expected) && session->rand().seed() == rng.seed() &&
          session->rand().calls() == rng.calls() && flow.state_json().find(bmh::test::expected_text("app_controller_tests-external-6").c_str()) != std::string::npos,
          "accepted Original offer matches Source contract, premiums, mood and RNG");
    const auto accepted_notice_json = flow.state_json();
    check(!flow.process_timer_expired(61, 33).changed && flow.state_json() == accepted_notice_json,
          "accepted manager response retains its separate KLICK input owner");
    check(flow.click(150, 170).changed, "KLICK dismisses the Original manager confirmation");

    const int refusal_row = negotiable[1].row;
    auto refusal_quote = talk(refusal_row);
    expected = session->state(); rng = session->rand();
    const auto refusal_demands = contract_demands(expected, manager, refusal_row, true, rng);
    serve(refusal_quote);
    const auto refused = offer(refusal_row, 0, 1);
    expected = session->state(); rng = session->rand();
    check(!contract_offer(expected, manager, refusal_row, 0, 1, refusal_demands[0], false, false, rng,
          &refused.contract_action.proposed_bonuses), "one EU is an authentic refusal");
    serve(refused);
    check(save_stream(session->state()) == save_stream(expected) && session->rand().seed() == rng.seed() &&
          session->rand().calls() == rng.calls() && flow.state_json().find(bmh::test::expected_text("app_controller_tests-external-7").c_str()) != std::string::npos,
          "refusal preserves Source offence/mood/RNG and uses the Original text");
    flow.click(150, 170);
    auto offended_quote = talk(refusal_row);
    serve(offended_quote);
    const auto before_blocked = save_stream(session->state()); const auto blocked_revision = revision;
    const auto blocked_rng = session->rand();
    auto blocked = flow.click(100, (refusal_row < 13 ? 130 : 22) + 52);
    check(blocked.request == FlowRequest::none && !blocked.changed &&
          save_stream(session->state()) == before_blocked && revision == blocked_revision &&
          session->rand().seed() == blocked_rng.seed() && session->rand().calls() == blocked_rng.calls(),
          "Original offence flag disables another salary calculator and offer");
    flow.send_key("ESC");
    const auto closed = flow.send_key("ESC");
    check(closed.request == FlowRequest::contracts_closed && flow.screen_name() == "desktop",
          "closing talk and roster returns to the updated desk");
    serve(closed);
    const auto final_bytes = save_stream(session->state());
    serve(menu(11)); flow.click(150, 214); flow.text("CONTRACT"); serve(flow.send_key("ENTER"));
    check(flow.screen_name() == "desktop" && std::filesystem::exists(save_directory / "CONTRACT.FBM"),
          "the updated Original contract state saves through the real UI");
    serve(menu(10)); serve(flow.send_key("ENTER"));
    check(session && flow.screen_name() == "desktop" && save_stream(session->state()) == final_bytes,
          "the real save/load path restores salary, premiums and insurance");
    serve(menu(27)); serve(flow.send_key("ESC"));
    const auto timed_kickoff = flow.process_timer_expired(0, 0);
    check(timed_kickoff.request == FlowRequest::kickoff && !flow.process_timer_expired(0, 0).changed,
          "returned contract desk emits exactly one deadline kickoff request");
    serve(timed_kickoff);
    check(flow.screen_name() != "contracts", "the reloaded contract career can advance");
}

void check_live_observer_determinism(const std::filesystem::path& data) {
    const uint32_t seed = 0x31415926;
    auto make_started = [&](StartupFlow& flow, std::unique_ptr<app::CareerSession>& session,
                            openbmh::audio::Playback& playback, std::optional<uint64_t>& audio_session,
                            uint64_t& revision) {
        auto serve = [&](const FlowResult& result) {
            const auto message = app::serve_flow_request(flow, result, session, data, data, seed,
                revision, playback, audio_session, false);
            check(message.empty(), "live observer test uses normal App initialization");
        };
        serve(flow.click(150, 215));
        serve(flow.click(5, 225, true));
        serve(flow.click(5, 225, true));
        serve(flow.click(90, 169));
        check(session && flow.screen_name() == "desktop", "observer comparison reaches real career desk");
    };
    StartupFlow plain_flow(data), observed_flow(data);
    std::unique_ptr<app::CareerSession> plain, observed;
    openbmh::audio::Playback plain_playback, observed_playback;
    std::optional<uint64_t> plain_audio, observed_audio;
    uint64_t plain_revision = 0, observed_revision = 0;
    make_started(plain_flow, plain, plain_playback, plain_audio, plain_revision);
    make_started(observed_flow, observed, observed_playback, observed_audio, observed_revision);

    const int home_team = observed->state().manager_teams.at(0);
    const int away_team = home_team == 0 ? 1 : 0;
    auto marker_state = observed->state();
    marker_state.live_matches = {home_team};
    MatchGroup marker_group;
    marker_group.desc.resize(0xbc);
    marker_group.fixtures = {home_team, away_team};
    marker_group.results = {0xcc};
    auto marker_frame = ui::live_matchday_frame(124, {marker_group}, marker_state);
    check(marker_frame.minute == 124 && marker_frame.matches.size() == 1 &&
          !marker_frame.matches.front().score_known,
          "minute 124 stays exact and a 0xCC not-played marker never becomes 12:12");
    marker_group.results[0] = 0xcd;
    marker_frame = ui::live_matchday_frame(124, {marker_group}, marker_state);
    check(!marker_frame.matches.front().score_known,
          "postponed 0xCD marker never becomes a live score");
    marker_group.results[0] = 0x02;
    marker_frame = ui::live_matchday_frame(124, {marker_group}, marker_state);
    check(marker_frame.matches.front().score_known && marker_frame.matches.front().home_goals == 0 &&
          marker_frame.matches.front().away_goals == 2,
          "a valid 0:2 forfeit remains a real score");
    marker_group.results.clear();
    marker_group.cup_results = {0xcccc};
    marker_frame = ui::live_matchday_frame(124, {marker_group}, marker_state);
    check(!marker_frame.matches.front().score_known,
          "0xCCCC cup no-result marker never becomes 6:12");
    marker_group.cup_results = {uint16_t((31 << 5) | 14)};
    marker_group.spectators = {7000};
    marker_group.desc[0x50] = 1;
    marker_group.desc[0x30] = uint8_t(-4);
    marker_group.log.clear();
    for (int kind : {0, 1, 12, 3, 4, 5}) {
        marker_group.log.push_back(uint16_t((kind << 12) | 1));
        marker_group.log.push_back(uint16_t(home_team * 0x80 + 17));
    }
    marker_group.log.push_back(uint16_t(3 << 12));
    marker_group.log.push_back(uint16_t(away_team * 0x80 + 18));
    marker_frame = ui::live_matchday_frame(124, {marker_group}, marker_state);
    const auto& marker_row = marker_frame.matches.front();
    check(marker_row.score_known && marker_row.home_goals == 31 && marker_row.away_goals == 14 &&
          marker_row.home_stats[1] == 2 && marker_row.home_stats[2] == 2 && marker_row.home_stats[3] == 2 &&
          marker_row.away_stats[2] == 1 && marker_row.away_stats[1] == 0 && marker_row.away_stats[3] == 0,
          "owned scores retain cup values above nine and paired event counts belong to the correct side");
    check(marker_row.spectators_text == "7.000" && marker_row.temperature == -4 && marker_row.rain == 1 &&
          marker_frame.minute_text == "124.Minute",
          "original number and weather projection preserves grouping, signed temperature and minute suffix");
    ui::LiveMatchdayScreen boundary_screen(data);
    check(boundary_screen.render(marker_frame).pixels.size() == 320 * 240,
          "score display clamps to original digit nine without reading past the 189-pixel atlas");
    check(ui::LiveMatchdayScreen::ball_frame_count(51) == 25 &&
          ui::LiveMatchdayScreen::ball_frame_count(-51) == 25 &&
          ui::LiveMatchdayScreen::ball_frame_count(52) == 26 &&
          ui::LiveMatchdayScreen::ball_frame_count(0) == 1,
          "Source rolling ball truncates signed odd positions and omits the final target present");
    const auto ball_home = boundary_screen.render_ball(marker_frame, 52, 1);
    const auto ball_away = boundary_screen.render_ball(marker_frame, -52, 1);
    check(ball_home.pixels != ball_away.pixels &&
          boundary_screen.render_ball(marker_frame, 52, 0).pixels ==
              boundary_screen.render_ball(marker_frame, -52, 0).pixels,
          "both Source directions start in midfield, then move and rotate in opposite directions");
    const auto no_ball = boundary_screen.render(marker_frame);
    for (int y = 0; y < 240; ++y) for (int x = 0; x < 320; ++x)
        if (x < 95 || x >= 224 || y < 75 || y >= 88)
            check(ball_home.pixels[size_t(y) * 320 + x] == no_ball.pixels[size_t(y) * 320 + x],
                  "chance strip cannot overwrite live controls, scores or weather");

    // Two watched AI sides without a squad exercise the actual chance path;
    // observing and rendering it must preserve every saved byte and both RNGs.
    auto run_ball = [&](bool observer, bool disabled, bool ticker = false, bool suppress_misses = false,
                        int shots = 0, int set_pieces = 0, bool penalty_test = false) {
        auto state = observed->state();
        state.team_morale.at(200) = ticker ? 0x40 : 0x80;
        const int opponent = ticker ? 1 : 201;
        state.team_morale.at(size_t(opponent)) = 0;
        // A deliberate missing-keeper unit fixture guarantees an actual goal
        // boundary for the ticker's goal-only path; no normal save is edited.
        if (ticker) state.squad_slots.at(squad_range(opponent).first) = kEmptySlot;
        state.live_matches = {200};
        MatchGroup group;
        group.desc.resize(0xd0);
        group.fixtures = {200, opponent}; group.results = {0};
        group.end_minute = {0}; group.spectators = {12000};
        group.records = {70,70,90,90,90,90,80,60,3, 70,70,90,90,90,90,80,60,3};
        std::vector<MatchGroup> groups{group};
        GameRandom game(314159); WatcomRandom rng(0x31415926);
        MatchEngine engine(state, groups, game, rng, {});
        engine.match_events_disabled = true;
        engine.buttons.resize(0xb4);
        engine.buttons.at(::bmh::layout::MatchEngine_Side_ExecutableField3 - ::bmh::layout::LeaguePresentationOptions) = disabled;
        engine.buttons.at(::bmh::layout::MatchEngine_Side_ExecutableField5 - ::bmh::layout::LeaguePresentationOptions) = suppress_misses;
        engine.buttons.at(::bmh::layout::MatchEngine_Side_ExecutableField2 - ::bmh::layout::LeaguePresentationOptions) = uint8_t(shots);
        engine.buttons.at(::bmh::layout::MatchEngine_Side_ExecutableField - ::bmh::layout::LeaguePresentationOptions) = uint8_t(set_pieces);
        int events = 0; bool home_seen = false, away_seen = false;
        std::optional<LiveChanceEvent> preview;
        uint8_t preview_score = 0;
        int previews = 0, outcomes = 0;
        const auto source_exe = load_le(asset_path(data, "BMH.exe"));
        int shot_count = 0;
        int set_piece_count = 0;
        int set_piece_kinds = 0, set_piece_sides = 0;
        if (observer) engine.live_set_piece = [&](const LiveSetPieceEvent& event) {
            check(event.minute > 0 && event.group == 0 && event.match == 0 &&
                  event.source_side >= 0 && event.source_side <= 1 && event.kind > 0 && event.kind < 4,
                  "set-piece observation retains its actual fixture, kind and Source side");
            const auto before = save_stream(state);
            const auto gs = game.seed(); const auto ws = rng.seed();
            const auto gc = game.calls(); const auto wc = rng.calls();
            const auto scene = ui::live_set_piece_frames(event,source_exe);
            check(!scene.empty() && scene == ui::live_set_piece_frames(event,source_exe),
                  "complete Source figure timelines are deterministic from existing draws");
            for (const auto& step:scene) {
                check(step.camera_x >= 0 && step.camera_x <= 128 && step.camera_y >= 0 && step.camera_y <= 57,
                      "moving Source viewport stays inside KB66");
                for (size_t i=1;i<step.sprites.size();++i)
                    if (step.sprites[i].figure != 24)
                        check(step.sprites[i-1].y <= step.sprites[i].y,"Source figures retain stable depth order");
            }
            const auto frame = ui::live_matchday_frame(event.minute,groups,state);
            const auto images = boundary_screen.render_set_piece(frame,event);
            check(images.size() == scene.size(),"each Source marker produces exactly one complete displayed frame");
            const auto backdrop = boundary_screen.render(frame);
            for (const auto& image:images) for (int y=0;y<240;++y) for (int x=0;x<320;++x)
                if (x<64 || x>=256 || y<2 || y>=97)
                    check(image.pixels[size_t(y)*320+x] == backdrop.pixels[size_t(y)*320+x],
                          "set-piece figures cannot overwrite scores, weather or controls");
            auto missing=event;missing.draws.pop_back();bool refused=false;
            try { (void)ui::live_set_piece_frames(missing,source_exe); } catch (const FormatError&) { refused=true; }
            check(refused,"incomplete Source observations cannot invent missing figure randomness");
            check(save_stream(state)==before && game.seed()==gs && rng.seed()==ws && game.calls()==gc && rng.calls()==wc,
                  "figure simulation and repeated rendering leave Native state and both RNG streams untouched");
            ++set_piece_count;
            set_piece_kinds |= 1<<event.kind; set_piece_sides |= 1<<event.source_side;
        };
        if (observer) engine.live_shot = [&](const LiveShotEvent& event) {
            check(event.minute > 0 && event.group == 0 && event.match == 0 &&
                  event.acting_side >= 0 && event.acting_side <= 1,
                  "shot observer receives the actual acting team and fixture");
            const auto before = save_stream(state);
            const auto g_seed = game.seed(); const auto w_seed = rng.seed();
            const auto g_calls = game.calls(); const auto w_calls = rng.calls();
            const auto frame = ui::live_matchday_frame(event.minute, groups, state);
            const auto rendered = boundary_screen.render_shot(frame, event);
            const auto repeat = boundary_screen.render_shot(frame, event);
            const auto palette = load_palette(asset_path(data, "M3PC.COL"));
            (void)boundary_screen.shot_palette(palette, event, groups, state);
            const auto backdrop = boundary_screen.render(frame);
            bool changed = false;
            for (size_t i = 0; i < rendered.size(); ++i) {
                check(rendered[i].pixels == repeat[i].pixels, "shot frames are deterministic owned images");
                changed |= i && rendered[i].pixels != rendered[0].pixels;
                for (int y = 0; y < 240; ++y) for (int x = 0; x < 320; ++x)
                    if (x < 64 || x >= 256 || y < 2 || y >= 97)
                        check(rendered[i].pixels[size_t(y)*320+x] == backdrop.pixels[size_t(y)*320+x],
                              "all eighteen shot frames preserve scores and controls outside the Source viewport");
            }
            check(changed, "shot includes moving ball/keeper frames");
            check(save_stream(state) == before && game.seed() == g_seed && rng.seed() == w_seed &&
                  game.calls() == g_calls && rng.calls() == w_calls,
                  "repeated shot rendering does not touch Native state or either random stream");
            ++shot_count;
        };
        if (observer) engine.live_chance = [&](const LiveChanceEvent& event) {
            if (ticker && suppress_misses) check(event.goal, "ticker miss switch suppresses both chance views");
            check(event.minute > 0 && event.group == 0 && event.match == 0 && event.kind >= 0 && event.kind <= 3,
                  "both chance stages retain the actual Source fixture, side and kind");
            const auto before = save_stream(state);
            const auto g_seed = game.seed(); const auto w_seed = rng.seed();
            const auto g_calls = game.calls(); const auto w_calls = rng.calls();
            const auto projection = ui::live_chance_projection(event, groups, state, source_exe);
            const auto frame = ui::live_matchday_frame(event.minute, groups, state);
            (void)boundary_screen.render_chance(frame, projection);
            (void)boundary_screen.render_chance(frame, projection);
            check(save_stream(state) == before && game.seed() == g_seed && rng.seed() == w_seed &&
                  game.calls() == g_calls && rng.calls() == w_calls,
                  "chance projection and repeated rendering are entirely read-only");
            if (event.stage == LiveChanceStage::preview) {
                check(!preview, "a Source chance preview cannot overwrite an unresolved chance");
                preview = event; preview_score = groups[0].results[0]; ++previews;
            } else {
                check(preview && preview->minute == event.minute && preview->side == event.side &&
                      preview->goal == event.goal && preview->distance == event.distance,
                      "each Source chance has one matching result after the ball/shot path");
                check(groups[0].results[0] == uint8_t(preview_score +
                      (event.goal ? (event.side == 0 ? 0x10 : 1) : 0)),
                      "preview observes the old score and resolution observes the Source score write");
                preview.reset(); ++outcomes;
            }
        };
        if (observer) engine.live_ball = [&](const LiveBallEvent& event) {
            check(event.group == 0 && event.match == 0 && event.minute > 0,
                  "rolling ball belongs to its actual synchronous fixture and minute");
            const int distance = std::abs(event.displacement);
            check((distance >= 40 && distance <= 50) || distance == 52,
                  "live ball uses the already drawn Watcom distance or the Source goal endpoint");
            home_seen |= event.displacement > 0; away_seen |= event.displacement < 0;
            const auto frame = ui::live_matchday_frame(event.minute, groups, state);
            for (int i = 0; i < ui::LiveMatchdayScreen::ball_frame_count(event.displacement); ++i)
                (void)boundary_screen.render_ball(frame, event.displacement, i);
            ++events;
        };
        for (int minute = 1; minute <= 40; ++minute) {
            const auto report=engine.minute(minute,90,5);
            check(!report.unsupported,report.reason.c_str());
        }
        if (penalty_test) {
            groups[0].desc[0]=1; groups[0].results.clear(); groups[0].cup_results={0};
            MinuteReport report;
            engine.penalty_shootout(groups[0],0,report);
            check(!report.unsupported,"Source shoot-out fixture completes");
            if (observer) check((set_piece_kinds&4) && set_piece_sides==3,
                                "shoot-out takes the Source follow-up scene on both attacking sides");
        }
        if (observer && !disabled && !ticker)
            check(events > 0 && home_seen && away_seen, "both real chance directions reach the renderer");
        if (observer && !disabled && ticker)
            check(events > 0, "ticker goals reach the ball presenter while misses remain suppressed");
        if (disabled) check(events == 0, "D5 suppresses the ball presenter without suppressing game draws");
        if (observer) check((shots != 0) == (shot_count != 0),
                            "Source shot switch alone determines whether an observed scene exists");
        if (observer) check((set_pieces != 0) == (set_piece_count != 0),
                            "Source set-piece switch alone determines whether figure scenes are observed");
        if (observer) check(previews > 0 && previews == outcomes && !preview,
                            "chance captions remain present even when D5 disables the ball");
        return std::make_tuple(save_stream(state), groups[0].results, groups[0].cup_results, groups[0].records, groups[0].log,
            game.seed(), game.calls(), rng.seed(), rng.calls());
    };
    const auto unobserved_ball = run_ball(false, false);
    check(run_ball(true, false) == unobserved_ball && run_ball(true, true) == unobserved_ball,
          "rolling ball on/off and repeated rendering preserve Native state, results and both RNG streams");
    check(run_ball(true, false, true, true) == run_ball(false, false, true, true),
          "ticker chance observation preserves Native state and RNG with the original miss switch enabled");
    check(run_ball(true, false, false, false, 4) == run_ball(false, false, false, false, 4),
          "shot observation preserves every saved byte, event and Game/Watcom seed and call count");
    check(run_ball(true, false, true, true, 4) == run_ball(false, false, true, true, 4),
          "ticker goals retain the original shot RNG sequence while being rendered");
    check(run_ball(true,false,false,false,4,4) == run_ball(false,false,false,false,4,4),
          "normal set-piece observation preserves complete Native bytes, results and both RNG seeds and call counts");
    check(run_ball(true,false,true,true,4,4) == run_ball(false,false,true,true,4,4),
          "ticker set-piece observation preserves complete Native bytes and both random streams");
    check(run_ball(true,false,false,false,4,4,true) == run_ball(false,false,false,false,4,4,true),
          "both follow-up scenes in the shoot-out preserve Native bytes, the cup result and both RNG streams");

    auto kit_state = marker_state;
    kit_state.memory.set8(::bmh::layout::ClubKitSelectorsUnsigned + uint32_t(home_team), 0x34);
    kit_state.memory.set8(::bmh::layout::ClubKitSelectorsUnsigned + uint32_t(away_team), 0x43);
    const auto base_palette = load_palette(asset_path(data, "M3PC.COL"));
    const auto kit_before = save_stream(kit_state);
    const auto kit_exe = load_le(asset_path(data, "BMH.exe"));
    const auto kit_palette = boundary_screen.shot_palette(base_palette, LiveShotEvent{}, {marker_group}, kit_state);
    const auto expected_color = [&](uint32_t table, int index) {
        const auto c = [&](int component) { return uint32_t(((kit_exe.u8(table + uint32_t(index*3+component)) & 63)*255+31)/63); };
        return 0xff000000u | c(0)<<16 | c(1)<<8 | c(2);
    };
    check(kit_palette.argb[13] == expected_color(::bmh::layout::LiveMatchday_FormatError_ExecutableField12, 4) &&
          kit_palette.argb[8] == expected_color(::bmh::layout::LiveMatchday_FormatError_ExecutableField14, 3) &&
          kit_palette.argb[29] == expected_color(::bmh::layout::LiveMatchday_FormatError_ExecutableField12, 3) &&
          kit_palette.argb[24] == expected_color(::bmh::layout::LiveMatchday_FormatError_ExecutableField14, 3),
          "Original swapped away kits use the Source contrast change and separate colour banks");
    for (int i = 0; i < 256; ++i)
        if (i != 8 && i != 9 && i != 13 && i != 14 && i != 15 &&
            i != 24 && i != 25 && i != 29 && i != 30 && i != 31)
            check(kit_palette.argb[size_t(i)] == base_palette.argb[size_t(i)],
                  "Source kit palette changes only its ten designated entries");
    check(save_stream(kit_state) == kit_before, "kit palette leaves ordinary saved club colours untouched");
    const LiveShotEvent transparent_shot{22,0,0,1,90,40,90,40,0,true,false};
    const auto transparent_frames = boundary_screen.render_shot(marker_frame, transparent_shot);
    const auto original_goal = load_vga(asset_path(data / "KB", "62.VGA"));
    const auto original_keeper = load_vga(asset_path(data / "KB", "63.VGA"));
    check(original_keeper.pixels[0] == 0 && original_goal.pixels[38 * 192 + 85] != 0 &&
          transparent_frames[0].pixels[40 * 320 + 149] == original_goal.pixels[38 * 192 + 85],
          "the zero-index keeper corner preserves the exact Original goal background without a black rectangle");

    // AI teams and an explicitly strict unit referee exercise genuine foul decisions;
    // none of these deliberate fixtures is written into the ordinary GUI input saves.
    const auto booking_exe=load_le(asset_path(data,"BMH.exe"));
    int booking_home=0;while (observed->state().manager_of(booking_home)>=0) ++booking_home;
    int booking_away=booking_home+1;while (observed->state().manager_of(booking_away)>=0) ++booking_away;
    int all_booking_kinds=0;
    const auto run_bookings=[&](bool observer,int watch,int switches,int seed) {
        auto state=observed->state();state.live_matches={booking_home};
        state.team_morale.at(size_t(booking_home))=uint8_t(watch);
        state.team_morale.at(size_t(booking_away))=0;
        MatchGroup group;group.desc.resize(0xd0);group.fixtures={booking_home,booking_away};
        group.results={0};group.end_minute={0};group.spectators={12000};
        group.records={70,70,90,90,90,90,80,60,3,70,70,90,90,90,90,80,60,3};
        std::vector<MatchGroup> groups{group};GameRandom game(314159+seed);WatcomRandom rng(0x31415926u+uint32_t(seed));
        MatchEngine engine(state,groups,game,rng,{24});engine.alternate_event_roll=true;engine.buttons.resize(0xb4);
        engine.buttons.at(::bmh::layout::MatchEngine_EventImpl_ExecutableField-::bmh::layout::LeaguePresentationOptions)=uint8_t(switches&1);
        engine.buttons.at(::bmh::layout::MatchEngine_EventImpl_ExecutableField2 - ::bmh::layout::LeaguePresentationOptions)=uint8_t(switches&2);
        engine.buttons.at(::bmh::layout::MatchEngine_EventImpl_ExecutableField3-::bmh::layout::LeaguePresentationOptions)=uint8_t(switches&4);
        int seen=0;
        if (observer) engine.live_booking=[&](const LiveBookingEvent& event) {
            const bool yellow=event.kind==0 || event.kind==3;
            check(!(watch==128 && yellow && (switches&1)) &&
                  !(watch==64 && ((yellow && (switches&2)) || (!yellow && (switches&4)))),
                  "live/ticker Source switches suppress exactly their corresponding booking boxes");
            check(event.group==0 && event.match==0 && event.minute>0 && event.kind>=0 && event.kind<4,
                  "booking retains its actual Source fixture, minute and shown kind");
            const auto range=squad_range(event.team);
            bool found=false;
            for (size_t slot=range.first;slot<range.first+range.count;++slot)
                if ((state.squad_slots.at(slot)&0xfff)==event.player) {
                    found=true;
                    check(event.kind==0 ? (state.squad_slots[slot]&0x1000)!=0 :
                          (state.squad_slots[slot]&kSlotInLineup)==0,
                          "booking is presented after the card/removal write and before any replacement");
                    break;
                }
            check(found,"the actual offending player is still in the Source slot at the presentation boundary");
            const auto before=save_stream(state);const auto gs=game.seed();const auto ws=rng.seed();
            const auto gc=game.calls();const auto wc=rng.calls();
            const auto projection=ui::live_booking_projection(event,groups,state,booking_exe);
            const auto frame=ui::live_matchday_frame(event.minute,groups,state);
            const auto image=boundary_screen.render_booking(frame,projection);
            check(image.pixels==boundary_screen.render_booking(frame,projection).pixels,
                  "booking redraw is deterministic and owned");
            const auto normal=boundary_screen.render(frame);
            for (int y=0;y<240;++y) for (int x=0;x<320;++x)
                if (x<64 || x>=256 || y<2 || y>=98)
                    check(image.pixels[size_t(y)*320+x]==normal.pixels[size_t(y)*320+x],
                          "booking header/message preserves live controls and already updated scoreboards");
            std::string expected=message_text(state,::bmh::layout::LiveMatchday_FormatError_TextPointer2Unsigned+uint32_t(event.kind*4))+state.player_name(event.player);
            if (event.kind==0) expected+=booking_exe.cstring(::bmh::layout::CupDraw_FormatError_TextPointer)+format_number(state,event.season_yellows,1)+booking_exe.cstring(::bmh::layout::CupDraw_FormatError_ExecutableField);
            check(projection.message==original_to_utf8(expected),"Source message/count syntax is exact for all four event kinds");
            check(save_stream(state)==before && game.seed()==gs && rng.seed()==ws && game.calls()==gc && rng.calls()==wc,
                  "booking projection and repeat drawing leave Native bytes and both RNG streams untouched");
            all_booking_kinds|=1<<event.kind;++seen;
        };
        for (int minute=1;minute<=70;++minute) {
            const auto report=engine.minute(minute,90,5);check(!report.unsupported,report.reason.c_str());
        }
        if (observer && switches==0) check(seen>0,"real booking decisions reach the observer");
        return std::make_tuple(save_stream(state),groups[0].desc,groups[0].results,groups[0].records,groups[0].log,
                               game.seed(),game.calls(),rng.seed(),rng.calls());
    };
    for (int seed=0;seed<12;++seed) {
        const auto live=run_bookings(false,128,0,seed);
        check(run_bookings(true,128,0,seed)==live && run_bookings(true,128,1,seed)==live,
              "booking observation/live-yellow suppression preserve all Native, descriptor, log and RNG results");
    }
    check(all_booking_kinds==15,"natural unit decisions cover yellow, red, injury and second yellow");
    for (int switches:{0,2,4,6})
        check(run_bookings(true,64,switches,3)==run_bookings(false,64,0,3),
              "both ticker booking switches change only presentation, never the game trajectory");

    bool shootout_miss=false,shootout_goal=false,shootout_sudden=false,shootout_no_player=false;
    const auto run_shootout=[&](bool observer,int watch,int seed,bool no_squad=false) {
        auto state=observed->state();const int home=no_squad?200:booking_home,away=no_squad?201:booking_away;
        state.team_morale.at(size_t(home))=uint8_t(watch);state.team_morale.at(size_t(away))=0;
        state.live_matches={home};
        MatchGroup group;group.desc.resize(0xd0);group.desc[1]=8;
        group.fixtures={home,away};group.cup_results={uint16_t(0x800+8*32+8)};
        group.end_minute={120};group.spectators={12000};
        group.records={70,70,90,90,90,90,80,60,3,70,70,90,90,90,90,80,60,3};
        std::vector<MatchGroup> groups{group};GameRandom game(314159+seed);WatcomRandom rng(0x31415926u+uint32_t(seed));
        MatchEngine engine(state,groups,game,rng,{});engine.buttons.resize(0xb4);
        std::vector<ShowEvent> shows;engine.show=&shows;
        int starts=0,previews=0,ends=0,last_side=-1;
        if(observer) engine.live_shootout=[&](const LiveShootoutEvent& event) {
            const auto before=save_stream(state);const auto gs=game.seed();const auto ws=rng.seed();
            const auto gc=game.calls(),wc=rng.calls();
            check(event.group==0 && event.match==0 && event.minute==121 && watch!=0,
                  "shoot-out presentation retains the real fixture and only Source watched games");
            check(groups[0].cup_results[0]==group.cup_results[0],
                  "shoot-out observer cannot prematurely publish partial lottery goals as Native cup results");
            const auto projection=ui::live_shootout_projection(event,groups,state,booking_exe);
            const auto frame=ui::live_matchday_frame(121,groups,state);
            const auto normal=boundary_screen.render(frame);const auto image=boundary_screen.render_shootout(frame,projection);
            check(image.pixels==boundary_screen.render_shootout(frame,projection).pixels,
                  "shoot-out redraw is deterministic and owned");
            for(int y=0;y<240;++y) for(int x=0;x<320;++x) if(x<64 || x>=256 || y<2 || y>=98)
                check(image.pixels[size_t(y)*320+x]==normal.pixels[size_t(y)*320+x],
                      "lottery headers preserve Source live scoreboards and controls");
            if(event.stage==LiveShootoutStage::start) {
                ++starts;check(previews==0 && event.side==3 && event.home_goals==8 && event.away_goals==8,
                              "Source introduction retains extra-time score and highlights neither club");
                check(projection.message==original_to_utf8(message_text(state,::bmh::layout::LiveMatchday_FormatError_TextPointer)),"Source outdoor lottery introduction");
            } else if(event.stage==LiveShootoutStage::preview) {
                check(starts==1 && ends==0 && event.side<2,"kick caption precedes its animation/decision");
                int home_score=8,away_score=8;
                for(const auto& show:shows) if(show.what==Show::PenaltyKick){home_score=show.a;away_score=show.b;}
                check(event.home_goals==home_score && event.away_goals==away_score,
                      "Source next-kick header shows prior lottery score, not extra-time plus lottery");
                if(event.player==0xfff){check(projection.message.empty(),"no-squad kick has no invented taker");shootout_no_player=true;}
                else check(projection.message==original_to_utf8(format_number(state,event.round,0x81)+message_text(state,::bmh::layout::LiveMatchday_FormatError_TextPointer4)+
                      format_number(state,event.of,1)+message_text(state,::bmh::layout::LiveMatchday_FormatError_TextPointer5)+state.player_name(event.player)),
                      "Source kick number/of-five/taker text is exact");
                shootout_sudden|=event.of==1;last_side=event.side;++previews;
            } else {
                ++ends;check(starts==1 && previews>0 && event.side==last_side && event.home_goals!=event.away_goals,
                             "Source terminal header follows the deciding kick and keeps its acting side");
                check(projection.message==original_to_utf8(message_text(state,::bmh::layout::LiveMatchday_FormatError_TextPointer6)),"Source terminal text is exact");
            }
            check(save_stream(state)==before && game.seed()==gs && rng.seed()==ws && game.calls()==gc && rng.calls()==wc,
                  "lottery projection/repeated drawing leave Native bytes and both RNG streams untouched");
        };
        MinuteReport report;engine.penalty_shootout(groups[0],0,report);check(!report.unsupported,report.reason.c_str());
        int kicks=0;for(const auto& show:shows) if(show.what==Show::PenaltyKick){++kicks;shootout_goal|=show.flag;shootout_miss|=!show.flag;}
        if(observer && watch)check(starts==1 && ends==1 && previews==kicks,"exactly one preview per actual kick and one intro/end");
        if(observer && !watch)check(starts==0 && previews==0 && ends==0,"no lottery overlay for unwatched games");
        return std::make_tuple(save_stream(state),groups[0].cup_results,groups[0].log,game.seed(),game.calls(),rng.seed(),rng.calls());
    };
    for(int seed=0;seed<12;++seed) for(int watch:{0,64,128})
        check(run_shootout(true,watch,seed)==run_shootout(false,watch,seed),
              "lottery caption observation preserves every Native byte, result, log and both RNG streams");
    check(run_shootout(true,128,3,true)==run_shootout(false,128,3,true),"Source no-squad lottery follows unchanged path");
    check(shootout_miss && shootout_goal && shootout_sudden && shootout_no_player,
          "actual unit lottery decisions cover misses, goals, sudden death and missing takers");

    // Header digit geometry includes the Source multi-digit cup scores (no clamping).
    LiveBookingProjection multi_digit;
    multi_digit.header.home="HOME";multi_digit.header.away="AWAY";multi_digit.header.side=0;
    multi_digit.score_known=true;multi_digit.home_goals=31;multi_digit.away_goals=14;
    const auto large_score=boundary_screen.render_booking(marker_frame,multi_digit);
    const auto digits=load_vga(asset_path(data/"KB","13.VGA"));
    for (int side=0;side<2;++side) {
        const int anchor=booking_exe.u8(::bmh::layout::LiveMatchday_RenderHeader_RuntimeFieldUnsigned+uint32_t(side));
        const int right_digit=side==0?1:4;
        bool painted=false;
        for (int y=0;y<20;++y) for (int x=0;x<19;++x) {
            const auto pixel=digits.pixels[size_t(22+y)*189+size_t(right_digit*19+x)];
            if (pixel==255) continue;
            painted=true;check(large_score.pixels[size_t(37+y)*320+size_t(anchor+1+x)]==pixel,
                               "Source multi-digit large score uses original centered decimal atlas positions");
        }
        check(painted,"Original decimal atlas is not empty");
    }

    auto chance_state = marker_state;
    const auto source_exe = load_le(asset_path(data, "BMH.exe"));
    const int shooter = chance_state.squad_slots.at(squad_range(home_team).first) & 0xfff;
    const int assistant = chance_state.squad_slots.at(squad_range(home_team).first + 1) & 0xfff;
    chance_state.players.at(size_t(shooter)).w364 =
        (chance_state.players.at(size_t(shooter)).w364 & ~uint32_t(0xfc)) | (3u << 2);
    chance_state.players.at(size_t(assistant)).w368 =
        (chance_state.players.at(size_t(assistant)).w368 & ~uint32_t(0xfc0000)) | (4u << 18);
    marker_group.desc[0] = 0; marker_group.desc[1] = 2;
    LiveChanceEvent chance_event{LiveChanceStage::preview, 24, 0, 0, 0, 0, shooter, assistant, 13,
        true, false, true};
    const auto open_chance = ui::live_chance_projection(chance_event, {marker_group}, chance_state, source_exe);
    check(open_chance.description.empty() && open_chance.shooter_detail ==
          original_to_utf8(chance_state.player_name(shooter) + bmh::test::expected_text("app-chance-deflection-fixture")),
          "open play keeps the original shooter, comma, distance and deflected-shot wording");
    check(open_chance.outcome == "TOR" && open_chance.participants ==
          original_to_utf8(chance_state.player_name(shooter) + "(3)  (" + chance_state.player_name(assistant) + "(4))"),
          "league result captions use the Source goal/assist counters and exact parentheses");
    chance_event.kind = 1;
    check(ui::live_chance_projection(chance_event,{marker_group},chance_state,source_exe).description ==
          original_to_utf8(bmh::test::expected_text("app-chance-free-kick-fixture")), "free kick text is taken from the original kind table");
    chance_event.kind = 2; chance_state.indoor = true;
    chance_state.memory.set8(::bmh::layout::Cup_AdvanceCup_RuntimeField4,0); chance_state.memory.set8(::bmh::layout::LiveMatchday_FormatError_RuntimeField14,uint8_t(home_team));
    const auto indoor_chance = ui::live_chance_projection(chance_event,{marker_group},chance_state,source_exe);
    check(indoor_chance.description == bmh::test::expected_text("app_controller_tests-external-4").c_str() && indoor_chance.stadium.starts_with(bmh::test::expected_text("app_controller_tests-external-5").c_str()),
          "indoor kind two and the actual tournament host use the original seven-metre/hall strings");
    chance_state.indoor = false;
    check(ui::live_chance_projection(chance_event,{marker_group},chance_state,source_exe).description == bmh::test::expected_text("app_controller_tests-external-2").c_str(),
          "outdoor kind two retains the original penalty label");
    chance_event.kind = 3; chance_event.goal = false; chance_event.ticker = true;
    chance_event.stage = LiveChanceStage::result;
    const auto corner = ui::live_chance_projection(chance_event,{marker_group},chance_state,source_exe);
    check(corner.description == bmh::test::expected_text("app_controller_tests-external-1").c_str() && corner.outcome == bmh::test::expected_text("app_controller_tests-external-3").c_str() && corner.ticker_score == "31:14",
          "corner/miss/ticker retain original wording and full cup score rather than a digit clamp");
    const auto empty = boundary_screen.render(marker_frame);
    const auto described = boundary_screen.render_chance(marker_frame, open_chance);
    check(empty.pixels != described.pixels &&
          boundary_screen.render_ball(marker_frame,50,0,&open_chance).pixels != described.pixels,
          "Source header/captions remain visible underneath the independent rolling-ball strip");
    const int marker_manager = marker_state.manager_of(home_team);
    check(marker_manager >= 0, "capacity-color fixture has a human home manager");
    marker_state.manager_records[size_t(marker_manager) * 0xb0c + 0x5c] = 7;
    marker_group.spectators[0] = 7999;
    marker_state.memory.set8(::bmh::layout::TextGradientDisabled, 0);
    const auto gradient_frame = ui::live_matchday_frame(124, {marker_group}, marker_state);
    check(gradient_frame.gradient_text && gradient_frame.matches.front().attendance_at_capacity,
          "original home attendance highlight compares whole thousands, including 7999");
    marker_state.memory.set8(::bmh::layout::TextGradientDisabled, 1);
    marker_group.spectators[0] = 8000;
    const auto flat_frame = ui::live_matchday_frame(124, {marker_group}, marker_state);
    check(!flat_frame.gradient_text && !flat_frame.matches.front().attendance_at_capacity,
          "font setting and attendance highlight retain their independent original conditions");
    check(boundary_screen.render(gradient_frame).pixels != boundary_screen.render(flat_frame).pixels,
          "font and capacity options have visible output rather than remaining unused metadata");
    marker_state.live_matches.clear();
    check(ui::live_matchday_frame(124, {marker_group}, marker_state).matches.empty(),
          "an unselected match never creates a human-manager fallback board");

    auto kickoff = [](StartupFlow& flow) {
        const auto& items = flow.desktop()->state().items;
        const auto it = std::find_if(items.begin(), items.end(), [](const DeskItem& item) {
            return item.kind == 12 && !item.disabled;
        });
        check(it != items.end(), "career desk exposes its normal kickoff control");
        return flow.click(it->x + it->width / 2, it->y + it->height / 2);
    };
    std::vector<ui::LiveMatchdayFrame> frames;
    std::vector<MatchGroup> observed_groups;
    std::optional<ui::LiveMatchdayFrame> selected_frame;
    std::string plain_message, observed_message;
    for (int turn = 0; turn < 12 && frames.empty(); ++turn) {
        auto plain_request = kickoff(plain_flow);
        auto observed_request = kickoff(observed_flow);
        check(plain_request.request == FlowRequest::kickoff && observed_request.request == FlowRequest::kickoff,
              "comparison advances through the real desk kickoff control");
        plain_message = app::serve_flow_request(plain_flow, plain_request, plain, data, data, seed,
            plain_revision, plain_playback, plain_audio, false);
        observed_message = app::serve_flow_request(observed_flow, observed_request, observed, data, data, seed,
            observed_revision, observed_playback, observed_audio, false,
            [&](int minute, const std::vector<MatchGroup>& groups, const GameState& state) {
                frames.push_back(ui::live_matchday_frame(minute, groups, state));
                observed_groups = groups;
                if (!groups.empty() && !groups.front().fixtures.empty()) {
                    auto selected_state = state;
                    selected_state.live_matches = {groups.front().fixtures.front()};
                    selected_frame = ui::live_matchday_frame(minute, groups, selected_state);
                }
            });
    }
    check(plain_message.empty() && observed_message.empty() && !frames.empty(),
          "read-only observer receives actual in-match frames");
    check(frames.front().minute > 0 && frames.front().matches.empty() && selected_frame &&
          selected_frame->matches.size() == 1,
          "only original live-match selections create boards; a selected home team resolves in source order");
    MatchGroup capped;
    capped.desc.resize(0xbc);
    capped.fixtures = {0,1,2,3,4,5,6,7,8,9};
    capped.results = {0x12,0x23,0x34,0x45,0x56};
    auto capped_state = observed->state();
    capped_state.live_matches = {6,2,8,0,4};
    const auto capped_frame = ui::live_matchday_frame(124, {capped}, capped_state);
    check(capped_frame.matches.size() == 4 && capped_frame.matches[0].home_team == 6 &&
          capped_frame.matches[1].home_team == 2 && capped_frame.matches[2].home_team == 8 &&
          capped_frame.matches[3].home_team == 0,
          "original board selection order is retained and capped at four");
    check(capped_frame.matches[0].score_known && capped_frame.matches[0].home_goals == 4 &&
          capped_frame.matches[0].away_goals == 5,
          "owned projection preserves actual score values before the source display clamp");
    ui::LiveMatchdayScreen live_screen(data);
    const auto live_image = live_screen.render(*selected_frame);
    check(live_image.width == 320 && live_image.height == 240 &&
          std::any_of(live_image.pixels.begin(), live_image.pixels.end(), [](uint8_t p) { return p != 0x15; }),
          "live frame renders through the normal UI font and image path");
    check(plain_revision == observed_revision && save_stream(plain->state()) == save_stream(observed->state()) &&
          plain->game().seed() == observed->game().seed() && plain->game().calls() == observed->game().calls() &&
          plain->rand().seed() == observed->rand().seed() && plain->rand().calls() == observed->rand().calls(),
          "observer on/off leaves save bytes, results, and both RNG streams identical");

    // Exercise both synchronous live-menu dispatchers, including the mode,
    // owner, selected fixture and stale-request boundaries.
    StartupFlow live_flow(data);
    std::unique_ptr<app::CareerSession> live_session;
    openbmh::audio::Playback live_playback;
    std::optional<uint64_t> live_audio;
    uint64_t live_revision = 0;
    make_started(live_flow, live_session, live_playback, live_audio, live_revision);
    auto& live_state = live_session->state();
    const int live_manager = int(live_state.memory.u8(::bmh::layout::ActiveManager));
    const int live_team = live_state.manager_teams.at(size_t(live_manager));
    const int live_opponent = live_team == 0 ? 1 : 0;
    live_state.live_matches = {live_opponent};
    const auto live_range = squad_range(live_team);
    int field_row = -1, bench_row = -1;
    for (int row = 0; row < 11; ++row) {
        const int id = live_state.squad_slots.at(live_range.first + size_t(row)) & 0x0fff;
        if (id != kEmptySlot && size_t(id) < live_state.players.size()) { field_row = row; break; }
    }
    for (size_t row = 11; row < live_range.count; ++row) {
        const int id = live_state.squad_slots.at(live_range.first + row) & 0x0fff;
        if (id != kEmptySlot && size_t(id) < live_state.players.size() &&
            !(live_state.players[size_t(id)].w364 & 0x1000)) { bench_row = int(row); break; }
    }
    check(field_row >= 0 && bench_row >= 11, "live fixture has a field player and unused reserve");
    const size_t field_at = live_range.first + size_t(field_row), bench_at = live_range.first + size_t(bench_row);
    live_state.squad_slots[field_at] = uint16_t((live_state.squad_slots[field_at] & 0x1fff) | 0x8000);
    live_state.squad_slots[bench_at] = uint16_t((live_state.squad_slots[bench_at] & 0x1fff) | 0x4000);
    MatchGroup live_group;
    live_group.desc = group_descriptor(live_state, 0);
    live_group.fixtures = {live_opponent, live_team};
    live_group.results = {0xcc};
    live_group.records.assign(18, 0);
    live_group.spectators = {30000};
    if (live_group.desc.size() < 0xbc) live_group.desc.resize(0xbc);
    live_group.desc[0x30] = 6;
    live_group.desc[0x50] = 0;
    std::vector<MatchGroup> live_groups{live_group};
    MatchEngine live_engine(live_state, live_groups, live_session->game(), live_session->rand(), {});
    live_engine.month = 6;
    {
        const GameState before_event_scope = live_state;
        const auto groups_before_event_scope = live_groups;
        const auto game_before_event_scope = live_session->game();
        const auto rand_before_event_scope = live_session->rand();
        const int event_player = before_event_scope.squad_slots.at(live_range.first + size_t(field_row)) & 0x0fff;
        live_state.team_morale.at(size_t(live_team)) |= 0x80;
        live_state.manager_options.at(size_t(live_manager) * 5 + 1) = 0;
        live_state.players.at(size_t(event_player)).w35c =
            (live_state.players.at(size_t(event_player)).w35c & 0x07ffffffu) | 0x08000000u;
        live_state.squad_slots.at(live_range.first + size_t(field_row)) &= 0x1fff;
        LiveMatchEvent event{0, 0, 1, live_team, live_opponent, event_player, 2, 1, live_manager};
        std::string event_scope_error;
        check(live_session->begin_live_event_intervention(event, live_engine, live_revision, event_scope_error),
              "authenticated injury event opens the Source line-up even in minute one");
        check(live_session->finish_live_event_intervention(event, live_engine, live_revision, event_scope_error) &&
              live_groups[0].records == groups_before_event_scope[0].records &&
              live_session->game().seed() == game_before_event_scope.seed() &&
              live_session->game().calls() == game_before_event_scope.calls() &&
              live_session->rand().seed() == rand_before_event_scope.seed() &&
              live_session->rand().calls() == rand_before_event_scope.calls(),
              "event-menu close leaves the one Source event rerate to MatchEngine::event");
        const auto finished_revision = live_revision;
        check(!live_session->finish_live_event_intervention(event, live_engine, live_revision, event_scope_error) &&
              live_revision == finished_revision && live_groups[0].records == groups_before_event_scope[0].records &&
              live_session->game().calls() == game_before_event_scope.calls() &&
              live_session->rand().calls() == rand_before_event_scope.calls(),
              "a duplicate automatic event-menu close is stale and neutral");
        live_state = before_event_scope;
    }
    const auto live_exe = load_le(asset_path(data, "BMH.exe"));
    ui::TacticsRequest tactic_request;
    tactic_request.kind = ui::TacticsRequestKind::set_tactic;
    tactic_request.manager_slot = live_manager;
    tactic_request.runtime_club = live_team;
    tactic_request.revision = live_revision;
    tactic_request.index = 1;
    const size_t live_record = size_t(live_manager) * 0xb0c;
    const uint8_t old_tactic = live_state.manager_records.at(live_record + 0xd3);
    tactic_request.desired_value = old_tactic < live_exe.u8(::bmh::layout::FixtureFirstTacticMaximum) ? old_tactic + 1 : 0;
    const auto original_manager_records = live_state.manager_records;
    const auto original_manager_options = live_state.manager_options;
    const auto original_tactics = live_state.tactics;
    auto expected_manager_records = original_manager_records;
    expected_manager_records.at(live_record + 0xd3) = uint8_t(tactic_request.desired_value);
    const auto original_groups = live_groups;
    const auto original_game = live_session->game();
    const auto original_rand = live_session->rand();
    std::string live_error;
    check(!live_session->dispatch_live_tactic(tactic_request, 3, 2, live_engine, live_revision, live_error) &&
          live_state.manager_records == original_manager_records && live_groups[0].records == original_groups[0].records,
          "mode-two request cannot enter the mode-eight tactics dispatcher");
    check(!live_session->dispatch_live_tactic(tactic_request, 2, 8, live_engine, live_revision, live_error) &&
          live_state.manager_records == original_manager_records,
          "minute-two tactics request is neutral");
    int disabled_tactic = -1;
    for (int i = 0; i < 8; ++i)
        if (live_exe.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField2 + i) == 0) { disabled_tactic = i; break; }
    if (disabled_tactic >= 0 && live_state.memory.has(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3)) {
        const uint8_t saved_live_option = live_state.memory.u8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3);
        live_state.memory.set8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3, 1);
        ui::TacticsRequest disabled_request = tactic_request;
        disabled_request.index = disabled_tactic;
        disabled_request.desired_value = 0;
        disabled_request.revision = live_revision;
        if (disabled_tactic == 0) disabled_request.desired_assistant_attitude = 0;
        check(!live_session->dispatch_live_tactic(disabled_request, 3, 8, live_engine, live_revision, live_error) &&
              live_state.manager_records == original_manager_records && live_state.manager_options == original_manager_options,
              "Source-disabled live tactic is neutral");
        live_state.memory.set8(::bmh::layout::CareerSession_DispatchLiveTactic_RuntimeField3, saved_live_option);
    }
    ui::TacticsRequest invalid_tactic = tactic_request;
    invalid_tactic.desired_value = int(live_exe.u8(::bmh::layout::FixtureFirstTacticMaximum)) + 1;
    check(!live_session->dispatch_live_tactic(invalid_tactic, 3, 8, live_engine, live_revision, live_error) &&
          live_state.manager_records == original_manager_records && live_revision == tactic_request.revision &&
          live_session->game().seed() == original_game.seed() && live_session->game().calls() == original_game.calls() &&
          live_session->rand().seed() == original_rand.seed() && live_session->rand().calls() == original_rand.calls(),
          "out-of-range live tactic is neutral");
    check(live_session->begin_live_intervention(3, 8, live_manager, live_team,
          live_engine, live_revision, live_error), "mode-eight live tactics menu opens for the current manager match");
    const bool tactic_accepted = live_session->dispatch_live_tactic(tactic_request, 3, 8,
        live_engine, live_revision, live_error);
    check(tactic_accepted && live_revision == tactic_request.revision + 1 &&
          live_state.manager_records == expected_manager_records &&
          live_state.manager_options == original_manager_options && live_state.tactics == original_tactics &&
          live_groups[0].records == original_groups[0].records &&
          live_session->game().seed() == original_game.seed() && live_session->game().calls() == original_game.calls() &&
          live_session->rand().seed() == original_rand.seed() && live_session->rand().calls() == original_rand.calls(),
          "mode-eight tactic clicks write Source fields without rerating before menu close");
    ui::TacticsRequest second_tactic = tactic_request;
    second_tactic.index = 2;
    second_tactic.revision = live_revision;
    const uint8_t old_second_tactic = live_state.manager_records.at(live_record + 0xd4);
    second_tactic.desired_value = old_second_tactic < live_exe.u8(::bmh::layout::FixtureSecondTacticMaximum) ? old_second_tactic + 1 : 0;
    check(live_session->dispatch_live_tactic(second_tactic, 3, 8, live_engine, live_revision, live_error) &&
          live_revision == second_tactic.revision + 1 &&
          live_state.manager_records.at(live_record + 0xd4) == second_tactic.desired_value &&
          live_groups[0].records == original_groups[0].records &&
          live_session->game().seed() == original_game.seed() && live_session->game().calls() == original_game.calls() &&
          live_session->rand().seed() == original_rand.seed() && live_session->rand().calls() == original_rand.calls(),
          "a second tactic click remains staged in the same Source menu");
    auto expected_tactic_state = live_state;
    auto expected_tactic_groups = live_groups;
    auto expected_tactic_game = live_session->game();
    auto expected_tactic_rand = live_session->rand();
    MatchEngine expected_tactic_engine(expected_tactic_state, expected_tactic_groups,
        expected_tactic_game, expected_tactic_rand, {});
    expected_tactic_engine.month = live_engine.month;
    MinuteReport expected_tactic_report;
    expected_tactic_engine.rerate_team(live_team, expected_tactic_report);
    check(!expected_tactic_report.unsupported, "live tactics fixture can be rerated on menu completion");
    check(live_session->finish_live_intervention(3, 8, live_manager, live_team,
          live_engine, live_revision, live_error) && live_revision == second_tactic.revision + 2 &&
          live_groups[0].records == expected_tactic_groups[0].records &&
          live_session->game().seed() == expected_tactic_game.seed() && live_session->game().calls() == expected_tactic_game.calls() &&
          live_session->rand().seed() == expected_tactic_rand.seed() && live_session->rand().calls() == expected_tactic_rand.calls(),
          "menu close applies one Source rerate after repeated tactic clicks");
    const auto after_tactic_records = live_state.manager_records;
    const auto after_tactic_match_records = live_groups[0].records;
    const uint64_t after_tactic_finish_revision = live_revision;
    check(!live_session->finish_live_intervention(3, 8, live_manager, live_team,
          live_engine, live_revision, live_error) && live_revision == after_tactic_finish_revision &&
          live_state.manager_records == after_tactic_records && live_groups[0].records == after_tactic_match_records,
          "duplicate menu completion is rejected after consuming its one-shot scope");
    check(!live_session->dispatch_live_tactic(tactic_request, 3, 8, live_engine, live_revision, live_error) &&
          live_state.manager_records == after_tactic_records && live_groups[0].records == after_tactic_match_records,
          "duplicate live tactic request is stale and neutral after menu close");
    const auto no_action_game = live_session->game();
    const auto no_action_rand = live_session->rand();
    auto expected_no_action_state = live_state;
    auto expected_no_action_groups = live_groups;
    auto expected_no_action_game = no_action_game;
    auto expected_no_action_rand = no_action_rand;
    MatchEngine expected_no_action_engine(expected_no_action_state, expected_no_action_groups,
        expected_no_action_game, expected_no_action_rand, {});
    expected_no_action_engine.month = live_engine.month;
    MinuteReport expected_no_action_report;
    expected_no_action_engine.rerate_team(live_team, expected_no_action_report);
    check(!expected_no_action_report.unsupported, "no-action live menu fixture supports Source rerate");
    check(live_session->begin_live_intervention(3, 8, live_manager, live_team,
          live_engine, live_revision, live_error) &&
          live_session->finish_live_intervention(3, 8, live_manager, live_team,
              live_engine, live_revision, live_error) &&
          live_session->game().seed() == expected_no_action_game.seed() &&
          live_session->game().calls() == expected_no_action_game.calls() &&
          live_session->rand().seed() == expected_no_action_rand.seed() &&
          live_session->rand().calls() == expected_no_action_rand.calls() &&
          live_groups[0].records == expected_no_action_groups[0].records,
          "closing an untouched Source tactics menu still completes its one rerate");
    const auto before_aborted_menu = save_stream(live_state);
    const auto before_aborted_groups = live_groups;
    const auto before_aborted_game = live_session->game();
    const auto before_aborted_rand = live_session->rand();
    const uint64_t before_aborted_revision = live_revision;
    check(live_session->begin_live_intervention(3, 8, live_manager, live_team,
          live_engine, live_revision, live_error), "live tactics can open a second transactional menu");
    ui::TacticsRequest abandoned_tactic = tactic_request;
    abandoned_tactic.revision = live_revision;
    abandoned_tactic.index = 3;
    const uint8_t abandoned_old = live_state.manager_records.at(live_record + 0xd5);
    abandoned_tactic.desired_value = abandoned_old < live_exe.u8(::bmh::layout::FixtureThirdTacticMaximum) ? abandoned_old + 1 : 0;
    check(live_session->dispatch_live_tactic(abandoned_tactic, 3, 8, live_engine, live_revision, live_error),
          "a valid action may be staged before SDL abort");
    live_session->abort_live_intervention(3, 8, live_manager, live_team, live_engine, live_revision);
    check(save_stream(live_state) == before_aborted_menu && live_groups[0].records == before_aborted_groups[0].records &&
          live_revision == before_aborted_revision && live_session->game().seed() == before_aborted_game.seed() &&
          live_session->game().calls() == before_aborted_game.calls() && live_session->rand().seed() == before_aborted_rand.seed() &&
          live_session->rand().calls() == before_aborted_rand.calls(),
          "an aborted modal restores the complete menu-entry checkpoint");

    ui::LineupRequest live_request;
    live_request.kind = ui::LineupRequestKind::swap_players;
    live_request.manager_slot = live_manager;
    live_request.runtime_club = live_team;
    live_request.revision = live_revision;
    live_request.first_row = field_row;
    live_request.second_row = bench_row;
    live_request.first_slot = live_state.squad_slots[field_at];
    live_request.second_slot = live_state.squad_slots[bench_at];
    live_request.first_player = live_request.first_slot & 0x0fff;
    live_request.second_player = live_request.second_slot & 0x0fff;
    ui::LineupRequest reversed_live_request = live_request;
    std::swap(reversed_live_request.first_row, reversed_live_request.second_row);
    std::swap(reversed_live_request.first_slot, reversed_live_request.second_slot);
    std::swap(reversed_live_request.first_player, reversed_live_request.second_player);
    reversed_live_request.revision = live_revision;
    live_request.revision = live_revision;
    const auto before_gate = save_stream(live_state);
    const auto before_gate_game = live_session->game();
    const auto before_gate_rand = live_session->rand();
    check(!live_session->dispatch_live_substitution(live_request, 2, 2, live_engine, live_revision, live_error) &&
          save_stream(live_state) == before_gate && live_session->game().seed() == before_gate_game.seed() &&
          live_session->game().calls() == before_gate_game.calls() && live_session->rand().seed() == before_gate_rand.seed() &&
          live_session->rand().calls() == before_gate_rand.calls(), "minute-two live request is neutral");
    check(!live_session->dispatch_live_substitution(live_request, 3, 8, live_engine, live_revision, live_error) &&
          save_stream(live_state) == before_gate, "mode-eight does not enter the mode-two lineup action");
    const auto before_live_substitution_groups = live_groups;
    check(live_session->begin_live_intervention(3, 2, live_manager, live_team,
          live_engine, live_revision, live_error), "mode-two live lineup menu opens for the current manager match");
    const auto before_live_pitch_gate = save_stream(live_state);
    ui::PitchRequest forbidden_live_pitch;
    forbidden_live_pitch.manager_slot = live_manager;
    forbidden_live_pitch.runtime_club = live_team;
    forbidden_live_pitch.revision = live_revision;
    forbidden_live_pitch.indoor = live_state.indoor;
    for (const auto kind : {ui::PitchRequestKind::set_automatic_weight,
                            ui::PitchRequestKind::apply_formation,
                            ui::PitchRequestKind::save_formation}) {
        forbidden_live_pitch.kind = kind;
        check(!live_session->dispatch_live_pitch(forbidden_live_pitch, 3, 2,
                  live_engine, live_revision, live_error) &&
              save_stream(live_state) == before_live_pitch_gate,
              "live pitch wrapper rejects preparation-only AA/formation actions");
    }
    const uint32_t marking_display_at = ::bmh::layout::CareerSession_AnyOf_ExecutableFieldUnsigned + uint32_t(live_manager) * 2;
    if (!live_state.memory.has(marking_display_at)) live_state.memory.add_block(marking_display_at, 2);
    const uint16_t marking_old_word = live_state.memory.u16(marking_display_at);
    const uint16_t marking_page_word = uint16_t(((marking_old_word & ~uint16_t(0x80)) & 0x008f) | 0x0040);
    ui::PitchRequest marking_page;
    marking_page.kind = ui::PitchRequestKind::select_pitch_page;
    marking_page.manager_slot = live_manager; marking_page.runtime_club = live_team;
    marking_page.revision = live_revision; marking_page.indoor = live_state.indoor;
    marking_page.page_control = 4; marking_page.opponent_team = live_opponent;
    marking_page.expected_display_word = marking_old_word; marking_page.new_display_word = marking_page_word;
    marking_page.expected_mode = uint8_t(live_state.memory.has(::bmh::layout::GameMode) ? live_state.memory.u8(::bmh::layout::GameMode) : live_exe.u8(::bmh::layout::GameMode));
    const size_t marking_manager_record = size_t(live_manager) * 0xb0c;
    for (size_t i = 0; i < 3; ++i) {
        const size_t offset = std::array<size_t, 3>{0xafc, 0xafe, 0xb08}[i];
        marking_page.expected_set_piece_values[i] = uint16_t(live_state.manager_records.at(marking_manager_record + offset) |
            uint16_t(live_state.manager_records.at(marking_manager_record + offset + 1)) << 8);
    }
    marking_page.expected_set_piece_signature = uint16_t(live_state.manager_records.at(marking_manager_record + 0xb0a) |
        uint16_t(live_state.manager_records.at(marking_manager_record + 0xb0b)) << 8);
    const auto marking_page_game = live_session->game();
    const auto marking_page_rand = live_session->rand();
    check(live_session->dispatch_live_pitch(marking_page, 3, 2, live_engine, live_revision, live_error) &&
          live_state.memory.u16(marking_display_at) == marking_page_word &&
          live_revision == marking_page.revision + 1 &&
          live_session->game().calls() == marking_page_game.calls() &&
          live_session->rand().calls() == marking_page_rand.calls(),
          "Source page-control four selects Manndeckung with an exact mask and no early rerate");
    const auto opponent_range = squad_range(live_opponent);
    int opponent_row = -1;
    for (int row = 1; row < 11; ++row) {
        const uint16_t slot = live_state.squad_slots.at(opponent_range.first + size_t(row));
        if ((slot & 0xc000) == 0x8000 && (slot & 0x0fff) != kEmptySlot) { opponent_row = row; break; }
    }
    int duplicate_mark_row = -1;
    for (int row = 0; row < 11; ++row)
        if (row != field_row && (live_state.squad_slots.at(live_range.first + size_t(row)) & 0xc000) == 0x8000) {
            duplicate_mark_row = row; break;
        }
    check(opponent_row >= 0 && duplicate_mark_row >= 0,
          "Source marking fixture has opponent and own field rows");
    if (opponent_row >= 0 && duplicate_mark_row >= 0) {
        const int opponent_player = live_state.squad_slots.at(opponent_range.first + size_t(opponent_row)) & 0x0fff;
        const size_t marking_board = size_t(live_manager) * 0x840;
        const size_t duplicate_mark_at = marking_board + size_t(duplicate_mark_row) * 0x58 + 0x44;
        ui::PitchRequest marking;
        marking.kind = ui::PitchRequestKind::set_player_marking;
        marking.manager_slot = live_manager; marking.runtime_club = live_team;
        marking.revision = live_revision; marking.indoor = live_state.indoor;
        marking.opponent_team = live_opponent; marking.opponent_row = opponent_row;
        marking.opponent_slot = live_state.squad_slots.at(opponent_range.first + size_t(opponent_row));
        marking.opponent_player = opponent_player; marking.row = field_row;
        marking.slot = live_state.squad_slots.at(live_range.first + size_t(field_row));
        marking.player_id = marking.slot & 0x0fff;
        marking.expected_display_word = live_state.memory.u16(marking_display_at);
        const size_t own_mark_at = marking_board + size_t(field_row) * 0x58 + 0x44;
        for (size_t row = 0; row < 11; ++row) {
            const size_t at = marking_board + row * 0x58 + 0x44;
            if (uint16_t(live_state.tactics.at(at) | uint16_t(live_state.tactics.at(at + 1)) << 8) ==
                uint16_t(opponent_player)) {
                live_state.tactics.at(at) = 0xff;
                live_state.tactics.at(at + 1) = 0x0f;
            }
        }
        bool target_unassigned = true;
        for (size_t row = 0; row < 11; ++row) {
            const size_t at = marking_board + row * 0x58 + 0x44;
            if (uint16_t(live_state.tactics.at(at) | uint16_t(live_state.tactics.at(at + 1)) << 8) ==
                uint16_t(opponent_player)) target_unassigned = false;
        }
        marking.expected_marking_player_id = uint16_t(live_state.tactics.at(own_mark_at) |
            uint16_t(live_state.tactics.at(own_mark_at + 1)) << 8);
        ui::PitchRequest clear_unassigned = marking;
        clear_unassigned.row = -1; clear_unassigned.player_id = -1;
        clear_unassigned.expected_marking_player_id = uint16_t(opponent_player);
        const auto unassigned_clear_tactics = live_state.tactics;
        const uint64_t unassigned_clear_revision = live_revision;
        check(target_unassigned && live_session->dispatch_live_pitch(clear_unassigned, 3, 2, live_engine, live_revision, live_error) &&
              live_state.tactics == unassigned_clear_tactics && live_revision == unassigned_clear_revision + 1,
              "Source re-click accepts clearing a verified unassigned opponent without changing its board");
        live_state.tactics.at(duplicate_mark_at) = uint8_t(opponent_player);
        live_state.tactics.at(duplicate_mark_at + 1) = uint8_t(uint16_t(opponent_player) >> 8);
        marking.revision = live_revision;
        marking.expected_display_word = live_state.memory.u16(marking_display_at);
        const auto marking_game = live_session->game();
        const auto marking_rand = live_session->rand();
        const auto marking_groups = live_groups;
        check(live_session->dispatch_live_pitch(marking, 3, 2, live_engine, live_revision, live_error) &&
              uint16_t(live_state.tactics.at(own_mark_at) | uint16_t(live_state.tactics.at(own_mark_at + 1)) << 8) == uint16_t(opponent_player) &&
              uint16_t(live_state.tactics.at(duplicate_mark_at) | uint16_t(live_state.tactics.at(duplicate_mark_at + 1)) << 8) == 0x0fff &&
              live_groups[0].records == marking_groups[0].records &&
              live_session->game().calls() == marking_game.calls() && live_session->rand().calls() == marking_rand.calls(),
              "Source marking assignment clears prior duplicates on the own board without rerating");
        const auto assigned_mark_state = save_stream(live_state);
        const uint64_t assigned_mark_revision = live_revision;
        check(!live_session->dispatch_live_pitch(marking, 3, 2, live_engine, live_revision, live_error) &&
              save_stream(live_state) == assigned_mark_state && live_revision == assigned_mark_revision,
              "replayed marking request is stale and cannot mutate the assignment");
        ui::PitchRequest clear_mark = marking;
        clear_mark.revision = live_revision; clear_mark.row = -1; clear_mark.player_id = -1;
        clear_mark.expected_marking_player_id = uint16_t(opponent_player);
        check(live_session->dispatch_live_pitch(clear_mark, 3, 2, live_engine, live_revision, live_error) &&
              uint16_t(live_state.tactics.at(own_mark_at) | uint16_t(live_state.tactics.at(own_mark_at + 1)) << 8) == 0x0fff &&
              uint16_t(live_state.tactics.at(duplicate_mark_at) | uint16_t(live_state.tactics.at(duplicate_mark_at + 1)) << 8) == 0x0fff &&
              live_session->game().calls() == marking_game.calls() && live_session->rand().calls() == marking_rand.calls(),
              "Source re-click clears the selected opponent from every own assignment without rerating");
    }
    int field_a = -1, field_b = -1;
    for (int row = 0; row < 11; ++row) {
        const uint16_t slot = live_state.squad_slots.at(live_range.first + size_t(row));
        if ((slot & 0x0fff) == kEmptySlot || (slot & 0xc000) != 0x8000) continue;
        if (field_a < 0) field_a = row;
        else { field_b = row; break; }
    }
    check(field_a >= 0 && field_b >= 0, "live fixture has two marked field rows for a tactical exchange");
    if (field_a >= 0 && field_b >= 0) {
        auto make_pitch_swap = [&](int first_row, int second_row) {
            ui::PitchRequest swap;
            swap.kind = ui::PitchRequestKind::swap_players;
            swap.manager_slot = live_manager; swap.runtime_club = live_team;
            swap.revision = live_revision; swap.indoor = live_state.indoor;
            swap.row = first_row; swap.second_row = second_row;
            swap.slot = live_state.squad_slots.at(live_range.first + size_t(first_row));
            swap.second_slot = live_state.squad_slots.at(live_range.first + size_t(second_row));
            swap.player_id = swap.slot & 0x0fff;
            swap.second_player = swap.second_slot & 0x0fff;
            return swap;
        };
        const auto before_slots = live_state.squad_slots;
        const auto before_cards = live_state.player_cards;
        const auto before_tactics = live_state.tactics;
        const auto before_team_flags = live_state.team_flags;
        const auto before_swap_game = live_session->game();
        const auto before_swap_rand = live_session->rand();
        const size_t board = size_t(live_manager) * 0x840;
        const size_t row_a = board + size_t(field_a) * 0x58;
        const size_t row_b = board + size_t(field_b) * 0x58;
        const uint8_t ax = before_tactics[row_a + 0x41], ay = before_tactics[row_a + 0x42];
        const uint8_t bx = before_tactics[row_b + 0x41], by = before_tactics[row_b + 0x42];
        auto pitch_swap = make_pitch_swap(field_a, field_b);
        check(live_session->dispatch_live_pitch(pitch_swap, 3, 2, live_engine, live_revision, live_error) &&
              live_state.squad_slots[live_range.first + size_t(field_a)] == uint16_t((before_slots[live_range.first + size_t(field_b)] & 0x0fff) | 0x8000) &&
              live_state.squad_slots[live_range.first + size_t(field_b)] == uint16_t((before_slots[live_range.first + size_t(field_a)] & 0x0fff) | 0x8000) &&
              live_state.player_cards == before_cards && live_state.team_flags == before_team_flags &&
              live_state.tactics[row_a + 0x41] == ax && live_state.tactics[row_a + 0x42] == ay &&
              live_state.tactics[row_b + 0x41] == bx && live_state.tactics[row_b + 0x42] == by &&
              live_groups[0].records == before_live_substitution_groups[0].records &&
              live_session->game().calls() == before_swap_game.calls() &&
              live_session->rand().calls() == before_swap_rand.calls(),
              "Source same-zone pitch exchange preserves role, position, cards and subcount without rerating");
        auto team_sheet_swap = make_pitch_swap(field_b, field_a);
        ui::LineupRequest lineup_swap;
        lineup_swap.kind = ui::LineupRequestKind::swap_players;
        lineup_swap.manager_slot = live_manager; lineup_swap.runtime_club = live_team;
        lineup_swap.revision = live_revision;
        lineup_swap.first_row = team_sheet_swap.row; lineup_swap.first_player = team_sheet_swap.player_id;
        lineup_swap.first_slot = team_sheet_swap.slot;
        lineup_swap.second_row = team_sheet_swap.second_row; lineup_swap.second_player = team_sheet_swap.second_player;
        lineup_swap.second_slot = team_sheet_swap.second_slot;
        check(live_session->dispatch_live_substitution(lineup_swap, 3, 2, live_engine, live_revision, live_error) &&
              live_state.squad_slots == before_slots && live_state.player_cards == before_cards &&
              live_state.team_flags == before_team_flags && live_state.tactics[row_a + 0x41] == ax &&
              live_state.tactics[row_a + 0x42] == ay && live_state.tactics[row_b + 0x41] == bx &&
              live_state.tactics[row_b + 0x42] == by &&
              live_groups[0].records == before_live_substitution_groups[0].records,
              "the parent teamsheet uses the same no-subcount same-zone exchange path");
    }
    live_request.revision = live_revision;
    live_request.first_slot = live_state.squad_slots[field_at];
    live_request.second_slot = live_state.squad_slots[bench_at];
    live_request.first_player = live_request.first_slot & 0x0fff;
    live_request.second_player = live_request.second_slot & 0x0fff;
    reversed_live_request.revision = live_revision;
    // The Original blocks a source-marked injured incoming player with a timed
    // notice; it does not ask for yes/no and leaves the roster/revision untouched.
    // format field is executable/runtime data rather than a native save block;
    // materialize the one-byte Source flag in this synthetic in-memory fixture.
    live_state.memory.add_block(::bmh::layout::ForeignPlayerLimitEnabled, 1);
    check(live_state.memory.has(::bmh::layout::ForeignPlayerLimitEnabled), "live fixture exposes the Source foreign-limit flag");
    if (live_state.memory.has(::bmh::layout::ForeignPlayerLimitEnabled)) {
        const auto foreign_baseline_state = live_state;
        const uint64_t foreign_baseline_revision = live_revision;
        const auto foreign_baseline_groups = live_groups;
        const auto foreign_baseline_game = live_session->game();
        const auto foreign_baseline_rand = live_session->rand();
        const auto set_nationality = [&](int player, int nation, bool bonus_foreign = false) {
            Player& p = live_state.players.at(size_t(player));
            p.w368 = (p.w368 & 0x07ffffffu) | (uint32_t(nation & 0x1f) << 27);
            live_state.player_bonus.at(size_t(player)) = uint8_t(
                (live_state.player_bonus.at(size_t(player)) & 0x7f) | (bonus_foreign ? 0x80 : 0));
        };
        live_state.memory.set8(::bmh::layout::ForeignPlayerLimitEnabled, 1);
        // Start at four foreign field players, including the selected outgoing
        // player via legacy routine's player_bonus bit. Replacing him with a domestic
        // reserve leaves three, so the foreign rule must allow this swap.
        for (int row = 0; row < 11; ++row) {
            const uint16_t slot = live_state.squad_slots.at(live_range.first + size_t(row));
            if ((slot & 0xc000) == 0x8000 && (slot & 0x0fff) != kEmptySlot)
                set_nationality(slot & 0x0fff, 0);
        }
        set_nationality(live_request.first_player, 0, true);
        int additional_foreigners = 0;
        for (int row = 0; row < 11 && additional_foreigners < 3; ++row) {
            const uint16_t slot = live_state.squad_slots.at(live_range.first + size_t(row));
            if ((slot & 0xc000) != 0x8000 || (slot & 0x0fff) == kEmptySlot ||
                (slot & 0x0fff) == live_request.first_player) continue;
            set_nationality(slot & 0x0fff, 1);
            ++additional_foreigners;
        }
        set_nationality(live_request.second_player, 0);
        const auto three_foreign = live_session->dispatch_live_substitution_result(live_request, 3, 2,
            live_engine, live_revision);
        check(additional_foreigners == 3 && three_foreign.status == app::LiveSubstitutionStatus::applied &&
              live_revision == foreign_baseline_revision + 1 &&
              live_state.squad_slots[bench_at] == uint16_t((live_request.first_slot & 0x1fff) | 0x4000) &&
              live_state.squad_slots[field_at] == uint16_t((live_request.second_slot & 0x1fff) | 0x8000) &&
              live_groups[0].records == foreign_baseline_groups[0].records &&
              live_session->game().calls() == foreign_baseline_game.calls() &&
              live_session->rand().calls() == foreign_baseline_rand.calls(),
              "Source foreign rule allows an outgoing flagged foreign when the resulting field has three");

        // The accepted request advances the private menu revision too. Close
        // this scope through the real abort path, then restore and reopen a
        // clean menu so the second boundary starts from a matching checkpoint.
        live_session->abort_live_intervention(3, 2, live_manager, live_team, live_engine, live_revision);
        live_state = foreign_baseline_state;
        live_revision = foreign_baseline_revision;
        live_state.memory.set8(::bmh::layout::ForeignPlayerLimitEnabled, 1);
        check(live_session->begin_live_intervention(3, 2, live_manager, live_team,
              live_engine, live_revision, live_error),
              "foreign-quota rejection uses a fresh live-menu revision scope");
        for (int row = 0; row < 11; ++row) {
            const uint16_t slot = live_state.squad_slots.at(live_range.first + size_t(row));
            if ((slot & 0xc000) == 0x8000 && (slot & 0x0fff) != kEmptySlot)
                set_nationality(slot & 0x0fff, 0);
        }
        int three_existing = 0;
        for (int row = 0; row < 11 && three_existing < 3; ++row) {
            const uint16_t slot = live_state.squad_slots.at(live_range.first + size_t(row));
            if ((slot & 0xc000) != 0x8000 || (slot & 0x0fff) == kEmptySlot ||
                (slot & 0x0fff) == live_request.first_player) continue;
            set_nationality(slot & 0x0fff, 1);
            ++three_existing;
        }
        set_nationality(live_request.first_player, 0);
        set_nationality(live_request.second_player, 1);
        const auto before_four_notice = save_stream(live_state);
        const auto four_foreign_game = live_session->game();
        const auto four_foreign_rand = live_session->rand();
        const auto four_foreign_groups = live_groups;
        const auto four_foreign = live_session->dispatch_live_substitution_result(live_request, 3, 2,
            live_engine, live_revision);
        check(three_existing == 3 && four_foreign.status == app::LiveSubstitutionStatus::source_notice &&
              four_foreign.source_text_address == ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField8 && live_revision == foreign_baseline_revision &&
              save_stream(live_state) == before_four_notice && live_groups[0].records == four_foreign_groups[0].records &&
              live_session->game().calls() == four_foreign_game.calls() &&
              live_session->rand().calls() == four_foreign_rand.calls(),
              "Source foreign rule rejects a resulting fourth foreign with af148 and no state or RNG change");
        live_session->abort_live_intervention(3, 2, live_manager, live_team, live_engine, live_revision);
        live_state = foreign_baseline_state;
        live_revision = foreign_baseline_revision;
        check(live_session->begin_live_intervention(3, 2, live_manager, live_team,
              live_engine, live_revision, live_error),
              "injury checks continue in a newly opened baseline menu scope");
    }
    const uint32_t bench_injury_value = live_state.players[size_t(live_request.second_player)].w364;
    const uint32_t bench_history_value = live_state.players[size_t(live_request.second_player)].w35c;
    live_state.players[size_t(live_request.second_player)].w35c &= 0x07ffffffu;
    live_state.players[size_t(live_request.second_player)].w364 |= 1u << 16;
    const auto before_injury_notice = save_stream(live_state);
    const auto before_injury_game = live_session->game();
    const auto before_injury_rand = live_session->rand();
    const auto injury_notice = live_session->dispatch_live_substitution_result(live_request, 3, 2,
        live_engine, live_revision);
    check(injury_notice.status == app::LiveSubstitutionStatus::source_notice &&
          injury_notice.source_text_address == ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField5 && injury_notice.manager_slot == live_manager &&
          live_revision == live_request.revision && save_stream(live_state) == before_injury_notice,
          "Source injury gate returns its exact timed notice without applying the swap");
    live_state.players[size_t(live_request.second_player)].w35c |= 1u << 27;
    live_state.players[size_t(live_request.second_player)].w364 |= 1u << 16;
    const auto before_bench_notice = save_stream(live_state);
    const auto bench_notice = live_session->dispatch_live_substitution_result(live_request, 3, 2,
        live_engine, live_revision);
    check(bench_notice.status == app::LiveSubstitutionStatus::source_notice &&
          bench_notice.source_text_address == ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField7 &&
          live_revision == live_request.revision && save_stream(live_state) == before_bench_notice,
          "Source exemption routes an injured incoming bench player to the exact second timed notice");
    live_state.players[size_t(live_request.second_player)].w364 = bench_injury_value;
    live_state.players[size_t(live_request.second_player)].w35c = bench_history_value;
    check(live_session->game().calls() == before_injury_game.calls() &&
          live_session->rand().calls() == before_injury_rand.calls(),
          "Source injury notice performs no match rerate or random draw");
    // A player with existing injury history (w35c high bits) follows Source's
    // exemption for the outgoing field player and may leave the pitch.
    live_state.players[size_t(live_request.first_player)].w364 |= 1u << 16;
    live_state.players[size_t(live_request.first_player)].w35c |= 1u << 27;
    const bool live_accepted = live_session->dispatch_live_substitution(reversed_live_request, 3, 2,
        live_engine, live_revision, live_error);
    if (!live_accepted) std::fprintf(stderr, "live substitution rejected: %s\n", live_error.c_str());
    check(live_accepted &&
          live_revision == reversed_live_request.revision + 1 &&
          live_state.squad_slots[field_at] == uint16_t((live_request.second_slot & 0x1fff) | 0x8000) &&
          live_state.tactics[size_t(live_manager) * 0x840 + size_t(field_row) * 0x58] == 3 &&
          (live_state.players[size_t(live_request.second_player)].w364 & 0x1000) &&
          live_groups[0].records == before_live_substitution_groups[0].records,
          "live mode-two substitution updates real squad and played flag without early rerating");
    check(live_session->finish_live_intervention(3, 2, live_manager, live_team,
          live_engine, live_revision, live_error) &&
          std::any_of(live_groups[0].records.begin(), live_groups[0].records.end(), [](uint8_t v) { return v != 0; }),
          "mode-two menu completion rerates the substituted side once");
    const auto after_live_change = save_stream(live_state);
    check(!live_session->dispatch_live_substitution(live_request, 3, 2, live_engine, live_revision, live_error) &&
          save_stream(live_state) == after_live_change,
          "duplicate live lineup request is stale and leaves the committed match unchanged");
    if (opponent_row >= 0) {
        if (!live_state.memory.has(marking_display_at)) live_state.memory.add_block(marking_display_at, 2);
        const uint16_t before_mark_abort_word = live_state.memory.u16(marking_display_at);
        live_state.memory.set16(marking_display_at,
            uint16_t(((before_mark_abort_word & ~uint16_t(0x80)) & 0x008f) | 0x0040));
        const auto before_mark_abort = save_stream(live_state);
        const auto before_mark_abort_tactics = live_state.tactics;
        const auto before_mark_abort_groups = live_groups;
        const auto before_mark_abort_game = live_session->game();
        const auto before_mark_abort_rand = live_session->rand();
        const uint64_t before_mark_abort_revision = live_revision;
        check(live_session->begin_live_intervention(3, 2, live_manager, live_team,
              live_engine, live_revision, live_error), "marking mutation opens a fresh Source menu scope");
        const uint16_t own_slot = live_state.squad_slots.at(live_range.first + size_t(field_row));
        const uint16_t marked_opponent_slot = live_state.squad_slots.at(opponent_range.first + size_t(opponent_row));
        const size_t own_mark_at = size_t(live_manager) * 0x840 + size_t(field_row) * 0x58 + 0x44;
        ui::PitchRequest abandoned_mark;
        abandoned_mark.kind = ui::PitchRequestKind::set_player_marking;
        abandoned_mark.manager_slot = live_manager; abandoned_mark.runtime_club = live_team;
        abandoned_mark.revision = live_revision; abandoned_mark.indoor = live_state.indoor;
        abandoned_mark.opponent_team = live_opponent; abandoned_mark.opponent_row = opponent_row;
        abandoned_mark.opponent_slot = marked_opponent_slot; abandoned_mark.opponent_player = marked_opponent_slot & 0x0fff;
        abandoned_mark.row = field_row; abandoned_mark.slot = own_slot; abandoned_mark.player_id = own_slot & 0x0fff;
        abandoned_mark.expected_display_word = live_state.memory.u16(marking_display_at);
        abandoned_mark.expected_marking_player_id = uint16_t(live_state.tactics.at(own_mark_at) |
            uint16_t(live_state.tactics.at(own_mark_at + 1)) << 8);
        check(live_session->dispatch_live_pitch(abandoned_mark, 3, 2, live_engine, live_revision, live_error),
              "a valid Source marking assignment can be staged before modal abort");
        live_session->abort_live_intervention(3, 2, live_manager, live_team, live_engine, live_revision);
        check(save_stream(live_state) == before_mark_abort && live_state.tactics == before_mark_abort_tactics &&
              live_groups[0].records == before_mark_abort_groups[0].records && live_revision == before_mark_abort_revision &&
              live_session->game().seed() == before_mark_abort_game.seed() &&
              live_session->game().calls() == before_mark_abort_game.calls() &&
              live_session->rand().seed() == before_mark_abort_rand.seed() &&
              live_session->rand().calls() == before_mark_abort_rand.calls(),
              "aborted Manndeckung restores the full menu-entry state, assignments, revision and RNGs");
    }

    const size_t observed_count = frames.size();
    auto plain_next = kickoff(plain_flow), observed_next = kickoff(observed_flow);
    plain_message = app::serve_flow_request(plain_flow, plain_next, plain, data, data, seed,
        plain_revision, plain_playback, plain_audio, false);
    observed_message = app::serve_flow_request(observed_flow, observed_next, observed, data, data, seed,
        observed_revision, observed_playback, observed_audio, false);
    check(plain_message.empty() && observed_message.empty() && frames.size() == observed_count &&
          save_stream(plain->state()) == save_stream(observed->state()) &&
          plain->rand().seed() == observed->rand().seed() && plain->rand().calls() == observed->rand().calls(),
          "RAII removes the observer after kickoff and later turns remain deterministic");
}
}

void check_incoming_sale_controller(const std::filesystem::path& data) {
    for (const bool accept : {false, true}) {
        StartupFlow flow(data);
        flow.click(150, 215); flow.click(5, 225, true); flow.click(5, 225, true);
        const auto started = flow.click(90, 169);
        std::unique_ptr<app::CareerSession> session;
        uint64_t revision = 0;
        openbmh::audio::Playback playback;
        std::optional<uint64_t> audio_session;
        constexpr uint32_t seed = 0x52061004;
        check(app::serve_flow_request(flow, started, session, data, data, seed, revision,
            playback, audio_session, false).empty() && session, "incoming sale starts through production controller");
        auto& game = session->state();
        const int seller_manager = int(game.memory.u8(::bmh::layout::ActiveManager));
        const int seller = game.manager_teams.at(size_t(seller_manager));
        const SquadRange seller_range = squad_range(seller);
        int entry = -1, player = -1;
        MinuteReport listing_report;
        for (int row = int(seller_range.count) - 1; row >= 11; --row) {
            const auto result = list_manager_player(game, seller_manager, row, session->rand(), listing_report);
            if (result.status == TransferActionStatus::accepted) { entry = result.list_entry; player = result.player; break; }
        }
        check(entry >= 0 && player >= 0 && !listing_report.unsupported, "incoming fixture lists actual reserve player");
        std::array<int, 2> buyers{-1, -1};
        int found = 0;
        for (int team = 0; team < 138 && found < 2; ++team) {
            if (game.manager_of(team) >= 0) continue;
            const auto range = squad_range(team);
            if (squad_count(game, team, 3) < int(range.count) - 1) buyers[size_t(found++)] = team;
        }
        check(found == 2, "two original AI squads can receive the sold player");
        const uint32_t at = ::bmh::layout::EditorList + uint32_t(entry) * 0x58;
        for (int bid = 0; bid < 4; ++bid) game.memory.set16(at + uint32_t(bid) * 2, 0xffff);
        for (int bid = 0; bid < 2; ++bid) {
            game.memory.set16(at + uint32_t(bid) * 2, uint16_t(buyers[size_t(bid)]));
            const uint32_t multiplier = bid == 0 ? 4 : 3;
            game.memory.set32(at + 0x1c + uint32_t(bid) * 4, game.memory.u32(at + 0x2c) * multiplier);
            game.memory.set32(at + 0x0c + uint32_t(bid) * 4, game.memory.u32(at + 0x30) * multiplier);
            game.memory.set8(at + 0x34 + uint32_t(bid), 12);
        }
        game.memory.set8(at + 0x56, 1);
        for (int k = 0; k < 40; ++k)
            if (k != entry && (game.squad_slots.at(0xb70 + size_t(k)) & 0xfff) != 0xfff)
                game.memory.set8(::bmh::layout::EditorList + uint32_t(k) * 0x58 + 0x56, 9);
        const int32_t money_before = signed_u32(game.manager_records, size_t(seller_manager) * 0xb0c + 0x314);
        const uint32_t selected_fee = game.memory.u32(at + 0x20);
        flow.attach_preparation_context(session->preparation_projection(revision));
        app::CareerSession expected = *session;
        uint64_t expected_revision = revision;
        int expected_calls = 0, actual_calls = 0;
        expected.set_transfer_offer_selector([&](const GameState&, const TransferOfferListView& view) {
            if (view.list_entry != entry || view.player != player) return -1;
            ++expected_calls;
            return accept ? 1 : -1;
        });
        app::TransferOfferPresenter presenter = [&](const GameState&, const TransferOfferListView& view) {
            if (view.list_entry != entry || view.player != player) return -1;
            check(view.seller_manager == seller_manager && view.seller == seller &&
                  view.offers[0].present && view.offers[1].present &&
                  view.offers[1].buyer == buyers[1] && view.offers[1].fee == selected_fee,
                  "modal receives immutable bound original raw offer rows");
            ++actual_calls;
            return accept ? 1 : -1;
        };
        for (int turns = 0; turns < 20 && actual_calls == 0; ++turns) {
            const auto request = flow.send_key("G");
            check(request.request == FlowRequest::kickoff, "incoming fixture advances through ordinary Weiter");
            std::string error;
            const auto expected_result = expected.dispatch_kickoff(request.desk_action, expected_revision, error);
            check(expected_result.kind != TurnDeskKind::Failed && error.empty(), "synchronous selection baseline advances");
            check(app::serve_flow_request(flow, request, session, data, data, seed, revision,
                playback, audio_session, false, {}, presenter).empty(), "modal selector is wired through production controller");
            check(flow.screen_name() == "desktop" || flow.screen_name() == "matchday_results",
                  "completed modal returns to the real desk or result flow");
            check(!session->state().transfer_offer_selector, "controller removes borrowed modal hook after each turn");
            check(save_stream(session->state()) == save_stream(expected.state()) &&
                  session->rand().seed() == expected.rand().seed() && session->rand().calls() == expected.rand().calls() &&
                  session->game().seed() == expected.game().seed() && session->game().calls() == expected.game().calls() &&
                  revision == expected_revision,
                  "controller choice matches synchronous complete calendar state and both RNG streams");
            if (flow.in_results_view()) flow.send_key("ESC");
        }
        check(actual_calls == 1 && expected_calls == 1, "human sale prompt runs exactly once");
        const auto owns = [&](int team) {
            const auto range = squad_range(team);
            return std::any_of(game.squad_slots.begin() + std::ptrdiff_t(range.first),
                game.squad_slots.begin() + std::ptrdiff_t(range.first + range.count),
                [&](uint16_t slot) { return (slot & 0xfff) == player; });
        };
        check(owns(seller) == !accept && owns(buyers[1]) == accept && !owns(buyers[0]),
              "selecting a non-top offer or declining applies only the chosen transfer");
        if (accept) check(signed_u32(game.manager_records, size_t(seller_manager) * 0xb0c + 0x314) > money_before,
                          "accepted sale credits the original seller account");
    }
}

void check_league_family_controller(const std::filesystem::path& data) {
    StartupFlow flow(data);
    flow.click(150,215);flow.click(5,225,true);flow.click(5,225,true);
    std::unique_ptr<app::CareerSession> session;uint64_t revision=0;
    openbmh::audio::Playback playback;std::optional<uint64_t> audio;
    app::TransferOfferTeamInfoPresenter presenter;
    const auto run=[&](const FlowResult& input) {
        return app::serve_flow_request(flow,input,session,data,data,0x1789abcd,revision,playback,audio,false,
            {},{},{},{},{},{},{},{},{},app::PendingOfferEntryPolicy::legacy_unpresented,{},
            app::HumanBuyerMarketEntryPolicy::legacy_unpresented,presenter);
    };
    check(run(flow.click(90,169)).empty()&&session&&flow.screen_name()=="desktop",
          "family integration begins with a genuine desk session");
    const auto family_word=[&] {const int manager=session->state().memory.u8(::bmh::layout::ActiveManager);
        return session->state().memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(manager)*2);};
    const auto before_unowned=save_stream(session->state());const auto unowned_game=session->game();
    const auto unowned_rand=session->rand();const auto unowned_revision=revision;
    const auto unowned_runtime=session->club_modal_runtime_checkpoint();
    std::string family_error;
    app::LeagueFamilyActions common_family(*session);
    check(!common_family.apply(revision, [&] {
        // A partial table operation followed by frontend rejection must not
        // leak Native writes, draws, runtime mode, revision or a new owner.
        auto fresh=session->strengths_open_request(revision);
        check(bool(fresh),"shared family rollback has a valid observed entry");
        check(bool(session->begin_league_family(fresh->identity,1,revision,family_error)),
              "shared family rollback creates the same real family owner");
        session->state().date++;
        session->rand().rand();session->game().range(0,10);++revision;
        return false;
    }) && save_stream(session->state())==before_unowned && revision==unowned_revision &&
        session->rand().seed()==unowned_rand.seed() && session->rand().calls()==unowned_rand.calls() &&
        session->game().seed()==unowned_game.seed() && session->game().calls()==unowned_game.calls() &&
        !session->league_family_owner() && session->club_modal_runtime_checkpoint()==unowned_runtime,
        "shared family rejection restores full Native, both RNGs, revision and family owner");
    FlowResult forged;forged.request=FlowRequest::league_family_action;
    forged.league_family_action.kind=LeagueFamilyRequestKind::close;
    forged.league_family_action.from_child=1;forged.league_family_action.target_child=0;
    forged.league_family_action.identity.family_id=1;
    check(!run(forged).empty()&&save_stream(session->state())==before_unowned&&revision==unowned_revision&&
          session->game().seed()==unowned_game.seed()&&session->game().calls()==unowned_game.calls()&&
          session->rand().seed()==unowned_rand.seed()&&session->rand().calls()==unowned_rand.calls(),
          "unowned forged family close is rejected without Native, W/G or revision mutation");
    const auto table_menu=std::find_if(flow.desktop()->state().items.begin(),flow.desktop()->state().items.end(),
        [](const auto& item){return item.kind==0&&!item.disabled;});
    check(table_menu!=flow.desktop()->state().items.end(),"family integration has an enabled Table entry");
    check(run(flow.click(table_menu->x+10,table_menu->y+10)).empty()&&flow.in_family_table(),
          "Table desk entry establishes the bound family owner");

    const auto first_row=flow.league_family_identity()->league;
    (void)first_row;
    const auto table_request=flow.pointer_down(10,29);
    check(table_request.request==FlowRequest::league_family_action&&
          table_request.league_family_action.kind==LeagueFamilyRequestKind::team_info,
          "Table row DOWN emits its typed nested TeamInfo request");
    const auto stale_close=table_request.league_family_action;
    int entered=-1;bool nested_close_rejected=false;
    presenter=[&](app::ClubModalServices& services,int target,const Image&) {
        const auto native=save_stream(session->state());const auto w=session->rand();const auto g=session->game();
        auto inner=stale_close;inner.kind=LeagueFamilyRequestKind::close;inner.target_child=0;
        uint64_t inner_revision=revision;std::string inner_error;
        nested_close_rejected=!session->close_league_family(inner,inner_revision,inner_error).has_value()&&
            save_stream(session->state())==native&&session->rand().seed()==w.seed()&&session->rand().calls()==w.calls()&&
            session->game().seed()==g.seed()&&session->game().calls()==g.calls();
        entered=target;return !services.enter_club(target).displayed_slots.empty();
    };
    check(run(table_request).empty()&&nested_close_rejected&&entered==table_request.league_family_action.runtime_team&&
          flow.in_family_table()&&flow.league_family_identity()->family_sequence==1,
          "staged Table TeamInfo enters the selected club once and blocks reentrant final close");

    const int initial_league=flow.league_family_identity()->league;
    bool selected=false;
    for(int y=176;y<=216&&!selected;++y)for(int x=205;x<=317&&!selected;++x) {
        const auto probe=flow.pointer_down(x,y);
        if(probe.request==FlowRequest::league_family_action&&
           probe.league_family_action.kind==LeagueFamilyRequestKind::select_league) {
            const int wanted=probe.league_family_action.league_id;
            check(run(probe).empty(),"Table selector ACK is accepted");
            (void)flow.click(x,y); // consume Source's orphan mouse-up after immediate selector DOWN
            if(wanted!=initial_league) {
                check(flow.league_family_identity()->league==wanted,
                      "Table selector commits its non-default Source league");
                selected=true;
            }
        }
    }
    if(!selected) {
        // Some generated careers have no secondary selector pixels; exercise the
        // same acknowledged semantic selector with a valid non-default league.
        auto identity=*flow.league_family_identity();
        ui::LeagueFamilyRequest choose;choose.identity=identity;
        choose.kind=LeagueFamilyRequestKind::select_league;choose.from_child=1;
        choose.league_id=(identity.league+1)%9;
        std::string error;
        check(session->select_league_family_page(choose,revision,error),
              "semantic Table selector accepts a valid non-default Source league");
        const auto refreshed=session->league_family_owner()->identity;
        flow.show_family_league_tables(ui::league_tables_from_game(session->state(),identity.manager),
            refreshed.league,refreshed);
        selected=true;
    }
    check(selected,"family fixture commits a non-default league selection");
    const int carried_league=flow.league_family_identity()->league;
    const auto footer=[&](int x,int y) {
        const auto down=flow.pointer_down(x,y);check(down.request==FlowRequest::none,"family footer DOWN only presses");
        const auto up=flow.click(x,y);check(up.request==FlowRequest::league_family_action,"family footer emits on matching UP");
        const auto error=run(up);
        if(!error.empty())throw std::runtime_error("family footer transition is accepted: "+error+
            "; source="+std::to_string(up.league_family_action.from_child)+
            "; target="+std::to_string(up.league_family_action.target_child));
        return up;
    };
    const auto first_navigation=footer(140,207); // Table -> Pairings.
    check(flow.in_league_pairings()&&flow.league_pairings()->state().identity.league==carried_league,
          "Table to Pairings preserves selected league identity");
    const auto control_down=flow.pointer_down(10,180);
    check(control_down.request==FlowRequest::none,"Pairings reset DOWN does not dispatch early");
    const auto control=flow.click(10,180);
    check(control.request==FlowRequest::league_pairings_action&&run(control).empty()&&flow.in_league_pairings(),
          "ordinary Pairings control ACK retains the family child and allows navigation");
    const auto pairings_to_strengths=footer(80,215); // Pairings -> Strengths.
    check(flow.in_strengths()&&flow.strengths()->state().identity.league==carried_league,
          "Pairings to Strengths preserves selected league identity");
    footer(140,215); // Strengths -> Table.
    check(flow.in_family_table()&&flow.league_family_identity()->league==carried_league,
          "Strengths to Table preserves selected league identity");
    footer(140,215); // Table -> Strengths.
    check(flow.in_strengths(),"Table to Strengths transition succeeds");
    footer(140,207); // Strengths -> Pairings.
    check(flow.in_league_pairings(),"Strengths to Pairings transition succeeds");
    footer(80,202); // Pairings -> Table.
    check(flow.in_family_table()&&flow.league_family_identity()->league==carried_league,
          "Pairings to Table preserves selected league identity");
    const auto state_before_stale=save_stream(session->state());const auto w_before_stale=session->rand();
    const auto g_before_stale=session->game();const auto rev_before_stale=revision;
    check(!run(first_navigation).empty()&&save_stream(session->state())==state_before_stale&&revision==rev_before_stale&&
          session->rand().seed()==w_before_stale.seed()&&session->rand().calls()==w_before_stale.calls()&&
          session->game().seed()==g_before_stale.seed()&&session->game().calls()==g_before_stale.calls(),
          "Table-Pairings-Table cycle consumes the original navigation token and rejects stale replay neutrally");

    const auto preclose=save_stream(session->state());auto expected_game=session->game();auto expected_rand=session->rand();
    stir_random(expected_game,expected_rand);
    const auto close=flow.pointer_down(5,225,true);
    check(close.request==FlowRequest::league_family_action&&close.league_family_action.kind==LeagueFamilyRequestKind::close,
          "Table right-click emits Original Source family close on DOWN");
    check(run(close).empty()&&!flow.league_family_identity()&&revision==rev_before_stale+1&&
          save_stream(session->state())==preclose&&session->game().seed()==expected_game.seed()&&
          session->game().calls()==expected_game.calls()&&session->rand().seed()==expected_rand.seed()&&
          session->rand().calls()==expected_rand.calls(),
          "final family close matches exactly one Source W(100,300)+N G(0,1) stir");
    const auto closed_native=save_stream(session->state());const auto closed_game=session->game();
    const auto closed_rand=session->rand();const auto closed_revision=revision;
    check(!run(close).empty()&&save_stream(session->state())==closed_native&&revision==closed_revision&&
          session->game().seed()==closed_game.seed()&&session->game().calls()==closed_game.calls()&&
          session->rand().seed()==closed_rand.seed()&&session->rand().calls()==closed_rand.calls(),
          "replayed consumed final close cannot stir twice");
}

void check_pairings_timer_controller(const std::filesystem::path& data) {
    check(league_pairings_source_seconds(499)==0 && league_pairings_source_seconds(500)==1 &&
          league_pairings_source_seconds(1499)==1 && league_pairings_source_seconds(1500)==2,
          "Source hundredths clock rounds at 500 milliseconds");
    StartupFlow flow(data);
    flow.click(150,215); flow.click(5,225,true); flow.click(5,225,true);
    const auto start=flow.click(90,169);
    std::unique_ptr<app::CareerSession> session;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio;
    uint64_t revision=0;
    const auto serve=[&](const FlowResult& input) {
        check(app::serve_flow_request(flow,input,session,data,data,0x1789abcd,revision,
              playback,audio,false).empty(),"timer uses production pairings controller");
    };
    serve(start);
    const auto menu=[&](int kind) {
        const auto& items=flow.desktop()->state().items;
        const auto it=std::find_if(items.begin(),items.end(),[&](const auto& item) {
            return item.kind==kind && !item.disabled;
        });
        check(it!=items.end(),"timer's actual desk menu is enabled");
        return flow.click(it->x+it->width/2,it->y+it->height/2);
    };
    const auto exe=load_le(asset_path(data,"BMH.exe"));
    bool ready=false;
    for(int turn=0;turn<24;++turn) {
        auto staged=session->state();
        const auto owner=open_league_pairings_source(staged,int(staged.memory.u8(::bmh::layout::ActiveManager)),
                                                     league_pairings_constants(exe));
        const auto checkpoint=session->manager_join_checkpoint();
        const auto packet=league_pairings_active_packet_source(staged,owner,checkpoint.driver.groups,
                                                               checkpoint.driver.referees);
        if(packet) {ready=true;break;}
        serve(menu(12));
        if(flow.in_results_view())serve(flow.send_key("ESC"));
    }
    check(ready,"actual career reaches an active league fixture desk");
    const auto native_before_open=save_stream(session->state());
    const auto family_word_before=session->state().memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(session->state().memory.u8(::bmh::layout::ActiveManager))*2);
    serve(menu(1));
    check(flow.in_league_pairings() && flow.league_pairings()->state().active_packet &&
          !flow.league_pairings()->state().source.owner.active_phase,
          "actual opening accepts active fixtures in ordinary initial phase");
    check(save_stream(session->state())!=native_before_open &&
          session->state().memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(session->state().memory.u8(::bmh::layout::ActiveManager))*2)!=family_word_before,
          "legacy routine Pairings entry performs its Source family-mode word update");
    const auto native=save_stream(session->state());const auto w=session->rand();const auto g=session->game();
    const auto rev=revision;
    const auto phase0=flow.render().pixels;
    const auto unchanged=[&] {
        check(save_stream(session->state())==native && revision==rev &&
              session->rand().seed()==w.seed() && session->rand().calls()==w.calls() &&
              session->game().seed()==g.seed() && session->game().calls()==g.calls(),
              "timer, rendering and ACK preserve live Native and both generators");
    };
    check(flow.tick_league_pairings_source_seconds(100).request==FlowRequest::none,
          "an unpresented page has no timer anchor");
    flow.mark_league_pairings_presented_source_seconds(10);
    check(flow.tick_league_pairings_source_seconds(11).request==FlowRequest::none,
          "one Source second does not redraw");
    const auto tick=flow.tick_league_pairings_source_seconds(12);
    check(tick.request==FlowRequest::league_pairings_action &&
          tick.league_pairings_action.event.kind==LeaguePairingsAction::presentation_redraw &&
          tick.league_pairings_action.event.value==1 &&
          !flow.league_pairings()->state().source.owner.active_phase,
          "two seconds posts an owned request without optimistic phase change");
    check(flow.tick_league_pairings_source_seconds(100).request==FlowRequest::none &&
          *flow.pending_league_pairings()==tick.league_pairings_action,
          "timer never re-emits or overwrites an existing pending action");
    bool wrong_ack=false;
    try {flow.acknowledge_league_pairings(flow.league_pairings()->state());}
    catch(const FormatError&) {wrong_ack=true;}
    check(wrong_ack && *flow.pending_league_pairings()==tick.league_pairings_action &&
          flow.render().pixels==phase0,"failed page ACK keeps the accepted phase and request");
    serve(tick);
    check(flow.league_pairings()->state().source.owner.active_phase &&
          flow.render().pixels!=phase0,"accepted redraw exposes referee and weather phase");
    unchanged();
    const auto snapshot=flow.render().pixels;
    serve(tick);
    check(flow.render().pixels==snapshot,"consumed timer replay is inert");
    check(flow.tick_league_pairings_source_seconds(100).request==FlowRequest::none,
          "accepted page waits for actual presentation before arming again");
    flow.mark_league_pairings_presented_source_seconds(12);
    check(flow.tick_league_pairings_source_seconds(13).request==FlowRequest::none,
          "one second after phase presentation does not redraw");
    serve(flow.tick_league_pairings_source_seconds(100));
    check(!flow.league_pairings()->state().source.owner.active_phase &&
          flow.render().pixels==phase0,"long gap toggles once and restores the original phase");
    flow.mark_league_pairings_presented_source_seconds(100);
    check(flow.tick_league_pairings_source_seconds(101).request==FlowRequest::none,
          "long-gap redraw gets a fresh presentation anchor");
    const int initial_league=flow.league_pairings()->state().source.owner.league;
    const auto select_league=[&](int target) {
        for(int y=176;y<=216;++y)for(int x=205;x<=260;++x) {
            const auto selection=flow.league_pairings()->selector_event(x,y);
            if(selection && selection->kind==LeaguePairingsAction::select_league &&
               selection->value==target)return flow.pointer_down(x,y);
        }
        throw std::runtime_error("missing actual Original league selector");
    };
    const auto same_league=select_league(initial_league);
    check(same_league.request==FlowRequest::none && !flow.pending_league_pairings() &&
          flow.tick_league_pairings_source_seconds(101).request==FlowRequest::none,
          "same-league click neither navigates nor resets the Source anchor");
    serve(flow.tick_league_pairings_source_seconds(102));
    check(flow.league_pairings()->state().source.owner.active_phase,
          "same-league click cannot postpone the due Source phase");
    flow.mark_league_pairings_presented_source_seconds(102);
    serve(flow.tick_league_pairings_source_seconds(104));
    check(flow.render().pixels==phase0,"same-league timing restores strength normally");
    flow.mark_league_pairings_presented_source_seconds(104);
    // legacy routine runs before a due timer. The pending selector must retain the
    // initial phase, and its acknowledged redraw needs a fresh presentation.
    const auto selection=select_league(initial_league==0?1:0);
    check(selection.request==FlowRequest::league_pairings_action &&
          flow.tick_league_pairings_source_seconds(200).request==FlowRequest::none,
          "due timer cannot precede an owned changed-league selector");
    serve(selection);
    check(!flow.league_pairings()->state().source.owner.active_phase &&
          flow.tick_league_pairings_source_seconds(200).request==FlowRequest::none,
          "selector retains phase and waits for its actual redraw anchor");
    serve(select_league(initial_league));
    check(flow.render().pixels==phase0,"return selector restores the original strength phase");
    flow.mark_league_pairings_presented_source_seconds(200);
    unchanged();
    const auto down=flow.pointer_down(90,194); // Original ellipsis release helper.
    check(down.changed && down.request==FlowRequest::none,"actual held button is owned");
    check(flow.tick_league_pairings_source_seconds(200).request==FlowRequest::none,
          "timer cannot interrupt a held-control release helper");
    serve(flow.click(90,194));
    const auto next=flow.pointer_down(180,200);
    check(next.changed && next.request==FlowRequest::none,"round arrow begins its physical hold");
    const auto navigation=flow.click(180,200);
    check(navigation.request==FlowRequest::league_pairings_action,"release posts round navigation");
    check(flow.tick_league_pairings_source_seconds(200).request==FlowRequest::none &&
          *flow.pending_league_pairings()==navigation.league_pairings_action,
          "deadline cannot service an unrelated pending navigation request");
    serve(navigation);
    check(!flow.league_pairings()->state().active_packet &&
          flow.tick_league_pairings_source_seconds(300).request==FlowRequest::none,
          "unmatched round does not arm active presentation");
    unchanged();
    serve(flow.pointer_down(0,0,true));
    check(flow.screen_name()=="desktop" &&
          flow.tick_league_pairings_source_seconds(400).request==FlowRequest::none,
          "closed family owns no timer");
}

void check_stadium_investment_controller(const std::filesystem::path& data) {
    const auto saves=std::filesystem::temp_directory_path()/
        ("openbmh-stadium-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(saves);
    StartupFlow flow(data,saves);
    flow.click(150,215); flow.click(5,225,true); flow.click(5,225,true);
    const auto start=flow.click(90,169);
    std::unique_ptr<app::CareerSession> session;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio;
    uint64_t revision=0;
    ConstructionNoticeHook construction_notice;
    const auto serve=[&](const FlowResult& input) {
        check(app::serve_flow_request(flow,input,session,data,saves,0x1789abcd,revision,
              playback,audio,false,{}, {}, {}, {}, {}, {}, {}, {}, {},
              app::PendingOfferEntryPolicy::legacy_unpresented,{},
              app::HumanBuyerMarketEntryPolicy::legacy_unpresented,{}, {},construction_notice).empty(),
              "stadium uses the production controller");
    };
    serve(start);
    {
        app::StadiumActions common(*session);
        const auto native=save_stream(session->state());const auto g=session->game();const auto w=session->rand();
        const auto rev=revision;const auto before=session->stadium_checkpoint();std::string error;
        auto observed=*session->preparation_projection(revision).stadium;
        ++observed.revision;
        check(!common.dispatch(app::StadiumActions::request(observed,StadiumRequestKind::open),revision,error),
              "shared stadium boundary rejects a stale observed opening");
        bool acknowledged=false;
        try {
            --observed.revision;
            common.dispatch(app::StadiumActions::request(observed,StadiumRequestKind::open),revision,error,{},{},[&] {
                acknowledged=session->stadium_checkpoint().stadium.opened;
                session->state().date++;session->game().range(0,10);session->rand().rand();
                throw std::runtime_error("stadium frontend ACK refused");
            });
        } catch(const std::runtime_error&) {}
        check(acknowledged && save_stream(session->state())==native && revision==rev &&
            session->game().seed()==g.seed() && session->game().calls()==g.calls() &&
            session->rand().seed()==w.seed() && session->rand().calls()==w.calls() &&
            session->stadium_checkpoint().stadium.opened==before.stadium.opened &&
            session->stadium_checkpoint().next_scope==before.next_scope,
            "shared stadium rejection restores complete Native, RNGs, revision, owner and next scope");
    }
    auto& initial=session->state();
    const int manager=initial.memory.u8(::bmh::layout::ActiveManager);
    const size_t record=size_t(manager)*0xb0c;
    const auto put32=[&](size_t offset,int32_t value) {
        for(unsigned b=0;b<4;++b) initial.manager_records.at(record+offset+b)=uint8_t(uint32_t(value)>>(8*b));
    };
    // Explicit in-memory test controls; not an alleged Original saved career.
    put32(0x314,100000000); put32(0x31c,0);
    initial.manager_records.at(record+0x171)=0;
    for(unsigned choice=0;choice<8;++choice) {
        initial.manager_records.at(record+0x2c+choice)=0;
        initial.manager_records.at(record+0x3c+choice)=uint8_t(choice==7?3:1);
    }
    auto copied_w=session->rand();
    write_savegame(saves/"AUTOSAVE.FBM",make_savegame(initial,copied_w));
    const auto exe=load_le(asset_path(data,"BMH.exe"));
    flow=StartupFlow(data,saves);
    revision=0;
    serve(flow.click(190,215));
    serve(flow.click(100,45));
    const auto load_fixture=flow.send_key("ENTER");
    check(load_fixture.request==FlowRequest::file_action&&load_fixture.file_action.action==SaveLoadAction::load,
          "controlled fixture requests actual load, not Save");
    serve(load_fixture);
    check(flow.screen_name()=="desktop","controlled stadium fixture enters through real Native loading");
    auto& game=session->state();
    const auto menu=[&] {
        const auto& items=flow.desktop()->state().items;
        const auto it=std::find_if(items.begin(),items.end(),[](const auto& item){return item.kind==22&&!item.disabled;});
        check(it!=items.end(),"actual MEN layout offers the club-only stadium menu");
        return flow.click(it->x+it->width/2,it->y+it->height/2);
    };
    const auto released=[&](int x,int y,bool right=false) {
        flow.pointer_down(x,y,right);
        return flow.click(x,y,right);
    };
    const auto native_before_open=save_stream(game);
    auto expected_game=session->game();
    const int percent=construction_price_percent(game,manager,expected_game);
    const auto open=menu();
    serve(open);
    check(flow.in_stadium()&&flow.stadium()->state().opened&&flow.stadium()->state().scope>0&&
          flow.stadium()->state().sequence==1&&save_stream(game)==native_before_open&&
          session->game().seed()==expected_game.seed()&&session->game().calls()==expected_game.calls(),
          "opening owns exactly the Source discount draw and no Native edits");
    constexpr uint32_t prices[]{900000,750000,1800000,2200000,350000,600000,550000,1000000};
    constexpr uint8_t weeks[]{15,12,25,20,4,7,9,5};
    StadiumInvestmentConstants constants;
    std::copy(std::begin(prices),std::end(prices),constants.price.begin());
    std::copy(std::begin(weeks),std::end(weeks),constants.weeks.begin());
    for(unsigned choice=0;choice<8;++choice) {
        const auto& row=flow.stadium()->state().investments[choice];
        check(row.available&&row.options.front().base_price==prices[choice]*uint32_t(percent)/100&&
              row.options.front().base_weeks==weeks[choice]&&
              row.options.size()==((choice==2||choice==3||choice==7)?1:3),
              "all eight investments use literal facility prices/weeks and relative Source tiers");
    }
    check(flow.click(115,190).request==FlowRequest::none,
          "an orphan release cannot select investment six");
    flow.pointer_down(115,190);
    flow.move_action(300,20);
    check(flow.click(115,190).request==FlowRequest::none,
          "moving outside cancels a held investment without a new DOWN");
    // Header G draw belongs to selection; initial Abbruch never asks/draws W.
    const auto cancel_native=save_stream(game);
    const auto cancel_w=session->rand();
    serve(released(115,190));
    check(flow.stadium()->state().selected_investment==StadiumInvestment::Condition,
          "lower third cell routes Condition, not an invented block G");
    expected_game.set_seed(int32_t(game.memory.u32(::bmh::layout::CareerSession_Invalid_RuntimeField2)));
    expected_game.range(3,10);
    check(session->game().seed()==expected_game.seed()&&session->game().calls()==expected_game.calls(),
          "selection consumes one authentic reseeded header draw");
    serve(released(110,140));
    check(!flow.stadium()->state().selected_investment&&save_stream(game)==cancel_native&&
          session->rand().seed()==cancel_w.seed()&&session->rand().calls()==cancel_w.calls(),
          "initial Abbruch leaves Native and W unchanged before the tip prompt");

    for(const unsigned choice : {6u,7u,2u,3u}) {
        serve(released(10+int(choice%4)*54,160+int(choice/4)*30));
        check(flow.stadium()->state().selected_investment==static_cast<StadiumInvestment>(choice),
              "real lower-grid hit routes its exact facility choice");
        check(!flow.stadium()->state().selected_option&&released(10,140).request==FlowRequest::none,
              "Source requires an explicit tier choice before Ausbauen");
        const int option_index=choice==6?1:0;
        serve(released(10+60*option_index,80));
        const auto option=flow.stadium()->state().investments[choice].options.at(size_t(option_index));
        check(option.increment==(choice==6?2:1)&&option.target_level==(choice==6?3:choice==7?4:2),
              "normal options are relative; heating/VIP stop at installed level two");
        const auto native_before=save_stream(game);
        const auto before_w=session->rand();
        const auto before_g=session->game();
        auto expected_w=before_w;
        auto tip=prepare_stand_block_offer(option.base_price,option.base_weeks,expected_w);
        const auto confirm=released(10,140);
        const auto confirm_request=std::get<StadiumRequest>(confirm.submenu_action);
        auto forged=confirm_request; --forged.price;
        std::string error;
        const auto rejected_revision=revision;
        check(!session->dispatch_stadium(forged,revision,error)&&revision==rejected_revision&&
              save_stream(game)==native_before&&session->rand().calls()==before_w.calls()&&
              session->game().calls()==before_g.calls(),"cheap forged initial quote is neutral before W");
        auto wrong_scope=confirm_request; ++wrong_scope.scope;
        check(!session->dispatch_stadium(wrong_scope,revision,error)&&save_stream(game)==native_before&&
              session->rand().seed()==before_w.seed(),"wrong scope cannot ask or reroll the tip");
        serve(confirm);
        check(flow.stadium()->state().construction_tip_dialog&&
              flow.stadium()->state().construction_tip_dialog->source_flags==0x204&&
              flow.stadium()->state().construction_tip_price==tip.offered_price&&
              save_stream(game)==native_before&&session->rand().seed()==expected_w.seed()&&
              session->rand().calls()==expected_w.calls(),"initial Ausbauen asks Original mode 0x204 with exactly W(6,14)");
        serve(confirm);
        check(session->rand().calls()==expected_w.calls(),"consumed confirm replay cannot reroll a tip");
        check(flow.send_key("ESC").request==FlowRequest::none&&released(20,20,true).request==FlowRequest::none,
              "ESC and right-click do not answer the Source tip question");
        auto premature_close=confirm_request;
        premature_close.kind=StadiumRequestKind::close;
        premature_close.object=-1; premature_close.option=-1; premature_close.price=0;
        premature_close.revision=revision;
        premature_close.sequence=flow.stadium()->state().sequence;
        const auto before_close_w=session->rand();
        check(!session->dispatch_stadium(premature_close,revision,error)&&save_stream(game)==native_before&&
              session->rand().calls()==before_close_w.calls(),"a direct close cannot discard an owned tip modal");
        auto wrong_ack=session->preparation_projection(revision+1);
        ++wrong_ack.stadium->scope;
        // Pending answer remains owned if a foreign ACK arrives.
        const bool yes=choice==7||choice==3;
        const auto answer=released(yes?70:180,170);
        bool rejected=false;
        try {flow.acknowledge_preparation(wrong_ack);} catch(const FormatError&) {rejected=true;}
        check(rejected&&flow.matches_pending_stadium(std::get<StadiumRequest>(answer.submenu_action)),
              "a wrong-scope ACK preserves the accepted tip and pending answer");
        resolve_stand_block_offer(tip,yes,expected_w);
        auto expected_native=game;
        check(build_stadium_investment_source(expected_native,manager,static_cast<StadiumInvestment>(choice),
              percent,constants,option,tip,expected_w),"Source facility payment accepts the controlled account");
        const auto accepted_revision=revision;
        serve(answer);
        check(revision==accepted_revision+1&&save_stream(game)==save_stream(expected_native)&&
              session->rand().seed()==expected_w.seed()&&session->rand().calls()==expected_w.calls()&&
              session->game().seed()==before_g.seed()&&session->game().calls()==before_g.calls()&&
              !flow.stadium()->state().selected_investment&&!flow.stadium()->state().construction_tip_dialog,
              "actual controller NO/YES builds once with exact Native and W terms");
        check(game.manager_records.at(record+0x2c+choice)==tip.construction_days&&
              (game.manager_records.at(record+0x3c+choice)>>4)==option.target_level&&
              (game.manager_records.at(record+0x3c+choice)&7)==option.current_level,
              "acceptance starts construction and preserves current level until weekly completion");
        const auto accepted_native=save_stream(game);
        serve(answer);
        check(revision==accepted_revision+1&&save_stream(game)==accepted_native&&
              session->rand().calls()==expected_w.calls(),"answer replay performs no second payment or RNG draw");
        auto completed=game;
        auto completed_g=session->game(); auto completed_w=session->rand();
        // A controlled last construction week exercises the existing weekly path.
        completed.manager_records.at(record+0x2c+choice)=1;
        MinuteReport report;
        facility_ageing(completed,manager,completed_g,completed_w,report);
        check(completed.manager_records.at(record+0x2c+choice)==0&&
              completed.manager_records.at(record+0x3c+choice)==option.target_level&&
              completed.manager_records.at(record + 0x0e + choice)>0,
              "weekly completion promotes the pending facility level and renews condition");
    }
    // Source's process timer owns NO after the held helper has returned.
    serve(released(10,160));
    serve(released(10,80));
    const auto timeout_option=flow.stadium()->state().investments[0].options.front();
    auto timeout_w=session->rand();
    auto timeout_tip=prepare_stand_block_offer(timeout_option.base_price,timeout_option.base_weeks,timeout_w);
    serve(released(10,140));
    auto runtime=session->club_modal_runtime_checkpoint();
    for(unsigned b=0;b<4;++b) {
        const uint8_t started=b==0?1:0;
        if(game.memory.has(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField2+b))game.memory.at(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField2+b)=started;
        else runtime[::bmh::layout::CareerSession_DispatchStrengths_ExecutableField2+b]=started;
        if(game.memory.has(::bmh::layout::CareerSession_FormatError_ExecutableField2+b))game.memory.at(::bmh::layout::CareerSession_FormatError_ExecutableField2+b)=0xff;
        else runtime[::bmh::layout::CareerSession_FormatError_ExecutableField2+b]=0xff;
    }
    session->restore_club_modal_runtime(runtime);
    check(session->stadium_tip_deadline_expired(),"Source process deadline uses signed remaining time");
    flow.pointer_down(70,170);
    check(flow.stadium()->construction_tip_held(),"the actual Yes button is held before a deadline answer");
    const auto expiry=flow.expire_stadium_tip();
    check(expiry.request==FlowRequest::submenu_action&&!std::get<StadiumRequest>(expiry.submenu_action).accept_tip,
          "deadline emits the owned base-price No rather than abandoning construction");
    auto timeout_native=game;
    resolve_stand_block_offer(timeout_tip,false,timeout_w);
    check(build_stadium_investment_source(timeout_native,manager,StadiumInvestment::Floodlights,
          percent,constants,timeout_option,timeout_tip,timeout_w),"controlled deadline construction is affordable");
    serve(expiry);
    check(save_stream(game)==save_stream(timeout_native)&&session->rand().seed()==timeout_w.seed()&&
          session->rand().calls()==timeout_w.calls()&&!flow.stadium()->construction_tip_held()&&
          flow.click(70,170).request==FlowRequest::none,"timeout consumes the held modal and its later release exactly once");
    const auto close=flow.send_key("ESC");
    check(flow.in_stadium()&&flow.matches_pending_stadium(std::get<StadiumRequest>(close.submenu_action)),
          "close remains pending until its owned ACK");
    auto invalid_close=session->preparation_projection(revision+1);
    invalid_close.stadium->sequence=flow.stadium()->state().sequence+1;
    bool close_rejected=false;
    try{flow.acknowledge_preparation(invalid_close);}catch(const FormatError&){close_rejected=true;}
    check(close_rejected&&flow.in_stadium()&&flow.matches_pending_stadium(std::get<StadiumRequest>(close.submenu_action)),
          "an ACK retaining the open stadium cannot consume a close");
    const auto closed_native=save_stream(game); const auto closed_w=session->rand();
    serve(close);
    check(!flow.in_stadium()&&flow.screen_name()=="desktop"&&save_stream(game)==closed_native&&
          session->rand().calls()==closed_w.calls(),"accepted close disposes of the modal without Native/W effects");

    // Explicit negative-credit fixture enters through the real Native loader.
    // Source may consume a sponsor contribution before later payment refusal.
    auto refusal_fixture=game;
    for(unsigned b=0;b<4;++b) {
        refusal_fixture.manager_records.at(record+0x314+b)=uint8_t(uint32_t(10000000)>>(8*b));
        refusal_fixture.manager_records.at(record+0x31c+b)=uint8_t(uint32_t(-20000000)>>(8*b));
    }
    refusal_fixture.manager_records.at(record+0x171)=4;
    refusal_fixture.manager_records.at(record+0x2d)=0;
    refusal_fixture.manager_records.at(record+0x3d)=1;
    auto refusal_save_w=session->rand();
    write_savegame(saves/"AUTOSAVE.FBM",make_savegame(refusal_fixture,refusal_save_w));
    flow=StartupFlow(data,saves);
    revision=0;
    serve(flow.click(190,215));
    serve(flow.click(100,45));
    const auto load_refusal_fixture=flow.send_key("ENTER");
    check(load_refusal_fixture.request==FlowRequest::file_action&&load_refusal_fixture.file_action.action==SaveLoadAction::load,
          "controlled fixture requests actual load, not Save");
    serve(load_refusal_fixture);
    auto& refused_game=session->state();
    const auto rd_fixture32=[&](size_t offset) {
        uint32_t value=0;for(unsigned byte=0;byte<4;++byte)
            value|=uint32_t(refused_game.manager_records.at(record+offset+byte))<<(8*byte);
        return int32_t(value);
    };
    check(rd_fixture32(0x314)==10000000&&rd_fixture32(0x31c)==-20000000&&
          refused_game.manager_records.at(record+0x171)==4&&refused_game.level!=0,
          "real Native load installs the non-free negative-credit sponsor-refusal fixture");
    serve(menu());
    const int refusal_percent=flow.stadium()->state().construction_percent;
    serve(released(65,160)); serve(released(10,80));
    const auto refusal_option=flow.stadium()->state().investments[1].options.front();
    auto refusal_w=session->rand();
    auto refusal_tip=prepare_stand_block_offer(refusal_option.base_price,refusal_option.base_weeks,refusal_w);
    serve(released(10,140));
    auto refused_expected=refused_game;
    resolve_stand_block_offer(refusal_tip,false,refusal_w);
    check(!build_stadium_investment_source(refused_expected,manager,StadiumInvestment::Scoreboard,
          refusal_percent,constants,refusal_option,refusal_tip,refusal_w)&&
          refused_expected.manager_records.at(record+0x171)==0,
          "eligible sponsor is consumed before signed balance-plus-credit refuses payment");
    const auto refusal_answer=released(180,170); const auto refusal_revision=revision;
    const auto refusal_request=std::get<StadiumRequest>(refusal_answer.submenu_action);
    const auto staged_native=save_stream(refused_game);
    const auto staged_w=session->rand(); const auto staged_g=session->game();
    const auto staged_owner=session->stadium_checkpoint().stadium;
    // Even a FormatError from presentation must escape validation-only engine
    // catches and restore the entire pending answer, not become a funds refusal.
    for(const auto stop_at:{ConstructionNoticeKind::SponsorExploited,
                            ConstructionNoticeKind::InsufficientFunds}) {
        bool interrupted=false;
        std::string error;
        try {
            session->dispatch_stadium(refusal_request,revision,error,
                [&](const GameState&,const ConstructionNotice& notice) {
                    if(notice.kind==stop_at)throw FormatError("controlled presenter interruption");
                });
        } catch(const FormatError&) {interrupted=true;}
        const auto restored=session->stadium_checkpoint().stadium;
        check(interrupted&&revision==refusal_revision&&save_stream(refused_game)==staged_native&&
              session->rand().seed()==staged_w.seed()&&session->rand().calls()==staged_w.calls()&&
              session->game().seed()==staged_g.seed()&&session->game().calls()==staged_g.calls()&&
              restored.scope==staged_owner.scope&&restored.sequence==staged_owner.sequence&&
              restored.tip&&restored.tip->decision==StandBlockOfferDecision::Pending&&
              flow.matches_pending_stadium(refusal_request),
              "sponsor/funds presenter abort restores Native, both RNGs, owner and pending tip answer");
    }
    // Exercise the real controller catch as well: it clears the rejected UI
    // envelope, while the engine retains the same unconsumed tip for a retry.
    construction_notice=[](const GameState&,const ConstructionNotice&) {
        throw std::runtime_error("controlled controller presenter interruption");
    };
    serve(refusal_answer);
    check(revision==refusal_revision&&save_stream(refused_game)==staged_native&&
          session->rand().seed()==staged_w.seed()&&session->rand().calls()==staged_w.calls()&&
          flow.stadium()->state().construction_tip_dialog,
          "production controller abort preserves the payment checkpoint and lets the UI answer again");
    const auto retry_answer=released(180,170);
    check(flow.matches_pending_stadium(std::get<StadiumRequest>(retry_answer.submenu_action)),
          "retry owns the original unconsumed tip rather than a new random quote");
    std::vector<ConstructionNoticeKind> notice_order;
    construction_notice=[&](const GameState& staged,const ConstructionNotice& notice) {
        check(revision==refusal_revision&&flow.matches_pending_stadium(refusal_request),
              "notice does not advance revision or consume the outer UI ACK");
        if(notice.kind==ConstructionNoticeKind::SponsorExploited)
            check(staged.manager_records.at(record+0x171)==4&&notice.gross_price==refusal_tip.price,
                  "Source sponsor notice observes the original share before it is cleared");
        if(notice.kind==ConstructionNoticeKind::InsufficientFunds)
            check(staged.manager_records.at(record+0x171)==0&&
                  staged.manager_records.at(record+0x2d)==0,
                  "Source funds notice follows share consumption without construction");
        notice_order.push_back(notice.kind);
    };
    serve(retry_answer);
    check(revision==refusal_revision+1&&save_stream(refused_game)==save_stream(refused_expected)&&
          session->rand().seed()==refusal_w.seed()&&session->rand().calls()==refusal_w.calls()&&
          !flow.stadium()->state().construction_tip_dialog&&!flow.stadium()->state().selected_investment&&
          flow.stadium()->state().action_feedback.empty()&&
          notice_order==std::vector<ConstructionNoticeKind>{ConstructionNoticeKind::SponsorExploited,
                                                          ConstructionNoticeKind::InsufficientFunds},
          "production ACK consumes genuine refusal and retains Source sponsor/W effects without construction");
    serve(retry_answer);
    check(revision==refusal_revision+1&&session->rand().calls()==refusal_w.calls(),
          "a consumed refusal cannot charge or draw again");
    check(notice_order.size()==2,"a consumed refusal cannot display either notice again");
    construction_notice={};
    serve(flow.send_key("ESC"));
    std::filesystem::remove_all(saves);
}

void check_menu_layout_session_io(const std::filesystem::path& data) {
    StartupFlow flow(data);
    flow.click(150, 215); flow.click(5, 225, true); flow.click(5, 225, true);
    const auto career = flow.click(90, 169);
    std::unique_ptr<app::CareerSession> session;
    uint64_t app_revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    const auto serve = [&](const FlowResult& result) {
        const auto error = app::serve_flow_request(flow, result, session, data, data, 0x4d454e31,
            app_revision, playback, audio_session, false);
        if (!error.empty()) throw std::runtime_error("menu layout fixture setup failed: " + error);
    };
    serve(career);
    check(session && flow.screen_name() == "desktop", "menu layout uses an initialized active career desk");
    auto& game = session->state();
    const int manager = game.memory.u8(::bmh::layout::ActiveManager);
    check(manager >= 0 && manager < 4, "menu layout fixture has an active manager");
    constexpr uint32_t layouts = ::bmh::layout::Desktops;
    constexpr size_t bytes_count = 0x360;
    const uint32_t base = layouts + uint32_t(manager) * uint32_t(bytes_count);
    const int origin = game.memory.u8(base + 53u * 16u + 12u) == 72 ? 53 : -1;
    check(origin == 53, "Source last compact-bar cell resolves MEN from record 53/kind 72");
    const auto temp = std::filesystem::temp_directory_path() /
        ("openbmh-menu-layout-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temp);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{temp};
    const auto path = temp / "DESIGN.MEN";
    const auto capture_slot = [&] {
        std::vector<uint8_t> result(bytes_count);
        for (size_t i = 0; i < bytes_count; ++i) result[i] = game.memory.u8(base + uint32_t(i));
        return result;
    };
    const auto source_layout = capture_slot();
    std::array<std::vector<uint8_t>, 3> other_layouts;
    for (int other = 0; other < 4; ++other) {
        if (other == manager) continue;
        auto& bytes = other_layouts[size_t(other < manager ? other : other - 1)];
        const uint32_t other_base = layouts + uint32_t(other) * uint32_t(bytes_count);
        bytes.resize(bytes_count);
        for (size_t i = 0; i < bytes_count; ++i) bytes[i] = game.memory.u8(other_base + uint32_t(i));
    }
    const auto other_slots_unchanged = [&] {
        size_t captured = 0;
        for (int other = 0; other < 4; ++other) {
            if (other == manager) continue;
            const uint32_t other_base = layouts + uint32_t(other) * uint32_t(bytes_count);
            for (size_t i = 0; i < bytes_count; ++i)
                if (game.memory.u8(other_base + uint32_t(i)) != other_layouts[captured][i]) return false;
            ++captured;
        }
        return true;
    };
    const auto game_before = session->game();
    const auto rand_before = session->rand();
    uint64_t revision = 7;
    std::string error = "stale error";
    ui::SaveLoadRequest request{ui::SaveLoadAction::save, manager, revision, path, false,
                                ui::SaveLoadFileKind::menu_layout, origin};
    check(session->dispatch_menu_layout(request, revision, error) && error.empty() && revision == 8,
          "menu layout save writes a raw Source payload and clears old errors");
    check(read_file(path) == source_layout && session->game().seed() == game_before.seed() &&
          session->game().calls() == game_before.calls() && session->rand().seed() == rand_before.seed() &&
          session->rand().calls() == rand_before.calls(),
          "MEN save is exactly the active 864-byte slot and consumes no RNG");
    for (size_t i = 0; i < bytes_count; ++i) game.memory.set8(base + uint32_t(i), uint8_t(source_layout[i] ^ 0x5a));
    // Keep the live Source MEN item intact; loading replaces the remaining
    // deliberately modified layout bytes from the authenticated native source.
    game.memory.set8(base + uint32_t(origin) * 16 + 12, source_layout[size_t(origin) * 16 + 12]);
    request.action = ui::SaveLoadAction::load;
    request.revision = revision;
    check(session->dispatch_menu_layout(request, revision, error) && error.empty() && revision == 9 &&
          capture_slot() == source_layout && other_slots_unchanged(),
          "menu layout load restores only the active native slot");
    check(session->game().seed() == game_before.seed() && session->game().calls() == game_before.calls() &&
          session->rand().seed() == rand_before.seed() && session->rand().calls() == rand_before.calls(),
          "MEN load leaves both career RNG streams untouched");

    const auto before_reject = capture_slot();
    const uint64_t before_reject_revision = revision;
    request.revision = revision;
    auto rejected = request;
    rejected.revision--;
    check(!session->dispatch_menu_layout(rejected, revision, error) && capture_slot() == before_reject &&
          revision == before_reject_revision, "stale MEN owner is rejected without mutation");
    rejected = request;
    rejected.manager_slot = (manager + 1) % 4;
    check(!session->dispatch_menu_layout(rejected, revision, error) && capture_slot() == before_reject &&
          revision == before_reject_revision, "wrong manager cannot load a MEN layout");
    rejected = request;
    rejected.file_kind = ui::SaveLoadFileKind::game;
    check(!session->dispatch_menu_layout(rejected, revision, error) && capture_slot() == before_reject &&
          revision == before_reject_revision, "FBM requests cannot enter the menu-layout handler");
    rejected = request;
    rejected.file_kind = ui::SaveLoadFileKind::menu_layout;
    rejected.origin_item_id = (origin + 1) % 54;
    check(!session->dispatch_menu_layout(rejected, revision, error) && capture_slot() == before_reject &&
          revision == before_reject_revision, "nonmatching drawer source address is rejected");
    {
        std::ofstream malformed(path, std::ios::binary | std::ios::trunc);
        malformed << "short";
    }
    rejected = request;
    check(!session->dispatch_menu_layout(rejected, revision, error) && capture_slot() == before_reject &&
          revision == before_reject_revision, "malformed MEN payload is rejected atomically");

    game.memory.set8(::bmh::layout::CareerSession_DispatchDesktopLayout_RuntimeField,0); // Source enables handle/drag dispatch when this byte is zero.
    const auto live_desk=session->desktop_projection(revision);
    check(!live_desk.items.empty(),"layout mutation fixture exposes an original linked desk item");
    const auto moved=live_desk.items.front();
    auto drag_desk=live_desk;drag_desk.drawer_handles=true;
    ui::DesktopScreen local_layout(data,drag_desk);
    local_layout.pointer_down(moved.x+1,moved.y+1,true);
    check(local_layout.pointer_move(moved.x+17,moved.y+17),"layout drag updates a local Source-snapped preview");
    const auto move_request=local_layout.pointer_up(moved.x+17,moved.y+17,true);
    const auto before_move=capture_slot();
    const auto move_rng=session->game();
    const auto move_rand=session->rand();
    check(move_request.kind==ui::DeskRequestKind::change_layout &&
          session->dispatch_desktop_layout(move_request,revision,error) && error.empty() &&
          revision==before_reject_revision+1 && capture_slot()!=before_move,
          "owner-bound desk drag commits its exact typed layout block and advances one revision");
    check(session->game().seed()==move_rng.seed()&&session->game().calls()==move_rng.calls()&&
          session->rand().seed()==move_rand.seed()&&session->rand().calls()==move_rand.calls(),
          "desktop layout movement preserves both RNG streams");

    // Exercise the newly source-backed folder-child return and bin-mode
    // transfer through the authenticated CareerSession boundary, not only the
    // DesktopScreen request projection.
    constexpr uint32_t native_layout=::bmh::layout::Desktops;
    const uint32_t native_base=native_layout+uint32_t(manager)*0x360u;
    for(size_t i=0;i<bytes_count;++i)game.memory.set8(native_base+uint32_t(i),0xff);
    auto put=[&](size_t item,size_t offset,uint8_t value){game.memory.set8(native_base+uint32_t(item*16+offset),value);};
    put(0,5,49);put(0,6,0);
    put(49,0,40);put(49,1,0);put(49,2,40);put(49,3,100);put(49,4,65);
    put(49,5,0);put(49,6,0);put(49,12,50);put(49,13,0x80);
    put(3,0,0);put(3,1,0);put(3,2,0);put(3,3,32);put(3,4,25);
    put(3,5,0xff);put(3,6,0xff);put(3,12,2);put(3,13,0);
    auto folder_desk=session->desktop_projection(revision);
    DesktopScreen child_return_screen(data,folder_desk);
    child_return_screen.pointer_down(50,49,true);
    child_return_screen.pointer_move(175,100);
    const auto return_request=child_return_screen.pointer_up(175,100,true);
    const auto before_return_rng=session->game();const auto before_return_rand=session->rand();
    check(return_request.layout_operation==ui::DeskLayoutOperation::return_folder_child&&
          session->dispatch_desktop_layout(return_request,revision,error)&&error.empty()&&
          game.memory.u8(native_base+3*16+5)==0&&game.memory.u8(native_base+3*16+6)==49&&
          game.memory.u8(native_base+49*16+5)==3,
          "authenticated folder child return appends to the visible chain");
    check(session->game().seed()==before_return_rng.seed()&&session->game().calls()==before_return_rng.calls()&&
          session->rand().seed()==before_return_rand.seed()&&session->rand().calls()==before_return_rand.calls(),
          "folder child return consumes no RNG");
    const auto returned_desk=session->desktop_projection(revision);
    const auto returned_child=std::find_if(returned_desk.items.begin(),returned_desk.items.end(),
        [](const ui::DeskItem& item){return item.id==3;});
    check(returned_child!=returned_desk.items.end(),"returned child is projected as a desk item");
    const auto saved_child_x=game.memory.u16(native_base+3*16);
    const auto saved_child_y=game.memory.u8(native_base+3*16+2);
    auto bin_desk=returned_desk;bin_desk.shortcut_bar_always=true;
    DesktopScreen bin_screen(data,bin_desk);
    bin_screen.click(2,215); // Source local_50 bin mode toggle.
    const auto bin_request=bin_screen.click(returned_child->x+1,returned_child->y+1,true);
    const auto before_bin_rng=session->game();const auto before_bin_rand=session->rand();
    check(bin_request.layout_operation==ui::DeskLayoutOperation::send_to_folder&&
          session->dispatch_desktop_layout(bin_request,revision,error)&&error.empty()&&
          game.memory.u8(native_base+3*16+5)==0xff&&game.memory.u8(native_base+3*16+6)==0xff&&
          game.memory.u8(native_base+3*16+13)==0,
          "bin mode moves the item into its original associated folder without clearing its coordinates");
    check(session->game().seed()==before_bin_rng.seed()&&session->game().calls()==before_bin_rng.calls()&&
          session->rand().seed()==before_bin_rand.seed()&&session->rand().calls()==before_bin_rand.calls(),
          "folder bin transfer consumes no RNG");
    auto binned_desk=session->desktop_projection(revision);binned_desk.shortcut_bar_always=true;
    DesktopScreen restore_screen(data,binned_desk);
    restore_screen.click(2,215);
    const int folder_x=std::find_if(binned_desk.items.begin(),binned_desk.items.end(),
        [](const ui::DeskItem& item){return item.id==49;})->x;
    const int folder_y=std::find_if(binned_desk.items.begin(),binned_desk.items.end(),
        [](const ui::DeskItem& item){return item.id==49;})->y;
    const auto restore_request=restore_screen.click(folder_x+10,folder_y+9,true);
    check(restore_request.layout_operation==ui::DeskLayoutOperation::restore_folder_child&&
          session->dispatch_desktop_layout(restore_request,revision,error)&&error.empty()&&
          game.memory.u8(native_base+3*16+5)==0&&game.memory.u8(native_base+49*16+5)==3&&
          game.memory.u16(native_base+3*16)==saved_child_x&&game.memory.u8(native_base+3*16+2)==saved_child_y,
          "right-clicking a folder child in bin mode restores its saved original position");
}

int main(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 3 && argc != 4) throw std::runtime_error("app_controller_tests requires data directory [optional Original save]");
        const std::filesystem::path data = argv[1];
        if(argc==4 && std::string_view(argv[2])=="--delay-settings-only") {
            check_live_delay_settings_writer(data,argv[3]);check_gameplay_switches_writer(data,argv[3]);
            check_settings_refusal_controller(data,argv[3]);return 0;
        }
        if(argc==4 && std::string_view(argv[2])=="--live-settings-only") {
            check_loaded_live_settings(data,argv[3]);return 0;
        }
        if(argc==4 && std::string_view(argv[2])=="--settings-files-only") {
            check_settings_files(data,argv[3]);return 0;
        }
        if(argc==3 && std::string_view(argv[2])=="--live-intervention-only") {
            check_live_observer_determinism(data); return 0;
        }
        if(argc==3 && std::string_view(argv[2])=="--poach-only") {
            check_poach_ui_components(data);return 0;
        }
        check_poach_ui_components(data);
        check_stadium_investment_controller(data);
        check_pairings_timer_controller(data);
        check_live_observer_determinism(data);
        check_incoming_sale_controller(data);
        check_contract_controller(data);
        check_mailbox_actions(data);
        check_readonly_views(data);
        check_desk_records_controller(data);
        check_menu_layout_session_io(data);
        check_calendar_readonly_controller(data);
        check_indoor_standings_controller(data);
        check_strengths_semantic_and_controller(data);
        check_strengths_controller_ack_rollback(data);
        check_league_family_controller(data);
        check_settings_writer(data);
        check_friendly_match_controller(data);
        check_advertising_controller(data);
        check_environment_controller(data);
        check_advertising_renewal_controller(data);
        check_advertising_spending_controller(data);
        check_original_bank_controller(data);
        if (argc == 3) {
            check_loaded_settings_writer(data, argv[2]);
            check_live_delay_settings_writer(data, argv[2]);
            check_loaded_lineup(data, argv[2]);
        }
        check_pitch_actions_semantic(data);
        check_pitch_actions_controller(data);
        check_semantic_desk_settings(data);
        check_desk_widgets(data);
        check_league_only_national_coach_handoff(data);
        check_two_manager_national_handoff(data);
        check_transfer_market_controller(data);
        StartupFlow flow(data);
        std::unique_ptr<app::CareerSession> session;
        openbmh::audio::Playback playback; // No audio device or window is required.
        std::optional<uint64_t> audio_session;
        uint64_t revision = 0;
        const uint32_t seed = 0x12345678;
        const auto serve = [&](const FlowResult& result) {
            check(app::serve_flow_request(flow, result, session, data, data, seed, revision,
                                         playback, audio_session, false).empty(), "shared App controller accepts flow request");
        };
        serve(flow.click(150, 215));
        serve(flow.click(5, 225, true));
        serve(flow.click(5, 225, true));
        serve(flow.click(90, 169));
        check(session && flow.screen_name() == "desktop", "actual App path initializes career and desktop");
        const auto menu = [&](int kind) {
            const auto& items = flow.desktop()->state().items;
            const auto it = std::find_if(items.begin(), items.end(), [&](const DeskItem& item) {
                return item.kind == kind && !item.disabled;
            });
            check(it != items.end(), "requested menu is enabled in the projected desk");
            return flow.click(it->x + it->width / 2, it->y + it->height / 2);
        };

        // Exercise the actual first-open path, including the UI's pending
        // initialization request and authoritative ACK before the first edit.
        const auto training_game_calls = session->game().calls(), training_watcom_calls = session->rand().calls();
        session->state().manager_options[4] &= 0x7f;
        flow.attach_preparation_context(session->preparation_projection(revision));
        const auto training_open = menu(16);
        check(training_open.request == FlowRequest::submenu_action &&
              std::get<TrainingRequest>(training_open.submenu_action).kind == TrainingRequestKind::initialize,
              "opening uninitialized training emits the original initialization request");
        serve(training_open);
        check(flow.screen_name() == "training" && session->state().manager_options[4] == 0x9f,
              "App initializes training and acknowledges its new projection");
        const auto training = *session->preparation_projection(revision).training;
        const auto type = std::find_if(training.types.begin(), training.types.end(), [](uint8_t value) { return value > 1; });
        check(type != training.types.end(), "training has a type whose intensity can be reduced");
        const int type_index = int(type - training.types.begin());
        const auto exe = load_le(asset_path(data, "BMH.exe"));
        const auto manual = flow.click(exe.u16(::bmh::layout::Training_Click_RuntimeField) + type_index * exe.u8(::bmh::layout::Training_Click_RuntimeField8) + 1,
                                       exe.u8(::bmh::layout::Training_Click_RuntimeField3) + type_index * exe.u8(::bmh::layout::Training_Click_RuntimeField9) + 1);
        check(manual.request == FlowRequest::submenu_action &&
              std::get<TrainingRequest>(manual.submenu_action).kind == TrainingRequestKind::set_type,
              "training click uses the original ball hitbox");
        check(flow.click(125, 200).request == FlowRequest::none, "pending training blocks a duplicate change");
        serve(manual);
        check(session->state().manager_records[0xc5 + size_t(type_index)] == 1 &&
              !(session->state().manager_options[4] & 2), "App persists a manual plan and clears only its automatic type bit");
        serve(flow.click(125, 200));
        check(!(session->state().manager_options[4] & 1), "App switches global automatic training off");
        serve(flow.click(125, 200));
        check(session->state().manager_options[4] & 1, "App switches global automatic training on and recalculates enabled groups");
        check(session->game().calls() == training_game_calls && session->rand().calls() == training_watcom_calls,
              "first-open, manual training and automatic plan changes consume neither RNG stream");
        serve(flow.send_key("ESC"));
        check(flow.screen_name() == "desktop", "training returns to a usable desktop after ACK");

        serve(menu(18));
        check(flow.screen_name() == "tactics", "tactics opens through the real desktop");
        const auto board = session->state().tactics;
        const auto squad = session->state().squad_slots;
        const auto records = session->state().manager_records;
        const auto game_calls = session->game().calls(), watcom_calls = session->rand().calls();
        const auto old_revision = revision;
        const auto toggle = flow.click(180, 50);
        check(toggle.request == FlowRequest::submenu_action &&
              std::holds_alternative<TacticsRequest>(toggle.submenu_action), "tactic click emits a typed request");
        const auto request = std::get<TacticsRequest>(toggle.submenu_action);
        check(request.index == 5, "original hitbox selects the supported tactic");
        check(flow.click(180, 50).request == FlowRequest::none, "pending tactic cannot be submitted twice");
        serve(toggle);
        check(revision == old_revision + 1 && session->state().manager_records[0xd7] == request.desired_value,
              "App writes and acknowledges the requested tactic exactly once");
        auto expected_records = records;
        expected_records[0xd7] = uint8_t(request.desired_value);
        check(session->state().manager_records == expected_records && session->state().tactics == board &&
              session->state().squad_slots == squad && session->game().calls() == game_calls &&
              session->rand().calls() == watcom_calls, "tactic changes only its byte without board, squad or RNG effects");
        std::string error;
        check(!session->dispatch_tactics(request, revision, error) && revision == old_revision + 1,
              "replayed tactic is rejected before mutation");
        for (int variant = 0; variant < 5; ++variant) {
            auto invalid = request; invalid.revision = revision;
            if (variant == 0) ++invalid.manager_slot;
            if (variant == 1) ++invalid.runtime_club;
            if (variant == 2) invalid.desired_value = 256;
            if (variant == 3) invalid.desired_assistant_attitude = 1;
            if (variant == 4) { invalid.index = 0; invalid.desired_assistant_attitude.reset(); }
            const auto before = session->state().manager_records;
            const auto options = session->state().manager_options;
            check(!session->dispatch_tactics(invalid, revision, error) &&
                  session->state().manager_records == before && session->state().manager_options == options &&
                  session->game().calls() == game_calls && session->rand().calls() == watcom_calls,
                  "invalid tactic identity, range or assistant side effect leaves engine and RNG untouched");
        }
        const auto attitude = flow.click(80, 50);
        check(attitude.request == FlowRequest::submenu_action &&
              std::get<TacticsRequest>(attitude.submenu_action).index == 0, "attitude request has the original index");
        serve(attitude);
        check(session->state().manager_options[2] == 0 && session->state().tactics == board,
              "offensive/defensive choice disables the assistant without changing player coordinates");
        const auto tactics_native_before_escape = save_stream(session->state());
        const auto tactics_game_before_escape = session->game();
        const auto tactics_rand_before_escape = session->rand();
        const auto tactics_revision_before_escape = revision;
        serve(flow.send_key("ESC"));
        check(flow.screen_name() == "tactics" &&
              save_stream(session->state()) == tactics_native_before_escape &&
              session->game().seed() == tactics_game_before_escape.seed() &&
              session->game().calls() == tactics_game_before_escape.calls() &&
              session->rand().seed() == tactics_rand_before_escape.seed() &&
              session->rand().calls() == tactics_rand_before_escape.calls() &&
              revision == tactics_revision_before_escape,
              "Source tactics Escape preserves the menu, full native state, RNG and revision");
        serve(flow.click(310,230,true));
        check(flow.screen_name() == "desktop", "tactics closes after acknowledgement");

        {
            app::LineupActions common(*session);
            const auto native=save_stream(session->state());const auto g=session->game();const auto w=session->rand();
            const auto rev=revision;const auto before=session->lineup_checkpoint();
            auto observed=session->preparation_projection(revision).lineup;
            ++observed.revision;
            check(!common.lineup(app::LineupActions::prepare(observed),revision,error),
                  "shared lineup boundary rejects a stale observed preparation");
            bool acknowledged=false;
            try {
                --observed.revision;
                common.lineup(app::LineupActions::prepare(observed),revision,error,[&] {
                    acknowledged=session->preparation_projection(revision).lineup.entry_prepared;
                    session->state().date++;session->game().range(0,10);session->rand().rand();
                    throw std::runtime_error("lineup frontend ACK refused");
                });
            } catch(const std::runtime_error&) {}
            check(acknowledged && save_stream(session->state())==native && revision==rev &&
                session->game().seed()==g.seed() && session->game().calls()==g.calls() &&
                session->rand().seed()==w.seed() && session->rand().calls()==w.calls() &&
                session->lineup_checkpoint().manager==before.manager && session->lineup_checkpoint().club==before.club,
                "shared lineup rejection restores full Native, both RNGs, revision and prepared entry");
        }
        auto lineup_records = session->state().manager_records;
        // legacy routine initializes only the eight supported edition tail bytes on first entry.
        const size_t lineup_record = size_t(session->state().memory.u8(::bmh::layout::ActiveManager)) * 0xb0c;
        if (uint16_t(lineup_records.at(lineup_record+0xb0a) |
                     uint16_t(lineup_records.at(lineup_record+0xb0b))<<8) != 0x3253) {
            for (size_t off : {size_t(0xafc),size_t(0xafd),size_t(0xafe),size_t(0xaff),
                               size_t(0xb08),size_t(0xb09)})
                lineup_records.at(lineup_record+off)=0;
            lineup_records.at(lineup_record+0xb0a)=0x53;
            lineup_records.at(lineup_record+0xb0b)=0x32;
        }
        const auto lineup_revision = revision;
        serve(menu(3));
        check(flow.screen_name() == "lineup" && flow.state_json().find("\"pending_action\":null") != std::string::npos,
              "lineup request is acknowledged or rejected before input resumes");
        const auto lineup_ack_revision = revision;
        check(lineup_ack_revision == lineup_revision || lineup_ack_revision == lineup_revision + 1,
              "lineup entry changes revision only for accepted preparation");
        {
            app::LineupActions common(*session);
            const auto native=save_stream(session->state());const auto g=session->game();const auto w=session->rand();
            const auto rev=revision;auto observed=session->preparation_projection(revision);
            ++observed.lineup.revision;
            check(!common.pitch(app::LineupActions::page(observed),revision,error),
                  "shared pitch boundary rejects a stale observed page switch");
            bool acknowledged=false;
            try {
                --observed.lineup.revision;
                common.pitch(app::LineupActions::page(observed),revision,error,[&] {
                    acknowledged=true;session->state().date++;
                    session->game().range(0,10);session->rand().rand();
                    throw std::runtime_error("pitch frontend ACK refused");
                });
            } catch(const std::runtime_error&) {}
            check(acknowledged && save_stream(session->state())==native && revision==rev &&
                session->game().seed()==g.seed() && session->game().calls()==g.calls() &&
                session->rand().seed()==w.seed() && session->rand().calls()==w.calls(),
                "shared pitch rejection restores Native, both RNGs and revision after a real page switch");
        }
        // Real mouse requests, owned pending identity, and exact Core bytes.
        const auto projected = session->preparation_projection(revision).lineup;
        std::vector<int> fit_rows;
        for (int row = 0; row < 11; ++row)
            if (((projected.players.at(size_t(row)).attributes.w364 >> 16) & 0x1f) == 0)
                fit_rows.push_back(row);
        check(fit_rows.size() >= 2, "prepared new career has two fit starters");
        const int first_row = fit_rows[0], second_row = fit_rows[1];
        const auto row_y = [](int row) { return 33 + row * 7; };
        check(flow.click(20, row_y(first_row)).request == FlowRequest::none,
              "first name click selects a roster row without an engine write");
        const auto swap_event = flow.click(20, row_y(second_row));
        check(swap_event.request == FlowRequest::submenu_action &&
              std::get<LineupRequest>(swap_event.submenu_action).kind == LineupRequestKind::swap_players,
              "second name click emits a real pending swap");
        const auto swap_request = std::get<LineupRequest>(swap_event.submenu_action);
        const auto swap_before = save_stream(session->state());
        const auto swap_revision = revision;
        auto forged_swap = swap_event;
        ++std::get<LineupRequest>(forged_swap.submenu_action).first_player;
        serve(forged_swap);
        check(save_stream(session->state()) == swap_before && revision == swap_revision &&
              flow.matches_pending_lineup(swap_request), "forged player identity leaves the real swap pending");
        check(flow.click(20, row_y(first_row)).request == FlowRequest::none,
              "pending swap blocks duplicate mouse input");
        GameState swap_expected = session->state();
        const auto range = squad_range(projected.runtime_club);
        const size_t first = range.first + size_t(first_row), second = range.first + size_t(second_row);
        const uint16_t a_slot = swap_expected.squad_slots.at(first), b_slot = swap_expected.squad_slots.at(second);
        swap_expected.squad_slots[first] = uint16_t((b_slot & 0x1fff) | (a_slot & 0xc000));
        swap_expected.squad_slots[second] = uint16_t((a_slot & 0x1fff) | (b_slot & 0xc000));
        auto* a = swap_expected.tactics.data() + size_t(projected.manager_slot) * 0x840 + size_t(first_row) * 0x58;
        auto* b = swap_expected.tactics.data() + size_t(projected.manager_slot) * 0x840 + size_t(second_row) * 0x58;
        std::swap_ranges(a, a + 0x58, b);
        std::swap(a[0x41], b[0x41]); std::swap(a[0x42], b[0x42]);
        for (size_t row = 0; row < range.count; ++row) {
            const uint16_t slot = swap_expected.squad_slots[range.first + row];
            if (slot & 0xc000)
                swap_expected.player_cards.at(slot & 0x0fff) = uint16_t(
                    (swap_expected.player_cards.at(slot & 0x0fff) & 0xffe0) | row);
        }
        swap_expected.manager_options.at(size_t(projected.manager_slot) * 5) = 0;
        serve(swap_event);
        check(revision == swap_revision + 1 && save_stream(session->state()) == save_stream(swap_expected) &&
              !flow.matches_pending_lineup(swap_request) && session->game().calls() == game_calls &&
              session->rand().calls() == watcom_calls, "actual UI swap writes only Source-derived bytes and ACKs once");
        serve(swap_event);
        check(revision == swap_revision + 1 && save_stream(session->state()) == save_stream(swap_expected),
              "consumed UI swap replay is inert");
        // A projection can fail after the Core write (for example, another
        // calendar context arrived). Restore the edit and revision atomically.
        flow.click(20, row_y(first_row));
        const auto rollback_swap = flow.click(20, row_y(second_row));
        const auto normal_date = session->state().date;
        ++session->state().date;
        const auto rollback_before = save_stream(session->state());
        const auto rollback_revision = revision;
        serve(rollback_swap);
        check(save_stream(session->state()) == rollback_before && revision == rollback_revision &&
              !flow.matches_pending_lineup(std::get<LineupRequest>(rollback_swap.submenu_action)) &&
              session->game().calls() == game_calls && session->rand().calls() == watcom_calls,
              "failed production ACK rolls back the entire swap and revision without RNG");
        session->state().date = normal_date;
        const auto lineup_ack_state = save_stream(session->state());
        check(session->state().manager_records == lineup_records &&
              session->game().calls() == game_calls && session->rand().calls() == watcom_calls,
              "lineup preparation consumes no RNG and preserves all records outside Source supported edition initialization");
        const auto lineup_pixels=flow.render().pixels;
        const auto ignored_lineup_escape=flow.send_key("ESC");
        check(!ignored_lineup_escape.changed&&ignored_lineup_escape.request==FlowRequest::none&&
              flow.screen_name()=="lineup"&&flow.render().pixels==lineup_pixels&&
              save_stream(session->state())==lineup_ack_state&&revision==swap_revision+1&&
              session->game().calls()==game_calls&&session->rand().calls()==watcom_calls,
              "Source desktop lineup Escape preserves pixels, Native, revision and both RNGs");
        serve(flow.click(310,230,true));
        check(flow.screen_name() == "desktop" && revision == swap_revision + 1 &&
              save_stream(session->state()) == lineup_ack_state &&
              session->game().calls() == game_calls && session->rand().calls() == watcom_calls,
              "closing prepared lineup performs no additional writes or RNG draws");

        bool saw_results = false;
        const auto statistics_league = size_t(team_league(session->state().manager_teams.at(0)));
        // legacy routine disables A-D until two league rounds have been played.
        // Keep the mouse-up test in a real, Source-valid career state.
        for (int turn = 0; turn < 24 &&
             (!saw_results || session->state().league_rounds.at(statistics_league) < 2); ++turn) {
            const auto before_date = session->state().date;
            serve(menu(12));
            check(session->state().date > before_date, "real App kickoff advances one event");
            if (!session->completed_matchday_results()) {
                check(!flow.in_results_view() && flow.screen_name() == "desktop",
                      "calendar event without fixtures does not show a stale result modal");
                continue;
            }
            saw_results = true;
            const auto& results = *session->completed_matchday_results();
            check(!flow.in_results_view() && flow.screen_name() == "desktop" &&
                  results.date == session->last_turn_results().date && results.date < session->state().date,
                  "completed event stays queryable while Source returns directly to the next desk");
            const auto saved_native = save_stream(session->state());
            const auto saved_results = results.groups[0].matches[0];
            auto& ids = session->state().club_ids;
            const auto saved_ids = ids;
            std::reverse(ids.begin(), ids.end());
            check(session->completed_matchday_results()->groups[0].matches[0].home == saved_results.home &&
                  session->completed_matchday_results()->groups[0].matches[0].away == saved_results.away,
                  "query snapshot names remain owned without a result modal");
            ids = saved_ids;
            const auto event_date = session->state().date;
            const auto result_revision = revision;
            const auto result_game_calls = session->game().calls(), result_watcom_calls = session->rand().calls();
            const auto result_board = session->state().tactics;
            flow.render(); flow.render();
            check(!flow.in_results_view() && flow.screen_name() == "desktop" && revision == result_revision &&
                  session->state().date == event_date && session->state().tactics == result_board &&
                  save_stream(session->state()) == saved_native &&
                  session->game().calls() == result_game_calls && session->rand().calls() == result_watcom_calls,
                  "rendering the next desk preserves the finished match and requires no extra acknowledgement");
        }
        check(saw_results, "career completes a real matchday and returns to a usable desk");
        const auto serve_statistics = [&](const FlowResult& result) {
            const auto message=app::serve_flow_request(flow,result,session,data,data,seed,revision,
                playback,audio_session,false);
            if(!message.empty())throw std::runtime_error("statistics selector controller: "+message);
        };
        const auto statistics_menu = menu(23);
        serve_statistics(statistics_menu);
        check(flow.in_statistics_menu()&&flow.in_statistics_rankings()&&
              flow.screen_name()=="statistics_rankings"&&
              flow.statistics_rankings()->state().category==0&&!flow.desktop()->pending(),
              "normal menu 23 enters Source title history at local category zero");
        flow.pointer_down(12, 207);
        const auto opportunities_open = flow.click(12, 207);
        check(opportunities_open.request == FlowRequest::statistics_menu_action &&
              opportunities_open.statistics_menu_action.selected_index == 0,
              "only Original statistics entry zero routes into Möglichkeiten");
        serve_statistics(opportunities_open);
        check(flow.in_opportunities() && flow.screen_name() == "opportunities" && flow.in_statistics_menu(),
              "Möglichkeiten opens over the retained selector and can return to it");
        const auto mode_revision = revision;
        flow.pointer_down(144, 123);
        const auto mode_b = flow.click(144, 123);
        check(mode_b.request == FlowRequest::opportunities_action &&
              mode_b.opportunities_action.action == OpportunitiesAction::select_mode &&
              mode_b.opportunities_action.mode == 2,
              "the SDL mouse-up click path forwards the pending mode-B request");
        serve_statistics(mode_b);
        check(revision == mode_revision + 1 && flow.opportunities()->state().mode == 2 &&
              !flow.pending_opportunities(), "mode B is acknowledged before parent navigation");
        flow.pointer_down(120,232);
        const auto history_open=flow.click(120,232);
        check(history_open.request==FlowRequest::statistics_menu_action&&
              history_open.statistics_menu_action.selected_index==7,
              "global statistics entry seven can switch from A-D to title history");
        serve_statistics(history_open);
        check(!flow.in_opportunities()&&flow.in_statistics_rankings()&&
              flow.statistics_rankings()->state().category==0&&flow.screen_name()=="statistics_rankings",
              "title history opens at Original inner category zero");
        flow.pointer_down(34,170);
        const auto category=flow.click(34,170);
        check(category.request==FlowRequest::statistics_rankings_action&&
              category.statistics_rankings_action.category==1,
              "history category selection is a typed matching-release action");
        serve_statistics(category);
        check(flow.statistics_rankings()->state().category==1,
              "accepted history category redraws the selected title list");
        flow.pointer_down(120,207);
        const auto transfers=flow.click(120,207);
        check(transfers.request==FlowRequest::statistics_menu_action&&
              transfers.statistics_menu_action.selected_index==1,
              "global transfer-history entry is typed from the title-history child");
        serve_statistics(transfers);
        check(flow.in_transfer_history()&&!flow.in_statistics_rankings()&&
              flow.transfer_history()->projection()==transfer_history_from_game(
                  session->state(),flow.desktop()->state().manager_slot),
              "transfer history uses the accepted manager's saved rows");
        flow.pointer_down(245,232);
        const auto minutes_from_transfers=flow.click(245,232);
        check(minutes_from_transfers.request==FlowRequest::statistics_menu_action&&
              minutes_from_transfers.statistics_menu_action.selected_index==8,
              "global minute-statistics entry is typed from transfer history");
        serve_statistics(minutes_from_transfers);
        check(flow.in_minute_statistics()&&!flow.in_transfer_history(),
              "minute statistics replaces only the active child");
        flow.pointer_down(120,232);
        const auto history_from_minutes=flow.click(120,232);
        check(history_from_minutes.request==FlowRequest::statistics_menu_action&&
              history_from_minutes.statistics_menu_action.selected_index==7,
              "global title-history entry is reachable from minute statistics");
        serve_statistics(history_from_minutes);
        check(flow.in_statistics_rankings()&&!flow.in_minute_statistics(),
              "title history replaces minute statistics");
        flow.pointer_down(12,207);
        const auto back_to_opportunities=flow.click(12,207);
        check(back_to_opportunities.request==FlowRequest::statistics_menu_action&&
              back_to_opportunities.statistics_menu_action.selected_index==0,
              "global entry zero can switch from history to A-D");
        serve_statistics(back_to_opportunities);
        check(flow.in_opportunities()&&!flow.in_statistics_rankings(),
              "history-to-A-D transition replaces only the active child");
        flow.pointer_down(245,232);
        const auto minutes_from_opportunities=flow.click(245,232);
        check(minutes_from_opportunities.request==FlowRequest::statistics_menu_action&&
              minutes_from_opportunities.statistics_menu_action.selected_index==8,
              "global minute-statistics entry is reachable from A-D");
        serve_statistics(minutes_from_opportunities);
        check(!flow.in_opportunities()&&flow.in_minute_statistics(),
              "A-D closes before minute statistics opens");
        flow.pointer_down(120,207);
        const auto transfers_from_minutes=flow.click(120,207);
        check(transfers_from_minutes.request==FlowRequest::statistics_menu_action&&
              transfers_from_minutes.statistics_menu_action.selected_index==1,
              "global transfer-history entry is reachable from minute statistics");
        serve_statistics(transfers_from_minutes);
        check(flow.in_transfer_history()&&!flow.in_minute_statistics(),
              "transfer history replaces minute statistics");
        flow.pointer_down(12,207);
        const auto opportunities_from_transfers=flow.click(12,207);
        check(opportunities_from_transfers.request==FlowRequest::statistics_menu_action&&
              opportunities_from_transfers.statistics_menu_action.selected_index==0,
              "global A-D entry is reachable from transfer history");
        serve_statistics(opportunities_from_transfers);
        check(flow.in_opportunities()&&!flow.in_transfer_history(),
              "A-D replaces transfer history without losing its parent");

        // Exercise Analytics through the exact DOWN/UP route used by Main.
        // Its Session commit advances the parent revision; all other Source
        // selector children must remain reachable from that refreshed owner.
        flow.pointer_down(230,207);
        const auto analytics_open=flow.click(230,207);
        check(analytics_open.request==FlowRequest::statistics_menu_action&&
              analytics_open.statistics_menu_action.selected_index==2,
              "global Analytics index is typed from A-D");
        serve_statistics(analytics_open);
        check(flow.in_analytics()&&flow.in_statistics_menu(),
              "Analytics replaces A-D while retaining the statistics parent");
        const int analytics_manager=flow.analytics()->state().manager_slot;
        const uint32_t preference_address=::bmh::layout::FlagsUnsigned+uint32_t(analytics_manager)*3;
        const uint8_t preference_before=session->state().memory.u8(preference_address);
        const auto analytics_revision=revision;
        const auto analytics_down=flow.pointer_down(30,145);
        const auto analytics_up=flow.click(30,145);
        check(analytics_down.request==FlowRequest::analytics_action&&
              analytics_down.analytics_action.kind==AnalyticsRequestKind::toggle_metric&&
              analytics_up.request==FlowRequest::none,
              "Analytics metric commits on DOWN and ignores its orphan UP");
        serve_statistics(analytics_down);
        check(revision==analytics_revision+1&&
              session->state().memory.u8(preference_address)==uint8_t(preference_before^1U)&&
              flow.desktop()->state().revision==revision&&
              flow.statistics_menu()->state().owner.revision==revision&&
              flow.analytics()->state().owner.revision==revision,
              "typed Analytics XOR ACK refreshes desktop, child and parent revision together");

        flow.pointer_down(120,232);
        const auto history_after_xor=flow.click(120,232);
        check(history_after_xor.request==FlowRequest::statistics_menu_action&&
              history_after_xor.statistics_menu_action.selected_index==7,
              "refreshed Analytics owner can switch to title history");
        serve_statistics(history_after_xor);
        flow.pointer_down(120,207);
        const auto transfers_after_xor=flow.click(120,207);
        check(transfers_after_xor.request==FlowRequest::statistics_menu_action&&
              transfers_after_xor.statistics_menu_action.selected_index==1,
              "refreshed parent can switch from history to transfers");
        serve_statistics(transfers_after_xor);
        flow.pointer_down(245,232);
        const auto minutes_after_xor=flow.click(245,232);
        check(minutes_after_xor.request==FlowRequest::statistics_menu_action&&
              minutes_after_xor.statistics_menu_action.selected_index==8,
              "refreshed parent can switch to minute statistics");
        serve_statistics(minutes_after_xor);
        flow.pointer_down(12,207);
        const auto opportunities_after_xor=flow.click(12,207);
        check(opportunities_after_xor.request==FlowRequest::statistics_menu_action&&
              opportunities_after_xor.statistics_menu_action.selected_index==0,
              "refreshed parent can return to A-D");
        serve_statistics(opportunities_after_xor);
        flow.pointer_down(230,207);
        const auto analytics_again=flow.click(230,207);
        serve_statistics(analytics_again);
        const uint8_t preference_after_first=session->state().memory.u8(preference_address);
        const auto second_xor_revision=revision;
        const auto second_xor=flow.pointer_down(30,145);
        (void)flow.click(30,145);
        check(second_xor.request==FlowRequest::analytics_action,
              "Analytics accepts a later preference action after cross-child navigation");
        serve_statistics(second_xor);
        check(revision==second_xor_revision+1&&
              session->state().memory.u8(preference_address)==preference_before&&
              preference_after_first==uint8_t(preference_before^1U),
              "second XOR is separately acknowledged and restores the original metric byte");
        serve_statistics(flow.pointer_down(12,80,true));
        check(!flow.in_analytics()&&!flow.in_statistics_menu()&&flow.screen_name()=="desktop",
              "one right-click after Analytics ACK exits the whole statistics menu");
        check(!flow.in_opportunities()&&!flow.in_statistics_menu()&&flow.screen_name()=="desktop",
              "one Source right-close exits A-D and its parent selector");
        serve_statistics(menu(23));
        flow.pointer_down(12,207);
        const auto opportunities_reopen=flow.click(12,207);
        serve_statistics(opportunities_reopen);
        check(flow.in_opportunities(),"A-D can be reopened from a fresh statistics parent");
        serve_statistics(flow.pointer_down(12,80,true));
        check(!flow.in_opportunities()&&!flow.in_statistics_menu(),"right-close returns to desktop");
        serve_statistics(menu(23));
        flow.pointer_down(12,207);
        const auto reopened_opportunities = flow.click(12, 207);
        check(reopened_opportunities.request == FlowRequest::statistics_menu_action &&
              reopened_opportunities.statistics_menu_action.selected_index==0,
              "fresh parent can submit another A-D opening");
        serve_statistics(reopened_opportunities);
        check(flow.in_opportunities(), "second A-D opening succeeds after a prior close");
        serve_statistics(flow.pointer_down(12, 80, true));
        check(!flow.in_opportunities() && !flow.in_statistics_menu(), "second close also returns to desktop");

        // legacy routine requires two distinct right-button presses at the active
        // quickbar socket, with the first physical release between them.
        const auto shortcut_state = flow.desktop()->state_json();
        if (shortcut_state.find("\"shortcut_bar_visible\":false") != std::string::npos)
            flow.click(160,239); // Source compact-bar toggle; ensure afb86 is active.
        const auto socket_revision = revision;
        const auto socket_game_rng = session->game();
        const auto socket_week_rng = session->rand();
        const auto idle_socket = flow.render();
        const auto socket_down = flow.pointer_down(299,207,true);
        check(socket_down.request == FlowRequest::none && flow.desktop()->socket_confirmation_armed(),
              "first right press arms the Source socket without ending the session");
        const auto pressed_socket = flow.render();
        size_t socket_frame_changes=0;
        for(int y=207;y<227;++y)for(int x=299;x<319;++x)
            socket_frame_changes += idle_socket.pixels[size_t(y)*idle_socket.width+x] !=
                                    pressed_socket.pixels[size_t(y)*pressed_socket.width+x];
        check(socket_frame_changes!=0,"armed socket renders legacy routine's pressed sprite");
        (void)flow.key("~");
        check(flow.desktop()->socket_confirmation_armed(),
              "key input cannot pass the Source wait for the first RMB release");
        flow.click(299,207,true); // Matching first RMB release is consumed.
        check(flow.desktop()->socket_confirmation_armed(),
              "socket remains visibly armed after its first physical release");
        (void)flow.key("~");
        check(!flow.desktop()->socket_confirmation_armed(),
              "a key event after release cancels the pending socket confirmation");
        flow.pointer_down(299,207,true);
        flow.click(299,207,true);
        const auto cancel_down = flow.pointer_down(319,0,false);
        check(cancel_down.request == FlowRequest::none && !flow.desktop()->socket_confirmation_armed(),
              "a different click cancels the pending socket confirmation");
        flow.click(319,0,false);
        check(flow.desktop()->state().revision == socket_revision && revision == socket_revision &&
              session->game().seed() == socket_game_rng.seed() && session->game().calls() == socket_game_rng.calls() &&
              session->rand().seed() == socket_week_rng.seed() && session->rand().calls() == socket_week_rng.calls(),
              "cancelling the socket leaves career state, revision, and RNG streams unchanged");
        flow.pointer_down(299,207,true);
        flow.click(299,207,true);
        const auto confirmed_exit = flow.pointer_down(299,207,true);
        check(confirmed_exit.request == FlowRequest::end_session &&
              confirmed_exit.desk_action.kind == DeskRequestKind::end_session,
              "second right press at the socket emits the typed end-session request");
        check(flow.desktop()->state().revision == socket_revision && revision == socket_revision &&
              session->game().seed() == socket_game_rng.seed() && session->game().calls() == socket_game_rng.calls() &&
              session->rand().seed() == socket_week_rng.seed() && session->rand().calls() == socket_week_rng.calls(),
              "confirmed session exit does not advance the calendar or either RNG stream");
        std::puts("shared App controller: career, tactics and owned matchday results OK");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1;
    }
}
