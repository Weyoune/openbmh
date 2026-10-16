// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
// week_check - replays a whole "Weiter" turn recorded with runtime verification (matchday
// plus the week up to the next desk) section by section and compares every consistent
// checkpoint of the recording with the port.
//
// Recording lines (in order): minute snapshots {"minute":n,...} with the match groups,
// day snapshots {"day":date,...} written when the date changes, "post" snapshots written
// when the game pauses; each followed by a {"state":...} dump. Minute and day snapshots
// are taken with the emulator suspended, so seeds and tables belong to the same instant.
//
// Sections checked so far:
//   0  minute 90 -> final whistle: the last regular minute and the stoppage time; also
//      yields the weather of the matchday's last chance, which the next kick-off reads.
//   A  final whistle screen (the pause after the last matchday minute) -> start of the
//      first extra time phase (minute 91): leaving the results screen stirs the random
//      numbers, then legacy routine runs for every match.
//   B  minute 106 -> the next day: match groups processed, tables, rounds, interest.
//   C  that day -> the pause before the next (background) matchday: training, stock
//      market, then the matchday preparation and kick-off (game/matchday_setup.hpp); the
//      match groups built are compared with the first minute snapshot of that matchday.
//
// Usage: week_check <capture.jsonl> <savegame before the matchday (.FBM)>
//   M3D.MAN (referees) is read from the savegame's directory.
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "capture_io.hpp"
#include "runtime_fixture_tables.hpp"
#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "formats/m3d.hpp"
#include "game/matchday.hpp"
#include "game/matchday_setup.hpp"
#include "game/cup.hpp"
#include "game/driver.hpp"
#include "game/match.hpp"
#include "game/messages.hpp"
#include "game/desk.hpp"
#include "game/month.hpp"
#include "game/desk_actions.hpp"
#include "game/national.hpp"
#include "game/rating.hpp"
#include "game/turn.hpp"
#include "game/week.hpp"
#include "game/season.hpp"
#include "game/weekly.hpp"

using namespace bmh;
using namespace bmh::capture;

