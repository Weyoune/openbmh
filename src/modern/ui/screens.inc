// SPDX-License-Identifier: GPL-3.0-only
// Included in the native UI implementation's anonymous namespace.
void App::init_desktop() {
  if (!desktop_.contains("objects")) {
    desktop_ = default_desktop();
    saved_desktop_ = desktop_;
  }
}

std::string App::widget(const std::string &id, const Json &o) {
  const auto s = snap();
  const auto *own = club(s);
  const auto w = o.value("w", 472), h = o.value("h", 472);
  const auto row = world_->clubs(own ? own->value("competition", "") : league_);
  const auto *opponent =
      row.size() > 1 ? (row[0] == own ? row[1] : row[0]) : own;
  auto heading = [&](const std::string &name) {
    return "<div class=\"card-heading\">" + esc(name) + "</div>";
  };
  if (id == "calendar") {
    const auto date = s.state.value("date", std::string("2026-07-01"));
    const auto year = date.substr(0, 4), day = date.substr(8, 2);
    const int month = std::stoi(date.substr(5, 2));
    const std::array<const char *, 12> months = {
        "Januar", "Februar", "März",      "April",   "Mai",      "Juni",
        "Juli",   "August",  "September", "Oktober", "November", "Dezember"};
    return "<div "
           "style=\"height:116dp;background-color:#a45254;color:white;text-"
           "align:center;padding-top:30dp;font-size:38dp;\">" +
           std::string(months[size_t(std::clamp(month, 1, 12) - 1)]) + " " +
           year +
           "</div><div class=\"center\" style=\"padding-top:66dp;\"><p "
           "class=\"muted\" style=\"font-size:38dp;\">Saisonvorbereitung</p><p "
           "style=\"font-size:240dp;line-height:1.1;\">" +
           std::to_string(std::stoi(day)) + "</p><p class=\"small\">" +
           esc(date) + "</p></div>" +
           btn("open:calendar",
               glyph("calendar", 32) +
                   "<span>Kalender öffnen</span><span "
                   "class=\"calendar-arrow\">" +
                   glyph("arrow-diagonal", 30) + "</span>",
               "card-button calendar-link",
               "style=\"left:0;bottom:0;width:100%;height:90dp;border-top:2dp "
               "var(--border);font-size:25dp;color:var(--muted);\"");
  }
  if (id == "account") {
    if (h < 300)
      return "<div style=\"padding:18dp 28dp;\"><span "
             "class=\"small\">Konto</span><p "
             "style=\"font-size:46dp;color:var(--accent);\">" +
             cash(s.state.value("cash", int64_t(0))) + "</p></div>" +
             btn("ticket",
                 "Eintritt &nbsp; " +
                     std::to_string(s.state.value("ticket", 13)) + " €",
                 "card-button",
                 "id=\"desktop-ticket\" data-interactive=\"true\" "
                 "style=\"right:20dp;top:45dp;font-size:32dp;\"");
    return heading("Vereinskasse") +
           "<div style=\"position:absolute;left:34dp;top:90dp;\"><p "
           "class=\"small\">Guthaben</p><p style=\"font-size:" +
           std::to_string(w < 600 ? 48 : 65) +
           "dp;letter-spacing:-2dp;color:var(--accent);margin-top:10dp;\">" +
           cash(s.state.value("cash", int64_t(0))) +
           "</p></div><div "
           "style=\"position:absolute;left:0;right:0;bottom:0;height:132dp;"
           "border-top:2dp var(--border);padding:22dp 34dp;\"><span "
           "class=\"small\">Sparbuch</span><p style=\"font-size:34dp;\">" +
           cash(s.state.value("deposit", int64_t(0))) + "</p></div>" +
           btn("ticket",
               "<span class=\"small\">Eintritt</span><br/>" +
                   std::to_string(s.state.value("ticket", 13)) + " € &nbsp; ›",
               "card-button",
               "id=\"desktop-ticket\" data-interactive=\"true\" "
               "style=\"right:30dp;bottom:20dp;min-width:200dp;padding:12dp "
               "24dp;font-size:34dp;background-color:var(--soft);border-radius:"
               "18dp;\"");
  }
  if (id == "portrait")
    return img(std::string(manager(s).value("portrait_style", "photo") ==
                                   "painted"
                               ? "manager-painted."
                               : "manager.") +
                   std::to_string(manager(s).value("portrait", 0)),
               w, h, false, true, 800) +
           "<div "
           "style=\"position:absolute;left:0;right:0;bottom:0;padding:50dp "
           "30dp "
           "28dp;decorator:linear-gradient(0deg,#0d1928ed,#0d192800);color:#"
           "eff4f8;\"><small>Manager</small><p style=\"font-size:34dp;\">" +
           esc(manager(s).value("name", "")) + "</p></div>";
  if (id == "crest" || id == "opponent-crest")
    return "<div class=\"center\" style=\"padding:35dp 30dp;\">" +
           crest(id == "crest" ? own : opponent, std::min(w - 60, h - 150)) +
           "</div><div "
           "style=\"position:absolute;left:15dp;right:15dp;bottom:28dp;text-"
           "align:center;font-size:28dp;\"><p class=\"small\">" +
           (id == "crest" ? "Dein Verein" : "Nächster Gegner") + "</p><p>" +
           esc(world_->name(*(id == "crest" ? own : opponent))) + "</p></div>";
  if (id == "competition")
    return heading("Nächste Begegnung") +
           "<div class=\"row\" "
           "style=\"position:absolute;left:30dp;right:30dp;top:180dp;justify-"
           "content:space-around;text-align:center;\"><div>" +
           crest(own, 80) + "<p style=\"font-size:23dp;margin-top:14dp;\">" +
           esc(own ? world_->name(*own) : "") +
           "</p></div><div><p class=\"small\">Freundschaftsspiel</p><p "
           "style=\"font-size:54dp;\">19:00</p><p class=\"small\">Heim · "
           "Samstag</p></div><div>" +
           crest(opponent, 80) +
           "<p style=\"font-size:23dp;margin-top:14dp;\">" +
           esc(opponent ? world_->name(*opponent) : "") + "</p></div></div>";
  if (id == "warnings") {
    const std::array<const char *, 10> names = {
        "Aktiengewinn",   "Kontostand",         "Freie Werbeflächen",
        "Sondertraining", "Erschöpfung",        "Startaufstellung",
        "Ersatzspieler",  "Taktischer Einsatz", "Auslaufende Verträge",
        "Lizenz"};
    const std::array<const char *, 10> icons = {
        "chart",  "coins",  "shirt", "star", "heart",
        "person", "person", "chart", "save", "check"};
    const std::array<int, 8> wide_members = {0, 1, 2, 3, 5, 7, 8, 9};
    const bool wide = h < 300;
    const int columns = wide ? 8 : 5, count = wide ? 8 : 10;
    std::string out = heading("Vereinsstatus");
    for (int i = 0; i < count; ++i) {
      const int member = wide ? wide_members[size_t(i)] : i;
      const int cell_w = (w - 52) / columns;
      const int x = 26 + cell_w * (i % columns),
                y = wide ? 90 : 95 + (i / columns) * 175;
      out += "<div style=\"position:absolute;" +
             rect(x, y, cell_w, wide ? 110 : 160) +
             "\"><div style=\"position:absolute;left:" +
             std::to_string((cell_w - 64) / 2) +
             "dp;top:0;width:64dp;height:64dp;border:2dp "
             "#9ddbc653;border-radius:32dp;padding:12dp;background-color:" +
             std::string(member == 2 ? "#c57c67" : "#65a58d") + ";\">" +
             glyph(icons[size_t(member)], 36) + "</div>" +
             (wide ? ""
                   : "<p class=\"center\" "
                     "style=\"position:absolute;left:0;right:0;top:80dp;font-"
                     "size:18dp;line-height:1.15;\">" +
                         std::string(names[size_t(member)]) + "</p>") +
             "</div>";
    }
    return out;
  }
  if (id == "trash")
    return "<div class=\"center\" style=\"padding-top:35dp;\">" +
           glyph("trash", 100) +
           "</div><p class=\"center small\">Papierkorb</p>";
  if (id == "fans")
    return heading("Fan-Stimmung") +
           "<p class=\"center accent\" "
           "style=\"font-size:80dp;padding-top:85dp;\">" +
           std::to_string(s.state.value("fans", 67)) + " %</p>";
  if (id == "news")
    return heading("Nachrichten") +
           "<p style=\"padding:90dp 30dp;font-size:34dp;\">Willkommen zur "
           "neuen Saison.</p>";
  if (id == "results")
    return heading("Letzte sechs Spiele") +
           "<div class=\"row\" style=\"padding:100dp "
           "30dp;justify-content:space-around;font-size:40dp;\"><span>–</"
           "span><span>–</span><span>–</span><span>–</span><span>–</"
           "span><span>–</span></div>";
  if (id == "own-summary" || id == "opponent-summary")
    return heading(id == "own-summary" ? "Platz &amp; Stärke · eigener Verein"
                                       : "Platz &amp; Stärke · Gegner") +
           "<div class=\"row\" style=\"padding:80dp "
           "30dp;justify-content:space-around;font-size:56dp;\"><span>–</"
           "span><span>–</span></div>";
  const auto caption = id == "duels"              ? "Direkter Vergleich"
                       : id == "comparison-curve" ? "Tabellenplätze · Vergleich"
                       : id == "own-curve" ? "Tabellenplatz · eigener Verein"
                                           : "Tabellenplatz · Gegner";
  return heading(caption) +
         "<div "
         "style=\"position:absolute;left:50dp;right:30dp;top:95dp;bottom:45dp;"
         "border-left:2dp var(--muted);border-bottom:2dp var(--muted);\"><p "
         "class=\"center muted\" "
         "style=\"padding-top:80dp;\">Saisonvorbereitung</p></div>";
}
std::string App::render_desktop() {
  init_desktop();
  const auto s = snap();
  const auto *c = club(s);
  std::string out =
      "<div class=\"info-bar\">" +
      btn("desktop-context",
          crest(c, 82) + "<span><strong>" + esc(c ? world_->name(*c) : "") +
              "</strong><small>" + esc(manager(s).value("name", "")) +
              "</small></span>",
          "identity") +
      "<div class=\"info-date\">Saisonvorbereitung<small>" +
      esc(s.state.value("date", std::string())) +
      "</small></div><div class=\"info-cash\"><small>Guthaben</small><strong>" +
      cash(s.state.value("cash", int64_t(0))) + "</strong></div></div>";
  for (const auto &[id, o] : desktop_["objects"].items()) {
    if (!o.value("visible", false))
      continue;
    const auto style = rect(o.value("x", 0), o.value("y", 0), o.value("w", 224),
                            o.value("h", 224));
    if (o.value("widget", false))
      out += "<div id=\"object-" + esc(id) + "\" data-object=\"" + esc(id) +
             "\" class=\"desktop-card\" style=\"" + style + "\">" +
             widget(id, o) +
             (o.value("pinned", true) ? "<div class=\"pin-dot\"></div>" : "") +
             "</div>";
    else {
      const auto route = id.substr(4);
      const std::array<std::string, 31> colors = {
          "#3d789b", "#375a83", "#477c73", "#42846b", "#a58a49", "#586ba0",
          "#517d71", "#668a98", "#af6860", "#6b809c", "#657cb3", "#507b9c",
          "#559b76", "#a7474e", "#967d50", "#648f91", "#54856b", "#558f93",
          "#6379a3", "#788598", "#8d7856", "#a66d64", "#748474", "#6887a2",
          "#6b9270", "#7c8b9c", "#ac8b52", "#7783a4", "#648f83", "#927d9d",
          "#ac5e63"};
      const auto i = std::find(routes.begin(), routes.end(), route);
      const auto color = colors[size_t(i - routes.begin())];
      const auto top = theme_ == "matchday" ? mixed_color(color, "#27282e", .55)
                                            : mixed_color(color, "#c5ddf1", .1);
      const auto bottom =
          theme_ == "matchday" ? "#272a30" : mixed_color(color, "#07111e", .38);
      out += "<button id=\"object-" + esc(id) + "\" data-object=\"" + esc(id) +
             "\" data-action=\"open:" + route +
             "\" class=\"desktop-app\" style=\"" + style +
             "\"><div class=\"app-tile\" "
             "style=\"decorator:linear-gradient(140deg," +
             top + "," + bottom + ");\">" +
             img("button." + route, 146, 146, false, false, 256) +
             "</div><span class=\"app-name\">" + esc(title(route)) + "</span>" +
             (o.value("pinned", true) ? "<div class=\"pin-dot\"></div>" : "") +
             "</button>";
    }
  }
  return out;
}
std::string App::render_newgame() {
  std::string body = "<h2>Grundeinstellungen</h2><p class=\"small\" "
                     "style=\"position:absolute;right:0;top:10dp;\">Zuerst die "
                     "Partie, dann die Manager.</p>";
  const std::array<const char *, 4> keys = {"duration", "difficulty", "players",
                                            "mode"};
  const std::array<const char *, 4> names = {"Spieldauer", "Schwierigkeit",
                                             "Mitspieler", "Modus"};
  const std::array<int, 4> limits = {
      5, 10, start_options_.value("league", true) ? 4 : 1, 3};
  for (int i = 0; i < 4; ++i) {
    const auto key = std::string(keys[size_t(i)]);
    const int value = start_options_.value(key, 1), x = (i % 2) * 1320,
              y = 120 + (i / 2) * 716;
    std::string symbols, caption;
    if (i == 0) {
      symbols = "<strong style=\"font-size:210dp;color:var(--accent);\">" +
                std::string(value == 5 ? "∞" : std::to_string(value)) +
                "</strong>";
      caption = value == 5 ? "Unbegrenzt" : value == 1 ? "Saison" : "Saisons";
    } else if (i == 1) {
      symbols = "<div style=\"position:relative;width:500dp;height:200dp;\">";
      const int cols = std::min(value, 5);
      for (int n = 0; n < value; ++n)
        symbols += "<div style=\"position:absolute;" +
                   rect((n % cols) * 100,
                        (n / cols) * 100 + (value <= 5 ? 50 : 0), 92, 92) +
                   "\">" + glyph("ball", 92) + "</div>";
      symbols += "</div>";
      caption = "Spielstärke " + std::to_string(value) + " von 10";
    } else if (i == 2) {
      symbols =
          "<div class=\"row\" style=\"height:200dp;justify-content:center;\">";
      for (int n = 0; n < value; ++n)
        symbols += glyph("person", 110);
      symbols += "</div>";
      caption = std::to_string(value) + " Mitspieler";
    } else {
      symbols = "<strong style=\"font-size:174dp;color:#dfcb91;\">" +
                std::string(value == 1   ? "I"
                            : value == 2 ? "II"
                                         : "III") +
                "</strong>";
      caption = value == 1   ? "Grundfunktionen"
                : value == 2 ? "Erweiterte Funktionen"
                             : "Alle Funktionen";
    }
    body += "<div class=\"panel\" style=\"position:absolute;" +
            rect(x, y, 1288, 688) +
            "padding:40dp 30dp;\"><h2 class=\"center\">" + names[size_t(i)] +
            "</h2><div class=\"row\" "
            "style=\"position:absolute;left:30dp;right:30dp;top:190dp;justify-"
            "content:space-between;\">";
    body +=
        btn("setup-step", "−", {},
            "data-id=\"" + key +
                "\" data-step=\"-1\" "
                "style=\"width:94dp;height:124dp;padding:0;font-size:72dp;\" " +
                (value == 1 ? "disabled" : ""));
    body += "<div class=\"column center\" "
            "style=\"width:800dp;align-items:center;gap:20dp;\">" +
            symbols + "<p>" + caption + "</p></div>";
    body +=
        btn("setup-step", "+", {},
            "data-id=\"" + key +
                "\" data-step=\"1\" "
                "style=\"width:94dp;height:124dp;padding:0;font-size:72dp;\" " +
                (value == limits[size_t(i)] ? "disabled" : "")) +
        "</div></div>";
  }
  body += "<div class=\"row\" "
          "style=\"position:absolute;left:0;right:0;bottom:205dp;\">";
  for (const auto *key : {"league", "world"}) {
    const bool league = std::string(key) == "league",
               active = start_options_.value(key, true),
               locked = active &&
                        (league ? (start_options_.value("players", 1) > 1 ||
                                   !start_options_.value("world", true))
                                : !start_options_.value("league", true));
    body +=
        btn("setup-competition",
            glyph(league ? "pitch" : "world", 70) + "<span>" +
                (league ? "Ligaspiel" : "WM / EM") + " &nbsp; " +
                (active ? "Aktiv" : "Aus") + "</span>",
            active ? "primary row" : "row",
            "data-id=\"" + std::string(key) +
                "\" style=\"flex:1;height:126dp;justify-content:center;\" " +
                (locked ? "disabled" : ""));
  }
  body += "</div><p class=\"small center\" "
          "style=\"position:absolute;left:0;right:0;bottom:155dp;\">Mindestens "
          "ein Wettbewerb bleibt aktiv.</p><div class=\"bottom-controls\">" +
          btn("close", "Abbrechen") + btn("setup-next", "Weiter", "primary") +
          "</div>";
  return window("Neues Spiel", body);
}
std::string App::render_manager() {
  const int limit = adding_manager_ ? 4 : start_options_.value("players", 1);
  const auto used_club = [&](const std::string &id) {
    return std::any_of(
        managers_draft_.begin(), managers_draft_.end(),
        [&](const auto &m) { return m.value("club", "") == id; });
  };
  const auto used_portrait = [&](int id) {
    return std::any_of(
        managers_draft_.begin(), managers_draft_.end(),
        [&](const auto &m) { return m.value("portrait", 0) == id; });
  };
  const int portrait =
      selections_["portrait"].empty() ? 0 : std::stoi(selections_["portrait"]);
  const auto style = selections_["portrait-style"] == "painted"
                         ? "manager-painted."
                         : "manager.";
  if (!world_->manageable(selected_club_) || used_club(selected_club_))
    for (auto *c : world_->clubs())
      if (world_->manageable(c->value("id", "")) &&
          !used_club(c->value("id", ""))) {
        selected_club_ = c->value("id", "");
        break;
      }
  std::string body;
  std::vector<std::pair<std::string, std::string>> countries;
  std::set<std::string> seen;
  for (auto *r : world_->leagues()) {
    const auto id = r->value("nation", "");
    if (seen.insert(id).second)
      countries.emplace_back(id, world_->nation_name(id));
  }
  auto leagues = world_->leagues(country_);
  if (std::none_of(leagues.begin(), leagues.end(),
                   [&](auto *r) { return r->value("id", "") == league_; }))
    league_ = leagues.empty() ? "" : leagues.front()->value("id", "");
  std::vector<std::pair<std::string, std::string>> choices;
  for (auto *r : leagues)
    choices.emplace_back(r->value("id", ""), world_->name(*r));
  if (manager_picker_) {
    body =
        "<div class=\"row\" "
        "style=\"justify-content:space-between;\"><h2>Verein auswählen</h2>" +
        btn("manager-clubs-back", "Zurück") +
        "</div><div class=\"toolbar\" style=\"margin-top:35dp;\">" +
        select("manager-country", countries, country_, "manager-country") +
        select("manager-league", choices, league_, "manager-league") +
        "</div><div class=\"list\" "
        "style=\"position:absolute;left:0;right:0;top:240dp;bottom:0;\">";
    const auto clubs = world_->clubs(league_);
    for (size_t i = 0; i < clubs.size(); ++i) {
      const auto *c = clubs[i];
      const auto id = c->value("id", "");
      const bool locked = !world_->manageable(id), used = used_club(id);
      const auto x = (i % 4) * 650, y = (i / 4) * 340;
      body +=
          "<button data-action=\"manager-club\" data-id=\"" + esc(id) +
          "\" class=\"choice " +
          (locked                 ? "locked"
           : id == selected_club_ ? "selected"
                                  : "") +
          "\" style=\"position:absolute;" + rect(x, y, 618, 310) +
          "padding:25dp;\" " + (locked || used ? "disabled" : "") +
          "><div class=\"center\">" + crest(c, 150) +
          "</div><p style=\"font-size:30dp;margin-top:15dp;\">" +
          esc(world_->name(*c)) + "</p>" +
          (locked ? "<div style=\"position:absolute;right:24dp;top:24dp;\">" +
                        glyph("lock", 40) + "</div>"
                  : "") +
          "<p class=\"small\" style=\"margin-top:12dp;\">" +
          (locked ? "Nicht spielbar"
           : used ? "Bereits vergeben"
           : id == selected_club_
               ? "Ausgewählt"
               : esc(world_->name(*world_->find("competitions", league_)))) +
          "</p></button>";
    }
    return window("Neuer Spieler", body + "</div>");
  }
  const auto *c = world_->find("clubs", selected_club_);
  body = "<h2>Mitspieler " + std::to_string(manager_index_ + 1) + " von " +
         std::to_string(limit) +
         "</h2><p class=\"small\" "
         "style=\"position:absolute;right:0;top:10dp;\">Dein Name. Dein "
         "Verein. Dein Porträt.</p>";
  body +=
      "<div class=\"panel\" style=\"position:absolute;" +
      rect(0, 90, 1288, 330) +
      "padding:32dp;\"><div "
      "style=\"position:absolute;left:32dp;top:32dp;width:970dp;\"><label "
      "class=\"small\">Managername</label><input id=\"manager-name\" "
      "maxlength=\"24\" value=\"" +
      esc(selections_["manager-name"]) +
      "\" style=\"width:100%;font-size:38dp;height:86dp;margin-top:20dp;\"/><p "
      "class=\"small\" style=\"margin-top:20dp;\">So wirst du im Spiel "
      "angesprochen.</p><p class=\"small accent\" "
      "style=\"margin-top:16dp;\">Porträt " +
      std::to_string(portrait + 1) +
      "</p></div><div "
      "style=\"position:absolute;right:32dp;top:45dp;border-radius:12dp;"
      "overflow:hidden;\">" +
      img(style + std::to_string(portrait), 174, 232, false, true, 320) +
      "</div></div>";
  body += "<div class=\"panel\" style=\"position:absolute;" +
          rect(1320, 90, 1288, 330) +
          "padding:30dp;\"><p class=\"small\">Verein</p>" +
          btn("manager-clubs",
              crest(c, 176) +
                  "<div class=\"column\" style=\"gap:20dp;\"><strong "
                  "style=\"font-size:38dp;\">" +
                  esc(c ? world_->name(*c) : "") +
                  "</strong><small class=\"small\">" +
                  esc(c ? world_->name(*world_->find(
                              "competitions", c->value("competition", "")))
                        : "") +
                  "</small><span class=\"accent\">Verein wählen ›</span></div>",
              "row",
              "style=\"margin-top:10dp;width:100%;height:230dp;background-"
              "color:transparent;border-width:0;padding:0;text-align:left;\"") +
          "</div>";
  body +=
      "<div style=\"position:absolute;" + rect(0, 460, 1288, 170) +
      "\"><h3>Startliga</h3><div class=\"row\" style=\"margin-top:20dp;\">" +
      select("manager-country", countries, country_, "manager-country") +
      select("manager-league", choices, league_, "manager-league") +
      "</div></div><div style=\"position:absolute;" +
      rect(1320, 460, 1288, 170) +
      "\"><h3>Vereinsstärke</h3><div style=\"margin-top:20dp;\">" +
      select("manager-strength",
             {{"0", "Unteres Drittel"},
              {"1", "Mittelfeld"},
              {"2", "Oberes Drittel"}},
             selections_["manager-strength"].empty()
                 ? "1"
                 : selections_["manager-strength"],
             "manager-strength") +
      "</div></div>";
  body += "<div class=\"panel\" "
          "style=\"position:absolute;left:0;top:670dp;width:1978dp;bottom:"
          "145dp;padding:26dp;\"><h3>Managerporträt</h3><div class=\"row\" "
          "style=\"position:absolute;right:26dp;top:16dp;\">" +
          btn("portrait-style", "Foto",
              selections_["portrait-style"] != "painted" ? "primary" : "",
              "data-id=\"photo\"") +
          btn("portrait-style", "Illustration",
              selections_["portrait-style"] == "painted" ? "primary" : "",
              "data-id=\"painted\"") +
          "</div>";
  for (int i = 0; i < 12; ++i) {
    const int x = 26 + (i % 6) * 320, y = 100 + (i / 6) * 440;
    const bool used = used_portrait(i);
    body += "<button data-action=\"portrait\" data-id=\"" + std::to_string(i) +
            "\" class=\"choice " + (i == portrait ? "selected" : "") +
            "\" style=\"position:absolute;" + rect(x, y, 300, 412) +
            "padding:18dp 30dp;\" " + (used ? "disabled" : "") + ">" +
            img(style + std::to_string(i), 230, 306, false, true, 350) +
            "<p style=\"font-size:24dp;margin-top:18dp;\">" +
            (used            ? "Vergeben"
             : i == portrait ? "Ausgewählt"
                             : "Porträt " + std::to_string(i + 1)) +
            "</p></button>";
  }
  body += "</div><div class=\"panel\" "
          "style=\"position:absolute;left:2010dp;right:0;top:670dp;bottom:"
          "145dp;padding:28dp;\"><h3>Mitspieler dieser Partie</h3>";
  for (size_t i = 0; i < managers_draft_.size(); ++i) {
    const auto &m = managers_draft_[i];
    body += "<div class=\"row\" style=\"margin-top:40dp;\">" +
            img("manager." + std::to_string(m.value("portrait", 0)), 90, 120,
                false, true, 180) +
            "<div><small class=\"small\">Spieler " + std::to_string(i + 1) +
            "</small><p style=\"margin-top:10dp;\">" +
            esc(m.value("name", "")) + "</p><small class=\"small\">" +
            esc(world_->name(*world_->find("clubs", m.value("club", "")))) +
            "</small></div></div>";
  }
  body += "<p class=\"small\" "
          "style=\"position:absolute;left:28dp;right:28dp;bottom:28dp;\">" +
          std::to_string(limit - int(managers_draft_.size())) +
          " weitere Plätze frei</p></div><div class=\"bottom-controls\">" +
          btn("manager-back", adding_manager_ ? "Abbrechen" : "Zurück") +
          btn("manager-next",
              adding_manager_               ? "Mitspieler hinzufügen"
              : manager_index_ + 1 == limit ? "Spiel starten"
                                            : "Mitspieler bestätigen",
              "primary",
              world_->manageable(selected_club_) && manager_index_ < limit
                  ? ""
                  : "disabled") +
          "</div>";
  return window("Neuer Spieler", body);
}
std::string App::render_opening() {
  const std::array<const char *, 3> names = {"UEFA-Cup", "Champions League",
                                             "Europapokal der Pokalsieger"};
  const auto german = world_->clubs("comp:de-1");
  std::vector<const Json *> pool;
  for (auto *l : world_->leagues("nation:de"))
    for (auto *c : world_->clubs(l->value("id", "")))
      if (pool.size() < 64)
        pool.push_back(c);
  std::string body;
  if (opening_page_ < 3) {
    body = "<h2 style=\"font-size:46dp;\">" +
           std::string(names[size_t(opening_page_)]) +
           "</h2><p class=\"small\" style=\"margin-top:16dp;\">Deutsche Teams "
           "· Saison " +
           esc(snap().state.value("date", std::string()).substr(0, 4)) + "</p>";
    const int count = opening_page_ == 0 ? 4 : 1;
    for (int i = 0; i < count && size_t(i) < german.size(); ++i) {
      const auto *c = german[size_t((i + opening_page_) % int(german.size()))];
      const bool single = count == 1;
      const int x = single ? 704 : 150 + (i % 2) * 1178,
                y = single ? 360 : 180 + (i / 2) * 730;
      body += "<div class=\"panel center\" style=\"position:absolute;" +
              rect(x, y, single ? 1200 : 1128, single ? 1000 : 680) +
              "padding:65dp;\">" + crest(c, single ? 620 : 360) +
              "<h3 style=\"margin-top:26dp;font-size:" +
              std::to_string(single ? 64 : 50) + "dp;font-weight:normal;\">" +
              esc(world_->name(*c)) + "</h3></div>";
    }
    body +=
        "<div class=\"bottom-controls\" style=\"justify-content:flex-end;\">" +
        btn("opening-next", "Weiter", "primary",
            "style=\"flex:none;width:390dp;\"") +
        "</div>";
  } else {
    const auto pair = [&](int index, int known, int size) {
      const size_t a = size_t(index * 2), b = a + 1;
      if (b >= pool.size())
        return std::string();
      const auto label_for = [&](const Json *c, bool home) {
        return "<span class=\"club-label\" style=\"flex:1;justify-content:" +
               std::string(home ? "flex-end" : "flex-start") + ";\">" +
               (home ? "<span>" + esc(world_->name(*c)) + "</span>" +
                           crest(c, size)
                     : crest(c, size) + "<span>" + esc(world_->name(*c)) +
                           "</span>") +
               "</span>";
      };
      return "<div class=\"row\" style=\"height:100%;gap:18dp;\"><span "
             "class=\"small\" style=\"width:40dp;text-align:right;\">" +
             std::to_string(index + 1) + "</span>" +
             (known > 0 ? label_for(pool[a], true)
                        : "<span class=\"muted\" "
                          "style=\"flex:1;text-align:right;\">Heim</span>") +
             "<span class=\"muted\" "
             "style=\"width:30dp;text-align:center;\">–</span>" +
             (known > 1 ? label_for(pool[b], false)
                        : "<span class=\"muted\" style=\"flex:1;\">Noch nicht "
                          "gezogen</span>") +
             "</div>";
    };
    body = "<h2 style=\"font-size:46dp;\">" +
           std::string(opening_page_ == 3 ? "1. Runde" : "DFB-Pokal") +
           "</h2><p class=\"small\" style=\"margin-top:16dp;\">" +
           (opening_page_ == 3 ? "DFB-Pokal · Saison " : "1. Runde · Saison ") +
           esc(snap().state.value("date", std::string()).substr(0, 4)) + "</p>";
    if (opening_page_ == 3) {
      body += "<span class=\"small\" "
              "style=\"position:absolute;right:0;top:20dp;\">" +
              std::to_string(draw_revealed_) +
              " / 64 Mannschaften</span><div style=\"position:absolute;" +
              rect(0, 138, 2608, 850) + "\">" +
              img("art.cup-draw-v1.png", 2608, 850, false, false, 1800) +
              "</div>";
      const int current = std::max(0, (draw_revealed_ - 1) / 2),
                known = draw_revealed_ ? draw_revealed_ - current * 2 : 0;
      body += "<div class=\"panel\" style=\"position:absolute;" +
              rect(130, 1026, 2348, 126) +
              "padding:10dp 28dp;font-size:38dp;\">" +
              pair(current, known, 66) + "</div>";
      for (int i = std::max(0, current - 3); i < current; ++i)
        body +=
            "<div style=\"position:absolute;" +
            rect(158, 1190 + (i - std::max(0, current - 3)) * 155, 2292, 145) +
            "border-bottom:2dp var(--border);\">" + pair(i, 2, 42) + "</div>";
      body += "<div class=\"bottom-controls\" "
              "style=\"justify-content:flex-end;\">" +
              btn("opening-play", draw_playing_ ? "Pause" : "Fortsetzen", {},
                  "style=\"flex:none;width:390dp;\"") +
              btn("opening-all", "Alle Paarungen anzeigen", {},
                  "style=\"flex:none;width:600dp;\"") +
              "</div>";
    } else {
      for (int column = 0; column < 2; ++column) {
        body += "<div class=\"panel\" style=\"position:absolute;" +
                rect(column * 1321, 154, 1287, 1500) + "padding:12dp 18dp;\">";
        for (int row = 0; row < 16; ++row)
          body += "<div style=\"height:92dp;font-size:27dp;border-bottom:1dp "
                  "var(--border);\">" +
                  pair(column * 16 + row, 2, 36) + "</div>";
        body += "</div>";
      }
      body += "<div class=\"bottom-controls\" "
              "style=\"justify-content:flex-end;\">" +
              btn("opening-finish", "Weiter zum Desktop", "primary",
                  "style=\"flex:none;width:550dp;\"") +
              "</div>";
    }
  }
  return window(opening_page_ < 3 ? "Deutsche Teams" : "DFB-Pokal", body,
                false);
}
std::string App::render_table(bool strengths, bool indoor) {
  auto leagues = world_->leagues(country_);
  if (std::none_of(leagues.begin(), leagues.end(),
                   [&](auto *r) { return r->value("id", "") == league_; }))
    league_ = leagues.empty() ? "" : leagues.front()->value("id", "");
  const auto clubs = world_->clubs(league_);
  std::string body = "<div class=\"list\" "
                     "style=\"position:absolute;left:0;right:0;top:0;bottom:"
                     "160dp;\"><div class=\"table\"><div class=\"tr\"><div "
                     "class=\"th\" style=\"width:42%;\">" +
                     btn("table-sort", "Pl. / Verein", {}, "data-id=\"rank\"") +
                     "</div>";
  const std::vector<std::string> headers =
      strengths ? std::vector<std::string>{"Tor", "Abwehr", "Mittelfeld",
                                           "Sturm", "Gesamt"}
                : std::vector<std::string>{"Sp.",    "S",    "U",    "N",
                                           "Punkte", "Tore", "Diff."};
  for (const auto &h : headers)
    body += "<div class=\"th center\">" +
            btn("table-sort", esc(h), {}, "data-id=\"" + esc(h) + "\"") +
            "</div>";
  body += "</div>";
  for (size_t i = 0; i < clubs.size(); ++i) {
    auto *c = clubs[i];
    body += "<div class=\"tr " + std::string(i % 2 ? "" : "odd") +
            "\"><div class=\"td\">" +
            btn("club-detail",
                "<span class=\"club-label\"><span class=\"rank\">" +
                    std::to_string(i + 1) + ".</span>" + crest(c, 40) +
                    "<span>" + esc(world_->name(*c)) + "</span></span>",
                {}, "data-id=\"" + esc(c->value("id", "")) + "\"") +
            "</div>";
    if (strengths) {
      const auto players = world_->squad(c->value("id", ""));
      for (const auto &position : {"gk", "def", "mid", "fwd", "all"}) {
        int sum = 0, n = 0;
        for (auto *p : players) {
          const auto positions = p->value("positions", Json::array());
          if (std::string(position) == "all" ||
              std::find(positions.begin(), positions.end(), position) !=
                  positions.end()) {
            sum += p->value("attributes", Json::object()).value("strength", 0);
            ++n;
          }
        }
        body += "<div class=\"td center\">" +
                (n ? std::to_string(sum / n) : "–") + "</div>";
      }
    } else
      for (int n = 0; n < 7; ++n)
        body += "<div class=\"td center\">" +
                std::string(n == 5 ? "0:0" : "0") + "</div>";
    body += "</div>";
  }
  body += "</div></div><div class=\"bottom-controls\">";
  if (!strengths && !indoor)
    body += btn("table-overall", "Gesamt") + select("table-scope",
                                                    {{"all", "Alle Spiele"},
                                                     {"first", "Hinrunde"},
                                                     {"second", "Rückrunde"},
                                                     {"home", "Heim"},
                                                     {"away", "Auswärts"}},
                                                    selections_["table-scope"],
                                                    "table-scope");
  std::vector<std::pair<std::string, std::string>> countries, league_rows;
  std::set<std::string> seen;
  for (auto *l : world_->leagues()) {
    const auto id = l->value("nation", "");
    if (seen.insert(id).second)
      countries.emplace_back(id, world_->nation_name(id));
    if (id == country_)
      league_rows.emplace_back(l->value("id", ""), world_->name(*l));
  }
  body += select("table-country", countries, country_, "table-country") +
          select("table-league", league_rows, league_, "table-league") +
          "</div>";
  return body;
}
std::string App::render_camp() {
  const auto index_file =
      Json::parse(editor::read_file(gui_.root() / "gui-assets.json"));
  const auto demo = index_file.at("demo"), camps = demo.at("training_camps");
  const auto s = snap();
  const auto id = s.state.at("camp").value("selected", std::string("resort:4"));
  const Json *chosen = nullptr;
  for (const auto &c : camps)
    if (c.value("id", "") == id)
      chosen = &c;
  if (!chosen)
    chosen = &camps.at(0);
  auto region = selections_["camp-region"];
  if (region.empty())
    region = chosen->value("map", "map:eu");
  std::string body = "<div "
                     "style=\"position:absolute;left:0;top:0;width:1440dp;"
                     "bottom:0;\"><div class=\"toolbar\">";
  for (const auto &m : demo.at("maps"))
    body += btn("camp-region", esc(m.value("name", "")),
                m.value("id", "") == region ? "primary" : "",
                "data-id=\"" + esc(m.value("id", "")) + "\"");
  body += "</div><div "
          "style=\"position:absolute;left:0;top:120dp;width:1440dp;height:1603."
          "2dp;overflow:hidden;border-radius:22dp;\">" +
          img("map." + region.substr(4), 1440, 1603.2, false, false, 1600);
  for (const auto &c : camps)
    if (c.value("map", "") == region) {
      const double x = c.value("x", 0) * 1.2, y = c.value("y", 0) * 1.2;
      body += btn("camp-select",
                  "<span "
                  "style=\"display:block;width:18dp;height:18dp;background-"
                  "color:#dfb668;border-radius:9dp;\"></span>",
                  c.value("id", "") == id ? "primary" : "",
                  "data-id=\"" + esc(c.value("id", "")) +
                      "\" style=\"position:absolute;" +
                      rect(x - 34, y - 34, 68, 68) +
                      "padding:23dp;border-radius:34dp;\"");
    }
  body += "</div></div><div "
          "style=\"position:absolute;left:1500dp;right:0;top:0;bottom:0;\"><"
          "div style=\"height:570dp;overflow:hidden;border-radius:24dp;\">" +
          img(chosen->value("gui_image", ""), 1100, 570, false, true, 1400) +
          "</div><h2 style=\"margin-top:22dp;\">" +
          esc(chosen->value("name", "")) +
          "</h2><p class=\"small\" style=\"margin-top:14dp;\">" +
          esc(chosen->value("place", "")) + "</p>";
  const std::array<const char *, 4> qualities = {
      "Platzqualität", "Fitnessgeräte", "Verpflegung", "Unterkunft"};
  for (size_t i = 0; i < 4; ++i) {
    const int q = chosen->at("quality")[i].get<int>();
    std::string stars;
    for (int j = 0; j < q; ++j)
      stars += glyph("star", 32);
    body += "<div class=\"field-row\" style=\"min-height:78dp;padding:12dp "
            "0;\"><span>" +
            std::string(qualities[i]) +
            "</span><span class=\"row\" style=\"gap:5dp;color:#d9b96f;\">" +
            stars + "</span></div>";
  }
  body += "<div class=\"field-row\"><strong>Preis</strong><strong "
          "class=\"accent\">" +
          cash(chosen->value("price", int64_t(0))) + "</strong></div>";
  const std::array<const char *, 4> focus = {"Kondition", "Technik", "Spiel",
                                             "Erholung"};
  const auto values = s.state.at("camp").at("focus");
  int sum = 0;
  for (const auto &v : values)
    sum += v.get<int>();
  for (int i = 0; i < 4; ++i) {
    const int value = i == 3 ? 15 - sum / 3 : values[size_t(i)].get<int>();
    body += "<div class=\"field-row\" style=\"min-height:100dp;padding:8dp "
            "0;\"><label>" +
            std::string(focus[size_t(i)]) +
            "</label><input type=\"range\" id=\"camp-focus-" +
            std::to_string(i) + "\" data-input=\"camp-focus\" data-id=\"" +
            std::to_string(i) + "\" min=\"0\" max=\"15\" step=\"1\" value=\"" +
            std::to_string(value) + "\" style=\"width:400dp;\" " +
            (i == 3 ? "disabled" : "") + "/><span id=\"camp-value-" +
            std::to_string(i) +
            "\" style=\"width:145dp;flex-shrink:0;text-align:right;\">" +
            std::to_string(value) + " / 15</span></div>";
  }
  body += "<div class=\"row\" "
          "style=\"margin-top:26dp;justify-content:flex-end;\">" +
          btn("camp-remember", "Vormerken", "primary") + "</div></div>";
  return body;
}
std::string App::render_settings() {
  if (draft_preferences_.is_null())
    draft_preferences_ =
        source_ ? snap().state.at("preferences")
                : Json{{"theme", theme_}, {"volume", 80}, {"effects", true}};
  std::string nav;
  for (const auto &[key, label] :
       std::vector<std::pair<std::string, std::string>>{
           {"display", "Anzeige"},
           {"sound", "Sound"},
           {"game", "Spiel"},
           {"automation", "Automatik"}}) {
    if (scene_ == "main" && (key == "game" || key == "automation"))
      continue;
    nav += btn("settings-category", label,
               settings_category_ == key ? "primary" : "",
               "data-id=\"" + key +
                   "\" "
                   "style=\"display:flex;justify-content:flex-start;width:100%;"
                   "height:100dp;margin-"
                   "bottom:14dp;text-align:left;\"");
  }
  std::string pane = "<h2>" +
                     std::string(settings_category_ == "display" ? "Anzeige"
                                 : settings_category_ == "sound" ? "Sound"
                                 : settings_category_ == "game"  ? "Spiel"
                                                                : "Automatik") +
                     "</h2>";
  if (settings_category_ == "display") {
    pane +=
        "<h3 style=\"margin:40dp 0 24dp;\">Gestaltung</h3><div class=\"row\">";
    for (const auto &[id, name] :
         std::vector<std::pair<std::string, std::string>>{
             {"midnight", "Midnight Glass"},
             {"silver", "Silver Glass"},
             {"matchday", "Matchday"}})
      pane +=
          btn("settings-theme",
              img("background", 520, 270, false, true, 650) +
                  "<p style=\"margin-top:20dp;\">" + name + "</p>",
              std::string("choice ") +
                  (draft_preferences_.value("theme", theme_) == id ? "selected"
                                                                   : ""),
              "data-id=\"" + id + "\" style=\"width:570dp;padding:20dp;\"");
    pane +=
        "</div><h3 style=\"margin-top:50dp;\">Bildschirm</h3><div "
        "class=\"field-row\"><label>Modus</label>" +
        select("display-mode",
               {{"window", "Fenster"}, {"fullscreen", "Vollbild"}},
               draft_preferences_.value("mode", "window")) +
        "</div><div class=\"field-row\"><label>Auflösung</label>" +
        select("display-resolution",
               {{"1280x720", "1280 × 720"},
                {"1920x1080", "1920 × 1080"},
                {"2560x1440", "2560 × 1440"},
                {"3840x2160", "3840 × 2160"}},
               draft_preferences_.value("resolution", "1920x1080")) +
        "</div><div class=\"field-row\"><label>Skalierung</label>" +
        select("display-scale",
               {{"auto", "Auto"}, {"1", "1×"}, {"1.5", "1,5×"}, {"2", "2×"}},
               draft_preferences_.value("scale", "auto")) +
        "</div><div class=\"field-row\"><label>Sprache</label><select "
        "id=\"display-language\"><option value=\"de\" "
        "selected>Deutsch</option><option "
        "disabled>Englisch</option></select></div>";
  } else if (settings_category_ == "sound")
    pane +=
        "<div class=\"field-row\"><label>Lautstärke</label><input "
        "type=\"range\" min=\"0\" max=\"100\" value=\"" +
        std::to_string(draft_preferences_.value("volume", 80)) +
        "\" id=\"sound-volume\" data-input=\"sound-volume\"/><span "
        "id=\"volume-value\">" +
        std::to_string(draft_preferences_.value("volume", 80)) +
        " %</span></div><div class=\"field-row\"><label>Effekte</label><input "
        "type=\"checkbox\" id=\"sound-effects\" " +
        (draft_preferences_.value("effects", true) ? "checked" : "") +
        "/></div><div class=\"field-row\"><label>Effektstil</label>" +
        select("sound-style", {{"classic", "Klassisch"}, {"modern", "Modern"}},
               draft_preferences_.value("sound-style", "classic")) +
        "</div>";
  else if (settings_category_ == "automation") {
    pane += "<div class=\"field-row\"><label>Alles automatisch</label><input "
            "type=\"checkbox\" data-change=\"automation-all\"/></div>";
    for (const auto *name :
         {"Aufstellung", "Auswechslungen", "Taktik", "Manndeckung", "Training"})
      pane += "<div class=\"field-row\"><label>" + std::string(name) +
              "</label><input type=\"checkbox\"/></div>";
  } else
    pane +=
        std::string(
            "<div class=\"field-row\"><label>Spieloption suchen</label><input "
            "type=\"text\" id=\"settings-search\"/></div><div "
            "class=\"field-row\"><label>Reality-Modus</label><input "
            "type=\"checkbox\"/></div><div "
            "class=\"field-row\"><label>Spielszenen anzeigen</label><input "
            "type=\"checkbox\"/></div><div class=\"field-row\"><label>Desktop "
            "am Raster ausrichten</label><input type=\"checkbox\" ") +
        (desktop_.value("grid", true) ? "checked" : "") + "/></div>";
  return "<div class=\"panel\" "
         "style=\"position:absolute;left:0;top:0;width:370dp;bottom:150dp;"
         "padding:24dp 20dp;\">" +
         nav +
         "</div><div class=\"panel list\" "
         "style=\"position:absolute;left:410dp;right:0;top:0;bottom:150dp;"
         "padding:36dp;\">" +
         pane + "</div><div class=\"bottom-controls\">" +
         btn("close", "Abbrechen") +
         btn("settings-apply", "Anwenden", "primary") + "</div>";
}
std::string App::render_save(bool save) {
  const auto directory = user_dir() / "saves";
  std::string body = "<div class=\"panel list\" "
                     "style=\"position:absolute;left:0;top:0;width:1120dp;"
                     "bottom:145dp;padding:24dp;\">";
  if (save)
    body += btn("save-new", "Neuer Speicherstand", "choice",
                "style=\"width:100%;text-align:left;min-height:100dp;margin-"
                "bottom:14dp;\"");
  if (fs::exists(directory))
    for (const auto &file : fs::directory_iterator(directory))
      if (file.path().extension() == ".json") {
        const auto name = editor::utf8(file.path().stem());
        body += btn("save-select", esc(name),
                    std::string("choice ") +
                        (selections_["save"] == name ? "selected" : ""),
                    "data-id=\"" + esc(name) +
                        "\" "
                        "style=\"width:100%;min-height:110dp;text-align:left;"
                        "margin-bottom:14dp;\"");
      }
  body += "</div><div class=\"panel\" "
          "style=\"position:absolute;left:1160dp;right:0;top:0;bottom:145dp;"
          "padding:50dp;\"><h2>" +
          esc(selections_["save"].empty() ? "Speicherstand auswählen"
                                          : selections_["save"]) +
          "</h2>";
  const auto s = snap();
  body +=
      "<div style=\"margin:40dp 0;\">" + crest(club(s), 340) +
      "</div><div class=\"field-row\"><span>Saison</span><strong>" +
      esc(s.state.value("date", std::string()).substr(0, 4)) +
      "</strong></div><div "
      "class=\"field-row\"><span>Spieltag</span><strong>Saisonvorbereitung</"
      "strong></div><div class=\"field-row\"><span>Datum</span><strong>" +
      esc(s.state.value("date", std::string())) +
      "</strong></div><div "
      "class=\"field-row\"><span>Mitspieler</span><strong>" +
      std::to_string(s.state.at("managers").size()) + "</strong></div>";
  if (save && selections_["save"].empty())
    body += "<div class=\"field-row\"><label>Bezeichnung</label><input "
            "id=\"save-name\" value=\"Meine Karriere\"/></div>";
  return body + "</div><div class=\"bottom-controls\">" +
         btn("save-delete", "Löschen") + btn("save-rename", "Umbenennen") +
         btn("close", "Abbrechen") +
         btn(save ? "save-confirm" : "load-confirm",
             save ? "Speichern" : "Laden", "primary") +
         "</div>";
}
std::string App::render_menu(const std::string &key) {
  std::string body;
  if (key == "tables" || key == "strengths")
    body = render_table(key == "strengths", false);
  else if (key == "camp")
    body = render_camp();
  else if (key == "settings")
    body = render_settings();
  else if (key == "save" || key == "load")
    body = render_save(key == "save");
  else if (key == "widgets" || key == "view-save" || key == "view-load" ||
           key == "background")
    body = render_desktop_tools(key);
  else
    throw editor::Error("Diese native Ansicht ist noch nicht verfügbar.");
  return "<div class=\"fill\" style=\"z-index:100;\">" +
         window(title(key), body) + "</div>";
}
