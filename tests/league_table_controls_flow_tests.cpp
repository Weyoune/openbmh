// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
// Controller/Flow integration coverage for the Source Table UI protocol.
// These tests simulate the SDL host's real-present callback sequence; they are
// not evidence of an actual SDL window run. The separate E37 root QA runner
// owns physical SDL captures and fresh-process reload proof.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "app/controller.hpp"
#include "app/career_session.hpp"
#include "formats/le.hpp"
#include "game/save.hpp"
#include "game/matchday.hpp"
#include "ui/flow.hpp"

using namespace bmh;
namespace fs = std::filesystem;

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

struct Context {
    fs::path data;
    fs::path dir;
    ui::StartupFlow flow;
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback audio;
    std::optional<uint64_t> audio_session;

    Context(const fs::path& data_dir, const fs::path& genuine_week196,int menu_kind=0)
        : data(data_dir),
          dir(fs::temp_directory_path() / ("e37-table-flow-" + std::to_string(
              std::chrono::steady_clock::now().time_since_epoch().count()))),
          flow(data_dir, dir) {
        fs::create_directories(dir);
        fs::copy_file(genuine_week196, dir / "AUTOSAVE.FBM", fs::copy_options::overwrite_existing);
        audio.set_muted(true);
        check(serve(flow.click(214, 215)).empty(), "open the saved-career menu");
        check(serve(flow.send_key("ENTER")).empty() && session && revision == 1,
              "load the unchanged WEEK196 through the real Controller/Flow path");
        const auto& items = flow.desktop()->state().items;
        const auto table = std::find_if(items.begin(), items.end(), [menu_kind](const auto& item) {
            return item.kind == menu_kind && !item.disabled;
        });
        check(table != items.end(), "loaded desk exposes its Source Table command");
        const auto open=flow.click(table->x + table->width / 2, table->y + table->height / 2);
        const auto open_error=serve(open);
        if(!open_error.empty()||!flow.in_family_table())
            throw std::runtime_error("controller binds the Table child to the loaded career family: "+
                open_error+"; screen="+flow.screen_name()+"; request="+std::to_string(int(open.request)));
    }

    ~Context() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    std::string serve(const ui::FlowResult& request) {
        return app::serve_flow_request(flow, request, session, data, dir, 0x1789abcd,
            revision, audio, audio_session, false);
    }

    ui::FlowResult footer(int index) {
        // legacy routine footer controls arm on DOWN and commit on matching UP.
        const int x = 1 + (index / 4) * 65 + 31;
        const int y = 176 + (index % 4) * 13 + 5;
        (void)flow.pointer_down(x, y);
        return flow.click(x, y);
    }
};

void forecast_present_ack_is_a_flow_boundary(const fs::path& data, const fs::path& save,int menu_kind=0) {
    Context c(data,save,menu_kind);
    const auto native = save_stream(c.session->state());
    const auto runtime = c.session->club_modal_runtime_checkpoint();
    const auto game = c.session->game();
    const auto watcom = c.session->rand();

    const auto draw_only = c.flow.render();
    (void)draw_only;
    check(c.session->game().seed() == game.seed() && c.session->game().calls() == game.calls() &&
          c.session->rand().seed() == watcom.seed() && c.session->rand().calls() == watcom.calls(),
          "render/getters do not commit or advance staged KI RNG");

    const auto revision_before_first = c.revision;
    const auto click = c.footer(3);
    check(click.request == ui::FlowRequest::league_family_action,
          "footer KI release creates a typed family request");
    check(c.serve(click).empty() && c.revision == revision_before_first + 1,
          "Controller stages KI without an implicit Present ACK");
    check(save_stream(c.session->state()) == native &&
          c.session->club_modal_runtime_checkpoint() == runtime &&
          c.session->game().seed() == game.seed() && c.session->game().calls() == game.calls() &&
          c.session->rand().seed() == watcom.seed() && c.session->rand().calls() == watcom.calls(),
          "forecast staging leaves Native/runtime/W/G live state untouched");
    (void)c.flow.render();
    check(save_stream(c.session->state()) == native && c.session->game().calls() == game.calls(),
          "a second getter/render still cannot acknowledge the forecast");

    const auto first_present = c.flow.after_family_table_present(10);
    check(first_present.request == ui::FlowRequest::league_family_action &&
          first_present.league_family_action.kind == ui::LeagueFamilyRequestKind::table_forecast_presented,
          "only the explicit actual-present callback emits the bound KI ACK");
    check(save_stream(c.session->state()) == native && c.session->game().calls() == game.calls(),
          "the ACK remains unapplied until the Controller serves it");
    check(c.serve(first_present).empty(), "Controller accepts the first presented KI frame");
    check(c.session->game().calls() > game.calls() && c.revision == revision_before_first + 2,
          "serving the first physical-present ACK commits the staged G work and advances revision");

    const auto revision_before_second = c.revision;
    const auto second_click = c.footer(3);
    check(second_click.request == ui::FlowRequest::league_family_action && c.serve(second_click).empty() &&
          c.revision == revision_before_second + 1,
          "a second KI click creates a distinct staged transaction");
    const auto second_present = c.flow.after_family_table_present(11);
    check(second_present.request == ui::FlowRequest::league_family_action &&
          second_present.league_family_action.kind == ui::LeagueFamilyRequestKind::table_forecast_presented,
          "the second KI transaction requires its own actual-present callback");
    check(c.serve(second_present).empty() && c.revision == revision_before_second + 2,
          "second present ACK is served exactly once as the next transaction");
}

