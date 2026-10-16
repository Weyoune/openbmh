// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
// capture_check - compares the ported match engine with a matchday recorded from the
// original (runtime verification).
//
// The capture holds one JSON object per line: minute snapshots (seeds, per match group
// the fixtures, results and the 18-byte side records) and one "state" object with the
// tables the engine used at kick-off. This tool rebuilds a GameState from that dump and
// compares the six side ratings (bytes 0-5 of each side record) with team_rating().
//
// Usage: capture_check <capture.jsonl>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "formats/binary.hpp"
#include "runtime_fixture_tables.hpp"
#include "formats/fbm.hpp"
#include "formats/m3d.hpp"
#include "formats/savestate.hpp"
#include "game/lineup.hpp"
#include "game/match.hpp"
#include "game/match_engine.hpp"
#include "game/rating.hpp"
#include "game/state.hpp"

using namespace bmh;

#include "capture_io.hpp"

using namespace bmh::capture;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: capture_check <capture.jsonl>\n");
        return 1;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::ifstream in(argv[1]);
    std::string line, first_minute, dump;
    while (std::getline(in, line)) {
        if (first_minute.empty() && line.rfind("{\"minute\":1,", 0) == 0) first_minute = line;
        // the kick-off state: the first state line after the minute-1 snapshot
        if (line.rfind("{\"state\"", 0) == 0 && dump.empty() && !first_minute.empty()) dump = line;
    }
    if (first_minute.empty() || dump.empty()) {
        std::printf("capture has no minute-1 snapshot or no state dump\n");
        return 1;
    }
    GameState base = state_from_dump(dump);

    int checked = 0, exact = 0;
    size_t pos = first_minute.find("\"groups\":[");
    for (int group = 0;; group++) {
        size_t gpos = first_minute.find("{\"type\":", pos);
        if (gpos == std::string::npos) break;
        pos = gpos + 1;
        int matches = std::stoi(field(first_minute, "matches", gpos));
        Bytes fixtures = unhex(field(first_minute, "fixtures", gpos));
        Bytes sides = unhex(field(first_minute, "sides", gpos));
        std::printf("group %d: %d matches\n", group, matches);
        for (int m = 0; m < matches && size_t(m * 4 + 3) < fixtures.size(); m++) {
            for (int side = 0; side < 2; side++) {
                int team = le16(fixtures, m * 4 + side * 2);
                const uint8_t* rec = sides.data() + m * 18 + side * 9;
                std::printf("  match %d %s team %3d  original:", m, side ? "away" : "home", team);
                for (int i = 0; i < 6; i++) std::printf(" %3d", rec[i]);
                if (base.manager_of(team) >= 0 || team >= 168) {
                    std::printf("   (not comparable: %s)\n", team >= 168 ? "no squad" : "human manager");
                    continue;
                }
                std::printf("   ported:");
                bool same = true;
                for (int i = 0; i < 6; i++) {
                    GameState g = base;
                    int v = team_rating(g, team, kRatingProfiles[i]);
                    std::printf(" %3d", v);
                    if (i != 2 && v != rec[i]) same = false;  // byte 2 gets a home/away modifier afterwards
                }
                std::printf("%s\n", same ? "   OK" : "   DIFF");
                checked++;
                exact += same;
            }
        }
    }
    std::printf("\n%d of %d computer sides match (bytes 0,1,3,4,5)\n", exact, checked);

    // Full side records via build_side_records. Spectators are not captured, so the home
    // bonus is unknown: byte 2 must equal the computed value or the computed value + 6.
    std::printf("\nfull side records (bytes 0-8):\n");
    int full_checked = 0, full_exact = 0;
    pos = first_minute.find("\"groups\":[");
    for (int group = 0;; group++) {
        size_t gpos = first_minute.find("{\"type\":", pos);
        if (gpos == std::string::npos) break;
        pos = gpos + 1;
        int matches = std::stoi(field(first_minute, "matches", gpos));
        int flags = std::stoi(field(first_minute, "flags", gpos));
        Bytes desc = unhex(field(first_minute, "desc", gpos));
        Bytes fixtures = unhex(field(first_minute, "fixtures", gpos));
        Bytes sides = unhex(field(first_minute, "sides", gpos));
        for (int m = 0; m < matches && size_t(m * 4 + 3) < fixtures.size(); m++) {
            MatchSetup setup;
            setup.home = le16(fixtures, m * 4);
            setup.away = le16(fixtures, m * 4 + 2);
            setup.match_index = m;
            setup.climate_effect = (flags & 2) != 0;
            setup.temperature = int8_t(desc.at(0x30 + m));
            setup.spectators = 0;
            setup.month = 8;  // format field read from the running original in August
            if (base.manager_of(setup.home) >= 0 || base.manager_of(setup.away) >= 0 || setup.home >= 168 ||
                setup.away >= 168)
                continue;
            GameState g = base;
            WatcomRandom rand(1);
            auto rec = build_side_records(g, setup, rand);
            for (int side = 0; side < 2; side++) {
                const uint8_t* orig = sides.data() + m * 18 + side * 9;
                bool ok = true;
                for (int i = 0; i < 8; i++) {
                    if (i == 2) {
                        int d = orig[2] - rec[side].b[2];
                        ok = ok && (d == 0 || (side == 0 && d == 6));
                    } else {
                        ok = ok && orig[i] == rec[side].b[i];
                    }
                }
                // Attitude (byte 8). Home: compared against uninitialised heap memory in the
                // original, so the "stronger by more than 10" step may or may not apply.
                // Away: recompute with the original's home byte 2 (it includes the home bonus).
                int base_attitude = 2 - side;
                int own2 = orig[2], other2 = sides[m * 18 + (1 - side) * 9 + 2];
                int expected = base_attitude;
                if (own2 + 10 < other2) expected = 1 - side;
                if (other2 < own2 - 10 && m != 0) expected++;
                if (side == 0) ok = ok && (orig[8] == 2 || orig[8] == 3 || orig[8] == 1);
                else ok = ok && orig[8] == expected;
                full_checked++;
                full_exact += ok;
                if (!ok) {
                    std::printf("  group %d match %d %s:", group, m, side ? "away" : "home");
                    for (int i = 0; i < 9; i++) std::printf(" %3d/%-3d", orig[i], rec[side].b[i]);
                    std::printf("\n");
                }
            }
        }
    }
    std::printf("%d of %d full side records match (original/ported shown for mismatches)\n", full_exact, full_checked);

    // Line-ups: apply auto_lineup to the savegame taken right before the matchday and
    // compare the first 11 squad slots (ids and flags) with the kick-off dump.
    if (argc >= 4 && !std::getenv("BMH_TRACE")) {  // line-up tests (skipped when tracing a minute)
        Savegame sg = load_savegame(argv[3]);
        SaveState st(sg.stream);
        GameState before = GameState::from_save(st);
        for (int mode : {-1, 0, 100, 200}) {
            int teams = 0, same = 0, same_players = 0;
            pos = first_minute.find("\"groups\":[");
            size_t gpos;
            while ((gpos = first_minute.find("{\"type\":", pos)) != std::string::npos) {
                pos = gpos + 1;
                Bytes fixtures = unhex(field(first_minute, "fixtures", gpos));
                for (size_t k = 0; k + 1 < fixtures.size(); k += 2) {
                    int team = le16(fixtures, k);
                    if (team >= 138 || base.manager_of(team) >= 0) continue;
                    GameState g = before;
                    if (mode == 200) {
                        // Scramble: reverse the used part of the squad and clear all flags, then
                        // select from scratch. The same starting eleven must come out.
                        SquadRange rr = squad_range(team);
                        size_t used = 0;
                        while (used < rr.count && (g.squad_slots[rr.first + used] & 0x0FFF) != 0x0FFF) used++;
                        for (size_t a = 0, b = used - 1; a < b; a++, b--)
                            std::swap(g.squad_slots[rr.first + a], g.squad_slots[rr.first + b]);
                        for (size_t a = 0; a < used; a++) g.squad_slots[rr.first + a] &= 0x0FFF;
                        auto_lineup(g, team, 100);
                    }
                    if (mode == 100) auto_lineup(g, team, 100);
                    if (mode >= 0 && mode < 200) auto_lineup(g, team, 0);  // -1: unchanged savegame, as a baseline
                    SquadRange r = squad_range(team);
                    bool ok = true;
                    for (size_t s = 0; s < 11; s++) {
                        uint16_t mine = g.squad_slots[r.first + s], orig = base.squad_slots[r.first + s];
                        if (mode == 200) {
                            // Tie-insensitive: the scrambled order decides between equal players,
                            // so compare line-up flag, position type and value per slot instead of ids.
                            const Player &pm = before.players[mine & 0x0FFF], &po = before.players[orig & 0x0FFF];
                            auto val = [](const Player& p) { return p.form() / 3 + p.condition() + p.technique(); };
                            if ((mine & 0x8000) != (orig & 0x8000) || pm.position() != po.position() || val(pm) != val(po))
                                ok = false;
                        } else if ((mine & 0x8FFF) != (orig & 0x8FFF)) {
                            ok = false;
                        }
                    }
                    teams++;
                    same += ok;
                    if (!ok && mode == 200) {
                        // same eleven players regardless of order?
                        std::vector<int> a, b;
                        for (size_t s = 0; s < 11; s++) {
                            a.push_back(g.squad_slots[r.first + s] & 0x0FFF);
                            b.push_back(base.squad_slots[r.first + s] & 0x0FFF);
                        }
                        std::sort(a.begin(), a.end());
                        std::sort(b.begin(), b.end());
                        if (a == b) same_players++;
                        if (teams - same <= 2) {
                            std::printf("  team %3d formation %d\n", team, g.formation(team));
                            for (size_t s = 0; s < 11; s++) {
                                int pid = g.squad_slots[r.first + s] & 0x0FFF, oid = base.squad_slots[r.first + s] & 0x0FFF;
                                const Player &pp = before.players[pid], &op = before.players[oid];
                                int x, y;
                                g.formation_position(team, int(s), x, y);
                                std::printf("    slot %2zu x%2d  ported %4d pos%d val%3d   original %4d pos%d val%3d\n", s, x, pid,
                                            pp.position(), pp.form() / 3 + pp.condition() + pp.technique(), oid, op.position(),
                                            op.form() / 3 + op.condition() + op.technique());
                            }
                        }
                    }
                    if (!ok && mode == 0 && teams - same <= 3) {
                        std::printf("  team %3d  ported :", team);
                        for (size_t s = 0; s < 11; s++) std::printf(" %04x", g.squad_slots[r.first + s]);
                        std::printf("\n            original:");
                        for (size_t s = 0; s < 11; s++) std::printf(" %04x", base.squad_slots[r.first + s]);
                        std::printf("\n");
                    }
                }
            }
            std::printf("line-up mode %s: %d of %d computer teams match the kick-off line-up\n",
                        mode < 0      ? "none (baseline)"
                        : mode == 0   ? "<100 (re-sort 11)"
                        : mode == 100 ? "100 then <100"
                                      : "scrambled squad, 100",
                        same, teams);
            if (mode == 200) std::printf("  (same eleven players in another order: %d more)\n", same_players);
        }
    }

    // Minute by minute: start from the recorded state of minute n, simulate it, compare
    // with the recording of minute n + 1 (both RNG states and all results).
    if (argc >= 3) {
        Database db = load_database(argv[2]);
        bmh::test::load_executable(std::filesystem::path(argv[2]).parent_path() / "BMH.exe");
        // Button settings (block data-field offset, 0xb4 bytes, same layout as the .EIN files): they
        // switch the live-match animations on and off. Taken from the savegame if given.
        // Tactics boards, manager records and the game level are not in the per-minute
        // dumps either; they come from the savegame as well.
        std::vector<uint8_t> buttons;
        GameState saved;
        if (argc >= 4) {
            Savegame sg = load_savegame(argv[3]);
            SaveState st(sg.stream);
            buttons.assign(st.data(::bmh::layout::LeaguePresentationOptions), st.data(::bmh::layout::LeaguePresentationOptions) + 0xb4);
            saved = GameState::from_save(st);
            std::printf("\nbutton settings: a2507=%d a2510=%d a2543=%d a2570=%d, game level %d\n", buttons[0x10], buttons[0x19],
                        buttons[0x4c], buttons[0x79], saved.level);
        }
        // minute lines, each with the state line that follows it (newer captures) if any
        std::vector<std::pair<std::string, std::string>> minutes;
        {
            std::ifstream again(argv[1]);
            while (std::getline(again, line)) {
                if (line.rfind("{\"minute\":", 0) == 0 && line.rfind("{\"minute\":-1", 0) != 0) minutes.push_back({line, ""});
                else if (line.rfind("{\"state\"", 0) == 0 && !minutes.empty() && minutes.back().second.empty())
                    minutes.back().second = line;
            }
        }
        // Field of one group: search only between this group's start and the next group.
        auto group_field = [](const std::string& l, const std::string& key, size_t start) {
            size_t end = l.find("{\"type\":", start + 1);
            std::string v = field(l, key, start);
            size_t at = l.find("\"" + key + "\":", start);
            return (at == std::string::npos || (end != std::string::npos && at > end)) ? std::string() : v;
        };
        std::printf("\nminute simulation (%zu snapshots):\n", minutes.size());
        int ok = 0, bad = 0, skipped = 0;
        // Stoppage time: from minute last + 1 on, the game draws the end-of-match rolls right
        // after storing the minute, so the recorder reads the seeds about one draw late.
        // Those minutes are therefore simulated in a chain from the last reliable snapshot
        // (the regular last minute) instead of restarting from each recording.
        struct Chain {
            std::vector<MatchGroup> groups;
            GameState state;
            GameRandom game{0};
            WatcomRandom rand{0};
            int minute = -1;
        } chain;
        for (size_t i = 0; i + 1 < minutes.size(); i++) {
            const std::string &a = minutes[i].first, &b = minutes[i + 1].first;
            int minute = std::stoi(field(a, "minute")), next = std::stoi(field(b, "minute"));
            // Regular league/cup phases (legacy routine(1, 45, 0) and (46, 90, 4)); extra time
            // and indoor tournaments are not covered here yet.
            const int phase_first = minute <= 45 ? 1 : 46, last = minute <= 45 ? 45 : 90, stoppage = minute <= 45 ? 0 : 4;
            if (next != minute + 1 || minute > last + stoppage) continue;
            if (std::getenv("BMH_TRACE") && std::stoi(std::getenv("BMH_TRACE")) != minute) continue;  // one minute only
            std::vector<MatchGroup> groups;
            size_t p = a.find("\"groups\":[");
            while ((p = a.find("{\"type\":", p)) != std::string::npos) {
                MatchGroup g;
                g.desc = unhex(group_field(a, "desc", p));
                Bytes fx = unhex(group_field(a, "fixtures", p));
                for (size_t k = 0; k + 1 < fx.size(); k += 2) g.fixtures.push_back(le16(fx, k));
                g.results = unhex(group_field(a, "results", p));
                g.records = unhex(group_field(a, "sides", p));
                Bytes cup = unhex(group_field(a, "cup0", p));
                for (size_t k = 0; k + 1 < cup.size(); k += 2) g.cup_results.push_back(le16(cup, k));
                Bytes crowd = unhex(group_field(a, "spectators", p));
                for (size_t k = 0; k + 3 < crowd.size(); k += 4) g.spectators.push_back(le32(crowd, k));
                g.end_minute = unhex(group_field(a, "end_minutes", p));
                g.end_minute.resize(g.matches(), 0);
                // Matches that ended earlier in this phase (end minute recorded since then).
                for (int m = 0; m < g.matches(); m++)
                    if (g.end_minute[m] >= phase_first && g.end_minute[m] < minute) g.ended |= 1u << m;
                groups.push_back(g);
                p++;
            }
            bool cup_recorded = a.find("\"cup0\":") != std::string::npos;
            GameState gs = minutes[i].second.empty() ? base : state_from_dump(minutes[i].second);
            gs.tactics = saved.tactics;
            gs.manager_records = saved.manager_records;
            gs.level = saved.level;
            gs.manager_options = saved.manager_options;
            GameRandom game(int32_t(std::stoll(field(a, "game_seed"))));
            WatcomRandom rnd(uint32_t(std::stoull(field(a, "rand_seed"))));
            const bool chained = minute > last && chain.minute == minute - 1;
            if (chained) {
                groups = chain.groups;
                gs = chain.state;
                game = chain.game;
                rnd = chain.rand;
            }
            MatchEngine engine{gs, groups, game, rnd, db.raw_referee_properties};
            if (!field(a, "alternate_event_roll").empty()) engine.alternate_event_roll = field(a, "alternate_event_roll") != "0";
            if (!field(a, "match_events_disabled").empty()) engine.match_events_disabled = field(a, "match_events_disabled") != "0";
            if (!field(a, "live_desk_disabled").empty()) engine.live_desk_disabled = field(a, "live_desk_disabled") != "0";
            engine.month = field(a, "month").empty() ? 8 : std::stoi(field(a, "month"));
            engine.buttons = buttons;
            engine.trace = std::getenv("BMH_TRACE") && std::stoi(std::getenv("BMH_TRACE")) == minute;
            MinuteReport rep;
            try {
                rep = engine.minute(minute, last, stoppage);
            } catch (const std::exception& ex) {
                rep.fail(std::string("exception: ") + ex.what());
            }
            chain.minute = rep.unsupported ? -1 : minute;
            if (!rep.unsupported) {
                chain.groups = groups;
                chain.state = gs;
                chain.game = game;
                chain.rand = rnd;
            }
            if (rep.unsupported) {
                skipped++;
                std::printf("  minute %2d: not checkable (%s)\n", minute, rep.reason.c_str());
                continue;
            }
            bool seeds = game.seed() == int32_t(std::stoll(field(b, "game_seed"))) &&
                         rnd.seed() == uint32_t(std::stoull(field(b, "rand_seed")));
            // A late snapshot of a stoppage minute: the recorded Watcom seed may be one draw
            // (the first end-of-match roll) ahead.
            bool late = false;
            if (!seeds && next > last && game.seed() == int32_t(std::stoll(field(b, "game_seed")))) {
                WatcomRandom probe = rnd;
                probe.range(0, 1);
                late = seeds = probe.seed() == uint32_t(std::stoull(field(b, "rand_seed")));
            }
            // End minutes of this phase up to the simulated minute. The recorder reads the
            // groups after the seeds while the game runs on, so later entries may already
            // belong to the next minute; they are left out on both sides.
            auto end_key = [&](int e) {
                char h[3];
                std::snprintf(h, 3, "%02x", e >= phase_first && e <= minute ? e : 0);
                return std::string(h);
            };
            std::string ours;
            for (auto& g : groups) {
                for (uint8_t r : g.results) { char h[3]; std::snprintf(h, 3, "%02x", r); ours += h; }
                for (uint8_t e : g.end_minute) ours += end_key(e);
                if (cup_recorded)
                    for (uint16_t r : g.cup_results) { char h[5]; std::snprintf(h, 5, "%02x%02x", r & 0xFF, r >> 8); ours += h; }
            }
            std::string theirs;
            size_t q = b.find("\"groups\":[");
            while ((q = b.find("{\"type\":", q)) != std::string::npos) {
                theirs += group_field(b, "results", q);
                for (uint8_t e : unhex(group_field(b, "end_minutes", q))) theirs += end_key(e);
                if (cup_recorded) theirs += group_field(b, "cup0", q);
                q++;
            }
            bool results = ours == theirs;
            if (seeds && results) ok++; else bad++;
            std::printf("  minute %2d: %s  game rng calls %llu%s%s%s\n", minute, seeds && results ? "OK      " : "MISMATCH",
                        (unsigned long long)game.calls(), results ? "" : "  (results differ)", chained ? "  (chained)" : "",
                        late ? "  (snapshot 1 draw late)" : "");
            if (!seeds) {
                // The recorder may read the seeds a little late (the game runs on); find out
                // whether the recorded Watcom seed is a few draws ahead of ours.
                WatcomRandom probe = rnd;
                int ahead = -1;
                for (int k = 1; k <= 64 && ahead < 0; k++) {
                    probe.range(0, 1);
                    if (probe.seed() == uint32_t(std::stoull(field(b, "rand_seed")))) ahead = k;
                }
                bool game_ok = game.seed() == int32_t(std::stoll(field(b, "game_seed")));
                if (ahead > 0 && game_ok) std::printf("      recorded Watcom seed is %d draws ahead (late snapshot)\n", ahead);
                else std::printf("      seeds differ (game %s)\n", game_ok ? "same" : "differs");
                // ... or ours ahead of the recording (we drew too much)
                WatcomRandom back(uint32_t(std::stoull(field(b, "rand_seed"))));
                for (int k = 1; k <= 64; k++) {
                    back.range(0, 1);
                    if (back.seed() == rnd.seed()) {
                        std::printf("      ported Watcom seed is %d draws ahead of the recording\n", k);
                        break;
                    }
                }
                if (ahead > 0 && !game_ok) std::printf("      recorded Watcom seed is %d draws ahead\n", ahead);
            }
            if ((!seeds || !results) && !minutes[i + 1].second.empty()) {
                // Which tables differ from the state recorded at the next minute?
                GameState o = state_from_dump(minutes[i + 1].second);
                int shown = 0;
                for (size_t k = 0; k < gs.players.size() && k < o.players.size() && shown < 6; k++) {
                    const Player &x = gs.players[k], &y = o.players[k];
                    if (x.w35c != y.w35c || x.w368 != y.w368 || x.w364 != y.w364) {
                        std::printf("      player %zu: ported %08x %08x %08x, original %08x %08x %08x\n", k, x.w35c, x.w368,
                                    x.w364, y.w35c, y.w368, y.w364);
                        shown++;
                    }
                }
                for (size_t k = 0; k < gs.squad_slots.size() && k < o.squad_slots.size() && shown < 12; k++)
                    if (gs.squad_slots[k] != o.squad_slots[k]) {
                        std::printf("      slot %zu (team ~%zu): ported %04x, original %04x\n", k, k / 24, gs.squad_slots[k], o.squad_slots[k]);
                        shown++;
                    }
                for (size_t k = 0; k < gs.team_flags.size() && k < o.team_flags.size(); k++)
                    if (gs.team_flags[k] != o.team_flags[k])
                        std::printf("      team_flags %zu: ported %04x, original %04x\n", k, gs.team_flags[k], o.team_flags[k]);
            }
            if (!results) {
                std::printf("      ported  : %s\n      original: %s\n", ours.c_str(), theirs.c_str());
                // name the first differing league match (cup results follow the league bytes)
                size_t at = 0, diff = 0;
                while (diff < ours.size() && diff < theirs.size() && ours[diff] == theirs[diff]) diff++;
                for (size_t gi = 0; gi < groups.size(); gi++) {
                    size_t len = groups[gi].results.size() * 2;
                    if (diff < at + len) {
                        int mi = int((diff - at) / 2);
                        int home = groups[gi].fixtures.at(mi * 2), away = groups[gi].fixtures.at(mi * 2 + 1);
                        std::printf("      first difference: group %zu match %d, teams %d-%d%s\n", gi, mi, home, away,
                                    gs.manager_of(home) >= 0 || gs.manager_of(away) >= 0 ? " (human manager)" : "");
                        break;
                    }
                    at += len + groups[gi].end_minute.size() * 2 + (cup_recorded ? groups[gi].cup_results.size() * 4 : 0);
                }
            }
        }
        std::printf("%d minutes exact, %d mismatched, %d not checkable\n", ok, bad, skipped);
    }
    return exact == checked && full_exact == full_checked ? 0 : 2;
}
