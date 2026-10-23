// SPDX-License-Identifier: GPL-3.0-only
std::string App::training_balls(const std::string &group, int index, int value,
                                bool automatic, const std::string &player,
                                int size, int gap, bool overtraining) {
  std::string body =
      "<div class=\"training-balls\" style=\"gap:" + std::to_string(gap) +
      "dp;\">";
  for (int i = 1; i <= 3; ++i)
    body += btn(
        "training-ball", img("training.ball", size, size, false, false, 128),
        std::string("training-ball ") +
            (i <= value ? "is-selected" : "is-empty") +
            (overtraining && i <= value ? " overtraining" : ""),
        "id=\"training-ball-" + group + "-" + std::to_string(index) + "-" +
            std::to_string(i) + "\" data-group=\"" + group + "\" data-id=\"" +
            std::to_string(index) + "\" data-player=\"" + esc(player) +
            "\" data-value=\"" + std::to_string(i) + "\" style=\"width:" +
            std::to_string(size) + "dp;height:" + std::to_string(size) +
            "dp;\" " + (automatic ? "disabled" : ""));
  return body + "</div>";
}
std::string App::render_training() {
  const auto s = snap();
  const auto &t = s.state.at("training"), &automatic = t.at("autos");
  const std::array<const char *, 9> skills = {
      "Kondition", "Technik", "Eckball",       "Freistoß", "Zweikampf",
      "Pass",      "Schuss",  "Schnelligkeit", "Teamplay"};
  const std::vector<std::pair<std::string, std::string>> focuses = {
      {"0", "Allgemein"},     {"1", "Eckball"}, {"2", "Freistoß"},
      {"3", "Zweikampf"},     {"4", "Pass"},    {"5", "Schuss"},
      {"6", "Schnelligkeit"}, {"7", "Teamplay"}};
  int skill_total = 0, type_total = 0;
  for (size_t i = 2; i < 9; ++i)
    skill_total += t["areas"][i].get<int>();
  for (const auto &v : t["types"])
    type_total += v.get<int>();
  std::string body = "<div class=\"training-skills\">";
  for (size_t i = 0; i < 9; ++i)
    body +=
        "<div class=\"training-skill\"><div style=\"width:86dp;\">" +
        img("training.skill." + std::to_string(i), 80, 80, false, false, 160) +
        "</div><span style=\"width:677dp;\">" + skills[i] +
        "</span><span style=\"width:35dp;\">:</span><div "
        "style=\"width:220dp;\">" +
        training_balls("area", int(i), t["areas"][i].get<int>(),
                       automatic[i < 2 ? 1 : 2].get<bool>(), {}, 56, 10,
                       i >= 2 && skill_total > 14) +
        "</div></div>";
  body += "</div><div class=\"training-pitch\">";
  for (int i = 0; i < 10; ++i)
    body += "<div style=\"position:absolute;left:0;right:0;top:" +
            std::to_string(i * 43) + "dp;height:43dp;background-color:" +
            std::string(i % 2 ? "#306b4c" : "#2d6447") + ";\"></div>";
  body += "<div class=\"training-midline\"></div><div "
          "class=\"training-circle\"></div><div class=\"training-box "
          "left-box\"></div><div class=\"training-box right-box\"></div>";
  for (int i = 0; i < 4; ++i)
    body += "<div class=\"training-type-cell\" style=\"left:" +
            std::to_string(30 + (i % 2) * 277) +
            "dp;top:" + std::to_string(26 + (i / 2) * 176) + "dp;\">" +
            training_balls("type", i, t["types"][size_t(i)].get<int>(),
                           automatic[0].get<bool>(), {}, 48, 5) +
            "</div>";
  body += "</div>";
  const std::array<const char *, 4> auto_names = {
      "AUTO · Arten", "AUTO · Kondition", "AUTO · Fertigkeiten",
      "AUTO · Spieler"};
  const std::array<std::array<int, 4>, 4> auto_boxes = {
      {{0, 1456, 570, 76},
       {604, 1040, 440, 76},
       {604, 1136, 440, 76},
       {2312, 1803, 300, 61}}};
  for (size_t i = 0; i < 4; ++i) {
    const auto a = auto_boxes[i];
    body += btn(
        "training-auto", auto_names[i],
        std::string(i == 3 ? "training-auto auto-players " : "training-auto ") +
            (automatic[i].get<bool>() ? "primary" : ""),
        "id=\"training-auto-" + std::to_string(i) + "\" data-id=\"" +
            std::to_string(i) + "\" style=\"position:absolute;" +
            rect(a[0], a[1], a[2], a[3]) + "\"");
  }
  const bool all_auto = std::all_of(automatic.begin(), automatic.end(),
                                    [](const auto &v) { return v == true; });
  body += btn("training-auto-all", "Automatisch",
              std::string("training-auto ") + (all_auto ? "primary" : ""),
              "id=\"training-auto-all\" style=\"position:absolute;" +
                  rect(604, 1232, 440, 76) + "\"");
  body += "<p class=\"training-budget\">Arten: " + std::to_string(type_total) +
          " / 8 Bälle ·<br/>Kondition/Technik: " +
          std::to_string(t["areas"][0].get<int>() + t["areas"][1].get<int>()) +
          " / 4 ·<br/>Fertigkeiten: " + std::to_string(skill_total) + " / 18" +
          (skill_total > 14 ? " · hohe<br/>Belastung" : "") +
          "</p><div id=\"training-roster\" class=\"training-roster\"><div "
          "class=\"training-row "
          "training-header\">";
  const std::array<const char *, 5> headings = {"Pos.", "Spieler", "Stärke",
                                                "Schwerpunkt", "Intensität"};
  const std::array<int, 5> widths = {90, 300, 95, 510, 497};
  for (size_t i = 0; i < 5; ++i)
    body += "<div style=\"width:" + std::to_string(widths[i]) + "dp;\">" +
            headings[i] + "</div>";
  body += "</div>";
  const auto players = world_->squad(t.value("club", ""));
  for (size_t i = 0; i < players.size(); ++i) {
    const auto *p = players[i];
    const auto id = p->value("id", "");
    const auto &row = t["players"].at(id);
    const auto position = p->value("positions", Json::array());
    const auto pos = position.empty() ? "" : position[0].get<std::string>();
    body +=
        "<div class=\"training-row " + std::string(i % 2 ? "striped" : "") +
        "\"><div class=\"training-position\" style=\"width:90dp;\">" +
        std::string(pos == "gk"    ? "TW"
                    : pos == "def" ? "ABW"
                    : pos == "mid" ? "MIT"
                    : pos == "fwd" ? "STU"
                                   : pos) +
        "</div><div class=\"training-name\" style=\"width:300dp;\" title=\"" +
        esc(world_->name(*p)) + "\">" + esc(world_->name(*p)) +
        "</div><div style=\"width:95dp;\">" +
        std::to_string(
            p->value("attributes", Json::object()).value("strength", 0)) +
        "</div><div style=\"width:510dp;\">";
    auto selector =
        select("training-focus-" + std::to_string(i), focuses,
               std::to_string(row.value("focus", 0)), "training-focus");
    const auto end = selector.find('>');
    selector.insert(end, " data-player=\"" + esc(id) + "\" " +
                             (automatic[3] == true ? "disabled" : ""));
    body += selector + "</div><div style=\"width:497dp;\">" +
            training_balls("player", int(i), row.value("intensity", 0),
                           automatic[3].get<bool>(), id, 50, 9) +
            "</div></div>";
  }
  return body + "</div>";
}
std::string App::render_tactics() {
  const auto t = snap().state.at("tactics");
  const std::array<const char *, 8> names = {
      "Defensiv / offensiv",    "Pass-/Schussbild unten",
      "Pass-/Schussbild Mitte", "Pass-/Schussbild links",
      "Flache / hohe Bälle",    "Flügelbild links",
      "Flügelbild rechts",      "Einsatz"};
  const std::array<int, 8> limits = {3, 3, 3, 3, 1, 1, 1, 11};
  const std::array<std::array<int, 4>, 7> areas = {{{19, 9, 104, 62},
                                                    {61, 108, 50, 34},
                                                    {63, 84, 54, 30},
                                                    {26, 95, 24, 24},
                                                    {168, 53, 71, 34},
                                                    {137, 10, 58, 37},
                                                    {205, 10, 58, 37}}};
  std::string body =
      "<div class=\"tactics-board\"><div class=\"tactics-surface\"></div>" +
      picture(renderer_.diagram("tactics", tactics_diagram(t, theme_)), 2300,
              1864);
  for (size_t i = 0; i < 7; ++i) {
    const auto a = areas[i];
    body += btn("tactic-cycle", "", "tactic-hotspot",
                "id=\"tactic-cycle-" + std::to_string(i) + "\" data-id=\"" +
                    std::to_string(i) + "\" aria-label=\"" + esc(names[i]) +
                    "\" style=\"position:absolute;" +
                    rect(a[0] / 295.0 * 2300, a[1] / 206.0 * 1864,
                         a[2] / 295.0 * 2300, a[3] / 206.0 * 1864) +
                    "z-index:" + std::to_string(8 - i) + ";\"");
  }
  const int effort = t["values"][7].get<int>();
  body +=
      "<div class=\"tactic-effort\"><div "
      "class=\"tactic-effort-label\"><span>EINSATZ</span><span "
      "id=\"tactic-effort-value\" class=\"tactic-effort-marks\">" +
      std::string(size_t(effort), '!') +
      "</span></div><input type=\"range\" id=\"tactic-effort\" "
      "data-input=\"tactic-effort\" min=\"0\" max=\"11\" step=\"1\" value=\"" +
      std::to_string(effort) +
      "\"/></div><p id=\"tactic-hint\" class=\"tactic-hint\">" +
      esc(names[size_t(last_tactic_)]) + ": " +
      std::to_string(t["values"][size_t(last_tactic_)].get<int>()) + " / " +
      std::to_string(limits[size_t(last_tactic_)]) + "</p></div>";
  return body;
}
