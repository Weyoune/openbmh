// SPDX-License-Identifier: GPL-3.0-only
#include "launch/archive.hpp"
#include "launch/launcher_ui.hpp"
#include "launch/process.hpp"
#include "launch/rml_path.hpp"
#include "launch/rml_video.hpp"
#include "version.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi_Platform_SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <nfd.hpp>
#include <stdexcept>

namespace bmh::launch {
namespace {
std::string escape(std::string_view value) {
    std::string result;
    for (char c : value)
        switch (c) {
        case '&':
            result += "&amp;";
            break;
        case '<':
            result += "&lt;";
            break;
        case '>':
            result += "&gt;";
            break;
        case '"':
            result += "&quot;";
            break;
        case '\n':
            result += "<br/>";
            break;
        default:
            result += c;
        }
    return result;
}
int modifiers(Uint16 state) {
    int result = 0;
    if (state & KMOD_CTRL)
        result |= Rml::Input::KM_CTRL;
    if (state & KMOD_SHIFT)
        result |= Rml::Input::KM_SHIFT;
    if (state & KMOD_ALT)
        result |= Rml::Input::KM_ALT;
    if (state & KMOD_GUI)
        result |= Rml::Input::KM_META;
#ifdef __APPLE__
    // RmlUI text widgets use the Ctrl flag for editing shortcuts.
    if (state & KMOD_GUI)
        result |= Rml::Input::KM_CTRL;
#endif
    if (state & KMOD_CAPS)
        result |= Rml::Input::KM_CAPSLOCK;
    if (state & KMOD_NUM)
        result |= Rml::Input::KM_NUMLOCK;
    return result;
}
class RmlLauncher final : public LauncherUI, public Rml::EventListener {
    rml::Video video_{"OpenBMH", 1100, 688, 800, 540};
    rml::AssetFiles files_;
    SystemInterface_SDL system_{video_.window};
    rml::Renderer renderer_{video_};
    TextInputMethodEditor_SDL ime_;
    Rml::Context *context_ = nullptr;
    Rml::ElementDocument *document_ = nullptr;
    bool initialized_ = false, closed_ = false, dirty_ = true;
    bool answered_ = false, accepted_ = false, busy_ = false, cancelled_ = false;
    bool cancellable_ = false, modal_visible_ = false;
    int selected_ = -1;
    std::string selected_name_;
    std::vector<ModEntry> mods_;
    std::optional<LauncherAction> action_;

