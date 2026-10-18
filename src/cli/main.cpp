// SPDX-License-Identifier: GPL-3.0-only
#include "cli/protocol.hpp"
#include "cli/console/console.hpp"
#include "cli/seat_hub.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdio>
#include <cstdlib>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cctype>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "launch/archive.hpp"
#include "launch/game_data.hpp"
#include "content/package.hpp"
#include "launch/process.hpp"
#include "launch/mods.hpp"
#include "version.hpp"

namespace {
using namespace bmh::cli;
using Object = Json::Object;

struct Arguments {
    std::filesystem::path data, log, replay;
    std::filesystem::path saves;  // --saves: save directory of the console (default --data)
    std::uint32_t seed = 1;
    bool seed_set = false, json = false, help = false;
    bool console = false;  // --console: the console language of docs/CLI_CONCEPT
    bool echo = false;     // --echo: repeat each input line as "< line" (golden transcript format)
    bool instant = false;  // --time instant: matchdays run through (default stepped, 07-spieltag 3.2)
    // --serve <dir>: the console session answers blocks dropped into a directory (agents);
    // --send <dir>: hand one block to such a session and print its answer
    std::filesystem::path serve, send;
    int timeout_seconds = 300;  // --timeout: how long --send waits for the answer
    // --keep-going: a console replay reports differing blocks and goes on (re-checking a
    // QA session after console texts changed)
    bool keep_going = false;
    int seats = 0;  // --seats N: with --serve, one mailbox per human manager (several agents)
    int listen_port = 0;  // --listen <port>: the console on 127.0.0.1:<port> (front ends, stage 2.5)
    // --resume <jsonl>: a --seats game starts by playing the blocks of an earlier session log
    // again (seed, time mode and saves from its header) and goes on from there
    std::filesystem::path resume;
};

std::uint32_t parse_seed(std::string_view value) {
    int base = 10;
    if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) { base = 16; value.remove_prefix(2); }
    std::uint32_t seed{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), seed, base);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) throw std::runtime_error("invalid --seed");
    return seed;
}

Arguments parse_args(int argc, char** argv) {
    Arguments args;
    const std::vector<std::string> argv8 = bmh::launch::utf8_arguments(argc, argv);  // (names beyond the code page)
    for (size_t i = 1; i < argv8.size(); ++i) {
        const std::string_view arg(argv8[i]);
        auto next = [&](std::string_view name) -> std::string {
            if (i + 1 >= argv8.size()) throw std::runtime_error("missing value for " + std::string(name));
            return argv8[++i];
        };
        if (arg == "--version") {
            std::printf("OpenBMH console %s\n", bmh::kOpenBmhVersion);
            std::exit(0);
        }
        if (arg == "--help" || arg == "-h") args.help = true;
        else if (arg == "--data" || arg == "--mod")
            args.data = bmh::launch::path_from_utf8(next(arg));
        else if (arg == "--seed") { args.seed = parse_seed(next(arg)); args.seed_set = true; }
        else if (arg == "--text") args.json = false;
        else if (arg == "--json") args.json = true;
        else if (arg == "--log") args.log = bmh::launch::path_from_utf8(next(arg));
        else if (arg == "--replay") args.replay = bmh::launch::path_from_utf8(next(arg));
        else if (arg == "--console") args.console = true;
        else if (arg == "--saves") args.saves = bmh::launch::path_from_utf8(next(arg));
        else if (arg == "--echo") args.echo = true;
        else if (arg == "--keep-going") args.keep_going = true;
        else if (arg == "--resume") args.resume = bmh::launch::path_from_utf8(next(arg));
        else if (arg == "--listen") {
            args.listen_port = std::stoi(next(arg));
            if (args.listen_port < 1 || args.listen_port > 65535) throw std::runtime_error("--listen is a port 1-65535");
        }
        else if (arg == "--seats") {
            args.seats = std::stoi(next(arg));
            if (args.seats < 1 || args.seats > 4) throw std::runtime_error("--seats is 1-4");
        }
        else if (arg == "--serve") args.serve = bmh::launch::path_from_utf8(next(arg));
        else if (arg == "--send") args.send = bmh::launch::path_from_utf8(next(arg));
        else if (arg == "--timeout") args.timeout_seconds = std::stoi(next(arg));
        else if (arg == "--time") {
            const std::string mode = next(arg);
            if (mode != "stepped" && mode != "instant") throw std::runtime_error("--time is stepped or instant");
            args.instant = mode == "instant";
        }
        else throw std::runtime_error("unknown argument: " + std::string(arg));
    }
    return args;
}

