// SPDX-License-Identifier: GPL-3.0-only
std::string App::render_desktop_tools(const std::string &key) {
  init_desktop();
  if (key == "background")
    return "<div class=\"center\" style=\"padding:90dp;\">" +
           img("background", 2200, 1240, false, true, 1800) +
           "<h2 style=\"margin-top:45dp;\">Stadion bei Nacht</h2><p "
           "class=\"muted\" style=\"margin-top:20dp;\">Aktueller "
           "Hintergrund</p></div>";
  if (key == "widgets") {
    std::string body =
        "<div class=\"list\" "
        "style=\"position:absolute;left:0;right:0;top:0;bottom:0;\">";
    size_t i = 0;
    for (const auto &definition : widget_definitions()) {
      const auto &o = desktop_["objects"].at(definition.id);
      body += "<div class=\"gallery-card panel\" style=\"left:" +
              std::to_string((i % 3) * 870) +
              "dp;top:" + std::to_string((i / 3) * 480) + "dp;\"><h2>" +
              esc(definition.name) + "</h2><p>";
      for (auto [w, h] : definition.sizes)
        body += std::to_string(w) + " × " + std::to_string(h) + " &nbsp; ";
      body += "</p>" +
              btn("widget-add",
                  o.value("visible", false) ? "Bereits auf dem Desktop"
                                            : "Hinzufügen",
                  {},
                  "data-id=\"" + definition.id +
                      "\" "
                      "style=\"position:absolute;left:30dp;right:30dp;bottom:"
                      "30dp;\" " +
                      (o.value("visible", false) ? "disabled" : "")) +
              "</div>";
      ++i;
    }
    return body + "</div>";
  }
  const bool saving = key == "view-save";
  const auto directory = user_dir() / "layouts";
  std::vector<std::string> names;
  if (fs::exists(directory))
    for (const auto &entry : fs::directory_iterator(directory))
      if (entry.is_regular_file() && entry.path().extension() == ".json")
        names.push_back(editor::utf8(entry.path().stem()));
  std::sort(names.begin(), names.end());
  std::string body = "<div class=\"panel list\" "
                     "style=\"position:absolute;left:0;top:0;width:1000dp;"
                     "bottom:150dp;padding:24dp;\">";
  if (!saving)
    body += btn("view-default", "Standardansicht · fixiert", "choice",
                "style=\"width:100%;height:120dp;text-align:left;margin-bottom:"
                "18dp;\"");
  for (const auto &name : names)
    body += btn("view-select", esc(name),
                std::string("choice ") +
                    (name == selections_["view"] ? "selected" : ""),
                "data-id=\"" + esc(name) +
                    "\" "
                    "style=\"width:100%;height:120dp;text-align:left;margin-"
                    "bottom:18dp;\"");
  body +=
      "</div><div class=\"panel\" "
      "style=\"position:absolute;left:1040dp;right:0;top:0;bottom:150dp;"
      "padding:50dp;\"><h2>" +
      std::string(saving                        ? "Aktuelle Ansicht speichern"
                  : selections_["view"].empty() ? "Ansicht auswählen"
                                                : esc(selections_["view"])) +
      "</h2>";
  if (!selections_["view"].empty()) {
    launch::ModLibrary::validate_name(selections_["view"]);
    auto preview = directory / editor::path(selections_["view"] + ".png");
    if (fs::is_regular_file(preview))
      body += "<img src=\"" + renderer_.local_image(preview, 1400).token +
              "\" style=\"width:1400dp;height:787.5dp;margin-top:70dp;\"/>";
  }
  if (saving)
    body += "<div class=\"field-row\" "
            "style=\"margin-top:70dp;\"><label>Bezeichnung</label><input "
            "id=\"view-name\" value=\"" +
            esc(selections_["view"].empty() ? "Meine Ansicht"
                                            : selections_["view"]) +
            "\"/></div>";
  body += "</div><div class=\"bottom-controls\">" +
          btn("view-delete", "Löschen", {},
              selections_["view"].empty() ? "disabled" : "") +
          btn("close", "Abbrechen") +
          btn(saving ? "view-save-confirm" : "view-load-confirm",
              saving ? "Speichern" : "Laden", "primary",
              !saving && selections_["view"].empty() ? "disabled" : "") +
          "</div>";
  return body;
}
