// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include "app/career_session.hpp"
#include "game/save.hpp"
#include "formats/le.hpp"
#include "ui/league_family_request.hpp"
#include "ui/league_table.hpp"

using namespace bmh;
namespace fs = std::filesystem;
namespace {
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct Fixture {
    app::CareerSession session;
    uint64_t revision = 1;
    ui::StrengthsIdentity id;
    app::CareerSession::LeagueTableDispatch page;
    Fixture(const fs::path& data, const fs::path& save, bool raw_round_0x80=false) : session(data, 0x3710u, save) {
        if(raw_round_0x80) session.state().league_rounds.at(2)=0x80;
        std::string error;
        const auto source = session.strengths_open_request(revision);
        check(bool(source), "loaded WEEK196 has a live manager identity");
        id = source->identity;
        check(id.manager == 0 && id.runtime_club == 45 && id.league == 2 &&
              id.day_counter == session.state().date &&
              id.club_identity == int(session.state().club_ids.at(45)),
              "family identity is checked against the actual WEEK196 manager, club, league and date");
        check(session.prepare_league_family_child(id, 1, revision, error), "Source Table entry prepares its selector context");
        const auto begun = session.begin_league_family(id, 1, revision, error);
        check(bool(begun), "validated Table family opens"); id = *begun;
        const auto current = session.current_league_table_dispatch(error);
        check(bool(current), "active Table family has a source-owned page"); page = *current;
    }
    ui::LeagueFamilyRequest control(int n) const {
        ui::LeagueFamilyRequest r; r.identity=id; r.kind=ui::LeagueFamilyRequestKind::control;
        r.from_child=1; r.control_index=n; return r;
    }
    void ack(const app::CareerSession::LeagueTableDispatch& p) { page=p; id=p.identity; }
};

void ten_controls(const fs::path& data, const fs::path& save) {
    for (int n=0;n<10;++n) {
        Fixture f(data,save); const auto g=f.session.game(); const auto w=f.session.rand();
        const auto native=save_stream(f.session.state()); const auto runtime=f.session.club_modal_runtime_checkpoint();
        std::string error;
        const auto pre=f.session.current_league_table_dispatch(error);
        check(bool(pre), "ready Table projection exists before a control");
        const uint16_t old=pre->identity.expected_family_word;
        auto result=f.session.dispatch_league_table_control(f.control(n),f.revision,error);
        check(bool(result)&&error.empty()&&f.revision==2, "each Source button 0..9 consumes one bound request");
        check(f.session.state().date==f.id.day_counter &&
              f.session.state().club_ids.at(size_t(f.id.runtime_club))==f.id.club_identity,
              "controls retain fixture date and manager-club identity");
        check(f.session.rand().seed()==w.seed()&&f.session.rand().calls()==w.calls(),
              "Table controls do not consume Watcom RNG");
        const uint16_t actual=result->identity.expected_family_word;
        if(n==3) {
            check(result->phase==app::CareerSession::TableDispatchPhase::forecast_pending_present &&
                  result->page.forecast && save_stream(f.session.state())==native &&
                  f.session.club_modal_runtime_checkpoint()==runtime &&
                  f.session.game().seed()==g.seed()&&f.session.game().calls()==g.calls(),
                  "control 3 stages forecast and G until its bound frame-Present ACK");
            uint16_t source_display=uint16_t(old&0xff7fu);
            source_display=uint16_t((source_display|0x0008u)&0x0f7fu);
            source_display=uint16_t((source_display&0x0cf7u)+0x0108u);
            check(actual==old && result->page.display_word==source_display,
                  "KI keeps its live owner word unchanged until Present and draws its independent source display word");
        } else if(n==9) {
            check(result->phase==app::CareerSession::TableDispatchPhase::range_start&&result->dialog&&
                  result->dialog->title_textd_address==::bmh::layout::CareerSession_CurrentLeagueTableDispatch_ExecutableField,
                  "control 9 starts the Source start-round dialog");
        } else {
            check(result->phase==app::CareerSession::TableDispatchPhase::ready,
                  "ordinary controls return to the ready phase");
            const bool normal=(result->page.source_normal_mode!=0);
            uint16_t expected=old;
            if(n<=2) expected=uint16_t((old&0x0ff0u)|uint16_t(1u<<n));
            else if(n<=6) {
                expected=uint16_t(old^uint16_t(0x10u<<(n-4)));
                if((expected&0x70u)==0) expected=uint16_t(expected|0x70u);
            } else if(n==7) expected=uint16_t(old&0x0f7fu);
            else expected=uint16_t((old&0x0cffu)|0x0100u);
            expected=uint16_t(expected&0x0f7fu);
            check(actual==expected && (actual&0x80u)==0,
                  "ready projection uses Source-normalized word with bit7 clear and upper mask exact");
            check(result->page.source_controls_projection,
                  "projection advertises the active Source controls");
            if(n<=2) check(result->page.source_aggregation_flags==(n==0?7:n==1?1:2),
                           "controls 0–2 select their independently specified aggregation flags");
            check(f.session.game().seed()==g.seed()&&f.session.game().calls()==g.calls(),
                  "non-forecast controls preserve the live G stream");
        }
    }
}

void ranges(const fs::path& data,const fs::path& save) {
    Fixture f(data,save); const auto base=f.session.league_family_checkpoint(); std::string error;
    auto start=f.session.dispatch_league_table_control(f.control(9),f.revision,error);
    check(bool(start)&&start->phase==app::CareerSession::TableDispatchPhase::range_start,"start dialog phase is active");
    const auto after_control9=f.session.league_family_checkpoint();
    auto reply=[&](const app::CareerSession::LeagueTableDispatch& p,ui::TableDialogStage stage,
                   ui::TableDialogOutcome outcome,int value,int x=140,int y=80) {
        ui::LeagueFamilyRequest r; r.identity=p.identity;r.kind=ui::LeagueFamilyRequestKind::table_range_result;
        r.from_child=1;r.control_index=9;r.table_dialog_reply=ui::TableDialogReply{stage,outcome,value,x,y};
        return f.session.dispatch_league_table_range_reply(r,f.revision,error);
    };
    check(!reply(*start,ui::TableDialogStage::end,ui::TableDialogOutcome::accept,1),
          "wrong-stage end reply cannot satisfy the first modal");
    auto canceled_first=reply(*start,ui::TableDialogStage::start,ui::TableDialogOutcome::cancel,0,143,86);
    check(bool(canceled_first)&&canceled_first->phase==app::CareerSession::TableDispatchPhase::ready &&
          save_stream(f.session.state())==save_stream(after_control9.state) &&
          f.session.club_modal_runtime_checkpoint()==after_control9.runtime &&
          f.session.game().seed()==after_control9.game.seed()&&f.session.game().calls()==after_control9.game.calls()&&
          f.session.rand().seed()==after_control9.rand.seed()&&f.session.rand().calls()==after_control9.rand.calls(),
          "cancel-first exits cleanly without half-range, Native, or RNG effects");
    f.ack(*canceled_first);
    auto again=f.session.dispatch_league_table_control(f.control(9),f.revision,error);
    auto accepted_start=reply(*again,ui::TableDialogStage::start,ui::TableDialogOutcome::accept,1,151,92);
    check(bool(accepted_start)&&accepted_start->phase==app::CareerSession::TableDispatchPhase::range_end&&
          accepted_start->dialog&&accepted_start->dialog->minimum==1&&
          accepted_start->dialog->x==151&&accepted_start->dialog->y==92,
          "accepted start opens distinct end stage with carried modal position and legal minimum");
    auto wrong_shape=f.control(9);wrong_shape.identity=accepted_start->identity;
    wrong_shape.kind=ui::LeagueFamilyRequestKind::table_range_result;
    wrong_shape.table_dialog_reply=ui::TableDialogReply{ui::TableDialogStage::end,
        static_cast<ui::TableDialogOutcome>(99),0,151,92};
    check(!f.session.dispatch_league_table_range_reply(wrong_shape,f.revision,error),
          "current end-stage owner rejects an invalid typed outcome instead of passing on a stale token");
    const auto before_second_cancel=f.session.league_family_checkpoint().family->table.value();
    auto canceled_second=reply(*accepted_start,ui::TableDialogStage::end,ui::TableDialogOutcome::cancel,0,151,92);
    const auto after_second_cancel=f.session.league_family_checkpoint().family->table.value();
    check(bool(canceled_second)&&canceled_second->phase==app::CareerSession::TableDispatchPhase::ready&&
          after_second_cancel.first_round==before_second_cancel.previous_start_round&&
          after_second_cancel.end_round==before_second_cancel.previous_end_round&&
          after_second_cancel.retained_range_label_source_bytes==before_second_cancel.retained_range_label_source_bytes,
          "second cancel restores the prior endpoints and exact previously retained label, while leaving Source custom-mode effects distinct");
    f.ack(*canceled_second);
    auto equal=f.session.dispatch_league_table_control(f.control(9),f.revision,error);
    auto one=reply(*equal,ui::TableDialogStage::start,ui::TableDialogOutcome::accept,1);
    auto same=reply(*one,ui::TableDialogStage::end,ui::TableDialogOutcome::accept,1);
    check(bool(same)&&same->phase==app::CareerSession::TableDispatchPhase::ready&&
          same->page.retained_range_label_source_bytes.size()>0&&
          f.session.league_family_checkpoint().family->table->first_round==0&&
          f.session.league_family_checkpoint().family->table->end_round==1,
          "equal start/end normalizes to the literal one-round Source range and carries its label");
}

void forecast_range_preserves_visible_and_scratch(const fs::path& data,const fs::path& save) {
    Fixture f(data,save);std::string error;
    auto staged=f.session.dispatch_league_table_control(f.control(3),f.revision,error);
    check(bool(staged)&&staged->page.forecast,"KI fixture reaches the staged forecast page");
    ui::LeagueFamilyRequest present;present.identity=staged->identity;
    present.kind=ui::LeagueFamilyRequestKind::table_forecast_presented;present.from_child=1;present.control_index=3;
    auto accepted=f.session.acknowledge_league_forecast_present(present,f.revision,error);
    check(bool(accepted)&&accepted->page.forecast,"Present ACK retains the visible forecast projection");
    f.ack(*accepted);
    const auto accepted_owner=f.session.league_family_checkpoint().family->table.value();
    auto dialog=f.session.dispatch_league_table_control(f.control(9),f.revision,error);
    check(bool(dialog)&&dialog->phase==app::CareerSession::TableDispatchPhase::range_start&&
          dialog->page==*accepted_owner.forecast_page&&
          f.session.league_family_checkpoint().family->table->page==accepted_owner.page&&
          f.session.league_family_checkpoint().family->table->forecast_page==accepted_owner.forecast_page,
          "opening KB6 preserves both the accepted visible forecast and normal scratch table");
    auto reply=[&](const app::CareerSession::LeagueTableDispatch& p,ui::TableDialogStage stage,
                   ui::TableDialogOutcome outcome,int value) {
        ui::LeagueFamilyRequest r;r.identity=p.identity;r.kind=ui::LeagueFamilyRequestKind::table_range_result;
        r.from_child=1;r.control_index=9;r.table_dialog_reply=ui::TableDialogReply{stage,outcome,value,140,80};
        return f.session.dispatch_league_table_range_reply(r,f.revision,error);
    };
    auto second=reply(*dialog,ui::TableDialogStage::start,ui::TableDialogOutcome::accept,1);
    check(bool(second)&&second->phase==app::CareerSession::TableDispatchPhase::range_end&&
          second->page==*accepted_owner.forecast_page&&
          f.session.league_family_checkpoint().family->table->page==accepted_owner.page&&
          f.session.league_family_checkpoint().family->table->forecast_page==accepted_owner.forecast_page,
          "accepted first modal keeps the original forecast backdrop and scratch through KB6 call two");
    auto ready=reply(*second,ui::TableDialogStage::end,ui::TableDialogOutcome::cancel,0);
    check(bool(ready)&&ready->phase==app::CareerSession::TableDispatchPhase::ready&&!ready->page.forecast&&
          !f.session.league_family_checkpoint().family->table->forecast_page&&
          f.session.league_family_checkpoint().family->table->page==ready->page,
          "returning ready redraws normal Source scratch and clears the transient forecast");
}

void team_info_uses_visible_identity_and_normal_rank(const fs::path& data,const fs::path& save) {
    Fixture f(data,save);std::string error;
    auto staged=f.session.dispatch_league_table_control(f.control(3),f.revision,error);
    ui::LeagueFamilyRequest present;present.identity=staged->identity;
    present.kind=ui::LeagueFamilyRequestKind::table_forecast_presented;present.from_child=1;present.control_index=3;
    auto forecast=f.session.acknowledge_league_forecast_present(present,f.revision,error);
    check(bool(forecast)&&forecast->page.forecast,"TeamInfo fixture has a presented KI table");f.ack(*forecast);
    const auto before=f.session.league_family_checkpoint();
    const auto visible=before.family->table->forecast_page.value();
    const auto normal=before.family->table->page;
    check(!visible.rows.empty()&&!normal.rows.empty(),"visible and normal rank rows are populated");
    size_t clicked=visible.rows.size();
    for(size_t i=0;i<visible.rows.size()&&i<normal.rows.size();++i)
        if(visible.rows[i].runtime_team_id!=normal.rows[i].runtime_team_id) {clicked=i;break;}
    check(clicked<visible.rows.size(),"WEEK196 KI visibly changes at least one normal-scratch rank target");
    ui::LeagueFamilyRequest request;request.identity=f.id;request.kind=ui::LeagueFamilyRequestKind::team_info;
    request.from_child=1;request.table_row_index=int(clicked);
    request.runtime_team=visible.rows[clicked].runtime_team_id;
    request.expected_club_identity=visible.rows[clicked].club_identity_key;
    const int expected_normal_team=normal.rows[clicked].runtime_team_id;
    check(expected_normal_team!=request.runtime_team,"selected normal-scratch club differs from clicked forecast team");
    int entered=-1;bool backdrop_matches=false;
    auto visible_tables=ui::league_tables_from_game(f.session.state(),f.id.manager);
    visible_tables[size_t(f.id.league)]=visible;
    const auto expected_backdrop=ui::LeagueTableScreen(data,visible_tables,f.id.league,
        ui::LeagueTableScreen::FamilyMode{f.id}).render();
    auto stale=request;stale.table_row_index=int((clicked+1)%visible.rows.size());
    const auto stale_revision=f.revision;
    const auto rejected=f.session.dispatch_family_table_team_info(stale,f.revision,error,
        [](app::ClubModalServices&,int,const Image&) { return false; });
    const auto rejected_owner=f.session.league_family_checkpoint();
    check(!rejected&&f.revision==stale_revision&&rejected_owner.family==before.family&&
          rejected_owner.game.calls()==before.game.calls()&&rejected_owner.rand.calls()==before.rand.calls(),
          "stale clicked-row position cannot enter a different visible team or mutate the owner");
    auto refreshed=f.session.dispatch_family_table_team_info(request,f.revision,error,
        [&](app::ClubModalServices& services,int selected,const Image& backdrop) {
            entered=selected;backdrop_matches=backdrop.width==expected_backdrop.width&&
                backdrop.height==expected_backdrop.height&&backdrop.pixels==expected_backdrop.pixels;
            return !services.enter_club(selected).displayed_slots.empty();
        });
    check(bool(refreshed)&&entered==expected_normal_team&&backdrop_matches&&f.revision==4,
          "TeamInfo displays exact accepted forecast pixels while entering the distinct normal-rank club");
    const auto after=f.session.league_family_checkpoint();
    check(after.family&&after.family->table&&after.family->table->phase==app::CareerSession::TableDispatchPhase::ready&&
          !after.family->table->forecast_page&&after.family->table->page.rows.size()==normal.rows.size()&&
          after.family->table->display_word_override==refreshed->expected_family_word,
          "TeamInfo LAB return installs the normalized ready page and clears the consumed forecast view");
}

void control_four_then_domestic_map_preserves_source_locals(const fs::path& data,const fs::path& save) {
    Fixture f(data,save);std::string error;
    auto filter=f.session.dispatch_league_table_control(f.control(4),f.revision,error);
    check(bool(filter)&&filter->phase==app::CareerSession::TableDispatchPhase::ready,
          "Control4 produces a ready Source table before a domestic map change");
    f.ack(*filter);
    auto filter_word=filter->identity.expected_family_word;
    if(filter_word&0x10u) {
        filter=f.session.dispatch_league_table_control(f.control(4),f.revision,error);
        check(bool(filter)&&filter->phase==app::CareerSession::TableDispatchPhase::ready,
              "Control4 can turn the Source suppression bit back off");
        f.ack(*filter);filter_word=filter->identity.expected_family_word;
    }
    check((filter_word&0x10u)==0,
          "fixture reaches the explicit Source suppression-0x10-off selector state");
    const auto before=f.session.league_family_checkpoint();
    const auto retained=before.family->table.value();
    const uint8_t selected_mode=load_le(data/"BMH.exe").u8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField+3);
    const uint16_t common=uint16_t((filter_word&0x0f7fu)|(selected_mode?0x80u:0u));
    const uint16_t redraw=uint16_t(common&0xff7fu);
    ui::LeagueFamilyRequest map;map.identity=f.id;map.kind=ui::LeagueFamilyRequestKind::select_league;
    map.from_child=1;map.league_id=3;
    check(f.session.select_league_family_page(map,f.revision,error),
          "league2-to-league3 domestic map click is accepted");
    const auto dispatch=f.session.current_league_table_dispatch(error);
    const auto after=f.session.league_family_checkpoint();
    check(bool(dispatch)&&dispatch->identity.league==3&&dispatch->identity.league_group==f.id.league_group,
          "domestic map changes league while preserving Source group");
    check(after.family->table->aggregation_flags==retained.aggregation_flags&&
          after.family->table->first_round==retained.first_round&&
          after.family->table->end_round==retained.end_round&&
          after.family->table->retained_range_label_source_bytes==retained.retained_range_label_source_bytes,
          "outer Table redraw retains Control4 aggregation, round range and exact Source range-label bytes");
    check((dispatch->identity.expected_family_word&0x10u)==0&&
          dispatch->identity.expected_family_word==redraw&&
          dispatch->page.display_word==redraw&&
          dispatch->page.source_aggregation_flags==retained.aggregation_flags,
          "map applies exact common-event then outer-redraw word masks and preserves suppression-0x10-off");
    check(after.state.date==before.state.date&&after.runtime.at(::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField)==selected_mode&&
          after.game.seed()==before.game.seed()&&after.game.calls()==before.game.calls()&&
          after.rand.seed()==before.rand.seed()&&after.rand.calls()==before.rand.calls(),
          "different domestic map causes no calendar or RNG mutation");
}

