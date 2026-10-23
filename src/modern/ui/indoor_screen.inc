// SPDX-License-Identifier: GPL-3.0-only
std::string App::render_indoor() {
  const auto snapshot = snap();
  const auto &state = snapshot.state.at("reports");
  const auto &sort = state.at("sorts").at("indoor");
  const auto rows = report_indoor_rows(*world_, state);
  std::string body = "<div id=\"report-underlay\" class=\"report-underlay\">"
                     "<div id=\"report-list\" class=\"report-list indoor-list\">"
                     "<div class=\"report-table\" role=\"table\">"
                     "<div class=\"report-row report-head\" role=\"row\">";
  for (const auto &[key, label] :
       std::array<std::pair<std::string, std::string>, 2>{
           {{"rank", "Pl. / Verein"}, {"points", "Punkte"}}}) {
    const bool current = sort.at("key") == key;
    const bool descending = sort.at("desc").get<bool>();
    std::string indicator = "<span class=\"report-sort-symbol\">";
    if (!current || !descending)
      indicator += "<span class=\"report-sort-arrow up\"></span>";
    if (!current || descending)
      indicator += "<span class=\"report-sort-arrow down\"></span>";
    indicator += "</span>";
    body += "<div class=\"report-cell " + std::string(key == "rank" ? "first" : "") +
            "\" role=\"columnheader\" aria-sort=\"" +
            (current ? descending ? "descending" : "ascending" : "none") + "\">" +
            btn("report-sort", "<span>" + esc(label) + "</span>" + indicator,
                "report-sort", "id=\"report-sort-" + key + "\" data-id=\"" + key +
                    "\" aria-label=\"Nach " + esc(label) + " sortieren\"") + "</div>";
  }
  body += "</div>";
  for (size_t i = 0; i < rows.size(); ++i) {
    const auto &row = rows[i];
    body += "<div class=\"report-row " + std::string(i % 2 == 0 ? "striped" : "") +
            "\" role=\"row\" data-club=\"" + esc(row.at("club").get<std::string>()) +
            "\"><div class=\"report-cell first\" role=\"cell\">"
            "<span class=\"report-rank\"><span>" +
            std::to_string(row.at("rank").get<int>()) + ".</span></span>" +
            report_club_button(row.at("club").get<std::string>()) +
            "</div><div class=\"report-cell\" role=\"cell\">"
            "<span class=\"report-value\">" + std::to_string(row.at("points").get<int>()) +
            "</span></div></div>";
  }
  body += "</div></div><nav class=\"report-footer\">" +
          btn("indoor-info", "<span>Nach dem 5. Turnier</span>", "report-reset indoor-info",
              "id=\"indoor-info\"") + "</nav></div>";
  return body + render_report_detail();
}

bool App::indoor_action(const std::string &action, Rml::Element *target) {
  if (action != "indoor-info")
    return false;
  if (menu_ != "indoor" || !world_ || !target)
    return true;
  report_detail_return_focus_ = target->GetId();
  report_detail_kind_ = "indoor-info";
  report_detail_club_.clear();
  report_detail_player_.clear();
  focus_after_render_ = "report-detail-close";
  refresh();
  return true;
}
