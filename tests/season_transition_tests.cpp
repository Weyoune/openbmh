// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "test_inputs.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "app/career_session.hpp"
#include "formats/le.hpp"
#include "game/career.hpp"
#include "game/messages.hpp"
#include "game/save.hpp"
#include "game/weekly.hpp"

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bmh::TurnAdvanceResult advance(bmh::app::CareerSession& session, uint64_t& revision, std::string& error) {
    bmh::ui::DeskRequest request;
    request.kind = bmh::ui::DeskRequestKind::advance_turn;
    request.manager_slot = session.state().memory.u8(::bmh::layout::ActiveManager);
    request.revision = revision;
    return session.dispatch_kickoff(request, revision, error);
}

void check_starting_sponsors(const std::filesystem::path& data) {
    bmh::GameState g;
    g.manager_records.assign(5 * 0xb0c, 0);
    g.memory.add_block(::bmh::layout::GameMode, 1);
    g.memory.add_block(::bmh::layout::CareerDuration, 1);
    g.memory.add_block(::bmh::layout::CalendarDay, 4);
    g.memory.add_block(::bmh::layout::Difficulty, 1);
    g.memory.set8(::bmh::layout::Difficulty, 2);
    g.memory.set32(::bmh::layout::CalendarDay, 0); // stale at nonzero GameState.date
    static int draws = 0;
    auto previous_draw_hook = bmh::draw_hook;
    bmh::draw_hook = [](int) { ++draws; };
    auto set_contracts = [&](uint32_t value) {
        for (size_t o : {size_t(0xae8), size_t(0xadc), size_t(0xa34)})
            for (int b = 0; b < 4; ++b) g.manager_records[o + size_t(b)] = uint8_t(value >> (8 * b));
        for (size_t o : {size_t(0xaed), size_t(0xae1), size_t(0xa39)}) g.manager_records[o] = 0;
    };
    auto verify = [&](uint32_t amount, uint8_t active) {
        for (size_t o : {size_t(0xae8), size_t(0xadc), size_t(0xa34)}) {
            const uint32_t got = uint32_t(g.manager_records[o]) | uint32_t(g.manager_records[o + 1]) << 8 |
                uint32_t(g.manager_records[o + 2]) << 16 | uint32_t(g.manager_records[o + 3]) << 24;
            require(got == amount, "starting sponsor amount differs from source formula/guard");
        }
        for (size_t o : {size_t(0xaed), size_t(0xae1), size_t(0xa39)})
            require(g.manager_records[o] == active, "starting sponsor active flag differs from source guard");
    };

    g.manager_records[0x210] = 0;
    const uint32_t mode_i_amount = 250000; // legacy routine source integer stages, league 0/difficulty 2.
    g.memory.set8(::bmh::layout::GameMode, 1); g.memory.set8(::bmh::layout::CareerDuration, 5);
    for (uint32_t day : {0u, 31u, 365u}) {
        g.date = day; set_contracts(0); bmh::starting_sponsors(g, 0);
        verify(mode_i_amount, 1); // Mode I is date-independent; CalendarDay deliberately remains stale at 0.
    }

    g.memory.set8(::bmh::layout::GameMode, 3); g.memory.set8(::bmh::layout::CareerDuration, 1);
    g.memory.set32(::bmh::layout::CalendarDay, 365); // opposite stale value: GameState.date decides.
    g.date = 0; set_contracts(0); bmh::starting_sponsors(g, 0);
    verify(mode_i_amount, 1);
    g.date = 1; g.memory.set32(::bmh::layout::CalendarDay, 0);
    set_contracts(0x12345678); const auto before_noop = g.manager_records;
    bmh::starting_sponsors(g, 0);
    verify(0x12345678, 0);
    require(g.manager_records == before_noop, "stale bb254=0 cannot initialize contracts after day zero");

    g.memory.set8(::bmh::layout::CareerDuration, 5); g.date = 0;
    set_contracts(0x76543210); const auto before_duration5 = g.manager_records;
    bmh::starting_sponsors(g, 0);
    require(g.manager_records == before_duration5, "mode III duration 5 leaves sponsor contracts unchanged");

    constexpr uint32_t source_amounts[2][5] = {
        {270000, 210000, 160000, 160000, 100000}, // difficulty 0, leagues 0..4
        {200000, 150000, 110000, 110000, 70000},  // difficulty 9, leagues 0..4
    };
    g.memory.set8(::bmh::layout::GameMode, 1); g.memory.set8(::bmh::layout::CareerDuration, 5);
    for (int difficulty_case = 0; difficulty_case < 2; ++difficulty_case) {
        const uint8_t difficulty = difficulty_case == 0 ? 0 : 9;
        g.memory.set8(::bmh::layout::Difficulty, difficulty);
        for (int league = 0; league < 5; ++league) {
            g.manager_records[0x210] = uint8_t(league);
            g.date = uint32_t(league * 17 + difficulty);
            set_contracts(0); bmh::starting_sponsors(g, 0);
            verify(source_amounts[difficulty_case][league], 1);
        }
    }
    require(draws == 0, "starting sponsor initialization has no RNG draws");
    bmh::draw_hook = previous_draw_hook;

    for (const auto [mode, duration] : {std::pair{1, 1}, std::pair{1, 5},
                                        std::pair{3, 1}, std::pair{3, 5}}) {
        bmh::app::CareerSession session(data, 0x6a77u + uint32_t(mode * 10 + duration));
        auto options = bmh::ui::SetupOptions{};
        options.mode = mode; options.duration = duration; options.managers = 1;
        const auto context = session.selection_context(1);
        bmh::ui::ManagerChoice choice;
        choice.master_club = context.club_order.at(0);
        choice.name = "Sponsor-Test"; choice.start_league = 0; choice.strength = 1;
        std::string error;
        require(session.confirm_manager(options, choice, error), error.c_str());
        const bool should_start = mode == 1 || duration == 1;
        const auto& records = session.state().manager_records;
        for (size_t o : {size_t(0xae8), size_t(0xadc), size_t(0xa34)}) {
            const uint32_t amount = uint32_t(records[o]) | uint32_t(records[o + 1]) << 8 |
                uint32_t(records[o + 2]) << 16 | uint32_t(records[o + 3]) << 24;
            require(amount == (should_start ? mode_i_amount : 0),
                    "fresh CareerSession did not initialize sponsor contracts for selected mode/duration");
        }
        bmh::ui::CareerInitializationResult result;
        require(session.finish(result, error), (std::string("sponsor career finish: ") + error).c_str());
        for (size_t o : {size_t(0xae8), size_t(0xadc), size_t(0xa34)}) {
            const uint32_t amount = uint32_t(session.state().manager_records[o]) |
                uint32_t(session.state().manager_records[o + 1]) << 8 |
                uint32_t(session.state().manager_records[o + 2]) << 16 |
                uint32_t(session.state().manager_records[o + 3]) << 24;
            require(amount == (should_start ? mode_i_amount : 0),
                    "CareerSession season initialization changed the expected starting sponsor contracts");
        }
        const auto save_path = std::filesystem::temp_directory_path() /
            ("openbmh-sponsor-mode-" + std::to_string(mode) + "-duration-" + std::to_string(duration) + ".FBM");
        auto saved_rand = session.rand();
        bmh::write_savegame(save_path, bmh::make_savegame(session.state(), saved_rand));
        bmh::app::CareerSession loaded(data, 1, save_path);
        require(bmh::load_savegame(save_path).checksum_ok &&
                bmh::save_stream(loaded.state()) == bmh::save_stream(session.state()),
                "sponsor state changes across native CareerSession save/reload");
        std::filesystem::remove(save_path);
    }
}

