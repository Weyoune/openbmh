// SPDX-License-Identifier: GPL-3.0-only
std::string App::render_advertising() {
  const auto snapshot = snap();
  const auto view = demo_advertising_view(snapshot.state, *world_);
  if (!view.value("ok", false)) return {};
  const auto brand = [&](const Json &c) {
    if (c.at("sponsor") == -1) return std::string();
    return "<span class=\"ad-brand ad-brand-" + std::to_string(c.at("sponsor").get<int>() % 5) +
        "\"><span>" + esc(c.at("name").get<std::string>()) + "</span></span>";
  };
  std::string out = "<div class=\"advertising-page\"><div class=\"advertising-boards\" role=\"group\" aria-label=\"Vierzehn Werbebanden\">";
  for (int i = 0; i < 14; ++i) {
    const auto &c = view.at("slots")[size_t(i)];
    out += btn("ad-contract", (c.at("active") == true ? brand(c) : "<span class=\"advertising-free\">Platz frei</span>") +
        "<small><span>Bande " + std::to_string(i + 1) + "</span></small>", "advertising-board " + std::string(advertising_selected_ == i ? "selected" : ""),
        "id=\"ad-contract-" + std::to_string(i) + "\" data-ad-contract=\"" + std::to_string(i) + "\" data-id=\"" + std::to_string(i) +
        "\" aria-pressed=\"" + (advertising_selected_ == i ? "true" : "false") + "\" aria-label=\"Bande " + std::to_string(i + 1) + ", " +
        esc(c.at("name").get<std::string>()) + "\" style=\"" + rect(16 + (i % 7) * (354.285714 + 16), 16 + (i / 7) * 145, 354.285714, 129) + "\"");
  }
  out += "</div>";
  const std::array<const char *, 4> headings{"Trikotwerbung", "Fanartikel", "Öffentlichkeitsarbeit", "Fanbetreuung"};
  for (int i = 0; i < 4; ++i) {
    std::string body = "<strong><span>" + std::string(headings[size_t(i)]) + "</span></strong><div class=\"advertising-art\">" +
        img("advertising.panel." + std::to_string(i), 510, 510, false, false, 1000) + "</div>";
    if (i == 0) body += brand(view.at("slots")[14]);
    else body += "<output><span>" + esc(cash(view.at("spending")[size_t(i - 1)].at("amount").get<int64_t>())) + "</span></output>";
    out += btn(i == 0 ? "ad-contract" : "ad-spending", body,
        "advertising-card advertising-panel " + std::string(i == 0 ? "advertising-shirt " : "") + (i == 0 && advertising_selected_ == 14 ? "selected" : ""),
        "id=\"" + std::string(i == 0 ? "ad-contract-14" : "ad-spending-" + std::to_string(i - 1)) + "\" data-id=\"" + std::to_string(i == 0 ? 14 : i - 1) +
        "\" " + (i == 0 ? "data-ad-contract=\"14\" aria-pressed=\"" + std::string(advertising_selected_ == 14 ? "true" : "false") + "\" "
                         : "data-ad-spending=\"" + std::to_string(i - 1) + "\" ") +
        "style=\"" + rect(i * 658.5, 346, 636.5, 650) + "\"");
  }
  out += btn("ad-contract", "<strong><span>TV-Rechte</span></strong><div class=\"advertising-tv-art\">" +
      img("art.advertising-tv-v1.png", 540, 540, false, false, 1000) + "</div>" + brand(view.at("slots")[15]),
      "advertising-card advertising-tv " + std::string(advertising_selected_ == 15 ? "selected" : ""),
      "id=\"ad-contract-15\" data-ad-contract=\"15\" data-id=\"15\" aria-pressed=\"" + std::string(advertising_selected_ == 15 ? "true" : "false") + "\"");
  out += "<section class=\"advertising-details\" aria-label=\"Vertrag und Vorschau\" aria-live=\"polite\">";
  if (advertising_selected_ < 0 || advertising_selected_ > 15) {
    out += "<div class=\"advertising-hint\"><h2>Werbung und Vereinsausgaben</h2>"
        "<p>Bande, Trikot oder TV-Rechte auswählen.</p><p>Die drei Ausgabenbilder öffnen die Zahleneingabe.</p></div>";
  } else {
    const auto quote = demo_advertising_quote(snapshot.state, advertising_selected_, advertising_offer_, *world_);
    const bool active = quote.at("active");
    out += "<div class=\"advertising-offer\"><div class=\"ad-offer-mark\">" + brand(quote) +
        "</div><div class=\"ad-offer-arrows\">" +
        btn("ad-offer", "<span>‹</span>", "ad-offer-arrow", "id=\"ad-offer-previous\" data-step=\"-1\" aria-label=\"Vorheriges Angebot\" " + std::string(active ? "disabled" : "")) +
        btn("ad-offer", "<span>›</span>", "ad-offer-arrow", "id=\"ad-offer-next\" data-step=\"1\" aria-label=\"Nächstes Angebot\" " + std::string(active ? "disabled" : "")) +
        "</div><div class=\"ad-offer-facts\"><div><span>" + esc(quote.at("label").get<std::string>()) + "</span><strong>" +
        esc(quote.at("name").get<std::string>()) + "</strong></div><div><span>Dauer</span><strong>" + std::to_string(quote.at("months").get<int>()) +
        " Monate</strong></div><div><span>" + (active ? "Vertrag" : "Angebot") + "</span><strong>" +
        esc(cash(quote.at("amount").get<int64_t>())) + " / Monat</strong></div></div></div><nav class=\"advertising-actions\">" +
        btn("ad-cancel", active ? "Schließen" : "Abbrechen", "advertising-control", "id=\"ad-cancel\"") +
        btn("ad-accept", active ? "Vertrag läuft" : "Annehmen", "advertising-control", "id=\"ad-accept\" " + std::string(active ? "disabled" : "")) + "</nav>";
  }
  out += "<div class=\"advertising-totals\">";
  for (const auto &[label, key] : std::array<std::pair<const char *, const char *>, 3>{{{"Einnahmen", "income"}, {"Ausgaben", "expenses"}, {"Saldo", "balance"}}})
    out += "<div><span>" + std::string(label) + "</span><output>" + esc(cash(view.at(key).get<int64_t>())) + "</output></div>";
  return out + "</div></section></div>";
}