void selector_and_no_lab_range(const fs::path& data,const fs::path& save) {
    Fixture f(data,save); const auto original_club=f.id.runtime_club;
    const auto original_default=f.id.manager_default_league; std::string error;
    auto rejected_selector=[&](ui::LeagueFamilyRequest request) {
        const auto before=f.session.league_family_checkpoint();const auto old_revision=f.revision;
        check(!f.session.select_league_family_page(request,f.revision,error),
              "forged map/group selector is rejected");
        const auto after=f.session.league_family_checkpoint();
        check(f.revision==old_revision&&save_stream(after.state)==save_stream(before.state)&&
              after.runtime==before.runtime&&after.family==before.family&&
              after.game.seed()==before.game.seed()&&after.game.calls()==before.game.calls()&&
              after.rand.seed()==before.rand.seed()&&after.rand.calls()==before.rand.calls(),
              "bad map/group target, group, or sequence rolls back the complete owned checkpoint");
    };
    ui::LeagueFamilyRequest invalid;invalid.identity=f.id;invalid.kind=ui::LeagueFamilyRequestKind::select_league;
    invalid.from_child=1;invalid.league_id=5;rejected_selector(invalid); // foreign map row is not in group 0.
    invalid.identity=f.id;invalid.kind=ui::LeagueFamilyRequestKind::toggle_league_group;
    invalid.from_child=1;invalid.league_id=6;rejected_selector(invalid); // not EXE's group-1 target.
    invalid.identity=f.id;invalid.identity.league_group=1;invalid.league_id=5;rejected_selector(invalid);
    invalid.identity=f.id;invalid.identity.family_sequence++;invalid.league_id=5;rejected_selector(invalid);
    invalid.identity=f.id;invalid.kind=static_cast<ui::LeagueFamilyRequestKind>(255);
    invalid.league_id=2;rejected_selector(invalid);
    invalid.identity=f.id;invalid.kind=ui::LeagueFamilyRequestKind::select_league;
    invalid.league_id=2;invalid.control_index=0;rejected_selector(invalid); // selector cannot carry a Table control.
    invalid.control_index=-1;invalid.table_dialog_reply=ui::TableDialogReply{};rejected_selector(invalid);
    const auto selector_baseline=f.session.league_family_checkpoint();
    auto unavailable=selector_baseline;
    check(unavailable.state.memory.has(::bmh::layout::CalendarState_FormatError_RuntimeField2),"WEEK196 exposes the Source group availability byte");
    unavailable.state.memory.set8(::bmh::layout::CalendarState_FormatError_RuntimeField2,1);
    unavailable.family->native=save_stream(unavailable.state);
    f.session.restore_league_family_checkpoint(std::move(unavailable));
    invalid={};invalid.identity=f.id;invalid.kind=ui::LeagueFamilyRequestKind::toggle_league_group;
    invalid.from_child=1;invalid.league_id=5;rejected_selector(invalid);
    f.session.restore_league_family_checkpoint(selector_baseline);
    auto select=[&](ui::LeagueFamilyRequestKind kind,int league) {
        ui::LeagueFamilyRequest request;request.identity=f.id;request.kind=kind;
        request.from_child=1;request.league_id=league;
        check(f.session.select_league_family_page(request,f.revision,error),
              "bound Table map/group selector is accepted");
        auto result=f.session.current_league_table_dispatch(error);
        check(bool(result),"accepted selector exposes the authoritative Table page");
        f.ack(*result);return *result;
    };
    // WEEK196 is Source group 0. Its map click to 3 changes league but must
    // keep the independently owned group. The Source group button targets 5.
    auto mapped=select(ui::LeagueFamilyRequestKind::select_league,3);
    check(mapped.identity.league==3&&mapped.identity.league_group==0&&
          mapped.identity.runtime_club==original_club&&mapped.identity.manager_default_league==original_default&&
          f.session.club_modal_runtime_checkpoint().at(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField2)==3&&
          f.session.club_modal_runtime_checkpoint().at(::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField)==0,
          "map click changes only the selected league and preserves Source group/manager identity");
    const auto same_page=mapped.page;const auto prior_identity=mapped.identity;
    const uint16_t prior_word=f.session.state().memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(f.id.manager)*2);
    auto same_map=select(ui::LeagueFamilyRequestKind::select_league,3);
    check(same_map.identity.league==3&&same_map.identity.league_group==0&&
          same_map.page==same_page&&same_map.identity.family_sequence==prior_identity.family_sequence+1&&
          f.session.state().memory.u16(::bmh::layout::LeagueTableDisplayModes+uint32_t(f.id.manager)*2)==
              uint16_t((prior_word&0x0f7f)|((f.session.club_modal_runtime_checkpoint().at(::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField)!=0)?0x80:0)),
          "same-league map consumes one owner token and Source event mask while retaining the full body");
    auto toggled=select(ui::LeagueFamilyRequestKind::toggle_league_group,5);
    check(toggled.identity.league==5&&toggled.identity.league_group==1&&
          f.session.club_modal_runtime_checkpoint().at(::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField)==1,
          "group button uses explicit Source XOR target instead of deriving group from league selection");
    auto mapped_group1=select(ui::LeagueFamilyRequestKind::select_league,6);
    check(mapped_group1.identity.league==6&&mapped_group1.identity.league_group==1&&
          f.session.club_modal_runtime_checkpoint().at(::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField)==1,
          "a foreign-map click to league6 preserves the independently bound group1");
    const auto group1_page=mapped_group1.page;
    auto toggled_back=select(ui::LeagueFamilyRequestKind::toggle_league_group,original_default);
    check(toggled_back.identity.league==original_default&&toggled_back.identity.league_group==0&&
          toggled_back.page.league_id==original_default&&
          toggled_back.page!=group1_page,
          "changed-target toggle returns to manager default and rebuilds only the selected league body");
    f.ack(toggled_back);
    auto foreign_default_group1=select(ui::LeagueFamilyRequestKind::toggle_league_group,5);
    check(foreign_default_group1.identity.league==5&&foreign_default_group1.identity.league_group==1,
          "fixture reaches the audited foreign group before the controlled default case");
    // Explicit controlled-domain case only: the Source owner accepts manager
    // defaults 5..8 even though ordinary career creation does not produce them.
    auto controlled=f.session.league_family_checkpoint();
    const size_t manager_record=size_t(f.id.manager)*0xb0c;
    controlled.state.manager_records[manager_record+0x210]=5;
    controlled.family->identity.manager_default_league=5;
    controlled.family->native=save_stream(controlled.state);
    f.session.restore_league_family_checkpoint(std::move(controlled));
    f.id=f.session.league_family_owner()->identity;
    auto controlled_page=f.session.current_league_table_dispatch(error);
    check(bool(controlled_page),"controlled foreign-default owner remains internally consistent");
    f.ack(*controlled_page);
    const auto retained_group1=controlled_page->page;
    auto same_target_toggle=select(ui::LeagueFamilyRequestKind::toggle_league_group,5);
    check(same_target_toggle.identity.league==5&&same_target_toggle.identity.league_group==0&&
          same_target_toggle.page==retained_group1,
          "same-target group toggle preserves the Table body when group0 manager default is league5");
    f.ack(same_target_toggle);
    auto same_target_back=select(ui::LeagueFamilyRequestKind::toggle_league_group,5);
    check(same_target_back.identity.league==5&&same_target_back.identity.league_group==1&&
          same_target_back.page==retained_group1,
          "group1 base toggle back to the same league also retains its body");
    f.ack(same_target_back);
    auto staged=f.session.dispatch_league_table_control(f.control(3),f.revision,error);
    check(bool(staged)&&staged->phase==app::CareerSession::TableDispatchPhase::forecast_pending_present,
          "forecast can be staged before a same-league map event");
    ui::LeagueFamilyRequest presented;presented.identity=staged->identity;
    presented.kind=ui::LeagueFamilyRequestKind::table_forecast_presented;presented.from_child=1;
    presented.control_index=3;
    auto accepted=f.session.acknowledge_league_forecast_present(presented,f.revision,error);
    check(bool(accepted)&&accepted->page.forecast,"forecast is visible after its bound Present ACK");f.ack(*accepted);
    const auto accepted_forecast=accepted->page;
    auto same_after_forecast=select(ui::LeagueFamilyRequestKind::select_league,5);
    const auto retained=f.session.league_family_checkpoint().family->table.value();
    check(same_after_forecast.page==accepted_forecast&&retained.forecast_page&&
          *retained.forecast_page==accepted_forecast,
          "same-league map consumes its event while preserving exact accepted forecast pixels/body");
    f.ack(same_after_forecast);
    const auto before_full=f.session.league_family_checkpoint().family->table.value();
    auto full=f.session.dispatch_league_table_control(f.control(8),f.revision,error);
    const auto after_full=f.session.league_family_checkpoint().family->table.value();
    check(bool(full)&&after_full.first_round==0&&after_full.end_round==league_matchdays(f.id.league)&&
          after_full.retained_range_label_source_bytes==before_full.retained_range_label_source_bytes,
          "Control8 sets all games without LAB and retains exact prior label bytes");
}

