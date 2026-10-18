// SPDX-License-Identifier: GPL-3.0-only
#include "launch/archive.hpp"
#include "launch/mods.hpp"
#include "modern/json.hpp"
#include "modern/model.hpp"
#include "version.hpp"
#include <deque>
#include <iomanip>
#include <sstream>
namespace bmh::modern {
using namespace detail;
namespace {
using rml::escape;
std::string button(const std::string &action, const std::string &label, const std::string &css = "",
                   bool active = true) {
    return "<button " + std::string(active ? "" : "disabled=\"disabled\" ") + "class=\"" + css +
           "\" data-action=\"" + escape(action) + "\"><span>" + escape(label) + "</span></button>";
}
std::string amount(int32_t value) {
    const bool negative = value < 0;
    std::string raw = std::to_string(negative ? -int64_t(value) : int64_t(value));
    for (int i = int(raw.size()) - 3; i > 0; i -= 3)
        raw.insert(size_t(i), ".");
    return (negative ? "−" : "") + raw;
}
std::string position(int value) {
    static const char *names[] = {"TW", "ABW", "MIT", "ANG"};
    return names[std::clamp(value, 0, 3)];
}
class View final : public rml::Window, public Rml::EventListener {
    Model model_;
    std::shared_ptr<const content::Classic> content_;
    std::optional<Snapshot> observed_;
    std::deque<std::string> commands_;
    std::string page_ = "office", modal_, deferred_, after_save_, save_name_;
    std::vector<std::filesystem::path> files_;
    int selected_ = -1, second_ = -1, league_ = 0;
    bool lineup_open_ = false;
    void ProcessEvent(Rml::Event &event) override {
        for (auto *target = event.GetTargetElement(); target && target != document_;
             target = target->GetParentNode())
            if (target->HasAttribute("data-action")) {
                if (!target->HasAttribute("disabled"))
                    commands_.push_back(target->GetAttribute<Rml::String>("data-action", ""));
                return;
            }
    }
    void on_close() override { commands_.push_back("quit"); }
    bool on_key(const SDL_Event &event) override {
        if (event.type != SDL_KEYDOWN || event.key.repeat)
            return false;
        if (!modal_.empty() && event.key.keysym.sym == SDLK_TAB) {
            trap_tab(event, "modal");
            return true;
        }
        if (event.key.keysym.sym == SDLK_ESCAPE) {
            commands_.push_back(modal_.empty() ? "nav:office" : "cancel");
            return true;
        }
        if (event.key.keysym.sym == SDLK_RETURN && !modal_.empty()) {
            auto *focus = context_->GetFocusElement();
            if (focus && focus->GetTagName() == "select")
                return false;
            if (!focus || !focus->HasAttribute("data-action")) {
                commands_.push_back("accept");
                return true;
            }
        }
        if (modal_.empty() && (event.key.keysym.mod & (KMOD_CTRL | KMOD_GUI))) {
            if (event.key.keysym.sym == SDLK_s) {
                commands_.push_back("save");
                return true;
            }
            if (event.key.keysym.sym == SDLK_l) {
                commands_.push_back("load");
                return true;
            }
        }
        return false;
    }
    void close_modal() {
        modal_.clear();
        display("overlay", false);
        SDL_StopTextInput();
        element(model_.active() ? "nav-office" : "new")->Focus(true);
    }
    void modal(std::string kind, const std::string &title, const std::string &body, const std::string &accept,
               bool cancel = true) {
        modal_ = std::move(kind);
        text("modal-title", title);
        element("modal-body")->SetInnerRML(body);
        text("modal-error", "");
        text("accept", accept);
        display("cancel", cancel, "inline-block");
        display("discard", modal_ == "unsaved", "inline-block");
        display("overlay", true);
        context_->Update();
        Rml::ElementList inputs;
        element("modal-body")->QuerySelectorAll(inputs, "input, select");
        if (!inputs.empty())
            inputs.front()->Focus(true);
        else
            element(cancel ? "cancel" : "accept")->Focus(true);
        dirty_ = true;
    }
    void manager_dialog() {
        std::string choices;
        for (const auto &club : model_.clubs())
            choices +=
                "<option value=\"" + std::to_string(club.selection) + "\">" + escape(club.name) + "</option>";
        modal("manager", "Manager " + std::to_string(model_.manager_number()),
              "<label>Dein Name</label><input id=\"manager-name\" type=\"text\" "
              "value=\"Manager " +
                  std::to_string(model_.manager_number()) +
                  "\" maxlength=\"24\"/><label>Verein</label><select "
                  "id=\"club-choice\">" +
                  choices +
                  "</select><div class=\"form-note\">Vereinskarriere · mittlere "
                  "Mannschaftsstärke · Original-Spielregeln</div>",
              "Übernehmen");
    }
    void execute(const std::string &action) {
        if (action == "quit") {
            closed_ = true;
            return;
        }
        if (action == "new") {
            modal("setup", "Neue Karriere",
                  "<label>Spielstufe</label><select id=\"difficulty\"><option "
                  "value=\"0\">1 · Einsteiger</option><option "
                  "value=\"1\">2</option><option value=\"2\" "
                  "selected=\"selected\">3</option><option "
                  "value=\"3\">4</option><option value=\"4\">5</option><option "
                  "value=\"5\">6</option><option value=\"6\">7</option><option "
                  "value=\"7\">8</option><option value=\"8\">9</option><option "
                  "value=\"9\">10 · "
                  "Schwer</option></select><label>Mitspieler</label><select "
                  "id=\"manager-count\"><option value=\"1\">1</option><option "
                  "value=\"2\">2</option><option value=\"3\">3</option><option "
                  "value=\"4\">4</option></select><div "
                  "class=\"form-note\">Vereinskarriere mit den Regeln deiner "
                  "importierten Spielwelt. Die erste Version endet in der "
                  "Spielvorbereitung.</div>",
                  "Verein wählen");
        } else if (action == "load") {
            files_ = model_.saves();
            if (files_.empty()) {
                modal("info", "Spielstand laden",
                      "Für diese Spielwelt sind noch keine nativen Spielstände "
                      "vorhanden.",
                      "OK", false);
                return;
            }
            std::string choices;
            for (size_t i = 0; i < files_.size(); ++i)
                choices += "<option value=\"" + std::to_string(i) + "\">" +
                           escape(launch::path_to_utf8(files_[i].stem())) + "</option>";
            modal("load", "Karriere laden",
                  "<label>Spielstand</label><select id=\"save-choice\">" + choices + "</select>", "Laden");
        }
    }
    void request(const std::string &action) {
        if (model_.dirty()) {
            deferred_ = action;
            modal("unsaved", "Karriere speichern?", "Deine Änderungen sind noch nicht gespeichert.",
                  "Speichern");
        } else
            execute(action);
    }
    void save_dialog() {
        if (!model_.active())
            return;
        modal("save", "Karriere speichern",
              "<label>Bezeichnung</label><input id=\"save-name\" type=\"text\" "
              "value=\"Modern Karriere\" maxlength=\"100\"/>",
              "Speichern");
    }
    void saved(bool overwrite) {
        model_.save(save_name_, overwrite);
        close_modal();
        refresh();
        const auto next = after_save_;
        after_save_.clear();
        if (!next.empty())
            execute(next);
    }
    void dispatch(const std::string &action) {
        if (action == "cancel") {
            model_.cancel_new();
            after_save_.clear();
            deferred_.clear();
            close_modal();
            return;
        }
        if (action == "discard" && modal_ == "unsaved") {
            const auto next = deferred_;
            deferred_.clear();
            close_modal();
            execute(next);
            return;
        }
        if (action == "accept") {
            const auto current = modal_;
            if (current == "setup") {
                model_.begin_new(std::stoi(value("difficulty")), std::stoi(value("manager-count")));
                manager_dialog();
            } else if (current == "manager") {
                if (model_.confirm_manager(value("manager-name"), std::stoi(value("club-choice")))) {
                    close_modal();
                    page_ = "office";
                    lineup_open_ = false;
                    selected_ = second_ = -1;
                    refresh();
                } else
                    manager_dialog();
            } else if (current == "load") {
                model_.load(files_.at(size_t(std::stoul(value("save-choice")))));
                close_modal();
                page_ = "office";
                lineup_open_ = false;
                selected_ = second_ = -1;
                refresh();
            } else if (current == "save") {
                save_name_ = value("save-name");
                auto stem = save_name_;
                if (stem.ends_with(".obs"))
                    stem.resize(stem.size() - 4);
                launch::ModLibrary::validate_name(stem);
                const auto files = model_.saves();
                const bool exists = std::any_of(files.begin(), files.end(), [&](const auto &path) {
                    return launch::path_to_utf8(path.stem()) == stem;
                });
                if (exists)
                    modal("overwrite", "Spielstand überschreiben?", escape(stem), "Überschreiben");
                else
                    saved(false);
            } else if (current == "overwrite")
                saved(true);
            else if (current == "unsaved") {
                after_save_ = deferred_;
                deferred_.clear();
                save_dialog();
            } else
                close_modal();
            return;
        }
        if (!modal_.empty())
            return;
        if (action == "new" || action == "load" || action == "quit") {
            request(action);
            return;
        }
        if (action == "save") {
            save_dialog();
            return;
        }
        if (action.starts_with("nav:")) {
            if (!model_.active())
                return;
            const auto next = action.substr(4);
            if (next == "lineup" && !lineup_open_) {
                model_.prepare();
                lineup_open_ = true;
            }
            page_ = next;
            refresh();
            return;
        }
        if (action.starts_with("player:")) {
            const int row = std::stoi(action.substr(7));
            if (selected_ < 0 || second_ >= 0 || selected_ == row) {
                selected_ = row;
                second_ = -1;
            } else
                second_ = row;
            refresh();
            return;
        }
        if (action == "swap" && observed_ && selected_ >= 0 && second_ >= 0) {
            model_.swap(observed_->squad, selected_, second_);
            second_ = -1;
            refresh();
            return;
        }
        if (action.starts_with("league:")) {
            league_ = std::stoi(action.substr(7));
            refresh();
        }
    }
    std::string office(const Snapshot &s) const {
        const auto &d = s.desk;
        std::string opponent =
            d.next_opponent_name.empty() ? "Noch offen" : ui::original_to_utf8(d.next_opponent_name);
        return "<div class=\"cards\"><div class=\"card panel\"><div "
               "class=\"card-label\">TABELLENPLATZ</div><div "
               "class=\"card-value\">" +
               (d.table_position ? std::to_string(*d.table_position) : "—") +
               "</div><div class=\"muted\">Deine aktuelle Liga</div></div><div "
               "class=\"card panel\"><div class=\"card-label\">NÄCHSTER "
               "GEGNER</div><div class=\"card-value small\">" +
               escape(opponent) + "</div><div class=\"muted\">" +
               escape(ui::original_to_utf8(d.next_competition_label)) +
               "</div></div><div class=\"card panel\"><div "
               "class=\"card-label\">MANNSCHAFTSSTÄRKE</div><div "
               "class=\"card-value\">" +
               (d.strength ? std::to_string(*d.strength) : "—") +
               "</div><div "
               "class=\"muted\">Original-Bewertung</div></div></div><div "
               "class=\"section panel\"><h2>Dein Verein im Blick</h2><p>Bereite "
               "deine Mannschaft vor, prüfe den Kader und behalte die Tabelle im "
               "Auge. Alle Änderungen an der Aufstellung werden in deiner Karriere "
               "gespeichert.</p>" +
               button("nav:lineup", "Aufstellung öffnen", "primary") +
               button("nav:table", "Tabelle ansehen") +
               "</div><div class=\"section "
               "panel\"><h2>Spielvorbereitung</h2><p>Training, Transfers, Finanzen "
               "und Stadion sowie der nächste Spieltag werden Schritt für Schritt "
               "ergänzt.</p><span class=\"badge\">Vorbereitungs-POC</span></div>";
    }
    std::string lineup(const Snapshot &s) const {
        std::string rows, markers,
            detail = "<div class=\"muted\">Wähle einen Spieler für seine Daten. "
                     "Zwei Spieler auswählen und Tauschen bestätigen.</div>";
        for (const auto &p : s.squad.players) {
            if (p.player_id < 0 || p.player_id == 0xfff)
                continue;
            const bool selected = p.row == selected_ || p.row == second_;
            const std::string a = "player:" + std::to_string(p.row);
            rows += "<button class=\"player-row" + std::string(selected ? " selected" : "") +
                    "\" data-action=\"" + a + "\"><span class=\"num\">" + std::to_string(p.row + 1) +
                    "</span><span class=\"player-name\">" + escape(ui::original_to_utf8(p.name)) +
                    "</span><span class=\"pos\">" + position(p.attributes.position()) +
                    "</span><span class=\"stat\">" + std::to_string(p.attributes.strength()) +
                    "</span><span class=\"stat\">" + std::to_string(p.attributes.condition()) +
                    "</span><span class=\"stat\">" + std::to_string(p.attributes.form()) + "</span></button>";
            if (p.row == selected_)
                detail = "<img class=\"portrait\" src=\"" + escape(model_.photo(p.player_id)) +
                         "\"/><div class=\"club-name\">" + escape(ui::original_to_utf8(p.name)) +
                         "</div><div class=\"muted\">" + position(p.attributes.position()) + " · " +
                         std::to_string(p.attributes.age()) + " Jahre<br/>Technik " +
                         std::to_string(p.attributes.technique()) + "<br/>Stärke " +
                         std::to_string(p.attributes.strength()) + " · Kondition " +
                         std::to_string(p.attributes.condition()) + "</div>";
        }
        for (const auto &marker : s.pitch) {
            const float x = 12 + marker.lane * 76, y = 90 - marker.depth * 76;
            const bool selected = marker.row == selected_ || marker.row == second_;
            markers += "<button class=\"pitch-player" + std::string(selected ? " selected" : "") +
                       "\" style=\"left: " + std::to_string(x) + "%; top: " + std::to_string(y) +
                       "%;\" data-action=\"player:" + std::to_string(marker.row) + "\"><span>" +
                       std::to_string(marker.row + 1) + "</span></button>";
        }
        return "<div class=\"lineup-layout\"><div class=\"roster panel\"><div "
               "class=\"toolbar\"><h2>Kader &amp; Aufstellung</h2>" +
               button("swap", "Tauschen", selected_ >= 0 && second_ >= 0 ? "primary" : "",
                      selected_ >= 0 && second_ >= 0) +
               "</div><div class=\"sheet-head\"><span "
               "class=\"num\">Nr.</span><span "
               "class=\"player-name\">Spieler</span><span "
               "class=\"pos\">Pos.</span><span class=\"stat\">STÄ</span><span "
               "class=\"stat\">KO</span><span class=\"stat\">FO</span></div>" +
               rows +
               "</div><div class=\"pitch-panel panel\"><h2>Deine "
               "Mannschaft</h2><div class=\"pitch-board\"><div "
               "class=\"pitch-half\"/><div class=\"pitch-circle\"/><div "
               "class=\"pitch-box top\"/><div class=\"pitch-box bottom\"/>" +
               markers + "</div><div class=\"player-detail\">" + detail + "</div></div></div>";
    }
    std::string table(const Snapshot &s) {
        if (s.tables.empty())
            return "<div class=\"section panel\">Keine Tabelle verfügbar.</div>";
        if (league_ < 0 || size_t(league_) >= s.tables.size())
            league_ = 0;
        std::string tabs;
        for (size_t i = 0; i < s.tables.size(); ++i)
            tabs += button("league:" + std::to_string(i), s.tables[i].league_name,
                           int(i) == league_ ? "selected" : "");
        std::string out = "<div class=\"league-tabs\">" + tabs +
                          "</div><div class=\"league-sheet panel\"><div class=\"sheet-head "
                          "league-head\"><span class=\"num\">Pl.</span><span "
                          "class=\"player-name\">Verein</span>";
        for (const char *label : {"Sp.", "S", "U", "N", "Tore", "Pkt."})
            out += "<span class=\"stat\">" + std::string(label) + "</span>";
        out += "</div>";
        for (const auto &row : s.tables[size_t(league_)].rows) {
            out += "<div class=\"league-row" +
                   std::string(row.runtime_team_id == s.desk.runtime_club ? " own" : "") +
                   "\"><span class=\"num\">" + std::to_string(row.rank) +
                   "</span><img class=\"crest\" src=\"" + model_.crest(row.club_identity_key) +
                   "\"/><span class=\"player-name\">" + escape(row.name) + "</span>";
            for (const auto value : {row.games, row.wins, row.draws, row.losses})
                out += "<span class=\"stat\">" + std::to_string(value) + "</span>";
            out += "<span class=\"stat\">" + std::to_string(row.goals_for) + ":" +
                   std::to_string(row.goals_against) + "</span><span class=\"stat\">" +
                   std::to_string(row.points) + "</span></div>";
        }
        return out + "</div>";
    }
    void refresh() {
        for (const auto *id : {"nav-office", "nav-lineup", "nav-table", "save"})
            enabled(id, model_.active());
        if (!model_.active()) {
            text("club-header", "Dein Weg an die Seitenlinie beginnt hier.");
            element("content")->SetInnerRML(
                "<div class=\"section panel welcome\"><h1>Willkommen bei "
                "OpenBMH</h1><p>Die vertrauten Aufgaben eines Fußballmanagers, in "
                "einem neuen Look. Starte eine Karriere oder spiele mit deinem "
                "vorhandenen Spielstand weiter.</p>" +
                button("new", "Neue Karriere", "primary") + button("load", "Karriere laden") + "</div>");
            dirty_ = true;
            return;
        }
        observed_ = model_.snapshot();
        const auto &s = *observed_;
        const auto &d = s.desk;
        int identity = -1;
        if (d.manager_slot >= 0 && size_t(d.manager_slot) < d.managers.size())
            identity = d.managers[size_t(d.manager_slot)].club_identity_key;
        const auto currency =
            str(get(parse(content_->documents, "manifest.json"), "currency").object()->at("code"));
        element("club-header")
            ->SetInnerRML("<img class=\"crest\" src=\"" + model_.crest(identity) +
                          "\"/><div class=\"club-meta\"><div class=\"club-name\">" +
                          escape(ui::original_to_utf8(d.club_name)) + "</div><div class=\"muted\">" +
                          escape(ui::original_to_utf8(d.manager_name)) +
                          "</div></div><div class=\"header-right\"><div class=\"money\">" +
                          amount(d.balance) + " " + escape(currency) + "</div><div class=\"muted\">" +
                          std::to_string(d.date.day) + ". " + std::to_string(d.date.month) + ". " +
                          std::to_string(d.date.year) + "</div></div>");
        text("page-title", page_ == "office" ? "Dein Büro" : page_ == "lineup" ? "Aufstellung" : "Tabelle");
        text("page-subtitle", page_ == "office"   ? "Alles Wichtige für deinen Verein."
                              : page_ == "lineup" ? "Spieler wählen, Aufstellung vorbereiten."
                                                  : "Deine Liga und ihre Mannschaften.");
        text("career-status", model_.dirty() ? "Ungespeicherte Änderungen" : "Gespeichert");
        for (const auto *page : {"office", "lineup", "table"})
            element((std::string("nav-") + page).c_str())->SetClass("selected", page_ == page);
        element("content")->SetInnerRML(page_ == "office"   ? office(s)
                                        : page_ == "lineup" ? lineup(s)
                                                            : table(s));
        dirty_ = true;
    }

