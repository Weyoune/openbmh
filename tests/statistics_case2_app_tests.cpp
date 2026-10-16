// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include "app/controller.hpp"
#include "game/save.hpp"
#include "game/messages.hpp"
#include "ui/world_cup_statistics.hpp"

using namespace bmh;
using namespace bmh::ui;

namespace {
void check(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }

void test_national_friendly_calendar_deadline(const std::filesystem::path& data,const std::filesystem::path& fixture) {
    for(int kind=0;kind<3;++kind) {
        const auto saves=std::filesystem::temp_directory_path()/("openbmh-friendly-calendar-deadline-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(saves);
        std::filesystem::copy_file(fixture,saves/"AUTOSAVE.FBM");
        StartupFlow flow(data,saves);std::unique_ptr<app::CareerSession> session;
        uint64_t revision=0;openbmh::audio::Playback playback;std::optional<uint64_t> audio;
        const app::NationalFriendlyQuestionPresenter accept_question=[](const GameState&,TaxDialogProjection,const Image&) {
            return std::optional<bool>{true};
        };
        const auto serve=[&](const FlowResult& action) {
            const auto error=app::serve_flow_request(flow,action,session,data,saves,0x12345678,
                revision,playback,audio,false,accept_question);
            if(!error.empty())throw std::runtime_error(error);
        };
        flow.click(214,215);serve(flow.send_key("ENTER"));
        const auto open_calendar=[&] {
            const auto& items=flow.desktop()->state().items;
            const auto item=std::find_if(items.begin(),items.end(),[](const DeskItem& i){return i.kind==6&&!i.disabled;});
            check(item!=items.end(),"normal national fixture exposes its friendly caller");
            serve(flow.click(item->x+item->width/2,item->y+item->height/2));
            const auto projection=world_cup_statistics_from_game(session->state(),load_le(asset_path(data,"BMH.exe")),
                WorldCupStatisticsOwner::friendly_match_nation_picker);
            const auto cell=projection.flag_cells[1];
            serve(flow.pointer_down(cell.x+1,cell.y+1));
            check(flow.friendly_calendar_quote()!=nullptr,"real Source questions lead to an owned normal calendar");
            check(flow.click(cell.x+1,cell.y+1).request==FlowRequest::none && flow.friendly_calendar_quote(),
                  "opponent selection UP is consumed before calendar input");
        };
        open_calendar();
        const auto quote=*flow.friendly_calendar_quote();
        const auto before=save_stream(session->state());const auto w=session->rand();const auto g=session->game();
        const auto before_revision=revision;
        if(kind==2) {
            FlowResult booking;
            const auto exe=load_le(asset_path(data,"BMH.exe"));
            for(const auto& date:quote.dates)if(date.mark==FriendlyDateMark::available || date.mark==FriendlyDateMark::available_flagged) {
                const auto cell=friendly_calendar_cell(exe,session->state().season_start,date.season_day);
                booking=flow.click(cell.x+cell.width/2,cell.y+cell.height/2);
                if(booking.request==FlowRequest::friendly_action)break;
            }
            check(booking.request==FlowRequest::friendly_action && flow.pending_friendly_action(),
                  "normal Source calendar supplies a genuine pending booking command");
            const auto pending=flow.state_json();
            check(!flow.process_timer_expired().changed && flow.state_json()==pending &&
                  flow.pending_friendly_action() && *flow.pending_friendly_action()==booking.friendly_action,
                  "calendar deadline cannot steal a booking ACK or invent a second request");
        } else {
            const auto closed=kind==0?flow.click(0,0,true):flow.process_timer_expired();
            check(closed.request==FlowRequest::submenu_closed && flow.screen_name()=="desktop" &&
                  !flow.friendly_calendar_quote() && !flow.pending_friendly_action(),
                  "manual No and calendar event8 return without booking a date");
            serve(closed);
        }
        check(save_stream(session->state())==before && revision==before_revision &&
              session->rand().seed()==w.seed() && session->rand().calls()==w.calls() &&
              session->game().seed()==g.seed() && session->game().calls()==g.calls(),
              "unbooked close or pending guard preserves every Native byte and complete W/G checkpoints");
        if(kind!=2) {
            open_calendar();
            check(flow.friendly_calendar_quote()->request_id!=quote.request_id,
                  "reopened calendar receives a new Source frame owner even at the same desk revision");
        }
        std::filesystem::remove_all(saves);
        std::printf("PASS: friendly calendar deadline kind=%d\n",kind);
    }
}

void test_national_picker_deadline(const std::filesystem::path& data,const std::filesystem::path& fixture) {
    for(int kind=0;kind<3;++kind) {
        const auto saves=std::filesystem::temp_directory_path()/("openbmh-national-picker-deadline-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(saves);
        std::filesystem::copy_file(fixture,saves/"AUTOSAVE.FBM");
        StartupFlow flow(data,saves);std::unique_ptr<app::CareerSession> session;
        uint64_t revision=0;openbmh::audio::Playback playback;std::optional<uint64_t> audio;
        const auto serve=[&](const FlowResult& action) {
            const auto error=app::serve_flow_request(flow,action,session,data,saves,0x12345678,
                revision,playback,audio,false);
            if(!error.empty())throw std::runtime_error(error);
        };
        flow.click(214,215);serve(flow.send_key("ENTER"));
        check(session&&flow.desktop()->state().national_team,"normal untouched Original fixture supplies the national desk");
        const auto open=[&] {
            const auto& items=flow.desktop()->state().items;
            const auto item=std::find_if(items.begin(),items.end(),[](const DeskItem& i){return i.kind==6&&!i.disabled;});
            check(item!=items.end(),"national desk supplies the genuine menu6 entry");
            serve(flow.click(item->x+item->width/2,item->y+item->height/2));
            check(flow.screen_name()=="national_friendly_picker","normal menu6 opens format field");
        };
        open();
        const auto before=save_stream(session->state());const auto w=session->rand();const auto g=session->game();
        const auto before_revision=revision;
        FlowResult answer;
        if(kind==0)answer=flow.pointer_down(0,0,true);
        else {
            const auto projection=world_cup_statistics_from_game(session->state(),load_le(asset_path(data,"BMH.exe")),
                WorldCupStatisticsOwner::friendly_match_nation_picker);
            const int x=kind==2?projection.flag_cells[1].x+1:0;
            const int y=kind==2?projection.flag_cells[1].y+1:0;
            flow.move(x,y);
            answer=flow.process_timer_expired(x,y);
        }
        check(answer.changed && answer.request==FlowRequest::submenu_closed &&
              answer.national_friendly_opponent==-1 && !flow.national_friendly_family_active() &&
              flow.screen_name()=="desktop","event8 leaves the complete picker without selecting even a hovered nation");
        serve(answer);
        if(kind==0)(void)flow.click(0,0,true); // consume only the real physical DOWN's UP
        check(save_stream(session->state())==before && revision==before_revision &&
              session->rand().seed()==w.seed() && session->rand().calls()==w.calls() &&
              session->game().seed()==g.seed() && session->game().calls()==g.calls(),
              "manual close and both picker deadlines preserve all Native bytes and W/G checkpoints");
        open();
        flow.pointer_down(141,220);
        const auto navigation=flow.click(141,220);
        check(navigation.request==FlowRequest::statistics_menu_action && flow.pending_statistics_menu(),
              "deadline creates no synthetic release barrier swallowing the next real footer UP");
        const auto pending=flow.state_json();
        check(!flow.process_timer_expired().changed && flow.state_json()==pending &&
              save_stream(session->state())==before && session->rand().seed()==w.seed() &&
              session->rand().calls()==w.calls() && session->game().seed()==g.seed() && session->game().calls()==g.calls(),
              "pending selector ACK remains owned when the picker deadline arrives");
        std::filesystem::remove_all(saves);
        std::printf("PASS: national picker deadline kind=%d\n",kind);
    }
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("statistics_case2_app_tests requires German data directory and national save fixture");
        const std::filesystem::path data=argv[1];
        constexpr uint32_t seed=0x12345678;
        StartupFlow flow(data);
        std::unique_ptr<app::CareerSession> session;
        uint64_t revision=0;
        openbmh::audio::Playback playback;
        std::optional<uint64_t> audio_session;
        const auto serve=[&](const FlowResult& result) {
            const auto error=app::serve_flow_request(flow,result,session,data,data,seed,revision,
                                                      playback,audio_session,false);
            if(!error.empty())throw std::runtime_error(error);
        };
        serve(flow.click(150,215)); serve(flow.click(5,225,true));
        serve(flow.click(5,225,true)); serve(flow.click(90,169));
        check(session&&flow.screen_name()=="desktop","real career setup reaches the desk");
        const auto& items=flow.desktop()->state().items;
        const auto menu=std::find_if(items.begin(),items.end(),[](const DeskItem& item) {
            return item.kind==23&&!item.disabled;
        });
        check(menu!=items.end(),"statistics menu 23 is Source-enabled");
        serve(flow.click(menu->x+menu->width/2,menu->y+menu->height/2));
        check(flow.in_statistics_menu()&&flow.in_statistics_rankings(),"normal statistics parent opens its default index 7");
        flow.pointer_down(230,207);
        const auto open=flow.click(230,207);
        check(open.request==FlowRequest::statistics_menu_action&&open.statistics_menu_action.selected_index==2,
              "global selector index 2 emits its typed operation");
        serve(open);
        check(flow.in_analytics()&&flow.in_statistics_menu()&&flow.screen_name()=="analytics",
              "Analytics child uses the retained menu-23 owner");
        const int manager=flow.analytics()->state().manager_slot;
        const uint32_t pref_address=::bmh::layout::FlagsUnsigned+uint32_t(manager)*3;
        const uint8_t before=session->state().memory.u8(pref_address);
        const auto watcom_calls=session->rand().calls(), game_calls=session->game().calls();
        const auto request=flow.pointer_down(30,145);
        check(request.request==FlowRequest::analytics_action,"metric click emits typed request");
        auto stale=request; ++stale.analytics_action.owner.revision;
        serve(stale);
        check(session->state().memory.u8(pref_address)==before&&flow.pending_analytics(),
              "stale owner revision cannot write preference");
        auto forged=request; forged.analytics_action.target_flags^=0xc0;
        serve(forged);
        check(session->state().memory.u8(pref_address)==before&&flow.pending_analytics(),
              "forged control operation cannot write preference");
        serve(request);
        check(session->state().memory.u8(pref_address)==uint8_t(before^1U)&&
              flow.analytics()->state().source_flags==uint8_t(before^1U)&&
              session->rand().calls()==watcom_calls&&session->game().calls()==game_calls,
              "accepted typed metric action persists exact Source flag through Session");
        serve(flow.pointer_down(10,80,true));
        check(!flow.in_analytics()&&!flow.in_statistics_menu()&&flow.screen_name()=="desktop",
              "one right-click returns directly to the desk");
        const auto reopen_menu=std::find_if(flow.desktop()->state().items.begin(),flow.desktop()->state().items.end(),[](const DeskItem& item) {
            return item.kind==23&&!item.disabled;
        });
        check(reopen_menu!=flow.desktop()->state().items.end(),"menu23 remains available after closing a statistics child");
        serve(flow.click(reopen_menu->x+reopen_menu->width/2,reopen_menu->y+reopen_menu->height/2));
        flow.pointer_down(33,220);
        serve(flow.click(33,220));
        check(flow.in_statistics_page()&&flow.screen_name()=="alltime_table",
              "page 3 opens through the retained menu-23 owner");
        serve(flow.pointer_down(0,0,true));
        check(!flow.in_statistics_menu()&&flow.screen_name()=="desktop",
              "page 3 right-click exits the full statistics family to Desk");
        const auto reopen_again=std::find_if(flow.desktop()->state().items.begin(),flow.desktop()->state().items.end(),[](const DeskItem& item) {
            return item.kind==23&&!item.disabled;
        });
        serve(flow.click(reopen_again->x+reopen_again->width/2,reopen_again->y+reopen_again->height/2));
        flow.pointer_down(141,220);
        serve(flow.click(141,220));
        check(flow.in_statistics_page()&&flow.screen_name()=="world_cup_statistics",
              "page 4 opens as read-only history for the normal statistics owner");
        flow.move(40,37);
        const auto page4_frame=flow.render();
        check(page4_frame.width==320&&page4_frame.height==240,
              "page 4 hover restores the Original parent/flag assets and renders a full frame");
        const auto read_only_projection=world_cup_statistics_from_game(session->state(),
            load_le(asset_path(data,"BMH.exe")),WorldCupStatisticsOwner::desk_read_only);
        check(flow.pointer_down(read_only_projection.flag_cells[1].x+1,
              read_only_projection.flag_cells[1].y+1).request!=FlowRequest::national_friendly_selected,
              "ordinary menu-23 page 4 remains read-only");
        const auto navigate=[&](int x,int y,const char* screen) {
            serve(flow.pointer_down(x,y));
            serve(flow.click(x,y));
            check(flow.in_statistics_menu()&&flow.screen_name()==screen,
                  "global buttons remain reachable above each statistics child");
        };
        navigate(33,220,"alltime_table");
        navigate(141,220,"world_cup_statistics");
        navigate(249,220,"finance_totals");
        navigate(33,232,"series_records");
        navigate(141,232,"statistics_rankings");
        navigate(141,220,"world_cup_statistics");
        serve(flow.pointer_down(0,0,true));
        check(!flow.in_statistics_menu()&&flow.screen_name()=="desktop",
              "page 4 right-click returns directly to Desk");

        const std::filesystem::path national_fixture=argv[2];
        const auto national_saves=std::filesystem::temp_directory_path()/(
            "openbmh-statistics-national-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(national_saves);
        std::filesystem::copy_file(national_fixture,national_saves/"AUTOSAVE.FBM");
        StartupFlow national_flow(data,national_saves);
        std::unique_ptr<app::CareerSession> national_session;
        uint64_t national_revision=0;
        openbmh::audio::Playback national_playback;
        std::optional<uint64_t> national_audio;
        const auto serve_national=[&](const FlowResult& result) {
            const auto error=app::serve_flow_request(national_flow,result,national_session,data,national_saves,
                seed,national_revision,national_playback,national_audio,false);
            if(!error.empty())throw std::runtime_error(error);
        };
        national_flow.click(214,215);
        serve_national(national_flow.send_key("ENTER"));
        check(national_session&&national_flow.desktop()->state().national_team&&
              national_session->state().manager_teams.at(size_t(national_session->state().memory.u8(::bmh::layout::ActiveManager)))==0x165,
              "national fixture loads the national manager as the active owner");
        const auto national_menu=std::find_if(national_flow.desktop()->state().items.begin(),
            national_flow.desktop()->state().items.end(),[](const DeskItem& item){return item.kind==23&&!item.disabled;});
        check(national_menu!=national_flow.desktop()->state().items.end(),"national desk exposes the normal menu23 action");
        serve_national(national_flow.click(national_menu->x+national_menu->width/2,
                                           national_menu->y+national_menu->height/2));
        check(national_flow.in_statistics_menu()&&national_flow.in_statistics_rankings()&&
              national_flow.screen_name()=="statistics_rankings",
              "normal national manager menu23 keeps legacy routine param0 and default index7");
        serve_national(national_flow.pointer_down(0,0,true));
        const auto friendly_menu=std::find_if(national_flow.desktop()->state().items.begin(),
            national_flow.desktop()->state().items.end(),[](const DeskItem& item){return item.kind==6&&!item.disabled;});
        check(friendly_menu!=national_flow.desktop()->state().items.end(),
              "national manager exposes the explicit menu-6 friendly caller");
        std::printf("National-friendly desk menu 6: rect=(%d,%d %dx%d) midpoint=(%d,%d)\n",
                    friendly_menu->x,friendly_menu->y,friendly_menu->width,friendly_menu->height,
                    friendly_menu->x+friendly_menu->width/2,friendly_menu->y+friendly_menu->height/2);
        const auto friendly_open=national_flow.click(friendly_menu->x+friendly_menu->width/2,
                                                     friendly_menu->y+friendly_menu->height/2);
        check(friendly_open.request==FlowRequest::desk_action&&friendly_open.desk_action.kind==DeskRequestKind::open_menu&&friendly_open.desk_action.menu_kind==6,
              "national friendly click emits the typed menu-6 request");
        check(national_flow.desktop()->pending()&&*national_flow.desktop()->pending()==friendly_open.desk_action,
              "menu-6 action retains its exact local pending owner");
        check(friendly_open.desk_action.revision==national_revision&&
              friendly_open.desk_action.manager_slot==int(national_session->state().memory.u8(::bmh::layout::ActiveManager)),
              "menu-6 owner binds the current manager and revision");
        serve_national(friendly_open);
        check(national_flow.screen_name()=="national_friendly_picker",
              "menu 6 opens the source nation picker");
        const auto national_exe=load_le(asset_path(data,"BMH.exe"));
        const auto picker_projection=world_cup_statistics_from_game(national_session->state(),
            national_exe,WorldCupStatisticsOwner::friendly_match_nation_picker);
        check(picker_projection.flag_cells.size()>1,"friendly picker exposes selectable opponent rows");
        const auto navigate_national_page=[&](int x,int y,int index,const char* screen) {
            national_flow.pointer_down(x,y);
            const auto action=national_flow.click(x,y);
            check(action.request==FlowRequest::statistics_menu_action&&
                  action.statistics_menu_action.selected_index==index,
                  "menu-6 parent keeps its typed global selector action");
            serve_national(action);
            check(national_flow.screen_name()==screen,"menu-6 global selector opens the requested Original page");
        };
        navigate_national_page(141,208,1,"transfer_history");
        navigate_national_page(249,208,2,"analytics");
        const auto family_metric=national_flow.pointer_down(30,145);
        check(family_metric.request==FlowRequest::analytics_action,
              "param-1 Analytics retains its typed preference action");
        serve_national(family_metric);
        check(national_flow.screen_name()=="analytics"&&national_flow.national_friendly_owner()&&
              national_flow.national_friendly_owner()->revision==national_revision,
              "accepted Analytics ACK rebinds both family owners to the advanced revision");
        navigate_national_page(33,220,3,"alltime_table");
        navigate_national_page(249,220,5,"finance_totals");
        navigate_national_page(33,232,6,"series_records");
        navigate_national_page(141,232,7,"statistics_rankings");
        navigate_national_page(249,232,8,"minute_statistics");
        navigate_national_page(141,220,4,"national_friendly_picker");
        national_flow.pointer_down(51,208);
        const auto opportunities_open=national_flow.click(51,208);
        check(opportunities_open.request==FlowRequest::statistics_menu_action&&
              opportunities_open.statistics_menu_action.selected_index==0,
              "menu-6 parameter-1 selector routes its Source Opportunities page");
        serve_national(opportunities_open);
        check(national_flow.in_opportunities()&&national_flow.opportunities()->state().identity.runtime_team==0x165&&
              national_flow.opportunities()->state().identity.stable_club_identity==
                  national_flow.desktop()->state().managers[0].club_identity_key&&
              national_flow.opportunities()->state().league_size>0,
              "qualified national Opportunities uses the native desk identity and league-0 projection");
        const auto national_rounds=national_session->state().league_rounds.at(0);
        const int national_expected_rank=(national_rounds<2||national_rounds>=league_matchdays(0))?0:9;
        check(national_flow.opportunities()->state().current_rank==national_expected_rank&&
              std::all_of(national_flow.opportunities()->state().ranks.begin(),
                          national_flow.opportunities()->state().ranks.end(),[](uint8_t rank){return rank==1;}),
              "national Opportunities follows legacy routine league-0 round/rank guards and all-one rows");
        if(national_expected_rank==0)
            check(national_flow.pointer_down(130,113).request==FlowRequest::none,
                  "legacy routine rank zero disables national mode controls for this fixture");
        else {
            const auto national_mode=national_flow.pointer_down(130,113);
            check(national_mode.request==FlowRequest::opportunities_action&&
                  national_mode.opportunities_action.action==OpportunitiesAction::select_mode,
                  "national Opportunities mode control emits its owner-bound typed action");
            serve_national(national_mode);
            check(national_flow.in_opportunities()&&national_flow.opportunities()->state().mode==
                      national_mode.opportunities_action.mode&&
                  national_flow.national_friendly_owner()->revision==national_revision,
                  "mode acknowledgement advances the live national family revision");
        }
        const auto close_opportunities=national_flow.pointer_down(1,1,true);
        check(close_opportunities.request==FlowRequest::opportunities_action&&
              close_opportunities.opportunities_action.action==OpportunitiesAction::close,
              "national Opportunities close emits its typed Source close action");
        serve_national(close_opportunities);
        check(national_flow.screen_name()=="national_friendly_picker"&&
              national_flow.national_friendly_owner()->revision==national_revision,
              "closing national Opportunities returns to the caller's page-4 picker");
        check(national_flow.pointer_down(picker_projection.flag_cells[0].x+1,
              picker_projection.flag_cells[0].y+1).request==FlowRequest::none,
              "country zero remains disabled in the menu-6 opponent picker");
        const auto opponent_cell=picker_projection.flag_cells[1];
        std::printf("National-friendly opponent 0x166: rect=(%d,%d 11x8) center=(%d,%d)\n",
                    opponent_cell.x,opponent_cell.y,opponent_cell.x+5,opponent_cell.y+4);
        const auto selected=national_flow.pointer_down(opponent_cell.x+1,opponent_cell.y+1);
        check(selected.request==FlowRequest::national_friendly_selected&&
              selected.national_friendly_opponent==0x166,
              "picker DOWN returns only the selected opponent identity");
        auto stale_national_revision=national_revision+1;
        const auto stale_family_error=app::serve_flow_request(national_flow,selected,
            national_session,data,national_saves,seed,stale_national_revision,national_playback,national_audio,false);
        check(stale_family_error.empty()&&national_flow.screen_name()=="national_friendly_picker"&&
              national_flow.national_friendly_family_active(),
              "stale family owner cannot start a national quote or close its picker");
        check(national_flow.click(opponent_cell.x+1,opponent_cell.y+1).request==FlowRequest::none,
              "the matching UP cannot leak into the newly opened calendar");
        std::vector<uint16_t> question_modes;
        std::string cost_body;
        const app::NationalFriendlyQuestionPresenter accept_question=[&](const GameState&,TaxDialogProjection projection,const Image&) {
            question_modes.push_back(projection.source_flags);
            if(projection.source_flags==0x404)cost_body=projection.body;
            return std::optional<bool>{true};
        };
        const auto national_error=app::serve_flow_request(national_flow,selected,national_session,data,national_saves,
            seed,national_revision,national_playback,national_audio,false,accept_question);
        check(national_error.empty()&&national_flow.screen_name()=="friendly_match",
              "venue and source approval questions lead to the existing friendly calendar");
        check(std::find(question_modes.begin(),question_modes.end(),0x606)!=question_modes.end(),
              "national friendly caller presents the Original venue question");
        check(std::find(question_modes.begin(),question_modes.end(),0x404)!=question_modes.end(),
              "national friendly caller presents the Original home-payer cost question");
        check(national_flow.friendly_calendar_quote() && cost_body==
              bmh::message_text(national_session->state(),::bmh::layout::Controller_FormatError_TextPointer2)+
              bmh::format_number(national_session->state(),national_flow.friendly_calendar_quote()->cost,0x101),
              "real national question displays precisely the immutable calendar quote cost");
        const uint8_t plans_before=national_session->state().memory.u8(::bmh::layout::FriendlyPlanCount);
        FlowResult booking_action;
        int booked_day=-1;
        for(int day=0;day<=365&&booking_action.request==FlowRequest::none;++day) {
            const auto cell=friendly_calendar_cell(national_exe,national_session->state().season_start,uint16_t(day));
            booking_action=national_flow.click(cell.x+cell.width/2,cell.y+cell.height/2);
            if(booking_action.request==FlowRequest::friendly_action)booked_day=day;
        }
        check(booking_action.request==FlowRequest::friendly_action,
              "existing Original calendar offers a Source-valid national booking date");
        const auto booked_error=app::serve_flow_request(national_flow,booking_action,national_session,data,national_saves,
            seed,national_revision,national_playback,national_audio,false,accept_question);
        check(booked_error.empty()&&national_session->state().memory.u8(::bmh::layout::FriendlyPlanCount)==uint8_t(plans_before+2),
              "calendar request reaches the atomic national engine booking path");
        check(booked_day>=0,"national booking selected a concrete calendar day");
        check(national_flow.screen_name()=="desktop" && !national_flow.friendly_calendar_quote() &&
              !national_flow.pending_friendly_action(),
              "Source national menu6 returns immediately after booking without a success dialog");
        const auto booked_cell=friendly_calendar_cell(national_exe,national_session->state().season_start,uint16_t(booked_day));
        std::printf("National-friendly fixture booking: day=%d cell=(%d,%d)\n",booked_day,
                    booked_cell.x+booked_cell.width/2,booked_cell.y+booked_cell.height/2);
        national_flow.click(1,1,true);
        check(national_flow.screen_name()=="desktop","successful calendar can return to the desk");
        const auto close_menu=std::find_if(national_flow.desktop()->state().items.begin(),
            national_flow.desktop()->state().items.end(),[](const DeskItem& item){return item.kind==6&&!item.disabled;});
        check(close_menu!=national_flow.desktop()->state().items.end(),"menu 6 remains available after booking");
        serve_national(national_flow.click(close_menu->x+close_menu->width/2,close_menu->y+close_menu->height/2));
        check(national_flow.screen_name()=="national_friendly_picker","menu 6 opens a second source picker");
        national_flow.pointer_down(51,208);
        const auto closed_picker=national_flow.pointer_down(1,1,true);
        check(closed_picker.request==FlowRequest::submenu_closed&&national_flow.screen_name()=="desktop",
              "right-button DOWN clears the complete menu-6 statistics family");
        check(national_flow.click(1,1,true).request==FlowRequest::none,
              "the close-button UP cannot activate the desk behind the picker");
        std::filesystem::remove_all(national_saves);
        test_national_picker_deadline(data,national_fixture);
        test_national_friendly_calendar_deadline(data,national_fixture);
        std::puts("Statistics case 2: navigation, stale guards, preference ACK and close passed");
        return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1;
    }
}
