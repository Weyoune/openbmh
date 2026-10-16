// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <chrono>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>

#include "app/career_session.hpp"
#include "app/controller.hpp"
#include "game/cup.hpp"
#include "game/club_info.hpp"
#include "game/save.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "game/messages.hpp"
#include "ui/flow.hpp"
#include "ui/other_games_state.hpp"

using namespace bmh;

namespace bmh::ui {
uint32_t other_games_source_round_day_for_test(const GameState&,uint32_t,int);
}

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void start_career(const std::filesystem::path& data, ui::StartupFlow& flow,
                  std::unique_ptr<app::CareerSession>& session, uint64_t& revision,
                  openbmh::audio::Playback& playback, std::optional<uint64_t>& audio_session) {
    flow.click(150,215);
    flow.click(5,225,true);
    flow.click(5,225,true);
    const auto start = flow.click(90,169);
    const auto error = app::serve_flow_request(flow,start,session,data,data,0x3195ab71,
        revision,playback,audio_session,false);
    check(error.empty() && session && flow.screen_name()=="desktop",
          "Other Games test starts a production career through Flow/Controller");
}

void test_menu5_source_transaction(const std::filesystem::path& data) {
    ui::StartupFlow flow(data);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision=0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    start_career(data,flow,session,revision,playback,audio_session);
    const auto item=std::find_if(flow.desktop()->state().items.begin(),flow.desktop()->state().items.end(),
        [](const ui::DeskItem& i){return i.kind==5&&!i.disabled;});
    check(item!=flow.desktop()->state().items.end(),"the real initial desk exposes an enabled Other Games item");
    const auto open=flow.click(item->x+item->width/2,item->y+item->height/2);
    check(open.request==ui::FlowRequest::desk_action&&open.desk_action.menu_kind==5,
          "menu 5 is opened from its real DesktopScreen request");

    // Controlled Source-valid descriptor-30 friendly fixture. The real career's
    // descriptor pointers target the Original 30-entry plan arrays; initialize 32
    // valid domestic team IDs and 16 signed date words within those Native buffers.
    // Flags/round/link fields describe a live one-round knockout-style view so the
    // Source 16-participant page window is exercised. This is intentionally a
    // controlled descriptor fixture, not a claim that the loaded career already has
    // these planned friendlies.
    auto& fixture=session->state().memory;
    const uint32_t friendly_entry=cup_entry_address(30);
    constexpr uint32_t friendly_teams=::bmh::layout::FriendlyTeams;
    constexpr uint32_t friendly_dates=::bmh::layout::FriendlyDays;
    check(cup_entry_index(friendly_entry)==30&&fixture.has(friendly_entry+0x27),
          "controlled ID12 entry is wholly inside the 36-entry Source descriptor table");
    fixture.set_pointer(friendly_entry,friendly_teams);
    fixture.set_pointer(friendly_entry+0x0c,friendly_dates);
    check(fixture.pointer(friendly_entry)==friendly_teams&&
          fixture.pointer(friendly_entry+0x0c)==friendly_dates&&
          fixture.has(friendly_teams+32*2-1)&&fixture.has(friendly_dates+16*2-1),
          "controlled ID12 descriptor uses fully mapped Original team/date buffers");
    for(uint32_t i=0;i<32;++i) {
        check(i<138&&i<session->state().club_ids.size(),
              "controlled friendly team is a valid loaded domestic club ID");
        fixture.set16(friendly_teams+i*2,uint16_t(i));
    }
    // The friendly words are signed offsets from this initialized Source date copy.
    // Planned ID12 rows consume these directly; they do not use the cup calendar table.
    fixture.set16(::bmh::layout::CalendarState_FormatError_RuntimeField3,200);
    for(uint32_t i=0;i<16;++i) {
        const int16_t offset=i==0?-1:int16_t(i*2);
        fixture.set16(friendly_dates+i*2,uint16_t(offset));
    }
    fixture.set8(friendly_entry+0x14,4);
    fixture.set8(friendly_entry+0x15,0xff);
    fixture.set8(friendly_entry+0x16,0);
    fixture.set8(friendly_entry+0x17,0);
    fixture.set16(friendly_entry+0x18,12);
    fixture.set8(friendly_entry+0x1a,32);
    fixture.set8(friendly_entry+0x1b,1);
    fixture.set8(friendly_entry+0x1c,0);
    fixture.set8(friendly_entry+0x1d,0);
    fixture.set8(friendly_entry+0x1e,1);
    const std::array<uint8_t,8> selectors{0,12,6,0x14,0x14,0x14,12,0x14};
    for(size_t i=0;i<selectors.size();++i)
        if(i!=4) fixture.set8(::bmh::layout::ActiveSelectors+uint32_t(i),selectors[i]);

    const GameState native_before=session->state();
    GameState expected=native_before;
    cup_overview_effects(expected);
    const auto w_before=session->rand();
    const auto g_before=session->game();
    const uint64_t revision_before=revision;
    std::string error;
    const auto underlay=flow.desktop()->render();
    auto page=session->open_other_games(open.desk_action,revision,underlay,error);
    check(page.has_value()&&error.empty(),"Source menu-5 entry builds a typed projection");
    check(revision==revision_before+1&&page->identity.revision==revision&&page->identity.sequence==0,
          "opening accepts exactly one UI revision and starts sequence zero");
    check(save_stream(session->state())==save_stream(expected),
          "opening changes only the demonstrated Source cup-overview Native fields");
    check(session->rand().seed()==w_before.seed()&&session->rand().calls()==w_before.calls()&&
          session->game().seed()==g_before.seed()&&session->game().calls()==g_before.calls(),
          "Source menu-5 entry consumes neither RNG stream");
    check(page->underlay.pixels==underlay.pixels&&page->identity.expected_view_byte==
          session->state().memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(page->identity.manager_slot)),
          "typed page retains actual desk underlay and binds the accepted view byte");
    check(page->projection.selectors.size()==8,
          "projection preserves all eight Source selector slots including holes");

    // Exercise the Source-byte formatting adapter against the accepted live projection.
    // These checks use the actual loaded executable/TextD and descriptor data; they do not
    // synthesize fixture rows or assert a broad pixel-parity claim.
    const LeImage exe=load_le(data/"BMH.exe");
    const TextDb texts=load_textd(data/"TEXTD.BLK");
    const auto formatting_native=save_stream(session->state());
    const auto formatting_rand=session->rand();
    const auto formatting_game=session->game();
    const auto formatted=ui::other_games_state_from_game(session->state(),page->identity,texts,exe,
        page->projection.competition,page->projection.linked_page,page->projection.round,
        page->projection.display_mode,page->source_participant_offset,false,underlay,
        session->club_modal_runtime_checkpoint());
    check(save_stream(session->state())==formatting_native&&
          session->rand().seed()==formatting_rand.seed()&&session->rand().calls()==formatting_rand.calls()&&
          session->game().seed()==formatting_game.seed()&&session->game().calls()==formatting_game.calls(),
          "formatting and projection adapter are const over full Native and both RNG streams");

    // Literal legacy routine calendar controls. The first two descriptors are parallel
    // siblings; the target in sibling zero must resolve before the sibling is collapsed,
    // while the following stage starts after the final sibling's round count.
    GameState calendar_fixture=session->state();
    calendar_fixture.memory.set8(::bmh::layout::CalendarState_FormatError_RuntimeField2,0);
    calendar_fixture.memory.set8(::bmh::layout::CalendarState_FormatError_RuntimeField,0);
    for(int day=0;day<366;++day) calendar_fixture.memory.set16(::bmh::layout::CompetitionCalendar+uint32_t(day)*2,0);
    const auto set_descriptor=[&](int entry,int competition,int previous,int next,int group,
                                  int flags,int rounds) {
        const uint32_t at=cup_entry_address(entry);
        calendar_fixture.memory.set8(at+0x14,uint8_t(flags));
        calendar_fixture.memory.set8(at+0x15,uint8_t(previous));
        calendar_fixture.memory.set8(at+0x16,uint8_t(next));
        calendar_fixture.memory.set8(at+0x17,uint8_t(group));
        calendar_fixture.memory.set16(at+0x18,uint16_t(competition));
        calendar_fixture.memory.set8(at+0x1b,uint8_t(rounds));
    };
    set_descriptor(0,0,-1,2,1,0,1);
    set_descriptor(1,0,0,2,0,0,2);
    set_descriptor(2,0,1,-1,0,0,3);
    for(const int day:{5,10,15,20})
        calendar_fixture.memory.set16(::bmh::layout::CompetitionCalendar+uint32_t(day)*2,0x20); // competition 0 => mask bit 5
    check(ui::other_games_source_round_day_for_test(calendar_fixture,cup_entry_address(0),0)==
              uint16_t(calendar_fixture.season_start+5)&&
          ui::other_games_source_round_day_for_test(calendar_fixture,cup_entry_address(1),0)==
              uint16_t(calendar_fixture.season_start+5)&&
          ui::other_games_source_round_day_for_test(calendar_fixture,cup_entry_address(2),1)==
              uint16_t(calendar_fixture.season_start+20),
          "legacy routine resolves a selected parallel sibling and collapses prior stage rounds exactly");

    // Indoor cup special: a zero-flags immediate successor shares its predecessor's
    // first date; a nonzero successor flags byte consumes that one date before its own.
    for(int day=0;day<366;++day) calendar_fixture.memory.set16(::bmh::layout::CompetitionCalendar+uint32_t(day)*2,0);
    calendar_fixture.memory.set16(::bmh::layout::CompetitionCalendar+8*2,0x4002);
    calendar_fixture.memory.set16(::bmh::layout::CompetitionCalendar+9*2,0x4002);
    set_descriptor(0,11,-1,1,0,0,3);
    set_descriptor(1,0,0,-1,0,0,2);
    check(ui::other_games_source_round_day_for_test(calendar_fixture,cup_entry_address(1),0)==
              uint16_t(calendar_fixture.season_start+8),
          "legacy routine returns the indoor predecessor date for its zero-flags successor");
    calendar_fixture.memory.set8(cup_entry_address(1)+0x14,1);
    check(ui::other_games_source_round_day_for_test(calendar_fixture,cup_entry_address(1),0)==
              uint16_t(calendar_fixture.season_start+9),
          "legacy routine consumes exactly one indoor calendar occurrence before a non-special successor");
    check(save_stream(session->state())==formatting_native,
          "calendar adapter fixtures mutate only their local GameState copy");
    if(page->projection.competition!=12&&page->projection.display_mode==OtherGamesDisplayMode::detail)
        check(!formatted.date_heading.empty(),"ordinary detail view includes Source weekday/date heading");
    if(page->projection.competition==12)
        check(formatted.date_heading.empty(),"planned-friendlies descriptor omits the ordinary date heading");
    int squad_detail_rows=0;
    for(size_t i=0;i<page->projection.rows.size();++i) {
        const auto& source=page->projection.rows[i];
        const auto& display=formatted.rows.at(i);
        const std::array<const OtherGamesClubMetadata*,2> metadata{&source.home_metadata,&source.away_metadata};
        for(size_t side=0;side<2;++side) {
            std::string expected_detail;
            if(metadata[side]->has_squad_values) {
                ++squad_detail_rows;
                expected_detail=exe.cstring(::bmh::layout::CupDraw_FormatError_ExecutableField2);
                const std::array<size_t,3> labels{
                    size_t((::bmh::layout::CupDraw_FormatError_TextPointer5-::bmh::layout::TextPointerTable)/4),size_t((::bmh::layout::LeaguePairings_FormatError_TextPointer8-::bmh::layout::TextPointerTable)/4),
                    size_t((::bmh::layout::LeaguePairings_FormatError_TextPointer9-::bmh::layout::TextPointerTable)/4)};
                for(size_t n=0;n<labels.size();++n)
                    expected_detail+=format_number(session->state(),metadata[side]->averages[n],2)+
                        texts.strings.at(labels[n]);
                expected_detail+=format_number(session->state(),metadata[side]->averages[3],2)+
                    exe.cstring(::bmh::layout::CupDraw_FormatError_TextPointer2)+format_number(session->state(),metadata[side]->averages[4],2)+
                    exe.cstring(::bmh::layout::CupDraw_FormatError_TextPointer3);
            } else if(metadata[side]->foreign_strength>=0) {
                const int strength=metadata[side]->foreign_strength;
                expected_detail=texts.strings.at(size_t(500+((strength>>5)&7)))+exe.cstring(::bmh::layout::CupDraw_FormatError_TextPointer);
                for(int bit=3;bit>=1;--bit)
                    expected_detail+=exe.cstring((strength&(1<<bit))?::bmh::layout::OtherGamesState_ForeignDetail_ExecutableField : ::bmh::layout::CupDraw_FormatError_ExecutableField3);
                expected_detail+=exe.cstring(::bmh::layout::CupDraw_FormatError_ExecutableField);
            }
            if(!expected_detail.empty())
                check(display.details[side]==expected_detail,
                      "club detail text follows Source TEXTD labels and marker order");
        }
        if(source.kind==OtherGamesRowKind::scheduled_competition) {
            const auto expected_day=source.scheduled_source_day;
            std::string expected;
            if(expected_day==0xffff) expected=exe.cstring(::bmh::layout::Fltd_Layout_TextPointer5);
            else {
                const auto date=ui::desk_date(expected_day,exe);
                expected=format_number(session->state(),date.day,2)+exe.cstring(::bmh::layout::OtherGamesState_ResultText_ExecutableField)+
                    format_number(session->state(),date.month,2)+exe.cstring(::bmh::layout::OtherGamesState_ResultText_ExecutableField)+
                    format_number(session->state(),date.year,0x44);
                std::replace(expected.begin(),expected.end(),'%','0');
            }
            check(display.primary_result==expected,
                  "scheduled result lock/date follows the computed Source uint32 day");
        } else if(source.source_result!=0xcccc) {
            std::string suffix;
            if(int16_t(source.source_result)>>10!=0) {
                suffix=texts.strings.at(160);
                if(suffix.size()>2) {
                    const int8_t high=int8_t(source.source_result>>8);
                    suffix[2]=char((int(high>>2)-1)*-0x11+'V');
                }
            }
            const std::string expected=format_number(session->state(),(source.source_result>>5)&0x1f,1)+
                exe.cstring(::bmh::layout::OtherGamesState_FormatNumber_ExecutableField)+format_number(session->state(),source.source_result&0x1f,1)+suffix;
            check(display.primary_result==expected,"primary score and TEXTD160 phase annotation preserve Source order");
        }
        if(source.second_leg_visible) {
            const auto value=source.source_second_leg;
            std::string expected=format_number(session->state(),value&0x1f,1)+exe.cstring(::bmh::layout::OtherGamesState_FormatNumber_ExecutableField)+
                format_number(session->state(),(value>>5)&0x1f,1);
            if(int16_t(value)>>10!=0) {
                auto suffix=texts.strings.at(160);
                if(suffix.size()>2) {
                    const int8_t high=int8_t(value>>8);
                    suffix[2]=char((int(high>>2)-1)*-0x11+'V');
                }
                expected+=suffix;
            }
            if(expected.size()<6) expected+=texts.strings.at(936);
            check(display.second_leg_result==expected,
                  "second-leg line uses reversed score order and Source short/CCCC suffix");
        }
    }
    check(!page->projection.rows.empty()&&squad_detail_rows>0,
          "live descriptor exercises the Original squad-detail formatter");

    auto forged=ui::OtherGamesRequest{page->identity,ui::OtherGamesAction::toggle_display_mode,-1};
    forged.identity.sequence++;
    const auto stable_revision=revision;
    const auto accepted_native=save_stream(session->state());
    check(!session->dispatch_other_games(forged,revision,error)&&!error.empty()&&revision==stable_revision&&
          save_stream(session->state())==accepted_native&&session->other_games_owner()->page.identity==page->identity,
          "forged sequence rejects without consuming the owner or Source Native state");

    const auto accept=[&](ui::OtherGamesAction action,int value,const char* message) {
        const auto before_action=session->state();
        const uint64_t old_revision=revision;
        const auto dispatch=session->dispatch_other_games({page->identity,action,value},revision,error);
        check(dispatch.has_value()&&error.empty()&&!dispatch->closed,message);
        check(dispatch->page.identity.revision==old_revision+1&&
              dispatch->page.identity.sequence==page->identity.sequence+1&&revision==old_revision+1,
              "accepted view request advances revision and sequence exactly once");
        GameState expected_action=before_action;
        if(action==ui::OtherGamesAction::select_competition) {
            const int manager=page->identity.manager_slot;
            const uint8_t before_view=before_action.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(manager));
            const auto& selected=page->projection.selectors.at(size_t(value));
            const bool source_reset=page->projection.competition!=value;
            if(page->projection.competition==selected.competition&&
               page->projection.source_participant_count>16) {
                const int old_cursor=page->source_participant_offset;
                int next_cursor=old_cursor+16;
                if(next_cursor>=page->projection.source_participant_count) next_cursor=0;
                expected_action.memory.set8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(manager),
                    uint8_t((before_view&0x80)|(next_cursor&0x7f)));
            } else if(source_reset) {
                expected_action.memory.set8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(manager),uint8_t(before_view&0x80));
            }
        } else if(action==ui::OtherGamesAction::toggle_display_mode) {
            const int manager=page->identity.manager_slot;
            expected_action.memory.set8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(manager),
                uint8_t(before_action.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(manager))^0x80));
        }
        check(save_stream(expected_action)==save_stream(session->state()),
              "view action remains within the exact format field write set");
        page=dispatch->page;
        return dispatch;
    };
    auto choose12=accept(ui::OtherGamesAction::select_competition,1,
        "competition selector action uses a visible slot whose ID differs from its slot number");
    check(page->projection.competition==12&&page->projection.source_participant_count>16,
          "controlled Source ID12 descriptor has a multi-page participant window");
    int small_competition_slot=-1;
    for(size_t slot=0;slot<page->projection.selectors.size();++slot) {
        const auto& selector=page->projection.selectors[slot];
        if(!selector.active||selector.competition==page->projection.competition)continue;
        const auto candidate=ui::other_games_state_from_game(session->state(),page->identity,texts,exe,
            selector.competition,0,-1,page->projection.display_mode,0,false,underlay,
            session->club_modal_runtime_checkpoint());
        if(candidate.projection.source_participant_count<=16) {
            small_competition_slot=int(slot);break;
        }
    }
    check(small_competition_slot>=0,
          "controlled Source selectors include a real descriptor at the <=16-participant boundary");
    const size_t participants_before_boundary=page->projection.source_participant_count;
    accept(ui::OtherGamesAction::select_competition,small_competition_slot,
        "valid Source selector crosses from a multi-page descriptor into a <=16 participant descriptor");
    check(participants_before_boundary>16&&page->projection.source_participant_count<=16&&
          page->projection.display_mode==OtherGamesDisplayMode::detail&&
          (page->identity.expected_view_byte&0x80)!=0&&
          (session->state().memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(page->identity.manager_slot))&0x80)!=0,
          "Source redraw at <=16 participants forces detail mode and persists bb13c bit 7");
    accept(ui::OtherGamesAction::select_competition,1,
        "return to controlled ID12 through its original visible selector slot");
    check(page->projection.competition==12&&page->projection.source_participant_count>16,
          "re-entered ID12 restores its multi-page descriptor before cursor tests");
    const auto friendly_state=ui::other_games_state_from_game(session->state(),page->identity,texts,exe,
        page->projection.competition,page->projection.linked_page,page->projection.round,
        page->projection.display_mode,page->source_participant_offset,false,underlay,
        session->club_modal_runtime_checkpoint());
    size_t scheduled_rows=0;
    for(size_t i=0;i<page->projection.rows.size();++i) {
        const auto& source=page->projection.rows[i];
        if(source.kind!=OtherGamesRowKind::scheduled_competition) continue;
        ++scheduled_rows;
        check(source.source_match>=0&&source.source_match<16,
              "controlled friendly result slot remains within the 16 initialized date words");
        const auto expected_offset=int16_t(session->state().memory.u16(friendly_dates+uint32_t(source.source_match)*2));
        check(source.scheduled_source_day==uint32_t(session->state().memory.u16(::bmh::layout::CalendarState_FormatError_RuntimeField3))+
              uint32_t(int32_t(expected_offset)),
              "ID12 date uses the signed word from the controlled Source Native plan array");
        std::string expected;
        if(source.scheduled_source_day==0xffff) expected=exe.cstring(::bmh::layout::Fltd_Layout_TextPointer5);
        else {
            const auto date=ui::desk_date(source.scheduled_source_day,exe);
            expected=format_number(session->state(),date.day,2)+exe.cstring(::bmh::layout::OtherGamesState_ResultText_ExecutableField)+
                format_number(session->state(),date.month,2)+exe.cstring(::bmh::layout::OtherGamesState_ResultText_ExecutableField)+
                format_number(session->state(),date.year,0x44);
            std::replace(expected.begin(),expected.end(),'%','0');
        }
        check(friendly_state.rows.at(i).primary_result==expected,
              "planned friendly applies signed offset before computed-day lock/date formatting");
    }
    check(scheduled_rows>0&&friendly_state.date_heading.empty(),
          "real ID12 schedule exercises date-result output without an ordinary date header");
    check(page->source_participant_offset==0&&
          session->state().memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(page->identity.manager_slot))==0x80,
          "ID-versus-slot reset clears both local and saved page cursor for menu slot1");
    accept(ui::OtherGamesAction::select_competition,1,
        "selected ID12 in non-12 slot1 takes the Source next-page action");
    check(page->projection.competition==12&&page->source_participant_offset==16&&
          (page->identity.expected_view_byte&0x7f)==16,
          "same selected ID12 pages by16 even though its selector slot is1");
    accept(ui::OtherGamesAction::select_competition,2,
        "menu slot2 selects Source competition ID6");
    check(page->projection.competition==6&&page->source_participant_offset==0,
          "clicking slot2 from ID12 resets the cursor and selects the slot's ID6");
    accept(ui::OtherGamesAction::select_competition,6,
        "selected ID6 clicked at slot6 adopts that slot's different ID12");
    check(page->projection.competition==12,
          "Source compares old selected ID6 to clicked slot6 before storing selected competition ID12");
    const auto before_close=save_stream(session->state());
    const auto close=session->dispatch_other_games({page->identity,ui::OtherGamesAction::close,-1},revision,error);
    check(close&&close->closed&&error.empty()&&!session->other_games_active()&&
          close->page.identity.revision==revision&&close->page.identity.sequence==page->identity.sequence+1,
          "Source close consumes the owner once and returns the closed transaction identity");
    check(save_stream(session->state())==before_close,
          "closing retains the menu's accepted Source effects");
}

