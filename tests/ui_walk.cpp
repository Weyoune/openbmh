// SPDX-License-Identifier: GPL-3.0-only
// ui_walk - regression tool for refactoring of the user interface: replays a fixed input script against
// the real UI flow and the real controller (no window) and writes one fingerprint line per step.
//
// A new career is started from the setup screen like a player does it (default options, first manager),
// with the given Watcom seed. Every script line is one input; its request is served by the production
// controller (app::serve_flow_request) with all presenters absent, so nothing waits for a window. After
// every step the tool writes
//
//     <step> <screen name> picture <fnv1a of the 320x240 palette indices> state <fnv1a of the state JSON>
//
// The values mean nothing by themselves: a change that must not alter the UI has to reproduce the file
// written by the build before it, byte for byte. A step that throws or that the controller refuses is
// recorded as "error <text>" and the walk goes on (the same error must then appear again).
//
// The walk is not a faithful player: the SDL loop of the app also feeds timers and presentation
// acknowledgements that this tool does not. Some screens therefore refuse their requests or cannot be
// left; the script command "home" returns to the desk and, when that fails, starts the same career
// again (deterministic), so one stuck screen does not end the coverage of the others.
//
// Usage: ui_walk <BMH data dir> <script> <output file> [watcom seed=1]
// Script lines: left X Y | right X Y | double X Y | move X Y | hold X Y MS | key NAME | text T | home
//               (native 320x240 coordinates; '#' starts a comment)
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

#include "app/controller.hpp"
#include "ui/flow.hpp"

using namespace bmh;

namespace {

uint64_t fnv1a(const uint8_t* data, size_t size) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < size; ++i) h = (h ^ data[i]) * 0x100000001b3ull;
    return h;
}

// The flow, the session behind it and the output file of one walk.
struct Walk {
    std::filesystem::path data, save_directory;
    uint32_t seed = 1;
    std::FILE* out = nullptr;
    std::unique_ptr<ui::StartupFlow> flow;
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    uint64_t step = 0, restarts = 0;

