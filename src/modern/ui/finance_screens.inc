// SPDX-License-Identifier: GPL-3.0-only
std::string App::render_balance() {
  const auto rows = demo_balance_rows(snap().state, *world_);
  if (!rows.value("ok", false)) throw editor::Error("Die Bilanzdaten sind ungültig.");
  std::string out = "<div class=\"balance-native\">";
  if (balance_future_) {
    out += "<section class=\"balance-plan\"><h2>Ausgabenplanung</h2><div class=\"balance-plan-rows\">";
    int i = 0;
    for (const auto &row : rows.at("plan")) {
      const auto name = row.at("name").get<std::string>();
      out += "<div class=\"balance-plan-row\"><span>" + esc(name) + "</span>" +
          btn("balance-plan", esc(number(row.at("amount").get<int64_t>() / finance_currency_factor(*world_))),
              "balance-plan-value amount-trigger", "id=\"balance-plan-" + std::to_string(i) +
              "\" data-id=\"" + std::to_string(i) + "\" aria-label=\"" + esc(name + " in " + (money_unit() == "€" ? "Euro" : money_unit())) + "\"") + "</div>";
      ++i;
    }
    out += "</div></section><p class=\"balance-plan-total\"><span>Geplante Ausgaben</span><span>" +
        esc(cash(rows.at("plan_total").get<int64_t>())) + "</span></p>";
  } else {
    int index = 0;
    for (const auto &kind : {std::string("income"), std::string("costs")}) {
      out += "<section class=\"balance-panel\" style=\"left:" + std::to_string(index++ * 1322) +
          "dp;\"><h2>" + (kind == "income" ? "Einnahmen" : "Ausgaben") + "</h2><div class=\"balance-table\">"
          "<div class=\"balance-row balance-head\"><span>Position</span><span>Betrag</span></div>";
      int n = 0;
      for (const auto &row : rows.at(kind)) {
        out += "<div class=\"balance-row" + std::string(n++ % 2 == 0 ? " striped" : "") + "\"><span>" +
            esc(row.at("name").get<std::string>()) + "</span><span>" + esc(cash(row.at("amount").get<int64_t>())) + "</span></div>";
      }
      out += "</div></section>";
    }
    const std::array<std::string, 6> values = {
        number(rows.at("league_home_games").get<int64_t>()) + " Liga-Heimspiele",
        number(rows.at("other_home_games").get<int64_t>()) + " sonstige Heimspiele",
        "Zuschauerschnitt: " + number(rows.at("attendance_average").get<int64_t>()),
        "Eintrittspreis: " + cash(rows.at("ticket").get<int64_t>()),
        "Kontostand: " + cash(rows.at("cash").get<int64_t>()),
        "Saldo: " + cash(rows.at("balance").get<int64_t>())};
    for (int i = 0; i < 6; ++i)
      out += "<p class=\"balance-summary-item" + std::string(i == 5 ? " balance-net" : "") + "\" style=\"" +
          rect(26 + (i % 2) * 1310, 1526 + (i / 2) * 68, 1250, 38) + "\">" + esc(values[size_t(i)]) + "</p>";
  }
  out += btn("balance-toggle", "<span>" + std::string(balance_future_ ? "Zukunft" : "Aktuell") +
      "</span><span class=\"balance-cycle\" aria-hidden=\"true\"><span class=\"cycle-line cycle-top\"></span><span class=\"cycle-line cycle-bottom\"></span><span class=\"cycle-head cycle-right\"></span><span class=\"cycle-head cycle-left\"></span></span>", "balance-toggle report-reset",
      "id=\"balance-toggle\" aria-label=\"Zwischen aktueller Bilanz und Zukunftsplanung wechseln\"");
  return out + "</div>";
}
