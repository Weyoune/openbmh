#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "formats/fbm.hpp"
#include "formats/le.hpp"
#include "game/cup.hpp"
#include "game/career.hpp"
#include "game/national.hpp"
#include "game/save.hpp"
#include "game/season.hpp"

using namespace bmh;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

GameState load(const std::filesystem::path& path, const LeImage& exe, const TextDb& texts) {
    const auto file = load_savegame(path);
    check(file.checksum_ok && file.version == "V2.24", "Original DFB FBM checksum/version");
    const DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
    GameState g = GameState::from_save(SaveState(file.stream, counts), counts);
    competition_pointers(g.memory, exe);
    g.texts = &texts;
    national_coach_after_load(g);
    return g;
}

struct Result {
    GameState state;
    GameRandom game;
    WatcomRandom watcom;
    std::vector<SeasonCupDrawEvent> events;
};

Result advance(GameState g, bool observe, bool hide_option = false, bool world_mode = true,
               bool first_leg = false, bool final_stage = false, int active_manager = 0) {
    if (first_leg) {
        g.memory.set8(kCupTable + 0x14, uint8_t(g.memory.u8(kCupTable + 0x14) | 1));
        g.memory.set8(kCupTable + 0x1d, 0);
        g.memory.set_pointer(kCupTable + 0x10, g.memory.pointer(kCupTable + 0x0c));
    }
    if (final_stage) g.memory.set8(kCupTable + 0x1b, 2);
    g.memory.set8(::bmh::layout::ActiveManager, uint8_t(active_manager));
    if (hide_option) g.memory.set8(::bmh::layout::LeaguePresentationOptions + 5 * 9, 1);
    // Positive case preserves the native mode byte; world_mode=false is an explicit gate variation.
    if (!world_mode) g.memory.set8(::bmh::layout::CompetitionSelection, uint8_t(g.memory.u8(::bmh::layout::CompetitionSelection) & ~1u));
    // Only this runtime UI marker is cleared; all native input bytes are preserved.
    if (g.memory.has(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField))
        for (int m = 0; m < 4; ++m) g.memory.set8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField + uint32_t(m), 0);
    Result r{std::move(g), GameRandom(665367), WatcomRandom(589388519), {}};
    const uint64_t w0 = r.watcom.calls();
    const auto hook = [&](const GameState& state, const SeasonCupDrawEvent& event) {
        check(event.entry_address == kCupTable && event.competition == 0,
              "follow-up event identifies the actual national-cup entry");
        check(event.round == 2 && event.offset == 96 && event.count == 16 &&
              event.participants.size() == 16,
              "Original day-66 native fixture reaches DFB round two with 16 clubs");
        check(event.phase == (r.events.empty() ? SeasonCupDrawPhase::draw : SeasonCupDrawPhase::overview),
              "unsorted draw snapshot precedes sorted overview snapshot");
        const uint32_t first = state.memory.pointer(event.entry_address) + uint32_t(event.offset) * 2;
        for (size_t i = 0; i < event.count; ++i)
            check(event.participants[i] == state.memory.u16(first + uint32_t(i) * 2),
                  "event owns the exact round field at its callback boundary");
        if (event.phase == SeasonCupDrawPhase::draw) {
            check(state.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField) == 0 && state.memory.u8(::bmh::layout::ActiveManager) == active_manager,
                  "draw callback sees pre-overview UI state and active manager");
        } else {
            const int selected = active_manager > 3 ? 0 : active_manager;
            check(state.memory.u8(::bmh::layout::ActiveManager) == selected &&
                  state.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField + uint32_t(selected)) == 0x80,
                  "overview callback follows legacy routine active-slot clamp/effect");
            auto tie_set = [](const std::vector<uint16_t>& v) {
                std::vector<std::pair<uint16_t, uint16_t>> out;
                for (size_t i = 0; i < v.size(); i += 2)
                    out.emplace_back(std::min(v[i], v[i + 1]), std::max(v[i], v[i + 1]));
                std::sort(out.begin(), out.end());
                return out;
            };
            check(tie_set(event.participants) == tie_set(r.events.front().participants),
                  "native home/attraction sort preserves every shuffled tie");
            check(r.watcom.calls() - w0 == 32,
                  "Original Watcom shuffle consumes exactly two draws per 16 entrants");
        }
        const Bytes before = save_stream(state);
        r.events.push_back(event);
        check(save_stream(state) == before, "retained event is owned and observer is read-only");
    };
    MinuteReport report;
    advance_cup(r.state, 0, r.watcom, report, &r.game,
                observe ? SeasonCupDrawHook(hook) : SeasonCupDrawHook{});
    check(!report.unsupported, "native DFB follow-up advances without unsupported work");
    const bool eligible = world_mode && !hide_option && !first_leg && !final_stage;
    if (observe && eligible) check(r.events.size() == 2, "exact draw/overview event pair");
    else check(r.events.empty(), "empty observer, display gate, first leg, or final stage suppresses events");
    if (!first_leg && !final_stage)
        check(r.watcom.calls() - w0 == 32, "follow-up Watcom calls do not depend on observation or display");
    return r;
}

