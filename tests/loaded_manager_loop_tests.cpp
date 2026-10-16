#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "formats/fbm.hpp"
#include "game/career.hpp"
#include "formats/le.hpp"
#include "formats/m3d.hpp"
#include "formats/savestate.hpp"
#include "formats/textd.hpp"
#include "game/national.hpp"
#include "game/pending_offer.hpp"
#include "game/save.hpp"
#include "game/turn.hpp"
#include "game/turn_driver.hpp"
#include "game/weekly.hpp"

using namespace bmh;
namespace {
constexpr uint32_t kManagerCount = ::bmh::layout::ManagerCount;
constexpr uint32_t kActiveManager = ::bmh::layout::ActiveManager;
constexpr uint32_t kManagerPosition = ::bmh::layout::DeskPosition;
constexpr uint32_t kManagerOrder = ::bmh::layout::NextDeskManager;
constexpr uint32_t kOfferWords = ::bmh::layout::OfferSlots;

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

GameState load(const std::filesystem::path& path, const LeImage& exe, const TextDb& texts) {
    const auto file = load_savegame(path);
    check(file.checksum_ok && file.version == "V2.24", "2PNWK001 Native fixture checksum/version");
    const DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
    auto state = GameState::from_save(SaveState(file.stream, counts), counts);
    competition_pointers(state.memory, exe);
    state.texts = &texts;
    national_coach_after_load(state);
    return state;
}

void check_rng_equal(const GameRandom& ag, const WatcomRandom& ar,
                     const GameRandom& bg, const WatcomRandom& br, const char* message) {
    check(ag.seed() == bg.seed() && ag.calls() == bg.calls() &&
          ar.seed() == br.seed() && ar.calls() == br.calls(), message);
}

uint32_t find_pre_match_seed(const GameState& state, int manager) {
    // Find a deterministic Source control that really produces a pre-match offer.
    for (uint32_t seed = 1; seed < 100000; ++seed) {
        auto probe = state;
        GameRandom game(seed);
        WatcomRandom rand(seed);
        MinuteReport report;
        if (pre_match_offer(probe, manager, game, rand, report)) return seed;
    }
    throw std::runtime_error("could not find deterministic generated pre-match offer control");
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 3, "usage: loaded_manager_loop_tests ORIGINAL_DATA 2PNWK001.FBM");
        if (!*argv[1]) {
            std::cout << "loaded manager loop tests: skipped (Original data directory not configured)\n";
            return 0;
        }
        const auto data = std::filesystem::path(argv[1]);
        const auto exe = bmh::test::load_executable(data / "BMH.exe");
        const auto database = load_database(data / "M3D.MAN");
        const auto texts = load_textd(data / "TEXTD.BLK");
        const auto original = load(argv[2], exe, texts);
        check(original.manager_teams.size() == 2 && original.memory.u8(kManagerCount) == 2,
              "unchanged Native 2PNWK001 fixture has two human managers");
        const int count = int(original.memory.u8(kManagerCount));
        const int position = int(original.memory.u8(kManagerPosition));
        check(position == 0 && position + 1 < count,
              "Native fixture is at the first desk of its prepared manager loop");
        const int active = int(original.memory.u8(kActiveManager));
        check(active == int(original.memory.u16(kManagerOrder + uint32_t(position) * 2)),
              "Native active manager follows the saved order");
        for (int raw = 0; raw < 4; ++raw)
            check(original.memory.u32(kOfferWords + uint32_t(raw) * 4) == 0xffffffffu,
                  "unchanged 2PNWK001 has no pending offer words");
        const int later_position = position + 1;
        const int later_manager = int(original.memory.u16(kManagerOrder + uint32_t(later_position) * 2));

        const uint32_t seed = find_pre_match_seed(original, later_manager);
        auto current = original;
        GameRandom current_game(seed);
        WatcomRandom current_rand(seed);
        MinuteReport pending_report;
        const auto pending = process_natural_pending_offers_after_desk(
            current, PendingOfferDeskRequest{PendingOfferDeskRuntimeMode::normal_interactive, count, position},
            current_game, current_rand, {}, pending_report);
        check(pending.completed && pending.processed == 0 && !pending_report.unsupported,
              "current Native manager's no-offer post-desk Source batch completes");
        check(save_stream(current) == save_stream(original) && current.memory.u8(kManagerPosition) == position &&
              current_game.calls() == 0 && current_rand.calls() == 0,
              "nonfinal empty Source batch leaves Native bytes, cursor and both RNGs untouched");

