// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
// kickoff_check - replays the kick-off of a matchday with a human team, starting at the
// manager's pre-match desk, and compares it with a recording (runtime verification).
//
// The recording must start at the desk: its first line is the minute -1 snapshot (seeds and
// the match groups as the matchday setup left them), followed by the "post 0" pause (the
// kick-off is done, seeds as at minute 1) and the minute 1 snapshot with the side records
// and attendances.
//
// The savegame is the one saved at that desk (loaded into a freshly started game, so all
// run-time globals such as the last chance's weather are at their start values).
//
// Usage: kickoff_check <capture.jsonl> <savegame saved at the desk (.FBM)>
//   TEXTD.BLK is read from the savegame's directory (messages of the table prediction).
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "capture_io.hpp"
#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "formats/textd.hpp"
#include "game/matchday_setup.hpp"
#include "game/week.hpp"

using namespace bmh;
using namespace bmh::capture;

namespace {

// Number of draws that lead from `from` to the seed `to` (-1 = not within the limit).
long game_distance(GameRandom from, int32_t to) {
    for (long k = 0; k < 200000; k++) {
        if (from.seed() == to) return k;
        from.range(0, 1);
    }
    return -1;
}
long watcom_distance(WatcomRandom from, uint32_t to) {
    for (long k = 0; k < 200000; k++) {
        if (from.seed() == to) return k;
        from.rand();
    }
    return -1;
}

int compare_bytes(const char* name, const std::vector<uint8_t>& ours, const std::vector<uint8_t>& theirs) {
    int diffs = 0;
    std::string first;
    for (size_t i = 0; i < ours.size() && i < theirs.size(); i++) {
        if (ours[i] == theirs[i]) continue;
        if (diffs++ < 8) {
            char b[32];
            std::snprintf(b, sizeof b, " [%zu] %02x/%02x", i, ours[i], theirs[i]);
            first += b;
        }
    }
    if (ours.size() != theirs.size()) std::printf("  %s: size %zu/%zu\n", name, ours.size(), theirs.size());
    if (diffs) std::printf("  %-12s DIFF (%d, ported/original):%s\n", name, diffs, first.c_str());
    return diffs;
}

}  // namespace

int run(int argc, char** argv);

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 2;
    }
}