bool same_other_games_checkpoint(const app::CareerSession::OtherGamesCheckpoint& a,
                                 const app::CareerSession::OtherGamesCheckpoint& b) {
    if(save_stream(a.state)!=save_stream(b.state)||a.runtime!=b.runtime||
       a.game.seed()!=b.game.seed()||a.game.calls()!=b.game.calls()||
       a.rand.seed()!=b.rand.seed()||a.rand.calls()!=b.rand.calls()||
       a.selection.competition!=b.selection.competition||a.selection.linked_page!=b.selection.linked_page||
       a.next_scope!=b.next_scope||a.owner.has_value()!=b.owner.has_value())return false;
    if(!a.owner)return true;
    const auto& x=*a.owner;const auto& y=*b.owner;
    return x.page.identity==y.page.identity&&x.desk_owner==y.desk_owner&&x.round==y.round&&
        x.participant_offset==y.participant_offset&&x.display_mode==y.display_mode&&
        x.native==y.native&&x.runtime==y.runtime&&x.rand_seed==y.rand_seed&&
        x.game_seed==y.game_seed&&x.rand_calls==y.rand_calls&&x.game_calls==y.game_calls&&
        x.page.projection.competition==y.page.projection.competition&&
        x.page.projection.entry_index==y.page.projection.entry_index&&
        x.page.projection.linked_page==y.page.projection.linked_page&&
        x.page.projection.round==y.page.projection.round;
}

