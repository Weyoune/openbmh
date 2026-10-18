// SPDX-License-Identifier: GPL-3.0-only
#include "launch/archive.hpp"
#include "launch/game_data.hpp"
#include "launch/launcher_ui.hpp"
#include "launch/process.hpp"
#include "launch/rml_window.hpp"
#include "version.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi_Platform_SDL.h>
#include <RmlUi_Renderer_GL3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <nfd.hpp>
#include <stdexcept>

namespace bmh::launch {
namespace {
using rml::escape;
class RmlLauncher final : public LauncherUI, public Rml::EventListener, public rml::Window {
    bool answered_ = false, accepted_ = false, busy_ = false, cancelled_ = false;
    bool cancellable_ = false, modal_visible_ = false;
    int selected_ = -1;
    std::string selected_name_;
    std::vector<ModEntry> mods_;
    std::optional<LauncherAction> action_;

    void select(int index, bool focus = false) {
        if (index < 0 || size_t(index) >= mods_.size())
            index = -1;
        selected_ = index;
        selected_name_ = index >= 0 ? mods_[size_t(index)].name : std::string{};
        for (size_t i = 0; i < mods_.size(); ++i) {
            auto *row = document_->GetElementById("mod-" + std::to_string(i));
            row->SetClass("selected", int(i) == selected_);
            row->SetAttribute("aria-selected", int(i) == selected_ ? "true" : "false");
            if (focus && int(i) == selected_) {
                row->Focus(true);
                row->ScrollIntoView();
            }
        }
        enabled("start", selected_ >= 0);
        enabled("delete", selected_ >= 0);
        enabled("rename", selected_ >= 0);
    }
    bool modal_open() const { return modal_visible_; }
    void close_modal() {
        modal_visible_ = false;
        display("overlay", false);
        answered_ = false;
        busy_ = false;
        SDL_StopTextInput();
        if (selected_ >= 0)
            select(selected_, true);
        else
            element("add")->Focus(true);
    }
    void modal(const std::string &title, const std::string &body, const std::string &accept,
               bool cancel = true, bool danger = false) {
        text("modal-title", title);
        element("modal-body")->SetInnerRML(body);
        text("modal-error", "");
        text("accept", accept);
        element("accept")->SetClass("danger", danger);
        element("accept")->SetClass("primary", !danger);
        display("accept", !accept.empty(), "inline-block");
        display("cancel", cancel, "inline-block");
        answered_ = accepted_ = false;
        display("overlay", true);
        modal_visible_ = true;
        context_->Update();
        if (auto *input = document_->GetElementById("mod-name"))
            input->Focus(true);
        else {
            Rml::ElementList controls;
            element("modal-body")->QuerySelectorAll(controls, "input, select");
            std::erase_if(controls, [](auto *control) { return control->HasAttribute("disabled"); });
            if (!controls.empty())
                controls.front()->Focus(true);
            else
                element(cancel ? "cancel" : "accept")->Focus(true);
        }
        context_->Update();
        dirty_ = true;
    }
    void dispatch(const std::string &command) {
        if (command == "cancel") {
            if (busy_) {
                if (cancellable_)
                    cancelled_ = true;
            } else {
                answered_ = true;
                accepted_ = false;
            }
        } else if (command == "accept" && !busy_) {
            answered_ = accepted_ = true;
        } else if (command == "browse" && !busy_) {
            try {
                if (auto path =
                        choose_folder("Spielstandsordner wählen", path_from_utf8(value("save-folder")))) {
                    auto *input = dynamic_cast<Rml::ElementFormControl *>(element("save-folder"));
                    input->SetValue(path_to_utf8(*path));
                }
            } catch (const std::exception &e) {
                text("modal-error", e.what());
            }
        } else if (!modal_open()) {
            if (command == "add")
                action_ = LauncherAction::add;
            else if (command == "end")
                action_ = LauncherAction::end;
            else if (selected_ >= 0) {
                if (command == "start")
                    action_ = LauncherAction::start;
                else if (command == "rename")
                    action_ = LauncherAction::rename;
                else if (command == "delete")
                    action_ = LauncherAction::remove;
            }
        }
        dirty_ = true;
    }
    void ProcessEvent(Rml::Event &event) override {
        const bool double_click = event.GetType() == "dblclick";
        if (event.GetType() != "click" && !double_click)
            return;
        for (auto *target = event.GetTargetElement(); target && target != document_;
             target = target->GetParentNode()) {
            if (target->HasAttribute("data-row") && !modal_open()) {
                select(target->GetAttribute<int>("data-row", -1));
                if (double_click)
                    dispatch("start");
                return;
            }
            if (target->HasAttribute("data-action")) {
                if (double_click)
                    return;
                if (!target->HasAttribute("disabled"))
                    dispatch(target->GetAttribute<Rml::String>("data-action", ""));
                return;
            }
        }
    }
    void on_close() override {
        closed_ = answered_ = true;
        accepted_ = false;
        cancelled_ = true;
        action_ = LauncherAction::end;
    }
    bool on_key(const SDL_Event &event) override {
        if (event.type != SDL_KEYDOWN || event.key.repeat)
            return false;
        const auto key = event.key.keysym.sym;
        if (key == SDLK_ESCAPE) {
            if (modal_open())
                dispatch("cancel");
            else
                action_ = LauncherAction::end;
        } else if (modal_open() && key == SDLK_TAB)
            trap_tab(event, "modal");
        else if (!modal_open() && (key == SDLK_UP || key == SDLK_DOWN)) {
            if (!mods_.empty())
                select(std::clamp(selected_ < 0 ? 0 : selected_ + (key == SDLK_UP ? -1 : 1), 0,
                                  int(mods_.size()) - 1),
                       true);
        } else if (key == SDLK_RETURN) {
            auto *focused = context_->GetFocusElement();
            if (focused && focused->GetTagName() == "select")
                return false;
            if (focused && focused->HasAttribute("data-action")) {
                if (!focused->HasAttribute("disabled"))
                    dispatch(focused->GetAttribute<Rml::String>("data-action", ""));
            } else if (modal_open() && !busy_)
                dispatch("accept");
            else if (!modal_open() && selected_ >= 0)
                dispatch("start");
        } else
            return false;
        dirty_ = true;
        return true;
    }
    bool wait_modal() {
        while (!answered_ && !closed_)
            step(true);
        answered_ = false;
        return accepted_ && !closed_;
    }

