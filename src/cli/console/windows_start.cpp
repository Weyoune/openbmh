// SPDX-License-Identifier: GPL-3.0-only
// The start of a game (docs/CLI_CONCEPT/03-start.md): the options screen NewGame, the manager
// selection NewGame.Select with its confirmation, the info sequence after the last manager,
// and the file windows (load browser at the start, LoadGame/SaveGame on the desk, 04-schreibtisch 8).

#include <algorithm>

#include "cli/console/console.hpp"
#include "cli/console/detail.hpp"
#include "ui/cup_draw.hpp"
#include "ui/desk_state.hpp"
#include "ui/season_entrants.hpp"

namespace bmh::cli::console {

using namespace detail;

namespace {

// TEXTD indices (docs/formats/textd_inventory.csv)
constexpr int kStrengthBottomThird = 18;            // strengthBottomThird "unteres Drittel"
constexpr int kErrorNotAGameScore = 814;            // "Dies ist kein BUNDESLIGA MANAGER Hattrick - Spielstand!"
constexpr int kSaveConfirmOverwrite = 854;          // "Möchten Sie diese Datei wirklich überschreiben?"
constexpr int kManagerConfirmAllDetailsCorrect = 907;  // "Sind alle Angaben richtig?"

// The picker tabs as the console names them (03-start 3); index = ManagerScreen group.
const std::vector<std::string> kTabs = {"Bundesliga", "SecondLeague", "RegionalA", "RegionalB",
                                        "Oberliga", "AdditionalA", "AdditionalB"};
const std::vector<std::string> kStartLeagues = {"Bundesliga", "SecondLeague", "RegionalA", "RegionalB", "Oberliga"};
// ManagerChoice::strength: 0 lower, 1 middle, 2 upper third
const std::vector<std::string> kStrengths = {"Lower", "Middle", "Upper"};

}  // namespace

void Console::register_start_commands() {
    // --- NewGame: the options screen (03-start 1) -----------------------------------------
    add({"NewGame", "ShowOptions", {}, "show the options with their current values", false}, [this](const Values&) {
        emit_table({"Option", "Value"},
                   {{"duration", setup_.duration == 5 ? "endless" : std::to_string(setup_.duration)},
                    {"difficulty", std::to_string(setup_.difficulty + 1)},
                    {"managers", std::to_string(setup_.managers)},
                    {"mode", std::to_string(setup_.mode)},
                    {"league", on_off(setup_.league)},
                    {"world", on_off(setup_.world)}});
    });
    add({"NewGame", "NewGame",
         {integer("duration", 1, 5), integer("difficulty", 1, 10), integer("managers", 1, 4), integer("mode", 1, 3),
          boolean("league"), boolean("world")},
         "set all options and continue (= right mouse)", true},
        [this](const Values& v) {
            if (!v.boolean("league") && !v.boolean("world"))
                fail("E_BAD_ARGUMENT", "league and world mode cannot both be off");
            setup_.duration = int(v.integer("duration"));
            setup_.difficulty = int(v.integer("difficulty")) - 1;  // shown 1-10, stored 0-9
            setup_.managers = int(v.integer("managers"));
            setup_.mode = int(v.integer("mode"));
            setup_.league = v.boolean("league");
            setup_.world = v.boolean("world");
            new_game_continue();
        });
    add({"NewGame", "SetDuration", {integer("duration", 1, 5)}, "1-4 seasons, 5 = endless", true},
        [this](const Values& v) { setup_.duration = int(v.integer("duration")); emit('+', "duration set"); });
    add({"NewGame", "SetDifficulty", {integer("difficulty", 1, 10)}, "number of balls", true},
        [this](const Values& v) { setup_.difficulty = int(v.integer("difficulty")) - 1; emit('+', "difficulty set"); });
    add({"NewGame", "SetManagers", {integer("managers", 1, 4)}, "human managers", true},
        [this](const Values& v) { setup_.managers = int(v.integer("managers")); emit('+', "managers set"); });
    add({"NewGame", "SetMode", {integer("mode", 1, 3)}, "range of desk functions", true},
        [this](const Values& v) { setup_.mode = int(v.integer("mode")); emit('+', "mode set"); });
    add({"NewGame", "SetLeagueMode", {boolean("on")}, "league mode on/off", true}, [this](const Values& v) {
        if (!v.boolean("on") && !setup_.world) fail("E_RULE", "league and world mode cannot both be off");
        setup_.league = v.boolean("on");
        emit('+', "league mode " + on_off(setup_.league));
    });
    add({"NewGame", "SetWorldMode", {boolean("on")}, "world cup mode on/off", true}, [this](const Values& v) {
        if (!v.boolean("on") && !setup_.league) fail("E_RULE", "league and world mode cannot both be off");
        setup_.world = v.boolean("on");
        emit('+', "world mode " + on_off(setup_.world));
    });
    add({"NewGame", "OpenLoadBrowser", {}, "open the load browser", false},
        [this](const Values&) {
            browse_directory_ = std::filesystem::absolute(save_directory()).lexically_normal();
            selected_file_.clear();
            set_window("NewGame.LoadGame", 1);
        });
    add({"NewGame", "LoadGame", {detail::text("filename")}, "load a saved game", true}, [this](const Values& v) {
        std::filesystem::path path(v.text("filename"));
        if (path.is_relative()) path = save_directory() / path;
        load_game(path);
    });

    // --- NewGame.Select: who plays? (03-start 3) ------------------------------------------
    Param tab = detail::text("league", 0, true);
    tab.display = "[league:int|tab:enum]";
    add({"NewGame.Select", "ListTeams", {tab}, "clubs of a picker tab (Bundesliga ... AdditionalB)", false},
        [this](const Values& v) {
            int group = -1;
            if (v.has("league")) {
                Arg a;
                a.text = v.text("league");
                for (size_t i = 0; i < kTabs.size(); ++i)
                    if (canonical(kTabs[i]) == canonical(a.text)) group = int(i);
                if (group < 0)
                    if (const auto n = to_int(a); n && *n >= 0 && *n < ui::ManagerScreen::group_count) group = int(*n);
                if (group < 0) fail("E_BAD_ARGUMENT", "league must be 0..6 or one of Bundesliga, SecondLeague, RegionalA, "
                                                      "RegionalB, Oberliga, AdditionalA, AdditionalB");
            } else {
                for (int g = 0; g < ui::ManagerScreen::group_count; ++g)
                    if (draft_.selection_club >= picker_->group_first(g) && draft_.selection_club <= picker_->group_last(g)) group = g;
                if (group < 0) group = 0;
            }
            const std::string label = utf8(picker_->group_label(group));
            emit('i', canonical(label) == canonical(kTabs[size_t(group)]) ? label : kTabs[size_t(group)] + ": " + label);
            std::vector<std::vector<std::string>> rows;
            for (int sel = picker_->group_first(group); sel <= picker_->group_last(group); ++sel) {
                const int master = picker_->master_of(sel);
                std::string taken = "-";
                for (const auto& c : confirmed_)
                    if (c.master_club == master) taken = "P" + std::to_string(c.slot + 1);
                std::string name = utf8(picker_->club_name(master));
                if (master == draft_.master_club) name += " [selected]";
                rows.push_back({"club:" + std::to_string(master), name, taken});
            }
            emit_table({"Club", "Name", "Taken"}, rows);
        });
    add({"NewGame.Select", "ListPortraits", {}, "the twelve portraits and who took them", false}, [this](const Values&) {
        std::vector<std::vector<std::string>> rows;
        for (int p = 0; p < 12; ++p) {
            std::string taken = "-";
            for (const auto& c : confirmed_)
                if (c.portrait == p) taken = "P" + std::to_string(c.slot + 1);
            rows.push_back({std::to_string(p), taken + (p == draft_.portrait ? " [selected]" : "")});
        }
        emit_table({"Portrait", "Taken"}, rows);
    });
    add({"NewGame.Select", "ListStrengths", {}, "the three club strengths", false}, [this](const Values&) {
        std::vector<std::vector<std::string>> rows;
        for (int s = 2; s >= 0; --s) rows.push_back({kStrengths[size_t(s)], text(kStrengthBottomThird + s)});
        emit_table({"Strength", "Text"}, rows);
    });
    add({"NewGame.Select", "ShowSelection", {}, "the choices of this player", false}, [this](const Values&) {
        emit_table({"Field", "Value"},
                   {{"managerName", utf8(draft_.name)},
                    {"club", "club:" + std::to_string(draft_.master_club) + " " + utf8(picker_->club_name(draft_.master_club))},
                    {"startLeague", kStartLeagues[size_t(draft_.start_league)]},
                    {"strength", kStrengths[size_t(draft_.strength)]},
                    {"portrait", std::to_string(draft_.portrait)}});
    });

    // The single choices; SelectTeam sets all five and confirms. Each check happens before
    // anything changes, so a failed SelectTeam leaves the draft as it was.
    auto set_name = [this](ui::ManagerChoice& c, const std::string& utf8) {
        if (utf8.empty()) fail("E_BAD_ARGUMENT", "the name must not be empty");
        std::string original;
        try {
            original = ui::utf8_to_original(utf8);
        } catch (const std::exception&) {
            fail("E_BAD_ARGUMENT", "the name has a character the game cannot show");
        }
        if (original.size() > 20) fail("E_BAD_ARGUMENT", "the name has more than 20 characters");
        c.name = original;
    };
    auto set_club = [this](ui::ManagerChoice& c, const std::string& reference) {
        const int master = resolve_club(reference);
        for (const auto& other : confirmed_)
            if (other.master_club == master)
                fail("E_RULE", "the club is taken by player " + std::to_string(other.slot + 1));
        for (int sel = 0; sel <= picker_->group_last(ui::ManagerScreen::group_count - 1); ++sel)
            if (picker_->master_of(sel) == master) {
                c.selection_club = sel;
                c.master_club = master;
                return;
            }
        fail("E_BAD_ARGUMENT", "the club cannot be chosen");
    };
    auto set_portrait = [this](ui::ManagerChoice& c, int portrait) {
        for (const auto& other : confirmed_)
            if (other.portrait == portrait) fail("E_RULE", "the portrait is taken by player " + std::to_string(other.slot + 1));
        c.portrait = portrait;
    };
    add({"NewGame.Select", "SetManagerName", {detail::text("name", 0)}, "the manager's name (1-20 characters)", true},
        [this, set_name](const Values& v) { set_name(draft_, v.text("name")); emit('+', "name: " + utf8(draft_.name)); });
    Param club = detail::text("club");
    club.display = "club:Club";
    add({"NewGame.Select", "SetClub", {club}, "the club: club:<n> from ListTeams or \"name\"", true},
        [this, set_club](const Values& v) {
            set_club(draft_, v.text("club"));
            emit('+', "club: " + utf8(picker_->club_name(draft_.master_club)));
        });
    add({"NewGame.Select", "SetStartLeague", {choice("league", kStartLeagues)}, "the league to start in", true},
        [this](const Values& v) { draft_.start_league = v.choice("league"); emit('+', "start league: " + kStartLeagues[size_t(draft_.start_league)]); });
    add({"NewGame.Select", "SetStrength", {choice("strength", {"Upper", "Middle", "Lower"})}, "the club's strength", true},
        [this](const Values& v) { draft_.strength = 2 - v.choice("strength"); emit('+', "strength: " + kStrengths[size_t(draft_.strength)]); });
    add({"NewGame.Select", "SetPortrait", {integer("portrait", 0, 11)}, "the manager's portrait", true},
        [this, set_portrait](const Values& v) { set_portrait(draft_, int(v.integer("portrait"))); emit('+', "portrait: " + std::to_string(draft_.portrait)); });
    add({"NewGame.Select", "SelectTeam",
         {detail::text("managerName"), club, choice("startLeague", kStartLeagues), choice("strength", {"Upper", "Middle", "Lower"}),
          integer("portrait", 0, 11)},
         "set all five and ask for confirmation", true},
        [this, set_name, set_club, set_portrait](const Values& v) {
            ui::ManagerChoice c = draft_;
            set_name(c, v.text("managerName"));
            set_club(c, v.text("club"));
            c.start_league = v.choice("startLeague");
            c.strength = 2 - v.choice("strength");
            set_portrait(c, int(v.integer("portrait")));
            draft_ = c;
            confirm_selection();
        });
    add({"NewGame.Select", "Confirm", {}, "show the summary and ask \"all details correct?\"", true},
        [this](const Values&) { confirm_selection(); });
    add({"NewGame.Select.Confirm", "ShowSummary", {}, "the choices of this player", false}, [this](const Values&) {
        emit_table({"Field", "Value"},
                   {{"managerName", utf8(draft_.name)},
                    {"club", utf8(picker_->club_name(draft_.master_club))},
                    {"startLeague", utf8(picker_->group_label(draft_.start_league))},
                    {"strength", text(kStrengthBottomThird + draft_.strength)},
                    {"portrait", std::to_string(draft_.portrait)}});
    });
}

// ---------------------------------------------------------------------------------------
// start flow

std::string Console::options_text() const {
    return "duration=" + (setup_.duration == 5 ? std::string("endless") : std::to_string(setup_.duration)) +
           " difficulty=" + std::to_string(setup_.difficulty + 1) + " managers=" + std::to_string(setup_.managers) +
           " mode=" + std::to_string(setup_.mode) + " league=" + on_off(setup_.league) + " world=" + on_off(setup_.world);
}

void Console::new_game_continue() {
    // The options are taken over with the first manager (CareerSession::confirm_manager).
    set_window("NewGame.Select", 1);
    emit('+', "Options set: " + options_text());
    begin_selection(0);
}

// The manager dialog for `slot`: the original picker model gives the tab ranges and the
// defaults (name "Manager n", the first free club and portrait). The club order comes from
// the engine: confirming a manager can move clubs between leagues (ConfirmManager).
void Console::begin_selection(int slot) {
    picker_ = std::make_unique<ui::ManagerScreen>(options_.data, slot, confirmed_, session_->selection_context(revision_));
    draft_ = picker_->choice();
}

// A club reference (01-grundlagen 1.3): club:<n> / <n> (master index) or a name. Names
// compare without case (also Ä/Ö/Ü); an exact name wins, otherwise a unique part of a name
// is enough ("Bayern").
int Console::resolve_club(const std::string& reference) const {
    std::string number = reference;
    if (lower(number).rfind("club:", 0) == 0) number = number.substr(5);
    Arg n;
    n.text = number;
    const int last = picker_->group_last(ui::ManagerScreen::group_count - 1);
    if (const auto id = to_int(n)) {
        for (int sel = 0; sel <= last; ++sel)
            if (picker_->master_of(sel) == *id) return int(*id);
        fail("E_BAD_ARGUMENT", "no club " + reference + " in the selection (see ListTeams)");
    }
    const std::string wanted = fold(reference);
    std::vector<int> exact, partial;
    for (int sel = 0; sel <= last; ++sel) {
        const int master = picker_->master_of(sel);
        const std::string name = fold(utf8(picker_->club_name(master)));
        if (name == wanted) exact.push_back(master);
        else if (name.find(wanted) != std::string::npos) partial.push_back(master);
    }
    const auto& found = exact.empty() ? partial : exact;
    if (found.size() == 1) return found.front();
    if (found.empty()) fail("E_BAD_ARGUMENT", "no club named \"" + reference + "\" (see ListTeams)");
    std::string candidates;
    for (size_t i = 0; i < found.size() && i < 8; ++i)
        candidates += (i ? ", " : "") + utf8(picker_->club_name(found[i])) + " = club:" + std::to_string(found[i]);
    fail("E_AMBIGUOUS", "\"" + reference + "\" fits several clubs: " + candidates);
}

void Console::confirm_selection() {
    // PresentManagerDialog: the summary and "Sind alle Angaben richtig?"; No = back to the same
    // player's selection, everything can be changed (H2 S. 15).
    open_window("NewGame.Select.Confirm");
    emit_table({"Field", "Value"},
               {{"managerName", utf8(draft_.name)},
                {"club", utf8(picker_->club_name(draft_.master_club))},
                {"startLeague", utf8(picker_->group_label(draft_.start_league))},
                {"strength", text(kStrengthBottomThird + draft_.strength)},
                {"portrait", std::to_string(draft_.portrait)}});
    ask(text(kManagerConfirmAllDetailsCorrect), {"Yes", "No"}, "No", [this](const std::string& answer) {
        close_window();
        if (answer != "Yes") {
            emit('+', "selection not confirmed");
            return;
        }
        std::string error;
        if (!session_->confirm_manager(setup_, draft_, error))
            fail("E_RULE", "the engine rejects the manager: " + error);
        confirmed_.push_back(draft_);
        emit('+', "Manager " + std::to_string(confirmed_.size()) + " confirmed: " + utf8(draft_.name) + ", " +
                      utf8(picker_->club_name(draft_.master_club)));
        if (int(confirmed_.size()) < setup_.managers) {
            set_window("NewGame.Select", int(confirmed_.size()) + 1);
            begin_selection(int(confirmed_.size()));
            return;
        }
        finish_career();
    });
}

// After the last manager: the start values, the first season up to the first desk
// (CareerSession::finish), and the info windows of the intermediate sequence (03-start 4).
void Console::finish_career() {
    ui::CareerInitializationResult init;
    std::string error;
    if (!session_->finish(init, error)) fail("E_INTERNAL", "the career cannot start: " + error);
    career_ = true;
    revision_ = init.desktop.revision;
    picker_.reset();
    // the entrants and cup draws of the intermediate sequence came through the hooks
    flush_info();
    enter_desktop();
}

// Installed on the session: the cup draws the engine makes inside finish() and kickoffs
// (CupDrawFromGame / Getenv). They are projected while the hook runs - the state moves
// on afterwards - and printed when the engine call returns (Info.CupDraw, 02-fenster 2).
// The projection is the SDL app's; it reads the state and draws no random numbers.
void Console::install_hooks() {
    // SeasonEntrantsFromGame / CompetitionWinnerScreen: the German entrants of the European cups, at the start of
    // a career (03-start 4) and at every season change. The engine pauses at this checkpoint
    // inside finish() / a kickoff; the pages are projected there (Info.EuropeanEntrants).
    session_->set_season_pause([this](const char* checkpoint, const GameState& state) {
        if (std::string_view(checkpoint) != "the German entrants (CompetitionWinnerScreen)") return;
        std::vector<std::string> lines;
        for (const auto& page : ui::season_entrants_from_game(state, exe_, texts_)) {
            std::string names;
            for (const auto& t : page.teams) names += (names.empty() ? "" : ", ") + t.name;
            lines.push_back(page.footer + ": " + names);
        }
        if (lines.empty()) return;
        pending_info_.push_back([this, lines] {
            set_window("Info.EuropeanEntrants", 0);
            for (const auto& l : lines) emit('i', l);
        });
    });
    session_->set_season_cup_draw([this](const GameState& state, const SeasonCupDrawEvent& event) {
        const ui::CupDrawProjection view = ui::cup_draw_from_game(state, event, exe_, texts_);
        if (!view.source_eligible) return;
        const std::string title = utf8(view.footer) + ", " + utf8(view.heading);
        if (event.phase == SeasonCupDrawPhase::draw && !view.group_stage) {
            std::vector<std::vector<std::string>> rows;
            for (size_t i = 0; i + 1 < view.clubs.size(); i += 2)
                rows.push_back({utf8(view.clubs[i].original_name), utf8(view.clubs[i + 1].original_name)});
            if (view.clubs.size() % 2) rows.push_back({utf8(view.clubs.back().original_name), "-"});
            pending_info_.push_back([this, title, rows] {
                set_window("Info.CupDraw", 0);
                emit('i', title + ":");
                emit_table({"Home", "Away"}, rows);
            });
        } else if (event.phase == SeasonCupDrawPhase::overview && view.group_stage) {
            std::vector<std::vector<std::string>> rows;
            for (const auto& r : view.group_rows)
                rows.push_back({std::to_string(r.rank), utf8(state.club_name(r.runtime_id))});
            const std::string caption = title + (view.group_caption.empty() ? "" : " " + utf8(view.group_caption));
            pending_info_.push_back([this, caption, rows] {
                set_window("Info.CupDraw", 0);
                emit('i', caption + ":");
                emit_table({"Rank", "Club"}, rows);
            });
        }
    });
}

void Console::flush_info() {
    auto items = std::move(pending_info_);
    pending_info_.clear();
    for (auto& f : items) f();
}

// ---------------------------------------------------------------------------------------
// file windows (03-start 2, 04-schreibtisch 8)

std::filesystem::path Console::save_directory() const {
    return options_.save_directory.empty() ? options_.data : options_.save_directory;
}

// The browser's directory relative to the save directory ("." at the start), so logs and
// transcripts do not depend on where the installation lies.
std::string Console::shown_directory() const {
    std::error_code ec;
    const std::filesystem::path rel = std::filesystem::relative(browse_directory_, save_directory(), ec);
    return ec || rel.empty() ? browse_directory_.generic_string() : rel.generic_string();
}

// A file name of the browser is a plain DOS name in the current directory: separators, drive
// colons and "." / ".." would write or read outside it (directories change only through
// ChangeDirectory / ParentDirectory, as in the original browser).
std::filesystem::path Console::browser_file(const std::string& name) const {
    if (name == "." || name == ".." || name.find_first_of("/\\:") != std::string::npos)
        fail("E_BAD_ARGUMENT", "not a file name: " + name + " (no directories; use ChangeDirectory)");
    std::filesystem::path path = browse_directory_ / name;
    if (!path.has_extension()) path += ".FBM";
    return path;
}

void Console::register_file_commands() {
    // The original browser shows subdirectories and the *.FBM files of the current directory;
    // a click selects a file (FILENAME field), a second click loads it. The same commands
    // serve the load browser of the options screen and the desk's LoadGame/SaveGame.
    for (const std::string window : {"NewGame.LoadGame", "LoadGame", "SaveGame"}) {
        add({window, "ListFiles", {}, "directories and saved games here", false}, [this](const Values&) {
            std::vector<std::vector<std::string>> dirs, files;
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(browse_directory_, ec)) {
                if (entry.is_directory(ec)) dirs.push_back({entry.path().filename().string(), "dir"});
                else if (entry.is_regular_file(ec) && lower(entry.path().extension().string()) == ".fbm")
                    files.push_back({entry.path().stem().string(), "file"});
            }
            if (ec) fail("E_IO", "cannot read the directory " + shown_directory());
            std::sort(dirs.begin(), dirs.end());
            std::sort(files.begin(), files.end());
            dirs.insert(dirs.end(), files.begin(), files.end());
            emit('i', "directory: " + shown_directory());
            emit_table({"Name", "Type"}, dirs);
        });
        add({window, "ChangeDirectory", {detail::text("name")}, "into a subdirectory", false}, [this](const Values& v) {
            const std::filesystem::path next = browse_directory_ / v.text("name");
            std::error_code ec;
            if (!std::filesystem::is_directory(next, ec)) fail("E_BAD_ARGUMENT", "no directory " + v.text("name"));
            browse_directory_ = next.lexically_normal();
            emit('+', "directory: " + shown_directory());
        });
        add({window, "ParentDirectory", {}, "one level up", false}, [this](const Values&) {
            browse_directory_ = browse_directory_.parent_path();
            emit('+', "directory: " + shown_directory());
        });
        add({window, "SelectFile", {detail::text("name", 8)}, "select a file (FILENAME field, DOS name)", false},
            [this](const Values& v) {
                selected_file_ = v.text("name");
                emit('+', "selected: " + selected_file_);
            });
        add({window, "FileInfo", {detail::text("name")}, "short info of a saved game", false}, [](const Values&) {
            fail("E_ENGINE_GAP", "the short info of saved games is not ported yet");
        });
        if (window != "SaveGame") {
            add({window, "Load", {detail::text("name", 0, true)}, "load the selected or named file", true}, [this](const Values& v) {
                const std::string name = v.has("name") ? v.text("name") : selected_file_;
                if (name.empty()) fail("E_BAD_ARGUMENT", "no file selected (SelectFile or Load(name))");
                load_game(browser_file(name));
            });
        } else {
            add({window, "Save", {detail::text("name", 8, true), boolean("overwrite", true)},
                 "save under the selected or given name", true},
                [this](const Values& v) {
                    const std::string name = v.has("name") ? v.text("name") : selected_file_;
                    // "Leerer Name = nichts gespeichert" (04-schreibtisch 8)
                    if (name.empty()) {
                        emit('+', "nothing saved (no name)");
                        return;
                    }
                    save_game(browser_file(name), v.boolean("overwrite"));
                });
        }
    }
}

