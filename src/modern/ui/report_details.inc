// SPDX-License-Identifier: GPL-3.0-only
namespace {
std::optional<std::array<int, 3>> report_detail_date(const Json &value) {
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
std::string report_detail_position(const Json &player) {
  std::string result;
  const auto positions = player.find("positions");
  if (positions == player.end() || !positions->is_array())
    return "—";
  for (const auto &position : *positions) {
    if (!position.is_string())
      continue;
    const auto value = position.get<std::string>();
    if (value.empty())
      continue;
    if (!result.empty())
      result += " / ";
    result += value == "gk" ? "TW" : value == "def" ? "ABW"
                               : value == "mid" ? "MIT"
                               : value == "fwd" ? "STU" : value;
  }
  return result.empty() ? "—" : result;
}
std::string report_detail_strength(const Json &player) {
  const auto attributes = player.find("attributes");
  if (attributes == player.end() || !attributes->is_object())
    return "—";
  const auto value = attributes->find("strength");
  return value != attributes->end() && value->is_number_integer()
             ? number(value->get<int64_t>()) : "—";
}
std::string report_detail_age(const Json &player, const Json &date,
                              bool with_unit) {
  const auto current = report_detail_date(date);
  const auto born = report_detail_date(player.value("born", Json()));
  if (born && current && *born <= *current) {
    const int years = (*current)[0] - (*born)[0] -
                      (std::pair{(*current)[1], (*current)[2]} <
                       std::pair{(*born)[1], (*born)[2]});
    return std::to_string(years) + (with_unit ? " Jahre" : "");
  }
  const auto initial = player.find("age_at_start");
  if (initial != player.end() && initial->is_object() &&
      initial->contains("years") && (*initial)["years"].is_number_integer()) {
    const auto on = report_detail_date(initial->value("on", Json()));
    if (on && (*initial)["years"].get<int>() >= 0) {
      const auto iso = (*initial)["on"].get<std::string>();
      // An imported age has no birthday. Keep its known reference date visible.
      return std::to_string((*initial)["years"].get<int>()) +
             (with_unit ? " Jahre" : "") + " (" + iso.substr(8, 2) + "." +
             iso.substr(5, 2) + "." + iso.substr(0, 4) + ")";
    }
  }
  return "—";
}
std::string report_detail_term(const Catalog &world, const Json &player,
                               const Json &date) {
  const auto current = report_detail_date(date);
  if (!current)
    return "—";
  for (const auto &contract : world.rows("contracts")) {
    if (contract.value("person", "") != player.value("id", "") ||
        contract.value("team", "") != player.value("team", ""))
      continue;
    const auto start = report_detail_date(contract.value("start", Json()));
    const auto end = report_detail_date(contract.value("end", Json()));
    if (!end || (start && *start > *current))
      continue;
    // Remaining complete calendar months; no Classic initializer is executed.
    int months = ((*end)[0] - (*current)[0]) * 12 + (*end)[1] - (*current)[1];
    if ((*end)[2] < (*current)[2])
      --months;
    return std::to_string(std::max(0, months)) + " Monate";
  }
  return "—";
}
std::string report_detail_fields(
    const std::vector<std::pair<std::string, std::string>> &fields,
    int spacing) {
  std::string result = "<dl class=\"report-detail-values\">";
  for (size_t i = 0; i < fields.size(); ++i)
    result += "<div class=\"report-detail-field\" style=\"top:" +
              std::to_string(i * spacing) + "dp;\"><dt>" +
              esc(fields[i].first) + "</dt><dd>" + esc(fields[i].second) +
              "</dd></div>";
  return result + "</dl>";
}
} // namespace

std::string App::render_report_detail() {
  if (report_detail_kind_.empty() || !world_)
    return {};
  const auto snapshot = snap();
  const auto current_date = snapshot.state.value("date", Json());
  const auto *club = world_->find("clubs", report_detail_club_);
  const auto *player = world_->find("players", report_detail_player_);
  const auto display_name = [&](const Json *row) {
    if (!row)
      return std::string("—");
    const auto name = world_->name(*row);
    return name.empty() ? std::string("—") : name;
  };
  const auto close = [&](const std::string &caption) {
    return btn("report-detail-close", esc(caption), "report-detail-action",
               "id=\"report-detail-close\"");
  };
  std::string title, body, actions;
  if (report_detail_kind_ == "indoor-info") {
    title = "Hallenmasters";
    std::string season = "—";
    if (const auto date = report_detail_date(current_date)) {
      const int start = (*date)[0] - ((*date)[1] < 7 ? 1 : 0);
      const auto next = std::to_string((start + 1) % 100);
      season = std::to_string(start) + "/" + (next.size() < 2 ? "0" : "") + next;
    }
    body = "<p class=\"report-detail-info-text\">Fiktive Ergebnisse und Platzierungen zur Saison " +
           esc(season) + ".</p>";
    actions = close("Zurück");
  } else if (report_detail_kind_ == "club" && club) {
    title = display_name(club);
    const Json *stadium = nullptr;
    if (const auto id = club->find("stadium");
        id != club->end() && id->is_string())
      stadium = world_->find("stadiums", id->get<std::string>());
    if (!stadium)
      for (const auto &candidate : world_->rows("stadiums"))
        if (candidate.value("owner", Json()) == report_detail_club_) {
          stadium = &candidate;
          break;
        }
    std::string capacity = "—", strength = "—", manager_name = "—";
    if (stadium) {
      const auto value = stadium->value("capacity", Json());
      if (value.is_number_integer())
        capacity = number(value.get<int64_t>());
      else if (value.is_object() && !value.empty()) {
        int64_t total = 0;
        bool valid = true;
        for (const auto &[area, seats] : value.items()) {
          if (!seats.is_number_integer() || seats.get<int64_t>() < 0) {
            valid = false;
            break;
          }
          total += seats.get<int64_t>();
        }
        if (valid)
          capacity = number(total);
      }
    }
    if (const auto value = club->find("strength");
        value != club->end() && value->is_number_integer())
      strength = number(value->get<int64_t>());
    else {
      int64_t sum = 0, count = 0;
      for (const auto *member : world_->squad(report_detail_club_)) {
        const auto attributes = member->value("attributes", Json::object());
        if (attributes.is_object() && attributes.contains("strength") &&
            attributes["strength"].is_number_integer()) {
          sum += attributes["strength"].get<int64_t>();
          ++count;
        }
      }
      if (count)
        strength = number(sum / count);
    }
    for (const auto &candidate : world_->rows("staff"))
      if (candidate.value("role", "") == "manager" &&
          candidate.value("team", "") == report_detail_club_)
        manager_name = display_name(&candidate);
    for (const auto &candidate : snapshot.state.at("managers"))
      if (candidate.value("club", "") == report_detail_club_)
        manager_name = candidate.value("name", std::string("—"));
    if (manager_name.empty())
      manager_name = "—";
    body = "<div class=\"report-detail-badge\">" + crest(club, 192) +
           "</div><p class=\"report-detail-note\">Vereinsprofil</p>" +
           report_detail_fields({{"Stadion", display_name(stadium)},
                                 {"Plätze", capacity}, {"Stärke", strength},
                                 {"Manager", manager_name}}, 72);
    actions = btn("report-club-roster", "Spieler", "report-detail-action",
                  "id=\"report-detail-roster\"") + close("Zurück");
  } else if (report_detail_kind_ == "roster" && club) {
    title = display_name(club) + " · Kader";
    body = "<div class=\"report-detail-roster-scroll\" id=\"report-detail-roster-scroll\">"
           "<div class=\"report-detail-table\"><div class=\"report-detail-row report-detail-table-head\">";
    for (const auto &heading : {"Spieler", "Pos.", "Stärke", "Alter"})
      body += "<div class=\"report-detail-cell\">" + std::string(heading) + "</div>";
    body += "</div>";
    const auto squad = world_->squad(report_detail_club_);
    for (size_t i = 0; i < squad.size(); ++i) {
      const auto *member = squad[i];
      body += "<div class=\"report-detail-row " +
              std::string(i % 2 == 0 ? "report-detail-striped" : "") +
              "\"><div class=\"report-detail-cell\">" +
              btn("player-detail", esc(display_name(member)),
                  "report-detail-player-link",
                  "id=\"report-detail-player-" + std::to_string(i) +
                      "\" data-id=\"" + esc(member->value("id", "")) + "\"") +
              "</div><div class=\"report-detail-cell\">" +
              esc(report_detail_position(*member)) +
              "</div><div class=\"report-detail-cell\">" +
              esc(report_detail_strength(*member)) +
              "</div><div class=\"report-detail-cell\">" +
              esc(report_detail_age(*member, current_date, false)) + "</div></div>";
    }
    body += "</div>";
    if (squad.empty())
      body += "<p class=\"report-detail-empty\">Keine Spieler vorhanden.</p>";
    body += "</div>";
    actions = close("Zurück");
  } else if (report_detail_kind_ == "player" && player) {
    title = display_name(player);
    std::string photo = player->value("photo", "");
    if (photo.empty()) {
      const auto id = player->value("id", "");
      // Catalog resolves the world's silhouette fallback for an absent photo key.
      photo = "asset:player/" + (id.starts_with("player:") ? id.substr(7) : id) +
              "/photo";
    }
    body = "<div class=\"report-detail-portrait\">" +
           img(photo, 496, 662.667, true, true, 1024) +
           "</div><p class=\"report-detail-photo-caption\">Porträt</p>" +
           report_detail_fields({{"Position", report_detail_position(*player)},
                                 {"Alter", report_detail_age(*player, current_date, true)},
                                 {"Stärke", report_detail_strength(*player)},
                                 {"Verein", display_name(club)},
                                 {"Vertrag", report_detail_term(*world_, *player, current_date)}}, 73);
    actions = close("Schließen");
  } else
    return {};
  return "<div id=\"report-detail-layer\" class=\"report-detail-layer\">"
         "<section id=\"report-detail-dialog\" class=\"report-detail-card report-detail-" +
         esc(report_detail_kind_) +
         "\" role=\"dialog\" aria-modal=\"true\" aria-labelledby=\"report-detail-title\">"
         "<h2 id=\"report-detail-title\">" + esc(title) + "</h2>" + body +
         "<nav class=\"report-detail-actions\">" + actions + "</nav></section></div>";
}

bool App::report_detail_action(const std::string &action, Rml::Element *target) {
  if (action == "report-detail-close") {
    if (report_detail_kind_.empty())
      return false;
    const auto restore = report_detail_return_focus_;
    report_detail_kind_.clear();
    report_detail_club_.clear();
    report_detail_player_.clear();
    report_detail_return_focus_.clear();
    focus_after_render_ = restore;
    refresh();
    return true;
  }
  if (action != "club-detail" && action != "player-detail" &&
      action != "report-club-roster")
    return false;
  if (!world_ || !target)
    return true;
  const auto id = target->GetAttribute<Rml::String>("data-id", "");
  if (action == "club-detail") {
    if (!world_->find("clubs", id))
      return true;
    if (report_detail_kind_.empty())
      report_detail_return_focus_ = target->GetId();
    report_detail_kind_ = "club";
    report_detail_club_ = id;
    report_detail_player_.clear();
    focus_after_render_ = "report-detail-roster";
  } else if (action == "report-club-roster") {
    if (!world_->find("clubs", report_detail_club_))
      return true;
    report_detail_kind_ = "roster";
    report_detail_player_.clear();
    focus_after_render_ = world_->squad(report_detail_club_).empty()
                              ? "report-detail-close" : "report-detail-player-0";
  } else {
    const auto *player = world_->find("players", id);
    if (!player)
      return true;
    if (report_detail_kind_.empty())
      report_detail_return_focus_ = target->GetId();
    report_detail_kind_ = "player";
    report_detail_player_ = id;
    report_detail_club_ = player->value("team", "");
    focus_after_render_ = "report-detail-close";
  }
  refresh();
  return true;
}
