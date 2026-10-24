// SPDX-License-Identifier: GPL-3.0-only
namespace {
const Json &contracts_field(const Json &object, const char *key) {
  static const Json absent;
  if (object.is_object()) {
    const auto found = object.find(key);
    if (found != object.end()) return *found;
  }
  return absent;
}
std::string contracts_text(const Json &value, const std::string &fallback = "—") {
  return value.is_string() && !value.get_ref<const std::string &>().empty()
             ? value.get<std::string>() : fallback;
}
std::string contracts_managed(const Snapshot &snapshot) {
  const auto &managers = contracts_field(snapshot.state, "managers");
  return managers.is_array() && !managers.empty()
             ? contracts_text(contracts_field(managers[0], "club"), "") : "";
}
Json contracts_projection(const Snapshot &snapshot) {
  if (!snapshot.world) return {{"ok", false}};
  try {
    return demo_contract_rows(snapshot.state, *snapshot.world, contracts_managed(snapshot));
  } catch (const std::exception &) { return {{"ok", false}}; }
}
Json contracts_row(const Json &projection, const std::string &id) {
  const auto &rows = contracts_field(projection, "rows");
  if (rows.is_array())
    for (const auto &row : rows)
      if (contracts_field(row, "id") == id) return row;
  return Json();
}
std::string contracts_number(const Json &value) {
  if (!value.is_number()) return "—";
  if (value.is_number_integer()) return number(value.get<int64_t>());
  const double n = value.get<double>();
  if (!std::isfinite(n)) return "—";
  std::ostringstream out;
  out << std::setprecision(6) << n;
  auto text = out.str();
  std::replace(text.begin(), text.end(), '.', ',');
  return text;
}
std::string contracts_money(const Json &value, const Catalog &world) {
  return value.is_number_integer()
      ? money(value.get<int64_t>(), world.manifest().value("currency", Json::object())) : "—";
}
std::string contracts_position_code(const std::string &code) {
  return code == "gk" ? "TW" : code == "def" ? "ABW"
      : code == "mid" ? "MIT" : code == "fwd" ? "STU" : code;
}
std::string contracts_position(const Json &row) {
  const auto &positions = contracts_field(row, "positions");
  std::string text;
  if (positions.is_array())
    for (const auto &value : positions) {
      if (!value.is_string() || value.get_ref<const std::string &>().empty()) continue;
      if (!text.empty()) text += " / ";
      text += contracts_position_code(value.get<std::string>());
    }
  return text.empty() ? contracts_position_code(contracts_text(contracts_field(row, "position"))) : text;
}
bool contracts_bound(const Snapshot &snapshot, uint64_t revision,
                     const std::shared_ptr<const Catalog> &world,
                     const std::string &club, const std::string &player = "") {
  if (!world || snapshot.world != world || snapshot.revision != revision ||
      contracts_managed(snapshot) != club) return false;
  const auto projection = contracts_projection(snapshot);
  if (!projection.value("ok", false) || contracts_field(projection, "club") != club) return false;
  return player.empty() || contracts_row(projection, player).is_object();
}
} // namespace