void table_group_toggle_is_typed_and_non_optimistic(const fs::path& data,const fs::path& save) {
    Fixture f(data,save);
    auto tables=ui::league_tables_from_game(f.session.state(),f.id.manager);
    auto screen=ui::LeagueTableScreen(data,std::move(tables),f.id.league,
        ui::LeagueTableScreen::FamilyMode{f.id});
    bool emitted=false;
    for(int y=176;y<=216&&!emitted;++y)for(int x=262;x<=317&&!emitted;++x) {
        if(!screen.pointer_down(x,y))continue;
        const auto& pending=screen.pending_family_action();
        check(bool(pending),"group selector DOWN creates its typed request");
        const uint8_t next_group=uint8_t(f.id.league_group^1u);
        const auto exe=load_le(data/"BMH.exe");
        const int expected=next_group?int(exe.u8(::bmh::layout::CareerSession_FormatError_RuntimeField4)):f.id.manager_default_league;
        check(pending->kind==ui::LeagueFamilyRequestKind::toggle_league_group&&
              pending->identity==f.id&&pending->from_child==1&&pending->league_id==expected,
              "group selector carries operation plus UI-computed expected target bound to old owner");
        check(screen.selected_league()==f.id.league&&f.id.league_group!=next_group,
              "group request does not optimistically change the owned screen identity");
        emitted=true;
    }
    check(emitted,"Source map exposes a physical group-selector hit pixel");
}