std::filesystem::path copy_test_data(const std::filesystem::path&,const char*);

void test_other_games_team_info_return(const std::filesystem::path& data) {
    const auto temp=copy_test_data(data,"team-info-return");
    struct Cleanup { std::filesystem::path path; ~Cleanup(){std::filesystem::remove_all(path);} } cleanup{temp};
    ui::StartupFlow flow(temp);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision=0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    start_career(temp,flow,session,revision,playback,audio_session);
    const auto item=std::find_if(flow.desktop()->state().items.begin(),flow.desktop()->state().items.end(),
        [](const ui::DeskItem& candidate){return candidate.kind==5&&!candidate.disabled;});
    check(item!=flow.desktop()->state().items.end(),"TeamInfo fixture has the real enabled menu-5 item");
    auto opened=flow.click(item->x+item->width/2,item->y+item->height/2);
    check(app::serve_flow_request(flow,opened,session,temp,temp,0x3195ab71,revision,
          playback,audio_session,false).empty()&&flow.in_other_games(),
          "TeamInfo fixture opens the actual Other Games Flow owner");

    const auto accepted_page=flow.other_games()->state();
    check(!accepted_page.projection.rows.empty()&&accepted_page.projection.fixtures_available&&
          !accepted_page.projection.unfinished_predecessor,
          "the genuine current Other Games page exposes a clickable fixture");
    const auto target_for=[&](bool away) {
        const auto& row=accepted_page.projection.rows.front();
        const int target=away?row.away_team:row.home_team;
        check(target>=0&&size_t(target)<session->state().club_ids.size(),
              "visible fixture resolves to a loaded runtime team");
        return target;
    };
    const auto expected_team_info_effects=[&](
        const app::CareerSession::OtherGamesCheckpoint& before,int selected_team) {
        // Independent Source model for legacy routine/SquadCount/PrepareLineupEntry/28802: count occupied
        // squad IDs, find the last already-marked row in that count-sized prefix,
        // mark the display prefix, then clear injured-player flags over the full range.
        auto expected=before.state;
        const auto range=squad_range(selected_team);
        check(range.first<=expected.squad_slots.size()&&
              range.count<=expected.squad_slots.size()-range.first,
              "bounded TeamInfo model has a complete Source squad range");
        size_t occupied=0;
        for(size_t row=0;row<range.count;++row)
            if((expected.squad_slots[range.first+row]&0x0fff)!=0x0fff)++occupied;
        int last=int(occupied)-1;
        while(last>=0&&(expected.squad_slots[range.first+size_t(last)]&0xc000)==0)--last;
        bool source_slot_changed=false;
        for(int row=0;row<=last&&row<17;++row) {
            auto& slot=expected.squad_slots[range.first+size_t(row)];
            const auto old=slot;
            slot=uint16_t((slot&0x3fff)|(row<11?0x8000:0x4000));
            source_slot_changed|=old!=slot;
        }
        for(size_t row=0;row<range.count;++row) {
            auto& slot=expected.squad_slots[range.first+row];
            const int player=slot&0x0fff;
            if(player!=0x0fff&&size_t(player)<expected.players.size()&&
               ((expected.players[size_t(player)].w364>>16)&0x1f)!=0)slot&=0x1fff;
        }
        check(source_slot_changed,
              "the accepted TeamInfo fixture exercises an actual Source squad-slot write");

        auto expected_rand=before.rand;
        auto expected_runtime=before.runtime;
        const auto put_source=[&](uint32_t address,uint8_t value) {
            if(expected.memory.has(address))expected.memory.set8(address,value);
            else expected_runtime[address]=value;
        };
        put_source(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2,0);
        constexpr std::array<uint16_t,5> flags{0x0f1,0x0f2,0x0f4,0x0f8,0x0ff};
        int final_displayed_team=-1;
        TeamAverage final_average{};
        for(const auto& row:accepted_page.projection.rows) {
            for(const int team:{row.home_team,row.away_team}) {
                check(team>=0&&team<138,
                      "bounded TeamInfo redraw fixture uses only squad-backed domestic clubs");
                for(const uint16_t source_flags:flags) {
                    final_average=team_average(expected,team,source_flags,&expected_rand);
                    if(team<138) {
                        put_source(::bmh::layout::CareerSession_FormatError_ExecutableField3,uint8_t(final_average.best_half_sum));
                        put_source(::bmh::layout::CareerSession_FormatError_ExecutableField4,uint8_t(final_average.best_half_sum>>8));
                    }
                }
                final_displayed_team=team;
            }
        }
        check(final_displayed_team>=0,"bounded TeamInfo redraw has a final displayed squad-backed team");
        return std::tuple{std::move(expected),std::move(expected_runtime),expected_rand,
                          final_displayed_team,final_average};
    };
    const int hidden_target=std::numeric_limits<int>::max();
    const auto before_reject=session->other_games_checkpoint();
    const uint64_t before_reject_revision=revision;
    std::string error;
    check(!session->dispatch_other_games({accepted_page.identity,ui::OtherGamesAction::team_info,hidden_target},
              revision,error,[](app::ClubModalServices&,int,const Image&){return true;})&&
          !error.empty()&&revision==before_reject_revision&&
          same_other_games_checkpoint(before_reject,session->other_games_checkpoint()),
          "forged off-page team target is rejected before the presenter and preserves all owned state");
    error.clear();
    check(!session->dispatch_other_games({accepted_page.identity,ui::OtherGamesAction::team_info,target_for(false)},
              revision,error)&&!error.empty()&&revision==before_reject_revision&&
          same_other_games_checkpoint(before_reject,session->other_games_checkpoint()),
          "missing TeamInfo presenter rejects without consuming the Other Games transaction");

    const auto fixture_click=[&](bool away) {
        const auto& state=flow.other_games()->state();
        const auto& projection=state.projection;
        const int x=away?200:50;
        int baseline=48;
        if(!projection.knockout) {
            const int teams=projection.group_tables.empty()?int(projection.rows.size()*2):projection.group_tables.front().size;
            baseline=teams==2?104:39;
        } else if(projection.display_mode==OtherGamesDisplayMode::detail) {
            baseline=projection.source_participant_count<16
                ?(16-projection.source_participant_count)*4+48:48;
        } else {
            baseline=((64-projection.source_participant_count)/2)*3+26;
        }
        return flow.pointer_down(x,baseline);
    };
    const auto make_one_unmarked_source_slot=[&](int team) {
        auto controlled=session->other_games_checkpoint();
        const auto range=squad_range(team);
        size_t occupied=0;
        for(size_t row=0;row<range.count;++row)
            if((controlled.state.squad_slots.at(range.first+row)&0x0fff)!=0x0fff)++occupied;
        const size_t limit=std::min<size_t>({occupied,range.count,17});
        for(size_t row=0;row+1<limit;++row) {
            const int player=controlled.state.squad_slots.at(range.first+row)&0x0fff;
            if(player==0x0fff||size_t(player)>=controlled.state.players.size()||
               ((controlled.state.players[size_t(player)].w364>>16)&0x1f)!=0)continue;
            const size_t terminal=row+1;
            auto& source_slot=controlled.state.squad_slots.at(range.first+row);
            auto& terminal_slot=controlled.state.squad_slots.at(range.first+terminal);
            source_slot=uint16_t(source_slot&0x3fff);
            terminal_slot=uint16_t((terminal_slot&0x3fff)|0x8000);
            if(controlled.owner)controlled.owner->native=save_stream(controlled.state);
            session->restore_other_games_checkpoint(std::move(controlled));
            return true;
        }
        return false;
    };
    const auto serve_team_info=[&](const ui::FlowResult& action,
                                   app::TransferOfferTeamInfoPresenter presenter) {
        return app::serve_flow_request(flow,action,session,temp,temp,0x3195ab71,revision,
            playback,audio_session,false,{},{},{},{},{},{},{},{},{},
            app::PendingOfferEntryPolicy::legacy_unpresented,{},
            app::HumanBuyerMarketEntryPolicy::legacy_unpresented,std::move(presenter));
    };

    {
        const auto action=fixture_click(false);
        const auto before=session->other_games_checkpoint();
        const auto before_revision=revision;
        bool changed_live_session=false;
        const auto error=serve_team_info(action,[&](app::ClubModalServices&,int,const Image&) {
            // Simulate a presenter closure that captured the session and used
            // its public mutable APIs while the staged callback was running.
            session->rand().rand();
            (void)session->game().range(0,1);
            const uint32_t view_at=::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(accepted_page.identity.manager_slot);
            session->state().memory.set8(view_at,uint8_t(session->state().memory.u8(view_at)^1));
            auto injected=session->other_games_checkpoint();
            injected.runtime[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2]=0x77;
            injected.selection.linked_page+=1;
            ++injected.next_scope;
            if(injected.owner) {
                ++injected.owner->participant_offset;
                ++injected.owner->page.identity.sequence;
            }
            session->restore_other_games_checkpoint(std::move(injected));
            ++revision;
            changed_live_session=true;
            return true;
        });
        check(changed_live_session&&!error.empty()&&flow.in_other_games()&&
              !flow.pending_other_games_action()&&revision==before_revision&&
              same_other_games_checkpoint(before,session->other_games_checkpoint()),
              "captured presenter mutations to live Native, both RNGs, runtime, selection, owner and revision are rejected and fully rolled back");
    }

    {
        const auto action=fixture_click(false);
        std::string dispatch_error;
        auto accepted=session->dispatch_other_games(action.other_games_action,revision,dispatch_error,
            [](app::ClubModalServices&,int,const Image&){return true;});
        check(accepted.has_value()&&dispatch_error.empty()&&accepted->page.identity.revision==revision&&
              accepted->page.identity.sequence==action.other_games_action.identity.sequence+1,
              "positive ACK control produces a real valid TeamInfo successor page");
        flow.acknowledge_other_games(accepted->page,accepted->preparation);
        check(flow.in_other_games()&&!flow.pending_other_games_action()&&
              flow.other_games()->state().identity==accepted->page.identity,
              "Flow accepts the exact valid successor identity, selection and cursor as the forged-ACK control");
    }

    for(int forged_kind=0;forged_kind<3;++forged_kind) {
        const auto action=fixture_click(false);
        const auto before=session->other_games_checkpoint();
        const auto before_revision=revision;
        bool flow_ack_rejected=false;
        const auto error=serve_team_info(action,[&](app::ClubModalServices&,int,const Image&) {
            auto forged_page=flow.other_games()->state();
            ++forged_page.identity.revision;
            ++forged_page.identity.sequence;
            auto preparation=session->preparation_projection(revision+1);
            if(forged_kind==0)++forged_page.identity.sequence;
            else if(forged_kind==1)++forged_page.projection.competition;
            else ++forged_page.source_participant_offset;
            try {
                flow.acknowledge_other_games(std::move(forged_page),std::move(preparation));
            } catch(const std::exception&) {
                flow_ack_rejected=true;
                throw;
            }
            throw std::runtime_error("forged Other Games ACK was unexpectedly accepted");
            return true;
        });
        check(flow_ack_rejected&&!error.empty()&&flow.in_other_games()&&
              !flow.pending_other_games_action()&&revision==before_revision&&
              same_other_games_checkpoint(before,session->other_games_checkpoint()),
              forged_kind==0?"forged identity ACK rejects and controller restores the session checkpoint":
              forged_kind==1?"forged selection ACK rejects and controller restores the session checkpoint":
                             "forged page-cursor ACK rejects and controller restores the session checkpoint");
    }

    for(const bool away:{false,true}) {
        const int target=target_for(away);
        auto action=fixture_click(away);
        check(action.request==ui::FlowRequest::other_games_action&&
              action.other_games_action.action==ui::OtherGamesAction::team_info&&
              action.other_games_action.value==target,
              away?"physical away-team row hit emits its exact runtime team":
                   "physical home-team row hit emits its exact runtime team");

        const auto before_cancel=session->other_games_checkpoint();
        const auto cancel_revision=revision;
        bool entered=false;
        const auto cancelled=serve_team_info(action,[&](app::ClubModalServices& services,int team,const Image& parent) {
            check(team==target&&parent.width==320&&parent.height==240,
                  "nested TeamInfo receives the clicked team and the accepted 320x240 parent frame");
            (void)services.enter_club(team);entered=true;
            return false;
        });
        check(!cancelled.empty()&&entered&&!flow.pending_other_games_action()&&flow.in_other_games()&&
              revision==cancel_revision&&same_other_games_checkpoint(before_cancel,session->other_games_checkpoint()),
              "cancelled nested TeamInfo rolls back Native, runtime, W/G and owner revision for retry");

        action=fixture_click(away);
        check(make_one_unmarked_source_slot(target),
              "controlled valid TeamInfo fixture has an uninjured squad row before a marked Source row");
        const auto prior_ack=session->other_games_checkpoint();
        const uint64_t prior_ack_revision=revision;
        const auto panel=temp/"KB"/"56.VGA";
        const auto saved_panel=temp/"KB"/"56.VGA.teaminfo-backup";
        std::filesystem::copy_file(panel,saved_panel,std::filesystem::copy_options::overwrite_existing);
        bool removed_panel=false;
        const auto rejected_ack=serve_team_info(action,[&](app::ClubModalServices& services,int team,const Image&) {
            (void)services.enter_club(team);
            std::filesystem::remove(panel);removed_panel=true;
            return true;
        });
        if(removed_panel)std::filesystem::rename(saved_panel,panel);
        check(!rejected_ack.empty()&&removed_panel&&flow.in_other_games()&&
              !flow.pending_other_games_action()&&revision==prior_ack_revision&&
              same_other_games_checkpoint(prior_ack,session->other_games_checkpoint()),
              "failed returned-screen construction restores the entire nested transaction and leaves the page retryable");

        action=fixture_click(away);
        bool accepted=false;
        const auto source_expected=expected_team_info_effects(prior_ack,target);
        const auto returned=serve_team_info(action,[&](app::ClubModalServices& services,int team,const Image& parent) {
            check(team==target&&parent.width==320&&parent.height==240,
                  "successful presenter remains bound to the same visible home/away target");
            (void)services.enter_club(team);accepted=true;return true;
        });
        check(returned.empty()&&accepted&&flow.in_other_games()&&!flow.pending_other_games_action(),
              "successful TeamInfo route acknowledges and redraws Other Games instead of closing it");
        const auto& after=flow.other_games()->state();
        check(after.identity.revision==prior_ack_revision+1&&
              after.identity.sequence==action.other_games_action.identity.sequence+1&&
              after.projection.competition==prior_ack.owner->page.projection.competition&&
              after.projection.entry_index==prior_ack.owner->page.projection.entry_index&&
              after.projection.linked_page==prior_ack.owner->page.projection.linked_page&&
              after.projection.round==prior_ack.owner->page.projection.round&&
              after.source_participant_offset==prior_ack.owner->participant_offset&&
              after.identity.expected_view_byte==prior_ack.owner->page.identity.expected_view_byte,
              "return preserves selected competition, descriptor, round, linked page and raw participant cursor");
        check(save_stream(std::get<0>(source_expected))==save_stream(session->state())&&
              std::get<1>(source_expected)==session->club_modal_runtime_checkpoint()&&
              session->rand().seed()==std::get<2>(source_expected).seed()&&
              session->rand().calls()==std::get<2>(source_expected).calls()&&
              session->game().seed()==prior_ack.game.seed()&&
              session->game().calls()==prior_ack.game.calls(),
              "accepted TeamInfo matches independent Source squad-entry, bb46e, redraw RNG and complete Native/runtime model");
        const auto returned_runtime=session->club_modal_runtime_checkpoint();
        const auto actual_source_byte=[&](uint32_t address) {
            return session->state().memory.has(address)?session->state().memory.u8(address):
                returned_runtime.at(address);
        };
        const uint32_t strength=uint32_t(actual_source_byte(::bmh::layout::CareerSession_FormatError_ExecutableField3))|
            uint32_t(actual_source_byte(::bmh::layout::CareerSession_FormatError_ExecutableField4))<<8;
        check(strength==std::get<4>(source_expected).best_half_sum&&
              std::get<3>(source_expected)>=0,
              "TeamInfo redraw leaves bd900 at the final displayed fixture team's 0xff average");
    }
}

