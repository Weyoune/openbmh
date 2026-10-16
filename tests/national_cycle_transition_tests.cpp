// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

#include "app/career_session.hpp"
#include "formats/binary.hpp"
#include "game/cup.hpp"
#include "game/national.hpp"
#include "game/save.hpp"

namespace {
using namespace bmh;
void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
TurnDeskKind advance(app::CareerSession& session, uint64_t& revision) {
    ui::DeskRequest request;
    request.kind = ui::DeskRequestKind::advance_turn;
    request.manager_slot = session.state().memory.u8(::bmh::layout::ActiveManager);
    request.revision = revision;
    std::string error;
    const auto result = session.dispatch_kickoff(request, revision, error);
    check(result.kind == TurnDeskKind::NextDesk || result.kind == TurnDeskKind::SeasonEndDesk,
          "national cycle stopped: " + error + "; " + result.reason);
    return result.kind;
}
struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            path = std::filesystem::temp_directory_path() /
                ("openbmh-national-cycle-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(path)) return;
        }
        throw std::runtime_error("cannot create isolated national-cycle save directory");
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) return 2;
    if (!*argv[1]) { std::cout << "SKIP: BMH_DATA not configured\n"; return 77; }
    try {
        // Existing Original world-only nomination save; legacy pending caller is
        // explicit. Interactive national offer ownership has separate App tests.
        app::CareerSession session(argv[1], 1, argv[2]);
        check(session.pending_offer_entry_policy() == app::PendingOfferEntryPolicy::legacy_unpresented,
              "this cycle test must retain its explicit legacy pending contract");
        check(session.state().season == 0 && session.state().date == 4 &&
              session.state().memory.u32(::bmh::layout::CompetitionSelection) == 2 && session.state().memory.u8(::bmh::layout::CareerDuration) == 5,
              "NATWK001 must be the unchanged unlimited world-only season-zero input");
        check(national_coach_active(session.state()), "Original starts at a real national nomination desk");
        const auto club_identity = session.state().club_ids.at(
            le16(session.state().manager_records, 4 * 0xb0c + 0x290));
        const auto owns_club = [&](const GameState& state) {
            if (state.manager_teams.size() != 1 || state.manager_teams[0] < 0 ||
                state.manager_teams[0] >= kNoTeam) return false;
            return state.club_ids.at(size_t(state.manager_teams[0])) == club_identity;
        };
        std::array<uint8_t, 80> old_honours;
        for (uint32_t i = 0; i < old_honours.size(); ++i)
            old_honours[i] = session.state().memory.u8(::bmh::layout::Placements + i);
        std::set<int> played_types, annual_openings, year_endings;
        int european_titles = 0, world_titles = 0;
        uint16_t european_champion = 0xffff, world_champion = 0xffff;
        session.set_season_pause([&](const char* screen, const GameState& state) {
            if (std::string(screen) != "the German entrants (legacy routine)") return;
            check(!national_coach_active(state) && owns_club(state),
                  "annual competition setup did not receive the restored club owner");
            annual_openings.insert(state.season);
        });
        session.set_live_minute([&](int, MatchEngine&, const std::vector<MatchGroup>& groups,
                                   const GameState& state) {
            for (const auto& group : groups) {
                const auto descriptor = le32(group.desc, 0xcc);
                if (descriptor < kCupTable || descriptor >= kCupTable + kCupEntries * kCupEntrySize) continue;
                const int type = int16_t(state.memory.u16(descriptor + 0x18));
                if (type < 4 || type > 7) continue;
                check(type == 7 - state.season % 4,
                      "calendar played the wrong member of the four-year national cycle");
                check(!group.fixtures.empty(), "national event must play actual fixtures");
                played_types.insert(type);
            }
        });
        SportschauPresentationHooks hooks;
        hooks.winner = [&](const GameState& state, const CompetitionWinnerView& winner) {
            if (winner.title == 6) {
                check(state.season == 1, "European final belongs to the second year of the cycle");
                check(winner.team >= kNoTeam && winner.team < kNoTeam + 62,
                      "European final selected an invalid national champion");
                european_champion = winner.team;
                ++european_titles;
            } else if (winner.title == 5) {
                check(state.season == 3, "World Cup final belongs to the fourth year of the cycle");
                world_champion = winner.team;
                ++world_titles;
                std::set<uint16_t> places;
                const std::array<uint32_t, 4> finalists{::bmh::layout::Cup_NationalTournamentEnd_RuntimeField, ::bmh::layout::Cup_NationalTournamentEnd_RuntimeField2, ::bmh::layout::Cup_NationalTournamentEnd_RuntimeField3, ::bmh::layout::Cup_NationalTournamentEnd_RuntimeField4};
                for (uint32_t i = 0; i < 4; ++i) {
                    const uint16_t team = state.memory.u16(finalists[i]);
                    check(team >= kNoTeam && team < kNoTeam + 62,
                          "World Cup honours contain an invalid national team");
                    places.insert(team);
                    check(state.memory.u8(::bmh::layout::Placements + i) == uint8_t((team - kNoTeam) | 0x80),
                          "World Cup final does not store its real four national places");
                }
                check(places.size() == 4 && state.memory.u16(finalists[0]) == winner.team,
                      "World Cup honours disagree with the actual distinct finalists/champion");
                for (uint32_t i = 0; i < 76; ++i)
                    check(state.memory.u8(::bmh::layout::Cup_NationalTournamentEnd_RuntimeField5 + i) == old_honours[i],
                          "World Cup completion did not shift the old honours by four bytes");
            }
        };
        session.set_sportschau_presentation_hooks(std::move(hooks));
        const TemporaryDirectory temporary;
        const auto path = temporary.path / "CYCLE.FBM";
        uint64_t revision = 1;
        int turns = 0;
        while (turns++ < 100) {
            const int before = session.state().season;
            const auto kind = advance(session, revision);
            const auto& state = session.state();
            if (kind == TurnDeskKind::SeasonEndDesk) {
                check(!national_coach_active(state) && owns_club(state) && state.memory.u8(::bmh::layout::CoachActive) == 0,
                      "season-end desk did not restore the actual club manager");
                year_endings.insert(state.season);
            }
            if (state.season != before) {
                // World-only mode deliberately suppresses empty opening club
                // desks. The actual return can already be a national fixture.
                check(state.season == before + 1,
                      "one annual transition skipped a national competition year");
                std::cout << "year " << state.season << " date " << state.date
                          << " next national competition " << 7 - state.season % 4 << '\n';
            }
            if (state.season == 4) {
                check(played_types == std::set<int>({4, 5, 6, 7}) &&
                      annual_openings == std::set<int>({1, 2, 3, 4}) &&
                      year_endings == std::set<int>({0, 1, 2, 3}) &&
                      european_titles == 1 && world_titles == 1,
                      "four natural years do not complete all national phases and both finals");
                check(state.memory.u16(::bmh::layout::Titles + 6 * 100 + 1 * 2) == european_champion &&
                      state.memory.u16(::bmh::layout::Titles + 5 * 100 + 3 * 2) == world_champion,
                      "the real European/World champions are missing from their saved title histories");
                std::string error;
                auto expected_rand = session.rand();
                const auto saved_game = session.game();
                const auto bytes = savegame_bytes(make_savegame(state, expected_rand));
                const auto native = save_stream(state);
                check(session.save_game(path, state.memory.u8(::bmh::layout::ActiveManager), true, error), error);
                if (argc == 4)
                    std::filesystem::copy_file(path, argv[3], std::filesystem::copy_options::overwrite_existing);
                check(read_file(path) == bytes && save_stream(state) == native &&
                      session.rand().seed() == expected_rand.seed() &&
                      session.rand().calls() == expected_rand.calls() &&
                      session.game().seed() == saved_game.seed() && session.game().calls() == saved_game.calls(),
                      "normal four-year opening save differs from native writer/state/RNG");
                app::CareerSession loaded(argv[1], session.rand().seed(), path);
                app::CareerSession control(argv[1], session.rand().seed(), path);
                // legacy routine finishes loading by calling legacy routine again. It copies
                // the saved active manager into the previous-active byte ClubPlayerDetail_InvalidArgument_RuntimeField3.
                // This is an expected-state oracle only, never a live input edit.
                auto expected_load = state;
                if (national_coach_active(state))
                    expected_load.memory.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField3, state.memory.u8(::bmh::layout::ActiveManager));
                check(load_savegame(path).checksum_ok && save_stream(loaded.state()) == save_stream(expected_load) &&
                      loaded.game().calls() == 0 && loaded.rand().calls() == 0,
                      "normal national save differs beyond Source's previous-active normalization");
                uint64_t loaded_revision = 1, control_revision = 1;
                advance(loaded, loaded_revision); advance(control, control_revision);
                check(save_stream(loaded.state()) == save_stream(control.state()) &&
                      loaded.game().seed() == control.game().seed() && loaded.rand().seed() == control.rand().seed() &&
                      loaded.game().calls() == control.game().calls() && loaded.rand().calls() == control.rand().calls(),
                      "two fresh next-cycle continuations differ in native state or either RNG");
                std::cout << "Four national calendar years, both real finals, honours, club handback and next-cycle reload OK; "
                          << turns << " ordinary desk advances\n";
                return 0;
            }
        }
        throw std::runtime_error("four national years did not complete within 100 ordinary desk advances");
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
