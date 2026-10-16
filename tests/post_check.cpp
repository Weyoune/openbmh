// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
// post_check - compares the ported matchday processing (game/matchday.hpp) with the
// post-match snapshots of a recording made with `recorder.ps1 -PostOnly`.
//
// The recording starts while the final whistle screen is shown (the original waits in
// legacy routine inside the end of the second-half phase): a minute -1 snapshot with the
// match groups and seeds, then the baseline state. Each click on the result screens lets
// the game continue; whenever the seeds changed and then stood still, a "post" snapshot
// with the state follows.
//
// Post snapshot 0 is taken while the first group's table is shown, i.e. after
//   - legacy routine for every match of every group (end of the phase),
//   - the (empty) extra time phases, legacy routine and legacy routine,
//   - the matchday processing of group 0 (club table, morale, player development).
// This tool replays exactly that from the baseline and compares seeds and tables.
//
// Usage: post_check <post.jsonl> <savegame before the matchday (.FBM)> [game directory]
//   The savegame supplies what the dumps lack: club identity numbers and the date.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "capture_io.hpp"
#include "runtime_fixture_tables.hpp"
#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "game/matchday.hpp"

using namespace bmh;
using namespace bmh::capture;

namespace {

template <typename T>
int compare(const char* name, const std::vector<T>& ours, const std::vector<T>& theirs, int width) {
    size_t n = std::min(ours.size(), theirs.size());
    int diff = 0;
    std::string first;
    for (size_t i = 0; i < n; i++) {
        if (ours[i] == theirs[i]) continue;
        if (diff++ < 6) {
            char b[64];
            std::snprintf(b, sizeof b, " [%zu] %0*llx/%0*llx", i, width, (unsigned long long)ours[i], width,
                          (unsigned long long)theirs[i]);
            first += b;
        }
    }
    if (ours.size() != theirs.size()) std::printf("  %-14s size %zu vs %zu\n", name, ours.size(), theirs.size());
    std::printf("  %-14s %s", name, diff == 0 ? "OK" : "DIFF");
    if (diff) std::printf(" (%d of %zu, ported/original):%s", diff, n, first.c_str());
    std::printf("\n");
    return diff;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: post_check <post.jsonl> <savegame.FBM> [game directory]\n");
        return 1;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::ifstream in(argv[1]);
    std::string line, start;
    std::vector<std::string> states, posts;
    while (std::getline(in, line)) {
        if (line.rfind("{\"minute\":-1", 0) == 0) start = line;
        else if (line.rfind("{\"state\"", 0) == 0) states.push_back(line);
        else if (line.rfind("{\"post\"", 0) == 0) posts.push_back(line);
    }
    if (start.empty() || states.size() < 2 || posts.empty()) {
        std::printf("recording has no baseline or no post snapshot\n");
        return 1;
    }
    Savegame sg = load_savegame(argv[2]);
    const auto data = argc > 3 ? std::filesystem::path(argv[3]) : std::filesystem::path(argv[2]).parent_path();
    bmh::test::load_executable(data / "BMH.exe");
    SaveState st(sg.stream);
    GameState saved = GameState::from_save(st);
    std::vector<uint8_t> buttons(st.data(::bmh::layout::LeaguePresentationOptions), st.data(::bmh::layout::LeaguePresentationOptions) + 0xb4);  // desk buttons

    GameState g = state_from_dump(states[0]);
    g.club_ids = saved.club_ids;
    g.date = saved.date, g.season_start = saved.season_start;
    g.manager_results = saved.manager_results;
    g.manager_ratings = saved.manager_ratings;
    g.position_history = saved.position_history;
    g.league_rounds = saved.league_rounds;
    std::vector<MatchGroup> groups = groups_from_snapshot(start);
    GameRandom game(int32_t(std::stoll(field(start, "game_seed"))));
    WatcomRandom rand(uint32_t(std::stoull(field(start, "rand_seed"))));
    std::printf("baseline: %zu groups, game level %d, trend rotation %d, balance %d\n", groups.size(), g.level,
                g.trend_rotation, g.development_balance);

    if (std::getenv("POST_SEARCH")) {
        // Diagnosis: are there unexplained draws before the group processing? Try every
        // number of extra Watcom (k) and game (j) draws after the final whistle.
        GameState g0 = g;
        std::vector<MatchGroup> gr0 = groups;
        GameRandom game0 = game;
        WatcomRandom rand0 = rand;
        int32_t want_g = int32_t(std::stoll(field(posts[0], "game_seed")));
        uint32_t want_w = uint32_t(std::stoull(field(posts[0], "rand_seed")));
        for (int k = 0; k <= 40; k++)
            for (int j = 0; j <= 600; j++) {
                GameState gg = g0;
                std::vector<MatchGroup> gr = gr0;
                GameRandom ga = game0;
                WatcomRandom wa = rand0;
                for (MatchGroup& grp : gr)
                    for (int m = 0; m < grp.matches(); m++) finish_match(gg, grp, m, 90, ga);
                for (int i = 0; i < k; i++) wa.range(0, 1);
                for (int i = 0; i < j; i++) ga.range(0, 1);
                MinuteReport r;
                process_group(gg, gr[0], ga, wa, r);
                if (ga.seed() == want_g && wa.seed() == want_w) {
                    const GameState o = state_from_dump(states[1]);
                    int d = 0;
                    for (size_t i = 0; i < gg.players.size(); i++)
                        d += gg.players[i].w35c != o.players[i].w35c || gg.players[i].w368 != o.players[i].w368;
                    for (size_t i = 0; i < gg.team_morale.size(); i++) d += gg.team_morale[i] != o.team_morale[i];
                    for (size_t i = 0; i < gg.team_flags.size(); i++) d += gg.team_flags[i] != o.team_flags[i];
                    std::printf("seeds match with %d extra Watcom, %d extra game draws: %d table differences\n", k, j, d);
                }
            }
        return 0;
    }
    // End of the second-half phase (legacy routine end loop), per group: the results screen
    // (legacy routine(2, 90)) - the baseline was taken while group 0's was shown, so leaving
    // it stirs the random numbers - then legacy routine for every match.
    for (size_t gi = 0; gi < groups.size(); gi++) {
        MatchGroup& grp = groups[gi];
        int league = int(le32(grp.desc, 0xcc));
        if (league < 0x15 && league_screen_shown(buttons, 2, 90, league)) stir_random(game, rand);
        for (int m = 0; m < grp.matches(); m++) finish_match(g, grp, m, 90, game);
    }
    // Number of draws between the baseline and a post snapshot in the original (step copies
    // of the baseline generators until they reach the recorded seeds).
    auto original_draws = [&](const std::string& post) {
        GameRandom gp(int32_t(std::stoll(field(start, "game_seed"))));
        WatcomRandom wp(uint32_t(std::stoull(field(start, "rand_seed"))));
        long gn = -1, wn = -1;
        for (long k = 1; k < 400000 && gn < 0; k++) {
            gp.range(0, 1);
            if (gp.seed() == int32_t(std::stoll(field(post, "game_seed")))) gn = k;
        }
        for (long k = 1; k < 400000 && wn < 0; k++) {
            wp.range(0, 1);
            if (wp.seed() == uint32_t(std::stoull(field(post, "rand_seed")))) wn = k;
        }
        std::printf("  draws since the baseline: game %llu (original %ld), Watcom %llu (original %ld)\n",
                    (unsigned long long)game.calls(), gn, (unsigned long long)rand.calls(), wn);
    };
    auto check = [&](size_t index, const char* what) {
        const std::string& post = posts[index];
        const GameState o = state_from_dump(states[index + 1]);
        bool seeds = game.seed() == int32_t(std::stoll(field(post, "game_seed"))) &&
                     rand.seed() == uint32_t(std::stoull(field(post, "rand_seed")));
        std::printf("post snapshot %zu (%s): seeds %s\n", index, what, seeds ? "OK" : "DIFF");
        if (!seeds) original_draws(post);
        int diffs = 0;
        std::vector<uint32_t> a35c, b35c, a368, b368, a364, b364;
        for (const Player& p : g.players) a35c.push_back(p.w35c), a368.push_back(p.w368), a364.push_back(p.w364);
        for (const Player& p : o.players) b35c.push_back(p.w35c), b368.push_back(p.w368), b364.push_back(p.w364);
        diffs += compare("squad_slots", g.squad_slots, o.squad_slots, 4);
        diffs += compare("w35c", a35c, b35c, 8);
        diffs += compare("w368", a368, b368, 8);
        diffs += compare("w364", a364, b364, 8);
        diffs += compare("team_morale", g.team_morale, o.team_morale, 2);
        diffs += compare("team_flags", g.team_flags, o.team_flags, 4);
        diffs += compare("player_cards", g.player_cards, o.player_cards, 4);
        diffs += compare("player_bonus", g.player_bonus, o.player_bonus, 2);
        diffs += compare("club_table", g.club_table, o.club_table, 2);
        diffs += compare("tactics", g.tactics, o.tactics, 2);
        diffs += compare("managers", g.manager_records, o.manager_records, 2);
        std::printf("  balance        %s (%d/%d), rotation %s (%d/%d)\n",
                    g.development_balance == o.development_balance ? "OK" : "DIFF", g.development_balance,
                    o.development_balance, g.trend_rotation == o.trend_rotation ? "OK" : "DIFF", g.trend_rotation,
                    o.trend_rotation);
        return seeds && diffs == 0;
    };

    // Matchday processing, group by group; after each group its table screen
    // (legacy routine(1, 90)) - post snapshot 0 was taken while group 0's was shown.
    MinuteReport report;
    bool ok = true;
    for (size_t gi = 0; gi < groups.size() && !report.unsupported; gi++) {
        process_group(g, groups[gi], game, rand, report);
        if (report.unsupported) break;
        int league = int(le32(groups[gi].desc, 0xcc));
        if (gi == 0) {
            ok = check(0, "after group 0, table shown") && ok;
            if (league < 0x15 && league_screen_shown(buttons, 1, 90, league)) stir_random(game, rand);
            continue;
        }
        if (league < 0x15 && league_screen_shown(buttons, 1, 90, league)) {
            std::printf("group %zu shows its table - the recording has no snapshot for it\n", gi);
            stir_random(game, rand);
        }
    }
    if (report.unsupported) std::printf("not supported: %s\n", report.reason.c_str());
    if (posts.size() > 1) check(1, "after all groups (and possibly more)");
    return ok ? 0 : 2;
}
