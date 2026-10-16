// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include "app/career_session.hpp"
#include "formats/binary.hpp"
#include "formats/savestate.hpp"
#include "game/matchday_setup.hpp"
#include "game/save.hpp"

using namespace bmh;
namespace {
void check(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
void set_f6(app::CareerSession& session, uint64_t& revision, uint8_t value) {
    if (session.state().memory.u8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField) == value) return;
    std::string error;
    const auto desk = session.desktop_projection(revision);
    const auto item = std::find_if(desk.items.begin(), desk.items.end(),
        [](const auto& i) { return i.kind == 19 && !i.disabled; });
    check(item != desk.items.end(), "normal desk has Settings item");
    ui::DeskRequest origin{ui::DeskRequestKind::open_menu, desk.manager_slot, item->id, 19, -1, revision};
    auto settings = session.open_desk_settings(origin, revision, error);
    check(bool(settings), "owned Settings opens");
    const auto game_calls = session.game().calls(), watcom_calls = session.rand().calls();
    ui::SettingsWriteRequest write{settings->owner.manager_slot, revision, settings->state.cells,
        5, 5, ui::SettingsEdit::toggle_binary};
    auto changed = session.dispatch_desk_settings(app::CareerSession::DeskSettingsAction{
        app::CareerSession::DeskSettingsWrite{settings->owner.sequence, write}}, revision, error);
    check(bool(changed), "F6 has a consumer and can be changed through its real owner");
    check(session.state().memory.u8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField) == value, "F6 writes the Original raw byte");
    auto closed = session.dispatch_desk_settings(app::CareerSession::DeskSettingsAction{
        app::CareerSession::DeskSettingsClose{changed->owner}}, revision, error);
    check(closed && closed->closed, "Settings closes using its updated owner");
    check(session.game().calls() == game_calls && session.rand().calls() == watcom_calls,
        "editing F6 consumes neither RNG");
}
TurnAdvanceResult advance(app::CareerSession& session, uint64_t& revision) {
    std::string error;
    ui::DeskRequest request{ui::DeskRequestKind::advance_turn,
        int(session.state().memory.u8(::bmh::layout::ActiveManager)), -1, 0, -1, revision};
    auto result = session.dispatch_kickoff(request, revision, error);
    if (result.kind != TurnDeskKind::NextDesk) throw std::runtime_error(result.reason + "; " + error);
    return result;
}
void gate_cases(GameState g) {
    check(g.season_day() != 0 && g.memory.u8(::bmh::layout::CareerSession_RuntimeError_RuntimeField4) == 0, "fixture is mid-season");
    g.memory.set8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField, 0);
    MatchGroup group;
    group.desc.assign(0xd0, 0);
    group.desc[2] = 1;
    group.desc[0xcc] = 3;
    group.fixtures = {g.manager_teams[0], 8};
    const size_t index = 3 * 0x17c + size_t(g.league_rounds[3]) * 10;
    g.results.at(index) = 0xcc;
    check(matchday_needs_manager_desks(g, 8, {group}), "unplayed human fixture admits all desks");
    for (uint8_t result : {uint8_t(0xcd), uint8_t(0xce), uint8_t(0), uint8_t(0xff)}) {
        g.results.at(index) = result;
        check(!matchday_needs_manager_desks(g, 8, {group}), "postponed, played and missing fixtures do not admit desks");
    }
    group.fixtures = {8, 9};
    g.results.at(index) = 0xcc;
    check(!matchday_needs_manager_desks(g, 8, {group}), "AI-only event skips F6-on desks");
    g.memory.set8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField, 1);
    check(matchday_needs_manager_desks(g, 8, {group}), "F6-off retains AI-only event desks");
    check(!matchday_needs_manager_desks(g, 8, {}), "F6-off alone does not create an empty event desk");
    g.memory.set8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField, 0);
    g.manager_teams.push_back(8);
    check(matchday_needs_manager_desks(g, 8, {group}), "second manager playing admits the entire desk loop");
    g.manager_teams.pop_back();
    g.memory.add_block(::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField, 1);
    g.memory.set8(::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField, 1);
    check(matchday_needs_manager_desks(g, 8, {}), "Source auto-continue latch forces its desk loop");
    g.memory.set8(::bmh::layout::BalancePlan_BalanceFromGame_RuntimeField, 0);
    g.memory.set8(::bmh::layout::CareerSession_RuntimeError_RuntimeField4, 1);
    check(matchday_needs_manager_desks(g, 8, {}), "season closing survives F6-on");
    g.memory.set8(::bmh::layout::CareerSession_RuntimeError_RuntimeField4, 0);
    g.memory.set8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField, 1);
    g.memory.set32(::bmh::layout::CompetitionSelection, 2);
    check(!matchday_needs_manager_desks(g, 8, {group}), "world-only mode suppresses ordinary F6-off desks");
    group.fixtures[0] = g.manager_teams[0];
    check(matchday_needs_manager_desks(g, 8, {group}), "world-only mode still admits a human fixture");
    g.memory.set32(::bmh::layout::CompetitionSelection, 3);
    g.memory.set8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField, 0);
    const uint32_t cup = 0xf0000; // isolated unit-test cup entry, outside saved blocks
    g.memory.add_block(cup, 0x20);
    for (int byte = 0; byte < 4; ++byte) group.desc[0xcc + byte] = uint8_t(cup >> (byte * 8));
    g.memory.set8(cup + 0x18, 12);
    check(matchday_needs_manager_desks(g, 0x20, {group}), "cup participant uses cup context instead of league result bytes");
    g.memory.set8(cup + 0x18, 25);
    check(!matchday_needs_manager_desks(g, 0x20, {group}), "Source context threshold is below 30");
    g.memory.set16(::bmh::layout::FriendlyDays, uint16_t(g.season_day()));
    g.memory.set16(::bmh::layout::FriendlyTeams, kNoTeam);
    check(matchday_needs_manager_desks(g, 0x4001, {}), "national friendly retains its forced desk");
    g.date -= uint32_t(g.season_day());
    check(matchday_needs_manager_desks(g, 0, {}), "season opening desk survives F6-on");
}
}
int main(int argc, char** argv) { try {
    check(argc == 3 && *argv[1], "Original data and unchanged WEEK196 are required");
    app::CareerSession on(argv[1], 8128, argv[2]), off(argv[1], 8128, argv[2]);
    check(on.state().manager_teams == std::vector<int>{45} && on.state().date == 1725 &&
        on.state().memory.u8(::bmh::layout::MatchdaySetup_MatchdayNeedsManagerDesks_RuntimeField) == 0, "normal Bayern WEEK196 binds genuine F6-on prepared desk");
    gate_cases(on.state());
    uint64_t on_revision = 1, off_revision = 1;
    set_f6(off, off_revision, 1);
    check(advance(off, off_revision).date == 1729, "F6-off stops at the genuine AI-only day 266");
    check(advance(on, on_revision).date == 1732, "F6-on simulates day 266 and reaches human day 269");
    check(on.state().league_rounds[4] == off.state().league_rounds[4] + 1,
        "skipped Regionalliga event actually runs and advances its round");
    check(on.state().league_rounds[2] == off.state().league_rounds[2],
        "upcoming human fixture is prepared, not prematurely played");
    auto toggled = app::CareerSession(argv[1], 8128, argv[2]);
    uint64_t revision = 1;
    set_f6(toggled, revision, 1); set_f6(toggled, revision, 0);
    advance(toggled, revision);
    check(save_stream(toggled.state()) == save_stream(on.state()) &&
        toggled.game().seed() == on.game().seed() && toggled.game().calls() == on.game().calls() &&
        toggled.rand().seed() == on.rand().seed() && toggled.rand().calls() == on.rand().calls(),
        "owned off/on round trip preserves the complete normal Native and both RNG trajectories");
    std::cout << "Source F6 gates and normal owned-settings/calendar/RNG integration passed\n";
    return 0;
} catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }
