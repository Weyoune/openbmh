// SPDX-License-Identifier: GPL-3.0-only
#include "editor/ui.hpp"
#include "editor/version.hpp"
#include <SDL.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace {
std::atomic<bool> stopped{false};
void stop(int) { stopped = true; }
} // namespace
int main(int argc, char **argv) {
    using namespace bmh::editor;
    std::string language = "de";
    try {
        bool headless = false, serve = false, stdio = false, demo = false, blank = false, validate = false,
             print = false;
        fs::path input, output, settings, script;
        UiConfig ui;
        int port = -1;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            auto next = [&]() -> std::string {
                if (++i >= argc)
                    throw Error("invalid_change", {{"argument", arg}});
                return argv[i];
            };
            if (arg == "--version") {
                std::cout << editor_product() << '\n';
                return 0;
            } else if (arg == "--headless")
                headless = true;
            else if (arg == "--serve")
                serve = headless = true;
            else if (arg == "--stdio-mcp")
                stdio = headless = true;
            else if (arg == "--open")
                input = path(next());
            else if (arg == "--output")
                output = path(next());
            else if (arg == "--settings")
                settings = path(next());
            else if (arg == "--commands")
                script = path(next());
            else if (arg == "--mcp-port")
                port = std::stoi(next());
            else if (arg == "--demo")
                demo = true;
            else if (arg == "--new")
                blank = true;
            else if (arg == "--validate")
                validate = true;
            else if (arg == "--print-mcp-config")
                print = true;
            else if (arg == "--assets")
                ui.assets = path(next());
            else if (arg == "--screenshot")
                ui.screenshot = path(next());
            else if (arg == "--page")
                ui.page = next();
            else if (arg == "--quit-after-ms")
                ui.quit_after_ms = std::stoi(next());
            else if (arg == "--help") {
                std::cout << editor_product() << '\n'
                          << "OpenBMHEditor [--open world.obw|installation] [--demo|--new]\n"
                             "  [--settings file] [--assets directory] [--page "
                             "clubs|players|...]\n"
                             "  [--headless --validate] [--output world.obw] [--commands "
                             "script.json]\n"
                             "  [--serve --mcp-port 8765] [--stdio-mcp] "
                             "[--print-mcp-config] [--version]\n";
                return 0;
            } else if (!arg.starts_with('-') && input.empty())
                input = path(arg);
            else
                throw Error("invalid_change", {{"argument", arg}});
        }
        auto options = Options::load(settings);
        language = options.language;
        if (port >= 0) {
            options.mcp_port = port;
            options.mcp_enabled = true;
        }
        Session session;
        if (!input.empty())
            session.execute("open", {{"path", utf8(input)}});
        else if (demo || blank)
            session.execute(demo ? "demo" : "new");
        if (!script.empty()) {
            const auto actions = Json::parse(read_file(script, 8 * 1024 * 1024));
            if (!actions.is_array())
                throw Error("invalid_change");
            for (const auto &action : actions) {
                auto result = session.execute(str(action, "command"), action.value("args", Json::object()));
                if (headless && !stdio)
                    std::cout << result.dump() << '\n';
            }
        }
        bool has_errors = false;
        if (validate) {
            auto result = session.execute("validate");
            for (auto &row : result["diagnostics"])
                if (row["severity"] == "error")
                    has_errors = true;
            if (headless && !stdio)
                std::cout << result.dump() << '\n';
        }
        if (!output.empty())
            session.execute("save", {{"path", utf8(output)}});
        McpServer server(session);
        if (stdio) {
            std::string line;
            while (std::getline(std::cin, line)) {
                try {
                    auto reply = server.dispatch(Json::parse(line));
                    if (!reply.is_null())
                        std::cout << reply.dump() << std::endl;
                } catch (const std::exception &) {
                    std::cout << "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-"
                                 "32700,\"message\":\"Parse error\"}}"
                              << std::endl;
                }
            }
            return 0;
        }
        if (serve || !headless) {
            try {
                server.start(options);
            } catch (const Error &e) {
                std::cerr << tr(language, e.code) << '\n';
                if (serve)
                    throw;
            }
        }
        if (print)
            std::cout << server.connection(options).dump(2) << '\n';
        if (serve) {
            std::cerr << "OpenBMH Editor MCP: http://127.0.0.1:" << server.port() << "/mcp\n";
            std::signal(SIGINT, stop);
            std::signal(SIGTERM, stop);
            while (!stopped)
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
        } else if (!headless) {
            if (ui.assets.empty()) {
                char *base = SDL_GetBasePath();
                ui.assets = (base ? path(base) : fs::absolute(path(argv[0])).parent_path()) / "assets/editor";
                SDL_free(base);
            }
            return run_ui(session, options, server, ui);
        }
        return has_errors ? 2 : 0;
    } catch (const Error &e) {
        std::cerr << Json{{"error", e.code}, {"message", tr(language, e.code)}, {"details", e.details}}.dump() << '\n';
        return 1;
    } catch (const std::exception &e) {
        std::cerr << tr(language, "error") << ": " << e.what() << '\n';
        return 1;
    }
}
