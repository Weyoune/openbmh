// SPDX-License-Identifier: GPL-3.0-only
// Original startup UI. Independent of match-engine builds.
#include <SDL.h>

#include <cstdio>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "formats/palette.hpp"
#include "formats/le.hpp"
#include "game/schedule.hpp"
#include "ui/flow.hpp"

namespace fs = std::filesystem;
using namespace bmh;

static void save_bmp(const Image& image, const Palette& palette, const fs::path& path) {
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, image.width, image.height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) throw FormatError(SDL_GetError());
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x)
            static_cast<uint32_t*>(s->pixels)[y * (s->pitch / 4) + x] = palette.argb[image.pixels[size_t(y) * image.width + x]];
    int rc = SDL_SaveBMP(s, path.string().c_str());
    SDL_FreeSurface(s);
    if (rc) throw FormatError("cannot write UI snapshot: " + std::string(SDL_GetError()));
}

static void emit(std::string fields) {
    static const auto epoch = std::chrono::steady_clock::now();
    static uint64_t step = 0;
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - epoch).count();
    fields.pop_back();
    std::cout << fields << ",\"step\":" << ++step << ",\"elapsed_ns\":" << elapsed << "}\n" << std::flush;
}

static void report(const ui::StartupFlow& screen, ui::FlowResult result,
                   std::string_view input = {}) {
    const char* request = result.request == ui::FlowRequest::new_career ? "new_career" :
                          result.request == ui::FlowRequest::manager_confirmed ? "manager_confirmed" :
                          result.request == ui::FlowRequest::kickoff ? "kickoff" :
                          result.request == ui::FlowRequest::desk_action ? "desk_action" :
                          result.request == ui::FlowRequest::submenu_opened ? "submenu_opened" :
                          result.request == ui::FlowRequest::submenu_closed ? "submenu_closed" :
                          result.request == ui::FlowRequest::submenu_action ? "submenu_action" :
                          result.request == ui::FlowRequest::friendly_action ? "friendly_action" :
                          result.request == ui::FlowRequest::file_action ? "file_action" :
                          result.request == ui::FlowRequest::load_game ? "load_game" : "none";
    auto state = screen.state_json();
    state.pop_back();
    std::ostringstream out;
    out << state << ",\"changed\":" << (result.changed ? "true" : "false")
        << ",\"request\":" << ui::json_quote(request);
    if (result.request == ui::FlowRequest::desk_action || result.request == ui::FlowRequest::kickoff)
        out << ",\"desk_action\":" << ui::desk_request_json(result.desk_action);
    if (result.request == ui::FlowRequest::file_action)
        out << ",\"file_action\":" << ui::save_load_request_json(result.file_action);
    if (result.request == ui::FlowRequest::friendly_action)
        out << ",\"friendly_action\":" << ui::friendly_match_request_json(result.friendly_action);
    if ((result.request == ui::FlowRequest::submenu_opened && result.desk_action.kind != ui::DeskRequestKind::none) ||
        (result.request == ui::FlowRequest::submenu_action && result.desk_action.kind != ui::DeskRequestKind::none))
        out << ",\"navigation\":" << ui::desk_request_json(result.desk_action);
    if (result.request == ui::FlowRequest::submenu_opened || result.request == ui::FlowRequest::submenu_closed)
        out << ",\"destination\":" << ui::json_quote(screen.screen_name());
    if (result.request == ui::FlowRequest::submenu_action)
        out << ",\"submenu_action\":" << ui::preparation_request_json(result.submenu_action);
    if (!input.empty()) out << ",\"input\":" << input;
    out << '}'; emit(out.str());
}

static std::string pointer_input(std::string_view type, int x, int y) {
    return "{\"type\":" + ui::json_quote(type) + ",\"x\":" + std::to_string(x) +
           ",\"y\":" + std::to_string(y) + "}";
}