void print_usage() {
    std::cout << "openbmh_cli --data <BMH directory> [--seed <n>] [--text|--json] [--log <jsonl>]\n"
                 "openbmh_cli --replay <jsonl> [--data <BMH directory>] [--text|--json]\n"
                 "openbmh_cli --console --data <BMH directory> [--saves <dir>] [--seed <n>] [--text|--json] [--echo]\n"
                 "            [--log|--replay <jsonl> [--keep-going]] [--serve <dir> | --listen <port>] [--seats N]\n"
                 "  --listen: the console on 127.0.0.1:<port>, line protocol (SEAT k with --seats; a block ends\n"
                 "            with go., the answer with a line \".\"); --json gives structured lines, State(view) projections.\n"
                 "  --seats N: one mailbox <dir>/seat<k> per manager; a request \"WaitTurn\" waits for k's turn.\n"
                 "  --resume <jsonl>: with --seats, first play an earlier session log again, then go on.\n"
                 "openbmh_cli --send <dir> [--timeout <seconds>]   (the block on stdin)\n"
                 "  --serve: the session answers blocks dropped into <dir> (request-N.txt -> response-N.txt);\n"
                 "  --send hands one block to it and prints the answer - for agents that work in steps.\n"
                 "  console language (docs/CLI_CONCEPT): command lines, executed by a line \"go.\";\n"
                 "  ListOptions shows what is possible, Help the reference, Quit ends.\n"
                 "Commands: help, new, load, save, status, desk, kickoff, camp.*, bank.*, stadium.*, toto.submit, quit\n"
                 "Text: new <master_club> <manager_name> [strength], load/save <path>, kickoff, status, desk, quit\n";
}

std::string command_line_response(const Json& response) {
    if (const Json* help = response.object() ? [&]() -> const Json* {
        const auto it = response.object()->find("help"); return it == response.object()->end() ? nullptr : &it->second;
    }() : nullptr) {
        std::string result = "Commands (JSON: one object per line):\n";
        if (const auto* obj = help->object()) {
            if (const auto it = obj->find("commands"); it != obj->end()) {
                if (const auto* list = it->second.array()) for (const auto& command : *list) {
                    const auto c = command.object()->find("cmd");
                    const auto p = command.object()->find("params");
                    if (c != command.object()->end() && p != command.object()->end())
                        result += "  " + *c->second.string() + " — " + *p->second.string() + "\n";
                }
            }
            if (const auto it = obj->find("not_available"); it != obj->end()) {
                result += "Unavailable until CareerSession adapters exist:";
                if (const auto* list = it->second.array()) for (const auto& value : *list) result += " " + *value.string();
                result += "\n";
            }
        }
        return result;
    }
    return text_response(response);
}

Json protocol_error(std::string detail, std::uint64_t revision) {
    return Json(Object{{"ok", false}, {"error", "invalid_request"}, {"detail", std::move(detail)},
        {"revision", static_cast<std::int64_t>(revision)}});
}