void same_league_map_ack_retains_forecast_and_rejects_forged_target(const fs::path& data,const fs::path& save,int menu_kind=0) {
    Context c(data,save,menu_kind);
    const auto initial=*c.flow.league_family_identity();
    const auto click=c.footer(3);
    check(click.request==ui::FlowRequest::league_family_action&&c.serve(click).empty(),
          "same-league fixture stages KI through Controller");
    const auto present=c.flow.after_family_table_present(10);
    check(present.request==ui::FlowRequest::league_family_action&&
          present.league_family_action.kind==ui::LeagueFamilyRequestKind::table_forecast_presented&&
          c.serve(present).empty(),"forecast Present ACK is committed before same-map selector test");
    const Image forecast=c.flow.render();
    const auto old=*c.flow.league_family_identity();
    int target_x=-1,target_y=-1;ui::FlowResult same_map;
    for(int y=176;y<=216&&target_x<0;++y)for(int x=205;x<=260&&target_x<0;++x) {
        const auto event=c.flow.pointer_down(x,y);
        if(event.request!=ui::FlowRequest::league_family_action)continue;
        if(event.league_family_action.kind==ui::LeagueFamilyRequestKind::select_league&&
           event.league_family_action.league_id==old.league) {
            target_x=x;target_y=y;same_map=event;
        } else c.flow.reject_league_family("test-only map-coordinate probe");
    }
    check(target_x>=0,"visible Source map exposes the already-selected league pixel");
    check(!same_map.changed,"same-league map request preserves the last presented frame");
    const auto before_native=save_stream(c.session->state());
    const auto before_runtime=c.session->club_modal_runtime_checkpoint();
    const auto before_game=c.session->game();const auto before_rand=c.session->rand();
    const auto before_revision=c.revision;
    auto forged=same_map;forged.league_family_action.league_id=(old.league+1)%9;
    check(!c.serve(forged).empty()&&save_stream(c.session->state())==before_native&&
          c.session->club_modal_runtime_checkpoint()==before_runtime&&c.session->game().seed()==before_game.seed()&&
          c.session->game().calls()==before_game.calls()&&c.session->rand().seed()==before_rand.seed()&&
          c.session->rand().calls()==before_rand.calls()&&c.revision==before_revision&&
          c.flow.league_family_identity()==std::optional<ui::StrengthsIdentity>(old),
          "forged selector target is rejected atomically against pending owner");
    const auto valid=c.flow.pointer_down(target_x,target_y);
    check(valid.request==ui::FlowRequest::league_family_action&&
          valid.league_family_action.kind==ui::LeagueFamilyRequestKind::select_league&&
          valid.league_family_action.league_id==old.league&&c.serve(valid).empty(),
          "same-league map request consumes only its accepted family token");
    check(c.flow.league_family_identity()->family_sequence==old.family_sequence+1&&
          c.flow.league_family_identity()->league_group==initial.league_group&&
          c.flow.render().pixels==forecast.pixels,
          "same-league ACK advances sequence while preserving forecast pixels and owner group");
    (void)c.flow.click(target_x,target_y); // consume the synchronous selector's orphan UP

    ui::FlowResult toggle;int tx=-1,ty=-1;
    for(int y=176;y<=216&&tx<0;++y)for(int x=262;x<=317&&tx<0;++x) {
        auto event=c.flow.pointer_down(x,y);
        if(event.request==ui::FlowRequest::league_family_action&&
           event.league_family_action.kind==ui::LeagueFamilyRequestKind::toggle_league_group) {
            tx=x;ty=y;toggle=event;
        }
    }
    check(tx>=0&&toggle.changed&&toggle.league_family_action.league_id>=0&&
          toggle.league_family_action.league_id<9,
          "group-toggle physical DOWN carries its expected league and requests selector redraw");
    const auto before_toggle_game=c.session->game();const auto before_toggle_rand=c.session->rand();
    const auto before_toggle_group=c.flow.league_family_identity()->league_group;
    const int expected_toggle_league=toggle.league_family_action.league_id;
    check(c.serve(toggle).empty()&&c.flow.league_family_identity()->league_group==(before_toggle_group^1u)&&
          c.flow.league_family_identity()->league==expected_toggle_league&&
          c.session->club_modal_runtime_checkpoint().at(::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField)==(before_toggle_group^1u)&&
          c.session->club_modal_runtime_checkpoint().at(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField2)==expected_toggle_league&&
          c.session->game().seed()==before_toggle_game.seed()&&c.session->game().calls()==before_toggle_game.calls()&&
          c.session->rand().seed()==before_toggle_rand.seed()&&c.session->rand().calls()==before_toggle_rand.calls(),
          "accepted group operation ACKs the exact target/group without another RNG transaction");
    (void)c.flow.click(tx,ty); // consume the synchronous selector's orphan UP
}

