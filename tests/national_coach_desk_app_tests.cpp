// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "test_inputs.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>

#include "app/controller.hpp"
#include "game/desk.hpp"
#include "game/matchday_setup.hpp"
#include "game/month.hpp"
#include "game/national.hpp"
#include "game/save.hpp"
#include "game/turn.hpp"
#include "game/week.hpp"
#include "game/weekly.hpp"
#include "game/human_buyer_sale.hpp"
#include "national_coach_desk_pending_fixture.hpp"

using namespace bmh;
namespace fs = std::filesystem;
namespace {
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct TemporarySaves {
    fs::path path;
    explicit TemporarySaves(const fs::path& input) {
        static unsigned serial = 0;
        path = fs::temp_directory_path() / ("openbmh-national-desk-app-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(++serial));
        require(fs::create_directory(path), "create isolated national desk save directory");
        fs::copy_file(input, path / "AUTOSAVE.FBM");
    }
    ~TemporarySaves() { std::error_code ignored; fs::remove_all(path, ignored); }
};
struct Snapshot { Bytes native; WatcomRandom w; GameRandom g; };
Snapshot snapshot(const GameState& s, const WatcomRandom& w, const GameRandom& g) { return {save_stream(s), w, g}; }
Snapshot snapshot(app::CareerSession& s) { return snapshot(s.state(), s.rand(), s.game()); }
void same(const Snapshot& a, const Snapshot& b) {
    require(a.native == b.native && a.w.seed() == b.w.seed() && a.w.calls() == b.w.calls() &&
        a.g.seed() == b.g.seed() && a.g.calls() == b.g.calls(), "entire Source Native save-form stream and W/G match");
}
struct Context {
    fs::path data;
    TemporarySaves saves;
    ui::StartupFlow flow;
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio;
    Context(const fs::path& d, const fs::path& input) : data(d), saves(input), flow(d, saves.path) {
        require(load_savegame(input).checksum_ok, "actual Native fixture checksum");
        playback.set_muted(true);
        flow.click(214, 215);
        const auto request = flow.send_key("ENTER");
        require(request.request == ui::FlowRequest::file_action && request.file_action.action == ui::SaveLoadAction::load,
                "actual StartupFlow requests load");
        require(serve(request).empty() && session && revision == 1 && flow.screen_name() == "desktop",
                "actual controller loads Native desk");
    }
    std::string serve(const ui::FlowResult& request, PendingOfferHooks hooks = {},
                      std::function<void(int, const std::vector<MatchGroup>&, const GameState&)> live = {}) {
        return app::serve_flow_request(flow, request, session, data, saves.path, 1, revision, playback, audio, false,
                                      std::move(live), {}, {}, {}, {}, {}, {}, {}, std::move(hooks),
                                      app::PendingOfferEntryPolicy::normal_interactive);
    }
    ui::FlowResult advance() {
        require(flow.desktop() && !flow.desktop()->pending(), "desktop has no previous owned request");
        const auto& items = flow.desktop()->state().items;
        const auto i = std::find_if(items.begin(), items.end(), [](const auto& v) {
            return !v.disabled && (v.kind == 12 || (v.kind == 58 && v.open));
        });
        require(i != items.end(), "actual enabled ADVANCE item exists");
        const auto request = flow.click(i->x + i->width / 2, i->y + i->height / 2);
        require(request.request == ui::FlowRequest::kickoff && flow.desktop()->pending() &&
                request.desk_action.revision == revision, "physical ADVANCE creates current owned request");
        return request;
    }
    fs::path save() {
        const auto& items = flow.desktop()->state().items;
        const auto i = std::find_if(items.begin(), items.end(), [](const auto& v) { return v.kind == 11 && !v.disabled; });
        require(i != items.end(), "actual national SAVE item exists");
        require(flow.click(i->x + i->width / 2, i->y + i->height / 2).request == ui::FlowRequest::submenu_opened,
                "physical save menu opens Native browser");
        flow.click(150, 214); require(flow.text("COACH"), "actual filename field accepts isolated name");
        const auto request = flow.send_key("ENTER");
        require(request.request == ui::FlowRequest::file_action && request.file_action.action == ui::SaveLoadAction::save,
                "actual flow owns save request");
        auto expected_w = session->rand(); const auto expected = make_savegame(session->state(), expected_w);
        const auto before_g = session->game(); const auto old_revision = revision;
        require(serve(request).empty() && revision == old_revision + 1 && flow.screen_name() == "desktop",
                "production controller saves and acknowledges once");
        require(load_savegame(request.file_action.path).stream == expected.stream &&
                session->rand().seed() == expected_w.seed() && session->rand().calls() == expected_w.calls() &&
                session->game().seed() == before_g.seed() && session->game().calls() == before_g.calls(),
                "actual save owns exact four W draws, Source handback bytes and unchanged G");
        return request.file_action.path;
    }
};
void restored(Context& c) {
    require(c.session->pending_offer_entry_policy() == app::PendingOfferEntryPolicy::legacy_unpresented &&
        !c.session->pending_offer_hooks().answer && !c.session->pending_offer_hooks().select_transfer &&
        !c.session->pending_offer_hooks().notice, "controller restores borrowed normal policy and presenters");
}
void same_cursor(const TurnDriver& a, const TurnDriver& b) {
    require(a.stage == b.stage && a.manager_position == b.manager_position && a.revision == b.revision &&
        a.calendar_entry == b.calendar_entry && a.session_id == b.session_id && a.next_event_id == b.next_event_id &&
        a.loaded_matchday_resume == b.loaded_matchday_resume &&
        a.suppress_remaining_pre_match_offers == b.suppress_remaining_pre_match_offers &&
        a.enable_national_nomination_desks == b.enable_national_nomination_desks &&
        a.national_nomination_desk == b.national_nomination_desk && a.groups.size() == b.groups.size() &&
        a.referee_source == b.referee_source && a.referees == b.referees &&
        a.last_results.valid == b.last_results.valid, "national Waiting/stale request preserves bound driver and provenance");
}
void national_identity(Context& c, bool loaded = true) {
    const auto& state = c.session->state(); const auto& d = c.flow.desktop()->state();
    const auto driver = c.session->manager_join_checkpoint().driver;
    require(state.manager_teams == std::vector<int>{0x165} && state.memory.u8(::bmh::layout::ManagerCount) == 1 &&
        state.memory.u8(::bmh::layout::CoachActive) == 1 && state.memory.u8(::bmh::layout::ActiveManager) == 0 && state.memory.u8(::bmh::layout::DeskPosition) == 0 &&
        driver.national_nomination_desk && driver.groups.empty() && driver.loaded_matchday_resume == loaded,
        "actual saved no-fixture desk rebinds runtime manager0 national takeover without regeneration");
    require(d.national_team && d.national_coach_takeover && d.runtime_club == 0x165 && d.manager_slot == 0 &&
        d.manager_teams == state.manager_teams && d.national_return_roster.size() == state.memory.u8(::bmh::layout::OriginalManagerCount) &&
        d.managers.at(0).name == d.manager_name && d.next_competition == 33 && d.next_opponent == -1,
        "actual Source national identity and no-fixture callargs reach StartupFlow desktop");
}
struct Oracle { Snapshot final; int active = -1; uint32_t date = 0; std::vector<MatchGroup> groups; bool national = false; };
Oracle departure_oracle(app::CareerSession::ManagerJoinCheckpoint checkpoint) {
    auto& s = checkpoint.state; auto& w = checkpoint.rand; auto& g = checkpoint.game;
    require(checkpoint.driver.groups.empty() && s.memory.u8(::bmh::layout::CoachActive) && s.manager_teams.size() == 1,
            "oracle begins from actual national desk, not an arbitrary empty matchday");
    // format field restores inherited original order, then completes its sole desk.
    // This composition deliberately does not call the new pending helper or driver.
    const int owner = s.memory.u16(::bmh::layout::NextDeskManager);
    for (int raw = 0; raw < 4; ++raw) {
        const auto word = s.memory.u32(::bmh::layout::OfferSlots + uint32_t(raw) * 4);
        require(word == 0xffffffffu || int(word & 31) != owner, "oracle has no unsupported owned pending word");
    }
    s.memory.set8(::bmh::layout::ActiveManager, uint8_t(owner)); s.memory.set8(::bmh::layout::DeskPosition, 1);
    for (int raw = 0; raw < 4; ++raw) {
        s.memory.set32(::bmh::layout::OfferSlots + uint32_t(raw) * 4, 0xffffffffu);
        s.squad_slots.at(size_t(0xb70 + 40 + raw)) = 0x0fff;
    }
    event_end(s); ++s.date; daily_interest(s, w);
    bool after_event = true; MinuteReport report;
    // Independent format field composition through the next real desk. No outgoing
    // kick_off, engine, coupon evaluation, finish_event or synthetic match result.
    for (int day = 0; day < 366; ++day) {
        const uint16_t source_rest_day = uint16_t(
            s.date - s.season_start - uint32_t(after_event ? 1 : 0));
        require(day_step_rest(s, g, w, report, source_rest_day) && !report.unsupported,
                "Source next-day rest is supported");
        require(!is_season_end(s), "bounded fixtures reach a desk before annual boundary");
        if (after_event) day_stock_market(s, g);
        after_event = false;
        const auto entry = s.memory.u16(::bmh::layout::CompetitionCalendar + uint32_t(s.season_day()) * 2);
        if (!(entry & 0x7fff)) { plain_day(s, g, w); continue; }
        MatchdayContext ctx; auto refs = checkpoint.driver.referee_source;
        ctx.month = month_code(s.date); ctx.weekday = weekday_code(s.date); ctx.season_day = s.season_day(); ctx.referees = &refs;
        const auto competitions = day_competitions(s, entry);
        if (!competitions) {
            require(empty_event_day(s, ctx, g, w, report) && !report.unsupported, "Source disabled-calendar step");
            after_event = true; continue;
        }
        require(competitions != 0x800000, "untouched fixture next desk precedes another nomination");
        // format field:22087 starts a fresh ordinary calendar event with one
        // pass. This Native byte is independent of nomination and draws no RNG.
        require(entry != 0x4002, "national departure oracle reaches an ordinary event");
        s.memory.set8(::bmh::layout::EventPasses, 1);
        auto groups = prepare_event(s, entry, ctx, g, w, report);
        require(!report.unsupported && !s.manager_teams.empty(), "Source next prepared event owns human desk");
        enter_event_desk(s, 0, groups, g, w, report);
        require(!report.unsupported, "Source next desk entry is supported");
        return {snapshot(s, w, g), int(s.memory.u8(::bmh::layout::ActiveManager)), s.date, std::move(groups),
                s.manager_teams == std::vector<int>{0x165}};
    }
    throw std::runtime_error("bounded national departure oracle did not reach next desk");
}
void advance_and_compare(Context& c) {
    // Declared F6-off control: this oracle ends at the first ordinary event,
    // before any additional background match simulation.
    c.session->state().memory.set8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField,1);
    const auto expected = departure_oracle(c.session->manager_join_checkpoint());
    const auto original_roster = c.flow.desktop()->state().national_return_roster;
    const auto old_revision = c.revision; const auto request = c.advance(); unsigned minutes = 0;
    const auto message = c.serve(request, {}, [&](int, const std::vector<MatchGroup>&, const GameState&) { ++minutes; });
    if (!message.empty()) throw std::runtime_error("national ADVANCE: " + message + " flow=" + c.flow.state_json());
    require(c.revision == old_revision + 1 && minutes == 0 && c.playback.queued_bytes() == 0,
            "actual national departure publishes once without match callback/audio");
    same(snapshot(*c.session), expected.final); restored(c);
    const auto after = c.session->manager_join_checkpoint();
    require(after.state.date == expected.date && after.state.memory.u8(::bmh::layout::ActiveManager) == expected.active &&
        after.driver.stage == TurnDriver::Stage::MatchdayDesks && !after.driver.national_nomination_desk &&
        !after.driver.loaded_matchday_resume && !after.driver.suppress_remaining_pre_match_offers &&
        !after.driver.last_results.valid && after.driver.groups.size() == expected.groups.size(),
        "next desk follows Source calendar, consumes provenance and creates no empty-event result");
    const auto& desk = c.flow.desktop()->state();
    if (expected.national) {
        require(desk.national_team && desk.national_coach_takeover && desk.runtime_club == 0x165 &&
            c.session->state().memory.u8(::bmh::layout::CoachActive) == 1 && desk.national_return_roster == original_roster,
            "Source next national fixture retains coach and complete original return roster without forced handback");
    } else {
        require(!desk.national_team && c.session->state().memory.u8(::bmh::layout::CoachActive) == 0 &&
            desk.managers == original_roster && desk.runtime_club == original_roster.at(size_t(expected.active)).runtime_club,
            "Source next ordinary event restores complete original club roster and identity");
    }
    const auto settled = snapshot(*c.session);
    require(c.serve(request).empty(), "controller ignores consumed old owned request");
    same(snapshot(*c.session), settled); same_cursor(c.session->manager_join_checkpoint().driver, after.driver);
}
void untouched(const fs::path& data, const fs::path& input) {
    Context c(data, input); national_identity(c); const auto original = load_savegame(input);
    require(c.session->rand().calls() == 0 && c.session->rand().seed() == 1 && c.session->game().calls() == 0,
            "actual load consumes no W/G or nomination/setup draws");
    auto canonical = original.stream; SaveState native(original.stream);
    canonical.at(native.block(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField3).offset) = c.session->state().memory.u8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField3);
    require(save_stream(c.session->state()) == canonical,
            "untouched load preserves entire Source handback stream except documented previous-active normalization");
    require(c.flow.desktop()->state().national_return_roster.at(0).runtime_club == 10,
            "untouched original return roster identifies club10");
    const auto before = snapshot(*c.session); const auto driver = c.session->manager_join_checkpoint().driver;
    std::string error; const auto result = c.session->dispatch_kickoff(
        {ui::DeskRequestKind::advance_turn, 0, -1, -1, -1, c.revision + 1}, c.revision, error);
    require(result.kind == TurnDeskKind::Failed && error == "stale_manager_or_revision", "future/stale request cannot claim national desk");
    same(snapshot(*c.session), before); same_cursor(c.session->manager_join_checkpoint().driver, driver);
    const auto saved = c.save(); national_identity(c);
    Context reload(data, saved); national_identity(reload);
    require(reload.session->rand().calls() == 0 && reload.session->game().calls() == 0 &&
        save_stream(reload.session->state()) == load_savegame(saved).stream,
        "actual saved desk reload restores Source save form without repeating nomination RNG");
    advance_and_compare(reload); advance_and_compare(c);
    require(load_savegame(input).stream == original.stream, "untouched Original Native input stays unchanged");
}
void unsupported_retry(const fs::path& data, const fs::path& input, const fs::path& qa) {
    Context c(data, input); const auto qa_save = load_savegame(qa); require(qa_save.checksum_ok, "genuine QA_INPUT checksum");
    SaveState qa_native(qa_save.stream); const auto at = qa_native.block(::bmh::layout::OfferSlots).offset;
    const auto real_word = le32(qa_save.stream, at);
    require(real_word != 0xffffffffu && (real_word & 31) == 0, "genuine N.BROWN QA_INPUT carries actual owner0 pending word");
    // Declared controlled boundary fixture: copy only that genuine pending payload
    // into an untouched NATWK001 runtime. No fabricated salary answer or player row.
    c.session->state().memory.set32(::bmh::layout::OfferSlots, real_word);
    const auto before = snapshot(*c.session); const auto driver = c.session->manager_join_checkpoint().driver;
    unsigned questions = 0; PendingOfferHooks hooks;
    hooks.answer = [&](const GameState&, const PendingOfferQuestion&) { ++questions; return PendingOfferAnswer::yes; };
    const auto revision = c.revision;
    require(c.serve(c.advance(), std::move(hooks)).empty() && questions == 0,
            "invalid one-word injection retains diagnostic boundary before salary presentation");
    same(snapshot(*c.session), before); same_cursor(c.session->manager_join_checkpoint().driver, driver); restored(c);
    require(c.revision == revision && c.flow.screen_name() == "desktop" && !c.flow.desktop()->pending() &&
        c.flow.desktop()->state().revision == revision && c.playback.queued_bytes() == 0,
        "unsupported rejects same-revision UI request with retryable owned national cursor");
    // Remove precisely the declared unsupported test injection, then a new physical
    // request must succeed. The production path never invents an answer or drops it.
    c.session->state().memory.set32(::bmh::layout::OfferSlots, 0xffffffffu);
    advance_and_compare(c);
}
void owned_offer_retry(const fs::path& data,const fs::path& saves) {
    namespace control=national_pending_control;
    for(int owner:{0,1})for(int failure=0;failure<4;++failure) {
        Context c(data,saves/(owner==1?"2PNWK060.FBM":"NATWK001.FBM"));
        const bool salary=failure!=3;
        const auto o=control::prepare(c.session->state(),owner,salary);
        const auto before=snapshot(*c.session);
        const auto cursor=c.session->manager_join_checkpoint().driver;
        const auto revision=c.revision;
        int questions=0,choices=0;PendingOfferHooks hooks;
        if(failure!=0)hooks.answer=[&](const GameState&,const PendingOfferQuestion& q) {
            ++questions;
            require(q.manager==owner && q.clicked_source_slot==o.anchor,"staged Source question has raw owner and anchor");
            if(failure==2)throw std::runtime_error("controlled interrupted national salary presenter");
            return PendingOfferAnswer::unsupported;
        };
        if(failure==3)hooks.select_transfer=[&](const auto&,const auto&) {
            ++choices;return PendingOfferTransferChoice{};
        };
        require(c.serve(c.advance(),hooks).empty(),"unavailable national presenter is a retryable controller boundary");
        same(snapshot(*c.session),before);same_cursor(c.session->manager_join_checkpoint().driver,cursor);restored(c);
        require(c.revision==revision && c.flow.desktop()->state().revision==revision &&
            !c.flow.desktop()->pending() && c.flow.screen_name()=="desktop" && c.playback.queued_bytes()==0,
            "failed national pending dispatch preserves revision, desktop, cursor and audio");
        require(questions==(failure==1||failure==2?1:0) && choices==(failure==3?1:0),
            "failed presenter was invoked only at its actual Source boundary");
    }
}

