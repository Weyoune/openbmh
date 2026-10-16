#include "test_inputs.hpp"
// SPDX-License-Identifier: GPL-3.0-only
// Soak test of the console language (proposal "Dauertest"): 1-4 human managers play many
// seasons through bmh::cli::console::Console in instant mode. At every desk the driver picks
// random commands from what ListOptions offers (random but legal arguments: integer ranges,
// enum choices, booleans, ids/rows seen in earlier output), answers every question with a
// random option, and after each kickoff checks invariants of the game state:
//   - no "! E_INTERNAL" / "! E_IO" line (a rule refusal like E_RULE is a normal answer)
//   - every player id appears in at most one squad slot, ids are inside the player table
//   - every league table adds up: goals for = goals against, wins = losses, draws even,
//     points = 3W + D (a human club may have fewer: the point deduction),
//     games played differ by at most six (postponed games are replayed later), and at the end
//     of the season every club has played 2 * (clubs - 1) games
//   - at every season change: Save, then Load + Save in a fresh console gives the same
//     decrypted save stream
// At each season change it prints one line per manager: club, league, final rank, balance.
//
// Usage: cli_soak <BMH data> <work dir> [seasons=1] [managers=2] [seed=1] [actions=4]
// The CTest entry runs a short variant; long runs are started by hand.

#include "cli/console/console.hpp"
#include "formats/fbm.hpp"
#include "game/state.hpp"
#include "ui/league_table.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <map>
#include <random>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "game/schedule.hpp"
#include "game/trace.hpp"

using namespace bmh;
using bmh::cli::console::Console;
using bmh::cli::console::Options;
using bmh::cli::console::TimeMode;

