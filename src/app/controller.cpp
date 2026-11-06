// SPDX-License-Identifier: GPL-3.0-only
// Implementation of the controller (controller.hpp): the request/acknowledge model between
// the UI flow, the controller and the session.
//   1. The SDL loop in main.cpp feeds input to ui::StartupFlow, which returns a FlowResult
//      with a FlowRequest (desk action, transfer, friendly match, load game, ...).
//   2. serve_flow_request() is the single entry for that request. It rejects requests whose
//      revision no longer matches (stale UI), or that are blocked by an open owner.
//   3. It forwards the request to the matching *Actions facade (desktop_actions.*,
//      window_actions.*), which runs it on the CareerSession inside a rollback transaction.
//   4. The result is acknowledged back to the flow (for example flow.acknowledge_*,
//      show_*), which then changes what is drawn; rendering alone never advances the engine.
//   5. A non-empty return string is an error message (main.cpp prints it); "career_complete"
//      ends the program. The shared revision counter grows only when state really changed.
// Presenters (dialogs, live match frames) are optional hooks; an empty hook means the
// headless default. The anonymous namespace holds friendly-match and national-friendly helpers.
#include "formats/badge_policy.hpp"
#include "formats/executable_layout.hpp"
#include "app/controller.hpp"
#include "app/debug_log.hpp"
#include "app/window_actions.hpp"
#include "app/desktop_actions.hpp"
#include "app/audio.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <limits>
#include <optional>

#include "game/save.hpp"
#include "game/reschedule.hpp"
#include "game/messages.hpp"
#include "game/highscore.hpp"
#include "game/schedule.hpp"
#include "ui/league_table.hpp"
#include "ui/desk_records.hpp"
#include "formats/textd.hpp"