void parked_buyer_retry(const fs::path& data,const fs::path& saves) {
    namespace control=national_pending_control;
    for(int failure=0;failure<5;++failure) {
        Context c(data,saves/"2PNWK060.FBM");
        const bool salary=failure<2,loan=failure==4;
        const auto o=control::prepare_parked_buyer(c.session->state(),loan,salary);
        if(failure==1)control::write(c.session->state().manager_records,4*0xb0c+0x314,0);
        const auto before=snapshot(*c.session);const auto state=c.session->state();
        const auto cursor=c.session->manager_join_checkpoint().driver;const auto revision=c.revision;
        PendingOfferHooks hooks;hooks.human_buyer_allocator_gate=PendingOfferAllocatorGate::zero;
        int questions=0,choices=0,notices=0;
        hooks.answer=[&](const auto&,const auto& q) {
            ++questions;require(q.manager==1 && q.kind==PendingOfferQuestionKind::salary_demand,
                "native buyer4 salary question belongs to retained Source param8 owner1");
            return PendingOfferAnswer::no;
        };
        hooks.select_transfer=[&](const auto&,const auto& view) {
            ++choices;require(view.seller_manager==1 && view.offers[0].buyer_manager==4,
                "staged fee chooser retains both Source owner roles");
            return failure==2?PendingOfferTransferChoice{}:
                PendingOfferTransferChoice{PendingOfferTransferChoiceKind::bid,0};
        };
        if(failure!=0)hooks.notice=[&](const GameState& staged,const PendingOfferNotice& notice) {
            ++notices;
            if(failure==1) {
                require(notice.manager==4 && notice.text_address==::bmh::layout::Advertising_FormatError_TextPointer18 && staged.memory.u8(::bmh::layout::ActiveManager)==4,
                    "Source funds notice temporarily sets active native buyer4");
            } else {
                require(notice.manager==4 && notice.text_address==::bmh::layout::PendingOffer_FormatError_ExecutableField2 && staged.memory.u8(::bmh::layout::ActiveManager)==1,
                    "Source paid ACK4 keeps the restored raw seller1 active");
                require(control::read(staged.manager_records,0xb0c+0x314)==control::read(state.manager_records,0xb0c+0x314)+o.fee &&
                    control::read(staged.manager_records,4*0xb0c+0x314)==control::read(state.manager_records,4*0xb0c+0x314)-o.fee &&
                    control::contains(staged,o.seller,o.player) && !control::contains(staged,o.buyer,o.player),
                    "interrupted ACK occurs after both accounts and before actual player movement");
            }
            throw std::runtime_error("controlled Source buyer4 notice interruption");
        };
        require(c.serve(c.advance(),hooks).empty(),"missing or interrupted buyer4 presenter keeps an owned retryable request");
        same(snapshot(*c.session),before);same_cursor(c.session->manager_join_checkpoint().driver,cursor);restored(c);
        require(c.revision==revision && c.flow.desktop()->state().revision==revision && !c.flow.desktop()->pending() &&
            c.flow.screen_name()=="desktop" && c.playback.queued_bytes()==0,
            "buyer4 staging rolls back complete Native, G/W, revision, cursor, desktop and audio");
        require(questions==int(salary) && choices==int(!salary) && notices==int(failure==1 || failure==3 || failure==4),
            "only actual Source presenter boundaries ran before rollback");
    }
}