  public:
    RmlLauncher() : rml::Window("launcher/launcher.rml", "OpenBMH", 1100, 688, 800, 540, true) {
        document_->AddEventListener("click", this);
        document_->AddEventListener("dblclick", this);
        text("version", std::string("v") + bmh::kOpenBmhVersion);
        element("add")->Focus(true);
    }
    ~RmlLauncher() override { shutdown(); }
    LauncherChoice next(const std::vector<ModEntry> &mods) override {
        if (closed_)
            return {LauncherAction::end, -1};
        mods_ = mods;
        std::string rows;
        int selection = -1;
        for (size_t i = 0; i < mods.size(); ++i) {
            const auto &mod = mods[i];
            if (mod.name == selected_name_)
                selection = int(i);
            rows += "<button id=\"mod-" + std::to_string(i) + "\" class=\"mod-row" +
                    (mod.error.empty() ? "" : " invalid") + "\" data-row=\"" + std::to_string(i) +
                    "\" title=\"" + escape(mod.error) + "\"><span class=\"name\">" + escape(mod.name) +
                    "</span><span class=\"type\">" + escape(mod.type) + "</span></button>";
        }
        if (mods.empty())
            rows = "<div class=\"empty\">Keine Mods vorhanden.<div "
                   "class=\"empty-detail\">Über Add eine "
                   "Installation hinzufügen.</div></div>";
        element("mod-list")->SetInnerRML(rows);
        text("mod-count", std::to_string(mods.size()) + (mods.size() == 1 ? " Mod" : " Mods"));
        select(selection);
        action_.reset();
        while (!action_ && !closed_)
            step(true);
        return {action_.value_or(LauncherAction::end), selected_};
    }
    std::optional<std::filesystem::path> choose_folder(const std::string &title,
                                                       const std::filesystem::path &initial = {}) override {
        (void)title;
        NFD::Guard guard;
        NFD::UniquePathU8 result;
        const auto directory = path_to_utf8(initial.empty() ? executable_path({}).parent_path() : initial);
        const auto status = NFD::PickFolder(result, directory.c_str());
        SDL_RaiseWindow(window_);
        dirty_ = true;
        if (status == NFD_CANCEL)
            return {};
        if (status != NFD_OKAY)
            throw std::runtime_error(std::string("Ordnerauswahl: ") + NFD::GetError());
        return path_from_utf8(result.get());
    }
    std::optional<AddMod> configure_add(AddMod initial) override {
        std::vector<ModEntry> originals;
        std::string sources = "<option "
                              "value=\"installation\">BMH-/Football-Limited-Installation</option>";
        for (const auto &mod : mods_)
            if (mod.kind == ModType::Classic && mod.error.empty()) {
                sources += "<option value=\"" + std::to_string(originals.size()) +
                           "\">Classic-Import: " + escape(mod.name) + "</option>";
                originals.push_back(mod);
            }
        modal("Mod hinzufügen",
              "<label>Quelle</label><select id=\"source-kind\">" + sources +
                  "</select><div class=\"subtitle\">Vorhandene Classic-Imports "
                  "können als Modern-Variante genutzt werden. Ihre Spielstände "
                  "bleiben gemeinsam verfügbar.</div>",
              "Weiter");
        if (!wait_modal()) {
            close_modal();
            return {};
        }
        const auto source_kind = value("source-kind");
        close_modal();
        if (source_kind == "installation") {
            const auto source = choose_folder("BMH-/Football-Limited-Installation wählen");
            if (!source)
                return {};
            const auto check = check_game_directory(*source);
            initial.source = check.directory;
            initial.saves = check.directory;
            initial.name = check.family == GameFamily::english ? "FTDL-classic" : "BMH-classic";
        } else {
            const auto &source = originals.at(size_t(std::stoul(source_kind)));
            initial.source = source.directory / "content.obw";
            initial.from_classic = true;
            initial.type = ModType::Modern;
            initial.migrate = false;
            initial.name = source.name;
            if (initial.name.ends_with("-classic"))
                initial.name.resize(initial.name.size() - 8);
            initial.name += "-modern";
        }
        std::string form = "<label>Art</label><select id=\"mod-type\"" +
                           std::string(initial.from_classic ? " disabled=\"disabled\"" : "") +
                           "><option value=\"classic\">Classic</option><option value=\"modern\"" +
                           std::string(initial.type == ModType::Modern ? " selected=\"selected\"" : "") +
                           ">Modern</option></select><label>Bezeichnung</label><input "
                           "id=\"mod-name\" type=\"text\" maxlength=\"100\" value=\"" +
                           escape(initial.name) + "\"/>";
        if (initial.from_classic)
            form += "<div class=\"subtitle\">Die vorhandene Spielwelt bleibt "
                    "unverändert. Startjahr und native Spielstände werden gemeinsam "
                    "mit dem Classic-Import genutzt.</div>";
        else
            form += "<label>Startjahr (nur Anzeige)</label><input id=\"start-year\" "
                    "type=\"text\" maxlength=\"4\" value=\"" +
                    std::to_string(initial.start_year) +
                    "\"/><div class=\"check-row\"><input id=\"migrate\" "
                    "class=\"checkbox\" type=\"checkbox\" " +
                    std::string(initial.migrate ? "checked=\"checked\"" : "") +
                    "/><label for=\"migrate\">Spielstände/Einstellungen "
                    "übernehmen</label></div><label>Spielstandsordner</label><div "
                    "class=\"browse-row\"><input id=\"save-folder\" type=\"text\" "
                    "value=\"" +
                    escape(path_to_utf8(initial.saves)) +
                    "\"/><button data-action=\"browse\">Auswählen …</button></div>";
        const auto suggested_name = initial.name;
        modal("Mod konfigurieren", form, "Hinzufügen");
        while (wait_modal()) {
            try {
                initial.type = value("mod-type") == "modern" ? ModType::Modern : ModType::Classic;
                initial.name = value("mod-name");
                if (initial.type == ModType::Modern && !initial.from_classic &&
                    initial.name == suggested_name && initial.name.ends_with("-classic")) {
                    initial.name.resize(initial.name.size() - 8);
                    initial.name += "-modern";
                }
                ModLibrary::validate_name(initial.name);
                if (!initial.from_classic) {
                    const auto year = value("start-year");
                    size_t end = 0;
                    initial.start_year = std::stoi(year, &end);
                    if (end != year.size() || initial.start_year < 1900 || initial.start_year > 9998)
                        throw std::runtime_error("Startjahr muss zwischen 1900 und 9998 liegen.");
                    initial.migrate = element("migrate")->HasAttribute("checked");
                    initial.saves = path_from_utf8(value("save-folder"));
                }
                close_modal();
                return initial;
            } catch (const std::exception &e) {
                text("modal-error", e.what());
            }
        }
        close_modal();
        return {};
    }
    std::optional<std::string> rename(const std::string &old) override {
        modal("Mod umbenennen",
              "<label>Bezeichnung</label><input id=\"mod-name\" type=\"text\" "
              "maxlength=\"100\" value=\"" +
                  escape(old) + "\"/>",
              "Umbenennen");
        std::optional<std::string> result;
        while (wait_modal()) {
            try {
                auto name = value("mod-name");
                ModLibrary::validate_name(name);
                result = name;
                break;
            } catch (const std::exception &e) {
                text("modal-error", e.what());
            }
        }
        close_modal();
        return result;
    }
    bool confirm_delete(const std::string &name) override {
        modal("Mod löschen?",
              "„" + escape(name) +
                  "“<br/><br/>Spielstände und persönliche Einstellungen bleiben "
                  "erhalten.",
              "Löschen", true, true);
        const bool result = wait_modal();
        close_modal();
        return result;
    }
    void message(const std::string &value, bool error) override {
        if (closed_)
            return;
        modal(error ? "OpenBMH · Hinweis" : "OpenBMH", escape(value), "OK", false);
        wait_modal();
        close_modal();
    }
    void progress(const std::string &value, bool cancellable) override {
        if (!busy_) {
            modal(cancellable ? "Mod importieren" : "Spiel läuft", escape(value), "", cancellable);
            busy_ = true;
            cancelled_ = false;
            cancellable_ = cancellable;
        } else
            text("modal-body", value);
    }
    bool pump() override {
        step(false);
        return cancelled_ || closed_;
    }
    void finish_progress() override {
        if (busy_)
            close_modal();
    }
};
} // namespace
std::unique_ptr<LauncherUI> make_launcher_ui() { return std::make_unique<RmlLauncher>(); }
} // namespace bmh::launch
