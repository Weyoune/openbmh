// SPDX-License-Identifier: GPL-3.0-only
#include "editor/mcp.hpp"
#include <atomic>
#include <httplib.h>
#include <thread>

namespace bmh::editor {
namespace {
Json schema(Json properties = Json::object(), Json required = Json::array()) {
    if (properties.is_null())
        properties = Json::object();
    if (required.is_null())
        required = Json::array();
    return {{"type", "object"}, {"properties", properties}, {"required", required}, {"additionalProperties", false}};
}
Json field(const char *type, const char *description = "") { return {{"type", type}, {"description", description}}; }
Json rpc_error(const Json &id, int code, const std::string &message, Json data = Json::object()) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}, {"data", data}}}};
}
bool local_authority(const std::string &host) {
    for (auto name : {"127.0.0.1", "localhost", "[::1]"}) {
        const std::string base(name);
        if (host == base)
            return true;
        if (host.starts_with(base + ':')) {
            const auto port = host.substr(base.size() + 1);
            if (!port.empty() && port.find_first_not_of("0123456789") == port.npos)
                return true;
        }
    }
    return false;
}
bool local_origin(std::string value) {
    if (value.starts_with("http://"))
        value.erase(0, 7);
    else if (value.starts_with("https://"))
        value.erase(0, 8);
    else
        return false;
    return local_authority(value);
}
bool equal_secret(const std::string &a, const std::string &b) {
    if (a.size() != b.size())
        return false;
    unsigned difference = 0;
    for (size_t i = 0; i < a.size(); ++i)
        difference |= unsigned(a[i] ^ b[i]);
    return difference == 0;
}
const std::map<std::string, std::string> commands{{"editor_info", "info"},
                                                  {"editor_open", "open"},
                                                  {"editor_new", "new"},
                                                  {"editor_list", "list"},
                                                  {"editor_get", "get"},
                                                  {"editor_apply", "apply"},
                                                  {"editor_validate", "validate"},
                                                  {"editor_rules", "rules"},
                                                  {"editor_save", "save"},
                                                  {"editor_undo", "undo"},
                                                  {"editor_redo", "redo"},
                                                  {"editor_history", "history"},
                                                  {"editor_roster_move", "roster.move"},
                                                  {"editor_roster_swap", "roster.swap"},
                                                  {"editor_roster_order", "roster.order"},
                                                  {"editor_review_resolve", "review.resolve"},
                                                  {"editor_image_import", "asset.import"},
                                                  {"editor_image_edit", "asset.edit"},
                                                  {"editor_image_preview", "asset.preview"},
                                                  {"editor_image_export", "asset.export"}};
} // namespace
struct McpServer::Impl {
    explicit Impl(Session &value) : session(value) {}
    Session &session;
    std::unique_ptr<httplib::Server> http;
    std::thread worker;
    std::atomic<bool> active{false};
    int port = 0;
    std::string token;
};
McpServer::McpServer(Session &s) : impl_(std::make_unique<Impl>(s)) {}
McpServer::~McpServer() { stop(); }
int McpServer::port() const { return impl_->port; }
bool McpServer::running() const { return impl_->active; }
void McpServer::stop() {
    if (impl_->http)
        impl_->http->stop();
    if (impl_->worker.joinable())
        impl_->worker.join();
    impl_->active = false;
    impl_->http.reset();
    impl_->port = 0;
}
Json McpServer::connection(const Options &options) const {
    return {{"mcpServers",
             {{"openbmh-editor",
               {{"type", "http"},
                {"url", "http://127.0.0.1:" + std::to_string(port()) + "/mcp"},
                {"headers", {{"Authorization", "Bearer " + options.token}}}}}}}};
}
Json McpServer::tools() {
    Json result = Json::array();
    const auto revision = field("integer", "Optional optimistic concurrency guard; use revision "
                                           "from editor_info/get.");
    const auto id = field("string", "Stable world identifier; never an array index.");
    const auto table = field("string", "nations, competitions, clubs, players, stadiums, staff, "
                                       "assets, review, or read-only preserved tables.");
    auto add = [&](const char *name, const char *description, Json properties, Json required = Json::array(),
                   bool read_only = false) {
        result.push_back({{"name", name},
                          {"description", description},
                          {"inputSchema", schema(properties, required)},
                          {"annotations",
                           {{"readOnlyHint", read_only}, {"destructiveHint", !read_only}, {"openWorldHint", false}}}});
    };
    add("editor_info",
        "Current world, counts, dirty flag, monotonic revision and undo/redo "
        "state.",
        {}, {}, true);
    add("editor_open",
        "Open OBW archive, unpacked world, or legacy BMH/FLTD installation. "
        "Refuses unsaved replacement unless discard=true.",
        {{"path", field("string")},
         {"start_date", field("string")},
         {"discard", field("boolean")},
         {"expected_revision", revision}},
        {"path"});
    add("editor_new", "Create a blank editor world; never writes legacy files.",
        {{"title", field("string")}, {"discard", field("boolean")}, {"expected_revision", revision}});
    add("editor_list",
        "Search, filter, sort and page rows. Maximum page size 200; no data is "
        "truncated on disk.",
        {{"table", table},
         {"search", field("string")},
         {"nation", id},
         {"competition", id},
         {"team", id},
         {"role", field("string")},
         {"offset", field("integer")},
         {"limit", field("integer")},
         {"sort", field("string")},
         {"descending", field("boolean")},
         {"unresolved_only", field("boolean")}},
        {"table"}, true);
    add("editor_get", "Read a complete row, including preserved extensions and sources.",
        {{"table", table}, {"id", id}}, {"table", "id"}, true);
    add("editor_apply",
        "Atomic undoable transaction, max 5000 changes. Each change: table, "
        "op=create/update/delete, id. Create has data/name; update has set {JSON "
        "pointer:value}, unset [JSON pointer]. IDs are immutable. References and "
        "declared attribute ranges are checked before commit.",
        {{"changes", {{"type", "array"}, {"items", {{"type", "object"}}}, {"maxItems", 5000}}},
         {"expected_revision", revision}},
        {"changes"});
    add("editor_validate", "Structural errors plus non-blocking missing/source warnings.", {}, {}, true);
    add("editor_rules",
        "Read attribute systems and their actual ranges. Do not guess an "
        "attribute scale.",
        {}, {}, true);
    add("editor_history", "Shared GUI/MCP transaction history.", {}, {}, true);
    add("editor_save",
        "Atomically save only a new-format .obw. A Classic source requires a new "
        "filename. This editor profile is not executable in Classic 0.2.",
        {{"path", field("string")}, {"expected_revision", revision}});
    for (auto name : {"editor_undo", "editor_redo"})
        add(name, "Undo/redo one shared transaction.", {{"expected_revision", revision}});
    add("editor_roster_move", "Move one player to a club, or null for an explicitly chosen free agent.",
        {{"player", id},
         {"team", {{"type", {"string", "null"}}}},
         {"number", field("integer")},
         {"expected_revision", revision}},
        {"player", "team"});
    add("editor_roster_swap",
        "Exchange club, shirt number and squad order between two players "
        "atomically.",
        {{"first", id}, {"second", id}, {"expected_revision", revision}}, {"first", "second"});
    add("editor_roster_order",
        "Set complete club squad order; every assigned player must occur exactly "
        "once.",
        {{"team", id}, {"players", {{"type", "array"}, {"items", field("string")}}}, {"expected_revision", revision}},
        {"team", "players"});
    add("editor_review_resolve",
        "Choose a recorded source alternative and update its linked entity. "
        "Original claims remain preserved.",
        {{"id", id}, {"choice", field("integer")}, {"expected_revision", revision}}, {"id", "choice"});
    const auto crop = schema(
        {{"x", field("number")}, {"y", field("number")}, {"width", field("number")}, {"height", field("number")}});
    const auto transform = schema(
        {{"angle", field("number")}, {"flip_x", field("boolean")}, {"flip_y", field("boolean")}, {"crop", crop}});
    add("editor_image_import",
        "Import PNG/JPEG/WebP/SVG with optional arbitrary rotation, mirror, crop "
        "(normalized on the rotated canvas). Preserves source and recipe; "
        "identity import preserves exact bytes. target={table,id,field} "
        "optionally assigns the logical key.",
        {{"file", field("string")},
         {"key", id},
         {"target", field("object")},
         {"transform", transform},
         {"expected_revision", revision}},
        {"file"});
    add("editor_image_edit",
        "Re-edit an existing image using its preserved original. No repeated "
        "resampling. Optional source_key clones that source into key; target assigns it to one entity without changing "
        "other users of a shared fallback.",
        {{"key", id},
         {"source_key", id},
         {"target", field("object")},
         {"transform", transform},
         {"expected_revision", revision}},
        {"key", "transform"});
    add("editor_image_preview", "Return a PNG preview as an MCP image content block.",
        {{"key", id}, {"transform", transform}}, {"key"}, true);
    add("editor_image_export", "Export an image as PNG to an explicit filesystem path.",
        {{"key", id}, {"path", field("string")}}, {"key", "path"});
    return result;
}
Json McpServer::dispatch(const Json &request) {
    if (!request.is_object())
        return rpc_error(nullptr, -32600, "Invalid request");
    const auto id = request.value("id", Json());
    if (!request.is_object() || str(request, "jsonrpc") != "2.0" || !request.contains("method") ||
        !request["method"].is_string() || (request.contains("id") && !id.is_string() && !id.is_number_integer()))
        return rpc_error(id, -32600, "Invalid request");
    const auto method = str(request, "method");
    if (!request.contains("id"))
        return Json();
    const auto params = request.value("params", Json::object());
    if (!params.is_object())
        return rpc_error(id, -32602, "Invalid parameters");
    Json result;
    if (method == "initialize") {
        auto version = str(params, "protocolVersion");
        if (version != "2025-06-18")
            version = "2025-11-25";
        result = {{"protocolVersion", version},
                  {"capabilities", {{"tools", {{"listChanged", false}}}}},
                  {"serverInfo", {{"name", "OpenBMH Editor"}, {"version", "0.1.0-poc"}}},
                  {"instructions", "Edits share the native editor's undo history. Read rules for real "
                                   "attribute ranges. Save only OBW editor worlds. These are not Classic "
                                   "0.2 playable packages."}};
    } else if (method == "ping")
        result = Json::object();
    else if (method == "tools/list")
        result = {{"tools", tools()}};
    else if (method == "tools/call") {
        auto name = str(params, "name");
        if (!commands.contains(name))
            return rpc_error(id, -32602, "Unknown tool");
        auto args = params.value("arguments", Json::object());
        if (!args.is_object())
            return rpc_error(id, -32602, "Arguments must be an object");
        try {
            auto response = impl_->session.execute(commands.at(name), args);
            if (name == "editor_image_preview") {
                result = {{"content",
                           Json::array({{{"type", "image"}, {"mimeType", "image/png"}, {"data", response["data"]}}})},
                          {"isError", false}};
            } else
                result = {{"content", Json::array({{{"type", "text"}, {"text", response.dump()}}})},
                          {"structuredContent", response.is_object() ? response : Json{{"items", response}}},
                          {"isError", false}};
        } catch (const Error &e) {
            Json detail{{"code", e.code}, {"details", e.details}};
            result = {{"content", Json::array({{{"type", "text"}, {"text", detail.dump()}}})}, {"isError", true}};
        } catch (const std::exception &) {
            result = {{"content",
                       Json::array({{{"type", "text"}, {"text", "Invalid tool arguments or inaccessible file."}}})},
                      {"isError", true}};
        }
    } else
        return rpc_error(id, -32601, "Method not found");
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}
void McpServer::start(const Options &options) {
    stop();
    if (!options.mcp_enabled)
        return;
    impl_->token = options.token;
    auto server = std::make_unique<httplib::Server>();
    server->set_payload_max_length(8 * 1024 * 1024);
    server->set_read_timeout(10, 0);
    server->set_write_timeout(30, 0);
    server->new_task_queue = [] { return new httplib::ThreadPool(2); };
    auto authorized = [this](const httplib::Request &request, httplib::Response &response) {
        response.set_header("Cache-Control", "no-store");
        if (!local_authority(request.get_header_value("Host")) ||
            (request.has_header("Origin") && !local_origin(request.get_header_value("Origin")))) {
            response.status = 403;
            return false;
        }
        if (!equal_secret(request.get_header_value("Authorization"), "Bearer " + impl_->token)) {
            response.status = 401;
            response.set_header("WWW-Authenticate", "Bearer");
            return false;
        }
        return true;
    };
    server->Post("/mcp", [this, authorized](const httplib::Request &request, httplib::Response &response) {
        if (!authorized(request, response))
            return;
        if (!request.get_header_value("Content-Type").starts_with("application/json")) {
            response.status = 415;
            return;
        }
        const auto version = request.get_header_value("MCP-Protocol-Version");
        if (!version.empty() && version != "2025-11-25" && version != "2025-06-18" && version != "2025-03-26") {
            response.status = 400;
            response.set_content(rpc_error(nullptr, -32600, "Supported versions: 2025-11-25, 2025-06-18").dump(),
                                 "application/json");
            return;
        }
        try {
            auto input = Json::parse(request.body);
            if (!input.is_object()) {
                response.status = 400;
                response.set_content(rpc_error(nullptr, -32600, "Invalid request").dump(), "application/json");
                return;
            }
            auto output = dispatch(input);
            if (output.is_null()) {
                response.status = 202;
                return;
            }
            response.set_content(output.dump(), "application/json");
        } catch (const Json::exception &) {
            response.status = 400;
            response.set_content(rpc_error(nullptr, -32700, "Parse error").dump(), "application/json");
        } catch (const std::exception &) {
            response.status = 500;
            response.set_content(rpc_error(nullptr, -32603, "Internal error").dump(), "application/json");
        }
    });
    for (auto method : {0, 1}) {
        auto unsupported = [authorized](const httplib::Request &request, httplib::Response &response) {
            if (authorized(request, response)) {
                response.status = 405;
                response.set_header("Allow", "POST");
            }
        };
        if (method == 0)
            server->Get("/mcp", unsupported);
        else
            server->Delete("/mcp", unsupported);
    }
    impl_->port = options.mcp_port ? (server->bind_to_port("127.0.0.1", options.mcp_port) ? options.mcp_port : -1)
                                   : server->bind_to_any_port("127.0.0.1");
    if (impl_->port <= 0) {
        impl_->port = 0;
        throw Error("server_failed");
    }
    impl_->http = std::move(server);
    impl_->active = true;
    impl_->worker = std::thread([this] {
        impl_->http->listen_after_bind();
        impl_->active = false;
    });
}
} // namespace bmh::editor