void range_waits_for_present_and_source_units(const fs::path& data, const fs::path& save,int menu_kind=0) {
    Context c(data,save,menu_kind);
    const auto open = c.footer(9);
    check(open.request == ui::FlowRequest::league_family_action && c.serve(open).empty() && c.flow.in_family_table_range_dialog(),
          "control 9 enters Source-owned range modal through Controller");

    // The initial frame must be presented before the modal accepts input.
    check(!c.flow.family_table_range_input_ready(), "modal starts behind its initial-present gate");
    (void)c.flow.render();
    check(c.flow.after_family_table_present(0).request == ui::FlowRequest::none &&
          c.flow.family_table_range_input_ready(), "initial actual-present callback opens modal input");

    const auto enter = c.flow.send_key("ENTER");
    (void)enter;
    check(!c.flow.pending_league_family_action(), "accept key does not dispatch before its flash frames");
    (void)c.flow.render();
    (void)c.flow.after_family_table_present(100); // pressed frame, anchoring the four Source units
    (void)c.flow.tick_family_table_source_units(103);
    check(!c.flow.pending_league_family_action(), "three elapsed Source units are insufficient");
    const auto changed = c.flow.tick_family_table_source_units(104);
    check(changed.changed && !c.flow.pending_league_family_action(),
          "the fourth Source unit changes the modal value but is not a typed result");
    (void)c.flow.render();
    const auto updated_modal_present = c.flow.after_family_table_present(104);
    check(updated_modal_present.changed &&
          updated_modal_present.request != ui::FlowRequest::league_family_action &&
          !c.flow.pending_league_family_action(),
          "the updated modal is presented before the exit frame is requested");
    (void)c.flow.render(); // modal's Source exit-frame restoration
    const auto exit_present = c.flow.after_family_table_present(104);
    check(exit_present.request == ui::FlowRequest::league_family_action &&
          exit_present.league_family_action.kind == ui::LeagueFamilyRequestKind::table_range_result &&
          exit_present.league_family_action.table_dialog_reply &&
          exit_present.league_family_action.table_dialog_reply->stage == ui::TableDialogStage::start,
          "a separate backdrop/exit present produces the start-stage reply");

    // A malformed phase/revision ACK is rejected before any family/session effect;
    // keep the legitimate pending request intact so the Controller can retry it.
    const auto before_native = save_stream(c.session->state());
    const auto before_runtime = c.session->club_modal_runtime_checkpoint();
    const auto before_game = c.session->game();
    const auto before_rng = c.session->rand();
    const auto before_revision = c.revision;
    auto stale = exit_present;
    stale.league_family_action.identity.revision += 1;
    check(!c.serve(stale).empty(), "wrong-revision dialog ACK is rejected");
    check(save_stream(c.session->state()) == before_native &&
          c.session->club_modal_runtime_checkpoint() == before_runtime &&
          c.session->game().seed() == before_game.seed() && c.session->game().calls() == before_game.calls() &&
          c.session->rand().seed() == before_rng.seed() && c.session->rand().calls() == before_rng.calls() &&
          c.revision == before_revision && c.flow.pending_league_family_action(),
          "rejected ACK preserves live session checkpoint, revision and retryable Flow request");
    auto wrong_phase = exit_present;
    wrong_phase.league_family_action.table_dialog_reply->stage = ui::TableDialogStage::end;
    check(!c.serve(wrong_phase).empty(), "wrong-phase dialog ACK is rejected");
    check(save_stream(c.session->state()) == before_native &&
          c.session->club_modal_runtime_checkpoint() == before_runtime &&
          c.session->game().seed() == before_game.seed() && c.session->game().calls() == before_game.calls() &&
          c.session->rand().seed() == before_rng.seed() && c.session->rand().calls() == before_rng.calls() &&
          c.revision == before_revision && c.flow.pending_league_family_action(),
          "wrong-phase rejection is atomic and leaves the real reply retryable");
    check(c.serve(exit_present).empty() && c.revision == before_revision + 1,
          "the exact source-bound start reply remains acceptable after stale rejection");
    check(c.flow.in_family_table_range_dialog(),
          "accepted start result serves the second dialog stage");
    check(!c.flow.family_table_range_input_ready(),
          "the end dialog has its own initial-present gate");
    (void)c.flow.render();
    (void)c.flow.after_family_table_present(105);
    check(c.flow.family_table_range_input_ready(),
          "only presenting the end dialog opens its input gate");
}

