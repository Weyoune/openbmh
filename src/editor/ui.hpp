// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "editor/mcp.hpp"
namespace bmh::editor {
struct UiConfig {
    fs::path assets, screenshot;
    std::string page = "overview";
    int quit_after_ms = 0;
};
int run_ui(Session &, Options &, McpServer &, const UiConfig &);
} // namespace bmh::editor