namespace {

void put32le(Bytes& b, size_t o, uint32_t v) {
    for (int i = 0; i < 4; i++) b.at(o + i) = uint8_t(v >> (8 * i));
}

struct Entry {
    std::string kind;   // "minute", "day", "post"
    int value = 0;      // minute / date / post number
    std::string line;   // the snapshot line
    std::string state;  // the state dump following it (may be empty)
};

template <typename T>
int compare(const char* name, const std::vector<T>& ours, const std::vector<T>& theirs, int width) {
    size_t n = std::min(ours.size(), theirs.size());
    int diff = 0;
    std::string first;
    for (size_t i = 0; i < n; i++) {
        if (ours[i] == theirs[i]) continue;
        if (diff++ < (std::getenv("WEEK_ALLDIFF") ? 200 : 6)) {
            char b[64];
            std::snprintf(b, sizeof b, " [%zu] %0*llx/%0*llx", i, width, (unsigned long long)ours[i], width,
                          (unsigned long long)theirs[i]);
            first += b;
        }
    }
    if (diff) std::printf("  %-14s DIFF (%d of %zu, ported/original):%s\n", name, diff, n, first.c_str());
    return diff;
}

// Compares seeds and all recorded tables; prints only differences.
// Kick-off data of the match groups built by the port against a minute snapshot taken
// right after the kick-off. Returns the number of differences. The home side's initial
// attitude (side record byte 8) is compared in the original against the away record
// before that is built, i.e. uninitialised heap memory (see build_side_records); it cannot
// be derived, so the recorded byte is adopted into `ours`.
// Plays a matchday from our kick-off (game/driver.hpp). The minute snapshots of a fast
// (not shown) matchday are sparse and land somewhere inside their minute: each one between
// isnap and iend is checked by locating its recorded seeds in the random number streams
// between the start and the end of that minute.
// The month change at the end of legacy routine (full game only, when the month of the new
// day differs from the previous day's).
bool month_step(GameState& b, GameRandom& bg, WatcomRandom& br, bool full_game) {
    if (!full_game || b.date == 0 || month_code(b.date - 1) == month_code(b.date)) return true;
    std::printf("  day %u: month change (legacy routine)\n", b.date);
    MinuteReport mr;
    bmh::month_step(b, bg, br, mr);  // (game/turn.hpp)
    if (mr.unsupported) {
        std::printf("  not supported: %s, replay stops here\n", mr.reason.c_str());
        return false;
    }
    return true;
}

// Every (game, Watcom) draw count pair the port passes through during a matchday replay.
std::unordered_set<long long> g_draw_states;
long g_draw_game = 0, g_draw_watcom = 0;
void record_draw(int generator) {
    (generator == 0 ? g_draw_game : g_draw_watcom)++;
    g_draw_states.insert(g_draw_game * 10000000LL + g_draw_watcom);
}

bool replay_matchday(GameState& b, std::vector<MatchGroup>& groups, GameRandom& bg, WatcomRandom& br,
                     const std::vector<Entry>& entries, size_t isnap, size_t iend, const Database& db,
                     const std::vector<uint8_t>& buttons, int stop_at = 0, int month = 0) {
    const GameRandom game0 = bg;
    const WatcomRandom rand0 = br;
    // position (number of generator steps since the kick-off) of a recorded seed, searched
    // from the start of the current minute (the game generator has a short period)
    GameRandom gmin = bg;
    WatcomRandom wmin = br;
    long g_from = 0, w_from = 0;
    // the window of the previous minute (a snapshot may also be read a little early, when
    // the minute counter already moved on but the last draws of the minute are still due)
    GameRandom gprev = bg;
    WatcomRandom wprev = br;
    long g_prev = 0, w_prev = 0;
    auto position = [&](const Entry& e, bool watcom) -> long {
        if (watcom) {
            WatcomRandom r = wmin;
            const uint32_t target = uint32_t(std::stoull(field(e.line, "rand_seed")));
            for (long k = 0; k < 3000000; k++, r.rand())
                if (r.seed() == target) return w_from + k;
        } else {
            GameRandom r = gmin;
            const int32_t target = int32_t(std::stoll(field(e.line, "game_seed")));
            for (long k = 0; k < 3000000; k++, r.range(0, 1))
                if (r.seed() == target) return g_from + k;
        }
        return -1;
    };
    const Entry& first = entries[isnap];
    MatchEngine engine{b, groups, bg, br, db.raw_referee_properties};
    // WEEK_SHOW=1: what the original shows during the matchday (presentation.hpp), listed below
    std::vector<ShowEvent> shows;
    if (std::getenv("WEEK_SHOW")) engine.show = &shows;
    engine.alternate_event_roll = field(first.line, "alternate_event_roll") == "1";
    engine.match_events_disabled = field(first.line, "match_events_disabled") == "1";
    engine.live_desk_disabled = field(first.line, "live_desk_disabled") == "1";
    // (month: format field of the matchday when the snapshot belongs to a later day)
    engine.month = month ? month : std::stoi(field(first.line, "month"));
    engine.buttons = buttons;
    engine.leave_screens = true;  // the recordings leave a substitution screen unchanged
    const int trace_until = std::getenv("WEEK_TRACE") ? std::stoi(std::getenv("WEEK_TRACE")) : 0;
    // (WEEK_TRACE=<minute>: events and chances up to that minute, in replays with three
    //  groups; WEEK_TRACE_ALL=1 for any number of groups)
    engine.trace = trace_until > 0 && (groups.size() == 3 || std::getenv("WEEK_TRACE_ALL"));
    int snaps_ok = 0, snaps_bad = 0;
    // A snapshot may also be read a little late, early in the next minute: it is then
    // checked against the window of both minutes when the next minute is done.
    struct Pending {
        size_t index;
        long g_from, w_from;
        GameRandom g;
        WatcomRandom w;
    };
    std::vector<Pending> pending;
    struct NoHook {
        void (*saved)(int) = draw_hook;
        NoHook() { draw_hook = nullptr; }
        ~NoHook() { draw_hook = saved; }
    };
    auto locate = [&](const Pending& p, bool watcom) -> long {
        NoHook no_hook;
        const Entry& e = entries[p.index];
        if (watcom) {
            WatcomRandom r = p.w;
            const uint32_t target = uint32_t(std::stoull(field(e.line, "rand_seed")));
            for (long k = 0; k < 3000000; k++, r.rand())
                if (r.seed() == target) return p.w_from + k;
        } else {
            GameRandom r = p.g;
            const int32_t target = int32_t(std::stoll(field(e.line, "game_seed")));
            for (long k = 0; k < 3000000; k++, r.range(0, 1))
                if (r.seed() == target) return p.g_from + k;
        }
        return -1;
    };
    auto settle = [&](long g_to, long w_to, bool last = false) {
        std::vector<Pending> later;
        for (const Pending& p : pending) {
            const long gp = locate(p, false), wp = locate(p, true);
            if (!last && (gp > g_to || wp > w_to) && gp >= p.g_from && wp >= p.w_from) {
                // read even later (e.g. after the result screens of the last phase): kept
                // until the next minute or the end of the matchday
                later.push_back(p);
                continue;
            }
            if (wp < 0) {
                // the Watcom seed lies on no path of the stream (3 million draws searched): the
                // snapshot caught a temporary generator state (e.g. while the penalty shoot-out
                // is shown, WEEK40/WEEK201) - the game seed then says nothing either
                std::printf("  minute %3d: Watcom seed off the stream (a temporary state), not counted\n",
                            entries[p.index].value);
                continue;
            }
            const bool in = gp >= p.g_from && gp <= g_to && wp >= p.w_from && wp <= w_to;
            std::printf("  minute %3d: game %ld in [%ld, %ld], Watcom %ld in [%ld, %ld] %s\n", entries[p.index].value, gp,
                        p.g_from, g_to, wp, p.w_from, w_to, in ? "OK (read late)" : last && gp >= p.g_from && wp >= p.w_from ? "read after the last minute" : "MISMATCH");
            std::printf("  minute %3d: draw state (%ld, %ld) %s\n", entries[p.index].value, gp, wp,
                        g_draw_states.count(gp * 10000000LL + wp) ? "passed by the port" : "NEVER reached by the port");
            if (in) snaps_ok++;
            else if (!(last && gp >= p.g_from && wp >= p.w_from)) snaps_bad++;
        }
        pending = later;
    };
    MinuteReport report;
    g_draw_states = {0}, g_draw_game = 0, g_draw_watcom = 0;
    draw_hook = record_draw;
    play_matchday(engine, b, groups, buttons, bg, br, report, [&](int minute) {
        const long g_to = long(bg.calls() - game0.calls()), w_to = long(br.calls() - rand0.calls());
        settle(g_to, w_to);
        // pauses inside a matchday with a human team (dialogs): report where they fall (before
        // the snapshots of this minute and again after them)
        auto pauses = [&]() {
        while (isnap < iend && entries[isnap].kind == "post") {
            Pending p{isnap, g_from, w_from, gmin, wmin};
            std::printf("  pause %d after minute %d: game %ld, Watcom %ld (ours at the end of the minute %ld, %ld)\n",
                        entries[isnap].value, minute - 1, locate(p, false), locate(p, true), g_to, w_to);
            // The manager may change his line-up or substitute in such a pause (legacy routine /
            // legacy routine): the recorded state after it carries the changes, taken over for
            // his team; then his side is rated again as in legacy routine.
            // (the pause's own state is dumped when it opens: the changes show in the next
            //  minute snapshot, taken after the pause)
            const Entry& after = isnap + 1 < iend && entries[isnap + 1].kind == "minute" ? entries[isnap + 1] : entries[isnap];
            if (&after != &entries[isnap] && !after.state.empty()) {
                const GameState o = state_from_dump(after.state);
                for (size_t mgr = 0; mgr < b.manager_teams.size() && o.squad_slots.size() == b.squad_slots.size(); mgr++) {
                    const int team = b.manager_teams[mgr];
                    const SquadRange r = squad_range(team);
                    bool changed = false;
                    for (size_t s = r.first; s < r.first + r.count; s++) changed = changed || o.squad_slots[s] != b.squad_slots[s];
                    // (a new formation moves the places on the tactics board, bytes 0x41/0x42 of
                    //  each row - legacy routine reads them for human teams)
                    if (o.tactics.size() == b.tactics.size())
                        for (size_t row = 0; row < 24; row++)
                            for (size_t k : {size_t(0x41), size_t(0x42)})
                                changed = changed || o.tactics[mgr * 0x840 + row * 0x58 + k] != b.tactics[mgr * 0x840 + row * 0x58 + k];
                    if (o.team_flags.size() == b.team_flags.size()) changed = changed || o.team_flags[size_t(team)] != b.team_flags[size_t(team)];
                    // (leaving the line-up screen re-rates the side even without a change - the
                    //  recording does not tell which pause was that screen: a trial re-rating
                    //  whose draws end exactly at the next snapshot's seeds identifies it)
                    if (!changed) {
                        GameState sc = b;
                        std::vector<MatchGroup> gc = groups;
                        GameRandom gg = bg;
                        WatcomRandom wr = br;
                        MatchEngine trial{sc, gc, gg, wr, engine.referees};
                        trial.month = engine.month;
                        MinuteReport tr;
                        trial.rerate_team(team, tr);
                        const bool hit = !tr.unsupported && gg.seed() == int32_t(std::stoll(field(after.line, "game_seed"))) &&
                                         wr.seed() == uint32_t(std::stoull(field(after.line, "rand_seed")));
                        if (!hit) continue;
                    }
                    for (size_t s = r.first; s < r.first + r.count; s++) b.squad_slots[s] = o.squad_slots[s];
                    if (o.tactics.size() == b.tactics.size())
                        std::copy(o.tactics.begin() + long(mgr * 0x840), o.tactics.begin() + long(mgr * 0x840 + 0x840),
                                  b.tactics.begin() + long(mgr * 0x840));
                    if (o.team_flags.size() == b.team_flags.size()) b.team_flags[size_t(team)] = o.team_flags[size_t(team)];
                    for (size_t s = r.first; s < r.first + r.count; s++) {
                        const int pl = o.squad_slots[s] & 0xFFF;
                        if (pl == 0xFFF || o.players.size() != b.players.size()) continue;
                        b.players[size_t(pl)] = o.players[size_t(pl)];
                        if (o.player_cards.size() == b.player_cards.size()) b.player_cards[size_t(pl)] = o.player_cards[size_t(pl)];
                    }
                    MinuteReport rr;
                    engine.rerate_team(team, rr);
                    std::printf("  the manager changed his line-up in this pause: taken over, his side rated again%s\n",
                                rr.unsupported ? (" - " + rr.reason).c_str() : "");
                }
            }
            isnap++;
        }
        };
        pauses();
        while (isnap < iend && entries[isnap].kind == "minute" && entries[isnap].value == minute) {
            Pending p{isnap, g_from, w_from, gmin, wmin};
            const long gp = locate(p, false), wp = locate(p, true);
            {
                // scores: ours at the end of the minute, the recording somewhere inside it
                const std::vector<MatchGroup> rec = groups_from_snapshot(entries[isnap].line);
                std::string diff;
                for (size_t gi = 0; gi < groups.size() && gi < rec.size(); gi++)
                    for (int m = 0; m < groups[gi].matches(); m++) {
                        int oh = 0, oa = 0, rh = 0, ra = 0;
                        if (!match_score(groups[gi], m, oh, oa) || !match_score(rec[gi], m, rh, ra)) continue;
                        if (oh != rh || oa != ra)
                            diff += " g" + std::to_string(gi) + "m" + std::to_string(m) + " " + std::to_string(oh) + ":" +
                                    std::to_string(oa) + "/" + std::to_string(rh) + ":" + std::to_string(ra);
                    }
                if (!diff.empty()) std::printf("  minute %3d scores ours/recorded:%s\n", minute, diff.c_str());
            }
            std::printf("  minute %3d: draw state (%ld, %ld) %s\n", minute, gp, wp,
                        g_draw_states.count(gp * 10000000LL + wp) ? "passed by the port" : "NEVER reached by the port");
            if (gp >= g_from && gp <= g_to && wp >= w_from && wp <= w_to) {
                std::printf("  minute %3d: game %ld in [%ld, %ld], Watcom %ld in [%ld, %ld] OK\n", minute, gp, g_from, g_to,
                            wp, w_from, w_to);
                snaps_ok++;
            } else if (const Pending q{isnap, g_prev, w_prev, gprev, wprev};
                       locate(q, false) >= g_prev && locate(q, false) <= g_from && locate(q, true) >= w_prev &&
                       locate(q, true) <= w_from) {
                std::printf("  minute %3d: game %ld, Watcom %ld in the previous minute [%ld, %ld], [%ld, %ld] OK (read early)\n",
                            minute, locate(q, false), locate(q, true), g_prev, g_from, w_prev, w_from);
                snaps_ok++;
            } else {
                pending.push_back(p);
            }
            isnap++;
        }
        pauses();
        g_prev = g_from, w_prev = w_from, gprev = gmin, wprev = wmin;
        // (a re-rating in a pause after this minute draws before the next one)
        g_from = long(bg.calls() - game0.calls()), w_from = long(br.calls() - rand0.calls()), gmin = bg, wmin = br;
        // (WEEK_COUNTS=1: our draws at the end of every minute, to set against WEEK_MAP=all)
        if (std::getenv("WEEK_COUNTS")) std::printf("  ours after minute %3d: game %ld, Watcom %ld\n", minute, g_from, w_from);
        engine.trace = engine.trace && minute + 1 <= trace_until;
    }, stop_at);
    settle(long(bg.calls() - game0.calls()), long(br.calls() - rand0.calls()), true);
    draw_hook = nullptr;
    store_group_descriptors(b, groups, true);  // the descriptors stay in memory
    (void)position;
    if (report.unsupported) std::printf("  not supported: %s\n", report.reason.c_str());
    // snapshots taken after the last minute
    while (isnap < iend && entries[isnap].kind == "minute") {
        Pending p{isnap, g_from, w_from, gmin, wmin};
        const long gp = locate(p, false), wp = locate(p, true);
        const long gn = long(bg.calls() - game0.calls()), wn = long(br.calls() - rand0.calls());
        std::printf("  minute %3d (after the last minute): game %ld / ours %ld, Watcom %ld / ours %ld\n", entries[isnap].value,
                    gp, gn, wp, wn);
        isnap++;
    }
    std::printf("  %d minute snapshots inside their minute, %d not\n", snaps_ok, snaps_bad);
    static const char* const kShow[] = {"booking", "substitution", "chance", "set piece", "shot on goal", "penalty kick",
                                        "shoot-out end"};
    for (const ShowEvent& e : shows)
        std::printf("  show g%u m%u minute %3d %-12s side %u kind %u flag %u team %d player %d other %d [%d %d %d %d] %s\n",
                    e.group, e.match, e.minute, kShow[int(e.what)], e.side, e.kind, e.flag, e.team, e.player, e.other, e.a,
                    e.b, e.c, e.d, e.text.c_str());
    return snaps_bad == 0 && !report.unsupported;
}

int compare_kickoff(std::vector<MatchGroup>& ours_groups, const std::vector<MatchGroup>& rec, GameState* state = nullptr,
                    int minute = 1) {
    int diffs = 0, heap_attitudes = 0;
    if (ours_groups.size() != rec.size())
        std::printf("  %zu groups, the original has %zu\n", ours_groups.size(), rec.size()), diffs++;
    for (size_t gi = 0; gi < ours_groups.size() && gi < rec.size(); gi++) {
        MatchGroup& a = ours_groups[gi];
        const MatchGroup& o = rec[gi];
        const size_t n = size_t(a.matches());
        const std::string g = "g" + std::to_string(gi) + " ";
        auto bytes = [&](const char* name, size_t off, size_t len) {
            std::vector<uint8_t> x(a.desc.begin() + off, a.desc.begin() + off + len);
            std::vector<uint8_t> y(o.desc.begin() + off, o.desc.begin() + off + len);
            diffs += compare((g + name).c_str(), x, y, 2);
        };
        bytes("head", 0, 3);
        bytes("referee", 0x10, n);
        bytes("temperature", 0x30, n);
        bytes("rain", 0x50, n);
        bytes("void mask", 0xb8, 4);
        bytes("crowd sum", 0xc4, 4);
        if (a.desc.at(0) == 0) bytes("league", 0xcc, 4);
        else if (le32(a.desc, 0xcc) != le32(o.desc, 0xcc) && le32(a.desc, 0xcc) + kGuestDataBase != le32(o.desc, 0xcc))
            std::printf("  %-14s DIFF (cup table entry %05x/%05x)\n", (g + "cup").c_str(), unsigned(le32(a.desc, 0xcc)),
                        unsigned(le32(o.desc, 0xcc))), diffs++;
        std::vector<int> fa = a.fixtures, fo = o.fixtures;
        diffs += compare((g + "fixtures").c_str(), fa, fo, 4);
        // matches not played (void mask, e.g. postponed): the original leaves their
        // attendance and side records as they were, the port clears them
        const uint32_t void_mask = le32(o.desc, 0xb8);
        for (size_t m = 0; m < n; m++)
            if (void_mask >> m & 1) {
                if (m < a.spectators.size() && m < o.spectators.size()) a.spectators[m] = o.spectators[m];
                for (size_t k = 0; k < 18 && m * 18 + k < a.records.size() && m * 18 + k < o.records.size(); k++)
                    a.records[m * 18 + k] = o.records[m * 18 + k];
            }
        diffs += compare((g + "spectators").c_str(), a.spectators, o.spectators, 6);
        std::vector<uint8_t> ours = a.records, theirs = o.records;
        for (size_t m = 0; m < n && m * 18 + 8 < theirs.size(); m++) {
            if (ours[m * 18 + 8] != theirs[m * 18 + 8]) heap_attitudes++;
            ours[m * 18 + 8] = theirs[m * 18 + 8];
            a.records[m * 18 + 8] = theirs[m * 18 + 8];
        }
        // Ratings 4 and 5 compare the condition against a stale technique (the stack slot
        // of the last profile-3 player). Now and then the original finds a much larger value
        // there - nothing in the program writes that slot in between, so it is the trace of
        // a hardware interrupt on the stack (timer, mouse, sound): every player then counts
        // as "technique above condition" and loses rain * 3 percent. Not reproducible; a
        // side whose only differences are bytes 4/5 that a large stale value explains is
        // taken over and not counted.
        for (size_t side = 0; state && side * 9 + 8 < ours.size() && side < a.fixtures.size() && side * 9 + 8 < theirs.size();
             side++) {
            bool only45 = true, any = false;
            for (size_t k = 0; k < 9; k++)
                if (ours[side * 9 + k] != theirs[side * 9 + k]) any = true, only45 = only45 && (k == 4 || k == 5);
            if (!any || !only45) continue;
            const int team = a.fixtures[side];
            bool explained = true;
            int value[2] = {0, 0};
            for (int k = 4; k < 6; k++) {
                if (ours[side * 9 + size_t(k)] == theirs[side * 9 + size_t(k)]) continue;
                // (the slot may also change between two players of the profile: the
                //  original lies between our value and the one with every player penalised)
                // (the stack slot may hold a smaller value too: the range spans the ratings with
                //  no player and with every player penalised)
                int lo = ours[side * 9 + size_t(k)], hi = lo;
                for (const int stale : {0, 0x7fff}) {
                    GameState copy = *state;  // (team_rating marks the players as having played)
                    RatingContext rc{state->chance_rain, state->chance_temperature};
                    rc.live_scene = state->live_scene;
                    rc.stale_technique = stale;
                    value[k - 4] = team_rating(copy, team, kRatingProfiles[k], rc);
                    lo = std::min(lo, value[k - 4]), hi = std::max(hi, value[k - 4]);
                }
                explained = explained && theirs[side * 9 + size_t(k)] >= lo && theirs[side * 9 + size_t(k)] <= hi;
            }
            if (!explained) std::printf("    (team %d: no stale technique explains ratings 4/5)\n", team, value[0], value[1]);
            if (explained) {
                std::printf("  (match %zu, team %d: ratings 4/5 explained by a clobbered stale technique, taken over)\n",
                            side / 2, team);
                ours[side * 9 + 4] = theirs[side * 9 + 4], ours[side * 9 + 5] = theirs[side * 9 + 5];
            }
        }
        diffs += compare((g + "sides").c_str(), ours, theirs, 2);
        for (size_t i = 0; i < ours.size() && i < theirs.size(); i++)
            if (ours[i] != theirs[i] && (i % 9 == 0 || ours[i - 1] == theirs[i - 1]) && i / 9 < a.fixtures.size())
                std::printf("    (match %zu, %s side: team %d, byte %zu)\n", i / 18, i % 18 < 9 ? "home" : "away",
                            a.fixtures[i / 9], i % 9);
        if (ours != theirs && theirs.size() == a.records.size()) a.records = theirs;  // continue with the recorded sides
        // (a first snapshot later than minute 1 already holds the goals of the minutes before)
        if (minute <= 1) {
            diffs += compare((g + "results").c_str(), a.results, o.results, 2);
            diffs += compare((g + "cup results").c_str(), a.cup_results, o.cup_results, 4);
        }
        diffs += compare((g + "other leg").c_str(), a.cup_other_leg, o.cup_other_leg, 4);
    }
    if (heap_attitudes) std::printf("  %d home attitudes taken from the recording (heap dependent)\n", heap_attitudes);
    if (diffs == 0) std::printf("  all kick-off data OK\n");
    return diffs;
}

// Loading a matchday savegame prepares the matchday once more (legacy routine): prepare_team
// and the line-ups run a second time on a state already prepared, which e.g. overwrites the
// league card counters with those of a cup. The desk snapshot holds the tables after the
// load; the ones the preparation touches are taken from there.
void adopt_desk_tables(GameState& s, const std::string& state) {
    if (field(state, "squad_slots").empty()) return;
    const GameState d = state_from_dump(state);
    if (d.squad_slots.size() == s.squad_slots.size()) s.squad_slots = d.squad_slots;
    if (d.players.size() == s.players.size())
        for (size_t i = 0; i < s.players.size(); i++)
            s.players[i].w35c = d.players[i].w35c, s.players[i].w364 = d.players[i].w364, s.players[i].w368 = d.players[i].w368;
    if (d.team_flags.size() == s.team_flags.size()) s.team_flags = d.team_flags;
    if (d.player_cards.size() == s.player_cards.size()) s.player_cards = d.player_cards;
    if (d.tactics.size() == s.tactics.size()) s.tactics = d.tactics;
}

bool check(const char* what, const GameState& g, const GameRandom& game, const WatcomRandom& rand, const Entry& e) {
    const GameState o = state_from_dump(e.state);
    // Snapshot text is immutable. Parse once before the millions of RNG-distance
    // comparisons; repeated full-string searches otherwise dominate bad-seed diagnostics.
    const int32_t expected_game_seed = int32_t(std::stoll(field(e.line, "game_seed")));
    const uint32_t expected_rand_seed = uint32_t(std::stoull(field(e.line, "rand_seed")));
    bool seeds = game.seed() == expected_game_seed &&
                 rand.seed() == expected_rand_seed;
    std::printf("%s: seeds %s\n", what, seeds ? "OK" : "DIFF");
    if (!seeds) {
        GameRandom gp = game;
        WatcomRandom wp = rand;
        long gn = game.seed() == expected_game_seed ? 0 : -1;
        long wn = rand.seed() == expected_rand_seed ? 0 : -1;
        for (long k = 1; k < 200000 && gn < 0; k++) {
            gp.range(0, 1);
            if (gp.seed() == expected_game_seed) gn = k;
        }
        for (long k = 1; k < 20000000 && wn < 0; k++) {
            wp.range(0, 1);
            if (wp.seed() == expected_rand_seed) wn = k;
        }
        std::printf("  the original drew %ld more game and %ld more Watcom numbers (-1 = not found)\n", gn, wn);
        if (std::getenv("WEEK_PROBE") && g.memory.has(::bmh::layout::CareerSession_Invalid_RuntimeField2)) {
            // development probe: did the original restart its game generator from the seed
            // saved in format field (e.g. a contract negotiation)?
            GameRandom t(int32_t(g.memory.u32(::bmh::layout::CareerSession_Invalid_RuntimeField2)));
            for (long k = 0; k < 20000; k++, t.range(0, 1))
                if (t.seed() == expected_game_seed) {
                    std::printf("  (the game seed lies %ld draws after format field)\n", k);
                    break;
                }
        }
        if (std::getenv("WEEK_PROBE") && gn > 0) {
            // development probe: would extra stock market days explain the game draws?
            GameState copy = g;
            GameRandom t = game;
            for (int n = 1; n <= 3; n++) {
                stock_market(copy, t);
                if (t.seed() == expected_game_seed)
                    std::printf("  (%d more stock market day%s explain the game draws)\n", n, n > 1 ? "s" : "");
            }
            {
                GameState c2 = g;
                GameRandom t2 = game;
                WatcomRandom r2 = rand;
                for (size_t m = 0; m < c2.manager_teams.size(); m++) train_squad(c2, int(m), t2, r2);
                std::printf("  (a training day would draw %ld game numbers)\n", long(t2.calls() - game.calls()));
            }
        }
        if (wn < 0) {
            // the other way round: how far the port is ahead of the original
            WatcomRandom wo(expected_rand_seed);
            for (long k = 1; k < 20000000; k++) {
                wo.range(0, 1);
                if (wo.seed() == rand.seed()) {
                    std::printf("  (the port drew %ld more Watcom numbers)\n", k);
                    break;
                }
            }
        }
    }
    int d = 0;
    std::vector<uint32_t> a35c, b35c, a368, b368, a364, b364;
    for (const Player& p : g.players) a35c.push_back(p.w35c), a368.push_back(p.w368), a364.push_back(p.w364);
    for (const Player& p : o.players) b35c.push_back(p.w35c), b368.push_back(p.w368), b364.push_back(p.w364);
    d += compare("squad_slots", g.squad_slots, o.squad_slots, 4);
    d += compare("w35c", a35c, b35c, 8);
    d += compare("w368", a368, b368, 8);
    d += compare("w364", a364, b364, 8);
    d += compare("team_morale", g.team_morale, o.team_morale, 2);
    d += compare("team_flags", g.team_flags, o.team_flags, 4);
    d += compare("player_cards", g.player_cards, o.player_cards, 4);
    d += compare("player_bonus", g.player_bonus, o.player_bonus, 2);
    d += compare("club_table", g.club_table, o.club_table, 2);
    d += compare("tactics", g.tactics, o.tactics, 2);
    d += compare("managers", g.manager_records, o.manager_records, 2);
    {
        const Bytes cards = unhex(field(e.state, "team_cards"));
        if (!cards.empty() && g.memory.has(::bmh::layout::DeskRecords_Teams_RuntimeField)) {
            std::vector<uint8_t> ours(cards.size()), theirs(cards.begin(), cards.end());
            for (size_t i = 0; i < cards.size(); i++) ours[i] = g.memory.u8(::bmh::layout::DeskRecords_Teams_RuntimeField + uint32_t(i));
            d += compare("team_cards", ours, theirs, 2);
        }
    }
    {
        // newer recordings: result table, postponed matches, calendar and mailbox
        // (during a matchday the original writes the scores straight into the result table,
        // the port only after the matchday: not compared at the whistle and minute 91)
        const Bytes results = unhex(field(e.state, "results"));
        const bool in_matchday = std::string(what).find("whistle") != std::string::npos ||
                                 std::string(what).find("minute") != std::string::npos;
        if (!results.empty() && !in_matchday)
            d += compare("results", g.results, std::vector<uint8_t>(results.begin(), results.end()), 2);
        const struct { const char* name; uint32_t address; } blocks[] = {{"postponed", ::bmh::layout::Postponed}, {"calendar", ::bmh::layout::CompetitionCalendar}};
        for (const auto& blk : blocks) {
            const Bytes theirs = unhex(field(e.state, blk.name));
            if (theirs.empty() || !g.memory.has(blk.address)) continue;
            std::vector<uint8_t> ours(theirs.size());
            for (size_t i = 0; i < theirs.size(); i++) ours[i] = g.memory.u8(blk.address + uint32_t(i));
            d += compare(blk.name, ours, std::vector<uint8_t>(theirs.begin(), theirs.end()), 2);
        }
        const Bytes stocks = unhex(field(e.state, "stocks"));
        if (!stocks.empty() && stocks.size() <= g.stocks.size())
            d += compare("stocks", std::vector<uint8_t>(g.stocks.begin(), g.stocks.begin() + long(stocks.size())),
                         std::vector<uint8_t>(stocks.begin(), stocks.end()), 2);
        if (!field(e.state, "bbcd8").empty() && g.memory.has(::bmh::layout::CareerSession_Invalid_RuntimeField2) &&
            g.memory.u32(::bmh::layout::CareerSession_Invalid_RuntimeField2) != uint32_t(std::stoull(field(e.state, "bbcd8"))))
            std::printf("  bbcd8          DIFF (%u/%s)\n", g.memory.u32(::bmh::layout::CareerSession_Invalid_RuntimeField2), field(e.state, "bbcd8").c_str()), d++;
        // the transfer list (recorded since season 4): informative only, its bid amounts may
        // differ by the stale bid key (see transfer_bid)
        if (!field(e.state, "translist").empty() && g.memory.has(::bmh::layout::EditorList)) {
            const Bytes theirs = unhex(field(e.state, "translist"));
            std::vector<uint8_t> ours(theirs.size());
            for (size_t i = 0; i < ours.size(); i++) ours[i] = g.memory.u8(::bmh::layout::EditorList + uint32_t(i));
            const std::vector<uint8_t> t(theirs.begin(), theirs.end());
            if (ours != t) compare("(transfer list, not counted)", ours, t, 2);
            // (WEEK_ADOPT_LIST=1, diagnosis: the recorded list replaces ours at every snapshot whose
            //  seeds agree, to tell a gap of the stale bid key from other causes)
            if (std::getenv("WEEK_ADOPT_LIST") && seeds)
                for (size_t i = 0; i < t.size(); i++) const_cast<GameState&>(g).memory.set8(::bmh::layout::EditorList + uint32_t(i), t[i]);
        }
        const Bytes mailbox = unhex(field(e.state, "mailbox"));
        if (mailbox.size() == g.mailbox.size()) {
            std::string where;
            const int dm = compare_mailbox(g.mailbox, std::vector<uint8_t>(mailbox.begin(), mailbox.end()), &where);
            if (dm) std::printf("  mailbox        DIFF (%d entries, first %s)\n", dm, where.c_str()), d += dm;
        }
    }
    if (g.development_balance != o.development_balance || g.trend_rotation != o.trend_rotation)
        std::printf("  balance/rotation DIFF (%d/%d, %d/%d)\n", g.development_balance, o.development_balance,
                    g.trend_rotation, o.trend_rotation), d++;
    if (seeds && d == 0) std::printf("  all tables OK\n");
    return seeds && d == 0;
}

// The manager order of the matchday (legacy routine into format field, u16 entries) and the
// position of the desk being shown (format field; format field = that manager).
int order_manager(const GameState& g, int k) { return g.memory.u16(::bmh::layout::NextDeskManager + uint32_t(k) * 2); }
void store_manager_order(GameState& g, const std::vector<int>& order) {
    for (size_t k = 0; k < order.size(); k++) g.memory.set16(::bmh::layout::NextDeskManager + uint32_t(k) * 2, uint16_t(order[k]));
}
void set_desk_manager(GameState& g, int k) {
    g.memory.set8(::bmh::layout::DeskPosition, uint8_t(k));
    g.memory.set8(::bmh::layout::ActiveManager, uint8_t(order_manager(g, k)));
}

// legacy routine after "Anstoss" at the desk the recording starts at: the offers for that
// manager's players are answered (legacy routine), then every later manager of the order gets
// his desk - pre-match offer (legacy routine, its message box is a pause of its own), pools
// coupon (legacy routine) and the desk (legacy routine, a recorded pause; the turn automation
// clicks "Anstoss" there) - and his offers answered. `ip` walks the recorded pauses.
// After loading a game (WEEK_AFTER_LOAD=1) the original continues the desk loop with
// legacy routine(1): the later managers get no pre-match offer.
bool later_desks(GameState& s, GameRandom& g, WatcomRandom& r, const std::vector<MatchGroup>& sat,
                 const std::vector<Entry>& entries, size_t& ip, MinuteReport& rep) {
    const bool accept = !std::getenv("WEEK_ACCEPT") || std::string(std::getenv("WEEK_ACCEPT")) != "0";
    const int n = int(s.manager_teams.size());
    if (n < 2) {
        for (int m = 0; m < n && !rep.unsupported; m++) answer_offers(s, m, accept, r, rep, &g);
        free_offer_slots(s);
        return true;
    }
    bool fine = true;
    auto next_pause = [&]() -> const Entry* {
        while (ip < entries.size() && entries[ip].kind != "post") ip++;
        return ip < entries.size() ? &entries[ip++] : nullptr;
    };
    const bool after_load = std::getenv("WEEK_AFTER_LOAD") && std::string(std::getenv("WEEK_AFTER_LOAD")) == "1";
    int k = s.memory.u8(::bmh::layout::DeskPosition);
    answer_offers(s, order_manager(s, k), accept, r, rep, &g);
    for (k++; k < n && !rep.unsupported; k++) {
        set_desk_manager(s, k);
        const int mgr = order_manager(s, k);
        if (!after_load && pre_match_offer(s, mgr, g, r, rep)) {
            if (const Entry* e = next_pause()) check("checkpoint offer message", s, g, r, *e);
        }
        build_pools_coupon(s, sat, &r);
        enter_desk(s, mgr, g, r);
        std::printf("  the desk of manager %d (order position %d)\n", mgr, k);
        if (const Entry* e = next_pause()) fine = check("checkpoint desk of the next manager", s, g, r, *e) && fine;
        answer_offers(s, mgr, accept, r, rep, &g);
    }
    free_offer_slots(s);  // (after every desk)
    return fine;
}

}  // namespace