void range_process_deadline(const fs::path& data, const fs::path& save,int menu_kind=0) {
    for (bool end_stage : {false,true}) {
        Context c(data,save,menu_kind);
        check(c.serve(c.footer(9)).empty(), "deadline opens the ordinary range control");
        if (end_stage) {
            (void)c.flow.render(); (void)c.flow.after_family_table_present(0);
            c.flow.send_key("ENTER");
            (void)c.flow.render(); (void)c.flow.after_family_table_present(0);
            (void)c.flow.tick_family_table_source_units(4);
            (void)c.flow.render(); (void)c.flow.after_family_table_present(4);
            (void)c.flow.render();
            check(c.serve(c.flow.after_family_table_present(4)).empty() && c.flow.in_family_table_range_dialog(),
                  "confirmed start reaches the separately owned end dialog");
        }
        const auto native=save_stream(c.session->state());const auto w=c.session->rand();const auto g=c.session->game();
        const auto revision=c.revision;
        check(!c.flow.process_timer_expired().changed,"deadline retains the range dialog's initial Present");
        (void)c.flow.render(); (void)c.flow.after_family_table_present(100);
        c.flow.send_key("3");
        check(!c.flow.process_timer_expired().changed,"deadline retains the unpresented digit flash");
        (void)c.flow.render(); (void)c.flow.after_family_table_present(100);
        (void)c.flow.tick_family_table_source_units(103);
        check(!c.flow.process_timer_expired().changed,"range digit owns all four Source wait units");
        (void)c.flow.tick_family_table_source_units(104);
        check(!c.flow.process_timer_expired().changed,"released range key requires its updated frame");
        (void)c.flow.render(); (void)c.flow.after_family_table_present(104);
        const auto cancel=c.flow.process_timer_expired();
        check(cancel.changed && cancel.request==ui::FlowRequest::none && !c.flow.pending_league_family_action() &&
              save_stream(c.session->state())==native && c.revision==revision,
              "deadline cancels locally without exposing a reply or saved writes before the exit frame");
        check(!c.flow.process_timer_expired().changed,"repeated expiry cannot skip the exit Present");
        (void)c.flow.render();const auto reply=c.flow.after_family_table_present(104);
        check(reply.request==ui::FlowRequest::league_family_action && reply.league_family_action.table_dialog_reply &&
              reply.league_family_action.table_dialog_reply->outcome==ui::TableDialogOutcome::cancel &&
              reply.league_family_action.table_dialog_reply->stage==(end_stage?ui::TableDialogStage::end:ui::TableDialogStage::start),
              "exit Present emits exactly the owned start/end cancellation reply");
        const auto pending=c.flow.state_json();
        check(!c.flow.process_timer_expired().changed && c.flow.state_json()==pending,"range reply retains its ACK under repeated expiry");
        auto expected=c.session->state();
        if(end_stage) {
            const auto address=::bmh::layout::LeagueTableDisplayModesUnsigned+uint32_t(c.flow.league_family_identity()->manager)*2;
            expected.memory.set16(address,uint16_t(((expected.memory.u16(address)&0x0cffu)|0x0200u)&0x0f7fu));
        }
        check(c.serve(reply).empty() && c.flow.in_family_table() && !c.flow.in_family_table_range_dialog() &&
              save_stream(c.session->state())==save_stream(expected),
              "cancel ACK restores previous range and only the Source end-stage display word");
        const auto accepted_revision=c.revision;const auto accepted_native=save_stream(c.session->state());
        check(!c.serve(reply).empty() && c.revision==accepted_revision && save_stream(c.session->state())==accepted_native,
              "replayed range reply is inert after its accepted ACK");
        check(c.session->rand().seed()==w.seed() && c.session->rand().calls()==w.calls() &&
              c.session->game().seed()==g.seed() && c.session->game().calls()==g.calls(),
              "range cancel and rejected replay retain both Source RNG streams");
        auto expected_w=w;auto expected_g=g;
        if(menu_kind==0)stir_random(expected_g,expected_w);
        check(c.serve(c.flow.process_timer_expired()).empty() && c.flow.screen_name()=="desktop" &&
              c.session->rand().seed()==expected_w.seed() && c.session->rand().calls()==expected_w.calls() &&
              c.session->game().seed()==expected_g.seed() && c.session->game().calls()==expected_g.calls(),
              "next outer poll retains the caller-specific Source RNG return");
        check(c.flow.process_timer_expired().request==ui::FlowRequest::kickoff && !c.flow.process_timer_expired().changed,
              "restored Table desk emits exactly one normal kickoff");
        std::cout << "PASS: range process deadline stage=" << (end_stage?"end":"start") << '\n';
    }
}