void test_team_info_forced_detail_boundary(const std::filesystem::path& data) {
    const auto temp=copy_test_data(data,"team-info-forced-detail");
    struct Cleanup { std::filesystem::path path; ~Cleanup(){std::filesystem::remove_all(path);} } cleanup{temp};
    ui::StartupFlow flow(temp);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision=0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    start_career(temp,flow,session,revision,playback,audio_session);

    const auto menu_item=std::find_if(flow.desktop()->state().items.begin(),flow.desktop()->state().items.end(),
        [](const ui::DeskItem& candidate){return candidate.kind==5&&!candidate.disabled;});
    check(menu_item!=flow.desktop()->state().items.end(),"forced-detail fixture opens from a real menu-5 item");
    const auto open=flow.click(menu_item->x+menu_item->width/2,menu_item->y+menu_item->height/2);
    check(open.request==ui::FlowRequest::desk_action,"forced-detail fixture owns the actual menu request");

    auto& memory=session->state().memory;
    const uint32_t entry=cup_entry_address(30),teams=::bmh::layout::FriendlyTeams,dates=::bmh::layout::FriendlyDays;
    memory.set_pointer(entry,teams);memory.set_pointer(entry+0x0c,dates);
    for(uint32_t i=0;i<32;++i)memory.set16(teams+i*2,uint16_t(i));
    memory.set16(::bmh::layout::CalendarState_FormatError_RuntimeField3,200);
    for(uint32_t i=0;i<16;++i)
        memory.set16(dates+i*2,uint16_t(i==0?-1:int16_t(i*2)));
    memory.set8(entry+0x14,4);memory.set8(entry+0x15,0xff);memory.set8(entry+0x16,0);
    memory.set8(entry+0x17,0);memory.set16(entry+0x18,12);memory.set8(entry+0x1a,32);
    memory.set8(entry+0x1b,1);memory.set8(entry+0x1c,0);memory.set8(entry+0x1d,0);
    memory.set8(entry+0x1e,1);
    const std::array<uint8_t,8> selector_ids{0,12,6,20,20,20,12,20};
    for(size_t slot=0;slot<selector_ids.size();++slot)memory.set8(::bmh::layout::ActiveSelectors+uint32_t(slot),selector_ids[slot]);

    std::string error;
    const auto underlay=flow.desktop()->render();
    auto opened=session->open_other_games(open.desk_action,revision,underlay,error);
    check(opened&&error.empty(),"controlled 32-participant Original descriptor opens");
    flow.show_other_games(*opened,session->preparation_projection(revision),open.desk_action);
    const auto id12=std::find_if(flow.other_games()->state().projection.selectors.begin(),
        flow.other_games()->state().projection.selectors.end(),
        [](const OtherGamesSelector& selector){return selector.active&&selector.competition==12;});
    check(id12!=flow.other_games()->state().projection.selectors.end(),
          "controlled descriptor is reached through a visible Source selector");
    const int slot=int(id12-flow.other_games()->state().projection.selectors.begin());
    const int selector_x=44+(slot/4)*90,selector_y=176+(slot%4)*13+5;
    flow.pointer_down(selector_x,selector_y);
    auto select=flow.click(selector_x,selector_y);
    check(select.request==ui::FlowRequest::other_games_action&&
          select.other_games_action.action==ui::OtherGamesAction::select_competition,
          "a physical Source selector click chooses the multi-page descriptor");
    auto selected=session->dispatch_other_games(select.other_games_action,revision,error);
    check(selected&&error.empty(),"multi-page selector action is accepted");
    flow.acknowledge_other_games(selected->page,selected->preparation);
    check(flow.other_games()->state().projection.source_participant_count==32&&
          flow.other_games()->state().projection.display_mode==OtherGamesDisplayMode::detail,
          "the genuine Source successor starts above 16 participants in detail mode");

    flow.pointer_down(230,220);
    auto toggle=flow.click(230,220);
    check(toggle.request==ui::FlowRequest::other_games_action&&
          toggle.other_games_action.action==ui::OtherGamesAction::toggle_display_mode,
          "the Source footer toggles this real page into compact mode");
    auto compact=session->dispatch_other_games(toggle.other_games_action,revision,error);
    check(compact&&error.empty(),"compact display action is accepted");
    flow.acknowledge_other_games(compact->page,compact->preparation);
    const auto compact_before=flow.other_games()->state();
    check(compact_before.projection.source_participant_count==32&&
          compact_before.projection.display_mode==OtherGamesDisplayMode::compact&&
          (compact_before.identity.expected_view_byte&0x80)==0,
          "compact pre-state has 32 participants and format field bit 7 clear");
    check(!compact_before.projection.rows.empty(),"compact boundary fixture has a displayed team row");
    const int team=compact_before.projection.rows.front().home_team;
    const int compact_baseline=((64-compact_before.projection.source_participant_count)/2)*3+26;
    auto team_action=flow.pointer_down(50,compact_baseline-3);
    check(team_action.request==ui::FlowRequest::other_games_action&&
          team_action.other_games_action.action==ui::OtherGamesAction::team_info&&
          team_action.other_games_action.value==team,
          "compact fixture hit opens TeamInfo for the visible home side");

    const uint32_t view_at=::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField+uint32_t(team_action.other_games_action.identity.manager_slot);
    const auto exe=load_le(temp/"BMH.exe");
    const auto texts=load_textd(temp/"TEXTD.BLK");
    const auto model_return=[&](const app::CareerSession::OtherGamesCheckpoint& source) {
        auto expected=source.state;
        auto expected_runtime=source.runtime;
        auto expected_rand=source.rand;
        expected.memory.set8(entry+0x1a,16);
        const uint8_t expected_view=uint8_t(expected.memory.u8(view_at)|0x80);
        expected.memory.set8(view_at,expected_view);

        const auto squad=squad_range(team);
        size_t occupied=0;
        for(size_t row=0;row<squad.count;++row)
            if((expected.squad_slots.at(squad.first+row)&0x0fff)!=0x0fff)++occupied;
        int last=int(occupied)-1;
        while(last>=0&&(expected.squad_slots.at(squad.first+size_t(last))&0xc000)==0)--last;
        for(int row=0;row<=last&&row<17;++row) {
            auto& value=expected.squad_slots.at(squad.first+size_t(row));
            value=uint16_t((value&0x3fff)|(row<11?0x8000:0x4000));
        }
        for(size_t row=0;row<squad.count;++row) {
            auto& value=expected.squad_slots.at(squad.first+row);
            const int player=value&0x0fff;
            if(player!=0x0fff&&size_t(player)<expected.players.size()&&
               ((expected.players[size_t(player)].w364>>16)&0x1f)!=0)value&=0x1fff;
        }

        const auto put_source=[&](uint32_t address,uint8_t value) {
            if(expected.memory.has(address))expected.memory.set8(address,value);
            else expected_runtime[address]=value;
        };
        put_source(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2,0);
        ui::OtherGamesIdentity identity=team_action.other_games_action.identity;
        ++identity.revision;++identity.sequence;identity.expected_view_byte=expected_view;
        auto expected_page=ui::other_games_state_from_game(expected,identity,texts,exe,
            compact_before.projection.competition,compact_before.projection.linked_page,
            compact_before.projection.round,OtherGamesDisplayMode::detail,
            compact_before.source_participant_offset,false,compact_before.underlay,expected_runtime);
        constexpr std::array<uint16_t,5> flags{0x0f1,0x0f2,0x0f4,0x0f8,0x0ff};
        TeamAverage last_average{};
        int last_team=-1;
        for(const auto& row:expected_page.projection.rows) {
            for(const int displayed_team:{row.home_team,row.away_team}) {
                check(displayed_team>=0&&displayed_team<138,
                      "ID12 boundary model stays within squad-backed domestic clubs");
                for(const uint16_t source_flags:flags) {
                    last_average=team_average(expected,displayed_team,source_flags,&expected_rand);
                    put_source(::bmh::layout::CareerSession_FormatError_ExecutableField3,uint8_t(last_average.best_half_sum));
                    put_source(::bmh::layout::CareerSession_FormatError_ExecutableField4,uint8_t(last_average.best_half_sum>>8));
                }
                last_team=displayed_team;
            }
        }
        check(last_team>=0,"ID12 boundary model has a last displayed team");
        const uint16_t scratch=uint16_t(expected.memory.has(::bmh::layout::CareerSession_FormatError_ExecutableField3)
            ?expected.memory.u8(::bmh::layout::CareerSession_FormatError_ExecutableField3):expected_runtime.at(::bmh::layout::CareerSession_FormatError_ExecutableField3))|
            uint16_t(expected.memory.has(::bmh::layout::CareerSession_FormatError_ExecutableField4)
                ?expected.memory.u8(::bmh::layout::CareerSession_FormatError_ExecutableField4):expected_runtime.at(::bmh::layout::CareerSession_FormatError_ExecutableField4))<<8;
        return std::tuple{std::move(expected),std::move(expected_runtime),expected_rand,
                          std::move(expected_page),scratch};
    };
    auto before=session->other_games_checkpoint();
    const auto unseeded_model=model_return(before);
    const uint16_t final_source_scratch=std::get<4>(unseeded_model);
    const uint16_t prior_scratch=uint16_t(final_source_scratch^0xffffu);
    check(prior_scratch!=final_source_scratch,
          "ID12 prior bd900 sentinel differs from the Source-computed final value");
    if(before.state.memory.has(::bmh::layout::CareerSession_FormatError_ExecutableField3)) {
        before.state.memory.set8(::bmh::layout::CareerSession_FormatError_ExecutableField3,uint8_t(prior_scratch));
        before.state.memory.set8(::bmh::layout::CareerSession_FormatError_ExecutableField4,uint8_t(prior_scratch>>8));
    } else {
        before.runtime[::bmh::layout::CareerSession_FormatError_ExecutableField3]=uint8_t(prior_scratch);
        before.runtime[::bmh::layout::CareerSession_FormatError_ExecutableField4]=uint8_t(prior_scratch>>8);
    }
    if(before.owner) {
        before.owner->native=save_stream(before.state);
        before.owner->runtime=before.runtime;
    }
    session->restore_other_games_checkpoint(before);
    before=session->other_games_checkpoint();
    const auto expected_result=model_return(before);
    const uint8_t expected_view=uint8_t(before.state.memory.u8(view_at)|0x80);
    auto accepted=session->dispatch_other_games(team_action.other_games_action,revision,error,
        [&](app::ClubModalServices& services,int presented_team,const Image&) {
            check(presented_team==team,"boundary presenter receives the physical row's team");
            // Controlled boundary only: Source descriptor and round/cursor remain
            // the same while its valid participant count shrinks to 16.
            services.state().memory.set8(entry+0x1a,16);
            (void)services.enter_club(presented_team);
            return true;
        });
    check(accepted&&error.empty(),"staged TeamInfo descriptor-boundary return is accepted");
    check(accepted->page.projection.source_participant_count==16&&
          accepted->page.projection.display_mode==OtherGamesDisplayMode::detail&&
          accepted->page.identity.expected_view_byte==expected_view&&
          accepted->page.identity.sequence==team_action.other_games_action.identity.sequence+1&&
          accepted->page.identity.revision==revision,
          "TeamInfo return crossing 32 compact participants to 16 forces detail and advances the full ACK identity");
    check(accepted->page.projection.competition==compact_before.projection.competition&&
          accepted->page.projection.entry_index==compact_before.projection.entry_index&&
          accepted->page.projection.round==compact_before.projection.round&&
          accepted->page.source_participant_offset==compact_before.source_participant_offset,
          "forced-detail return preserves the accepted competition, descriptor, round and raw cursor");
    check(save_stream(std::get<0>(expected_result))==save_stream(session->state())&&
          std::get<1>(expected_result)==session->club_modal_runtime_checkpoint()&&
          std::get<2>(expected_result).seed()==session->rand().seed()&&
          std::get<2>(expected_result).calls()==session->rand().calls()&&
          before.game.seed()==session->game().seed()&&before.game.calls()==session->game().calls(),
          "ID12 redraw matches independent full Native/runtime and both bounded RNG endpoints");
    const auto actual_runtime=session->club_modal_runtime_checkpoint();
    const auto read_actual=[&](uint32_t address) {
        return session->state().memory.has(address)?session->state().memory.u8(address):actual_runtime.at(address);
    };
    const uint16_t actual_scratch=uint16_t(read_actual(::bmh::layout::CareerSession_FormatError_ExecutableField3))|
        uint16_t(read_actual(::bmh::layout::CareerSession_FormatError_ExecutableField4))<<8;
    check(actual_scratch==final_source_scratch&&actual_scratch!=prior_scratch,
          "ID12 redraw overwrites the distinct bd900 sentinel with the final displayed team's 0xff strength");
    flow.acknowledge_other_games(accepted->page,accepted->preparation);
    check(flow.in_other_games()&&!flow.pending_other_games_action()&&
          flow.other_games()->state().identity==accepted->page.identity&&
          flow.other_games()->state().projection.display_mode==OtherGamesDisplayMode::detail&&
          session->state().memory.u8(view_at)==expected_view,
          "Flow accepts only the valid forced-detail TeamInfo successor and retains its ownership scope");
}

