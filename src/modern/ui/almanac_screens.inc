// SPDX-License-Identifier: GPL-3.0-only
namespace {
const Json &almanac_field(const Json &object, const char *key) {
  static const Json absent;
  if (object.is_object()) {
    const auto found = object.find(key);
    if (found != object.end())
      return *found;
  }
  return absent;
}
std::string almanac_text(const Json &value, const std::string &fallback = "—") {
  return value.is_string() && !value.get_ref<const std::string &>().empty()
             ? value.get<std::string>() : fallback;
}
std::string almanac_name(const Json &person) {
  return almanac_text(almanac_field(person, "name"));
}
bool almanac_flag(const Json &object, const char *key) {
  const auto &value = almanac_field(object, key);
  return value.is_boolean() && value.get<bool>();
}
std::string almanac_number(const Json &value) {
  return value.is_number_integer() ? number(value.get<int64_t>()) : "—";
}
std::string almanac_score(const Json &value) {
  return value.is_array() && value.size() == 2 &&
                 value[0].is_number_integer() && value[1].is_number_integer()
             ? std::to_string(value[0].get<int>()) + " : " +
                   std::to_string(value[1].get<int>()) : "—";
}
std::optional<std::array<int, 3>> almanac_date_parts(const Json &value) {
  if (!value.is_string())
    return {};
  const auto text = value.get<std::string>();
  if (text.size() != 10 || text[4] != '-' || text[7] != '-')
    return {};
  for (size_t i = 0; i < text.size(); ++i)
    if (i != 4 && i != 7 && (text[i] < '0' || text[i] > '9'))
      return {};
  const std::array<int, 3> date = {std::stoi(text.substr(0, 4)),
                                  std::stoi(text.substr(5, 2)),
                                  std::stoi(text.substr(8, 2))};
  if (!std::chrono::year_month_day{std::chrono::year{date[0]},
                                    std::chrono::month{unsigned(date[1])},
                                    std::chrono::day{unsigned(date[2])}}.ok())
    return {};
  return date;
}
const std::array<const char *, 12> almanac_months = {
    "Januar", "Februar", "März", "April", "Mai", "Juni",
    "Juli", "August", "September", "Oktober", "November", "Dezember"};
std::string almanac_date_label(const Json &value) {
  const auto date = almanac_date_parts(value);
  return date ? std::to_string((*date)[2]) + ". " +
                    almanac_months[size_t((*date)[1] - 1)] + " " +
                    std::to_string((*date)[0]) : "—";
}
std::string almanac_iso(int year, int month, int day) {
  std::ostringstream out;
  out << std::setfill('0') << std::setw(4) << year << '-' << std::setw(2)
      << month << '-' << std::setw(2) << day;
  return out.str();
}
std::string almanac_position(const Json &player) {
  const auto value = almanac_text(almanac_field(player, "position"));
  return value == "gk" ? "TW" : value == "def" ? "ABW"
         : value == "mid" ? "MIT" : value == "fwd" ? "STU" : value;
}
std::string almanac_competition_label(const Json &record) {
  const auto &competition = almanac_field(record, "competition");
  return almanac_name(competition) + " · " +
         almanac_number(almanac_field(record, "round")) + ". " +
         (almanac_text(almanac_field(competition, "type"), "") == "league"
              ? "Spieltag" : "Runde");
}
std::string almanac_event_kind(const std::string &kind) {
  if (kind == "goal") return "Tor";
  if (kind == "miss") return "Fehlschuss";
  if (kind == "yellow") return "Gelbe Karte";
  if (kind == "second_yellow") return "Gelb-Rote Karte";
  if (kind == "red") return "Rote Karte";
  if (kind == "injury") return "Verletzung";
  if (kind == "substitution") return "Wechsel";
  if (kind == "shootout") return "Elfmeterschießen";
  return "Ereignis";
}
} // namespace

