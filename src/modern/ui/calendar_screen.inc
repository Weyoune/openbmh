// SPDX-License-Identifier: GPL-3.0-only
std::string App::render_calendar() {
  const auto snapshot = snap();
  const auto &calendar = snapshot.state.at("calendar");
  const auto days = calendar_days(calendar.at("start").get<std::string>());
  const auto fixtures = calendar_fixtures(calendar, *world_,
                                          manager(snapshot).value("club", ""));
  const int selected = calendar.at("selected").get<int>();
  const auto league_id = calendar.at("league").get<std::string>();
  const auto *league = world_->find("competitions", league_id);
  const auto nation = league ? league->value("nation", "") : "";
  const std::array<const char *, 12> months = {
      "Januar", "Februar", "März", "April", "Mai", "Juni",
      "Juli", "August", "September", "Oktober", "November", "Dezember"};
  const std::array<const char *, 7> weekdays = {"MO", "DI", "MI", "DO",
                                               "FR", "SA", "SO"};
  std::string body;
  int first = 0;
  for (int panel = 0; panel < 12; ++panel) {
    const int month = days[size_t(first)].month;
    const int offset = days[size_t(first)].weekday;
    body += "<section class=\"calendar-month\" style=\"" +
            rect((panel % 4) * 660, (panel / 4) * 556, 632, 528) +
            "\"><h2>" + months[size_t(month - 1)] + "</h2>";
    constexpr double cell_width = (604.0 - 65) / 6, cell_height = 438.0 / 7;
    for (int row = 0; row < 7; ++row) {
      body += "<span class=\"calendar-weekday " +
              std::string(row == 6 ? "sunday" : "") + "\" style=\"" +
              rect(12, 66 + row * cell_height, 65, cell_height) + "\">" +
              "<span>" + weekdays[size_t(row)] + "</span></span>";
      for (int col = 0; col < 6; ++col) {
        const int number = col * 7 + row - offset + 1;
        const int index = first + number - 1;
        if (number < 1 || index >= int(days.size()) ||
            days[size_t(index)].month != month)
          continue;
        const bool match = std::any_of(fixtures.begin(), fixtures.end(),
                                       [&](const auto &f) {
                                         return f.day == index;
                                       });
        const bool chosen = selected == index;
        const auto &iso = days[size_t(index)].iso;
        const auto date = iso.substr(8, 2) + "." + iso.substr(5, 2) + "." +
                          iso.substr(0, 4);
        body += btn(
            "calendar-select",
            (chosen ? "<span class=\"calendar-outline\" style=\"" +
                          rect(3, 3, cell_width - 6, cell_height - 6) +
                          "\"></span>"
                    : "") +
                std::string("<span class=\"control-label\">") +
                std::to_string(number) + "</span>",
            std::string("calendar-day ") + (match ? "has-match " : "") +
                (chosen ? "selected-day" : ""),
            "id=\"calendar-day-" + std::to_string(index) + "\" data-id=\"" +
                std::to_string(index) + "\" aria-pressed=\"" +
                (chosen ? "true" : "false") + "\" aria-label=\"" + date +
                (match ? ", belegt" : ", frei") + "\" style=\"" +
                rect(77 + col * cell_width, 66 + row * cell_height,
                     cell_width, cell_height) + "\"");
      }
    }
    body += "</section>";
    while (first < int(days.size()) && days[size_t(first)].month == month)
      ++first;
  }
  const auto &iso = days[size_t(selected)].iso;
  std::string info = iso.substr(8, 2) + "." + iso.substr(5, 2) + "." +
                     iso.substr(0, 4) + " · spielfrei";
  for (const auto &f : fixtures)
    if (f.day == selected) {
      const auto *home = world_->find("clubs", f.home),
                 *away = world_->find("clubs", f.away);
      info = iso.substr(8, 2) + "." + iso.substr(5, 2) + "." + iso.substr(0, 4) +
             " · " + std::to_string(f.round + 1) + ". Spieltag · " +
             (home ? world_->name(*home) : "") + " – " +
             (away ? world_->name(*away) : "");
    }
  body += "<p id=\"calendar-info\" class=\"calendar-info\">" + esc(info) +
          "</p><nav class=\"bottom-controls\">";
  std::vector<std::pair<std::string, std::string>> countries, leagues;
  std::set<std::string> seen;
  for (auto *l : world_->leagues()) {
    const auto id = l->value("nation", "");
    if (seen.insert(id).second)
      countries.emplace_back(id, world_->nation_name(id));
    if (id == nation)
      leagues.emplace_back(l->value("id", ""), world_->name(*l));
  }
  return body + select("calendar-country", countries, nation, "calendar-country") +
         select("calendar-league", leagues, league_id, "calendar-league") +
         "</nav>";
}
