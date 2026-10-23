// SPDX-License-Identifier: GPL-3.0-only
namespace {
std::string report_number(const Json &value) {
  return value.is_number_integer() ? number(value.get<int64_t>()) : "—";
}
std::string report_zone(int rank, int count, int tier) {
  if (rank > count - 3)
    return "relegation";
  if (tier == 1) {
    if (rank <= 4) return "champions";
    if (rank == 5) return "europa";
    if (rank == 6) return "conference";
  } else {
    if (rank <= (tier == 4 ? 1 : 2)) return "promotion";
    if (tier != 4 && rank == 3) return "playoff";
  }
  return {};
}
} // namespace

std::string App::report_club_button(const std::string &id, bool away) {
  const auto *c = world_->find("clubs", id);
  if (!c)
    return "<span>—</span>";
  const auto name = esc(world_->name(*c));
  const auto text = "<span class=\"report-club-name\" title=\"" + name +
                    "\">" + name + "</span>";
  return btn("club-detail", away ? text + crest(c, 42) : crest(c, 42) + text,
             std::string("report-club-button ") + (away ? "away " : "") +
                 (manager(snap()).value("club", "") == id ? "current" : ""),
             "id=\"report-club-" + esc(id) + "\" data-id=\"" + esc(id) + "\"");
}

