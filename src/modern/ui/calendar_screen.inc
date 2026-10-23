// SPDX-License-Identifier: GPL-3.0-only
namespace {
std::string calendar_date_label(const std::string &iso) {
  return iso.substr(8, 2) + "." + iso.substr(5, 2) + "." + iso.substr(0, 4);
}
}

Json App::planning_calendar() const {
  auto calendar = snap().state.at("calendar");
  calendar["league"] = calendar_own_league(*world_, manager(snap()).value("club", ""));
  return calendar;
}

std::string App::calendar_day_info(const Json &calendar) const {
  const auto days = calendar_days(calendar.at("start").get<std::string>());
  const int selected = calendar.at("selected").get<int>();
  const auto date = calendar_date_label(days.at(size_t(selected)).iso);
  const auto own = manager(snap()).value("club", "");
  for (const auto &f : calendar_fixtures(calendar, *world_, own))
    if (f.day == selected) {
      const auto *home = world_->find("clubs", f.home), *away = world_->find("clubs", f.away);
      return date + " · " + std::to_string(f.round + 1) + ". Spieltag · " +
             (home ? world_->name(*home) : "") + " – " + (away ? world_->name(*away) : "");
    }
  if (calendar.at("league") == calendar_own_league(*world_, own))
    for (const auto &f : calendar.at("friendlies"))
      if (f.at("day") == selected) {
        const auto *opponent = world_->find("clubs", f.at("opponent"));
        return date + " · Freundschaftsspiel: " + (opponent ? world_->name(*opponent) : "—");
      }
  return date + " · spielfrei";
}

std::string App::calendar_grid(const Json &calendar, double top, double height,
                               double gap_x, double gap_y, bool booking,
                               bool postpone) {
  const auto days = calendar_days(calendar.at("start").get<std::string>());
  const auto own = manager(snap()).value("club", "");
  const auto fixtures = calendar_fixtures(calendar, *world_, own);
  const bool own_league = calendar.at("league") == calendar_own_league(*world_, own);
  const int selected = calendar.at("selected").get<int>();
  const double width = (2612 - gap_x * 3) / 4, card_height = (height - gap_y * 2) / 3;
  const double grid_top = postpone ? 58 : 66;
  const double cell_width = (width - 28 - 65) / 6;
  const double cell_height = (card_height - grid_top - (postpone ? 20 : 24)) / 7;
  const std::array<const char *, 12> months = {"Januar", "Februar", "März", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November", "Dezember"};
  const std::array<const char *, 7> weekdays = {"MO", "DI", "MI", "DO", "FR", "SA", "SO"};
  std::string body = "<div class=\"calendar-grid\">";
  int first = 0;
  for (int panel = 0; panel < 12; ++panel) {
    const int month = days[size_t(first)].month, offset = days[size_t(first)].weekday;
    body += "<section class=\"calendar-month\" style=\"" +
            rect((panel % 4) * (width + gap_x), top + (panel / 4) * (card_height + gap_y), width, card_height) +
            "\"><h2" + std::string(postpone ? " style=\"top:12dp;\"" : "") + ">" + months[size_t(month - 1)] + "</h2>";
    for (int row = 0; row < 7; ++row) {
      body += "<span class=\"calendar-weekday " + std::string(row == 6 ? "sunday" : "") + "\" style=\"" +
              rect(12, grid_top + row * cell_height, 65, cell_height) + "\"><span>" + weekdays[size_t(row)] + "</span></span>";
      for (int col = 0; col < 6; ++col) {
        const int day_number = col * 7 + row - offset + 1, index = first + day_number - 1;
        if (day_number < 1 || index >= int(days.size()) || days[size_t(index)].month != month) continue;
        const auto match = std::find_if(fixtures.begin(), fixtures.end(), [&](const auto &f) { return f.day == index; });
        const bool friendly = own_league && std::any_of(calendar.at("friendlies").begin(), calendar.at("friendlies").end(), [&](const auto &f) { return f.at("day") == index; });
        const bool occupied = match != fixtures.end() || friendly, chosen = selected == index;
        const bool source = postpone && match != fixtures.end() && match->from == calendar.value("source_day", -1);
        body += btn("calendar-select",
                    (chosen || source ? "<span class=\"calendar-outline " + std::string(source ? "source-outline" : "") + "\" style=\"" + rect(3, 3, cell_width - 6, cell_height - 6) + "\"></span>" : "") +
                    "<span class=\"control-label\">" + std::to_string(day_number) + "</span>",
                    std::string("calendar-day ") + (occupied ? "has-match " : "") + (friendly ? "friendly-date " : "") + (source ? "source-day " : "") + (chosen ? "selected-day" : ""),
                    "id=\"calendar-day-" + std::to_string(index) + "\" data-id=\"" + std::to_string(index) + "\" aria-pressed=\"" + (chosen ? "true" : "false") + "\" aria-label=\"" + calendar_date_label(days[size_t(index)].iso) +
                    (occupied ? ", belegt" : ", frei") + "\" style=\"" + rect(77 + col * cell_width, grid_top + row * cell_height, cell_width, cell_height) + "\" " + (booking && (index < 7 || occupied) ? "disabled" : ""));
      }
    }
    body += "</section>";
    while (first < int(days.size()) && days[size_t(first)].month == month) ++first;
  }
  return body + "</div>";
}

std::string App::render_calendar() {
  const auto calendar = snap().state.at("calendar");
  return "<div id=\"report-underlay\" class=\"report-underlay\">" +
         calendar_grid(calendar, 0, 1640, 28, 28, false, false) +
         "<p id=\"calendar-info\" class=\"calendar-info\">" + esc(calendar_day_info(calendar)) +
         "</p><nav class=\"report-footer\">" + report_picker("country", 0, 1294) +
         report_picker("league", 1318, 1294) + "</nav></div>";
}
