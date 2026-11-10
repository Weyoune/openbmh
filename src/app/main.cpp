// SPDX-License-Identifier: GPL-3.0-only
// Entry point of the SDL front end (openbmh_play) and the owner of everything the player
// sees and hears: window, renderer, 320x240 indexed canvas, audio Playback, quit handling.
// It parses the command line, resolves the game data (src/launch), builds ui::StartupFlow and
// then runs the event loops. The request/acknowledge model works like this:
//   - Input events go to the StartupFlow; a click or key may return a FlowResult carrying a
//     FlowRequest (kickoff, desk action, transfer, save/load, ...).
//   - main.cpp's handle() passes that result to app::serve_flow_request() (controller.cpp),
//     together with the shared revision counter and a large set of presenter lambdas.
//   - The controller asks the CareerSession (career_session.cpp, the engine side) to do the
//     work and acknowledges the outcome to the flow; the engine may call a presenter lambda
//     back, synchronously, to show a modal dialog or a live match frame, which runs its own
//     nested event loop here until the player answers.
//   - A non-empty message from the controller is printed to stderr; "career_complete" ends
//     the program; the screen is redrawn only when the flow marks it dirty.
// The engine never draws and the UI never advances the engine on its own.
#include "app/app.hpp"

// Why the game cannot start: a message box, and the same text on stderr.
// Reports a fatal startup problem: prints it to stderr and shows it in a message box.
// Called for failures before or while the window is being set up.
static void show_start_error(const std::string& text) {
    std::fprintf(stderr, "%s\n", text.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenBMH", text.c_str(), nullptr);
}


#ifdef _WIN32
// The game is a Windows GUI program (no console window on double-click). Started from a
// console, it writes --version, --help and start errors there (kernel32 AttachConsole).
extern "C" __declspec(dllimport) int __stdcall AttachConsole(unsigned long process_id);
// Windows only: reconnects stdout/stderr to the parent console when they are not valid streams.
static void attach_parent_console() {
    // (an output redirected to a file or pipe stays there; a GUI program without one has no
    //  valid stream, fileno < 0)
    const bool out_free = _fileno(stdout) < 0, err_free = _fileno(stderr) < 0;
    if ((out_free || err_free) && AttachConsole(static_cast<unsigned long>(-1))) {  // ATTACH_PARENT_PROCESS
        std::FILE* f = nullptr;
        if (out_free) freopen_s(&f, "CONOUT$", "w", stdout);
        if (err_free) freopen_s(&f, "CONOUT$", "w", stderr);
    }
}
#else
static void attach_parent_console() {}
#endif

// =====================================================================================
// Startup and option parsing
// =====================================================================================
// Program entry. Order of work: parse the command line, find and validate the game data,
// start the optional debug log, then build the window/renderer/audio and the presenter
// lambdas, and finally run the main event loop.

int main(int argc, char** argv) {
    attach_parent_console();
    const std::vector<std::string> args = launch::utf8_arguments(argc, argv);  // (file names beyond the code page)
    fs::path data, save_directory;
    uint32_t seed = uint32_t(std::chrono::system_clock::now().time_since_epoch().count());
    bool muted = false, music_enabled = true, debug_mode = false;
    int live_frame_ms = 0;
    bool live_frame_ms_explicit = false;
    // Optional environment default for the pause per live-match frame; --live-frame-ms below
    // may override it.
    if (const char* configured = std::getenv("BMH_LIVE_FRAME_MS")) {
        live_frame_ms = std::clamp(std::atoi(configured), 0, 1000);
        live_frame_ms_explicit = true;
    }
    float volume = 1.0F;
    // Option loop (index 0 is the program name). Options that take a value only match when
    // another argument follows ("value"); a first plain argument that does not start with
    // "--" is taken as the game location unless --data already set it.
    for (size_t i = 1; i < args.size(); ++i) {
        const auto is = [&](const char* name) { return args[i] == name; };
        const bool value = i + 1 < args.size();
        if (is("--mute")) {
            muted = true;
        } else if (is("--music")) {
            music_enabled = true;
        } else if (is("--no-music")) {
            music_enabled = false;
        } else if (is("--live-frame-ms") && value) {
            live_frame_ms = std::clamp(std::atoi(args[++i].c_str()), 0, 1000);
            live_frame_ms_explicit = true;
        } else if (is("--data") && value) {
            data = launch::path_from_utf8(args[++i]);
        } else if (is("--seed") && value) {
            seed = uint32_t(std::strtoul(args[++i].c_str(), nullptr, 0));
        } else if (is("-debug") || is("--debug")) {
            debug_mode = true;
        } else if (is("--save-directory") && value) {
            save_directory = launch::path_from_utf8(args[++i]);
        } else if (is("--volume") && value) {
            volume = std::strtof(args[++i].c_str(), nullptr);
        } else if (is("--version")) {
            std::printf("OpenBMH %s\n", bmh::kOpenBmhVersion);
            return 0;
        } else if (is("--help") || is("-h")) {
            std::printf("usage: OpenBMH [<game directory | BMH.EXE | game.zip | game.7z>] [--seed <Watcom seed>] "
                        "[--save-directory <directory>] [--mute] [--volume <0..1>] [--no-music] [--live-frame-ms <0..1000>] [-debug]\n");
            return 0;
        } else if (args[i].rfind("--", 0) != 0 && data.empty()) {
            data = launch::path_from_utf8(args[i]);
        }
    }
    // Start the video session now; a failure is reported to the user and ends the program.
    std::optional<app::VideoSession> video;
    try { video.emplace(); }
    catch (const std::exception& error) { show_start_error(error.what()); return 1; }
    // the game: as given (directory, EXE or archive); else the directory OpenBMH itself lies in,
    // when it holds a playable game (the simplest install: openbmh next to BMH.EXE); else
    // chosen in the file dialog
    if (data.empty()) {
        if (char* base = SDL_GetBasePath()) {
            const fs::path here = launch::path_from_utf8(base);
            SDL_free(base);
            try {
                const launch::ResolvedGame game = launch::resolve_game(here, launch::default_cache_directory());
                if (game.error.empty() && game.check.playable) data = here;
            } catch (const std::exception&) {
                // (not a game directory: fall through to the file dialog)
            }
        }
    }
    if (data.empty()) {
        try {
            const auto chosen = launch::choose_game_file("OpenBMH");
            if (!chosen) return 0;  // cancelled
            data = *chosen;
        } catch (const std::exception& e) {
            show_start_error(std::string(e.what()) +
                             "\nStart OpenBMH with the game as argument: openbmh_play <game directory | BMH.EXE | game.zip>");
            return 1;
        }
    }
    // Validate the chosen game data; the resolved directory (possibly an unpacked cache copy)
    // replaces `data` from here on.
    bool english_game = false;  // (the window title marks the English version as experimental)
    {
        const launch::ResolvedGame game = launch::resolve_game(data, launch::default_cache_directory());
        if (!game.error.empty() || !game.check.playable) {
            show_start_error("The game cannot be started:\n" + launch::describe(game));
            return 1;
        }
        english_game = game.check.family == launch::GameFamily::english;
        // (from an archive: its unpacked copy in the cache, which then also keeps the saves)
        data = game.check.directory;
    }
    if (save_directory.empty()) save_directory = data;
    if (debug_mode) {
        // -debug: log in <saves>/debug, with a savegame after every match day (app/debug_log.hpp).
        if (!app::debug::init(save_directory / "debug")) std::fprintf(stderr, "debug folder cannot be created\n");
        app::debug::log(std::string("OpenBMH ") + bmh::kOpenBmhVersion + ", data " + data.string() + ", saves " + save_directory.string() + ", seed " + std::to_string(seed));
        SDL_version linked; SDL_GetVersion(&linked);
        app::debug::log("SDL " + std::to_string(linked.major) + "." + std::to_string(linked.minor) + "." + std::to_string(linked.patch) + ", platform " + SDL_GetPlatform());
    }
    // =====================================================================================
    // Window, renderer, audio and shared UI state
    // =====================================================================================
    // Everything below lives inside this try block; an exception thrown during setup or the
    // main loop ends in the catch handler at the end of main.
    try {
        const Palette palette = load_palette(ui::asset_path(data, "M3PC.COL"));
        ui::StartupFlow flow(data, save_directory);
        std::unique_ptr<app::CareerSession> session;
        uint64_t revision = 0;
        openbmh::audio::Playback playback;
        std::string audio_error;
        playback.initialize(audio_error);  // Audio is optional; the game continues without a device.
        playback.set_muted(muted);
        playback.set_volume(volume);
        std::optional<uint64_t> audio_session;
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        const std::string window_title = std::string("OpenBMH ") + bmh::kOpenBmhVersion +
                                  (english_game ? " - Football Limited (experimental)" : "");
        // 960x720 initial window = 3 times the 320x240 game canvas; resizable and high-DPI aware.
        SDL_Window* window = SDL_CreateWindow(window_title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!window) throw FormatError(SDL_GetError());
        // Prefer a vsynced renderer, fall back to the software renderer.
        SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer) throw FormatError(SDL_GetError());
        // The game always draws a 320x240 canvas; SDL scales it to the window and maps mouse
        // coordinates back to these logical units.
        SDL_RenderSetLogicalSize(renderer, 320, 240);
        // Streaming texture that holds the current canvas as ARGB (filled from `pixels`).
        SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 240);
        if (!texture) throw FormatError(SDL_GetError());
        app::PitchPointer pitch_pointer;
        std::vector<uint32_t> pixels(320 * 240);
        // The picture on the window (app/canvas.hpp): canvas.show(image, palette) converts, uploads and presents.
        app::Canvas canvas{renderer, texture, pixels};
        ui::LiveMatchdayScreen live_screen(data);
        ui::RealityReporterPanel reality_reporter(data);
        ui::SportschauIntro sportschau_intro(data);
        ui::MatchdayIntro matchday_intro(data);
        ui::SportschauEleven sportschau_eleven(data);
        ui::SportschauTeamInfo sportschau_team_info(data);
        ui::CompetitionWinnerScreen competition_winner(data);
        ui::set_settings_product_version(bmh::kOpenBmhVersion);
        const auto season_exe = load_le(ui::asset_path(data, "BMH.exe"));
        english_game = english_game || season_exe.u8(layout::LeagueSizes) == 20;
        {
            // The pointer of the original is its default 11x11 bitmap (index 0 transparent, hot spot
            // top left, set when a window object is created); the pitch page draws the same bitmap
            // with its own fill colour. The edition's own bitmap is used for the English version.
            std::array<uint32_t, 121> bitmap{};
            for (uint32_t i = 0; i < bitmap.size(); ++i) {
                const auto index = season_exe.u8(layout::OriginalPointerPixels + i);
                bitmap[i] = index == 0 ? 0 : palette.argb[index];
            }
            pitch_pointer.set_default(bitmap);
            pitch_pointer.show({}, renderer);
        }
        // A robot run (app/robot.hpp: OPENBMH_ROBOT, a test hook) plays a script and answers the quit question itself.
        app::Robot::instance().start(window);
        app::QuitConfirmation quit_confirmation(window, english_game,
            app::Robot::instance().active() ? app::QuitConfirmation::Presenter(app::Robot::confirm_quit)
                                            : app::QuitConfirmation::Presenter(SDL_ShowMessageBox));
        try {
            if (!playback.initialize_original(app::original_audio_assets(data, season_exe), audio_error))
                std::fprintf(stderr, "audio: %s\n", audio_error.c_str());
            playback.set_original_options({music_enabled, true, true});
            app::original_audio_command(playback, 0);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "audio: %s\n", error.what());
        }
        app::OriginalMouseSound mouse_sound(playback);
        const auto season_texts = load_textd(ui::asset_path(data, "TEXTD.BLK"));
        const auto process_clock_origin = std::chrono::steady_clock::now();
        // Everything from here on is App (above): the presenters and the event loop.
        App app{data, save_directory, seed, music_enabled, live_frame_ms, live_frame_ms_explicit, english_game, palette, flow, session, revision, playback, audio_error, audio_session, window, renderer, texture, pitch_pointer, pixels, canvas, live_screen, reality_reporter, sportschau_intro, matchday_intro, sportschau_eleven, sportschau_team_info, competition_winner, season_exe, quit_confirmation, mouse_sound, season_texts, process_clock_origin};
        return app.run();
    // Any exception from setup or the main loop is printed and ends with exit code 1.
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
