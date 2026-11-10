// SPDX-License-Identifier: GPL-3.0-only
// The robot: a test hook that plays the real game program from an input script, without a person.
//
// Purpose. The window program (src/app/main.cpp) owns the SDL loop, the modal presentation loops
// and the timers; the headless tools (ui_walk, season_sim) bypass all of that. The robot covers it:
// started with
//
//     OPENBMH_ROBOT=<script file>  OPENBMH_ROBOT_LOG=<output file>
//
// (usually together with SDL_VIDEODRIVER=dummy and SDL_AUDIODRIVER=dummy, so that no window and no
// sound device are needed), the program reads the script instead of waiting for the mouse. After
// every command the robot waits until the picture has stopped changing and writes one line
//
//     <number> <command> -> <fingerprint of the settled picture> [unsettled]
//
// A refactoring of the window program must reproduce that file. Without the two environment
// variables the robot is inactive and costs one branch per frame and per empty event poll.
//
// How it hooks in (both hooks are called on the UI thread only):
//   - present(renderer) replaces SDL_RenderPresent in main.cpp: the robot reads the rendered
//     picture back and remembers its fingerprint;
//   - QuitConfirmation::poll calls idle() when SDL has no event: once the picture is settled the
//     robot pushes the events of the next command into SDL's queue (so they take exactly the path
//     of real input, including SDL's conversion to the 320x240 logical coordinates).
//
// Script: one command per line, '#' starts a comment. Coordinates are the game's 320x240 units.
//     left X Y | right X Y     a click (press, 120 ms, release)
//     key NAME                 RETURN, ESCAPE, BACKSPACE, SPACE or one letter/digit
//     text T                   typed text
//     wait MS                  nothing for MS milliseconds (then the settled picture is logged)
//     quit                     closes the program (the quit question is answered with yes)
// OPENBMH_ROBOT_SHOTS=<directory> additionally saves the settled picture of every command as a BMP.
// The time a command may take to settle is limited (OPENBMH_ROBOT_TIMEOUT_MS, default 15000); a
// screen that keeps animating is logged as "unsettled" with the picture shown at that moment.
#pragma once

#include <SDL.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace bmh::app {

class Robot {
public:
    static Robot& instance() {
        static Robot robot;
        return robot;
    }
    bool active() const { return active_; }

    // Reads the environment; called once the window exists.
    void start(SDL_Window* window) {
        const char* script = std::getenv("OPENBMH_ROBOT");
        const char* log = std::getenv("OPENBMH_ROBOT_LOG");
        if (!script || !log) return;
        std::ifstream in(script);
        if (!in) { std::fprintf(stderr, "robot: cannot read %s\n", script); return; }
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto first = line.find_first_not_of(" \t");
            if (first == std::string::npos || line[first] == '#') continue;
            commands_.push_back(line.substr(first));
        }
        log_ = std::fopen(log, "wb");
        if (!log_) { std::fprintf(stderr, "robot: cannot write %s\n", log); return; }
        if (const char* settle = std::getenv("OPENBMH_ROBOT_SETTLE_MS")) settle_ms_ = Uint32(std::atoi(settle));
        if (const char* timeout = std::getenv("OPENBMH_ROBOT_TIMEOUT_MS")) timeout_ms_ = Uint32(std::atoi(timeout));
        if (const char* shots = std::getenv("OPENBMH_ROBOT_SHOTS")) shots_ = shots;
        window_ = window;
        active_ = true;
        last_change_ = command_time_ = SDL_GetTicks();
    }

    // A picture is about to be shown: fingerprint it (FNV-1a over the rendered ARGB pixels).
    void frame(SDL_Renderer* renderer) {
        if (!active_) return;
        int w = 0, h = 0;
        if (SDL_GetRendererOutputSize(renderer, &w, &h) != 0 || w <= 0 || h <= 0) return;
        pixels_.resize(size_t(w) * size_t(h) * 4);
        width_ = w, height_ = h;
        if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, pixels_.data(), w * 4) != 0) return;
        uint64_t hash = 0xcbf29ce484222325ull;
        for (uint8_t byte : pixels_) hash = (hash ^ byte) * 0x100000001b3ull;
        if (hash != picture_) {
            picture_ = hash;
            ++pictures_;
            last_change_ = SDL_GetTicks();
        }
    }

    // SDL has no event: issue the release of a click, or the next command once the picture is settled.
    void idle() {
        if (!active_) return;
        const Uint32 now = SDL_GetTicks();
        if (release_pending_) {
            if (now - command_time_ < 120) return;
            push_button(SDL_MOUSEBUTTONUP, release_button_);
            release_pending_ = false;
            last_change_ = now;   // (the release may change the picture: settle again from here)
            return;
        }
        if (now < wait_until_) return;
        const bool settled = now - last_change_ >= settle_ms_ && now - command_time_ >= settle_ms_;
        const bool timed_out = now - command_time_ >= timeout_ms_;
        if (!settled && !timed_out) return;
        // the previous command is done: log it with the picture now on screen
        if (issued_ > 0 && !logged_) {
            // (the number of pictures in between depends on the clock and is not written)
            // A picture that was still changing at the time limit (a running live match, an animation) depends
            // on the clock: only the fact is written, not its fingerprint.
            if (settled) std::fprintf(log_, "%zu %s -> %016llx\n", issued_, commands_[issued_ - 1].c_str(), static_cast<unsigned long long>(picture_));
            else std::fprintf(log_, "%zu %s -> unsettled\n", issued_, commands_[issued_ - 1].c_str());
            std::fflush(log_);
            save_shot();
            logged_ = true;
        }
        if (issued_ >= commands_.size()) {
            if (!finished_) { finished_ = true; std::fclose(log_); log_ = nullptr; }
            return;
        }
        issue(commands_[issued_++]);
        logged_ = false;
        pictures_ = 0;
        command_time_ = last_change_ = SDL_GetTicks();
    }

    // The quit question of a robot run is answered with "quit".
    static int confirm_quit(const SDL_MessageBoxData*, int* selected) {
        if (selected) *selected = 1;
        return 0;
    }