std::string App::render_reports(const std::string &mode) {
  const auto snapshot = snap();
  const auto &state = snapshot.state.at("reports");
  const auto *league = world_->find("competitions", state.value("league", ""));
  const int tier = league ? league->value("tier", 1) : 1;
  const bool strengths = mode == "strengths", matches = mode == "league" || mode == "other";
  const bool opening = mode == "other" && state.value("competition", 0) == 3 &&
                       snapshot.state.value("opening_created", false);
  const auto rows = matches ? (opening ? report_opening_pairs(*world_)
                                      : report_match_rows(*world_, state, mode == "other"))
                            : strengths ? report_strength_rows(*world_, state)
                                        : report_table_rows(*world_, state);
  const std::vector<std::pair<std::string, std::string>> headings =
      matches ? std::vector<std::pair<std::string, std::string>>{
                    {"", "Heim"}, {"", "Ergebnis"}, {"", "Auswärts"}}
      : strengths ? std::vector<std::pair<std::string, std::string>>{
                        {"club", "Verein"}, {"keeper", "TW"}, {"defence", "Abwehr"},
                        {"midfield", "Mittelfeld"}, {"attack", "Sturm"}, {"strength", "Stärke"}}
                  : std::vector<std::pair<std::string, std::string>>{
                        {"rank", "Pl. / Verein"}, {"played", "Sp."}, {"wins", "S"},
                        {"draw", "U"}, {"loss", "N"}, {"form", "Letzte 8"},
                        {"points", "Punkte"}, {"gf", "Tore"}, {"diff", "Diff."}};
  std::vector<double> widths;
  if (matches)
    widths = {1104, 400, 1104}; // Match card has two 2dp borders.
  else if (strengths) {
    widths.push_back(2612 * .38);
    for (int i = 0; i < 5; ++i) widths.push_back(2612 * .62 / 5);
  } else {
    for (int i = 0; i < 9; ++i)
      widths.push_back(i == 0 ? 2612 * .32 : i == 5 ? 2612 * .14 : 2612 * .54 / 7);
  }
  const double heading_height = matches ? 80 : strengths ? (rows.size() >= 20 ? 64 : 66)
                                                               : rows.size() >= 20 ? 64 : 80;
  const double row_height = matches ? 118 : strengths ? (rows.size() >= 20 ? 64 : 66)
                           : rows.size() >= 20 ? (1682 - heading_height) / rows.size() : 80;
  std::string body = "<div id=\"report-underlay\" class=\"report-underlay\">";
  if (matches) {
    const int round = std::clamp(state.value("round", 0), 0,
                                 report_round_count(*world_, state, mode == "other") - 1);
    std::string subtitle;
    if (mode == "league" || state.value("competition", 0) == 0)
      subtitle = (league ? world_->nation_name(league->value("nation", "")) : "") +
                 " · " + (league ? world_->name(*league) : "");
    else {
      const auto choices = report_picker_items("competition");
      subtitle = choices.at(size_t(state.value("competition", 0))).second;
    }
    body += "<header class=\"report-match-heading\"><h2>" +
            (opening ? std::string("1. Runde") : std::to_string(round + 1) + ". Spieltag") +
            "</h2><p>" + esc(subtitle) + "</p></header>";
  }
  body += "<div id=\"report-list\" class=\"report-list " +
          std::string(matches ? "match-list" : strengths ? "strength-list" : "standings-list") +
          "\"><div class=\"report-table\" role=\"table\"><div class=\"report-row report-head\" "
          "role=\"row\" style=\"height:" + scalar(heading_height) + "dp;\">";
  for (size_t column = 0; column < headings.size(); ++column) {
    const auto &[key, label] = headings[column];
    const auto sorting = matches ? Json::object() : state.at("sorts").at(mode);
    const bool current = !matches && sorting.value("key", "") == key;
    const bool descending = sorting.value("desc", false);
    body += "<div class=\"report-cell " + std::string(column == 0 ? "first" : "") +
            (matches && column == 1 ? " score" : "") + "\" role=\"columnheader\" "
            "aria-sort=\"" + (current ? descending ? "descending" : "ascending" : "none") +
            "\" style=\"width:" + scalar(widths[column]) + "dp;\">";
    if (matches)
      body += "<span class=\"report-value\">" + esc(label) + "</span>";
    else {
      std::string indicator = "<span class=\"report-sort-symbol\">";
      if (!current || !descending)
        indicator += "<span class=\"report-sort-arrow up\"></span>";
      if (!current || descending)
        indicator += "<span class=\"report-sort-arrow down\"></span>";
      indicator += "</span>";
      body += btn("report-sort", "<span>" + esc(label) + "</span>" + indicator,
                  "report-sort", "id=\"report-sort-" + key + "\" data-id=\"" + key +
                                      "\" aria-label=\"Nach " + esc(label) + " sortieren\"");
    }
    body += "</div>";
  }
  body += "</div>";
  for (size_t i = 0; i < rows.size(); ++i) {
    const auto &row = rows[i];
    body += "<div class=\"report-row " + std::string(i % 2 == 0 ? "striped" : "") +
            "\" role=\"row\" data-club=\"" + esc(row.value("club", "")) +
            "\" style=\"height:" + scalar(row_height) + "dp;\">";
    for (size_t column = 0; column < headings.size(); ++column) {
      body += "<div class=\"report-cell " + std::string(column == 0 ? "first" : "") +
              (matches && column == 1 ? " score" : "") +
              (!matches && !strengths && column == 5 ? " form" : "") +
              "\" role=\"cell\" style=\"width:" + scalar(widths[column]) + "dp;\">";
      if (matches && column != 1)
        body += report_club_button(row.at(column == 0 ? "home" : "away").get<std::string>(), column == 2);
      else if (!matches && column == 0) {
        if (!strengths) {
          const auto zone = report_zone(row.value("rank", 0), int(rows.size()), tier);
          body += "<span class=\"report-zone " + zone + "\"></span><span class=\"report-rank\"><span>" +
                  std::to_string(row.value("rank", 0)) + ".</span></span>";
        }
        body += report_club_button(row.at("club").get<std::string>());
      } else {
        std::string value;
        if (matches)
          value = row.at("home_goals").is_null() || row.at("away_goals").is_null()
                      ? "–" : report_number(row["home_goals"]) + " : " + report_number(row["away_goals"]);
        else if (!strengths && column == 5)
          value = row.value("form_text", "");
        else if (!strengths && column == 7)
          value = report_number(row["gf"]) + ":" + report_number(row["ga"]);
        else
          value = report_number(row.at(headings[column].first));
        body += "<span class=\"report-value\">" + esc(value) + "</span>";
      }
      body += "</div>";
    }
    body += "</div>";
  }
  body += "</div></div>";
  if (!matches && !strengths) {
    const auto legend = tier == 1
        ? std::vector<std::pair<std::string, std::string>>{
              {"champions", "Champions League"}, {"europa", "Europa League"},
              {"conference", "Conference League"}, {"relegation", "Abstieg"}}
        : tier == 4 ? std::vector<std::pair<std::string, std::string>>{
                          {"promotion", "Aufstieg"}, {"relegation", "Abstieg"}}
                    : std::vector<std::pair<std::string, std::string>>{
                          {"promotion", "Aufstieg"}, {"playoff", "Relegation"}, {"relegation", "Abstieg"}};
    body += "<div class=\"report-legend\">";
    for (const auto &[kind, label] : legend)
      body += "<span class=\"report-legend-item\"><span class=\"report-zone " + kind +
              "\"></span><span>" + esc(label) + "</span></span>";
    body += "</div>";
  }
  body += "<nav class=\"report-footer\">";
  if (matches) {
    for (int i = 0; i < 2; ++i)
      body += btn("report-round", i ? "›" : "‹", "report-round",
                  "id=\"report-round-" + std::string(i ? "next" : "previous") +
                      "\" data-id=\"" + (i ? "1" : "-1") + "\" style=\"" +
                      rect(i * 130, 0, 110, 94) + "\" " + (opening ? "disabled" : ""));
    body += mode == "league" ? report_picker("country", 260, 1060) + report_picker("league", 1340, 1272)
                             : report_picker("competition", 260, 2352);
  } else if (strengths)
    body += report_picker("country", 0, 1294) + report_picker("league", 1318, 1294);
  else
    body += btn("report-reset", "Gesamt", "report-reset", "id=\"report-reset\" style=\"" +
                        rect(0, 0, 635, 94) + "\"") +
            report_picker("filter", 659, 635) + report_picker("country", 1318, 635) +
            report_picker("league", 1977, 635);
  return body + "</nav></div>" + render_report_detail();
}