int run_replay(Arguments args) {
    std::ifstream input(args.replay);
    if (!input) throw std::runtime_error("cannot_read_replay_log");
    std::string line;
    if (!std::getline(input, line)) throw std::runtime_error("empty_replay_log");
    const Json header = parse_json(line);
    if (!header.object()) throw std::runtime_error("invalid_replay_header");
    const auto type = header.object()->find("type");
    const auto data = header.object()->find("data");
    const auto seed = header.object()->find("seed");
        if (type == header.object()->end() || !type->second.string() || *type->second.string() != "openbmh_cli_log" ||
            data == header.object()->end() || !data->second.string() || seed == header.object()->end() || !seed->second.integer())
        throw std::runtime_error("invalid_replay_header");
    const auto protocol_version = header.object()->find("protocol");
    if (protocol_version == header.object()->end() || !protocol_version->second.integer() || *protocol_version->second.integer() != 1)
        throw std::runtime_error("unsupported_replay_protocol");
    if (args.data.empty()) args.data = bmh::launch::path_from_utf8(*data->second.string());
    if (args.seed_set && args.seed != static_cast<std::uint32_t>(*seed->second.integer()))
        throw std::runtime_error("replay_seed_mismatch");
    args.seed = static_cast<std::uint32_t>(*seed->second.integer());
    bmh::cli::Protocol protocol(args.data, args.seed);
    std::size_t index = 0;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        ++index;
        const Json record = parse_json(line);
        if (!record.object()) throw std::runtime_error("invalid_replay_record:" + std::to_string(index));
        const auto req = record.object()->find("request"), expected = record.object()->find("response");
        if (req == record.object()->end() || expected == record.object()->end())
            throw std::runtime_error("invalid_replay_record:" + std::to_string(index));
        const Json actual = protocol.handle(req->second, true);
        if (dump_json(actual) != dump_json(expected->second))
            throw std::runtime_error("replay_mismatch_at_record:" + std::to_string(index));
        std::cout << (args.json ? dump_json(actual) : command_line_response(actual)) << '\n';
        const auto quit = actual.object()->find("quit");
        if (quit != actual.object()->end() && quit->second.boolean() && *quit->second.boolean()) break;
    }
    return 0;
}

// --- console mode (docs/CLI_CONCEPT) ------------------------------------------------------
// Log format (JSONL): a header {"type":"openbmh_console_log","data","seed","json","time"}, then one
// entry per executed block {"input":[lines],"output":[lines]}; the first entry is the start
// output with no input. --replay feeds the inputs again (plus "go.") and stops at the first
// block whose output differs - the console draws no random numbers, so a replay is exact.

Json string_array(const std::vector<std::string>& lines) {
    Json::Array a;
    for (const auto& l : lines) a.emplace_back(l);
    return Json(std::move(a));
}

std::vector<std::string> strings_of(const Json& value) {
    std::vector<std::string> result;
    if (const auto* a = value.array())
        for (const auto& v : *a)
            if (const auto* s = v.string()) result.push_back(*s);
    return result;
}

bmh::cli::console::Options console_options(const Arguments& args) {
    bmh::cli::console::Options o;
    o.data = args.data;
    o.save_directory = args.saves;
    o.seed = args.seed;
    o.json = args.json;
    o.time = args.instant ? bmh::cli::console::TimeMode::instant : bmh::cli::console::TimeMode::stepped;
    return o;
}

int run_console_replay(Arguments args) {
    std::ifstream input(args.replay);
    if (!input) throw std::runtime_error("cannot_read_replay_log");
    std::string line;
    if (!std::getline(input, line)) throw std::runtime_error("empty_replay_log");
    const Json header = parse_json(line);
    const auto* h = header.object();
    if (!h || !h->count("type") || !h->at("type").string() || *h->at("type").string() != "openbmh_console_log")
        throw std::runtime_error("invalid_replay_header");
    if (args.data.empty()) args.data = bmh::launch::path_from_utf8(*h->at("data").string());
    if (h->count("saves") && h->at("saves").string() && args.saves.empty()) args.saves = bmh::launch::path_from_utf8(*h->at("saves").string());
    args.seed = static_cast<std::uint32_t>(*h->at("seed").integer());
    args.json = h->count("json") && h->at("json").boolean() && *h->at("json").boolean();
    // the time mode changes how far Kickoff runs; logs before the "time" field need --time
    if (h->count("time") && h->at("time").string()) args.instant = *h->at("time").string() == "instant";

    bmh::cli::console::Console console(console_options(args), [](const std::string& out) { std::cout << out << '\n'; });
    console.start();
    std::size_t index = 0, mismatches = 0;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        const Json record = parse_json(line);
        if (!record.object() || !record.object()->count("output")) throw std::runtime_error("invalid_replay_record:" + std::to_string(index));
        const auto expected = strings_of(record.object()->at("output"));
        if (index > 0) {  // entry 0 is the start output
            for (const auto& in : strings_of(record.object()->at("input"))) console.feed(in);
            console.feed("go.");
        }
        if (console.last_output() != expected) {
            if (!args.keep_going) throw std::runtime_error("replay_mismatch_at_block:" + std::to_string(index));
            std::cerr << "openbmh_cli: block " << index << " differs\n";
            ++mismatches;
        }
        ++index;
    }
    std::cerr << "openbmh_cli: replay " << (mismatches ? "done" : "ok") << ", " << index << " blocks, " << mismatches
              << " differ\n";
    return mismatches ? 1 : 0;
}

