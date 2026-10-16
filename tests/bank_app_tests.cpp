#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "test_inputs.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

#include "app/career_session.hpp"
#include "app/window_actions.hpp"
#include "formats/m3d.hpp"
#include "formats/fbm.hpp"
#include "game/career.hpp"
#include "game/cup.hpp"
#include "game/matchday_setup.hpp"
#include "game/schedule.hpp"
#include "game/table.hpp"
#include "game/desk_actions.hpp"
#include "game/bank_credit.hpp"
#include "game/match_engine.hpp"
#include "game/month.hpp"
#include "game/weekly.hpp"
#include "game/matchday.hpp"
#include "game/matchday_setup.hpp"
#include "game/messages.hpp"
#include "game/season.hpp"
#include "game/save.hpp"
#include "game/turn.hpp"
#include "game/week.hpp"
#include "ui/flow.hpp"
#include "ui/pending_offer.hpp"

using namespace bmh;
using namespace bmh::ui;

namespace { void check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); } }
namespace {
bool expected_symlink_unavailable(const std::error_code& error) {
#if defined(_WIN32)
    // MSVC's system_category does not map ERROR_PRIVILEGE_NOT_HELD to errc.
    if (error.category() == std::system_category() && error.value() == 1314)
        return true;
#endif
    return error == std::errc::permission_denied ||
           error == std::errc::operation_not_permitted ||
           error == std::errc::function_not_supported ||
           error == std::errc::operation_not_supported;
}

void test_toto_deadline_native_save(const std::filesystem::path& data) {
    const auto input = bmh::test::external_input("bank-deadline.fbm");
    check(std::filesystem::exists(input), "captured Original WEEK196 fixture is available");
    const auto directory=std::filesystem::temp_directory_path() /
        ("openbmh-toto-deadline-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    for (int scenario : {0,1,2,3,4}) {
        const bool change=scenario==1 || scenario==4;
        const auto captured=scenario==3 ? bmh::test::external_input("bank-timer.fbm") : input;
        const auto file=directory/"AUTOSAVE.FBM";
        std::filesystem::copy_file(captured,file,std::filesystem::copy_options::overwrite_existing);
        app::CareerSession session(data, 8128, file);
        uint64_t revision=1;
        StartupFlow flow(data, directory);
        flow.click(190,215);
        // Authenticate a normal load request; the production session reads
        // the unchanged captured save. No Native availability/coupon setters.
        auto load=flow.send_key("ENTER");
        check(load.request==FlowRequest::file_action && flow.pending_file() && flow.pending_file()->path==file,
              "ordinary startup request authenticates the actual captured input path");
        flow.attach_loaded_game(session.desktop_projection(revision),session.preparation_projection(revision));
        const auto& items=flow.desktop()->state().items;
        const auto item=std::find_if(items.begin(),items.end(),[](const DeskItem& v){return v.kind==13&&!v.disabled;});
        check(item!=items.end(), "Original save exposes the Toto desk entry");
        flow.click(item->x+item->width/2,item->y+item->height/2);
        check(flow.toto() && flow.toto()->state().available && flow.toto()->state().matches.size()==11,
              "captured save has eleven real coupon pairings");
        const auto before=save_stream(session.state());
        const auto w=session.rand(); const auto g=session.game();
        if(change) {
            const auto pick=flow.toto()->state().matches[0].pick;
            flow.click(pick==1?280:266,25);
        }
        if(scenario==2 || scenario==4) {
            flow.click(100,200);
            const auto initial=flow.state_json();
            check(flow.in_toto_calculator() && !flow.process_timer_expired().changed && flow.state_json()==initial,
                  "deadline cannot consume the calculator before its initial Present");
            (void)flow.render(); (void)flow.after_toto_present(100);
            flow.send_key("2");
            check(!flow.process_timer_expired().changed, "deadline does not interrupt an unpresented digit flash");
            (void)flow.render(); (void)flow.after_toto_present(100);
            check(!flow.tick_toto_source_units(103).changed && !flow.process_timer_expired().changed,
                  "deadline retains the four-unit key wait");
            check(flow.tick_toto_source_units(104).changed && !flow.process_timer_expired().changed,
                  "released key must be presented before raw event8 is serviced");
            (void)flow.render(); (void)flow.after_toto_present(104);
            const auto cancel=flow.process_timer_expired();
            check(cancel.changed && cancel.request==FlowRequest::none && flow.in_toto_calculator() &&
                  !flow.toto()->pending() && save_stream(session.state())==before,
                  "raw event8 cancels locally without a Core request or saved writes");
            const auto restoring=flow.state_json();
            check(!flow.process_timer_expired().changed && flow.state_json()==restoring,
                  "repeated deadline cannot skip the calculator's restored-frame Present");
            (void)flow.render();
            check(flow.after_toto_present(104).changed && !flow.in_toto_calculator() &&
                  flow.screen_name()=="toto" && save_stream(session.state())==before,
                  "restored-frame ACK returns to coupon before the outer close");
        }
        const auto expired=flow.process_timer_expired(0,0);
        if(change) {
            check(expired.request==FlowRequest::submenu_action && std::holds_alternative<TotoRequest>(expired.submenu_action),
                  "deadline emits a changed coupon through the ordinary typed action");
            const auto request=std::get<TotoRequest>(expired.submenu_action);
            if(scenario==4) check(request.stake==0, "cancelled calculator clears stake while retaining changed tips");
            auto expected=session.state();
            check(submit_pools_coupon(expected,request.manager_slot,request.stake,request.tips),
                  "independent Source coupon mutation accepts the requested values");
            const auto pending=flow.state_json();
            check(!flow.process_timer_expired(0,0).changed && flow.state_json()==pending && save_stream(session.state())==before,
                  "deadline before ACK has no saved writes and keeps its exact request");
            std::string error;
            check(session.dispatch_toto(request,revision,error), "normal session accepts the deadline coupon");
            flow.acknowledge_preparation(session.preparation_projection(revision));
            check(save_stream(session.state())==save_stream(expected) && flow.screen_name()=="desktop",
                  "coupon ACK performs exactly Source writes, preserves all other bytes and returns to desk");
            const auto accepted=save_stream(session.state()); const auto accepted_revision=revision;
            check(!session.dispatch_toto(request,revision,error) && revision==accepted_revision && save_stream(session.state())==accepted,
                  "replayed coupon is inert after the ACK");
        } else {
            check(expired.request==FlowRequest::submenu_closed && flow.screen_name()=="desktop" &&
                  save_stream(session.state())==before && revision==1, "unchanged deadline has no coupon write or revision change");
        }
        check(session.rand().seed()==w.seed() && session.rand().calls()==w.calls() &&
              session.game().seed()==g.seed() && session.game().calls()==g.calls(), "coupon expiry and ACK preserve both RNG streams");
        check(flow.process_timer_expired(0,0).request==FlowRequest::kickoff && !flow.process_timer_expired(0,0).changed,
              "returned coupon desk emits exactly one ordinary deadline kickoff");
        std::printf("PASS: captured Toto deadline scenario=%d change=%d H8=%u\n",scenario,int(change),unsigned(session.state().memory.u8(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField)));
    }
    std::filesystem::remove_all(directory);
}

void throw_ad_spending_draw(int) { throw std::runtime_error("injected merchandise draw failure"); }

void test_toto_source_calculator(const std::filesystem::path& data) {
    Image backdrop(320, 240);
    std::fill(backdrop.pixels.begin(), backdrop.pixels.end(), 0x42);
    LeagueTableRangeDialog stake(data, backdrop, TotoStakeDialogProfile{TableRangeTextMode::flat});
    check(stake.minimum() == 10000 && stake.maximum() == 400000 && stake.value() == 0,
          "Toto format field has fixed bounds and zero initial value");
    const auto presets = stake.presets();
    for (size_t i = 0; i < 6; ++i)
        check(presets[i] == 20000u * uint32_t(i + 1), "Toto Source presets advance by 20,000");
    check(presets[6] == 10000 && presets[7] == 400000, "Toto minimum and maximum preset keys");
    (void)stake.render(); (void)stake.after_present();
    const auto press = [&](std::string_view key) {
        check(stake.source_input_ready(), "Toto input is ready after its previous frame/wait");
        stake.key(key); (void)stake.render(); (void)stake.after_present();
        check(!stake.tick_source_units(3), "Toto calculator needs all four Source wait units");
        check(stake.tick_source_units(1), "fourth Toto Source unit completes the key flash");
        (void)stake.render(); (void)stake.after_present();
    };
    press("ENTER");
    check(stake.value() == 10000 && stake.open(), "below-minimum J clamps and leaves the calculator open");
    press("SPACE");
    for (char c : std::string("999999")) press(std::string(1, c));
    check(stake.value() == 400000, "Toto digit overflow clamps to the Source maximum");
    press("ENTER");
    check(stake.waiting_for_exit_present(), "Toto accepted amount still requires the exit frame");
    (void)stake.render();
    const auto accepted = stake.after_present();
    check(accepted && accepted->result == TableRangeDialogResult::accept && accepted->value == 400000,
          "Toto accepts only after the final restored-frame Present");

    LeagueTableRangeDialog cancelled(data, backdrop, TotoStakeDialogProfile{TableRangeTextMode::flat});
    (void)cancelled.render(); (void)cancelled.after_present();
    check(cancelled.cancel_from_source_event(), "Toto raw event 8 cancels its calculator");
    auto restored = cancelled.render();
    check(restored.pixels == backdrop.pixels, "Toto calculator cancel restores the exact owned backdrop");
    const auto cancelled_result = cancelled.after_present();
    check(cancelled_result && cancelled_result->result == TableRangeDialogResult::cancel,
          "Toto calculator cancel has no accepted payment value");

    TableRangeDialogProfile table_profile;
    table_profile.text_mode = TableRangeTextMode::flat;
    LeagueTableRangeDialog table(data, backdrop, table_profile);
    check(table.presets()[0] == 1 && table.presets()[5] == 32 && table.maximum() == 34,
          "shared calculator keeps the original Table preset profile");

    TotoState coupon;
    coupon.manager_slot = 0; coupon.runtime_club = 0; coupon.revision = 1; coupon.available = true;
    coupon.matches.resize(11);
    TotoScreen screen(data, coupon);
    const auto source_background = load_vga(asset_path(data / "KB", "49.VGA"));
    auto frame = screen.render();
    check(frame.at(255, 100) == source_background.pixels[100 * 320 + 255],
          "Toto retains the source coupon raster rather than painting a generic panel");
    const auto widgets = load_vga(asset_path(data / "KB", "0.VGA"));
    GameRandom cross_oracle(0x340f1); // row 0, middle (draw) column
    for (int discard = 0; discard < 10; ++discard) (void)cross_oracle.range(0, 8);
    const int cross_y = cross_oracle.range(0, 2) * 12;
    int transparent_pixels = 0, painted_pixels = 0;
    for (int y = 0; y < 12; ++y) for (int x = 0; x < 16; ++x) {
        const auto sprite_pixel = widgets.pixels[size_t(cross_y + y) * widgets.width + 48 + x];
        if (sprite_pixel == 0) {
            check(frame.at(275 + x, 20 + y) == source_background.pixels[size_t(20 + y) * 320 + 275 + x],
                  "Toto cross zero pixels preserve the KB49 raster underneath");
            ++transparent_pixels;
        } else {
            check(frame.at(275 + x, 20 + y) == sprite_pixel,
                  "Toto cross copies nonzero ink including palette ff (Source mode is not a comparekey)");
            ++painted_pixels;
        }
    }
    check(transparent_pixels > 0 && painted_pixels > 0, "Original cross oracle covers transparency and painted ink");
    screen.click(266, 25);
    const auto request = screen.click(300, 220, true);
    check(request.kind == TotoRequestKind::submit && request.stake == 10000 && request.tips[0] == 1 &&
          !screen.close_requested(), "source home pick defaults stake and right-close waits for ACK");
    screen.reject_action("stale_manager_or_revision");
    check(!screen.close_requested() && !screen.pending(), "a rejected right-close retains the coupon for retry");
    const auto retry = screen.click(300, 220, true);
    coupon.revision = 2; coupon.stake = retry.stake;
    for (size_t i = 0; i < 11; ++i) coupon.matches[i].pick = retry.tips[i];
    screen.acknowledge(coupon);
    check(screen.close_requested(), "only authoritative right-close ACK dismisses the coupon");

    GameState projection_game;
    projection_game.manager_teams = {0, 1};
    projection_game.manager_records.resize(2 * 0xb0c);
    projection_game.club_ids = {0, 2}; projection_game.club_names = {'A', 0, 'B', 0};
    projection_game.memory.add_block(::bmh::layout::ActiveManager, 1); projection_game.memory.set8(::bmh::layout::ActiveManager, 1);
    projection_game.memory.add_block(::bmh::layout::Coupon, 2 * 0x14);
    projection_game.memory.add_block(::bmh::layout::Toto_FormatError_RuntimeField, 0x14);
    projection_game.memory.add_block(::bmh::layout::DeskActions_SubmitPoolsCoupon_RuntimeField, 44);
    projection_game.memory.add_block(::bmh::layout::CareerSession_DispatchToto_RuntimeField, 1); projection_game.memory.set8(::bmh::layout::CareerSession_DispatchToto_RuntimeField, 11);
    for (int i = 0; i < 11; ++i) {
        projection_game.memory.set16(::bmh::layout::DeskActions_SubmitPoolsCoupon_RuntimeField + uint32_t(i) * 4 + 2, 1);
        projection_game.memory.set8(::bmh::layout::Toto_FormatError_RuntimeField + 8 + uint32_t(i), 2);
    }
    projection_game.memory.set32(::bmh::layout::Toto_FormatError_RuntimeField + 4, 111111); projection_game.memory.set8(::bmh::layout::Toto_FormatError_RuntimeField + 19, 9);
    projection_game.memory.set32(::bmh::layout::Coupon + 4, 333333); projection_game.memory.set8(::bmh::layout::Coupon + 19, 10);
    const uint32_t active_coupon = ::bmh::layout::Coupon + 0x14;
    projection_game.memory.set32(active_coupon + 4, 222222); projection_game.memory.set8(active_coupon + 19, 3);
    const auto projected = toto_from_game(projection_game, 77);
    check(projected.previous_tips[0] == 2 && projected.previous_tips[10] == 2,
          "previous Toto tips remain global across managers");
    check(projected.manager_slot == 1 && projected.previous_correct == 3 && projected.previous_winnings == 222222,
          "Toto hits and winnings come from the active manager coupon, not global or other-manager values");
}

void test_source_empty_issue3_cup_day(const std::filesystem::path& data,
                                      const std::filesystem::path& temp_path) {
    const auto qa06 = bmh::test::external_input("bank-cup.fbm");
    check(std::filesystem::exists(qa06), "Issue-3 QA06 native save exists");
    const Savegame stored = load_savegame(qa06);
    constexpr uint32_t watcom_seed = 0x41386a06;
    app::CareerSession loaded(data, watcom_seed, qa06);
    const uint32_t entry = cup_entry_address(23);
    const auto& mem = loaded.state().memory;
    check(stored.checksum_ok, "Issue-3 QA06 checksum is valid");
    check(loaded.state().date == 388 && loaded.state().season_day() == 17,
          "Issue-3 QA06 is the captured date-388/day-17 desk");
    check(find_cup(mem, 8) == 23 && mem.u32(::bmh::layout::SavedCompetition) == 0x2000 &&
          int16_t(mem.u16(entry + 0x18)) == 8 && mem.u8(entry + 0x1a) == 0 &&
          mem.pointer(entry) == ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField28,
          "QA06 identifies the zero-team cup-8 entry-23 stage and its rebound list pointer");
    const uint32_t game_seed = mem.has(::bmh::layout::CareerSession_Save_RuntimeField) ? mem.u32(::bmh::layout::CareerSession_Save_RuntimeField) : 0;
    const auto desk = loaded.desktop_projection(1);
    check(desk.next_competition == 34 && desk.next_opponent == -1,
          "QA06 empty cup desk projects Source defaults 34/-1");
    check(save_stream(loaded.state()) == stored.stream && loaded.game().seed() == int32_t(game_seed) &&
          loaded.game().calls() == 0 && loaded.rand().seed() == watcom_seed && loaded.rand().calls() == 0,
          "QA06 binds and projects byte-exactly without random draws");

    std::string error;
    uint64_t revision = 1;
    const auto next = loaded.dispatch_kickoff(
        DeskRequest{DeskRequestKind::advance_turn, int(loaded.state().memory.u8(::bmh::layout::ActiveManager)), -1, -1, -1, revision},
        revision, error);
    check(next.kind == TurnDeskKind::NextDesk && loaded.state().date > 388,
          (std::string("QA06 empty cup desk advances to the next real desk: ") + error).c_str());

    // A nonempty stage whose saved descriptor does not contain its fixtures is malformed.
    GameState malformed_state = app::CareerSession(data, watcom_seed, qa06).state();
    malformed_state.memory.set8(entry + 0x1a, 2);
    WatcomRandom save_rand(watcom_seed);
    const auto malformed_dir = temp_path / "empty-cup-tests";
    std::filesystem::create_directories(malformed_dir);
    const auto malformed_path = malformed_dir / "QA06-BAD-NONEMPTY-CUP.FBM";
    write_savegame(malformed_path, make_savegame(malformed_state, save_rand));
    app::CareerSession malformed(data, watcom_seed, malformed_path);
    const Bytes malformed_before = save_stream(malformed.state());
    const uint32_t malformed_game_seed = malformed.game().seed();
    const uint64_t malformed_game_calls = malformed.game().calls();
    uint64_t malformed_revision = 1;
    const auto rejected = malformed.dispatch_kickoff(
        DeskRequest{DeskRequestKind::advance_turn, int(malformed.state().memory.u8(::bmh::layout::ActiveManager)), -1, -1, -1,
                    malformed_revision}, malformed_revision, error);
    check(rejected.kind == TurnDeskKind::Failed && save_stream(malformed.state()) == malformed_before &&
          malformed.game().seed() == malformed_game_seed && malformed.game().calls() == malformed_game_calls &&
          malformed.rand().calls() == 0,
          "nonempty cup descriptor without saved fixtures remains guarded without mutation");
}

struct TemporaryDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("openbmh-save-load-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory() { std::filesystem::create_directories(path); }
    ~TemporaryDirectory() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

uint32_t record_u32(const std::vector<uint8_t>& bytes, size_t at) {
    return uint32_t(bytes.at(at)) | uint32_t(bytes.at(at + 1)) << 8 |
           uint32_t(bytes.at(at + 2)) << 16 | uint32_t(bytes.at(at + 3)) << 24;
}

bool prepared_league_fixture(const GameState& g, int team, int& opponent, bool& home_side) {
    for (uint32_t gi = 0; gi < 6; gi++) {
        const uint32_t d = ::bmh::layout::MatchdayGroupDescriptors + gi * 0xd0;
        if (!g.memory.has(d + 0xd0) || g.memory.u8(d) != 0) continue;
        const int league = int(g.memory.u32(d + 0xcc));
        if (league < 0 || size_t(league) >= g.league_rounds.size()) continue;
        for (const auto& fixture : league_fixtures(league, int(g.league_rounds[size_t(league)]) + 1)) {
            if (fixture.home == team) opponent = fixture.away, home_side = true;
            else if (fixture.away == team) opponent = fixture.home, home_side = false;
            else continue;
            return true;
        }
    }
    return false;
}

void set_test_record_u32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
    for (int k = 0; k < 4; ++k) bytes.at(at + size_t(k)) = uint8_t(value >> (8 * k));
}

void test_season_profit_tax(const std::filesystem::path& data, const SetupOptions& career_options,
                            const ManagerChoice& first) {
    check(season_profit_tax(99999) == 0 && season_profit_tax(100000) == 0 &&
          season_profit_tax(100001) == 0 && season_profit_tax(100010) == 1 &&
          season_profit_tax(200000) == 10000 && season_profit_tax(500000) == 100000 &&
          season_profit_tax(600009) == 150000,
          "progressive tax follows source threshold, six bands and integer rounding");
    check(season_profit_tax_refund(100000, 600000, 150000) == 0 &&
          season_profit_tax_refund(0, 600000, 150000) == 50000 &&
          season_profit_tax_refund(-500000, 600000, 150000) == 150000,
          "next-season refund follows the source floor, proportional retention and strict threshold");

    app::CareerSession session(data, 0x22aa7712);
    SetupOptions options = career_options;
    options.managers = 2;
    std::string error;
    check(session.confirm_manager(options, first, error), "profit-season first manager confirmed");
    const auto context = session.selection_context(2);
    const auto other = std::find_if(context.club_order.begin(), context.club_order.end(),
        [&](int master) { return master != first.master_club; });
    check(other != context.club_order.end(), "profit-season second club available");
    ManagerChoice second = first;
    second.slot = 1;
    second.master_club = *other;
    second.name += " B";
    check(session.confirm_manager(options, second, error), "profit-season second manager confirmed");
    CareerInitializationResult initialized;
    check(session.finish(initialized, error), (std::string("profit-season career setup: ") + error).c_str());
    // This two-season tax scenario owns its contract decisions explicitly.
    // CareerSession no longer invents renewals for an absent presenter.
    int contract_decisions = 0;
    session.set_contract_expiry_presenter([&](GameState& state, WatcomRandom& random,
                                             int manager, int row) -> int {
        ++contract_decisions;
        const auto demands = contract_demands(state, manager, row, false, random);
        const auto valid = [&](int term) {
            const uint32_t demand = demands[size_t(term)];
            return demand && demand != UINT32_MAX && demand <= 200000;
        };
        int term = valid(1) ? 1 : -1;
        if (term < 0)
            for (int i = 0; i < int(demands.size()); ++i)
                if (valid(i)) { term = i; break; }
        if (term < 0) return 0;
        const uint32_t demand = demands[size_t(term)];
        return contract_offer(state, manager, row, term, demand, demand,
                              false, true, random, nullptr) ? 1 : 0;
    });
    uint64_t revision = initialized.desktop.revision;
    TurnAdvanceResult at_end;
    int dispatches = 0;
    for (; dispatches < 700; ++dispatches) {
        const int active = int(session.state().memory.u8(::bmh::layout::ActiveManager));
        at_end = session.dispatch_kickoff(
            DeskRequest{DeskRequestKind::advance_turn, active, -1, -1, -1, revision}, revision, error);
        check(at_end.kind != TurnDeskKind::Failed,
              (std::string("two-manager career reaches its real season-end desk at dispatch ") +
               std::to_string(dispatches) + " date " + std::to_string(session.state().date) + ": " + error).c_str());
        if (at_end.kind == TurnDeskKind::SeasonEndDesk) break;
    }
    check(at_end.kind == TurnDeskKind::SeasonEndDesk && dispatches < 700,
          "two-manager production driver reaches SeasonEndDesk");
    // format field visits every manager before34e24 starts annual accounts.
    // The tax fixture seeds its balances only after all closing desk hooks.
    const auto finish_closing_desks = [&] {
        const auto date = session.state().date;
        const auto season = session.state().season;
        while (session.state().memory.u8(::bmh::layout::DeskPosition) + 1 < session.state().manager_teams.size()) {
            const int position = session.state().memory.u8(::bmh::layout::DeskPosition);
            const auto remaining = session.dispatch_kickoff(
                DeskRequest{DeskRequestKind::advance_turn, int(session.state().memory.u8(::bmh::layout::ActiveManager)),
                            -1, -1, -1, revision}, revision, error);
            check(remaining.kind == TurnDeskKind::SeasonEndDesk && session.state().date == date &&
                  session.state().season == season && session.state().memory.u8(::bmh::layout::DeskPosition) == position + 1,
                  "remaining manager acknowledges closing desktop before annual tax");
        }
    };
    finish_closing_desks();

    constexpr size_t record_size = 0xb0c;
    for (int manager = 0; manager < 2; ++manager) {
        const size_t rec = size_t(manager) * record_size;
        const int64_t seeded_balance = int64_t(int32_t(record_u32(session.state().manager_records, rec + 0x324))) +
                                       int32_t(record_u32(session.state().manager_records, rec + 0x30c)) + 10000000;
        check(seeded_balance < std::numeric_limits<int32_t>::max(),
              "profit-season seed remains within the source money field");
        set_test_record_u32(session.state().manager_records, rec + 0x314, uint32_t(seeded_balance));
    }
    std::array<bool, 2> tax_notices{}, tax_questions{};
    SeasonTaxHooks tax_hooks;
    tax_hooks.observe = [&](const GameState& state, const SeasonTaxEvent& event) {
        check(event.kind == SeasonTaxEventKind::notice && event.manager_slot >= 0 && event.manager_slot < 2,
              "legal annual tax presents one Original ACK for each manager");
        const size_t slot = size_t(event.manager_slot), rec = slot * record_size;
        check(!tax_notices[slot] && event.legal_tax == season_profit_tax(event.profit) &&
              record_u32(state.manager_records, rec + 0x328) == 0 &&
              record_u32(state.manager_records, rec + 0x32c) == 0,
              "annual tax notice is shown once before carry values are stored");
        tax_notices[slot] = true;
    };
    tax_hooks.choose = [&](const GameState&, const SeasonTaxEvent& event) {
        const size_t slot = size_t(event.manager_slot);
        check(event.kind == SeasonTaxEventKind::question && tax_notices.at(slot) && !tax_questions.at(slot),
              "Original tax question follows its manager's ACK once");
        tax_questions.at(slot) = true;
        return false;
    };
    session.set_season_tax(std::move(tax_hooks));
    const auto profit_turn = session.dispatch_kickoff(
        DeskRequest{DeskRequestKind::advance_turn, int(session.state().memory.u8(::bmh::layout::ActiveManager)), -1, -1, -1, revision},
        revision, error);
    check(profit_turn.kind != TurnDeskKind::Failed,
          (std::string("profitable two-manager SeasonEndDesk continues: ") + error).c_str());
    session.set_season_tax({});
    check(tax_notices[0] && tax_notices[1] && tax_questions[0] && tax_questions[1],
          "both managers acknowledge the Original notice and explicitly answer NO");
    for (int manager = 0; manager < 2; ++manager) {
        const size_t rec = size_t(manager) * record_size;
        const int32_t taxable = int32_t(record_u32(session.state().manager_records, rec + 0x32c));
        const int32_t paid = int32_t(record_u32(session.state().manager_records, rec + 0x328));
        check(taxable > 100000 && paid == season_profit_tax(taxable),
              "each manager stores the real-season taxable profit and full legal tax");
    }
    const auto& mailbox_bytes = session.state().mailbox;
    // The notice prefix is also used by unrelated qualification messages;
    // distinguish the tax notice using its additional message segment.
    const std::string source_tax_prefix = message_text(session.state(), ::bmh::layout::Season_SeasonApplyProfitTax_TextPointer2);
    size_t source_notices = 0;
    if (!source_tax_prefix.empty()) {
        for (size_t entry = 0; entry + kMailboxEntry <= mailbox_bytes.size(); entry += kMailboxEntry) {
            const std::string text(mailbox_bytes.begin() + long(entry + 10),
                                   mailbox_bytes.begin() + long(entry + kMailboxEntry));
            if (text.find(source_tax_prefix) != std::string::npos) ++source_notices;
        }
    }
    check(source_notices == 0,
          "Original legacy routine tax acknowledgement is not delivered as mailbox mail");

    std::array<int32_t, 2> previous_profit{}, previous_tax{};
    for (int manager = 0; manager < 2; ++manager) {
        const size_t rec = size_t(manager) * record_size;
        previous_profit[size_t(manager)] = int32_t(record_u32(session.state().manager_records, rec + 0x32c));
        previous_tax[size_t(manager)] = int32_t(record_u32(session.state().manager_records, rec + 0x328));
    }
    TurnAdvanceResult next_season_end;
    for (int next = 0; next < 700; ++next) {
        const int active = int(session.state().memory.u8(::bmh::layout::ActiveManager));
        next_season_end = session.dispatch_kickoff(
            DeskRequest{DeskRequestKind::advance_turn, active, -1, -1, -1, revision}, revision, error);
        check(next_season_end.kind != TurnDeskKind::Failed,
              (std::string("second season reaches its real end desk: ") + error).c_str());
        if (next_season_end.kind == TurnDeskKind::SeasonEndDesk) break;
    }
    check(next_season_end.kind == TurnDeskKind::SeasonEndDesk,
          "taxed career reaches the following SeasonEndDesk");
    finish_closing_desks();
    check(contract_decisions > 0, "two-season tax scenario handles real contract expiries explicitly");
    for (int manager = 0; manager < 2; ++manager) {
        const size_t rec = size_t(manager) * record_size;
        const int64_t loss_balance = int64_t(int32_t(record_u32(session.state().manager_records, rec + 0x324))) +
                                     int32_t(record_u32(session.state().manager_records, rec + 0x30c)) - 10000000;
        check(loss_balance > std::numeric_limits<int32_t>::min(),
              "refund-season seed remains within the source money field");
        set_test_record_u32(session.state().manager_records, rec + 0x314, uint32_t(loss_balance));
    }
    const auto refund_turn = session.dispatch_kickoff(
        DeskRequest{DeskRequestKind::advance_turn, int(session.state().memory.u8(::bmh::layout::ActiveManager)), -1, -1, -1, revision},
        revision, error);
    check(refund_turn.kind != TurnDeskKind::Failed,
          (std::string("loss-making second SeasonEndDesk continues with refund: ") + error).c_str());
    for (int manager = 0; manager < 2; ++manager) {
        const size_t rec = size_t(manager) * record_size;
        check(previous_profit[size_t(manager)] > 100000 && previous_tax[size_t(manager)] > 0 &&
              int32_t(record_u32(session.state().manager_records, rec + 0x32c)) == 0,
              "next season clears the taxable-profit carry after refund calculation");
        const std::string refund_amount = format_amount(session.state(), previous_tax[size_t(manager)]);
        const std::string second_mail(session.state().mailbox.begin(), session.state().mailbox.end());
        const std::string refund_notice = message_text(session.state(), ::bmh::layout::Season_SeasonAccounts_TextPointer4) + refund_amount + ".";
        check(second_mail.find(refund_notice) != std::string::npos,
              "next-season mailbox reports the source-computed tax refund amount");
    }
}

int first_source_reset_player(const GameState& g, int team) {
    const SquadRange range = squad_range(team);
    for (size_t i = range.first; i < range.first + range.count; i++) {
        const uint16_t slot = g.squad_slots.at(i);
        if ((slot & 0x0fff) == 0x0fff) continue;
        const int player = slot & 0x0fff;
        if (size_t(player) < g.players.size() && (g.players[size_t(player)].w35c >> 27) == 0) return player;
    }
    return -1;
}

void check_forfeit_reset(const GameState& before, const GameState& after, int player) {
    check(player >= 0, "forfeit source-reset fixture has eligible player");
    const uint32_t old368 = before.players.at(size_t(player)).w368;
    const uint32_t old364 = before.players.at(size_t(player)).w364;
    const uint16_t cards = before.player_cards.at(size_t(player));
    const uint32_t expected368 = (old368 & 0xf8fc3fffu) |
                                 (uint32_t(cards >> 9 & 7) << 24) |
                                 (uint32_t(cards >> 5 & 0xf) << 14);
    const uint32_t expected364 = (old364 & 0xffe0ffffu) |
                                 (uint32_t(old364 >> 13 & 7) << 16);
    check(after.players.at(size_t(player)).w368 == expected368 &&
          after.players.at(size_t(player)).w364 == expected364,
          "forfeit restores card and injury attributes through legacy routine");
}

void test_friendly_booking(const GameState& source, int manager) {
    const int own_team = source.manager_teams.at(size_t(manager));
    int opponent = -1;
    for (int team = 0; team < 168; ++team) {
        if (team != own_team) { opponent = team; break; }
    }
    check(opponent >= 0, "friendly test has a club opponent");
    const int today = source.memory.u16(::bmh::layout::DaysIntoSeason);
    uint16_t day = 0;
    const FriendlyBookingQuote quote = quote_friendly_match(source, manager, opponent, true);
    check(quote.status == FriendlyQuoteStatus::quoted, "friendly fee quote is available before date selection");
    for (int candidate = today + 1; candidate < 0x16e; ++candidate) {
        const FriendlyDateStatus status = friendly_match_date_status(source, quote, uint16_t(candidate));
        if (status == FriendlyDateStatus::available || status == FriendlyDateStatus::available_with_calendar_event) {
            day = uint16_t(candidate); break;
        }
    }
    check(day != 0, "source-calendar fixture has an available friendly date");
    const Bytes before_bytes = save_stream(source);
    const auto pure_quote = quote_friendly_match(source, manager, opponent, true);
    check(pure_quote.status == FriendlyQuoteStatus::quoted && pure_quote.cost == quote.cost &&
          save_stream(source) == before_bytes,
          "friendly quote is read-only and returns the source cost");

    GameState full_plan = source;
    int fill_slot = 0;
    for (int candidate = today + 3; candidate < 0x16e && fill_slot < 30; ++candidate) {
        if (std::abs(candidate - int(day)) <= 1) continue;
        const uint32_t date_at = ::bmh::layout::FriendlyDays + uint32_t(fill_slot) * 2;
        const uint32_t teams_at = ::bmh::layout::FriendlyTeams + uint32_t(fill_slot) * 4;
        full_plan.memory.set16(date_at, uint16_t(candidate));
        full_plan.memory.set16(teams_at, uint16_t((own_team + 1) % 168));
        full_plan.memory.set16(teams_at + 2, uint16_t((own_team + 2) % 168));
        ++fill_slot;
    }
    check(fill_slot == 30, "full friendly plan fixture fills all 30 source slots");
    const Bytes full_plan_before = save_stream(full_plan);
    FriendlyBookingRequest full_request;
    full_request.quote = quote; full_request.season_day = day;
    full_request.accept_cost = true; full_request.approving_manager = quote.payer_manager;
    check(friendly_match_date_status(full_plan, quote, day) == FriendlyDateStatus::plan_full &&
          book_friendly_match(full_plan, full_request).status == FriendlyBookingStatus::plan_full &&
          save_stream(full_plan) == full_plan_before,
          "full 30-slot plan rejects before fee and preserves every previously booked fixture");

    FriendlyBookingRequest request;
    request.quote = pure_quote; request.season_day = day; request.accept_cost = false;
    GameState declined = source;
    const auto declined_result = book_friendly_match(declined, request);
    check(declined_result.status == FriendlyBookingStatus::declined &&
          save_stream(declined) == before_bytes,
          "declining the home-game cost leaves planner, calendar and finances untouched");

    if (source.level != 0) {
        GameState cannot_pay = source;
        const size_t rec = size_t(manager) * 0xb0c;
        for (size_t i = 0; i < 4; ++i) {
            cannot_pay.manager_records.at(rec + 0x314 + i) = 0;
            cannot_pay.manager_records.at(rec + 0x31c + i) = 0;
        }
        const Bytes no_funds_before = save_stream(cannot_pay);
        request.accept_cost = true;
        request.approving_manager = pure_quote.payer_manager;
        check(book_friendly_match(cannot_pay, request).status == FriendlyBookingStatus::insufficient_funds &&
              save_stream(cannot_pay) == no_funds_before,
              "unaffordable friendly booking is rejected without partial planner or finance writes");
    }

    GameState adjacent_conflict = source;
    adjacent_conflict.memory.set16(::bmh::layout::FriendlyDays, uint16_t(day + 1));
    adjacent_conflict.memory.set16(::bmh::layout::FriendlyTeams, uint16_t(own_team));
    adjacent_conflict.memory.set16(::bmh::layout::FixtureSecondBookedFriendly, uint16_t(opponent));
    check(friendly_match_date_status(adjacent_conflict, pure_quote, day) == FriendlyDateStatus::unavailable,
          "source three-day conflict window rejects a fixture on the following day");

    for (const uint16_t raw_event : {uint16_t(0x0040), uint16_t(0x0100)}) {
        GameState raw_calendar = source;
        raw_calendar.memory.set16(::bmh::layout::CompetitionCalendar + uint32_t(day) * 2, raw_event);
        const Bytes raw_before = save_stream(raw_calendar);
        check(friendly_match_date_status(raw_calendar, pure_quote, day) == FriendlyDateStatus::unavailable &&
              book_friendly_match(raw_calendar, request).status == FriendlyBookingStatus::date_unavailable &&
              save_stream(raw_calendar) == raw_before,
              "selected raw 0x40/0x100 calendar flags fail legacy routine without UI bit 15 and stay inert");
    }

    GameState wrapped_counter = source;
    wrapped_counter.memory.set8(::bmh::layout::FriendlyPlanCount, 254);
    const FriendlyBookingQuote wrapped_quote = quote_friendly_match(wrapped_counter, manager, opponent, true);
    const Bytes wrapped_before = save_stream(wrapped_counter);
    FriendlyBookingRequest wrapped_request{wrapped_quote, day, false, -1, true, wrapped_quote.payer_manager};
    check(friendly_match_date_status(wrapped_counter, wrapped_quote, day) == FriendlyDateStatus::plan_full &&
          book_friendly_match(wrapped_counter, wrapped_request).status == FriendlyBookingStatus::plan_full &&
          save_stream(wrapped_counter) == wrapped_before,
          "counter overflow is rejected before charging or shifting the planner");

    request.accept_cost = true;
    request.approving_manager = pure_quote.payer_manager;
    GameState accepted = source;
    const size_t record = size_t(manager) * 0xb0c;
    const int32_t cash_before = int32_t(record_u32(accepted.manager_records, record + 0x314));
    const uint32_t expense_before = record_u32(accepted.manager_records, record + 0x8a0);
    const uint8_t planner_count_before = accepted.memory.u8(::bmh::layout::FriendlyPlanCount);
    const uint16_t calendar_before = accepted.memory.u16(::bmh::layout::CompetitionCalendar + uint32_t(day) * 2);
    const auto accepted_result = book_friendly_match(accepted, request);
    check(accepted_result.status == FriendlyBookingStatus::booked &&
          accepted_result.home_team == own_team && accepted_result.away_team == opponent,
          "accepted booking stores the manager club at home");
    bool found = false;
    for (int i = 0; i < 30; ++i) {
        if (accepted.memory.u16(::bmh::layout::FriendlyDays + uint32_t(i) * 2) != day) continue;
        found = accepted.memory.u16(::bmh::layout::FriendlyTeams + uint32_t(i) * 4) == uint16_t(own_team) &&
                accepted.memory.u16(::bmh::layout::FixtureSecondBookedFriendly + uint32_t(i) * 4) == uint16_t(opponent);
    }
    check(found && accepted.memory.u16(::bmh::layout::CompetitionCalendar + uint32_t(day) * 2) == uint16_t(calendar_before | 0x4001),
          "accepted booking inserts a sorted team pair and marks its calendar day");
    check(accepted.memory.u8(::bmh::layout::FriendlyPlanCount) == uint8_t(planner_count_before + 2),
          "accepted booking increments the original planned-team counter by two");
    if (source.level != 0) {
        check(int32_t(record_u32(accepted.manager_records, record + 0x314)) == cash_before - pure_quote.cost &&
              record_u32(accepted.manager_records, record + 0x8a0) == expense_before + uint32_t(pure_quote.cost),
              "successful home booking permanently charges the quoted fee and books expense-other");
    }

    FriendlyBookingRequest forged = request;
    forged.season_day = uint16_t(day + 4);
    for (; forged.season_day < 0x16e; ++forged.season_day) {
        const FriendlyDateStatus status = friendly_match_date_status(source, pure_quote, forged.season_day);
        if (status == FriendlyDateStatus::available || status == FriendlyDateStatus::available_with_calendar_event) break;
    }
    forged.approving_manager = manager == pure_quote.payer_manager ? manager + 1 : manager;
    GameState forged_state = source;
    const Bytes forged_bytes = save_stream(forged_state);
    check(forged.season_day < 0x16e &&
          book_friendly_match(forged_state, forged).status == FriendlyBookingStatus::invalid_request &&
          save_stream(forged_state) == forged_bytes,
          "stale or forged cost approval is rejected without mutation");

    FriendlyBookingRequest repeat = request;
    GameState repeated = accepted;
    const Bytes accepted_bytes = save_stream(repeated);
    check(book_friendly_match(repeated, repeat).status == FriendlyBookingStatus::stale_quote &&
          save_stream(repeated) == accepted_bytes,
          "same-day duplicate fixture is rejected without another charge");
}

void test_friendly_season_end_dates(const GameState& source, int manager) {
    int opponent = -1;
    for (int team = 0; team < 168; ++team) {
        if (team != source.manager_teams.at(size_t(manager))) { opponent = team; break; }
    }
    check(opponent >= 0, "season-boundary fixture has an opponent");
    auto at_season_start = [&](uint32_t start) {
        GameState g = source;
        g.season_start = start;
        g.date = start;
        g.memory.set16(::bmh::layout::DaysIntoSeason, 0);
        g.memory.set8(::bmh::layout::FriendlyPlanCount, 0);
        for (size_t i = 0; i < 30; ++i) {
            g.memory.set16(::bmh::layout::FriendlyDays + uint32_t(i) * 2, 0);
            g.memory.set16(::bmh::layout::FriendlyTeams + uint32_t(i) * 4, 0xffff);
            g.memory.set16(::bmh::layout::FixtureSecondBookedFriendly + uint32_t(i) * 4, 0xffff);
        }
        for (uint32_t day = 0; day <= 365; ++day)
            g.memory.set16(::bmh::layout::CompetitionCalendar + day * 2, 0);
        return g;
    };

    GameState regular = at_season_start(0);
    const auto regular_quote = quote_friendly_match(regular, manager, opponent, true);
    check(friendly_match_date_status(regular, regular_quote, 365) == FriendlyDateStatus::invalid_date &&
          friendly_match_date_status(regular, regular_quote, 366) == FriendlyDateStatus::invalid_date,
          "regular-year July 1 season-end and out-of-calendar day cannot be booked");

    GameState leap = at_season_start(365);
    const auto leap_quote = quote_friendly_match(leap, manager, opponent, true);
    check(friendly_match_date_status(leap, leap_quote, 365) == FriendlyDateStatus::available &&
          friendly_match_date_status(leap, leap_quote, 366) == FriendlyDateStatus::invalid_date,
          "leap-year June 30 remains bookable while following July 1 is outside the calendar");
}

// legacy routine: an expired contract (months 0, old duration in byte +6) is renewed on the
// manager's yes, freed on his no, and freed with a notice when the sponsor's demand is
// above the club's attraction; boards of the same bundle follow.
void test_advertising_renewal(const std::filesystem::path& data) {
    app::CareerSession session(data, 0x2a3b4c52);
    const auto choices = session.selection_context(1);
    SetupOptions options;
    ManagerChoice choice;
    choice.slot = 0;
    choice.name = "Verlaengerung";
    choice.master_club = choices.club_order.front();
    choice.selection_club = 0;
    choice.roster_revision = choices.revision;
    std::string error;
    check(session.confirm_manager(options, choice, error), "renewal test manager confirms");
    CareerInitializationResult initialized;
    check(session.finish(initialized, error), "renewal test career starts");
    GameState& g = session.state();
    const int manager = int(g.memory.u8(::bmh::layout::ActiveManager));
    const size_t contracts = size_t(manager) * 0xb0c + 0xa34;
    const int attraction = bmh::club_attraction(g, g.manager_teams.at(size_t(manager)));
    check(attraction > 0, "the renewal test club has an attraction above zero");
    auto expired = [&](int slot, uint8_t group, uint16_t demand) {
        const size_t at = contracts + size_t(slot) * 0x0c;
        set_test_record_u32(g.manager_records, at, 40000);
        g.manager_records.at(at + 4) = group;
        g.manager_records.at(at + 5) = 0;
        g.manager_records.at(at + 6) = 12;
        g.manager_records.at(at + 7) = 3;
        g.manager_records.at(at + 8) = uint8_t(demand);
        g.manager_records.at(at + 9) = uint8_t(demand >> 8);
    };
    WatcomRandom rand(7);
    // the shirt: a modest demand is offered, yes renews the old twelve months
    expired(14, 1, 0);
    auto shirt = check_advertising_renewal(g, manager, 14, rand);
    check(shirt.status == AdvertisingRenewal::Status::offered && shirt.contract_slot == 14 && !shirt.question.empty(),
          "an expired shirt contract with a low demand offers the renewal");
    check(answer_advertising_renewal(g, manager, 14, true) && g.manager_records.at(contracts + 14 * 0x0c + 5) == 12 &&
          g.manager_records.at(contracts + 14 * 0x0c + 6) == 0,
          "yes renews the shirt contract for its old duration");
    // TV: no frees the place
    expired(15, 1, 0);
    check(check_advertising_renewal(g, manager, 15, rand).status == AdvertisingRenewal::Status::offered &&
          answer_advertising_renewal(g, manager, 15, false) && record_u32(g.manager_records, contracts + 15 * 0x0c) == 0,
          "no frees the TV place");
    // a demand far above the attraction: the sponsor declines and the place is free
    expired(14, 1, 0xffff);
    const auto declined = check_advertising_renewal(g, manager, 14, rand);
    check(declined.status == AdvertisingRenewal::Status::declined_by_sponsor && !declined.notice.empty() &&
          record_u32(g.manager_records, contracts + 14 * 0x0c) == 0,
          "a sponsor demanding more than the attraction declines and frees the place");
    // not expired: nothing to renew
    check(check_advertising_renewal(g, manager, 14, rand).status == AdvertisingRenewal::Status::not_expired,
          "a free place has nothing to renew");

    // Exercise the Session transaction used by the UI, rather than only the two
    // underlying Core calls above. The click binds display 14 to physical slot 14.
    expired(14, 1, 0);
    ui::AdvertisingRequest request;
    request.request_id = 1;
    request.manager_slot = manager;
    request.runtime_club = g.manager_teams.at(size_t(manager));
    request.revision = 90;
    request.kind = ui::AdvertisingActionKind::check_renewal;
    request.renewal_display_slot = 14;
    request.renewal_contract_slot = 14;
    std::copy_n(g.manager_records.begin() + std::ptrdiff_t(contracts + 14 * 0x0c), 12,
                request.renewal_expected_contract.begin());
    const auto calls_before = session.rand().calls();
    const auto game_calls_before = session.game().calls();
    uint64_t revision = request.revision;
    const auto before_wrong_slot=save_stream(g);
    auto wrong_slot=request; wrong_slot.renewal_contract_slot=13;
    const auto wrong_slot_result=session.dispatch_advertising_renewal(wrong_slot,revision,error,
        [](const GameState&,const AdvertisingRenewal&) {return app::CareerSession::AdvertisingRenewalPresentation::renew;});
    check(!wrong_slot_result && save_stream(g)==before_wrong_slot &&
          revision==request.revision && session.rand().calls()==calls_before,
          "a display-to-physical slot mismatch is rejected without W, Native or revision changes");
    const auto applied = session.dispatch_advertising_renewal(request, revision, error,
        [](const GameState&, const AdvertisingRenewal& q) {
            return q.status == AdvertisingRenewal::Status::offered
                ? app::CareerSession::AdvertisingRenewalPresentation::renew
                : app::CareerSession::AdvertisingRenewalPresentation::abort;
        });
    check(applied && applied->status == AdvertisingRenewal::Status::offered &&
          revision == request.revision + 1 && session.rand().calls() == calls_before + 1 &&
          session.game().calls() == game_calls_before &&
          g.manager_records.at(contracts + 14 * 0x0c + 5) == 12 &&
          g.manager_records.at(contracts + 14 * 0x0c + 6) == 0,
          "Session commits one source-bound renewal answer and exactly one W draw");
    const auto committed = save_stream(g);
    const auto committed_w = session.rand().calls();
    request.request_id = 2;
    request.revision = revision;
    revision = request.revision;
    expired(14, 1, 0); // prepare a fresh offer, then decline modal completion
    std::copy_n(g.manager_records.begin() + std::ptrdiff_t(contracts + 14 * 0x0c), 12,
                request.renewal_expected_contract.begin());
    const auto before_abort = save_stream(g);
    const auto before_abort_w = session.rand().calls();
    const auto rejected = session.dispatch_advertising_renewal(request, revision, error,
        [](const GameState&, const AdvertisingRenewal&) { return app::CareerSession::AdvertisingRenewalPresentation::abort; });
    check(!rejected && revision == request.revision && save_stream(g) == before_abort &&
          session.rand().calls() == before_abort_w && !error.empty(),
          "unsupported modal completion rolls back the staged renewal and W draw");
    // These runtime-only bytes are intentionally absent from the freshly loaded
    // SaveState layout. Create explicit test-owned cells so this case exercises
    // the Session's optional-byte fingerprint and checkpoint restoration path.
    check(!g.memory.has(::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField) && !g.memory.has(::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField),
          "fresh-career fixture starts without the unsaved club-modal runtime bytes");
    const auto serialized_without_runtime_cells = save_stream(g);
    g.memory.add_block(::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField, 1);
    g.memory.add_block(::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField, 1);
    g.memory.set8(::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField, 0x2c);
    g.memory.set8(::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField, 0x35);
    check(save_stream(g) == serialized_without_runtime_cells,
          "controlled afb5b/afb5e fixture cells remain outside the serialized Native stream");
    const auto runtime_before = g.chance_rain;
    const auto managers_before = g.manager_teams;
    const auto word_b5b = g.memory.u8(::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField), word_b5e = g.memory.u8(::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField);
    const auto native_before_mutation = save_stream(g);
    const auto w_before_mutation = session.rand().calls();
    const auto g_before_mutation = session.game().calls();
    const auto revision_before_mutation = revision;
    const auto runtime_reject = session.dispatch_advertising_renewal(request, revision, error,
        [&](const GameState&, const AdvertisingRenewal&) {
            g.chance_rain = runtime_before + 7;
            g.manager_teams.at(size_t(manager)) = (managers_before.at(size_t(manager)) + 1) % 89;
            g.memory.set8(::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField, uint8_t(word_b5b ^ 0x20));
            g.memory.set8(::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField, uint8_t(word_b5e ^ 0x10));
            return app::CareerSession::AdvertisingRenewalPresentation::abort;
        });
    check(!runtime_reject && save_stream(g) == native_before_mutation &&
          g.chance_rain == runtime_before && g.manager_teams == managers_before &&
          g.memory.u8(::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField) == word_b5b && g.memory.u8(::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField) == word_b5e &&
          session.rand().calls() == w_before_mutation && session.game().calls() == g_before_mutation &&
          revision == revision_before_mutation,
          "presenter abort restores Native, tested runtime values, manager binding, source bytes, RNGs and revision");
    const auto before_answer_error = save_stream(g);
    const auto answer_error_w = session.rand().calls();
    const auto answer_error_revision = revision;
    const auto answer_error = session.dispatch_advertising_renewal(request, revision, error,
        [&](const GameState& staged, const AdvertisingRenewal&) {
            auto& altered=const_cast<GameState&>(staged);
            altered.manager_records.at(contracts + 14 * 0x0c + 6)=0;
            return app::CareerSession::AdvertisingRenewalPresentation::renew;
        });
    check(!answer_error && save_stream(g)==before_answer_error &&
          session.rand().calls()==answer_error_w && revision==answer_error_revision,
          "a failed staged Core answer restores the entire live session before returning");
    (void)committed; (void)committed_w;
    request.request_id = 2; request.revision = revision;
    const auto before_reentry_w = session.rand().calls();
    const auto before_reentry_game = session.game().calls();
    const auto before_reentry_scope = session.strengths_checkpoint().next_scope;
    const auto before_reentry_stream = save_stream(g);
    bool nested_strengths_opened=false;
    const auto outer = session.dispatch_advertising_renewal(request, revision, error,
        [&](const GameState&, const AdvertisingRenewal&) {
            const auto opening = session.strengths_open_request(revision);
            if (opening) nested_strengths_opened=session.dispatch_strengths(*opening, revision, error).has_value();
            return app::CareerSession::AdvertisingRenewalPresentation::renew;
        });
    check(!outer && nested_strengths_opened && !session.strengths_owner() &&
          session.strengths_checkpoint().next_scope==before_reentry_scope &&
          session.rand().calls()==before_reentry_w && session.game().calls()==before_reentry_game &&
          revision==request.revision && save_stream(g)==before_reentry_stream,
          "a reentrant valid Strengths-open restores its owner ID, Native, both RNGs and the outer revision");
    request.request_id = 2; request.revision = revision;
    const auto retry = session.dispatch_advertising_renewal(request, revision, error,
        [](const GameState&, const AdvertisingRenewal&) { return app::CareerSession::AdvertisingRenewalPresentation::decline; });
    check(retry && g.manager_records.at(contracts + 14 * 0x0c) == 0 &&
          revision == request.revision + 1,
          "an aborted/reentrant offer remains safely retryable and Source No frees its contract");
    request.request_id = 3;
    request.revision = revision;
    expired(14, 1, 0xffff);
    std::copy_n(g.manager_records.begin() + std::ptrdiff_t(contracts + 14 * 0x0c), 12,
                request.renewal_expected_contract.begin());
    const auto before_refusal_w = session.rand().calls();
    const auto refusal_bytes = save_stream(g);
    const auto refusal_revision = revision;
    const auto refusal_abort = session.dispatch_advertising_renewal(request, revision, error,
        [](const GameState&, const AdvertisingRenewal&) { return app::CareerSession::AdvertisingRenewalPresentation::abort; });
    check(!refusal_abort && save_stream(g) == refusal_bytes && session.rand().calls() == before_refusal_w &&
          revision == refusal_revision,
          "aborting the required sponsor refusal acknowledgement rolls back its staged mailbox/contract/W changes");
    // The notice projection is the Original interactive legacy routine(manager|0x80,text,0) ACK.
    AdvertisingRenewal expected_notice; expected_notice.status=AdvertisingRenewal::Status::declined_by_sponsor;
    expected_notice.contract_slot=14; expected_notice.notice="Source sponsor refusal";
    const auto notice_view=ui::advertising_renewal_notice_dialog(g,manager,expected_notice);
    check(notice_view.kind==TaxDialogKind::acknowledgement &&
          notice_view.source_manager_flags==uint8_t(manager|0x80) && notice_view.body==expected_notice.notice,
          "sponsor refusal uses the existing interactive manager ACK notice adapter");
    const auto refusal = session.dispatch_advertising_renewal(request, revision, error,
        [](const GameState&, const AdvertisingRenewal&) {
            return app::CareerSession::AdvertisingRenewalPresentation::acknowledged;
        });
    check(refusal && refusal->status == AdvertisingRenewal::Status::declined_by_sponsor &&
          !refusal->notice.empty() && session.rand().calls() == before_refusal_w + 1 &&
          revision == request.revision + 1 && record_u32(g.manager_records, contracts + 14 * 0x0c) == 0,
          "Source sponsor refusal settles the contract and mailbox notice without opening a question");

}

void test_advertising_acceptance(const std::filesystem::path& data,
                                 const std::filesystem::path& temp_path) {
    app::CareerSession session(data, 0x2a3b4c51);
    const auto choices = session.selection_context(1);
    check(!choices.club_order.empty(), "advertising acceptance has a startup manager choice");
    SetupOptions options;
    ManagerChoice choice;
    choice.slot = 0;
    choice.name = "Werbung";
    choice.master_club = choices.club_order.front();
    choice.selection_club = 0;
    choice.roster_revision = choices.revision;
    std::string error;
    check(session.confirm_manager(options, choice, error),
          (std::string("advertising manager confirms: ") + error).c_str());
    CareerInitializationResult initialized;
    check(session.finish(initialized, error),
          (std::string("advertising test career starts: ") + error).c_str());
    GameState& g = session.state();
    const int manager = int(g.memory.u8(::bmh::layout::ActiveManager));
    const size_t rec = size_t(manager) * 0xb0c;
    const size_t contracts = rec + 0xa34;
    const size_t pool = rec + 0x8cc;
    check(g.memory.has(::bmh::layout::AdGroupId), "advertising group counter is saved in the native state");
    std::fill(g.manager_records.begin() + std::ptrdiff_t(contracts),
              g.manager_records.begin() + std::ptrdiff_t(contracts + 16 * 0x0c), 0);
    std::fill(g.manager_records.begin() + std::ptrdiff_t(pool),
              g.manager_records.begin() + std::ptrdiff_t(pool + 3 * 0x78), 0);
    auto offer = [&](size_t at, uint32_t amount, uint8_t bundle, uint8_t sponsor) {
        set_test_record_u32(g.manager_records, at, amount);
        g.manager_records.at(at + 4) = bundle;
        g.manager_records.at(at + 5) = 12;
        g.manager_records.at(at + 7) = sponsor;
    };
    offer(pool, 32000, 2, 2);
    offer(pool + 0x0c, 18000, 1, 3);
    offer(pool + 0x78, 12000, 1, 1);
    offer(pool + 0xf0, 50000, 1, 4);
    g.memory.set8(::bmh::layout::AdGroupId, 41);

    const Bytes before_projection = save_stream(g);
    const uint64_t game_draws = session.game().calls();
    const uint64_t watcom_draws = session.rand().calls();
    auto view = advertising_offers(g, manager, 13);
    check(view.status == AdvertisingOfferStatus::available && view.pool_group == 0 &&
          view.free_boards == 14 && view.offers.size() == 10 && view.offers[0].present &&
          view.offers[0].monthly_amount == 32000 && view.offers[0].bundle_units == 2 &&
          view.offers[0].sponsor_id == 2 && view.offers[2].present == false,
          "read-only projection exposes every raw slot and reads the bundle byte at +4");
    check(save_stream(g) == before_projection && session.game().calls() == game_draws &&
          session.rand().calls() == watcom_draws,
          "advertising offer projection changes neither saved state nor either RNG stream");
    check(view.contract_slot >= 0 && view.contract_slot < 14,
          "screen index is explicitly mapped to the physical contract row");

    auto request_for = [&](const AdvertisingOfferList& projection, int raw) {
        AdvertisingOfferRequest request;
        request.manager = manager;
        request.display_slot = projection.display_slot;
        request.contract_slot = projection.contract_slot;
        const size_t target = contracts + size_t(request.contract_slot) * 0x0c;
        std::copy_n(g.manager_records.begin() + std::ptrdiff_t(target), 12, request.expected_contract.begin());
        request.raw_slot = raw;
        const size_t offer_at = pool + size_t(projection.pool_group) * 0x78 + size_t(raw) * 0x0c;
        std::copy_n(g.manager_records.begin() + std::ptrdiff_t(offer_at), 12, request.expected_offer.begin());
        return request;
    };

    auto stale = request_for(view, 0);
    const size_t stale_pool = pool;
    g.manager_records.at(stale_pool + 11) ^= 1;
    const Bytes before_stale = save_stream(g);
    check(accept_advertising_offer(g, stale).status == AdvertisingOfferStatus::stale_offer &&
          save_stream(g) == before_stale,
          "stale projected offer is rejected without partial writes");
    g.manager_records.at(stale_pool + 11) ^= 1;

    const uint32_t cash_before = record_u32(g.manager_records, rec + 0x314);
    auto accepted = accept_advertising_offer(g, request_for(view, 0));
    check(accepted.status == AdvertisingOfferStatus::accepted &&
          g.memory.u8(::bmh::layout::AdGroupId) == 42 && record_u32(g.manager_records, pool) == 0,
          "accepted two-board bundle consumes its saved offer and advances the saved group id once");
    int bundled = 0;
    for (int slot = 0; slot < 14; ++slot) {
        const size_t at = contracts + size_t(slot) * 0x0c;
        if (record_u32(g.manager_records, at) == 32000 && g.manager_records.at(at + 4) == 41 &&
            g.manager_records.at(at + 7) == 2) ++bundled;
    }
    check(bundled == 2 && record_u32(g.manager_records, rec + 0x314) == cash_before &&
          session.game().calls() == game_draws && session.rand().calls() == watcom_draws,
          "bundle copy writes group byte +4, books no immediate money, and draws no RNG");

    // The high bit is a replaceable option marker on a selected shirt slot.
    g.manager_records.at(contracts + 14 * 0x0c + 6) = 0x80;
    set_test_record_u32(g.manager_records, contracts + 14 * 0x0c, 777);
    auto shirt = advertising_offers(g, manager, 14);
    check(shirt.status == AdvertisingOfferStatus::available && shirt.offers.size() == 10 &&
          shirt.offers[0].can_accept, "shirt offer is available over its source option slot");
    check(accept_advertising_offer(g, request_for(shirt, 0)).status == AdvertisingOfferStatus::accepted &&
          record_u32(g.manager_records, contracts + 14 * 0x0c) == 12000 &&
          record_u32(g.manager_records, pool + 0x78) == 0,
          "shirt contract copies into slot 14 and consumes only its own offer amount");
    auto television = advertising_offers(g, manager, 15);
    check(television.status == AdvertisingOfferStatus::available && television.offers.size() == 5 &&
          accept_advertising_offer(g, request_for(television, 0)).status == AdvertisingOfferStatus::accepted &&
          record_u32(g.manager_records, contracts + 15 * 0x0c) == 50000 &&
          record_u32(g.manager_records, pool + 0xf0) == 0,
          "television offer uses the five-entry pool and fixed slot 15");

    GameState extension = g;
    const auto extension_view = advertising_offers(extension, manager, 0);
    const size_t extension_target = contracts + size_t(extension_view.contract_slot) * 0x0c;
    extension.manager_records.at(extension_target + 5) = 0;
    extension.manager_records.at(extension_target + 6) = 1;
    const auto extension_reoffer = advertising_offers(extension, manager, 0);
    const auto extension_before = save_stream(extension);
    AdvertisingOfferRequest extension_request;
    extension_request.manager = manager;
    extension_request.display_slot = 0;
    extension_request.contract_slot = extension_view.contract_slot;
    std::copy_n(extension.manager_records.begin() + std::ptrdiff_t(extension_target), 12,
                extension_request.expected_contract.begin());
    extension_request.raw_slot = 1;
    extension_request.expected_offer = extension_reoffer.offers[1].bytes;
    check(extension_reoffer.status == AdvertisingOfferStatus::unsupported_extension &&
          accept_advertising_offer(extension, extension_request).status ==
              AdvertisingOfferStatus::unsupported_extension &&
          save_stream(extension) == extension_before,
          "source extension prompt stays unavailable and leaves state unchanged");

    GameState full = g;
    for (int slot = 0; slot < 14; ++slot) {
        const size_t at = contracts + size_t(slot) * 0x0c;
        set_test_record_u32(full.manager_records, at, uint32_t(100000 + slot));
        full.manager_records.at(at + 6) = 0;
    }
    auto full_view = advertising_offers(full, manager, 0);
    check(full_view.status == AdvertisingOfferStatus::available && full_view.free_boards == 0 &&
          !full_view.offers[0].can_accept,
          "full fourteen-board inventory disables an otherwise present bundle");
    auto full_request = request_for(full_view, 1);
    std::copy_n(full.manager_records.begin() + std::ptrdiff_t(
                    contracts + size_t(full_request.contract_slot) * 0x0c),
                12, full_request.expected_contract.begin());
    std::copy_n(full.manager_records.begin() + std::ptrdiff_t(pool + 0x0c),
                12, full_request.expected_offer.begin());
    const Bytes full_before = save_stream(full);
    const auto full_result = accept_advertising_offer(full, full_request);
    check(full_result.status == AdvertisingOfferStatus::insufficient_boards &&
          save_stream(full) == full_before,
          "full board inventory rejects acceptance atomically");

    GameState selected_regular = g;
    for (int slot = 0; slot < 14; ++slot) {
        const size_t at = contracts + size_t(slot) * 0x0c;
        std::fill_n(selected_regular.manager_records.begin() + std::ptrdiff_t(at), 12, 0);
        if (slot < 13) set_test_record_u32(selected_regular.manager_records, at, uint32_t(100000 + slot));
    }
    const auto selected_regular_view = advertising_offers(selected_regular, manager, 0);
    check(selected_regular_view.status == AdvertisingOfferStatus::available &&
          selected_regular_view.offers[1].can_accept,
          "a regular selected board can open the offer list when another board is free");
    auto selected_regular_request = request_for(selected_regular_view, 1);
    // Bind the expected data from this independent fixture, not from the parent session.
    std::copy_n(selected_regular.manager_records.begin() + std::ptrdiff_t(
                    contracts + size_t(selected_regular_request.contract_slot) * 0x0c),
                12, selected_regular_request.expected_contract.begin());
    const auto selected_regular_before = save_stream(selected_regular);
    selected_regular_request.expected_offer = selected_regular_view.offers[1].bytes;
    check(accept_advertising_offer(selected_regular, selected_regular_request).status ==
              AdvertisingOfferStatus::accepted,
          "acceptance targets a free row rather than overwriting the clicked occupied board");
    for (int slot = 0; slot < 13; ++slot) {
        bool found = false;
        for (int row = 0; row < 14; ++row)
            found |= record_u32(selected_regular.manager_records, contracts + size_t(row) * 0x0c) ==
                     uint32_t(100000 + slot);
        check(found, "accepted ad preserves every existing regular board contract");
    }
    check(record_u32(selected_regular.manager_records, contracts + 13 * 0x0c) !=
              record_u32(selected_regular_before, contracts + 13 * 0x0c),
          "accepted offer fills the remaining board without mutating the selected regular slot");

    GameState ambiguous = g;
    for (int slot = 0; slot < 14; ++slot) {
        const size_t at = contracts + size_t(slot) * 0x0c;
        set_test_record_u32(ambiguous.manager_records, at, uint32_t(100000 + slot));
        ambiguous.manager_records.at(at + 6) = 0;
    }
    set_test_record_u32(ambiguous.manager_records, contracts, 2'000'000);
    ambiguous.manager_records.at(contracts + 6) = 0x80;
    // A very large option key can sort ahead of ordinary active keys. The source
    // insertion writes at the tail, so guard every destination before committing.
    const auto ambiguous_view = advertising_offers(ambiguous, manager, 0);
    AdvertisingOfferRequest ambiguous_request;
    ambiguous_request.manager = manager;
    ambiguous_request.display_slot = ambiguous_view.display_slot;
    ambiguous_request.contract_slot = ambiguous_view.contract_slot;
    std::copy_n(ambiguous.manager_records.begin() + std::ptrdiff_t(
                    contracts + size_t(ambiguous_request.contract_slot) * 0x0c),
                12, ambiguous_request.expected_contract.begin());
    ambiguous_request.raw_slot = 1;
    std::copy_n(ambiguous.manager_records.begin() + std::ptrdiff_t(pool + 0x0c),
                12, ambiguous_request.expected_offer.begin());
    const auto ambiguous_before = save_stream(ambiguous);
    const auto ambiguous_result = accept_advertising_offer(ambiguous, ambiguous_request);
    check((ambiguous_result.status == AdvertisingOfferStatus::stale_offer ||
           ambiguous_result.status == AdvertisingOfferStatus::unavailable_target) &&
          save_stream(ambiguous) == ambiguous_before,
          "ambiguous option sort cannot overwrite a regular occupied advertising row");

    const auto roundtrip_dir = temp_path / "advertising-roundtrip";
    std::filesystem::create_directories(roundtrip_dir);
    const auto save_path = roundtrip_dir / "ADVERTISING.FBM";
    check(session.save_game(save_path, manager, true, error),
          (std::string("accepted advertising state saves: ") + error).c_str());
    app::CareerSession loaded(data, 0x74102513, save_path);
    check(save_stream(loaded.state()) == load_savegame(save_path).stream &&
          loaded.state().memory.u8(::bmh::layout::AdGroupId) == 42 &&
          record_u32(loaded.state().manager_records, rec + 0x8cc) == 0 &&
          record_u32(loaded.state().manager_records, rec + 0xa34 + 14 * 0x0c) == 12000 &&
          record_u32(loaded.state().manager_records, rec + 0xa34 + 15 * 0x0c) == 50000,
          "accepted board group, shirt, TV and cleared offers survive FBM save/load");
}

void test_advertising_spending(const std::filesystem::path& data,
                               const std::filesystem::path& temp_path) {
    app::CareerSession session(data, 0x1a2b3c4d);
    const auto choices = session.selection_context(1);
    check(!choices.club_order.empty(), "advertising spend fixture has a manager choice");
    SetupOptions options;
    ManagerChoice choice;
    choice.slot = 0;
    choice.name = "Werbungskosten";
    choice.master_club = choices.club_order.front();
    choice.selection_club = 0;
    choice.roster_revision = choices.revision;
    std::string error;
    check(session.confirm_manager(options, choice, error), "advertising spend manager confirms");
    CareerInitializationResult initialized;
    check(session.finish(initialized, error), "advertising spend career starts");
    GameState& g = session.state();
    const int manager = int(g.memory.u8(::bmh::layout::ActiveManager));
    const size_t rec = size_t(manager) * 0xb0c;
    auto request = [&](int slot, uint32_t amount, uint32_t expected, uint8_t weeks) {
        return AdvertisingSpendingRequest{manager, slot, amount, expected, weeks};
    };
    set_test_record_u32(g.manager_records, rec + 0x850, 5000);
    set_test_record_u32(g.manager_records, rec + 0x854, 7000);
    set_test_record_u32(g.manager_records, rec + 0x858, 9000);
    g.manager_records.at(rec + 0x287) = 0;
    set_test_record_u32(g.manager_records, rec + 0x314, 1000000);
    set_test_record_u32(g.manager_records, rec + 0x31c, 1000000);
    const auto goods_view = advertising_spending(g, manager, 16);
    const auto press_view = advertising_spending(g, manager, 17);
    const auto fan_view = advertising_spending(g, manager, 18);
    check(goods_view.available && goods_view.current_amount == 5000 &&
          goods_view.maximum_amount == 350000 && goods_view.delivery_weeks == 0 &&
          press_view.available && press_view.maximum_amount == 35000 && press_view.current_amount == 7000 &&
          fan_view.available && fan_view.current_amount == 9000 && fan_view.maximum_amount == 35000,
          "advertising projection reads the three saved fields and source limits");

    WatcomRandom rand(0x12344321);
    const uint32_t game_seed = session.game().seed();
    const uint64_t game_draws = session.game().calls();
    const Bytes before_projection = save_stream(g);
    const auto zero_goods = set_advertising_spending(g, request(16, 0, 5000, 0), rand);
    check(zero_goods.status == AdvertisingSpendingStatus::no_change &&
          save_stream(g) == before_projection && rand.calls() == 0 &&
          session.game().seed() == game_seed && session.game().calls() == game_draws,
          "zero merchandise performs no write and consumes no RNG");

    const uint32_t balance_before = record_u32(g.manager_records, rec + 0x314);
    const uint32_t expense_stats_before = record_u32(g.manager_records, rec + 0x8a0);
    const auto purchase = set_advertising_spending(g, request(16, 125000, 5000, 0), rand);
    const uint8_t delivery = g.manager_records.at(rec + 0x287);
    check(purchase.status == AdvertisingSpendingStatus::accepted && purchase.changed &&
          purchase.message_address == ::bmh::layout::DeskActions_Reject_ExecutableField && record_u32(g.manager_records, rec + 0x850) == 125000 &&
          delivery >= 6 && delivery <= 9 &&
          record_u32(g.manager_records, rec + 0x314) == balance_before - 125000 &&
          record_u32(g.manager_records, rec + 0x8a0) == expense_stats_before + 125000 &&
          rand.calls() == 1 && session.game().seed() == game_seed && session.game().calls() == game_draws,
          "successful merchandise charges once, stores delivery and draws one 6..9 week value");

    const Bytes pending_before = save_stream(g);
    auto pending = set_advertising_spending(g, request(16, 90000, 125000, delivery), rand);
    check(pending.status == AdvertisingSpendingStatus::delivery_pending && !pending.changed &&
          save_stream(g) == pending_before && rand.calls() == 1,
          "merchandise is inert while its saved delivery countdown is pending");
    const auto stale = set_advertising_spending(g, request(16, 90000, 0, 0), rand);
    check(stale.status == AdvertisingSpendingStatus::stale_request &&
          save_stream(g) == pending_before && rand.calls() == 1,
          "stale merchandise snapshot is rejected without writes or RNG");

    const uint32_t monthly_balance = record_u32(g.manager_records, rec + 0x314);
    const auto press = set_advertising_spending(g, request(17, 35000, 7000, 0), rand);
    const auto fan = set_advertising_spending(g, request(18, 0, 9000, 0), rand);
    check(press.status == AdvertisingSpendingStatus::accepted && fan.status == AdvertisingSpendingStatus::accepted &&
          record_u32(g.manager_records, rec + 0x854) == 35000 &&
          record_u32(g.manager_records, rec + 0x858) == 0 &&
          record_u32(g.manager_records, rec + 0x314) == monthly_balance && rand.calls() == 1,
          "monthly press and fan-work budgets only write their saved DWORDs");
    const Bytes monthly_before = save_stream(g);
    check(set_advertising_spending(g, request(17, 35001, 35000, 0), rand).status ==
              AdvertisingSpendingStatus::invalid_amount && save_stream(g) == monthly_before &&
          set_advertising_spending(g, request(17, 35000, 35000, 0), rand).status ==
              AdvertisingSpendingStatus::no_change && save_stream(g) == monthly_before && rand.calls() == 1,
          "monthly bounds and repeated values are inert");

    GameState legacy_value = g;
    set_test_record_u32(legacy_value.manager_records, rec + 0x854, 40000);
    const auto legacy_view = advertising_spending(legacy_value, manager, 17);
    WatcomRandom legacy_rand(0x12121212);
    const auto clamped = set_advertising_spending(legacy_value,
        request(17, 35000, 40000, 0), legacy_rand);
    check(legacy_view.available && legacy_view.current_amount == 40000 && clamped.changed &&
          clamped.status == AdvertisingSpendingStatus::accepted &&
          record_u32(legacy_value.manager_records, rec + 0x854) == 35000 && legacy_rand.calls() == 0,
          "legacy over-limit monthly value remains editable down to the Source cap");

    GameState insufficient = g;
    set_test_record_u32(insufficient.manager_records, rec + 0x850, 15000);
    insufficient.manager_records.at(rec + 0x287) = 0;
    set_test_record_u32(insufficient.manager_records, rec + 0x314, 0);
    set_test_record_u32(insufficient.manager_records, rec + 0x31c, 0);
    const Bytes insufficient_before = save_stream(insufficient);
    WatcomRandom no_funds_rand(0x76543210);
    const auto no_funds = set_advertising_spending(insufficient,
        request(16, 350000, 15000, 0), no_funds_rand);
    const Bytes insufficient_after = save_stream(insufficient);
    check(no_funds.status == AdvertisingSpendingStatus::insufficient_funds && no_funds.changed &&
          record_u32(insufficient.manager_records, rec + 0x850) == 0 && no_funds_rand.calls() == 0 &&
          no_funds.message_address == ::bmh::layout::Advertising_FormatError_TextPointer18 &&
          insufficient_before != insufficient_after &&
          record_u32(insufficient.manager_records, rec + 0x314) == 0,
          "failed merchandise payment preserves Original clear-to-zero side effect without RNG");

    GameState draw_failure = g;
    draw_failure.manager_records.at(rec + 0x287) = 0;
    set_test_record_u32(draw_failure.manager_records, rec + 0x850, 0);
    const Bytes draw_failure_before = save_stream(draw_failure);
    WatcomRandom draw_failure_rand(0x01020304);
    const uint32_t draw_failure_seed = draw_failure_rand.seed();
    const auto old_draw_hook = draw_hook;
    draw_hook = throw_ad_spending_draw;
    bool threw = false;
    try { (void)set_advertising_spending(draw_failure, request(16, 1000, 0, 0), draw_failure_rand); }
    catch (const std::runtime_error&) { threw = true; }
    draw_hook = old_draw_hook;
    check(threw && save_stream(draw_failure) == draw_failure_before &&
          draw_failure_rand.seed() == draw_failure_seed && draw_failure_rand.calls() == 0,
          "failed delivery draw cannot leave a half-charged merchandise purchase");

    GameState level_zero = g;
    level_zero.level = 0;
    level_zero.manager_records.at(rec + 0x287) = 0;
    set_test_record_u32(level_zero.manager_records, rec + 0x850, 0);
    const uint32_t level_zero_balance = record_u32(level_zero.manager_records, rec + 0x314);
    WatcomRandom level_zero_rand(0xabcdef01);
    const auto level_zero_result = set_advertising_spending(level_zero,
        request(16, 1000, 0, 0), level_zero_rand);
    check(level_zero_result.status == AdvertisingSpendingStatus::accepted &&
          record_u32(level_zero.manager_records, rec + 0x850) == 1000 &&
          record_u32(level_zero.manager_records, rec + 0x314) == level_zero_balance &&
          level_zero_rand.calls() == 1,
          "level zero follows the source no-charge book_money success then stores/draws delivery");

    const auto roundtrip_dir = temp_path / "advertising-spending-roundtrip";
    std::filesystem::create_directories(roundtrip_dir);
    const auto path = roundtrip_dir / "SPENDING.FBM";
    check(session.save_game(path, manager, true, error), "advertising spending state saves");
    app::CareerSession loaded(data, 0x8899aabb, path);
    const size_t loaded_rec = size_t(manager) * 0xb0c;
    check(record_u32(loaded.state().manager_records, loaded_rec + 0x850) == 125000 &&
          loaded.state().manager_records.at(loaded_rec + 0x287) == delivery &&
          record_u32(loaded.state().manager_records, loaded_rec + 0x854) == 35000 &&
          record_u32(loaded.state().manager_records, loaded_rec + 0x858) == 0 &&
          loaded.rand().calls() == 0,
          "goods, delivery, and monthly budget values survive native FBM save/load");
}

void test_bank_credit(const std::filesystem::path& data, const std::filesystem::path& temp_path) {
    app::CareerSession session(data, 0x1a2b3c4d);
    const auto choices = session.selection_context(1);
    SetupOptions options;
    ManagerChoice choice;
    choice.slot = 0; choice.name = "Bankkredit";
    choice.master_club = choices.club_order.front(); choice.selection_club = 0;
    choice.roster_revision = choices.revision;
    std::string error;
    check(session.confirm_manager(options, choice, error), "credit manager confirms");
    CareerInitializationResult initialized;
    check(session.finish(initialized, error), "credit career starts");
    auto& g = session.state(); const int manager = int(g.memory.u8(::bmh::layout::ActiveManager));
    const size_t rec = size_t(manager) * 0xb0c;
    const uint32_t loan = ::bmh::layout::Loans + uint32_t(manager) * 0x78;
    g.level = 1;
    set_test_record_u32(g.manager_records, rec + 0x314, 2000000);
    set_test_record_u32(g.manager_records, rec + 0x31c, 100000);
    for (uint32_t i = 0; i < 6; ++i) g.memory.set32(loan + i * 4, 0);
    const auto exe = bmh::test::load_executable(asset_path(data, "BMH.exe"));
    const auto credit_projection = bank_from_game(g,exe,initialized.desktop.revision);
    check(credit_projection.credit_labels[0]==bmh::test::expected_text("bank_app_tests-external-1").c_str()
          && credit_projection.credit_value_text[3].find("Monat")!=std::string::npos
          && credit_projection.credit_value_text[7].find("EU")!=std::string::npos,
          "credit labels and units come from loaded TEXTD, not uninitialized EXE pointers");
    const auto context = bank_credit_context(g, exe, manager);
    check(context.available && context.rate_table[0] == 101 && context.divisor == 100.0,
          "credit context projects native EXE constants and the empty bank slot");
    struct Golden { uint32_t p, months, monthly; };
    const Golden goldens[] = {{123456,1,124690},{123456,3,42382},{123456,12,11517},
        {123456,24,5973},{123456,48,3239},{1000,48,26},{100000,48,2624},
        {100001,48,2624},{500000,48,13121}};
    const auto initial = save_stream(g); const auto wr = session.rand(); const auto gr = session.game();
    for (const auto& golden : goldens) {
        auto d = bank_credit_draft(context);
        check(edit_bank_credit(context,d,0,golden.p) && edit_bank_credit(context,d,3,golden.months)
              && d.values[2] == golden.monthly && d.values[1] == golden.monthly*golden.months,
              "credit rate uses Source integer steps and x87 truncation including midpoint");
    }
    auto draft = bank_credit_draft(context);
    check(!edit_bank_credit(context,draft,2,1000) && !edit_bank_credit(context,draft,6,1)
          && !edit_bank_credit(context,draft,0,999) && draft.edits.empty(),
          "bank rejects manager-only rate, repayment of empty slot and too-small loan");
    check(edit_bank_credit(context,draft,0,100000) && draft.values[3] == 48 && draft.values[2] == 2624
          && draft.values[7] == 2100000 && save_stream(g) == initial,
          "new credit draft is read-only and defaults to the Source 48-month term");
    uint64_t revision = initialized.desktop.revision;
    BankRequest request;
    request.kind=BankRequestKind::bank_credit; request.manager_slot=manager;
    request.runtime_club=g.manager_teams.at(size_t(manager)); request.revision=revision;
    request.expected_credit=context.saved; request.credit_edits=draft.edits;
    check(session.dispatch_bank(request,revision,error), "new credit commits through career dispatch");
    check(record_u32(g.manager_records,rec+0x314)==2100000 && g.memory.u32(loan)==100000
          && g.memory.u32(loan+4)==125952 && g.memory.u32(loan+8)==2624 && g.memory.u32(loan+12)==48,
          "new bank credit stores the Source six-word installment record");
    GameState baseline = g;
    set_test_record_u32(baseline.manager_records,rec+0x314,2000000);
    for(uint32_t i=0;i<6;++i) baseline.memory.set32(loan+i*4,0);
    check(save_stream(baseline)==initial && session.rand().seed()==wr.seed() && session.rand().calls()==wr.calls()
          && session.game().seed()==gr.seed() && session.game().calls()==gr.calls(),
          "credit changes only cash and loan record, preserves statistics/investments and both RNGs");
    const auto unchanged_context = bank_credit_context(g,exe,manager);
    check(commit_bank_credit(g,exe,manager,unchanged_context.saved,{{0,100000}},false).status==BankCreditStatus::no_change,
          "unchanged edited credit closes without a fabricated changed-projection ACK");
    const auto committed=save_stream(g); const auto rev=revision;
    check(!session.dispatch_bank(request,revision,error) && revision==rev && save_stream(g)==committed,
          "replayed credit request cannot borrow twice");
    auto current=bank_credit_context(g,exe,manager); auto partial=bank_credit_draft(current);
    check(edit_bank_credit(current,partial,6,20000) && partial.fees==5200
          && partial.values[2]==2099 && partial.values[1]==100752,
          "partial repayment includes format field fee and one-month interest on repayment");
    check(commit_bank_credit(g,exe,manager,current.saved,partial.edits,false).status==BankCreditStatus::confirmation_required
          && save_stream(g)==committed, "declined repayment fees leave the complete save state untouched");
    auto stale=current.saved; ++stale[0];
    check(commit_bank_credit(g,exe,manager,stale,partial.edits,true).status==BankCreditStatus::stale_request
          && save_stream(g)==committed, "changed native loan record rejects stale credit edits");
    check(commit_bank_credit(g,exe,manager,current.saved,{{3,0}},true).status==BankCreditStatus::invalid_request
          && save_stream(g)==committed, "zero-month request cannot enter the native monthly loan loop");
    check(commit_bank_credit(g,exe,manager,current.saved,partial.edits,true).status==BankCreditStatus::accepted
          && record_u32(g.manager_records,rec+0x314)==2074800 && g.memory.u32(loan)==80000,
          "accepted partial repayment books principal plus fees once");
    const auto save_dir=temp_path/"credit-roundtrip";
    std::filesystem::create_directories(save_dir);
    const auto save=save_dir/"CREDIT.FBM";
    check(session.save_game(save,manager,true,error), "credit saves as native FBM");
    app::CareerSession loaded(data,0x99887766,save);
    check(loaded.state().memory.u32(loan)==80000 && loaded.state().memory.u32(loan+8)==2099
          && save_stream(loaded.state())==load_savegame(save).stream,
          "native credit and balance survive FBM reload without reconstructed values");
    GameState monthly = loaded.state(), no_credit = monthly;
    for(uint32_t i=0;i<6;++i) no_credit.memory.set32(loan+i*4,0);
    auto month_random=session.rand(), baseline_random=month_random;
    auto month_game=session.game(), baseline_game=month_game;
    MinuteReport monthly_report, baseline_report;
    month_change(monthly,month_random,monthly_report,&month_game);
    month_change(no_credit,baseline_random,baseline_report,&baseline_game);
    check(!monthly_report.unsupported && !baseline_report.unsupported
          && monthly.memory.u32(loan+16)==1 && monthly.memory.u32(loan+20)==2099
          && record_u32(monthly.manager_records,rec+0x314)==record_u32(no_credit.manager_records,rec+0x314)-2099
          && record_u32(monthly.manager_records,rec+0x89c)==record_u32(no_credit.manager_records,rec+0x89c)+2099
          && month_random.seed()==baseline_random.seed() && month_random.calls()==baseline_random.calls()
          && month_game.seed()==baseline_game.seed() && month_game.calls()==baseline_game.calls(),
          "saved credit enters the existing monthly installment loop once with unchanged RNG consumption");
    current=bank_credit_context(g,exe,manager); auto full=bank_credit_draft(current);
    check(edit_bank_credit(current,full,6,80000) && full.fees==5800 && full.values[1]==0,
          "full repayment produces zero total and Source closing fees");
    check(commit_bank_credit(g,exe,manager,current.saved,full.edits,true).status==BankCreditStatus::accepted
          && record_u32(g.manager_records,rec+0x314)==1989000,
          "full repayment books its remaining principal and fee");
    for(uint32_t i=0;i<6;++i) check(g.memory.u32(loan+i*4)==0,"full repayment clears all six native loan words");
    const uint32_t old[] = {100000,125952,2624,48,12,31488};
    for(uint32_t i=0;i<6;++i) g.memory.set32(loan+i*4,old[i]);
    set_test_record_u32(g.manager_records,rec+0x314,2000000);
    current=bank_credit_context(g,exe,manager); auto elapsed=bank_credit_draft(current);
    check(elapsed.initial_display && elapsed.values[5]==0 && current.saved[5]==31488,
          "Source shows saved payments initially then clears the local working payments");
    check(edit_bank_credit(current,elapsed,6,20000) && elapsed.refund==24996
          && elapsed.interest_adjustment==187 && elapsed.fees==5200 && elapsed.values[7]==1999609,
          "elapsed repayment uses Source principal refund and remaining-interest adjustment");
    request.revision=revision; request.expected_credit=current.saved; request.credit_edits=elapsed.edits;
    request.accept_credit_fees=true;
    check(session.dispatch_bank(request,revision,error) && g.memory.u32(loan+16)==0 && g.memory.u32(loan+20)==0
          && record_u32(g.manager_records,rec+0x314)==1999609,
          "elapsed repayment resets elapsed and paid words after booking the actual refund");
    const auto notice=session.preparation_projection(revision).bank->credit_notice;
    check(notice && notice->refund==24996 && notice->text.find("24.996")!=std::string::npos,
          "credit dispatch returns the original already-booked principal-refund notice");
}

void leave_six_fit_starters(GameState& g, int team) {
    const SquadRange range = squad_range(team);
    int starters = 0;
    for (size_t i = range.first; i < range.first + range.count; i++) {
        uint16_t& slot = g.squad_slots.at(i);
        if ((slot & 0x0fff) == 0x0fff || !(slot & 0x8000)) continue;
        if (starters++ >= 6) slot &= 0x7fff;
    }
}

void test_loaded_manager_forfeit(const std::filesystem::path& data,
                                 const std::filesystem::path& temp_path) {
    StartupFlow flow(data, temp_path / "forfeit-startup");
    flow.click(150, 215); flow.click(5, 225, true); flow.click(5, 225, true); flow.click(90, 169);
    check(flow.pending_career() != nullptr, "forfeit test new-career flow yields manager setup");
    app::CareerSession source(data, 0x74102513);
    const SetupOptions options = [&] { auto o = flow.pending_career()->options; o.managers = 2; return o; }();
    ManagerChoice first = flow.pending_career()->managers.at(0); first.slot = 0;
    auto c1 = source.selection_context(1);
    auto p1 = std::find(c1.club_order.begin(), c1.club_order.end(), first.master_club);
    check(p1 != c1.club_order.end(), "first manager club exists in selection context");
    first.selection_club = int(p1 - c1.club_order.begin()); first.roster_revision = c1.revision;
    std::string error;
    check(source.confirm_manager(options, first, error), "first manager confirms for forfeit test");
    auto c2 = source.selection_context(2);
    auto p2 = std::find_if(c2.club_order.begin(), c2.club_order.end(),
                           [&](int club) { return club != first.master_club; });
    check(p2 != c2.club_order.end(), "second manager has a distinct club");
    ManagerChoice second = first; second.slot = 1; second.master_club = *p2;
    second.selection_club = int(p2 - c2.club_order.begin()); second.roster_revision = c2.revision;
    second.name += " B";
    check(source.confirm_manager(options, second, error), "second manager confirms for forfeit test");
    CareerInitializationResult init;
    check(source.finish(init, error), "two-manager forfeit test starts a career");

    // Without the World Cup mode legacy routine removes the international days from the
    // calendar: the first eight desk dispatches of a fresh career have no national team
    // fixture (it used to reach an unmanned national team there and award it 0:2).
    SetupOptions early_options = options;
    early_options.world = false;
    app::CareerSession early(data, 0x74102513);
    auto early_c1 = early.selection_context(1);
    ManagerChoice early_first = first;
    auto early_p1 = std::find(early_c1.club_order.begin(), early_c1.club_order.end(), early_first.master_club);
    check(early_p1 != early_c1.club_order.end(), "early forfeit manager is selectable");
    early_first.selection_club = int(early_p1 - early_c1.club_order.begin());
    early_first.roster_revision = early_c1.revision;
    check(early.confirm_manager(early_options, early_first, error), "early forfeit first manager confirms");
    auto early_c2 = early.selection_context(2);
    ManagerChoice early_second = second;
    auto early_p2 = std::find(early_c2.club_order.begin(), early_c2.club_order.end(), early_second.master_club);
    check(early_p2 != early_c2.club_order.end(), "early forfeit second manager is selectable");
    early_second.selection_club = int(early_p2 - early_c2.club_order.begin());
    early_second.roster_revision = early_c2.revision;
    check(early.confirm_manager(early_options, early_second, error), "early forfeit second manager confirms");
    CareerInitializationResult early_init;
    check(early.finish(early_init, error), "early forfeit career finishes setup");
    uint64_t early_revision = early_init.desktop.revision;
    bool special_award_found = false;
    for (int i = 0; i < 8; i++) {
        const int slot = early.state().memory.u8(::bmh::layout::ActiveManager);
        const auto result = early.dispatch_kickoff(
            DeskRequest{DeskRequestKind::advance_turn, slot, -1, -1, -1, early_revision},
            early_revision, error);
        check(result.kind != TurnDeskKind::Failed, "first eight new-career desk dispatches complete");
        for (const MatchGroup& grp : early.last_turn_results().groups)
            for (int m = 0; m < grp.matches(); m++)
                if (grp.fixtures.at(size_t(m) * 2) == kNoTeam || grp.fixtures.at(size_t(m) * 2 + 1) == kNoTeam)
                    special_award_found = true;
    }
    check(!special_award_found, "no national team fixture without the World Cup mode");

    const int managed_team = source.state().manager_teams.at(0);

    uint64_t revision = init.desktop.revision;
    int opponent = -1; bool home_side = false;
    for (int i = 0; i < 100; i++) {
        if (prepared_league_fixture(source.state(), managed_team, opponent, home_side)) break;
        const int active = source.state().memory.u8(::bmh::layout::ActiveManager);
        const auto result = source.dispatch_kickoff(
            DeskRequest{DeskRequestKind::advance_turn, active, -1, -1, -1, revision}, revision, error);
        check(result.kind != TurnDeskKind::Failed, "career advances until manager league fixture");
    }
    check(opponent >= 0, "a prepared league fixture for the manager is found");
    leave_six_fit_starters(source.state(), managed_team);
    check(team_average(source.state(), managed_team, 0x1f1).players < 8,
          "saved manager fixture has fewer than eight active fit players");

    const int manager_index = source.state().manager_of(managed_team);
    check(manager_index >= 0, "manager club maps to its manager record");
    const uint32_t manager_rec = uint32_t(manager_index) * 0xb0c;
    const int active = source.state().memory.u8(::bmh::layout::ActiveManager);
    const auto save_path = temp_path / "LOADED-MANAGER-FORFEIT.FBM";
    check(source.save_game(save_path, active, true, error), "unfit-manager desk save is written");

    app::CareerSession loaded(data, 0x74102513, save_path);
    check(save_stream(loaded.state()) == load_savegame(save_path).stream,
          "loaded manager-forfeit save remains byte-identical");
    check(loaded.game().calls() == 0 && loaded.rand().calls() == 0,
          "loaded manager-forfeit save binds without either RNG stream");
    const uint64_t game_calls_before = loaded.game().calls();
    const uint64_t rand_calls_before = loaded.rand().calls();
    // Inspect the immediate forfeit reset before later F6 background events/training.
    loaded.state().memory.set8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField,1);
    uint64_t loaded_revision = 1;
    bool forfeit_found = false;
    GameState before_forfeit;
    uint32_t cash_before = 0, expense_before = 0;
    int manager_player = -1, opponent_player = -1;
    for (int i = 0; i < 100 && !forfeit_found; i++) {
        leave_six_fit_starters(loaded.state(), managed_team);
        if (prepared_league_fixture(loaded.state(), managed_team, opponent, home_side))
            check(team_average(loaded.state(), managed_team, 0x1f1).players < 8,
                  "loaded prepared manager fixture has fewer than eight fit starters");
        before_forfeit = loaded.state();
        cash_before = record_u32(before_forfeit.manager_records, manager_rec + 0x314);
        expense_before = record_u32(before_forfeit.manager_records, manager_rec + 0x8a0);
        manager_player = first_source_reset_player(before_forfeit, managed_team);
        opponent_player = first_source_reset_player(before_forfeit, opponent);
        check(manager_player >= 0 && opponent_player >= 0,
              "both fixture squads have resettable source players");
        const auto advanced = loaded.dispatch_kickoff(
            DeskRequest{DeskRequestKind::advance_turn, loaded.state().memory.u8(::bmh::layout::ActiveManager), -1, -1, -1,
                        loaded_revision}, loaded_revision, error);
        check(advanced.kind != TurnDeskKind::Failed, "loaded manager-forfeit desk advances");
        for (const MatchGroup& grp : loaded.last_turn_results().groups)
            for (int m = 0; m < grp.matches(); m++)
                if (grp.fixtures.at(size_t(m) * 2) == managed_team ||
                    grp.fixtures.at(size_t(m) * 2 + 1) == managed_team) forfeit_found = true;
    }
    check(forfeit_found, "loaded manager fixture reaches its matchday and is awarded");
    const uint32_t cash_after = record_u32(loaded.state().manager_records, manager_rec + 0x314);
    const uint32_t expense_after = record_u32(loaded.state().manager_records, manager_rec + 0x8a0);
    check(cash_after < cash_before && expense_after == expense_before + 200000,
          "loaded manager forfeiture debits cash and records exactly 200000 expense");
    check(loaded.game().calls() > game_calls_before && loaded.rand().calls() > rand_calls_before,
          "forfeit continuation keeps both event RNG streams valid");
    check_forfeit_reset(before_forfeit, loaded.state(), manager_player);
    check_forfeit_reset(before_forfeit, loaded.state(), opponent_player);
    bool score_found = false;
    MatchGroup manager_group;
    int manager_match = -1;
    for (const MatchGroup& grp : loaded.last_turn_results().groups) {
        for (int m = 0; m < grp.matches(); m++) {
            if (grp.fixtures.at(size_t(m) * 2) != managed_team &&
                grp.fixtures.at(size_t(m) * 2 + 1) != managed_team) continue;
            int home_goals = -1, away_goals = -1;
            check(match_score(grp, m, home_goals, away_goals), "awarded manager score is readable");
            check((grp.kickoff_void_mask >> m & 1) != 0, "awarded manager fixture is marked void");
            check(home_side ? (home_goals == 0 && away_goals == 2)
                            : (home_goals == 2 && away_goals == 0),
                  "loaded manager fixture stores the source 0:2 or 2:0 award");
            score_found = true;
            manager_group = grp;
            manager_match = m;
        }
    }
    check(score_found, "last matchday snapshot retains the awarded manager result");