    void record(const std::string& error) {
        ++step;
        if (!error.empty()) {
            std::fprintf(out, "%llu error %s\n", static_cast<unsigned long long>(step), error.c_str());
            return;
        }
        const Image image = flow->render();
        const std::string state = flow->state_json();
        std::fprintf(out, "%llu %s picture %016llx state %016llx\n", static_cast<unsigned long long>(step),
                     flow->screen_name().c_str(),
                     static_cast<unsigned long long>(fnv1a(image.pixels.data(), image.pixels.size())),
                     static_cast<unsigned long long>(fnv1a(reinterpret_cast<const uint8_t*>(state.data()), state.size())));
    }
    // One request: served by the production controller. Its answer (an error text for a refused
    // request) is part of the fingerprint line.
    void serve(const ui::FlowResult& result) {
        try {
            const std::string answer = app::serve_flow_request(*flow, result, session, data, save_directory, seed, revision,
                                                               playback, audio_session, false);
            record(answer.empty() ? std::string() : "refused: " + answer);
        } catch (const std::exception& e) {
            record(std::string("exception: ") + e.what());
        }
    }
    // A mouse click as the SDL loop of the app delivers it: the pressed look at the press of the left
    // button, pointer_down() for the screens that react to the press itself (the same list as in
    // src/app/main.cpp), then the click at the release.
    void click(int x, int y, bool right) {
        try {
            if (!right && flow->press_hold(x, y)) record({});
            if (flow->desktop() || flow->in_balance() || flow->in_desk_records() || flow->in_achievements() ||
                flow->in_indoor_standings() || flow->in_reschedule_calendar() || flow->in_league_pairings() ||
                flow->in_family_table() || flow->in_toto_calculator() || flow->in_stadium() || flow->in_environment() ||
                flow->in_strengths() || flow->in_top_scorers() || flow->in_opportunities() || flow->in_statistics_menu() ||
                flow->in_statistics_page() || flow->in_analytics() || flow->in_other_games() || flow->in_pitch() ||
                flow->in_settings())
                serve(flow->pointer_down(x, y, right));
            serve(flow->click(x, y, right));
        } catch (const std::exception& e) {
            record(std::string("exception: ") + e.what());
        }
    }
    // The desk itself is shown (desktop() alone is also true while a screen lies over it).
    bool at_desk() const { return flow->screen_name() == "desktop"; }
    // Setup screen -> first manager -> career start (the clicks of a player who accepts every default).
    void start() {
        session.reset();
        revision = 0;
        audio_session.reset();
        flow = std::make_unique<ui::StartupFlow>(data, save_directory);
        record({});
        click(150, 215, false);
        click(5, 225, true);
        click(5, 225, true);
        click(90, 169, false);
        if (!at_desk()) throw std::runtime_error("the career start did not reach the desk");
    }
    // Back to the desk: right clicks and ESC as a player would try; a screen that cannot be left this
    // way ends in a restart of the same career.
    void home() {
        for (int i = 0; i < 6 && !at_desk(); ++i) {
            click(318, 2, true);
            if (!at_desk()) {
                try { serve(flow->send_key("ESC")); } catch (const std::exception& e) { record(std::string("exception: ") + e.what()); }
            }
        }
        if (!at_desk()) {
            ++restarts;
            std::fprintf(out, "restart from %s\n", flow->screen_name().c_str());
            start();
        }
    }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: ui_walk <BMH data dir> <script> <output file> [watcom seed]\n");
        return 2;
    }
    try {
        Walk walk;
        walk.data = argv[1];
        walk.seed = argc > 4 ? uint32_t(std::stoul(argv[4])) : 1u;
        std::ifstream script(argv[2]);
        if (!script) throw std::runtime_error("cannot read the script");
        walk.out = std::fopen(argv[3], "wb");
        if (!walk.out) throw std::runtime_error("cannot write the output file");
        // Saves written by the walk (file dialog) go to a scratch directory, never into the game data.
        walk.save_directory = std::filesystem::temp_directory_path() / "openbmh_ui_walk";
        std::filesystem::remove_all(walk.save_directory);
        std::filesystem::create_directories(walk.save_directory);
        walk.start();

        std::string line;
        while (std::getline(script, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto first = line.find_first_not_of(" \t");
            if (first == std::string::npos || line[first] == '#') continue;
            std::istringstream row(line);
            std::string command;
            row >> command;
            try {
                if (command == "home") { walk.home(); continue; }
                if (command == "key" || command == "text") {
                    std::string value;
                    std::getline(row, value);
                    if (!value.empty() && value[0] == ' ') value.erase(0, 1);
                    if (command == "key") walk.serve(walk.flow->send_key(value));
                    else walk.serve(ui::FlowResult{walk.flow->text(value)});
                    continue;
                }
                int x = 0, y = 0, ms = 0;
                if (!(row >> x >> y)) throw std::runtime_error("bad script line: " + line);
                if (command == "left") walk.click(x, y, false);
                else if (command == "right") walk.click(x, y, true);
                else if (command == "double") walk.serve(walk.flow->click(x, y, false, true));
                else if (command == "move") walk.serve(ui::FlowResult{walk.flow->move(x, y)});
                else if (command == "hold" && (row >> ms)) walk.serve(walk.flow->hold(x, y, ms));
                else throw std::runtime_error("bad script line: " + line);
            } catch (const std::exception& e) {
                walk.record(std::string("exception: ") + e.what());
            }
        }
        std::fclose(walk.out);
        std::printf("%llu steps, %llu restarts, last screen %s\n", static_cast<unsigned long long>(walk.step),
                    static_cast<unsigned long long>(walk.restarts), walk.flow->screen_name().c_str());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ui_walk: %s\n", e.what());
        return 1;
    }
}