int run_console(const Arguments& args) {
    if (args.data.empty()) throw std::runtime_error("--data is required");
    std::ofstream log;
    if (!args.log.empty()) {
        log.open(args.log, std::ios::binary | std::ios::trunc);
        if (!log) throw std::runtime_error("cannot_open_log");
        Object header{{"type", "openbmh_console_log"}, {"version", 1},
                      {"data", bmh::launch::path_to_utf8(std::filesystem::absolute(args.data).lexically_normal())},
                      {"seed", static_cast<std::int64_t>(args.seed)}, {"json", args.json},
                      {"time", std::string(args.instant ? "instant" : "stepped")}};
        if (!args.saves.empty()) header["saves"] = bmh::launch::path_to_utf8(std::filesystem::absolute(args.saves).lexically_normal());
        log << dump_json(Json(std::move(header))) << '\n';
    }
    auto write_entry = [&](const bmh::cli::console::Console& c) {
        if (!log.is_open()) return;
        log << dump_json(Json(Object{{"input", string_array(c.last_block())}, {"output", string_array(c.last_output())}})) << '\n';
        log.flush();
        if (!log) throw std::runtime_error("log_write_failed");
    };
    bmh::cli::console::Console console(console_options(args), [](const std::string& out) { std::cout << out << '\n' << std::flush; });
    console.start();
    write_entry(console);
    std::string line;
    while (!console.ended() && std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (args.echo) std::cout << "< " << line << '\n';
        const bool executes = bmh::cli::console::classify_line(line) == bmh::cli::console::LineKind::go;
        console.feed(line);
        if (executes) write_entry(console);
    }
    return 0;
}

// --- the directory mailbox for agents (--serve / --send) ---------------------------------
// A session started with --serve keeps running; each block is a file request-N.txt (N = 1,
// 2, ...) whose lines are fed to the console (a final "go." is added when missing); the
// output lines go to response-N.txt, written under a temporary name and renamed, so a
// reader never sees half a file. response-0.txt holds the start output. The session ends
// with Quit or a file named "stop". --send writes the next request and waits for its
// response. Plain files work the same on Windows, Linux and macOS and need no port.

std::filesystem::path mailbox_file(const std::filesystem::path& dir, const char* kind, long n) {
    return dir / (std::string(kind) + "-" + std::to_string(n) + ".txt");
}

void write_atomic(const std::filesystem::path& file, const std::vector<std::string>& lines) {
    const std::filesystem::path temp = file.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        for (const auto& l : lines) out << l << "\n";
        if (!out) throw std::runtime_error("cannot write " + temp.string());
    }
    std::filesystem::rename(temp, file);
}

int run_console_serve(const Arguments& args) {
    if (args.data.empty()) throw std::runtime_error("--data is required");
    std::filesystem::create_directories(args.serve);
    std::ofstream log;
    if (!args.log.empty()) {
        log.open(args.log, std::ios::binary | std::ios::trunc);
        if (!log) throw std::runtime_error("cannot_open_log");
        Object header{{"type", "openbmh_console_log"}, {"version", 1},
                      {"data", bmh::launch::path_to_utf8(std::filesystem::absolute(args.data).lexically_normal())},
                      {"seed", static_cast<std::int64_t>(args.seed)}, {"json", args.json},
                      {"time", std::string(args.instant ? "instant" : "stepped")}};
        if (!args.saves.empty()) header["saves"] = bmh::launch::path_to_utf8(std::filesystem::absolute(args.saves).lexically_normal());
        log << dump_json(Json(std::move(header))) << '\n';
    }
    std::vector<std::string> out;
    bmh::cli::console::Console console(console_options(args), [&out](const std::string& line) { out.push_back(line); });
    console.start();
    if (log.is_open())
        log << dump_json(Json(Object{{"input", string_array(console.last_block())}, {"output", string_array(console.last_output())}})) << '\n' << std::flush;
    write_atomic(mailbox_file(args.serve, "response", 0), out);
    for (long n = 1; !console.ended();) {
        std::error_code ec;
        if (std::filesystem::exists(args.serve / "stop", ec)) break;
        const auto request = mailbox_file(args.serve, "request", n);
        if (!std::filesystem::exists(request, ec)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        std::vector<std::string> lines;
        {
            std::ifstream in(request, std::ios::binary);
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(line);
            }
        }
        std::filesystem::remove(request, ec);
        while (!lines.empty() && lines.back().find_first_not_of(" \t") == std::string::npos) lines.pop_back();
        if (lines.empty() || bmh::cli::console::classify_line(lines.back()) != bmh::cli::console::LineKind::go) lines.push_back("go.");
        out.clear();
        for (const auto& l : lines) {
            const bool executes = bmh::cli::console::classify_line(l) == bmh::cli::console::LineKind::go;
            console.feed(l);
            if (executes && log.is_open())
                log << dump_json(Json(Object{{"input", string_array(console.last_block())}, {"output", string_array(console.last_output())}})) << '\n' << std::flush;
            if (console.ended()) break;
        }
        write_atomic(mailbox_file(args.serve, "response", n), out);
        ++n;
    }
    return 0;
}