void invalid_identity_and_close(const fs::path& data,const fs::path& save) {
    Fixture f(data,save); const auto base=f.session.league_family_checkpoint();
    auto rejected_without_change=[&](ui::LeagueFamilyRequest r,uint64_t rev) {
        const auto before=f.session.league_family_checkpoint(); std::string error;
        const auto result=f.session.dispatch_league_table_control(r,rev,error);
        const auto after=f.session.league_family_checkpoint();
        check(!result&&save_stream(after.state)==save_stream(before.state)&&after.runtime==before.runtime&&
              after.family==before.family&&after.game.seed()==before.game.seed()&&after.game.calls()==before.game.calls()&&
              after.rand.seed()==before.rand.seed()&&after.rand.calls()==before.rand.calls(),
              "invalid identity or revision is rejected without mutating current checkpoint");
    };
    auto r=f.control(0);r.identity.day_counter++;
    rejected_without_change(r,f.revision);
    r=f.control(0);r.identity.club_identity++;
    rejected_without_change(r,f.revision);
    r=f.control(0);r.identity.manager++;
    rejected_without_change(r,f.revision);
    r=f.control(0);rejected_without_change(r,f.revision+1);
    const auto original=f.session.state();f.session.state().date++;
    r=f.control(0);rejected_without_change(r,f.revision);
    f.session.state()=original;

    ui::LeagueFamilyRequest close;close.identity=f.id;close.kind=ui::LeagueFamilyRequestKind::close;
    close.from_child=1;close.target_child=0;
    const auto w=f.session.rand();const auto g=f.session.game();
    const uint32_t ws=w.seed()*0x41c64e6du+0x3039u;
    const uint32_t count=(((ws>>16)&32767u)*201u/32768u)+100u;
    int32_t gs=g.seed();
    for(uint32_t i=0;i<count;++i)gs=int32_t(uint32_t(gs)*16u+167u)%4000000;
    std::string error;auto desk=f.session.close_league_family(close,f.revision,error);
    check(bool(desk)&&f.revision==2&&f.session.rand().seed()==ws&&f.session.rand().calls()==w.calls()+1&&
          f.session.game().seed()==gs&&f.session.game().calls()==g.calls()+count,
          "valid final close consumes the literal W(100,300) then N G(0,1) source stir exactly once");
    const auto after=f.session.league_family_checkpoint();
    check(!f.session.close_league_family(close,f.revision,error)&&f.session.rand().seed()==after.rand.seed()&&
          f.session.rand().calls()==after.rand.calls()&&f.session.game().seed()==after.game.seed()&&
          f.session.game().calls()==after.game.calls(),
          "duplicate consumed close is rejected and RNG-neutral");
    (void)base;
}