namespace bmh::app {
namespace fs = std::filesystem;
namespace {
// True when the request that came back from the UI is exactly the friendly action the flow
// is still waiting for; a replayed or altered request fails this test.
bool same_pending_friendly_request(const ui::StartupFlow& flow,
                                   const ui::FriendlyMatchRequest& request) {
    const auto* pending = flow.pending_friendly_action();
    return pending && *pending == request;
}

// Maps a failed club friendly-match booking status to the German notice shown to the player.
// Called when the booking action reports that nothing was applied.
std::string friendly_status_message(FriendlyBookingStatus status) {
    switch (status) {
    case FriendlyBookingStatus::declined: return "Die Kosten wurden abgelehnt.";
    case FriendlyBookingStatus::opponent_declined: return "Der Gegner hat nicht zugestimmt.";
    case FriendlyBookingStatus::insufficient_funds: return "Das Geld reicht für dieses Spiel nicht aus.";
    case FriendlyBookingStatus::invalid_manager: return "Der zuständige Manager ist nicht mehr aktiv.";
    case FriendlyBookingStatus::invalid_opponent: return "Der gewählte Gegner ist nicht mehr verfügbar.";
    case FriendlyBookingStatus::invalid_date: return "Dieser Termin liegt außerhalb der Saison.";
    case FriendlyBookingStatus::date_unavailable: return "Dieser Termin ist inzwischen belegt.";
    case FriendlyBookingStatus::plan_full: return "Der Spielplan ist voll.";
    case FriendlyBookingStatus::stale_quote: return "Das Angebot ist veraltet. Bitte wähle den Gegner erneut.";
    case FriendlyBookingStatus::invalid_request: return "Die Angaben zum Spieltermin sind ungültig.";
    case FriendlyBookingStatus::booked: break;
    }
    return "Der Termin konnte nicht gebucht werden.";
}

// Builds the manager name, portrait, slot and club badge shown in the national friendly dialogs
// from the manager record and club table of the game state. Throws FormatError when the
// manager slot or its record is missing.
ui::NationalFriendlyIdentity national_friendly_identity(const GameState& game,int manager,int team) {
    if(manager<0||manager>=5||size_t(manager+1)*0xb0c>game.manager_records.size())
        throw FormatError("national friendly: missing Source manager identity");
    const size_t record=size_t(manager)*0xb0c;
    ui::NationalFriendlyIdentity out;
    for(size_t i=0;i<30&&game.manager_records[record+0xa8+i];++i)
        out.manager_name.push_back(char(game.manager_records[record+0xa8+i]));
    out.portrait=game.manager_records[record+0x221];
    out.manager_slot=manager;
    out.badge_fallback=original_badge_fallback(team);
    if(!out.badge_fallback&&team>=0&&size_t(team)*2+1<game.club_heap_78.size())
        out.badge_sprite=int(game.club_heap_78[size_t(team)*2]|
            uint16_t(game.club_heap_78[size_t(team)*2+1])<<8)&0x1ff;
    return out;
}

// Builds one question dialog (venue, cost or payer) of the friendly-match flow: the identity of
// the asking manager plus the body text. Reused by both the club and the national variant.
ui::TaxDialogProjection friendly_question(const GameState& game,int manager,uint16_t flags,std::string body) {
    const auto identity=national_friendly_identity(game,manager,game.manager_teams.at(size_t(manager)));
    ui::TaxDialogProjection p;
    p.kind=ui::TaxDialogKind::question;p.source_flags=flags;p.source_manager_flags=uint8_t(manager);
    p.compact_dialog_frame=game.memory.has(::bmh::layout::CompactDialogFrame)&&game.memory.u8(::bmh::layout::CompactDialogFrame)!=0;
    p.manager_name=identity.manager_name;p.portrait=identity.portrait;
    p.badge_fallback=identity.badge_fallback;p.badge_sprite=identity.badge_sprite;
    p.body=std::move(body);return p;
}

// Collects everything the club friendly-match calendar screen needs for the manager at the desk
// (leagues, calendar, planner count, venue question). Called when the opponent table is finished.
ui::FriendlyMatchState club_friendly_state(const CareerSession& session,const ui::DeskState& desk,
        const fs::path& data,uint64_t revision) {
    const auto& game=session.state();const int manager=desk.manager_slot;
    ui::FriendlyMatchState friendly;
    friendly.revision=revision;friendly.manager_slot=manager;
    friendly.runtime_team=game.manager_teams.at(size_t(manager));
    friendly.current_day=game.season_day();friendly.planner_count=game.memory.u8(::bmh::layout::FriendlyPlanCount);
    friendly.season_start_counter=game.season_start;friendly.mode_three=desk.options.mode==3;
    friendly.national_team=desk.national_team;
    friendly.manager_name=ui::original_to_utf8(desk.manager_name);
    friendly.leagues=ui::league_tables_from_game(game,manager);
    friendly.calendar=ui::calendar_from_game(game,load_le(ui::asset_path(data,"BMH.exe")));
    friendly.venue_question=friendly_question(game,manager,0x606,message_text(game,::bmh::layout::Controller_ClubFriendlyState_TextPointer));
    return friendly;
}

// Same as friendly_status_message, but with the wording for national teams as opponents.
std::string national_friendly_status_message(FriendlyBookingStatus status) {
    switch(status) {
    case FriendlyBookingStatus::declined:return "Die Kosten wurden abgelehnt.";
    case FriendlyBookingStatus::opponent_declined:return "Der Gegner hat nicht zugestimmt.";
    case FriendlyBookingStatus::insufficient_funds:return "Das Geld reicht für dieses Spiel nicht aus.";
    case FriendlyBookingStatus::invalid_manager:return "Der zuständige Manager ist nicht mehr aktiv.";
    case FriendlyBookingStatus::invalid_opponent:return "Die gewählte Nationalmannschaft ist nicht mehr verfügbar.";
    case FriendlyBookingStatus::invalid_date:return "Dieser Termin liegt außerhalb der Saison.";
    case FriendlyBookingStatus::date_unavailable:return "Dieser Termin ist inzwischen belegt.";
    case FriendlyBookingStatus::plan_full:return "Der Spielplan ist voll.";
    case FriendlyBookingStatus::stale_quote:return "Das Angebot ist veraltet. Bitte wähle den Gegner erneut.";
    case FriendlyBookingStatus::invalid_request:return "Die Angaben zum Spieltermin sind ungültig.";
    case FriendlyBookingStatus::booked:break;
    }
    return "Der Termin konnte nicht gebucht werden.";
}

// Reads the manager's transfer-history entry count from the manager record (at most 25).
// Throws FormatError for an invalid slot, a missing record or an out-of-range count.
int source_transfer_history_count(const GameState& game, int manager_slot) {
    if (manager_slot < 0 || manager_slot >= 4) throw FormatError("statistics: invalid manager slot");
    const size_t at=size_t(manager_slot)*0xb0c+0x21d;
    if(at>=game.manager_records.size())throw FormatError("statistics: missing manager transfer-history counter");
    const int count=game.manager_records[at];
    if(count>25)throw FormatError("statistics: Original transfer-history count exceeds 25");
    return count;
}
// Computes the vertical start of the transfer-history list from its entry count; a count of 0
// is treated like 1.
int source_transfer_history_origin(int count) {
    if(count<0||count>25)throw FormatError("statistics: invalid transfer-history count");
    const int source_count=count==0?1:count;
    return 0x0d+(0x1c-source_count)*3;
}

// Serves a friendly-match request from the UI (quote or booking, club or national team).
// It verifies the request is current, asks FriendlyActions for the quote or the booking and
// answers the flow with an acknowledgement or a rejection text. Silently returns when no desk
// is shown or the request is not the one the flow is waiting for.
void serve_friendly_action(ui::StartupFlow& flow, CareerSession& session,
                           const ui::FriendlyMatchRequest& request, uint64_t& revision,
                           const std::filesystem::path& data) {
    if (!flow.desktop() || !same_pending_friendly_request(flow, request)) return;
    auto& game = session.state();
    const auto& state = flow.desktop()->state();
    const int manager = request.manager_slot;
    // Identity and revision check before anything is booked: the request must carry the current
    // shared revision and the desk's revision, the manager slot of the desk, and that manager must
    // be the active manager in the game memory with a matching manager count. The club (runtime
    // team) and planner count in the request must equal the session's values and the desk's club.
    // Any mismatch means the UI built the request from an outdated screen, so it is rejected
    // instead of touching the session.
    const bool identity_ok = request.revision == revision && request.revision == state.revision &&
        manager == state.manager_slot && manager >= 0 && manager < 4 &&
        size_t(manager) < game.manager_teams.size() && game.memory.has(::bmh::layout::ActiveManager) &&
        game.memory.has(::bmh::layout::ManagerCount) && game.memory.has(::bmh::layout::FriendlyPlanCount) &&
        game.memory.u8(::bmh::layout::ActiveManager) == manager && game.memory.u8(::bmh::layout::ManagerCount) == game.manager_teams.size() &&
        request.runtime_team == game.manager_teams[size_t(manager)] &&
        request.runtime_team == state.runtime_club &&
        request.planner_count == game.memory.u8(::bmh::layout::FriendlyPlanCount);
    // Also rejected when the revision counter is already at its maximum and cannot advance.
    if (!identity_ok || revision == std::numeric_limits<uint64_t>::max()) {
        flow.reject_friendly_action("Der Spielstand hat sich geändert. Bitte öffne das Menü erneut.");
        return;
    }

    // National friendly booking (national-coach desk, team slot 0x165): every field of the request
    // is compared with the quote and owner the UI kept, so a stale quote or a changed date, cost or
    // team is refused. The date is re-checked, then book_national is called; on rejection the flow
    // gets a German notice and the screen stays usable.
    if(request.national_booking) {
        const auto& national=*request.national_booking;
        const auto expected=FriendlyActions::national_owner(request);
        if(request.kind!=ui::FriendlyRequestKind::book||!request.quote_id||
           request.runtime_team!=0x165||request.opponent<0x166||request.opponent>0x1a2||
           request.season_day<0||request.season_day>365||request.planner_count<0||
           request.planner_count>253||request.revision!=revision||
           national.owner!=expected||national.quote.owner!=expected||
           national.season_day!=request.season_day||national.quote.terms.manager!=manager||
           national.quote.terms.manager_team!=0x165||national.quote.terms.opponent!=request.opponent||
           national.quote.terms.planner_count!=request.planner_count||
           national.quote.terms.home_team!=request.home_team||national.quote.terms.away_team!=request.away_team||
           national.quote.terms.payer_manager!=request.payer_manager||
           national.quote.terms.opponent_manager!=request.opponent_manager||
           national.quote.terms.cost!=request.cost||national.quote.terms.manager_home!=request.manager_home||
           request.accept_cost!=national.accept_cost||request.accept_opponent!=national.accept_opponent) {
            flow.reject_friendly_action("Die Angaben zum nationalen Freundschaftsspiel sind veraltet.");
            return;
        }
        const auto date_status=FriendlyActions(session).national_date_status(national.quote,national.owner,uint16_t(request.season_day));
        if(date_status!=FriendlyDateStatus::available&&
           date_status!=FriendlyDateStatus::available_with_calendar_event) {
            flow.reject_friendly_action(date_status==FriendlyDateStatus::plan_full
                ?"Der Spielplan ist voll.":date_status==FriendlyDateStatus::current_or_past
                ?"Dieser Termin ist nicht mehr verfügbar.":"Dieser Termin ist inzwischen belegt.");
            return;
        }
        try {
            // Acknowledgement callback of the booking action: projects desk and preparation at the new
            // revision and gives them to the flow. A mismatch with the active screen throws, which the
            // catch below turns into a rejection.
            const auto applied=FriendlyActions(session).book_national(request,revision,[&](const ui::FriendlyBookingAck& ack) {
                auto desk=session.desktop_projection(ack.revision);
                auto preparation=session.preparation_projection(ack.revision);
                if(!flow.acknowledge_friendly_booking(ack,std::move(desk),std::move(preparation)))
                    throw FormatError("national friendly booking ACK did not match the active screen");
            });
            if(!applied.applied)
                flow.reject_friendly_action(applied.planner_mismatch ? "Der Spielplan konnte nicht übernommen werden." :
                    national_friendly_status_message(applied.result.status));
        } catch(...) {
            flow.reject_friendly_action("Der Termin konnte nicht übernommen werden.");
        }
        return;
    }

    // Quote request (club friendly): asks for the opponent's terms, then builds the cost and payer
    // questions and a 366-day calendar marking which days are free, flagged or the current day.
    // If no quote is possible, or the flow no longer accepts it, the request is rejected.
    if (request.kind == ui::FriendlyRequestKind::quote) {
        const auto quote = FriendlyActions(session).quote(manager, request.opponent, request.manager_home);
        if (quote.status != FriendlyQuoteStatus::quoted || quote.manager_team != request.runtime_team) {
            flow.reject_friendly_action("Für diese Mannschaft kann kein Freundschaftsspiel angeboten werden.");
            return;
        }
        ui::FriendlyQuoteProjection projection;
        projection.request_id = request.request_id;
        projection.revision = request.revision;
        projection.manager_slot = manager;
        projection.runtime_team = quote.manager_team;
        projection.opponent = quote.opponent;
        projection.opponent_manager = quote.opponent_manager;
        projection.payer_manager = quote.payer_manager;
        projection.home_team = quote.home_team;
        projection.away_team = quote.away_team;
        projection.cost = quote.cost;
        projection.planner_count = quote.planner_count;
        projection.manager_home = quote.manager_home;
        projection.cost_question=friendly_question(game,manager,0x404,
            message_text(game,::bmh::layout::Controller_FormatError_TextPointer2)+format_number(game,quote.cost,0x101));
        const int other=quote.opponent_manager>=0?quote.opponent_manager:quote.payer_manager;
        if(other>=0&&other!=manager) {
            auto body=message_text(game,::bmh::layout::Controller_FormatError_TextPointer3);
            if(!quote.manager_home) {
                const auto exe=load_le(ui::asset_path(data,"BMH.exe"));
                body+=exe.cstring(::bmh::layout::NationalFriendlyPayerPrefixText)+format_number(game,quote.cost,0x101)+exe.cstring(::bmh::layout::Controller_FormatError_ExecutableField);
            }
            projection.owner_question=friendly_question(game,other,0x303,std::move(body));
        }
        projection.dates.reserve(366);
        for (int day = 0; day <= 365; ++day) {
            const auto status = FriendlyActions(session).date_status(quote, uint16_t(day));
            ui::FriendlyDateMark mark = ui::FriendlyDateMark::unavailable;
            if (status == FriendlyDateStatus::available) mark = ui::FriendlyDateMark::available;
            else if (status == FriendlyDateStatus::available_with_calendar_event)
                mark = ui::FriendlyDateMark::available_flagged;
            else if (status == FriendlyDateStatus::current_or_past && day == game.season_day())
                mark = ui::FriendlyDateMark::current_day;
            projection.dates.push_back({day, mark});
        }
        if (!flow.acknowledge_friendly_quote(std::move(projection)))
            flow.reject_friendly_action("Das Angebot ist nicht mehr aktuell. Bitte versuche es erneut.");
        return;
    }

    // Club friendly booking: the request is first range-checked (quote id, day, opponent, cost,
    // planner count); invalid values are rejected, otherwise book_club is called and its status is
    // mapped to a German notice if it was not applied.
    if (request.kind != ui::FriendlyRequestKind::book || request.quote_id == 0 ||
        request.season_day < 0 || request.season_day > 365 || request.opponent < 0 ||
        request.opponent >= 168 || request.cost < 0 || request.planner_count > 253) {
        flow.reject_friendly_action("Die Angaben zum Spieltermin sind ungültig.");
        return;
    }
    try {
        // Acknowledgement callback of the booking action (see the national variant above).
        const auto applied=FriendlyActions(session).book_club(request,revision,[&](const ui::FriendlyBookingAck& ack) {
            auto desk=session.desktop_projection(ack.revision);
            auto preparation=session.preparation_projection(ack.revision);
            if(!flow.acknowledge_friendly_booking(ack,std::move(desk),std::move(preparation)))
                throw FormatError("friendly booking ACK did not match the active screen");
        });
        if(!applied.applied)
            flow.reject_friendly_action(applied.planner_mismatch ? "Der Spielplan konnte nicht übernommen werden." :
                friendly_status_message(applied.result.status));
    } catch(const std::exception&) {
        flow.reject_friendly_action("Der Termin konnte nicht übernommen werden.");
    }
}

// Checks that the national friendly screens (picker, statistics menu, desk) still belong to one
// owner: all carry the current revision and the same manager, that manager is on team slot
// 0x165 and is the active manager. Returns true when the family is not active at all.
bool valid_national_friendly_family(const ui::StartupFlow& flow,const GameState& game,uint64_t revision) {
    if(!flow.national_friendly_family_active())return true;
    const auto* owner=flow.national_friendly_owner();
    const auto* origin=flow.national_friendly_desk_owner();
    const auto* menu=flow.statistics_menu();
    const auto* desk=flow.desktop();
    if(!owner||!origin||!menu||!desk||owner->desk_menu!=6||owner->runtime_team!=0x165||
       owner->manager<0||owner->revision!=revision||origin->kind!=ui::DeskRequestKind::open_menu||
       origin->menu_kind!=6||origin->manager_slot!=owner->manager||origin->revision!=revision||
       desk->state().revision!=revision||desk->state().manager_slot!=owner->manager||
       menu->state().owner.kind!=ui::DeskRequestKind::open_menu||menu->state().owner.menu_kind!=23||
       menu->state().owner.manager_slot!=owner->manager||menu->state().owner.revision!=revision||
       size_t(owner->manager)>=game.manager_teams.size()||game.manager_teams[size_t(owner->manager)]!=0x165||
       !game.memory.has(::bmh::layout::ActiveManager)||int(game.memory.u8(::bmh::layout::ActiveManager))!=owner->manager)return false;
    return true;
}

// Runs the national friendly opponent dialogs after a nation was picked: venue question, quote,
// cost and payer questions (answered through the presenter hook), then opens the calendar.
// Any failed check or declined question closes the picker without booking anything.
void serve_national_friendly_selection(ui::StartupFlow& flow,CareerSession& session,
        int selected_team,uint64_t& revision,const std::filesystem::path& data,
        const NationalFriendlyQuestionPresenter& presenter) {
    const auto* owner=flow.national_friendly_owner();
    const auto* desk_request=flow.national_friendly_desk_owner();
    if(!owner||!desk_request||!flow.desktop()||!session.state().memory.has(::bmh::layout::ActiveManager)||
       !valid_national_friendly_family(flow,session.state(),revision))return;
    const auto& game=session.state();
    const auto& desk=flow.desktop()->state();
    const int manager=owner->manager;
    // Identity check: owner and desk request must be the national-coach friendly menu at the current
    // revision for the same manager, who must still be on team slot 0x165 and be the active manager.
    if(owner->desk_menu!=6||owner->runtime_team!=0x165||owner->revision!=revision||
       desk_request->kind!=ui::DeskRequestKind::open_menu||desk_request->menu_kind!=6||
       desk_request->revision!=revision||desk_request->manager_slot!=manager||
       desk.revision!=revision||desk.manager_slot!=manager||manager<0||
       size_t(manager)>=game.manager_teams.size()||game.manager_teams[size_t(manager)]!=0x165||
       int(game.memory.u8(::bmh::layout::ActiveManager))!=manager||selected_team<0x166||selected_team>0x1a2) {
        flow.close_national_friendly_picker();
        return;
    }
    if(!presenter) { flow.close_national_friendly_picker(); return; }
    ui::NationalFriendlyUi dialog_flow(*owner,national_friendly_identity(game,manager,0x165));
    auto venue=dialog_flow.select_opponent(game,selected_team);
    if(venue.kind!=ui::NationalFriendlyUiActionKind::show_question||!venue.question) {
        flow.close_national_friendly_picker();return;
    }
    const auto backdrop=flow.render();
    auto answer=presenter(game,*venue.question,backdrop);
    if(!answer) { flow.close_national_friendly_picker();return; }
    const auto quote_request=dialog_flow.answer_venue(*answer);
    if(quote_request.kind!=ui::NationalFriendlyUiActionKind::quote||!quote_request.engine_owner) {
        flow.close_national_friendly_picker();return;
    }
    const auto quote=FriendlyActions(session).national_quote(*quote_request.engine_owner,selected_team,quote_request.manager_home);
    if(quote.terms.status!=FriendlyQuoteStatus::quoted) { flow.close_national_friendly_picker();return; }
    if(quote.terms.opponent_manager>=0)
        dialog_flow.set_opponent_manager_identity(
            national_friendly_identity(game,quote.terms.opponent_manager,selected_team));
    auto action=dialog_flow.accept_quote(game,quote);
    while(action.kind==ui::NationalFriendlyUiActionKind::show_question&&action.question) {
        answer=presenter(game,*action.question,backdrop);
        if(!answer) { flow.close_national_friendly_picker();return; }
        if(dialog_flow.stage()==ui::NationalFriendlyUiStage::cost_question)
            action=dialog_flow.answer_cost(game,*answer);
        else if(dialog_flow.stage()==ui::NationalFriendlyUiStage::payer_question)
            action=dialog_flow.answer_payer(*answer);
        else { flow.close_national_friendly_picker();return; }
    }
    const auto booking=dialog_flow.booking_template();
    if(action.kind!=ui::NationalFriendlyUiActionKind::open_calendar||!booking) {
        flow.close_national_friendly_picker();return;
    }
    ui::FriendlyQuoteProjection projection;
    projection.request_id=quote_request.engine_owner->request_id;
    projection.revision=revision;projection.manager_slot=manager;projection.runtime_team=0x165;
    projection.opponent=selected_team;projection.opponent_manager=quote.terms.opponent_manager;
    projection.payer_manager=quote.terms.payer_manager;projection.home_team=quote.terms.home_team;
    projection.away_team=quote.terms.away_team;projection.cost=quote.terms.cost;
    projection.planner_count=quote.terms.planner_count;projection.manager_home=quote.terms.manager_home;
    projection.dates.reserve(366);
    for(int day=0;day<=365;++day) {
        const auto status=FriendlyActions(session).national_date_status(quote,*quote_request.engine_owner,uint16_t(day));
        ui::FriendlyDateMark mark=ui::FriendlyDateMark::unavailable;
        if(status==FriendlyDateStatus::available)mark=ui::FriendlyDateMark::available;
        else if(status==FriendlyDateStatus::available_with_calendar_event)mark=ui::FriendlyDateMark::available_flagged;
        else if(status==FriendlyDateStatus::current_or_past&&day==game.season_day())mark=ui::FriendlyDateMark::current_day;
        projection.dates.push_back({day,mark});
    }
    ui::FriendlyMatchState calendar;
    calendar.revision=revision;calendar.manager_slot=manager;calendar.runtime_team=0x165;
    calendar.current_day=game.season_day();calendar.planner_count=quote.terms.planner_count;
    calendar.season_start_counter=game.season_start;calendar.mode_three=desk.options.mode==3;
    calendar.national_team=true;calendar.manager_name=ui::original_to_utf8(desk.manager_name);
    calendar.leagues=ui::league_tables_from_game(game,manager);
    calendar.calendar=ui::calendar_from_game(game,load_le(ui::asset_path(data,"BMH.exe")));
    try { flow.show_national_friendly_calendar(std::move(calendar),std::move(projection),*booking); }
    catch(...) { flow.close_national_friendly_picker(); }
    (void)data;
}

// Applies the original sound options and resumes the desk song; called after load, new career
// and every turn.
void start_original_desk_audio(openbmh::audio::Playback& playback, const GameState& state, bool enabled) {
    playback.set_original_options(original_audio_options(state, enabled));
    original_audio_command(playback, 1); // SavePathInDirectory / CareerFirstSteps2, resume the same song.
}

// True when the path has the expected extension and lies directly in the given save
// directory; keeps UI-supplied file names inside the save folder.
bool save_path_in_directory(const fs::path& path, const fs::path& directory,
                            std::string_view expected_extension = ".fbm") {
    if (path.filename().empty()) return false;
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (ext != expected_extension) return false;
    std::error_code ec;
    const auto expected = fs::weakly_canonical(directory, ec);
    if (ec) return false;
    const auto actual = fs::weakly_canonical(path.parent_path(), ec);
    return !ec && expected == actual;
}

// Maps a transfer action status to the German notice shown after a transfer market action.
std::string transfer_status_message(TransferActionStatus status, ui::TransferMarketRequestKind kind) {
    switch (status) {
    case TransferActionStatus::accepted:
        return kind == ui::TransferMarketRequestKind::list_player
            ? "Der Spieler steht jetzt auf der Transferliste." : "Der Spieler hat das Angebot angenommen.";
    case TransferActionStatus::pending: return kind == ui::TransferMarketRequestKind::list_player
        ? "Der Spieler steht bereits auf der Transferliste." : "Der Spieler entscheidet sich nächste Woche.";
    case TransferActionStatus::offer_too_low: return "Das Angebot ist zu niedrig.";
    case TransferActionStatus::blocked_by_board: return kind == ui::TransferMarketRequestKind::list_player
        ? "Transferliste voll oder Spieler nicht verfügbar." : "Der Vorstand genehmigt diesen Transfer nicht.";
    case TransferActionStatus::insufficient_funds: return "Das Geld reicht für diesen Transfer nicht aus.";
    case TransferActionStatus::no_squad_room: return "Im Kader ist kein Platz für diesen Spieler.";
    case TransferActionStatus::rejected: return "Der Spieler hat das Angebot abgelehnt.";
    default: return "Diese Transferaktion ist nicht mehr verfügbar.";
    }
}

// Serves a transfer market request from the UI (list, bid, review offers, poach, offer dialog,
// preview). Dispatches by request kind to TransferActions and acknowledges or rejects through
// the flow.
void serve_transfer_action(ui::StartupFlow& flow, CareerSession& session,
                           const ui::TransferMarketRequest& request, uint64_t& revision,
                           const HumanBuyerSaleHooks& presenters,
                           HumanBuyerMarketEntryPolicy entry_policy,
                           const CareerSession::PoachPlayerSelector& poach_selector, const PoachNoticePresenter& poach_notice,
    const TransferOfferPresenter& review) {
    const auto* pending = flow.pending_transfer_market();
    // JSON covers every offer field except its redundant nested manager identity.
    // Check that identity too, before an engine helper can consume a random draw.
    if (!pending || request.bid.manager != pending->bid.manager ||
        ui::transfer_market_request_json(request) != ui::transfer_market_request_json(*pending)) return;
    // The action facade checks the request against the current revision and entry policy; a
    // non-empty text means it is refused and shown to the player.
    const auto rejection = TransferActions(session).rejection(request, revision, entry_policy);
    if (!rejection.empty()) {
        flow.reject_preparation_action(rejection);
        return;
    }
    // Poach request: opens the original player selector through the session; the callback either
    // shows the offer editor or presents the notice, and a failure is rejected with a message.
    if(request.kind==ui::TransferMarketRequestKind::open_poach) {
        if(entry_policy!=HumanBuyerMarketEntryPolicy::normal_interactive) {
            flow.reject_preparation_action("Die Original-Spielerauswahl ist in diesem Einstieg nicht verfügbar.");
            return;
        }
        std::string error;
        const auto backdrop=flow.render();
        const bool accepted=session.dispatch_poach_ui(request,backdrop,poach_selector,
            // Callback of the poach dispatch: shows the offer editor, or reports the start messages
            // (through the notice presenter if set, otherwise as plain text).
            [&](const CareerSession::PoachDispatch& result,ui::PreparationState preparation) {
                if(result.editor) {
                    flow.show_transfer_offer(result.editor->preparation,result.editor->editor);
                } else {
                    if(poach_notice&&!result.start.messages.empty())poach_notice(session.state(),result.start,backdrop);
                    std::string notice;
                    for(const auto& message:result.start.messages) {
                        if(poach_notice)break;
                        if(!notice.empty())notice+='\n';
                        notice+=ui::original_to_utf8(message);
                    }
                    if(preparation.desktop.revision==request.revision)
                        flow.reject_preparation_action(std::move(notice));
                    else flow.acknowledge_preparation(std::move(preparation),std::move(notice));
                }
            },revision,error);
        if(!accepted) {
            std::fprintf(stderr,"poach: %s\n",error.c_str());
            flow.reject_preparation_action("Das Abwerben konnte nicht abgeschlossen werden.");
        }
        return;
    }
    // Open-offer request: opens the offer dialog; only allowed in the normal interactive entry.
    if (request.kind == ui::TransferMarketRequestKind::open_offer) {
        if (entry_policy != HumanBuyerMarketEntryPolicy::normal_interactive) {
            flow.reject_preparation_action("Der Original-Angebotsdialog ist in diesem Einstieg nicht verfügbar.");
            return;
        }
        try {
            std::string error;
            auto result = TransferActions(session).open_offer(request, revision, error,
                [&](CareerSession::TransferOfferEditorDispatch& opened) {
                    flow.show_transfer_offer(std::move(opened.preparation), std::move(opened.editor));
                });
            if (!result) throw FormatError(error);
        } catch (const std::exception&) {
            flow.reject_preparation_action("Dieser Angebotsdialog konnte nicht geöffnet werden.");
        }
        return;
    }
    // Preview request: asks for a quote and acknowledges it without changing the market.
    if (request.kind == ui::TransferMarketRequestKind::preview_quote) {
        if (entry_policy != HumanBuyerMarketEntryPolicy::normal_interactive) {
            flow.reject_preparation_action("Die Original-Transfervorschau ist in diesem Einstieg nicht verfügbar.");
            return;
        }
        try {
            std::string error;
            auto result = TransferActions(session).preview(request, revision, error,
                [&](CareerSession::TransferMarketPreviewDispatch& preview) {
                    flow.acknowledge_transfer_market_preview(std::move(preview.preparation), preview.quote);
                });
            if (!result) throw FormatError(error);
        } catch (const std::exception&) {
            flow.reject_preparation_action("Die Transfervorschau konnte nicht übernommen werden.");
        }
        return;
    }
    // All other kinds (bid, list, review offers): run through TransferActions::market with the
    // sale hooks. The notice presenter is wrapped to remember whether a notice was shown, so the
    // callback can keep the original notice/acknowledge order.
    try {
        auto hooks = presenters;
        bool source_notice_presented = false;
        if (hooks.notice) {
            const auto present = hooks.notice;
            // Wrapper that forwards to the real notice presenter and records that it ran.
            hooks.notice = [present, &source_notice_presented](const GameState& staged, const HumanBuyerSaleNotice& notice) {
                present(staged, notice);
                source_notice_presented = true;
            };
        }
        std::string error;
        const auto outcome = TransferActions(session).market(request, hooks, revision, error, entry_policy,
            // Callback after the market action: picks the notice, then acknowledges the new preparation
            // state if something changed, or rejects/acknowledges unchanged otherwise.
            [&](const TransferActions::MarketDispatch& applied) {
                // Preserve the Source caller's existing notice/ACK sequence.
                const bool source_finished = request.kind != ui::TransferMarketRequestKind::review_offers &&
                    entry_policy == HumanBuyerMarketEntryPolicy::normal_interactive &&
                    (source_notice_presented || (request.kind == ui::TransferMarketRequestKind::submit_bid &&
                        applied.action.status == TransferActionStatus::pending));
                auto notice = source_finished ? std::string{} : transfer_status_message(applied.action.status, request.kind);
                if (request.kind == ui::TransferMarketRequestKind::review_offers) {
                    notice = applied.action.status == TransferActionStatus::accepted
                        ? "Das Angebot wurde angenommen."
                        : applied.action.status == TransferActionStatus::pending
                            ? "Spieler bleibt auf der Transferliste."
                            : "Angebot abgelehnt; Transferliste unverändert.";
                }
                if (!applied.changed && source_finished) flow.acknowledge_unchanged_transfer_market();
                else if (!applied.changed) flow.reject_preparation_action(std::move(notice));
                else flow.acknowledge_preparation(session.preparation_projection(revision), std::move(notice));
            }, review);
        if (!outcome) throw FormatError(error);
    } catch (const std::exception&) {
        flow.reject_preparation_action("Diese Transferaktion konnte nicht übernommen werden.");
    }
}

// Serves an outgoing transfer offer request from the UI. Ignored unless it equals the pending
// offer; refused outside the normal interactive entry; otherwise runs TransferActions::offer.
void serve_transfer_offer(ui::StartupFlow& flow, CareerSession& session,
    const ui::OutgoingTransferRequest& request, uint64_t& revision,
    const HumanBuyerSaleHooks& presenters, HumanBuyerMarketEntryPolicy policy,
    const TransferOfferTeamInfoPresenter& team_info_presenter,
    const TransferOfferPlayerInfoPresenter& player_info_presenter) {
    const auto* pending = flow.pending_transfer_offer();
    if (!pending || ui::outgoing_transfer_request_json(*pending) != ui::outgoing_transfer_request_json(request)) return;
    if (policy != HumanBuyerMarketEntryPolicy::normal_interactive) {
        flow.reject_transfer_offer("Der Original-Angebotsdialog ist in diesem Einstieg nicht verfügbar.");
        return;
    }
    try {
        std::string error;
        auto result = TransferActions(session).offer(request, presenters, revision, error,
            // Callback after the offer action: gives the updated preparation and editor to the flow.
            team_info_presenter, player_info_presenter, [&](CareerSession::TransferOfferEditorDispatch& applied) {
                flow.acknowledge_transfer_offer(std::move(applied.preparation), std::move(applied.editor));
            });
        if (!result) throw FormatError(error);
    } catch (const std::exception&) {
        flow.reject_transfer_offer("Diese Angebotsaktion konnte nicht übernommen werden.");
    }
}

} // namespace

// Main entry: handles one FlowResult. Each branch below serves one request kind: it checks that
// the request is still the pending one and current, calls the session (through an Actions
// facade) and then acknowledges or rejects through the flow. A non-empty return is an error
// text for the console. The long parameter list is the optional presenter hooks.
// Hands the flow's engine requests to the career session. Returns a message for the
// console when something went wrong.
std::string serve_flow_request(ui::StartupFlow& flow, const ui::FlowResult& result, std::unique_ptr<app::CareerSession>& session,
                  const fs::path& data, const fs::path& save_directory, uint32_t seed, uint64_t& revision,
                  openbmh::audio::Playback& playback, std::optional<uint64_t>& audio_session, bool music_enabled,
                  std::function<void(int, const std::vector<MatchGroup>&, const GameState&)> live_presenter,
                  TransferOfferPresenter transfer_presenter,
                  TransferDecisionPresenter transfer_decision_presenter, SeasonPresenter season_presenter,
                  SeasonTaxHooks tax_presenter, SeasonHighscoreHook highscore_presenter,
                  SeasonPromotionHook promotion_presenter, SeasonCupDrawHook cup_draw_presenter,
                  PendingOfferHooks pending_offer_presenter, PendingOfferEntryPolicy pending_entry_policy,
                  HumanBuyerSaleHooks market_sale_presenter, HumanBuyerMarketEntryPolicy market_entry_policy,
                  TransferOfferTeamInfoPresenter team_info_presenter,
                  TransferOfferPlayerInfoPresenter player_info_presenter,
                  ConstructionNoticeHook construction_notice_presenter,
                  SeasonTicketCompensationNoticeHook compensation_notice_presenter,
                  SportschauPresentationHooks sportschau_presenter,
                  AdvertisingRenewalPresenter advertising_renewal_presenter,
                  CareerSession::PoachPlayerSelector poach_player_selector,
                  PoachNoticePresenter poach_notice_presenter,
                  ContractExpiryPresenter contract_expiry_presenter,
                  NationalFriendlyQuestionPresenter national_friendly_question_presenter,
                  LoanReturnPresenter loan_return_presenter,
                  LoanReturnOfferPresenter loan_return_offer_presenter,
                  LiveInterventionPresenter live_intervention_presenter,
                  EnvironmentNoticePresenter environment_notice_presenter,
                  LiveMatchEventPresenter live_match_event_presenter,
                  LiveBallPresenter live_ball_presenter,
                  LiveChancePresenter live_chance_presenter,
                  LiveShotPresenter live_shot_presenter,
                  LiveSetPiecePresenter live_set_piece_presenter,
                  LiveBookingPresenter live_booking_presenter,
                  LiveShootoutPresenter live_shootout_presenter,
                  LiveRealityPresenter live_reality_presenter,
                  PhaseResultPresenter phase_result_presenter, bool enable_autosave) {
    std::string error;
    // Autosave is configured on the existing session before any request is served.
    if (enable_autosave && session) session->configure_autosave_directory(save_directory);
    // While the opportunities owner is open in the session only its own screens (and the
    // statistics menu and pages above it) may send requests; anything else is blocked.
    if(session&&session->opportunities_active()&&result.request!=ui::FlowRequest::none&&
       result.request!=ui::FlowRequest::opportunities_action&&
       result.request!=ui::FlowRequest::statistics_menu_action&&
       result.request!=ui::FlowRequest::statistics_page_action)
        return "opportunities: another session action is blocked while the Source owner is open";
    // Desk menu 5 (other games): the request must still be the desk's pending action at the current
    // revision, otherwise it is dropped silently. OtherGamesActions opens the page; on rejection the
    // desk action is rejected with the error text.
    if(result.request==ui::FlowRequest::desk_action&&session&&
       result.desk_action.kind==ui::DeskRequestKind::open_menu&&result.desk_action.menu_kind==5) {
        if(!flow.desktop()||!flow.desktop()->pending()||*flow.desktop()->pending()!=result.desk_action||
           result.desk_action.revision!=revision)return {};
        try {
            const auto underlay=flow.desktop()->render();
            auto page=OtherGamesActions(*session).open(result.desk_action,revision,underlay,error,
                [&](ui::OtherGamesState& opened) {
                    // Callback with the projected page. Identity check: manager slot, runtime club and day counter
                    // must equal the desk on screen and the club identity must equal the one stored for that manager
                    // on the desk; otherwise it throws and the page is not shown.
                    const auto& desk=flow.desktop()->state();
                    if(opened.identity.manager_slot!=desk.manager_slot||
                       opened.identity.runtime_team!=desk.runtime_club||
                       opened.identity.day_counter!=desk.day_counter||desk.manager_slot<0||
                       size_t(desk.manager_slot)>=desk.managers.size()||
                       // The session identifies the national-coach desk by the home club
                       // (stable team behind the desk), not by the desk's own runtime club.
                       opened.identity.club_identity!=desk.managers[size_t(desk.manager_slot)].club_identity_key)
                        throw FormatError("other games: stale desktop identity");
                    flow.show_other_games(std::move(opened),session->preparation_projection(revision),result.desk_action);
            });
            if(!page)flow.reject_desk_action(error);
        } catch(const std::exception& e) {
            if(flow.desktop()&&flow.desktop()->pending())flow.reject_desk_action(e.what());
            return e.what();
        }
        return error;
    }
    // Other-games page action: only the flow's pending action is accepted; the session dispatches it
    // and the page is either closed or updated. Rejections go back to the page with the error.
    if(result.request==ui::FlowRequest::other_games_action&&session) {
        const auto* pending=flow.pending_other_games_action();
        if(!pending||*pending!=result.other_games_action)return {};
        try {
            auto applied=OtherGamesActions(*session).dispatch(result.other_games_action,revision,error,team_info_presenter,
                [&](CareerSession::OtherGamesDispatch& action) {
                    if(action.closed)flow.close_other_games(std::move(action.page),std::move(action.preparation));
                    else flow.acknowledge_other_games(std::move(action.page),std::move(action.preparation));
                });
            if(!applied) {
                flow.reject_other_games(error);
            }
        } catch(const std::exception& e) {
            if(flow.pending_other_games_action())flow.reject_other_games(e.what());
            return e.what();
        }
        return error;
    }
    // While the other-games owner is open in the session, every other request is blocked.
    if(session&&session->other_games_active()&&result.request!=ui::FlowRequest::none)
        return "other games: another session action is blocked while the Source owner is open";
    // Desk menu 23 (statistics menu): shows the menu and its first page (rankings, category 0)
    // built from the session. If building the page fails the menu is closed and the error returned.
    if(result.request==ui::FlowRequest::desk_action&&session&&
       result.desk_action.kind==ui::DeskRequestKind::open_menu&&result.desk_action.menu_kind==23) {
        if(!flow.desktop()||!flow.desktop()->pending()||*flow.desktop()->pending()!=result.desk_action||
           result.desk_action.revision!=revision)return {};
        flow.show_statistics_menu(result.desk_action,
            source_transfer_history_count(session->state(),result.desk_action.manager_slot));
        // StatisticsMenuScreen enters the normal statistics page at index 7, whose
        // StatisticsRankingsFromGame child starts at local category 0.
        try {
            const auto underlay=flow.statistics_menu()->render_page(7);
            const auto exe=load_le(ui::asset_path(data,"BMH.exe"));
            auto projection=ui::statistics_rankings_from_game(session->state(),exe,
                result.desk_action.manager_slot,0,0,underlay);
            flow.show_statistics_rankings(std::move(projection));
        } catch(const std::exception& e) {
            flow.close_statistics_menu();
            return e.what();
        }
        return {};
    }
    // Statistics menu selection (indices 0 to 8 choose the page: 0 opportunities, 1 transfer history,
    // 2 analytics, 3 all-time table, 4 world cup, 5 finance totals, 6 series records, 7 rankings,
    // 8 minute statistics). Only the pending action is accepted; a failed national-friendly family
    // check closes the menu; the page is built from the session state and shown through the flow.
    if(result.request==ui::FlowRequest::statistics_menu_action&&session) {
        const auto* pending=flow.pending_statistics_menu();
        if(!pending||*pending!=result.statistics_menu_action)return {};
        if(flow.national_friendly_family_active()&&
           !valid_national_friendly_family(flow,session->state(),revision)) {
            flow.close_statistics_menu();return {};
        }
        if(pending->close) { flow.close_statistics_menu();return {}; }
        // Parent selector actions remain live above either implemented child.
        // Closing A-D uses the same typed Source close transaction as its right-click path.
        const auto menu_request=*pending;
        // Switching page while the opportunities owner (A-D) is open: the new page is built and test
        // rendered first, then the opportunities owner is closed in the session and the new page shown.
        // If the staging fails nothing is closed and the statistics menu is rejected.
        if((menu_request.selected_index==1||menu_request.selected_index==2||(menu_request.selected_index>=3&&menu_request.selected_index<=8))&&session->opportunities_active()) {
            if(!flow.opportunities()||flow.opportunities()->pending()||!flow.statistics_menu()||
               menu_request.owner!=flow.statistics_menu()->state().owner||
               !flow.desktop()||menu_request.owner.revision!=flow.desktop()->state().revision) {
                flow.reject_statistics_menu("Der Statistikwechsel ist veraltet.");return {};
            }
            try {
                // Transaction body: builds exactly one target page, closes the opportunities child and
                // switches the flow to the staged page.
                OpportunitiesActions(*session).apply(revision, [&] {
                    std::optional<ui::StatisticsRankingState> history;
                    std::optional<ui::MinuteStatisticsProjection> minutes;
                    std::optional<ui::TransferHistoryProjection> transfers;
                    std::optional<ui::AnalyticsState> analytics;
                    std::optional<ui::AllTimeTableState> alltime;
                    std::optional<ui::WorldCupStatisticsProjection> worldcup;
                    std::optional<ui::FinanceTotals> finance;
                    std::optional<ui::SeriesRecordsProjection> series;
                    if(menu_request.selected_index==2) {
                        const auto underlay=flow.statistics_menu()->render_page(2);
                        const auto exe=load_le(ui::asset_path(data,"BMH.exe"));
                        analytics=ui::analytics_from_game(session->state(),exe,menu_request.owner.manager_slot,underlay);
                        analytics->owner=menu_request.owner;
                        ui::AnalyticsScreen staged(data,*analytics);
                    } else if(menu_request.selected_index==7) {
                        const auto underlay=flow.statistics_menu()->render_page(7);
                        const auto exe=load_le(ui::asset_path(data,"BMH.exe"));
                        history=ui::statistics_rankings_from_game(session->state(),exe,
                            menu_request.owner.manager_slot,0,0,underlay);
                        ui::StatisticsRankingsScreen staged(data,*history);
                    } else if(menu_request.selected_index==8) {
                        minutes=ui::minute_statistics_from_game(session->state(),menu_request.owner.manager_slot);
                        ui::MinuteStatisticsScreen staged(data,*minutes);
                    } else if(menu_request.selected_index==3) {
                        alltime=ui::alltime_table_from_game(session->state(),load_le(ui::asset_path(data,"BMH.exe")),
                            menu_request.owner,flow.statistics_menu()->render_page(3));
                        ui::AllTimeTableScreen staged(data,*alltime);
                    } else if(menu_request.selected_index==4) {
                        worldcup=ui::world_cup_statistics_from_game(session->state(),load_le(ui::asset_path(data,"BMH.exe")),
                            flow.national_friendly_family_active() ? ui::WorldCupStatisticsOwner::friendly_match_nation_picker :
                                                                     ui::WorldCupStatisticsOwner::desk_read_only);
                        ui::WorldCupStatisticsScreen staged(data,*worldcup,flow.statistics_menu()->render_page(4));
                    } else if(menu_request.selected_index==5) {
                        finance=ui::finance_totals_from_game(session->state(),menu_request.owner.manager_slot);
                        ui::FinanceTotalsScreen staged(data,*finance,flow.statistics_menu()->render_page(5));
                    } else if(menu_request.selected_index==6) {
                        series=ui::series_records_from_game(session->state(),load_le(ui::asset_path(data,"BMH.exe")),menu_request.owner.manager_slot);
                        ui::SeriesRecordsScreen staged(data,*series,flow.statistics_menu()->render_page(6));
                    } else {
                        const int count=source_transfer_history_count(session->state(),menu_request.owner.manager_slot);
                        const int origin=source_transfer_history_origin(count);
                        const auto underlay=flow.statistics_menu()->render_page(1);
                        transfers=ui::transfer_history_from_game(session->state(),menu_request.owner.manager_slot);
                        ui::TransferHistoryScreen staged(data,session->state(),*transfers);
                        (void)staged.render(underlay,origin);
                    }
                    const auto child=flow.opportunities()->state();
                    const auto close=OpportunitiesActions::close_request(child);
                    auto applied=OpportunitiesActions(*session).dispatch(close,revision,error);
                    if(!applied||!applied->closed)throw FormatError(error.empty()?"A-D konnte nicht geschlossen werden.":error);
                    flow.close_opportunities_for_statistics_parent(std::move(applied->page),std::move(applied->preparation));
                    if(analytics) {
                        analytics->owner=flow.statistics_menu()->state().owner;
                        flow.show_analytics(std::move(*analytics));
                    }
                    else if(alltime) {
                        alltime->owner=flow.statistics_menu()->state().owner;
                        alltime->underlay=flow.statistics_menu()->render_page(3);
                        flow.show_alltime_table(std::move(*alltime));
                    }
                    else if(worldcup)flow.show_world_cup_statistics(std::move(*worldcup));
                    else if(finance)flow.show_finance_totals(std::move(*finance));
                    else if(series)flow.show_series_records(std::move(*series));
                    else if(history)flow.show_statistics_rankings(std::move(*history));
                    else if(minutes)flow.show_minute_statistics(std::move(*minutes));
                    else flow.show_transfer_history(std::move(*transfers),session->state(),
                        source_transfer_history_origin(source_transfer_history_count(session->state(),menu_request.owner.manager_slot)));
                    return true;
                });
            } catch(const std::exception& e) {
                if(flow.pending_statistics_menu())flow.reject_statistics_menu(e.what());
                return e.what();
            }
            return error;
        }
        // The request's owner must still be the desk action that opened the menu (pending, or already
        // shown) at the current revision; otherwise the request cannot be assigned to this desk.
        const bool opening_owner=flow.desktop()&&pending->owner.revision==revision&&
            ((flow.desktop()->pending()&&*flow.desktop()->pending()==pending->owner)||
             (!flow.desktop()->pending()&&flow.desktop()->state().revision==revision));
        if((pending->selected_index<0||pending->selected_index>8)||!opening_owner) {
            flow.reject_statistics_menu("Diese Statistikaktion konnte nicht zugeordnet werden.");
            return {};
        }
        if(!flow.desktop()||pending->owner.revision!=revision)return {};
        if(menu_request.selected_index==2) {
            if(flow.desktop()->state().manager_slot!=pending->owner.manager_slot) {
                flow.reject_statistics_menu("Die Analytics-Ansicht ist veraltet.");return {};
            }
            try {
                const auto underlay=flow.statistics_menu()->render_page(2);
                const auto exe=load_le(ui::asset_path(data,"BMH.exe"));
                auto projection=ui::analytics_from_game(session->state(),exe,pending->owner.manager_slot,underlay);
                projection.owner=pending->owner;
                flow.show_analytics(std::move(projection));
            } catch(const std::exception& e) {
                if(flow.pending_statistics_menu())flow.reject_statistics_menu(e.what());
                return e.what();
            }
            return {};
        }
        if(menu_request.selected_index==1) {
            try {
                const int count=source_transfer_history_count(session->state(),menu_request.owner.manager_slot);
                auto projection=ui::transfer_history_from_game(session->state(),menu_request.owner.manager_slot);
                const auto underlay=flow.statistics_menu()->render_page(1);
                ui::TransferHistoryScreen staged(data,session->state(),projection);
                (void)staged.render(underlay,source_transfer_history_origin(count));
                flow.show_transfer_history(std::move(projection),session->state(),source_transfer_history_origin(count));
            } catch(const std::exception& e) {
                flow.reject_statistics_menu(e.what());return e.what();
            }
            return {};
        }
        if(menu_request.selected_index==8) {
            if(flow.in_statistics_rankings())flow.close_statistics_rankings();
            try {
                auto projection=ui::minute_statistics_from_game(session->state(),menu_request.owner.manager_slot);
                flow.show_minute_statistics(std::move(projection));
            } catch(const std::exception& e) {
                flow.reject_statistics_menu(e.what());return e.what();
            }
            return {};
        }
        if(menu_request.selected_index>=3&&menu_request.selected_index<=6) {
            try {
                const auto exe=load_le(ui::asset_path(data,"BMH.exe"));
                if(menu_request.selected_index==3) {
                    auto state=ui::alltime_table_from_game(session->state(),exe,menu_request.owner,
                        flow.statistics_menu()->render_page(3));
                    ui::AllTimeTableScreen staged(data,state);
                    flow.show_alltime_table(std::move(state));
                } else if(menu_request.selected_index==4) {
                    auto state=ui::world_cup_statistics_from_game(session->state(),exe,
                        flow.national_friendly_family_active() ? ui::WorldCupStatisticsOwner::friendly_match_nation_picker :
                                                                 ui::WorldCupStatisticsOwner::desk_read_only);
                    ui::WorldCupStatisticsScreen staged(data,state,flow.statistics_menu()->render_page(4));
                    flow.show_world_cup_statistics(std::move(state));
                } else if(menu_request.selected_index==5) {
                    auto state=ui::finance_totals_from_game(session->state(),menu_request.owner.manager_slot);
                    ui::FinanceTotalsScreen staged(data,state,flow.statistics_menu()->render_page(5));
                    flow.show_finance_totals(std::move(state));
                } else {
                    auto state=ui::series_records_from_game(session->state(),exe,menu_request.owner.manager_slot);
                    ui::SeriesRecordsScreen staged(data,state,flow.statistics_menu()->render_page(6));
                    flow.show_series_records(std::move(state));
                }
            } catch(const std::exception& e) {
                if(flow.pending_statistics_menu())flow.reject_statistics_menu(e.what());
                return e.what();
            }
            return {};
        }
        if(menu_request.selected_index==7) {
            if(flow.in_opportunities())return {};
            if(flow.in_statistics_rankings())flow.close_statistics_rankings();
            try {
                const auto underlay=flow.statistics_menu()->render_page(7);
                const auto exe=load_le(ui::asset_path(data,"BMH.exe"));
                auto projection=ui::statistics_rankings_from_game(session->state(),exe,
                    pending->owner.manager_slot,0,0,underlay);
                flow.show_statistics_rankings(std::move(projection));
            } catch(const std::exception& e) {
                flow.reject_statistics_menu(e.what());
                return e.what();
            }
            return {};
        }
        if(flow.in_statistics_rankings())flow.close_statistics_rankings();
        try {
            const auto owner=pending->owner;
            const auto underlay=flow.statistics_menu()->render_page(0);
            // Opportunities page (index 0). In the national friendly case the desk's stable session identity
            // key is passed to the session so the national-coach desk is identified by its home club.
            const bool national_source_case=flow.national_friendly_family_active();
            const int stable_identity=national_source_case&&flow.desktop()&&
                owner.manager_slot>=0&&size_t(owner.manager_slot)<flow.desktop()->state().managers.size()?
                flow.desktop()->state().session_identity_key(size_t(owner.manager_slot)):-1;
            auto page=OpportunitiesActions(*session).open(owner,revision,underlay,error,
                // Callback with the projected opportunities page. Identity check: manager slot, runtime club and
                // day counter must equal the desk on screen, and the stable club identity must equal the desk's
                // session identity key for that manager; otherwise it throws and nothing is shown.
                national_source_case,stable_identity,[&](ui::OpportunitiesState& opened) {
                    const auto& desk=flow.desktop()->state();
                    if(opened.identity.manager_slot!=desk.manager_slot||opened.identity.runtime_team!=desk.runtime_club||
                       opened.identity.day_counter!=desk.day_counter||desk.manager_slot<0||
                       size_t(desk.manager_slot)>=desk.managers.size()||
                       opened.identity.stable_club_identity!=int(desk.session_identity_key(size_t(desk.manager_slot))))
                        throw FormatError("opportunities: stale desktop identity");
                    flow.show_opportunities(std::move(opened),session->preparation_projection(revision),owner);
                });
            if(!page)flow.reject_statistics_menu(error);
        } catch(const std::exception& e) {
            if(flow.pending_statistics_menu())flow.reject_statistics_menu(e.what());
            return e.what();
        }
        return error;
    }
    // Statistics page action: close, or re-query the all-time table. The request must be the
    // pending one and match the menu owner and the desk (revision, manager slot); otherwise it is
    // dropped. A failing rebuild closes the page.
    if(result.request==ui::FlowRequest::statistics_page_action&&session) {
        const auto* pending=flow.pending_statistics_page_action();
        if(!pending||*pending!=result.statistics_page_action||!flow.in_statistics_page()||
           !flow.statistics_menu()||pending->owner!=flow.statistics_menu()->state().owner||
           !flow.desktop()||pending->owner.revision!=flow.desktop()->state().revision||
           pending->owner.manager_slot!=flow.desktop()->state().manager_slot)return {};
        if(flow.national_friendly_family_active()&&
           !valid_national_friendly_family(flow,session->state(),revision)) {
            flow.close_statistics_menu();return {};
        }
        if(pending->kind==ui::StatisticsPageActionKind::close) {
            flow.close_statistics_page();
            return {};
        }
        if(pending->kind!=ui::StatisticsPageActionKind::alltime||pending->page!=3||!flow.in_statistics_page())return {};
        try {
            const auto exe=load_le(ui::asset_path(data,"BMH.exe"));
            auto state=ui::alltime_table_from_game(session->state(),exe,pending->owner,
                flow.statistics_menu()->render_page(3),pending->nation,pending->relative);
            flow.show_alltime_table(std::move(state));
        } catch(const std::exception&) {
            flow.close_statistics_page();
        }
        return {};
    }
    // Rankings category selection: rebuilds the rankings from the session for the chosen category
    // and acknowledges it; any failure is reported as a rejection.
    if(result.request==ui::FlowRequest::statistics_rankings_action&&session) {
        const auto* pending=flow.pending_statistics_rankings();
        if(!pending||*pending!=result.statistics_rankings_action||
           pending->kind!=ui::StatisticsRankingsRequestKind::select_category||!flow.statistics_rankings())return {};
        try {
            const auto& prior=flow.statistics_rankings()->state();
            const auto exe=load_le(ui::asset_path(data,"BMH.exe"));
            auto next=ui::statistics_rankings_from_game(session->state(),exe,prior.manager_slot,
                pending->category,0,prior.underlay);
            flow.acknowledge_statistics_rankings(std::move(next));
        } catch(const std::exception&) { flow.reject_statistics_rankings(); }
        return {};
    }
    // Analytics screen action: close, or store a preference through OpportunitiesActions and
    // acknowledge the rebuilt page at the new revision. Rejection resets the pending state and
    // returns the error.
    if(result.request==ui::FlowRequest::analytics_action&&session) {
        const auto* pending=flow.pending_analytics();
        if(!pending||*pending!=result.analytics_action||!flow.analytics()||!flow.statistics_menu()||
           pending->owner!=flow.analytics()->state().owner||pending->manager_slot!=pending->owner.manager_slot||
           pending->owner!=flow.statistics_menu()->state().owner)return {};
        if(pending->kind==ui::AnalyticsRequestKind::close) { flow.close_analytics();return {}; }
        try {
            if(!OpportunitiesActions(*session).preference(*pending,revision,error,[&] {
                const auto underlay=flow.analytics()->state().underlay;
                const auto exe=load_le(ui::asset_path(data,"BMH.exe"));
                auto accepted=ui::analytics_from_game(session->state(),exe,pending->manager_slot,underlay);
                accepted.owner=pending->owner;accepted.owner.revision=revision;
                flow.acknowledge_analytics(std::move(accepted),session->preparation_projection(revision));
            })) {
                flow.reject_analytics();
                return error;
            }
        } catch(const std::exception& e) {
            if(flow.pending_analytics())flow.reject_analytics();
            return e.what();
        }
        return error;
    }
    // Opportunities (A-D) screen action: dispatched to the session; on close the parent screen is
    // restored (world cup picker for the national friendly case, closing the statistics menu
    // otherwise), else the page is acknowledged. Rejections go back to the screen.
    if(result.request==ui::FlowRequest::opportunities_action&&session) {
        const auto* pending=flow.pending_opportunities();
        if(!pending||*pending!=result.opportunities_action)return {};
        try {
            auto applied=OpportunitiesActions(*session).dispatch(result.opportunities_action,revision,error,
                [&](CareerSession::OpportunitiesDispatch& action) {
                    if(action.closed) {
                        const bool source_parent=flow.in_statistics_menu();
                        const bool national_family=flow.national_friendly_family_active();
                        flow.close_opportunities(std::move(action.page),std::move(action.preparation));
                        if(national_family) {
                            const auto exe=load_le(ui::asset_path(data,"BMH.exe"));
                            auto projection=ui::world_cup_statistics_from_game(session->state(),exe,
                                ui::WorldCupStatisticsOwner::friendly_match_nation_picker);
                            flow.show_world_cup_statistics(std::move(projection));
                        } else if(source_parent)flow.close_statistics_menu();
                    } else flow.acknowledge_opportunities(std::move(action.page),std::move(action.preparation));
                });
            if(!applied)flow.reject_opportunities(error);
        } catch(const std::exception& e) {
            if(flow.pending_opportunities())flow.reject_opportunities(e.what());return e.what();
        }
        return error;
    }
    // Desk menu 20 (top scorers): opens the read-only top scorers page; same pending-request and
    // revision rules as the other menus, rejection goes back to the desk.
    if(result.request==ui::FlowRequest::desk_action&&session&&
       result.desk_action.kind==ui::DeskRequestKind::open_menu&&result.desk_action.menu_kind==20) {
        if(!flow.desktop()||!flow.desktop()->pending()||*flow.desktop()->pending()!=result.desk_action||
           result.desk_action.revision!=revision)return {};
        try {
            const auto underlay=flow.desktop()->render();
            auto page=TopScorersActions(*session).open(result.desk_action,revision,underlay,error,
                // Callback with the projected page. Identity check as for the other pages: manager slot, runtime
                // club and day counter must equal the desk; the club identity is the home club identity that
                // the session also builds for the national desk.
                [&](ui::TopScorersState& opened) {
                    const auto& desk=flow.desktop()->state();
                    if(opened.identity.manager_slot!=desk.manager_slot||
                       opened.identity.runtime_team!=desk.runtime_club||
                       opened.identity.day_counter!=desk.day_counter||
                       desk.manager_slot<0||
                       size_t(desk.manager_slot)>=desk.managers.size()||
                       // Same home-club identity as the session builds for the national desk.
                       opened.identity.club_identity!=desk.managers[size_t(desk.manager_slot)].club_identity_key)
                        throw FormatError("top scorers: stale desktop identity");
                    flow.show_top_scorers(std::move(opened),session->preparation_projection(revision),result.desk_action);
            });
            if(!page)flow.reject_desk_action(error);
        } catch(const std::exception& e) {
            if(flow.desktop()&&flow.desktop()->pending())flow.reject_desk_action(e.what());
            return e.what();
        }
        return error;
    }
    // Desk menus 25 (fair play) and 26 (highscore): builds the read-only records page from the
    // session for the active manager. The highscore page also loads, updates and saves the ranked
    // highscore file in the save directory. Any error rejects the desk action.
    if(result.request==ui::FlowRequest::desk_action&&session&&
       result.desk_action.kind==ui::DeskRequestKind::open_menu&&
       (result.desk_action.menu_kind==25||result.desk_action.menu_kind==26)) {
        if(!flow.desktop()||!flow.desktop()->pending()||*flow.desktop()->pending()!=result.desk_action||
           result.desk_action.revision!=revision)return {};
        try {
            const auto underlay=flow.desktop()->render();
            ui::DeskRecordsState page;
            std::optional<fs::path> ranked_file;
            HighscoreTable ranked_rows{};
            if(result.desk_action.menu_kind==25) {
                const int manager=result.desk_action.manager_slot;
                const auto& game=session->state();
                if(manager<0||size_t(manager)>=game.manager_teams.size()||
                   !game.memory.has(::bmh::layout::ActiveManager)||game.memory.u8(::bmh::layout::ActiveManager)!=manager||
                   size_t(manager+1)*0xb0c>game.manager_records.size())
                    throw FormatError("fair play: invalid active manager");
                const int team=game.manager_teams[size_t(manager)];
                if(team!=kNoTeam&&(team<0||size_t(team)>=game.team_flags.size()))
                    throw FormatError("fair play: active manager has no Source roster");
                const int league=team>0x164?0:team_league(team);
                page=ui::fair_play_state_from_game(game,league,underlay,manager);
            } else {
                page.kind=ui::DeskRecordKind::highscore;
                const auto& game=session->state();
                const int manager=result.desk_action.manager_slot;
                if(manager<0||size_t(manager)>=game.manager_teams.size()||
                   !game.memory.has(::bmh::layout::ActiveManager)||game.memory.u8(::bmh::layout::ActiveManager)!=manager||
                   size_t(manager+1)*0xb0c>game.manager_records.size())
                    throw FormatError("highscore: invalid active manager/roster owner");
                const int team=game.manager_teams[size_t(manager)];
                if(team!=kNoTeam&&(team<0||size_t(team)>=game.team_flags.size()))
                    throw FormatError("highscore: active manager has no Source roster");
                const size_t name_at=size_t(manager)*0xb0c+0xa8;
                size_t name_length=0;
                while(name_length<30&&game.manager_records[name_at+name_length])++name_length;
                if(name_length==30)throw FormatError("highscore: unterminated active manager name");
                page.manager_name.assign(reinterpret_cast<const char*>(game.manager_records.data()+name_at),name_length);
                page.highscore.signature=game.memory.u16(::bmh::layout::CareerSession_FormatError_RuntimeField7);
                page.highscore.ranked=page.highscore.signature!=0;
                page.highscore.rows={};
                if(page.highscore.ranked) {
                    auto texts=load_textd(ui::asset_path(data,"TEXTD.BLK"));
                    const auto file=highscore_file(save_directory,page.highscore.signature,texts.strings.at(889));
                    HighscoreTable rows{};
                    if(fs::exists(file))rows=load_highscore_table(file);
                    page.highscore=make_highscore_projection(game,std::move(rows),HighscoreContext::ActiveManager);
                    ranked_file=file;
                    ranked_rows=page.highscore.rows;
                } else {
                    page.highscore=make_highscore_projection(game);

                }
                page.underlay=underlay;
            }
            const auto exe=load_le(ui::asset_path(data,"BMH.exe"));
            const auto& game=session->state();
            const auto source_byte=[&](uint32_t at){return game.memory.has(at)?game.memory.u8(at):exe.u8(at);};
            page.decorated_background=source_byte(::bmh::layout::PlainBackground)==0;
            page.gradient_text=source_byte(::bmh::layout::TextGradientDisabled)==0&&source_byte(::bmh::layout::TextGradientMode)==2;
            flow.show_desk_records(std::move(page),result.desk_action);
            if(ranked_file) {
                std::error_code directory_error;
                fs::create_directories(ranked_file->parent_path(),directory_error);
                if(!directory_error)(void)save_highscore_table(*ranked_file,ranked_rows);
            }
        } catch(const std::exception& e) {
            if(flow.desktop()&&flow.desktop()->pending())flow.reject_desk_action(e.what());
            return e.what();
        }
        return {};
    }
    // Top scorers page action: dispatched to the session; the page is closed or acknowledged,
    // rejections go back to the page.
    if(result.request==ui::FlowRequest::top_scorers_action&&session) {
        const auto* pending=flow.pending_top_scorers();
        if(!pending||*pending!=result.top_scorers_action)return {};
        try {
            auto applied=TopScorersActions(*session).dispatch(result.top_scorers_action,revision,error,
                [&](CareerSession::TopScorersDispatch& action) {
                    if(action.closed)flow.close_top_scorers(std::move(action.page),std::move(action.preparation));
                    else flow.acknowledge_top_scorers(std::move(action.page),std::move(action.preparation));
                });
            if(!applied) {
                flow.reject_top_scorers(error);
            }
        } catch(const std::exception& e) {
            if(flow.pending_top_scorers())flow.reject_top_scorers(e.what());
            return e.what();
        }
        return error;
    }
    // While the read-only top scorers owner is open, every other request is blocked.
    if(session&&session->top_scorers_active()&&result.request!=ui::FlowRequest::none)
        return "top scorers: another session action is blocked while the read-only owner is open";
    // Desk menu 28 (balance sheet): opens the balance page through BalanceActions.
    if(result.request==ui::FlowRequest::desk_action&&session&&
       result.desk_action.kind==ui::DeskRequestKind::open_menu&&result.desk_action.menu_kind==28) {
        if(!flow.desktop()||!flow.desktop()->pending()||*flow.desktop()->pending()!=result.desk_action||
           result.desk_action.revision!=revision)return {};
        try {
            const auto underlay=flow.desktop()->render();
            auto page=BalanceActions(*session).open(result.desk_action,revision,underlay,error,
                [&](ui::BalanceState& opened) { flow.show_balance(std::move(opened),result.desk_action); });
            if(!page)flow.reject_desk_action(error);
        } catch(const std::exception& e) {
            if(flow.desktop()&&flow.desktop()->pending())flow.reject_desk_action(e.what());
            return e.what();
        }
        return error;
    }
    // Balance page action: dispatched to the session; closed or acknowledged, rejections go back
    // to the page.
    if(result.request==ui::FlowRequest::balance_action&&session) {
        const auto* pending=flow.pending_balance();
        if(!pending||*pending!=result.balance_action)return {};
        try {
            auto applied=BalanceActions(*session).dispatch(result.balance_action,revision,error,
                [&](CareerSession::BalanceDispatch& action) {
                    if(action.closed)flow.close_balance(std::move(action.page),std::move(action.preparation));
                    else flow.acknowledge_balance(std::move(action.page));
                });
            if(!applied) {
                flow.reject_balance(error);
            }
        } catch(const std::exception& e) {
            if(flow.pending_balance())flow.reject_balance(e.what());
            return e.what();
        }
        return error;
    }
    // While the balance owner is open in the session, every other request is blocked.
    if(session&&session->balance_active()&&result.request!=ui::FlowRequest::none)
        return "balance: another session action is blocked while the Bilanz owner is open";
    // Shows a league table result from the session. It first validates the dispatch (manager slot,
    // league, revision, controls, dialog phase) and throws FormatError if it is inconsistent, then
    // replaces the current league's page in the table set and hands it to the flow. Used by every
    // branch that enters or changes a league table.
    const auto show_table_dispatch = [&](const CareerSession::LeagueTableDispatch& applied,
                                         std::optional<ui::PreparationState> preparation = {},bool friendly_entry=false) {
        const auto& id = applied.identity;
        if (!session || id.manager < 0 || id.manager > 3 || id.league < 0 || id.league >= 9 ||
            id.revision != revision || applied.page.manager_slot != id.manager ||
            applied.page.league_id != id.league || !applied.page.source_controls_projection)
            throw FormatError("league family: invalid authoritative Table projection");
        std::optional<ui::TableDialogStage> dialog_stage;
        using Phase = CareerSession::TableDispatchPhase;
        if (applied.phase == Phase::range_start) dialog_stage = ui::TableDialogStage::start;
        else if (applied.phase == Phase::range_end) dialog_stage = ui::TableDialogStage::end;
        else if (applied.phase != Phase::ready && applied.phase != Phase::forecast_pending_present)
            throw FormatError("league family: invalid Table dispatch phase");
        if (applied.dialog.has_value() != dialog_stage.has_value())
            throw FormatError("league family: mismatched Table dialog profile");
        if (!preparation && flow.desktop() && flow.desktop()->state().revision != id.revision)
            preparation = session->preparation_projection(revision);
        auto tables = ui::league_tables_from_game(session->state(), id.manager);
        tables[size_t(id.league)] = applied.page;
        flow.show_family_table_dispatch(std::move(tables), id.league, id, applied.dialog,
            dialog_stage, applied.phase == Phase::forecast_pending_present, std::move(preparation),friendly_entry);
    };
    // League family action (table, pairings and strengths screens share one owner in the session).
    // The request must carry the identity of the owner held by both the flow and the session, at
    // the current revision and from the child screen that is really open; otherwise it is stale.
    if(result.request==ui::FlowRequest::league_family_action&&session) {
        const auto& action=result.league_family_action;
        const auto& owner=flow.league_family_identity();
        const auto session_family=session->league_family_owner();
        // Identity check: the action's identity must equal the flow's owner and the session's family
        // owner, with the same child and the current revision.
        if(!flow.desktop()||!owner||action.from_child<1||action.from_child>3||
           !session_family||session_family->child!=action.from_child||session_family->identity!=action.identity||
           action.identity!=*owner||flow.league_family_child()!=action.from_child||
           action.identity.revision!=revision)
            return "league family: stale child request";
        // The friendly opponent table is a restricted purpose of the same owner: the flow and the
        // session must agree on it, and only table controls, selection and close are allowed there.
        if(session_family->friendly_opponent!=flow.friendly_table_active()||
           (session_family->friendly_opponent&&action.kind!=ui::LeagueFamilyRequestKind::control&&
            action.kind!=ui::LeagueFamilyRequestKind::table_range_result&&
            action.kind!=ui::LeagueFamilyRequestKind::table_forecast_presented&&
            action.kind!=ui::LeagueFamilyRequestKind::select_league&&
            action.kind!=ui::LeagueFamilyRequestKind::toggle_league_group&&
            action.kind!=ui::LeagueFamilyRequestKind::select_opponent&&
            action.kind!=ui::LeagueFamilyRequestKind::close))
            return "friendly table: invalid owner purpose or action";
        // Opponent chosen in the friendly table: the session resolves it; picking the own club returns
        // to the previous state, otherwise the club friendly calendar state is built and the table
        // finished. Errors reject the family screen.
        if(action.kind==ui::LeagueFamilyRequestKind::select_opponent) {
            const auto* pending=flow.pending_league_family_action();
            if(!session_family->friendly_opponent||!pending||*pending!=action)
                return "friendly table: opponent is not the pending UI row";
            try {
                // Transaction body for this branch; an exception ends in the catch below.
                LeagueFamilyActions(*session).apply(revision, [&]() -> bool {
                    const auto opponent=session->resolve_friendly_table_opponent(action,revision,error);
                    if(!opponent)throw FormatError(error.empty()?"friendly table: opponent rejected":error);
                    if(*opponent==action.identity.runtime_club) {
                        const auto retained=session->acknowledge_friendly_own_club(action,revision,error);
                        if(!retained)throw FormatError(error.empty()?"friendly table: own-club return rejected":error);
                        flow.acknowledge_friendly_own_club(action,*retained);return true;
                    }
                    const auto visible=session->current_league_table_dispatch(error);
                    if(!visible)throw FormatError(error);
                    auto preparation=session->finish_friendly_opponent_table(action,revision,error);
                    if(!preparation)throw FormatError(error.empty()?"friendly table: return rejected":error);
                    auto friendly=club_friendly_state(*session,preparation->desktop,data,revision);
                    friendly.selected_opponent=*opponent;friendly.selected_league=action.identity.league;
                    // Preserve the final visible Source table below venue/cost questions.
                    friendly.leagues[size_t(action.identity.league)]=visible->page;
                    flow.complete_friendly_opponent_table(action,std::move(friendly),std::move(*preparation));
                    return true;
                });
            } catch(const std::exception& e) {
                flow.reject_league_family(e.what());return e.what();
            }
            return {};
        }
        // Table child: controls, range replies and the forecast acknowledgement. The request must be
        // the pending UI action; the session applies it and the new table is shown.
        if (action.from_child == 1 && (action.kind == ui::LeagueFamilyRequestKind::control ||
            action.kind == ui::LeagueFamilyRequestKind::table_range_result ||
            action.kind == ui::LeagueFamilyRequestKind::table_forecast_presented)) {
            const auto* pending = flow.pending_league_family_action();
            if (!pending || *pending != action)
                return "league family: Table request is not the pending UI action";

            try {
                LeagueFamilyActions(*session).apply(revision, [&]() -> bool {
                    std::optional<CareerSession::LeagueTableDispatch> applied;
                    if (action.kind == ui::LeagueFamilyRequestKind::control)
                        applied = session->dispatch_league_table_control(action, revision, error);
                    else if (action.kind == ui::LeagueFamilyRequestKind::table_range_result)
                        applied = session->dispatch_league_table_range_reply(action, revision, error);
                    else applied = session->acknowledge_league_forecast_present(action, revision, error);
                    if (!applied) throw FormatError(error.empty() ? "league family: Table action rejected" : error);
                    show_table_dispatch(*applied);
                    return true;
                });
            } catch (const std::exception& e) {
                flow.reject_league_family(e.what());
                return e.what();
            }
            return {};
        }
        // Team info opened from the table: the presenter runs in the session, then the same table is
        // shown again and must match the identity the session returned.
        if(action.kind==ui::LeagueFamilyRequestKind::team_info&&action.from_child==1) {
            try {
                LeagueFamilyActions(*session).apply(revision, [&]() -> bool {
                    const auto returned=session->dispatch_family_table_team_info(action,revision,error,team_info_presenter);
                    if(!returned)throw FormatError(error.empty()?"league family Table TeamInfo rejected":error);
                    const auto table = session->current_league_table_dispatch(error);
                    if (!table || table->identity != *returned)
                        throw FormatError(error.empty() ? "league family: missing Table TeamInfo return" : error);
                    auto preparation=session->preparation_projection(revision);
                    show_table_dispatch(*table, std::move(preparation));
                    return true;
                });
            } catch(const std::exception& e) {
                flow.reject_league_family(e.what());return e.what();
            }
            return {};
        }
        // Close: leaves the family (or finishes the friendly table) and gives back the desk
        // preparation state. A refused close rejects the family screen.
        if(action.kind==ui::LeagueFamilyRequestKind::close) {
            try {
                LeagueFamilyActions(*session).apply(revision, [&]() -> bool {
                    const auto* pending=flow.pending_league_family_action();
                    if(session_family->friendly_opponent&&(!pending||*pending!=action))
                        throw FormatError("friendly table: close is not the pending UI action");
                    auto preparation=session_family->friendly_opponent
                        ?session->finish_friendly_opponent_table(action,revision,error)
                        :session->close_league_family(action,revision,error);
                    if(!preparation) {
                        flow.reject_league_family(error.empty()?"league family close rejected":error); return false;
                    } else flow.close_league_family(action,std::move(*preparation));
                    return true;
                });
            } catch(const std::exception& e) {
                if(flow.league_family_identity())flow.reject_league_family(e.what());
                return e.what();
            }
            return error;
        }
        // League selection or group toggle in the table: the session selects the page, then the
        // resulting table identity is compared with the request (same manager, club, day counter,
        // revision and so on, and the family sequence advanced by one) so a result belonging to a
        // different operation is refused.
        if((action.kind==ui::LeagueFamilyRequestKind::select_league||
            action.kind==ui::LeagueFamilyRequestKind::toggle_league_group)&&action.from_child==1) {
            const auto* pending=flow.pending_league_family_action();
            if(!pending||*pending!=action)
                return "league family: selector request is not the pending UI action";
            try {
                LeagueFamilyActions(*session).apply(revision, [&]() -> bool {
                    if(!session->select_league_family_page(action,revision,error))
                        throw FormatError(error.empty()?"league family selector rejected":error);
                    const auto table=session->current_league_table_dispatch(error);
                    const bool toggle=action.kind==ui::LeagueFamilyRequestKind::toggle_league_group;
                    const uint8_t expected_group=toggle?uint8_t(action.identity.league_group^1):action.identity.league_group;
                    if(!table||table->identity.league!=action.league_id||
                       table->identity.league_group!=expected_group||
                       table->identity.manager!=action.identity.manager||
                       table->identity.runtime_club!=action.identity.runtime_club||
                       table->identity.club_identity!=action.identity.club_identity||
                       table->identity.day_counter!=action.identity.day_counter||
                       table->identity.manager_default_league!=action.identity.manager_default_league||
                       table->identity.source_param2!=action.identity.source_param2||
                       table->identity.scope!=action.identity.scope||
                       table->identity.sequence!=action.identity.sequence||
                       table->identity.family_id!=action.identity.family_id||
                       table->identity.family_sequence!=action.identity.family_sequence+1||
                       table->identity.revision!=action.identity.revision)
                        throw FormatError(error.empty()?"league family: selector result does not match its bound operation":error);
                    const bool league_changed=table->identity.league!=action.identity.league;
                    if(league_changed) show_table_dispatch(*table);
                    else flow.acknowledge_family_selector(action,table->identity);
                    return true;
                });
            } catch(const std::exception& e) {
                flow.reject_league_family(e.what());return e.what();
            }
            return {};
        }

        // Navigation between table, pairings and strengths: the current child is suspended, a fresh
        // manager projection supplies the new identity (family id and league settings are carried
        // over) and the target child is prepared, entered and shown. Any step refused rejects the
        // family screen.
        if(action.kind==ui::LeagueFamilyRequestKind::navigate&&flow.league_family_child()==action.from_child&&
           (action.target_child==1||action.target_child==2||action.target_child==3)) {
            try {
                LeagueFamilyActions(*session).apply(revision, [&]() -> bool {
                    bool suspended=false;
                    if(action.from_child==3)suspended=session->suspend_strengths_for_family(action,revision,error);
                    else if(action.from_child==2)suspended=session->suspend_pairings_for_family(action,revision,error);
                    else suspended=action.from_child==1;
                    if(!suspended)throw FormatError(error.empty()?"league family child transition rejected":error);
                    const auto fresh=session->strengths_open_request(revision);
                    if(!fresh)throw FormatError("league family: missing fresh manager projection");
                    auto identity=fresh->identity;
                    identity.family_id=action.identity.family_id;
                    identity.family_sequence=action.identity.family_sequence+1;
                    identity.league=action.identity.league;
                    identity.league_group=action.identity.league_group;
                    identity.manager_default_league=action.identity.manager_default_league;
                    identity.source_param2=action.identity.source_param2;
                    if(action.target_child==1) {
                        if(!session->prepare_league_family_child(identity,1,revision,error))
                            throw FormatError(error.empty()?"league family Table entry rejected":error);
                        identity.expected_family_word=session->state().memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(identity.manager)*2);
                        if(!session->advance_league_family(action,identity,1,revision,error))
                            throw FormatError(error.empty()?"league family Table handoff rejected":error);
                        const auto table = session->current_league_table_dispatch(error);
                        if (!table) throw FormatError(error.empty() ? "league family: missing Table entry projection" : error);
                        show_table_dispatch(*table);
                    } else if(action.target_child==2) {
                        if(!session->prepare_league_family_child(identity,2,revision,error))
                            throw FormatError(error.empty()?"league family Pairings entry rejected":error);
                        identity.expected_family_word=session->state().memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(identity.manager)*2);
                        const auto open=LeagueFamilyActions::pairings_entry(identity,revision);
                        auto page=session->open_league_pairings(open,revision,error);
                        if(!page)throw FormatError(error.empty()?"league family Pairings projection rejected":error);
                        if(page->identity.league!=identity.league) {
                            const auto select=LeagueFamilyActions::pairings_selection(page->identity,identity.league);
                            auto selected=session->dispatch_league_pairings(select,revision,error);
                            if(!selected||selected->closed)throw FormatError(error.empty()?"league family Pairings selection rejected":error);
                            page=std::move(selected->page);
                        }
                        // Pairings' Source selector can commit one page revision while
                        // choosing the family's requested league before first display.
                        identity.revision=revision;
                        identity.expected_family_word=session->state().memory.u16(
                            ::bmh::layout::LeagueTableDisplayModes+uint32_t(identity.manager)*2);
                        if(!session->advance_league_family(action,identity,2,revision,error))
                            throw FormatError(error.empty()?"league family Pairings handoff rejected":error);
                        flow.show_family_league_pairings(std::move(*page),identity);
                    } else {
                        const auto open=LeagueFamilyActions::strengths_entry(*fresh,identity);
                        auto page=session->dispatch_strengths(open,revision,error,{},true);
                        if(!page||page->closed)throw FormatError(error.empty()?"league family Strengths projection rejected":error);
                        if(!session->advance_league_family(action,page->page.identity,3,revision,error))
                            throw FormatError(error.empty()?"league family Strengths handoff rejected":error);
                        flow.show_family_strengths(std::move(page->page),std::move(page->preparation));
                    }
                    return true;
                });
            } catch(const std::exception& e) {
                flow.reject_league_family(e.what());return e.what();
            }
            return error;
        }
        // Anything else is not supported by this owner.
        flow.reject_league_family("league family action is not yet supported by this owner");
        return "league family action is not yet supported by this owner";
    }
    // Desk menu 2 (strengths): opens the strengths page and starts a league family owner for it.
    // The session's projection must match the desk on screen (manager slot, runtime club, day
    // counter and the desk's session identity key); otherwise the open is refused.
    if(result.request==ui::FlowRequest::desk_action&&session&&
       result.desk_action.kind==ui::DeskRequestKind::open_menu&&result.desk_action.menu_kind==2) {
        if(!flow.desktop()||!flow.desktop()->pending()||*flow.desktop()->pending()!=result.desk_action||
           result.desk_action.revision!=revision)return {};
        try {
            LeagueFamilyActions(*session).apply(revision, [&]() -> bool {
                const auto semantic=session->strengths_open_request(revision);
                const auto& desk=flow.desktop()->state();
                if(!semantic||semantic->identity.manager!=desk.manager_slot||semantic->identity.runtime_club!=desk.runtime_club||
                   semantic->identity.day_counter!=desk.day_counter||desk.manager_slot<0||size_t(desk.manager_slot)>=desk.managers.size()||
                   semantic->identity.club_identity!=desk.session_identity_key(size_t(desk.manager_slot)))
                    throw FormatError("strengths: stale desktop identity");
                const auto applied=session->dispatch_strengths(*semantic,revision,error);
                if(!applied)throw FormatError(error.empty()?"league family Strengths entry rejected":error);
                else {
                    const auto family=session->begin_league_family(applied->page.identity,3,revision,error);
                    if(!family)throw FormatError(error.empty()?"league family Strengths owner rejected":error);
                    auto page=applied->page;page.identity=*family;
                    flow.show_strengths(std::move(page),applied->preparation,result.desk_action);
                }
                return true;
            });
        } catch(const std::exception& e) {
            if(flow.desktop()&&flow.desktop()->pending())flow.reject_desk_action(e.what());
            return e.what();
        }
        return error;
    }
    // Desk menu 0 (league table): checks manager slot, runtime club and day counter against the
    // desk, prepares and begins the league family owner for the table and shows the table.
    if(result.request==ui::FlowRequest::desk_action&&session&&
       result.desk_action.kind==ui::DeskRequestKind::open_menu&&result.desk_action.menu_kind==0) {
        if(!flow.desktop()||!flow.desktop()->pending()||*flow.desktop()->pending()!=result.desk_action||
           result.desk_action.revision!=revision)return {};
        try {
            LeagueFamilyActions(*session).apply(revision, [&]() -> bool {
                const auto& desk=flow.desktop()->state();
                const auto request=session->strengths_open_request(revision);
                if(!request||request->identity.manager!=desk.manager_slot||request->identity.runtime_club!=desk.runtime_club||
                   request->identity.day_counter!=desk.day_counter)
                    throw FormatError("league family: stale Table entry identity");
                auto identity=request->identity;
                if(!session->prepare_league_family_child(identity,1,revision,error))
                    throw FormatError(error.empty()?"league family Table entry rejected":error);
                identity.expected_family_word=session->state().memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(identity.manager)*2);
                const auto bound=session->begin_league_family(identity,1,revision,error);
                if(!bound)throw FormatError(error.empty()?"league family Table owner rejected":error);
                const auto table = session->current_league_table_dispatch(error);
                if (!table || table->identity != *bound)
                    throw FormatError(error.empty() ? "league family: missing bound Table projection" : error);
                show_table_dispatch(*table);
                return true;
            });
        } catch(const std::exception& e) {
            if(flow.desktop()&&flow.desktop()->pending())flow.reject_desk_action(e.what());
            return e.what();
        }
        return error;
    }
    // Strengths page action: dispatched to the session; closed or acknowledged. When a league
    // family is open its identity is refreshed after the child changed.
    if(result.request==ui::FlowRequest::strengths_action&&session) {
        const auto* pending=flow.pending_strengths();
        if(!pending||*pending!=result.strengths_action)return {};
        const auto family_before=session->league_family_owner();
        try {
            LeagueFamilyActions(*session).apply(revision, [&]() -> bool {
                auto applied=session->dispatch_strengths(result.strengths_action,revision,error,team_info_presenter);
                if(!applied) {
                    flow.reject_strengths(error); return false;
                } else if(applied->closed)flow.close_strengths(std::move(applied->page),std::move(applied->preparation));
                else {
                    if(family_before) {
                        if(!session->refresh_family_after_child(family_before->identity,3,applied->page.identity,revision,error))
                            throw FormatError(error.empty()?"league family Strengths refresh rejected":error);
                        applied->page.identity=session->league_family_owner()->identity;
                    }
                    flow.acknowledge_strengths(std::move(applied->page),std::move(applied->preparation));
                }
                return true;
            });
        } catch(const std::exception& e) {
            if(flow.pending_strengths())flow.reject_strengths(e.what());
            return e.what();
        }
        return error;
    }
    // Desk menu 1 (pairings): checks manager slot, runtime club and day counter against the desk,
    // then opens the pairings page and begins the league family owner for it.
    if(result.request==ui::FlowRequest::desk_action&&session&&
        result.desk_action.kind==ui::DeskRequestKind::open_menu&&result.desk_action.menu_kind==1) {
        if(!flow.desktop()||!flow.desktop()->pending()||*flow.desktop()->pending()!=result.desk_action||
            result.desk_action.revision!=revision)return {};
        try {
            LeagueFamilyActions(*session).apply(revision, [&]() -> bool {
                const auto family_request=session->strengths_open_request(revision);
                if(!family_request||family_request->identity.manager!=result.desk_action.manager_slot||
                   family_request->identity.runtime_club!=flow.desktop()->state().runtime_club||
                   family_request->identity.day_counter!=flow.desktop()->state().day_counter)
                    throw FormatError("league family: stale Pairings entry identity");
                if(!session->prepare_league_family_child(family_request->identity,2,revision,error))
                    throw FormatError(error);
                auto family_identity=family_request->identity;
                family_identity.expected_family_word=session->state().memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(family_identity.manager)*2);
                auto page=session->open_league_pairings(result.desk_action,revision,error);
                if(!page)throw FormatError(error.empty()?"league family Pairings entry rejected":error);
                else {
                    const auto bound=session->begin_league_family(family_identity,2,revision,error);
                    if(!bound)throw FormatError(error.empty()?"league family Pairings owner rejected":error);
                    family_identity=*bound;
                    flow.show_family_league_pairings(std::move(*page),family_identity);
                }
                return true;
            });
        } catch(const std::exception& e) {
            if(flow.desktop()&&flow.desktop()->pending())flow.reject_desk_action(e.what());
            return e.what();
        }
        return error;
    }
    // Pairings page action: dispatched to the session; closed or acknowledged. With a league family
    // open, the family identity is refreshed from the new page state.
    if(result.request==ui::FlowRequest::league_pairings_action&&session) {
        const auto* pending=flow.pending_league_pairings();
        if(!pending||*pending!=result.league_pairings_action)return {};
        const auto family_before=session->league_family_owner();
        try {
            LeagueFamilyActions(*session).apply(revision, [&]() -> bool {
                auto applied=session->dispatch_league_pairings(result.league_pairings_action,revision,error,team_info_presenter);
                if(!applied) {
                    flow.reject_league_pairings(error); return false;
                } else if(applied->closed)flow.close_league_pairings(std::move(applied->preparation));
                else {
                    std::optional<ui::StrengthsIdentity> refreshed;
                    if(family_before) {
                        auto next=family_before->identity;
                        next.revision=revision;next.scope=applied->page.identity.scope;
                        next.sequence=applied->page.identity.sequence;next.league=applied->page.identity.league;
                        next.league_group=uint8_t(applied->page.source.owner.league_group);
                        if(!session->refresh_family_after_child(family_before->identity,2,next,revision,error))
                            throw FormatError(error.empty()?"league family Pairings refresh rejected":error);
                        refreshed=session->league_family_owner()->identity;
                    }
                    flow.acknowledge_league_pairings(std::move(applied->page),refreshed);
                }
                return true;
            });
        } catch(const std::exception& e) {
            if(flow.pending_league_pairings())flow.reject_league_pairings(e.what());
            return e.what();
        }
        return error;
    }
    if (result.request == ui::FlowRequest::desk_action && session &&
        result.desk_action.kind == ui::DeskRequestKind::open_menu && result.desk_action.menu_kind == 29) {
        // Desk menu 29: opens the dialog for adding a further manager to the running game.
        if (!flow.desktop() || !flow.desktop()->pending() || *flow.desktop()->pending() != result.desk_action ||
            result.desk_action.revision != revision) return {};
        try { flow.show_manager_join(session->manager_choices(revision),session->selection_context(revision)); }
        catch (const std::exception&) { flow.reject_desk_action("Der neue Manager kann hier nicht angelegt werden."); }
    } else if (result.request == ui::FlowRequest::join_cancel && session) {
        // Join cancelled: the session drops the half finished join and the flow returns to the desk.
        if (!flow.join_origin() || *flow.join_origin() != result.desk_action) return {};
        session->cancel_manager_join(); flow.reject_manager_join({});
    } else if ((result.request == ui::FlowRequest::join_options || result.request == ui::FlowRequest::join_manager) && session) {
        // Manager join steps (options, then manager choice): the origin desk action and revision must
        // still match. Until the join is complete the team selection resumes; once complete the new
        // manager is committed to the session and the flow shows the result. Any failure cancels the
        // join and returns an error text.
        if (!flow.join_origin() || *flow.join_origin() != result.desk_action || result.desk_action.revision != revision)
            return {};
        bool accepted=false;
        if (result.request == ui::FlowRequest::join_options) {
            const auto* pending=flow.pending_join_options();
            if (!pending || *pending != result.join_options) return {};
            accepted=session->begin_manager_join(result.desk_action,result.join_options,revision,error);
        } else {
            const auto* pending=flow.pending_join_manager();
            if (!pending || *pending != result.join_manager) return {};
            accepted=session->confirm_manager_join(result.join_manager,error);
        }
        if (!accepted) {
            session->cancel_manager_join(); flow.reject_manager_join("Der neue Manager konnte nicht übernommen werden.");
            return "manager join: " + error;
        }
        if (!session->manager_join_complete()) {
            try { flow.resume_manager_join(session->manager_join_context()); }
            catch (const std::exception& e) {
                session->cancel_manager_join(); flow.reject_manager_join("Die Mannschaftsauswahl konnte nicht geöffnet werden.");
                return "manager join: " + std::string(e.what());
            }
        } else {
            try {
                if (!ManagerJoinActions(*session).commit(revision,error,[&](ui::CareerInitializationResult& joined) {
                    flow.acknowledge_manager_join(std::move(joined));
                })) {
                    session->cancel_manager_join(); flow.reject_manager_join("Der Spielstand konnte nicht übernommen werden.");
                    return "manager join: " + error;
                }
            }
            catch (const std::exception& e) {
                flow.reject_manager_join("Der Spielstand konnte nicht angezeigt werden.");
                return "manager join: " + std::string(e.what());
            }
        }
    } else
    if (result.request == ui::FlowRequest::manager_confirmed) {
        // New-career setup: a manager was confirmed. The session is created on demand, the choice is
        // stored and the selection screen resumes with a new revision.
        if (!session) session = std::make_unique<app::CareerSession>(data, seed);
        if (enable_autosave) session->configure_autosave_directory(save_directory);
        const ui::ManagerChoice* choice = flow.pending_manager();
        if (!choice || !session->confirm_manager(flow.options(), *choice, error)) return "manager: " + error;
        flow.resume_manager_selection(session->selection_context(++revision));
    } else if (result.request == ui::FlowRequest::new_career) {
        // New career: confirms the managers not yet confirmed, then finishes the initialization in the
        // session, takes over the first revision and hands the desk to the flow. Season pause and cup
        // draw presenters are installed only for this request (see the reset guard).
        if (!session) session = std::make_unique<app::CareerSession>(data, seed);
        if (enable_autosave) session->configure_autosave_directory(save_directory);
        const ui::NewCareerRequest* request = flow.pending_career();
        if (!request) return "no pending career";
        for (size_t m = size_t(session->confirmed()); m < request->managers.size(); m++)
            if (!session->confirm_manager(request->options, request->managers[m], error)) return "manager: " + error;
        ui::CareerInitializationResult init;
        // Initial setup reaches the same native draw boundaries as annual turns.
        // Scope presentation hooks to this request, including failure paths.
        // Scope guard: removes the presenters again when the request ends, on success or failure.
        struct InitialHookReset {
            CareerSession* session;
            ~InitialHookReset() {
                session->set_season_pause({});
                session->set_season_cup_draw({});
            }
        } initial_reset{session.get()};
        session->set_season_pause(season_presenter);
        session->set_season_cup_draw(cup_draw_presenter);
        if (!session->finish(init, error)) return "career: " + error;
        playback.reset_session(session->session_id());
        audio_session = session->session_id();
        revision = init.desktop.revision;
        auto preparation = std::move(init.preparation);
        flow.attach_initialized_career(std::move(init));
        flow.attach_preparation_context(std::move(preparation));
        session->configure_sound_capabilities(playback.original_ready() && playback.available(),
            playback.original_ready() && playback.available());
        start_original_desk_audio(playback, session->state(), music_enabled);
    } else if(result.request==ui::FlowRequest::national_friendly_selected&&session) {
        // National friendly: a nation was selected in the picker; the dialog sequence is served by
        // serve_national_friendly_selection.
        serve_national_friendly_selection(flow,*session,result.national_friendly_opponent,revision,data,
                                          national_friendly_question_presenter);
    } else if (result.request == ui::FlowRequest::friendly_action && session) {
        // Friendly match request (club or national); served by serve_friendly_action.
        serve_friendly_action(flow, *session, result.friendly_action, revision,data);
    } else if (result.request == ui::FlowRequest::submenu_action && session &&
               std::holds_alternative<ui::TransferMarketRequest>(result.submenu_action)) {
        // Transfer market request; served by serve_transfer_action.
        serve_transfer_action(flow, *session, std::get<ui::TransferMarketRequest>(result.submenu_action), revision,
                              market_sale_presenter, market_entry_policy, poach_player_selector, poach_notice_presenter, transfer_presenter);
    } else if (result.request == ui::FlowRequest::submenu_action && session &&
               std::holds_alternative<ui::OutgoingTransferRequest>(result.submenu_action)) {
        // Outgoing transfer offer; served by serve_transfer_offer.
        serve_transfer_offer(flow, *session, std::get<ui::OutgoingTransferRequest>(result.submenu_action), revision,
                             market_sale_presenter, market_entry_policy, team_info_presenter, player_info_presenter);
    } else if (result.request == ui::FlowRequest::submenu_action && session &&
               std::holds_alternative<ui::LineupRequest>(result.submenu_action)) {
        // Lineup change: must equal the flow's pending lineup request, otherwise it is dropped. The
        // session applies it and the preparation state is acknowledged; a refusal is shown as notice.
        const auto& request = std::get<ui::LineupRequest>(result.submenu_action);
        if (!flow.matches_pending_lineup(request)) return {};
        try {
            // Callback after the lineup was applied: acknowledges the new preparation projection.
            if (!LineupActions(*session).lineup(request, revision, error, [&] {
                flow.acknowledge_preparation(session->preparation_projection(revision));
            })) {
                flow.reject_preparation_action(error.empty() ? "Die Aufstellungsaktion ist veraltet." : error);
                return {};
            }
        } catch (const std::exception&) {
            flow.reject_preparation_action("Diese Aufstellungsaktion konnte nicht übernommen werden.");
        }
    } else if (result.request == ui::FlowRequest::submenu_action && session &&
               std::holds_alternative<ui::PitchRequest>(result.submenu_action)) {
        // Pitch (field position) change: same pattern as the lineup request, through LineupActions.
        const auto& request = std::get<ui::PitchRequest>(result.submenu_action);
        if (!flow.matches_pending_pitch(request)) return {};
        try {
            if (!LineupActions(*session).pitch(request, revision, error, [&] {
                flow.acknowledge_preparation(session->preparation_projection(revision));
            })) {
                flow.reject_preparation_action(error.empty() ? "Diese Spielfeldaktion ist noch nicht verfügbar." : error);
                return {};
            }
        } catch (const std::exception&) {
            flow.reject_preparation_action("Diese Aufstellungsaktion konnte nicht übernommen werden.");
        }
    } else if (result.request == ui::FlowRequest::submenu_action && session &&
               std::holds_alternative<ui::TrainingRequest>(result.submenu_action)) {
        // Training change: dispatched to the session; the preparation state is acknowledged or the
        // request rejected with the session's error.
        const auto& request = std::get<ui::TrainingRequest>(result.submenu_action);
        if (!session->dispatch_training(request, revision, error)) {
            flow.reject_preparation_action(error);
            return {};
        }
        flow.acknowledge_preparation(session->preparation_projection(revision));
    } else if (result.request == ui::FlowRequest::submenu_action && session &&
               std::holds_alternative<ui::TacticsRequest>(result.submenu_action)) {
        // Tactics change: dispatched to the session; acknowledged or rejected with the error.
        const auto& request = std::get<ui::TacticsRequest>(result.submenu_action);
        if (!session->dispatch_tactics(request, revision, error)) {
            flow.reject_preparation_action(error);
            return {};
        }
        flow.acknowledge_preparation(session->preparation_projection(revision));
    } else if (result.request == ui::FlowRequest::submenu_action && session &&
               std::holds_alternative<ui::TrainingCampRequest>(result.submenu_action)) {
        // Training camp request: dispatched to the session; acknowledged or rejected.
        const auto& request = std::get<ui::TrainingCampRequest>(result.submenu_action);
        if (!session->dispatch_training_camp(request, revision, error)) {
            flow.reject_preparation_action(error);
            return {};
        }
        flow.acknowledge_preparation(session->preparation_projection(revision));
    } else if (result.request == ui::FlowRequest::submenu_action && session &&
               std::holds_alternative<ui::BankRequest>(result.submenu_action)) {
        // Bank action: the supplied request must equal the pending one, otherwise it is rejected as
        // stale. BankActions applies it and the preparation state is acknowledged.
        const auto* pending = flow.pending_bank_action();
        if (!pending) return {};
        const auto request = *pending;
        const auto& supplied = std::get<ui::BankRequest>(result.submenu_action);
        if (request != supplied) {
            flow.reject_preparation_action("stale_bank_request");
            return {};
        }
        try {
            if (!BankActions(*session).dispatch(request, revision, error, [&] {
                flow.acknowledge_preparation(session->preparation_projection(revision));
            })) {
                flow.reject_preparation_action(error.empty() ? "bank_action_failed" : error);
                return {};
            }
        } catch (const std::exception&) {
            flow.reject_preparation_action("bank_ack_failed");
        }
    } else if (result.request == ui::FlowRequest::submenu_action && session &&
               std::holds_alternative<ui::StadiumRequest>(result.submenu_action)) {
        // Stadium action: must equal the pending stadium request; StadiumActions applies it (with the
        // construction and compensation notice hooks) and the preparation state is acknowledged.
        const auto& request = std::get<ui::StadiumRequest>(result.submenu_action);
        if (!flow.matches_pending_stadium(request)) return {};
        try {
            if (!StadiumActions(*session).dispatch(request, revision, error, construction_notice_presenter,
                compensation_notice_presenter, [&] {
                    flow.acknowledge_preparation(session->preparation_projection(revision));
                })) {
                flow.reject_preparation_action(error.empty() ? "stadium_action_failed" : error);
                return {};
            }
        } catch (const std::exception&) {
            flow.reject_preparation_action("stadium_ack_failed");
        }
    } else if (result.request == ui::FlowRequest::submenu_action && session &&
               std::holds_alternative<ui::TotoRequest>(result.submenu_action)) {
        // Toto (betting) action: dispatched to the session; acknowledged or rejected.
        const auto& request = std::get<ui::TotoRequest>(result.submenu_action);
        if (!session->dispatch_toto(request, revision, error)) {
            flow.reject_preparation_action(error);
            return {};
        }
        flow.acknowledge_preparation(session->preparation_projection(revision));
    } else if (result.request == ui::FlowRequest::kickoff) {
        // Kickoff: plays the next match day. The request must still be the desk's pending action at
        // the current revision (a replayed one is ignored). All presenter hooks are installed on the
        // session for the duration of the turn and removed again by the reset guard below, then
        // dispatch_kickoff runs the whole turn synchronously. A failed or waiting turn rejects the
        // desk action, a finished career returns "career_complete", otherwise the new desk is shown.
        // A consumed/replayed request no longer owns a pending desk action. Ignore
        // it before installing borrowed presenters or trying to reject that desk.
        if (!flow.desktop() || !flow.desktop()->pending() ||
            *flow.desktop()->pending() != result.desk_action || result.desk_action.revision != revision) return {};
        if (!session) {
            flow.reject_desk_action("turn_driver_unavailable");
            return "turn: no active career session";
        }
        // Scope guard: when the kickoff request ends (normally or by exception) all live match,
        // season and presentation hooks are cleared and the previously installed ones restored.
        struct LiveHookReset {
            CareerSession* session;
            TransferOfferPresenter previous_transfer;
            bool transfer_installed;
            std::function<void(const TransferDecisionEvent&)> previous_decision;
            bool decision_installed;
            PendingOfferHooks previous_pending;
            PendingOfferEntryPolicy previous_pending_policy;
            SportschauPresentationHooks previous_sportschau;
            bool contract_expiry_installed;
            bool loan_return_installed;
            bool loan_return_offer_installed;
            ~LiveHookReset() {
                if (!session) return;
                session->set_live_minute({});
                session->set_original_sound({});
                session->set_live_event({});
                session->set_live_ball({});
                session->set_live_reality({});
                session->set_phase_results({});
                session->set_live_chance({});
                session->set_live_shot({});
                session->set_live_set_piece({});
                session->set_live_booking({});
                session->set_live_shootout({});
                session->set_season_pause({});
                session->set_season_tax({});
                session->set_season_highscore({});
                session->set_season_promotion({});
                session->set_season_cup_draw({});
                session->set_pending_offer_hooks(std::move(previous_pending));
                session->set_pending_offer_entry_policy(previous_pending_policy);
                session->set_sportschau_presentation_hooks(std::move(previous_sportschau));
                if (contract_expiry_installed) session->set_contract_expiry_presenter({});
                if (loan_return_installed) session->set_loan_return_presenter({});
                if (loan_return_offer_installed) session->set_loan_return_offer_presenter({});
                if (transfer_installed) session->set_transfer_offer_selector(std::move(previous_transfer));
                if (decision_installed) session->set_transfer_decision_observer(std::move(previous_decision));
            }
        } reset{session.get(), session->state().transfer_offer_selector, bool(transfer_presenter),
                session->state().transfer_decision_observer, bool(transfer_decision_presenter),
                session->pending_offer_hooks(), session->pending_offer_entry_policy(),
                session->sportschau_presentation_hooks(), bool(contract_expiry_presenter),
                bool(loan_return_presenter), bool(loan_return_offer_presenter)};
        session->set_pending_offer_hooks(std::move(pending_offer_presenter));
        session->set_pending_offer_entry_policy(pending_entry_policy);
        if (contract_expiry_presenter)
            session->set_contract_expiry_presenter(std::move(contract_expiry_presenter));
        if (loan_return_presenter)
            session->set_loan_return_presenter(std::move(loan_return_presenter));
        if (loan_return_offer_presenter)
            session->set_loan_return_offer_presenter(std::move(loan_return_offer_presenter), market_sale_presenter);
        if (transfer_presenter) session->set_transfer_offer_selector(transfer_presenter);
        if (transfer_decision_presenter) {
            session->set_transfer_decision_observer([active = session.get(), &transfer_decision_presenter]
                    (const TransferDecisionEvent& event) {
                transfer_decision_presenter(active->state(), event);
            });
        }
        // Revision used by live interventions during the turn; the live dispatch lambdas below share it
        // by reference.
        uint64_t live_revision = revision;
        session->set_live_ball(std::move(live_ball_presenter));
        session->set_live_reality(std::move(live_reality_presenter));
        session->set_phase_results(std::move(phase_result_presenter));
        session->set_live_chance(std::move(live_chance_presenter));
        session->set_live_shot(std::move(live_shot_presenter));
        session->set_live_set_piece(std::move(live_set_piece_presenter));
        session->set_live_booking(std::move(live_booking_presenter));
        session->set_live_shootout(std::move(live_shootout_presenter));
        // Live minute hook: called by the session every match minute. It forwards the minute to the
        // plain presenter and, if set, to the intervention presenter together with callbacks that let
        // the UI change the lineup, tactics or pitch of the human team while the match runs.
        if (live_presenter || live_intervention_presenter) {
            // Hook installed on the session; called once per live minute with the engine, groups and state.
            session->set_live_minute([&live_presenter, &live_intervention_presenter, active = session.get(), &live_revision]
                    (int minute, MatchEngine& engine,
                    std::vector<MatchGroup>& groups, GameState& state) mutable {
                if (live_presenter) live_presenter(minute, groups, state);
                if (live_intervention_presenter) {
                    // Substitution callback: asks the session to apply a live lineup change for this minute;
                    // rejected when the session is gone.
                    const auto dispatch = [active, minute, &engine, &live_revision]
                            (const ui::LineupRequest& request) {
                        if (!active) return LiveSubstitutionResult{LiveSubstitutionStatus::rejected, 0, -1,
                            "Die Karrieresitzung ist nicht verfügbar."};
                        return active->dispatch_live_substitution_result(request, minute, 2,
                            engine, live_revision);
                    };
                    // Tactics callback: applies a live tactics change through the session; returns false on failure
                    // and fills the error text.
                    const auto dispatch_tactic = [active, minute, &engine, &live_revision]
                            (const ui::TacticsRequest& request, std::string& error) {
                        return active && active->dispatch_live_tactic(request, minute, 8,
                            engine, live_revision, error);
                    };
                    // Pitch callback: applies a live pitch change through the session; rejected when the session is
                    // gone.
                    const auto dispatch_pitch = [active, minute, &engine, &live_revision]
                            (const ui::PitchRequest& request) {
                        if (!active) return LiveSubstitutionResult{LiveSubstitutionStatus::rejected, 0, -1,
                            "Die Karrieresitzung ist nicht verfügbar."};
                        return active->dispatch_live_pitch_result(request, minute, 2,
                            engine, live_revision);
                    };
                    // Scope callback: opens (begin), commits (finish) or cancels (abort) a live intervention in the
                    // session for this minute, mode, manager and team.
                    const auto dispatch_scope = [active, minute, &engine, &live_revision]
                            (LiveInterventionBoundary boundary, int mode, int manager, int team,
                             std::string& error) {
                        if (!active) { error = "Die Karrieresitzung ist nicht verfügbar."; return false; }
                        switch (boundary) {
                        case LiveInterventionBoundary::begin:
                            return active->begin_live_intervention(minute, mode, manager, team,
                                engine, live_revision, error);
                        case LiveInterventionBoundary::finish:
                            return active->finish_live_intervention(minute, mode, manager, team,
                                engine, live_revision, error);
                        case LiveInterventionBoundary::abort:
                            active->abort_live_intervention(minute, mode, manager, team,
                                engine, live_revision);
                            return true;
                        }
                        return false;
                    };
                    live_intervention_presenter(minute, engine, groups, state, live_revision,
                        dispatch, dispatch_tactic, dispatch_pitch, dispatch_scope);
                }
            });
        }
        // Live event hook: called by the session for match events that allow an intervention. The same
        // kinds of callbacks as for the minute hook are offered, bound to the event instead of a minute.
        if (live_match_event_presenter) {
            session->set_live_event([&live_match_event_presenter, active = session.get(), &live_revision]
                    (const LiveMatchEvent& event, MatchEngine& engine,
                     std::vector<MatchGroup>& groups, GameState& state) {
                if (!active) return;
                // Substitution callback bound to the event's minute.
                const auto dispatch = [active, &event, &engine, &live_revision](const ui::LineupRequest& request) {
                    return active->dispatch_live_substitution_result(request, event.minute, 2, engine, live_revision);
                };
                // Tactics callback bound to the event's minute.
                const auto dispatch_tactic = [active, &event, &engine, &live_revision]
                        (const ui::TacticsRequest& request, std::string& error) {
                    return active && active->dispatch_live_tactic(request, event.minute, 8,
                        engine, live_revision, error);
                };
                // Pitch callback bound to the event's minute.
                const auto dispatch_pitch = [active, &event, &engine, &live_revision]
                        (const ui::PitchRequest& request) {
                    if (!active) return LiveSubstitutionResult{LiveSubstitutionStatus::rejected, 0, -1,
                        "Die Karrieresitzung ist nicht verfügbar."};
                    return active->dispatch_live_pitch_result(request, event.minute, 2, engine, live_revision);
                };
                // Scope callback for event interventions. Identity check: the scope must be mode 2 and carry
                // the manager slot and team of the event itself; anything else is refused with an error text,
                // so a presenter cannot start an intervention for a different manager or team.
                const auto scope = [active, &event, &engine, &live_revision]
                        (LiveInterventionBoundary boundary, int mode, int manager, int team, std::string& error) {
                    if (!active || mode != 2 || manager != event.manager_slot || team != event.team) {
                        error = "Der Live-Ereignis-Scope gehört nicht zum authentifizierten Manager und Team.";
                        return false;
                    }
                    switch (boundary) {
                    case LiveInterventionBoundary::begin:
                        return active->begin_live_event_intervention(event, engine, live_revision, error);
                    case LiveInterventionBoundary::finish:
                        return active->finish_live_event_intervention(event, engine, live_revision, error);
                    case LiveInterventionBoundary::abort:
                        active->abort_live_intervention(event.minute, 2, event.manager_slot, event.team,
                            engine, live_revision);
                        return true;
                    }
                    return false;
                };
                live_match_event_presenter(event, engine, groups, state, live_revision,
                    dispatch, dispatch_tactic, dispatch_pitch, scope);
            });
        }
        // Sportschau (results show) hooks are gated by the game's own options before being installed:
        // most parts only run when the intro-after-matchday option is set; intro and fixture views also
        // check per-competition settings stored in the game memory.
        if (sportschau_presenter.kickoff || sportschau_presenter.intro || sportschau_presenter.fixture ||
            sportschau_presenter.replay.step || sportschau_presenter.replay.scene || sportschau_presenter.awards ||
            sportschau_presenter.team_info || sportschau_presenter.winner) {
            const SportschauSourceOptions source_options = session->sportschau_source_options();
            SportschauPresentationHooks gated;
            gated.winner = sportschau_presenter.winner; // I7 is independent of EXE Sportschau/CareerSession_SportschauSourceOptions_ExecutableField.
            if (source_options.intro_after_matchday != 0) gated.kickoff = sportschau_presenter.kickoff;
            if (source_options.intro_after_matchday != 0) gated.replay = sportschau_presenter.replay;
            if (source_options.intro_after_matchday != 0) gated.awards = sportschau_presenter.awards;
            if (source_options.intro_after_matchday != 0) gated.team_info = sportschau_presenter.team_info;
            if (sportschau_presenter.intro) {
                // Intro wrapper: shows the intro once per matchday or once per group as the options demand, and
                // only if the memory flags allow it and at least one shown competition has its setting enabled.
                gated.intro = [source_options, &sportschau_presenter](const GameState& state,
                                                                       const SportschauIntroView& view) {
                    const bool once = view.group_index < 0 && source_options.intro_after_matchday != 0;
                    const bool per_group = view.group_index >= 0 && source_options.intro_each_group != 0;
                    if (!once && !per_group) return;
                    if (!state.memory.has(::bmh::layout::Controller_FormatError_RuntimeField) || state.memory.u8(::bmh::layout::Controller_FormatError_RuntimeField) != 0 ||
                        !state.memory.has(::bmh::layout::Controller_FormatError_RuntimeField2) || state.memory.u8(::bmh::layout::Controller_FormatError_RuntimeField2) != 0)
                        return;
                    const bool setting_visible = std::any_of(view.groups.begin(), view.groups.end(),
                        [&state](const SportschauCompetitionView& competition) {
                            if (competition.source_setting_index < 0 || competition.source_setting_index > 19)
                                return false;
                            const uint32_t setting = ::bmh::layout::Controller_FormatError_ExecutableField2 + uint32_t(competition.source_setting_index) * 9;
                            return state.memory.has(setting) && state.memory.u8(setting) == 0;
                        });
                    if (setting_visible) sportschau_presenter.intro(state, view);
                };
            }
            if (sportschau_presenter.fixture) {
                // Fixture wrapper: forwards only when the option and the competition's setting in the game
                // memory allow it.
                gated.fixture = [source_options, &sportschau_presenter](const GameState& state,
                                                                          const SportschauFixtureView& fixture) {
                    if (source_options.intro_after_matchday == 0 || fixture.source_setting_index < 0 ||
                        fixture.source_setting_index > 19)
                        return;
                    const uint32_t setting = ::bmh::layout::Controller_FormatError_ExecutableField2 + uint32_t(fixture.source_setting_index) * 9;
                    if (!state.memory.has(setting) || state.memory.u8(setting) != 0 ||
                        !state.memory.has(::bmh::layout::Controller_FormatError_RuntimeField3) || state.memory.u8(::bmh::layout::Controller_FormatError_RuntimeField3) != 0)
                        return;
                    sportschau_presenter.fixture(state, fixture);
                };
            }
            session->set_sportschau_presentation_hooks(std::move(gated));
        }
        session->set_season_pause(std::move(season_presenter));
        session->set_season_tax(std::move(tax_presenter));
        session->set_season_highscore(std::move(highscore_presenter));
        session->set_season_promotion(std::move(promotion_presenter));
        session->set_season_cup_draw(std::move(cup_draw_presenter));
        playback.set_original_options(original_audio_options(session->state(), music_enabled));
        // Sound hook: plays the original match sound cue the session asks for.
        session->set_original_sound([&playback](uint8_t id, const GameState& state) {
            original_match_audio_command(playback, state, id);
        });
        // Runs the turn; the result tells whether it failed, is waiting at the desk, finished the
        // career or arrived at the next desk.
        const auto advanced = session->dispatch_kickoff(result.desk_action, revision, error);
        start_original_desk_audio(playback, session->state(), music_enabled);
        // Debug mode only: logs the outcome and, if the turn did not fail, writes one debug savegame in
        // the debug folder after every match day.
        if (app::debug::enabled()) {
            // -debug: one savegame after every match day, in the debug folder (DBG00001.FBM, ...).
            app::debug::log("kickoff handled: " + std::string(advanced.kind == TurnDeskKind::Failed ? "failed" : "ok") +
                            ", date counter " + std::to_string(session->state().date) + ", revision " + std::to_string(revision) +
                            (error.empty() ? std::string() : ", error " + error));
            if (advanced.kind != TurnDeskKind::Failed) {
            const auto path = app::debug::next_save_path();
            std::string save_error;
            const int slot = int(session->state().memory.u8(::bmh::layout::ActiveManager));
            const bool saved = session->save_game(path, slot, true, save_error);
            app::debug::log(saved ? "debug savegame " + path.filename().string()
                                  : "debug savegame failed: " + save_error);
            }
        }
        if (advanced.kind == TurnDeskKind::Failed || advanced.kind == TurnDeskKind::WaitingAtDesk) {
            flow.reject_desk_action(error.empty() ? advanced.reason : error);
            return {};
        }
        // Source DailyInterest returns before the opening desk when duration reaches0.
        // Its completed state remains owned until the app closes the final screens.
        if (advanced.kind == TurnDeskKind::CareerComplete) return "career_complete";
        // Original cues run at their presentation boundary. Do not replay late
        // synthetic kickoff/goal/full-time summaries after the synchronous turn.
        flow.acknowledge_desk(session->desktop_projection(revision));
        flow.attach_preparation_context(session->preparation_projection(revision));
        // The Source matchday already owns its live/Sportschau/table presenters.
        // Keep the completed snapshot for queries; an extra generic result modal
        // has no Original caller and blocks the following desk (UI #69).
    } else if (result.request == ui::FlowRequest::settings_file_action) {
        // Settings file (load or save of the settings file in the save directory): the request must be
        // the pending one, belong to the current settings owner (sequence, desk item and manager slot),
        // carry the current revision and point into the save directory with the .ein extension;
        // otherwise it is rejected as stale.
        const auto* pending = flow.pending_file();
        const auto& request = result.file_action;
        const auto& desk_owner = flow.settings_desk_owner();
        const bool same_pending = pending && *pending == request &&
            request.file_kind == ui::SaveLoadFileKind::settings &&
            result.settings_owner_sequence == flow.settings_owner_sequence() &&
            request.origin_item_id == desk_owner.item_id && request.manager_slot == desk_owner.manager_slot;
        if (!session || !same_pending || request.revision != revision ||
            !save_path_in_directory(request.path, save_directory, ".ein")) {
            flow.reject_settings_file("stale_request_or_invalid_settings_path");
            return {};
        }
        try {
            // Callback after the settings file was handled: acknowledges settings, desk and preparation.
            const auto dispatched = DeskSettingsActions(*session).file(result.settings_owner_sequence, request,
                revision, error, [&](CareerSession::DeskSettingsDispatch& applied) {
                    auto desk = session->desktop_projection(revision);
                    auto preparation = session->preparation_projection(revision);
                    flow.acknowledge_settings_file(std::move(applied.state), std::move(desk), std::move(preparation));
                });
            if (!dispatched) {
                flow.reject_settings_file(error.empty() ? "settings_file_operation_rejected" : error);
                return {};
            }
        } catch (const std::exception&) {
            flow.reject_settings_file("settings_file_projection_or_ack_failed");
        }
    } else if (result.request == ui::FlowRequest::file_action &&
               result.file_action.file_kind == ui::SaveLoadFileKind::menu_layout) {
        // Menu layout file (.men): the request must equal the pending file action field by field; if
        // it does not, it is ignored (and rejected if one is pending). Revision and path (inside the
        // save directory) are checked before MenuLayoutActions applies it.
        const auto& request = result.file_action;
        const auto* pending = flow.pending_file();
        const bool same_pending = pending && pending->action == request.action &&
            pending->manager_slot == request.manager_slot && pending->revision == request.revision &&
            pending->path == request.path && pending->overwrite == request.overwrite &&
            pending->file_kind == request.file_kind && pending->origin_item_id == request.origin_item_id;
        if (!session || !same_pending) {
            if (pending) flow.reject_file_action("menu_layout_request_not_owned");
            return {};
        }
        if (request.revision != revision ||
            !save_path_in_directory(request.path, save_directory, ".men")) {
            flow.reject_file_action("stale_request_or_invalid_menu_layout_path");
            return {};
        }
        try {
            if (!MenuLayoutActions(*session).dispatch(request, revision, error, [&] {
                auto desk = session->desktop_projection(revision);
                auto preparation = session->preparation_projection(revision);
                flow.acknowledge_menu_layout(std::move(desk), std::move(preparation));
            })) {
                flow.reject_file_action(error);
                return {};
            }
        } catch (const std::exception&) {
            flow.reject_file_action("menu_layout_projection_or_ack_failed");
        }
    } else if (result.request == ui::FlowRequest::file_action) {
        // Savegame file action. Revision and path (inside the save directory) are checked first.
        // Load: for a running game the request's manager must be the active manager; a new session
        // is built from the file and replaces the old one with the next revision. Save: only the
        // active manager can save; the revision grows by one after a successful write.
        const auto& request = result.file_action;
        if (request.revision != revision || !save_path_in_directory(request.path, save_directory)) {
            flow.reject_file_action("stale_request_or_invalid_save_path");
            return {};
        }
        if (request.action == ui::SaveLoadAction::load) {
            if (request.manager_slot >= 0 && (!session ||
                request.manager_slot != int(session->state().memory.u8(::bmh::layout::ActiveManager)))) {
                flow.reject_file_action("stale_manager");
                return {};
            }
            try {
                // Load: the new session is built and projected completely before it replaces the current one,
                // so a failed load leaves the running game untouched.
                auto loaded = std::make_unique<app::CareerSession>(data, seed, request.path);
                if (enable_autosave) loaded->configure_autosave_directory(save_directory);
                const uint64_t loaded_revision = revision + 1;
                auto desk = loaded->desktop_projection(loaded_revision);
                auto preparation = loaded->preparation_projection(loaded_revision);
                flow.attach_loaded_game(std::move(desk), std::move(preparation));
                playback.reset_session(loaded->session_id());
                audio_session = loaded->session_id();
                session = std::move(loaded); revision = loaded_revision;
                session->configure_sound_capabilities(playback.original_ready() && playback.available(),
                    playback.original_ready() && playback.available());
                start_original_desk_audio(playback, session->state(), music_enabled);
            } catch (const std::exception&) {
                flow.reject_file_action("load_failed_or_unsupported_save");
            }
            return {};
        }
        if (!session || request.manager_slot < 0 || request.manager_slot != int(session->state().memory.u8(::bmh::layout::ActiveManager))) {
            flow.reject_file_action("save_requires_the_active_manager");
            return {};
        }
        if (!session->save_game(request.path, request.manager_slot, request.overwrite, error)) {
            flow.reject_file_action(error);
            return {};
        }
        const uint64_t saved_revision = revision + 1;
        revision = saved_revision;
        try {
            auto desk = session->desktop_projection(saved_revision);
            auto preparation = session->preparation_projection(saved_revision);
            flow.acknowledge_saved_game(std::move(desk), std::move(preparation));
        } catch (const std::exception&) {
            flow.reject_file_action("save_written_but_projection_failed");
        }
    } else if (result.request == ui::FlowRequest::mailbox_closed) {
        // Mailbox closed: only the pending close request is accepted; MailboxActions applies it and
        // the desk is acknowledged.
        const auto* pending = flow.pending_mailbox_close();
        if (!session || !pending || *pending != result.mailbox_close) return {};
        try {
            if (!MailboxActions(*session).close(result.mailbox_close, revision, error, [&] {
                auto desk = session->desktop_projection(revision);
                auto preparation = session->preparation_projection(revision);
                flow.acknowledge_mailbox_close(result.mailbox_close, std::move(desk), std::move(preparation));
            })) {
                flow.reject_mailbox_close(error.empty() ? "Die Nachrichtenansicht ist veraltet." : error);
                return {};
            }
        } catch (const std::exception&) {
            flow.reject_mailbox_close("Die Nachrichtenansicht konnte nicht geschlossen werden.");
        }
    } else if (result.request == ui::FlowRequest::mailbox_action) {
        // Mailbox action (read, delete and so on): must equal the pending mailbox request; the session
        // applies it and the rebuilt mailbox state is acknowledged.
        // A consumed/replayed or forged event cannot write through a new modal request.
        if (!session || !flow.pending_mailbox() || *flow.pending_mailbox() != result.mailbox_action)
            return {};
        try {
            if (!MailboxActions(*session).dispatch(result.mailbox_action, revision, error, [&] {
                auto state = ui::mailbox_state_from_game(session->state(), result.mailbox_action.manager_slot, revision);
                auto preparation = session->preparation_projection(revision);
                flow.acknowledge_mailbox(std::move(state), std::move(preparation));
            })) {
                flow.reject_mailbox(error);
                return {};
            }
        } catch (const std::exception&) {
            flow.reject_mailbox("Diese Postfachaktion konnte nicht übernommen werden.");
        }
    } else if (result.request == ui::FlowRequest::settings_action) {
        // Settings change: must equal the pending request and belong to the current settings owner
        // sequence. If the session refuses and the refusal has an original notice text, that notice is
        // shown through the presenter before the request is rejected.
        if (!session || !flow.pending_settings() || *flow.pending_settings() != result.settings_action ||
            result.settings_owner_sequence != flow.settings_owner_sequence())
            return {};
        std::optional<CareerSession::DeskSettingsDispatch> dispatched;
        try {
            dispatched = DeskSettingsActions(*session).write(result.settings_owner_sequence, result.settings_action,
                revision, error, [&](CareerSession::DeskSettingsDispatch& applied) {
                    auto desk = session->desktop_projection(revision);
                    auto preparation = session->preparation_projection(revision);
                    flow.acknowledge_settings(std::move(applied.state), std::move(desk), std::move(preparation));
                });
        } catch (const std::exception&) {
            flow.reject_settings("Diese Einstellung konnte nicht übernommen werden.");
            return {};
        }
        if (!dispatched) {
            const auto notice = ui::settings_source_refusal(session->state(), result.settings_action, revision);
            if (notice && environment_notice_presenter &&
                error == ui::original_to_utf8(message_text(session->state(), *notice))) {
                auto view = ui::environment_notice_dialog(session->state(), result.settings_action.manager_slot,
                    message_text(session->state(), *notice));
                const bool shown = environment_notice_presenter(session->state(), std::move(view), flow.render());
                flow.reject_settings({});
                return shown ? std::string{} : "settings_source_notice_aborted";
            }
            flow.reject_settings(error);
            return {};
        }
    } else if (result.request == ui::FlowRequest::settings_close) {
        // Settings closed: the owner sequence must still match; DeskSettingsActions closes the owner
        // and the desk is acknowledged.
        if (!session || result.settings_owner_sequence != flow.settings_owner_sequence()) return {};
        const auto& desk_request = flow.settings_desk_owner();
        try {
            const auto dispatched = DeskSettingsActions(*session).close(flow.settings_owner_sequence(),
                flow.settings_owner_revision(), desk_request, revision, error,
                [&](CareerSession::DeskSettingsDispatch& applied) {
                    auto desk = session->desktop_projection(revision);
                    auto preparation = session->preparation_projection(revision);
                    flow.acknowledge_settings_closed(std::move(applied.state), std::move(desk), std::move(preparation));
                });
            if (!dispatched) { flow.reject_settings_close(error.empty() ? "Einstellungen konnten nicht geschlossen werden." : error); return {}; }
        } catch (const std::exception&) {
            flow.reject_settings_close("Einstellungen konnten nicht geschlossen werden.");
        }
    } else if (result.request == ui::FlowRequest::desk_action && session &&
               result.desk_action.kind == ui::DeskRequestKind::open_menu &&
               (result.desk_action.menu_kind == 4 || result.desk_action.menu_kind == 6 || result.desk_action.menu_kind == 7 || result.desk_action.menu_kind == 8 || result.desk_action.menu_kind == 9 || result.desk_action.menu_kind == 24 || result.desk_action.menu_kind == 27 || result.desk_action.menu_kind == 63 || result.desk_action.menu_kind == 19 || result.desk_action.menu_kind == 21)) {
        // Read-only desk menus (4 achievements, 6 friendly matches, 7 and 8 calendars, 9 indoor
        // standings, 19 settings, 21 advertising, 24 environment, 27 contracts, 63 mailbox, others
        // settings): the request must equal the desk's pending action at the current revision for the
        // active manager, otherwise it is rejected as stale. The page is built from the session and
        // shown; a failure rejects the desk action and writes the reason to stderr.
        const auto& request = result.desk_action;
        if (!flow.desktop() || !flow.desktop()->pending() || request.revision != revision ||
            request.manager_slot != int(session->state().memory.u8(::bmh::layout::ActiveManager)) ||
            ui::desk_request_json(request) != ui::desk_request_json(*flow.desktop()->pending())) {
            if (flow.desktop() && flow.desktop()->pending()) flow.reject_desk_action("stale_readonly_navigation");
            return {};
        }
        try {
            if (request.menu_kind == 4) {
                const auto& game=session->state();
                if(!ui::achievements_available(game)) {
                    if(!environment_notice_presenter)throw FormatError("achievements: Original first-match notice presenter unavailable");
                    const auto view=ui::environment_notice_dialog(game,request.manager_slot,message_text(game,::bmh::layout::Controller_FormatError_TextPointer));
                    const Image backdrop=flow.desktop()->render();
                    flow.acknowledge_desk_notice(request);
                    if(!environment_notice_presenter(game,view,backdrop))
                        throw FormatError("achievements: Source first-match notice stopped");
                } else {
                    const auto exe=load_le(ui::asset_path(data,"BMH.exe"));
                    auto state=ui::achievements_from_game(game,exe,request,flow.desktop()->render());
                    flow.show_achievements(std::move(state),request);
                }
            } else if (request.menu_kind == 7 || request.menu_kind == 8) {
                const auto& game = session->state();
                const auto& desk = flow.desktop()->state();
                const int manager = request.manager_slot;
                const size_t record = size_t(manager) * 0xb0c;
                const auto live = session->desktop_projection(revision);
                // Calendar identity check: the manager's record must point to the manager's current team and a
                // fresh desk projection must equal the desk on screen (manager, club, day counter, season,
                // options and so on); otherwise the calendar is refused as stale.
                if (session->career_complete() || manager < 0 || manager > 3 ||
                    size_t(manager) >= game.manager_teams.size() || record + 0x291 >= game.manager_records.size() ||
                    (uint16_t(game.manager_records[record + 0x290]) |
                     uint16_t(game.manager_records[record + 0x291]) << 8) != game.manager_teams[size_t(manager)] ||
                    live.manager_slot != desk.manager_slot || live.runtime_club != desk.runtime_club ||
                    live.manager_teams != desk.manager_teams || live.managers != desk.managers ||
                    live.day_counter != desk.day_counter || live.season_day != desk.season_day ||
                    live.season_index != desk.season_index || live.national_team != desk.national_team ||
                    live.badge != desk.badge || live.manager_name != desk.manager_name || live.options != desk.options)
                    throw FormatError("calendar: stale manager or season context");
                const auto executable = load_le(ui::asset_path(data, "BMH.exe"));
                auto calendar = request.menu_kind == 7
                    ? ui::reschedule_calendar_from_game(game, executable, manager)
                    : ui::calendar_from_game(game, executable);
                flow.show_calendar(std::move(calendar), request);
            } else if (request.menu_kind == 9) {
                flow.show_indoor_standings(ui::indoor_standings_from_game(session->state(),
                    load_le(ui::asset_path(data, "BMH.exe")),
                    load_textd(ui::asset_path(data, "TEXTD.BLK")), request), request);
            } else if (request.menu_kind == 63) {
                flow.show_mailbox(ui::mailbox_state_from_game(session->state(), request.manager_slot, revision));
            } else if (request.menu_kind == 21) {
                flow.show_advertising(ui::advertising_from_game(session->state(), request.manager_slot, revision));
            } else if (request.menu_kind == 24) {
                flow.show_environment(ui::environment_from_game(session->state(), revision), request);
            } else if (request.menu_kind == 27) {
                session->clear_contract_quote();
                flow.show_contracts(ui::contracts_from_game(session->state(), request.manager_slot, revision));
            } else if (request.menu_kind == 6) {
                const auto& game = session->state();
                const auto& desk = flow.desktop()->state();
                // Friendly menu: for the national-coach desk (team slot 0x165) the nation picker is shown;
                // for club managers the friendly opponent table is started in the session.
                const int manager=request.manager_slot;
                if(manager>=0&&size_t(manager)<game.manager_teams.size()&&
                   game.manager_teams[size_t(manager)]==0x165) {
                    const auto projection=ui::world_cup_statistics_from_game(game,
                        load_le(ui::asset_path(data,"BMH.exe")),
                        ui::WorldCupStatisticsOwner::friendly_match_nation_picker);
                    flow.show_national_friendly_picker(projection,request,0x165);
                    return {};
                }
                LeagueFamilyActions(*session).apply(revision, [&] {
                    const auto identity=session->begin_friendly_opponent_table(revision,error);
                    if(!identity)throw FormatError(error.empty()?"friendly table entry rejected":error);
                    const auto table=session->current_league_table_dispatch(error);
                    if(!table||table->identity!=*identity)throw FormatError(error.empty()?"friendly table page missing":error);
                    show_table_dispatch(*table,{},true);
                    return true;
                });
            } else if (request.menu_kind == 19) {
                auto opened = DeskSettingsActions(*session).open(request, revision, error,
                    [&](CareerSession::DeskSettingsProjection& applied) {
                        flow.show_settings(std::move(applied.state), applied.owner.sequence, request);
                    });
                if (!opened) throw FormatError(error.empty() ? "settings_owner_rejected" : error);
            } else {
                flow.show_settings(session->settings_projection(request.manager_slot, revision));
            }
        } catch (const std::exception& e) {
            flow.reject_desk_action("Diese Ansicht konnte nicht geladen werden.");
            std::fprintf(stderr,"desk menu %d: %s\n",request.menu_kind,e.what());
        }
    } else if (result.request == ui::FlowRequest::reschedule_action && session) {
        // Reschedule action (move a match in the calendar): the request must equal the pending one and
        // be owned by the open calendar (menu 7) at the current revision for the active manager.
        // RescheduleActions applies it; a day-full or team-clash refusal is explained with the original
        // notice dialog.
        const auto* pending = flow.pending_reschedule_action();
        const auto* screen = flow.calendar();
        const auto& request = result.reschedule_action;
        if (!pending || !screen || *pending != request || request.owner != screen->owner() ||
            request.owner.menu_kind != 7 || request.owner.kind != ui::DeskRequestKind::open_menu ||
            request.owner.revision != revision || request.owner.manager_slot != int(session->state().memory.u8(::bmh::layout::ActiveManager)) ||
            screen->state().reschedule_manager != request.owner.manager_slot || revision == UINT64_MAX) {
            if (pending) flow.reject_reschedule();
            return "stale_reschedule_request";
        }
        RescheduleStatus status=RescheduleStatus::invalid_match;
        try {
            status=RescheduleActions(*session).dispatch(request,revision,[&] {
                const auto executable=load_le(ui::asset_path(data,"BMH.exe"));
                auto calendar=ui::reschedule_calendar_from_game(session->state(),executable,request.owner.manager_slot);
                auto owner=request.owner;owner.revision=revision;
                auto desktop=session->desktop_projection(revision);
                auto preparation=session->preparation_projection(revision);
                flow.acknowledge_reschedule(std::move(calendar),owner,std::move(desktop),std::move(preparation));
            });
        } catch(const std::exception&) {
            if(flow.pending_reschedule_action())flow.reject_reschedule();
            return "reschedule_projection_failed";
        }
        if (status != RescheduleStatus::moved) {
            if (status == RescheduleStatus::day_full || status == RescheduleStatus::team_clash) {
                if (environment_notice_presenter) {
                    const auto body = message_text(session->state(), ::bmh::layout::Controller_CalendarFromGame_TextPointer);
                    auto notice = ui::environment_notice_dialog(session->state(), request.owner.manager_slot, body);
                    const bool presented = environment_notice_presenter(session->state(), std::move(notice), screen->render());
                    flow.reject_reschedule();
                    return presented ? std::string{} : "reschedule_source_notice_aborted";
                }
            }
            flow.reject_reschedule();
            return {};
        }
    } else if (result.request == ui::FlowRequest::environment_action && session) {
        // Environment (stadium surroundings) change: the pending request must match the screen's
        // owner, club, manager slot and revision, and the manager must be the active one with that club;
        // else it is rejected as stale. EnvironmentActions applies it, showing the original notice
        // through the presenter first.
        const auto* pending = flow.pending_environment();
        const auto* screen = flow.environment();
        const auto& request = result.environment_action;
        if (!pending || !screen || *pending != request ||
            ui::environment_request_json(*pending) != ui::environment_request_json(request) ||
            request.kind != ui::EnvironmentRequestKind::set_item || request.owner.kind != ui::DeskRequestKind::open_menu ||
            request.owner.menu_kind != 24 || request.manager_slot < 0 || request.manager_slot > 3 ||
            request.manager_slot != int(session->state().memory.u8(::bmh::layout::ActiveManager)) ||
            request.runtime_club != screen->state().runtime_club ||
            request.manager_slot != screen->state().manager_slot || request.revision != revision ||
            request.owner.revision != request.revision || request.owner.manager_slot != request.manager_slot ||
            screen->state().revision != revision || screen->state().owner != request.owner ||
            size_t(request.manager_slot) >= session->state().manager_teams.size() ||
            session->state().manager_teams[size_t(request.manager_slot)] != request.runtime_club) {
            if (flow.pending_environment()) flow.reject_environment_action({});
            return "stale_environment_owner";
        }
        try {
            const auto outcome=EnvironmentActions(*session).set(request,revision,error,
                // Notice callback: shows the original notice for the staged state; returning false aborts.
                [&](const GameState& staged,const std::string& message) {
                    if(!environment_notice_presenter) {
                        error="environment_source_notice_presenter_unavailable";
                        return false;
                    }
                    auto notice=ui::environment_notice_dialog(staged,request.manager_slot,message);
                    if(!environment_notice_presenter(staged,std::move(notice),screen->render())) {
                        error="environment_source_notice_aborted";
                        return false;
                    }
                    return true;
                // Acknowledge callback: rebuilds the environment view, with the new revision and preparation
                // state only if something changed.
                },[&](bool changed) {
                    auto view=ui::environment_from_game(session->state(),revision);
                    view.owner=request.owner;
                    if(changed) {
                        view.owner.revision=revision;
                        flow.acknowledge_environment(std::move(view),session->preparation_projection(revision));
                    } else flow.acknowledge_environment(std::move(view));
                });
            if(!outcome) {
                flow.reject_environment_action({});
                return error;
            }
            if(*outcome==EnvironmentResult::invalid)
                throw FormatError("environment request is outside the Source tile range");
        } catch(const std::exception& e) {
            if(flow.pending_environment())flow.reject_environment_action({});
            return e.what();
        }
    } else if (result.request == ui::FlowRequest::advertising_action && session) {
        // Advertising action: must equal the pending request, otherwise it is rejected as outdated.
        // The session applies it (for a renewal check the question is shown through the presenter) and
        // the advertising view, desk and preparation are acknowledged.
        const auto* pending = flow.pending_advertising_action();
        if (!pending) return {};
        if (*pending != result.advertising_action) {
            flow.reject_advertising_action("Dieses Werbeangebot ist nicht mehr aktuell.");
            return {};
        }
        const ui::AdvertisingRequest request = *pending;
        error.clear();
        try {
            CareerSession::AdvertisingRenewalPresenter presenter;
            if (request.kind == ui::AdvertisingActionKind::check_renewal && advertising_renewal_presenter) {
                const Image backdrop = flow.render();
                // Renewal presenter: shows the renewal question over the current screen.
                presenter = [&, backdrop](const GameState& staged, const AdvertisingRenewal& question) {
                    return advertising_renewal_presenter(staged, question, backdrop);
                };
            }
            // Acknowledge callback: rebuilds the advertising view and adds the original notification text
            // when a spending change produced one.
            const auto acknowledge = [&](const CareerSession::AdvertisingActionResult& applied) {
                auto state = ui::advertising_from_game(session->state(), request.manager_slot, revision);
                if (request.kind == ui::AdvertisingActionKind::set_spending && applied.spending.message_address != 0) {
                    const auto& game = session->state();
                    if (!game.texts || applied.spending.message_address < ::bmh::layout::TextPointerTable)
                        throw FormatError("advertising: missing original notification text");
                    state.notification_text = ui::original_to_utf8(
                        (*game.texts)[(applied.spending.message_address - ::bmh::layout::TextPointerTable) / 4]);
                    state.notification_warning = applied.spending.status == AdvertisingSpendingStatus::insufficient_funds;
                }
                auto desk = session->desktop_projection(revision);
                auto preparation = session->preparation_projection(revision);
                flow.acknowledge_advertising(std::move(state), std::move(desk), std::move(preparation));
            };
            if (!session->apply_advertising(request, revision, error, acknowledge, presenter))
                flow.reject_advertising_action(error.empty()
                    ? (request.kind == ui::AdvertisingActionKind::check_renewal
                        ? "Die Werbeverlängerung wurde abgelehnt." : "Das Werbeangebot wurde abgelehnt.") : error);
        } catch (const std::exception&) {
            flow.reject_advertising_action("Die Werbeänderung konnte nicht bestätigt werden.");
        }
    } else if (result.request == ui::FlowRequest::contracts_closed && session) {
        // Contracts screen closed: drops the stored contract quote.
        session->clear_contract_quote();
    } else if (result.request == ui::FlowRequest::contract_action && session) {
        // Contract action (demands, offer, bonuses, insurance): must equal the pending request. The
        // session applies it; the callback then rebuilds the contracts view and acknowledges it in the
        // form that matches the action kind.
        const auto* pending = flow.pending_contract_action();
        if (!pending) return {};
        if (*pending != result.contract_action) {
            flow.reject_contract_action("Diese Vertragsaktion ist nicht mehr aktuell.");
            return {};
        }
        const ui::ContractRequest request = *pending;
        ui::ContractActionResult action;
        error.clear();
        try {
            if (!ContractActions(*session).dispatch(request, revision, action, error, [&] {
                auto state = ui::contracts_from_game(session->state(), request.manager_slot, revision);
                auto desk = session->desktop_projection(revision);
                auto preparation = session->preparation_projection(revision);
                if (request.kind == ui::ContractActionKind::request_demands) {
                    state.selected_row = request.row;
                    state.quote_row = request.row;
                    state.quote_id = action.quote_id;
                    state.demands = action.demands;
                    flow.acknowledge_contract_quote(std::move(state), std::move(desk), std::move(preparation));
                } else {
                    state.selected_row = request.row;
                    state.feedback = action.feedback;
                    if (request.kind == ui::ContractActionKind::insure_player) {
                        state.quote_row = request.row;
                        state.quote_id = action.quote_id;
                        state.demands = action.demands;
                    }
                    if (request.kind == ui::ContractActionKind::insure_player)
                        flow.acknowledge_contract_insurance(std::move(state), std::move(desk), std::move(preparation));
                    else if (request.kind == ui::ContractActionKind::submit_bonuses)
                        flow.acknowledge_contract_bonuses(std::move(state), std::move(desk), std::move(preparation));
                    else
                        flow.acknowledge_contract_offer(std::move(state), std::move(desk), std::move(preparation));
                }
            })) {
                flow.reject_contract_action(error.empty() ? "Der Vertragsauftrag wurde abgelehnt." : error);
                return {};
            }
        } catch (const std::exception&) {
            flow.reject_contract_action("Die Vertragsänderung konnte nicht bestätigt werden.");
        }
    } else if (result.request == ui::FlowRequest::desk_action && session &&
               (result.desk_action.kind == ui::DeskRequestKind::change_ticket_price ||
                result.desk_action.kind == ui::DeskRequestKind::change_option)) {
        // Desk widgets (ticket price and options): dispatched to the session; the desk is acknowledged
        // with a fresh projection or the action rejected.
        if (!session->dispatch_desk_widget(result.desk_action, revision, error)) {
            flow.reject_desk_action(error);
            return {};
        }
        flow.acknowledge_desk(session->desktop_projection(revision));
        flow.attach_preparation_context(session->preparation_projection(revision));
    } else if (result.request == ui::FlowRequest::submenu_action) {
        // Fallback for sub-menu requests without an engine adapter.
        // Every pending request needs an ACK or a rejection. Until its engine
        // adapter exists, the screen must remain usable after this action.
        flow.reject_preparation_action("Diese Aktion ist noch nicht verfügbar.");
    } else if (result.request == ui::FlowRequest::desk_action) {
        // Remaining desk actions: a layout change is dispatched to the session; every other desk menu
        // is rejected as not available.
        if (result.desk_action.kind == ui::DeskRequestKind::change_layout) {
            std::string error;
            if (!session->dispatch_desktop_layout(result.desk_action, revision, error)) {
                flow.reject_desk_action(error);
                return {};
            }
            flow.acknowledge_desk(session->desktop_projection(revision));
            flow.attach_preparation_context(session->preparation_projection(revision));
            return {};
        }
        flow.reject_desk_action("Dieses Menü ist noch nicht verfügbar.");
    }
    return {};
}

// Convenience overload without presenters: forwards to the full version with empty hooks and the
// legacy (unpresented) policies; only the national friendly question presenter is passed on.
std::string serve_flow_request(ui::StartupFlow& flow,const ui::FlowResult& result,
        std::unique_ptr<CareerSession>& session,const fs::path& data,const fs::path& save_directory,
        uint32_t seed,uint64_t& revision,openbmh::audio::Playback& playback,
        std::optional<uint64_t>& audio_session,bool music_enabled,
        NationalFriendlyQuestionPresenter national_presenter) {
    return serve_flow_request(flow,result,session,data,save_directory,seed,revision,playback,
        audio_session,music_enabled,{}, {}, {}, {}, {}, {}, {}, {}, {},
        PendingOfferEntryPolicy::legacy_unpresented, {},
        HumanBuyerMarketEntryPolicy::legacy_unpresented, {}, {}, {}, {}, {}, {}, {}, {}, {},
        std::move(national_presenter));
}

} // namespace bmh::app
