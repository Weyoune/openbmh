// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "editor/world.hpp"
#include <mutex>

namespace bmh::editor {
// One command boundary for native widgets, MCP and synthetic tests. No command
// owns game rules or operates on a live career.
class Session {
  public:
    Json execute(const std::string &command, const Json &args = Json::object());
    std::pair<std::string, std::string> asset_bytes(const std::string &key_or_path) const;

  private:
    struct Delta {
        std::string table, id;
        std::optional<Json> before, after;
    };
    struct FileDelta {
        std::string name;
        std::shared_ptr<const std::string> before, after;
    };
    struct Frame {
        std::string label;
        std::vector<Delta> changes;
        std::vector<FileDelta> files;
        uint64_t before = 0, after = 0;
    };
    mutable std::recursive_mutex mutex_;
    std::unique_ptr<World> world_;
    std::vector<Frame> history_;
    size_t cursor_ = 0;
    uint64_t revision_ = 0, world_generation_ = 0, state_ = 0, next_state_ = 1, saved_state_ = 0;
    fs::path saved_path_;
    fs::file_time_type saved_time_{};
    uint64_t saved_size_ = 0;
    Json comparison_cache_;
    std::string comparison_key_;
    std::optional<uint64_t> comparison_revision_;
    World &world();
    bool dirty() const;
    Json info() const;
    Json apply(const Json &, std::vector<FileDelta> files = {}, const std::string &label = "edit");
    void frame(const Frame &, bool forward);
    void replace(World value);
    Json import_image(const Json &);
};
} // namespace bmh::editor
