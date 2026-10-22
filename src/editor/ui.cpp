// SPDX-License-Identifier: GPL-3.0-only
#include "editor/ui.hpp"
#include "editor/images.hpp"
#include "launch/rml_path.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi_Platform_SDL.h>
#include <RmlUi_Renderer_GL3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <future>
#include <nfd.hpp>
#include <sstream>

namespace bmh::editor {
namespace {
std::string number(double n) {
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s << n;
    return s.str();
}
std::string button(const std::string &action, const std::string &label, const std::string &css = "",
                   const std::string &attributes = "") {
    return "<button data-action=\"" + escaped(action) + "\" class=\"" + css + "\" " + attributes + ">" +
           escaped(label) + "</button>";
}
struct Video {
    SDL_Window *window = nullptr;
    SDL_GLContext gl = nullptr;
    bool loader = false;
    Video() {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0)
            throw Error("video_failed");
        SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        window =
            SDL_CreateWindow("OpenBMH Editor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1440, 900,
                             SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
        if (!window)
            throw Error("video_failed");
        SDL_SetWindowMinimumSize(window, 1080, 720);
        gl = SDL_GL_CreateContext(window);
        if (!gl)
            throw Error("video_failed");
        SDL_GL_MakeCurrent(window, gl);
        SDL_GL_SetSwapInterval(1);
        std::string error;
        if (!RmlGL3::Initialize(&error))
            throw Error("video_failed", {{"detail", error}});
        loader = true;
        SDL_StartTextInput();
    }
    ~Video() {
        if (loader)
            RmlGL3::Shutdown();
        if (gl)
            SDL_GL_DeleteContext(gl);
        if (window)
            SDL_DestroyWindow(window);
        SDL_Quit();
    }
};
class Renderer final : public RenderInterface_GL3 {
  public:
    Session &session;
    std::map<std::string, std::pair<std::string, std::string>> memory;
    std::map<std::string, std::string> logical;
    uint64_t serial = 0;
    explicit Renderer(Session &s) : session(s) {}
    std::string asset(const std::string &key) {
        auto token = "world-image-" + std::to_string(serial++) + ".png";
        logical[token] = key;
        return token;
    }
    std::string bytes(std::string data, const std::string &extension) {
        auto token = "memory-image-" + std::to_string(serial++) + ".png";
        memory[token] = {std::move(data), extension};
        return token;
    }
    void clear() {
        Rml::ReleaseTextures(this);
        logical.clear();
        memory.clear();
    }
    Rml::TextureHandle LoadTexture(Rml::Vector2i &dimensions, const Rml::String &source) override {
        const auto token = utf8(path(source).filename());
        if (!memory.contains(token) && !logical.contains(token))
            return RenderInterface_GL3::LoadTexture(dimensions, source);
        try {
            auto value = memory.contains(token) ? memory.at(token) : session.asset_bytes(logical.at(token));
            auto raster = images::decode(value.first, value.second, token.starts_with("icon-") ? 64 : 512);
            if (logical.contains(token) && raster.width != raster.height) {
                const int side = std::max(raster.width, raster.height);
                images::Raster square{side, side, std::vector<uint8_t>(size_t(side) * side * 4)};
                for (int y = 0; y < raster.height; ++y)
                    std::copy_n(raster.pixels.data() + size_t(y) * raster.width * 4, raster.width * 4,
                                square.pixels.data() +
                                    (size_t(y + (side - raster.height) / 2) * side + (side - raster.width) / 2) * 4);
                raster = std::move(square);
            }
            for (size_t i = 0; i < raster.pixels.size(); i += 4)
                for (int c = 0; c < 3; ++c)
                    raster.pixels[i + c] = uint8_t((unsigned(raster.pixels[i + c]) * raster.pixels[i + 3] + 127) / 255);
            dimensions = {raster.width, raster.height};
            return GenerateTexture(raster.pixels, dimensions);
        } catch (const std::exception &) {
            return {};
        }
    }
};
class EditorUi final : public Rml::EventListener {
    Session &session_;
    Options &options_;
    McpServer &server_;
    UiConfig config_;
    Video video_;
    bmh::rml::AssetFiles files_;
    SystemInterface_SDL system_{video_.window};
    Renderer renderer_;
    TextInputMethodEditor_SDL ime_;
    Rml::Context *context_ = nullptr;
    Rml::ElementDocument *doc_ = nullptr;
    bool initialized_ = false, closed_ = false, busy_ = false, modal_ = false;
    bool roster24_ = false, show_source_ = false, needs_refresh_ = true;
    uint64_t revision_ = uint64_t(-1);
    Json info_, selected_ = Json::object(), listed_ = Json::array();
    std::map<std::string, Json> lookup_;
    std::string page_ = "overview", selected_id_, search_, nation_, league_, team_, sort_ = "name", remembered_;
    bool descending_ = false;
    int offset_ = 0;
    size_t total_ = 0;
    struct Field {
        std::string id, pointer, kind, initial;
        Json original;
    };
    std::vector<Field> fields_;
    std::deque<std::pair<std::string, std::string>> pending_;
    std::future<Json> work_;
    std::function<void(const Json &)> completed_;
    std::function<void()> modal_accept_, discard_continuation_;
    std::string image_bytes_, image_extension_, image_file_, image_key_, image_source_key_;
    Json image_target_, image_recipe_;
    double drag_x_ = 0, drag_y_ = 0;
    bool dragging_ = false;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
    std::string t(const std::string &key) const { return tr(options_.language, key); }
    Rml::Element *el(const std::string &id) const {
        auto *value = doc_->GetElementById(id);
        if (!value)
            throw std::runtime_error("Missing editor element: " + id);
        return value;
    }
    void html(const std::string &id, const std::string &value) { el(id)->SetInnerRML(value); }
    void label(const std::string &id, const std::string &value) { html(id, escaped(value)); }
    std::string value(const std::string &id) const {
        auto *field = dynamic_cast<Rml::ElementFormControl *>(el(id));
        return field ? field->GetValue() : "";
    }
    void set_value(const std::string &id, const std::string &value) {
        if (auto *field = dynamic_cast<Rml::ElementFormControl *>(el(id)))
            field->SetValue(value);
    }
    std::string table() const { return page_ == "roster" ? "players" : page_; }
    static std::string identity(const Json &row, const std::string &table) {
        return str(row, table == "assets" ? "key" : "id");
    }
    std::string title(const Json &row) const {
        auto name = row.value("name", Json());
        if (name.is_object() && name.contains(options_.language) && name[options_.language].is_string())
            return name[options_.language];
        auto result = text(name);
        return result.empty() ? str(row, "key", str(row, "id")) : result;
    }
    std::string ref(const Json &row, const std::string &key) const {
        const auto id = str(row, key);
        auto found = lookup_.find(id);
        return found == lookup_.end() ? id : title(found->second);
    }
    std::string img(const std::string &key, const std::string &css) {
        return "<img class=\"" + css + "\" src=\"" + renderer_.asset(key) + "\"/>";
    }
    std::string icon(const std::string &name) {
        static const std::map<std::string, std::string> paths{
            {"overview", "M3 3h7v7H3zM14 3h7v7h-7zM3 14h7v7H3zM14 14h7v7h-7z"},
            {"nations", "M21 12a9 9 0 1 1-18 0 9 9 0 0 1 18 0M3 12h18M12 3c-5 5-5 "
                        "13 0 18M12 3c5 5 5 13 0 18"},
            {"competitions", "M7 3h10v6c0 6-10 6-10 0zM7 5H3v3c0 3 4 4 4 4M17 "
                             "5h4v3c0 3-4 4-4 4M12 14v6M7 21h10"},
            {"clubs", "M4 3h16v10c0 5-8 9-8 9S4 18 4 13zM8 8h8M8 12h8"},
            {"players", "M16 7a4 4 0 1 1-8 0 4 4 0 0 1 8 0M4 22v-3c0-8 16-8 16 0v3"},
            {"roster", "M3 3h18v18H3zM9 3v18M3 9h18M3 15h18"},
            {"stadiums", "M3 8c0-7 18-7 18 0v8c0 7-18 7-18 0zM3 8c0 7 18 7 18 0M8 "
                         "13v7M16 13v7"},
            {"staff", "M5 22V3h13l-4 5 4 5H5"},
            {"assets", "M3 3h18v18H3zM3 18l6-7 5 5 3-3 4 5M17 6v2"},
            {"review", "M12 2 1 21h22zM12 8v6M12 17v1"},
            {"changes", "M4 5v6h6M4 11a9 9 0 1 1 1 7"}};
        const auto token = "icon-" + options_.theme + '-' + name + ".png";
        if (!renderer_.memory.contains(token)) {
            auto color = options_.theme == "silver" ? "#537588" : options_.theme == "matchday" ? "#d3b3b0" : "#9cb9c8";
            renderer_.memory[token] = {"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" "
                                       "height=\"24\" viewBox=\"0 0 24 24\"><path d=\"" +
                                           paths.at(name) + "\" fill=\"none\" stroke=\"" + color +
                                           "\" stroke-width=\"1.5\" stroke-linecap=\"round\" "
                                           "stroke-linejoin=\"round\"/></svg>",
                                       ".svg"};
        }
        return "<img class=\"glyph\" src=\"" + token + "\"/>";
    }
    std::string image_key(const Json &row) const {
        if (table() == "assets")
            return str(row, "key");
        if (table() == "players" || table() == "staff")
            return str(row, "photo", "asset:player/" + str(row, "id") + "/photo");
        if (table() == "competitions")
            return str(row, "trophy", "asset:trophy/generic");
        return str(row, "crest", "asset:club/" + str(row, "id") + "/crest");
    }
    Json all(const std::string &table) {
        Json rows = Json::array();
        size_t offset = 0;
        do {
            auto page = session_.execute("list", {{"table", table}, {"offset", offset}, {"limit", 200}});
            for (auto &row : page["items"])
                rows.push_back(row);
            offset += page["items"].size();
            if (offset >= page["total"].get<size_t>())
                break;
        } while (true);
        return rows;
    }
    void refresh_lookup() {
        lookup_.clear();
        if (!info_.value("loaded", false))
            return;
        for (auto name : {"nations", "competitions", "clubs", "stadiums"})
            for (auto &row : all(name))
                lookup_[str(row, "id")] = row;
    }
    std::string choices(const std::string &name, const std::string &chosen, const std::string &empty = "",
                        const std::string &nation = "") const {
        std::string result =
            "<option value=\"\"" + std::string(chosen.empty() ? " selected" : "") + ">" + escaped(empty) + "</option>";
        std::vector<std::pair<std::string, std::string>> values;
        bool found = chosen.empty();
        for (auto &[id, row] : lookup_) {
            bool matches = name == "nations"        ? id.starts_with("nation:")
                           : name == "competitions" ? id.starts_with("comp:") && str(row, "type") == "league"
                           : name == "clubs"        ? id.starts_with("club:") || id.starts_with("team:")
                                                    : id.starts_with("stadium:");
            if (!matches || (!nation.empty() && str(row, "nation") != nation && id != chosen))
                continue;
            values.emplace_back(id, title(row));
        }
        std::stable_sort(values.begin(), values.end(),
                         [](auto &a, auto &b) { return folded(a.second) < folded(b.second); });
        for (auto &[id, name] : values) {
            found |= id == chosen;
            result += "<option value=\"" + escaped(id) + "\"" + (id == chosen ? " selected" : "") + ">" +
                      escaped(name) + "</option>";
        }
        if (!found)
            result += "<option value=\"" + escaped(chosen) + "\" selected>" + escaped(chosen) + "</option>";
        return result;
    }
    void resize() {
        int w, h, pw, ph;
        SDL_GetWindowSize(video_.window, &w, &h);
        SDL_GL_GetDrawableSize(video_.window, &pw, &ph);
        if (!w || !h)
            return;
        renderer_.SetViewport(pw, ph);
        context_->SetDimensions({pw, ph});
        context_->SetDensityIndependentPixelRatio(float(pw) / w *
                                                  std::clamp(std::min(w / 1440.f, h / 900.f), .78f, 1.8f));
    }
    void close_modal() {
        el("overlay")->SetProperty("display", "none");
        modal_ = dragging_ = false;
        modal_accept_ = {};
    }
    void modal(const std::string &title, const std::string &body, const std::string &accept,
               std::function<void()> callback, bool cancel = true, bool wide = false) {
        label("modal-title", title);
        html("modal-content", body);
        label("modal-error", "");
        el("modal")->SetClass("wide", wide);
        html("modal-buttons", (cancel ? button("cancel-modal", t("cancel")) : "") +
                                  (accept.empty() ? "" : button("accept-modal", accept, "primary")));
        modal_accept_ = std::move(callback);
        modal_ = true;
        el("overlay")->SetProperty("display", "flex");
        context_->Update();
    }
    void error(const std::exception &e) {
        const auto *typed = dynamic_cast<const Error *>(&e);
        auto message = t(typed ? typed->code : "invalid_change");
        if (typed && !typed->details.empty())
            message += "\n" + typed->details.dump(2).substr(0, 4000);
        if (modal_)
            label("modal-error", message);
        else
            modal(t("error"), "<p>" + escaped(message) + "</p>", t("close"), [this] { close_modal(); }, false);
    }
    void background(const std::string &command, Json args, std::function<void(const Json &)> callback = {}) {
        if (busy_)
            return;
        busy_ = true;
        completed_ = std::move(callback);
        label("status-message", t("loading"));
        work_ = std::async(std::launch::async, [this, command, args] { return session_.execute(command, args); });
    }
    std::string native_dialog(const std::string &kind) {
        NFD::Guard guard;
        NFD::UniquePathU8 result;
        nfdresult_t status;
        if (kind == "folder")
            status = NFD::PickFolder(result);
        else if (kind == "save") {
            const nfdu8filteritem_t filter[] = {{"OpenBMH world", "obw"}};
            status = NFD::SaveDialog(result, filter, 1, nullptr, "world.obw");
        } else if (kind == "image") {
            const nfdu8filteritem_t filter[] = {{"Images", "png,jpg,jpeg,webp,svg"}};
            status = NFD::OpenDialog(result, filter, 1);
        } else if (kind == "export") {
            const nfdu8filteritem_t filter[] = {{"PNG", "png"}};
            status = NFD::SaveDialog(result, filter, 1, nullptr, "image.png");
        } else {
            const nfdu8filteritem_t filter[] = {{"OpenBMH world", "obw"}};
            status = NFD::OpenDialog(result, filter, 1);
        }
        if (status == NFD_CANCEL)
            return "";
        if (status != NFD_OKAY)
            throw Error("dialog_failed");
        return result.get();
    }
    void save(bool save_as = false, std::function<void()> after = {}) {
        auto file = save_as ? "" : str(session_.execute("info"), "path");
        if (file.empty())
            file = native_dialog("save");
        if (file.empty())
            return;
        if (folded(utf8(path(file).extension())) != ".obw")
            file += ".obw";
        background("save", {{"path", file}}, [this, after](const Json &) {
            close_modal();
            label("status-message", t("saved_ok"));
            if (after)
                after();
        });
    }
    void leaving(std::function<void()> after) {
        if (has_draft())
            apply_fields();
        if (!session_.execute("info").value("dirty", false)) {
            after();
            return;
        }
        discard_continuation_ = after;
        modal(t("dirty_prompt"), "<p>" + escaped(t("dirty_detail")) + "</p>", t("save"),
              [this, after] { save(false, after); });
        html("modal-buttons", button("cancel-modal", t("cancel")) + button("discard-world", t("discard")) +
                                  button("accept-modal", t("save"), "primary"));
    }
    void chrome() {
        doc_->SetClassNames(options_.theme);
        label("tagline", t("poc"));
        label("nav-label", t("data_areas"));
        label("profile-note", t("not_playable"));
        label("world-caption", info_.value("loaded", false) ? str(info_, "title") : "");
        html("top-actions", button("new", t("new_world"), "quiet") + button("open", t("open")) +
                                button("save", t("save"), "primary", info_.value("loaded", false) ? "" : "disabled") +
                                button("options", t("options"), "quiet"));
        std::string nav;
        for (const std::string name : {"overview", "nations", "competitions", "clubs", "players", "roster", "stadiums",
                                       "staff", "assets", "review", "changes"})
            nav += "<button class=\"nav " + std::string(name == page_ ? "selected" : "") +
                   "\" data-action=\"navigate\" data-value=\"" + name + "\">" + icon(name) + "<span>" +
                   escaped(t(name)) + "</span></button>";
        html("navigation", nav);
        label("page-kicker", "OpenBMH / " + t(page_));
        label("page-title", t(page_));
        label("page-subtitle", page_ == "roster"   ? t("roster_hint")
                               : page_ == "review" ? t("review_hint")
                                                   : t("subtitle_" + page_));
        label("state", info_.value("dirty", false) ? "*  " + t("unsaved") : t("saved"));
        html("bottom-actions",
             button("undo", t("undo"), "quiet", info_.value("can_undo", false) ? "" : "disabled") +
                 button("redo", t("redo"), "quiet", info_.value("can_redo", false) ? "" : "disabled") +
                 button("options", "MCP · " + t(server_.running() ? "online" : "offline"), "quiet"));
    }
    void overview() {
        html("toolbar", "");
        html("pagination", "");
        html("page-actions",
             info_.value("loaded", false) ? button("validate", t("validate")) + button("save-as", t("save_as")) : "");
        std::string body = "<div class=\"dashboard\"><div class=\"hero\"><div "
                           "class=\"eyebrow\">OPENBMH WORLD STUDIO</div><h1>" +
                           escaped(t("workspace")) + "</h1><p>" +
                           escaped(info_.value("loaded", false) ? str(info_, "title") : t("welcome_hint")) +
                           "</p><div class=\"button-row\">" + button("open-file", t("open_world"), "primary") +
                           button("import-folder", t("import_legacy")) + button("demo", t("demo"), "quiet") +
                           "</div></div>";
        if (info_.value("loaded", false)) {
            body += "<div class=\"metrics\">";
            for (auto name : {"nations", "competitions", "clubs", "players", "stadiums", "staff", "assets", "review"})
                body += "<button class=\"metric\" data-action=\"navigate\" data-value=\"" + std::string(name) + "\">" +
                        escaped(t(name)) + "<span>" + std::to_string(info_["counts"].value(name, 0)) +
                        "</span></button>";
            body += "</div><div class=\"note\">" + escaped(t("not_playable")) + "<br/>" +
                    escaped(t("preservation_note")) + "</div>";
        }
        body += "</div>";
        html("page-content", body);
    }
    void field(std::string &html, const std::string &pointer, const std::string &caption,
               const std::string &kind = "text", const std::string &choice_table = "") {
        const auto id = "property-" + std::to_string(fields_.size());
        const auto jp = Json::json_pointer(pointer);
        Json original = selected_.contains(jp) ? selected_[jp] : Json();
        std::string val = original.is_null()     ? ""
                          : original.is_string() ? original.get<std::string>()
                                                 : original.dump();
        fields_.push_back({id, pointer, kind, val, original});
        html += "<div class=\"form-field\"><label for=\"" + id + "\">" + escaped(caption) + "</label>";
        if (kind == "select")
            html += "<select id=\"" + id + "\">" + choices(choice_table, val, t("unknown")) + "</select>";
        else if (kind == "bool")
            html += "<select id=\"" + id + "\"><option value=\"true\"" + (val == "true" ? " selected" : "") + ">" +
                    t("yes") + "</option><option value=\"false\"" + (val != "true" ? " selected" : "") + ">" + t("no") +
                    "</option></select>";
        else if (kind == "json")
            html += "<textarea id=\"" + id + "\">" + escaped(val) + "</textarea>";
        else
            html += "<input id=\"" + id + "\" type=\"text\" value=\"" + escaped(val) + "\"/>";
        html += "</div>";
    }
    void name_field(std::string &body) {
        if (selected_.value("name", Json()).is_object()) {
            if (selected_["name"].contains("first") || selected_["name"].contains("last")) {
                field(body, "/name/first", t("first"));
                field(body, "/name/last", t("last"));
                field(body, "/name/known_as", t("known_as"));
            } else
                field(body, "/name/default", t("name"));
        } else
            field(body, "/name", t("name"));
    }
    std::string inspector() {
        fields_.clear();
        if (selected_.empty())
            return "<div class=\"inspector\"><div class=\"empty\">" + escaped(t("select_record")) + "</div></div>";
        std::string body = "<div class=\"inspector\"><div class=\"inspector-head\">";
        if (table() == "players" || table() == "clubs" || table() == "competitions" || table() == "assets")
            body += img(table() == "players" && selected_.contains("appearance") ? "portrait:" + selected_id_
                                                                                 : image_key(selected_),
                        "hero-image");
        body += "<div><h2>" + escaped(title(selected_)) + "</h2><small>" + escaped(selected_id_) +
                "</small></div></div><div class=\"inspector-scroll\">";
        if (page_ == "review") {
            body += "<p>" + escaped(str(selected_, "reason", str(selected_, "field"))) +
                    "</p><div class=\"form-field\"><label>" + escaped(t("source")) +
                    "</label><select id=\"review-choice\">";
            int i = 0;
            for (const auto &alternative : selected_.value("alternatives", Json::array())) {
                body += "<option value=\"" + std::to_string(i++) + "\">" +
                        escaped(str(alternative, "source", "?") + ": " +
                                (alternative.value("value", Json()).is_string()
                                     ? alternative["value"].get<std::string>()
                                     : alternative.value("value", Json()).dump())) +
                        "</option>";
            }
            body += "</select></div>" + button("resolve", t("resolve"), "primary") + "<div class=\"source-json\">" +
                    escaped(selected_.dump(2)) + "</div>";
        } else {
            name_field(body);
            if (table() == "nations") {
                field(body, "/confederation", t("confederation"));
                field(body, "/playable", t("playable"), "bool");
            } else if (table() == "competitions") {
                field(body, "/nation", t("nation"), "select", "nations");
                field(body, "/type", t("type"));
                field(body, "/tier", t("tier"), "integer");
                field(body, "/visible", t("visible"), "bool");
                field(body, "/trophy", t("trophy"));
            } else if (table() == "clubs") {
                field(body, "/short_name", t("short_name"));
                field(body, "/nation", t("nation"), "select", "nations");
                field(body, "/competition", t("competition"), "select", "competitions");
                field(body, "/stadium", t("stadium"), "select", "stadiums");
                field(body, "/founded", t("founded"), "integer");
                field(body, "/colors", t("colors"), "json");
            } else if (table() == "players") {
                field(body, "/born", t("born"));
                if (selected_.contains("age_at_start"))
                    field(body, "/age_at_start", t("age_at_start"),
                          selected_["age_at_start"].is_object() ? "json" : "integer");
                field(body, "/nationalities", t("nationalities"), "json");
                field(body, "/positions", t("positions"), "json");
                field(body, "/team", t("team"), "select", "clubs");
                field(body, "/squad_number", t("squad_number"), "integer");
                if (page_ == "roster") {
                    body += "<div class=\"button-row\">" + button("remember", t("remember_player")) + "</div>";
                    if (!remembered_.empty() && remembered_ != selected_id_)
                        body += button("swap", t("swap_player"));
                    body +=
                        "<div class=\"button-row\">" + button("order-up", "↑") + button("order-down", "↓") + "</div>";
                }
                body += "<div class=\"form-section\">" + escaped(t("attributes")) + " · " +
                        escaped(str(selected_, "attribute_set")) + "</div>";
                auto attributes = selected_.value("attributes", Json::object());
                Json definitions = Json::object();
                for (const auto &rule : session_.execute("rules")) {
                    auto id = str(rule, "attribute_set", str(rule, "id"));
                    if (id == str(selected_, "attribute_set") || id == "rule:" + str(selected_, "attribute_set"))
                        definitions = rule.value("fields", Json::object());
                }
                for (auto it = attributes.begin(); it != attributes.end(); ++it) {
                    auto range = definitions.value(it.key(), Json::object());
                    const auto bounds = range.contains("min") && range.contains("max")
                                            ? "  (" + range["min"].dump() + "–" + range["max"].dump() + ")"
                                            : "";
                    field(body, "/attributes/" + it.key(), t(it.key()) + bounds, "integer");
                }
                if (attributes.empty())
                    body += "<p class=\"form-help\">" + escaped(t("unrated_hint")) + "</p>";
                field(body, "/attribute_set", t("attribute_set"));
                if (attributes.empty() || show_source_)
                    field(body, "/attributes", t("attributes"), "json");
            } else if (table() == "stadiums") {
                field(body, "/owner", t("owner"), "select", "clubs");
                for (auto key : {"total", "seated", "standing", "boxes"})
                    field(body, "/capacity/" + std::string(key), t(key), "integer");
                field(body, "/facilities", t("facilities"), "json");
            } else if (table() == "staff") {
                field(body, "/nation", t("nation"), "select", "nations");
                field(body, "/role", t("role"));
                field(body, "/attributes", t("attributes"), "json");
            } else if (table() == "assets") {
                field(body, "/source", t("source"));
                field(body, "/author", t("author"));
                field(body, "/license", t("license"));
                body += "<div class=\"form-help\">" +
                        escaped(selected_.value("width", Json()).dump() + " × " +
                                selected_.value("height", Json()).dump()) +
                        "</div>";
            }
            if (table() == "players" || table() == "clubs" || table() == "competitions" || table() == "assets") {
                body += "<div class=\"form-section\">" + escaped(t("assets")) + "</div><div class=\"button-row\">" +
                        button("image-import", t("image_import")) + button("image-edit", t("image_edit")) + "</div>" +
                        button("image-export", t("image_export"), "quiet");
            }
            body += "<div class=\"form-section\">" + button("toggle-source", t("provenance"), "quiet") + "</div>";
            if (show_source_)
                body += "<div class=\"source-json\">" + escaped(selected_.dump(2)) + "</div>";
        }
        body += "</div>";
        if (page_ != "review")
            body += "<div class=\"inspector-actions\">" + button("delete", t("delete"), "danger") +
                    button("apply", t("apply"), "primary") + "</div>";
        return body + "</div>";
    }
    void records() {
        if (!info_.value("loaded", false)) {
            overview();
            return;
        }
        html("page-actions", page_ == "review"
                                 ? button("validate", t("validate"))
                                 : button("duplicate", t("duplicate")) + button("add", t("add"), "primary"));
        std::string toolbar = "<input id=\"search\" type=\"text\" value=\"" + escaped(search_) + "\" placeholder=\"" +
                              escaped(t("search")) + "\"/>";
        if (table() == "clubs" || table() == "players" || table() == "competitions")
            toolbar += "<select id=\"filter-nation\">" + choices("nations", nation_, t("all_nations")) + "</select>";
        if (table() == "clubs" || table() == "players")
            toolbar += "<select id=\"filter-league\">" + choices("competitions", league_, t("all_leagues"), nation_) +
                       "</select>";
        if (table() == "players")
            toolbar += "<select id=\"filter-team\">" + choices("clubs", team_, t("all_clubs"), nation_) + "</select>";
        if (page_ == "roster")
            toolbar += button("view24", "24", roster24_ ? "primary" : "");
        html("toolbar", toolbar);
        Json args{{"table", table()},          {"search", search_}, {"nation", nation_},
                  {"competition", league_},    {"team", team_},     {"sort", sort_},
                  {"descending", descending_}, {"offset", offset_}, {"limit", roster24_ ? 24 : 200}};
        if (page_ == "staff")
            args["role"] = "referee";
        auto page = session_.execute("list", args);
        listed_ = page["items"];
        total_ = page["total"].get<size_t>();
        if (selected_id_.empty() && !listed_.empty())
            selected_id_ = identity(listed_[0], table());
        selected_ = Json::object();
        if (!selected_id_.empty()) {
            try {
                selected_ = session_.execute("get", {{"table", table()}, {"id", selected_id_}})["record"];
            } catch (const Error &) {
                selected_id_.clear();
            }
        }
        std::string columns =
            button("sort", t("name") + (sort_ == "name" ? (descending_ ? " -" : " +") : ""), "", "data-value=\"name\"");
        const bool players = table() == "players", clubs = table() == "clubs";
        columns += button("sort",
                          t(players                              ? "team"
                            : clubs || table() == "competitions" ? "nation"
                                                                 : "type"),
                          "", "data-value=\"" + std::string(players ? "team" : "nation") + "\"");
        columns += button("sort",
                          t(players ? "positions"
                            : clubs ? "competition"
                                    : "details"),
                          "", "data-value=\"" + std::string(players ? "positions" : "competition") + "\"");
        std::string rows;
        for (auto &row : listed_) {
            auto id = identity(row, table());
            std::string detail = players ? ""
                                 : clubs ? ref(row, "competition")
                                         : str(row, "confederation", str(row, "kind"));
            if (players)
                for (auto &p : row.value("positions", Json::array())) {
                    if (!detail.empty())
                        detail += " / ";
                    detail += p.is_string() ? t(p.get<std::string>()) : "?";
                }
            if (page_ == "review")
                detail = t(row.value("resolved", false) ? "resolved" : "unresolved");
            std::string second = players                              ? ref(row, "team")
                                 : clubs || table() == "competitions" ? ref(row, "nation")
                                                                      : str(row, "type", str(row, "role"));
            std::string image;
            if (clubs || table() == "assets")
                image = img(image_key(row), "mini-logo");
            rows += "<button class=\"record " + std::string(selected_id_ == id ? "selected" : "") +
                    "\" data-action=\"select\" data-value=\"" + escaped(id) + "\"><span class=\"cell\">" + image +
                    "<span>" + escaped(title(row)) + "</span>" + "</span><span class=\"cell\">" + escaped(second) +
                    "</span><span class=\"cell\">" + escaped(detail) + "</span></button>";
        }
        if (rows.empty())
            rows = "<div class=\"empty\">" + escaped(t("empty")) + "</div>";
        html("page-content", "<div class=\"split\"><div class=\"list-panel\"><div "
                             "class=\"table-header\">" +
                                 columns + "</div><div class=\"table-scroll\" id=\"table-scroll\">" + rows +
                                 "</div></div>" + inspector() + "</div>");
        // RmlUI's textarea parser retains its raw body rather than decoding
        // HTML entities. Assign native values after constructing the controls.
        for (auto &f : fields_) {
            set_value(f.id, f.kind == "bool" && f.initial.empty() ? "false" : f.initial);
            f.initial = value(f.id);
        }
        html("pagination",
             "<span>" + std::to_string(total_) + " " + escaped(t("records")) +
                 (roster24_ ? " · " + escaped(t("compatibility24")) : "") + "</span><div class=\"button-row\">" +
                 button("previous", t("previous"), "quiet", offset_ > 0 ? "" : "disabled") + "<span>" +
                 std::to_string(offset_ + (total_ ? 1 : 0)) + "–" + std::to_string(offset_ + listed_.size()) +
                 "</span>" +
                 button("next", t("next"), "quiet", size_t(offset_) + listed_.size() < total_ ? "" : "disabled") +
                 "</div>");
    }
    void changes() {
        html("toolbar", "");
        html("pagination", "");
        html("page-actions", button("validate", t("validate")));
        std::string body = "<div class=\"dashboard list-panel\">";
        auto frames = info_.value("loaded", false) ? session_.execute("history") : Json::array();
        for (auto &frame : frames) {
            body += "<div class=\"history-row\"><span class=\"badge\">" +
                    escaped(t(frame.value("applied", false) ? "applied" : "undone")) + "</span>  " +
                    escaped(t(str(frame, "label"))) + "<small>";
            for (auto &delta : frame["changes"])
                body += escaped(str(delta, "table") + " / " + str(delta, "id")) + "<br/>";
            body += "</small></div>";
        }
        if (frames.empty())
            body += "<div class=\"empty\">" + escaped(t("no_changes")) + "</div>";
        html("page-content", body + "</div>");
    }
    void refresh() {
        const auto *focus = context_->GetFocusElement();
        const bool search_focused = focus && focus->GetId() == "search";
        int selection_start = 0, selection_end = 0;
        if (search_focused)
            if (auto *input = dynamic_cast<Rml::ElementFormControlInput *>(context_->GetFocusElement()))
                input->GetSelection(&selection_start, &selection_end, nullptr);
        info_ = session_.execute("info");
        auto rev = info_.value("revision", uint64_t(0));
        renderer_.clear();
        if (revision_ != rev) {
            refresh_lookup();
            revision_ = rev;
        }
        chrome();
        if (page_ == "overview")
            overview();
        else if (page_ == "changes")
            changes();
        else
            records();
        if (search_focused && doc_->GetElementById("search")) {
            auto *input = dynamic_cast<Rml::ElementFormControlInput *>(el("search"));
            input->Focus();
            input->SetSelectionRange(selection_start, selection_end);
        }
        needs_refresh_ = false;
    }
    bool has_draft() const {
        if (!doc_)
            return false;
        for (const auto &field : fields_)
            if (doc_->GetElementById(field.id) && value(field.id) != field.initial)
                return true;
        return false;
    }
    void leave_fields(std::function<void()> next) {
        if (!has_draft()) {
            next();
            return;
        }
        discard_continuation_ = next;
        modal(t("draft_prompt"), "<p>" + escaped(t("draft_detail")) + "</p>", t("apply"), [this, next] {
            apply_fields();
            close_modal();
            next();
        });
        html("modal-buttons", button("cancel-modal", t("cancel")) + button("discard-world", t("discard")) +
                                  button("accept-modal", t("apply"), "primary"));
    }
    void apply_fields() {
        if (selected_.empty())
            return;
        Json set = Json::object();
        for (const auto &f : fields_) {
            const auto input = value(f.id);
            if (input == f.initial)
                continue;
            Json parsed;
            if (input.empty())
                parsed = nullptr;
            else if (f.kind == "integer" || f.kind == "json" || f.kind == "bool") {
                try {
                    parsed = Json::parse(input);
                } catch (const Json::exception &) {
                    throw Error("invalid_change", {{"field", f.pointer}});
                }
                if (f.kind == "integer" && !parsed.is_number_integer())
                    throw Error("invalid_change", {{"field", f.pointer}});
            } else
                parsed = input;
            set[f.pointer] = parsed;
        }
        auto result = session_.execute(
            "apply", {{"expected_revision", revision_},
                      {"changes", Json::array({{{"table", table()}, {"id", selected_id_}, {"set", set}}})}});
        if (result.value("changed", false))
            needs_refresh_ = true;
    }
    void options() {
        std::string body = "<div class=\"form-field\"><label>" + t("language") +
                           "</label><select id=\"option-language\"><option value=\"de\"" +
                           (options_.language == "de" ? " selected" : "") + ">Deutsch</option><option value=\"en\"" +
                           (options_.language == "en" ? " selected" : "") + ">English</option></select></div>";
        body += "<div class=\"form-field\"><label>" + t("theme") + "</label><select id=\"option-theme\">";
        for (auto [id, name] : std::vector<std::pair<std::string, std::string>>{
                 {"midnight", "Midnight Glass"}, {"silver", "Silver Glass"}, {"matchday", "Matchday"}})
            body +=
                "<option value=\"" + id + "\"" + (options_.theme == id ? " selected" : "") + ">" + name + "</option>";
        body += "</select></div><div class=\"form-section\">MCP</div><div "
                "class=\"form-field\"><label>" +
                t("mcp_enabled") + "</label><select id=\"option-mcp\"><option value=\"true\"" +
                (options_.mcp_enabled ? " selected" : "") + ">" + t("yes") + "</option><option value=\"false\"" +
                (!options_.mcp_enabled ? " selected" : "") + ">" + t("no") + "</option></select></div>";
        body += "<div class=\"form-field\"><label>" + t("mcp_port") +
                "</label><input id=\"option-port\" type=\"text\" value=\"" + std::to_string(options_.mcp_port) +
                "\"/></div>";
        body += "<p class=\"form-help\">" + t("mcp_local") + " · " + t(server_.running() ? "online" : "offline") +
                (server_.running() ? "<br/>http://127.0.0.1:" + std::to_string(server_.port()) + "/mcp" : "") + "</p>";
        if (server_.running())
            body += button("copy-connection", t("copy_connection"));
        modal(t("options"), body, t("apply"), [this] {
            auto next = options_;
            next.apply({{"language", value("option-language")},
                        {"theme", value("option-theme")},
                        {"mcp_port", std::stoi(value("option-port"))},
                        {"mcp_enabled", value("option-mcp") == "true"}});
            const bool restart = next.mcp_port != options_.mcp_port || next.mcp_enabled != options_.mcp_enabled ||
                                 (next.mcp_enabled && !server_.running());
            next.save();
            options_ = next;
            if (restart)
                server_.start(options_);
            close_modal();
            needs_refresh_ = true;
        });
    }
    Json crop_recipe() {
        Json recipe{{"angle", std::stod(value("image-angle"))},
                    {"flip_x", value("image-flip-x") == "true"},
                    {"flip_y", value("image-flip-y") == "true"}};
        Json crop;
        for (auto key : {"x", "y", "width", "height"})
            crop[key] = std::stod(value("crop-" + std::string(key)));
        recipe["crop"] = crop;
        return recipe;
    }
    void crop_box() {
        const auto crop = image_recipe_.value("crop", Json{{"x", 0}, {"y", 0}, {"width", 1}, {"height", 1}});
        for (auto [name, key] : std::vector<std::pair<std::string, std::string>>{
                 {"left", "x"}, {"top", "y"}, {"width", "width"}, {"height", "height"}})
            el("crop-box")->SetProperty(name, number(crop.value(key, 0.0) * 100) + '%');
    }
    void preview_image(bool from_controls = true) {
        if (from_controls)
            image_recipe_ = crop_recipe();
        Json full = image_recipe_;
        full.erase("crop");
        auto raster = images::preview(image_bytes_, image_extension_, full, 460);
        const double scale = std::min(400.0 / raster.width, 360.0 / raster.height);
        const auto w = int(std::round(raster.width * scale)), h = int(std::round(raster.height * scale));
        auto token = renderer_.bytes(images::encode_png(raster), ".png");
        el("edit-preview")->SetAttribute("src", token);
        el("edit-preview")->SetProperty("width", std::to_string(w) + "dp");
        el("edit-preview")->SetProperty("height", std::to_string(h) + "dp");
        el("crop-canvas")->SetProperty("width", std::to_string(w) + "dp");
        el("crop-canvas")->SetProperty("height", std::to_string(h) + "dp");
        crop_box();
    }
    void edit_image(bool existing, bool fresh = false) {
        image_file_.clear();
        image_key_ = fresh ? "" : image_key(selected_);
        image_source_key_ = image_key_;
        image_target_ = Json::object();
        image_recipe_ = {{"angle", 0},
                         {"flip_x", false},
                         {"flip_y", false},
                         {"crop", {{"x", 0}, {"y", 0}, {"width", 1}, {"height", 1}}}};
        if (table() != "assets") {
            image_target_ = {{"table", table()},
                             {"id", selected_id_},
                             {"field", table() == "players"        ? "photo"
                                       : table() == "competitions" ? "trophy"
                                                                   : "crest"}};
            // Editing one person/club must not overwrite a shared fallback or
            // a template image also referenced by another record.
            image_key_ = "asset:editor/" + table() + '/' + selected_id_ + '/' + str(image_target_, "field");
        }
        if (!existing) {
            image_file_ = native_dialog("image");
            if (image_file_.empty())
                return;
            image_bytes_ = read_file(path(image_file_));
            image_extension_ = folded(utf8(path(image_file_).extension()));
            if (table() == "assets" && selected_id_.empty())
                image_key_.clear();
        } else {
            Json row;
            try {
                row = session_.execute("get", {{"table", "assets"}, {"id", image_source_key_}})["record"];
            } catch (const Error &error) {
                if (error.code != "record_missing" || table() != "players" || !selected_.contains("appearance"))
                    throw;
                image_source_key_ = "portrait:" + selected_id_;
                row = Json::object();
            }
            auto metadata = row.value("x-editor", Json::object());
            auto source = str(metadata, "original");
            auto pair = session_.asset_bytes(source.empty() ? image_source_key_ : source);
            image_bytes_ = std::move(pair.first);
            image_extension_ = pair.second;
            if (metadata.contains("transform"))
                image_recipe_ = metadata["transform"];
        }
        std::string body = "<div class=\"image-layout\"><div class=\"image-stage\"><div "
                           "id=\"crop-canvas\"><img id=\"edit-preview\"/><div "
                           "id=\"crop-box\"></div></div></div><div class=\"image-controls\">";
        body += "<div class=\"form-field\"><label>" + t("angle") +
                "</label><input type=\"text\" id=\"image-angle\" value=\"" + number(image_recipe_.value("angle", 0.0)) +
                "\"/></div>";
        for (auto key : {"x", "y"})
            body += "<div class=\"form-field\"><label>" + t(std::string("flip_") + key) +
                    "</label><select id=\"image-flip-" + key + "\"><option value=\"false\"" +
                    (!image_recipe_.value(std::string("flip_") + key, false) ? " selected" : "") + ">" + t("no") +
                    "</option><option value=\"true\"" +
                    (image_recipe_.value(std::string("flip_") + key, false) ? " selected" : "") + ">" + t("yes") +
                    "</option></select></div>";
        body += "<div class=\"form-section\">" + t("crop") +
                "</div><div class=\"form-field\"><select "
                "id=\"image-aspect\"><option value=\"free\">" +
                t("free") +
                "</option><option value=\"1\">1:1</option><option "
                "value=\"0.8\">4:5</option><option "
                "value=\"0.75\">3:4</option></select></div>";
        for (auto key : {"x", "y", "width", "height"})
            body += "<div class=\"form-field\"><label>" + t(key) + " (0–1)</label><input type=\"text\" id=\"crop-" +
                    key + "\" value=\"" +
                    number(image_recipe_["crop"].value(key, std::string(key) == "x" || std::string(key) == "y" ? 0.0
                                                                                                               : 1.0)) +
                    "\"/></div>";
        body += button("image-preview", t("preview")) + button("image-reset", t("reset"), "quiet") +
                "</div></div><p class=\"form-help\">" + t("crop_hint") + "</p>";
        modal(
            t(existing ? "image_edit" : "image_import"), body, t("apply"),
            [this] {
                image_recipe_ = crop_recipe();
                Json args{{"key", image_key_}, {"transform", image_recipe_}, {"expected_revision", revision_}};
                if (!image_file_.empty())
                    args["file"] = image_file_;
                else
                    args["source_key"] = image_source_key_;
                if (!image_target_.empty())
                    args["target"] = image_target_;
                session_.execute(image_file_.empty() ? "asset.edit" : "asset.import", args);
                close_modal();
                needs_refresh_ = true;
            },
            true, true);
        preview_image(false);
    }
    void new_record(bool duplicate) {
        if (table() == "assets") {
            edit_image(false, true);
            return;
        }
        if (selected_.empty() && duplicate)
            return;
        modal(t(duplicate ? "duplicate" : "add"),
              "<div class=\"form-field\"><label>" + t("new_name") +
                  "</label><input type=\"text\" id=\"new-name\" value=\"" +
                  escaped(duplicate ? title(selected_) + " – " + t("copy") : "") + "\"/></div>",
              t("add"), [this, duplicate] {
                  if (value("new-name").empty())
                      throw Error("missing_name");
                  Json action{{"op", "create"}, {"table", table()}, {"name", value("new-name")}};
                  if (duplicate) {
                      Json copy = selected_;
                      copy.erase("id");
                      copy.erase("x-legacy");
                      if (table() == "players")
                          copy["name"] = {{"first", ""}, {"last", value("new-name")}};
                      else
                          copy["name"] = {{"default", value("new-name")}};
                      if (table() == "competitions") {
                          copy["members"] = Json::array();
                          copy["stages"] = Json::array();
                      }
                      action["data"] = copy;
                  }
                  auto result =
                      session_.execute("apply", {{"expected_revision", revision_}, {"changes", Json::array({action})}});
                  selected_id_ = result["created"][0];
                  close_modal();
                  needs_refresh_ = true;
              });
    }
    void dispatch(const std::string &action, const std::string &arg) {
        if (busy_)
            return;
        if (action == "navigate")
            leave_fields([this, arg] {
                page_ = arg;
                fields_.clear();
                selected_id_.clear();
                offset_ = 0;
                search_.clear();
                needs_refresh_ = true;
            });
        else if (action == "select")
            leave_fields([this, arg] {
                selected_id_ = arg;
                needs_refresh_ = true;
            });
        else if (action == "cancel-modal")
            close_modal();
        else if (action == "accept-modal") {
            if (modal_accept_) {
                auto callback = modal_accept_;
                callback();
            }
        } else if (action == "discard-world") {
            close_modal();
            if (discard_continuation_)
                discard_continuation_();
        } else if (action == "options")
            options();
        else if (action == "copy-connection") {
            SDL_SetClipboardText(server_.connection(options_).dump(2).c_str());
            label("modal-error", t("copied"));
        } else if (action == "save" || action == "save-as") {
            if (has_draft())
                apply_fields();
            save(action == "save-as");
        } else if (action == "open") {
            modal(t("open"),
                  "<div class=\"button-row\">" + button("open-file", t("open_world")) +
                      button("open-folder", t("open_folder")) + button("import-folder", t("import_legacy")) + "</div>",
                  "", {});
        } else if (action == "open-file" || action == "open-folder" || action == "import-folder") {
            const auto file = native_dialog(action == "open-file" ? "open" : "folder");
            if (!file.empty())
                leaving([this, file] {
                    close_modal();
                    background("open", {{"path", file}, {"discard", true}}, [this](const Json &) {
                        selected_id_.clear();
                        offset_ = 0;
                    });
                });
        } else if (action == "demo")
            leaving([this] {
                close_modal();
                background("demo", {{"discard", true}});
            });
        else if (action == "new")
            leaving([this] {
                modal(t("new_world"),
                      "<div class=\"form-field\"><label>" + t("title") +
                          "</label><input type=\"text\" id=\"new-title\" value=\"" + t("new_title") + "\"/></div>",
                      t("confirm"), [this] {
                          auto title = value("new-title");
                          close_modal();
                          background("new", {{"title", title}, {"discard", true}});
                      });
            });
        else if (action == "apply")
            apply_fields();
        else if (action == "undo" || action == "redo") {
            session_.execute(action, {{"expected_revision", revision_}});
            needs_refresh_ = true;
        } else if (action == "add" || action == "duplicate")
            new_record(action == "duplicate");
        else if (action == "delete" && !selected_.empty()) {
            modal(t("delete_prompt"), "<p>" + escaped(title(selected_)) + "</p>", t("delete"), [this] {
                session_.execute(
                    "apply",
                    {{"expected_revision", revision_},
                     {"changes", Json::array({{{"op", "delete"}, {"table", table()}, {"id", selected_id_}}})}});
                selected_id_.clear();
                close_modal();
                needs_refresh_ = true;
            });
        } else if (action == "sort") {
            descending_ = arg == sort_ ? !descending_ : false;
            sort_ = arg;
            needs_refresh_ = true;
        } else if (action == "next" || action == "previous") {
            offset_ = std::max(0, offset_ + (action == "next" ? 1 : -1) * (roster24_ ? 24 : 200));
            needs_refresh_ = true;
        } else if (action == "filters") {
            const auto next_nation = doc_->GetElementById("filter-nation") ? value("filter-nation") : "";
            const auto next_league = doc_->GetElementById("filter-league") ? value("filter-league") : "";
            const auto next_team = doc_->GetElementById("filter-team") ? value("filter-team") : "";
            const auto next_search = value("search");
            // Native select initialization also emits change. Rebuilding the
            // controls for an unchanged value would reset focus every frame.
            if (next_nation == nation_ && next_league == league_ && next_team == team_ && next_search == search_)
                return;
            auto old_nation = nation_;
            nation_ = next_nation;
            league_ = next_league;
            team_ = next_team;
            search_ = next_search;
            if (old_nation != nation_) {
                league_.clear();
                team_.clear();
            }
            selected_id_.clear();
            offset_ = 0;
            needs_refresh_ = true;
        } else if (action == "view24") {
            roster24_ = !roster24_;
            offset_ = 0;
            needs_refresh_ = true;
        } else if (action == "remember") {
            remembered_ = selected_id_;
            label("status-message", t("remember_player") + ": " + title(selected_));
            needs_refresh_ = true;
        } else if (action == "swap") {
            session_.execute("roster.swap",
                             {{"first", remembered_}, {"second", selected_id_}, {"expected_revision", revision_}});
            remembered_.clear();
            needs_refresh_ = true;
        } else if (action == "order-up" || action == "order-down") {
            auto club = str(selected_, "team");
            if (club.empty())
                return;
            auto rows = all("players");
            std::vector<Json> squad;
            for (auto &row : rows)
                if (str(row, "team") == club)
                    squad.push_back(row);
            std::stable_sort(squad.begin(), squad.end(), [](auto &a, auto &b) {
                return a.value("squad_order", Json(9999)) < b.value("squad_order", Json(9999));
            });
            for (int i = 0; i < int(squad.size()); ++i)
                if (str(squad[size_t(i)], "id") == selected_id_) {
                    int to = i + (action == "order-up" ? -1 : 1);
                    if (to >= 0 && to < int(squad.size()))
                        std::swap(squad[size_t(i)], squad[size_t(to)]);
                    break;
                }
            Json ids = Json::array();
            for (auto &row : squad)
                ids.push_back(row["id"]);
            session_.execute("roster.order", {{"team", club}, {"players", ids}, {"expected_revision", revision_}});
            sort_ = "squad_order";
            needs_refresh_ = true;
        } else if (action == "toggle-source") {
            show_source_ = !show_source_;
            needs_refresh_ = true;
        } else if (action == "resolve") {
            session_.execute("review.resolve", {{"id", selected_id_},
                                                {"choice", std::stoi(value("review-choice"))},
                                                {"expected_revision", revision_}});
            needs_refresh_ = true;
        } else if (action == "image-import" || action == "image-edit") {
            if (has_draft()) {
                apply_fields();
                refresh();
            }
            edit_image(action == "image-edit");
        } else if (action == "image-export") {
            auto file = native_dialog("export");
            if (!file.empty())
                session_.execute("asset.export", {{"key", image_key(selected_)}, {"path", file}});
        } else if (action == "image-preview")
            preview_image();
        else if (action == "image-reset") {
            set_value("image-angle", "0");
            set_value("image-flip-x", "false");
            set_value("image-flip-y", "false");
            for (auto key : {"x", "y", "width", "height"})
                set_value("crop-" + std::string(key), std::string(key) == "x" || std::string(key) == "y" ? "0" : "1");
            preview_image();
        } else if (action == "aspect") {
            auto aspect = value("image-aspect");
            if (aspect == "free")
                return;
            auto *canvas = el("crop-canvas");
            double ratio = std::stod(aspect) * canvas->GetOffsetHeight() / canvas->GetOffsetWidth();
            double w = std::min(1.0, ratio), h = std::min(1.0, 1 / ratio);
            set_value("crop-width", number(w));
            set_value("crop-height", number(h));
            set_value("crop-x", number((1 - w) / 2));
            set_value("crop-y", number((1 - h) / 2));
            image_recipe_ = crop_recipe();
            crop_box();
        } else if (action == "validate") {
            auto diagnostics = session_.execute("validate")["diagnostics"];
            std::string body;
            int errors = 0;
            for (auto &row : diagnostics) {
                if (row["severity"] == "error")
                    ++errors;
                if (body.size() < 100000)
                    body += "<div class=\"diagnostic " + str(row, "severity") + "\">" + escaped(t(str(row, "code"))) +
                            "<small>" + escaped(str(row, "table") + " / " + str(row, "id")) + "</small></div>";
            }
            body = "<p>" + std::to_string(errors) + " " + t("errors") + " · " +
                   std::to_string(diagnostics.size() - size_t(errors)) + " " + t("warnings") + "</p>" + body;
            modal(t("validate"), body, t("close"), [this] { close_modal(); }, false);
        }
    }
    void crop_drag(double x, double y, bool begin) {
        auto *canvas = el("crop-canvas");
        const double nx = std::clamp((x - canvas->GetAbsoluteLeft()) / canvas->GetOffsetWidth(), 0.0, 1.0);
        const double ny = std::clamp((y - canvas->GetAbsoluteTop()) / canvas->GetOffsetHeight(), 0.0, 1.0);
        if (begin) {
            drag_x_ = nx;
            drag_y_ = ny;
            dragging_ = true;
            return;
        }
        const auto left = std::min(.995, std::min(drag_x_, nx)), top = std::min(.995, std::min(drag_y_, ny));
        image_recipe_["crop"] = {{"x", left},
                                 {"y", top},
                                 {"width", std::max(.005, std::abs(nx - drag_x_))},
                                 {"height", std::max(.005, std::abs(ny - drag_y_))}};
        for (auto key : {"x", "y", "width", "height"})
            set_value("crop-" + std::string(key), number(image_recipe_["crop"][key].get<double>()));
        crop_box();
    }
    void screenshot() {
        if (config_.screenshot.empty())
            return;
        int w, h;
        SDL_GL_GetDrawableSize(video_.window, &w, &h);
        images::Raster raster{w, h, std::vector<uint8_t>(size_t(w) * h * 4)};
        using ReadPixels = void (*)(int, int, int, int, unsigned, unsigned, void *);
        auto read = reinterpret_cast<ReadPixels>(SDL_GL_GetProcAddress("glReadPixels"));
        if (!read)
            throw Error("video_failed");
        read(0, 0, w, h, 0x1908 /* RGBA */, 0x1401 /* UNSIGNED_BYTE */, raster.pixels.data());
        for (int y = 0; y < h / 2; ++y)
            for (int x = 0; x < w * 4; ++x)
                std::swap(raster.pixels[size_t(y) * w * 4 + x], raster.pixels[size_t(h - y - 1) * w * 4 + x]);
        write_atomic(config_.screenshot, images::encode_png(raster));
        config_.screenshot.clear();
    }

