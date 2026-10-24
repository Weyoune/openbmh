// SPDX-License-Identifier: GPL-3.0-only
std::string App::render_environment() {
  const auto snapshot = snap();
  if (!snapshot.world) return "<p class=\"environment-unavailable\">Die Umfeldvorschau ist derzeit nicht verfügbar.</p>";
  const auto view = demo_environment_rows(snapshot.state, *snapshot.world);
  if (!view.value("ok", false)) return "<p class=\"environment-unavailable\">Die Umfeldvorschau ist derzeit nicht verfügbar.</p>";
  const auto funded = theme_ == "silver" ? "#805b18" : theme_ == "matchday" ? "#e1c68b" : "#e5cd8c";
  const auto unfunded = theme_ == "silver" ? "#526779" : theme_ == "matchday" ? "#b7b4b9" : "#6f8595";
  const auto star = [&](bool active) {
    const auto color = active ? funded : unfunded;
    const std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"48\" height=\"48\" viewBox=\"0 0 48 48\">"
        "<path fill=\"" + std::string(color) + "\" d=\"m24 3 6 13 15 2-11 11 3 15-13-7-13 7 3-15L3 18l15-2Z\"/></svg>";
    return picture(renderer_.diagram("environment-star-" + theme_ + (active ? "-funded" : "-unfunded"), svg, 96), 40, 40);
  };
  struct Panel { double x, y, w, h, image_h; };
  const std::array<Panel, 6> panels{{
      {1912,970,600,623.328125,374.78125}, {1056,1060,500,556.40625,307.859375},
      {100,970,600,623.328125,374.78125}, {100,160,600,694.375,460.375},
      {1056,0,500,612.171875,378.171875}, {1912,160,600,694.375,460.375}}};
  std::string out = "<div class=\"environment-ring\" id=\"environment-ring\">";
  const auto &rows = view.at("rows");
  for (size_t i = 0; i < rows.size(); ++i) {
    const auto &row = rows[i];
    const auto &panel = panels[i];
    const auto name = row.at("name").get<std::string>(), id = row.at("id").get<std::string>();
    const auto level = row.at("level").get<int>();
    const auto inner = panel.w - 40;
    out += "<section class=\"environment-card " + id + "\" style=\"" + rect(panel.x, panel.y, panel.w, panel.h) +
        "\" aria-labelledby=\"environment-title-" + std::to_string(i) + "\"><h2 id=\"environment-title-" +
        std::to_string(i) + "\">" + esc(name) + "</h2><div class=\"environment-stars\" role=\"group\" aria-label=\"" +
        esc(name + " Budget") + "\">";
    for (int value = 0; value <= 6; ++value) {
      const bool selected = value == level;
      const auto label = value ? "<span class=\"environment-level-icon\">" + star(value <= level) + "</span>"
                               : std::string("<span class=\"environment-zero-label\">×</span>");
      out += btn("environment-level", label + (selected ? "<span class=\"environment-selection-line\"></span>" : ""),
          "environment-level " + std::string(value == 0 ? "zero " : "") + (selected ? "selected" : ""),
          "id=\"environment-level-" + std::to_string(i) + "-" + std::to_string(value) +
          "\" data-index=\"" + std::to_string(i) + "\" data-id=\"" + std::to_string(value) +
          "\" aria-pressed=\"" + (selected ? "true" : "false") + "\" aria-label=\"" +
          esc(name + ": " + std::to_string(value) + (value == 1 ? " Stern" : " Sterne")) +
          "\" title=\"" + (value ? std::to_string(value) + (value == 1 ? " Stern" : " Sterne") : "Budget auf null setzen") + "\"");
    }
    out += "</div><div class=\"environment-picture\" style=\"" + rect(18, 138, inner, panel.image_h) + "\">" +
        img("environment." + row.at("image").get<std::string>() + "." + std::to_string(row.at("cell").get<int>()),
            inner, panel.image_h, false, false, 1024) +
        "</div><div class=\"environment-caption\" style=\"top:" + scalar(152 + panel.image_h) + "dp;height:" +
        scalar(i < 3 ? 74.546875 : 60) + "dp;\"><span class=\"environment-cost\" id=\"environment-cost-" + std::to_string(i) +
        "\" aria-label=\"" + esc(name + " Monatskosten") + "\">" +
        esc(money(row.at("cost").get<int64_t>(), snapshot.world->manifest().value("currency", Json::object())) + " / Monat") + "</span>";
    if (i < 3)
      out += "<span class=\"environment-standing\">" + std::to_string(row.at("rank").get<int>()) + ". / " +
             std::to_string(row.at("league").get<int>()) + ". Liga</span>";
    out += "</div></section>";
  }
  out += "<div class=\"environment-emblem\"><div class=\"environment-ball\">" +
      img("art.environment-ball-v1.png", 400, 400, false, false, 640) + "</div>";
  const auto &managers = snapshot.state.at("managers");
  const auto managed = managers.is_array() && !managers.empty() ? managers[0].value("club", "") : "";
  const auto *club = snapshot.world->find("clubs", managed);
  out += "<div class=\"environment-crest\" aria-label=\"" + esc(club ? snapshot.world->name(*club) : "Vereinswappen nicht verfügbar") + "\">" +
      (club ? img(club->value("crest", ""), 162, 162, true) : "<span>—</span>") + "</div></div></div>";
  return out;
}