void Console::load_game(const std::filesystem::path& requested) {
    std::filesystem::path path = requested;
    if (!path.has_extension()) path += ".FBM";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) fail("E_IO", "file not found: " + path.filename().string());
    std::unique_ptr<app::CareerSession> loaded;
    try {
        loaded = std::make_unique<app::CareerSession>(options_.data, options_.seed, path);
    } catch (const std::exception&) {
        fail("E_RULE", text(kErrorNotAGameScore), "errorNotAGameScore");
    }
    session_ = std::move(loaded);
    install_hooks();
    career_ = true;
    revision_ = 1;
    confirmed_.clear();
    picker_.reset();
    seen_mail_.clear();
    pairings_.reset();
    table_league_ = -1;
    const ui::DeskState d = session_->desktop_projection(revision_);
    // The messages of a loaded game are not new: count them instead of repeating them all.
    size_t messages = 0;
    for (size_t slot = 0; slot < session_->state().manager_teams.size(); ++slot)
        for (const auto& m : ui::mailbox_state_from_game(session_->state(), int(slot), revision_).messages) {
            seen_mail_[int(slot)].insert(std::to_string(m.date_counter) + "|" + m.text);
            if (int(slot) == d.manager_slot) ++messages;
        }
    set_window("Desktop", d.manager_slot + 1);
    emit('+', "Game loaded: " + path.stem().string() + " (" + utf8(d.club_name) + ", " +
                  date_text(d.date.day, d.date.month, d.date.year) + ", " + std::to_string(messages) + " messages in the mailbox)");
    enter_desktop();
}

void Console::save_game(const std::filesystem::path& path, bool overwrite) {
    std::error_code ec;
    auto write = [this, path](bool allow_overwrite) {
        std::string error;
        if (!session_->save_game(path, active_slot(), allow_overwrite, error)) fail("E_IO", "not saved: " + error);
        emit('+', "Game saved: " + path.stem().string());
    };
    if (std::filesystem::exists(path, ec) && !overwrite) {
        ask(text(kSaveConfirmOverwrite), {"Yes", "No"}, "No",
            [this, write](const std::string& answer) {
                if (answer == "Yes") write(true);
                else emit('+', "nothing saved");
            },
            "saveConfirmOverwrite");
        return;
    }
    write(overwrite);
}

}  // namespace bmh::cli::console