std::filesystem::path copy_test_data(const std::filesystem::path& data,const char* tag) {
    const auto nonce=std::chrono::steady_clock::now().time_since_epoch().count();
    const auto target=std::filesystem::temp_directory_path() /
        (std::string("openbmh-e29-other-games-")+tag+"-"+std::to_string(nonce));
    std::filesystem::create_directories(target);
    for(const auto& entry:std::filesystem::recursive_directory_iterator(data)) {
        const auto relative=std::filesystem::relative(entry.path(),data);
        const auto destination=target/relative;
        if(entry.is_directory()) std::filesystem::create_directories(destination);
        else if(entry.is_regular_file()) {
            std::filesystem::create_directories(destination.parent_path());
            std::filesystem::copy_file(entry.path(),destination);
        }
    }
    return target;
}

void test_controller_rolls_back_asset_failures(const std::filesystem::path& data) {
    {
        const auto temp=copy_test_data(data,"open-rollback");
        try {
            ui::StartupFlow flow(temp);
            std::unique_ptr<app::CareerSession> session;
            uint64_t revision=0;
            openbmh::audio::Playback playback;
            std::optional<uint64_t> audio_session;
            start_career(temp,flow,session,revision,playback,audio_session);
            const auto item=std::find_if(flow.desktop()->state().items.begin(),flow.desktop()->state().items.end(),
                [](const ui::DeskItem& candidate){return candidate.kind==5&&!candidate.disabled;});
            check(item!=flow.desktop()->state().items.end(),"open rollback fixture has menu 5");
            const auto native=save_stream(session->state());
            const auto w=session->rand();
            const auto g=session->game();
            const auto old_revision=revision;
            std::filesystem::remove(temp/"KB"/"129.VGA");
            const auto open=flow.click(item->x+item->width/2,item->y+item->height/2);
            const auto error=app::serve_flow_request(flow,open,session,temp,temp,0x3195ab71,
                revision,playback,audio_session,false);
            check(!error.empty()&&!flow.in_other_games()&&!session->other_games_active()&&
                  revision==old_revision&&save_stream(session->state())==native&&
                  session->rand().seed()==w.seed()&&session->rand().calls()==w.calls()&&
                  session->game().seed()==g.seed()&&session->game().calls()==g.calls(),
                  "failed menu-5 screen construction restores entry Native, RNG and revision");
        } catch(...) { std::filesystem::remove_all(temp); throw; }
        std::filesystem::remove_all(temp);
    }
    {
        const auto temp=copy_test_data(data,"ack-rollback");
        try {
            ui::StartupFlow flow(temp);
            std::unique_ptr<app::CareerSession> session;
            uint64_t revision=0;
            openbmh::audio::Playback playback;
            std::optional<uint64_t> audio_session;
            start_career(temp,flow,session,revision,playback,audio_session);
            const auto item=std::find_if(flow.desktop()->state().items.begin(),flow.desktop()->state().items.end(),
                [](const ui::DeskItem& candidate){return candidate.kind==5&&!candidate.disabled;});
            check(item!=flow.desktop()->state().items.end(),"ACK rollback fixture has menu 5");
            const auto open=flow.click(item->x+item->width/2,item->y+item->height/2);
            check(app::serve_flow_request(flow,open,session,temp,temp,0x3195ab71,revision,
                  playback,audio_session,false).empty()&&flow.in_other_games(),
                  "ACK rollback fixture opens an accepted Other Games owner");
            const auto prior_page=flow.other_games()->state();
            const auto prior_native=save_stream(session->state());
            const auto prior_w=session->rand();
            const auto prior_g=session->game();
            const uint64_t prior_revision=revision;
            check(prior_page.projection.display_mode_toggle_available,
                  "production fixture exposes Source's display toggle for ACK-failure coverage");
            std::filesystem::remove(temp/"KB"/"56.VGA");
            flow.pointer_down(250,220);
            const auto action=flow.click(250,220);
            check(action.request==ui::FlowRequest::other_games_action&&flow.pending_other_games_action(),
                  "toggle creates a real pending typed view action");
            const auto error=app::serve_flow_request(flow,action,session,temp,temp,0x3195ab71,
                revision,playback,audio_session,false);
            check(!error.empty()&&flow.in_other_games()&&!flow.pending_other_games_action()&&
                  flow.other_games()->state().identity==prior_page.identity&&
                  session->other_games_owner()->page.identity==prior_page.identity&&revision==prior_revision&&
                  save_stream(session->state())==prior_native&&session->rand().seed()==prior_w.seed()&&
                  session->rand().calls()==prior_w.calls()&&session->game().seed()==prior_g.seed()&&
                  session->game().calls()==prior_g.calls(),
                  "failed action-screen ACK restores the prior owner, Native, both RNGs and revision for retry");
        } catch(...) { std::filesystem::remove_all(temp); throw; }
        std::filesystem::remove_all(temp);
    }
}
}

int main(int argc,char** argv) {
    try {
        if(argc!=2) throw std::runtime_error("other_games_app_tests requires game data directory");
        test_menu5_source_transaction(argv[1]);
        test_other_games_team_info_return(argv[1]);
        test_team_info_forced_detail_boundary(argv[1]);
        test_controller_rolls_back_asset_failures(argv[1]);
        std::puts("other_games_app_tests: menu-5 Source transaction passed");
        return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"other_games_app_tests: %s\n",e.what());
        return 1;
    }
}