void parked_seller_retry(const fs::path& data,const fs::path& saves) {
    namespace control=national_pending_control;
    const auto input=saves/"NATLOAN.FBM";
    for(int failure=0;failure<4;++failure) {
        Context c(data,input);
        if(failure==3)for(int i=0;i<12;++i)c.session->state().memory.set16(::bmh::layout::Career_CareerManagersEnd_RuntimeField+uint32_t(i)*2,77);
        const auto before=snapshot(*c.session);const auto cursor=c.session->manager_join_checkpoint().driver;
        const auto revision=c.revision;const auto cash=control::read(c.session->state().manager_records,0x314);
        int choices=0,notices=0;PendingOfferHooks hooks;
        hooks.answer=[](const auto&,const auto&){return PendingOfferAnswer::yes;};
        hooks.select_transfer=[&](const auto&,const auto& v) {
            ++choices;require(v.seller_manager==0 && v.seller==1 && v.offers[0].loan,
                "parked seller retry receives actual Source owner0 loan choice");
            if(failure==0)throw std::runtime_error("controlled interrupted parked seller chooser");
            return failure==2?PendingOfferTransferChoice{}:PendingOfferTransferChoice{PendingOfferTransferChoiceKind::bid,0};};
        if(failure==1)hooks.notice=[&](const GameState& staged,const PendingOfferNotice& notice) {
            ++notices;require(notice.manager==0 && notice.text_address==::bmh::layout::PendingOffer_FormatError_ExecutableField2 &&
                control::read(staged.manager_records,0x314)==cash+1360000 &&
                control::contains(staged,1,118) && !control::contains(staged,12,118),
                "interrupted seller0 ACK follows Source fee booking but precedes movement/snapshot");
            throw std::runtime_error("controlled parked seller paid ACK interruption");};
        require(c.serve(c.advance(),hooks).empty(),"failed parked seller transaction is an owned retryable boundary");
        same(snapshot(*c.session),before);same_cursor(c.session->manager_join_checkpoint().driver,cursor);restored(c);
        require(c.revision==revision && c.flow.desktop()->state().revision==revision && !c.flow.desktop()->pending() &&
            c.flow.screen_name()=="desktop" && c.playback.queued_bytes()==0 && choices==1 && notices==int(failure==1),
            "parked seller rollback restores complete Native/G/W/cursor/revision after chooser, ACK or snapshot failure");
    }
    Context c(data,input);c.session->state().memory.set8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField,1); // public F6 show-all-desks control
    const auto revision=c.revision;int notices=0;PendingOfferHooks hooks;
    hooks.answer=[](const auto&,const auto&){return PendingOfferAnswer::yes;};
    hooks.select_transfer=[](const auto&,const auto&){return PendingOfferTransferChoice{PendingOfferTransferChoiceKind::bid,0};};
    hooks.notice=[&](const auto&,const auto& n){++notices;require(n.manager==0 && n.text_address==::bmh::layout::PendingOffer_FormatError_ExecutableField2,"actual paid ACK0");};
    require(c.serve(c.advance(),hooks).empty() && c.revision==revision+1 && notices==1 &&
        !control::contains(c.session->state(),1,118) && control::contains(c.session->state(),12,118) &&
        c.session->state().memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField)==118 && c.session->state().memory.u8(::bmh::layout::ClubInfo_FormatError_RuntimeField2)==0 &&
        c.session->state().memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField)==1,"actual controller completes parked club loan and Source next desk");
    restored(c);const auto saved=c.save();Context reloaded(data,saved);
    require(control::contains(reloaded.session->state(),12,118) && !control::contains(reloaded.session->state(),1,118) &&
        reloaded.session->state().memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField)==118 && reloaded.session->state().memory.u8(::bmh::layout::ClubInfo_FormatError_RuntimeField2)==0 &&
        reloaded.session->state().memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField)==1,"physical save and fresh controller load retain persistent club loan snapshot/count");
}