        auto oracle = current;
        auto oracle_game = current_game;
        auto oracle_rand = current_rand;
        TurnDriver driver;
        driver.stage = TurnDriver::Stage::MatchdayDesks;
        driver.manager_position = position;
        driver.calendar_entry = current.memory.u16(::bmh::layout::CompetitionCalendar + uint32_t(current.season_day()) * 2);
        driver.revision = 1;
        driver.session_id = 1;
        driver.referee_source = database.raw_referee_properties;
        driver.referees = database.raw_referee_properties;
        // The actual Native load producer sets provenance. The normal-interactive
        // App enables suppression only after the staged Source batch above completes.
        driver.loaded_matchday_resume = true;
        driver.suppress_remaining_pre_match_offers = true;

        MinuteReport oracle_report;
        const bool oracle_offer = enter_event_desk(
            oracle, later_position, driver.groups, oracle_game, oracle_rand, oracle_report, false);
        MinuteReport driver_report;
        const auto result = advance_to_next_desk(
            driver, current, TurnAdvanceRequest{active, 1}, current_game, current_rand, driver_report);
        check(result.kind == TurnDeskKind::NextDesk && !result.offer_pending && !oracle_offer &&
              !driver_report.unsupported && !oracle_report.unsupported,
              "loaded continuation reaches next ordered desk while skipping only pre-match offer");
        check(save_stream(current) == save_stream(oracle),
              "loaded desk entry equals direct Source-suppressed oracle state");
        check_rng_equal(current_game, current_rand, oracle_game, oracle_rand,
                        "loaded desk entry consumes no RNG beyond direct Source-suppressed oracle");
        check(current.memory.u8(kManagerPosition) == later_position &&
              current.memory.u8(kActiveManager) == later_manager &&
              driver.manager_position == later_position && driver.loaded_matchday_resume &&
              driver.suppress_remaining_pre_match_offers,
              "saved order owner is restored and resume policy persists until loop completion");

        // An ordinary/fresh desk with this same seed does generate the pre-match
        // offer. Default entry remains identical to explicit Source param0 behavior.
        auto ordinary_default = original;
        auto ordinary_explicit = original;
        GameRandom default_game(seed), explicit_game(seed);
        WatcomRandom default_rand(seed), explicit_rand(seed);
        MinuteReport default_report, explicit_report;
        const bool default_offer = enter_event_desk(
            ordinary_default, later_position, {}, default_game, default_rand, default_report);
        const bool explicit_offer = enter_event_desk(
            ordinary_explicit, later_position, {}, explicit_game, explicit_rand, explicit_report, true);
        check(default_offer && explicit_offer && !default_report.unsupported && !explicit_report.unsupported,
              "deterministic fresh control generates a real pre-match offer");
        check(save_stream(ordinary_default) == save_stream(ordinary_explicit),
              "default entry is byte-identical to explicit ordinary pre-match entry");
        check_rng_equal(default_game, default_rand, explicit_game, explicit_rand,
                        "default entry retains ordinary pre-match W/G progression");

        auto suppressed_control = original;
        GameRandom suppressed_game(seed);
        WatcomRandom suppressed_rand(seed);
        MinuteReport suppressed_report;
        const bool suppressed_offer = enter_event_desk(
            suppressed_control, later_position, {}, suppressed_game, suppressed_rand,
            suppressed_report, false);
        check(!suppressed_offer && !suppressed_report.unsupported,
              "loaded-only parameter omits generated pre-match offer");
        check(suppressed_rand.calls() < default_rand.calls() || suppressed_game.calls() < default_game.calls(),
              "suppression avoids the pre-match call's RNG draws while keeping desk entry active");

        TurnDriver fresh;
        check(!fresh.loaded_matchday_resume && !fresh.suppress_remaining_pre_match_offers,
              "fresh driver defaults preserve legacy/headless pre-match behavior");

        // Waiting/stale work is rejected before claim and leaves loaded policy
        // available for retry; only a completed final manager loop consumes it.
        auto retry_state = current;
        TurnDriver retry = driver;
        auto retry_game = current_game;
        auto retry_rand = current_rand;
        const auto before = save_stream(retry_state);
        MinuteReport retry_report;
        const auto rejected = advance_to_next_desk(
            retry, retry_state, TurnAdvanceRequest{later_manager + 1, 2},
            retry_game, retry_rand, retry_report);
        check(rejected.kind == TurnDeskKind::Failed && retry.stage == TurnDriver::Stage::MatchdayDesks &&
              retry.loaded_matchday_resume && retry.suppress_remaining_pre_match_offers &&
              save_stream(retry_state) == before,
              "pre-driver rejection preserves Native state and loaded policy for retry");
        check_rng_equal(retry_game, retry_rand, current_game, current_rand,
                        "pre-driver rejection leaves W and G untouched");

        auto completed = driver;
        completed.complete_loaded_manager_loop();
        check(!completed.loaded_matchday_resume && !completed.suppress_remaining_pre_match_offers,
              "completed manager loop consumes load provenance and policy before the next event");

        std::cout << "loaded 2-manager loop: suppressed desk, generated fresh-offer control, native order and W/G oracle passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
