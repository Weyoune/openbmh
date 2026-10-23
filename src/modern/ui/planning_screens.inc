// SPDX-License-Identifier: GPL-3.0-only
std::string App::planning_progress(bool second, bool postpone) {
  const std::array<std::string, 2> captions = postpone
      ? std::array<std::string, 2>{"Spiel wählen", "Termin wählen"}
      : std::array<std::string, 2>{"Gegner", "Termin"};
  std::string out = "<div class=\"planning-progress\">";
  for (int i = 0; i < 2; ++i)
    out += "<span class=\"planning-step " + std::string(second == bool(i) ? "current" : "") +
           "\"><span class=\"planning-step-number\"><span>" + std::to_string(i + 1) +
           "</span></span><span>" + captions[size_t(i)] + "</span></span>";
  return out + "</div>";
}

std::string App::render_postpone() {
  const auto calendar = planning_calendar();
  const auto own = manager(snap()).value("club", "");
  const auto *league = world_->find("competitions", calendar.at("league"));
  const auto fixtures = calendar_fixtures(calendar, *world_, own);
  const int day = calendar.at("selected"), from = calendar.at("source_day");
  const auto selected = std::find_if(fixtures.begin(), fixtures.end(), [&](const auto &f) { return f.day == day; });
  const auto source = std::find_if(fixtures.begin(), fixtures.end(), [&](const auto &f) { return f.from == from; });
  const bool has_source = source != fixtures.end();
  const bool valid = has_source && day >= 7 && !calendar_day_occupied(calendar, *world_, own, day);
  const auto start = calendar.at("start").get<std::string>().substr(0, 4);
  const auto season = start + "/" + std::to_string((std::stoi(start) + 1) % 100);
  const auto status = !has_source ? "Zuerst ein gelb markiertes Ligaspiel wählen."
      : day < 7 ? "Bitte einen Termin ab dem 8. Juli wählen."
      : !valid ? "Dieser Termin ist bereits belegt." : "Freier Termin · Spiel kann verlegt werden.";
  auto source_calendar = calendar;
  source_calendar["selected"] = has_source ? source->day : day;
  const auto source_label = has_source || selected != fixtures.end() ? calendar_day_info(source_calendar)
      : "Ein gelb markiertes Ligaspiel im Kalender anklicken.";
  std::string body = "<div id=\"report-underlay\" class=\"report-underlay postpone-layout\">"
      "<header class=\"planning-heading\"><h2>" + esc(world_->name(*world_->find("clubs", own))) +
      "</h2><p>" + esc(league ? world_->name(*league) : "") + " · Saison " + season +
      "</p>" + planning_progress(has_source, true) + "</header>" +
      calendar_grid(calendar, 132, 1362, 28, 22, false, true) +
      "<section class=\"postpone-summary source " + std::string(has_source ? "selected-source" : "") + "\"><h3>Ausgangsspiel</h3><p>" + esc(source_label) +
      "</p><small>" + (has_source ? "Das Ausgangsspiel ist im Kalender grün umrahmt." : "Mit „Ausgangsspiel wählen“ bestätigen.") +
      "</small>" + (has_source ? btn("postpone-cancel", "×", "postpone-cancel", "id=\"postpone-cancel\" aria-label=\"Ausgangsspielauswahl aufheben\"") : "") +
      "</section><section class=\"postpone-summary target\"><h3>Neuer Termin</h3><p>" + esc(calendar_day_info(calendar)) +
      "</p><small class=\"" + (valid ? "valid" : "") + "\">" + status + "</small></section><nav class=\"planning-footer\">";
  body += btn("postpone-source", "Ausgangsspiel wählen", {}, "id=\"postpone-source\" " + std::string(selected == fixtures.end() ? "disabled" : "")) +
          btn("postpone-move", "Hierhin verlegen", {}, "id=\"postpone-move\" " + std::string(valid ? "" : "disabled")) +
          btn("postpone-undo", "Letzte Verlegung zurück", {}, "id=\"postpone-undo\" " + std::string(calendar.at("moves").empty() ? "disabled" : ""));
  return body + "</nav></div>" + render_planning_dialog();
}