void same(const Result& a, const Result& b) {
    check(save_stream(a.state) == save_stream(b.state) && a.game.seed() == b.game.seed() &&
          a.game.calls() == b.game.calls() && a.watcom.seed() == b.watcom.seed() &&
          a.watcom.calls() == b.watcom.calls(),
          "observer leaves complete native state and both random generators unchanged");
}
}

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    if (!*argv[1]) { std::cout << "SKIP: BMH_DATA not configured\n"; return 77; }
    try {
        const auto data = std::filesystem::path(argv[1]);
        const auto exe = bmh::test::load_executable(data / "BMH.exe");
        const auto texts = load_textd(data / "TEXTD.BLK");
        const auto before = load(argv[2], exe, texts);
        const auto after = load(argv[3], exe, texts);
        check(before.date == 1529 && before.season == 4 && before.memory.u8(::bmh::layout::CompetitionSelection) == 1 &&
              before.memory.u8(::bmh::layout::DeskPosition) == 0 &&
              before.memory.u8(kCupTable + 0x1d) == 1 && before.memory.u8(kCupTable + 0x14) == 0x2e,
              "DFBBEFORE is the actual day-66, round-one native input");
        check(after.date == 1536 && after.season == 4 && after.memory.u8(kCupTable + 0x1d) == 2,
              "DFBAFTER is separately valid day-73, round-two evidence");

        const auto plain = advance(before, false);
        const auto observed = advance(before, true);
        same(plain, observed);
        // DFBAFTER contains the actual stored first-round match results and next-round list.
        // Reopen only its played counter to the pre-advance boundary; no scores or RNG are fitted.
        auto stored_results = after;
        stored_results.memory.set8(kCupTable + 0x1d, 1);
        const auto from_original_results = advance(std::move(stored_results), true);
        std::vector<uint16_t> actual_winners = from_original_results.events.front().participants;
        std::vector<uint16_t> saved_next_round;
        const uint32_t native_next = after.memory.pointer(kCupTable) + 96u * 2u;
        for (int i = 0; i < 16; ++i) saved_next_round.push_back(after.memory.u16(native_next + uint32_t(i) * 2));
        std::sort(actual_winners.begin(), actual_winners.end());
        std::sort(saved_next_round.begin(), saved_next_round.end());
        check(actual_winners == saved_next_round,
              "actual saved round-one results reproduce the native DFBAFTER next-round participant multiset");
        check(observed.events[0].participants != observed.events[1].participants,
              "Source sort changes the shown ordering for this native fixture");

        const auto hidden_option = advance(before, true, true);
        check(hidden_option.events.empty(), "competition display-off option suppresses both events");
        const auto hidden_world = advance(before, true, false, false);
        check(hidden_world.events.empty(), "world-mode flag suppresses Original league cup UI");
        const auto first_leg = advance(before, true, false, true, true, false);
        check(first_leg.events.empty(), "two-legged first leg never announces a completed draw");
        const auto final_stage = advance(before, true, false, true, false, true);
        check(final_stage.events.empty(), "final-stage completion does not announce a next-round draw");
        const auto alternate_slot = advance(before, true, false, true, false, false, 2);
        check(alternate_slot.events.size() == 2 && alternate_slot.state.memory.u8(::bmh::layout::ActiveManager) == 2 &&
              alternate_slot.state.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField + 2) == 0x80 &&
              alternate_slot.state.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField) == 0,
              "overview effect addresses the selected manager slot, not slot zero");
        const auto invalid_slot = advance(before, true, false, true, false, false, 5);
        check(invalid_slot.events.size() == 2 && invalid_slot.state.memory.u8(::bmh::layout::ActiveManager) == 0 &&
              invalid_slot.state.memory.u8(::bmh::layout::CareerSession_DispatchOtherGames_RuntimeField) == 0x80,
              "overview effect clamps active slots above three to zero");
        std::cout << "Native DFB follow-up: round2 count16, draw→overview, 32 exact Watcom pulls, "
                     "observer neutrality and Source display gates OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
