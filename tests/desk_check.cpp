// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
// desk_check - replays desk actions (game/desk_actions.hpp) between two post snapshots of a
// recording and compares the result with the second snapshot.
//
// Usage: desk_check <capture.jsonl> <from post> <to post> <action>...
//   camp:<manager>:<resort>   book_training_camp (the four sliders are taken from the
//                             target snapshot: they are set on the screen before booking)
//   effect:<manager>:<resort> training_camp_effect only
//   percent:<manager>          opening the stadium screen (construction_price_percent)
//   offer:<object>             clicking a periphery object (periphery_offer)
//   build:<manager>:<object>   "Ja" on that offer (build_periphery)
//   buy:<m>:<stock>:<n> / sell:<m>:<stock>:<n> / tip:<m>:<stock>   stock exchange
//   W<n>                      n WatcomRandom draws not ported (stir etc.)
// Without actions it only prints how many Watcom / game draws lie between the snapshots.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "capture_io.hpp"
#include "game/desk_actions.hpp"

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
        if (diff++ < 8) first += " [" + std::to_string(i) + "] " + std::to_string(ours[i]) + "/" + std::to_string(theirs[i]);
    }
    std::printf("  %-16s %s%s\n", name, diff ? "DIFF" : "OK", first.c_str());
    return diff;
}

// Little endian int32 at a byte offset.
int32_t rd(const std::vector<uint8_t>& v, size_t o) {
    return int32_t(v[o] | v[o + 1] << 8 | v[o + 2] << 16 | uint32_t(v[o + 3]) << 24);
}

// Watcom draws needed to get from seed a to seed b (-1 if more than `limit`).
long distance(uint32_t a, uint32_t b, long limit = 100000) {
    WatcomRandom r(a);
    for (long n = 0; n <= limit; n++) {
        if (r.seed() == b) return n;
        r.rand();
    }
    return -1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: desk_check <capture.jsonl> <from post> <to post> <action>...\n");
        return 1;
    }
    std::ifstream in(argv[1]);
    std::vector<Post> posts;
    std::string line;
    bool want_state = false;
    while (std::getline(in, line)) {
        if (line.rfind("{\"post\":", 0) == 0) {
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
    const int from = std::atoi(argv[2]), to = std::atoi(argv[3]);
    if (from < 0 || to >= int(posts.size()) || from >= to) {
        std::printf("posts %d..%d not in the recording (%zu posts)\n", from, to, posts.size());
        return 1;
    }
    const Post &a = posts[size_t(from)], &b = posts[size_t(to)];
    std::printf("post %d -> %d: Watcom draws %ld, game seed %u -> %u\n", from, to, distance(a.rand_seed, b.rand_seed),
                a.game_seed, b.game_seed);
    if (argc == 4) return 0;

    GameState g = state_from_dump(a.state);
    GameState want = state_from_dump(b.state);
    g.stocks = unhex(field(a.state, "stocks"));
    want.stocks = unhex(field(b.state, "stocks"));
    WatcomRandom rand(a.rand_seed);
    GameRandom game(int32_t(a.game_seed));
    g.memory.add_block(::bmh::layout::CareerSession_Invalid_RuntimeField2, 4);
    g.memory.set32(::bmh::layout::CareerSession_Invalid_RuntimeField2, uint32_t(std::stoul(field(a.state, "bbcd8"))));
    uint32_t offer = 0;
    for (int i = 4; i < argc; i++) {
        const std::string act = argv[i];
        int m = 0, r = 0;
        if (act[0] == 'W') {
            for (int n = std::atoi(act.c_str() + 1); n > 0; n--) rand.rand();
        } else if (int s = 0, n = 0; std::sscanf(act.c_str(), "buy:%d:%d:%d", &m, &s, &n) == 3) {
            std::printf("  buy manager %d stock %d count %d: %s\n", m, s, n, buy_shares(g, m, s, n) ? "bought" : "refused");
        } else if (std::sscanf(act.c_str(), "sell:%d:%d:%d", &m, &s, &n) == 3) {
            sell_shares(g, m, s, n);
        } else if (std::sscanf(act.c_str(), "tip:%d:%d", &m, &s) == 2) {
            std::printf("  tip manager %d stock %d: %d\n", m, s, stock_tip(g, m, s, rand));
        } else if (std::sscanf(act.c_str(), "percent:%d", &m) == 1) {
            std::printf("  construction price percent %d\n", construction_price_percent(g, m, game));
        } else if (std::sscanf(act.c_str(), "offer:%d", &s) == 1) {
            offer = uint32_t(periphery_offer(g, s, game));
            std::printf("  periphery %d offer %u\n", s, offer);
        } else if (std::sscanf(act.c_str(), "build:%d:%d", &m, &s) == 2) {
            std::printf("  build manager %d periphery %d: %s\n", m, s, build_periphery(g, m, s, offer, rand) ? "paid" : "no money");
        } else if (std::sscanf(act.c_str(), "camp:%d:%d", &m, &r) == 2 || std::sscanf(act.c_str(), "effect:%d:%d", &m, &r) == 2) {
            for (size_t u = 0; u < 4; u++) g.manager_records.at(size_t(m) * 0xb0c + 0xc1 + u) = want.manager_records.at(size_t(m) * 0xb0c + 0xc1 + u);
            if (act[0] == 'c') {
                std::printf("  camp manager %d resort %d price %d: %s\n", m, r, training_camp_price(g, m, r),
                            book_training_camp(g, m, r, rand) ? "booked" : "no money");
            } else {
                training_camp_effect(g, m, r, rand);
            }
        } else {
            std::printf("unknown action %s\n", act.c_str());
            return 1;
        }
    }
    const long left = distance(rand.seed(), b.rand_seed);
    std::printf("  Watcom seed %s (%ld draws to the target)\n", rand.seed() == b.rand_seed ? "OK" : "DIFF", left);
    std::printf("  game seed %s (%d / %u)\n", uint32_t(game.seed()) == b.game_seed ? "OK" : "DIFF", game.seed(), b.game_seed);
    std::vector<uint32_t> w35c, want_w35c;
    for (auto& p : g.players) w35c.push_back(p.w35c);
    for (auto& p : want.players) want_w35c.push_back(p.w35c);
    int diff = compare("w35c", w35c, want_w35c);
    diff += compare("team_morale", g.team_morale, want.team_morale);
    diff += compare("tactics", g.tactics, want.tactics);
    diff += compare("managers", g.manager_records, want.manager_records);
    diff += compare("stocks", g.stocks, want.stocks);
    for (size_t o = 0; o + 4 <= std::min(g.manager_records.size(), want.manager_records.size()); o += 4)
        if (rd(g.manager_records, o) != rd(want.manager_records, o))
            std::printf("    manager %zu +0x%03zx: %d / %d\n", o / 0xb0c, o % 0xb0c, rd(g.manager_records, o), rd(want.manager_records, o));
    return diff == 0 && rand.seed() == b.rand_seed && uint32_t(game.seed()) == b.game_seed ? 0 : 2;
}