int main(int argc, char** argv) {
    fs::path data = ".", dump, script, save, save_directory;
    std::optional<bool> indoor;
    auto league_symbol=ui::SetupLeagueSymbol::source;
    try {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help") {
                std::puts("openbmh_ui --data BMH [--save original.FBM] [--save-directory dir] [--pitch-context outdoor|indoor] [--league-symbol source|countries] [--dump screen.bmp] [--script inputs.txt]\n"
                          "Script: left X Y / right X Y / move X Y / hold X Y MS / text NAME / key ENTER|BACKSPACE|ESC|letter.\n"
                          "Script context injection: resume PATH (decimal original master IDs in observed selection order).\n"
                          "Native coordinates: 320x240. manager_confirmed waits for updated order; final new_career waits for engine.\n"
                          "--save renders a read-only one-manager league reference; A/I/D open lineup/training/tactics.\n"
                          "Pitch requires observed --pitch-context outdoor; transient indoor state is absent from FBM.\n"
                          "Game changes remain pending until an explicit engine acknowledgement; RMT closes idle menus.");
                return 0;
            }
            if (arg != "--data" && arg != "--dump" && arg != "--script" && arg != "--save" && arg != "--save-directory" && arg != "--pitch-context" && arg!="--league-symbol") throw FormatError("unknown option: " + arg);
            if (++i == argc) throw FormatError("missing value for " + arg);
            if (arg == "--data") data = argv[i];
            if (arg == "--dump") dump = argv[i];
            if (arg == "--script") script = argv[i];
            if (arg == "--save") save = argv[i];
            if (arg == "--save-directory") save_directory = argv[i];
            if(arg=="--league-symbol") {
                const std::string value=argv[i];
                if(value!="source" && value!="dual" && value!="countries")throw FormatError("league symbol must be source or countries");
                league_symbol=value!="source"?ui::SetupLeagueSymbol::country_selection:ui::SetupLeagueSymbol::source;
            }
            if (arg == "--pitch-context") {
                const std::string value = argv[i];
                if (value != "outdoor" && value != "indoor") throw FormatError("pitch context must be outdoor or indoor");
                indoor = value == "indoor";
            }
        }
        if (save_directory.empty()) save_directory = data;
        install_fixture_tables(load_le(ui::asset_path(data, "BMH.exe")));
        ui::StartupFlow screen(data, save_directory,league_symbol);
        if (indoor && save.empty()) throw FormatError("pitch context requires a reference save");
        if (!save.empty()) screen.load_reference(save, indoor);
        Palette palette = load_palette(ui::asset_path(data, "M3PC.COL"));
        report(screen, {}, "{\"type\":\"initialize\",\"data\":" + ui::json_quote(fs::absolute(data).string()) +
               ",\"reference_save\":" + (save.empty() ? "null" : ui::json_quote(fs::absolute(save).string())) +
               ",\"pitch_context\":" + (!indoor ? "null" : *indoor ? "\"indoor\"" : "\"outdoor\"") + "}");
        if (!script.empty()) {
            std::ifstream input(script);
            if (!input) throw FormatError("cannot read input script");
            std::string line;
            while (std::getline(input, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const auto start = line.find_first_not_of(" \t");
                if (start == std::string::npos || line[start] == '#') continue;
                std::istringstream row(line);
                std::string command, extra; int x, y;
                row >> command;
                if (command == "resume") {
                    std::string path; std::getline(row, path);
                    if (path.empty() || path[0] != ' ') throw FormatError("resume requires a context filename");
                    path.erase(0, 1);
                    const auto* previous = screen.pending_manager();
                    if (!previous) throw FormatError("no pending manager context request");
                    bool changed = screen.resume_manager_selection(ui::load_selection_context(path, previous->roster_revision + 1));
                    report(screen, {changed}, "{\"type\":\"context_update\",\"source\":\"reference_fixture\",\"path\":" + ui::json_quote(path) + "}");
                } else if (command == "text" || command == "key") {
                    std::string value;
                    std::getline(row, value);
                    if (value.empty() || value[0] != ' ') throw FormatError("invalid input script line: " + line);
                    value.erase(0, 1);
                    if (command == "key" && value != "ENTER" && value != "BACKSPACE" && value != "ESC" && value.size() != 1)
                        throw FormatError("unsupported script key: " + value);
                    auto result = command == "text" ? ui::FlowResult{screen.text(value)} : screen.send_key(value);
                    report(screen, result, "{\"type\":" + ui::json_quote(command) + ",\"value\":" + ui::json_quote(value) + "}");
                } else {
                    if(command=="hold"){
                        int milliseconds;
                        if(!(row>>x>>y>>milliseconds)||(row>>extra))throw FormatError("invalid hold script line: "+line);
                        auto result=screen.hold(x,y,milliseconds);
                        auto input=pointer_input("hold",x,y);input.pop_back();
                        report(screen,result,input+",\"milliseconds\":"+std::to_string(milliseconds)+"}");
                        continue;
                    }
                    if (!(row >> x >> y) || (row >> extra) || (command != "left" && command != "right" && command != "move"))
                        throw FormatError("invalid input script line: " + line);
                    auto result = command == "move" ? ui::FlowResult{screen.move(x, y)} : screen.click(x, y, command == "right");
                    report(screen, result, pointer_input(command, x, y));
                }
            }
        }
        if (!dump.empty()) { save_bmp(screen.render(), palette, dump); return 0; }
        if (!script.empty()) return 0;
        if (SDL_Init(SDL_INIT_VIDEO) != 0) throw FormatError(SDL_GetError());
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        auto* window = SDL_CreateWindow("OpenBMH - Spieloptionen", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                        960, 720, SDL_WINDOW_RESIZABLE);
        if (!window) throw FormatError(SDL_GetError());
        auto* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer) throw FormatError(SDL_GetError());
        SDL_RenderSetLogicalSize(renderer, 320, 240);
        auto* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 240);
        if (!texture) throw FormatError(SDL_GetError());
        std::vector<uint32_t> pixels(320 * 240);
        bool running = true, dirty = true;
        struct Press {int x,y;uint32_t time;std::string screen;};std::optional<Press> left_press;
        SDL_StartTextInput();
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE && !screen.in_preparation_menu())) running = false;
                if(event.type==SDL_MOUSEBUTTONDOWN&&event.button.button==SDL_BUTTON_LEFT)
                    left_press=Press{event.button.x,event.button.y,SDL_GetTicks(),screen.screen_name()};
                if(event.type==SDL_WINDOWEVENT&&event.window.event==SDL_WINDOWEVENT_FOCUS_LOST)left_press.reset();
                if (event.type == SDL_MOUSEBUTTONUP && (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)) {
                    const bool held=event.button.button==SDL_BUTTON_LEFT&&left_press&&
                        event.button.x==left_press->x&&event.button.y==left_press->y&&left_press->screen==screen.screen_name()&&SDL_GetTicks()-left_press->time>=2000;
                    auto result = held ? screen.hold(event.button.x,event.button.y,2000) :
                        screen.click(event.button.x, event.button.y, event.button.button == SDL_BUTTON_RIGHT);
                    auto input=pointer_input(held ? "hold" : event.button.button == SDL_BUTTON_RIGHT ? "right" : "left",event.button.x,event.button.y);
                    if(held){input.pop_back();input+=",\"milliseconds\":"+std::to_string(SDL_GetTicks()-left_press->time)+"}";}
                    left_press.reset();report(screen, result, input);dirty |= result.changed;
                    SDL_SetWindowTitle(window, ("OpenBMH - " + screen.screen_name()).c_str());
                }
                if (event.type == SDL_MOUSEMOTION) {
                    if(left_press&&(event.motion.x!=left_press->x||event.motion.y!=left_press->y))left_press.reset();
                    bool changed = screen.move(event.motion.x, event.motion.y);
                    report(screen, {changed}, pointer_input("move", event.motion.x, event.motion.y)); dirty |= changed;
                }
                if (event.type == SDL_TEXTINPUT) {
                    const auto input = "{\"type\":\"text\",\"value\":" + ui::json_quote(event.text.text) + "}";
                    try {
                        bool changed = screen.text(event.text.text);
                        report(screen, {changed}, input); dirty |= changed;
                    } catch (const FormatError& e) {
                        // Bad text input is a rejected action, not an application crash.
                        auto state = screen.state_json(); state.pop_back();
                        emit(state + ",\"changed\":false,\"request\":\"none\",\"input\":" + input +
                             ",\"error\":{\"kind\":\"invalid_action\",\"message\":" + ui::json_quote(e.what()) + "}}");
                    }
                }
                if (event.type == SDL_KEYDOWN && (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_BACKSPACE ||
                    (screen.in_preparation_menu() && event.key.keysym.sym == SDLK_ESCAPE) ||
                    (screen.desktop() && event.key.keysym.sym >= SDLK_a && event.key.keysym.sym <= SDLK_z))) {
                    std::string key = event.key.keysym.sym == SDLK_RETURN ? "ENTER" : event.key.keysym.sym == SDLK_BACKSPACE ? "BACKSPACE" : event.key.keysym.sym == SDLK_ESCAPE ? "ESC" :
                                      std::string(1, char('A' + event.key.keysym.sym - SDLK_a));
                    auto result = screen.send_key(key);
                    report(screen, result, "{\"type\":\"key\",\"value\":" + ui::json_quote(key) + "}"); dirty |= result.changed;
                }
            }
            if (dirty) {
                const auto image = screen.render();
                for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = palette.argb[image.pixels[i]];
                SDL_UpdateTexture(texture, nullptr, pixels.data(), 320 * 4); dirty = false;
            }
            SDL_RenderClear(renderer); SDL_RenderCopy(renderer, texture, nullptr, nullptr); SDL_RenderPresent(renderer);
            SDL_Delay(10);
        }
        SDL_StopTextInput();
        SDL_DestroyTexture(texture); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "UI error: %s\n", e.what()); SDL_Quit(); return 1;
    }
}