// --- several agents in one game (--serve <dir> --seats N) ---------------------------------
// Every human manager k gets a mailbox <dir>/seat<k> that works like the single one
// (request-N.txt -> response-N.txt, response-0.txt = start output). What a seat may do and
// what it sees (its own manager only, the meanwhile part, WaitTurn) is the seat hub's
// (src/cli/seat_hub.hpp), shared with --listen. The log holds the blocks as executed (with
// @k), so --replay plays the game again.

namespace {

std::vector<std::string> read_request(const std::filesystem::path& file) {
    std::vector<std::string> lines;
    {
        std::ifstream in(file, std::ios::binary);
        for (std::string line; std::getline(in, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
    }
    std::error_code ec;
    std::filesystem::remove(file, ec);
    while (!lines.empty() && lines.back().find_first_not_of(" \t") == std::string::npos) lines.pop_back();
    return lines;
}

std::string trimmed(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t") - a + 1);
}

}  // namespace

// The session log of a hub game: the header, then every executed block (as --log).
std::function<void(const bmh::cli::console::Console&)> hub_log(std::ofstream& log, const Arguments& args) {
    if (!args.log.empty()) {
        log.open(args.log, std::ios::binary | std::ios::trunc);
        if (!log) throw std::runtime_error("cannot_open_log");
        Object header{{"type", "openbmh_console_log"}, {"version", 1},
                      {"data", bmh::launch::path_to_utf8(std::filesystem::absolute(args.data).lexically_normal())},
                      {"seed", static_cast<std::int64_t>(args.seed)}, {"json", args.json},
                      {"time", std::string(args.instant ? "instant" : "stepped")}, {"seats", args.seats}};
        if (!args.saves.empty()) header["saves"] = bmh::launch::path_to_utf8(std::filesystem::absolute(args.saves).lexically_normal());
        log << dump_json(Json(std::move(header))) << '\n';
    }
    return [&log](const bmh::cli::console::Console& console) {
        if (log.is_open())
            log << dump_json(Json(Object{{"input", string_array(console.last_block())}, {"output", string_array(console.last_output())}}))
                << '\n' << std::flush;
    };
}

// --serve <dir> --seats N: the seat hub (src/cli/seat_hub.hpp) behind one mailbox per seat.
int run_console_seats(Arguments args) {
    // --resume: the earlier log's header decides seed, time mode and saves (as --replay)
    std::ifstream resume;
    if (!args.resume.empty()) {
        resume.open(args.resume, std::ios::binary);
        std::string line;
        if (!resume || !std::getline(resume, line)) throw std::runtime_error("cannot_read_resume_log");
        const Json header = parse_json(line);
        const auto* h = header.object();
        if (!h || !h->count("type") || !h->at("type").string() || *h->at("type").string() != "openbmh_console_log")
            throw std::runtime_error("invalid_resume_header");
        args.seed = static_cast<std::uint32_t>(*h->at("seed").integer());
        if (h->count("time") && h->at("time").string()) args.instant = *h->at("time").string() == "instant";
        if (h->count("saves") && h->at("saves").string() && args.saves.empty()) args.saves = bmh::launch::path_from_utf8(*h->at("saves").string());
        if (args.resume == args.log) throw std::runtime_error("--resume and --log must be different files");
    }
    if (args.data.empty()) throw std::runtime_error("--data is required");
    std::ofstream log;
    bmh::cli::SeatHub hub(console_options(args), args.seats, hub_log(log, args));
    if (resume.is_open()) {
        std::size_t index = 0, differ = 0;
        for (std::string line; std::getline(resume, line);) {
            if (line.empty()) continue;
            const Json record = parse_json(line);
            if (!record.object() || !record.object()->count("output")) throw std::runtime_error("invalid_resume_record");
            if (index++ == 0) continue;  // the start output
            if (!hub.replay(strings_of(record.object()->at("input")), strings_of(record.object()->at("output")))) ++differ;
        }
        std::cerr << "openbmh_cli: resumed " << index << " blocks, " << differ << " differ\n";
    }
    struct Seat {
        std::filesystem::path dir;
        long next = 1;     // the request number expected next
        long waiting = 0;  // a held WaitTurn request (its number), 0 = none
    };
    std::vector<Seat> seats(size_t(args.seats));
    for (size_t k = 0; k < seats.size(); ++k) {
        seats[k].dir = args.serve / ("seat" + std::to_string(k + 1));
        std::filesystem::create_directories(seats[k].dir);
        write_atomic(mailbox_file(seats[k].dir, "response", 0), hub.start_output());
    }
    auto deliver = [&](int seat, long n, const std::vector<std::string>& lines) {
        write_atomic(mailbox_file(seats[size_t(seat - 1)].dir, "response", n), lines);
        std::error_code gone;
        std::filesystem::remove(mailbox_file(seats[size_t(seat - 1)].dir, "request", n), gone);  // (a held WaitTurn)
    };
    auto release = [&] {
        for (auto& [seat, lines] : hub.release()) {
            deliver(seat, seats[size_t(seat - 1)].waiting, lines);
            seats[size_t(seat - 1)].waiting = 0;
        }
    };
    std::error_code ec;
    while (!hub.ended() && !std::filesystem::exists(args.serve / "stop", ec)) {
        bool worked = false;
        for (size_t k = 0; k < seats.size() && !hub.ended(); ++k) {
            Seat& seat = seats[k];
            if (seat.waiting) continue;  // one request at a time per seat
            const auto request = mailbox_file(seat.dir, "request", seat.next);
            if (!std::filesystem::exists(request, ec)) continue;
            worked = true;
            const long n = seat.next++;
            auto answer = hub.submit(int(k) + 1, read_request(request));
            if (answer) {
                deliver(int(k) + 1, n, *answer);
            } else {
                // the request file stays until the answer: --send numbers the next request
                // after the highest request or response it finds
                write_atomic(request, {"WaitTurn"});
                seat.waiting = n;
            }
            release();
        }
        if (!worked) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    release();  // the session is over: every held WaitTurn gets its answer
    return 0;
}

// --- the socket access for front ends (--listen <port>, stage 2.5) -----------------------
// A TCP server on 127.0.0.1 (local connections only) in front of the seat hub. Line based,
// UTF-8, "\n" or "\r\n":
// - On connecting with --seats N the client names its manager: "SEAT <k>"; without --seats it
//   is seat 1 at once. The server answers with the start output (Window/Player lines).
// - Every block of command lines up to "go." is executed; the answer is its output lines and
//   then a line holding only "." (output lines are never just "."). With --json each output
//   line is one JSON object (class, window, player, text, data - see State(view)).
// - A block holding only "WaitTurn" is answered when the seat is on turn (SeatHub).
// - A seat is one connection at a time; a client that goes away loses a held WaitTurn.
// The session ends with Quit; the log (--log) replays it like any other.
#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t kNoSocket = INVALID_SOCKET;
void close_socket(socket_t s) { closesocket(s); }
#else
using socket_t = int;
constexpr socket_t kNoSocket = -1;
void close_socket(socket_t s) { close(s); }
#endif

int run_console_listen(const Arguments& args) {
    if (args.data.empty()) throw std::runtime_error("--data is required");
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) throw std::runtime_error("cannot start Winsock");
#endif
    const int seat_count = args.seats > 0 ? args.seats : 1;
    std::ofstream log;
    Arguments hub_args = args;
    hub_args.seats = seat_count;
    bmh::cli::SeatHub hub(console_options(args), seat_count, hub_log(log, hub_args));

    socket_t server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == kNoSocket) throw std::runtime_error("cannot open a socket");
    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(args.listen_port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // local connections only
    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0 || listen(server, 8) != 0)
        throw std::runtime_error("cannot listen on 127.0.0.1:" + std::to_string(args.listen_port));
    std::cerr << "openbmh_cli: listening on 127.0.0.1:" << args.listen_port << " (" << seat_count << " seat"
              << (seat_count > 1 ? "s" : "") << ")\n";

    struct Client {
        socket_t socket = kNoSocket;
        std::string input;                 // received, not yet a full line
        int seat = 0;                      // 0 = not seated yet
        std::vector<std::string> block;    // command lines up to "go."
        bool held = false;                 // a WaitTurn is held for it
    };
    std::vector<Client> clients;
    auto send_lines = [](Client& c, const std::vector<std::string>& lines) {
        std::string text;
        for (const auto& l : lines) text += l + "\n";
        text += ".\n";
        size_t sent = 0;
        while (sent < text.size()) {
            const int n = int(send(c.socket, text.data() + sent, int(text.size() - sent), 0));
            if (n <= 0) return;
            sent += size_t(n);
        }
    };
    auto seat_taken = [&](int seat) {
        for (const auto& c : clients) if (c.seat == seat) return true;
        return false;
    };
    auto release = [&] {
        for (auto& [seat, lines] : hub.release())
            for (auto& c : clients)
                if (c.seat == seat && c.held) {
                    send_lines(c, lines);
                    c.held = false;
                }
    };
    auto on_line = [&](Client& c, std::string line) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (c.seat == 0) {
            int seat = seat_count == 1 ? 1 : 0;
            if (seat == 0 && line.rfind("SEAT ", 0) == 0) seat = std::atoi(line.c_str() + 5);
            if (seat < 1 || seat > seat_count) {
                send_lines(c, {"! E_BAD_ARGUMENT: first line SEAT <1-" + std::to_string(seat_count) + ">"});
                return;
            }
            if (seat_taken(seat)) {
                send_lines(c, {"! E_BAD_ARGUMENT: seat " + std::to_string(seat) + " is connected already"});
                return;
            }
            c.seat = seat;
            send_lines(c, hub.start_output());
            return;
        }
        if (c.held) {
            send_lines(c, {"! E_BAD_ARGUMENT: a WaitTurn is pending for this seat"});
            return;
        }
        c.block.push_back(line);
        if (bmh::cli::console::classify_line(line) != bmh::cli::console::LineKind::go) return;
        auto answer = hub.submit(c.seat, std::move(c.block));
        c.block.clear();
        if (answer) send_lines(c, *answer);
        else c.held = true;
        release();
    };