    Rml::Element *element(const char *id) const {
        auto *value = document_->GetElementById(id);
        if (!value)
            throw std::runtime_error(std::string("Launcher element missing: ") + id);
        return value;
    }
    void text(const char *id, const std::string &value) {
        element(id)->SetInnerRML(escape(value));
        dirty_ = true;
    }
    void display(const char *id, bool shown, const char *style = "block") {
        element(id)->SetProperty("display", shown ? style : "none");
        dirty_ = true;
    }
    void enabled(const char *id, bool active) {
        auto *button = element(id);
        if (active)
            button->RemoveAttribute("disabled");
        else
            button->SetAttribute("disabled", "disabled");
        button->SetPseudoClass("disabled", !active);
        button->SetProperty("focus", active ? "auto" : "none");
        dirty_ = true;
    }
    std::string value(const char *id) const {
        auto *input = dynamic_cast<Rml::ElementFormControl *>(element(id));
        if (!input)
            throw std::runtime_error("Launcher input has the wrong type");
        return input->GetValue();
    }
    void resize() {
        int w = 0, h = 0, pixels_w = 0, pixels_h = 0;
        SDL_GetWindowSize(video_.window, &w, &h);
        video_.drawable_size(pixels_w, pixels_h);
        if (w <= 0 || h <= 0 || pixels_w <= 0 || pixels_h <= 0)
            return;
        renderer_.SetViewport(pixels_w, pixels_h);
        context_->SetDimensions({pixels_w, pixels_h});
        const float scale = std::clamp(std::min(w / 1100.f, h / 688.f), .72f, 2.5f);
        context_->SetDensityIndependentPixelRatio(float(pixels_w) / w * scale);
        dirty_ = true;
    }
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
        else
            element(cancel ? "cancel" : "accept")->Focus(true);
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
    void step(bool wait) {
        // Modal visibility and refreshed rows must be laid out before queued
        // pointer events are hit-tested against the new document state.
        if (dirty_) context_->Update();
        SDL_Event event;
        const int timeout = dirty_ ? 0 : int(std::clamp(context_->GetNextUpdateDelay(), 0.0, .2) * 1000);
        bool got = wait ? SDL_WaitEventTimeout(&event, timeout) != 0 : SDL_PollEvent(&event) != 0;
        bool redraw = dirty_ || got || context_->GetNextUpdateDelay() <= 0;
        while (got) {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE)) {
                closed_ = answered_ = true;
                accepted_ = false;
                cancelled_ = true;
                action_ = LauncherAction::end;
            } else if (event.type == SDL_KEYDOWN && !event.key.repeat &&
                       event.key.keysym.sym == SDLK_ESCAPE) {
                if (modal_open())
                    dispatch("cancel");
                else
                    action_ = LauncherAction::end;
            } else if (event.type == SDL_KEYDOWN && modal_open() && event.key.keysym.sym == SDLK_TAB) {
                Rml::ElementList controls;
                element("modal")->QuerySelectorAll(controls, "input, button");
                std::erase_if(controls, [](auto *control) {
                    return !control->IsVisible() || control->HasAttribute("disabled");
                });
                if (!controls.empty()) {
                    const auto at = std::find(controls.begin(), controls.end(), context_->GetFocusElement());
                    const int current = at == controls.end() ? -1 : int(at - controls.begin());
                    const int step = event.key.keysym.mod & KMOD_SHIFT ? -1 : 1;
                    const int next = (current + step + int(controls.size())) % int(controls.size());
                    controls[size_t(next)]->Focus(true);
                }
            } else if (event.type == SDL_KEYDOWN && !modal_open() && !event.key.repeat &&
                       (event.key.keysym.sym == SDLK_UP || event.key.keysym.sym == SDLK_DOWN)) {
                const int direction = event.key.keysym.sym == SDLK_UP ? -1 : 1;
                if (!mods_.empty())
                    select(std::clamp(selected_ < 0 ? 0 : selected_ + direction, 0, int(mods_.size()) - 1),
                           true);
            } else if (event.type == SDL_KEYDOWN && !event.key.repeat &&
                       (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_SPACE) &&
                       context_->GetFocusElement() &&
                       context_->GetFocusElement()->HasAttribute("data-action")) {
                auto *focused = context_->GetFocusElement();
                if (!focused->HasAttribute("disabled"))
                    dispatch(focused->GetAttribute<Rml::String>("data-action", ""));
            } else if (event.type == SDL_KEYDOWN && !event.key.repeat &&
                       event.key.keysym.sym == SDLK_RETURN) {
                if (modal_open() && !busy_)
                    dispatch("accept");
                else if (!modal_open() && selected_ >= 0)
                    dispatch("start");
            } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                // Use the modifiers captured with this event, not SDL's final
                // state after pumping a batch containing the key releases.
                const auto key = RmlSDL::ConvertKey(event.key.keysym.sym);
                if (event.type == SDL_KEYDOWN)
                    context_->ProcessKeyDown(key, modifiers(event.key.keysym.mod));
                else
                    context_->ProcessKeyUp(key, modifiers(event.key.keysym.mod));
            } else {
                if (event.type == SDL_TEXTEDITING)
                    ime_.HandleEdit(event.edit);
                // SDL2 reports mouse coordinates in window units on Retina/HiDPI.
                int w = 0, h = 0, pw = 0, ph = 0;
                SDL_GetWindowSize(video_.window, &w, &h);
                video_.drawable_size(pw, ph);
                if (w > 0 && h > 0) {
                    if (event.type == SDL_MOUSEMOTION) {
                        event.motion.x = int(double(event.motion.x) * pw / w);
                        event.motion.y = int(double(event.motion.y) * ph / h);
                    }
                    if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
                        event.button.x = int(double(event.button.x) * pw / w);
                        event.button.y = int(double(event.button.y) * ph / h);
                        // Repeated clicks need a fresh hover target even when
                        // the pointer has not moved since a modal was closed.
                        context_->ProcessMouseMove(event.button.x, event.button.y,
                                                   RmlSDL::GetKeyModifierState());
                    }
                }
                RmlSDL::InputEventHandler(context_, video_.window, event);
            }
            if (event.type == SDL_WINDOWEVENT)
                resize();
            got = SDL_PollEvent(&event) != 0;
        }
        if (redraw) {
            context_->Update();
            renderer_.BeginFrame();
            renderer_.Clear();
            context_->Render();
            renderer_.EndFrame();
            video_.present();
            dirty_ = false;
        }
    }
    bool wait_modal() {
        while (!answered_ && !closed_)
            step(true);
        answered_ = false;
        return accepted_ && !closed_;
    }

  public:
    RmlLauncher() {
        SDL_StopTextInput();
        if (!renderer_)
            throw std::runtime_error("RmlUI could not create the graphics renderer");
        try {
            Rml::SetSystemInterface(&system_);
            Rml::SetFileInterface(&files_);
            Rml::SetRenderInterface(&renderer_);
            Rml::SetTextInputHandler(&ime_);
            initialized_ = Rml::Initialise();
            if (!initialized_)
                throw std::runtime_error("RmlUI initialization failed");
            const auto assets = executable_path({}).parent_path() / "assets" / "launcher";
            for (const char *weight : {"Regular", "Bold"})
                if (!Rml::LoadFontFace(rml::asset_path(assets / (std::string("LatoLatin-") + weight + ".ttf"))))
                    throw std::runtime_error("Launcher font missing; extract the complete OpenBMH package");
            context_ = Rml::CreateContext("launcher", {1100, 688});
            if (!context_)
                throw std::runtime_error("RmlUI launcher context failed");
            document_ = context_->LoadDocument(rml::asset_path(assets / "launcher.rml"));
            if (!document_)
                throw std::runtime_error("Launcher layout missing; extract the complete OpenBMH package");
            document_->AddEventListener("click", this);
            document_->AddEventListener("dblclick", this);
            text("version", std::string("v") + bmh::kOpenBmhVersion);
            resize();
            document_->Show();
            SDL_ShowWindow(video_.window);
            element("add")->Focus(true);
        } catch (...) {
            if (initialized_)
                Rml::Shutdown();
            throw;
        }
    }
    ~RmlLauncher() override {
        if (initialized_)
            Rml::Shutdown();
    }
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
            rows = "<div class=\"empty\">Keine Mods vorhanden.<div class=\"empty-detail\">Über Add eine "
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
        SDL_RaiseWindow(video_.window);
        dirty_ = true;
        if (status == NFD_CANCEL)
            return {};
        if (status != NFD_OKAY)
            throw std::runtime_error(std::string("Ordnerauswahl: ") + NFD::GetError());
        return path_from_utf8(result.get());
    }
    std::optional<AddMod> configure_add(AddMod initial) override {
        modal("Classic-Mod hinzufügen",
              "<label>Bezeichnung</label><input id=\"mod-name\" type=\"text\" maxlength=\"100\" value=\"" +
                  escape(initial.name) +
                  "\"/><div class=\"fields\"><div class=\"year\"><label>Startjahr (nur "
                  "Anzeige)</label><input id=\"start-year\" type=\"text\" maxlength=\"4\" value=\"" +
                  std::to_string(initial.start_year) +
                  "\"/></div></div><div class=\"check-row\"><input id=\"migrate\" class=\"checkbox\" "
                  "type=\"checkbox\" " +
                  (initial.migrate ? "checked=\"checked\"" : "") +
                  "/><label for=\"migrate\">Spielstände/Einstellungen "
                  "übernehmen</label></div><label>Spielstandsordner</label><div class=\"browse-row\"><input "
                  "id=\"save-folder\" type=\"text\" value=\"" +
                  escape(path_to_utf8(initial.saves)) +
                  "\"/><button data-action=\"browse\">Auswählen …</button></div>",
              "Importieren");
        while (wait_modal()) {
            try {
                initial.name = value("mod-name");
                ModLibrary::validate_name(initial.name);
                const auto year = value("start-year");
                size_t end = 0;
                initial.start_year = std::stoi(year, &end);
                if (end != year.size() || initial.start_year < 1900 || initial.start_year > 9998)
                    throw std::runtime_error("Startjahr muss zwischen 1900 und 9998 liegen.");
                initial.migrate = element("migrate")->HasAttribute("checked");
                initial.saves = path_from_utf8(value("save-folder"));
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
              "<label>Bezeichnung</label><input id=\"mod-name\" type=\"text\" maxlength=\"100\" value=\"" +
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
              "„" + escape(name) + "“<br/><br/>Spielstände und persönliche Einstellungen bleiben erhalten.",
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
