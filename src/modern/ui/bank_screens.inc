// SPDX-License-Identifier: GPL-3.0-only
std::string App::render_bank() {
  const auto snapshot = snap();
  const auto assets = demo_bank_assets(*world_);
  const auto summary = demo_bank_summary(snapshot.state, *world_);
  if (!summary.value("ok", false)) return {};
  const auto &finance = snapshot.state.at("finance");
  bank_property_ = std::clamp(bank_property_, 0, 5);
  bank_stock_ = std::clamp(bank_stock_, 0, 11);
  bank_owned_ = std::clamp(bank_owned_, 0, 5);
  if (finance.at("properties")[size_t(bank_owned_)] == 0)
    for (int i = 0; i < 6; ++i)
      if (finance.at("properties")[size_t(i)] != 0) { bank_owned_ = i; break; }
  const auto actions = [&](const char *left, const char *lk, const char *right, const char *rk) {
    return "<nav class=\"bank-actions\">" +
        btn("bank-calc", left, "bank-control", "id=\"bank-" + std::string(lk) + "\" data-kind=\"" + lk + "\"") +
        btn("bank-calc", right, "bank-control", "id=\"bank-" + std::string(rk) + "\" data-kind=\"" + rk + "\"") + "</nav>";
  };
  std::string out = "<div class=\"bank-page\"><div class=\"bank-scene\">" +
      img("art.bank-hall-v1.png", 2608, 1466.016746, false, false, 3000);
  if (bank_view_ == "entrance") {
    struct Service { const char *id, *label, *hint; double x, y, w, h; };
    const std::array<Service, 5> services{{
        {"properties", "Immobilien", "Immobilien – Gebäude ansehen, kaufen oder verkaufen.", .5, 18.5, 17.5, 43.5},
        {"stocks", "Aktien", "Aktien – Kurse und Bestände ansehen, Aktien kaufen oder verkaufen.", 30, 30, 13.5, 19.5},
        {"overview", "Finanzübersicht", "Finanzübersicht – Konto, Sparbuch, Immobilien, Aktien und Kredit im Überblick.", 30.5, 49.5, 13, 14.5},
        {"savings", "Sparbuch", "Sparbuch – Guthaben ansehen, Geld einzahlen oder abheben.", 85, 60, 6.5, 7.5},
        {"credit", "Kredit", "Kredit – Laufzeit wählen, einen Kredit aufnehmen oder tilgen.", 85.3, 31.5, 13.7, 27.5}}};
    out += "<div class=\"bank-hotspots\">";
    for (const auto &s : services)
      out += btn("bank-view", "", "bank-hotspot", "id=\"bank-view-" + std::string(s.id) +
          "\" data-view=\"" + s.id + "\" data-bank-hint=\"" + esc(s.hint) + "\" aria-label=\"" + s.label +
          "\" style=\"left:" + scalar(s.x) + "%;top:" + scalar(s.y) + "%;width:" + scalar(s.w) + "%;height:" + scalar(s.h) + "%;\"");
    out += "</div>";
  }
  out += "</div>";
  if (bank_view_ == "entrance")
    return out + "<p id=\"bank-interaction-hint\" role=\"status\" aria-live=\"polite\"><span>" +
        esc(bank_hint_.empty() ? "Ein Element in der Bankhalle auswählen." : bank_hint_) + "</span></p></div>";
  out += "<section class=\"bank-overlay " + esc(bank_view_) + "-overlay\">";
  if (bank_view_ == "savings") {
    out += "<div class=\"passbook\">" + img("art.bank-passbook-v1.png", 2100, 1293.109375, false, false, 2400) +
        "<div class=\"passbook-left\"><h2>Waldpark Bank</h2><h3>Sparbuch</h3><p class=\"passbook-note-one\">Bitte beachten Sie:</p>"
        "<p class=\"passbook-note-two\">Einzahlungen und Abhebungen werden hier als fiktive Vorschau verbucht.</p>"
        "<p class=\"passbook-note-three\">2,5 % Beispielzins</p></div><div class=\"passbook-right\"><h2>Guthaben</h2><output>" +
        esc(cash(snapshot.state.at("deposit").get<int64_t>())) + "</output></div></div>" + actions("Einzahlen", "deposit", "Abheben", "withdraw");
  } else if (bank_view_ == "properties") {
    out += "<h2>Immobilien</h2>";
    const auto &owned = assets.at("properties")[size_t(bank_owned_)], &offer = assets.at("properties")[size_t(bank_property_)];
    const auto count = finance.at("properties")[size_t(bank_owned_)].get<int64_t>();
    out += "<div class=\"property-row owned-property\"><div class=\"property-description\"><h3>Ihre Immobilien</h3>" +
        (count ? "<p>" + std::to_string(count) + " × " + esc(owned.at("name").get<std::string>()) + "</p><p>Wert: " +
                    esc(cash(count * owned.at("price").get<int64_t>())) + "</p>" : "<p>Keine Immobilien im Bestand</p>") +
        "</div>" + btn("property-owned-next", count ? img("bank.property." + std::to_string(bank_owned_), 996, 566, false, false, 1200)
            : "<span class=\"property-empty\"></span>", "property-picture", "id=\"property-owned-next\" aria-label=\"Nächste eigene Immobilie\" " + std::string(count ? "" : "disabled")) + "</div>";
    out += "<div class=\"property-row offered-property\"><div class=\"property-description\"><h3>Kaufangebot</h3><p>" +
        esc(offer.at("name").get<std::string>()) + "</p><p>Kaufpreis: " + esc(cash(offer.at("price").get<int64_t>())) +
        "</p></div>" + btn("property-next", img("bank.property." + std::to_string(bank_property_), 996, 566, false, false, 1200),
        "property-picture", "id=\"property-next\" aria-label=\"Nächstes Immobilienangebot\"") + "</div>" +
        actions("Kaufen", "property-buy", "Verkaufen", "property-sell");
  } else if (bank_view_ == "stocks") {
    out += "<h2>Aktien</h2><div class=\"stock-table\"><div class=\"stock-row stock-head\">";
    for (const auto *label : {"Wert", "Kurs", "Vortag", "Bestand"}) out += "<div><span>" + std::string(label) + "</span></div>";
    out += "</div>";
    const auto factor = finance_currency_factor(*world_);
    for (int i = 0; i < 12; ++i) {
      const auto &s = assets.at("stocks")[size_t(i)];
      out += "<div class=\"stock-row\"><div>" + btn("bank-stock", esc(s.at("name").get<std::string>()), "stock-name",
          "id=\"bank-stock-" + std::to_string(i) + "\" data-bank-stock=\"" + std::to_string(i) + "\" data-id=\"" + std::to_string(i) +
          "\" aria-pressed=\"" + (i == bank_stock_ ? "true" : "false") + "\"") + "</div><div><span>" +
          number(s.at("price").get<int64_t>() / factor) + "</span></div><div><span>" + number(s.at("previous").get<int64_t>() / factor) +
          "</span></div><div><span>" + number(finance.at("stocks")[size_t(i)].get<int64_t>()) + "</span></div></div>";
    }
    const double price = assets.at("stocks")[size_t(bank_stock_)].at("price").get<double>() / factor;
    std::array<double, 30> history{};
    for (int i = 0; i < 30; ++i) history[size_t(i)] = price * (.85 + .08 * std::sin(i * .6 + bank_stock_) + i * .003);
    const double lo = *std::min_element(history.begin(), history.end()) * .95, hi = *std::max_element(history.begin(), history.end()) * 1.05;
    std::string points;
    for (int i = 0; i < 30; ++i) points += scalar(20 + i * 690. / 29) + "," + scalar(900 - (history[size_t(i)] - lo) / (hi - lo) * 830) + " ";
    const auto stroke = theme_ == "silver" ? "#526779" : theme_ == "matchday" ? "#b7b4b9" : "#9ac7d8";
    const auto axis = theme_ == "silver" ? "#526779" : theme_ == "matchday" ? "#b7b4b9" : "#6c8798";
    const auto svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"740\" height=\"1320\" viewBox=\"0 0 740 950\">"
        "<path d=\"M10 10V930H730\" fill=\"none\" stroke=\"" + std::string(axis) + "\" stroke-width=\"5\"/>"
        "<polyline points=\"" + points + "\" fill=\"none\" stroke=\"" + stroke + "\" stroke-width=\"9\"/></svg>";
    out += "</div><div class=\"stock-chart\" role=\"img\" aria-label=\"Erfundener Kursverlauf für " +
        esc(assets.at("stocks")[size_t(bank_stock_)].at("name").get<std::string>()) + "\">" +
        picture(renderer_.diagram("bank-stock", svg, 1600), 740, 1320) + "</div>" + actions("Kaufen", "stock-buy", "Verkaufen", "stock-sell");
  } else if (bank_view_ == "credit") {
    out += "<h2>Kredit</h2><div class=\"credit-portrait\">" + img("art.bank-adviser-v1.png", 620, 515, false, false, 1000) +
        "</div><div class=\"credit-facts\">";
    const auto fact = [&](const char *label, const std::string &value, int row) {
      constexpr std::array<int, 5> y{0, 83, 166, 273, 356};
      return "<div class=\"credit-fact\" style=\"top:" + std::to_string(y[size_t(row)]) + "dp;\"><span>" + label + "</span><strong>" + value + "</strong></div>";
    };
    out += fact("Restschuld", esc(cash(summary.at("debt").get<int64_t>())), 0) + fact("Beispielzins", "8 % pro Jahr", 1);
    std::string select = "<select id=\"bank-term\" aria-label=\"Kreditlaufzeit\" " + std::string(summary.at("debt") == 0 ? "" : "disabled") + ">";
    for (const auto &term : demo_bank_terms()) select += "<option value=\"" + std::to_string(term.get<int>()) + "\" " +
        std::string(term == finance.at("term") ? "selected" : "") + ">" + std::to_string(term.get<int>()) + " Monate</option>";
    out += fact("Laufzeit", select + "</select>", 2) + fact("Monatsrate, Beispiel", esc(cash(summary.at("monthly_rate").get<int64_t>())), 3) +
        fact("Maximaler Kredit", esc(cash(5000000 * finance_currency_factor(*world_))), 4) + "</div>";
    const int term = finance.at("term");
    const auto principal = summary.at("schedule_principal").get<int64_t>();
    const auto color = theme_ == "silver" ? "#233443" : theme_ == "matchday" ? "#efeded" : "#a8c0cf";
    const auto accent = theme_ == "silver" ? "#25617a" : theme_ == "matchday" ? "#efb2aa" : "#a9dfd1";
    const auto grid_color = theme_ == "silver" ? "#58738c38" : "#b7d0de24";
    std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"2240\" height=\"420\" viewBox=\"0 0 2240 420\">"
        "<defs><linearGradient id=\"fill\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" stop-color=\"#9ddbd1\" stop-opacity=\".23\"/>"
        "<stop offset=\"1\" stop-color=\"#9ddbd1\" stop-opacity=\".01\"/></linearGradient></defs>";
    for (int i = 0; i < 5; ++i) {
      const auto y = 30 + i * 77.5;
      svg += "<line x1=\"220\" y1=\"" + scalar(y) + "\" x2=\"2180\" y2=\"" + scalar(y) + "\" stroke=\"" + grid_color + "\" stroke-width=\"2\"/>"
          "<text x=\"184\" y=\"" + scalar(y + 9) + "\" text-anchor=\"end\" fill=\"" + color + "\" font-size=\"28\" font-family=\"Arial, Liberation Sans, sans-serif\">" +
          esc(cash((principal * (4 - i) + 2) / 4)) + "</text>";
    }
    const int interval = term <= 12 ? 3 : term <= 24 ? 6 : 12;
    std::vector<int> labels;
    for (int month = 0; month <= term; month += interval) labels.push_back(month);
    if (labels.back() != term) labels.push_back(term);
    for (const int month : labels) {
      svg += "<text x=\"" + scalar(220 + double(month) / term * 1960) + "\" y=\"394\" text-anchor=\"" +
          std::string(month == 0 ? "start" : month == term ? "end" : "middle") + "\" fill=\"" + color +
          "\" font-size=\"28\" font-family=\"Arial, Liberation Sans, sans-serif\">" + (month == 0 ? "Heute" : std::to_string(month) + (month == term ? " Monate" : "")) + "</text>";
    }
    std::string points;
    for (const auto &point : summary.at("schedule")) points += scalar(220 + point.at("month").get<double>() / term * 1960) + "," +
        scalar(340 - point.at("remaining").get<double>() / principal * 310) + " ";
    svg += "<path d=\"M220 340 L220 30 L2180 340 Z\" fill=\"url(#fill)\"/><polyline points=\"" + points + "\" fill=\"none\" stroke=\"" +
        accent + "\" stroke-width=\"6\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><circle cx=\"220\" cy=\"30\" r=\"7\" fill=\"#c1ecdf\" stroke=\"#294553\" stroke-width=\"4\"/>"
        "<circle cx=\"2180\" cy=\"340\" r=\"7\" fill=\"#c1ecdf\" stroke=\"#294553\" stroke-width=\"4\"/></svg>";
    out += "<figure class=\"credit-schedule\"><div class=\"credit-schedule-caption\"><h3>Restschuld über die Laufzeit</h3><p class=\"credit-schedule-amount\">" +
        esc(cash(principal)) + " <span>→</span> " + esc(cash(0)) + "</p><div class=\"credit-schedule-context\"><span>" + std::to_string(term) +
        " Monate</span><p>" + (summary.at("schedule_example") == true ? "Beispiel · kein laufender Kredit" : "Aktueller Kredit") +
        "</p></div></div><div class=\"credit-chart\" role=\"img\" aria-label=\"Restschuld über " + std::to_string(term) +
        " Monate bei gleichmäßiger Tilgung ohne Zinsen\">" + picture(renderer_.diagram("bank-credit", svg, 2400), 2432, 420) +
        "</div><p class=\"credit-schedule-note\">Gleichmäßige monatliche Tilgung · Restschuld ohne Zinsen</p></figure>"
        "<p class=\"bank-note credit-note\">Fiktiver Kredit. Im POC erfolgen keine Zins- oder Ratenbuchungen.</p>" + actions("Aufnehmen", "loan", "Tilgen", "repay");
  } else if (bank_view_ == "overview") {
    out += "<h2>Finanzübersicht</h2><div class=\"bank-overview\">";
    const std::array<std::pair<const char *, int64_t>, 6> fields{{
        {"Konto", summary.at("cash")}, {"Sparbuch", summary.at("deposit")},
        {"Immobilien", summary.at("properties")}, {"Aktien", summary.at("stocks")},
        {"Kredit", -summary.at("debt").get<int64_t>()}, {"Saldo", summary.at("net")}}};
    for (size_t i = 0; i < fields.size(); ++i)
      out += "<div class=\"bank-overview-row " + std::string(i == 5 ? "bank-overview-total" : "") + "\"><span>" +
          fields[i].first + "</span><strong>" + (i == 4 ? "-" + esc(cash(summary.at("debt").get<int64_t>())) : esc(cash(fields[i].second))) + "</strong></div>";
    out += "</div><p class=\"bank-note overview-note\">Erfundene Bestände · Änderungen betreffen ausschließlich die Bankvorschau.</p>";
  }
  return out + "</section></div>";
}