namespace {

int failures = 0;
void fail(const std::string& what) {
    ++failures;
    std::cerr << "SOAK FAIL: " << what << "\n";
}

bool starts(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

// Commands the random driver never calls: they end the session, replace the game, or touch
// files (the save roundtrip runs on its own schedule).
const std::set<std::string> kSkipped = {
    "Quit", "PullPlug", "Load", "Save", "LoadGame", "SaveGame", "NewGame", "Kickoff", "Help",
    "ListOptions", "ChatLog", "Delete", "Key", "Open", "GoBack", "Answer", "Run", "Step"};
// Windows the driver leaves at once (files and the start screens).
const std::set<std::string> kLeave = {"LoadGame", "SaveGame", "NewGame", "NewGame.LoadGame"};

struct Option {
    std::string name;
    std::vector<std::string> params;  // "price:8..25", "letter:enum(A,B)", "[on:bool]", ...
};

// "> Name(p1, p2)   - description" -> Option. Lines starting with ">    " (indented) are the
// Open() menu lines and are skipped.
bool parse_option(const std::string& line, Option& out) {
    if (!starts(line, "> ") || starts(line, ">  ")) return false;
    const size_t open = line.find('(');
    if (open == std::string::npos) return false;
    out.name = line.substr(2, open - 2);
    out.params.clear();
    int depth = 0;
    std::string cur;
    size_t i = open + 1;
    for (; i < line.size(); ++i) {
        const char c = line[i];
        if ((c == '(' || c == '[') ) ++depth;
        if (c == ')' && depth == 0) break;
        if ((c == ')' || c == ']')) --depth;
        if (c == ',' && depth == 0) {
            out.params.push_back(cur);
            cur.clear();
            continue;
        }
        if (!(cur.empty() && c == ' ')) cur += c;
    }
    if (!cur.empty()) out.params.push_back(cur);
    return true;
}

class Driver {
public:
    Driver(const std::filesystem::path& data, const std::filesystem::path& dir, uint32_t seed, int managers, int actions)
        : data_(data), dir_(dir), rng_(seed), managers_(managers), actions_(actions) {
        Options o;
        o.data = data;
        o.save_directory = dir;
        o.seed = seed;
        o.time = TimeMode::instant;
        console_ = std::make_unique<Console>(o, [this](const std::string& l) { out_.push_back(l); });
        console_->start();
    }

    // Feeds one block, answers questions randomly, keeps track of window/ids.
    std::vector<std::string> block(const std::vector<std::string>& lines, bool answer = true) {
        std::vector<std::string> all;
        std::vector<std::string> input = lines;
        for (int guard = 0; guard < 200; ++guard) {
            out_.clear();
            for (const auto& l : input) console_->feed(l);
            console_->feed("go.");
            all.insert(all.end(), out_.begin(), out_.end());
            scan(out_, input);
            check_pay(input);
            input.clear();
            // an open question: answer with a random option
            std::string q;
            for (const auto& l : out_)
                if (starts(l, "? Q")) q = l;
                else if (starts(l, "+ ") || starts(l, "! ")) {}  // keep the last question
            if (q.empty() || !answer || console_->ended()) break;
            const std::string id = q.substr(3, q.find(' ', 3) - 3);
            std::vector<std::string> opts;
            const size_t at = q.find("options: ");
            if (at != std::string::npos) {
                std::string rest = q.substr(at + 9);
                size_t p = 0;
                while (p <= rest.size()) {
                    size_t bar = rest.find(" | ", p);
                    opts.push_back(rest.substr(p, bar == std::string::npos ? std::string::npos : bar - p));
                    if (bar == std::string::npos) break;
                    p = bar + 3;
                }
            }
            if (opts.empty() || pick(5) == 0) input = {"GoBack"};
            else input = {"Answer(" + id + ", " + quote_if_needed(opts[pick(int(opts.size()))]) + ")"};
        }
        return all;
    }

    void new_game() {
        static const char* strength[] = {"Upper", "Middle", "Lower"};
        static const char* leagues[] = {bmh::test::expected_text("cli_soak-external-1").c_str(), bmh::test::expected_text("cli_soak-external-1").c_str(), "SecondLeague"};
        block({"NewGame(5, 3, " + std::to_string(managers_) + ", 3, true, false)"}, false);
        // clubs by number (any version): the first 16 clubs of the database are league clubs in
        // the German (18 per league) and the English (20) versions
        std::vector<int> order(16);
        for (int i = 0; i < 16; ++i) order[size_t(i)] = i;
        std::shuffle(order.begin(), order.end(), rng_);
        const int portrait = pick(12);  // portraits must differ between the managers
        for (int m = 0; m < managers_; ++m) {
            const int c = order[size_t(m)];
            const std::string league = leagues[pick(3)];
            block({"SelectTeam(M" + std::to_string(m + 1) + ", club:" + std::to_string(c) + ", " + league + ", " + strength[pick(3)] + ", " +
                   std::to_string((portrait + 3 * m) % 12) + ")"}, false);
            for (const auto& l : out_)
                if (starts(l, "! ")) std::cerr << "setup: " << l << "\n";
            for (const auto& l : block({"Answer(" + std::to_string(m + 1) + ", Yes)"}, false))
                if (starts(l, "! ")) std::cerr << "setup: " << l << "\n";
        }
        // the start info windows end at the desk
        for (int i = 0; i < 10 && window_ != "Desktop"; ++i) block({"GoBack"});
        if (window_ != "Desktop") {
            fail("the new game did not reach the desk (window " + window_ + ")");
            std::exit(1);
        }
        playing_ = true;
        check_pay({"(game start)"});
    }

    // One desk turn: random actions, back to the desk, Kickoff.
    void turn() {
        to_desk();
        const int n = pick(actions_ + 1);
        for (int i = 0; i < n; ++i) random_action();
        to_desk();
        const auto status = block({"Status"});
        for (const auto& l : status) {
            std::smatch m;
            static const std::regex re(R"(^\+ (\S+) \| (.+) \| (\d{4}-\d{2}-\d{2}) \| balance (-?\d+))");
            if (std::regex_search(l, m, re)) balance_[m[1]] = std::stoll(m[4]);
        }
        if (getenv("SOAK_TRACE")) std::cerr << "turn " << kickoffs_ << " date " << console_->session().state().date << " window " << window_ << std::endl;
        block({"Kickoff"});
        ++kickoffs_;
    }

    bool ended() const { return console_->ended(); }
    const GameState& state() const { return console_->session().state(); }
    std::map<std::string, long long>& balances() { return balance_; }
    int kickoffs() const { return kickoffs_; }
    int actions_done() const { return actions_done_; }
    const std::map<std::string, int>& errors() const { return errors_; }
    Console& console() { return *console_; }

private:
    int pick(int n) { return n <= 1 ? 0 : int(std::uniform_int_distribution<int>(0, n - 1)(rng_)); }
    static std::string quote_if_needed(const std::string& s) {
        if (s.find_first_of(" ,()") == std::string::npos) return s;
        return "\"" + s + "\"";
    }

    void scan(const std::vector<std::string>& lines, const std::vector<std::string>& input) {
        static const std::regex token(R"(\b(id|row|club|player|game|msg|slot):(\d+))");
        for (const auto& i : input) history_.push_back("<< " + i);
        for (const auto& l : lines) history_.push_back(l.size() > 160 ? l.substr(0, 160) : l);
        if (history_.size() > 400) history_.erase(history_.begin(), history_.end() - 400);
        for (const auto& l : lines) {
            if (starts(l, "# Window: ")) window_ = l.substr(10);
            if (starts(l, "! ")) {
                const std::string code = l.substr(2, l.find_first_of(" :", 2) - 2);
                ++errors_[code];
                if (code == "E_INTERNAL" || code == "E_IO") {
                    std::string in;
                    for (const auto& i : input) in += i + "; ";
                    fail("in window " + window_ + " after [" + in + "]: " + l);
                    if (!dumped_) {
                        dumped_ = true;
                        for (const auto& h : history_) std::cerr << "    " << h << "\n";
                    }
                }
            }
            for (std::sregex_iterator it(l.begin(), l.end(), token), end; it != end; ++it) {
                auto& pool = tokens_[(*it)[1]];
                if (pool.size() < 64) pool.push_back((*it)[0]);
                else pool[size_t(pick(64))] = (*it)[0];
            }
        }
    }

    // Every occupied squad row of a human club has a base pay on the tactics board (+0x50):
    // team_rating divides by it. Checked after each block to name the command that broke it.
    void check_pay(const std::vector<std::string>& input) {
        if (!playing_) return;
        const GameState& g = console_->session().state();
        for (size_t m = 0; m < g.manager_teams.size(); ++m) {
            const int team = g.manager_teams[m];
            if (team < 0 || team >= 138 || g.tactics.size() < (m + 1) * 0x840) continue;
            const SquadRange r = squad_range(team);
            for (size_t row = 0; row < r.count && row < 24; ++row) {
                if ((g.squad_slots[r.first + row] & 0xFFF) == 0xFFF) continue;
                const size_t at = m * 0x840 + row * 0x58 + 0x50;
                const uint32_t base = g.tactics[at] | g.tactics[at + 1] << 8 | g.tactics[at + 2] << 16 | uint32_t(g.tactics[at + 3]) << 24;
                const std::string key = std::to_string(m) + "/" + std::to_string(row);
                if (base == 0 && !zero_pay_.count(key)) {
                    zero_pay_.insert(key);
                    std::string in;
                    for (const auto& i : input) in += i + "; ";
                    fail("manager " + std::to_string(m + 1) + " row " + std::to_string(row) + " has no base pay after [" + in + "] in " + window_);
                    if (!dumped_) {
                        dumped_ = true;
                        for (const auto& h : history_) std::cerr << "    " << h << "\n";
                    }
                }
                if (base != 0) zero_pay_.erase(key);
            }
        }
    }

    void to_desk() {
        for (int i = 0; i < 12 && window_ != "Desktop" && !console_->ended(); ++i) block({"GoBack"});
        if (window_ != "Desktop") {
            fail("cannot return to the desk from " + window_);
            if (!dumped_) {
                dumped_ = true;
                for (const auto& l : history_) std::cerr << "    " << l << "\n";
            }
        }
    }

    std::string argument(const std::string& param) {
        std::string p = param;
        const bool optional = !p.empty() && p.front() == '[';
        if (optional) p = p.substr(1, p.size() - 2);
        const size_t colon = p.find(':');
        const std::string type = colon == std::string::npos ? p : p.substr(colon + 1);
        std::smatch m;
        static const std::regex range(R"(^(-?\d+)\.\.(-?\d+)$)");
        if (std::regex_match(type, m, range)) {
            const long long lo = std::stoll(m[1]), hi = std::stoll(m[2]);
            // money ranges are wide: mostly small values, sometimes the edges
            const int r = pick(10);
            if (r == 0) return std::to_string(lo);
            if (r == 1) return std::to_string(hi);
            const long long span = std::min<long long>(hi - lo, r < 6 ? 30 : 2000000);
            return std::to_string(lo + std::uniform_int_distribution<long long>(0, std::max(0LL, span))(rng_));
        }
        if (starts(type, "enum(")) {
            std::vector<std::string> c;
            std::string body = type.substr(5, type.size() - 6), cur;
            for (char ch : body) {
                if (ch == ',' || ch == '|') { c.push_back(cur); cur.clear(); }
                else cur += ch;
            }
            c.push_back(cur);
            return c[size_t(pick(int(c.size())))];
        }
        if (type == "bool") return pick(2) ? "true" : "false";
        if (type == "int") return std::to_string(pick(30));
        // text / reference: an id or row seen before, a club, or a short word
        const std::string name = colon == std::string::npos ? p : p.substr(0, colon);
        for (const char* kind : {"id", "row", "club", "player", "game", "msg", "slot"}) {
            if (name.find(kind) == std::string::npos) continue;
            auto it = tokens_.find(kind);
            if (it != tokens_.end() && !it->second.empty() && pick(4) != 0) return it->second[size_t(pick(int(it->second.size())))];
            if (std::string(kind) == "row") return "row:" + std::to_string(pick(24));
            if (std::string(kind) == "club") return "club:" + std::to_string(pick(138));
        }
        std::vector<std::string> pool;
        for (auto& [k, v] : tokens_) pool.insert(pool.end(), v.begin(), v.end());
        if (!pool.empty() && pick(2)) return pool[size_t(pick(int(pool.size())))];
        static const char* words[] = {"\"soak\"", "1", "row:0", "club:0", "\"x\""};
        return words[pick(5)];
    }

    // Opens a random window (or stays) and calls a few random commands there.
    void random_action() {
        if (window_ == "Desktop") {
            // menus 0..29 without the file windows
            int menu;
            do menu = pick(30); while (menu == 10 || menu == 11 || menu == 12);
            block({pick(4) == 0 ? "Key(" + std::string(1, char('A' + pick(26))) + ")" : "Open(" + std::to_string(menu) + ")"});
            if (kLeave.count(window_)) { block({"GoBack"}); return; }
        }
        for (int k = 0; k < 1 + pick(5); ++k) {
            const auto listed = block({"ListOptions"});
            std::vector<Option> opts;
            for (const auto& l : listed) {
                Option o;
                if (parse_option(l, o) && !kSkipped.count(o.name)) opts.push_back(o);
            }
            if (opts.empty()) break;
            const Option& o = opts[size_t(pick(int(opts.size())))];
            std::string call = o.name + "(";
            bool first = true;
            for (const auto& p : o.params) {
                if (p.front() == '[' && pick(2)) continue;
                call += (first ? "" : ", ") + argument(p);
                first = false;
            }
            call += ")";
            block({call});
            ++actions_done_;
            if (kLeave.count(window_)) { block({"GoBack"}); break; }
            if (window_ == "Desktop" || window_ == "Match") break;
        }
    }

    std::filesystem::path data_, dir_;
    std::mt19937 rng_;
    int managers_, actions_;
    std::unique_ptr<Console> console_;
    std::vector<std::string> out_;
    std::string window_;
    std::map<std::string, std::vector<std::string>> tokens_;
    std::map<std::string, long long> balance_;
    std::map<std::string, int> errors_;
    int kickoffs_ = 0, actions_done_ = 0;
    std::vector<std::string> history_;  // the last input and output lines, for failure reports
    bool dumped_ = false;
    std::set<std::string> zero_pay_;
    bool playing_ = false;  // the game runs (the board checks start)
};

// The invariants of the game state after a kickoff.
void check_state(const GameState& g, const std::string& when) {
    std::map<int, int> seen;  // player -> slot index
    for (int team = 0; team < 138; ++team) {
        const SquadRange r = squad_range(team);
        for (size_t i = r.first; i < r.first + r.count && i < g.squad_slots.size(); ++i) {
            const int id = g.squad_slots[i] & 0xFFF;
            if (id == 0xFFF) continue;
            if (size_t(id) >= g.players.size()) fail(when + ": team " + std::to_string(team) + " has player id " + std::to_string(id) + " outside the table");
            auto [it, inserted] = seen.emplace(id, team);
            if (!inserted) fail(when + ": player " + std::to_string(id) + " in team " + std::to_string(it->second) + " and team " + std::to_string(team));
        }
    }
    const auto tables = ui::league_tables_from_game(g, 0);
    for (const auto& t : tables) {
        if (t.rows.empty()) continue;
        int gf = 0, ga = 0, w = 0, l = 0, d = 0, lo = 1 << 30, hi = 0;
        for (const auto& r : t.rows) {
            gf += r.goals_for; ga += r.goals_against; w += r.wins; l += r.losses; d += r.draws;
            lo = std::min(lo, r.games); hi = std::max(hi, r.games);
            if (r.wins + r.draws + r.losses != r.games) fail(when + ": " + t.league_name + " " + r.name + " W+D+L != games");
            // a human club may carry a point deduction (manager record +0x220)
            const bool human = std::find(g.manager_teams.begin(), g.manager_teams.end(), r.runtime_team_id) != g.manager_teams.end();
            if (r.points > r.wins * 3 + r.draws || (!human && r.points != r.wins * 3 + r.draws))
                fail(when + ": " + t.league_name + " " + r.name + " has " + std::to_string(r.points) + " points for " + std::to_string(r.wins) + "W " + std::to_string(r.draws) + "D");
        }
        if (gf != ga) fail(when + ": " + t.league_name + " goals for " + std::to_string(gf) + " != against " + std::to_string(ga));
        if (w != l || d % 2) fail(when + ": " + t.league_name + " wins/losses/draws do not pair up");
        // postponed games (weather, cup) are replayed later, so the counts may drift apart
        if (hi - lo > 6) fail(when + ": " + t.league_name + " games played range " + std::to_string(lo) + ".." + std::to_string(hi));
    }
}

struct Standing {
    std::string league;
    int rank = 0, size = 0, pts = 0, gf = 0, ga = 0;
    std::string club;
};

std::map<int, Standing> standings(const GameState& g) {
    std::map<int, Standing> s;
    const auto tables = ui::league_tables_from_game(g, 0);
    for (size_t m = 0; m < g.manager_teams.size(); ++m)
        for (const auto& t : tables)
            for (const auto& r : t.rows)
                if (r.runtime_team_id == g.manager_teams[m])
                    s[int(m)] = {t.league_name, r.rank, int(t.rows.size()), r.points, r.goals_for, r.goals_against, r.name};
    return s;
}

// Save, Load in a fresh console, Save again: the decrypted streams must agree.
void roundtrip(Driver& d, const std::filesystem::path& data, const std::filesystem::path& dir, int season) {
    const std::string a = "SOAK" + std::to_string(season), b = "SOAKB" + std::to_string(season);
    d.block({"Open(SaveGame)", "Save(" + a + ", true)"});
    d.block({"GoBack"});
    Options o;
    o.data = data;
    o.save_directory = dir;
    o.time = TimeMode::instant;
    std::vector<std::string> lines;
    Console second(o, [&](const std::string& l) { lines.push_back(l); });
    second.start();
    for (const std::string& l : std::vector<std::string>{"LoadGame(" + a + ")", "go.", "Open(SaveGame)", "Save(" + b + ", true)", "go."}) second.feed(l);
    for (const auto& l : lines)
        if (starts(l, "! ")) fail("roundtrip season " + std::to_string(season) + ": " + l);
    try {
        const Savegame x = load_savegame(dir / (a + ".FBM")), y = load_savegame(dir / (b + ".FBM"));
        if (x.plain_header != y.plain_header || x.stream != y.stream) fail("roundtrip season " + std::to_string(season) + ": Load + Save changes the game");
    } catch (const std::exception& e) {
        fail("roundtrip season " + std::to_string(season) + ": " + e.what());
    }
}

}  // namespace

int run(int argc, char** argv);

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cout << std::flush;
        std::cerr << "SOAK FAIL: exception: " << e.what() << "\n";
        return 1;
    }
}

