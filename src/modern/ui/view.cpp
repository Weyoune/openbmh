// SPDX-License-Identifier: GPL-3.0-only
#include "content/package.hpp"
#include "launch/game_data.hpp"
#include "launch/process.hpp"
#include "launch/rml_path.hpp"
#include "launch/rml_video.hpp"
#include "modern/ui/controls.hpp"
#include "modern/ui/demo_calendar.hpp"
#include "modern/ui/demo_reports.hpp"
#include "modern/ui/demo_sports.hpp"
#include "modern/ui/desktop.hpp"
#include "modern/ui/gui_assets.hpp"
#include "modern/ui/model.hpp"
#include "modern/ui/ticket_popover.hpp"
#include "modern/ui/view.hpp"
#include "version.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi_Platform_SDL.h>
#include <SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <nfd.hpp>
#include <optional>
#include <RmlUi/Core/ScrollTypes.h>
#include <sstream>

namespace bmh::modern_ui {
namespace {
std::string esc(std::string_view value) { return editor::escaped(value); }
std::string scalar(double n) {
  std::ostringstream s;
  s.imbue(std::locale::classic());
  s << n;
  return s.str();
}
std::string mixed_color(const std::string &first, const std::string &second,
                        double amount) {
  std::ostringstream out;
  out << '#' << std::hex << std::setfill('0');
  for (size_t i = 1; i < 7; i += 2) {
    const int a = std::stoi(first.substr(i, 2), nullptr, 16),
              b = std::stoi(second.substr(i, 2), nullptr, 16);
    out << std::setw(2) << int(std::round(a * (1 - amount) + b * amount));
  }
  return out.str();
}
std::string rect(double x, double y, double w, double h) {
  return "left:" + scalar(x) + "dp;top:" + scalar(y) + "dp;width:" + scalar(w) +
         "dp;height:" + scalar(h) + "dp;";
}
std::string btn(const std::string &action, const std::string &label,
                const std::string &css = {}, const std::string &attrs = {}) {
  return button_markup(action, label, css, attrs);
}
const std::array<std::string, 31> routes = {
    "tables",   "league",      "strengths", "lineup",     "achievements",
    "other",    "friendlies",  "postpone",  "calendar",   "indoor",
    "load",     "save",        "kickoff",   "toto",       "bank",
    "camp",     "training",    "transfer",  "tactics",    "settings",
    "scorers",  "advertising", "stadium",   "statistics", "environment",
    "fairplay", "highscore",   "contracts", "balance",    "newplayer",
    "news"};
const std::array<std::string, 31> titles = {"Tabellen",
                                            "Ligaspiele",
                                            "Stärken",
                                            "Aufstellung",
                                            "Erfolge",
                                            "Übrige Spiele",
                                            "Freundschaftsspiele",
                                            "Verlegen",
                                            "Kalender",
                                            "Hallentabellen",
                                            "Laden",
                                            "Speichern",
                                            "Anstoß",
                                            "TOTO",
                                            "Bank",
                                            "Trainingslager",
                                            "Training",
                                            "Transfermarkt",
                                            "Taktik",
                                            "Einstellungen",
                                            "Spielerstatistiken",
                                            "Werbung",
                                            "Stadion",
                                            "Statistiken",
                                            "Umfeld",
                                            "Fairplay",
                                            "Highscore",
                                            "Verträge",
                                            "Bilanz",
                                            "Neuer Spieler",
                                            "Nachrichten"};
std::string title(const std::string &route) {
  const auto i = std::find(routes.begin(), routes.end(), route);
  if (i != routes.end())
    return titles[size_t(i - routes.begin())];
  if (route == "widgets")
    return "Widget hinzufügen";
  if (route == "view-save")
    return "Ansicht speichern";
  if (route == "view-load")
    return "Ansicht laden";
  if (route == "background")
    return "Hintergrundbild ändern";
  if (route == "newgame")
    return "Neues Spiel";
  if (route == "mods")
    return "Mod";
  if (route == "opening")
    return "Saisonvorbereitung";
  return "OpenBMH";
}
struct Picture {
  std::string token;
  int width = 1, height = 1;
};
class Renderer final : public rml::Renderer {
  GuiAssets &gui_;
  fs::path assets_;
  std::shared_ptr<const Catalog> world_;
  struct Entry {
    std::string key;
    bool world;
    int pixels;
    editor::images::Raster raster;
  };
  std::map<std::string, Entry> entries_;
  std::map<std::string, std::string> keys_;
  std::map<std::string, std::string> loaded_sources_;
  size_t serial_ = 0;

public:
  Renderer(rml::Video &video, GuiAssets &gui, fs::path assets)
      : rml::Renderer(video), gui_(gui), assets_(std::move(assets)) {}
  Picture diagram(const std::string &slot, const std::string &svg,
                  int pixels = 2400) {
    const auto token = "modern-dynamic-" + slot + ".png",
               digest = content::sha256(svg);
    auto found = entries_.find(token);
    if (found == entries_.end() || found->second.key != digest) {
      auto raster = editor::images::decode(svg, ".svg", pixels);
      if (const auto loaded = loaded_sources_.find(token);
          loaded != loaded_sources_.end()) {
        Rml::ReleaseTexture(loaded->second, this);
        loaded_sources_.erase(loaded);
      }
      entries_[token] = {digest, false, pixels, std::move(raster)};
    }
    const auto &r = entries_.at(token).raster;
    return {token, r.width, r.height};
  }
  Picture local_image(const fs::path &file, int pixels) {
    const auto identity =
        editor::utf8(file) + ":" +
        std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(
                           fs::last_write_time(file).time_since_epoch())
                           .count());
    if (!keys_.contains(identity)) {
      auto raster = editor::images::decode(
          editor::read_file(file), editor::utf8(file.extension()), pixels);
      const auto token = "modern-image-" + std::to_string(serial_++) + ".png";
      entries_[token] = {identity, false, pixels, std::move(raster)};
      keys_[identity] = token;
    }
    const auto token = keys_.at(identity);
    const auto &r = entries_.at(token).raster;
    return {token, r.width, r.height};
  }
  void world(std::shared_ptr<const Catalog> value) {
    Rml::ReleaseTextures(this);
    entries_.clear();
    keys_.clear();
    loaded_sources_.clear();
    world_ = std::move(value);
  }
  Picture image(const std::string &key, bool world = false, int pixels = 512) {
    const auto identity =
        (world ? "world:" : "gui:") + key + ":" + std::to_string(pixels);
    auto found = keys_.find(identity);
    if (found == keys_.end()) {
      auto token = "modern-image-" + std::to_string(serial_++) + ".png";
      editor::images::Raster raster;
      if (world) {
        if (!world_)
          return {};
        const auto bytes = world_->asset(key, pixels);
        if (bytes.first.empty())
          return {};
        raster = editor::images::decode(bytes.first, bytes.second, pixels);
      } else if (key.starts_with("icon.")) {
        auto name = key.substr(5);
        const bool ink = name.ends_with("-ink");
        if (ink)
          name.resize(name.size() - 4);
        if (name.empty() ||
            name.find_first_not_of("abcdefghijklmnopqrstuvwxyz-") != name.npos)
          return {};
        auto svg = editor::read_file(assets_ / "icons" / (name + ".svg"));
        if (ink) {
          size_t at = 0;
          while ((at = svg.find("#dceae7", at)) != std::string::npos) {
            svg.replace(at, 7, "#426071");
            at += 7;
          }
        }
        raster = editor::images::decode(svg, ".svg", pixels);
      } else if (gui_.contains(key))
        raster = gui_.decode(key, pixels);
      else if (key == "background")
        raster = editor::images::decode(
            editor::read_file(assets_.parent_path() / "launcher" /
                              "stadium.tga"),
            ".tga", pixels);
      else
        return {};
      // Decode at source fidelity, then bound GPU storage to the requested
      // display detail.
      const auto largest = std::max(raster.width, raster.height);
      if (largest > pixels) {
        const double scale = double(pixels) / largest;
        editor::images::Raster thumbnail{
            std::max(1, int(std::round(raster.width * scale))),
            std::max(1, int(std::round(raster.height * scale))),
            {}};
        thumbnail.pixels.resize(size_t(thumbnail.width) * thumbnail.height * 4);
        SDL_Surface *source = SDL_CreateRGBSurfaceWithFormatFrom(
            raster.pixels.data(), raster.width, raster.height, 32,
            raster.width * 4, SDL_PIXELFORMAT_RGBA32);
        SDL_Surface *destination = SDL_CreateRGBSurfaceWithFormatFrom(
            thumbnail.pixels.data(), thumbnail.width, thumbnail.height, 32,
            thumbnail.width * 4, SDL_PIXELFORMAT_RGBA32);
        if (!source || !destination) {
          if (source)
            SDL_FreeSurface(source);
          if (destination)
            SDL_FreeSurface(destination);
          throw std::runtime_error(SDL_GetError());
        }
        SDL_SetSurfaceBlendMode(source, SDL_BLENDMODE_NONE);
        SDL_BlitScaled(source, nullptr, destination, nullptr);
        SDL_FreeSurface(source);
        SDL_FreeSurface(destination);
        raster = std::move(thumbnail);
      }
      entries_[token] = {key, world, pixels, std::move(raster)};
      keys_[identity] = token;
      found = keys_.find(identity);
    }
    const auto &r = entries_.at(found->second).raster;
    return {found->second, r.width, r.height};
  }
  Rml::TextureHandle LoadTexture(Rml::Vector2i &dimensions,
                                 const Rml::String &source) override {
    const auto token = editor::utf8(editor::path(source).filename());
    auto found = entries_.find(token);
    if (found == entries_.end())
      return rml::Renderer::LoadTexture(dimensions, source);
    loaded_sources_[token] = source;
    auto pixels = found->second.raster.pixels;
    for (size_t i = 0; i < pixels.size(); i += 4)
      for (int c = 0; c < 3; ++c)
        pixels[i + c] =
            uint8_t((unsigned(pixels[i + c]) * pixels[i + 3] + 127) / 255);
    dimensions = {found->second.raster.width, found->second.raster.height};
    return GenerateTexture(pixels, dimensions);
  }
};
struct Mod {
  std::string name, type, error;
  fs::path file;
  Json manifest;
};
class App final : public Rml::EventListener {
  fs::path executable_, root_, assets_, gui_root_;
  launch::SaveMigrator migrate_;
  Config config_;
  GuiAssets gui_;
  rml::Video video_;
  rml::AssetFiles files_;
  SystemInterface_SDL system_;
  Renderer renderer_;
  TextInputMethodEditor_SDL ime_;
  Rml::Context *context_ = nullptr;
  Rml::ElementDocument *doc_ = nullptr;
  bool initialized_ = false, closed_ = false, dirty_ = true,
       needs_render_ = true;
  double scale_ = 1, canvas_left_ = 0, canvas_top_ = 0;
  std::string scene_ = "main", menu_, theme_ = "midnight", active_mod_,
              selected_mod_, notice_, country_ = "nation:de", league_,
              selected_club_;
  std::vector<Mod> mods_;
  std::shared_ptr<const Catalog> world_;
  std::unique_ptr<DemoSource> source_;
  std::unique_ptr<TicketPopover> ticket_;
  bool focus_ticket_ = false;
  std::set<std::string> pending_selects_;
  std::string focus_after_render_;
  bool keyboard_navigation_ = false;
  std::string last_rendered_menu_;
  std::optional<float> training_scroll_;
  std::optional<float> report_scroll_;
  std::string last_report_state_, report_picker_, report_detail_kind_,
      report_detail_club_, report_detail_player_, report_detail_return_focus_,
      report_option_to_scroll_;
  std::string planning_dialog_, planning_notice_, planning_return_focus_;
  float device_ratio_ = 1.f;
  Json desktop_ = Json::object(), saved_desktop_ = Json::object(),
       options_ = Json::object();
  Json managers_draft_ = Json::array(),
       start_options_ = {{"duration", 5}, {"difficulty", 3}, {"players", 1},
                         {"mode", 3},     {"league", true},  {"world", true}};
  int manager_index_ = 0;
  bool manager_picker_ = false, adding_manager_ = false;
  struct Drag {
    bool active = false, moved = false;
    std::string id;
    float x = 0, y = 0;
    double left = 0, top = 0;
  };
  Drag drag_;
  bool suppress_click_ = false;
  std::vector<Json> undo_;
  std::function<void()> confirm_;
  std::string popup_, rendered_popup_;
  bool rendered_confirm_ = false;
  std::string popup_input_, popup_extra_, popup_accept_ = "Bestätigen";
  bool popup_has_input_ = false, quit_after_save_ = false;
  std::function<void(const std::string &)> text_accept_;
  bool context_open_ = false, context_dirty_ = false;
  std::string context_id_;
  int context_x_ = 0, context_y_ = 0;
  int opening_page_ = 0, draw_revealed_ = 0;
  int last_tactic_ = 0;
  bool draw_playing_ = true;
  uint64_t draw_at_ = 0;
  std::string settings_category_ = "display";
  Json draft_preferences_;
  std::map<std::string, std::string> selections_;
  Rml::Element *element(const std::string &id) const {
    return doc_->GetElementById(id);
  }
  std::string field(const std::string &id) const {
    auto *p = dynamic_cast<Rml::ElementFormControl *>(element(id));
    return p ? p->GetValue() : "";
  }
  Snapshot snap() const { return source_->snapshot(); }
  const Json &manager(const Snapshot &s) const {
    return s.state.at("managers").at(0);
  }
  const Json *club(const Snapshot &s) const {
    return s.world->find("clubs", manager(s).value("club", ""));
  }
  std::string cash(int64_t value) const {
    return money(value,
                 world_ ? world_->manifest().value("currency", Json::object())
                        : Json::object());
  }
  fs::path user_dir() const {
    const auto id = world_ ? world_->manifest().value("id", std::string("none"))
                           : std::string("none");
    return root_ / "userdata" / "ui-demo" / content::sha256(id);
  }
  std::string img(const std::string &key, double w, double h,
                  bool world = false, bool cover = false, int pixels = 512) {
    return picture(renderer_.image(key, world, pixels), w, h, cover);
  }
  std::string picture(const Picture &p, double w, double h,
                      bool cover = false) {
    if (p.token.empty())
      return "<div style=\"width:" + scalar(w) + "dp;height:" + scalar(h) +
             "dp;\"></div>";
    double iw = w, ih = h;
    std::string crop;
    if (cover) {
      const auto a = double(p.width) / p.height, b = w / h;
      double x = 0, y = 0, sw = p.width, sh = p.height;
      if (a > b) {
        sw = sh * b;
        x = (p.width - sw) / 2;
      } else {
        sh = sw / b;
        y = (p.height - sh) / 2;
      }
      crop = " rect=\"" + scalar(x) + " " + scalar(y) + " " + scalar(sw) + " " +
             scalar(sh) + "\"";
    } else {
      const auto factor = std::min(w / p.width, h / p.height);
      iw = p.width * factor;
      ih = p.height * factor;
    }
    return "<div class=\"image-frame\" style=\"width:" + scalar(w) +
           "dp;height:" + scalar(h) + "dp;\"><img src=\"" + p.token + "\"" +
           crop + " style=\"width:" + scalar(iw) + "dp;height:" + scalar(ih) +
           "dp;\"/></div>";
  }
  std::string crest(const Json *c, double size) {
    return c ? img(c->value("crest", ""), size, size, true) : "";
  }
  std::string glyph(const std::string &name, double size) {
    return img("icon." + name + (theme_ == "silver" ? "-ink" : ""), size, size,
               false, false, int(size));
  }
  std::string label(const Json *c, double size = 40) {
    return c ? "<span class=\"club-label\">" + crest(c, size) + "<span>" +
                   esc(world_->name(*c)) + "</span></span>"
             : "";
  }
  std::string window(const std::string &heading, const std::string &content,
                     bool close = true) {
    return "<div class=\"window\"><div class=\"titlebar\">" +
           (close ? btn("close", "<span>×</span>", "close")
                  : "<span style=\"width:76dp;\"></span>") +
           "<h1>" + esc(heading) + "</h1></div><div class=\"content\">" +
           content + "</div></div>";
  }
  std::string
  select(const std::string &id,
         const std::vector<std::pair<std::string, std::string>> &rows,
         const std::string &value, const std::string &action = {}) {
    std::string out =
        "<select id=\"" + id + "\" data-change=\"" + action + "\">";
    for (const auto &[key, name] : rows)
      out += "<option value=\"" + esc(key) + "\" " +
             (key == value ? "selected" : "") + ">" + esc(name) + "</option>";
    return out + "</select>";
  }
  bool apply(const std::string &kind, const Json &value) {
    const auto r = source_->apply({source_->snapshot().revision, kind, value});
    if (!r.ok) {
      if (kind == "training" && r.error == "training_preview_budget") {
        const int limit = value.value("group", "") == "type" ? 8
                          : value.value("index", 0) < 2      ? 4
                                                             : 18;
        message("Für diesen Trainingsbereich stehen höchstens " +
                std::to_string(limit) + " Bälle zur Verfügung.");
        return false;
      }
      message("Die Eingabe konnte nicht übernommen werden: " + r.error);
      return false;
    }
    return true;
  }
  void reset_popup() {
    popup_has_input_ = false;
    popup_input_.clear();
    popup_extra_.clear();
    popup_accept_ = "Bestätigen";
    text_accept_ = {};
    rendered_popup_.clear();
  }
  void message(const std::string &text) {
    reset_popup();
    popup_ = text;
    confirm_ = {};
    dirty_ = true;
  }
  void confirmation(const std::string &text, std::function<void()> accepted) {
    reset_popup();
    popup_ = text;
    confirm_ = std::move(accepted);
    dirty_ = true;
  }
  void text_dialog(const std::string &text, const std::string &initial,
                   std::function<void(const std::string &)> accepted) {
    confirmation(text, {});
    popup_has_input_ = true;
    popup_input_ = initial;
    text_accept_ = std::move(accepted);
    dirty_ = true;
  }
  void refresh() {
    needs_render_ = true;
    dirty_ = true;
  }
  void collect_mods() {
    mods_.clear();
    const auto folder = root_ / "mods";
    if (!fs::exists(folder))
      return;
    for (const auto &entry : fs::directory_iterator(folder)) {
      if (!entry.is_directory() || entry.is_symlink() ||
          editor::utf8(entry.path().filename()).starts_with('.'))
        continue;
      Mod m{editor::utf8(entry.path().filename()),
            "",
            {},
            entry.path() / "content.obw",
            {}};
      try {
        auto store = editor::ArchiveStore::open(m.file);
        m.manifest = Json::parse(store->read("manifest.json"));
        m.type = Catalog::modern_manifest(m.manifest) ? "Modern" : "Classic";
      } catch (const std::exception &e) {
        m.error = e.what();
        m.type = "Unbekannt";
      }
      mods_.push_back(std::move(m));
    }
    std::sort(mods_.begin(), mods_.end(),
              [](const auto &a, const auto &b) { return a.name < b.name; });
  }
  Mod *mod(const std::string &name) {
    for (auto &m : mods_)
      if (m.name == name)
        return &m;
    return nullptr;
  }
  void load_world(const fs::path &file) {
    auto next = Catalog::open(file);
    auto demo = std::make_unique<DemoSource>(next);
    ticket_.reset();
    renderer_.world(next);
    world_ = std::move(next);
    source_ = std::move(demo);
    desktop_ = Json::object();
    ticket_ =
        std::make_unique<TicketPopover>(*context_, *doc_, *source_, [this] {
          focus_ticket_ = true;
          refresh();
        });
    league_ = world_->leagues("nation:de").empty()
                  ? ""
                  : world_->leagues("nation:de").front()->value("id", "");
    selected_club_ = manager(snap()).value("club", "");
  }
  std::string main_menu() {
    const auto *m = mod(active_mod_);
    std::string out =
        "<div class=\"fill front-shade\"></div><div "
        "class=\"brand\"><p>FUSSBALLMANAGER</p><h1>Open<span>BMH</span></h1></"
        "div><div class=\"main-options\">";
    const std::array<std::pair<std::string, std::string>, 6> actions = {
        {{"new", "Neues Spiel"},
         {"load", "Spiel laden"},
         {"network", "Netzwerk"},
         {"mods", "Mod"},
         {"settings", "Einstellungen"},
         {"exit", "Beenden"}}};
    for (const auto &[key, name] : actions)
      out += btn(
          key,
          (key == "network" ? "<span>" + esc(name) + "</span>" : esc(name)) +
              (key == "network" ? "<small>Demnächst</small>" : ""),
          {},
          key == "network" || (key == "new" && !m && !world_) ? "disabled"
                                                              : "");
    out +=
        "</div><div class=\"front-footer\"><div><small>Aktive Mod</small><p>" +
        esc(m        ? m->name
            : world_ ? world_->title()
                     : "Keine Mod aktiviert") +
        "</p></div><small>v" + std::string(kOpenBmhVersion) +
        (m && m->type == "Classic" ? "" : " · Modern UI Demo") +
        "</small></div>";
    return out;
  }
  std::string mods_page() {
    if (selected_mod_.empty() && !mods_.empty())
      selected_mod_ = mods_.front().name;
    std::string left = "<h3 style=\"padding:28dp;\">Installierte Mods</h3><div "
                       "class=\"list\" style=\"height:1310dp;\">";
    for (const auto &m : mods_)
      left += btn(
          "mod-select",
          "<span class=\"grow\">" + esc(m.name) + "</span><span>" +
              esc(m.type) + "</span>",
          std::string("row choice ") +
              (m.name == selected_mod_ ? "selected" : ""),
          "data-id=\"" + esc(m.name) +
              "\" style=\"width:100%;margin-bottom:12dp;min-height:120dp;\"");
    left += "</div>";
    std::string right;
    if (auto *m = mod(selected_mod_))
      right =
          "<div class=\"column\" style=\"padding:38dp;\">" +
          img("background", 1300, 400, false, true, 1600) + "<h2>" +
          esc(m->name) + "</h2><p class=\"muted\">" + esc(m->type) +
          (m->type == "Modern" ? " · Oberfläche bis zum Anstoß" : "") +
          "</p><p>" + esc(m->error) + "</p>" +
          btn("mod-activate", m->name == active_mod_ ? "Aktiv" : "Aktivieren",
              "primary", m->name == active_mod_ ? "disabled" : "") +
          "</div>";
    return window(
        "Mod",
        "<div class=\"row\" style=\"height:1560dp;align-items:stretch;\"><div "
        "class=\"panel\" style=\"width:1120dp;\">" +
            left + "</div><div class=\"panel grow\">" + right +
            "</div></div><div class=\"bottom-controls\">" +
            btn("import-modern", "OBW-Paket hinzufügen") +
            btn("import-classic", "BMH / Football Limited") +
            btn("mod-rename", "Umbenennen") + btn("mod-remove", "Löschen") +
            "</div>");
  }
  std::string render_desktop();
  void init_desktop();
  std::string widget(const std::string &, const Json &);
  std::string render_reports(const std::string &);
  std::string report_club_button(const std::string &, bool away = false);
  std::string render_report_detail();
  bool report_detail_action(const std::string &, Rml::Element *);
  std::string report_picker(const std::string &, double, double);
  std::vector<std::pair<std::string, std::string>>
      report_picker_items(const std::string &) const;
  std::string report_picker_value(const std::string &) const;
  bool report_picker_event(Rml::Event &);
  bool report_picker_action(const std::string &, Rml::Element *);
  void close_report_picker(bool focus = false);
  std::string render_camp();
  std::string render_training();
  std::string render_tactics();
  std::string render_calendar();
  Json planning_calendar() const;
  std::string calendar_day_info(const Json &) const;
  std::string calendar_grid(const Json &, double, double, double, double, bool, bool);
  std::string render_postpone();
  std::string render_friendlies();
  std::string render_planning_dialog();
  std::string planning_progress(bool, bool);
  bool planning_action(const std::string &, Rml::Element *);
  std::string render_indoor();
  bool indoor_action(const std::string &, Rml::Element *);
  std::string training_balls(const std::string &, int, int, bool,
                             const std::string &player = {}, int size = 56,
                             int gap = 10, bool overtraining = false);
  std::string render_settings();
  std::string render_save(bool save);
  std::string render_desktop_tools(const std::string &);
  void render_context();
  void context_menu(const std::string &id, int x, int y);
  void drag_event(Rml::Event &);
  std::string render_newgame();
  std::string render_manager();
  std::string render_opening();
  std::string render_menu(const std::string &);
  void dispatch(const std::string &, Rml::Element *);
  void change(Rml::Element *);
  void flush_selects() {
    std::vector<std::string> ready;
    for (const auto &id : pending_selects_) {
      auto *select = dynamic_cast<Rml::ElementFormControlSelect *>(element(id));
      if (!select || !select->IsSelectBoxVisible())
        ready.push_back(id);
    }
    for (const auto &id : ready) {
      pending_selects_.erase(id);
      if (auto *select = element(id))
        change(select);
    }
  }
  void input(Rml::Element *);
  void scale_chalk_shadow() {
    // Pinned RmlUI font effects read integer pixels even for dp properties.
    // Match the HTML's two logical pixels at the current stage density.
    if (auto *label = doc_->QuerySelector(".tactic-effort-label")) {
      const auto offset = std::to_string(int(std::round(2 * scale_)));
      label->SetProperty("font-effect",
                          "shadow(" + offset + "px " + offset + "px #386824)");
    }
  }
  void resize() {
    int w, h, pw, ph;
    SDL_GetWindowSize(video_.window, &w, &h);
    video_.drawable_size(pw, ph);
    if (w <= 0 || h <= 0 || pw <= 0 || ph <= 0)
      return;
    scale_ = std::min(double(pw) / 3840, double(ph) / 2160);
    canvas_left_ = (pw - 3840 * scale_) / 2;
    canvas_top_ = (ph - 2160 * scale_) / 2;
    device_ratio_ = float(pw) / w;
    renderer_.SetViewport(pw, ph);
    context_->SetDimensions({pw, ph});
    context_->SetDensityIndependentPixelRatio(float(scale_));
    element("canvas")->SetProperty("left", scalar(canvas_left_) + "px");
    element("canvas")->SetProperty("top", scalar(canvas_top_) + "px");
    if (ticket_)
      ticket_->resize(device_ratio_);
    scale_chalk_shadow();
    dirty_ = true;
  }
  void render() {
    // A window-close save prompt can leave a menu while its local dialog is
    // open. Never keep that menu's modal/input gate in the destination view.
    if (menu_ != last_rendered_menu_) {
      planning_dialog_.clear();
      report_detail_kind_.clear();
      report_picker_.clear();
    }
    training_scroll_.reset();
    if (menu_ == "training" && last_rendered_menu_ == "training")
      if (auto *roster = element("training-roster"))
        training_scroll_ = roster->GetScrollTop();
    report_scroll_.reset();
    const auto report_state = source_ ? snap().state.at("reports").dump() : "";
    if (menu_ == last_rendered_menu_ && report_state == last_report_state_)
      if (auto *list = element("report-list"))
        report_scroll_ = list->GetScrollTop();
    if (ticket_)
      ticket_->close(false);
    context_open_ = false;
    context_dirty_ = true;
    doc_->SetClassNames(theme_);
    std::string markup;
    if (scene_ == "main")
      markup = main_menu();
    else if (scene_ == "mods")
      markup = mods_page();
    else if (scene_ == "newgame")
      markup = render_newgame();
    else if (scene_ == "manager")
      markup = render_manager();
    else if (scene_ == "opening")
      markup = render_opening();
    else if (source_)
      markup = render_desktop();
    if (!menu_.empty())
      markup += render_menu(menu_);
    element("canvas")->SetInnerRML(markup);
    if (!report_detail_kind_.empty() || !planning_dialog_.empty()) {
      Rml::ElementList behind;
      doc_->QuerySelectorAll(behind,
          "#report-underlay button,#report-underlay input,#report-underlay select,.window .titlebar button");
      for (auto *node : behind)
        node->SetProperty("pointer-events", "none");
    }
    const auto size = context_->GetDimensions();
    const auto background =
        img("background", size.x / scale_, size.y / scale_, false, true, 2048);
    element("background")->SetInnerRML(background);
    element("background")
        ->SetProperty("filter", scene_ == "main" ? "none"
                                : theme_ == "silver"
                                    ? "saturate(0.2) brightness(0.9)"
                                : theme_ == "matchday"
                                    ? "saturate(0.13) brightness(0.56)"
                                    : "saturate(0.34) brightness(0.70)");
    needs_render_ = false;
    last_rendered_menu_ = menu_;
    last_report_state_ = report_state;
    scale_chalk_shadow();
    dirty_ = true;
    if (focus_ticket_) {
      if (auto *n = element("desktop-ticket"))
        n->Focus();
      focus_ticket_ = false;
    }
    if (!focus_after_render_.empty()) {
      if (auto *node = element(focus_after_render_))
        node->Focus(keyboard_navigation_);
      if (focus_after_render_.starts_with("report-option-") ||
          focus_after_render_.starts_with("friendly-opponent-"))
        report_option_to_scroll_ = focus_after_render_;
      focus_after_render_.clear();
    }
  }
  void render_popup() {
    if (popup_.empty()) {
      element("popup")->SetClass("visible", false);
      rendered_popup_.clear();
      return;
    }
    element("popup")->SetClass("visible", true);
    if (popup_ == rendered_popup_ && bool(confirm_) == rendered_confirm_)
      return;
    element("popup")->SetInnerRML(
        "<div class=\"fill popup-backdrop\"></div><div "
        "class=\"popup-box\"><h2>OpenBMH</h2><p>" +
        esc(popup_) + "</p>" +
        (popup_has_input_
             ? "<input id=\"popup-text\" value=\"" + esc(popup_input_) +
                   "\" style=\"width:100%;margin-bottom:38dp;\"/>"
             : "") +
        "<footer>" +
        btn("popup-close",
            confirm_ || text_accept_ ? "Abbrechen" : "Verstanden") +
        popup_extra_ +
        ((confirm_ || text_accept_)
             ? btn("popup-confirm", popup_accept_, "primary")
             : "") +
        "</footer></div>");
    rendered_popup_ = popup_;
    rendered_confirm_ = bool(confirm_);
    if (auto *button = element("popup")->QuerySelector("button"))
      button->Focus();
  }
  void frame() {
    flush_selects();
    if (ticket_)
      ticket_->update();
    if (needs_render_)
      render();
    render_popup();
    render_context();
    context_->Update();
    if (center_selects(*doc_))
      context_->Update();
    if (training_scroll_) {
      if (auto *roster = element("training-roster"))
        roster->SetScrollTop(*training_scroll_);
      training_scroll_.reset();
    }
    if (report_scroll_) {
      if (auto *list = element("report-list"))
        list->SetScrollTop(*report_scroll_);
      report_scroll_.reset();
    }
    if (!report_option_to_scroll_.empty()) {
      if (auto *node = element(report_option_to_scroll_))
        node->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest));
      report_option_to_scroll_.clear();
    }
    renderer_.Clear();
    renderer_.BeginFrame();
    context_->Render();
    renderer_.EndFrame();
    video_.present();
    dirty_ = false;
  }
  void screenshot(const fs::path &file) {
    frame();
    int w, h;
    auto bytes = video_.capture(w, h);
    fs::create_directories(file.parent_path());
    editor::write_atomic(file,
                         editor::images::encode_png({w, h, std::move(bytes)}));
  }
  void load_fonts() {
    bool regular = false, bold = false;
#ifdef _WIN32
    const char *base = std::getenv("WINDIR");
    if (base) {
      const auto folder = fs::path(base) / "Fonts";
      regular = Rml::LoadFontFace(rml::asset_path(folder / "segoeui.ttf"),
                                  "Modern", Rml::Style::FontStyle::Normal,
                                  Rml::Style::FontWeight::Normal);
      bold = Rml::LoadFontFace(rml::asset_path(folder / "segoeuib.ttf"),
                               "Modern", Rml::Style::FontStyle::Normal,
                               Rml::Style::FontWeight::Bold);
    }
#elif defined(__APPLE__)
    const fs::path system = "/System/Library/Fonts/SFNS.ttf";
    if (fs::exists(system)) {
      regular = Rml::LoadFontFace(rml::asset_path(system), "Modern",
                                  Rml::Style::FontStyle::Normal,
                                  Rml::Style::FontWeight::Normal);
      bold = Rml::LoadFontFace(rml::asset_path(system), "Modern",
                               Rml::Style::FontStyle::Normal,
                               Rml::Style::FontWeight::Bold);
    }
#else
    const fs::path fonts = fs::exists("/usr/share/fonts/truetype/liberation2")
                               ? "/usr/share/fonts/truetype/liberation2"
                               : "/usr/share/fonts/truetype/liberation";
    if (fs::exists(fonts)) {
      regular = Rml::LoadFontFace(
          rml::asset_path(fonts / "LiberationSans-Regular.ttf"), "Modern",
          Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal);
      bold = Rml::LoadFontFace(
          rml::asset_path(fonts / "LiberationSans-Bold.ttf"), "Modern",
          Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Bold);
    }
#endif
    if (!regular)
      regular = Rml::LoadFontFace(
          rml::asset_path(assets_ / "LatoLatin-Regular.ttf"), "Modern",
          Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal);
    if (!bold)
      bold = Rml::LoadFontFace(rml::asset_path(assets_ / "LatoLatin-Bold.ttf"),
                               "Modern", Rml::Style::FontStyle::Normal,
                               Rml::Style::FontWeight::Bold);
    std::vector<fs::path> chalk_fonts;
#ifdef _WIN32
    if (const char *windir = std::getenv("WINDIR")) {
      chalk_fonts.push_back(fs::path(windir) / "Fonts" / "comic.ttf");
      chalk_fonts.push_back(fs::path(windir) / "Fonts" / "times.ttf");
    }
#elif defined(__APPLE__)
    chalk_fonts = {"/System/Library/Fonts/Supplemental/Comic Sans MS.ttf",
                   "/System/Library/Fonts/Supplemental/Apple Chancery.ttf",
                   "/System/Library/Fonts/Times.ttc"};
#else
    chalk_fonts = {
        "/usr/share/fonts/truetype/liberation2/LiberationSerif-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf"};
#endif
    chalk_fonts.push_back(assets_ / "LatoLatin-Regular.ttf");
    for (const auto &font : chalk_fonts)
      if (fs::exists(font) && Rml::LoadFontFace(rml::asset_path(font), "Chalk",
                                                Rml::Style::FontStyle::Normal,
                                                Rml::Style::FontWeight::Normal))
        break;
    if (!regular || !bold)
      throw std::runtime_error(
          "GUI-Schriften fehlen. Bitte das vollständige Paket entpacken.");
  }
  void script();

