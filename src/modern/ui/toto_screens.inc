// SPDX-License-Identifier: GPL-3.0-only
namespace {
std::string toto_pick_id(int col, int row, int pick) {
  return "toto-pick-" + std::to_string(col) + "-" + std::to_string(row) + "-" + std::to_string(pick);
}
}
std::string App::render_toto() {
  const auto state = snap().state;
  const auto &t = state.at("toto");
  const auto summary = demo_toto_summary(state, *world_), programme = demo_toto_programme(*world_);
  const bool available = programme.at("complete").get<bool>();
  const auto &games = programme.at("rows");
  const std::array<int, 3> picks{1, 0, 2};
  std::string out = "<div id=\"toto-underlay\" class=\"toto-native\"><section class=\"toto-program\">"
      "<header><p>Die Begegnungen</p><h2>Spielprogramm</h2><span>Spielrunde 27 · Saison " + esc(programme.at("season").get<std::string>()) +
      "</span></header><div class=\"toto-program-head\"><span>Nr.</span><span>Heim – Gast</span><span>Liga</span></div>";
  const auto club_name = [&](const Json *club, bool home) {
    if (!club) return std::string("<span class=\"toto-club-name\">—</span>");
    const auto name = "<span class=\"toto-club-name\">" + esc(world_->name(*club)) + "</span>", logo = crest(club, 34);
    return "<span class=\"toto-club-label\" title=\"" + esc(world_->name(*club)) + "\">" +
        (home ? name + logo : logo + name) + "</span>";
  };
  for (int r = 0; r < 11; ++r) {
    const bool has = size_t(r) < games.size();
    const auto *home = has ? world_->find("clubs", games[size_t(r)].at("home")) : nullptr;
    const auto *away = has ? world_->find("clubs", games[size_t(r)].at("away")) : nullptr;
    out += "<div class=\"toto-match-row" + std::string(r % 2 == 0 ? " striped" : "") + "\" style=\"top:" + scalar(298 + r * 82) + "dp;\">"
        "<span class=\"toto-match-number\">" + std::to_string(r + 1) + "</span><div class=\"toto-match-teams\"><div class=\"toto-home\">" +
        club_name(home, true) + "</div><span class=\"toto-versus\">–</span><div class=\"toto-away\">" + club_name(away, false) +
        "</div></div><span class=\"toto-match-league\">" + (has ? std::to_string(games[size_t(r)].at("tier").get<int>()) + ". BL" : "—") + "</span></div>";
  }
  out += "<p class=\"toto-program-note\">" + std::string(available ? "Heim links · Gast rechts<br/>Die Spielnummer steht in jeder Tipp-Spalte."
      : "Das Spielprogramm ist unvollständig.<br/>Ein Schein kann derzeit nicht vorgemerkt werden.") + "</p></section>"
      "<article class=\"toto-coupon\"><span class=\"toto-print-mark top\"></span><span class=\"toto-print-mark bottom\"></span>"
      "<header class=\"toto-paper-heading\"><div class=\"toto-paper-brand\"><strong>TOTO</strong><h2>ELFERWETTE</h2></div>"
      "<div class=\"toto-paper-caption\"><strong>Spielschein</strong><span>11 Spiele · 6 Tipp-Spalten</span><small>Spielprogramm 27</small></div></header>";
  const auto cross = renderer_.diagram("toto-ink-cross", "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"60\" height=\"80\" viewBox=\"0 0 60 80\"><path d=\"M10 14L49 66M48 12L12 69\" fill=\"none\" stroke=\"#253e62\" stroke-width=\"5\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/></svg>", 160);
  constexpr double col_width = 221.619792, gap = 14, pick_width = (col_width - 43) / 3;
  for (int c = 0; c < 6; ++c) {
    const auto count = summary.at("counts")[size_t(c)].get<int>();
    out += "<section class=\"toto-field" + std::string(c == toto_column_ ? " selected-column" : "") + "\" style=\"left:" + scalar(28 + c * (col_width + gap)) +
        "dp;\" aria-label=\"Tipp-Spalte " + std::to_string(c + 1) + "\">" +
        btn("toto-column", "<span>Spalte</span><strong>" + std::to_string(c + 1) + "</strong>", "toto-column-heading",
          "id=\"toto-column-" + std::to_string(c) + "\" data-column=\"" + std::to_string(c) + "\" aria-pressed=\"" + (c == toto_column_ ? "true" : "false") + "\"") +
        "<div class=\"toto-pick-head\"><span style=\"left:0;width:28dp;\">Nr.</span>";
    for (int p = 0; p < 3; ++p)
      out += "<span style=\"left:" + scalar(33 + p * (pick_width + 5)) + "dp;width:" + scalar(pick_width) + "dp;\">" + std::to_string(picks[size_t(p)]) + "</span>";
    out += "</div>";
    for (int r = 0; r < 11; ++r) {
      out += "<div class=\"toto-pick-row\" style=\"top:" + std::to_string(116 + r * 82) + "dp;\"><span class=\"toto-row-number\">" + std::to_string(r + 1) + "</span>";
      for (int p = 0; p < 3; ++p) {
        const auto pick = picks[size_t(p)]; const bool chosen = t.at("columns")[size_t(c)][size_t(r)] == pick;
        const bool tabstop = toto_focus_[size_t(c)] == std::pair(r, pick);
        out += btn("toto-pick", "<span class=\"toto-pick-value\">" + std::to_string(pick) + "</span>" + (chosen ?
            "<span class=\"toto-cross\" style=\"transform:rotate(" + std::to_string(((c + r) % 3 - 1) * 3) + "deg);\">" + picture(cross, pick_width, 64) + "</span>" : ""),
            "toto-pick", "id=\"" + toto_pick_id(c, r, pick) + "\" data-column=\"" + std::to_string(c) + "\" data-row=\"" + std::to_string(r) +
            "\" data-pick=\"" + std::to_string(pick) + "\" aria-pressed=\"" + (chosen ? "true" : "false") + "\" aria-label=\"Spalte " + std::to_string(c + 1) +
            ", Spiel " + std::to_string(r + 1) + ", " + (pick == 1 ? "Heimsieg" : pick == 0 ? "Unentschieden" : "Auswärtssieg") +
            "\" style=\"left:" + scalar(33 + p * (pick_width + 5)) + "dp;width:" + scalar(pick_width) + "dp;tab-index:" + (tabstop ? "auto" : "none") + ";\" data-tabstop=\"" + (tabstop ? "true" : "false") + "\"");
      }
      out += "</div>";
    }
    out += "<p class=\"toto-field-progress\">" + (count == 0 ? "Leer" : count == 11 ? "Vollständig" : std::to_string(count) + " / 11") + "</p></section>";
  }
  out += "<footer class=\"toto-paper-footer\"><p><strong>1</strong> Heimsieg <b>·</b> <strong>0</strong> Unentschieden <b>·</b> <strong>2</strong> Auswärtssieg</p>"
      "<span>Pro Spiel und Spalte ein Kreuz. Leere Spalten bleiben unberücksichtigt.</span><small>OpenBMH · Musterschein · Grafik-POC</small></footer></article>";
  const int complete = summary.at("complete"), partial = summary.at("partial");
  out += "<div class=\"toto-status\"><div><strong>" + std::to_string(complete) + (complete == 1 ? " vollständige Spalte" : " vollständige Spalten") +
      "</strong><span>" + (partial ? std::to_string(partial) + (partial == 1 ? " Spalte noch unvollständig" : " Spalten noch unvollständig") :
        "Spalte " + std::to_string(toto_column_ + 1) + " für Tippvorschlag und Leeren ausgewählt") + "</span></div><p>Einsatz gesamt <strong>" + esc(cash(t.at("stake"))) + "</strong></p></div>";
  out += "<nav class=\"toto-nav\">" + btn("toto-stake", "Einsatz", "toto-control", "id=\"toto-stake\"") +
      btn("toto-auto", "Tippvorschlag", "toto-control", "id=\"toto-auto\"") +
      btn("toto-clear", "Spalte leeren", "toto-control", "id=\"toto-clear\" " + std::string(summary.at("counts")[size_t(toto_column_)] == 0 ? "disabled" : "")) +
      btn("toto-submit", "Übernehmen", "toto-control", "id=\"toto-submit\" " + std::string(available && summary.at("can_submit").get<bool>() ? "" : "disabled")) +
      btn("toto-last", "Letzter Schein", "toto-control", "id=\"toto-last\" " + std::string(t.at("coupon").is_null() ? "disabled" : "")) + "</nav></div>";
  if (toto_dialog_.empty()) return out;
  const auto &coupon = t.at("coupon");
  if (coupon.is_null()) { toto_dialog_.clear(); return out; }
  std::vector<int> active;
  for (int c = 0; c < 6; ++c) if (coupon.at("columns")[size_t(c)][0] != -1) active.push_back(c);
  out += "<div id=\"toto-layer\" class=\"toto-layer\"><section class=\"toto-dialog " + toto_dialog_ + "\" role=\"dialog\" aria-modal=\"true\" aria-labelledby=\"toto-dialog-title\">"
      "<h2 id=\"toto-dialog-title\">" + std::string(toto_dialog_ == "receipt" ? "Letzter vorgemerkter Schein" : "Tippschein vorgemerkt") + "</h2>";
  const auto caption = std::to_string(active.size()) + (active.size() == 1 ? " Spalte" : " Spalten");
  if (toto_dialog_ == "receipt") {
    out += "<div class=\"toto-receipt\">";
    for (int row = -1; row < 11; ++row) {
      out += "<div class=\"toto-receipt-row " + std::string(row == -1 ? "head" : row % 2 == 0 ? "striped" : "") + "\"><span style=\"width:16%;\">" + (row == -1 ? "Spiel" : std::to_string(row + 1)) + "</span>";
      for (const auto c : active) out += "<span style=\"width:" + scalar(84.0 / active.size()) + "%;\">" +
          (row == -1 ? "Spalte " + std::to_string(c + 1) : std::to_string(coupon.at("columns")[size_t(c)][size_t(row)].get<int>())) + "</span>";
      out += "</div>";
    }
    out += "</div><p class=\"toto-receipt-total\">" + caption + " · Einsatz gesamt " + esc(cash(coupon.at("stake"))) + "</p>";
  } else out += "<p class=\"toto-notice\">" + caption + " · " + std::to_string(active.size() * 11) + " Tipps · Einsatz gesamt " + esc(cash(coupon.at("stake"))) + "</p>";
  out += "<nav class=\"toto-dialog-actions\">" + btn("toto-dialog-close", toto_dialog_ == "receipt" ? "Schließen" : "Verstanden", "toto-control", "id=\"toto-dialog-close\"") + "</nav></section></div>";
  return out;
}