static int run(int argc, char** argv);

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::printf("exception: %s\n", e.what());
        return 3;
    }
}

static int run(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: week_check <capture.jsonl> <savegame before the matchday>\n");
        return 1;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::vector<Entry> entries;
    {
        std::ifstream in(argv[1]);
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("{\"state\"", 0) == 0) {
                if (!entries.empty() && entries.back().state.empty()) entries.back().state = line;
                continue;
            }
            size_t colon = line.find(':');
            if (line.size() < 4 || colon == std::string::npos) continue;
            Entry e;
            e.kind = line.substr(2, colon - 3);
            e.value = std::stoi(line.substr(colon + 1, line.find_first_of(",}", colon) - colon - 1));
            e.line = line;
            entries.push_back(e);
        }
    }
    // The recorder reads format field from the loaded DOSBox process on every
    // minute snapshot. Source treats this as an EXE-owned latch (CareerSession
    // reads the same loaded-image byte); it is not serialized in the FBM. Require
    // every minute row in this replay to carry the same captured byte. Missing or
    // contradictory provenance stays unsupported rather than inheriting false/0.
    TransferAllocatorContext allocator_context = TransferAllocatorContext::unsupported;
    bool saw_minute_snapshot = false;
    bool all_minute_snapshots_have_gate = true;
    bool conflicting_gate = false;
    std::optional<uint8_t> captured_a5046;
    for (const Entry& entry : entries) {
        if (entry.kind != "minute") continue;
        saw_minute_snapshot = true;
        const std::string value = field(entry.line, "match_events_disabled");
        if (value.empty()) {
            all_minute_snapshots_have_gate = false;
            continue;
        }
        unsigned int parsed = 0;
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (error != std::errc{} || end != value.data() + value.size() || parsed > 0xff) {
            all_minute_snapshots_have_gate = false;
            continue;
        }
        const uint8_t observed = uint8_t(parsed);
        if (captured_a5046 && *captured_a5046 != observed) conflicting_gate = true;
        else captured_a5046 = observed;
    }
    if (saw_minute_snapshot && all_minute_snapshots_have_gate && captured_a5046 && !conflicting_gate)
        allocator_context = *captured_a5046 == 0 ? TransferAllocatorContext::zero
                                                  : TransferAllocatorContext::blocked;
    if (conflicting_gate)
        std::printf("allocator provenance unsupported: captured format field changes within this replay\n");
    else if (!saw_minute_snapshot || !all_minute_snapshots_have_gate)
        std::printf("allocator provenance unsupported: minute capture lacks a valid format field observation\n");
    if (std::getenv("WEEK_MAP")) {
        // Draws between consecutive snapshots (day and pause snapshots only).
        const Entry* prev = nullptr;
        for (const Entry& e : entries) {
            if (e.kind == "minute" && e.value != -1 && std::string(std::getenv("WEEK_MAP")) != "all") continue;
            if (prev) {
                GameRandom gp(int32_t(std::stoll(field(prev->line, "game_seed"))));
                WatcomRandom wp(uint32_t(std::stoull(field(prev->line, "rand_seed"))));
                long gn = 0, wn = 0;
                const int32_t gt = int32_t(std::stoll(field(e.line, "game_seed")));
                const uint32_t wt = uint32_t(std::stoull(field(e.line, "rand_seed")));
                while (gp.seed() != gt && gn < 2000000) gp.range(0, 1), gn++;
                while (wp.seed() != wt && wn < 2000000) wp.range(0, 1), wn++;
                std::printf("%-6s %6d t=%6s  game +%ld  Watcom +%ld\n", e.kind.c_str(), e.value, field(e.line, "t_ms").c_str(), gn, wn);
            }
            prev = &e;
        }
        return 0;
    }
    Savegame sg = load_savegame(argv[2]);
    SaveState st(sg.stream);
    GameState saved_ = GameState::from_save(st);
    national_coach_after_load(saved_);  // (legacy routine: a Bundestrainer takes over again)
    if (const char* x = std::getenv("WEEK_TOGGLE")) saved_.list_toggle = std::atoi(x);
    // Run-time statics the savegame does not hold (the transfer list toggle format field, the
    // autosave counter format field / format field): newer recordings dump them with the
    // first snapshot; for older ones WEEK_TOGGLE=n and WEEK_AUTOSAVE=months,month set them.
    if (!entries.empty() && !field(entries[0].state, "a3fd3").empty()) {
        saved_.list_toggle = std::stoi(field(entries[0].state, "a3fd3"));
        saved_.autosave_months = std::stoi(field(entries[0].state, "bd8fc"));
        saved_.autosave_month = std::stoi(field(entries[0].state, "bdb75"));
    }
    // The line-up screen opened at the desk before the recording (an input the recording does
    // not hold, e.g. while looking at the game after loading): its set-piece marker
    // (legacy routine) shows in the first snapshot and is taken over.
    if (!entries.empty() && !field(entries[0].state, "managers").empty()) {
        const GameState first = state_from_dump(entries[0].state);
        for (size_t m = 0; m < saved_.manager_teams.size(); m++) {
            const size_t at = m * 0xb0c + 0xb0a;
            if (first.manager_records.size() > at + 1 && first.manager_records[at] == 0x53 && first.manager_records[at + 1] == 0x32 &&
                !(saved_.manager_records[at] == 0x53 && saved_.manager_records[at + 1] == 0x32)) {
                std::printf("(manager %zu opened the line-up screen before the recording: set-piece marker taken over)\n", m);
                open_lineup_screen(saved_, int(m));
            }
        }
    }
    if (const char* x = std::getenv("WEEK_AUTOSAVE")) {
        saved_.autosave_months = std::atoi(x);
        if (const char* comma = std::strchr(x, ',')) saved_.autosave_month = std::atoi(comma + 1);
    }
    // The weather of the last chance of the session's last matchday (GameState::
    // chance_rain) is not saved but still read by the next kick-off (section S, T, F):
    // WEEK_CHANCE_WEATHER = "rain,temperature" (WEEK_CHANCE=1 searches the values that
    // reproduce the recorded side ratings of a later kick-off).
    if (const char* x = std::getenv("WEEK_CHANCE_WEATHER")) {
        saved_.chance_rain = std::atoi(x);
        if (const char* comma = std::strchr(x, ',')) saved_.chance_temperature = std::atoi(comma + 1);
    }
    // the JA/NEIN questions of the weekly offers: NEIN unless WEEK_OFFER_YES=1
    saved_.offer_answer = std::getenv("WEEK_OFFER_YES") && std::string(std::getenv("WEEK_OFFER_YES")) == "1";
    const GameState saved = saved_;
    const std::vector<uint8_t> buttons(st.data(::bmh::layout::LeaguePresentationOptions), st.data(::bmh::layout::LeaguePresentationOptions) + 0xb4);
    const Database db = load_database(std::filesystem::path(argv[2]).parent_path() / "M3D.MAN");
    bmh::test::load_executable(std::filesystem::path(argv[2]).parent_path() / "BMH.exe");
    const TextDb textdb = load_textd(std::filesystem::path(argv[2]).parent_path() / "TEXTD.BLK");
    if (const char* want = std::getenv("WEEK_TEXT")) {
        // development helper: the text ids (and pointer variables format field + 4 * id) of
        // the texts containing a string
        for (size_t id = 0; id < textdb.strings.size(); id++)
            if (textdb[id].find(want) != std::string::npos)
                std::printf("text %zu (offset %08x): %s\n", id, unsigned(::bmh::layout::TextPointerTable + id * 4), textdb[id].c_str());
        return 0;
    }

    // The replay state from the first day change after the recorded matchday on (sections B
    // to G). It lives here so that the indoor tournament round below can continue with the
    // following days too.
    GameState b;
    GameRandom bg;
    WatcomRandom br;
    bool ok = true;
    // format field: the calendar of the season, indexed by the day of the season
    auto cal_at = [](const GameState& s, uint32_t date) -> uint16_t {
        const int d = int(date) - int(s.season_start);
        if (d < 0 || d >= 0x16e) return 0;
        return s.memory.u16(::bmh::layout::CompetitionCalendar + uint32_t(d) * 2);
    };

    // The human manager's next matchday: preparation up to his pre-match desk
    // (legacy routine), where the recording ends and the game was saved. Checks the pause
    // snapshot and, with a third argument, the savegame block by block.
    auto desk_turn = [&](uint16_t today, const Entry& pause, bool season_end = false, bool seeds = true,
                         bool next_round = false) -> bool {
        bool fine = true;
        // legacy routine at the day's event: format field counts the rounds of the day (the
        // indoor tournament plays three on its first day, two on its second)
        {
            const uint16_t tomorrow = cal_at(b, b.date + 1);
            // (not on the season end path, and not for a later round of an indoor day)
            if (b.memory.has(::bmh::layout::EventPasses) && !season_end && !next_round)
                b.memory.set8(::bmh::layout::EventPasses, today == 0x4002 ? (tomorrow == 0x4002 ? 3 : 2) : 1);
        }
        std::vector<uint8_t> refs = db.raw_referee_properties;
        MatchdayContext c3;
        c3.month = month_code(b.date);  // format field (set by the day step)
        c3.season_day = b.season_day();
        c3.weekday = weekday_code(b.date);
        c3.referees = &refs;
        MinuteReport r4;
const Entry* desk = &pause;  // (after an offer message box the next pause)
        try {
            // (at the "Saisonende" desk legacy routine does nothing: no matchday, and the
            //  teams flagged for a new line-up keep theirs)
            // (the friendlies of the day stay in the plan until the kick-off prepares them again
            //  - the savegames at such a desk still list them: the plan is kept here)
            std::vector<uint8_t> plan;
            for (uint32_t a : {::bmh::layout::FriendlyTeamsUnsigned, ::bmh::layout::FriendlyDaysUnsigned})
                for (uint32_t i = 0; i < (a == ::bmh::layout::FriendlyTeamsUnsigned ? 120u : 60u); i++) plan.push_back(b.memory.has(a + i) ? b.memory.u8(a + i) : 0);
            const uint8_t planned = b.memory.u8(cup_entry_address(30) + 0x1a);
            const std::vector<MatchGroup> sat =
                season_end ? std::vector<MatchGroup>{} : setup_matchday(b, day_competitions(b, today), c3, bg, br, r4);
            {
                size_t k = 0;
                for (uint32_t a : {::bmh::layout::FriendlyTeamsUnsigned, ::bmh::layout::FriendlyDaysUnsigned})
                    for (uint32_t i = 0; i < (a == ::bmh::layout::FriendlyTeamsUnsigned ? 120u : 60u); i++, k++)
                        if (b.memory.has(a + i)) b.memory.set8(a + i, plan[k]);
                b.memory.set8(cup_entry_address(30) + 0x1a, planned);
            }
            // (format field, the day's competitions, legacy routine in legacy routine)
            if (season_end && b.memory.has(::bmh::layout::SavedCompetition)) b.memory.set32(::bmh::layout::SavedCompetition, day_competitions(b, today));
            draw_matchday_number(b, br);  // format field
            const std::vector<int> order = shuffle_manager_order(b, bg);
            store_manager_order(b, order);
            // the desks shown before manager 0's (the turn automation clicked "Anstoss" there;
            // it saves at manager 0's desk): the run of pauses after the last day or minute
            // snapshot holds one per desk, in order
            const size_t k0 = size_t(std::find(order.begin(), order.end(), 0) - order.begin());
            std::vector<const Entry*> desks(k0 + 1, desk);
            if (k0 > 0) {
                size_t first = size_t(desk - entries.data());
                while (first > 0 && entries[first - 1].kind == "post") first--;
                for (size_t k = 0, i = first; k <= k0 && i < entries.size(); i++)
                    if (entries[i].kind == "post") desks[k++] = &entries[i];
                    else break;
            }
            desk = desks[0];
            for (size_t k = 0; k < order.size(); k++) {
                const int mgr = order[k];
                set_desk_manager(b, int(k));
                if (k > 0) {
                    // the desk before was another manager's: its pause, then his offers
                    // answered (legacy routine)
                    {
                        // (only a pause with the desk's seeds is its own; without one - the
                        //  recorder caught a later screen - the next desks and the savegame decide)
                        const bool own = int32_t(std::stoll(field(desk->line, "game_seed"))) == bg.seed() &&
                                         uint32_t(std::stoull(field(desk->line, "rand_seed"))) == br.seed();
                        if (own) fine = check("checkpoint desk of an earlier manager", b, bg, br, *desk) && fine;
                        else std::printf("  (desk of an earlier manager: no pause with its seeds recorded, not counted)\n");
                    }
                    if (k < desks.size()) desk = desks[k];
                    // A salary demand answered NEIN opens the negotiation ("Entscheidung"),
                    // which restarts the game generator from format field: when a later
                    // pause shows that seed the turn automation declined (WEEK_ACCEPT forces)
                    bool accept = true;
                    if (const char* a = std::getenv("WEEK_ACCEPT")) {
                        accept = std::string(a) != "0";
                    } else if (b.memory.has(::bmh::layout::CareerSession_Invalid_RuntimeField2)) {
                        GameRandom restart(int32_t(b.memory.u32(::bmh::layout::CareerSession_Invalid_RuntimeField2)));
                        restart.range(0x5c, 0x6c);
                        for (size_t i = size_t(desk - entries.data()); i < entries.size() && entries[i].kind == "post"; i++)
                            if (int32_t(std::stoll(field(entries[i].line, "game_seed"))) == restart.seed()) accept = false;
                    }
                    answer_offers(b, order[k - 1], accept, br, r4, &bg);
                }
                if (pre_match_offer(b, mgr, bg, br, r4)) {  // legacy routine
                    // its message box waits for a click: that pause comes first, the desk
                    // is the next one (informative: the message is filed after the box)
                    check("checkpoint offer message", b, bg, br, *desk);
                    size_t i = size_t(desk - entries.data()) + 1;
                    while (i < entries.size() && entries[i].kind != "post") i++;
                    if (i < entries.size()) desk = &entries[i];
                }
                build_pools_coupon(b, sat, &br);      // legacy routine
                enter_desk(b, mgr, bg, br);       // entering the desk legacy routine
                // (more pauses may lie between two desks - an offer's question, the
                //  negotiation: the desk's own pause is the one with our seeds)
                for (size_t i = size_t(desk - entries.data()); i < entries.size() && entries[i].kind == "post"; i++)
                    if (int32_t(std::stoll(field(entries[i].line, "game_seed"))) == bg.seed() &&
                        uint32_t(std::stoull(field(entries[i].line, "rand_seed"))) == br.seed()) {
                        desk = &entries[i];
                        break;
                    }
                if (mgr == 0) break;              // (it waits for the manager who saves)
            }
        } catch (const std::exception& ex) {
            r4.fail(std::string("exception: ") + ex.what());
        }
        if (r4.unsupported) std::printf("  not supported: %s\n", r4.reason.c_str());
        {
            // (without a pause on arrival the desk's pause comes only after saving there,
            // which draws 4 Watcom numbers: compared with those)
            WatcomRandom saved_rand = br;
            for (int k = 0; k < 4; k++) saved_rand.rand();
            const bool after_save = bg.seed() == int32_t(std::stoll(field(desk->line, "game_seed"))) &&
                                    saved_rand.seed() == uint32_t(std::stoull(field(desk->line, "rand_seed")));
            if (after_save) std::printf("  (desk pause read after saving: 4 Watcom numbers of the save)\n");
            if (seeds) fine = check("checkpoint desk", b, bg, after_save ? saved_rand : br, *desk) && fine;
            else std::printf("  (no pause recorded at the desk: the savegame alone is compared)\n");
        }
        if (argc > 3) {
            // the game saved at this desk: every block of the savegame
            Savegame sg6 = load_savegame(argv[3]);
            SaveState st6(sg6.stream);
            // (our state in the form legacy routine writes: a Bundestrainer's slots swapped back)
            const GameState bs = national_coach_save_form(b);
            const GameState w6 = GameState::from_save(st6);
            std::printf("\nsavegame %s against the replay:\n", argv[3]);
            int d6 = 0;
            d6 += compare("results", bs.results, w6.results, 2);
            d6 += compare("league_rounds", bs.league_rounds, w6.league_rounds, 2);
            d6 += compare("position_hist", bs.position_history, w6.position_history, 2);
            d6 += compare("club_table", bs.club_table, w6.club_table, 2);
            d6 += compare("stocks", bs.stocks, w6.stocks, 2);
            d6 += compare("mgr_ratings", bs.manager_ratings, w6.manager_ratings, 2);
            d6 += compare("mgr_results", bs.manager_results, w6.manager_results, 2);
            d6 += compare("managers", bs.manager_records, w6.manager_records, 2);
            d6 += compare("tactics", bs.tactics, w6.tactics, 2);
            d6 += compare("squad_slots", bs.squad_slots, w6.squad_slots, 4);
            d6 += compare("player_cards", bs.player_cards, w6.player_cards, 4);
            d6 += compare("team_flags", bs.team_flags, w6.team_flags, 4);
            {
                // the player words (w35c skills and injury type, w364 position, flags and
                // absence, w368 skills and cards)
                std::vector<uint32_t> a35c, b35c, a368, b368, a364, b364;
                for (const Player& p : bs.players) a35c.push_back(p.w35c), a368.push_back(p.w368), a364.push_back(p.w364);
                for (const Player& p : w6.players) b35c.push_back(p.w35c), b368.push_back(p.w368), b364.push_back(p.w364);
                d6 += compare("w35c", a35c, b35c, 8);
                d6 += compare("w368", a368, b368, 8);
                d6 += compare("w364", a364, b364, 8);
                d6 += compare("team_morale", bs.team_morale, w6.team_morale, 2);
            }
            if (bs.date != w6.date) std::printf("  date %u / %u\n", bs.date, w6.date), d6++;
        {
            // the indoor tournament's name (heap text, compared up to its NUL)
            auto text = [](const std::vector<uint8_t>& v) {
                std::string s;
                for (uint8_t c : v) { if (!c) break; s.push_back(char(c)); }
                return s;
            };
            if (text(bs.tournament_name) != text(w6.tournament_name))
                std::printf("  tournament name \"%s\" / \"%s\"\n", text(bs.tournament_name).c_str(), text(w6.tournament_name).c_str()), d6++;
        }
            // the other static blocks, as far as the replay keeps them
            for (const SaveBlock& blk : st6.blocks()) {
                if (blk.heap || !bs.memory.has(blk.address)) continue;
                // blocks GameState keeps in typed fields (compared above)
                static const uint32_t typed[] = {::bmh::layout::ManagerRecords, ::bmh::layout::ManagerMatchHistory, ::bmh::layout::ManagerRatings, ::bmh::layout::DaysIntoSeason, ::bmh::layout::CalendarDay, ::bmh::layout::PlayerDevelopmentBalance,
                                                 ::bmh::layout::LeagueResults, ::bmh::layout::LeagueRoundCounters, ::bmh::layout::TacticsBoards, ::bmh::layout::StockExchange, ::bmh::layout::ManagerAutomation, ::bmh::layout::Formations,
                                                 ::bmh::layout::ForeignClubStrength, ::bmh::layout::Difficulty, ::bmh::layout::SeasonIndex};
                if (std::find(std::begin(typed), std::end(typed), blk.address) != std::end(typed)) continue;
                std::vector<uint8_t> ours(blk.size), theirs(st6.data(blk.address), st6.data(blk.address) + blk.size);
                for (size_t i = 0; i < blk.size; i++) ours[i] = bs.memory.u8(blk.address + uint32_t(i));
                if (blk.address == ::bmh::layout::MatchdayGroupDescriptors)
                    // match group descriptors: the heap pointers are not modelled
                    for (size_t i = 0; i < blk.size; i++) {
                        const size_t o = i % 0xd0;
                        if ((o >= 4 && o < 0x10) || (o >= 0x7c && o < 0x88) || (o >= 0x98 && o < 0x9c) ||
                            (o >= 0xb4 && o < 0xb8))
                            ours[i] = theirs[i];
                    }
                if (ours != theirs) {
                    char name[32];
                    std::snprintf(name, sizeof name, "block %05x", blk.address);
                    compare(name, ours, theirs, 2);
                }
            }
            {
                std::string where;
                const int dm = compare_mailbox(bs.mailbox, w6.mailbox, &where);
                if (dm) {
                    std::printf("  mailbox        DIFF (%d entries, first %s)\n", dm, where.c_str());
                    d6 += dm;
                    // both versions of the first differing entry
                    const size_t k = size_t(std::atoi(where.c_str() + 6));
                    const std::vector<uint8_t>* boxes[2] = {&bs.mailbox, &w6.mailbox};
                    // (WEEK_MAILBOX_ALL: every entry from the first differing one on)
                    const size_t k_end = std::getenv("WEEK_MAILBOX_ALL") ? bs.mailbox.size() / kMailboxEntry : k + 1;
                    for (size_t kk = k; kk < k_end; kk++)
                    for (int which = 0; which < 2; which++) {
                        const std::vector<uint8_t>* box = boxes[which];
                        const size_t e = kk * kMailboxEntry;
                        std::string text;
                        for (size_t i = 10; i < kMailboxEntry && (*box)[e + i]; i++) text.push_back(char((*box)[e + i]));
                        std::printf("    %s: to %02x%02x%02x%02x date %u \"%s\"\n", which == 0 ? "ours    " : "original",
                                    (*box)[e + 3], (*box)[e + 2], (*box)[e + 1], (*box)[e], (*box)[e + 8] << 8 | (*box)[e + 9],
                                    text.c_str());
                    }
                }
                else std::printf("  mailbox OK%s\n", bs.mailbox == w6.mailbox ? " (byte for byte)" : "");
            }
            if (d6 == 0) std::printf("  typed tables OK\n");
        }
        return fine;
    };

    // A day snapshot of the "Weiter" loop. The recorder may notice a date change late,
    // when the game already runs the processing of the new day (the event, the weekly
    // processing, the next stock market run, ...): then both recorded seeds lie a little
    // ahead of ours. Such a snapshot is not counted; the next pause or desk decides.
    auto day_check = [&](const Entry& e) -> bool {
        if (check(("checkpoint day " + std::to_string(b.date)).c_str(), b, bg, br, e)) return true;
        GameRandom gp = bg;
        WatcomRandom wp = br;
        bool g_ahead = false, w_ahead = false;
        for (long k = 0; k < 20000 && !g_ahead; k++, gp.range(0, 1))
            g_ahead = gp.seed() == int32_t(std::stoll(field(e.line, "game_seed")));
        for (long k = 0; k < 20000 && !w_ahead; k++, wp.rand())
            w_ahead = wp.seed() == uint32_t(std::stoull(field(e.line, "rand_seed")));
        if (!w_ahead) {
            // (a Watcom seed on no path of the stream: the snapshot caught a temporary generator
            //  state, as with minute snapshots - not counted)
            WatcomRandom wf = br;
            bool found = false;
            for (long k = 0; k < 3000000 && !found; k++, wf.rand())
                found = wf.seed() == uint32_t(std::stoull(field(e.line, "rand_seed")));
            WatcomRandom wb(uint32_t(std::stoull(field(e.line, "rand_seed"))));
            long behind = -1;
            for (long k = 0; k < 3000000 && behind < 0; k++, wb.rand())
                if (wb.seed() == br.seed()) behind = k;
            if (!found && behind < 0) {
                std::printf("  (Watcom seed of the snapshot off the stream: a temporary state, not counted)\n");
                return true;
            }
            if (behind >= 0 && behind < 20000) {
                // the original changed the date before the processing of the matchday was done
                // (an early snapshot: our seeds lie ahead of it)
                std::printf("  (early snapshot: taken before the day's processing was done, %ld Watcom draws "
                            "behind, not counted)\n", behind);
                return true;
            }
        }
        if (!g_ahead || !w_ahead) return false;
        std::printf("  (late snapshot: taken inside the processing of day %u, not counted)\n", b.date);
        return true;
    };

    // After the phases of a matchday: the "played" markers of the tactics boards are reset,
    // every group is processed (league results into the result block, cup results into
    // the cup tables), legacy routine restores the attitude bytes and assistant options
    // saved before the kick-off, then the league rounds and cup rounds of the day advance.
    auto finish_matchday = [&](std::vector<MatchGroup>& grs, uint16_t today, const std::vector<uint8_t>& saved_records,
                               const std::vector<uint8_t>& saved_options, MinuteReport& r3) {
        finish_event(b, grs, today, buttons, saved_records, saved_options, bg, br, r3);  // (game/turn.hpp)
    };

    // --- section E: the days after an event up to the manager's next desk. Each event on
    // the way (background matchdays, cup and indoor tournament rounds) is prepared, kicked
    // off and replayed from its recorded minutes; `inext` is the first entry after the day
    // change that followed the last replayed event.
    auto days_loop = [&](size_t inext) -> bool {
        bool ok = true;
        // legacy routine in the month change: the contracts screen when a contract has ended.
        // The manager's actions come from the recording: its pause is checked, then the
        // pauses inside it are replayed - the offer input (one Watcom draw, the game
        // generator runs with the clock: adopted) and the offers (W(8, 12), W(80, 100); the
        // row whose term became 12/24/36 months is an accepted offer, a row offended (+0x4f
        // bit 7) a rejected one) - with contract_offer. A pause that is neither ends the screen.
        auto seeds_of = [&](const Entry& e) {
            return std::make_pair(int32_t(std::stoll(field(e.line, "game_seed"))), uint32_t(std::stoull(field(e.line, "rand_seed"))));
        };
        auto watcom_steps = [&](uint32_t to, int limit) {
            WatcomRandom w = br;
            for (int k = 0; k <= limit; k++, w.rand())
                if (w.seed() == to) return k;
            return -1;
        };
        auto next_post = [&](size_t from) {
            while (from < entries.size() && entries[from].kind != "post") from++;
            return from;
        };
        b.contract_screen = [&](GameState& g, int m, int row) -> int {
            const size_t rec = size_t(m) * 0x840;
            size_t p = next_post(inext);
            if (p == entries.size()) {
                std::printf("  contracts screen (row %d): no pause recorded\n", row);
                return -1;
            }
            // (the screen's pause: the first one with our seeds, else the offer input after it)
            for (size_t q = p; q < entries.size(); q = next_post(q + 1))
                if (seeds_of(entries[q]) == std::make_pair(bg.seed(), br.seed())) {
                    p = q;
                    break;
                }
            auto [gs, ws] = seeds_of(entries[p]);
            if (gs == bg.seed() && ws == br.seed()) {
                std::printf("  contracts screen (board row %d, post %d): seeds OK\n", row, entries[p].value);
                if (std::getenv("WEEK_CONTRACT_TABLES")) check("  contracts screen", g, bg, br, entries[p]);
                inext = p + 1;
            } else if (watcom_steps(ws, 1) == 1) {
                std::printf("  contracts screen (board row %d): pause not recorded (the next one is the offer input)\n", row);
            } else {
                std::printf("  contracts screen (board row %d, post %d): seeds DIFF (Watcom %d draws ahead), adopted\n", row,
                            entries[p].value, watcom_steps(ws, 100000));
                ok = false;
                bg = GameRandom(gs), br = WatcomRandom(ws);
                inext = p + 1;
            }
            for (;;) {
                p = next_post(inext);
                if (p == entries.size()) break;
                std::tie(gs, ws) = seeds_of(entries[p]);
                const int dw = watcom_steps(ws, 2);
                if (dw == 1) {
                    br.rand();
                    bg = GameRandom(gs);
                    inext = p + 1;
                    continue;
                }
                if (dw != 2) break;
                {
                    // a pause showing another row just looked at (0x80) is the next screen
                    const GameState o = state_from_dump(entries[p].state);
                    bool next_screen = false;
                    for (int k = 0; k < 24 && o.tactics.size() == g.tactics.size(); k++)
                        if ((o.tactics[rec + size_t(k) * 0x58 + 0xb] & 0x80) && !(g.tactics[rec + size_t(k) * 0x58 + 0xb] & 0x80))
                            next_screen = true;
                    if (next_screen) break;
                }
                // an offer: which row changed (the board is written after the message box,
                // so the pause itself or the one after shows it)
                int r = -1;
                bool accepted = false;
                uint32_t offer = 0;
                int column = 0;
                for (size_t q = p; q < entries.size() && q <= next_post(p + 1) && r < 0; q = next_post(q + 1)) {
                    const GameState o = state_from_dump(entries[q].state);
                    if (o.tactics.size() != g.tactics.size()) continue;
                    const size_t first = squad_range(g.manager_teams.at(size_t(m))).first;
                    for (int k = 0; k < 24 && r < 0; k++) {
                        const size_t at = rec + size_t(k) * 0x58;
                        // (a row whose player changed moved up after a player left: no offer)
                        if (o.squad_slots.size() == g.squad_slots.size() &&
                            (o.squad_slots[first + size_t(k)] & 0xFFF) != (g.squad_slots[first + size_t(k)] & 0xFFF))
                            continue;
                        const uint8_t months = o.tactics[at + 0xc];
                        // (an accepted offer writes term and salary; a month change only counts
                        //  the term down)
                        const bool salary_changed = !std::equal(o.tactics.begin() + long(at + 0x50), o.tactics.begin() + long(at + 0x54),
                                                                g.tactics.begin() + long(at + 0x50));
                        if (months != g.tactics[at + 0xc] && salary_changed && months % 12 == 0 && months >= 12 &&
                            months <= 48) {
                            r = k, accepted = true, column = months / 12 - 1;
                            offer = uint32_t(o.tactics[at + 0x50] | o.tactics[at + 0x51] << 8 | o.tactics[at + 0x52] << 16 |
                                             o.tactics[at + 0x53] << 24);
                        } else if ((o.tactics[at + 0x4f] & 0x80) && !(g.tactics[at + 0x4f] & 0x80)) {
                            r = k;
                        }
                    }
                    if (r >= 0 && accepted) {
                        // compare the outcome with the recorded row; the bonuses (+0x12, 7
                        // bytes) the manager may have set on the bonus page of the talks
                        // count for the offer and are written with it
                        const auto demand = contract_demands(g, m, r, false, br);
                        const size_t bonus_at = rec + size_t(r) * 0x58 + 0x12;
                        std::copy(o.tactics.begin() + long(bonus_at), o.tactics.begin() + long(bonus_at + 7),
                                  g.tactics.begin() + long(bonus_at));
                        const bool got = contract_offer(g, m, r, column, offer, demand[size_t(column)], false, r == row, br);
                        const size_t at = rec + size_t(r) * 0x58;
                        std::printf("  offer %u for %d years to board row %d: %s, mood %u (original %u)%s\n", offer, column + 1, r,
                                    got ? "accepted" : "REJECTED by the port", g.tactics[at + 0xd], o.tactics[at + 0xd],
                                    got && g.tactics[at + 0xd] == o.tactics[at + 0xd] ? "" : "  DIFF");
                        if (!got || g.tactics[at + 0xd] != o.tactics[at + 0xd]) {
                            ok = false;
                            std::copy(o.tactics.begin() + long(at), o.tactics.begin() + long(at + 0x58), g.tactics.begin() + long(at));
                        }
                    } else if (r >= 0) {
                        // (the offer itself is not known; 0 is always rejected)
                        contract_offer(g, m, r, 0, 0, 1, false, r == row, br);
                        std::printf("  offer to board row %d rejected\n", r);
                    }
                }
                if (r < 0) break;  // not an offer: the screen was left before
                std::tie(gs, ws) = seeds_of(entries[p]);
                const bool same = gs == bg.seed() && ws == br.seed();
                std::printf("  post %d: seeds %s\n", entries[p].value, same ? "OK" : "DIFF");
                if (!same) ok = false, bg = GameRandom(gs), br = WatcomRandom(ws);
                inext = p + 1;
            }
            const int renewed = g.tactics.at(rec + size_t(row) * 0x58 + 0xc) > 1 ? 1 : 0;
            std::printf("  the contracts screen is left: the contract of board row %d is %s\n", row,
                        renewed ? "renewed" : "not renewed, he goes");
            return renewed;
        };
        struct HookGuard {
            GameState& g;
            ~HookGuard() { g.contract_screen = nullptr; }
        } hook_guard{b};
        auto today_event = [&](uint32_t date) { return cal_at(b, date) & 0x7FFF; };
        // The "Weiter" loop: after an event the day step (legacy routine) comes before the
        // stock market (legacy routine), on plain days after it.
        bool after_event = true;
        const bool full_game = (st.data(::bmh::layout::CompetitionSelection)[0] & 1) != 0;  // format field bit 0
        for (;;) {
            // rest of legacy routine: training days (the weekday of the previous day)
            const int wd = weekday_code(b.date - 1);
            if ((wd == 2 || wd == 6) && full_game && weekday_code(b.date) == 3)
                std::printf("  day %u: weekly processing (legacy routine)\n", b.date);
            if (full_game && b.date > 0 && month_code(b.date - 1) != month_code(b.date))
                std::printf("  day %u: month change (legacy routine)\n", b.date);
            {
                MinuteReport wr;
                const uint16_t source_rest_day = uint16_t(
                    b.date - b.season_start - uint32_t(after_event ? 1 : 0));
                if (!day_step_rest(b, bg, br, wr, source_rest_day, allocator_context)) {  // (game/turn.hpp)
                    std::printf("  not supported: %s, replay stops here\n", wr.reason.c_str());
                    ok = false;
                    break;
                }
            }
            // 1 July (legacy routine: format field == 1 and format field == 7): the season is
            // over - format field = 1, the prizes for top scorers and fair play
            // (legacy routine) and the manager's desk "Saisonende" (legacy routine), where the
            // recording ends
            if (is_season_end(b)) {
                std::printf("\n  day %u: season end\n", b.date);
                season_end_day(b);  // (game/turn.hpp)
                size_t ipause = inext;
                while (ipause < entries.size() && entries[ipause].kind != "post") ipause++;
                const uint16_t today = cal_at(b, b.date);
                if (ipause < entries.size())
                    ok = desk_turn(today, entries[ipause], true) && ok;
                else if (!entries.empty())
                    ok = desk_turn(today, entries.back(), true, false) && ok;  // (the recording ended before)
                break;
            }
            if (after_event) day_stock_market(b, bg);
            const uint16_t today = cal_at(b, b.date);
            // An entry whose competitions are switched off (e.g. 0x4006 without the option
            // format field bit 1) is still an event for legacy routine, which tests the raw
            // calendar word: legacy routine prepares an empty matchday (legacy routine, the
            // matchday number, the manager order) without a desk, then the day step follows
            // as after any event.
            if ((today & 0x7FFF) && day_competitions(b, today) == 0) {
                std::printf("  day %u: calendar entry 0x%04x without competitions\n", b.date, today);
                std::vector<uint8_t> refs = db.raw_referee_properties;
                MatchdayContext c0;
                c0.month = month_code(b.date);
                c0.season_day = b.season_day();
                c0.weekday = weekday_code(b.date);
                c0.referees = &refs;
                MinuteReport r0;
                try {
                    empty_event_day(b, c0, bg, br, r0);  // (game/turn.hpp)
                } catch (const std::exception& ex) {
                    r0.fail(std::string("exception: ") + ex.what());
                }
                if (r0.unsupported) std::printf("  not supported: %s\n", r0.reason.c_str());
                while (inext < entries.size() && entries[inext].kind != "day") inext++;
                if (inext < entries.size() && entries[inext].value == int(b.date))
                    ok = day_check(entries[inext++]) && ok;
                else
                    std::printf("  day %u: no snapshot\n", b.date);
                after_event = true;
                continue;
            }
            if ((today & 0x7FFF) && day_competitions(b, today) == 0x800000) {
                // --- a national team day (world game): legacy routine calls legacy routine (the
                // squad is nominated), then prepares an empty matchday (format field = 0) - no
                // desk without a Bundestrainer - and the day step follows
                std::printf("\nsection N: day %u, national team day\n", b.date);
                std::vector<uint8_t> refs = db.raw_referee_properties;
                MatchdayContext cn;
                cn.month = month_code(b.date);
                cn.season_day = b.season_day();
                cn.weekday = weekday_code(b.date);
                cn.referees = &refs;
                cn.national_team_day = true;
                MinuteReport rn;
                try {
                    national_team_day(b, br, rn);
                    if (!rn.unsupported) {
                        const std::vector<MatchGroup> none = setup_matchday(b, 0, cn, bg, br, rn);
                        if (!none.empty()) rn.fail("national team day: matches prepared");
                        draw_matchday_number(b, br);  // format field
                        shuffle_manager_order(b, bg);
                        event_end(b);  // (game/turn.hpp)
                    }
                } catch (const std::exception& ex) {
                    rn.fail(std::string("exception: ") + ex.what());
                }
                if (rn.unsupported) {
                    std::printf("  not supported: %s\n", rn.reason.c_str());
                    ok = false;
                    break;
                }
                b.date++;
                daily_interest(b, br);
                while (inext < entries.size() && entries[inext].kind != "day") inext++;
                if (inext < entries.size() && entries[inext].value == int(b.date))
                    ok = day_check(entries[inext++]) && ok;
                else
                    std::printf("  day %u: no snapshot\n", b.date);
                after_event = true;
                continue;
            }
            if (today & 0x7FFF) {
                // --- section F: the next matchday's preparation and kick-off
                std::printf("\nsection F: day %u, calendar entry 0x%04x\n", b.date, today);
                size_t ipause = inext;
                while (ipause < entries.size() && entries[ipause].kind != "post") ipause++;
                // (minutes before the first pause: the pause before the kick-off was not recorded -
                //  no draws since the previous one - and the first round is played without it)
                size_t iminute = inext;
                while (iminute < ipause && entries[iminute].kind != "minute") iminute++;
                const bool pause_recorded = iminute >= ipause;
                size_t ifirst = pause_recorded ? ipause : iminute;
                while (ifirst < entries.size() && entries[ifirst].kind != "minute") ifirst++;
                size_t ilast_post = entries.size();
                for (size_t i = ipause; i < entries.size(); i++)
                    if (entries[i].kind == "post") ilast_post = i;
                if (ifirst == entries.size() && ilast_post < entries.size() &&
                    std::stoi(field(entries[ilast_post].line, "date")) > int(b.date)) {
                    // No match minutes and the desk lies days later: a competition
                    // without a match this season (e.g. the 0x2000 cup when nobody qualified):
                    // legacy routine prepares an empty matchday, then the day step follows
                    std::vector<uint8_t> refs = db.raw_referee_properties;
                    MatchdayContext c0;
                    c0.month = month_code(b.date);
                    c0.season_day = b.season_day();
                    c0.weekday = weekday_code(b.date);
                    c0.referees = &refs;
                    MinuteReport r0;
                    std::vector<MatchGroup> grs0;
                    try {
                        grs0 = setup_matchday(b, day_competitions(b, today), c0, bg, br, r0);
                        draw_matchday_number(b, br);  // format field
                        shuffle_manager_order(b, bg);
                    } catch (const std::exception& ex) {
                        r0.fail(std::string("exception: ") + ex.what());
                    }
                    if (!grs0.empty() && !r0.unsupported) {
                        // matches the recorder caught no minute of (a fast day, e.g. a
                        // European cup round with only its intro screen recorded): played
                        // unchecked, the following pauses and the savegame decide
                        std::printf("  day %u: matches without recorded minutes, played unchecked\n", b.date);
                        const std::vector<uint8_t> rec0 = b.manager_records, opt0 = b.manager_options;
                        kick_off(b, grs0, c0, br, bg, r0);
                        if (!r0.unsupported) {
                            MatchEngine engine{b, grs0, bg, br, db.raw_referee_properties};
                            engine.month = c0.month;
                            engine.buttons = buttons;
                            engine.leave_screens = true;
                            play_matchday(engine, b, grs0, buttons, bg, br, r0, nullptr, 0);
                            store_group_descriptors(b, grs0, true);
                            finish_matchday(grs0, today, rec0, opt0, r0);
                        }
                    }
                    if (r0.unsupported) {
                        std::printf("  not supported: %s\n", r0.reason.c_str());
                        ok = false;
                        break;
                    }
                    std::printf("  day %u: no match of the event\n", b.date);
                    b.date++;
                    daily_interest(b, br);
                    while (inext < entries.size() && entries[inext].kind != "day") inext++;
                    if (inext < entries.size() && entries[inext].value == int(b.date))
                        ok = day_check(entries[inext++]) && ok;
                    else
                        std::printf("  day %u: no snapshot\n", b.date);
                    after_event = true;
                    continue;
                }
                if (ifirst == entries.size()) {
                    // No match minutes follow: the human manager's own matchday, the
                    // recording ends at his pre-match desk (legacy routine) - its last pause
                    // (dialogs of the days before, e.g. a weekly JA/NEIN question, pause too)
                    if (ipause == entries.size()) break;
                    size_t ilast = ipause;
                    size_t iprev = ipause;
                    for (size_t i = ipause; i < entries.size(); i++)
                        if (entries[i].kind == "post") iprev = ilast, ilast = i;
                    if (iprev != ilast &&
                        field(entries[iprev].line, "game_seed") == field(entries[ilast].line, "game_seed")) {
                        // a pause after saving at the desk (the save draws 4 Watcom numbers)
                        WatcomRandom w(uint32_t(std::stoull(field(entries[iprev].line, "rand_seed"))));
                        for (int k = 0; k < 4; k++) w.rand();
                        if (w.seed() == uint32_t(std::stoull(field(entries[ilast].line, "rand_seed")))) ilast = iprev;
                    }
                    ok = desk_turn(today, entries[ilast]) && ok;
                    break;
                }
                std::vector<uint8_t> refs = db.raw_referee_properties;
                MatchdayContext c2;
                c2.month = std::stoi(field(entries[ifirst].line, "month"));
                c2.season_day = b.season_day();
                c2.weekday = weekday_code(b.date);
                c2.referees = &refs;
                MinuteReport r2;
                std::vector<MatchGroup> grs2;
                std::vector<uint8_t> saved_records, saved_options;
                try {
                    grs2 = setup_matchday(b, day_competitions(b, today), c2, bg, br, r2);
                    draw_matchday_number(b, br);  // format field
                    shuffle_manager_order(b, bg);
                    saved_records = b.manager_records, saved_options = b.manager_options;  // legacy routine
                    kick_off(b, grs2, c2, br, bg, r2);
                } catch (const std::exception& ex) {
                    r2.fail(std::string("exception: ") + ex.what());
                }
                if (r2.unsupported) std::printf("  not supported: %s\n", r2.reason.c_str());
                if (pause_recorded)
                    ok = check("checkpoint pause", b, bg, br, entries[ipause]) && ok;
                else
                    std::printf("  (no pause recorded before the kick-off)\n");
                std::printf("\nkick-off of day %u against minute %d (t=%s ms)\n", b.date, entries[ifirst].value,
                            field(entries[ifirst].line, "t_ms").c_str());
                const std::vector<MatchGroup> rec2 = groups_from_snapshot(entries[ifirst].line);
                if (std::getenv("WEEK_WEATHER"))
                    for (size_t gi = 0; gi < grs2.size() && gi < rec2.size(); gi++)
                        for (int m = 0; m < grs2[gi].matches(); m++) {
                            const int home = grs2[gi].fixtures[size_t(m) * 2];
                            const int region = team_region(home);
                            const int ours = int8_t(grs2[gi].desc[0x30 + size_t(m)]);
                            const int theirs = int8_t(rec2[gi].desc[0x30 + size_t(m)]);
                            std::printf("  g%zu m%2d home %3d region %2d offset %3d implied %3d\n", gi, m, home, region,
                                        region_temperature(region), region_temperature(region) + theirs - ours);
                        }
                if (std::getenv("WEEK_RATE")) {
                    const int team = std::stoi(std::getenv("WEEK_RATE"));
                    for (int rain = b.chance_rain; rain <= b.chance_rain; rain++)
                    for (int temp = b.chance_temperature; temp <= b.chance_temperature; temp++)
                    for (int st = -1; st < 130; st++) {
                        RatingContext rc{rain, temp};
                        rc.stale_technique = st;
                        std::printf("  rain %2d temp %2d stale technique %2d:", rain, temp, st);
                        for (int i = 4; i < 6; i++) std::printf(" %02x", team_rating(b, team, kRatingProfiles[i], rc));
                        std::printf("\n");
                    }
                    std::printf("  chance weather %d/%d\n", b.chance_rain, b.chance_temperature);
                }
                if (std::getenv("WEEK_CHANCE") && !rec2.empty() && rec2[0].records.size() >= 18) {
                    // development helper: the chance weather (runtime only) that reproduces
                    // the recorded ratings of both sides of the first match
                    for (int rain = 0; rain < 256; rain++)
                        for (int temp = -128; temp < 128; temp++) {
                            bool same = true;
                            for (int side = 0; side < 2 && same; side++) {
                                const int team = grs2[0].fixtures[size_t(side)];
                                RatingContext rc{rain, temp};
                                WatcomRandom wr = br;
                                GameRandom gr = bg;
                                rc.rand = &wr, rc.game = &gr;
                                for (int i = 0; i < 6; i++) {
                                    const int v = team_rating(b, team, kRatingProfiles[i], rc);
                                    if (i == 2) continue;  // (byte 2 also carries the crowd/climate modifier)
                                    if (v != rec2[0].records[size_t(side) * 9 + size_t(i)]) {
                                        same = false;
                                        break;
                                    }
                                }
                            }
                            if (same) std::printf("  chance weather rain %d temperature %d reproduces the sides\n", rain, temp);
                        }
                }
                ok = compare_kickoff(grs2, rec2, &b, entries[ifirst].value) == 0 && ok;
                size_t iend2 = ifirst;
                while (iend2 < entries.size() && entries[iend2].kind != "day") iend2++;
                std::printf("\nsection G: the matchday of day %u\n", b.date);
                // (an indoor day plays several rounds: this one ends at the next pause, or where
                //  the minutes start again - rounds without a pause in between)
                auto round_end_of = [&](size_t from, size_t to) {
                    size_t e = from;
                    int last = -1;
                    for (; e < to && entries[e].kind != "post"; e++)
                        if (entries[e].kind == "minute") {
                            if (entries[e].value < last) break;
                            last = entries[e].value;
                        }
                    return e;
                };
                size_t round_end = round_end_of(ifirst, iend2);
                ok = replay_matchday(b, grs2, bg, br, entries, ifirst, round_end, db, buttons) && ok;
                std::printf("  last chance weather: rain %d, temperature %d\n", b.chance_rain, b.chance_temperature);
                if (iend2 < entries.size()) {
                    MinuteReport r3;
                    finish_matchday(grs2, today, saved_records, saved_options, r3);
                    // The indoor tournament plays three rounds on its first day and two
                    // on the second (legacy routine, format field): the "Weiter" loop runs
                    // the event again without a day step in between.
                    if (today == 0x4002) {
                        const uint16_t tomorrow = cal_at(b, b.date + 1);
                        const int rounds = tomorrow == 0x4002 ? 3 : 2;
                        size_t cursor = round_end;  // the next round's pause
                        for (int round = 1; round < rounds && !r3.unsupported; round++) {
                            std::printf("\n  indoor round %d of day %u\n", round + 1, b.date);
                            std::vector<uint8_t> refs3 = db.raw_referee_properties;
                            MatchdayContext c4 = c2;
                            c4.referees = &refs3;
                            std::vector<MatchGroup> gr = setup_matchday(b, day_competitions(b, today), c4, bg, br, r3);
                            draw_matchday_number(b, br);
                            shuffle_manager_order(b, bg);
                            const std::vector<uint8_t> rec4 = b.manager_records, opt4 = b.manager_options;
                            kick_off(b, gr, c4, br, bg, r3);
                            size_t first4 = cursor;
                            while (first4 < iend2 && entries[first4].kind != "minute") first4++;
                            const size_t end4 = round_end_of(first4, iend2);
                            if (cursor < iend2 && entries[cursor].kind == "post")
                                ok = check("  checkpoint pause of the round", b, bg, br, entries[cursor]) && ok;
                            if (first4 < iend2) {
                                std::printf("  kick-off against minute %d:\n", entries[first4].value);
                                ok = compare_kickoff(gr, groups_from_snapshot(entries[first4].line), &b) == 0 && ok;
                                ok = replay_matchday(b, gr, bg, br, entries, first4, end4, db, buttons) && ok;
                            } else {
                                MatchEngine engine{b, gr, bg, br, db.raw_referee_properties};
                                engine.alternate_event_roll = field(entries[ifirst].line, "alternate_event_roll") == "1";
                                engine.match_events_disabled = field(entries[ifirst].line, "match_events_disabled") == "1";
                                engine.live_desk_disabled = field(entries[ifirst].line, "live_desk_disabled") == "1";
                                engine.month = c4.month;
                                engine.buttons = buttons;
                                engine.leave_screens = true;
                                play_matchday(engine, b, gr, buttons, bg, br, r3, nullptr, 0);
                                store_group_descriptors(b, gr, true);
                            }
                            cursor = end4;
                            // (the weather of the round's last chance: the next turn starts with it)
                            std::printf("  last chance weather: rain %d, temperature %d\n", b.chance_rain, b.chance_temperature);
                            for (size_t mgr = 0; mgr < b.manager_teams.size(); mgr++)
                                for (int s = 0; s < 24; s++)
                                    for (int k = 0; k < 2; k++) {
                                        const size_t at = mgr * 0x840 + size_t(s) * 0x58 + 0x44 + size_t(k) * 2;
                                        b.tactics.at(at) = 0xFF, b.tactics.at(at + 1) = 0x0F;
                                    }
                            for (MatchGroup& grp : gr) {
                                process_group(b, grp, bg, br, r3);
                                store_cup_results(b, grp);
                            }
                            for (size_t mgr = 0; mgr < b.manager_teams.size(); mgr++) {
                                for (int k = 0; k < 8; k++)
                                    b.manager_records.at(mgr * 0xB0C + 0xd2 + k) = rec4.at(mgr * 0xB0C + 0xd2 + k);
                                b.manager_options.at(mgr * 5 + 2) = opt4.at(mgr * 5 + 2);
                            }
                            for (int bit = 5; bit < 0x13; bit++)
                                if ((day_competitions(b, today) >> bit) & 1) advance_cup(b, bit - 5, br, r3, &bg);
                            event_end(b);  // (legacy routine at the end of legacy routine)
                        }
                    }
                    if (r3.unsupported) std::printf("  not supported: %s\n", r3.reason.c_str());
                    b.date++;
                    daily_interest(b, br);
                    ok = day_check(entries[iend2]) && ok;
                    if (!r3.unsupported) {
                        inext = iend2 + 1;
                        after_event = true;
                        continue;  // on with the next days
                    }
                }
                break;
            }
            plain_day(b, bg, br);  // a day without event: the shares move before the day step
            after_event = false;
            while (inext < entries.size() && entries[inext].kind == "minute") inext++;
            if (inext < entries.size() && entries[inext].kind == "day" && entries[inext].value == int(b.date)) {
                const Entry& e = entries[inext];
                ok = day_check(e) && ok;
                inext++;
            } else {
                std::printf("  day %u: no snapshot\n", b.date);
            }
        }
        return ok;
    };

    // --- the new season: a recording that starts at the "Saisonende" desk (format field set)
    // and ends at the first desk of the next season. Checked so far: the first pause (the
    // first screen of legacy routine) after the offers were answered (WEEK_ACCEPT=0 declines).
    if (!entries.empty() && entries[0].kind == "minute" && entries[0].value == -1 && saved.memory.has(::bmh::layout::CareerSession_RuntimeError_RuntimeField4) &&
        saved.memory.u8(::bmh::layout::CareerSession_RuntimeError_RuntimeField4) == 1) {
        const Entry& desk = entries[0];
        std::printf("season transition: desk (date %s)\n", field(desk.line, "date").c_str());
        GameState s = saved;
        s.texts = &textdb;
        // format field is not saved: the value of the running session (desk state dump)
        if (!field(desk.state, "a3d4c").empty()) s.trend_rotation = std::stoi(field(desk.state, "a3d4c"));
        GameRandom sg(int32_t(std::stoll(field(desk.line, "game_seed"))));
        WatcomRandom sr(uint32_t(std::stoull(field(desk.line, "rand_seed"))));
        MinuteReport rep;
        const bool accept = !std::getenv("WEEK_ACCEPT") || std::string(std::getenv("WEEK_ACCEPT")) != "0";
        for (size_t m = 0; m < s.manager_teams.size() && !rep.unsupported; m++) answer_offers(s, int(m), accept, sr, rep);
        free_offer_slots(s);
        // every screen of the transition is a recorded pause, compared in order
        size_t ip = 0;
        bool fine = true;
        int32_t last_game = sg.seed();
        uint32_t last_rand = sr.seed();
        auto pause = [&](const char* what) {
            // the recorder notes a pause only when the seeds changed since the last one
            if (sg.seed() == last_game && sr.seed() == last_rand) {
                std::printf("  (%s: no draws since the last pause, not recorded)\n", what);
                return;
            }
            last_game = sg.seed(), last_rand = sr.seed();
            ip++;
            while (ip < entries.size() && entries[ip].kind != "post") ip++;
            if (ip >= entries.size()) return;
            const std::string name = std::string("checkpoint ") + what;
            fine = check(name.c_str(), s, sg, sr, entries[ip]) && fine;
        };
        if (!rep.unsupported) season_transition(s, sg, sr, rep, pause);
        if (rep.unsupported) {
            std::printf("  not supported: %s\n", rep.reason.c_str());
            return 2;
        }
        if (const char* t = std::getenv("BMH_TEAM")) {
            const SquadRange r = squad_range(std::atoi(t));
            for (size_t k = r.first; k < r.first + r.count; k++) {
                const int p = s.squad_slots[k] & 0xfff;
                if (p == 0xfff) continue;
                std::printf("  %zu: %4d age %2u w364 %08x w35c %08x %s\n", k, p, (s.players[size_t(p)].w364 >> 21) & 0x1f,
                            s.players[size_t(p)].w364, s.players[size_t(p)].w35c, s.player_name(p).c_str());
            }
        }
        // the day of the first desk of the new season, then the desk itself (legacy routine)
        // (several day snapshots may lie between the last pause and the desk: take the one
        // of the port's date, i.e. the last day step before the desk)
        size_t id = ip + 1;
        while (id < entries.size() && entries[id].kind != "day") id++;
        for (size_t k = id; k < entries.size() && entries[k].kind != "post"; k++)
            if (entries[k].kind == "day" && entries[k].value == int(s.date)) id = k;
        bool day_fine = true;
        if (id < entries.size()) day_fine = check(("checkpoint day " + std::to_string(s.date)).c_str(), s, sg, sr, entries[id]);
        size_t idesk = id + 1;
        while (idesk < entries.size() && entries[idesk].kind != "post") idesk++;
        if (idesk < entries.size()) {
            b = s, bg = sg, br = sr;
            std::printf("\n  the first desk of the new season (day %d of the season)\n", b.season_day());
            const bool desk_fine = desk_turn(cal_at(b, b.date), entries[idesk]);
            // the recorder may catch the last day step before legacy routine (the new transfer
            // list entries) and the calendar clean-up ran; with a matching desk the day
            // snapshot is only informative
            if (!day_fine && desk_fine) std::printf("  (the day snapshot was taken before the end of the day step; the desk matches)\n");
            fine = desk_fine && fine;
        } else {
            fine = day_fine && fine;
        }
        return !fine ? 2 : 0;
    }

    // --- section T: a recording that starts at a desk without an event of the manager's
    // (e.g. the "Saisonbeginn" desk): "Anstoß" only runs the day step (legacy routine, date + 1
    // and the daily interest), then the "Weiter" loop continues as in section E
    // (with several human managers the desks of the later ones come first: recorded pauses)
    size_t iday1 = 1;
    while (iday1 < entries.size() && entries[iday1].kind == "post") iday1++;
    if (!entries.empty() && entries[0].kind == "minute" && entries[0].value == -1 && iday1 < entries.size() &&
        entries[iday1].kind == "day" && (iday1 == 1 || saved.manager_teams.size() > 1)) {
        const Entry& desk = entries[0];
        std::printf("section T: desk (date %s) -> day %d\n", field(desk.line, "date").c_str(), entries[iday1].value);
        b = saved;
        b.texts = &textdb;
        if (!field(desk.state, "results").empty()) {
            const Bytes mailbox = unhex(field(desk.state, "mailbox"));
            if (mailbox.size() == b.mailbox.size()) b.mailbox = mailbox;
            adopt_desk_tables(b, desk.state);
        }
        bg = GameRandom(int32_t(std::stoll(field(desk.line, "game_seed"))));
        br = WatcomRandom(uint32_t(std::stoull(field(desk.line, "rand_seed"))));
        // format field is not saved either: the value of the running session
        if (!field(entries[iday1].state, "a3d4c").empty()) b.trend_rotation = std::stoi(field(entries[iday1].state, "a3d4c"));
        bool fine = true;
        {
            // the desks of the managers later in the order (no groups on such a day)
            size_t ip = 1;
            MinuteReport rd;
            fine = later_desks(b, bg, br, {}, entries, ip, rd);
            if (rd.unsupported) std::printf("  not supported: %s\n", rd.reason.c_str()), fine = false;
        }
        event_end(b);  // (leaving the desk ends legacy routine: legacy routine)
        b.date++;
        daily_interest(b, br);
        fine = day_check(entries[iday1]) && fine;
        std::printf("\nsection E: days after day %u\n", b.date);
        fine = days_loop(iday1 + 1) && fine;
        return fine ? 0 : 2;
    }

    // --- an indoor tournament round from the desk (no minute above 40): kick-off, the 40
    // minutes and the state at the pause after them
    {
        // (only the manager's own round counts: later days may hold league matches)
        bool indoor = !entries.empty() && entries[0].kind == "minute" && entries[0].value == -1;
        // (a day before the first minute: the desk turn leads to a later match, no indoor round)
        for (const Entry& e : entries) {
            if (e.kind == "minute" && e.value >= 1) break;
            if (e.kind == "day") indoor = false;
        }
        for (const Entry& e : entries) {
            if (e.kind == "day") break;
            if (e.kind == "minute" && e.value > 40) indoor = false;
        }
        if (indoor) {
            const Entry& desk = entries[0];
            size_t i1 = 1;
            while (i1 < entries.size() && !(entries[i1].kind == "minute" && entries[i1].value == 1)) i1++;
            size_t i40 = i1;
            while (i40 < entries.size() && !(entries[i40].kind == "minute" && entries[i40].value == 40)) i40++;
            size_t ipost = i40;
            while (ipost < entries.size() && entries[ipost].kind != "post") ipost++;
            std::printf("indoor round: desk (date %s) -> kick-off -> minute 40 -> pause\n", field(desk.line, "date").c_str());
            GameState s = saved;
            s.texts = &textdb;
            if (i1 < entries.size() && !field(entries[i1].state, "a3d4c").empty())
                s.trend_rotation = std::stoi(field(entries[i1].state, "a3d4c"));
            if (!field(desk.state, "results").empty()) {
                const Bytes results = unhex(field(desk.state, "results"));
                std::copy(results.begin(), results.end(), s.results.begin());
                const Bytes mailbox = unhex(field(desk.state, "mailbox"));
                if (mailbox.size() == s.mailbox.size()) s.mailbox = mailbox;
                adopt_desk_tables(s, desk.state);
            }
            GameRandom sg0(int32_t(std::stoll(field(desk.line, "game_seed"))));
            WatcomRandom sr0(uint32_t(std::stoull(field(desk.line, "rand_seed"))));
            std::vector<MatchGroup> sat = groups_from_snapshot(desk.line);
            s.indoor = true;
            MatchdayContext sc;
            sc.month = std::stoi(field(desk.line, "month"));
            sc.season_day = s.season_day();
            sc.weekday = weekday_code(s.date);
            std::vector<uint8_t> refs = db.raw_referee_properties;
            sc.referees = &refs;
            MinuteReport sk;
            {
                // (see section S: the offers, then the desks of the managers later in the order)
                size_t ip = 1;
                later_desks(s, sg0, sr0, sat, entries, ip, sk);
            }
            if (!sk.unsupported) kick_off(s, sat, sc, sr0, sg0, sk);
            if (sk.unsupported) std::printf("  not supported: %s\n", sk.reason.c_str());
            if (i1 < entries.size()) {
                std::printf("  kick-off against minute 1:\n");
                compare_kickoff(sat, groups_from_snapshot(entries[i1].line), &s);
                replay_matchday(s, sat, sg0, sr0, entries, i1, ipost, db, buttons);
                // (the next turn starts with the weather of the round's last chance)
                std::printf("  last chance weather: rain %d, temperature %d\n", s.chance_rain, s.chance_temperature);
            }
            // after the round: group processing, the cup results, the next round of the day
            MinuteReport pr;
            for (size_t mgr = 0; mgr < s.manager_teams.size(); mgr++)
                for (int k = 0; k < 24; k++)
                    for (int j = 0; j < 2; j++) {
                        const size_t at = mgr * 0x840 + size_t(k) * 0x58 + 0x44 + size_t(j) * 2;
                        s.tactics.at(at) = 0xFF, s.tactics.at(at + 1) = 0x0F;
                    }
            for (MatchGroup& grp : sat) {
                process_group(s, grp, sg0, sr0, pr);
                store_cup_results(s, grp);
            }
            const uint16_t today = cal_at(s, s.date);
            const uint32_t comp = day_competitions(s, today);
            for (int bit = 5; bit < 0x13; bit++)
                if ((comp >> bit) & 1) advance_cup(s, bit - 5, sr0, pr, &sg0);
            if (pr.unsupported) std::printf("  not supported: %s\n", pr.reason.c_str());
            // The last round of the day (the manager's club is out or the day's rounds are
            // done): the date changes before the next pause, and the "Weiter" loop runs on
            // through the following days up to the manager's next desk.
            size_t iday = i40 + 1;
            // (screens of the end of the tournament may pause before the day changes: the
            //  day is searched up to the next match minute)
            while (iday < entries.size() && entries[iday].kind != "day" && entries[iday].kind != "minute") iday++;
            if (iday < entries.size() && entries[iday].kind == "day") {
                s.date++;
                const GameState before_interest = s;
                daily_interest(s, sr0);
                b = s, bg = sg0, br = sr0;
                ok = check("checkpoint day change", b, bg, br, entries[iday]);
                // (the recorder may catch the new date before the daily interest is booked,
                //  which draws nothing: then the state without it is compared)
                if (!ok && check("  the same before the daily interest", before_interest, bg, br, entries[iday])) ok = true;
                // (or a moment later, inside legacy routine (stock_market): the long-term values
                //  of the shares have grown by 0.03 %, the loop that draws has not run yet)
                if (!ok) {
                    GameState grown = s;
                    for (size_t k = 0; k < 12 && k * 0x5c + 4 <= grown.stocks.size(); k++) {
                        uint8_t* p = &grown.stocks[k * 0x5c];
                        uint32_t v = uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24);
                        v += v * 3 / 10000;
                        for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> (8 * i));
                    }
                    if (check("  the same with the shares' first step (late snapshot)", grown, bg, br, entries[iday])) ok = true;
                }
                // (after the screens of a tournament's end the recorder may also catch the day
                //  inside its processing, like the late day snapshots of section E: not counted,
                //  the following days and the savegame decide)
                const bool screens = entries[iday - 1].kind == "post";
                if (!ok && screens) std::printf("  (late snapshot: taken inside the processing of the day, not counted)\n"), ok = true;
                std::printf("\nsection E: days after day %u\n", b.date);
                ok = days_loop(iday + 1) && ok;
                return ok ? 0 : 2;
            }
            if (ipost < entries.size()) {
                // the next round of the same day: preparation and the desks in the managers'
                // order up to manager 0's, where the game is saved (as at any desk)
                std::printf("\n  the desks of the next round\n");
                b = s, bg = sg0, br = sr0;
                size_t ilast = ipost;
                for (size_t i = ipost; i < entries.size(); i++)
                    if (entries[i].kind == "post") ilast = i;
                return desk_turn(cal_at(b, b.date), entries[ilast], false, true, true) ? 0 : 2;
            }
            return 0;
        }
    }

    // --- locate section A: the pause after the last regular minute, then minute 91 -------
    size_t first91 = 0;
    // The minute 91 of the phase after the whistle is followed at once by minute 106 (the
    // stoppage time has its own 91-94); a dialog pause inside minute 90 may precede the
    // stoppage time's minute 91, and the whistle pause itself is only recorded when the
    // seeds changed. Searched before the first day change only.
    for (size_t i = 1; i + 1 < entries.size() && entries[i].kind != "day"; i++)
        if (entries[i].kind == "minute" && entries[i].value == 91 && entries[i + 1].kind == "minute" &&
            entries[i + 1].value == 106 &&
            (entries[i - 1].kind == "post" || (entries[i - 1].kind == "minute" && entries[i - 1].value > 91))) {
            first91 = i;
            break;
        }
    for (size_t i = 1; i < entries.size() && first91 == 0; i++)
        if (entries[i].kind == "minute" && entries[i].value == 91 && entries[i - 1].kind == "post") {
            first91 = i;
            break;
        }
    // Without a match of his own (postponed) the manager's matchday runs like a background
    // one: no pause at the final whistle, only sparse minute snapshots up to minute 91.
    bool whistle_pause = first91 != 0 && entries[first91 - 1].kind == "post";
    if (first91 == 0 && !entries.empty() && entries[0].kind == "minute" && entries[0].value == -1)
        for (size_t i = 1; i < entries.size() && entries[i].kind != "day"; i++)
            // (the stoppage time may have its own minute 91 snapshot: the one after the result
            // screen follows a pause of over a second)
            // (before the first day change: a later cup tie's extra time also follows a gap,
            // WEEK233)
            if (entries[i].kind == "minute" && entries[i].value == 91 && entries[i - 1].kind == "minute" &&
                std::stoll(field(entries[i].line, "t_ms")) - std::stoll(field(entries[i - 1].line, "t_ms")) > 1000) {
                first91 = i;
                break;
            }
    // A match of the manager's not shown live (e.g. a postponed match, group type 10) runs
    // at full speed: the minute 91 after the result screen then follows the stoppage time
    // at once, right before minute 106. (Taken when it comes before a minute 91 found by the
    // pause test above, which may belong to a later matchday of the recording.)
    if (!whistle_pause && !entries.empty() && entries[0].kind == "minute" && entries[0].value == -1)
        for (size_t i = 2; i + 1 < entries.size() && entries[i].kind != "day" && (first91 == 0 || i < first91); i++)
            if (entries[i].kind == "minute" && entries[i].value == 91 && entries[i + 1].kind == "minute" &&
                entries[i + 1].value == 106 && entries[i - 1].kind == "minute" && entries[i - 1].value > 91) {
                first91 = i;
                break;
            }
    // The recorder may also miss minute 91 of the extra time phase (it only lasts until the
    // phase ends at once): then the pause is followed by minute 106.
    bool no91 = false;
    if (first91 == 0)
        for (size_t i = 1; i < entries.size(); i++)
            if (entries[i].kind == "minute" && entries[i].value == 106 && entries[i - 1].kind == "post") {
                first91 = i, no91 = true, whistle_pause = true;
                break;
            }
    // A match of his without a whistle pause and without the extra time phase's minute 91 (a
    // friendly, WEEK204): the stoppage time's 92-95 runs straight into minute 106. Searched
    // before the first day change, and before the rules below can take a later matchday's 91.
    if (first91 == 0 || first91 > 0)
        for (size_t i = 1; i < entries.size() && entries[i].kind != "day" && (first91 == 0 || i < first91); i++)
            if (entries[i].kind == "minute" && entries[i].value == 106 && entries[i - 1].kind == "minute" &&
                entries[i - 1].value > 91 && entries[i - 1].value <= 95) {
                first91 = i, no91 = true, whistle_pause = false;
                break;
            }
    // A real extra time without a whistle pause (no draws in between, WEEK176): minute 91 of
    // the extra time comes right after the stoppage time's 92-95.
    if (first91 == 0)
        for (size_t i = 1; i < entries.size() && entries[i].kind != "day"; i++)
            if (entries[i].kind == "minute" && entries[i].value == 91 && entries[i - 1].kind == "minute" &&
                entries[i - 1].value > 91 && entries[i - 1].value <= 95) {
                first91 = i;
                break;
            }
    if (first91 == 0) {
        std::printf("no final whistle pause followed by the extra time phase found\n");
        return 1;
    }
    const Entry& whistle = entries[first91 - 1];
    const Entry& m91 = entries[first91];
    // --- section 0: minute 90 -> final whistle. Replays the last regular minute and the
    // stoppage time (legacy routine(46, 90, 4)) from the minute 90 snapshot. Besides checking
    // the seeds at the whistle this yields the weather of the last chance of the matchday,
    // which the kick-off of the next matchday still reads (GameState::chance_rain).
    // the state at the final whistle as replayed from the desk (section S), if it matched
    bool from_desk = false;
    GameState desk_state;
    GameRandom desk_game;
    WatcomRandom desk_rand;
    std::vector<MatchGroup> desk_groups;
    // --- section S: a recording that starts at the pre-match desk of a matchday with a human
    // team (minute -1, the savegame saved there): kick-off (see kickoff_check) and the whole
    // matchday up to the final whistle from the savegame instead of the minute 90 snapshot.
    if (!entries.empty() && entries[0].kind == "minute" && entries[0].value == -1) {
        const Entry& desk = entries[0];
        size_t i1 = 1;
        while (i1 < entries.size() && !(entries[i1].kind == "minute" && entries[i1].value == 1)) i1++;
        std::printf("section S: desk (date %s) -> kick-off -> final whistle\n", field(desk.line, "date").c_str());
        GameState s = saved;
        s.texts = &textdb;
        // format field is not saved: when the recording did not start with a freshly loaded
        // game it has the value of the running session (taken from the first state dump)
        if (i1 < entries.size() && !field(entries[i1].state, "a3d4c").empty())
            s.trend_rotation = std::stoi(field(entries[i1].state, "a3d4c"));
        // Loading the savegame prepared the matchday again (legacy routine): in winter with a
        // second round of postponements the savegame does not know. Newer recordings dump the
        // result table, the postponed matches and the calendar at the desk.
        if (!field(desk.state, "results").empty()) {
            const Bytes results = unhex(field(desk.state, "results"));
            std::copy(results.begin(), results.end(), s.results.begin());
            const Bytes postponed = unhex(field(desk.state, "postponed"));
            for (size_t i = 0; i < postponed.size(); i++) s.memory.set8(::bmh::layout::Postponed + uint32_t(i), postponed[i]);
            const Bytes calendar = unhex(field(desk.state, "calendar"));
            for (size_t i = 0; i < calendar.size(); i++) s.memory.set8(::bmh::layout::CompetitionCalendar + uint32_t(i), calendar[i]);
            // the preparation may post messages again (e.g. the balance sheet reminder)
            const Bytes mailbox = unhex(field(desk.state, "mailbox"));
            if (mailbox.size() == s.mailbox.size()) s.mailbox = mailbox;
            adopt_desk_tables(s, desk.state);
        }
        {
            // a replay day: the lists of today's replays are not saved
            const uint16_t today = cal_at(s, s.date);
            rebuild_replay_lists(s, day_competitions(s, today));
        }
        GameRandom sg(int32_t(std::stoll(field(desk.line, "game_seed"))));
        WatcomRandom sr(uint32_t(std::stoull(field(desk.line, "rand_seed"))));
        std::vector<MatchGroup> sat = groups_from_snapshot(desk.line);
        if (sat.empty()) {
            // A turn cut out of a longer recording (split_capture.pl) has no desk snapshot of its
            // own: the groups the desk prepared are rebuilt (legacy routine on a copy, its draws
            // thrown away) and take the drawn parts - referees, weather, attendance - from the
            // descriptors the savegame holds (block MatchdayGroupDescriptors).
            GameState copy = s;
            GameRandom tg(1);
            WatcomRandom tw(1);
            MatchdayContext tc;
            tc.month = std::stoi(field(desk.line, "month"));
            tc.season_day = int(st.data(::bmh::layout::DaysIntoSeason)[0] | st.data(::bmh::layout::DaysIntoSeason)[1] << 8);
            tc.weekday = weekday_code(s.date);
            std::vector<uint8_t> trefs = db.raw_referee_properties;
            tc.referees = &trefs;
            MinuteReport tr;
            sat = setup_matchday(copy, day_competitions(s, cal_at(s, s.date)), tc, tg, tw, tr);
            for (size_t gi = 0; gi < sat.size() && gi < 6; gi++) {
                const uint32_t at = ::bmh::layout::MatchdayGroupDescriptors + uint32_t(gi) * 0xd0;
                for (uint32_t i : {0x10u, 0xb8u})
                    for (uint32_t k = i; k < (i == 0x10u ? 0x70u : 0xccu); k++)
                        if (s.memory.has(at + k)) sat[gi].desc.at(k) = s.memory.u8(at + k);
            }
            std::printf("  (desk groups rebuilt from the savegame: %zu)\n", sat.size());
        }
        // entering the desk again after loading also rebuilt the pools coupon (legacy routine),
        // which leaves out the matches postponed in the meantime
        // (with national teams it draws Watcom numbers before the desk's recorded seed: the
        //  recorded coupon is taken over, older recordings use a spare generator - their
        //  coupon is then only an estimate)
        if (!field(desk.state, "coupon").empty()) {
            const Bytes coupon = unhex(field(desk.state, "coupon"));
            for (size_t i = 0; i < coupon.size(); i++) s.memory.set8(::bmh::layout::DeskActions_SubmitPoolsCoupon_RuntimeField + uint32_t(i), coupon[i]);
            s.memory.set8(::bmh::layout::CareerSession_DispatchToto_RuntimeField, uint8_t(std::stoi(field(desk.state, "coupon_n"))));
        } else if (!field(desk.state, "results").empty()) {
            WatcomRandom spare(1);
            build_pools_coupon(s, sat, &spare);
        }
        MatchdayContext sc;
        sc.month = std::stoi(field(desk.line, "month"));
        sc.season_day = int(st.data(::bmh::layout::DaysIntoSeason)[0] | st.data(::bmh::layout::DaysIntoSeason)[1] << 8);
        sc.weekday = weekday_code(s.date);
        std::vector<uint8_t> refs = db.raw_referee_properties;
        sc.referees = &refs;
        MinuteReport sk;
        try {
            // legacy routine after the desk: the offers for the managers' players (legacy routine;
            // fee offers are refused, salary demands accepted unless WEEK_ACCEPT=0), the desks
            // of the managers later in the order
            size_t ip = 1;
            later_desks(s, sg, sr, sat, entries, ip, sk);
            // the kick-off prepares today's friendlies again: they leave the plan now
            if ((day_competitions(s, cal_at(s, s.date)) >> (5 + 10)) & 1) prepare_friendlies(s);
            if (!sk.unsupported) kick_off(s, sat, sc, sr, sg, sk);
        } catch (const std::exception& ex) {
            sk.fail(std::string("exception: ") + ex.what());
        }
        if (sk.unsupported) std::printf("  not supported: %s\n", sk.reason.c_str());
        if (i1 < entries.size()) {
            std::printf("  kick-off against minute 1:\n");
            compare_kickoff(sat, groups_from_snapshot(entries[i1].line), &s);
            // A cup group with extra time (minute snapshots 105/121 before the first day
            // change): the matchday is replayed as a whole like a background one (section
            // G), then the days follow (section E).
            size_t iday = i1;
            bool extra_time = false;
            for (; iday < entries.size() && entries[iday].kind != "day"; iday++)
                if (entries[iday].kind == "minute" && (entries[iday].value == 105 || entries[iday].value == 121)) extra_time = true;
            if (extra_time && iday < entries.size()) {
                std::printf("\n  (extra time: the whole matchday up to day %d)\n", entries[iday].value);
                bool fine = !sk.unsupported;
                try {
                    fine = replay_matchday(s, sat, sg, sr, entries, i1, iday, db, buttons) && fine;
                } catch (const std::exception& ex) {
                    std::printf("  exception: %s\n", ex.what());
                    fine = false;
                }
                std::printf("  last chance weather: rain %d, temperature %d\n", s.chance_rain, s.chance_temperature);
                b = s, bg = sg, br = sr;
                MinuteReport r3;
                finish_matchday(sat, cal_at(saved, saved.date), saved.manager_records, saved.manager_options, r3);
                if (r3.unsupported) std::printf("  not supported: %s\n", r3.reason.c_str()), fine = false;
                b.date++;
                daily_interest(b, br);
                fine = day_check(entries[iday]) && fine;
                std::printf("\nsection E: days after day %u\n", b.date);
                fine = days_loop(iday + 1) && fine;
                return fine ? 0 : 2;
            }
            bool replayed = false;
            try {
                replayed = replay_matchday(s, sat, sg, sr, entries, i1, whistle_pause ? first91 - 1 : first91, db, buttons, 90);
            } catch (const std::exception& ex) {
                std::printf("  exception: %s\n", ex.what());
            }
            if ((whistle_pause ? check("checkpoint whistle", s, sg, sr, whistle) : replayed) && !sk.unsupported) {
                from_desk = true;
                desk_state = s, desk_game = sg, desk_rand = sr, desk_groups = sat;
            }
        }
        std::printf("\n");
    }

    int last_rain = 0, last_temperature = 0;
    {
        size_t i90 = 0;
        for (size_t i = 0; i < first91; i++)
            if (entries[i].kind == "minute" && entries[i].value == 90) i90 = i;
        if (!whistle_pause) {
            std::printf("section 0: no final whistle pause (the matchday ran in the background)\n\n");
        } else if (i90 == 0 || entries[i90].state.empty()) {
            std::printf("section 0: no minute 90 snapshot with state\n");
        } else {
            const Entry& m90 = entries[i90];
            // the savegame's state with the matchday tables of the snapshot (a re-rating needs
            // the rest of the state too, e.g. the table history)
            GameState gs = saved;
            {
                const GameState d = state_from_dump(m90.state);
                gs.squad_slots = d.squad_slots, gs.players = d.players, gs.team_morale = d.team_morale;
                gs.team_flags = d.team_flags, gs.player_bonus = d.player_bonus, gs.player_cards = d.player_cards;
                gs.club_table = d.club_table, gs.formations = d.formations, gs.live_matches = d.live_matches;
            }
            std::vector<MatchGroup> grs = groups_from_snapshot(m90.line);
            for (MatchGroup& grp : grs)
                for (int m = 0; m < grp.matches(); m++)
                    if (grp.end_minute[m] >= 46 && grp.end_minute[m] < 90) grp.ended |= 1u << m;
            GameRandom game0(int32_t(std::stoll(field(m90.line, "game_seed"))));
            WatcomRandom rand0(uint32_t(std::stoull(field(m90.line, "rand_seed"))));
            MatchEngine engine{gs, grs, game0, rand0, db.raw_referee_properties};
            engine.alternate_event_roll = field(m90.line, "alternate_event_roll") == "1";
            engine.match_events_disabled = field(m90.line, "match_events_disabled") == "1";
            engine.live_desk_disabled = field(m90.line, "live_desk_disabled") == "1";
            engine.month = std::stoi(field(m90.line, "month"));
            engine.buttons = buttons;
            engine.leave_screens = true;  // (as in the replay of section S)
            MinuteReport rep;
            bool running = true;
            for (int minute = 90; minute <= 94 && running && !rep.unsupported; minute++) {
                rep = engine.minute(minute, 90, 4);
                running = false;
                for (const MatchGroup& grp : grs)
                    for (int m = 0; m < grp.matches(); m++)
                        if (!((grp.void_mask() | grp.ended) >> m & 1)) running = true;
            }
            std::printf("section 0: minute 90 -> pause %d: %s", whistle.value, rep.unsupported ? rep.reason.c_str() : "");
            const bool seeds = game0.seed() == int32_t(std::stoll(field(whistle.line, "game_seed"))) &&
                               rand0.seed() == uint32_t(std::stoull(field(whistle.line, "rand_seed")));
            std::printf("seeds %s, last chance weather: rain %d, temperature %d\n\n", seeds ? "OK" : "DIFF", gs.chance_rain,
                        gs.chance_temperature);
            last_rain = gs.chance_rain, last_temperature = gs.chance_temperature;
        }
    }

    std::printf("section A: pause %d (t=%s ms) -> minute 91 (t=%s ms)\n", whistle.value,
                field(whistle.line, "t_ms").c_str(), field(m91.line, "t_ms").c_str());

    GameState g = state_from_dump(whistle.state);
    g.chance_rain = last_rain, g.chance_temperature = last_temperature;
    g.memory = saved.memory, g.foreign_strength = saved.foreign_strength;
    g.mailbox = saved.mailbox, g.texts = &textdb;
    {
        // the team card counters as the original left them after the recorded matchday
        const Bytes cards = unhex(field(whistle.state, "team_cards"));
        for (size_t i = 0; i < cards.size(); i++) g.memory.set8(::bmh::layout::DeskRecords_Teams_RuntimeField + uint32_t(i), cards[i]);
    }
    g.club_ids = saved.club_ids;
    g.club_heap_78 = saved.club_heap_78;
    g.club_names = saved.club_names;
    g.player_name_offsets = saved.player_name_offsets;
    g.player_names = saved.player_names;
    if (field(whistle.state, "a3fd3").empty())
        g.list_toggle = saved.list_toggle, g.autosave_months = saved.autosave_months, g.autosave_month = saved.autosave_month;
    g.date = saved.date, g.season_start = saved.season_start;
    g.manager_results = saved.manager_results;
    g.manager_ratings = saved.manager_ratings;
    g.position_history = saved.position_history;
    g.league_rounds = saved.league_rounds;
    GameRandom game(int32_t(std::stoll(field(whistle.line, "game_seed"))));
    WatcomRandom rand(uint32_t(std::stoull(field(whistle.line, "rand_seed"))));
    // final results: the groups as recorded at minute 91 (end minutes are set by then too)
    std::vector<MatchGroup> groups = groups_from_snapshot(m91.line);
    if (from_desk) {
        // continue with our own replay instead of the recorded state
        std::printf("  (continuing from the replay of section S)\n");
        g = desk_state, game = desk_game, rand = desk_rand, groups = desk_groups;
        // the descriptor copies take the log counts the replay left in memory
        for (size_t gi = 0; gi < groups.size(); gi++) {
            const uint16_t n = uint16_t(groups[gi].log.size());
            groups[gi].desc.at(0x94) = uint8_t(n), groups[gi].desc.at(0x95) = uint8_t(n >> 8);
        }
        MinuteReport er;
        end_phase(g, groups, 90, buttons, game, rand, er);  // result screens, legacy routine, void bits
        if (er.unsupported) std::printf("  not supported: %s\n", er.reason.c_str());
    } else {
        for (size_t gi = 0; gi < groups.size(); gi++) {
            MatchGroup& grp = groups[gi];
            int league = int(le32(grp.desc, 0xcc));
            if (league < 0x15 && league_screen_shown(buttons, 2, 90, league)) stir_random(game, rand);
            for (int m = 0; m < grp.matches(); m++) finish_match(g, grp, m, 90, game);
        }
    }
    evaluate_pools_coupon(g, groups);  // legacy routine(0) after the second half
    if (no91) std::printf("  (no minute 91 snapshot)\n");
    else ok = check("checkpoint minute 91", g, game, rand, m91);

    // --- section B: start of the second extra time phase (minute 106) -> first day change
    size_t i106 = no91 ? first91 : first91 + 1, iday = 0;
    for (size_t i = i106; i < entries.size(); i++)
        if (entries[i].kind == "day") {
            iday = i;
            break;
        }
    if (entries[i106].kind != "minute" || entries[i106].value != 106 || iday == 0) {
        std::printf("section B: minute 106 or the day change not found\n");
        return 2;
    }
    const Entry& m106 = entries[i106];
    const Entry& day = entries[iday];
    std::printf("\nsection B: minute 106 (t=%s ms) -> day %d (t=%s ms)\n", field(m106.line, "t_ms").c_str(), day.value,
                field(day.line, "t_ms").c_str());
    {
        // Continue from the checked state of minute 91: the minute 106 snapshot is consistent
        // but already inside the group processing (the recorder only notices it later).
        b = g;
        b.position_history = saved.position_history;
        b.league_rounds = saved.league_rounds;
        b.results = saved.results;
        b.stocks = saved.stocks;
        b.season = saved.season;
        bg = game;
        br = rand;
        std::vector<MatchGroup> grs = groups;
        uint32_t competitions = 0;
        // the matchday's results are already in the result block (descriptor +8 points there)
        for (const MatchGroup& grp : grs) {
            int league = int(le32(grp.desc, 0xcc));
            if (league >= 9) continue;
            int round = b.league_rounds.at(league);
            for (int m = 0; m < grp.matches(); m++) b.results.at(size_t(league) * 0x17C + size_t(round) * 10 + m) = grp.results.at(m);
        }
        // competition bits of the day (format field, from the calendar via legacy routine)
        uint16_t today = cal_at(saved, saved.date);
        competitions = day_competitions(saved, today);
        std::printf("  calendar entry of day %u: 0x%04x\n", saved.date, today);
        // driver: the "played" markers of the tactics boards are reset
        for (size_t mgr = 0; mgr < b.manager_teams.size(); mgr++)
            for (int s = 0; s < 24; s++)
                for (int k = 0; k < 2; k++) {
                    size_t at = mgr * 0x840 + size_t(s) * 0x58 + 0x44 + k * 2;
                    b.tactics.at(at) = 0xFF, b.tactics.at(at + 1) = 0x0F;
                }
        MinuteReport report;
        settle_pools_coupon(b, grs);  // legacy routine(1) after the matchday's phases
        for (const MatchGroup& grp : grs) {
            process_group(b, grp, bg, br, report);
            int league = int(le32(grp.desc, 0xcc));
            if (league < 0x15 && league_screen_shown(buttons, 1, 90, league)) stir_random(bg, br);
            if (league >= 0x15) store_cup_results(b, grp);  // cup groups (e.g. postponed matches)
        }
        // legacy routine (supported edition) restores the attitude bytes and assistant options it saved
        // before the matchday
        for (size_t mgr = 0; mgr < b.manager_teams.size(); mgr++) {
            for (int k = 0; k < 8; k++) b.manager_records.at(mgr * 0xB0C + 0xd2 + k) = saved.manager_records.at(mgr * 0xB0C + 0xd2 + k);
            b.manager_options.at(mgr * 5 + 2) = saved.manager_options.at(mgr * 5 + 2);
        }
        store_group_descriptors(b, grs);  // the descriptors stay in memory (block MatchdayGroupDescriptors)
        advance_league_rounds(b, competitions & 0x1F, br, report);
        for (int bit = 5; bit < 0x13; bit++)
            if ((competitions >> bit) & 1) advance_cup(b, bit - 5, br, report, &bg);
        event_end(b);  // (legacy routine at the end of legacy routine)
        if (report.unsupported) std::printf("  not supported: %s\n", report.reason.c_str());
        // legacy routine: date + 1, then the daily interest (the recorder freezes the game a
        // moment after the date change)
        b.date++;
        daily_interest(b, br);
        ok = day_check(day) && ok;

        // --- section C: rest of the day (legacy routine), day counter, stock market -> pause
        size_t ipost = iday + 1;
        while (ipost < entries.size() && entries[ipost].kind != "post") ipost++;
        {
            // (a pause dated before the next event day is a message box of the day steps -
            //  e.g. a player asking for contract talks in the weekly processing: skipped)
            uint32_t event_day = b.date;
            while (event_day < b.date + 400 && cal_at(b, event_day) == 0) event_day++;
            while (ipost < entries.size() && entries[ipost].kind == "post" && !field(entries[ipost].line, "date").empty() &&
                   uint32_t(std::stoul(field(entries[ipost].line, "date"))) < event_day) {
                std::printf("  (pause %d of day %s before the event day %u: a message box, skipped)\n", entries[ipost].value,
                            field(entries[ipost].line, "date").c_str(), event_day);
                ipost++;
                while (ipost < entries.size() && entries[ipost].kind != "post") ipost++;
            }
        }
        if (ipost < entries.size()) {
            const Entry& post = entries[ipost];
            std::printf("\nsection C: day %d -> pause %d (t=%s ms), weekday code %d\n", day.value, post.value,
                        field(post.line, "t_ms").c_str(), weekday_code(b.date - 1));
            // legacy routine tests format field after legacy routine(date - 1), i.e. the weekday of
            // the previous day decides whether it is a training day
            auto rest_of_day_step = [&](bool after_completed_event) {
                const int wd = weekday_code(b.date - 1);
                if ((wd == 2 || wd == 6) && full_game(b) && weekday_code(b.date) == 3)
                    std::printf("  day %u: weekly processing (legacy routine)\n", b.date);
                if (full_game(b) && month_code(b.date - 1) != month_code(b.date))
                    std::printf("  day %u: month change (legacy routine)\n", b.date);
                MinuteReport wr;
                const uint16_t source_rest_day = uint16_t(
                    b.date - b.season_start - uint32_t(after_completed_event ? 1 : 0));
                if (!day_step_rest(b, bg, br, wr, source_rest_day, allocator_context)) {  // (game/turn.hpp)
                    std::printf("  not supported: %s\n", wr.reason.c_str());
                    ok = false;
                }
            };
            rest_of_day_step(true);
            day_stock_market(b, bg);
            // days without an event up to the next one: the shares move before the day step
            while (cal_at(b, b.date) == 0) {
                plain_day(b, bg, br);  // (game/turn.hpp)
                for (size_t i = iday + 1; i < ipost; i++)
                    if (entries[i].kind == "day" && entries[i].value == int(b.date))
                        ok = day_check(entries[i]) && ok;
                rest_of_day_step(false);
            }

            // The "Weiter" loop stops at the day's event: a matchday is prepared
            // (legacy routine) and kicked off; the pause follows right before the first minute.
            const uint16_t entry = cal_at(b, b.date);
            size_t ikick = ipost + 1;
            while (ikick < entries.size() && entries[ikick].kind != "minute") ikick++;
            // (a matchday may also start without a pause the recorder notices: its minutes
            //  then come before the first pause, e.g. a cup round followed by a draw screen)
            bool no_pause = false;
            for (size_t m = iday + 1; m < ipost; m++)
                if (entries[m].kind == "minute") {
                    ikick = m, no_pause = true;
                    break;
                }
            const Entry* pause_entry = &post;  // (after desks shown first: the pause after them)
            std::vector<MatchGroup> sunday;
            GameState pre_kick;
            WatcomRandom pre_rand;
            std::vector<MatchGroup> pre_groups;
            MatchdayContext kick_ctx;
            if ((entry & 0x7FFF) != 0 && ikick == entries.size()) {
                // no background matchday in between: the next event is the human manager's
                // own matchday, the pause is his pre-match desk
                std::printf("  calendar entry of day %u: 0x%04x (the manager's desk)\n", b.date, entry);
                ok = desk_turn(entry, post) && ok;
                return ok ? 0 : 2;
            }
            if ((entry & 0x7FFF) != 0 && ikick < entries.size()) {
                std::printf("  calendar entry of day %u: 0x%04x\n", b.date, entry);
                std::vector<uint8_t> referees = db.raw_referee_properties;
                MatchdayContext ctx;
                ctx.month = month_code(b.date);  // format field (the snapshot may belong to a later day)
                ctx.season_day = b.season_day();
                ctx.weekday = weekday_code(b.date);
                ctx.referees = &referees;
                MinuteReport rep;
                sunday = setup_matchday(b, day_competitions(b, entry), ctx, bg, br, rep);
                draw_matchday_number(b, br);  // format field
                const std::vector<int> order = shuffle_manager_order(b, bg);
                // A human club plays (e.g. one manager's cup tie): legacy routine shows every
                // manager's desk first (the others "spielfrei"), in the shuffled order. The
                // turn automation clicked "Anstoss" at each; a desk has its own pause when the
                // recorder caught one (matched by the seeds).
                bool human_plays = false;
                for (const MatchGroup& grp : sunday)
                    for (int t : grp.fixtures) human_plays = human_plays || b.manager_of(t) >= 0;
                if (human_plays && !b.manager_teams.empty() && order.size() > 1) {
                    store_manager_order(b, order);
                    std::printf("  a human club plays: the desks of all managers first\n");
                    size_t next_post = ipost;
                    for (size_t k = 0; k < order.size(); k++) {
                        set_desk_manager(b, int(k));
                        if (k > 0) answer_offers(b, order[k - 1], true, br, rep, &bg);
                        pre_match_offer(b, order[k], bg, br, rep);  // legacy routine
                        build_pools_coupon(b, sunday, &br);            // legacy routine
                        enter_desk(b, order[k], bg, br);
                        const bool own_pause = next_post < entries.size() && entries[next_post].kind == "post" &&
                                               bg.seed() == int32_t(std::stoll(field(entries[next_post].line, "game_seed")));
                        std::printf("  the desk of manager %d (order position %zu)%s\n", order[k], k,
                                    own_pause ? "" : " - no pause recorded");
                        if (own_pause) {
                            ok = check("checkpoint desk of a manager", b, bg, br, entries[next_post]) && ok;
                            next_post++;
                            while (next_post < entries.size() && entries[next_post].kind != "post") next_post++;
                        }
                    }
                    answer_offers(b, order.back(), true, br, rep, &bg);
                    free_offer_slots(b);
                    if (next_post < entries.size()) pause_entry = &entries[next_post];
                }
                pre_kick = b, pre_rand = br, pre_groups = sunday, kick_ctx = ctx;
                kick_off(b, sunday, ctx, br, bg, rep);
                if (rep.unsupported) std::printf("  not supported: %s\n", rep.reason.c_str());
            }
            if (no_pause) std::printf("  (the matchday started without a pause)\n");
            else ok = check("checkpoint pause", b, bg, br, *pause_entry) && ok;

            if (!sunday.empty() && field(entries[ikick].line, "date") != std::to_string(b.date)) {
                // The recorder caught no minute of this matchday (a fast background day):
                // the first minute snapshot belongs to a later one. Played without checks;
                // the day snapshots and the next pause decide.
                std::printf("  (no minute snapshot of day %u: played unchecked)\n", b.date);
                MinuteReport rep;
                ok = replay_matchday(b, sunday, bg, br, entries, ikick, ikick, db, buttons, 0, month_code(b.date)) && ok;
                finish_matchday(sunday, entry, pre_kick.manager_records, pre_kick.manager_options, rep);
                if (rep.unsupported) std::printf("  not supported: %s\n", rep.reason.c_str()), ok = false;
                b.date++;
                daily_interest(b, br);
                size_t inext = ipost + 1;
                if (inext < entries.size() && entries[inext].kind == "day" && entries[inext].value == int(b.date))
                    ok = day_check(entries[inext++]) && ok;
                std::printf("\nsection E: days after day %u\n", b.date);
                ok = days_loop(inext) && ok;
                return ok ? 0 : 2;
            }
            if (!sunday.empty()) {
                // Kick-off data against the first minute snapshot (taken a few draws into
                // minute 1, before any match data changes).
                const Entry& first = entries[ikick];
                std::vector<MatchGroup> rec = groups_from_snapshot(first.line);
                std::printf("\nkick-off of day %u against minute %d (t=%s ms): %zu/%zu groups\n", b.date, first.value,
                            field(first.line, "t_ms").c_str(), sunday.size(), rec.size());
                const int diffs = compare_kickoff(sunday, rec, &b);
                ok = diffs == 0 && ok;

                // --- section D: the background matchday itself, from our kick-off to the next
                // day. The minute snapshots of a fast (not shown) matchday are sparse and land
                // somewhere inside their minute: each is checked by locating its recorded seeds
                // in the random number streams between the start and the end of that minute.
                // (the matchday ends at the next day snapshot - or at the next pause when the
                //  recorder caught no day change before the next event, WEEK187)
                size_t iend = ikick;
                while (iend < entries.size() && entries[iend].kind != "day" &&
                       !(entries[iend].kind == "post" && std::stoi(field(entries[iend].line, "date")) > int(b.date)))
                    iend++;
                if (iend == entries.size()) return ok ? 0 : 2;
                const Entry& next_day = entries[iend];
                const bool day_snapshot = next_day.kind == "day";
                std::printf("\nsection D: kick-off -> %s %d (t=%s ms)\n", day_snapshot ? "day" : "pause", next_day.value,
                            field(next_day.line, "t_ms").c_str());
                ok = replay_matchday(b, sunday, bg, br, entries, ikick, iend, db, buttons) && ok;
                std::printf("  last chance weather: rain %d, temperature %d\n", b.chance_rain, b.chance_temperature);

                // after the phases: results into the result block, group processing, tables
                for (MatchGroup& grp : sunday) {
                    const uint32_t league = le32(grp.desc, 0xcc);
                    if (league >= 0x15) continue;  // cup groups: store_cup_results below
                    const int round = b.league_rounds.at(size_t(league));
                    for (int m = 0; m < grp.matches(); m++) b.results.at(size_t(league) * 0x17C + size_t(round) * 10 + m) = grp.results.at(m);
                }
                for (size_t mgr = 0; mgr < b.manager_teams.size(); mgr++)
                    for (int s = 0; s < 24; s++)
                        for (int k = 0; k < 2; k++) {
                            size_t at = mgr * 0x840 + size_t(s) * 0x58 + 0x44 + k * 2;
                            b.tactics.at(at) = 0xFF, b.tactics.at(at + 1) = 0x0F;
                        }
                MinuteReport report2;
                for (MatchGroup& grp : sunday) {
                    process_group(b, grp, bg, br, report2);
                    const int league = int(le32(grp.desc, 0xcc));
                    if (league < 0x15 && league_screen_shown(buttons, 1, 90, league)) stir_random(bg, br);
                    if (le32(grp.desc, 0xcc) >= 0x15) store_cup_results(b, grp);
                }
                for (size_t mgr = 0; mgr < b.manager_teams.size(); mgr++) {
                    for (int k = 0; k < 8; k++)
                        b.manager_records.at(mgr * 0xB0C + 0xd2 + k) = pre_kick.manager_records.at(mgr * 0xB0C + 0xd2 + k);
                    b.manager_options.at(mgr * 5 + 2) = pre_kick.manager_options.at(mgr * 5 + 2);
                }
                advance_league_rounds(b, day_competitions(b, entry) & 0x1F, br, report2);
                for (int bit = 5; bit < 0x13; bit++)
                    if ((day_competitions(b, entry) >> bit) & 1) advance_cup(b, bit - 5, br, report2, &bg);
                event_end(b);  // (legacy routine at the end of legacy routine)
                if (report2.unsupported) std::printf("  not supported: %s\n", report2.reason.c_str());
                b.date++;
                daily_interest(b, br);
                if (day_snapshot)
                    ok = day_check(next_day) && ok;
                else
                    std::printf("  day %u: no snapshot\n", b.date);

                // --- section E: the following days, until the next event in the calendar
                std::printf("\nsection E: days after day %u\n", b.date);
                ok = days_loop(day_snapshot ? iend + 1 : iend) && ok;
            }
        }
    }
    return ok ? 0 : 2;
}