    while (!hub.ended()) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(server, &readable);
        socket_t top = server;
        for (const auto& c : clients) {
            FD_SET(c.socket, &readable);
            if (c.socket > top) top = c.socket;
        }
        timeval wait{0, 100000};
        if (select(int(top + 1), &readable, nullptr, nullptr, &wait) < 0) break;
        if (FD_ISSET(server, &readable)) {
            const socket_t s = accept(server, nullptr, nullptr);
            if (s != kNoSocket) {
                clients.push_back(Client{s});
                if (seat_count == 1 && !seat_taken(1)) {  // one seat: no SEAT line needed
                    clients.back().seat = 1;
                    send_lines(clients.back(), hub.start_output());
                } else if (seat_count == 1) {
                    send_lines(clients.back(), {"! E_BAD_ARGUMENT: seat 1 is connected already"});
                }
            }
        }
        for (size_t i = 0; i < clients.size() && !hub.ended();) {
            Client& c = clients[i];
            if (!FD_ISSET(c.socket, &readable)) { ++i; continue; }
            char buffer[4096];
            const int n = int(recv(c.socket, buffer, sizeof buffer, 0));
            if (n <= 0) {  // gone
                if (c.held) hub.forget(c.seat);
                close_socket(c.socket);
                clients.erase(clients.begin() + std::ptrdiff_t(i));
                continue;
            }
            c.input.append(buffer, size_t(n));
            for (size_t nl; (nl = c.input.find('\n')) != std::string::npos;) {
                std::string line = c.input.substr(0, nl);
                c.input.erase(0, nl + 1);
                on_line(c, std::move(line));
            }
            ++i;
        }
    }
    release();  // the session ended: held WaitTurns get their answer
    for (auto& c : clients) close_socket(c.socket);
    close_socket(server);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

