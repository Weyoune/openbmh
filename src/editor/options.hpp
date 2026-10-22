// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "editor/types.hpp"
namespace bmh::editor {
struct Options {
    std::string language = "de", theme = "midnight", token;
    bool mcp_enabled = true;
    int mcp_port = 8765;
    fs::path settings_file;
    static Options load(fs::path override_path = {});
    Json json(bool include_secret = false) const;
    void apply(const Json &);
    void save() const;
};
std::string tr(const std::string &language, const std::string &key);
} // namespace bmh::editor