private:
    // OPENBMH_ROBOT_SHOTS=<directory>: the settled picture of every command as <number>.bmp (for
    // writing scripts and for looking at a run; not needed for the comparison).
    void save_shot() {
        if (shots_.empty() || pixels_.empty()) return;
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels_.data(), width_, height_, 32, width_ * 4,
                                                                  SDL_PIXELFORMAT_ARGB8888);
        if (!surface) return;
        char name[32];
        std::snprintf(name, sizeof name, "/%04zu.bmp", issued_);
        SDL_SaveBMP(surface, (shots_ + name).c_str());
        SDL_FreeSurface(surface);
    }
    void push_button(Uint32 type, Uint8 button) {
        SDL_Event e{};
        e.type = type;
        e.button.windowID = SDL_GetWindowID(window_);
        e.button.button = button;
        e.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
        e.button.clicks = 1;
        e.button.x = window_x_;
        e.button.y = window_y_;
        SDL_PushEvent(&e);
    }
    void push_motion() {
        SDL_Event e{};
        e.type = SDL_MOUSEMOTION;
        e.motion.windowID = SDL_GetWindowID(window_);
        e.motion.x = window_x_;
        e.motion.y = window_y_;
        SDL_PushEvent(&e);
    }
    void push_key(SDL_Keycode key) {
        for (const Uint32 type : {Uint32(SDL_KEYDOWN), Uint32(SDL_KEYUP)}) {
            SDL_Event e{};
            e.type = type;
            e.key.windowID = SDL_GetWindowID(window_);
            e.key.state = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
            e.key.keysym.sym = key;
            e.key.keysym.scancode = SDL_GetScancodeFromKey(key);
            SDL_PushEvent(&e);
        }
    }
    void issue(const std::string& command) {
        std::istringstream row(command);
        std::string name;
        row >> name;
        if (name == "left" || name == "right") {
            int x = 0, y = 0;
            row >> x >> y;
            // the middle of the logical pixel in window coordinates (SDL converts back)
            int w = 960, h = 720;
            SDL_GetWindowSize(window_, &w, &h);
            window_x_ = x * w / 320 + w / 640;
            window_y_ = y * h / 240 + h / 480;
            // (the warp moves SDL's own pointer state too, which the program reads in a few places, and
            //  queues the motion event)
            SDL_WarpMouseInWindow(window_, window_x_, window_y_);
            release_button_ = name == "left" ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT;
            push_button(SDL_MOUSEBUTTONDOWN, release_button_);
            release_pending_ = true;
        } else if (name == "key") {
            std::string key;
            row >> key;
            SDL_Keycode code = SDLK_UNKNOWN;
            if (key == "RETURN" || key == "ENTER") code = SDLK_RETURN;
            else if (key == "ESCAPE" || key == "ESC") code = SDLK_ESCAPE;
            else if (key == "BACKSPACE") code = SDLK_BACKSPACE;
            else if (key == "SPACE") code = SDLK_SPACE;
            else if (key.size() == 1) code = SDL_Keycode(key[0] >= 'A' && key[0] <= 'Z' ? key[0] - 'A' + 'a' : key[0]);
            if (code != SDLK_UNKNOWN) push_key(code);
        } else if (name == "text") {
            std::string text;
            std::getline(row, text);
            if (!text.empty() && text[0] == ' ') text.erase(0, 1);
            SDL_Event e{};
            e.type = SDL_TEXTINPUT;
            e.text.windowID = SDL_GetWindowID(window_);
            SDL_strlcpy(e.text.text, text.c_str(), sizeof(e.text.text));
            SDL_PushEvent(&e);
        } else if (name == "wait") {
            int ms = 0;
            row >> ms;
            wait_until_ = SDL_GetTicks() + Uint32(ms > 0 ? ms : 0);
        } else if (name == "quit") {
            SDL_Event e{};
            e.type = SDL_QUIT;
            SDL_PushEvent(&e);
        }
    }

    bool active_ = false, release_pending_ = false, logged_ = true, finished_ = false;
    SDL_Window* window_ = nullptr;
    std::FILE* log_ = nullptr;
    std::vector<std::string> commands_;
    std::vector<uint8_t> pixels_;
    size_t issued_ = 0;
    uint64_t picture_ = 0;
    unsigned pictures_ = 0;
    Uint32 last_change_ = 0, command_time_ = 0, wait_until_ = 0;
    Uint32 settle_ms_ = 400, timeout_ms_ = 15000;
    int window_x_ = 0, window_y_ = 0, width_ = 0, height_ = 0;
    std::string shots_;
    Uint8 release_button_ = SDL_BUTTON_LEFT;
};

// Shows the rendered picture (SDL_RenderPresent), letting the robot see it first.
inline void present(SDL_Renderer* renderer) {
    Robot& robot = Robot::instance();
    if (robot.active()) robot.frame(renderer);
    SDL_RenderPresent(renderer);
}

} // namespace bmh::app