void forecast_present_and_rejections(const fs::path& data,const fs::path& save) {
    Fixture f(data,save);const auto native=save_stream(f.session.state());
    const auto runtime=f.session.club_modal_runtime_checkpoint();const auto G=f.session.game();const auto W=f.session.rand();
    std::string error;auto staged=f.session.dispatch_league_table_control(f.control(3),f.revision,error);
    check(bool(staged)&&staged->phase==app::CareerSession::TableDispatchPhase::forecast_pending_present,
          "first KI click enters staged-present phase");
    auto present=[&](const ui::StrengthsIdentity& id) {ui::LeagueFamilyRequest r;r.identity=id;
        r.kind=ui::LeagueFamilyRequestKind::table_forecast_presented;r.from_child=1;r.control_index=3;
        return f.session.acknowledge_league_forecast_present(r,f.revision,error);};
    auto wrong=staged->identity;wrong.family_sequence++;
    check(!present(wrong)&&save_stream(f.session.state())==native&&f.session.game().calls()==G.calls(),
          "wrong/stale Present token cannot commit staged G");
    check(save_stream(f.session.state())==native&&f.session.club_modal_runtime_checkpoint()==runtime&&
          f.session.rand().seed()==W.seed()&&f.session.rand().calls()==W.calls()&&
          f.session.game().seed()==G.seed()&&f.session.game().calls()==G.calls(),
          "before actual-frame ACK every live Native/runtime/W/G value is unchanged");
    auto first=present(staged->identity);
    check(bool(first)&&first->phase==app::CareerSession::TableDispatchPhase::ready&&
          f.session.game().calls()>G.calls()&&f.session.state().date==f.id.day_counter&&
          f.session.club_modal_runtime_checkpoint()==runtime&&
          (first->identity.expected_family_word&0x80u)==0,
          "bound ACK commits forecast G while retaining calendar and returns to ordinary mode");
    f.ack(*first);
    auto staged2=f.session.dispatch_league_table_control(f.control(3),f.revision,error);
    check(bool(staged2)&&staged2->phase==app::CareerSession::TableDispatchPhase::forecast_pending_present,
          "a second KI click is accepted only after first ACK and stages a fresh forecast");
    auto second=present(staged2->identity);
    check(bool(second)&&second->phase==app::CareerSession::TableDispatchPhase::ready,
          "second forecast has its own bound Present completion");
    check(!present(staged2->identity),"duplicate consumed Present request is rejected");

    Fixture bad(data,save);const auto checkpoint=bad.session.league_family_checkpoint();
    auto reject_same=[&](ui::LeagueFamilyRequest r) {
        std::string why;const auto out=bad.session.dispatch_league_table_control(r,bad.revision,why);
        const auto now=bad.session.league_family_checkpoint();
        check(!out&&save_stream(now.state)==save_stream(checkpoint.state)&&now.runtime==checkpoint.runtime&&
              now.family==checkpoint.family&&now.game.seed()==checkpoint.game.seed()&&
              now.game.calls()==checkpoint.game.calls()&&now.rand.seed()==checkpoint.rand.seed()&&
              now.rand.calls()==checkpoint.rand.calls()&&bad.session.state().date==checkpoint.state.date,
              "invalid/stale typed request leaves the complete Session checkpoint fixed");
    };
    auto r=bad.control(0);r.from_child=2;reject_same(r);
    r=bad.control(0);r.identity.manager++;reject_same(r);
    r=bad.control(0);r.identity.sequence++;reject_same(r);
    r=bad.control(10);reject_same(r);
    r=bad.control(0);r.kind=ui::LeagueFamilyRequestKind::table_range_result;reject_same(r);
    r=bad.control(0);r.identity.source_param2=1;reject_same(r);
    auto close=bad.control(0);close.kind=ui::LeagueFamilyRequestKind::close;
    close.identity.family_id++;close.target_child=0;
    std::string why;check(!bad.session.close_league_family(close,bad.revision,why)&&
        save_stream(bad.session.state())==save_stream(checkpoint.state)&&
        bad.session.game().calls()==checkpoint.game.calls()&&bad.session.rand().calls()==checkpoint.rand.calls(),
        "forged unowned final close is rejected without RNG effects");
}
}
int main(int argc,char** argv) {
    try {
        check(argc==3,"usage: league_table_controls_app_tests <data> <WEEK196.FBM>");
        const fs::path data(argv[1]),save(argv[2]);
        ten_controls(data,save);ranges(data,save);forecast_present_and_rejections(data,save);
        forecast_range_preserves_visible_and_scratch(data,save);team_info_uses_visible_identity_and_normal_rank(data,save);
        invalid_identity_and_close(data,save);control_four_then_domestic_map_preserves_source_locals(data,save);
        selector_and_no_lab_range(data,save);
        table_group_toggle_is_typed_and_non_optimistic(data,save);
        Fixture high_round(data,save,true);
        std::string high_error;auto high_range=high_round.session.dispatch_league_table_control(high_round.control(9),high_round.revision,high_error);
        check(bool(high_range),"0x80 round-byte fixture opens the range selector");
        ui::LeagueFamilyRequest high_start;high_start.identity=high_range->identity;high_start.kind=ui::LeagueFamilyRequestKind::table_range_result;
        high_start.from_child=1;high_start.control_index=9;
        high_start.table_dialog_reply=ui::TableDialogReply{ui::TableDialogStage::start,ui::TableDialogOutcome::accept,1,140,80};
        auto high_end=high_round.session.dispatch_league_table_range_reply(high_start,high_round.revision,high_error);
        check(bool(high_end),"0x80 fixture accepts a Source-valid start round");
        ui::LeagueFamilyRequest high_finish;high_finish.identity=high_end->identity;high_finish.kind=ui::LeagueFamilyRequestKind::table_range_result;
        high_finish.from_child=1;high_finish.control_index=9;
        high_finish.table_dialog_reply=ui::TableDialogReply{ui::TableDialogStage::end,ui::TableDialogOutcome::accept,10,140,80};
        auto high_ready=high_round.session.dispatch_league_table_range_reply(high_finish,high_round.revision,high_error);
        check(bool(high_ready)&&high_round.session.league_family_checkpoint().family->table->end_round==10,
              "raw 0x80 compares as unsigned current round against signed end and preserves accepted range end");
        std::cout<<"Table controls/range/forecast Session protocol assertions completed\n";return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