    MatchGroup forfeit_only = manager_group;
    const size_t fixture_at = size_t(manager_match) * 2;
    forfeit_only.fixtures = {manager_group.fixtures.at(fixture_at), manager_group.fixtures.at(fixture_at + 1)};
    if (!forfeit_only.results.empty()) forfeit_only.results = {manager_group.results.at(size_t(manager_match))};
    if (!forfeit_only.cup_results.empty())
        forfeit_only.cup_results = {manager_group.cup_results.at(size_t(manager_match))};
    forfeit_only.kickoff_void_mask = 1;
    GameState skipped_state = loaded.state();
    GameRandom skipped_game = loaded.game();
    WatcomRandom skipped_rand = loaded.rand();
    const Bytes skipped_bytes = save_stream(skipped_state);
    const uint32_t skipped_game_seed = uint32_t(skipped_game.seed());
    const uint64_t skipped_game_calls = skipped_game.calls();
    const uint32_t skipped_rand_seed = skipped_rand.seed();
    const uint64_t skipped_rand_calls = skipped_rand.calls();
    MinuteReport skipped_report;
    process_group(skipped_state, forfeit_only, skipped_game, skipped_rand, skipped_report);
    check(!skipped_report.unsupported && save_stream(skipped_state) == skipped_bytes &&
          uint32_t(skipped_game.seed()) == skipped_game_seed && skipped_game.calls() == skipped_game_calls &&
          uint32_t(skipped_rand.seed()) == skipped_rand_seed && skipped_rand.calls() == skipped_rand_calls,
          "kickoff-voided fixture has no morale, manager, development, or RNG effects");