int run_send(const Arguments& args) {
    // the next number: one above the highest request or response there
    long n = 0;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(args.send, ec)) {
        const std::string name = e.path().filename().string();
        for (const char* prefix : {"request-", "response-"}) {
            const std::string p = prefix;
            if (name.rfind(p, 0) == 0 && name.size() > p.size() + 4 && name.compare(name.size() - 4, 4, ".txt") == 0)
                n = std::max(n, std::atol(name.substr(p.size(), name.size() - p.size() - 4).c_str()));
        }
    }
    if (ec) throw std::runtime_error("no session directory: " + args.send.string());
    ++n;
    std::vector<std::string> lines;
    for (std::string line; std::getline(std::cin, line);) lines.push_back(line);
    write_atomic(mailbox_file(args.send, "request", n), lines);
    const auto response = mailbox_file(args.send, "response", n);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(args.timeout_seconds);
    while (!std::filesystem::exists(response, ec)) {
        if (std::chrono::steady_clock::now() > deadline) {
            std::cerr << "openbmh_cli: no answer from the session in " << args.timeout_seconds << " s\n";
            return 3;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::ifstream in(response, std::ios::binary);
    std::cout << in.rdbuf();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        Arguments args = parse_args(argc, argv);
        if (args.help) { print_usage(); return 0; }
        if (!args.send.empty()) return run_send(args);
        std::shared_ptr<const bmh::content::Classic> contents;
        std::unique_ptr<bmh::content::Mount> mount;
        if (!args.data.empty()) {
            if (std::filesystem::is_directory(args.data))
                args.data /= "content.obw";
            contents = bmh::content::open(args.data);
            mount = std::make_unique<bmh::content::Mount>(args.data, contents);
            args.data = mount->root();
            if (args.saves.empty()) {
                bmh::launch::ModLibrary library(
                    bmh::launch::executable_path(bmh::launch::path_from_utf8(argv[0])).parent_path());
                bmh::launch::ModEntry mod;
                mod.content_id = contents->content_id;
                args.saves = library.user_directory(mod);
            }
        } else
            throw std::runtime_error(
                "0.2 requires --mod <content.obw>; import installations in OpenBMH first");
        if (args.console) {
            if (!args.log.empty() && !args.replay.empty()) throw std::runtime_error("--log and --replay cannot be combined");
            if (args.listen_port) return run_console_listen(args);
            if (!args.serve.empty()) return args.seats > 0 ? run_console_seats(args) : run_console_serve(args);
            return args.replay.empty() ? run_console(args) : run_console_replay(std::move(args));
        }
        if (!args.replay.empty()) {
            if (!args.log.empty()) throw std::runtime_error("--log and --replay cannot be combined");
            return run_replay(std::move(args));
        }
        if (args.data.empty()) throw std::runtime_error("--data is required unless --replay supplies it");
        std::ofstream log;
        if (!args.log.empty()) {
            log.open(args.log, std::ios::binary | std::ios::trunc);
            if (!log) throw std::runtime_error("cannot_open_log");
            const auto abs_data = bmh::launch::path_to_utf8(std::filesystem::absolute(args.data).lexically_normal());
            log << dump_json(Json(Object{{"type", "openbmh_cli_log"}, {"protocol", 1},
                {"data", abs_data}, {"seed", static_cast<std::int64_t>(args.seed)}})) << '\n';
        }
        Protocol protocol(args.data, args.seed);
        std::string line;
        while (std::getline(std::cin, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            Json request;
            Json response;
            try {
                request = args.json ? parse_json(line) : text_request(line, &protocol);
                response = protocol.handle(request);
            } catch (const std::exception& error) {
                request = Json(Object{{"cmd", "__protocol_error"}, {"detail", error.what()}});
                response = protocol_error(error.what(), protocol.revision());
            }
            if (log.is_open()) {
                log << dump_json(Json(Object{{"request", request}, {"response", response}})) << '\n';
                log.flush();
                if (!log) throw std::runtime_error("log_write_failed");
            }
            std::cout << (args.json ? dump_json(response) : command_line_response(response)) << '\n';
            if (const auto* object = response.object()) {
                const auto it = object->find("quit");
                if (it != object->end() && it->second.boolean() && *it->second.boolean()) break;
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "openbmh_cli: " << error.what() << '\n';
        return 2;
    }
}