std::string App::render_contracts() {
  const auto snapshot = snap();
  const auto projection = contracts_projection(snapshot);
  const bool ready = projection.value("ok", false) && snapshot.world;
  const auto &rows = contracts_field(projection, "rows");
  const size_t count = rows.is_array() ? rows.size() : 0;
  if (contracts_tab_ < 0 || contracts_tab_ > 2) contracts_tab_ = 2;
  if (contracts_dialog_.empty() && contracts_player_.empty() && count)
    contracts_player_ = contracts_text(contracts_field(rows[0], "id"), "");
  const auto managed = contracts_managed(snapshot);
  const auto *own = snapshot.world ? snapshot.world->find("clubs", managed) : nullptr;
  const auto club_name = own ? snapshot.world->name(*own) : std::string("—");
  std::string output = "<div id=\"contracts-underlay\" class=\"contracts-overview\">"
      "<header class=\"contracts-heading\"><div class=\"contracts-heading-club\"><div class=\"contracts-club-label\">" +
      (own ? img(own->value("crest", ""), 58, 58, true) : "") +
      "<span>" + esc(club_name) + "</span></div><p>" + std::to_string(count) +
      " Spieler · Spieler anklicken, um einen Vertrag zu verhandeln</p></div>"
      "<div class=\"contracts-salary-total\"><span>Monatsgehälter</span><span class=\"contracts-total-value\">" +
      (ready ? esc(contracts_money(contracts_field(projection, "salary_total"), *snapshot.world)) : "—") +
      "</span></div></header><div id=\"contracts-roster-scroll\" class=\"contracts-roster\">"
      "<div class=\"contracts-table\" role=\"table\"><div class=\"contracts-row contracts-table-head\" role=\"row\">";
  const std::array<std::vector<std::pair<std::string, std::string>>, 3> headings = {{
      {{"name", "Spieler"}, {"position", "Pos."}, {"condition", "Kond."},
       {"technique", "Technik"}, {"form", "Form"}, {"strength", "Stärke"}, {"age", "Alter"}},
      {{"name", "Spieler"}, {"age", "Alter"}, {"shoot", "Schuss"}, {"pass", "Pass"},
       {"team", "Team"}, {"games", "Spiele"}, {"grade", "Note"}},
      {{"name", "Spieler"}, {"position", "Pos."}, {"strength", "Stärke"}, {"age", "Alter"},
       {"salary", "Gehalt / Monat"}, {"months", "Vertrag"}, {"insurance", "Versicherung"}}
  }};
  const auto &columns = headings[size_t(contracts_tab_)];
  for (size_t column = 0; column < columns.size(); ++column)
    output += "<div class=\"contracts-cell c" + std::to_string(column) +
              "\" role=\"columnheader\"><span>" + esc(columns[column].second) + "</span></div>";
  output += "</div>";
  if (rows.is_array())
    for (size_t i = 0; i < rows.size(); ++i) {
      const auto &row = rows[i];
      const auto id = contracts_text(contracts_field(row, "id"), "");
      const auto name = contracts_text(contracts_field(row, "name"));
      output += "<div class=\"contracts-row " + std::string(i % 2 == 0 ? "striped " : "") +
                (contracts_player_ == id ? "selected" : "") + "\" role=\"row\" data-player=\"" + esc(id) + "\">";
      for (size_t column = 0; column < columns.size(); ++column) {
        const auto key = columns[column].first;
        const auto source = contracts_text(contracts_field(row, "source_label"), "");
        output += "<div class=\"contracts-cell c" + std::to_string(column) +
                  "\" role=\"cell\" title=\"" + esc(key == "salary" ? source : "") + "\">";
        if (column == 0)
          output += btn("contract-player", "<span class=\"contract-player-rank\">" +
                        std::to_string(i + 1) + ".</span><span class=\"contract-player-name\" title=\"" +
                        esc(name) + "\">" + esc(name) + "</span>", "contract-player-button",
                        "id=\"contract-player-" + esc(id) + "\" data-id=\"" + esc(id) +
                        "\" aria-haspopup=\"dialog\" aria-pressed=\"" + (contracts_player_ == id ? "true" : "false") + "\"");
        else {
          std::string value;
          if (key == "position") value = contracts_position(row);
          else if (key == "salary") value = ready ? contracts_money(contracts_field(row, "salary"), *snapshot.world) : "—";
          else if (key == "months") value = contracts_number(contracts_field(row, "months")) + " Monate";
          else if (key == "insurance") {
            const auto &insurance = contracts_field(row, "insurance");
            value = insurance.is_number_integer() ? insurance.get<int>() == 0 ? "Ohne"
                      : contracts_number(insurance) + " Monate" : "—";
          } else value = contracts_number(contracts_field(row, key.c_str()));
          output += "<span class=\"contract-value\">" + esc(value) + "</span>";
        }
        output += "</div>";
      }
      output += "</div>";
    }
  output += "</div>";
  if (!ready || count == 0)
    output += "<p class=\"contracts-empty\">" + std::string(ready
        ? "Für diesen Verein sind keine Spieler im Katalog vorhanden."
        : "Die Vertragsvorschau ist derzeit nicht verfügbar.") + "</p>";
  output += "</div><nav class=\"contracts-actions\">";
  const std::array<const char *, 3> tabs = {"Daten A", "Daten B", "Gehalt"};
  for (int tab = 0; tab < 3; ++tab)
    output += btn("contracts-tab", "<span>" + std::string(tabs[size_t(tab)]) + "</span>",
        "contract-control " + std::string(contracts_tab_ == tab ? "selected" : ""),
        "id=\"contracts-tab-" + std::to_string(tab) + "\" data-id=\"" + std::to_string(tab) +
        "\" aria-pressed=\"" + (contracts_tab_ == tab ? "true" : "false") + "\"");
  output += btn("contracts-bonuses", "<span>Team-Prämien</span>", "contract-control bonuses",
                "id=\"contracts-bonuses\" " + std::string(ready ? "" : "disabled")) + "</nav></div>";
  if (contracts_dialog_.empty()) return output;

  const auto &draft_row = contracts_field(contract_draft_, "row");
  const auto player_name = contracts_text(contracts_field(draft_row, "name"));
  const auto &world = contracts_world_ ? contracts_world_ : snapshot.world;
  const bool offer = contracts_dialog_ == "offer", insurance = contracts_dialog_ == "insurance";
  std::string title = offer ? "Vertragsverhandlung · " + player_name
      : insurance ? "Spielerversicherung · " + player_name
      : contracts_dialog_ == "bonuses" ? "Team-Prämien"
      : contracts_text(contracts_field(contract_draft_, "notice_title"), "Vertragsvorschau");
  std::string body, actions;
  const auto close = [&](const std::string &caption) {
    return btn("contract-close", "<span>" + esc(caption) + "</span>", "contract-control",
               "id=\"contract-close\"");
  };
  if (offer) {
    const auto &salary = contracts_field(draft_row, "salary");
    const auto &proposed = contracts_field(contract_draft_, "salary");
    const auto &months = contracts_field(contract_draft_, "months");
    const auto source = contracts_text(contracts_field(draft_row, "source_label"));
    const int64_t factor = world ? finance_currency_factor(*world) : 1;
    // Fictional asking price follows the HTML preview, never an engine demand.
    const Json demand = salary.is_number_integer()
        ? Json(((salary.get<int64_t>() / factor * 11 + 5) / 10) * factor) : Json();
    body = "<p class=\"contract-player-summary\">" + esc(contracts_position(draft_row)) +
        " · Stärke " + esc(contracts_number(contracts_field(draft_row, "strength"))) +
        " · " + esc(contracts_number(contracts_field(draft_row, "age"))) + " Jahre</p>"
        "<div class=\"contract-scenes\"><div class=\"contract-scene\">" + img("contracts.scene.0", 955, 291.0625) +
        "</div><div class=\"contract-scene\">" + img("contracts.scene.1", 955, 291.0625) + "</div></div>"
        "<div class=\"contract-amounts\"><div class=\"contract-amount-card\" title=\"" + esc(source) +
        "\"><span class=\"contract-amount-label\">Aktuelles Monatsgehalt</span><span class=\"contract-amount-value\">" +
        (world ? esc(contracts_money(salary, *world)) : "—") +
        "</span></div><div class=\"contract-amount-card\"><span class=\"contract-amount-label\">Fiktive Forderung</span>"
        "<span class=\"contract-amount-value\">" + (world ? esc(contracts_money(demand, *world)) : "—") +
        "</span></div></div><div class=\"contract-duration-heading\"><span>Laufzeit</span><span class=\"contract-duration-value\">" +
        esc(contracts_number(months)) + " Monate</span></div><nav class=\"contract-duration\" aria-label=\"Vertragslaufzeit\">";
    for (const int term : {12, 24, 36, 48})
      body += btn("contract-term", "<span>" + std::to_string(term / 12) +
          (term == 12 ? " Jahr" : " Jahre") + "</span>", "contract-control " +
          std::string(months == term ? "selected" : ""), "id=\"contract-term-" + std::to_string(term) +
          "\" data-id=\"" + std::to_string(term) + "\" aria-pressed=\"" + (months == term ? "true" : "false") + "\"");
    body += "</nav><div class=\"contract-salary\"><div class=\"contract-salary-label\"><span>Dein Angebot pro Monat</span>"
        "<span class=\"contract-salary-hint\">Betrag über die gemeinsame Zahleneingabe ändern</span></div>" +
        btn("contract-salary", "<span>" + (world && proposed.is_number_integer()
            ? esc(std::to_string(proposed.get<int64_t>())) : "—") + "</span>",
            "contract-salary-input amount-trigger", "id=\"contract-salary\" aria-haspopup=\"dialog\" aria-label=\"Angebotenes Monatsgehalt ändern\"") +
        "</div><p class=\"contract-draft-note\">Erst „Angebot machen“ übernimmt Gehalt und Laufzeit.</p>";
    actions = close("Abbrechen") +
        btn("contract-insurance", "<span>Versichern</span>", "contract-control", "id=\"contract-insurance\"") +
        btn("contract-submit", "<span>Angebot machen</span>", "contract-control contract-submit",
            "id=\"contract-submit\" " + std::string(proposed.is_number_integer() ? "" : "disabled"));
  } else if (insurance) {
    const auto current = contracts_field(draft_row, "insurance");
    body = "<p class=\"contract-insurance-prompt\">Wähle die Versicherungsdauer für diesen Spieler.</p>"
           "<nav class=\"contract-insurance-options\">";
    for (const int duration : {0, 12, 24})
      body += btn("contract-insure", "<span class=\"contract-insurance-title\">" +
          (duration ? std::to_string(duration) + " Monate" : "Ohne") +
          "</span><span class=\"contract-insurance-caption\">" +
          (duration ? "Versicherungslaufzeit" : "Keine Versicherung") + "</span>",
          "contract-control " + std::string(current == duration ? "selected" : ""),
          "id=\"contract-insure-" + std::to_string(duration) + "\" data-id=\"" +
              std::to_string(duration) + "\" aria-pressed=\"" + (current == duration ? "true" : "false") + "\"");
    body += "</nav>";
    actions = close("Abbrechen");
  } else if (contracts_dialog_ == "bonuses") {
    body = "<p class=\"contract-player-summary\">Prämienvereinbarung für die gesamte Mannschaft</p>"
           "<div class=\"contract-bonuses\">";
    const auto &bonuses = contracts_field(contract_draft_, "bonuses");
    const std::array<const char *, 7> names = {"Torprämie", "Punktprämie", "Auflaufprämie", "Meisterprämie",
        "Prämie Pl. 2–5", "Prämie Pl. 6–10", "Nichtabstiegsprämie"};
    for (size_t i = 0; i < names.size(); ++i) {
      const Json bonus = bonuses.is_array() && i < bonuses.size() ? bonuses[i] : Json();
      body += "<div class=\"contract-bonus-row\"><span>" + esc(contracts_text(contracts_field(bonus, "name"), names[i])) + "</span>" +
          btn("contract-bonus", "<span>" + esc(contracts_number(contracts_field(bonus, "value"))) + "</span>",
              "contract-control amount-trigger", "id=\"contract-bonus-" + std::to_string(i) + "\" data-id=\"" +
                  std::to_string(i) + "\" aria-haspopup=\"dialog\"") + "</div>";
    }
    body += "</div>";
    actions = close("Zurück");
  } else {
    body = "<p class=\"contract-notice-text\">" + esc(contracts_notice_) + "</p>";
    actions = close("Verstanden");
  }
  return output + "<div id=\"contracts-layer\" class=\"contracts-layer\"><section id=\"contracts-dialog\" "
      "class=\"contracts-dialog contracts-dialog-" + esc(contracts_dialog_) +
      "\" role=\"dialog\" aria-modal=\"true\" aria-labelledby=\"contract-dialog-title\">"
      "<h2 id=\"contract-dialog-title\">" + esc(title) + "</h2>" + body +
      "<nav class=\"contract-dialog-actions\">" + actions + "</nav></section></div>";
}