    GameState played_state = loaded.state();
    GameRandom played_game = loaded.game();
    WatcomRandom played_rand = loaded.rand();
    const uint64_t played_game_calls = played_game.calls();
    const uint64_t played_rand_calls = played_rand.calls();
    MinuteReport played_report;
    process_group(played_state, manager_group, played_game, played_rand, played_report);
    check(!played_report.unsupported && played_game.calls() > played_game_calls &&
          played_rand.calls() > played_rand_calls &&
          played_state.team_morale != loaded.state().team_morale,
          "normal matches still process morale and player development despite ended descriptor bits");
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("bank_app_tests requires original data directory");
        const std::filesystem::path data = argv[1];
        test_toto_source_calculator(data);
        test_toto_deadline_native_save(data);
        TemporaryDirectory temp;
        { std::FILE* ignored = std::fopen((temp.path / "IGNORE.TXT").string().c_str(), "wb");
          if (!ignored) throw std::runtime_error("cannot create non-save filter fixture");
          std::fclose(ignored); }
        test_source_empty_issue3_cup_day(data, temp.path);
        StartupFlow flow(data, temp.path);
        flow.click(150, 215); flow.click(5, 225, true); flow.click(5, 225, true);
        const auto accepted = flow.click(90, 169);
        check(accepted.request == FlowRequest::new_career && flow.pending_career(), "manager accepted");
        app::CareerSession session(data, 0x22aa7711);
        std::string error;
        const ManagerChoice first_manager_choice = flow.pending_career()->managers[0];
        test_season_profit_tax(data, flow.options(), first_manager_choice);
        check(session.confirm_manager(flow.pending_career()->options, first_manager_choice, error), "manager initialized");
        CareerInitializationResult result;
        check(session.finish(result, error), "career reaches desktop");
        test_friendly_booking(session.state(), 0);
        test_friendly_season_end_dates(session.state(), 0);
        test_advertising_acceptance(data, temp.path);
        test_advertising_renewal(data);
        test_advertising_spending(data, temp.path);
        test_bank_credit(data, temp.path);
        uint64_t revision = result.desktop.revision;
        {
            app::BankActions common(session);
            const auto native=save_stream(session.state());const auto g=session.game();const auto w=session.rand();
            const auto rev=revision;const auto checkpoint=session.bank_checkpoint();
            auto observed=*session.preparation_projection(revision).bank;
            ++observed.revision;
            check(!common.dispatch(app::BankActions::savings(observed,1),revision,error),
                  "shared bank boundary rejects a stale observed account");
            bool acknowledged=false;
            try {
                --observed.revision;
                common.dispatch(app::BankActions::savings(observed,1),revision,error,[&] {
                    acknowledged=true;session.state().date++;
                    session.game().range(0,10);session.rand().rand();
                    throw std::runtime_error("bank frontend ACK refused");
                });
            } catch(const std::runtime_error&) {}
            check(acknowledged && save_stream(session.state())==native && revision==rev &&
                session.game().seed()==g.seed() && session.game().calls()==g.calls() &&
                session.rand().seed()==w.seed() && session.rand().calls()==w.calls() &&
                session.bank_checkpoint().last_tip==checkpoint.last_tip &&
                session.bank_checkpoint().property_notice==checkpoint.property_notice &&
                session.bank_checkpoint().credit_notice==checkpoint.credit_notice,
                "shared bank rejection restores Native, both RNGs, revision and bank notices after a real deposit");
        }
        auto preparation = std::move(result.preparation);
        flow.attach_initialized_career(std::move(result)); flow.attach_preparation_context(std::move(preparation));
        {
            const auto cursor_saves = temp.path / "cursor-checks";
            std::filesystem::create_directories(cursor_saves);
            const auto initial_save = cursor_saves / "INITIAL.FBM";
            const int initial_manager = int(session.state().memory.u8(::bmh::layout::ActiveManager));
            check(session.save_game(initial_save, initial_manager, true, error),
                  "new career desk can be saved for continuation test");
            app::CareerSession resumed(data, 0x22aa7712, initial_save);
            uint64_t resumed_revision = 1;
            const int resumed_manager = int(resumed.state().memory.u8(::bmh::layout::ActiveManager));
            const uint32_t saved_date = resumed.state().date;
            const auto resumed_turn = resumed.dispatch_kickoff(
                DeskRequest{DeskRequestKind::advance_turn, resumed_manager, -1, -1, -1, resumed_revision},
                resumed_revision, error);
            check(resumed_turn.kind != TurnDeskKind::Failed && resumed_turn.date >= saved_date,
                  (std::string("loaded single-manager save resumes its active turn: ") + error).c_str());
        }
        {
            app::CareerSession multi_resume(data, 0x22aa7715);
            SetupOptions multi_options = flow.options();
            multi_options.managers = 2;
            const ManagerChoice first = first_manager_choice;
            check(multi_resume.confirm_manager(multi_options, first, error), "resume fixture first manager initialized");
            const auto choices = multi_resume.selection_context(2);
            const auto second_master = std::find_if(choices.club_order.begin(), choices.club_order.end(),
                [&](int master) { return master != first.master_club; });
            check(second_master != choices.club_order.end(), "resume fixture has a distinct second club");
            ManagerChoice second = first;
            second.slot = 1; second.master_club = *second_master; second.name += " B";
            check(multi_resume.confirm_manager(multi_options, second, error), "resume fixture second manager initialized");
            CareerInitializationResult multi_result;
            check(multi_resume.finish(multi_result, error), "resume fixture reaches the first desk");
            const auto multi_save = temp.path / "cursor-checks" / "MULTI.FBM";
            const int saved_slot = int(multi_resume.state().memory.u8(::bmh::layout::ActiveManager));
            check(multi_resume.save_game(multi_save, saved_slot, true, error), "multi-manager first desk saves");
            app::CareerSession loaded_multi(data, 0x22aa7716, multi_save);
            uint64_t loaded_revision = 1;
            const int active_slot = int(loaded_multi.state().memory.u8(::bmh::layout::ActiveManager));
            const uint32_t saved_date = loaded_multi.state().date;
            const auto handoff = loaded_multi.dispatch_kickoff(
                DeskRequest{DeskRequestKind::advance_turn, active_slot, -1, -1, -1, loaded_revision},
                loaded_revision, error);
            check(handoff.kind == TurnDeskKind::NextDesk && handoff.date == saved_date &&
                  handoff.manager_slot != active_slot,
                  (std::string("loaded shuffled multi-manager cursor preserves next slot: ") + error).c_str());

            const GameState& multi_source = multi_resume.state();
            const int other_human_team = multi_source.manager_teams.at(1);
            const FriendlyBookingQuote home_quote = quote_friendly_match(multi_source, 0, other_human_team, true);
            uint16_t home_day = 0;
            for (int candidate = int(multi_source.memory.u16(::bmh::layout::DaysIntoSeason)) + 1; candidate < 0x16e; ++candidate) {
                const auto status = friendly_match_date_status(multi_source, home_quote, uint16_t(candidate));
                if (status == FriendlyDateStatus::available ||
                    status == FriendlyDateStatus::available_with_calendar_event) {
                    home_day = uint16_t(candidate); break;
                }
            }
            check(home_day != 0 && home_quote.opponent_manager == 1 && home_quote.payer_manager == 0,
                  "home booking records the other human opponent separately from its payer");
            FriendlyBookingRequest home_request;
            home_request.quote = home_quote; home_request.season_day = home_day;
            home_request.accept_cost = true; home_request.approving_manager = 0;
            GameState home_unconfirmed = multi_source;
            const Bytes home_unconfirmed_bytes = save_stream(home_unconfirmed);
            check(book_friendly_match(home_unconfirmed, home_request).status == FriendlyBookingStatus::opponent_declined &&
                  save_stream(home_unconfirmed) == home_unconfirmed_bytes,
                  "own-home price acceptance cannot skip the opposing human manager's consent");
            home_request.accept_opponent = true; home_request.consenting_manager = 1;
            GameState home_confirmed = multi_source;
            check(book_friendly_match(home_confirmed, home_request).status == FriendlyBookingStatus::booked,
                  "home booking proceeds after the distinct opponent manager consents");

            GameState friendly_state = multi_resume.state();
            const int host_team = friendly_state.manager_teams.at(1);
            uint16_t friendly_day = 0;
            const FriendlyBookingQuote away_quote = quote_friendly_match(friendly_state, 0, host_team, false);
            const int current_day = friendly_state.memory.u16(::bmh::layout::DaysIntoSeason);
            for (int candidate = current_day + 1; candidate < 0x16e; ++candidate) {
                const FriendlyDateStatus status = friendly_match_date_status(friendly_state, away_quote,
                                                                               uint16_t(candidate));
                if (status == FriendlyDateStatus::available ||
                    status == FriendlyDateStatus::available_with_calendar_event) {
                    friendly_day = uint16_t(candidate); break;
                }
            }
            check(friendly_day != 0 && away_quote.payer_manager == 1,
                  "away friendly requires the opposing human home manager's approval");
            FriendlyBookingRequest away_request;
            away_request.quote = away_quote; away_request.season_day = friendly_day;
            away_request.accept_opponent = true; away_request.consenting_manager = 1;
            away_request.accept_cost = true; away_request.approving_manager = 1;
            const size_t host_record = 0xb0c;
            const int32_t host_cash = int32_t(record_u32(friendly_state.manager_records, host_record + 0x314));
            const uint32_t host_expenses = record_u32(friendly_state.manager_records, host_record + 0x8a0);
            const auto away_booked = book_friendly_match(friendly_state, away_request);
            check(away_booked.status == FriendlyBookingStatus::booked &&
                  away_booked.home_team == host_team && away_booked.away_team == multi_resume.state().manager_teams.at(0) &&
                  (multi_resume.state().level == 0 ||
                   (int32_t(record_u32(friendly_state.manager_records, host_record + 0x314)) == host_cash - away_quote.cost &&
                    record_u32(friendly_state.manager_records, host_record + 0x8a0) == host_expenses + uint32_t(away_quote.cost))),
                  "away booking charges only the consenting opposing home manager");
        }
        // Engine-side transfer actions use the same initialized CareerSession state as
        // the desk. Keep this simulator-only mutable view out of the read-only app API.
        GameState& transfer_state = const_cast<GameState&>(session.state());
        WatcomRandom& transfer_rand = session.rand();
        MinuteReport transfer_report;
        const SquadRange own_squad = squad_range(transfer_state.manager_teams.at(0));
        bool listed = false;
        int own_list_entry = -1;
        int own_list_player = -1;
        for (int row = int(own_squad.count) - 1; row >= 11; --row) {
            if ((transfer_state.squad_slots.at(own_squad.first + size_t(row)) & 0xfff) == 0xfff) continue;
            const auto listing = list_manager_player(transfer_state, 0, row, transfer_rand, transfer_report);
            listed = listing.status == TransferActionStatus::accepted && listing.list_entry >= 0;
            own_list_entry = listing.list_entry;
            own_list_player = listing.player;
            break;
        }
        check(listed && !transfer_report.unsupported, "human manager can list a reserve player");
        new_transfer_list_entries(transfer_state, transfer_rand, transfer_report);
        check(!transfer_report.unsupported, "weekly transfer pool contains sellable AI players");
        const GameState incoming_offer_base = transfer_state;
        const WatcomRandom incoming_offer_rand = transfer_rand;
        const int32_t transfer_cash = int32_t(uint32_t(transfer_state.manager_records.at(0x314)) |
            uint32_t(transfer_state.manager_records.at(0x315)) << 8 | uint32_t(transfer_state.manager_records.at(0x316)) << 16 |
            uint32_t(transfer_state.manager_records.at(0x317)) << 24);
        int buy_entry = -1;
        for (int entry = 0; entry < 40; ++entry) {
            const uint32_t at = ::bmh::layout::EditorListUnsigned + uint32_t(entry) * 0x58;
            const int player = transfer_state.squad_slots.at(0xb70u + size_t(entry)) & 0xfff;
            if (player == 0xfff || int16_t(transfer_state.memory.u16(at)) != -1 ||
                (transfer_state.memory.u16(at + 8) & 0x7fff) == uint16_t(transfer_state.manager_teams.at(0))) continue;
            const uint32_t fee = transfer_state.memory.u32(at + 0x2c);
            const uint32_t salary = transfer_state.memory.u32(at + 0x30);
            if (fee == 0 || salary == 0 || fee > uint32_t(transfer_cash / 2)) continue;
            buy_entry = entry;
            GameState direct_state = transfer_state;
            WatcomRandom direct_rand = transfer_rand;
            TransferBidRequest direct;
            // Source mode III rolls salary demand through 120..190 and fee
            // acceptance through 92..150; make this direct-path fixture clear
            // both gates so it tests settlement, not a probabilistic pending bid.
            direct.manager = 0; direct.list_entry = entry; direct.player = player; direct.fee = int32_t(uint64_t(fee) * 300 / 100);
            direct.annual_salary = int32_t(uint64_t(salary) * 300 / 100); direct.contract_years = 2;
            direct.source_slot = int(0xb70u + size_t(entry));
            GameState stale_state = transfer_state;
            WatcomRandom stale_rand = transfer_rand;
            const uint64_t stale_draws = stale_rand.calls();
            TransferBidRequest stale = direct;
            stale.player = player == 0 ? 1 : 0;
            const auto stale_result = submit_transfer_bid(stale_state, stale, stale_rand);
            check(stale_result.status == TransferActionStatus::invalid_request &&
                  stale_state.squad_slots == transfer_state.squad_slots &&
                  stale_state.manager_records == transfer_state.manager_records &&
                  stale_state.memory.u16(::bmh::layout::EditorListUnsigned + uint32_t(entry) * 0x58) == 0xffff &&
                  stale_rand.calls() == stale_draws, "recycled list entry rejects stale player identity without state or RNG changes");
            HumanBuyerSaleHooks direct_hooks;
            direct_hooks.allocator_gate = HumanBuyerSaleAllocatorGate::zero;
            direct_hooks.direct_mode = HumanBuyerSaleDirectMode::ordinary_ai_purchase;
            direct_hooks.notice = [](const GameState&, const HumanBuyerSaleNotice&) {};
            MinuteReport direct_report;
            const auto direct_result = submit_transfer_bid_source_human_buyer(direct_state, direct, direct_rand,
                  direct_hooks, direct_report);
            check(direct_result.status == TransferActionStatus::accepted && !direct_report.unsupported,
                  "source-qualified full-value offer is accepted immediately");
            bool immediate_player = false;
            for (size_t row = own_squad.first; row < own_squad.first + own_squad.count; ++row)
                if ((direct_state.squad_slots.at(row) & 0xfff) == player) immediate_player = true;
            check(immediate_player, "immediate agreement transfers player into the manager's squad");
            TransferBidRequest unaffordable;
            unaffordable.manager = 0; unaffordable.list_entry = entry; unaffordable.player = player; unaffordable.fee = 0x7fffffff;
            unaffordable.annual_salary = int32_t(salary); unaffordable.contract_years = 2;
            check(submit_transfer_bid(transfer_state, unaffordable, transfer_rand).status ==
                  TransferActionStatus::blocked_by_board, "board refuses fees above balance plus credit");
            TransferBidRequest bid;
            bid.manager = 0; bid.list_entry = entry; bid.player = player; bid.fee = int32_t(uint64_t(fee) * 130 / 100);
            bid.annual_salary = int32_t(uint64_t(salary) * 120 / 100); bid.contract_years = 2;
            check(submit_transfer_bid(transfer_state, bid, transfer_rand).status == TransferActionStatus::pending,
                  "human bid is recorded for the weekly decision");
            check(transfer_state.memory.u16(at) == uint16_t(transfer_state.manager_teams.at(0)) &&
                  transfer_state.memory.u32(at + 0x1c) == uint32_t(bid.fee) &&
                  transfer_state.memory.u32(at + 0x0c) == uint32_t(bid.annual_salary),
                  "bid fee and contract terms persist in the shared transfer entry");
            transfer_state.memory.set8(at + 0x56, 1);
            for (int other = 0; other < 40; ++other)
                if (other != entry && (transfer_state.squad_slots.at(0xb70u + size_t(other)) & 0xfff) != 0xfff)
                    transfer_state.memory.set8(::bmh::layout::EditorListUnsigned + uint32_t(other) * 0x58 + 0x56, 9);
            const uint16_t player_before = uint16_t(player);
            GameRandom transfer_game(0x1199);
            transfer_list_countdown(transfer_state, transfer_game, transfer_rand, transfer_report, false,
                                    TransferAllocatorContext::zero);
            bool acquired = false;
            for (size_t row = own_squad.first; row < own_squad.first + own_squad.count; ++row)
                if ((transfer_state.squad_slots.at(row) & 0xfff) == player_before) acquired = true;
            check(!transfer_report.unsupported && acquired, "weekly offer accepts a competitive human bid and registers the player");
            const int32_t cash_after_transfer = int32_t(uint32_t(transfer_state.manager_records.at(0x314)) |
                uint32_t(transfer_state.manager_records.at(0x315)) << 8 | uint32_t(transfer_state.manager_records.at(0x316)) << 16 |
                uint32_t(transfer_state.manager_records.at(0x317)) << 24);
            check(cash_after_transfer < transfer_cash, "accepted transfer books the fee against the manager account");
            break;
        }
        check(buy_entry >= 0, "initialized career has an affordable external transfer candidate");
        {
            const uint32_t buyer_entry_at = ::bmh::layout::EditorListUnsigned + uint32_t(buy_entry) * 0x58;
            const int ai_seller = int(incoming_offer_base.memory.u16(buyer_entry_at + 8) & 0x7fff);
            const int buyer_player = int(incoming_offer_base.squad_slots.at(0xb70u + size_t(buy_entry)) & 0x0fff);
            check(ai_seller >= 0 && incoming_offer_base.manager_of(ai_seller) < 0 &&
                  buyer_player >= 0 && buyer_player < int(incoming_offer_base.players.size()),
                  "last-week fixture has a valid AI seller/player row");
            auto expire_others_later = [&](GameState& state, int selected) {
                for (int row = 0; row < 40; ++row) {
                    if (state.squad_slots.at(0xb70u + size_t(row)) == 0x0fff) continue;
                    const uint32_t at = ::bmh::layout::EditorListUnsigned + uint32_t(row) * 0x58;
                    state.memory.set8(at + 0x56, row == selected ? 2 : 9);
                }
            };
            auto observe_last_week = [&](GameState state, int selected, bool all, bool expected_notice) {
                expire_others_later(state, selected);
                const uint32_t at = ::bmh::layout::EditorListUnsigned + uint32_t(selected) * 0x58;
                const int player = int(state.squad_slots.at(0xb70u + size_t(selected)) & 0x0fff);
                const GameState before = state;
                std::vector<TransferDecisionEvent> events;
                state.transfer_decision_observer = [&](const TransferDecisionEvent& event) { events.push_back(event); };
                GameRandom game(0x6141);
                WatcomRandom rand = transfer_rand;
                const auto game_before = game;
                const auto rand_before = rand;
                MinuteReport report;
                transfer_list_countdown(state, game, rand, report, all);
                check(!report.unsupported, "last-week countdown stays on the supported no-expiry path");
                if (!expected_notice) {
                    check(events.empty(), "AI-only and all-at-season-end countdowns do not emit a last-week modal");
                } else {
                    check(events.size() == 1 && events[0].kind == TransferDecisionEventKind::last_week &&
                          events[0].offers.valid && events[0].offers.list_entry == selected &&
                          events[0].offers.player == player && events[0].viewer_manager == -1 &&
                          events[0].result_text_address == ::bmh::layout::PendingOffer_TransferLastWeekDialog_ExecutableField,
                          "last-week observer carries one system event, its player, and TEXTD aedcc");
                }
                check(game.seed() == game_before.seed() && game.calls() == game_before.calls() &&
                      rand.seed() == rand_before.seed() && rand.calls() == rand_before.calls(),
                      "last-week observer is Native RNG-neutral");
                if (!all) {
                    GameState expected = before;
                    for (int row = 0; row < 40; ++row) {
                        if (expected.squad_slots.at(0xb70u + size_t(row)) == 0x0fff) continue;
                        const uint32_t row_at = ::bmh::layout::EditorListUnsigned + uint32_t(row) * 0x58;
                        expected.memory.set8(row_at + 0x56,
                            uint8_t(expected.memory.u8(row_at + 0x56) - 1));
                    }
                    expected.memory.set8(at + 0x56, 1);
                    check(save_stream(state) == save_stream(expected),
                          "non-season countdown changes only Source timer bytes; observer is save-neutral");
                }
            };

            GameState human_seller = incoming_offer_base;
            expire_others_later(human_seller, own_list_entry);
            observe_last_week(human_seller, own_list_entry, false, true);

            GameState human_bidder = incoming_offer_base;
            TransferBidRequest last_week_bid;
            last_week_bid.manager = 0;
            last_week_bid.list_entry = buy_entry;
            last_week_bid.player = buyer_player;
            last_week_bid.fee = int32_t(uint64_t(human_bidder.memory.u32(buyer_entry_at + 0x2c)) * 130 / 100);
            last_week_bid.annual_salary = int32_t(uint64_t(human_bidder.memory.u32(buyer_entry_at + 0x30)) * 120 / 100);
            last_week_bid.contract_years = 2;
            WatcomRandom bidder_rand = transfer_rand;
            check(submit_transfer_bid(human_bidder, last_week_bid, bidder_rand).status == TransferActionStatus::pending,
                  "controlled last-week row owns a real human bid");
            observe_last_week(human_bidder, buy_entry, false, true);

            // legacy routine scans all four raw rows, including an occupied row after
            // an empty one; the system warning is independent of bid sorting.
            GameState sparse_bidder = human_bidder;
            for (int bid = 0; bid < 4; ++bid)
                sparse_bidder.memory.set16(buyer_entry_at + uint32_t(bid) * 2, 0xffff);
            sparse_bidder.memory.set16(buyer_entry_at + 6, uint16_t(sparse_bidder.manager_teams.at(0)));
            observe_last_week(sparse_bidder, buy_entry, false, true);

            GameState coach_bidder = human_bidder;
            coach_bidder.memory.set16(::bmh::layout::CupDraw_FormatError_RuntimeField3, uint16_t(coach_bidder.manager_teams.at(0)));
            coach_bidder.manager_teams = {0x165};
            observe_last_week(coach_bidder, buy_entry, false, true);

            GameState ai_only = incoming_offer_base;
            observe_last_week(ai_only, buy_entry, false, false);
            GameState season_all = incoming_offer_base;
            for (int row = 0; row < 40; ++row)
                if (row != own_list_entry)
                    season_all.squad_slots.at(0xb70u + size_t(row)) = 0x0fff;
            observe_last_week(season_all, own_list_entry, true, false);
        }
        auto prepare_ai_offer = [&](GameState& state) {
            const uint32_t at = ::bmh::layout::EditorListUnsigned + uint32_t(own_list_entry) * 0x58;
            int buyer = -1;
            for (int team = 0; team < 138; ++team) {
                if (state.manager_of(team) >= 0) continue;
                const SquadRange target = squad_range(team);
                bool free = false;
                for (size_t row = target.first; row < target.first + target.count; ++row)
                    if (state.squad_slots.at(row) == 0x0fff) { free = true; break; }
                if (free) { buyer = team; break; }
            }
            check(buyer >= 0, "AI buyer has squad room");
            state.memory.set16(at, uint16_t(buyer));
            state.memory.set32(at + 0x1c, state.memory.u32(at + 0x2c) * 130 / 100);
            state.memory.set32(at + 0x0c, state.memory.u32(at + 0x30));
            state.memory.set8(at + 0x34, 12);
            state.memory.set8(at + 0x56, 1);
        };
        {
            GameState projection_state = transfer_state;
            prepare_ai_offer(projection_state);
            const uint32_t at = ::bmh::layout::EditorListUnsigned + uint32_t(own_list_entry) * 0x58;
            int second_buyer = -1;
            for (int team = 0; team < 138; ++team) {
                if (projection_state.manager_of(team) >= 0 ||
                    team == int16_t(projection_state.memory.u16(at))) continue;
                const SquadRange range = squad_range(team);
                if (range.count == 0) continue;
                bool free = false;
                for (size_t row = range.first; row < range.first + range.count; ++row)
                    if (projection_state.squad_slots.at(row) == 0x0fff) { free = true; break; }
                if (free) { second_buyer = team; break; }
            }
            check(second_buyer >= 0, "second offer candidate has a buyer with squad room");
            projection_state.memory.set16(at + 2, uint16_t(second_buyer));
            projection_state.memory.set32(at + 0x20, projection_state.memory.u32(at + 0x2c) * 140 / 100);
            projection_state.memory.set32(at + 0x10, projection_state.memory.u32(at + 0x30) * 130 / 100);
            projection_state.memory.set8(at + 0x35, 0x9c); // raw 28-month loan term
            for (int bonus = 0; bonus < 7; ++bonus)
                projection_state.memory.set8(at + 0x3f + uint32_t(bonus), uint8_t(bonus + 1));
            const auto save_before_projection = save_stream(projection_state);
            const auto projection = project_transfer_offer_list(projection_state, own_list_entry);
            check(projection.valid && projection.player == own_list_player && projection.seller_manager == 0 &&
                  projection.offer_count == 2 && projection.offers[0].present &&
                  projection.offers[0].bid_index == 0 && projection.offers[1].present &&
                  projection.offers[1].bid_index == 1,
                  "read-only transfer projection preserves raw bidder row identities");
            check(projection.offers[0].fee == projection_state.memory.u32(at + 0x1c) &&
                  projection.offers[0].monthly_salary == projection_state.memory.u32(at + 0x0c) &&
                  projection.offers[1].buyer == second_buyer &&
                  projection.offers[0].buyer != projection.offers[1].buyer &&
                  projection.offers[1].fee == projection_state.memory.u32(at + 0x20) &&
                  projection.offers[1].monthly_salary == projection_state.memory.u32(at + 0x10) &&
                  projection.offers[0].fee != projection.offers[1].fee &&
                  projection.offers[0].monthly_salary != projection.offers[1].monthly_salary &&
                  projection.offers[1].loan &&
                  projection.offers[1].contract_months == 0x1c &&
                  projection.offers[1].bonuses == std::array<uint8_t, 7>{1, 2, 3, 4, 5, 6, 7} &&
                  projection.offers[1].eligible && projection.offers[1].loan_permitted &&
                  projection.offers[1].transaction_supported &&
                  projection.offers[1].fee_reference > 0 && projection.offers[1].salary_reference > 0,
                  "projection exposes raw prices, bonuses, and the supported human-owner-to-AI loan path");
            check(save_stream(projection_state) == save_before_projection &&
                  !project_transfer_offer_list(projection_state, -1).valid,
                  "offer projection leaves the complete serialized game state unchanged");

            GameState full_buyer = projection_state;
            const SquadRange full_range = squad_range(second_buyer);
            for (size_t row = full_range.first; row < full_range.first + full_range.count; ++row)
                if (full_buyer.squad_slots.at(row) == 0x0fff)
                    full_buyer.squad_slots[row] = uint16_t(own_list_player);
            const auto full_projection = project_transfer_offer_list(full_buyer, own_list_entry);
            check(!full_projection.offers[1].has_squad_room && !full_projection.offers[1].eligible,
                  "projection marks a source-full buyer ineligible without accepting the offer");

            GameState no_cash_buyer = projection_state;
            no_cash_buyer.manager_teams.push_back(second_buyer);
            no_cash_buyer.manager_records.resize(2 * 0xb0c, 0);
            const auto no_cash_projection = project_transfer_offer_list(no_cash_buyer, own_list_entry);
            check(no_cash_projection.offers[1].buyer_manager == 1 &&
                  !no_cash_projection.offers[1].affordable && !no_cash_projection.offers[1].eligible,
                  "projection applies the signed balance-plus-credit gate for a human bidder");

            GameState malformed_buyer = projection_state;
            malformed_buyer.memory.set16(at + 2, 138);
            const auto malformed_projection = project_transfer_offer_list(malformed_buyer, own_list_entry);
            check(malformed_projection.offer_count == 2 && malformed_projection.offers[1].buyer == 138 &&
                  !malformed_projection.offers[1].has_squad_room &&
                  !malformed_projection.offers[1].passes_foreign_limit &&
                  !malformed_projection.offers[1].eligible,
                  "malformed buyer IDs remain safely visible but cannot pass source eligibility");

            GameState terminated_rows = projection_state;
            terminated_rows.memory.set16(at + 2, 0xffff);
            terminated_rows.memory.set16(at + 4, uint16_t(second_buyer)); // stale bytes after terminator
            const auto terminated_projection = project_transfer_offer_list(terminated_rows, own_list_entry);
            check(terminated_projection.offer_count == 1 && terminated_projection.offers[0].bid_index == 0 &&
                  !terminated_projection.offers[1].present && terminated_projection.offers[1].bid_index == 1,
                  "projection stops at the original first-empty bid terminator");
        }
        {
            auto with_second_offer = [&](GameState& state, uint32_t fee_percent) {
                prepare_ai_offer(state);
                const uint32_t at = ::bmh::layout::EditorListUnsigned + uint32_t(own_list_entry) * 0x58;
                int buyer = -1;
                for (int team = 0; team < 138; ++team) {
                    if (state.manager_of(team) >= 0 || team == int16_t(state.memory.u16(at))) continue;
                    const SquadRange range = squad_range(team);
                    if (range.count == 0) continue;
                    for (size_t row = range.first; row < range.first + range.count; ++row)
                        if (state.squad_slots.at(row) == 0x0fff) { buyer = team; break; }
                    if (buyer >= 0) break;
                }
                check(buyer >= 0, "selector second buyer has room");
                state.memory.set32(at + 0x1c, state.memory.u32(at + 0x2c) * 3);
                state.memory.set32(at + 0x0c, state.memory.u32(at + 0x30) * 3);
                state.memory.set16(at + 2, uint16_t(buyer));
                state.memory.set32(at + 0x20, state.memory.u32(at + 0x2c) * fee_percent / 100);
                state.memory.set32(at + 0x10, state.memory.u32(at + 0x30) * 3 / 2);
                state.memory.set8(at + 0x35, 12);
                return buyer;
            };
            auto run_selector = [&](GameState& state, GameRandom& game, WatcomRandom& rand,
                                    std::function<int(const GameState&, const TransferOfferListView&)> selector,
                                    MinuteReport& report) {
                state.transfer_offer_selector = std::move(selector);
                transfer_list_countdown(state, game, rand, report);
            };
            GameState direct = transfer_state;
            const int selected_buyer = with_second_offer(direct, 160);
            GameRandom direct_game(0x66a1);
            WatcomRandom direct_rand = transfer_rand;
            MinuteReport direct_report;
            int direct_calls = 0;
            run_selector(direct, direct_game, direct_rand,
                [&](const GameState&, const TransferOfferListView& view) {
                    ++direct_calls;
                    check(view.preferred_bid_index == 0 && view.offer_count == 2 && view.offers[1].present &&
                          view.quoted_fee != 0 && view.quoted_monthly_salary != 0 && view.quoted_years >= 1 &&
                          view.asking_fee != 0 && view.asking_monthly_salary != 0,
                          "selector receives ranked preference, raw rows, and Source quote/request values");
                    return 1; // choose the second raw row, not the pre-ranked row
                }, direct_report);
            bool selected_owned = false;
            const SquadRange selected_range = squad_range(selected_buyer);
            for (size_t row = selected_range.first; row < selected_range.first + selected_range.count; ++row)
                if ((direct.squad_slots.at(row) & 0x0fff) == own_list_player) selected_owned = true;
            check(!direct_report.unsupported && direct_calls == 1 && selected_owned,
                  "seller can select a non-top raw bid and complete that transfer");

            GameState low_fee_selected = transfer_state;
            const int low_fee_buyer = with_second_offer(low_fee_selected, 1);
            const uint32_t low_fee_at = ::bmh::layout::EditorListUnsigned + uint32_t(own_list_entry) * 0x58;
            low_fee_selected.memory.set8(low_fee_at + 0x55, 0);
            GameRandom low_fee_game(0x66a2);
            WatcomRandom low_fee_rand = transfer_rand;
            MinuteReport low_fee_report;
            run_selector(low_fee_selected, low_fee_game, low_fee_rand,
                [](const GameState&, const TransferOfferListView&) { return 1; }, low_fee_report);
            bool low_fee_player_moved = false;
            const SquadRange low_fee_range = squad_range(low_fee_buyer);
            for (size_t row = low_fee_range.first; row < low_fee_range.first + low_fee_range.count; ++row)
                if ((low_fee_selected.squad_slots.at(row) & 0x0fff) == own_list_player) low_fee_player_moved = true;
            check(!low_fee_report.unsupported && low_fee_player_moved,
                  "human seller accepts a selected offer when Source salary_pct passes even if fee_pct is zero");

            GameState invalid_then_direct = transfer_state;
            with_second_offer(invalid_then_direct, 160);
            const uint32_t invalid_at = ::bmh::layout::EditorListUnsigned + uint32_t(own_list_entry) * 0x58;
            GameRandom invalid_game(0x66a1);
            WatcomRandom invalid_rand = transfer_rand;
            MinuteReport invalid_report;
            int invalid_calls = 0;
            int invalid_ack_calls = 0;
            invalid_then_direct.transfer_decision_observer = [&](const TransferDecisionEvent& event) {
                ++invalid_ack_calls;
                bool buyer_owns = false;
                const int buyer = event.offers.offers[1].buyer;
                const SquadRange range = squad_range(buyer);
                for (size_t row = range.first; row < range.first + range.count; ++row)
                    if ((invalid_then_direct.squad_slots.at(row) & 0x0fff) == own_list_player) buyer_owns = true;
                check(invalid_calls == 2 && buyer_owns && event.kind == TransferDecisionEventKind::confirmation &&
                      event.accepted && event.selected_bid_index == 1 && event.viewer_manager == 0,
                      "malformed empty-row response emits no acknowledgement before the later real transfer");
            };
            uint32_t first_quote = 0;
            run_selector(invalid_then_direct, invalid_game, invalid_rand,
                [&](const GameState&, const TransferOfferListView& view) {
                    ++invalid_calls;
                    if (invalid_calls == 1) { first_quote = view.quoted_fee; return 4; }
                    check(view.quoted_fee == first_quote && view.rejected_bid_index == -1,
                          "invalid raw-row input retries the same screen without a new quote or notice");
                    return 1;
                }, invalid_report);
            check(!invalid_report.unsupported && invalid_calls == 2 &&
                  invalid_ack_calls == 1 &&
                  save_stream(invalid_then_direct) == save_stream(direct) &&
                  invalid_game.seed() == direct_game.seed() && invalid_game.calls() == direct_game.calls() &&
                  invalid_rand.seed() == direct_rand.seed() && invalid_rand.calls() == direct_rand.calls(),
                  "invalid selector response has no extra transfer, game-state, or RNG effects");

            GameState retry = transfer_state;
            with_second_offer(retry, 160);
            const uint32_t retry_at = ::bmh::layout::EditorListUnsigned + uint32_t(own_list_entry) * 0x58;
            const int retry_buyer = int16_t(retry.memory.u16(retry_at));
            retry.memory.set8(retry_at + 0x55, 250); // row 1's 150% salary fails the Source recheck; row 0's 300% passes
            GameRandom retry_game(0x7788);
            WatcomRandom retry_rand = transfer_rand;
            MinuteReport retry_report;
            int retry_calls = 0;
            run_selector(retry, retry_game, retry_rand,
                [&](const GameState&, const TransferOfferListView& view) {
                    ++retry_calls;
                    if (retry_calls == 1) return 1;
                    check(view.rejected_bid_index == 1 && view.notice_text_address == ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField3 &&
                          !view.offers[1].present && view.offers[0].present,
                          "failed salary/fee check removes only that raw bid and projects Source retry notice");
                    return 0;
                }, retry_report);
            check(!retry_report.unsupported && retry_calls == 2,
                  "rejected selected offer returns to the seller choice while another bid remains");
            bool retry_buyer_owns = false;
            const SquadRange retry_range = squad_range(retry_buyer);
            for (size_t row = retry_range.first; row < retry_range.first + retry_range.count; ++row)
                if ((retry.squad_slots.at(row) & 0x0fff) == own_list_player) retry_buyer_owns = true;
            check(retry_buyer_owns, "after the salary rejection the seller can accept the remaining bid");

            {
                GameState top_retry = transfer_state;
                const int second_buyer = with_second_offer(top_retry, 160);
                const uint32_t at = ::bmh::layout::EditorListUnsigned + uint32_t(own_list_entry) * 0x58;
                const int first_buyer = int16_t(top_retry.memory.u16(at));
                const uint32_t salary_base = top_retry.memory.u32(at + 0x30);
                top_retry.memory.set32(at + 0x0c, salary_base * 180 / 100); // raw 0: 180%, passes after 125% recheck
                top_retry.memory.set32(at + 0x10, salary_base);             // raw 1: 100%, still fails at 125%
                top_retry.memory.set8(at + 0x55, 200);

                GameRandom game(0x7a19);
                WatcomRandom rand = transfer_rand;
                MinuteReport report;
                int calls = 0;
                run_selector(top_retry, game, rand,
                    [&](const GameState&, const TransferOfferListView& view) {
                        ++calls;
                        if (calls == 1) {
                            check(view.preferred_bid_index == 1 && view.offer_count == 2 &&
                                  view.offers[0].present && view.offers[1].present,
                                  "top-ranked raw bid is row 1 before the seller's choice");
                            return 1;
                        }
                        check(calls == 2 && view.preferred_bid_index == -1 &&
                              view.rejected_bid_index == 1 && view.offer_count == 1 &&
                              !view.offers[1].present && view.offers[0].present &&
                              view.notice_text_address == ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField3,
                              "rejecting the preferred row clears its preferred marker before retry");
                        return 0;
                    }, report);

                bool first_buyer_owns_player = false;
                const SquadRange range = squad_range(first_buyer);
                for (size_t row = range.first; row < range.first + range.count; ++row)
                    if ((top_retry.squad_slots.at(row) & 0x0fff) == own_list_player) first_buyer_owns_player = true;
                check(!report.unsupported && calls == 2 && first_buyer_owns_player &&
                      second_buyer != first_buyer,
                      "the remaining raw row is selectable after the preferred row fails salary recheck");
            }

            GameState terminal = transfer_state;
            prepare_ai_offer(terminal); // one bid only
            const uint32_t terminal_at = ::bmh::layout::EditorListUnsigned + uint32_t(own_list_entry) * 0x58;
            terminal.memory.set8(terminal_at + 0x55, 255);
            GameRandom terminal_game(0x7789);
            WatcomRandom terminal_rand = transfer_rand;
            MinuteReport terminal_report;
            int terminal_calls = 0;
            int terminal_observer_calls = 0;
            terminal.transfer_decision_observer = [&](const TransferDecisionEvent&) { ++terminal_observer_calls; };
            uint32_t terminal_quote = 0;
            uint64_t terminal_game_calls = 0, terminal_rand_calls = 0;
            run_selector(terminal, terminal_game, terminal_rand,
                [&](const GameState&, const TransferOfferListView& view) {
                    ++terminal_calls;
                    if (terminal_calls == 1) {
                        terminal_quote = view.quoted_fee;
                        terminal_game_calls = terminal_game.calls();
                        terminal_rand_calls = terminal_rand.calls();
                        return 0;
                    }
                    check(view.offer_count == 0 && view.preferred_bid_index == -1 &&
                          view.notice_text_address == ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField3 && view.quoted_fee == terminal_quote &&
                          terminal_game.calls() == terminal_game_calls &&
                          terminal_rand.calls() == terminal_rand_calls + 1,
                          "last salary refusal displays terminal Source notice without a fresh quote/RNG redraw");
                    return 0; // Source has already left the choice screen; this answer is ignored.
                }, terminal_report);
            check(!terminal_report.unsupported && terminal_calls == 2,
                  "last failed human bid shows one final refusal and then returns");
            check(terminal_observer_calls == 0,
                  "terminal salary-refusal notice is not misreported as an explicit seller acknowledgement");

            GameState loan_retry = transfer_state;
            with_second_offer(loan_retry, 160);
            const uint32_t loan_at = ::bmh::layout::EditorListUnsigned + uint32_t(own_list_entry) * 0x58;
            loan_retry.memory.set8(loan_at + 0x35, 0x8c);
            loan_retry.memory.set8(::bmh::layout::DeskActions_Unsupported_RuntimeField, 3);
            GameRandom loan_game(0x8899);
            WatcomRandom loan_rand = transfer_rand;
            MinuteReport loan_report;
            int loan_calls = 0;
            run_selector(loan_retry, loan_game, loan_rand,
                [&](const GameState&, const TransferOfferListView& view) {
                    ++loan_calls;
                    if (loan_calls <= 10) {
                        check(view.offers[1].present && view.notice_text_address == (loan_calls == 1 ? 0u : ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField2Unsigned),
                              "repeated valid selection of a loan blocked by the source manager limit redraws without dropping it");
                        return 1;
                    }
                    check(view.rejected_bid_index == 1 && view.notice_text_address == ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField2 &&
                          view.offers[1].present && view.offers[1].loan && !view.offers[1].loan_permitted,
                          "seller loan-limit refusal preserves the raw bid and returns the Source notice");
                    return 0;
                }, loan_report);
            check(!loan_report.unsupported && loan_calls == 11,
                  "legitimate Source loan-limit retries are not cut off by the malformed-input bound");

            GameState owner_loan = transfer_state;
            const int loan_buyer = with_second_offer(owner_loan, 160);
            const uint32_t owner_loan_at = ::bmh::layout::EditorListUnsigned + uint32_t(own_list_entry) * 0x58;
            owner_loan.memory.set8(owner_loan_at + 0x35, 0x8c); // 12-month loan
            owner_loan.memory.set8(::bmh::layout::DeskActions_Unsupported_RuntimeField, 0);
            owner_loan.transfer_offer_selector = [](const GameState&, const TransferOfferListView&) { return 1; };
            GameRandom owner_loan_game(0x9901);
            WatcomRandom owner_loan_rand = transfer_rand;
            MinuteReport owner_loan_report;
            transfer_list_countdown(owner_loan, owner_loan_game, owner_loan_rand, owner_loan_report);
            bool buyer_has_loaned_player = false;
            for (size_t row = squad_range(loan_buyer).first;
                 row < squad_range(loan_buyer).first + squad_range(loan_buyer).count; ++row)
                if ((owner_loan.squad_slots.at(row) & 0x0fff) == own_list_player) buyer_has_loaned_player = true;
            int saved_owner_record = -1;
            for (int i = 0; i < 12; ++i)
                if (owner_loan.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + uint32_t(i) * 2) == uint16_t(own_list_player)) saved_owner_record = i;
            check(!owner_loan_report.unsupported && buyer_has_loaned_player && saved_owner_record >= 0 &&
                  owner_loan.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField) == 1 &&
                  owner_loan.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField) == uint32_t(own_list_player +
                      transfer_state.manager_teams.at(0) * 0x1000 + loan_buyer * 0x400000),
                  "human owner completes an AI loan and persists Source owner snapshot plus loan ledger");

            {
                GameState full_owner = owner_loan;
                const uint32_t ledger = full_owner.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField);
                int ledger_slot = -1;
                for (int i = 0; i < 200; ++i)
                    if (full_owner.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(i) * 4) == ledger) { ledger_slot = i; break; }
                const SquadRange range = squad_range(transfer_state.manager_teams.at(0));
                int filler = -1;
                for (size_t i = 0; i < range.count; ++i) {
                    const int p = full_owner.squad_slots.at(range.first + i) & 0x0fff;
                    if (p != 0x0fff && p != own_list_player) { filler = p; break; }
                }
                check(ledger_slot >= 0 && filler >= 0, "full-owner return fixture has a live loan and roster filler");
                for (size_t i = 0; i < range.count; ++i)
                    if (full_owner.squad_slots.at(range.first + i) == 0x0fff)
                        full_owner.squad_slots[range.first + i] = uint16_t(filler);
                full_owner.memory.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField + uint32_t(ledger_slot), 1);
                GameRandom full_game(0x9904);
                WatcomRandom full_rand = owner_loan_rand;
                MinuteReport full_report;
                month_change(full_owner, full_rand, full_report, &full_game);
                check(full_report.unsupported && full_report.reason.find("cannot be restored safely") != std::string::npos &&
                      full_owner.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(ledger_slot) * 4) == ledger &&
                      full_owner.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + uint32_t(saved_owner_record) * 2) == uint16_t(own_list_player) &&
                      full_owner.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField) == 1,
                      "a full owner roster leaves the expiring loan ledger and owner snapshot intact");
            }
            {
                GameState zero_term = owner_loan;
                int ledger_slot = -1;
                const uint32_t ledger = zero_term.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField);
                for (int i = 0; i < 200; ++i)
                    if (zero_term.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(i) * 4) == ledger) { ledger_slot = i; break; }
                check(ledger_slot >= 0, "byte-wrap fixture keeps the human-owner loan ledger");
                zero_term.memory.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField + uint32_t(ledger_slot), 0);
                GameRandom wrap_game(0x9905);
                WatcomRandom wrap_rand = owner_loan_rand;
                MinuteReport wrap_report;
                month_change(zero_term, wrap_rand, wrap_report, &wrap_game);
                check(!wrap_report.unsupported && zero_term.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(ledger_slot) * 4) == ledger &&
                      zero_term.memory.u8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField + uint32_t(ledger_slot)) == 0xff &&
                      zero_term.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + uint32_t(saved_owner_record) * 2) == uint16_t(own_list_player),
                      "zero loan month follows the Source unsigned-byte decrement wrap instead of returning immediately");
            }

            GameState no_loan_record = transfer_state;
            with_second_offer(no_loan_record, 160);
            const uint32_t no_record_at = ::bmh::layout::EditorListUnsigned + uint32_t(own_list_entry) * 0x58;
            no_loan_record.memory.set8(no_record_at + 0x35, 0x8c);
            const uint32_t other_player = uint32_t(own_list_player == 0x0ffe ? 1 : own_list_player + 1);
            for (int i = 0; i < 200; ++i)
                no_loan_record.memory.set32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(i) * 4,
                    other_player | (uint32_t(transfer_state.manager_teams.at(0)) << 12));
            const auto no_record_mail = no_loan_record.mailbox;
            const auto no_record_money = no_loan_record.manager_records;
            const auto no_record_squads = no_loan_record.squad_slots;
            const auto no_record_snapshot_ids = [&] {
                std::array<uint16_t, 12> ids{};
                for (int i = 0; i < 12; ++i) ids[size_t(i)] = no_loan_record.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + uint32_t(i) * 2);
                return ids;
            }();
            no_loan_record.transfer_offer_selector = [](const GameState&, const TransferOfferListView&) { return 1; };
            GameRandom no_record_game(0x9903);
            WatcomRandom no_record_rand = transfer_rand;
            MinuteReport no_record_report;
            transfer_list_countdown(no_loan_record, no_record_game, no_record_rand, no_record_report);
            bool snapshot_ids_unchanged = true;
            for (int i = 0; i < 12; ++i)
                snapshot_ids_unchanged = snapshot_ids_unchanged &&
                    no_loan_record.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + uint32_t(i) * 2) == no_record_snapshot_ids[size_t(i)];
            check(no_record_report.unsupported && no_record_report.reason.find("loan table full") != std::string::npos &&
                  no_loan_record.mailbox == no_record_mail && no_loan_record.manager_records == no_record_money &&
                  no_loan_record.squad_slots == no_record_squads && snapshot_ids_unchanged &&
                  no_loan_record.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField) == 0,
                  "full loan ledger is rejected before transfer news, seller payment, or roster/snapshot mutation");

            {
                GameState no_snapshot = transfer_state;
                with_second_offer(no_snapshot, 160);
                const uint32_t at = ::bmh::layout::EditorListUnsigned + uint32_t(own_list_entry) * 0x58;
                no_snapshot.memory.set8(at + 0x35, 0x8c);
                for (int i = 0; i < 12; ++i)
                    no_snapshot.memory.set16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + uint32_t(i) * 2, uint16_t(other_player));
                const auto before = save_stream(no_snapshot);
                WatcomRandom copied_rand = transfer_rand;
                MinuteReport no_snapshot_report;
                check(!accept_transfer_bid_immediately(no_snapshot, own_list_entry, 1,
                                                       copied_rand, no_snapshot_report) &&
                      no_snapshot_report.unsupported &&
                      no_snapshot_report.reason.find("snapshots full") != std::string::npos &&
                      save_stream(no_snapshot) == before &&
                      copied_rand.seed() == transfer_rand.seed() &&
                      copied_rand.calls() == transfer_rand.calls(),
                      "all twelve occupied owner snapshots reject before state, RNG, news or finance mutation");
            }

            {
                const uint32_t loan_record = owner_loan.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField);
                int loan_slot = -1;
                for (int i = 0; i < 200; ++i)
                    if (owner_loan.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(i) * 4) == loan_record) { loan_slot = i; break; }
                check(loan_slot >= 0, "human-owner loan has an indexed Source ledger entry");
                owner_loan.memory.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField + uint32_t(loan_slot), 1);
                WatcomRandom save_rand = owner_loan_rand;
                const auto loan_dir = temp.path / "loan-roundtrip";
                std::filesystem::create_directories(loan_dir);
                const auto loan_save_path = loan_dir / "HUMAN-OWNER-LOAN.FBM";
                write_savegame(loan_save_path, make_savegame(owner_loan, save_rand));
                app::CareerSession loaded_loan(data, 0x7711, loan_save_path);
                check(save_stream(loaded_loan.state()) == load_savegame(loan_save_path).stream &&
                      loaded_loan.game().calls() == 0 && loaded_loan.rand().calls() == 0 &&
                      loaded_loan.state().memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(loan_slot) * 4) == loan_record &&
                      loaded_loan.state().memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + uint32_t(saved_owner_record) * 2) == uint16_t(own_list_player),
                      "native FBM load preserves the owner snapshot and mid-loan ledger without RNG draws");
                GameState& returning = const_cast<GameState&>(loaded_loan.state());
                returning.mailbox.assign(kMailboxEntries * kMailboxEntry, 0);
                MinuteReport return_report;
                month_change(returning, loaded_loan.rand(), return_report, &loaded_loan.game());
                bool owner_has_player = false, borrower_still_has_player = false;
                const SquadRange owner_range = squad_range(transfer_state.manager_teams.at(0));
                const SquadRange borrower_range = squad_range(loan_buyer);
                for (size_t row = owner_range.first; row < owner_range.first + owner_range.count; ++row)
                    if ((returning.squad_slots.at(row) & 0x0fff) == own_list_player) owner_has_player = true;
                for (size_t row = borrower_range.first; row < borrower_range.first + borrower_range.count; ++row)
                    if ((returning.squad_slots.at(row) & 0x0fff) == own_list_player) borrower_still_has_player = true;
                bool owner_notice_mask = false;
                for (size_t i = 0; i < kMailboxEntries; ++i)
                    if (record_u32(returning.mailbox, i * kMailboxEntry) == 0x80000001u) owner_notice_mask = true;
                check(!return_report.unsupported && owner_has_player && !borrower_still_has_player &&
                      returning.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + uint32_t(loan_slot) * 4) == 0 &&
                      returning.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + uint32_t(saved_owner_record) * 2) == 0x0fff &&
                      returning.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField) == 0 &&
                      returning.memory.u8(::bmh::layout::ClubInfo_FormatError_RuntimeField2 + uint32_t(saved_owner_record)) == 0 && owner_notice_mask,
                      (std::string("monthly expiry returns loaned player to human owner with Source notice: ") +
                       (return_report.unsupported ? return_report.reason : "")).c_str());
                bool snapshot_bytes_unchanged = true;
                for (uint32_t b = 0; b < 0x58; ++b)
                    snapshot_bytes_unchanged = snapshot_bytes_unchanged &&
                        owner_loan.memory.u8(::bmh::layout::ClubInfo_FormatError_RuntimeField + uint32_t(saved_owner_record) * 0x58 + b) ==
                        returning.memory.u8(::bmh::layout::ClubInfo_FormatError_RuntimeField + uint32_t(saved_owner_record) * 0x58 + b);
                check(snapshot_bytes_unchanged,
                      "loan return releases only the snapshot ID and count; saved board bytes remain Source-owned");
            }

            GameState human_borrower = transfer_state;
            with_second_offer(human_borrower, 160);
            const uint32_t human_borrower_at = ::bmh::layout::EditorListUnsigned + uint32_t(own_list_entry) * 0x58;
            const int human_borrower_team = human_borrower.memory.u16(human_borrower_at + 2);
            human_borrower.manager_teams.push_back(human_borrower_team);
            human_borrower.manager_records.resize(2 * 0xb0c, 0);
            human_borrower.memory.set8(human_borrower_at + 0x35, 0x8c);
            GameRandom unsupported_game(0x9902);
            WatcomRandom unsupported_rand = transfer_rand;
            MinuteReport unsupported_report;
            int unsupported_calls = 0;
            int unsupported_observer_calls = 0;
            human_borrower.transfer_decision_observer = [&](const TransferDecisionEvent&) {
                ++unsupported_observer_calls;
            };
            human_borrower.transfer_offer_selector = [&](const GameState& current, const TransferOfferListView& view) {
                ++unsupported_calls;
                check(!view.offers[1].transaction_supported && view.offers[1].loan_permitted,
                      "loan to a human borrower remains visible but unavailable without its consent path");
                (void)current;
                return 1;
            };
            transfer_list_countdown(human_borrower, unsupported_game, unsupported_rand, unsupported_report);
            check(!unsupported_report.unsupported && unsupported_calls == 8 &&
                  (human_borrower.squad_slots.at(0xb70u + size_t(own_list_entry)) & 0x0fff) == own_list_player &&
                  human_borrower.memory.u16(human_borrower_at + 2) != 0xffff,
                  "human borrower loan remains inert and preserves the listed player and bid");
            check(unsupported_observer_calls == 0,
                  "preserved human borrower loan emits no acceptance or decline acknowledgement");

            GameState decline = transfer_state;
            with_second_offer(decline, 160);
            GameRandom decline_game(0x66a1);
            WatcomRandom decline_rand = transfer_rand;
            MinuteReport decline_report;
            run_selector(decline, decline_game, decline_rand,
                [](const GameState&, const TransferOfferListView&) { return -1; }, decline_report);
            bool kept_after_decline = false;
            for (size_t row = own_squad.first; row < own_squad.first + own_squad.count; ++row)
                if ((decline.squad_slots.at(row) & 0x0fff) == own_list_player) kept_after_decline = true;
            check(!decline_report.unsupported && kept_after_decline,
                  "explicit source decline keeps the listed player with the seller");

            GameState decline_silent = transfer_state;
            GameState decline_observed = transfer_state;
            with_second_offer(decline_silent, 160);
            with_second_offer(decline_observed, 160);
            int decline_ack_count = 0;
            decline_observed.transfer_decision_observer = [&](const TransferDecisionEvent& event) {
                ++decline_ack_count;
                check(event.kind == TransferDecisionEventKind::confirmation && !event.accepted &&
                      event.selected_bid_index == -1 && event.viewer_manager == 0 &&
                      event.result_text_address == ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField,
                      "explicit right-click decline emits a seller acknowledgement with the original text");
            };
            decline_silent.transfer_offer_selector = [](const GameState&, const TransferOfferListView&) { return -1; };
            decline_observed.transfer_offer_selector = decline_silent.transfer_offer_selector;
            GameRandom decline_silent_game(0x7661), decline_observed_game(0x7661);
            WatcomRandom decline_silent_rand = transfer_rand, decline_observed_rand = transfer_rand;
            MinuteReport decline_silent_report, decline_observed_report;
            transfer_list_countdown(decline_silent, decline_silent_game, decline_silent_rand, decline_silent_report);
            transfer_list_countdown(decline_observed, decline_observed_game, decline_observed_rand, decline_observed_report);
            check(!decline_silent_report.unsupported && !decline_observed_report.unsupported &&
                  decline_ack_count == 1 &&
                  save_stream(decline_silent) == save_stream(decline_observed) &&
                  decline_silent_game.seed() == decline_observed_game.seed() &&
                  decline_silent_game.calls() == decline_observed_game.calls() &&
                  decline_silent_rand.seed() == decline_observed_rand.seed() &&
                  decline_silent_rand.calls() == decline_observed_rand.calls(),
                  "decline observer is RNG- and game-state-neutral");

            GameState suppressed_decline = transfer_state;
            with_second_offer(suppressed_decline, 160);
            suppressed_decline.transfer_notice_suppressed = true; // format field is runtime-only, not a save block
            suppressed_decline.transfer_offer_selector = [](const GameState&, const TransferOfferListView&) { return -1; };
            int suppressed_ack_count = 0;
            suppressed_decline.transfer_decision_observer = [&](const TransferDecisionEvent&) { ++suppressed_ack_count; };
            GameRandom suppressed_game(0x7663);
            WatcomRandom suppressed_rand = transfer_rand;
            MinuteReport suppressed_report;
            transfer_list_countdown(suppressed_decline, suppressed_game, suppressed_rand, suppressed_report);
            check(!suppressed_report.unsupported && suppressed_ack_count == 0,
                  "Source suppression flag hides explicit-decline notice and its UI acknowledgement");

            GameState selected_silent = transfer_state;
            GameState selected_observed = transfer_state;
            const int selected_ack_buyer = with_second_offer(selected_silent, 160);
            check(with_second_offer(selected_observed, 160) == selected_ack_buyer,
                  "selected-offer observer fixtures have same raw-slot buyer");
            selected_silent.transfer_offer_selector = [](const GameState&, const TransferOfferListView&) { return 1; };
            selected_observed.transfer_offer_selector = selected_silent.transfer_offer_selector;
            int selected_ack_count = 0;
            selected_observed.transfer_decision_observer = [&](const TransferDecisionEvent& event) {
                ++selected_ack_count;
                bool buyer_owns = false;
                const SquadRange buyer_range = squad_range(selected_ack_buyer);
                for (size_t row = buyer_range.first; row < buyer_range.first + buyer_range.count; ++row)
                    if ((selected_observed.squad_slots.at(row) & 0x0fff) == own_list_player) buyer_owns = true;
                check(event.kind == TransferDecisionEventKind::confirmation && event.accepted &&
                      event.selected_bid_index == 1 && event.viewer_manager == 0 &&
                      event.result_text_address == ::bmh::layout::PendingOffer_FormatError_ExecutableField2 && event.offers.player == own_list_player &&
                      event.offers.offers[1].buyer == selected_ack_buyer && buyer_owns,
                      "non-top selected offer acknowledges the human seller only after the real move");
            };
            GameRandom selected_silent_game(0x7662), selected_observed_game(0x7662);
            WatcomRandom selected_silent_rand = transfer_rand, selected_observed_rand = transfer_rand;
            MinuteReport selected_silent_report, selected_observed_report;
            transfer_list_countdown(selected_silent, selected_silent_game, selected_silent_rand, selected_silent_report);
            transfer_list_countdown(selected_observed, selected_observed_game, selected_observed_rand, selected_observed_report);
            check(!selected_silent_report.unsupported && !selected_observed_report.unsupported &&
                  selected_ack_count == 1 && save_stream(selected_silent) == save_stream(selected_observed) &&
                  selected_silent_game.seed() == selected_observed_game.seed() &&
                  selected_silent_game.calls() == selected_observed_game.calls() &&
                  selected_silent_rand.seed() == selected_observed_rand.seed() &&
                  selected_silent_rand.calls() == selected_observed_rand.calls(),
                  "successful manual selection emits one acknowledgement without changing state or RNG");
        }
        {
            GameState seller_state = transfer_state;
            WatcomRandom seller_rand = transfer_rand;
            GameRandom seller_game(0x3399);
            prepare_ai_offer(seller_state);
            int decisions = 0;
            int automatic_events=0;
            seller_state.transfer_decision_observer=[&](const TransferDecisionEvent&){++automatic_events;};
            seller_state.transfer_decision = [&](GameState&, int, int, int, int32_t, int32_t) { ++decisions; return true; };
            MinuteReport sale_report;
            const int32_t balance = int32_t(uint32_t(seller_state.manager_records.at(0x314)) |
                uint32_t(seller_state.manager_records.at(0x315)) << 8 | uint32_t(seller_state.manager_records.at(0x316)) << 16 |
                uint32_t(seller_state.manager_records.at(0x317)) << 24);
            transfer_list_countdown(seller_state, seller_game, seller_rand, sale_report);
            bool still_owned = false;
            for (size_t row = own_squad.first; row < own_squad.first + own_squad.count; ++row)
                if ((seller_state.squad_slots.at(row) & 0xfff) == own_list_player) still_owned = true;
            const int32_t after_sale = int32_t(uint32_t(seller_state.manager_records.at(0x314)) |
                uint32_t(seller_state.manager_records.at(0x315)) << 8 | uint32_t(seller_state.manager_records.at(0x316)) << 16 |
                uint32_t(seller_state.manager_records.at(0x317)) << 24);
            check(!sale_report.unsupported && decisions == 1 && automatic_events==0 && !still_owned && after_sale > balance,
                  "manager accepts an AI offer, sells the player and receives the fee");
        }
        {
            // Source legacy routine presents an automatic decision when an AI seller
            // receives at least one human bid. The observer must not participate in
            // ranking, quote generation, acceptance RNG, or the transfer itself.
            auto prepare_human_bid = [&](GameState& state, uint32_t multiplier) {
                const uint32_t at = ::bmh::layout::EditorListUnsigned + uint32_t(buy_entry) * 0x58;
                const int player = state.squad_slots.at(0xb70u + size_t(buy_entry)) & 0x0fff;
                check(player != 0x0fff && state.manager_of(state.memory.u16(at + 8) & 0x7fff) < 0,
                      "observer fixture uses a listed AI-seller player");
                const int human_team = state.manager_teams.at(0);
                const uint32_t base_fee = state.memory.u32(at + 0x2c);
                const uint32_t base_salary = state.memory.u32(at + 0x30);
                state.memory.set16(at, uint16_t(human_team));
                state.memory.set16(at + 2, 0xffff);
                state.memory.set32(at + 0x1c, base_fee * multiplier);
                state.memory.set32(at + 0x0c, base_salary * multiplier);
                state.memory.set8(at + 0x34, 24);
                state.memory.set8(at + 0x56, 1);
                for (int other = 0; other < 40; ++other) {
                    if (other != buy_entry &&
                        (state.squad_slots.at(0xb70u + size_t(other)) & 0x0fff) != 0x0fff)
                        state.memory.set8(::bmh::layout::EditorListUnsigned + uint32_t(other) * 0x58 + 0x56, 9);
                }
                return player;
            };
            auto run_observer_case = [&](uint32_t multiplier, int seed, bool expected_acceptance) {
                GameState silent = incoming_offer_base;
                GameState observed = incoming_offer_base;
                const int player = prepare_human_bid(silent, multiplier);
                check(prepare_human_bid(observed, multiplier) == player, "observed and silent fixtures bind the same player");
                std::vector<TransferDecisionEvent> events;
                observed.transfer_decision_observer = [&](const TransferDecisionEvent& event) {
                    const auto human_squad=squad_range(observed.manager_teams.at(0));
                    bool owned=false;
                    for(size_t row=human_squad.first; row<human_squad.first+human_squad.count; ++row)
                        if((observed.squad_slots.at(row)&0xfff)==player) owned=true;
                    check(owned==(event.kind==TransferDecisionEventKind::confirmation),
                          "evaluation precedes the transfer and success confirmation follows actual ownership");
                    events.push_back(event); // copy only; presenter cannot mutate the game state
                };
                GameRandom silent_game(0x2300 + seed), observed_game(0x2300 + seed);
                WatcomRandom silent_rand = incoming_offer_rand, observed_rand = incoming_offer_rand;
                MinuteReport silent_report, observed_report;
                transfer_list_countdown(silent, silent_game, silent_rand, silent_report, false,
                                        TransferAllocatorContext::zero);
                transfer_list_countdown(observed, observed_game, observed_rand, observed_report, false,
                                        TransferAllocatorContext::zero);
                check(!silent_report.unsupported && !observed_report.unsupported &&
                      save_stream(silent) == save_stream(observed) &&
                      silent.squad_slots == observed.squad_slots && silent.tactics == observed.tactics &&
                      silent.manager_records == observed.manager_records &&
                      silent_game.seed() == observed_game.seed() && silent_game.calls() == observed_game.calls() &&
                      silent_rand.seed() == observed_rand.seed() && silent_rand.calls() == observed_rand.calls(),
                      "observer on/off leaves saved state, transfer state, and both RNG streams identical");
                check(events.size() == (expected_acceptance ? 2u : 1u) &&
                      !events.empty() && events[0].accepted == expected_acceptance,
                      "automatic decision emits evaluation and only emits confirmation after a successful transfer");
                check(!events.empty() && events[0].kind == TransferDecisionEventKind::evaluation &&
                      events[0].offers.valid && events[0].offers.player == player &&
                      events[0].offers.seller_manager == -1 && events[0].offers.offer_count == 1 &&
                      events[0].selected_bid_index == 0 && events[0].viewer_manager == 0 &&
                      events[0].offers.quoted_years == 1 && events[0].offers.quoted_fee != 0 &&
                      events[0].offers.quoted_monthly_salary != 0 &&
                      events[0].offers.asking_fee != 0 && events[0].offers.asking_monthly_salary != 0 &&
                      events[0].result_text_address == (events[0].accepted ? ::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer3Unsigned : ::bmh::layout::IncomingTransfer_RenderAtElapsed_TextPointer4Unsigned),
                      "observer receives an owned one-year decision snapshot and source outcome text");
                if (expected_acceptance) {
                    check(events.size() == 2 && events[1].kind == TransferDecisionEventKind::confirmation &&
                          events[1].accepted && events[1].selected_bid_index == 0 &&
                          events[1].viewer_manager == 0 && events[1].result_text_address == ::bmh::layout::PendingOffer_FormatError_ExecutableField2Unsigned,
                          "successful human-buyer transfer emits the source confirmation only after the move");
                } else {
                    check(events.size() == 1, "rejected automatic offer has no success confirmation");
                }
            };
            run_observer_case(1, 0x51, false);
            run_observer_case(3, 0x52, true);
            for(bool human_loser : {false,true}) {
                GameState ai_winner=incoming_offer_base;
                prepare_human_bid(ai_winner,3);
                const uint32_t at=::bmh::layout::EditorListUnsigned+uint32_t(buy_entry)*0x58;
                int ai_buyer=-1;
                for(int t=0;t<18;++t) {
                    if(ai_winner.manager_of(t)<0 && squad_count(ai_winner,t,3)<int(squad_range(t).count)-1) {ai_buyer=t;break;}
                }
                check(ai_buyer>=0,"AI-winner fixture has squad room");
                ai_winner.memory.set16(at,uint16_t(ai_buyer));
                if(human_loser) {
                    ai_winner.memory.set16(at+2,uint16_t(ai_winner.manager_teams.at(0)));
                    ai_winner.memory.set16(at+4,0xffff);
                    ai_winner.memory.set32(at+0x20,0); ai_winner.memory.set32(at+0x10,0);
                    ai_winner.memory.set8(at+0x35,12);
                }
                int evaluation_calls=0,confirmation_calls=0;
                ai_winner.transfer_decision_observer=[&](const TransferDecisionEvent& event) {
                    if(event.kind==TransferDecisionEventKind::evaluation)++evaluation_calls;
                    else ++confirmation_calls;
                    check(event.selected_bid_index==0,"AI-winner retains the ranked raw slot");
                };
                GameRandom ai_game(0x2398);WatcomRandom ai_rand=incoming_offer_rand;MinuteReport ai_report;
                transfer_list_countdown(ai_winner,ai_game,ai_rand,ai_report);
                check(!ai_report.unsupported && evaluation_calls==(human_loser?1:0) && confirmation_calls==0,
                      "any human raw bidder sees evaluation even when AI wins; AI-only rows emit no UI events");
            }
        }
        {
            GameState seller_state = transfer_state;
            WatcomRandom seller_rand = transfer_rand;
            GameRandom seller_game(0x4499);
            prepare_ai_offer(seller_state);
            seller_state.transfer_decision = [](GameState&, int, int, int, int32_t, int32_t) { return false; };
            MinuteReport sale_report;
            transfer_list_countdown(seller_state, seller_game, seller_rand, sale_report);
            bool still_owned = false;
            for (size_t row = own_squad.first; row < own_squad.first + own_squad.count; ++row)
                if ((seller_state.squad_slots.at(row) & 0xfff) == own_list_player) still_owned = true;
            check(!sale_report.unsupported && still_owned, "declining a sale offer keeps the manager's player");
        }
        {
            GameState departure_state = transfer_state;
            WatcomRandom departure_rand = transfer_rand;
            GameRandom departure_game(0x5599);
            departure_state.squad_slots.at(0xb70u + 0x28) = uint16_t(own_list_player);
            departure_state.memory.set32(::bmh::layout::OfferSlots, 0x80000000u | (uint32_t(own_list_player) << 10));
            MinuteReport departure_report;
            answer_offers(departure_state, 0, false, departure_rand, departure_report, &departure_game);
            bool stayed = false;
            for (size_t row = own_squad.first; row < own_squad.first + own_squad.count; ++row)
                if ((departure_state.squad_slots.at(row) & 0xfff) == own_list_player) stayed = true;
            check(!departure_report.unsupported && stayed, "declined release-clause salary demand keeps the player");
        }

        const auto item = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
            [](const DeskItem& v) { return v.kind == 14 && !v.disabled; });
        check(item != flow.desktop()->state().items.end(), "bank menu in projected desk");
        const auto open = flow.click(item->x + item->width / 2, item->y + item->height / 2);
        check(flow.screen_name() == "bank" && open.request == FlowRequest::submenu_opened, "bank screen opens from desk");
        flow.click(250, 80); // Original Bank_FinishCredit_RuntimeField/Bank_FinishCredit_RuntimeField3 cashier hitbox, index 3.
        flow.click(80, 178); // Source EINZAHLEN button opens KB/6 calculator.
        const int32_t balance_before = session.state().manager_records.at(0x314) |
            int32_t(session.state().manager_records.at(0x315)) << 8 |
            int32_t(session.state().manager_records.at(0x316)) << 16 |
            int32_t(session.state().manager_records.at(0x317)) << 24;
        check(balance_before > 1000, "career has transferable funds");
        const int32_t amount = std::min<int32_t>(1000, balance_before);
        const int32_t deposit_before = int32_t(uint32_t(session.state().manager_records.at(0x318)) |
            uint32_t(session.state().manager_records.at(0x319)) << 8 | uint32_t(session.state().manager_records.at(0x31a)) << 16 |
            uint32_t(session.state().manager_records.at(0x31b)) << 24);
        flow.text(std::to_string(amount));
        const auto submitted = flow.send_key("ENTER");
        check(submitted.request == FlowRequest::submenu_action && std::holds_alternative<BankRequest>(submitted.submenu_action),
              "deposit emits typed request");
        const auto request = std::get<BankRequest>(submitted.submenu_action);
        check(request.amount == amount && request.revision == revision && request.manager_slot == 0 &&
              request.runtime_club == session.state().manager_teams.at(0), "request binds account context");
        check(session.dispatch_bank(request, revision, error), "controller books fixed deposit");
        flow.acknowledge_preparation(session.preparation_projection(revision));
        const auto rec = session.state().manager_records;
        const int32_t balance_after = int32_t(uint32_t(rec.at(0x314)) | uint32_t(rec.at(0x315)) << 8 |
            uint32_t(rec.at(0x316)) << 16 | uint32_t(rec.at(0x317)) << 24);
        const int32_t deposit_after = int32_t(uint32_t(rec.at(0x318)) | uint32_t(rec.at(0x319)) << 8 |
            uint32_t(rec.at(0x31a)) << 16 | uint32_t(rec.at(0x31b)) << 24);
        check(balance_before - balance_after == amount && deposit_after - deposit_before == amount, "authoritative account transfer returned");
        flow.send_key("BACKSPACE"); flow.send_key("B");
        check(flow.state_json().find("\"exchange_open\":true") != std::string::npos, "bank opens stock exchange locally");
        int stock = -1;
        for (int i = 0; i < kStocks; ++i) {
            const size_t at = size_t(i) * 0x5c;
            const uint16_t price = uint16_t(session.state().stocks.at(at + 0x4e) |
                uint16_t(session.state().stocks.at(at + 0x4f)) << 8);
            if (price != 0 && max_shares_to_buy(session.state(), 0, i) > 0) { stock = i; break; }
        }
        check(stock >= 0, "at least one stock can be bought in the initialized career");
        const size_t holding = size_t(stock) * 0x5c + 0x50;
        const uint16_t held_before = uint16_t(session.state().stocks.at(holding) | uint16_t(session.state().stocks.at(holding + 1)) << 8);
        flow.click(55, 64 + stock * 8); flow.click(80, 178); flow.text("1");
        auto buy = flow.send_key("ENTER");
        check(buy.request == FlowRequest::submenu_action && std::get<BankRequest>(buy.submenu_action).kind == BankRequestKind::buy_shares,
              "stock purchase is an explicit request");
        check(session.dispatch_bank(std::get<BankRequest>(buy.submenu_action), revision, error), "controller buys one share");
        flow.acknowledge_preparation(session.preparation_projection(revision));
        const uint16_t held_after = uint16_t(session.state().stocks.at(holding) | uint16_t(session.state().stocks.at(holding + 1)) << 8);
        check(held_after == held_before + 1, "share count returns from authoritative engine projection");

        const auto calls_before_tip = session.rand().calls();
        auto tip = flow.click(80, 165);
        check(tip.request == FlowRequest::submenu_action && std::get<BankRequest>(tip.submenu_action).kind == BankRequestKind::stock_tip,
              "expert advice is requested only on explicit click");
        check(flow.click(80, 165).request == FlowRequest::none, "duplicate advice click suppressed");
        check(session.dispatch_bank(std::get<BankRequest>(tip.submenu_action), revision, error), "controller executes paid advice");
        const uint64_t tip_draws = session.rand().calls() - calls_before_tip;
        check(tip_draws == 1 || tip_draws == 2, "expert advice uses original conditional Watcom draw count");
        flow.acknowledge_preparation(session.preparation_projection(revision));
        check(flow.state_json().find("\"last_tip\":") != std::string::npos, "returned bank state contains advice result");
        flow.send_key("BACKSPACE"); flow.send_key("I");
        check(flow.state_json().find("\"funds_open\":true") != std::string::npos, "bank opens fund menu locally");
        int fund = -1;
        for (int i = 0; i < kFunds; ++i) {
            if (session.state().memory.u32(::bmh::layout::FundBlock + uint32_t(i) * 0x1c) != 0 && max_fund_units(session.state(), 0, i) > 0) {
                fund = i; break;
            }
        }
        check(fund >= 0, "at least one fund can be bought in the initialized career");
        const uint32_t held_offset = ::bmh::layout::FundBlock + uint32_t(fund) * 0x1c + 0x14;
        const uint16_t fund_before = session.state().memory.u16(held_offset);
        for (int i = 0; i < fund; ++i) flow.click(205, 130);
        flow.click(80, 178); flow.text("1");
        const auto fund_buy = flow.send_key("ENTER");
        check(fund_buy.request == FlowRequest::submenu_action &&
              std::get<BankRequest>(fund_buy.submenu_action).kind == BankRequestKind::buy_fund,
              "fund purchase is an explicit request");
        check(session.dispatch_bank(std::get<BankRequest>(fund_buy.submenu_action), revision, error), "controller buys one fund unit");
        flow.acknowledge_preparation(session.preparation_projection(revision));
        check(session.state().memory.u16(held_offset) == fund_before + 1, "fund unit returns from authoritative projection");
        if (fund > 0) flow.send_key("ENTER"); // Acknowledge the already-booked Source renovation notice.
        flow.click(205, 178); flow.text("1");
        const auto fund_sell = flow.send_key("ENTER");
        check(fund_sell.request == FlowRequest::submenu_action &&
              std::get<BankRequest>(fund_sell.submenu_action).kind == BankRequestKind::sell_fund,
              "fund sale is an explicit request");
        check(session.dispatch_bank(std::get<BankRequest>(fund_sell.submenu_action), revision, error), "controller sells one fund unit");
        flow.acknowledge_preparation(session.preparation_projection(revision));
        check(session.state().memory.u16(held_offset) == fund_before, "fund sale returns authoritative holding");
        check(flow.send_key("ESC").request == FlowRequest::none && flow.screen_name() == "bank",
              "first escape restores the Original bank room");
        check(flow.send_key("ESC").request == FlowRequest::submenu_closed, "second escape returns to desk without extra action");
        const auto stadium_item = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
            [](const DeskItem& v) { return v.kind == 22 && !v.disabled; });
        check(stadium_item != flow.desktop()->state().items.end(), "stadium menu in projected desk");
        const int stadium_x = stadium_item->x + stadium_item->width / 2;
        const int stadium_y = stadium_item->y + stadium_item->height / 2;
        const auto stadium_open = flow.click(stadium_x, stadium_y);
        check(stadium_open.request == FlowRequest::submenu_action &&
              std::get<StadiumRequest>(stadium_open.submenu_action).kind == StadiumRequestKind::open,
              "stadium entry emits original opening action");
        const auto opening_draws = session.game().calls();
        check(session.dispatch_stadium(std::get<StadiumRequest>(stadium_open.submenu_action), revision, error), "controller opens stadium");
        check(session.game().calls() == opening_draws + 1, "stadium entry consumes exactly the opening discount draw");
        flow.acknowledge_preparation(session.preparation_projection(revision));
        check(flow.state_json().find("\"opened\":true") != std::string::npos, "opening price projection returned");
        const auto stadium_click = [&](int x, int y) {
            flow.pointer_down(x,y);
            return flow.click(x,y);
        };
        const auto stadium_apply = [&](const FlowResult& event) {
            check(event.request == FlowRequest::submenu_action && std::holds_alternative<StadiumRequest>(event.submenu_action),
                  "stadium dispatch has an owned typed request");
            check(session.dispatch_stadium(std::get<StadiumRequest>(event.submenu_action), revision, error),
                  "session accepts the source stadium action");
            flow.acknowledge_preparation(session.preparation_projection(revision));
        };
        stadium_click(30,220); // Original button0 switches inside to periphery.
        stadium_click(160,220); // Source button1 enables missing-building selection.
        int object = -1;
        for (int i = 0; i < 14; ++i) {
            const size_t rec_at = size_t(i);
            const uint8_t level = session.state().manager_records.at(rec_at);
            const uint8_t days = session.state().manager_records.at(0x1e + rec_at);
            if (!days && (i == 0 ? level < 0xab : level == 0)) { object = i; break; }
        }
        check(object >= 0, "at least one peripheral construction is selectable");
        const auto before_no = session.state().manager_records;
        const auto draws_before_no = session.rand().calls();
        const auto periphery_exe = bmh::test::load_executable(asset_path(data, "BMH.exe"));
        const int object_x = (periphery_exe.u16(::bmh::layout::Stadium_PeripheryObjectContains_ExecutableField + object * 8) + periphery_exe.u16(::bmh::layout::Stadium_RenderPanorama_RuntimeField2 + object * 8)) / 2;
        const int object_y = (periphery_exe.u16(::bmh::layout::Stadium_RenderPanorama_RuntimeField + object * 8) + periphery_exe.u16(::bmh::layout::Stadium_RenderPanorama_RuntimeField3 + object * 8)) / 2;
        const auto quote = stadium_click(object_x, object_y);
        check(quote.request == FlowRequest::submenu_action &&
              std::get<StadiumRequest>(quote.submenu_action).kind == StadiumRequestKind::quote_periphery,
              "periphery selection requests real offer");
        const auto offer_draws = session.game().calls();
        check(session.dispatch_stadium(std::get<StadiumRequest>(quote.submenu_action), revision, error), "controller quotes periphery");
        check(session.game().calls() == offer_draws + uint64_t(object + 1), "periphery quote consumes skipped entries then one price draw");
        flow.acknowledge_preparation(session.preparation_projection(revision));
        check(flow.state_json().find("\"quoted_object\":" + std::to_string(object)) != std::string::npos,
              "offer projection returned");
        const auto game_draws_before_decline = session.game().calls();
        const auto decline = stadium_click(200, 170);
        check(decline.request == FlowRequest::submenu_action &&
              std::get<StadiumRequest>(decline.submenu_action).kind == StadiumRequestKind::decline_periphery,
              "NEIN emits a drawless offer-resolution request");
        check(session.dispatch_stadium(std::get<StadiumRequest>(decline.submenu_action), revision, error), "controller declines offer");
        flow.acknowledge_preparation(session.preparation_projection(revision));
        check(session.state().manager_records == before_no && session.rand().calls() == draws_before_no &&
              session.game().calls() == game_draws_before_decline,
              "declining offer leaves engine state and RNG untouched");
        const auto quote_again = stadium_click(object_x, object_y);
        check(session.dispatch_stadium(std::get<StadiumRequest>(quote_again.submenu_action), revision, error), "second quote accepted");
        flow.acknowledge_preparation(session.preparation_projection(revision));
        const auto build = stadium_click(100, 170);
        check(build.request == FlowRequest::submenu_action &&
              std::get<StadiumRequest>(build.submenu_action).kind == StadiumRequestKind::build_periphery,
              "JA emits construction request");
        check(session.dispatch_stadium(std::get<StadiumRequest>(build.submenu_action), revision, error), "controller books construction");
        flow.acknowledge_preparation(session.preparation_projection(revision));
        check(session.state().manager_records.at(0x1e + size_t(object)) != 0, "authoritative construction countdown returned");
        stadium_apply(flow.send_key("ESC"));
        check(flow.screen_name() == "desktop", "stadium closes after the owned ACK");
        const auto stadium_reopen = flow.click(stadium_x, stadium_y);
        check(stadium_reopen.request == FlowRequest::submenu_action, "stadium may be opened again");
        const auto repeat_open_draws = session.game().calls();
        check(session.dispatch_stadium(std::get<StadiumRequest>(stadium_reopen.submenu_action), revision, error),
              "repeat opening refreshes the quote context");
        check(session.game().calls() == repeat_open_draws + 1, "repeat entry consumes one opening discount draw");
        flow.acknowledge_preparation(session.preparation_projection(revision));
        const auto facilities = session.preparation_projection(revision).stadium;
        check(facilities.has_value(), "real facility projection is available after reopening");
        int facility = -1;
        for (int choice = 0; choice < 8; ++choice) {
            const auto& row = facilities->investments[size_t(choice)];
            if (row.available && !row.options.empty() &&
                uint64_t(row.options.front().base_price) <= uint64_t(std::max<int32_t>(0,facilities->balance))) {
                facility = choice; break;
            }
        }
        check(facility >= 0, "at least one Original facility tier is payable");
        const auto text_draws = session.game().calls();
        stadium_apply(stadium_click(10 + facility % 4 * 54,160 + facility / 4 * 30));
        check(session.game().calls() == text_draws + 1, "facility entry draws one Source G header");
        check(!flow.stadium()->state().selected_option && stadium_click(10,140).request == FlowRequest::none,
              "facility entry has no invented default tier");
        stadium_apply(stadium_click(10,80));
        const auto selected = flow.stadium()->state().investments[size_t(facility)].options.front();
        auto expected = session.state(); auto expected_w = session.rand();
        auto offer = prepare_stand_block_offer(selected.base_price,selected.base_weeks,expected_w);
        stadium_apply(stadium_click(10,140));
        check(session.rand().seed() == expected_w.seed() && session.rand().calls() == expected_w.calls(),
              "Ausbauen prepares exactly one Source W tip quote");
        resolve_stand_block_offer(offer,false,expected_w);
        StadiumInvestmentConstants constants;
        const auto stadium_exe = bmh::test::load_executable(asset_path(data, "BMH.exe"));
        for(unsigned c=0;c<8;++c) {
            constants.price[c] = stadium_exe.u32(::bmh::layout::CareerSession_DispatchStadium_RuntimeField+c*4);
            constants.weeks[c] = stadium_exe.u8(::bmh::layout::CareerSession_DispatchStadium_RuntimeField2+(14+c)*5);
        }
        check(build_stadium_investment_source(expected,0,static_cast<StadiumInvestment>(facility),
              facilities->construction_percent,constants,selected,offer,expected_w), "Source base-price construction is payable");
        stadium_apply(stadium_click(180,170)); // No declines the tip, not construction.
        check(save_stream(session.state()) == save_stream(expected) &&
              session.rand().seed() == expected_w.seed() && session.rand().calls() == expected_w.calls(),
              "real App tip No pays and starts exactly the expected facility construction");
        int cancellable = -1;
        for(int c=0;c<8;++c)if(flow.stadium()->state().investments[size_t(c)].available){cancellable=c;break;}
        check(cancellable >= 0, "another Original facility remains for initial Abbruch");
        stadium_apply(stadium_click(10+cancellable%4*54,160+cancellable/4*30));
        const auto cancel_native=save_stream(session.state()); const auto cancel_w=session.rand();
        stadium_apply(stadium_click(110,140));
        check(save_stream(session.state()) == cancel_native && session.rand().calls() == cancel_w.calls(),
              "initial Abbruch performs no payment or W tip draw");
        stadium_apply(flow.send_key("ESC"));
        check(flow.screen_name() == "desktop", "reopened stadium closes only after the session ACK");

        auto& game = session.state();
        check(game.memory.has(::bmh::layout::Coupon + 0x13) && game.memory.has(::bmh::layout::DeskActions_SubmitPoolsCoupon_RuntimeField + 43) && game.memory.has(::bmh::layout::CareerSession_DispatchToto_RuntimeField),
              "original coupon storage exists in the current game state");
        game.memory.set8(::bmh::layout::CareerSession_DispatchToto_RuntimeField, 11);
        for (int i = 0; i < 11; ++i) {
            game.memory.set16(::bmh::layout::DeskActions_SubmitPoolsCoupon_RuntimeField + uint32_t(i) * 4, uint16_t(i * 2));
            game.memory.set16(::bmh::layout::DeskActions_SubmitPoolsCoupon_RuntimeField + uint32_t(i) * 4 + 2, uint16_t(i * 2 + 1));
        }
        const uint32_t coupon = ::bmh::layout::Coupon;
        game.memory.set32(coupon, 0);
        flow.attach_preparation_context(session.preparation_projection(revision));
        const auto toto_item = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
            [](const DeskItem& v) { return v.kind == 13 && !v.disabled; });
        check(toto_item != flow.desktop()->state().items.end(), "toto menu in projected desk");
        const int toto_x = toto_item->x + toto_item->width / 2, toto_y = toto_item->y + toto_item->height / 2;
        auto toto_open = flow.click(toto_x, toto_y);
        check(flow.screen_name() == "toto" && toto_open.request == FlowRequest::submenu_opened,
              "Toto coupon opens from the desk");
        flow.click(260, 25);
        check(game.memory.u32(coupon) == 0 && game.memory.u8(coupon + 8) == 0,
              "ESC discards local picks without engine writes");
        check(flow.send_key("ESC").request == FlowRequest::submenu_closed, "cancelled Toto dialog returns to desk");

        toto_open = flow.click(toto_x, toto_y);
        check(flow.screen_name() == "toto", "Toto dialog can be reopened");
        flow.click(280, 25); // Source middle column: draw (stored 0).
        const auto toto_underlay = flow.render();
        flow.click(100, 200);
        uint64_t toto_units = 100;
        const auto toto_present = [&] { (void)flow.render(); return flow.after_toto_present(toto_units); };
        const auto toto_key = [&](std::string_view value) {
            flow.send_key(value); toto_present(); toto_units += 4;
            flow.tick_toto_source_units(toto_units); toto_present(); toto_present();
        };
        toto_present();
        for (char digit : std::string("20000")) toto_key(std::string(1, digit));
        flow.send_key("ENTER"); // SDL handle also redraws keys whose numeric value stays unchanged.
        toto_present();
        toto_units += 3;
        check(!flow.tick_toto_source_units(toto_units).changed && flow.in_toto_calculator(),
              "Toto accept stays modal until the full Source four-unit wait");
        ++toto_units;
        check(flow.tick_toto_source_units(toto_units).changed,
              "Toto accept requests its final modal redraw without further input");
        check(toto_present().changed && flow.in_toto_calculator(),
              "presented final modal requests the restored-underlay exit frame");
        check(flow.render().pixels == toto_underlay.pixels,
              "Toto exit Present restores the original coupon before applying the new amount");
        check(flow.after_toto_present(toto_units).changed && !flow.in_toto_calculator(),
              "fresh exit Present ACK applies Toto amount without a mouse or keyboard wake-up");
        check(flow.state_json().find("\"local_stake\":20000") != std::string::npos &&
              flow.render().pixels != toto_underlay.pixels && game.memory.u32(coupon) == 0 &&
              !flow.toto()->pending(),
              "exit ACK requests the 20,000 coupon preview while authoritative coupon stays untouched");
        flow.click(100, 200);
        toto_present();
        for (char digit : std::string("50000")) toto_key(std::string(1, digit));
        toto_key("ENTER");
        check(!flow.in_toto_calculator(), "Toto amount commits after source wait and exit Present");
        const auto toto_submit = flow.send_key("ENTER");
        check(toto_submit.request == FlowRequest::submenu_action &&
              std::holds_alternative<TotoRequest>(toto_submit.submenu_action), "Tipp abgeben emits typed request");
        const auto toto_request = std::get<TotoRequest>(toto_submit.submenu_action);
        check(toto_request.manager_slot == 0 && toto_request.runtime_club == game.manager_teams.at(0) &&
              toto_request.revision == revision && toto_request.stake == 50000 && toto_request.tips[0] == 0,
              "coupon request binds manager, revision, stake and picks");
        check(preparation_request_json(toto_submit.submenu_action).find("\"kind\":\"submit\"") != std::string::npos,
              "Toto submission is serializable in the UI JSONL contract");
        const int32_t toto_balance_before = session.state().manager_records.at(0x314) |
            int32_t(session.state().manager_records.at(0x315)) << 8 |
            int32_t(session.state().manager_records.at(0x316)) << 16 |
            int32_t(session.state().manager_records.at(0x317)) << 24;
        auto stale_toto = toto_request; ++stale_toto.revision;
        check(!session.dispatch_toto(stale_toto, revision, error) && error == "stale_manager_or_revision",
              "stale Toto request is rejected before coupon mutation");
        check(session.state().memory.u32(coupon) == 0 && session.state().memory.u8(coupon + 8) == 0,
              "stale Toto request preserves coupon bytes");
        check(session.dispatch_toto(toto_request, revision, error), "controller submits Toto coupon");
        check(session.state().memory.u32(coupon) == 50000 && session.state().memory.u8(coupon + 8) == 0,
              "stake and draw are persisted in original coupon bytes");
        check((session.state().manager_records.at(0x314) |
              int32_t(session.state().manager_records.at(0x315)) << 8 |
              int32_t(session.state().manager_records.at(0x316)) << 16 |
              int32_t(session.state().manager_records.at(0x317)) << 24) == toto_balance_before,
              "stake is charged only later during matchday settlement");
        flow.acknowledge_preparation(session.preparation_projection(revision));
        check(flow.state_json().find("\"feedback\":\"TIPP ABGEGEBEN\"") != std::string::npos,
              "accepted coupon returns authoritative state and confirmation");
        check(flow.send_key("ESC").request == FlowRequest::submenu_closed, "Toto returns to desk after submission");
        flow.click(toto_x, toto_y);
        check(flow.process_timer_expired(10, 10).request == FlowRequest::submenu_closed,
              "unchanged available Toto deadline returns without a request");
        flow.click(toto_x, toto_y);
        flow.click(295, 25);
        const auto right_submit = flow.process_timer_expired(10, 10);
        check(right_submit.request == FlowRequest::submenu_action && flow.screen_name() == "toto",
              "changed Toto deadline waits for authoritative coupon ACK");
        const auto pending_toto = flow.state_json();
        check(!flow.process_timer_expired(10,10).changed && flow.state_json()==pending_toto,
              "repeated deadline preserves the exact pending coupon rather than resubmitting it");
        check(session.dispatch_toto(std::get<TotoRequest>(right_submit.submenu_action), revision, error),
              "original right-click coupon submits through the normal controller action");
        flow.acknowledge_preparation(session.preparation_projection(revision));
        check(flow.screen_name() == "desktop" && session.state().memory.u8(coupon + 8) == 2,
              "right-click ACK saves the away pick and returns to the desk");

        const auto kickoff_item = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
            [](const DeskItem& v) { return v.kind == 12 && !v.disabled; });
        check(kickoff_item != flow.desktop()->state().items.end(), "kickoff icon in projected desk");
        const auto game_seed_before_kickoff = session.game().seed();
        const auto game_draws_before_kickoff = session.game().calls();
        const auto watcom_seed_before_kickoff = session.rand().seed();
        const auto watcom_draws_before_kickoff = session.rand().calls();
        const auto date_before_kickoff = game.date;
        const auto kickoff = flow.click(kickoff_item->x + kickoff_item->width / 2,
                                        kickoff_item->y + kickoff_item->height / 2);
        check(kickoff.request == FlowRequest::kickoff && kickoff.desk_action.kind == DeskRequestKind::advance_turn,
              "kickoff emits its own flow request and the typed desk request");
        check(kickoff.desk_action.manager_slot == game.memory.u8(::bmh::layout::ActiveManager) && kickoff.desk_action.revision == revision,
              "kickoff request binds current manager and revision");
        check(desk_request_json(kickoff.desk_action).find("\"kind\":\"advance_turn\"") != std::string::npos,
              "kickoff request is serialized for JSONL/app adapters");
        const auto duplicate_kickoff = flow.click(kickoff_item->x + kickoff_item->width / 2,
                                                  kickoff_item->y + kickoff_item->height / 2);
        check(duplicate_kickoff.request == FlowRequest::none, "a pending kickoff cannot be submitted twice");
        check(game.date == date_before_kickoff && session.game().seed() == game_seed_before_kickoff &&
              session.game().calls() == game_draws_before_kickoff && session.rand().seed() == watcom_seed_before_kickoff &&
              session.rand().calls() == watcom_draws_before_kickoff, "UI kickoff request and duplicate draw no engine state or RNG");
        const auto advanced = session.dispatch_kickoff(kickoff.desk_action, revision, error);
        check(advanced.kind == TurnDeskKind::NextDesk && advanced.date > date_before_kickoff &&
              advanced.manager_slot == int(game.memory.u8(::bmh::layout::ActiveManager)), "kickoff drives the engine to the next manager desk");
        int kickoff_cues = 0, goal_cues = 0, full_time_cues = 0;
        for (size_t i = 0; i < advanced.audio_events.size(); ++i) {
            check(advanced.audio_events[i].session_id == advanced.audio_events.front().session_id &&
                  advanced.audio_events[i].event_id == advanced.audio_events.front().event_id + i &&
                  !advanced.audio_events[i].original_id, "audio events have monotone identity and no guessed original cue");
            kickoff_cues += advanced.audio_events[i].kind == TurnAudioKind::kickoff_whistle;
            goal_cues += advanced.audio_events[i].kind == TurnAudioKind::goal;
            full_time_cues += advanced.audio_events[i].kind == TurnAudioKind::full_time_whistle;
        }
        check(kickoff_cues <= 1 && goal_cues <= 1 && full_time_cues <= 1,
              "synchronous matchday audio is bounded to one cue per semantic kind");
        const auto& last_results = session.last_turn_results();
        check(last_results.valid, "completed matchday results are marked as retained");
        check(last_results.date == date_before_kickoff,
              "retained matchday result keeps the played date after the next desk advances");
        bool manager_fixture = false, manager_goal = false;
        for (const MatchGroup& group : last_results.groups)
            for (int m = 0; m < group.matches(); ++m) {
                if (group.void_mask() >> m & 1) continue;
                const bool managed = game.manager_of(group.fixtures[size_t(m * 2)]) >= 0 ||
                                     game.manager_of(group.fixtures[size_t(m * 2 + 1)]) >= 0;
                manager_fixture = manager_fixture || managed;
                int home = 0, away = 0;
                if (managed && match_score(group, m, home, away) && (home > 0 || away > 0)) manager_goal = true;
            }
        check(kickoff_cues == int(manager_fixture) && full_time_cues == int(manager_fixture) &&
              goal_cues == int(manager_goal),
              "only manager fixtures emit matchday cues, with one goal summary when one scored");
        check(game.date == advanced.date && revision == advanced.revision && revision > kickoff.desk_action.revision,
              "accepted kickoff advances authoritative date and revision");
        auto stale_kickoff = kickoff.desk_action;
        const auto stale_game_seed = session.game().seed();
        const auto stale_game_calls = session.game().calls();
        const auto stale_watcom_seed = session.rand().seed();
        const auto stale_watcom_calls = session.rand().calls();
        const auto stale_date = session.state().date;
        const auto rejected = session.dispatch_kickoff(stale_kickoff, revision, error);
        check(rejected.kind == TurnDeskKind::Failed && error == "stale_manager_or_revision",
              "duplicate old kickoff revision is rejected");
        check(session.state().date == stale_date && session.game().seed() == stale_game_seed &&
              session.game().calls() == stale_game_calls && session.rand().seed() == stale_watcom_seed &&
              session.rand().calls() == stale_watcom_calls, "stale kickoff has no state or RNG side effects");
        flow.acknowledge_desk(session.desktop_projection(revision));
        flow.attach_preparation_context(session.preparation_projection(revision));

        auto find_menu = [&](int kind) {
            return std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
                [kind](const DeskItem& v) { return v.kind == kind && !v.disabled; });
        };
        auto save_item = find_menu(11);
        check(save_item != flow.desktop()->state().items.end(), "save menu in projected desk");
        auto save_open = flow.click(save_item->x + save_item->width / 2, save_item->y + save_item->height / 2);
        check(save_open.request == FlowRequest::submenu_opened && flow.screen_name() == "save_load",
              "save menu opens a local file dialog");
        check(flow.state_json().find("IGNORE.TXT") == std::string::npos,
              "file browser lists only regular .FBM saves");
        check(!flow.text("IGNORED"), "typing without the Original filename field focus is inert");
        flow.click(150, 214); check(flow.text("TESTSAVE"), "Original filename field accepts DOS-safe text");
        const auto save_action = flow.send_key("ENTER");
        check(save_action.request == FlowRequest::file_action && save_action.file_action.action == SaveLoadAction::save &&
              save_action.file_action.manager_slot == int(game.memory.u8(::bmh::layout::ActiveManager)) && save_action.file_action.revision == revision &&
              save_action.file_action.path.filename() == "TESTSAVE.FBM" && !save_action.file_action.overwrite,
              "save request binds active manager, revision and a new filename");
        const auto save_draws = session.rand().calls();
        check(session.save_game(save_action.file_action.path, save_action.file_action.manager_slot,
                                save_action.file_action.overwrite, error), "controller writes the selected temporary save");
        check(session.rand().calls() == save_draws + 4 && std::filesystem::exists(save_action.file_action.path),
              "successful save performs exactly four Watcom draws and writes only to the temporary directory");
        ++revision;
        flow.acknowledge_saved_game(session.desktop_projection(revision), session.preparation_projection(revision));
        check(flow.screen_name() == "desktop" && flow.desktop()->state().revision == revision,
              "saved-game acknowledgement refreshes the desk projection");

        save_item = find_menu(11);
        save_open = flow.click(save_item->x + save_item->width / 2, save_item->y + save_item->height / 2);
        check(save_open.request == FlowRequest::submenu_opened, "save browser reopens for overwrite path");
        flow.click(150, 214);
        check(flow.send_key("ENTER").request == FlowRequest::none, "existing filename needs explicit overwrite confirmation");
        const auto overwrite_action = flow.send_key("ENTER");
        check(overwrite_action.request == FlowRequest::file_action && overwrite_action.file_action.overwrite,
              "confirmed overwrite is explicit in the save request");
        const auto overwrite_draws = session.rand().calls();
        check(session.save_game(overwrite_action.file_action.path, overwrite_action.file_action.manager_slot,
                                overwrite_action.file_action.overwrite, error), "controller overwrites selected temporary save");
        check(session.rand().calls() == overwrite_draws + 4, "overwrite consumes the original four save draws once");
        ++revision;
        flow.acknowledge_saved_game(session.desktop_projection(revision), session.preparation_projection(revision));

        auto load_item = find_menu(10);
        check(load_item != flow.desktop()->state().items.end(), "load menu in projected desk");
        auto load_open = flow.click(load_item->x + load_item->width / 2, load_item->y + load_item->height / 2);
        check(load_open.request == FlowRequest::submenu_opened && flow.screen_name() == "save_load",
              "load menu opens the temporary-save list");
        const auto load_action = flow.send_key("ENTER");
        check(load_action.request == FlowRequest::file_action && load_action.file_action.action == SaveLoadAction::load &&
              load_action.file_action.manager_slot == int(game.memory.u8(::bmh::layout::ActiveManager)) && load_action.file_action.revision == revision,
              "selected save emits a manager-/revision-bound load request");
        auto loaded = std::make_unique<app::CareerSession>(data, 0x1199, load_action.file_action.path);
        const auto loaded_revision = revision + 1;
        flow.attach_loaded_game(loaded->desktop_projection(loaded_revision), loaded->preparation_projection(loaded_revision));
        session = std::move(*loaded); revision = loaded_revision;
        check(flow.screen_name() == "desktop" && flow.desktop()->state().source == "engine_projection" &&
              flow.desktop()->state().revision == revision && session.state().date == game.date,
              "loaded state replaces the session and is projected without setup defaults");
        {
            app::CareerSession multi(data, 0x22aa7712);
            SetupOptions options = flow.options();
            options.managers = 2;
            check(multi.confirm_manager(options, first_manager_choice, error), "first multi-manager choice initialized");
            const auto remapped = multi.selection_context(1);
            const auto other = std::find_if(remapped.club_order.begin(), remapped.club_order.end(),
                [&](int master) { return master != first_manager_choice.master_club; });
            check(other != remapped.club_order.end(), "second manager has a distinct club choice");
            ManagerChoice second = first_manager_choice;
            second.slot = 1;
            second.master_club = *other;
            second.name += " B";
            check(multi.confirm_manager(options, second, error), "second multi-manager choice initialized");
            career_managers_end(multi.state(), false);
            MinuteReport multi_report;
            check(career_season_setup(multi.state(), multi.game(), multi.rand(), multi_report),
                  "two-manager season setup completes");
            check(career_first_steps(multi.state(), multi.game(), multi.rand(), multi_report),
                  "two-manager first steps complete");
            auto refs = load_database(data / "M3D.MAN").raw_referee_properties;
            MatchdayContext context;
            context.month = month_code(multi.state().date);
            context.season_day = multi.state().season_day();
            context.weekday = weekday_code(multi.state().date);
            context.referees = &refs;
            const uint16_t entry = multi.state().memory.u16(::bmh::layout::CompetitionCalendar + uint32_t(context.season_day) * 2);
            auto groups = prepare_event(multi.state(), entry, context, multi.game(), multi.rand(), multi_report);
            check(!multi_report.unsupported,
                  (std::string("two-manager event setup: ") + multi_report.reason).c_str());
            (void)enter_event_desk(multi.state(), 0, groups, multi.game(), multi.rand(), multi_report);
            check(!multi_report.unsupported,
                  (std::string("two-manager first desk: ") + multi_report.reason).c_str());
            TurnDriver driver;
            const uint64_t driver_revision = 1;
            check(begin_turn_driver(driver, multi.state(), std::move(groups), entry, refs,
                                    multi.session_id(), driver_revision, error),
                  "two-manager turn driver binds prepared event");
            uint64_t multi_revision = driver_revision;
            const uint32_t event_date = multi.state().date;
            const int first_slot = int(multi.state().memory.u8(::bmh::layout::ActiveManager));
            const auto next_manager = advance_to_next_desk(driver, multi.state(),
                TurnAdvanceRequest{first_slot, multi_revision}, multi.game(), multi.rand(), multi_report);
            check(next_manager.kind == TurnDeskKind::NextDesk && next_manager.date == event_date &&
                  next_manager.manager_slot != first_slot,
                  "first manager kickoff opens the next manager desk on the same event date");
            multi_revision = next_manager.revision;
            const auto game_seed = multi.game().seed();
            const auto watcom_seed = multi.rand().seed();
            const auto game_calls = multi.game().calls(), watcom_calls = multi.rand().calls();
            const auto stale_revision_result = advance_to_next_desk(driver, multi.state(),
                TurnAdvanceRequest{next_manager.manager_slot, driver_revision}, multi.game(), multi.rand(), multi_report);
            check(stale_revision_result.kind == TurnDeskKind::Failed && multi.state().date == event_date &&
                  multi.game().seed() == game_seed && multi.rand().seed() == watcom_seed &&
                  multi.game().calls() == game_calls && multi.rand().calls() == watcom_calls,
                  "stale revision for the current manager is rejected without RNG or date effects");
            const auto stale_result = advance_to_next_desk(driver, multi.state(),
                TurnAdvanceRequest{first_slot, multi_revision}, multi.game(), multi.rand(), multi_report);
            check(stale_result.kind == TurnDeskKind::Failed && multi.state().date == event_date &&
                  multi.game().seed() == game_seed && multi.rand().seed() == watcom_seed &&
                  multi.game().calls() == game_calls && multi.rand().calls() == watcom_calls,
                  "wrong-manager multi-manager kickoff is rejected without RNG or date effects");
            const int final_slot = int(multi.state().memory.u8(::bmh::layout::ActiveManager));
            const auto after_matchday = advance_to_next_desk(driver, multi.state(),
                TurnAdvanceRequest{final_slot, multi_revision}, multi.game(), multi.rand(), multi_report);
            check(after_matchday.kind == TurnDeskKind::NextDesk && after_matchday.date > event_date &&
                  driver.last_results.valid && driver.last_results.date == event_date,
                  "final manager kickoff alone resolves the event and preserves its result snapshot");
        }
        {
            app::CareerSession four(data, 0x22aa7714);
            SetupOptions options = flow.options();
            options.managers = 4;
            std::vector<int> selected;
            ManagerChoice choice = first_manager_choice;
            check(four.confirm_manager(options, choice, error), "four-manager first career choice");
            selected.push_back(choice.master_club);
            for (int slot = 1; slot < 4; ++slot) {
                const auto context = four.selection_context(uint64_t(slot));
                const auto next = std::find_if(context.club_order.begin(), context.club_order.end(),
                    [&](int master) { return std::find(selected.begin(), selected.end(), master) == selected.end(); });
                check(next != context.club_order.end(), "four-manager choice is distinct");
                choice = first_manager_choice;
                choice.slot = slot;
                choice.master_club = *next;
                choice.name += " M";
                check(four.confirm_manager(options, choice, error), "four-manager career choice");
                selected.push_back(*next);
            }
            CareerInitializationResult initialized;
            check(four.finish(initialized, error), (std::string("four-manager finish: ") + error).c_str());
            uint64_t rev = initialized.desktop.revision;
            const uint32_t event_date = four.state().date;
            for (int desk = 0; desk < 4; ++desk) {
                const int slot = int(four.state().memory.u8(::bmh::layout::ActiveManager));
                const auto result = four.dispatch_kickoff(
                    DeskRequest{DeskRequestKind::advance_turn, slot, -1, -1, -1, rev}, rev, error);
                check(result.kind == TurnDeskKind::NextDesk, "four-manager CareerSession accepts current desk ACK");
                if (desk < 3)
                    check(result.date == event_date, "four-manager intermediate desks keep the event date");
                else
                    check(result.date > event_date && four.last_turn_results().valid &&
                          four.last_turn_results().date == event_date,
                          "only the fourth manager ACK resolves and retains the matchday");
            }
        }
        {
            // Core's source path narrows the result-array offset to uint8_t before
            // indexing. A valid 16-team, two-leg group at played=16 wraps 8*16*2 to 0.
            GameState cup_state;
            cup_state.memory.add_block(0, ::bmh::layout::FixtureBankMemoryImageSize);
            cup_state.team_flags.resize(0x200);
            StaticMemory& mem = cup_state.memory;
            const uint32_t entry = cup_entry_address(0);
            constexpr uint32_t teams = ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField24;
            constexpr uint32_t first_leg = ::bmh::layout::SyntheticCupFirstLegBegin;
            constexpr uint32_t second_leg = ::bmh::layout::SyntheticCupSecondLegBegin;
            mem.set_pointer(entry, teams);
            mem.set_pointer(entry + 8, ::bmh::layout::CareerSession_RuntimeError_ExecutableField3);  // original sixteen-team round strings
            mem.set_pointer(entry + 0x0c, first_leg);
            mem.set_pointer(entry + 0x10, second_leg);
            mem.set8(entry + 0x14, 1);  // group stage, two legs
            mem.set16(entry + 0x18, 0);
            mem.set8(entry + 0x1a, 16);
            mem.set8(entry + 0x1b, 30);  // 15 rounds per leg
            mem.set8(entry + 0x1d, 16);  // active leg 2; uint8_t(8 * 16 * 2) == 0
            mem.set8(entry + 0x1e, 1);
            for (int i = 0; i < 16; ++i) mem.set16(teams + uint32_t(i) * 2, uint16_t(i));
            for (int i = 0; i < 16; ++i) {
                mem.set16(first_leg + uint32_t(i) * 2, uint16_t(0x120 + i));
                mem.set16(second_leg + uint32_t(i) * 2, uint16_t(0x220 + i));
            }
            std::vector<MatchGroup> cup_groups;
            MinuteReport cup_report;
            setup_cup_groups(cup_state, 0, cup_groups, cup_report);
            check(!cup_report.unsupported && cup_groups.size() == 1 &&
                  cup_groups[0].fixtures.size() == 16 && cup_groups[0].cup_results.size() == 8 &&
                  cup_groups[0].cup_other_leg.size() == 8,
                  "Core accepts a valid two-leg cup group at the uint8 result-offset wrap");
            check(cup_groups[0].cup_results.front() == 0x120 &&
                  cup_groups[0].cup_other_leg.front() == 0x220,
                  "Core cup result arrays use wrapped byte offset zero for the active leg");
        }
        if (const char* regression_dir = std::getenv("BMH_CUP_REGRESSION_DIR")) {
            const std::filesystem::path saves(regression_dir);
            if (std::filesystem::exists(saves / "BACKUP.FBM")) {
                const auto backup = saves / "BACKUP.FBM";
                const Savegame stored = load_savegame(backup);
                app::CareerSession loaded(data, 0x22aa7729, backup);
                const StaticMemory& mem = loaded.state().memory;
                check(mem.pointer(::bmh::layout::CupTable) == ::bmh::layout::Field && mem.pointer(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField11) == ::bmh::layout::ReplayFixtures &&
                      mem.pointer(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField12) == ::bmh::layout::ReplayResults && mem.pointer(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField13) == ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField28,
                      "BACKUP rebinds competition-table pointers as the original load path does");
                check(loaded.game().calls() == 0 && loaded.rand().calls() == 0,
                      "BACKUP competition-pointer rebind consumes no random draws");
                uint64_t revision = 1;
                const int active = int(mem.u8(::bmh::layout::ActiveManager));
                const auto resumed = loaded.dispatch_kickoff(
                    DeskRequest{DeskRequestKind::advance_turn, active, -1, -1, -1, revision}, revision, error);
                check(resumed.kind != TurnDeskKind::Failed,
                      (std::string("BACKUP resumes without stale competition pointers: ") + error).c_str());
                check(load_savegame(backup).stream == stored.stream,
                      "BACKUP source file remains unchanged by pointer rebinding and kickoff");
            }
            if (const char* stress_dir = std::getenv("BMH_CUP_STRESS_DIR")) {
                struct SavedCupAnchor { const char* file; int cup; int entry; uint8_t desc_flags; };
                static constexpr SavedCupAnchor anchors[] = {
                    {"event-028.FBM", 8, 23, 0x0c},
                    {"event-152.FBM", 8, 23, 0x0c},
                    {"event-020.FBM", 0, 0, 0x2c},
                    {"event-144.FBM", 0, 0, 0x2c},
                };
                for (size_t n = 0; n < std::size(anchors); ++n) {
                    const SavedCupAnchor& anchor = anchors[n];
                    const auto path = std::filesystem::path(stress_dir) / anchor.file;
                    app::CareerSession loaded(data, uint32_t(0x22aa7730 + n), path);
                    const StaticMemory& mem = loaded.state().memory;
                    const uint32_t entry = cup_entry_address(anchor.entry);
                    check(find_cup(mem, anchor.cup) == anchor.entry &&
                          int16_t(mem.u16(entry + 0x18)) == anchor.cup &&
                          (mem.u8(entry + 0x14) & 4) &&
                          mem.u8(entry + 0x1a) == (anchor.cup == 8 ? 2 : 64),
                          (std::string(anchor.file) + " identifies the captured active knockout stage").c_str());
                    bool descriptor_matches = false;
                    for (int group = 0; group < 6; ++group) {
                        const uint32_t desc = ::bmh::layout::MatchdayGroupDescriptors + uint32_t(group) * 0xd0;
                        if (mem.u8(desc) == anchor.cup + 1 &&
                            mem.u32(desc + 0xcc) == entry + kGuestDataBase) {
                            descriptor_matches = mem.u8(desc + 1) == anchor.desc_flags && mem.u8(desc + 2) == 1;
                            break;
                        }
                    }
                    check(descriptor_matches,
                          (std::string(anchor.file) + " preserves the source-built final/Type-8 descriptor flags").c_str());
                    const Savegame stored = load_savegame(path);
                    check(save_stream(loaded.state()) == stored.stream && loaded.game().calls() == 0 &&
                          loaded.rand().calls() == 0,
                          (std::string(anchor.file) + " loads byte-exactly without RNG or event preparation").c_str());
                    const int slot = int(mem.u8(::bmh::layout::ActiveManager));
                    uint64_t revision = 1;
                    const auto resumed = loaded.dispatch_kickoff(
                        DeskRequest{DeskRequestKind::advance_turn, slot, -1, -1, -1, revision}, revision, error);
                    check(resumed.kind != TurnDeskKind::Failed,
                          (std::string(anchor.file) + " resumes its special cup round: " + error).c_str());
                }
            }
            for (const char* name : {"2PNWK060.FBM", "NATWK001.FBM", "NATWK008.FBM", "NATWK012.FBM"}) {
                const auto path = saves / name;
                constexpr uint32_t watcom_seed = 0x22aa7750;
                app::CareerSession loaded(data, watcom_seed, path);
                const GameState& state = loaded.state();
                const int active = int(state.memory.u8(::bmh::layout::ActiveManager));
                const uint32_t entry_at = ::bmh::layout::CompetitionCalendar + uint32_t(state.season_day()) * 2;
                check(state.memory.u16(entry_at) == 0x4006 &&
                      day_competitions(state, state.memory.u16(entry_at)) == 0x800000 &&
                      state.memory.u32(::bmh::layout::SavedCompetition) == 0,
                      (std::string(name) + " has the source-defined no-fixture national-team marker").c_str());
                const Savegame stored = load_savegame(path);
                const Bytes rebound = save_stream(state);
                bool canonical_stream = rebound == stored.stream;
                if (!canonical_stream && state.memory.u8(::bmh::layout::CoachActive) != 0 && rebound.size() == stored.stream.size()) {
                    SaveState original_state(stored.stream);
                    const size_t coach_byte = original_state.block(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField3).offset;
                    canonical_stream = rebound[coach_byte] == state.memory.u8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField3);
                    for (size_t i = 0; canonical_stream && i < rebound.size(); ++i)
                        if (i != coach_byte && rebound[i] != stored.stream[i]) canonical_stream = false;
                }
                check(canonical_stream && loaded.game().calls() == 0 &&
                      loaded.rand().calls() == 0 && loaded.rand().seed() == watcom_seed,
                      (std::string(name) + " binds with only the documented national-coach save transform and no RNG").c_str());
                uint64_t revision = 1;
                const auto resumed = loaded.dispatch_kickoff(
                    DeskRequest{DeskRequestKind::advance_turn, active, -1, -1, -1, revision}, revision, error);
                check(resumed.kind != TurnDeskKind::Failed,
                      (std::string(name) + " accepts its national no-fixture desk: " + error).c_str());
            }
            auto reject_competition_mismatch = [&](const char* name, uint32_t corrupt_mask) {
                const auto original = saves / name;
                app::CareerSession source(data, 0x22aa7751, original);
                const int active = int(source.state().memory.u8(::bmh::layout::ActiveManager));
                const uint32_t original_mask = source.state().memory.u32(::bmh::layout::SavedCompetition);
                check((original_mask & corrupt_mask) != 0,
                      (std::string(name) + " fixture has the expected active bit").c_str());
                source.state().memory.set32(::bmh::layout::SavedCompetition, original_mask ^ corrupt_mask);
                const auto corrupt_save = temp.path / (std::string(name) + ".BAD-CALENDAR.FBM");
                check(source.save_game(corrupt_save, active, true, error),
                      (std::string(name) + " corrupted calendar fixture saves: " + error).c_str());
                bool rejected = false;
                try { app::CareerSession bad(data, 0x22aa7752, corrupt_save); }
                catch (const std::runtime_error& e) {
                    rejected = std::string(e.what()) ==
                        "saved turn cursor: current calendar and saved competition flags disagree";
                }
                check(rejected, (std::string(name) + " rejects a corrupted saved league/cup bit").c_str());
            };
            if (std::filesystem::exists(saves / "WEEK196.FBM"))
                reject_competition_mismatch("WEEK196.FBM", 1u);  // active league bit
            if (std::filesystem::exists(saves / "NATWK009.FBM"))
                reject_competition_mismatch("NATWK009.FBM", 1u << 11);  // active cup bit
            const auto week196 = saves / "WEEK196.FBM";
            if (std::filesystem::exists(week196)) {
                const auto roundtrip_dir = temp.path / "league-voidmask-roundtrip";
                std::filesystem::create_directories(roundtrip_dir);
                app::CareerSession source(data, 0x22aa7713, week196);
                uint64_t source_revision = 1;
                std::filesystem::path third_save;
                for (int event = 0; event < 3; ++event) {
                    const int active = int(source.state().memory.u8(::bmh::layout::ActiveManager));
                    const auto advanced = source.dispatch_kickoff(
                        DeskRequest{DeskRequestKind::advance_turn, active, -1, -1, -1, source_revision},
                        source_revision, error);
                    check(advanced.kind != TurnDeskKind::Failed,
                          (std::string("WEEK196 setup event advances: ") + error).c_str());
                    third_save = roundtrip_dir / ("event-" + std::to_string(event + 1) + ".FBM");
                    check(source.save_game(third_save, int(source.state().memory.u8(::bmh::layout::ActiveManager)), true, error),
                          "WEEK196 intermediate desk saves without touching the original fixture");
                }
                const Savegame stored = load_savegame(third_save);
                SaveState stored_state(stored.stream);
                const GameState decoded = GameState::from_save(stored_state);
                const uint32_t descriptor = ::bmh::layout::MatchdayGroupDescriptors;
                const int matches = decoded.memory.u8(descriptor + 2);
                const uint32_t mask = decoded.memory.u32(descriptor + 0xb8);
                const uint32_t valid_mask = matches == 32 ? 0xffffffffu : ((1u << matches) - 1u);
                check((mask & ~valid_mask) != 0,
                      "WEEK196 event-3 save reproduces stale void-mask padding from the previous larger group");

                app::CareerSession restored(data, 0x22aa7714, third_save);
                const int active = int(restored.state().memory.u8(::bmh::layout::ActiveManager));
                const int32_t stored_game_seed = restored.state().memory.has(::bmh::layout::CareerSession_Save_RuntimeField)
                    ? int32_t(restored.state().memory.u32(::bmh::layout::CareerSession_Save_RuntimeField)) : 0;
                check(save_stream(restored.state()) == stored.stream &&
                      restored.game().seed() == stored_game_seed &&
                      restored.game().calls() == 0 && restored.rand().seed() == 0x22aa7714 &&
                      restored.rand().calls() == 0,
                      "loading the self-saved stale-padding desk preserves GameState and consumes no RNG");
                uint64_t restored_revision = 1;
                const auto resumed = restored.dispatch_kickoff(
                    DeskRequest{DeskRequestKind::advance_turn, active, -1, -1, -1, restored_revision},
                    restored_revision, error);
                check(resumed.kind != TurnDeskKind::Failed,
                      (std::string("self-saved stale-padding desk loads and advances: ") + error).c_str());
            }
            for (const char* name : {"NATWK009.FBM", "NATWK004.FBM"}) {
                const auto path = saves / name;
                app::CareerSession loaded(data, 0x22aa7718, path);
                const int slot = int(loaded.state().memory.u8(::bmh::layout::ActiveManager));
                check(loaded.rand().calls() == 0, "national cup save binds without Watcom RNG");
                uint64_t rev = 1;
                const auto resumed = loaded.dispatch_kickoff(
                    DeskRequest{DeskRequestKind::advance_turn, slot, -1, -1, -1, rev}, rev, error);
                check(resumed.kind != TurnDeskKind::Failed && loaded.last_turn_results().valid &&
                      !loaded.last_turn_results().groups.empty(),
                      (std::string(name) + " resumes its saved cup matchday: " + error).c_str());
            }
            for (const char* name : {"WEEK204N.FBM", "WEEK204.FBM", "WEEK205.FBM"}) {
                const auto path = saves / name;
                constexpr uint32_t watcom_seed = 0x22aa7740;
                app::CareerSession loaded(data, watcom_seed, path);
                const int slot = int(loaded.state().memory.u8(::bmh::layout::ActiveManager));
                const Savegame stored = load_savegame(path);
                const uint32_t stored_game_seed = loaded.state().memory.has(::bmh::layout::CareerSession_Save_RuntimeField)
                    ? loaded.state().memory.u32(::bmh::layout::CareerSession_Save_RuntimeField) : 0;
                check(save_stream(loaded.state()) == stored.stream &&
                      loaded.game().seed() == int32_t(stored_game_seed) && loaded.game().calls() == 0 &&
                      loaded.rand().seed() == watcom_seed && loaded.rand().calls() == 0,
                      (std::string(name) + " restores friendly desk bytes without setup or RNG").c_str());
                check((loaded.state().memory.u32(::bmh::layout::SavedCompetition) & (1u << 15)) != 0,
                      (std::string(name) + " retains its active friendly competition").c_str());
                uint64_t revision = 1;
                const auto resumed = loaded.dispatch_kickoff(
                    DeskRequest{DeskRequestKind::advance_turn, slot, -1, -1, -1, revision}, revision, error);
                const bool friendly_group = std::any_of(loaded.last_turn_results().groups.begin(),
                    loaded.last_turn_results().groups.end(), [](const MatchGroup& group) {
                        return !group.fixtures.empty() && group.desc[0] == 11 && group.matches() == 1;
                    });
                check(resumed.kind != TurnDeskKind::Failed && loaded.last_turn_results().valid && friendly_group,
                      (std::string(name) + " resumes the saved friendly match: " + error).c_str());
            }
            if (std::filesystem::exists(week196)) {
                app::CareerSession source(data, 0x22aa7721, week196);
                bool verified_ko_resume = false;
                uint64_t source_revision = 1;
                for (int event = 0; event < 40 && !verified_ko_resume; ++event) {
                    const uint32_t competitions = source.state().memory.u32(::bmh::layout::SavedCompetition);
                    int ko_entry = -1;
                    for (int bit = 5; bit < 0x13 && ko_entry < 0; ++bit) {
                        if (!((competitions >> bit) & 1u)) continue;
                        int entry = find_cup(source.state().memory, bit - 5);
                        for (int guard = 0; entry >= 0 && guard < kCupEntries; ++guard) {
                            const uint32_t address = cup_entry_address(entry);
                            if (cup_round(source.state().memory, entry) < source.state().memory.u8(address + 0x1b)) {
                                if ((bit - 5) <= 8 && (source.state().memory.u8(address + 0x14) & 4))
                                    ko_entry = entry;
                                break;
                            }
                            entry = int8_t(source.state().memory.u8(address + 0x16));
                        }
                    }
                    if (ko_entry >= 0) {
                        const int slot = int(source.state().memory.u8(::bmh::layout::ActiveManager));
                        const auto ko_save = temp.path / "generated-ko-round.FBM";
                        check(source.save_game(ko_save, slot, true, error),
                              (std::string("generated KO-round save: ") + error).c_str());
                        const Savegame stored = load_savegame(ko_save);
                        app::CareerSession restored(data, 0x22aa7722, ko_save);
                        const int32_t stored_seed = restored.state().memory.has(::bmh::layout::CareerSession_Save_RuntimeField)
                            ? int32_t(restored.state().memory.u32(::bmh::layout::CareerSession_Save_RuntimeField)) : 0;
                        check(save_stream(restored.state()) == stored.stream &&
                              restored.game().seed() == stored_seed && restored.game().calls() == 0 &&
                              restored.rand().calls() == 0,
                              "KO round reload preserves saved bytes and consumes no RNG or setup work");
                        app::CareerSession malformed_source(data, 0x22aa7723, ko_save);
                        const uint32_t entry_address = cup_entry_address(ko_entry);
                        const int round = cup_round(source.state().memory, ko_entry);
                        const int first = uint8_t(cup_round_offset(source.state().memory, ko_entry, round));
                        const uint32_t team_list = malformed_source.state().memory.pointer(entry_address);
                        malformed_source.state().memory.set16(team_list + uint32_t(first) * 2, 0x7fff);
                        const auto malformed_path = temp.path / "generated-ko-round-bad-team.FBM";
                        check(malformed_source.save_game(malformed_path, slot, true, error),
                              "malformed KO save is written from a real generated round");
                        app::CareerSession malformed(data, 0x22aa7724, malformed_path);
                        const Bytes malformed_before = save_stream(malformed.state());
                        const uint32_t malformed_seed = malformed.game().seed();
                        const uint64_t malformed_calls = malformed.game().calls();
                        uint64_t malformed_revision = 1;
                        const auto rejected = malformed.dispatch_kickoff(
                            DeskRequest{DeskRequestKind::advance_turn, slot, -1, -1, -1, malformed_revision},
                            malformed_revision, error);
                        check(rejected.kind == TurnDeskKind::Failed &&
                              save_stream(malformed.state()) == malformed_before &&
                              malformed.game().seed() == malformed_seed &&
                              malformed.game().calls() == malformed_calls && malformed.rand().calls() == 0,
                              "malformed active KO team is rejected without state or RNG mutation");
                        uint64_t restored_revision = 1;
                        const auto resumed = restored.dispatch_kickoff(
                            DeskRequest{DeskRequestKind::advance_turn, slot, -1, -1, -1, restored_revision},
                            restored_revision, error);
                        check(resumed.kind != TurnDeskKind::Failed,
                              (std::string("generated saved KO round resumes and advances: ") + error).c_str());
                        verified_ko_resume = true;
                        break;
                    }
                    const int slot = int(source.state().memory.u8(::bmh::layout::ActiveManager));
                    const auto advanced = source.dispatch_kickoff(
                        DeskRequest{DeskRequestKind::advance_turn, slot, -1, -1, -1, source_revision},
                        source_revision, error);
                    check(advanced.kind != TurnDeskKind::Failed,
                          (std::string("WEEK196 advances toward a generated KO round: ") + error).c_str());
                }
                check(verified_ko_resume,
                      "WEEK196 generated a saved KO round within the bounded resume probe");
            }
            const auto source_path = saves / "NATWK009.FBM";
            const auto cup_dir = temp.path / "cup-corruptions";
            std::filesystem::create_directories(cup_dir);
            auto reject_corruption = [&](const char* name, auto corrupt) {
                app::CareerSession source(data, 0x22aa7719, source_path);
                const int slot = int(source.state().memory.u8(::bmh::layout::ActiveManager));
                corrupt(source.state().memory);
                const auto damaged_path = cup_dir / name;
                check(source.save_game(damaged_path, slot, true, error),
                      "corrupt cup test save is written from a real national save");
                app::CareerSession damaged(data, 0x22aa7720, damaged_path);
                const uint32_t before_date = damaged.state().date;
                const uint16_t before_fixture = damaged.state().memory.u16(::bmh::layout::MatchdayGroupDescriptors + 0x88);
                const uint32_t before_stage = damaged.state().memory.u32(::bmh::layout::MatchdayGroupDescriptors + 0xcc);
                const auto game_seed = damaged.game().seed();
                const auto game_calls = damaged.game().calls();
                const auto rand_seed = damaged.rand().seed();
                const auto rand_calls = damaged.rand().calls();
                uint64_t rev = 1;
                const auto rejected = damaged.dispatch_kickoff(
                    DeskRequest{DeskRequestKind::advance_turn, slot, -1, -1, -1, rev}, rev, error);
                check(rejected.kind == TurnDeskKind::Failed && damaged.state().date == before_date &&
                      damaged.state().memory.u16(::bmh::layout::MatchdayGroupDescriptors + 0x88) == before_fixture &&
                      damaged.state().memory.u32(::bmh::layout::MatchdayGroupDescriptors + 0xcc) == before_stage &&
                      damaged.game().seed() == game_seed && damaged.game().calls() == game_calls &&
                      damaged.rand().seed() == rand_seed && damaged.rand().calls() == rand_calls,
                      "corrupt cup save is rejected without state or RNG mutation");
            };
            reject_corruption("NATWK009-BAD-FIXTURE.FBM", [](StaticMemory& mem) {
                mem.set16(::bmh::layout::MatchdayGroupDescriptors + 0x8a, mem.u16(::bmh::layout::MatchdayGroupDescriptors + 0x88));
            });
            reject_corruption("NATWK009-BAD-STAGE.FBM", [](StaticMemory& mem) {
                mem.set_pointer(::bmh::layout::MatchdayGroupDescriptors + 0xcc, cup_entry_address(11));
            });
        }
        if (const char* indoor_dir = std::getenv("BMH_CUP_STRESS_DIR")) {
            const std::filesystem::path saves(indoor_dir);
            for (const auto& [filename, cup_type] : {
                     std::pair<const char*, int>{"event-089.FBM", 11},
                     std::pair<const char*, int>{"event-145.FBM", 13}}) {
                const auto path = saves / filename;
                const uint32_t watcom_seed = 0x22aa7730u + unsigned(cup_type);
                app::CareerSession loaded(data, watcom_seed, path);
                const int slot = int(loaded.state().memory.u8(::bmh::layout::ActiveManager));
                const uint32_t saved_seed = loaded.state().memory.has(::bmh::layout::CareerSession_Save_RuntimeField)
                    ? loaded.state().memory.u32(::bmh::layout::CareerSession_Save_RuntimeField) : 0;
                const Savegame stored = load_savegame(path);
                check(save_stream(loaded.state()) == stored.stream &&
                      loaded.game().seed() == int32_t(saved_seed) && loaded.game().calls() == 0 &&
                      loaded.rand().calls() == 0 && loaded.rand().seed() == watcom_seed,
                      (std::string(filename) + " binds saved cup cursor byte-for-byte without RNG").c_str());
                check(loaded.state().indoor == (cup_type == 11),
                      (std::string(filename) + " restores the active indoor/outdoor runtime mode").c_str());
                uint64_t revision = 1;
                const auto advanced = loaded.dispatch_kickoff(
                    DeskRequest{DeskRequestKind::advance_turn, slot, -1, -1, -1, revision}, revision, error);
                const bool result_group = std::any_of(loaded.last_turn_results().groups.begin(),
                    loaded.last_turn_results().groups.end(), [cup_type](const MatchGroup& group) {
                        return !group.fixtures.empty() && group.desc[0] == cup_type + 1;
                    });
                check(advanced.kind != TurnDeskKind::Failed && loaded.last_turn_results().valid && result_group,
                      (std::string(filename) + " resumes its saved special-cup fixtures: " + error).c_str());
            }
        }
        if (const char* replay_dir = std::getenv("BMH_REPLAY_STRESS_DIR")) {
            // Four ordinary advances from WEEK196 lead to the recorded replay at day 271.
            // The test saves that live state and exercises the public CareerSession loader.
            const auto week196 = std::filesystem::path(replay_dir) / "WEEK196.FBM";
            app::CareerSession path_to_replay(data, 0x77112233, week196);
            for (int event = 0; event < 4; ++event) {
                const int slot = int(path_to_replay.state().memory.u8(::bmh::layout::ActiveManager));
                uint64_t rev = uint64_t(event + 1);
                const auto advanced = path_to_replay.dispatch_kickoff(
                    DeskRequest{DeskRequestKind::advance_turn, slot, -1, -1, -1, rev}, rev, error);
                check(advanced.kind != TurnDeskKind::Failed,
                      (std::string("WEEK196 replay path advance: ") + error).c_str());
            }
            const GameState& at_replay = path_to_replay.state();
            check(at_replay.season_day() == 271 && at_replay.memory.u32(::bmh::layout::SavedCompetition) == 0x4000 &&
                  at_replay.memory.u8(cup_entry_address(22) + 0x1a) == 2,
                  "four advances from WEEK196 reach the saved postponed replay day");

            const auto replay_dir_temp = temp.path / "replay-resume";
            std::filesystem::create_directories(replay_dir_temp);
            const auto replay_save = replay_dir_temp / "REPLAY.FBM";
            const int slot = int(at_replay.memory.u8(::bmh::layout::ActiveManager));
            check(path_to_replay.save_game(replay_save, slot, true, error),
                  "replay-day state saves before continuation");
            const Bytes source_stream = load_savegame(replay_save).stream;
            app::CareerSession replay_loaded(data, 0x77112234, replay_save);
            const GameState& rebound = replay_loaded.state();
            check(replay_loaded.rand().calls() == 0 && replay_loaded.game().calls() == 0 &&
                  replay_loaded.game().seed() == (rebound.memory.has(::bmh::layout::CareerSession_Save_RuntimeField)
                      ? int32_t(rebound.memory.u32(::bmh::layout::CareerSession_Save_RuntimeField)) : 0) &&
                  rebound.memory.has(::bmh::layout::ReplayFixtures) &&
                  rebound.memory.has(::bmh::layout::ReplayResults) && save_stream(rebound) == source_stream,
                  "replay load rebuilds only transient buffers, preserving save bytes and load RNG");
            const int postponed_slot = [&] {
                for (int i = 0; i < 0x28; ++i)
                    if (rebound.memory.u16(::bmh::layout::Postponed + uint32_t(i) * 6) == 271) return i;
                return -1;
            }();
            check(postponed_slot >= 0, "replay save retains its current six-byte record");
            const int league = rebound.memory.u8(::bmh::layout::Postponed + uint32_t(postponed_slot) * 6 + 3);
            const int round = rebound.memory.u8(::bmh::layout::Postponed + uint32_t(postponed_slot) * 6 + 2);
            const int match = rebound.memory.u8(::bmh::layout::Postponed + uint32_t(postponed_slot) * 6 + 4);
            const size_t result_at = size_t(league) * 0x17c + size_t(round) * 10 + size_t(match);
            const auto scheduled = generate_fixtures(kLeagueSizes[league], round + 1,
                league_matchdays(league) / 2, league_first_team(league));
            const std::vector<int> expected_replay{
                scheduled.at(size_t(match)).home, scheduled.at(size_t(match)).away};
            uint64_t replay_revision = 1;
            const auto replayed = replay_loaded.dispatch_kickoff(
                DeskRequest{DeskRequestKind::advance_turn, slot, -1, -1, -1, replay_revision},
                replay_revision, error);
            check(replayed.kind != TurnDeskKind::Failed && replay_loaded.last_turn_results().valid &&
                  replay_loaded.last_turn_results().groups.size() == 1 &&
                  replay_loaded.last_turn_results().groups[0].fixtures == expected_replay &&
                  replay_loaded.last_turn_results().groups[0].cup_results.size() == 1 &&
                  replay_loaded.state().memory.u16(::bmh::layout::Postponed + uint32_t(postponed_slot) * 6) == 0 &&
                  replay_loaded.state().results.at(result_at) != 0xcd,
                  (std::string("loaded postponed replay completes and is stored once: ") + error).c_str());

            // A malformed match index must fail during cursor binding, before transient
            // list allocation or any saved-state/RNG mutation at the next desk ACK.
            app::CareerSession bad_source(data, 0x77112235, replay_save);
            bad_source.state().memory.set8(::bmh::layout::Postponed + uint32_t(postponed_slot) * 6 + 4, 0xff);
            const auto bad_path = replay_dir_temp / "REPLAY-BAD-RECORD.FBM";
            check(bad_source.save_game(bad_path, slot, true, error),
                  "malformed replay fixture save is written from the valid replay save");
            app::CareerSession bad_replay(data, 0x77112236, bad_path);
            const Bytes before = save_stream(bad_replay.state());
            const uint32_t game_seed = uint32_t(bad_replay.game().seed());
            const uint64_t game_calls = bad_replay.game().calls();
            const uint32_t rand_seed = bad_replay.rand().seed();
            const uint64_t rand_calls = bad_replay.rand().calls();
            uint64_t bad_revision = 1;
            const auto rejected = bad_replay.dispatch_kickoff(
                DeskRequest{DeskRequestKind::advance_turn, slot, -1, -1, -1, bad_revision},
                bad_revision, error);
            check(rejected.kind == TurnDeskKind::Failed &&
                  error == "Der Pokalspieltag kann mit den gespeicherten Paarungen nicht sicher fortgesetzt werden." &&
                  !bad_replay.state().memory.has(::bmh::layout::ReplayFixtures) && !bad_replay.state().memory.has(::bmh::layout::ReplayResults) &&
                  save_stream(bad_replay.state()) == before &&
                  uint32_t(bad_replay.game().seed()) == game_seed && bad_replay.game().calls() == game_calls &&
                  bad_replay.rand().seed() == rand_seed && bad_replay.rand().calls() == rand_calls,
                  "invalid replay record is rejected without save-state, transient-buffer, or RNG effects");
        }
        if (const char* anchors_dir = std::getenv("BMH_REPLAY_ANCHORS_DIR")) {
            for (const char* name : {"event-004.FBM", "event-005.FBM"}) {
                const auto anchor = std::filesystem::path(anchors_dir) / name;
                app::CareerSession loaded(data, 0x77112237, anchor);
                const GameState& before = loaded.state();
                const int slot = int(before.memory.u8(::bmh::layout::ActiveManager));
                const int day = before.season_day();
                check(loaded.rand().calls() == 0 &&
                      save_stream(before) == load_savegame(anchor).stream,
                      "recorded replay anchor binds without save-byte or load-RNG changes");
                uint64_t rev = 1;
                const auto resumed = loaded.dispatch_kickoff(
                    DeskRequest{DeskRequestKind::advance_turn, slot, -1, -1, -1, rev}, rev, error);
                bool cleared_today = true;
                for (int i = 0; i < 0x28; ++i) {
                    const uint32_t at = ::bmh::layout::Postponed + uint32_t(i) * 6;
                    if (loaded.state().memory.u16(at) == day) cleared_today = false;
                }
                check(resumed.kind != TurnDeskKind::Failed && loaded.last_turn_results().valid &&
                      loaded.last_turn_results().groups.size() == 1 &&
                      loaded.last_turn_results().groups[0].cup_results.size() == 1 && cleared_today,
                      (std::string(name) + " replays and stores its current postponement once: " + error).c_str());
            }
        }
        test_loaded_manager_forfeit(data, temp.path);
        {
            const auto many_saves = temp.path / "many-saves";
            std::filesystem::create_directories(many_saves);
            for (int i = 0; i < 16; ++i) {
                char name[16];
                std::snprintf(name, sizeof(name), "SAVE%02d.FBM", i);
                std::ofstream(many_saves / name, std::ios::binary).put('x');
            }
            const auto outside_save = temp.path / "OUTSIDE.FBM";
            std::ofstream(outside_save, std::ios::binary).put('x');
            const auto symlink_path = many_saves / "ESCAPE.FBM";
            std::error_code symlink_error;
            std::filesystem::create_symlink(outside_save, symlink_path, symlink_error);
            if (symlink_error && !expected_symlink_unavailable(symlink_error)) {
                throw std::filesystem::filesystem_error(
                    "create symlink for save selector test", outside_save, symlink_path, symlink_error);
            }
            SaveLoadScreen browser(data, many_saves, SaveLoadMode::load, 0, revision, flow.desktop()->render());
            check(browser.entries().size() == 16 && browser.state_json().find("\"scroll\":0") != std::string::npos,
                  "Original save selector retains all sixteen saves and begins at its first fifteen-row page");
            if (!symlink_error) {
                check(browser.entries().size() == 16,
                      "Original save selector excludes symlinks");
            } else {
                std::fprintf(stderr,
                    "SKIP: save selector symlink exclusion subcase (symlinks unavailable: %s)\n",
                    symlink_error.message().c_str());
            }
            (void)browser.render();
            check(browser.click(210, 188) && browser.state_json().find("\"scroll\":15") != std::string::npos,
                  "Original Seite button advances to the next fifteen-row page");
            check(browser.click(210, 188) && browser.state_json().find("\"scroll\":0") != std::string::npos,
                  "Original Seite button cycles back to the first page");
        }
        {
            const auto utf8 = [](const std::u8string& value) {
                return std::string(value.begin(), value.end());
            };
            const auto modern_dir = temp.path / std::filesystem::path(u8"Gr\u00fc\u00dfe \u4fdd\u5b58 Saison");
            std::filesystem::create_directory(modern_dir);
            const auto original_native = save_stream(session.state());
            const auto w_seed = session.rand().seed();
            const auto g_seed = session.game().seed();
            const auto w_calls = session.rand().calls(), g_calls = session.game().calls();
            SaveLoadScreen modern(data, modern_dir, SaveLoadMode::save, 0, revision, flow.desktop()->render());
            check(modern.click(150, 214) && modern.text("Mein HSV Saison 2026"),
                  "modern save names accept spaces, mixed case and more than eight characters");
            check(!modern.text("/outside") && !modern.text("\\outside"),
                  "modern file input stays a single directory-confined filename");
            check(modern.key("ENTER") && modern.pending() &&
                  modern.pending()->path.filename() == "Mein HSV Saison 2026.FBM",
                  "modern save request preserves the exact entered case");
            SaveLoadScreen unicode(data, modern_dir, SaveLoadMode::save, 0, revision, flow.desktop()->render());
            check(unicode.click(150, 214) && unicode.text(utf8(u8"K\u00f6ln \u4fdd\u5b58 \u00e4")) &&
                  unicode.key("BACKSPACE") && unicode.key("ENTER") && unicode.pending() &&
                  unicode.pending()->path == modern_dir / std::filesystem::path(u8"K\u00f6ln \u4fdd\u5b58 .FBM"),
                  "SDL UTF-8 text and Backspace preserve the exact Unicode save path");
            check(unicode.state_json().find("\"directory\":" + json_quote(utf8(modern_dir.u8string()))) != std::string::npos &&
                  save_load_request_json(*unicode.pending()).find("\"path\":" +
                      json_quote(utf8(unicode.pending()->path.u8string()))) != std::string::npos,
                  "save dialog state and pending save JSON preserve UTF-8 paths");
            (void)unicode.render();
            const auto existing = modern_dir / std::filesystem::path(u8"MiXeD K\u00f6ln \u4fdd\u5b58.fBm");
            auto rng_copy = session.rand();
            write_savegame(existing, make_savegame(session.state(), rng_copy));
            SaveLoadScreen overwrite(data, modern_dir, SaveLoadMode::save, 0, revision, flow.desktop()->render());
            check(overwrite.state_json().find("\"filename\":" +
                      json_quote(utf8(existing.stem().u8string()))) != std::string::npos &&
                  overwrite.click(70, 45) && overwrite.key("ENTER") && !overwrite.pending() && overwrite.key("ENTER") &&
                  overwrite.pending() && overwrite.pending()->overwrite && overwrite.pending()->path == existing,
                  "selected Unicode filename with mixed-case extension overwrites that exact path");
            SaveLoadScreen info(data, modern_dir, SaveLoadMode::load, 0, revision, flow.desktop()->render());
            check(info.entries().size() == 1 && info.entries().front().path == existing &&
                  info.click(70, 45) && info.selected() == 0 &&
                  info.state_json().find("\"directory\":" + json_quote(utf8(modern_dir.u8string()))) != std::string::npos &&
                  info.state_json().find("\"entries\":[" + json_quote(utf8(existing.filename().u8string())) + "]") != std::string::npos,
                  "load dialog selects a real Unicode FBM and reports UTF-8 directory and entry JSON");
            const auto list = info.render();
            check(info.info_press(70, 45) && info.info_open() && !info.pending(),
                  "right DOWN opens Original native save information without a load request");
            const auto popup = info.render();
            check(popup.pixels[128 * 320 + 264] == 0x40 && popup.pixels != list.pixels,
                  "Original information well includes its two-pixel Source shadow");
            check(!info.key("ENTER") && !info.text("IGNORED") && info.info_open(),
                  "keyboard and filename input cannot act while Source information waits for mouse release");
            check(info.click(70, 45, true) && !info.info_open() && !info.pending() && info.render().pixels == list.pixels,
                  "mouse release restores the exact file browser and never starts a load");
            check(info.key("ENTER") && info.pending() && info.pending()->action == SaveLoadAction::load &&
                  info.pending()->path == existing && save_load_request_json(*info.pending()).find("\"path\":" +
                      json_quote(utf8(existing.u8string()))) != std::string::npos,
                  "normal selected-file load request and JSON retain the exact Unicode path");
            check(save_stream(session.state()) == original_native && session.rand().seed() == w_seed &&
                  session.rand().calls() == w_calls && session.game().seed() == g_seed && session.game().calls() == g_calls,
                  "information, hover and filename interactions leave full native state and both RNGs unchanged");
        }
        std::puts("bank app: account, investments, stadium, Toto, kickoff and temporary save/load flows OK");
    } catch (const std::exception& e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
    return 0;
}