std::string App::render_friendlies() {
  const auto calendar = planning_calendar();
  const auto own = manager(snap()).value("club", "");
  const auto *league = world_->find("competitions", calendar.at("friendly_league"));
  const auto *opponent = world_->find("clubs", calendar.at("opponent"));
  const bool booking = calendar.at("friendly_stage") == 1, full = calendar.at("friendlies").size() >= 20;
  const int day = calendar.at("selected");
  const bool available = day >= 7 && !calendar_day_occupied(calendar, *world_, own, day);
  const auto start = calendar.at("start").get<std::string>().substr(0, 4);
  const auto season = start + "/" + std::to_string((std::stoi(start) + 1) % 100);
  const auto venue = calendar.at("home").get<bool>() ? "Heimspiel" : "Auswärtsspiel";
  std::string body = "<div id=\"report-underlay\" class=\"report-underlay friendlies-layout\">"
      "<header class=\"planning-heading " + std::string(booking ? "booking" : "") + "\"><h2>" + (booking ? label(opponent, 58) : esc(league ? world_->name(*league) : "—")) +
      "</h2><p>" + (booking ? std::string(venue) + " · Kostenbeitrag " + cash(calendar_friendly_fee(calendar, *world_))
                            : "Freundschaftsspiel vereinbaren · Saison " + season) +
      "</p>" + planning_progress(booking, false) + "</header>";
  const auto count_label = "Vorgemerkte Spiele · " + std::to_string(calendar.at("friendlies").size());
  if (!booking) {
    const auto clubs = world_->clubs(calendar.at("friendly_league"));
    auto reports = snap().state.at("reports");
    reports["league"] = calendar.at("friendly_league");
    reports["round"] = 0;
    const auto strengths = report_strength_rows(*world_, reports);
    body += "<div id=\"friendly-opponents\" class=\"friendly-opponents\"><div class=\"friendly-table\"><div class=\"friendly-row head\">";
    for (const auto &heading : {"Gegner", "Stärke", "Platz", "Spiele"}) body += "<div class=\"friendly-cell\"><span>" + std::string(heading) + "</span></div>";
    body += "</div>";
    size_t visible = 0;
    for (size_t i = 0; i < clubs.size(); ++i) {
      const auto *c = clubs[i]; const auto id = c->value("id", "");
      if (id == own) continue;
      const auto value = std::find_if(strengths.begin(), strengths.end(), [&](const auto &r) { return r.at("club") == id; });
      const auto strength = value == strengths.end() ? "—" : report_number(value->at("strength"));
      body += "<div class=\"friendly-row " + std::string(visible++ % 2 == 0 ? "striped " : "") + (calendar.at("opponent") == id ? "selected" : "") + "\"><div class=\"friendly-cell\">" +
          btn("friendly-opponent", label(c, 42), "friendly-opponent", "id=\"friendly-opponent-" + esc(id) + "\" data-id=\"" + esc(id) + "\" aria-pressed=\"" + (calendar.at("opponent") == id ? "true" : "false") + "\"") +
          "</div><div class=\"friendly-cell\"><span>" + strength + "</span></div><div class=\"friendly-cell\"><span>" + std::to_string(i + 1) +
          "</span></div><div class=\"friendly-cell\"><span>26</span></div></div>";
    }
    body += "</div></div><nav class=\"report-footer\">" + report_picker("country", 0, 547.526882) + report_picker("league", 569.526882, 766.537634) +
        btn("friendly-calendar", "Termin auswählen", "report-reset", "id=\"friendly-calendar\" style=\"" + rect(1358.064516, 0, 547.526882, 94) + "\" " + (full || !opponent ? "disabled" : "")) +
        btn("friendly-list", count_label, "report-reset", "id=\"friendly-list\" style=\"" + rect(1927.591398, 0, 684.408602, 94) + "\"");
  } else {
    body += calendar_grid(calendar, 148, 1424, 22, 22, true, false) +
        "<section class=\"friendly-date-summary\"><div class=\"date-side\"><span>Gewählter Termin</span><p>" + esc(calendar_day_info(calendar)) +
        "</p></div><div class=\"status-side\"><div class=\"friendly-key\"><span class=\"league-dot\"></span><span>Ligaspiel</span><span class=\"friendly-dot\"></span><span>Freundschaftsspiel</span></div><p class=\"" + (available && !full ? "valid" : "") + "\">" +
        (full ? "Alle 20 Vormerkungen sind belegt." : available ? "Freier Termin · Spiel kann vorgemerkt werden." : "Bitte einen freien Termin ab dem 8. Juli wählen.") +
        "</p></div></section><nav class=\"planning-footer\">" +
        btn("friendly-back", "Gegner ändern", {}, "id=\"friendly-back\"") +
        btn("friendly-list", count_label, {}, "id=\"friendly-list\"") +
        btn("friendly-book", "Termin vormerken", {}, "id=\"friendly-book\" " + std::string(available && !full && opponent ? "" : "disabled"));
  }
  return body + "</nav></div>" + render_planning_dialog();
}