void check_new_career_intro_messages(const std::filesystem::path& data) {
    const bmh::TextDb texts = bmh::load_textd(data / "TEXTD.BLK");
    require(texts[1001] == bmh::test::expected_text("season_transition_tests.0") &&
            texts[1002] == bmh::test::expected_text("season_transition_tests.1") &&
            texts[1003] == bmh::test::expected_text("season_transition_tests.2"),
            "welcome message TEXTD ids do not match the original intro sequence");
    const auto previous_draw_hook = bmh::draw_hook;
    static int draws = 0;
    draws = 0;
    bmh::draw_hook = [](int) { ++draws; };
    try {
        for (uint8_t managers : {uint8_t(1), uint8_t(2)}) {
            bmh::GameState g;
            g.texts = &texts;
            g.manager_records.assign(5 * 0xb0c, 0);
            g.mailbox.assign(bmh::kMailboxEntries * bmh::kMailboxEntry, 0);
            g.memory.add_block(::bmh::layout::ManagerCount, 1);
            g.memory.add_block(::bmh::layout::CareerSession_Invalid_RuntimeField2, 4);
            g.memory.add_block(::bmh::layout::CurrentMatchday, 4);
            g.memory.set8(::bmh::layout::ManagerCount, managers);
            g.memory.set32(::bmh::layout::CareerSession_Invalid_RuntimeField2, 0x13572468);
            g.memory.set32(::bmh::layout::CurrentMatchday, 0x24681357);

            bmh::career_managers_begin(g);
            require(g.manager_teams.size() == managers,
                    "new-career manager slots are not initialized before welcome recipients");
            const uint32_t recipient_mask = 0x80000000u | ((uint32_t(1) << managers) - 1);
            if (managers == 2)
                require(recipient_mask == 0x80000003u, "two-manager welcome recipient mask changed");
            std::vector<uint8_t> expected(bmh::kMailboxEntries * bmh::kMailboxEntry, 0);
            for (size_t i = 0; i < 3; ++i) {
                const size_t at = i * bmh::kMailboxEntry;
                expected[at] = uint8_t(recipient_mask);
                expected[at + 1] = uint8_t(recipient_mask >> 8);
                expected[at + 2] = uint8_t(recipient_mask >> 16);
                expected[at + 3] = uint8_t(recipient_mask >> 24);
                const auto raw = texts.raw(1001 + i, bmh::kMailboxText);
                std::copy(raw.begin(), raw.end(), expected.begin() + at + 0x0a);
            }
            require(g.mailbox == expected,
                    "new-career intro messages differ in content, order, recipients, or mailbox layout");
            require(g.memory.u32(::bmh::layout::CareerSession_Invalid_RuntimeField2) == 0x13572468 && g.memory.u32(::bmh::layout::CurrentMatchday) == 0x24681357,
                    "intro messages changed the save seed or matchday draw fields");
        }

        // Invalid manager-count guards must not post partial welcome mail.
        for (uint8_t managers : {uint8_t(0), uint8_t(5)}) {
            bmh::GameState g;
            g.texts = &texts;
            g.manager_records.assign(5 * 0xb0c, 0);
            g.mailbox.assign(bmh::kMailboxEntries * bmh::kMailboxEntry, 0);
            g.memory.add_block(::bmh::layout::ManagerCount, 1);
            g.memory.set8(::bmh::layout::ManagerCount, managers);
            bmh::career_managers_begin(g);
            require(g.mailbox == std::vector<uint8_t>(g.mailbox.size(), 0),
                    "invalid manager count posted intro messages");
        }

        // A manager joining an existing career is not the param_1==0 source path.
        bmh::GameState joined;
        joined.texts = &texts;
        joined.manager_records.assign(5 * 0xb0c, 0);
        joined.manager_teams = {17, 45};
        joined.mailbox.assign(bmh::kMailboxEntries * bmh::kMailboxEntry, 0);
        bmh::post_text(joined, -1, 1001);
        bmh::post_text(joined, -1, 1002);
        bmh::post_text(joined, -1, 1003);
        const auto mailbox_before_join = joined.mailbox;
        const auto records_before_join = joined.manager_records;
        bmh::GameRandom game(0x12345678);
        bmh::WatcomRandom watcom(0x87654321);
        const int16_t teams_before[2] = {17, 45};
        bmh::MinuteReport report;
        const bool accepted = bmh::career_add_manager(joined, bmh::LeImage{}, 2, bmh::CareerManager{},
                                                      true, game, watcom, report);
        require(!accepted && report.unsupported && joined.mailbox == mailbox_before_join &&
                joined.manager_records == records_before_join && joined.manager_teams[0] == teams_before[0] &&
                joined.manager_teams[1] == teams_before[1] && game.calls() == 0 && watcom.calls() == 0,
                "invalid joining-manager request changed existing state/RNG");
        require(draws == 0, "welcome messages consumed RNG draws");
    } catch (...) {
        bmh::draw_hook = previous_draw_hook;
        throw;
    }
    bmh::draw_hook = previous_draw_hook;
}
void check_joining_manager(const std::filesystem::path& data, const std::filesystem::path& fixture) {
    const auto input = fixture.parent_path() / "2026-10-06-new-player" / "INPUT.FBM";
    bmh::app::CareerSession session(data, 1, input);
    const bmh::LeImage exe = bmh::load_le(data / "BMH.exe");
    const bmh::GameState before = session.state();
    require(before.manager_teams == std::vector<int>{15} && before.club_ids.at(2) == 193,
            "unexpected HSV joining-manager input identities");
    bmh::CareerManager choice{"Nobody", 2, 0, 1, 0};
    auto prepare = [&]() {
        bmh::GameState g = before;
        g.memory.set8(::bmh::layout::ManagerCount, 2);
        // Existing loans unrelated to this regression are retained by the real path.
        for (uint32_t i = 0; i < 200; ++i) g.memory.set32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + i * 4, 0);
        for (uint32_t i = 0; i < 12; ++i) g.memory.set16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + i * 2, 0xfff);
        bmh::career_managers_begin(g, true);
        require(g.manager_teams[0] == 15 && g.mailbox == before.mailbox,
                "join begin erased existing manager or reposted startup mail");
        return g;
    };
    auto execute = [&](bmh::GameState& g, bmh::MinuteReport& report) {
        bmh::GameRandom game(410903);
        bmh::WatcomRandom watcom(1916934687);
        const bool ok = bmh::career_add_manager(g, exe, 1, choice, true, game, watcom, report);
        require(game.calls() == 0, "same-league join consumed a game RNG draw");
        return ok;
    };
    // Invalid numeric choices and undersized board state are inert.
    for (bool short_board : {false, true}) {
        bmh::GameState invalid = prepare();
        if (short_board) invalid.tactics.resize(0x840);
        const auto records = invalid.manager_records;
        const auto clubs = invalid.club_ids;
        const auto teams = invalid.manager_teams;
        bmh::CareerManager bad = choice;
        if (!short_board) bad.team = -1;
        bmh::GameRandom game(410903);
        bmh::WatcomRandom watcom(1916934687);
        bmh::MinuteReport report;
        require(!bmh::career_add_manager(invalid, exe, 1, bad, true, game, watcom, report) &&
                report.unsupported && invalid.manager_records == records && invalid.club_ids == clubs &&
                invalid.manager_teams == teams && game.calls() == 0 && watcom.calls() == 0,
                "invalid join request mutated records/clubs or consumed RNG");
    }
    bmh::GameState g = prepare();
    const uint32_t bidbase = ::bmh::layout::EditorList;
    g.memory.set16(bidbase + 8, 20); // seller is neither swapped club
    for (uint32_t i = 0; i < 4; ++i) {
        g.memory.set16(bidbase + i * 2, uint16_t(i == 0 ? 2 : 30 + i));
        g.memory.set32(bidbase + 0xc + i * 4, 100 + i);
        g.memory.set32(bidbase + 0x1c + i * 4, 200 + i);
        g.memory.set8(bidbase + 0x34 + i, uint8_t(50 + i));
        for (uint32_t b = 0; b < 7; ++b) g.memory.set8(bidbase + 0x38 + i * 7 + b, uint8_t(60 + i * 7 + b));
    }
    g.memory.set16(bidbase + 0x58 + 8, 2); // joining club's sale entry
    const uint32_t youthbase = bidbase + 40 * 0x58;
    g.memory.set16(youthbase + 8, 20);
    for (uint32_t i = 0; i < 4; ++i) g.memory.set16(youthbase + i * 2, uint16_t(i == 3 ? 2 : 30 + i));
    g.squad_slots.at(2928 + 80) = 3;
    auto loan = [](uint32_t player, uint32_t owner, uint32_t borrower) {
        return player | owner << 12 | borrower << 22;
    };
    g.memory.set32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField, loan(41, 2, 20));
    g.memory.set32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + 4, loan(42, 15, 2));
    const uint32_t unrelated = loan(43, 20, 21);
    g.memory.set32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + 8, unrelated);
    g.memory.set16(::bmh::layout::Career_CareerManagersEnd_RuntimeField, 42);
    g.memory.set16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + 2, 77);
    bmh::MinuteReport report;
    require(execute(g, report), report.reason.c_str());
    require(g.manager_teams == std::vector<int>({15, 10}) && g.club_ids.at(10) == 193 &&
            g.club_ids.at(2) == before.club_ids.at(10), "HSV did not move from runtime club 2 to 10");
    require(g.memory.u8(::bmh::layout::ActiveManager) == before.memory.u8(::bmh::layout::ActiveManager), "Core join changed the active desk cursor");
    require(g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField) == 0 && g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + 4) == 0 &&
            g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + 8) == unrelated && g.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField) == 0xfff,
            "joining club's loans were not cleaned after club identity remapping");
    require(g.memory.u16(bidbase) == 31 && g.memory.u16(bidbase + 2) == 0xffff &&
            g.memory.u16(bidbase + 4) == 33 && g.memory.u32(bidbase + 0xc) == 101 &&
            g.memory.u32(bidbase + 0x1c) == 201 && g.memory.u8(bidbase + 0x34) == 51 &&
            g.memory.u8(bidbase + 0x38) == 67 && g.squad_slots.at(2929) == 0xfff &&
            g.memory.u16(youthbase + 6) == 0xffff && g.squad_slots.at(2928 + 80) == 0xfff &&
            g.memory.u32(::bmh::layout::Career_RemoveJoinTransfers_RuntimeField + 40 * 4) == 0xffffffff,
            "Source transfer cleanup changed bid terminator or lost parallel bid fields");
    for (uint32_t base : {::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField25Unsigned, ::bmh::layout::Career_CompetitionPointersEnglish_RuntimeField14Unsigned, ::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField19Unsigned, ::bmh::layout::FieldUnsigned, ::bmh::layout::Cup_AdvanceCup_RuntimeField6Unsigned})
        require(g.memory.u16(base) == before.memory.u16(base), "joining swap remapped European identity lists");
    require(std::equal(before.club_table.begin() + 2 * 74, before.club_table.begin() + 3 * 74,
                       g.club_table.begin() + 10 * 74), "HSV club record failed to follow its identity");
    require(bmh::squad_count(g, 10, 2) == bmh::squad_count(before, 2, 2), "join changed full squad size");
    bmh::career_managers_end(g, true);
    require(g.memory.u8(::bmh::layout::CareerSession_FormatError_RuntimeField6) == 0 && g.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + 2) == 77,
            "join end erased unrelated human loans or left swap flag set");

    // The Source skips European and reserved destination slots even in duration 1.
    bmh::GameState skipped = prepare();
    skipped.memory.set8(::bmh::layout::CareerDuration, 1);
    skipped.memory.set8(::bmh::layout::Career_EuropeanClub_ExecutableField, 1); skipped.memory.set16(::bmh::layout::Career_CompetitionPointersEnglish_ExecutableField25, 10);
    skipped.memory.set16(::bmh::layout::CompetitionCalendar + 11 * 2, 0x4001);
    bmh::MinuteReport skipped_report;
    require(execute(skipped, skipped_report) && skipped.manager_teams[1] == 12,
            "join ignored m + 9, European prefix, or reserved calendar place");

    // A missing human-owner loan row is the Source error 0x3c, left to controller rollback.
    bmh::GameState broken = prepare();
    broken.memory.set32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField, loan(42, 15, 2));
    bmh::MinuteReport broken_report;
    require(!execute(broken, broken_report) && broken_report.unsupported &&
            broken_report.reason.find("0x3c") != std::string::npos,
            "missing human loan row did not report Source failure");
}

}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    if (!*argv[1]) { std::puts("season transition: skipped (Original data directory not configured)"); return 77; }
    try {
        const std::filesystem::path fixture = argv[2];
        check_new_career_intro_messages(argv[1]);
        check_starting_sponsors(argv[1]);
        check_joining_manager(argv[1], fixture);
        bmh::app::CareerSession plain(argv[1], 1, fixture / "INPUT.FBM");
        bmh::app::CareerSession presented(argv[1], 1, fixture / "INPUT.FBM");
        bmh::app::CareerSession original(argv[1], 1, fixture / "ORIGINAL.FBM");
        require(plain.state().date == 365 && plain.state().season == 0, "unexpected original season-end input");
        require(original.state().date == 371 && original.state().season_day() == 0,
                "unexpected paired original new-season desk");
        std::vector<std::string> screens;
        presented.set_season_pause([&](const char* screen, const bmh::GameState& state) {
            require(state.season == 1, "presentation before season increment");
            const auto before = bmh::save_stream(state);
            screens.emplace_back(screen);
            require(bmh::save_stream(state) == before, "presentation changed serialized state");
        });
        uint64_t plain_revision = 1, presented_revision = 1;
        std::string error;
        require(advance(plain, plain_revision, error).kind == bmh::TurnDeskKind::NextDesk, error.c_str());
        require(advance(presented, presented_revision, error).kind == bmh::TurnDeskKind::NextDesk, error.c_str());
        const auto& state = presented.state();
        require(state.date == original.state().date && state.season == original.state().season && state.season_day() == 0,
                "season transition skipped the original opening desk");
        require(state.memory.u8(::bmh::layout::CareerSession_RuntimeError_RuntimeField4) == 0 && state.memory.u8(::bmh::layout::ActiveManager) == 0 && presented_revision == 2,
                "opening desk did not own the next revision and manager");
        require(bmh::save_stream(state) == bmh::save_stream(plain.state()) &&
                presented.rand().seed() == plain.rand().seed() && presented.rand().calls() == plain.rand().calls() &&
                presented.game().seed() == plain.game().seed() && presented.game().calls() == plain.game().calls(),
                "presentation changes game state or random draws");
        require(!screens.empty() && screens.front() == "the German entrants (legacy routine)",
                "missing original international entrants checkpoint");
        const auto settled = bmh::save_stream(state);
        const auto wseed = presented.rand().seed();
        const auto gseed = presented.game().seed();
        const auto wcalls = presented.rand().calls(), gcalls = presented.game().calls();
        bmh::ui::DeskRequest stale;
        stale.kind = bmh::ui::DeskRequestKind::advance_turn; stale.manager_slot = 0; stale.revision = 1;
        require(presented.dispatch_kickoff(stale, presented_revision, error).kind == bmh::TurnDeskKind::Failed,
                "stale end-of-season request accepted");
        require(bmh::save_stream(state) == settled && presented.rand().seed() == wseed && presented.game().seed() == gseed &&
                presented.rand().calls() == wcalls && presented.game().calls() == gcalls,
                "stale request mutated opening desk or RNG");
        const auto path = std::filesystem::temp_directory_path() / "openbmh-season-opening-test.FBM";
        auto copied_rand = presented.rand();
        bmh::write_savegame(path, bmh::make_savegame(state, copied_rand));
        bmh::app::CareerSession reloaded(argv[1], 1, path);
        require(bmh::load_savegame(path).checksum_ok && bmh::save_stream(reloaded.state()) == settled,
                "opening desk native save/reload differs");
        uint64_t reload_revision = 1;
        require(advance(reloaded, reload_revision, error).kind != bmh::TurnDeskKind::Failed && reloaded.state().date > 371,
                "reloaded opening desk cannot continue");
        require(advance(presented, presented_revision, error).kind != bmh::TurnDeskKind::Failed && presented.state().date > 371,
                "opening desk does not advance on next kickoff");
        std::filesystem::remove(path);
        std::printf("Original season-end 365 -> opening desk 371; %zu presentation checkpoints; native reload and next kickoff OK\n", screens.size());
        return 0;
    } catch (const std::exception& ex) { std::fprintf(stderr, "season transition: %s\n", ex.what()); return 1; }
}