std::string App::render_almanac() {
  const auto query = [&](const Json &request) -> Json {
    try {
      return source_ ? source_->read_almanac(request)
                     : Json{{"ok", false}, {"error", "almanac_unavailable"}};
    } catch (const std::exception &) {
      return {{"ok", false}, {"error", "almanac_unavailable"}};
    }
  };
  const auto index = query({{"kind", "index"}});
  const bool available = almanac_flag(index, "ok");
  std::vector<int> years;
  const auto &year_values = almanac_field(index, "years");
  if (available && year_values.is_array())
    for (const auto &value : year_values)
      if (value.is_number_integer() && value >= 1 && value <= 9999)
        years.push_back(value.get<int>());
  std::sort(years.begin(), years.end());
  years.erase(std::unique(years.begin(), years.end()), years.end());
  const auto snapshot = snap();
  const auto &reference = almanac_field(snapshot.state, "date");
  const auto date = almanac_date_parts(reference);
  if (almanac_year_ == 0 ||
      std::find(years.begin(), years.end(), almanac_year_) == years.end()) {
    almanac_year_ = years.empty() ? (date ? (*date)[0] : 0) : years.back();
    almanac_day_.clear();
    almanac_match_.clear();
    almanac_tab_ = "lineups";
  }
  if (almanac_tab_ != "lineups" && almanac_tab_ != "events")
    almanac_tab_ = "lineups";
  if (!almanac_day_.empty()) {
    const auto selected = almanac_date_parts(almanac_day_);
    if (!selected || (*selected)[0] != almanac_year_) {
      almanac_day_.clear();
      almanac_match_.clear();
    }
  }
  Json record;
  if (!almanac_match_.empty()) {
    const auto response = query({{"kind", "match"}, {"id", almanac_match_}});
    if (almanac_flag(response, "ok") && almanac_field(response, "match").is_object())
      record = response.at("match");
    else {
      almanac_match_.clear();
      almanac_tab_ = "lineups";
    }
  }
  const bool detail = record.is_object(), day_view = !almanac_day_.empty();
  const auto title = detail ? almanac_competition_label(record)
                    : day_view ? almanac_date_label(almanac_day_)
                    : "Spielarchiv" + (almanac_year_ ? " · " + std::to_string(almanac_year_) : "");
  const auto crest_image = [&](const Json &team, double size) {
    const auto &key = almanac_field(team, "crest");
    return "<div class=\"alm-crest\" style=\"width:" + scalar(size) +
           "dp;height:" + scalar(size) + "dp;\">" +
           (key.is_string() && !key.get_ref<const std::string &>().empty()
                ? img(key.get<std::string>(), size, size, true)
                : "<span class=\"alm-no-crest\">—</span>") + "</div>";
  };
  const auto team_label = [&](const Json &team, bool home) {
    const auto label = "<span class=\"alm-team-name\" title=\"" +
                       esc(almanac_name(team)) + "\">" + esc(almanac_name(team)) + "</span>";
    return "<div class=\"alm-team " + std::string(home ? "alm-team-home" : "alm-team-away") +
           "\">" + (home ? label + crest_image(team, 42)
                           : crest_image(team, 42) + label) + "</div>";
  };
  std::string output = "<div class=\"almanac\"><nav class=\"alm-toolbar\" aria-label=\"Archivnavigation\">" +
      btn("alm-back", "<span>‹ " + std::string(detail ? "Tagespaarungen" : day_view ? "Jahreskalender" : "Desktop") +
                      "</span>", "alm-control alm-back", "id=\"alm-back\"") +
      "<h2 id=\"alm-heading\" class=\"alm-heading\" tabindex=\"-1\" style=\"tab-index:auto;\">" + esc(title) + "</h2>";
  if (!detail && !day_view)
    output += report_picker("year", 2192, 420);
  else
    output += "<div class=\"alm-year-context\"><span>" +
              std::to_string(almanac_year_) + "</span></div>";
  output += "</nav><div id=\"almanac-underlay\" class=\"alm-underlay\">";

  Json day_response;
  if (!detail && day_view) {
    day_response = query({{"kind", "day"}, {"date", almanac_day_}});
    output += "<div id=\"alm-day-list\" class=\"alm-day-list\">"
              "<div class=\"alm-list-head\"><div class=\"alm-row-home\"><span>Heim</span></div>"
              "<div class=\"alm-row-score\"><span>Ergebnis</span></div><div class=\"alm-row-away\"><span>Auswärts</span></div></div>";
    const auto &matches = almanac_field(day_response, "matches");
    if (matches.is_array())
      for (size_t i = 0; i < matches.size(); ++i) {
        const auto &match = matches[i], &result = almanac_field(match, "result");
        const auto id = almanac_text(almanac_field(match, "id"), "");
        const bool penalties = almanac_flag(result, "penalties_played"), extra = almanac_flag(result, "extra_played");
        const auto description = almanac_name(almanac_field(match, "home")) + " " +
                                 almanac_score(almanac_field(result, "final")) + " " +
                                 almanac_name(almanac_field(match, "away")) + " · Spielbericht öffnen";
        output += btn("alm-match",
            "<div class=\"alm-match-main\"><div class=\"alm-row-home\">" +
                team_label(almanac_field(match, "home"), true) + "</div>"
                "<div class=\"alm-row-score alm-list-score\"><span class=\"alm-list-score-value\">" +
                esc(almanac_score(almanac_field(result, penalties ? "penalties" : "final"))) +
                "</span><span class=\"alm-list-score-phase\">" +
                (penalties ? "i. E." : extra ? "n. V." : "") + "</span></div>"
                "<div class=\"alm-row-away\">" + team_label(almanac_field(match, "away"), false) +
                "</div></div><div class=\"alm-match-meta\"><span>" +
                esc(almanac_competition_label(match) + " · Saison " +
                    almanac_text(almanac_field(match, "season"))) + "</span></div>",
            "alm-match-row " + std::string(i % 2 == 0 ? "striped" : ""),
            "id=\"alm-match-" + esc(id) + "\" data-id=\"" + esc(id) +
                "\" aria-label=\"" + esc(description) + "\"");
      }
    if (!matches.is_array() || matches.empty())
      output += "<p class=\"alm-empty\">Keine gespielten Partien an diesem Tag.</p>";
    output += "</div>";
  } else if (!detail) {
    output += "<div class=\"alm-calendar\">";
    Json days = Json::array();
    if (available && almanac_year_) {
      const auto calendar = query({{"kind", "calendar"}, {"year", almanac_year_}});
      if (almanac_flag(calendar, "ok") && almanac_field(calendar, "days").is_array())
        days = calendar.at("days");
    }
    constexpr double month_width = 635, month_height = 1550.0 / 3;
    if (almanac_year_ >= 1 && almanac_year_ <= 9999)
      for (int month = 1; month <= 12; ++month) {
        const int column = (month - 1) % 4, row = (month - 1) / 4;
        const auto first = std::chrono::year{almanac_year_} / std::chrono::month{unsigned(month)} / 1;
        const int offset = int(std::chrono::weekday{std::chrono::sys_days{first}}.iso_encoding()) - 1;
        const int count = int(unsigned(std::chrono::year_month_day{
            std::chrono::sys_days{std::chrono::year{almanac_year_} /
                                  std::chrono::month{unsigned(month)} / std::chrono::last}}.day()));
        constexpr double grid_width = 599;
        const double grid_height = month_height - 86, cell_height = grid_height / 7;
        constexpr double cell_width = (grid_width - 65) / 6;
        output += "<section class=\"alm-month\" style=\"" +
                  rect(column * 659, row * (month_height + 24), month_width, month_height) +
                  "\"><h3>" + almanac_months[size_t(month - 1)] + "</h3>"
                  "<div class=\"alm-month-grid\" style=\"width:599dp;height:" + scalar(grid_height) + "dp;\">";
        const std::array<const char *, 7> weekdays = {"MO", "DI", "MI", "DO", "FR", "SA", "SO"};
        for (int weekday = 0; weekday < 7; ++weekday) {
          output += "<div class=\"alm-weekday " + std::string(weekday == 6 ? "sunday" : "") +
                    "\" style=\"" + rect(0, weekday * cell_height, 65, cell_height) +
                    "\"><span>" + weekdays[size_t(weekday)] + "</span></div>";
          for (int week = 0; week < 6; ++week) {
            const int day = week * 7 + weekday - offset + 1;
            if (day < 1 || day > count)
              continue;
            const auto iso = almanac_iso(almanac_year_, month, day);
            const auto found = std::find_if(days.begin(), days.end(), [&](const auto &entry) {
              return almanac_field(entry, "date") == iso;
            });
            const bool played = found != days.end() &&
                                almanac_field(*found, "count").is_number_integer() &&
                                found->at("count").get<int>() > 0;
            const auto style = rect(65 + week * cell_width, weekday * cell_height, cell_width, cell_height);
            const auto text = "<span class=\"alm-day-text\">" + std::to_string(day) + "</span>";
            if (played)
              output += btn("alm-day", text, "alm-day played",
                  "id=\"alm-day-" + iso + "\" data-id=\"" + iso + "\" style=\"" + style +
                  "\" aria-label=\"" + esc(almanac_date_label(iso) + ", " +
                      almanac_number(found->at("count")) + " gespielte Partien") + "\"");
            else {
              const bool future = reference.is_string() && iso > reference.get<std::string>();
              output += "<div class=\"alm-day " + std::string(future ? "future" : "") +
                        "\" style=\"" + style + "\">" + text + "</div>";
            }
          }
        }
        output += "</div></section>";
      }
    else
      output += "<p class=\"alm-empty\">Keine gespielten Partien im Archiv.</p>";
    output += "</div>";
  } else {
    const auto &result = almanac_field(record, "result"), &weather = almanac_field(record, "weather");
    const bool extra = almanac_flag(result, "extra_played"), penalties = almanac_flag(result, "penalties_played");
    const auto phase = std::string(extra ? "Nach Verlängerung" : "Nach regulärer Spielzeit") +
                       (penalties ? " · Entscheidung i. E." : "");
    std::string phases = "Reguläre Zeit: " + almanac_score(almanac_field(result, "regulation"));
    if (extra) phases += " · Verlängerung: " + almanac_score(almanac_field(result, "extra"));
    if (penalties) phases += " · i. E.: " + almanac_score(almanac_field(result, "penalties"));
    output += "<section class=\"alm-summary\"><div class=\"alm-summary-teams\">";
    const auto summary_team = [&](const char *side, const std::string &css) {
      const auto &team = almanac_field(record, side), &manager = almanac_field(team, "manager");
      const auto manager_name = manager.is_null() ? std::string("Computer") : almanac_name(manager);
      return "<div class=\"alm-summary-team " + css + "\">" + crest_image(team, 80) +
             "<span class=\"alm-summary-team-name\" title=\"" + esc(almanac_name(team)) + "\">" +
             esc(almanac_name(team)) + "</span><span class=\"alm-summary-manager\">Manager: " +
             esc(manager_name) + "</span></div>";
    };
    output += summary_team("home", "home") +
              "<div class=\"alm-summary-result\"><span class=\"alm-summary-score\">" +
              esc(almanac_score(almanac_field(result, "final"))) +
              "</span><span class=\"alm-summary-phase\">" + esc(phase) +
              "</span><span class=\"alm-summary-phases\">" + esc(phases) + "</span></div>" +
              summary_team("away", "away") + "</div><div class=\"alm-match-facts\">";
    const std::vector<std::pair<std::string, std::string>> facts = {
        {"Schiedsrichter", almanac_text(almanac_field(record, "referee"))},
        {"Zuschauer", almanac_number(almanac_field(record, "attendance"))},
        {"Temperatur", almanac_number(almanac_field(weather, "temperature")) +
                           (almanac_field(weather, "temperature").is_number_integer() ? " °C" : "")},
        {"Regen", almanac_number(almanac_field(weather, "rain")) +
                     (almanac_field(weather, "rain").is_number_integer() ? " / 7" : "")}};
    for (const auto &[label, value] : facts)
      output += "<div class=\"alm-match-fact\"><span class=\"alm-fact-label\">" + esc(label) +
                "</span><span class=\"alm-fact-value\">" + esc(value) + "</span></div>";
    output += "</div></section><nav class=\"alm-tabs\" role=\"tablist\" aria-label=\"Spielbericht\">";
    for (const auto &[key, label] :
         std::array<std::pair<std::string, std::string>, 2>{{{"lineups", "Aufstellungen"}, {"events", "Ereignisse"}}})
      output += btn("alm-tab", "<span>" + label + "</span>",
          "alm-tab " + std::string(almanac_tab_ == key ? "selected" : ""),
          "id=\"alm-tab-" + key + "\" data-id=\"" + key +
              "\" role=\"tab\" aria-selected=\"" + (almanac_tab_ == key ? "true" : "false") +
              "\" aria-controls=\"alm-detail-content\"");
    output += "</nav><div id=\"alm-detail-content\" class=\"alm-detail-content\" role=\"tabpanel\" "
              "aria-labelledby=\"alm-tab-" + almanac_tab_ + "\">";
    if (almanac_tab_ == "lineups") {
      output += "<div class=\"alm-lineups\">";
      for (const auto *side : {"home", "away"}) {
        const auto &team = almanac_field(record, side), &lineup = almanac_field(almanac_field(record, "lineups"), side);
        // Both roster headings read left-to-right. Pairing rows alone mirror the home label.
        output += "<section class=\"alm-lineup\"><header>" + team_label(team, false) + "</header>";
        if (!lineup.is_object())
          output += "<p class=\"alm-empty\">Keine Aufstellung überliefert.</p>";
        else
          for (const auto *part : {"starters", "bench"}) {
            output += "<h3>" + std::string(std::string_view(part) == "starters" ? "Startelf" : "Bank") + "</h3>";
            const auto &players = almanac_field(lineup, part);
            if (players.is_array())
              for (size_t i = 0; i < players.size(); ++i) {
                const auto &player = players[i];
                output += "<div class=\"alm-player\"><span class=\"alm-player-number\">" +
                    std::to_string(i + (std::string_view(part) == "starters" ? 1 : 12)) +
                    "</span><span class=\"alm-position\">" + esc(almanac_position(player)) +
                    "</span><span class=\"alm-player-name\" title=\"" + esc(almanac_name(player)) +
                    "\">" + esc(almanac_name(player)) + "</span></div>";
              }
          }
        output += "</section>";
      }
      output += "</div>";
    } else {
      std::vector<Json> events;
      const auto &source_events = almanac_field(record, "events");
      if (source_events.is_array())
        for (const auto &event : source_events) events.push_back(event);
      std::stable_sort(events.begin(), events.end(), [](const Json &a, const Json &b) {
        const auto &am = almanac_field(a, "minute"), &bm = almanac_field(b, "minute");
        return (am.is_number_integer() ? am.get<int>() : 0) <
               (bm.is_number_integer() ? bm.get<int>() : 0);
      });
      output += "<div class=\"alm-events\" role=\"list\">";
      int kick = 0;
      const std::array<const char *, 4> goal_types = {"Spiel", "Elfmeter/Freistoß", "Nachschuss", "Ecke"};
      for (size_t i = 0; i < events.size(); ++i) {
        const auto &event = events[i];
        const auto kind = almanac_text(almanac_field(event, "kind"), "");
        const auto side = almanac_text(almanac_field(event, "side"), "");
        const auto player = almanac_name(almanac_field(event, "player"));
        const auto other = almanac_name(almanac_field(event, "other"));
        const auto &value = almanac_field(event, "detail");
        const int detail_value = value.is_number_integer() ? value.get<int>() : -1;
        std::string description = player;
        if (kind == "shootout")
          description = std::string(detail_value == 1 ? "Treffer" : detail_value == 0 ? "Fehlversuch" : "—") + " · " + player;
        else if (kind == "substitution")
          description = "Ein: " + player + " · Aus: " + other;
        else if (kind == "goal" || kind == "miss")
          description = player + " · " + (kind == "miss" ? "Zuspiel" : "Vorlage") + ": " + other +
                        " · " + (detail_value >= 0 && detail_value < 4 ? goal_types[size_t(detail_value)] : "—");
        const auto minute = kind == "shootout" ? "i. E. · " + std::to_string(++kick)
                            : almanac_number(almanac_field(event, "minute")) + "′";
        const auto team = side == "home" || side == "away"
                              ? almanac_name(almanac_field(record, side.c_str())) : "—";
        output += "<div class=\"alm-event " + esc(kind) + (i % 2 == 0 ? " striped" : "") +
                  "\" role=\"listitem\"><span class=\"alm-event-minute\">" + esc(minute) +
                  "</span><span class=\"alm-event-team\" title=\"" + esc(team) + "\">" + esc(team) +
                  "</span><div class=\"alm-event-description\"><span class=\"alm-event-kind\">" +
                  almanac_event_kind(kind) + "</span><span class=\"alm-event-text\">" + esc(description) + "</span></div></div>";
      }
      if (events.empty())
        output += "<p class=\"alm-empty\">Keine Ereignisse überliefert.</p>";
      output += "</div>";
    }
    output += "</div>";
  }
  std::string footer;
  if (detail)
    footer = almanac_date_label(almanac_field(record, "date")) + " · Saison " +
             almanac_text(almanac_field(record, "season"));
  else if (day_view) {
    const auto &matches = almanac_field(day_response, "matches");
    footer = std::to_string(matches.is_array() ? matches.size() : 0) + " gespielte Partien";
  } else if (!available)
    footer = "Almanach derzeit nicht verfügbar.";
  else if (years.empty())
    footer = "Keine gespielten Partien im Archiv";
  else
    footer = almanac_number(almanac_field(index, "match_count")) + " Partien · " +
             std::to_string(years.front()) + "–" + std::to_string(years.back());
  output += "<footer class=\"alm-footer\"><div class=\"alm-key\"><span class=\"alm-key-marker\"></span>"
            "<span>Gespielte Partien</span></div><span class=\"alm-footer-info\">" + esc(footer) + "</span></footer></div>";
  if (report_picker_ == "year")
    output += btn("alm-year-cancel", "", "alm-picker-shield", "id=\"alm-year-shield\" aria-label=\"Jahresauswahl schließen\"");
  return output + "</div>";
}