void cancel_wait_anchors_at_present(const fs::path& data, const fs::path& save,int menu_kind=0) {
    Context c(data,save,menu_kind);
    const auto table_underlay = c.flow.render();
    const auto open = c.footer(9);
    check(open.request == ui::FlowRequest::league_family_action && c.serve(open).empty(), "open range dialog for cancellation test");
    (void)c.flow.after_family_table_present(0);
    // Source Cancel button 14 has an inclusive hitbox; coordinates from the
    // reviewed legacy routine dialog contract are exercised by the dialog API.
    // Flow deliberately has no guessed ESC or SDL-right-button mapping.
    const LeImage exe = load_le(data / "BMH.exe");
    const int cancel_x = 140 + exe.u16(::bmh::layout::Advertising_Click_RuntimeField + 14 * 2) + 4;
    const int cancel_y = 80 + exe.u8(::bmh::layout::Advertising_Click_RuntimeField2 + 14) + 4;
    const auto down = c.flow.pointer_down(cancel_x, cancel_y);
    (void)down;
    (void)c.flow.tick_family_table_source_units(1000);
    check(!c.flow.pending_league_family_action(), "cancel timer cannot advance before its action frame is presented");
    (void)c.flow.render();
    (void)c.flow.after_family_table_present(100);
    (void)c.flow.tick_family_table_source_units(120);
    (void)c.flow.click(cancel_x, cancel_y); // matching physical UP; does not re-anchor the wait
    (void)c.flow.render();
    (void)c.flow.after_family_table_present(120);
    (void)c.flow.tick_family_table_source_units(149);
    check(!c.flow.pending_league_family_action(), "49 post-present units do not finish the cancel wait");
    (void)c.flow.tick_family_table_source_units(150);
    (void)c.flow.render();
    const auto result = c.flow.after_family_table_present(150);
    check(result.request == ui::FlowRequest::league_family_action && result.league_family_action.table_dialog_reply &&
          result.league_family_action.table_dialog_reply->outcome == ui::TableDialogOutcome::cancel,
          "exactly 50 source units and the exit-frame present deliver the physical Cancel result");
    check(c.serve(result).empty() && c.flow.render().pixels == table_underlay.pixels,
          "cancel returns to the exact previously rendered Table underlay pixels");
}
void friendly_controls_and_normal_rank_return(const fs::path& data,const fs::path& save) {
    Context c(data,save,6);
    check(c.flow.friendly_table_active() && c.session->league_family_owner()->friendly_opponent,
          "menu6 uses direct opponent purpose rather than the outer family close");
    const auto entry=*c.flow.league_family_identity();
    check((entry.expected_family_word&0x03f0)==0x0170,
          "Source case6 resets the table format before its first visible page");
    const auto w=c.session->rand();const auto g=c.session->game();
    for(int control : {0,1,2,4,5,6,7,8}) {
        const int x=32+(control/4)*65,y=181+(control%4)*13;
        const auto before=save_stream(c.session->state());const auto revision=c.revision;
        check(c.flow.pointer_down(x,y).request==ui::FlowRequest::none &&
              save_stream(c.session->state())==before && c.revision==revision,
              "opponent footer DOWN holds the physical widget without committing its control");
        const auto release=c.flow.click(x,y);
        check(release.request==ui::FlowRequest::league_family_action &&
              release.league_family_action.kind==ui::LeagueFamilyRequestKind::control &&
              release.league_family_action.control_index==control,
              "matching physical UP submits the exact opponent-table control");
        auto forged=release;++forged.league_family_action.identity.family_sequence;
        check(!c.serve(forged).empty() && c.flow.pending_league_family_action() &&
              save_stream(c.session->state())==before && c.revision==revision,
              "forged control leaves the valid pending opponent request and native state intact");
        check(c.serve(release).empty() && c.revision==revision+1 &&
              c.session->league_family_owner()->friendly_opponent && c.flow.friendly_table_active(),
              "accepted control refreshes the owned direct table and preparation revision");
        const auto page=c.flow.friendly_opponent_picker()->state();
        if(control<=2)check(page.source_aggregation_flags==(control==0?7:control==1?1:2),
              "total/home/away use the Source engine aggregation rather than local UI totals");
        check(page.source_controls_projection && !page.forecast &&
              c.session->rand().seed()==w.seed() && c.session->rand().calls()==w.calls() &&
              c.session->game().seed()==g.seed() && c.session->game().calls()==g.calls(),
              "ordinary controls retain both RNG streams and publish an engine table");
        const auto accepted=save_stream(c.session->state());const auto accepted_revision=c.revision;
        check(!c.serve(release).empty() && c.revision==accepted_revision &&
              save_stream(c.session->state())==accepted,
              "a replay cannot apply the same opponent-table control twice");
    }
    check(c.serve(c.footer(3)).empty(),"stage an opponent-table forecast");
    check(c.serve(c.flow.after_family_table_present(0)).empty(),"commit only the presented opponent forecast");
    const auto normal=c.session->league_family_owner()->table->page;
    const auto visible=c.flow.friendly_opponent_picker()->state();
    check(visible.forecast,"the forecast remains the visible table after normal scratch restoration");
    int own=-1,other=-1;
    for(size_t row=0;row<normal.rows.size();++row) {
        if(normal.rows[row].runtime_team_id==entry.runtime_club)own=int(row);
        else if(other<0 || visible.rows[row].runtime_team_id!=normal.rows[row].runtime_team_id)other=int(row);
    }
    check(own>=0&&other>=0,"normal ranks include own club and a selectable other club");
    const auto calls=c.session->game().calls();const auto seed=c.session->game().seed();
    check(c.serve(c.flow.pointer_down(300,29+own*7)).empty() && c.flow.friendly_table_active() &&
          !c.flow.pending_league_family_action() && c.session->game().calls()==calls,
          "the own club in restored normal ranks keeps Source opponent selection open");
    const auto selected=c.flow.pointer_down(300,29+other*7);const auto revision=c.revision;
    check(selected.league_family_action.runtime_team==visible.rows[size_t(other)].runtime_team_id,
          "physical selection binds its visible forecast row identity");
    check(c.serve(selected).empty() && c.revision==revision+1 &&
          !c.session->league_family_owner() && !c.flow.friendly_table_active() &&
          c.flow.state_json().find("\"stage\":\"venue\"")!=std::string::npos &&
          c.flow.state_json().find("\"opponent\":"+std::to_string(normal.rows[size_t(other)].runtime_team_id)+",")!=std::string::npos &&
          c.session->game().calls()==calls && c.session->game().seed()==seed &&
          c.session->rand().calls()==w.calls() && c.session->rand().seed()==w.seed(),
          "Source mode2 returns normal-rank opponent directly to venue without family RNG teardown");
    const auto final=save_stream(c.session->state());
    check(!c.serve(selected).empty() && c.revision==revision+1 && save_stream(c.session->state())==final,
          "replayed opponent return cannot reopen a quote or change the accepted owner");
}

