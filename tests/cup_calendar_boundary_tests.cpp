// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <array>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

#include "app/career_session.hpp"
#include "formats/binary.hpp"
#include "game/cup.hpp"
#include "game/matchday_setup.hpp"
#include "game/save.hpp"

namespace {
using namespace bmh;
void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

void calendar_domain() {
    // A deliberately synthetic, exhaustive unit check of legacy routine's domain.
    // It is separate from the unchanged native-save continuation below.
    GameState state;
    state.memory.add_block(::bmh::layout::CalendarState_FormatError_RuntimeField, 1);
    state.memory.add_block(::bmh::layout::CalendarState_FormatError_RuntimeField2, 1);
    state.memory.add_block(::bmh::layout::CompetitionSelection, 4);
    uint32_t combined = 0;
    for (int year = 0; year < 4; ++year) {
        state.season = year;
        for (unsigned flags = 0; flags < 8; ++flags) {
            state.memory.set8(::bmh::layout::CalendarState_FormatError_RuntimeField, uint8_t(flags & 1));
            state.memory.set8(::bmh::layout::CalendarState_FormatError_RuntimeField2, uint8_t((flags >> 1) & 1));
            state.memory.set32(::bmh::layout::CompetitionSelection, flags & 4 ? 2 : 0);
            for (unsigned word = 0; word <= 0xffff; ++word) {
                const uint32_t competitions = day_competitions(state, uint16_t(word));
                check((competitions & (1u << 17)) == 0,
                      "calendar dispatched the friendly planner as cup ID12");
                check((competitions & ~0x85ffffu) == 0,
                      "calendar produced a competition outside the Source domain");
                combined |= competitions;
            }
        }
    }
    check(combined == 0x85ffff, "exhaustive calendar domain changed");
    std::cout << "calendar: 2097152 words/gates/years, cup ID12 never dispatched\n";
}

void source_stages(const LeImage& exe, const GameState& state) {
    std::set<int> types;
    for (int entry = 0; entry < kCupEntries; ++entry)
        types.insert(int16_t(exe.u16(cup_entry_address(entry) + 0x18)));
    check(types == std::set<int>({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}),
          "German Original cup schema gained a type requiring review");

    std::set<int> visited;
    std::set<int> schedules;
    const auto& memory = state.memory;
    const auto visit = [&](auto&& self, int entry, int type) -> void {
        check(entry >= 0 && entry < kCupEntries, "Source stage link outside the cup table");
        if (!visited.insert(entry).second) return;
        const uint32_t at = cup_entry_address(entry);
        check(int16_t(memory.u16(at + 0x18)) == type, "Source stage link changes cup type");
        check(memory.u16(at + 0x18) == exe.u16(at + 0x18) &&
              memory.u8(at + 0x16) == exe.u8(at + 0x16) &&
              memory.u8(at + 0x17) == exe.u8(at + 0x17),
              "normal input differs from the German Original stage topology");
        if (!(memory.u8(at + 0x14) & 4)) {
            const int teams = cup_group_schedule_teams(memory, at);
            check(teams != 0 && memory.u8(at + 0x1a) <= teams,
                  "reachable Source group has an unsupported round table");
            schedules.insert(teams);
        }
        for (uint32_t offset : {0x16u, 0x17u}) {
            const int next = memory.u8(at + offset);
            if (next != 0) self(self, next, type);
        }
    };
    for (int type : types) {
        if (type == 12) continue;  // Entry30 is the plan, not a played competition.
        const int first = find_cup(memory, type);
        check(first >= 0, "normal input lost an Original competition");
        visit(visit, first, type);
    }
    check(visited.size() == 34 && !visited.count(17) && !visited.count(30),
          "Source reachable stages changed (entry17 is an unlinked old group)");
    check(schedules == std::set<int>({4, 6}), "German cup group schedule domain changed");
    std::cout << "schema: all14 Original IDs, 34 reachable played stages, schedules4/6; "
                 "entry17 unlinked, entry30 planner\n";
}

void native_friendly(const std::filesystem::path& data, const std::filesystem::path& input) {
    auto session = std::make_unique<app::CareerSession>(data, 8128, input);
    const auto& initial = session->state();
    check(initial.date == 0 && initial.season == 0 && initial.manager_teams.size() == 2 &&
          initial.memory.u8(cup_entry_address(30) + 0x1a) == 2 &&
          initial.memory.u16(::bmh::layout::FriendlyDays) == 1,
          "M93 normal GUI booking input changed");
    const auto exe = load_le(data / "BMH.exe");
    const auto before = save_stream(initial);
    source_stages(exe, initial);
    check(save_stream(initial) == before, "schema inspection mutated the normal input");
    const std::array<uint16_t, 2> booked{initial.memory.u16(::bmh::layout::FriendlyTeams), initial.memory.u16(::bmh::layout::FixtureSecondBookedFriendly)};
    bool observed = false;
    session->set_live_minute([&](int, MatchEngine&, const std::vector<MatchGroup>& groups,
                                const GameState& state) {
        for (const auto& group : groups) {
            if (group.desc.at(0) == 0) continue;
            const int type = int(group.desc.at(0)) - 1;
            check(type != 12, "normal kickoff played the planning list");
            if (type != 10) continue;
            check(state.season_day() == 1 && group.fixtures.size() == 2 &&
                  group.fixtures[0] == booked[0] && group.fixtures[1] == booked[1] &&
                  le32(group.desc, 0xcc) == cup_entry_address(24),
                  "normal friendly did not consume the actual booked pair at entry24");
            check(state.memory.u8(cup_entry_address(30) + 0x1a) == 0 &&
                  state.memory.u16(::bmh::layout::FriendlyDays) == 0,
                  "normal preparation did not remove the consumed planning entry");
            observed = true;
        }
    });
    uint64_t revision = 1;
    for (int turns = 0; turns < 8; ++turns) {
        ui::DeskRequest request;
        request.kind = ui::DeskRequestKind::advance_turn;
        request.manager_slot = session->state().memory.u8(::bmh::layout::ActiveManager);
        request.revision = revision;
        std::string error;
        const auto result = session->dispatch_kickoff(request, revision, error);
        check(result.kind == TurnDeskKind::NextDesk,
              "normal two-manager continuation stopped: " + error + "; " + result.reason);
        const auto& state = session->state();
        if (observed && state.season_day() > 1) {
            check(state.memory.u8(cup_entry_address(24) + 0x1a) == 0 &&
                  (day_competitions(state, state.memory.u16(::bmh::layout::CompetitionCalendar + 2)) & (1u << 15)) == 0,
                  "completed friendly retained today's teams or calendar dispatch");
            std::cout << "native M93: planned ID12 -> actual ID10 pair " << booked[0] << '/' << booked[1]
                      << ", completed and reached next desk at date " << state.date
                      << ", G=" << session->game().seed() << '/' << session->game().calls()
                      << ", W=" << session->rand().seed() << '/' << session->rand().calls() << '\n';
            return;
        }
    }
    throw std::runtime_error("normal friendly did not complete within eight manager desks");
}
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    if (!*argv[1]) { std::cout << "SKIP: BMH_DATA not configured\n"; return 77; }
    try {
        calendar_domain();
        native_friendly(argv[1], argv[2]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