public:
  App(fs::path executable, launch::SaveMigrator migrate, Config config)
      : executable_(std::move(executable)), root_(executable_.parent_path()),
        assets_(root_ / "assets" / "modern"),
        gui_root_(config.gui_assets.empty() ? assets_ / "gui"
                                            : config.gui_assets),
        migrate_(std::move(migrate)), config_(std::move(config)),
        gui_(gui_root_),
        video_("OpenBMH", config_.width, config_.height, 960, 540),
        system_(video_.window), renderer_(video_, gui_, assets_) {
    try {
      Rml::SetSystemInterface(&system_);
      Rml::SetFileInterface(&files_);
      Rml::SetRenderInterface(&renderer_);
      Rml::SetTextInputHandler(&ime_);
      initialized_ = Rml::Initialise();
      if (!initialized_)
        throw std::runtime_error("RmlUI initialization failed");
      load_fonts();
      context_ =
          Rml::CreateContext("modern-ui", {config_.width, config_.height});
      if (!context_)
        throw std::runtime_error("Modern UI context failed");
      doc_ = context_->LoadDocument(rml::asset_path(assets_ / "modern.rml"));
      if (!doc_)
        throw std::runtime_error("Modern UI layout missing");
      for (const char *event :
           {"click", "dblclick", "change", "input", "keydown", "blur", "mousedown",
            "mouseup", "mousemove", "contextmenu"})
        doc_->AddEventListener(event, this, Rml::String(event) == "keydown" ||
                                               Rml::String(event) == "blur");
      if (!config_.theme.empty())
        theme_ = config_.theme;
      if (theme_ != "midnight" && theme_ != "silver" && theme_ != "matchday")
        throw editor::Error("Unbekanntes Modern-Theme.");
      collect_mods();
      if (!mods_.empty())
        active_mod_ = mods_.front().name;
      if (!config_.world.empty()) {
        active_mod_.clear();
        load_world(config_.world);
        scene_ = config_.scene;
        if (scene_ != "main" && scene_ != "newgame" && scene_ != "manager" &&
            scene_ != "opening" && scene_ != "desktop") {
          menu_ = scene_;
          scene_ = "desktop";
        }
      }
      if (!config_.state.empty() && source_) {
        desktop_ = source_->load(config_.state);
        saved_desktop_ = desktop_;
        if (config_.theme.empty())
          theme_ = snap().state.at("preferences").value("theme", theme_);
      }
      if (source_ && !config_.theme.empty()) {
        auto preferences = snap().state.at("preferences");
        preferences["theme"] = theme_;
        apply("preferences", preferences);
      }
      if (scene_ == "manager" && selections_["manager-name"].empty())
        selections_["manager-name"] = "Alex Berger";
      doc_->Show();
      resize();
      render();
      SDL_ShowWindow(video_.window);
      frame();
    } catch (...) {
      shutdown();
      throw;
    }
  }
  void shutdown() {
    ticket_.reset();
    if (doc_) {
      doc_->Close();
      doc_ = nullptr;
    }
    if (context_) {
      const auto name = context_->GetName();
      context_ = nullptr;
      Rml::RemoveContext(name);
    }
    if (initialized_) {
      Rml::Shutdown();
      initialized_ = false;
    }
  }
  ~App() { shutdown(); }
  void ProcessEvent(Rml::Event &event) override;
  int run() {
    if (!config_.commands.empty())
      script();
    if (!config_.screenshot.empty()) {
      screenshot(config_.screenshot);
      return 0;
    }
    while (!closed_) {
      if (scene_ == "opening" && opening_page_ == 3 && draw_revealed_ < 64 &&
          draw_playing_ && SDL_GetTicks64() >= draw_at_ &&
          (SDL_GetWindowFlags(video_.window) & SDL_WINDOW_INPUT_FOCUS)) {
        ++draw_revealed_;
        draw_at_ = SDL_GetTicks64() + 900;
        if (draw_revealed_ == 64)
          opening_page_ = 4;
        refresh();
      }
      if (needs_render_ || dirty_)
        frame();
      SDL_Event event;
      const auto timeout =
          int(std::clamp(context_->GetNextUpdateDelay(), 0.0, .2) * 1000);
      bool got = SDL_WaitEventTimeout(&event, timeout) != 0;
      while (got) {
        if (event.type == SDL_QUIT) {
          if (source_ && scene_ != "main") {
            dispatch("context-quit", nullptr);
          } else
            closed_ = true;
        } else {
          if (event.type == SDL_WINDOWEVENT)
            resize();
          int w, h, pw, ph;
          SDL_GetWindowSize(video_.window, &w, &h);
          video_.drawable_size(pw, ph);
          if (w > 0 && h > 0) {
            if (event.type == SDL_MOUSEMOTION) {
              event.motion.x = int(double(event.motion.x) * pw / w);
              event.motion.y = int(double(event.motion.y) * ph / h);
            }
            if (event.type == SDL_MOUSEBUTTONDOWN ||
                event.type == SDL_MOUSEBUTTONUP) {
              event.button.x = int(double(event.button.x) * pw / w);
              event.button.y = int(double(event.button.y) * ph / h);
              context_->ProcessMouseMove(event.button.x, event.button.y,
                                         RmlSDL::GetKeyModifierState());
            }
          }
          RmlSDL::InputEventHandler(context_, video_.window, event);
          dirty_ = true;
        }
        got = SDL_PollEvent(&event) != 0;
      }
      if (context_->GetNextUpdateDelay() <= 0)
        dirty_ = true;
    }
    return 0;
  }
};
#include "modern/ui/controls.inc"
#include "modern/ui/desktop_screens.inc"
#include "modern/ui/screens.inc"
#include "modern/ui/sports_screens.inc"
#include "modern/ui/calendar_screen.inc"
#include "modern/ui/report_pickers.inc"
#include "modern/ui/report_details.inc"
#include "modern/ui/reports_screens.inc"
#include "modern/ui/indoor_screen.inc"
#include "modern/ui/planning_screens.inc"
#include "modern/ui/planning_controls.inc"
} // namespace
bool is_modern_world(const fs::path &file) {
  try {
    auto store = editor::ArchiveStore::open(file);
    return Catalog::modern_manifest(Json::parse(store->read("manifest.json")));
  } catch (const std::exception &) {
    return false;
  }
}
int run(const fs::path &executable, const launch::SaveMigrator &migrate,
        const Config &config) {
  App app(executable, migrate, config);
  return app.run();
}
} // namespace bmh::modern_ui
