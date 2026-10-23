// SPDX-License-Identifier: GPL-3.0-only
#include "editor/options.hpp"
#include <cstdlib>
#include <map>
#include <random>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif
namespace bmh::editor {
Options Options::load(fs::path override_path) {
    Options result;
    if (override_path.empty()) {
#ifdef _WIN32
        // The narrow CRT environment is encoded in the active Windows code
        // page, not UTF-8. Use the native path for names such as Müller.
        wchar_t base[32768]{};
        const auto count = GetEnvironmentVariableW(L"APPDATA", base, 32768);
        override_path =
            (count > 0 && count < 32768 ? fs::path(base) : fs::temp_directory_path()) / "OpenBMH" / "editor.json";
#elif defined(__APPLE__)
        auto base = std::getenv("HOME");
        override_path =
            (base ? path(base) : fs::temp_directory_path()) / "Library/Application Support/OpenBMH/editor.json";
#else
        if (auto base = std::getenv("XDG_CONFIG_HOME"))
            override_path = path(base) / "openbmh/editor.json";
        else {
            auto user = std::getenv("HOME");
            override_path = (user ? path(user) : fs::temp_directory_path()) / ".config/openbmh/editor.json";
        }
#endif
    }
    result.settings_file = std::move(override_path);
    if (fs::exists(result.settings_file)) {
        try {
            auto json = Json::parse(read_file(result.settings_file, 1024 * 1024));
            result.apply(json);
            result.token = str(json, "mcp_token");
        } catch (const std::exception &) {
            throw Error("invalid_settings", {{"path", utf8(result.settings_file)}});
        }
    }
    if (result.token.size() < 32) {
        // random_device uses the OS entropy source on supported release targets;
        // identifiers from the editor's PRNG are deliberately not credentials.
        std::random_device entropy;
        static constexpr char hex[] = "0123456789abcdef";
        result.token.clear();
        for (int i = 0; i < 8; ++i) {
            auto number = entropy();
            for (int j = 0; j < 8; ++j) {
                result.token += hex[number & 15];
                number >>= 4;
            }
        }
    }
    result.save();
    return result;
}
Json Options::json(bool secret) const {
    Json value{{"language", language}, {"theme", theme}, {"mcp_enabled", mcp_enabled}, {"mcp_port", mcp_port}};
    if (secret)
        value["mcp_token"] = token;
    return value;
}
void Options::apply(const Json &value) {
    auto language_value = str(value, "language", language), theme_value = str(value, "theme", theme);
    auto port = value.value("mcp_port", mcp_port);
    if ((language_value != "de" && language_value != "en") ||
        (theme_value != "midnight" && theme_value != "silver" && theme_value != "matchday") || port < 0 || port > 65535)
        throw Error("invalid_settings");
    language = language_value;
    theme = theme_value;
    mcp_port = port;
    mcp_enabled = value.value("mcp_enabled", mcp_enabled);
}
void Options::save() const {
    if (settings_file.has_parent_path())
        fs::create_directories(settings_file.parent_path());
    write_atomic(settings_file, json(true).dump(2));
#ifndef _WIN32
    chmod(settings_file.c_str(), 0600);
#endif
}
std::string tr(const std::string &language, const std::string &key) {
    using Pair = std::pair<std::string, std::string>;
    static const std::map<std::string, Pair> words{
        {"data_areas", {"Datenbereiche", "World data"}},
        {"drill_nations", {"Doppelklick: Ligen anzeigen", "Double-click to view leagues"}},
        {"drill_competitions",
         {"Doppelklick auf eine Liga: Vereine anzeigen", "Double-click a league to view clubs"}},
        {"drill_clubs", {"Doppelklick: Spieler anzeigen", "Double-click to view players"}},
        {"regulations", {"Regelwerk", "Regulations"}},
        {"regulations_title", {"Spiel- & Ligaregeln", "Game & league rules"}},
        {"subtitle_regulations",
         {"Aufstieg, Abstieg und internationale Qualifikation",
          "Promotion, relegation and international qualification"}},
        {"regulation_readonly", {"Nur Anzeige", "Read only"}},
        {"regulation_competitions", {"Ligen & Wettbewerbe", "Leagues & competitions"}},
        {"regulation_game", {"Spielregeln", "Game rules"}},
        {"regulation_movement_title", {"Auf- & Abstieg", "Promotion & relegation"}},
        {"regulation_qualification_title",
         {"Internationale Qualifikation & Teilnehmer", "International qualification & entrants"}},
        {"regulation_promotion", {"Aufstieg", "Promotion"}},
        {"regulation_relegation", {"Abstieg", "Relegation"}},
        {"regulation_playoff", {"Relegationsspiele", "Playoffs"}},
        {"regulation_playoff_winner", {"Sieger der Classic-Relegation", "Classic playoff winner"}},
        {"regulation_classic_playoffs",
         {"Bei parallelen unteren Ligen steigen die Meister direkt auf. Weitere Aufsteiger bestimmt die "
          "Classic-Relegation. Ohne deren Ergebnis werden nur die Meister ausgetauscht.",
          "For parallel lower leagues, champions go up directly. Additional promotion follows the Classic "
          "playoffs. Without a playoff result, only champions are exchanged."}},
        {"regulation_movement", {"Ligenwechsel", "League movement"}},
        {"regulation_rank", {"Platz", "Rank"}},
        {"regulation_to", {"in", "to"}},
        {"regulation_closed", {"Kein regulärer Auf- oder Abstieg.", "No regular promotion or relegation."}},
        {"regulation_unspecified", {"Nicht im OBW festgelegt.", "Not specified in the OBW."}},
        {"regulation_amateur_pool",
         {"Nachrückerpool der untersten Spielklasse", "Lowest-tier newcomer pool"}},
        {"regulation_declared", {"Im OBW festgelegt", "Declared in the OBW"}},
        {"regulation_classic", {"Classic-Verfahrensregel", "Classic adapter policy"}},
        {"regulation_classic_movement_note",
         {"Reguläre Plätze. Lizenzentzüge können die Saisonentscheidung ändern. Die Zuordnung zu parallelen "
          "Ligen erfolgt beim Saisonwechsel.",
          "Regular places. License withdrawals can change the season outcome. Allocation to parallel leagues "
          "occurs at season rollover."}},
        {"regulation_classic_intro",
         {"Übernommenes Classic-Verfahren, keine heutigen UEFA-Regeln.",
          "Inherited Classic policy; current UEFA regulations do not apply."}},
        {"regulation_classic_champion",
         {"Meister qualifiziert für die Champions League. Titelverteidiger werden zusätzlich berücksichtigt; "
          "ist dieser bereits Meister, rückt der Zweite nach.",
          "Champion qualifies for the Champions League. The holder also qualifies; if already champion, the "
          "runner-up receives the extra place."}},
        {"regulation_classic_cup",
         {"Nationaler Pokalsieger qualifiziert für den Pokalsieger-Wettbewerb, bei bestehender Qualifikation "
          "der Finalist. Ausländische Pokalvertreter werden im Classic-Verfahren bestimmt.",
          "Domestic cup winner qualifies for the Cup Winners’ Cup; the finalist replaces an already "
          "qualified winner. Foreign cup entrants follow the Classic selection policy."}},
        {"regulation_classic_uefa",
         {"reguläre Plätze nach Tabellenrang; bereits qualifizierte Teams werden übersprungen.",
          "regular places by league rank; already qualified teams are skipped."}},
        {"regulation_classic_holders",
         {"Titelverteidiger von UEFA- und Pokalsieger-Cup sowie Doppelqualifikationen können zusätzliche "
          "Plätze und Nachrücker auslösen.",
          "UEFA/Cup Winners’ Cup holders and overlapping qualifications may produce extra places or "
          "replacements."}},
        {"regulation_runtime",
         {"Teilnehmer werden durch das hinterlegte Classic-Verfahren bestimmt.",
          "Entrants are resolved by the stored Classic policy."}},
        {"regulation_winner", {"Sieger", "Winner"}},
        {"regulation_runner_up", {"Finalist / Zweiter", "Runner-up"}},
        {"regulation_next_unqualified", {"Nächstes noch nicht qualifiziertes Team", "Next unqualified team"}},
        {"regulation_holder", {"Titelverteidiger", "Title holder"}},
        {"regulation_coefficient",
         {"Qualifikation nach Koeffizientenrang", "Qualification by coefficient rank"}},
        {"regulation_fallback", {"Nachrücken", "Replacement"}},
        {"regulation_stage", {"Phase", "Stage"}},
        {"regulation_stages", {"Wettbewerbsphasen & Spielregeln", "Competition stages & match rules"}},
        {"regulation_show_source", {"OBW-Details anzeigen", "Show OBW details"}},
        {"regulation_hide_source", {"OBW-Details ausblenden", "Hide OBW details"}},
        {"regulation_preserved",
         {"Unveränderte Angaben aus world/rules.json. Diese Ansicht ist schreibgeschützt.",
          "Unmodified values from world/rules.json. This view is read only."}},
        {"regulation_type_league", {"Liga", "League"}},
        {"regulation_type_cup", {"Pokal", "Cup"}},
        {"regulation_type_national_team", {"Nationalmannschaften", "National teams"}},
        {"regulation_type_club_international",
         {"Internationaler Vereinswettbewerb", "International club competition"}},
        {"regulation_type_friendly", {"Freundschaftsspiel", "Friendly"}},
        {"regulation_type_indoor", {"Hallenturnier", "Indoor tournament"}},
        {"rule_type_attribute_set", {"Spielwerte & Wertebereiche", "Attributes & ranges"}},
        {"rule_type_fixture_tables", {"Spielplanverfahren", "Fixture generation"}},
        {"rule_type_legacy_ladder", {"Classic-Ligenpyramide", "Classic league pyramid"}},
        {"rule_type_training_camps", {"Trainingslager", "Training camps"}},
        {"rule_type_credit_rates", {"Kreditregeln", "Credit rules"}},
        {"rule_type_classic_runtime", {"Classic-Verfahren", "Classic runtime policies"}},
        {"rule_type_classic_data_regions", {"Übernommene Classic-Regeldaten", "Preserved Classic rule data"}},
        {"comparison", {"Wertevergleich", "Comparison"}},
        {"comparison_title", {"Stärken & Gehälter", "Ratings & salaries"}},
        {"subtitle_comparison",
         {"Spieler, Mannschaften und Ligen im Vergleich", "Compare players, teams and leagues"}},
        {"teams", {"Mannschaften", "Teams"}},
        {"leagues", {"Ligen", "Leagues"}},
        {"physical_strength", {"Körperkraft", "Physical strength"}},
        {"comparison_columns", {"Spalten auswählen", "Choose columns"}},
        {"comparison_columns_hint",
         {"Spielwerte einblenden. Weitere Spalten sind horizontal erreichbar.",
          "Select attributes. Scroll horizontally to see further columns."}},
        {"comparison_all_sets", {"Alle Spieler · Systeme getrennt", "All players · separate systems"}},
        {"comparison_all_players", {"alle Spieler", "all players"}},
        {"all_columns", {"Alle auswählen", "Select all"}},
        {"no_columns", {"Keine auswählen", "Select none"}},
        {"comparison_player_values", {"Originalskala des Datensatzes", "Dataset's original scale"}},
        {"comparison_mean_values", {"Spielwerte: Durchschnitt je Spieler", "Attributes: mean per player"}},
        {"comparison_known_values", {"bekannte Werte", "known values"}},
        {"salary_season", {"Gehalt / Saison", "Salary / season"}},
        {"salary_total", {"Gehaltssumme / Saison", "Total salary / season"}},
        {"salary_average", {"Ø Gehalt / Spieler", "Mean salary / player"}},
        {"salary_coverage", {"Gehälter bekannt", "Salaries known"}},
        {"salary_missing", {"Gehälter unbekannt", "salaries unknown"}},
        {"salary_ambiguous", {"mehrere aktive Verträge", "multiple active contracts"}},
        {"comparison_salary_note",
         {"Gehälter je Saison in Weltwährung. * Nur bekannte Gehälter; Summe/Ø unvollständig. Stichtag",
          "Salaries per season in world currency. * Known salaries only; total/mean incomplete. As of"}},
        {"comparison_scope_note",
         {"Gesamter Kader inklusive Reserven. Fehlende Spielwerte bleiben leer; Ø nur aus bekannten Werten.",
          "Full squads including reserves. Missing attributes stay blank; means use known values only."}},
        {"comparison_mixed_note",
         {"Für Stärken ein Attributsystem auswählen. Unterschiedliche Systeme werden nicht vermischt.",
          "Choose an attribute set to compare ratings. Different systems are not mixed."}},
        {"comparison_excluded", {"Quellenduplikate nicht eingerechnet", "source duplicates excluded"}},
        {"comparison_fictional",
         {"Der Datensatz enthält als fiktiv oder geschätzt gekennzeichnete Ergänzungen.",
          "The dataset includes additions marked as fictional or estimated."}},
        {"comparison_sort_hint", {"Spaltenkopf anklicken zum Sortieren", "Click a column heading to sort"}},
        {"invalid_attribute_set",
         {"Dieses Attributsystem ist nicht im Datenbestand.",
          "This attribute set is not present in the dataset."}},
        {"invalid_comparison_sort",
         {"Diese Vergleichsspalte ist nicht verfügbar.", "This comparison column is unavailable."}},
        {"invalid_manifest",
         {"Die Kennzeichnung der Editorwelt ist ungültig.", "The editor world identification is invalid."}},
        {"facilities", {"Ausstattung", "Facilities"}},
        {"draft_prompt", {"Eingaben übernehmen?", "Apply these entries?"}},
        {"draft_detail",
         {"Die Eigenschaften enthalten noch nicht übernommene Eingaben.",
          "The inspector contains entries that have not been applied."}},
        {"trophy", {"Pokalbild", "Trophy image"}},
        {"author", {"Urheber", "Author"}},
        {"license", {"Lizenz", "License"}},
        {"copy", {"Kopie", "Copy"}},
        {"applied", {"Übernommen", "Applied"}},
        {"undone", {"Zurückgenommen", "Undone"}},
        {"edit", {"Bearbeitung", "Edit"}},
        {"image_import", {"Bild importieren", "Import image"}},
        {"width", {"Breite", "Width"}},
        {"height", {"Höhe", "Height"}},
        {"x", {"Links", "Left"}},
        {"y", {"Oben", "Top"}},
        {"open_folder", {"Entpackte Welt öffnen", "Open unpacked world"}},
        {"subtitle_overview",
         {"Ein Arbeitsplatz für deine Fußballwelt", "A workspace for your football world"}},
        {"subtitle_nations", {"Länder, Verbände und Simulation", "Countries, associations and simulation"}},
        {"subtitle_competitions",
         {"Ligen, Zuordnungen und Pokalbilder", "Leagues, membership and trophy images"}},
        {"subtitle_clubs", {"Stammdaten, Farben und Spielstätten", "Details, colours and home grounds"}},
        {"subtitle_players",
         {"Spielerdaten und quellengerechte Spielwerte", "Player details and source-specific attributes"}},
        {"subtitle_stadiums", {"Kapazität und Ausstattung", "Capacity and facilities"}},
        {"subtitle_staff", {"Schiedsrichter und ihre Eigenschaften", "Referees and their attributes"}},
        {"subtitle_assets", {"Wappen, Porträts und Pokale", "Crests, portraits and trophies"}},
        {"subtitle_review", {"Quellen vergleichen", "Compare sources"}},
        {"subtitle_roster", {"Vollständige Kader ohne Datenverlust", "Complete squads without losing data"}},
        {"subtitle_changes",
         {"Gemeinsame Historie von Oberfläche und MCP", "One history for the interface and MCP"}},
        {"preservation_note",
         {"Quellen, Bilder und Erweiterungen bleiben erhalten. Bearbeitungen "
          "ändern keine Originalinstallation.",
          "Sources, images and extensions are retained. Edits do not change the "
          "original installation."}},
        {"unrated_hint",
         {"Keine belegten Spielwerte vorhanden. Es werden keine Stärken "
          "erfunden.",
          "No sourced attributes available. Ratings are not invented."}},
        {"crop_hint",
         {"Im Bild einen Rahmen ziehen oder den Ausschnitt als Werte eingeben. "
          "Ohne Zuschnitt bleibt die Originalauflösung erhalten.",
          "Drag a rectangle on the image or enter crop values. Without cropping, "
          "the original resolution is retained."}},
        {"video_failed",
         {"Die Grafikausgabe konnte nicht initialisiert werden.", "Graphics initialization failed."}},
        {"ui_assets_missing",
         {"Editor-Oberfläche fehlt. Bitte das vollständige Paket entpacken.",
          "Editor interface files are missing. Please extract the complete "
          "package."}},
        {"dialog_failed",
         {"Der Dateidialog konnte nicht geöffnet werden.", "The file dialog could not be opened."}},
        {"invalid_attributes", {"Ungültige Spielwerte.", "Invalid player attributes."}},
        {"app", {"OpenBMH Editor", "OpenBMH Editor"}},
        {"editor_tagline", {"OBW-Dateneditor", "OBW world editor"}},
        {"overview", {"Übersicht", "Overview"}},
        {"nations", {"Länder", "Countries"}},
        {"competitions", {"Ligen & Pokale", "Leagues & cups"}},
        {"clubs", {"Vereine", "Clubs"}},
        {"players", {"Spieler", "Players"}},
        {"roster", {"Kader", "Squads"}},
        {"stadiums", {"Stadien", "Stadiums"}},
        {"staff", {"Schiedsrichter", "Referees"}},
        {"assets", {"Bilder", "Images"}},
        {"review", {"Prüfen", "Review"}},
        {"changes", {"Änderungen", "Changes"}},
        {"options", {"Optionen", "Options"}},
        {"open", {"Öffnen", "Open"}},
        {"open_world", {"OBW öffnen", "Open OBW"}},
        {"import_legacy", {"BMH / FLTD importieren", "Import BMH / FLTD"}},
        {"new_world", {"Neue Welt", "New world"}},
        {"demo", {"Demo öffnen", "Open demo"}},
        {"save", {"Speichern", "Save"}},
        {"save_as", {"Speichern unter …", "Save as …"}},
        {"undo", {"Rückgängig", "Undo"}},
        {"redo", {"Wiederholen", "Redo"}},
        {"search", {"Suchen …", "Search …"}},
        {"all", {"Alle", "All"}},
        {"all_nations", {"Alle Länder", "All countries"}},
        {"all_leagues", {"Alle Ligen", "All leagues"}},
        {"all_clubs", {"Alle Vereine", "All clubs"}},
        {"add", {"Hinzufügen", "Add"}},
        {"duplicate", {"Duplizieren", "Duplicate"}},
        {"delete", {"Löschen", "Delete"}},
        {"apply", {"Anwenden", "Apply"}},
        {"cancel", {"Abbrechen", "Cancel"}},
        {"close", {"Schließen", "Close"}},
        {"confirm", {"Bestätigen", "Confirm"}},
        {"discard", {"Verwerfen", "Discard"}},
        {"unknown", {"Unbekannt", "Unknown"}},
        {"empty", {"Keine Einträge", "No records"}},
        {"select_record", {"Datensatz auswählen", "Select a record"}},
        {"name", {"Name", "Name"}},
        {"short_name", {"Kurzname", "Short name"}},
        {"first", {"Vorname", "First name"}},
        {"last", {"Nachname", "Last name"}},
        {"known_as", {"Anzeigename", "Known as"}},
        {"nation", {"Land", "Country"}},
        {"nationalities", {"Nationalitätsangaben", "Nationality claims"}},
        {"competition", {"Liga", "League"}},
        {"team", {"Verein", "Club"}},
        {"stadium", {"Stadion", "Stadium"}},
        {"born", {"Geburtsdatum", "Date of birth"}},
        {"age_at_start", {"Alter zum Saisonbeginn", "Age at season start"}},
        {"positions", {"Position", "Position"}},
        {"gk", {"Tor", "Goalkeeper"}},
        {"def", {"Abwehr", "Defender"}},
        {"mid", {"Mittelfeld", "Midfielder"}},
        {"fwd", {"Sturm", "Forward"}},
        {"squad_number", {"Rückennummer", "Shirt number"}},
        {"squad_order", {"Kaderplatz", "Squad order"}},
        {"attribute_set", {"Attributsystem", "Attribute set"}},
        {"attributes", {"Spielwerte", "Attributes"}},
        {"type", {"Art", "Type"}},
        {"tier", {"Spielklasse", "Tier"}},
        {"visible", {"Sichtbare Liga", "Visible league"}},
        {"playable", {"Ligen vollständig simulieren", "Fully simulated leagues"}},
        {"confederation", {"Verband", "Confederation"}},
        {"founded", {"Gründungsjahr", "Founded"}},
        {"capacity", {"Kapazität", "Capacity"}},
        {"total", {"Gesamt", "Total"}},
        {"standing", {"Stehplätze", "Standing"}},
        {"seated", {"Sitzplätze", "Seated"}},
        {"boxes", {"Logen", "Boxes"}},
        {"owner", {"Eigentümer", "Owner"}},
        {"colors", {"Vereinsfarben", "Club colours"}},
        {"role", {"Rolle", "Role"}},
        {"strictness", {"Strenge", "Strictness"}},
        {"strength", {"Stärke", "Strength"}},
        {"technique", {"Technik", "Technique"}},
        {"condition", {"Kondition", "Condition"}},
        {"form", {"Form", "Form"}},
        {"overall", {"Gesamtstärke", "Overall"}},
        {"tacticalawareness", {"Taktisches Verständnis", "Tactical awareness"}},
        {"potential", {"Potenzial", "Potential"}},
        {"acceleration", {"Antritt", "Acceleration"}},
        {"sprintspeed", {"Sprinttempo", "Sprint speed"}},
        {"positioning", {"Offensivstellung", "Attacking position"}},
        {"finishing", {"Abschluss", "Finishing"}},
        {"shotpower", {"Schusskraft", "Shot power"}},
        {"longshots", {"Fernschüsse", "Long shots"}},
        {"volleys", {"Volleys", "Volleys"}},
        {"penalties", {"Elfmeter", "Penalties"}},
        {"crossing", {"Flanken", "Crossing"}},
        {"shortpassing", {"Kurzpass", "Short passing"}},
        {"longpassing", {"Langpass", "Long passing"}},
        {"curve", {"Effet", "Curve"}},
        {"freekickaccuracy", {"Freistöße", "Free kicks"}},
        {"vision", {"Übersicht", "Vision"}},
        {"dribbling", {"Dribbling", "Dribbling"}},
        {"ballcontrol", {"Ballkontrolle", "Ball control"}},
        {"agility", {"Beweglichkeit", "Agility"}},
        {"balance", {"Balance", "Balance"}},
        {"reactions", {"Reaktion", "Reactions"}},
        {"composure", {"Ruhe am Ball", "Composure"}},
        {"interceptions", {"Abfangen", "Interceptions"}},
        {"headingaccuracy", {"Kopfball", "Heading"}},
        {"standingtackle", {"Zweikampf", "Standing tackle"}},
        {"slidingtackle", {"Grätsche", "Sliding tackle"}},
        {"defensiveawareness", {"Defensivstellung", "Defensive awareness"}},
        {"jumping", {"Sprungkraft", "Jumping"}},
        {"stamina", {"Ausdauer", "Stamina"}},
        {"aggression", {"Aggressivität", "Aggression"}},
        {"gkdiving", {"TW-Hechten", "GK diving"}},
        {"gkhandling", {"TW-Fangen", "GK handling"}},
        {"gkkicking", {"TW-Abschlag", "GK kicking"}},
        {"gkpositioning", {"TW-Stellung", "GK positioning"}},
        {"gkreflexes", {"TW-Reflexe", "GK reflexes"}},
        {"bonus", {"Bonus", "Bonus"}},
        {"teamplay", {"Teamspiel", "Team play"}},
        {"speed", {"Schnelligkeit", "Speed"}},
        {"shooting", {"Schuss", "Shooting"}},
        {"passing", {"Pass", "Passing"}},
        {"tackling", {"Zweikampf", "Tackling"}},
        {"free_kick", {"Freistoß", "Free kick"}},
        {"corner", {"Ecke", "Corner"}},
        {"yes", {"Ja", "Yes"}},
        {"no", {"Nein", "No"}},
        {"league", {"Liga", "League"}},
        {"cup", {"Pokal", "Cup"}},
        {"national", {"Nationalmannschaft", "National team"}},
        {"club", {"Verein", "Club"}},
        {"foreign", {"Auslandsverein", "Foreign club"}},
        {"amateur", {"Amateur", "Amateur"}},
        {"image_import", {"Bild importieren", "Import image"}},
        {"image_edit", {"Bild bearbeiten", "Edit image"}},
        {"image_export", {"PNG exportieren", "Export PNG"}},
        {"angle", {"Drehwinkel", "Rotation"}},
        {"flip_x", {"Horizontal spiegeln", "Flip horizontally"}},
        {"flip_y", {"Vertikal spiegeln", "Flip vertically"}},
        {"crop", {"Zuschnitt", "Crop"}},
        {"free", {"Frei", "Free"}},
        {"reset", {"Zurücksetzen", "Reset"}},
        {"source", {"Quelle", "Source"}},
        {"original", {"Original", "Original"}},
        {"preview", {"Vorschau", "Preview"}},
        {"language", {"Sprache", "Language"}},
        {"theme", {"Darstellung", "Theme"}},
        {"mcp_enabled", {"MCP-Server aktivieren", "Enable MCP server"}},
        {"mcp_port", {"Port", "Port"}},
        {"copy_connection", {"Verbindung kopieren", "Copy connection"}},
        {"mcp_local", {"Lokal auf diesem Rechner", "Local to this computer"}},
        {"online", {"Aktiv", "Online"}},
        {"offline", {"Aus", "Offline"}},
        {"saved", {"Gespeichert", "Saved"}},
        {"unsaved", {"Ungespeichert", "Unsaved"}},
        {"not_playable", {"Editorwelt · noch nicht im Spiel startbar", "Editor world · not yet playable"}},
        {"workspace", {"Deine Fußballwelt", "Your football world"}},
        {"welcome",
         {"Stammdaten bearbeiten. Quellen erhalten. Neues gestalten.",
          "Edit the details. Keep the sources. Build something new."}},
        {"welcome_hint",
         {"Öffne eine OBW-Welt, importiere BMH oder Football Limited oder "
          "probiere die Demo aus.",
          "Open an OBW world, import BMH or Football Limited, or explore the "
          "demo."}},
        {"records", {"Einträge", "records"}},
        {"previous", {"Zurück", "Previous"}},
        {"next", {"Weiter", "Next"}},
        {"details", {"Eigenschaften", "Properties"}},
        {"provenance", {"Herkunft & Hinweise", "Sources & notes"}},
        {"resolve", {"Auswahl übernehmen", "Use selected value"}},
        {"resolved", {"Erledigt", "Resolved"}},
        {"unresolved", {"Offen", "Open"}},
        {"review_hint",
         {"Quellen vergleichen und eine Angabe auswählen.", "Compare sources and choose a value."}},
        {"roster_hint",
         {"Verein wählen, Spieler verschieben oder zwei Spieler tauschen.",
          "Choose a club, move a player or swap two players."}},
        {"move_player", {"Spieler verschieben", "Move player"}},
        {"remember_player", {"Zum Tausch merken", "Remember for swap"}},
        {"swap_player", {"Mit gemerktem Spieler tauschen", "Swap with remembered player"}},
        {"compatibility24", {"24-Spieler-Ansicht", "24-player view"}},
        {"validate", {"Welt prüfen", "Validate world"}},
        {"warnings", {"Hinweise", "Warnings"}},
        {"errors", {"Fehler", "Errors"}},
        {"no_changes", {"Noch keine Änderungen", "No changes yet"}},
        {"dirty_prompt", {"Änderungen speichern?", "Save changes?"}},
        {"dirty_detail",
         {"Diese Welt enthält ungespeicherte Änderungen.", "This world has unsaved changes."}},
        {"delete_prompt", {"Datensatz wirklich löschen?", "Delete this record?"}},
        {"new_name", {"Name des neuen Datensatzes", "Name of the new record"}},
        {"new_title", {"Neue Fußballwelt", "New football world"}},
        {"start_date", {"Saisonbeginn", "Season start"}},
        {"title", {"Titel", "Title"}},
        {"currency", {"Währung", "Currency"}},
        {"loading", {"Wird geladen …", "Loading …"}},
        {"saved_ok", {"Welt gespeichert", "World saved"}},
        {"copied", {"In die Zwischenablage kopiert", "Copied to clipboard"}},
        {"error", {"Fehler", "Error"}},
        {"open_failed", {"Die Datei konnte nicht geöffnet werden.", "The file could not be opened."}},
        {"save_failed",
         {"Speichern fehlgeschlagen. Die bisherige Datei bleibt erhalten.",
          "Saving failed. The previous file is retained."}},
        {"invalid_archive",
         {"Das Archiv ist beschädigt oder wird nicht unterstützt.",
          "The archive is damaged or unsupported."}},
        {"unsupported_archive",
         {"Dieses Archivformat wird nicht unterstützt.", "This archive format is unsupported."}},
        {"archive_limit",
         {"Das Datenpaket überschreitet die unterstützten Grenzen.",
          "The data package exceeds the supported limits."}},
        {"unsafe_path", {"Unzulässiger Dateipfad.", "Invalid file path."}},
        {"size_limit", {"Die Datei ist zu groß.", "The file is too large."}},
        {"missing_manifest", {"Keine OBW-Welt gefunden.", "No OBW world found."}},
        {"not_world", {"Bitte eine OBW-Startdatenbank öffnen.", "Please open an OBW starting world."}},
        {"unsupported_version",
         {"Diese Formatversion wird nicht unterstützt.", "This format version is unsupported."}},
        {"invalid_json", {"Die JSON-Daten sind ungültig.", "The JSON data is invalid."}},
        {"invalid_table", {"Eine Datentabelle ist ungültig.", "A data table is invalid."}},
        {"unknown_table", {"Unbekannter Datenbereich.", "Unknown data table."}},
        {"missing_asset", {"Das Bild fehlt im Datenpaket.", "The image is missing from the package."}},
        {"unsafe_asset", {"Das Bild kann nicht übernommen werden.", "The image cannot be imported."}},
        {"invalid_image", {"Das Bild konnte nicht gelesen werden.", "The image could not be decoded."}},
        {"image_dimensions",
         {"Das Bild hat nicht unterstützte Abmessungen.", "The image dimensions are unsupported."}},
        {"invalid_transform",
         {"Der Bildzuschnitt oder Drehwinkel ist ungültig.", "The image crop or rotation is invalid."}},
        {"external_svg_resource",
         {"Das SVG verweist auf externe Bilddateien. Bitte ein eigenständiges "
          "SVG verwenden.",
          "The SVG references external images. Please use a self-contained "
          "SVG."}},
        {"image_encode_failed",
         {"Das bearbeitete Bild konnte nicht erstellt werden.", "The edited image could not be created."}},
        {"obw_only",
         {"Es kann ausschließlich als .obw gespeichert werden.", "Only .obw output is supported."}},
        {"preserve_source",
         {"Bitte unter einem neuen Namen speichern. Der Classic-Import bleibt "
          "erhalten.",
          "Please save under a new name. The Classic source is retained."}},
        {"validation_failed",
         {"Bitte zuerst die angezeigten Datenfehler beheben.", "Please fix the reported data errors first."}},
        {"invalid_date",
         {"Bitte ein gültiges Datum im Format JJJJ-MM-TT eingeben.",
          "Please enter a valid date in YYYY-MM-DD format."}},
        {"duplicate_id",
         {"Eine Kennung fehlt oder ist mehrfach vorhanden.", "An identifier is missing or duplicated."}},
        {"missing_reference", {"Ein verknüpfter Datensatz fehlt.", "A referenced record is missing."}},
        {"attribute_range",
         {"Ein Spielwert liegt außerhalb seines Wertebereichs.",
          "An attribute is outside its declared range."}},
        {"unknown_attribute_set",
         {"Das Attributsystem ist nicht definiert.", "The attribute set is not defined."}},
        {"missing_name", {"Ein Name fehlt.", "A name is missing."}},
        {"missing_birthday", {"Das Geburtsdatum ist unbekannt.", "The date of birth is unknown."}},
        {"review_open", {"Eine Quellenfrage ist noch offen.", "A source decision is still open."}},
        {"record_referenced",
         {"Der Datensatz wird noch verwendet. Bitte zuerst die Verknüpfungen "
          "ändern.",
          "The record is still referenced. Please update its links first."}},
        {"record_missing", {"Der Datensatz wurde nicht gefunden.", "The record was not found."}},
        {"immutable_id",
         {"Kennungen können nicht nachträglich geändert werden.",
          "Identifiers cannot be changed after creation."}},
        {"stale_revision",
         {"Die Daten wurden inzwischen geändert. Bitte die Ansicht "
          "aktualisieren.",
          "The data has changed. Please refresh the view."}},
        {"invalid_change", {"Diese Änderung ist nicht gültig.", "This change is invalid."}},
        {"nothing_to_undo", {"Keine Änderung zum Rückgängigmachen.", "There is nothing to undo."}},
        {"nothing_to_redo", {"Keine Änderung zum Wiederholen.", "There is nothing to redo."}},
        {"no_world", {"Bitte zuerst eine Welt öffnen.", "Please open a world first."}},
        {"invalid_settings", {"Die Optionen sind ungültig.", "The options are invalid."}},
        {"output_parent_missing", {"Der Zielordner existiert nicht.", "The output folder does not exist."}},
        {"destination_exists", {"Die Zieldatei existiert bereits.", "The destination file already exists."}},
        {"duplicate_entry",
         {"Das Archiv enthält doppelte Dateinamen.", "The archive contains duplicate entry names."}},
        {"server_failed",
         {"MCP konnte nicht gestartet werden. Bitte den Port prüfen.",
          "MCP could not be started. Please check the port."}},
        {"source_changed",
         {"Die Quelldatei wurde außerhalb des Editors geändert. Bitte Speichern "
          "unter verwenden.",
          "The source file changed outside the editor. Please use Save as."}}};
    const auto found = words.find(key);
    if (found == words.end())
        return key;
    return language == "en" ? found->second.second : found->second.first;
}
} // namespace bmh::editor