int run(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: cli_soak <BMH data> <work dir> [seasons] [managers] [seed] [actions]\n";
        return 2;
    }
    const std::filesystem::path data = argv[1], dir = argv[2];
    const int seasons = argc > 3 ? std::atoi(argv[3]) : 1;
    const int managers = argc > 4 ? std::clamp(std::atoi(argv[4]), 1, 4) : 2;
    const uint32_t seed = argc > 5 ? uint32_t(std::strtoul(argv[5], nullptr, 10)) : 1;
    const int actions = argc > 6 ? std::atoi(argv[6]) : 4;
    std::filesystem::create_directories(dir);
    const auto t0 = std::chrono::steady_clock::now();

    Driver d(data, dir, seed, managers, actions);
    // a turn that takes minutes is a hang: name the engine step it is in
    std::thread([] {
        const char* last = nullptr;
        int same = 0;
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            const char* now = bmh::turn_step.load();
            same = now == last ? same + 1 : 0;
            last = now;
            if (same == 6) std::cerr << "SOAK FAIL: no progress for 30 s in step " << now << std::endl;
        }
    }).detach();
    if (getenv("SOAK_TRACE")) std::cerr << "new game" << std::endl;
    d.new_game();
    if (getenv("SOAK_TRACE")) std::cerr << "at the desk" << std::endl;
    const GameState& g = d.state();
    int season = 0;
    uint32_t start = g.season_start;
    std::map<int, Standing> last = standings(g);
    auto final_tables = ui::league_tables_from_game(g, 0);  // the tables before the last kickoff
    std::cout << "soak: seed " << seed << ", " << managers << " managers, " << seasons << " seasons\n";
    for (int guard = 0; season < seasons && guard < seasons * 200 * managers && !d.ended(); ++guard) {
        d.turn();
        if (d.ended()) break;
        check_state(g, "season " + std::to_string(season + 1) + " day " + std::to_string(g.season_day()));
        if (g.season_start != start) {
            ++season;
            std::cout << "season " << season << ":\n";
            for (auto& [m, s] : last) {
                const std::string name = "M" + std::to_string(m + 1);
                std::cout << "  " << name << " " << s.club << ": " << s.league << " rank " << s.rank << "/" << s.size << ", "
                          << s.pts << " pts, " << s.gf << ":" << s.ga << ", balance " << d.balances()[name] << "\n";
            }
            for (const auto& t : final_tables) {
                // (a league without calendar dates is not played: the English fifth league)
                if (t.league_id >= 0 && t.league_id < bmh::kLeagueCount && bmh::kLeagueBit[size_t(t.league_id)] > 7) continue;
                for (const auto& r : t.rows)
                    if (r.games != 2 * (int(t.rows.size()) - 1))
                        fail("season " + std::to_string(season) + ": " + t.league_name + " " + r.name + " ended with " + std::to_string(r.games) + " games");
            }
            start = g.season_start;
            roundtrip(d, data, dir, season);
        }
        // (during a Bundestrainer take-over slot 0 holds the national team: keep the last
        // known standing of a manager whose club is not in a table)
        for (auto& [m, s] : standings(g)) last[m] = s;
        final_tables = ui::league_tables_from_game(g, 0);
        if (failures > 50) break;
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "soak: " << season << " seasons, " << d.kickoffs() << " kickoffs, " << d.actions_done() << " random commands, "
              << int(secs) << " s" << (d.ended() ? " (session ended)" : "") << "\n  errors:";
    for (const auto& [code, n] : d.errors()) std::cout << " " << code << "=" << n;
    std::cout << "\n";
    if (season < seasons && !d.ended()) fail("only " + std::to_string(season) + " seasons played");
    std::cout << (failures ? "SOAK FAILED: " + std::to_string(failures) : std::string("soak passed")) << "\n";
    return failures ? 1 : 0;
}