  public:
    EditorUi(Session &s, Options &o, McpServer &server, UiConfig config)
        : session_(s), options_(o), server_(server), config_(std::move(config)), renderer_(s) {
        page_ = config_.page;
        Rml::SetSystemInterface(&system_);
        Rml::SetFileInterface(&files_);
        Rml::SetRenderInterface(&renderer_);
        Rml::SetTextInputHandler(&ime_);
        initialized_ = Rml::Initialise();
        if (!initialized_)
            throw Error("video_failed");
        for (auto weight : {"Regular", "Bold"})
            if (!Rml::LoadFontFace(utf8(config_.assets / (std::string("LatoLatin-") + weight + ".ttf"))))
                throw Error("ui_assets_missing");
        context_ = Rml::CreateContext("editor", {1440, 900});
        doc_ = context_->LoadDocument(utf8(config_.assets / "editor.rml"));
        if (!doc_)
            throw Error("ui_assets_missing");
        for (auto event : {"click", "change", "mousedown", "mouseup", "mousemove"})
            doc_->AddEventListener(event, this);
        resize();
        refresh();
        doc_->Show();
        SDL_ShowWindow(video_.window);
    }
    ~EditorUi() override {
        if (work_.valid())
            work_.wait();
        if (initialized_)
            Rml::Shutdown();
    }
    void ProcessEvent(Rml::Event &event) override {
        auto *target = event.GetTargetElement();
        const auto type = event.GetType();
        if (type == "mousedown" && modal_ && doc_->GetElementById("crop-canvas")) {
            for (auto *node = target; node; node = node->GetParentNode())
                if (node->GetId() == "crop-canvas") {
                    crop_drag(event.GetParameter<float>("mouse_x", 0), event.GetParameter<float>("mouse_y", 0), true);
                    return;
                }
        }
        if (type == "mousemove" && dragging_) {
            crop_drag(event.GetParameter<float>("mouse_x", 0), event.GetParameter<float>("mouse_y", 0), false);
            return;
        }
        if (type == "mouseup") {
            dragging_ = false;
            return;
        }
        if (type == "change") {
            auto id = target->GetId();
            if (id.starts_with("filter-") || id == "search")
                pending_.emplace_back("filters", "");
            else if (id == "image-aspect")
                pending_.emplace_back("aspect", "");
            else if (id.starts_with("image-") && id != "edit-preview")
                pending_.emplace_back("image-preview", "");
            return;
        }
        if (type != "click")
            return;
        for (auto *node = target; node; node = node->GetParentNode())
            if (node->HasAttribute("data-action") && !node->HasAttribute("disabled")) {
                pending_.emplace_back(node->GetAttribute<Rml::String>("data-action", ""),
                                      node->GetAttribute<Rml::String>("data-value", ""));
                return;
            }
    }
    int run() {
        auto last_poll = start_;
        while (!closed_) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    try {
                        leaving([this] { closed_ = true; });
                    } catch (const std::exception &e) {
                        error(e);
                    }
                    continue;
                }
                if (event.type == SDL_WINDOWEVENT && (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                                                      event.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED))
                    resize();
                if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                    const bool command = event.key.keysym.mod & (KMOD_CTRL | KMOD_GUI);
                    if (command && event.key.keysym.sym == SDLK_s) {
                        pending_.emplace_back(event.key.keysym.mod & KMOD_SHIFT ? "save-as" : "save", "");
                        continue;
                    }
                    auto *focused = context_->GetFocusElement();
                    const bool text_input =
                        focused && (focused->GetTagName() == "input" || focused->GetTagName() == "textarea");
                    if (command && event.key.keysym.sym == SDLK_z && !text_input) {
                        pending_.emplace_back(event.key.keysym.mod & KMOD_SHIFT ? "redo" : "undo", "");
                        continue;
                    }
                    if (command && event.key.keysym.sym == SDLK_o) {
                        pending_.emplace_back("open", "");
                        continue;
                    }
                    if (event.key.keysym.sym == SDLK_ESCAPE && modal_) {
                        close_modal();
                        continue;
                    }
                }
                if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                    int mods = 0;
                    auto state = event.key.keysym.mod;
                    if (state & (KMOD_CTRL | KMOD_GUI))
                        mods |= Rml::Input::KM_CTRL;
                    if (state & KMOD_SHIFT)
                        mods |= Rml::Input::KM_SHIFT;
                    if (state & KMOD_ALT)
                        mods |= Rml::Input::KM_ALT;
                    if (event.type == SDL_KEYDOWN)
                        context_->ProcessKeyDown(RmlSDL::ConvertKey(event.key.keysym.sym), mods);
                    else
                        context_->ProcessKeyUp(RmlSDL::ConvertKey(event.key.keysym.sym), mods);
                } else {
                    if (event.type == SDL_TEXTEDITING)
                        ime_.HandleEdit(event.edit);
                    int w, h, pw, ph;
                    SDL_GetWindowSize(video_.window, &w, &h);
                    SDL_GL_GetDrawableSize(video_.window, &pw, &ph);
                    if (w && h) {
                        if (event.type == SDL_MOUSEMOTION) {
                            event.motion.x = int(double(event.motion.x) * pw / w);
                            event.motion.y = int(double(event.motion.y) * ph / h);
                        }
                        if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
                            event.button.x = int(double(event.button.x) * pw / w);
                            event.button.y = int(double(event.button.y) * ph / h);
                            context_->ProcessMouseMove(event.button.x, event.button.y, RmlSDL::GetKeyModifierState());
                        }
                    }
                    RmlSDL::InputEventHandler(context_, video_.window, event);
                }
            }
            while (!pending_.empty()) {
                auto [action, arg] = pending_.front();
                pending_.pop_front();
                try {
                    dispatch(action, arg);
                } catch (const std::exception &e) {
                    error(e);
                }
            }
            if (busy_ && work_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                busy_ = false;
                try {
                    auto result = work_.get();
                    if (completed_)
                        completed_(result);
                    needs_refresh_ = true;
                } catch (const std::exception &e) {
                    error(e);
                }
                completed_ = {};
            }
            const auto now = std::chrono::steady_clock::now();
            if (!busy_ && now - last_poll > std::chrono::milliseconds(400)) {
                auto info = session_.execute("info");
                if (info.value("revision", uint64_t(0)) != revision_) {
                    if (!has_draft() && !modal_)
                        needs_refresh_ = true;
                    else
                        label("status-message", t("stale_revision"));
                }
                last_poll = now;
            }
            if (needs_refresh_ && !busy_) {
                try {
                    refresh();
                } catch (const std::exception &e) {
                    needs_refresh_ = false;
                    error(e);
                }
            }
            context_->Update();
            renderer_.BeginFrame();
            renderer_.Clear();
            context_->Render();
            renderer_.EndFrame();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_).count();
            if (!busy_ && elapsed > 1200 && !config_.screenshot.empty())
                screenshot();
            SDL_GL_SwapWindow(video_.window);
            SDL_Delay(10);
            if (config_.quit_after_ms > 0 && elapsed >= config_.quit_after_ms)
                closed_ = true;
        }
        return 0;
    }
};
} // namespace
int run_ui(Session &session, Options &options, McpServer &server, const UiConfig &config) {
    EditorUi ui(session, options, server, config);
    return ui.run();
}
} // namespace bmh::editor