void later_coach_prefix_one(const fs::path& data, const fs::path& input, const fs::path& output) {
    Context base(data, input);
    const auto home = base.flow.desktop()->state().managers;
    require(home.size() == 2 && base.session->state().manager_teams == std::vector<int>{1,54},
            "controlled national fixture starts from genuine two-manager Native base");
    auto state = base.session->state(); auto w = base.session->rand(); auto g = base.session->game();
    // Exactly declared controlled changes: select original manager1 as coach,
    // move to the base's actual day4 nomination cell and select existing permutation1,0.
    state.manager_records.at(0x21c) = 0; state.manager_records.at(0xb0c + 0x21c) = 1;
    state.date = state.season_start + 4;
    require(state.memory.u16(::bmh::layout::CompetitionCalendar + 8) == 0x4006, "genuine base already contains actual day4 nomination calendar");
    state.memory.set16(::bmh::layout::NextDeskManager, 1); state.memory.set16(::bmh::layout::FixtureSecondDeskCursor, 0);
    MinuteReport report;
    require(national_team_day(state, w, report) && !report.unsupported, "actual Source nomination selects original manager1");
    MatchdayContext ctx; auto refs = base.session->manager_join_checkpoint().driver.referee_source;
    ctx.month = month_code(state.date); ctx.weekday = weekday_code(state.date); ctx.season_day = state.season_day();
    ctx.referees = &refs; ctx.national_team_day = true;
    const auto groups = setup_matchday(state, 0, ctx, g, w, report);
    require(groups.empty() && !report.unsupported, "actual national-context setup retains no-fixture takeover");
    draw_matchday_number(state, w); shuffle_manager_order(state, g);
    state.memory.set8(::bmh::layout::DeskPosition, 0); state.memory.set8(::bmh::layout::ActiveManager, 0);
    // Source's screen-entry reset is transient and absent from Native FBM.
    // The composition covers available fields; normal policy is explicitly owned.
    if (state.memory.has(::bmh::layout::MatchdaySetupRuntimeField14)) state.memory.set8(::bmh::layout::MatchdaySetupRuntimeField14, 0);
    pre_match_offer(state, 0, g, w, report); build_pools_coupon(state, groups, &w); enter_desk(state, 0, g, w);
    require(!report.unsupported && state.memory.u16(::bmh::layout::NextDeskManager) == 1 && state.memory.u8(::bmh::layout::OriginalManagerCount) == 2,
            "Source count1 shuffle preserves original nonzero prefix and complete return roster");
    const auto controlled = base.saves.path / "NAT2P.FBM";
    auto save_w = w; write_savegame(controlled, make_savegame(state, save_w));
    if (!output.empty()) {
        fs::create_directories(output); fs::copy_file(controlled, output / "NAT2P-PREFIX1.FBM", fs::copy_options::overwrite_existing);
        std::ofstream meta(output / "NAT2P-PREFIX1.json");
        meta << "{\"scope\":\"Controlled Core composition, not an untouched Original recording\","
                "\"source_sha256\":\"66e5e60cfa17ac971c23838bf73f94779ac7e4fb290113e3c331665006fef3a2\","
                "\"changes\":[\"manager0 eligibility0\",\"manager1 eligibility1\",\"date=season_start+4\",\"order=[1,0,2,3]\"],"
                "\"helpers\":[\"national_team_day\",\"setup_matchday(0,national_context)\",\"draw_matchday_number\",\"shuffle_manager_order\",\"pre_match_offer\",\"build_pools_coupon\",\"enter_desk\",\"make_savegame(copyW)\"],"
                "\"W_seed_before_save\":" << w.seed() << ",\"W_calls_before_save\":" << w.calls()
             << ",\"G_seed\":" << g.seed() << ",\"G_calls\":" << g.calls() << "}\n";
    }
    Context national(data, controlled); national_identity(national);
    const auto& d = national.flow.desktop()->state();
    require(d.manager_name == home.at(1).name && d.portrait == home.at(1).portrait &&
        d.managers.at(0).club_identity_key == home.at(1).club_identity_key && d.national_return_roster == home &&
        national.session->state().memory.u16(::bmh::layout::NextDeskManager) == 1 && national.session->rand().calls() == 0 &&
        national.session->game().calls() == 0, "actual prefix1 load projects later coach and both home identities without RNG");
    const auto before = snapshot(*national.session); const auto driver = national.session->manager_join_checkpoint().driver;
    std::string error;
    const auto wrong = national.session->dispatch_kickoff(
        {ui::DeskRequestKind::advance_turn, 1, -1, -1, -1, national.revision}, national.revision, error);
    require(wrong.kind == TurnDeskKind::Failed && error == "stale_manager_or_revision",
            "original prefix owner1 cannot impersonate the actual runtime coach UI manager0");
    same(snapshot(*national.session), before); same_cursor(national.session->manager_join_checkpoint().driver, driver);
    const auto saved = national.save(); Context reloaded(data, saved); national_identity(reloaded);
    require(reloaded.flow.desktop()->state().national_return_roster == home &&
        reloaded.session->state().memory.u16(::bmh::layout::NextDeskManager) == 1, "actual save/reload preserves prefix1 and both original identities");
    advance_and_compare(reloaded); advance_and_compare(national);
}
void national_contract_expiry(const fs::path& data, const fs::path& input) {
    constexpr int manager = 0;
    constexpr int national_team = 0x165;
    struct RowBefore { size_t row; int player; uint8_t term; };
    auto national_rows = [](GameState& g) {
        const SquadRange squad = squad_range(national_team);
        std::vector<RowBefore> rows;
        for (size_t row = 0; row < squad.count; ++row) {
            const int player = int(g.squad_slots.at(squad.first + row) & 0x0fff);
            if (player == 0x0fff || size_t(player) >= g.players.size()) continue;
            const size_t board = size_t(manager) * 0x840 + row * 0x58;
            rows.push_back({row, player, g.tactics.at(board + 0x0c)});
        }
        return rows;
    };

    // Genuine NATWK001 state: observe its actual terms and verify the ordinary monthly
    // decrement without manufacturing an expiry or a contract presenter call.
    {
        Context c(data, input);
        auto& session = *c.session;
        GameState& g = session.state();
        require(g.manager_teams == std::vector<int>{national_team},
                "contract expiry fixture is owned by the national coach in manager slot 0");
        const auto rows = national_rows(g);
        require(!rows.empty(), "genuine National team contains players");
        for (const auto& r : rows)
            require(r.term > 1 && r.term != 0xff,
                    "genuine National contracts are not already expiring; use loaded runtime terms");
        unsigned presenter_calls = 0;
        session.set_contract_expiry_presenter([&](GameState&, WatcomRandom&, int, int) {
            ++presenter_calls;
            return 1;
        });
        MinuteReport report;
        month_change(g, session.rand(), report, &session.game());
        session.set_contract_expiry_presenter({});
        require(!report.unsupported && presenter_calls == 0,
                "genuine NATWK001 monthly update does not present a non-expiring National contract");
        const SquadRange squad = squad_range(national_team);
        for (const auto& r : rows) {
            const size_t board = size_t(manager) * 0x840 + r.row * 0x58;
            require(int(g.squad_slots.at(squad.first + r.row) & 0x0fff) == r.player &&
                        g.tactics.at(board + 0x0c) == uint8_t(r.term - 1),
                    "genuine National monthly update retains each player and shortens its loaded term once");
        }
    }

    // Controlled API/owner-routing case: explicitly prepare one existing National player
    // for expiry with mood 100; all other occupied rows are protected from this test's tick.
    {
        Context c(data, input);
        auto& session = *c.session;
        GameState& g = session.state();
        require(g.manager_teams == std::vector<int>{national_team},
                "controlled contract case retains the actual National manager owner");
        const auto rows = national_rows(g);
        require(!rows.empty(), "controlled contract case uses an existing National player");
        const auto selected = rows.front();
        const size_t board = size_t(manager) * 0x840 + selected.row * 0x58;
        for (const auto& r : rows)
            if (r.row != selected.row)
                g.tactics.at(size_t(manager) * 0x840 + r.row * 0x58 + 0x0c) = 0xff;
        g.tactics.at(board + 0x0c) = 1;
        g.tactics.at(board + 0x0d) = 100;  // explicit test setup, not an untouched fixture value
        const unsigned initial_w_calls = session.rand().calls();
        const unsigned initial_g_calls = session.game().calls();
        unsigned presenter_calls = 0;
        session.set_contract_expiry_presenter([&](GameState& state, WatcomRandom& random, int owner, int row) {
            require(&state == &g && &random == &session.rand() && owner == manager &&
                        row == int(selected.row) && state.manager_teams.at(size_t(owner)) == national_team &&
                        int(state.squad_slots.at(squad_range(national_team).first + selected.row) & 0x0fff) == selected.player,
                    "controlled expiry callback is bound to the real National state, W stream, owner, row and player");
            ++presenter_calls;
            require(state.tactics.at(board + 0x0d) == 100 && state.tactics.at(board + 0x0c) == 0,
                    "controlled presenter sees the explicitly prepared one-year expiry and mood");
            const auto demand = contract_demands(state, owner, row, false, random);
            require(std::none_of(demand.begin(), demand.end(), [](uint32_t value) { return value == 0xffffffffu; }) &&
                        demand[0] != 0,
                    "controlled expiry uses Source demand arithmetic with nonzero prepared mood");
            require(contract_offer(state, owner, row, 0, demand[0], demand[0], false, true, random),
                    "controlled full-demand Source offer accepts");
            return 1;
        });
        MinuteReport report;
        month_change(g, session.rand(), report, &session.game());
        session.set_contract_expiry_presenter({});
        require(!report.unsupported && presenter_calls == 1 &&
                    int(g.squad_slots.at(squad_range(national_team).first + selected.row) & 0x0fff) == selected.player &&
                    g.tactics.at(board + 0x0c) == 12 && g.manager_teams.at(manager) == national_team,
                "controlled accepted Source offer renews the same National player's term to twelve months");
        require(session.rand().calls() > initial_w_calls && session.game().calls() == initial_g_calls,
                "controlled monthly offer uses the session W stream and leaves G untouched");
    }
}
void fresh_entry(const fs::path& data, const fs::path& input) {
    Context c(data, input), stale_cache(data, input);
    const auto home = c.flow.desktop()->state().managers;
    require(home.size() == 2 && c.session->state().date == 0 &&
            stale_cache.session->state().date == 0,
            "fresh entry begins at genuine two-manager opening desk");

    const auto reach_nomination = [](Context& context, uint16_t cached_day) {
        // One declared eligibility control only; the original date/calendar/order,
        // roster and actual loaded desk loop are kept intact. The memory copy of DaysIntoSeason
        // is overridden by the typed current counter during Native serialization,
        // so these are equivalent live starts.
        context.session->state().manager_records.at(0x21c) = 0;
        context.session->state().manager_records.at(0xb0c + 0x21c) = 1;
        context.session->state().memory.set16(::bmh::layout::DaysIntoSeason, cached_day);
        require(context.serve(context.advance()).empty() && context.revision == 2 &&
                context.session->state().date == 0,
                "actual normal loaded opening loop hands off to its second human manager");
        require(context.serve(context.advance()).empty() && context.revision == 3 &&
                context.session->state().season_day() == 4,
                "second actual ADVANCE reaches fresh day4 nomination instead of skipping it");
        national_identity(context, false);
        restored(context);
    };
    reach_nomination(c, 0);
    reach_nomination(stale_cache, 1);
    same(snapshot(*c.session), snapshot(*stale_cache.session));
    const auto& driver_a = c.session->manager_join_checkpoint().driver;
    const auto& driver_b = stale_cache.session->manager_join_checkpoint().driver;
    require(driver_a.stage == driver_b.stage && driver_a.manager_position == driver_b.manager_position &&
            driver_a.revision == driver_b.revision && driver_a.calendar_entry == driver_b.calendar_entry &&
            driver_a.next_event_id == driver_b.next_event_id &&
            driver_a.national_nomination_desk == driver_b.national_nomination_desk &&
            driver_a.groups.size() == driver_b.groups.size() &&
            driver_a.last_results.valid == driver_b.last_results.valid,
            "production TurnDriver stage, cursor, event and desk progression match");
    require(c.revision == stale_cache.revision &&
            c.flow.desktop()->state().manager_name == stale_cache.flow.desktop()->state().manager_name &&
            c.flow.desktop()->state().national_coach_takeover ==
                stale_cache.flow.desktop()->state().national_coach_takeover &&
            c.flow.desktop()->state().national_return_roster ==
                stale_cache.flow.desktop()->state().national_return_roster,
            "production TurnDriver day-zero-to-day-four progression ignores stale bb468");

    national_identity(c, false); restored(c);
    const auto& d = c.flow.desktop()->state();
    require(d.manager_name == home.at(1).name && d.portrait == home.at(1).portrait && d.national_return_roster == home &&
        c.session->manager_join_checkpoint().driver.enable_national_nomination_desks,
        "normal GUI enables fresh nomination only after successful staged batch and projects selected later coach");
    // Compare departure separately with independent Source composition. The
    // opening event's prior generic result remains valid and must not be replaced
    // by a synthetic result for this national desk.
    const auto previous_results = c.session->manager_join_checkpoint().driver.last_results;
    c.session->state().memory.set8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField,1); // same one-event departure control
    const auto expected = departure_oracle(c.session->manager_join_checkpoint());
    const auto revision = c.revision; unsigned minutes = 0;
    require(c.serve(c.advance(), {}, [&](int, const std::vector<MatchGroup>&, const GameState&) { ++minutes; }).empty() &&
        c.revision == revision + 1 && minutes == 0 && c.playback.queued_bytes() == 0,
        "fresh nomination desk completes without a simulated match or intermediate revision");
    same(snapshot(*c.session), expected.final); restored(c);
    const auto results = c.session->manager_join_checkpoint().driver.last_results;
    require(results.valid == previous_results.valid && results.date == previous_results.date &&
        results.calendar_entry == previous_results.calendar_entry && results.matchday == previous_results.matchday &&
        results.groups.size() == previous_results.groups.size(), "empty national departure preserves prior result identity");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 4 || argc == 5, "usage: national_coach_desk_app_tests ORIGINAL_DATA REGRESSION_SAVE_DIR 2PNWK001 [OUTPUT_CONTROLS]");
        if (!*argv[1]) { std::puts("national coach desk app tests skipped: Original data not configured"); return 77; }
        for (const char* name : {"NATWK001.FBM", "NATWK008.FBM", "NATWK012.FBM"}) untouched(argv[1], fs::path(argv[2]) / name);
        const auto qa = bmh::test::external_input("national-pending.fbm");
        owned_offer_retry(argv[1], fs::path(argv[2]));
        parked_buyer_retry(argv[1],fs::path(argv[2]));
        parked_seller_retry(argv[1],fs::path(argv[2]));
        unsupported_retry(argv[1], fs::path(argv[2]) / "NATWK001.FBM", qa);
        national_contract_expiry(argv[1], fs::path(argv[2]) / "NATWK001.FBM");
        later_coach_prefix_one(argv[1], argv[3], argc == 5 ? fs::path(argv[4]) : fs::path{});
        fresh_entry(argv[1], argv[3]);
        std::puts("national coach desk app tests: untouched Native load, physical save/reload/ADVANCE, Source empty departure, stale and unsupported retry passed");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "national_coach_desk_app_tests: %s\n", e.what()); return 1; }
}