std::string App::render_planning_dialog() {
  if (planning_dialog_.empty()) return {};
  const auto calendar = planning_calendar();
  const auto *opponent = world_->find("clubs", calendar.at("opponent"));
  std::string heading, body, actions;
  double height = planning_dialog_ == "undo-blocked" ? 332 : 341;
  const auto close = [&](const std::string &caption) { return btn("planning-close", caption, {}, "id=\"planning-close\""); };
  if (planning_dialog_ == "venue" || planning_dialog_ == "quote") {
    const bool quote = planning_dialog_ == "quote";
    heading = quote ? "Freundschaftsspiel · Kosten" : "Austragungsort";
    height = quote ? 720.5 : 496;
    body = "<div class=\"friendly-dialog-club\"><span class=\"club-label\"><span class=\"planning-badge\">" + crest(opponent, 72) + "</span><span>" + esc(opponent ? world_->name(*opponent) : "—") + "</span></span><span>" +
        (quote ? std::string(calendar.at("home").get<bool>() ? "Heimspiel" : "Auswärtsspiel") : "Heimspiel oder Auswärtsspiel?") + "</span></div>";
    if (quote) {
      body += "<div class=\"friendly-quote-amount\"><span>Fiktiver Kostenbeitrag</span><strong>" + cash(calendar_friendly_fee(calendar, *world_)) +
          "</strong></div><p class=\"friendly-quote-note\">Anschließend einen freien Termin im Kalender wählen.</p>";
      actions = close("Abbrechen") + btn("friendly-quote-accept", "Termin wählen", {}, "id=\"friendly-quote-accept\"");
    } else actions = btn("friendly-venue", "Heim", {}, "id=\"friendly-home\" data-id=\"home\"") +
        btn("friendly-venue", "Auswärts", {}, "id=\"friendly-away\" data-id=\"away\"") + close("Abbrechen");
  } else if (planning_dialog_ == "list") {
    heading = "Vorgemerkte Freundschaftsspiele";
    auto bookings = calendar.at("friendlies");
    std::sort(bookings.begin(), bookings.end(), [](const auto &a, const auto &b) { return a.at("day") < b.at("day"); });
    const auto days = calendar_days(calendar.at("start"));
    height = bookings.empty() ? 330.5 : 287 + std::min(1020., 80. * (bookings.size() + 1));
    body = "<div class=\"friendly-bookings\">";
    if (bookings.empty()) body += "<p>Noch keine Freundschaftsspiele vorgemerkt.</p>";
    else {
      body += "<div class=\"friendly-booking-row head\"><span><span>Datum</span></span><span><span>Gegner</span></span><span><span>Ort</span></span></div>";
      for (const auto &f : bookings)
        body += "<div class=\"friendly-booking-row\"><span><span>" + calendar_date_label(days.at(f.at("day").get<size_t>()).iso) +
            "</span></span><span>" + label(world_->find("clubs", f.at("opponent")), 42) + "</span><span><span>" +
            (f.at("home").get<bool>() ? "Heim" : "Auswärts") + "</span></span></div>";
    }
    body += "</div>";
    actions = close("Zurück");
  } else {
    heading = planning_dialog_ == "booked" ? "Spiel vorgemerkt" : "Rücknahme nicht möglich";
    body = "<p class=\"planning-notice\">" + esc(planning_notice_) + "</p>";
    actions = close("Verstanden");
  }
  return "<div id=\"planning-layer\" class=\"planning-layer\"><section id=\"planning-dialog\" class=\"planning-dialog " + planning_dialog_ +
      "\" role=\"dialog\" aria-modal=\"true\" aria-labelledby=\"planning-dialog-title\" style=\"height:" + scalar(height) +
      "dp;\"><h2 id=\"planning-dialog-title\">" + heading + "</h2>" + body +
      "<nav class=\"planning-dialog-actions\">" + actions + "</nav></section></div>";
}