int run(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: kickoff_check <capture.jsonl> <savegame.FBM>\n");
        return 2;
    }
    std::ifstream in(argv[1]);
    std::vector<std::string> lines;
    for (std::string l; std::getline(in, l);) lines.push_back(l);
    // desk (minute -1), post 0 + state, minute 1 + state
    size_t idesk = lines.size(), ipost = lines.size(), iminute = lines.size();
    for (size_t i = 0; i < lines.size(); i++) {
        if (idesk == lines.size() && field(lines[i], "minute") == "-1") idesk = i;
        if (ipost == lines.size() && idesk < i && field(lines[i], "post") == "0") ipost = i;
        if (iminute == lines.size() && ipost < i && field(lines[i], "minute") == "1") iminute = i;
    }
    if (iminute >= lines.size()) {
        std::fprintf(stderr, "the recording has no desk / post 0 / minute 1 sequence\n");
        return 2;
    }
    const std::string& desk = lines[idesk];
    const std::string& post = lines[ipost];
    const std::string& minute1 = lines[iminute];

    Savegame sg = load_savegame(argv[2]);
    SaveState st(sg.stream);
    GameState g = GameState::from_save(st);
    const TextDb texts = load_textd(std::filesystem::path(argv[2]).parent_path() / "TEXTD.BLK");
    g.texts = &texts;

    const int32_t desk_game = int32_t(std::stoll(field(desk, "game_seed")));
    const uint32_t desk_rand = uint32_t(std::stoull(field(desk, "rand_seed")));
    const int32_t saved_game = g.memory.has(::bmh::layout::CareerSession_Save_RuntimeField) ? int32_t(g.memory.u32(::bmh::layout::CareerSession_Save_RuntimeField)) : -1;
    std::printf("desk: date %s, game seed %d (savegame %d), Watcom seed %u\n", field(desk, "date").c_str(), desk_game,
                saved_game, desk_rand);

    GameRandom game(desk_game);
    WatcomRandom rand(desk_rand);
    std::vector<MatchGroup> groups = groups_from_snapshot(desk);
    MatchdayContext ctx;
    ctx.month = std::stoi(field(desk, "month"));
    ctx.season_day = int(st.data(::bmh::layout::DaysIntoSeason)[0] | st.data(::bmh::layout::DaysIntoSeason)[1] << 8);
    ctx.weekday = weekday_code(g.date);
    MinuteReport report;
    int draws_game = 0, draws_watcom = 0;
    draw_hook = nullptr;
    const uint64_t g0 = game.calls(), w0 = rand.calls();
    try {
        kick_off(g, groups, ctx, rand, game, report);
    } catch (const std::exception& ex) {
        report.fail(std::string("exception: ") + ex.what());
    }
    draws_game = int(game.calls() - g0), draws_watcom = int(rand.calls() - w0);
    if (report.unsupported) std::printf("not supported: %s\n", report.reason.c_str());

    const int32_t post_game = int32_t(std::stoll(field(post, "game_seed")));
    const uint32_t post_rand = uint32_t(std::stoull(field(post, "rand_seed")));
    std::printf("kick-off: %d game and %d Watcom draws; the original: %ld and %ld\n", draws_game, draws_watcom,
                game_distance(GameRandom(desk_game), post_game), watcom_distance(WatcomRandom(desk_rand), post_rand));
    const bool seeds = game.seed() == post_game && rand.seed() == post_rand;
    std::printf("seeds after the kick-off: %s\n", seeds ? "OK" : "DIFF");

    // side records and attendances against the minute 1 snapshot
    const std::vector<MatchGroup> rec = groups_from_snapshot(minute1);
    int diffs = 0, heap_attitudes = 0;
    for (size_t k = 0; k < groups.size() && k < rec.size(); k++) {
        // The home side's initial attitude compares against the away record before that is
        // built (uninitialised heap memory, see build_side_records): adopt the recorded byte.
        for (size_t m = 0; m * 18 + 8 < groups[k].records.size() && m * 18 + 8 < rec[k].records.size(); m++) {
            uint8_t& ours = groups[k].records[m * 18 + 8];
            if (ours != rec[k].records[m * 18 + 8]) heap_attitudes++, ours = rec[k].records[m * 18 + 8];
        }
        char name[32];
        std::snprintf(name, sizeof name, "g%zu sides", k);
        diffs += compare_bytes(name, groups[k].records, rec[k].records);
        std::vector<uint8_t> a, b;
        for (uint32_t s : groups[k].spectators) for (int i = 0; i < 4; i++) a.push_back(uint8_t(s >> (8 * i)));
        for (uint32_t s : rec[k].spectators) for (int i = 0; i < 4; i++) b.push_back(uint8_t(s >> (8 * i)));
        std::snprintf(name, sizeof name, "g%zu crowd", k);
        diffs += compare_bytes(name, a, b);
    }
    // tables at the post 0 pause
    const GameState o = state_from_dump(lines[ipost + 1]);
    diffs += compare_bytes("tactics", g.tactics, o.tactics);
    diffs += compare_bytes("managers", g.manager_records, o.manager_records);
    std::vector<uint8_t> a364, b364;
    for (const Player& p : g.players) for (int i = 0; i < 4; i++) a364.push_back(uint8_t(p.w364 >> (8 * i)));
    for (const Player& p : o.players) for (int i = 0; i < 4; i++) b364.push_back(uint8_t(p.w364 >> (8 * i)));
    diffs += compare_bytes("w364", a364, b364);
    if (heap_attitudes)
        std::printf("  %d home attitudes read uninitialised heap memory (recorded values adopted)\n", heap_attitudes);
    std::printf("%s\n", seeds && diffs == 0 ? "kick-off OK" : "kick-off differs");
    return seeds && diffs == 0 ? 0 : 1;
}