  public:
    View(const std::filesystem::path &data, const std::filesystem::path &saves,
         std::shared_ptr<const content::Classic> content, uint32_t seed)
        : Window("modern/modern.rml", "OpenBMH Modern", 1280, 800, 1024, 640, false,
                 [this](const std::string &key) { return model_.image(key); }),
          model_(data, saves, content, seed), content_(std::move(content)) {
        document_->AddEventListener("click", this);
        text("version", std::string("v") + bmh::kOpenBmhVersion);
        refresh();
        element("new")->Focus(true);
    }
    ~View() override { shutdown(); }
    int run() {
        while (!closed_) {
            step();
            while (!commands_.empty()) {
                auto action = std::move(commands_.front());
                commands_.pop_front();
                try {
                    dispatch(action);
                } catch (const std::exception &error) {
                    if (modal_.empty())
                        modal("info", "OpenBMH · Hinweis", escape(error.what()), "OK", false);
                    else
                        text("modal-error", error.what());
                }
            }
        }
        return 0;
    }
};
} // namespace
int run(const std::filesystem::path &data, const std::filesystem::path &saves,
        std::shared_ptr<const content::Classic> content, uint32_t seed) {
    View view(data, saves, std::move(content), seed);
    return view.run();
}
} // namespace bmh::modern
