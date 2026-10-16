// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
// init_check - compares the start of a new career (game/career.hpp) with a recording of the
// original made from the options screen to the first desk (ref/2p/init_capture.jsonl).
//
// Usage: init_check <init_capture.jsonl> <M3D.MAN of the game directory>
//
// The recording starts in the options dialog (legacy routine), i.e. after legacy routine and
// the first part of legacy routine. The game generator starts at 0; the Watcom seed of the
// program start (clock) is unknown, but the ages and skills legacy routine draws for every
// club player reveal the seed before that loop, and the draws before it are counted back.
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "capture_io.hpp"
#include "runtime_fixture_tables.hpp"
#include "formats/fbm.hpp"
#include "formats/le.hpp"
#include "formats/savestate.hpp"
#include "formats/m3d.hpp"
#include "formats/textd.hpp"
#include "game/career.hpp"
#include "game/desk.hpp"
#include "game/matchday.hpp"
#include "game/matchday_setup.hpp"
#include "game/season.hpp"
#include "game/week.hpp"
#include "game/weekly.hpp"

using namespace bmh;
using namespace bmh::capture;

namespace {

struct Post {
    uint32_t game_seed = 0, rand_seed = 0;
    std::string state;
};

template <typename T>
int compare(const char* name, const std::vector<T>& ours, const std::vector<T>& theirs) {
    int diff = 0;
    std::string first;
    for (size_t i = 0; i < std::min(ours.size(), theirs.size()); i++) {
        if (ours[i] == theirs[i]) continue;
        if (diff++ < 6) {
            char b[64];
            std::snprintf(b, sizeof b, " [%zu] %llx/%llx", i, (unsigned long long)ours[i], (unsigned long long)theirs[i]);
            first += b;
        }
    }
    if (ours.size() != theirs.size()) std::printf("  %-12s size %zu vs %zu\n", name, ours.size(), theirs.size());
    std::printf("  %-12s %s", name, diff ? "DIFF" : "OK");
    if (diff) std::printf(" (%d of %zu, ported/original):%s", diff, std::min(ours.size(), theirs.size()), first.c_str());
    std::printf("\n");
    return diff;
}

uint32_t watcom_back(uint32_t seed, long steps) {
    uint32_t inv = 0x41C64E6Du;  // inverse of the multiplier modulo 2^32 (Newton iteration)
    for (int i = 0; i < 5; i++) inv *= 2u - 0x41C64E6Du * inv;
    for (long i = 0; i < steps; i++) seed = (seed - 0x3039u) * inv;
    return seed;
}

// The Watcom seed before legacy routine's age/skill loop: the skills of the first club player
// fix 14 bits of the seed after its skill draw, the following players decide the rest.
// `ids` = the club players in squad order at the time of the loop.
bool find_loop_seed(const GameState& rec, const std::vector<int>& ids, uint32_t& seed) {
    auto age = [&](int id) { return int(rec.players.at(size_t(id)).w364 >> 21 & 0x1f); };
    auto skills = [&](int id) { return int(rec.players.at(size_t(id)).w368 & 0x3fff); };
    for (uint32_t c = 0; c < (1u << 18); c++) {
        const uint32_t x2 = uint32_t(skills(ids[0])) << 17 | (c >> 17) << 31 | (c >> 16 & 1) << 16 | (c & 0xffff);
        WatcomRandom r(x2);
        size_t ok = 1;
        while (ok < ids.size() && ok < 40 && r.range(4, 16) == age(ids[ok]) && r.range(0, 0x3fff) == skills(ids[ok])) ok++;
        if (ok < 40) continue;
        seed = watcom_back(x2, 2);
        return true;  // (the other solution differs in bit 31 only and gives the same numbers)
    }
    return false;
}

}  // namespace

int run(int argc, char** argv);

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::printf("error: %s\n", e.what());
        return 3;
    }
}