void friendly_question_deadline(const fs::path& data,const fs::path& save) {
    for(bool held : {false,true}) {
        Context c(data,save,6);
        const auto table=c.flow.friendly_opponent_picker()->state();
        int row=-1;
        for(size_t i=0;i<table.rows.size();++i)
            if(std::find(c.session->state().manager_teams.begin(),c.session->state().manager_teams.end(),
                table.rows[i].runtime_team_id)==c.session->state().manager_teams.end()){row=int(i);break;}
        check(row>=0,"deadline fixture has an ordinary AI club opponent");
        check(c.serve(c.flow.pointer_down(40,29+row*7)).empty() && c.flow.friendly_question_screen(),
              "accepted table row opens the original venue question");
        (void)c.flow.click(40,29+row*7); // consume the table-row release outside the question widgets
        const auto native=save_stream(c.session->state());const auto revision=c.revision;
        const auto w=c.session->rand();const auto g=c.session->game();
        if(held) {
            (void)c.flow.pointer_down(99,171);(void)c.flow.move_action(100,171);
            check(c.flow.friendly_question_screen()->question_held() && !c.flow.process_timer_expired().changed &&
                  save_stream(c.session->state())==native,
                  "motion within a held question retains its physical release under expiry");
        }
        const auto no=held?c.flow.expire_friendly_question(true):c.flow.process_timer_expired();
        check(no.request==ui::FlowRequest::friendly_action &&
              no.friendly_action.kind==ui::FriendlyRequestKind::quote && !no.friendly_action.manager_home &&
              c.flow.pending_friendly_action() && save_stream(c.session->state())==native && c.revision==revision,
              "venue expiry selects Source No/away and submits only a bound quote, never a booking");
        check(!c.flow.process_timer_expired().changed && !c.flow.expire_friendly_question().changed,
              "an owned pending quote cannot be consumed by repeated question expiry");
        check(c.serve(no).empty() && c.flow.friendly_calendar_quote() &&
              !c.flow.friendly_question_screen() && save_stream(c.session->state())==native,
              "AI-away quote opens its authoritative calendar without charging or booking");
        check(c.serve(c.flow.process_timer_expired()).empty() && c.flow.screen_name()=="desktop" &&
              c.revision==revision && save_stream(c.session->state())==native &&
              c.session->rand().seed()==w.seed() && c.session->rand().calls()==w.calls() &&
              c.session->game().seed()==g.seed() && c.session->game().calls()==g.calls(),
              "expired returned calendar leaves the caller without any saved or RNG changes");
    }
    for(bool leave_button : {false,true}) {
        Context c(data,save,6);const auto table=c.flow.friendly_opponent_picker()->state();
        int row=0;while(table.rows[size_t(row)].runtime_team_id==c.flow.league_family_identity()->runtime_club)++row;
        check(c.serve(c.flow.pointer_down(40,29+row*7)).empty(),"cost deadline selects a real opponent");
        (void)c.flow.click(40,29+row*7);
        (void)c.flow.pointer_down(99,171);check(c.serve(c.flow.click(99,171)).empty(),"home venue creates its fee question");
        const auto native=save_stream(c.session->state());const auto revision=c.revision;
        const auto w=c.session->rand();const auto g=c.session->game();
        (void)c.flow.pointer_down(99,171);
        if(leave_button)(void)c.flow.move_action(0,0);
        const auto no=leave_button?c.flow.process_timer_expired():c.flow.expire_friendly_question(true);
        check(no.request==ui::FlowRequest::submenu_closed && c.flow.screen_name()=="desktop" &&
              !c.flow.pending_friendly_action() && !c.flow.friendly_calendar_quote() &&
              c.flow.friendly_question_release_pending()==leave_button,
              "fee expiry selects No and retains only a still-outstanding orphan left release");
        if(leave_button) {
            const auto* desk=c.flow.desktop();const auto item=std::find_if(desk->state().items.begin(),desk->state().items.end(),
                [](const auto& i){return i.kind==11&&!i.disabled;});
            check(item!=desk->state().items.end(),"deadline desk exposes normal save");
            const int x=item->x+item->width/2,y=item->y+item->height/2;
            check(c.flow.click(x,y).request==ui::FlowRequest::none && !c.flow.friendly_question_release_pending(),
                  "expired-question orphan UP cannot activate the returned desk save command");
            check(c.serve(c.flow.click(x,y)).empty() && c.flow.screen_name()=="save_load",
                  "the subsequent complete save gesture remains available after orphan release");
        }
        check(c.revision==revision && save_stream(c.session->state())==native &&
              c.session->rand().seed()==w.seed() && c.session->rand().calls()==w.calls() &&
              c.session->game().seed()==g.seed() && c.session->game().calls()==g.calls(),
              "fee cancellation does not charge money, book, advance revision or draw randomness");
    }
    Context c(data,fs::path(__FILE__).parent_path()/"fixtures/season_promotion/2PNWK001.FBM",6);
    check(c.serve(c.flow.pointer_down(237,205)).empty(),"other-manager deadline selects Source league3");
    (void)c.flow.click(237,205);
    check(c.serve(c.flow.pointer_down(40,34)).empty(),"other-manager deadline selects Player2's team");
    (void)c.flow.click(40,34);
    (void)c.flow.pointer_down(99,171);check(c.serve(c.flow.click(99,171)).empty(),"other-manager deadline quotes home fee");
    (void)c.flow.pointer_down(99,171);(void)c.flow.click(99,171);
    check(c.flow.friendly_question_screen() && c.flow.friendly_question_screen()->stage()==ui::FriendlyStage::owner_approval &&
          c.flow.friendly_question_screen()->active_question()->source_manager_flags==1,
          "Player2's own Source consent question precedes the date");
    const auto native=save_stream(c.session->state());const auto revision=c.revision;const auto g=c.session->game();const auto w=c.session->rand();
    check(c.flow.process_timer_expired().request==ui::FlowRequest::submenu_closed && c.flow.screen_name()=="desktop" &&
          save_stream(c.session->state())==native && c.revision==revision &&
          c.session->game().seed()==g.seed() && c.session->game().calls()==g.calls() &&
          c.session->rand().seed()==w.seed() && c.session->rand().calls()==w.calls(),
          "consent expiry returns No without a date, payment, booking or RNG work");
}

} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 3, "expected data directory and genuine WEEK196.FBM");
        friendly_controls_and_normal_rank_return(argv[1],argv[2]);
        friendly_question_deadline(argv[1],argv[2]);
        for(int menu_kind : {0,6}) {
            forecast_present_ack_is_a_flow_boundary(argv[1],argv[2],menu_kind);
            same_league_map_ack_retains_forecast_and_rejects_forged_target(argv[1],argv[2],menu_kind);
            range_waits_for_present_and_source_units(argv[1],argv[2],menu_kind);
            cancel_wait_anchors_at_present(argv[1],argv[2],menu_kind);
            range_process_deadline(argv[1],argv[2],menu_kind);
        }
        std::cout << "Flow/Controller Table controls passed (simulated present protocol; not an SDL run)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
