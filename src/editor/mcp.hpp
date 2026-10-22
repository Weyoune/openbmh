// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "editor/options.hpp"
#include "editor/session.hpp"
#include <memory>

namespace bmh::editor {
// A local Streamable HTTP MCP endpoint. GUI and MCP own no separate world
// state. Protocol 2025-11-25 (also negotiates 2025-06-18), no optional
// SSE/session state.
class McpServer {
  public:
    explicit McpServer(Session &);
    ~McpServer();
    McpServer(const McpServer &) = delete;
    void start(const Options &);
    void stop();
    int port() const;
    bool running() const;
    Json connection(const Options &) const;
    static Json tools();
    Json dispatch(const Json &request);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace bmh::editor