int run(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: init_check <init_capture.jsonl> <M3D.MAN>\n");
        return 1;
    }
    std::ifstream in(argv[1]);
    std::vector<Post> posts;
    std::string line;
    bool want_state = false;
    while (std::getline(in, line)) {
        if (line.rfind("{\"post\":", 0) == 0 || line.rfind("{\"minute\":", 0) == 0) {
            Post p;
            p.game_seed = uint32_t(std::stoul(field(line, "game_seed")));
            p.rand_seed = uint32_t(std::stoul(field(line, "rand_seed")));
            posts.push_back(p);
            want_state = true;
        } else if (want_state && line.rfind("{\"state\"", 0) == 0) {
            posts.back().state = line;
            want_state = false;
        }
    }
    if (posts.empty() || posts[0].state.empty()) {
        std::printf("no first snapshot with a state\n");
        return 1;
    }
    const std::filesystem::path dir = std::filesystem::path(argv[2]).parent_path();
    const Database db = load_database(argv[2]);
    const LeImage exe = bmh::test::load_executable(dir / "BMH.exe");
    const TextDb texts = load_textd(dir / "TEXTD.BLK");
    const GameState rec = state_from_dump(posts[0].state);

    // the program start: data files, then new values for every player (game generator from 0)
    GameState g = startup_state(exe, db, read_file(dir / "M3.MEN"), read_file(dir / "M3.DAT"));
    g.texts = &texts;
    GameRandom game(0);
    WatcomRandom startup_rand(1);  // (chooses only the extra players' levels and skills drawn again later)
    startup_players(g, game, startup_rand);
    int club_players = 0, same = 0;
    std::vector<int> ids;
    for (size_t slot = 0; slot < 2928; slot++) {
        const int id = g.squad_slots.at(slot) & 0xfff;
        if (id == 0xfff) continue;
        ids.push_back(id);
        club_players++;
        if (g.players[size_t(id)].w35c == rec.players.at(size_t(id)).w35c) same++;
    }
    std::printf("program start: %d of %d club players have the recorded w35c\n", same, club_players);

    // legacy routine up to the dialog, with the Watcom seed counted back from the age/skill loop
    uint32_t loop_seed = 0;
    if (!find_loop_seed(rec, ids, loop_seed)) {
        std::printf("no Watcom seed explains the age/skill loop\n");
        return 2;
    }
    // (diagnosis: INIT_PROBE_SHIFT=1 tries extra game draws right after the stir and reports
    //  which make the shares of the snapshot)
    if (std::getenv("INIT_PROBE_SHIFT")) {
        const Bytes want = unhex(field(posts[0].state, "stocks"));
        for (int extra = -30; extra <= 200; extra++) {
            GameState t = g;
            GameRandom tg(0);
            // replay the game generator: startup_players drew game.seed() already; restart there
            tg.set_seed(game.seed());
            WatcomRandom tr(1);
            for (uint32_t k = 0; k < 12; k++) {
                const size_t s = size_t(k) * 0x5c;
                const uint32_t value = k * 2000 + 10000;
                for (int i = 0; i < 4; i++) t.stocks.at(s + size_t(i)) = uint8_t(value >> (8 * i));
                t.stocks.at(s + 0x4e) = uint8_t(k * 20 + 100), t.stocks.at(s + 0x4f) = uint8_t((k * 20 + 100) >> 8);
            }
            for (int i = 0; i < 100 + 30 + extra; i++) tg.range(0, 1);  // stir (n unknown) + name
            for (int day = 0; day < 30; day++) stock_market(t, tg);
            int same = 0;
            for (size_t i = 0; i < want.size() && i < t.stocks.size(); i++) same += want[i] == t.stocks[i];
            static int best = 0;
            if (same > best) best = same, std::printf("  best so far: %d game draws before the markets, %d of %zu bytes\n", 130 + extra, same, want.size());
            if (same > int(want.size()) - 20) std::printf("  shares match with %d game draws before the markets (%d of %zu bytes)\n", 130 + extra, same, want.size());
        }
    }
    GameState probe = g;
    GameRandom probe_game = game;
    WatcomRandom probe_rand(12345);
    new_career_markets_and_moods(probe, probe_game, probe_rand);
    const long before = long(probe_rand.calls());
    // all again from the program start with the Watcom seed counted back over both parts
    const long at_start = before + long(startup_rand.calls());
    WatcomRandom rand(watcom_back(loop_seed, at_start));
    std::printf("Watcom seed at the program start (legacy routine, 54 extra players): %u\n", rand.seed());
    g = startup_state(exe, db, read_file(dir / "M3.MEN"), read_file(dir / "M3.DAT"));
    g.texts = &texts;
    game.set_seed(0);
    startup_players(g, game, rand);
    new_career_markets_and_moods(g, game, rand);
    std::printf("Watcom seed before the age/skill loop %u: %s after %ld draws (shares, stir, club moods)\n", loop_seed,
                rand.seed() == loop_seed ? "reached" : "NOT reached", before);
    MinuteReport report;
    new_career_players(g, game, rand, report);
    if (report.unsupported) std::printf("  not supported: %s\n", report.reason.c_str());

    std::printf("options dialog (first snapshot): game seed %s (%d / %u), Watcom %u / %u\n",
                uint32_t(game.seed()) == posts[0].game_seed ? "OK" : "DIFF", game.seed(), posts[0].game_seed, rand.seed(),
                posts[0].rand_seed);
    {
        GameRandom d(game.seed());
        long n = 0;
        while (n < 15625 && uint32_t(d.seed()) != posts[0].game_seed) d.range(0, 1), n++;
        std::printf("  the original drew %ld more game numbers (mod 15625)\n", n);
    }
    int diff = 0;
    std::vector<uint32_t> a, b, c, ra, rb, rc;
    for (const auto& p : g.players) a.push_back(p.w35c), b.push_back(p.w368), c.push_back(p.w364);
    for (const auto& p : rec.players) ra.push_back(p.w35c), rb.push_back(p.w368), rc.push_back(p.w364);
    diff += compare("w35c", a, ra);
    diff += compare("w368", b, rb);
    diff += compare("w364", c, rc);
    diff += compare("squad_slots", g.squad_slots, rec.squad_slots);
    diff += compare("team_morale", g.team_morale, rec.team_morale);
    diff += compare("team_flags", g.team_flags, rec.team_flags);
    // (the last 4 bytes of *PlayerBonuses are not read from M3D.MAN: uninitialised heap, no player uses them)
    for (size_t i = 3146; i < 3150 && i < g.player_bonus.size() && i < rec.player_bonus.size(); i++) g.player_bonus[i] = rec.player_bonus[i];
    diff += compare("player_bonus", g.player_bonus, rec.player_bonus);
    const Bytes stocks = unhex(field(posts[0].state, "stocks"));
    diff += compare("stocks", std::vector<uint8_t>(g.stocks.begin(), g.stocks.begin() + long(std::min(stocks.size(), g.stocks.size()))),
                    std::vector<uint8_t>(stocks.begin(), stocks.end()));
    diff += compare("managers", g.manager_records, rec.manager_records);
    // The managers (legacy routine): the options come from the first savegame of the career
    // (argv[3], e.g. 2PNWK001.FBM: level, mode, competitions, number of managers), each
    // manager's dialog values from the snapshot after his setup; the club is given by name
    // (INIT_CLUBS="FRANKFURT,KICKERS") and the strength by INIT_STRENGTH (e.g. "2,2").
    if (argc > 3 && posts.size() > 2) {
        Savegame sg = load_savegame(argv[3]);
        SaveState st(sg.stream);
        for (uint32_t a : {::bmh::layout::DifficultyUnsigned, ::bmh::layout::CareerDurationUnsigned, ::bmh::layout::GameModeUnsigned, ::bmh::layout::ManagerCountUnsigned}) g.memory.set8(a, st.data(a)[0]);
        g.level = st.data(::bmh::layout::Difficulty)[0];  // (the typed copy of format field)
        for (uint32_t i = 0; i < 4; i++) g.memory.set8(::bmh::layout::CompetitionSelection + i, st.data(::bmh::layout::CompetitionSelection)[i]);
        std::vector<std::string> clubs;
        std::string list = std::getenv("INIT_CLUBS") ? std::getenv("INIT_CLUBS") : "FRANKFURT,KICKERS";
        for (size_t p = 0; p != std::string::npos;) {
            const size_t q = list.find(',', p);
            clubs.push_back(list.substr(p, q == std::string::npos ? q : q - p));
            p = q == std::string::npos ? q : q + 1;
        }
        const std::string strengths = std::getenv("INIT_STRENGTH") ? std::getenv("INIT_STRENGTH") : "2,2";
        career_managers_begin(g);
        for (size_t m = 0; m < clubs.size() && m + 1 < posts.size(); m++) {
            const GameState after = state_from_dump(posts[m + 1].state);
            const size_t base = m * 0xb0c;
            CareerManager c;
            for (size_t i = 0; i < 0x19 && after.manager_records.at(base + 0xa8 + i); i++) c.name.push_back(char(after.manager_records[base + 0xa8 + i]));
            c.league = after.manager_records.at(base + 0x210);
            c.portrait = after.manager_records.at(base + 0x221);
            c.strength = strengths.at(m * 2) - '0';
            c.team = -1;
            for (size_t t = 0; t < 138; t++) {
                std::string n;
                for (size_t i = g.club_ids.at(t); i < g.club_names.size() && g.club_names[i]; i++) n.push_back(char(std::toupper(g.club_names[i])));
                if (n.find(clubs[m]) != std::string::npos) {
                    c.team = int(t);
                    break;
                }
            }
            std::printf("\nmanager %zu: %s, club %d, league %d, strength %d, portrait %d\n", m, c.name.c_str(), c.team, c.league,
                        c.strength, c.portrait);
            if (std::getenv("INIT_PROBE_G")) {
                GameRandom d(game.seed());
                long n = 0;
                while (n < 15625 && uint32_t(d.seed()) != posts[m + 1].game_seed) d.range(0, 1), n++;
                std::printf("  game draws to the snapshot after: %ld; G(54, 66) by draw:", n);
                GameRandom e(game.seed());
                for (int k = 0; k < 12; k++) std::printf(" %d", e.range(54, 66));
                std::printf("\n");
            }
            MinuteReport mr;
            career_add_manager(g, exe, int(m), c, false, game, rand, mr);
            if (mr.unsupported) std::printf("  not supported: %s\n", mr.reason.c_str());
            const Post& p = posts[m + 1];
            std::printf("  seeds: game %s (%d / %u), Watcom %s (%u / %u)\n", uint32_t(game.seed()) == p.game_seed ? "OK" : "DIFF",
                        game.seed(), p.game_seed, rand.seed() == p.rand_seed ? "OK" : "DIFF", rand.seed(), p.rand_seed);
            std::vector<uint32_t> x35, x68, x64, y35, y68, y64;
            for (const auto& q : g.players) x35.push_back(q.w35c), x68.push_back(q.w368), x64.push_back(q.w364);
            for (const auto& q : after.players) y35.push_back(q.w35c), y68.push_back(q.w368), y64.push_back(q.w364);
            compare("w35c", x35, y35);
            compare("w368", x68, y68);
            compare("w364", x64, y64);
            compare("squad_slots", g.squad_slots, after.squad_slots);
            compare("team_morale", g.team_morale, after.team_morale);
            compare("team_flags", g.team_flags, after.team_flags);
            compare("tactics", g.tactics, after.tactics);
            compare("managers", g.manager_records, after.manager_records);
            for (size_t i = 0; i + 4 <= std::min(g.manager_records.size(), after.manager_records.size()); i += 4)
                if (std::memcmp(&g.manager_records[i], &after.manager_records[i], 4) != 0)
                    std::printf("    manager %zu +0x%03zx: %02x%02x%02x%02x / %02x%02x%02x%02x\n", i / 0xb0c, i % 0xb0c, g.manager_records[i],
                                g.manager_records[i + 1], g.manager_records[i + 2], g.manager_records[i + 3], after.manager_records[i],
                                after.manager_records[i + 1], after.manager_records[i + 2], after.manager_records[i + 3]);
        }
    }
    // the end of legacy routine, compared with every later snapshot
    if (argc > 3 && posts.size() > 2) {
        career_managers_end(g, false);
        size_t next = 2;
        auto check_at = [&](const char* what) {
            if (next >= posts.size()) return;
            const size_t k = next++;
            std::printf("\n%s - snapshot %zu:\n", what, k);
            const GameState after = state_from_dump(posts[k].state);
            std::printf(" snapshot %zu: game %s, Watcom %s\n", k, uint32_t(game.seed()) == posts[k].game_seed ? "OK" : "DIFF",
                        rand.seed() == posts[k].rand_seed ? "OK" : "DIFF");
            std::vector<uint32_t> x35, x68, x64, y35, y68, y64;
            for (const auto& q : g.players) x35.push_back(q.w35c), x68.push_back(q.w368), x64.push_back(q.w364);
            for (const auto& q : after.players) y35.push_back(q.w35c), y68.push_back(q.w368), y64.push_back(q.w364);
            compare("w35c", x35, y35);
            compare("w368", x68, y68);
            compare("w364", x64, y64);
            compare("squad_slots", g.squad_slots, after.squad_slots);
            compare("team_morale", g.team_morale, after.team_morale);
            compare("team_flags", g.team_flags, after.team_flags);
            compare("tactics", g.tactics, after.tactics);
            compare("managers", g.manager_records, after.manager_records);
            for (size_t i = 0; i + 4 <= std::min(g.manager_records.size(), after.manager_records.size()); i += 4)
                if (std::memcmp(&g.manager_records[i], &after.manager_records[i], 4) != 0)
                    std::printf("    manager %zu +0x%03zx: %d / %d\n", i / 0xb0c, i % 0xb0c, int32_t(g.manager_records[i] | g.manager_records[i + 1] << 8 | g.manager_records[i + 2] << 16 | uint32_t(g.manager_records[i + 3]) << 24), int32_t(after.manager_records[i] | after.manager_records[i + 1] << 8 | after.manager_records[i + 2] << 16 | uint32_t(after.manager_records[i + 3]) << 24));
        };
        MinuteReport sr;
        career_season_setup(g, game, rand, sr, check_at);
        if (sr.unsupported) std::printf("  not supported: %s\n", sr.reason.c_str());
        career_first_steps(g, game, rand, sr);
        if (sr.unsupported) std::printf("  not supported: %s\n", sr.reason.c_str());
        if (std::getenv("INIT_DESK")) {
            const int dm = std::atoi(std::getenv("INIT_DESK"));
            std::printf("  seeds before the desk: game %d, Watcom %u\n", game.seed(), rand.seed());
            for (int s = std::getenv("INIT_STIRS") ? std::atoi(std::getenv("INIT_STIRS")) : 0; s > 0; s--) stir_random(game, rand);
            if (std::getenv("INIT_PREP")) {
                // legacy routine for the first event (season day 0)
                const uint16_t entry = g.memory.u16(::bmh::layout::CompetitionCalendar);
                std::vector<uint8_t> referees = db.raw_referee_properties;
                MatchdayContext ctx;
                ctx.month = month_code(g.date);
                ctx.season_day = g.season_day();
                ctx.weekday = weekday_code(g.date);
                ctx.referees = &referees;
                MinuteReport rep;
                std::printf("  first event: calendar 0x%04x, competitions %x\n", entry, day_competitions(g, entry));
                const auto groups = setup_matchday(g, day_competitions(g, entry), ctx, game, rand, rep);
                if (rep.unsupported) std::printf("  not supported: %s\n", rep.reason.c_str());
                draw_matchday_number(g, rand);
                shuffle_manager_order(g, game);
                std::printf("  %zu groups; seeds game %d, Watcom %u\n", groups.size(), game.seed(), rand.seed());
            }
            for (int s = std::getenv("INIT_STIRS_AFTER") ? std::atoi(std::getenv("INIT_STIRS_AFTER")) : 0; s > 0; s--)
                stir_random(game, rand);
            if (std::getenv("INIT_OFFER")) {
                MinuteReport orep;
                const bool offered = pre_match_offer(g, dm, game, rand, orep);  // legacy routine
                std::printf("  pre-match offer at the first desk: %s%s\n", offered ? "yes" : "no",
                            orep.unsupported ? (" (" + orep.reason + ")").c_str() : "");
            }
            if (std::getenv("INIT_NO_DESK")) {
                std::printf("  (no desk entry)\n");
            } else
            enter_desk(g, dm, game, rand);
        }
        if (next < posts.size()) {
            GameRandom d(game.seed());
            long n = 0;
            while (n < 15625 && uint32_t(d.seed()) != posts[next].game_seed) d.range(0, 1), n++;
            WatcomRandom w(rand.seed());
            long k = 0;
            while (k < 200000 && w.seed() != posts[next].rand_seed) w.rand(), k++;
            std::printf("  to the next snapshot: %ld game draws, %ld Watcom draws\n", n, k < 200000 ? k : -1);
        }
        check_at("league shuffle and transfer list (legacy routine)");
    }
    // (INIT_SHOW_RECORDS=1: the manager record bytes that change from snapshot to snapshot)
    if (std::getenv("INIT_SHOW_RECORDS"))
        for (size_t k = 1; k < posts.size(); k++) {
            if (posts[k].state.empty() || posts[k - 1].state.empty()) continue;
            const GameState x = state_from_dump(posts[k - 1].state), y = state_from_dump(posts[k].state);
            std::printf("records, snapshot %zu -> %zu:\n", k - 1, k);
            for (size_t i = 0; i < std::min(x.manager_records.size(), y.manager_records.size()); i++)
                if (x.manager_records[i] != y.manager_records[i])
                    std::printf("  manager %zu +0x%03zx: %02x -> %02x\n", i / 0xb0c, i % 0xb0c, x.manager_records[i], y.manager_records[i]);
            std::printf("  team_morale diffs:");
            for (size_t i = 0; i < std::min(x.team_morale.size(), y.team_morale.size()); i++)
                if (x.team_morale[i] != y.team_morale[i]) std::printf(" [%zu] %02x->%02x", i, x.team_morale[i], y.team_morale[i]);
            std::printf("\n");
        }
    return diff == 0 && uint32_t(game.seed()) == posts[0].game_seed ? 0 : 2;
}
