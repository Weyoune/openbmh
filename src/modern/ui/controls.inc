// SPDX-License-Identifier: GPL-3.0-only
// Input and navigation share the same paths for human and scripted events.
void App::context_menu(const std::string &id, int x, int y) {
  init_desktop();
  context_id_ = id;
  context_x_ = x;
  context_y_ = y;
  context_open_ = true;
  context_dirty_ = true;
  dirty_ = true;
}
void App::render_context() {
  auto *root = element("desktop-context");
  root->SetProperty("display", context_open_ ? "block" : "none");
  if (!context_open_ || !context_dirty_)
    return;
  std::string body;
  if (!context_id_.empty()) {
    const auto &o = desktop_["objects"].at(context_id_);
    body += btn("context-pin",
                o.value("pinned", true) ? "Fixierung lösen" : "Fixieren");
    if (o.value("widget", false)) {
      body += btn("context-remove", "Widget entfernen");
      for (const auto &d : widget_definitions())
        if (d.id == context_id_)
          for (auto [w, h] : d.sizes)
            body += btn("context-size",
                        std::to_string(w) + " × " + std::to_string(h), {},
                        "data-id=\"" + std::to_string(w) + "x" +
                            std::to_string(h) + "\"");
    }
    body += "<div class=\"separator\"></div>";
  }
  bool all_pinned = true;
  for (const auto &[id, o] : desktop_["objects"].items()) {
    (void)id;
    if (o.value("visible", false) && !o.value("pinned", true))
      all_pinned = false;
  }
  body +=
      btn("context-undo", "Rückgängig", {}, undo_.empty() ? "disabled" : "") +
      btn("context-pin-all", all_pinned ? "Alles lösen" : "Alles fixieren") +
      btn("context-grid", desktop_.value("grid", true) ? "Raster ausschalten"
                                                       : "Raster einschalten") +
      btn("context-arrange", "Am Raster anordnen") +
      "<div class=\"separator\"></div>" +
      btn("context-widgets", "Widget hinzufügen") +
      btn("context-view-save", "Ansicht speichern") +
      btn("context-view-load", "Ansicht laden") +
      btn("context-background", "Hintergrundbild ändern") +
      "<div class=\"separator\"></div>" + btn("context-quit", "Spiel beenden");
  root->SetInnerRML(body);
  context_->Update();
  const auto size = context_->GetDimensions();
  root->SetProperty(
      "left",
      scalar(std::clamp(context_x_, 4, std::max(4, size.x - 288))) + "px");
  root->SetProperty(
      "top", scalar(std::clamp(
                 context_y_, 4,
                 std::max(4, size.y - int(root->GetBox().GetSize().y) - 8))) +
                 "px");
  context_dirty_ = false;
}
void App::drag_event(Rml::Event &event) {
  const auto type = event.GetType();
  if (type == "mousedown")
    suppress_click_ = false;
  const float x = event.GetParameter<float>("mouse_x", 0),
              y = event.GetParameter<float>("mouse_y", 0);
  if (type == "mousedown" && event.GetParameter<int>("button", 0) == 0 &&
      scene_ == "desktop" && menu_.empty() && popup_.empty()) {
    auto *target = event.GetTargetElement();
    bool interactive = false;
    for (auto *n = target; n && n != doc_; n = n->GetParentNode()) {
      if (n->HasAttribute("data-interactive"))
        interactive = true;
      if (n->HasAttribute("data-object")) {
        if (interactive)
          return;
        const auto id = n->GetAttribute<Rml::String>("data-object", "");
        const auto &o = desktop_["objects"].at(id);
        if (o.value("pinned", true))
          return;
        drag_ = {true, false, id, x, y, o.value("x", 0.0), o.value("y", 0.0)};
        return;
      }
    }
  }
  if (!drag_.active)
    return;
  if (type == "mousemove") {
    if (!drag_.moved && std::hypot(x - drag_.x, y - drag_.y) < 6)
      return;
    if (!drag_.moved) {
      undo_.push_back(desktop_);
      if (undo_.size() > 40)
        undo_.erase(undo_.begin());
      drag_.moved = true;
    }
    auto &o = desktop_["objects"][drag_.id];
    if (o.value("pinned", true))
      return;
    const double left = std::clamp(drag_.left + (x - drag_.x) / scale_, 0.0,
                                   3840.0 - o.value("w", 224));
    const double top = std::clamp(drag_.top + (y - drag_.y) / scale_, 224.0,
                                  2160.0 - o.value("h", 224));
    o["x"] = left;
    o["y"] = top;
    if (auto *n = element("object-" + drag_.id)) {
      n->SetProperty("left", scalar(left) + "dp");
      n->SetProperty("top", scalar(top) + "dp");
    }
    dirty_ = true;
  }
  if (type == "mouseup") {
    if (drag_.moved) {
      suppress_click_ = true;
      auto &o = desktop_["objects"][drag_.id];
      auto proposed = undo_.back();
      const int x = o.value("x", 64), y = o.value("y", 224);
      if (place_desktop_object(proposed, drag_.id, x, y))
        desktop_ = std::move(proposed);
      else
        desktop_ = undo_.back();

      refresh();
    }
    drag_.active = false;
  }
}
void App::ProcessEvent(Rml::Event &event) try {
  const auto type = event.GetType();
  for (auto *n = event.GetTargetElement(); n && n != doc_;
       n = n->GetParentNode())
    if (n->GetId() == "native-ticket-layer")
      return;
  if (type == "mouseup" && event.GetParameter<int>("button", 0) == 1) {
    if (scene_ == "desktop" && menu_.empty() && popup_.empty()) {
      std::string id;
      for (auto *n = event.GetTargetElement(); n && n != doc_;
           n = n->GetParentNode())
        if (n->HasAttribute("data-object")) {
          id = n->GetAttribute<Rml::String>("data-object", "");
          break;
        }
      context_menu(id, event.GetParameter<int>("mouse_x", 0),
                   event.GetParameter<int>("mouse_y", 0));
    }
    event.StopPropagation();
    return;
  }
  if (type == "mousedown" && event.GetParameter<int>("button", 0) == 0 &&
      context_open_) {
    bool inside = false;
    for (auto *n = event.GetTargetElement(); n && n != doc_;
         n = n->GetParentNode())
      if (n->GetId() == "desktop-context")
        inside = true;
    if (!inside) {
      context_open_ = false;
      dirty_ = true;
      event.StopPropagation();
      return;
    }
  }
  if (type == "mousedown" || type == "mousemove" || type == "mouseup") {
    drag_event(event);
    return;
  }
  auto *target = event.GetTargetElement();
  if (type == "contextmenu") {
    if (scene_ != "desktop" || !menu_.empty() || !popup_.empty())
      return;
    std::string id;
    for (auto *n = target; n && n != doc_; n = n->GetParentNode())
      if (n->HasAttribute("data-object")) {
        id = n->GetAttribute<Rml::String>("data-object", "");
        break;
      }
    context_menu(id, event.GetParameter<int>("mouse_x", 0),
                 event.GetParameter<int>("mouse_y", 0));
    event.StopPropagation();
    return;
  }
  if (type == "keydown") {
    const auto key =
        Rml::Input::KeyIdentifier(event.GetParameter<int>("key_identifier", 0));
    if (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_SPACE) {
      for (auto *n = target; n && n != doc_; n = n->GetParentNode())
        if (n->GetTagName() == "button") {
          if (!n->HasAttribute("disabled"))
            n->DispatchEvent("click", {});
          event.StopImmediatePropagation();
          return;
        }
    }
    if (key == Rml::Input::KI_ESCAPE) {
      if (context_open_) {
        context_open_ = false;
        dirty_ = true;
      } else if (!popup_.empty()) {
        popup_.clear();
        confirm_ = {};
        text_accept_ = {};
        quit_after_save_ = false;
        dirty_ = true;
      } else if (!menu_.empty()) {
        menu_.clear();
        draft_preferences_ = nullptr;
        refresh();
      } else if (scene_ == "manager") {
        scene_ = "newgame";
        refresh();
      } else if (scene_ == "newgame" || scene_ == "mods") {
        scene_ = "main";
        refresh();
      }
      event.StopPropagation();
      return;
    }
    if (key == Rml::Input::KI_Z && event.GetParameter<int>("ctrl_key", 0) &&
        scene_ == "desktop" && menu_.empty() && !undo_.empty()) {
      desktop_ = undo_.back();
      undo_.pop_back();
      refresh();
      event.StopPropagation();
      return;
    }
    if (key == Rml::Input::KI_TAB &&
        (!menu_.empty() || !popup_.empty() || scene_ != "desktop")) {
      Rml::ElementList controls;
      auto *root = !popup_.empty() ? element("popup") : element("canvas");
      root->QuerySelectorAll(
          controls, !popup_.empty()
                        ? "button,input,select"
                        : ".window button,.window input,.window select");
      controls.erase(std::remove_if(controls.begin(), controls.end(),
                                    [](auto *n) {
                                      return !n->IsVisible() ||
                                             n->HasAttribute("disabled");
                                    }),
                     controls.end());
      const auto current = context_->GetFocusElement();
      const auto i = std::find(controls.begin(), controls.end(), current);
      if (!controls.empty()) {
        const int old = i == controls.end() ? -1 : int(i - controls.begin());
        const bool backwards = event.GetParameter<int>("shift_key", 0) != 0;
        const auto next = (old + (backwards ? -1 : 1) + int(controls.size())) %
                          int(controls.size());
        controls[size_t(next)]->Focus(true);
        event.StopImmediatePropagation();
      }
      return;
    }
  }
  if (type == "change") {
    change(target);
    return;
  }
  if (type == "input") {
    input(target);
    return;
  }
  if (type != "click" && type != "dblclick")
    return;
  if (suppress_click_) {
    suppress_click_ = false;
    event.StopPropagation();
    return;
  }
  if (type == "dblclick")
    return;
  for (auto *n = target; n && n != doc_; n = n->GetParentNode())
    if (n->HasAttribute("data-action")) {
      if (!n->HasAttribute("disabled"))
        dispatch(n->GetAttribute<Rml::String>("data-action", ""), n);
      return;
    }
} catch (const std::exception &e) {
  message(e.what());
}
void App::dispatch(const std::string &action, Rml::Element *target) {
  const auto id =
      target ? target->GetAttribute<Rml::String>("data-id", "") : std::string();
  if (action == "popup-close") {
    popup_.clear();
    confirm_ = {};
    text_accept_ = {};
    quit_after_save_ = false;
    dirty_ = true;
    return;
  }
  if (action == "popup-confirm") {
    auto fn = std::move(confirm_);
    auto text_fn = std::move(text_accept_);
    const auto text = field("popup-text");
    popup_.clear();
    confirm_ = {};
    text_accept_ = {};
    if (text_fn)
      text_fn(text);
    else if (fn)
      fn();
    dirty_ = true;
    return;
  }
  if (action == "exit") {
    closed_ = true;
    return;
  }
  if (action == "quit-without-save") {
    closed_ = true;
    return;
  }
  if (action.starts_with("context-")) {
    context_open_ = false;
    dirty_ = true;
    init_desktop();
    if (action == "context-quit") {
      if (!source_->dirty() && desktop_ == saved_desktop_) {
        confirmation("Spiel beenden?", [this] { closed_ = true; });
        return;
      }
      confirmation("Deine Änderungen wurden noch nicht gespeichert. Vor dem "
                   "Beenden speichern?",
                   [this] {
                     quit_after_save_ = true;
                     menu_ = "save";
                     refresh();
                   });
      popup_accept_ = "Speichern";
      popup_extra_ = btn("quit-without-save", "Nicht speichern");
      return;
    }
    if (action == "context-widgets" || action == "context-view-save" ||
        action == "context-view-load" || action == "context-background") {
      menu_ = action.substr(8);
      refresh();
      return;
    }
    if (action == "context-undo") {
      if (!undo_.empty()) {
        desktop_ = undo_.back();
        undo_.pop_back();
        refresh();
      }
      return;
    }
    auto before = desktop_;
    bool ok = true;
    if (action == "context-pin")
      desktop_["objects"][context_id_]["pinned"] =
          !desktop_["objects"][context_id_].value("pinned", true);
    else if (action == "context-pin-all") {
      bool all = true;
      for (const auto &[key, o] : desktop_["objects"].items()) {
        (void)key;
        if (o.value("visible", false) && !o.value("pinned", true))
          all = false;
      }
      for (auto &[key, o] : desktop_["objects"].items()) {
        (void)key;
        if (o.value("visible", false))
          o["pinned"] = !all;
      }
    } else if (action == "context-grid")
      desktop_["grid"] = !desktop_.value("grid", true);
    else if (action == "context-arrange")
      ok = arrange_desktop(desktop_);
    else if (action == "context-remove")
      desktop_["objects"][context_id_]["visible"] = false;
    else if (action == "context-size") {
      const auto split = id.find('x');
      ok = split != id.npos &&
           resize_desktop_widget(desktop_, context_id_,
                                 std::stoi(id.substr(0, split)),
                                 std::stoi(id.substr(split + 1)));
    }
    if (ok && desktop_ != before) {
      undo_.push_back(std::move(before));
      refresh();
    } else if (!ok)
      message("Kein freier Platz. Fixierte Widgets zuerst lösen oder andere "
              "Elemente entfernen.");
    return;
  }
  if (action == "widget-add") {
    auto before = desktop_;
    auto &o = desktop_["objects"].at(id);
    o["pinned"] = false;
    if (place_desktop_object(desktop_, id, 64, 224)) {
      undo_.push_back(before);
      menu_.clear();
      refresh();
    } else {
      desktop_ = before;
      message("Für dieses Widget ist kein freier Platz vorhanden.");
    }
    return;
  }
  if (action == "view-default") {
    undo_.push_back(desktop_);
    desktop_ = default_desktop();
    menu_.clear();
    refresh();
    return;
  }
  if (action == "view-save-confirm") {
    const auto name = field("view-name");
    launch::ModLibrary::validate_name(name);
    const auto file = user_dir() / "layouts" / editor::path(name + ".json");
    const auto save = [this, file] {
      const auto old_menu = menu_;
      menu_.clear();
      refresh();
      frame();
      auto image = file;
      image.replace_extension(".png");
      try {
        screenshot(image);
        editor::write_atomic(file, desktop_.dump(2));
        menu_.clear();
        refresh();
      } catch (...) {
        menu_ = old_menu;
        refresh();
        throw;
      }
    };
    if (fs::exists(file))
      confirmation("Gespeicherte Ansicht überschreiben?", save);
    else
      save();
    return;
  }
  if (action == "view-select") {
    selections_["view"] = id;
    refresh();
    return;
  }
  if (action == "view-load-confirm") {
    launch::ModLibrary::validate_name(selections_["view"]);
    const auto file =
        user_dir() / "layouts" / editor::path(selections_["view"] + ".json");
    auto next = Json::parse(editor::read_file(file));
    if (!valid_desktop(next))
      throw editor::Error("Ungültige Desktopansicht.");
    undo_.push_back(desktop_);
    desktop_ = std::move(next);
    menu_.clear();
    refresh();
    return;
  }
  if (action == "view-delete") {
    const auto name = selections_["view"];
    if (name.empty())
      return;
    launch::ModLibrary::validate_name(name);
    confirmation("Ansicht „" + name + "“ löschen?", [this, name] {
      const auto file = user_dir() / "layouts" / editor::path(name + ".json");
      fs::remove(file);
      auto image = file;
      image.replace_extension(".png");
      fs::remove(image);
      selections_["view"].clear();
      refresh();
    });
    return;
  }
  if (action == "close") {
    menu_.clear();
    draft_preferences_ = nullptr;
    if (scene_ != "desktop")
      scene_ = "main";
    refresh();
    return;
  }
  if (action == "mods") {
    collect_mods();
    scene_ = "mods";
    refresh();
    return;
  }
  if (action == "settings") {
    settings_category_ = "display";
    draft_preferences_ = nullptr;
    menu_ = "settings";
    refresh();
    return;
  }
  if (action == "load") {
    if (!source_) {
      if (auto *m = mod(active_mod_); m && m->type == "Modern")
        load_world(m->file);
      else {
        message("Bitte zuerst eine Modern-Mod aktivieren.");
        return;
      }
    }
    menu_ = "load";
    refresh();
    return;
  }
  if (action == "new") {
    if (auto *m = mod(active_mod_); m && m->type == "Classic") {
      try {
        const auto contents = content::open(m->file);
        launch::ModEntry entry{m->name,
                               "Classic",
                               contents->content_id,
                               {},
                               m->file.parent_path()};
        launch::ModLibrary library(root_);
        const auto user = library.user_directory(entry);
        SDL_HideWindow(video_.window);
        const int code =
            launch::run_child(executable_,
                              {"--run-mod", editor::utf8(m->file),
                               "--save-directory", editor::utf8(user)},
                              [] {});
        SDL_ShowWindow(video_.window);
        if (code)
          message("Classic wurde mit Fehlercode " + std::to_string(code) +
                  " beendet.");
        refresh();
      } catch (const std::exception &e) {
        SDL_ShowWindow(video_.window);
        message(e.what());
      }
      return;
    }
    if (!gui_.available()) {
      message("Die GUI-Ressourcen für Modern fehlen. Bitte das vollständige "
              "Modern-UI-Paket verwenden.");
      return;
    }
    if (auto *m = mod(active_mod_); m && m->type == "Modern")
      load_world(m->file);
    if (!source_) {
      message("Bitte eine Modern-Mod aktivieren.");
      return;
    }
    scene_ = "newgame";
    menu_.clear();
    refresh();
    return;
  }
  if (action == "mod-select") {
    selected_mod_ = id;
    refresh();
    return;
  }
  if (action == "mod-activate") {
    if (auto *m = mod(selected_mod_)) {
      if (!m->error.empty()) {
        message(m->error);
        return;
      }
      active_mod_ = m->name;
    }
    refresh();
    return;
  }
  if (action == "mod-remove") {
    if (auto *m = mod(selected_mod_)) {
      const auto name = m->name;
      confirmation(
          "Mod „" + name + "“ löschen? Persönliche Daten bleiben erhalten.",
          [this, name] {
            auto *m = mod(name);
            if (!m)
              return;
            launch::ModLibrary library(root_);
            library.remove({m->name, m->type, {}, {}, m->file.parent_path()});
            if (active_mod_ == name)
              active_mod_.clear();
            selected_mod_.clear();
            collect_mods();
            refresh();
          });
    }
    return;
  }
  if (action == "mod-rename") {
    auto *m = mod(selected_mod_);
    if (!m)
      return;
    const auto previous = m->name;
    text_dialog("Mod umbenennen", previous,
                [this, previous](const std::string &name) {
                  launch::ModLibrary::validate_name(name);
                  auto *m = mod(previous);
                  if (!m)
                    throw editor::Error("Mod ist nicht mehr vorhanden.");
                  launch::ModLibrary library(root_);
                  library.rename(
                      {m->name, m->type, {}, {}, m->file.parent_path()}, name);
                  if (active_mod_ == previous)
                    active_mod_ = name;
                  selected_mod_ = name;
                  collect_mods();
                  refresh();
                });
    return;
  }
  if (action == "import-modern") {
    NFD::Guard guard;
    NFD::UniquePath chosen;
    const nfdfilteritem_t filter[] = {{"OpenBMH-Welt", "obw"}};
    const auto result = NFD::OpenDialog(chosen, filter, 1);
    if (result == NFD_CANCEL)
      return;
    if (result != NFD_OKAY) {
      message("Die Dateiauswahl konnte nicht geöffnet werden.");
      return;
    }
    try {
      auto candidate = Catalog::open(editor::path(chosen.get()));
      std::string name = candidate->title();
      launch::ModLibrary::validate_name(name);
      const auto destination = root_ / "mods" / editor::path(name);
      if (fs::exists(destination))
        throw std::runtime_error(
            "Eine Mod mit diesem Namen ist bereits vorhanden.");
      fs::create_directories(destination.parent_path());
      const auto staging = destination.parent_path() /
                           (".ui-import-" + std::to_string(SDL_GetTicks64()));
      fs::create_directory(staging);
      try {
        fs::copy_file(editor::path(chosen.get()), staging / "content.obw");
        (void)Catalog::open(staging / "content.obw");
        fs::rename(staging, destination);
      } catch (...) {
        fs::remove_all(staging);
        throw;
      }
      collect_mods();
      selected_mod_ = name;
      refresh();
    } catch (const std::exception &e) {
      message(e.what());
    }
    return;
  }
  if (action == "import-classic") {
    NFD::Guard guard;
    NFD::UniquePath chosen;
    const auto result = NFD::PickFolder(chosen);
    if (result == NFD_CANCEL)
      return;
    if (result != NFD_OKAY) {
      message("Die Ordnerauswahl konnte nicht geöffnet werden.");
      return;
    }
    try {
      launch::AddMod request;
      request.source = editor::path(chosen.get());
      const auto game = launch::check_game_directory(request.source);
      request.name = game.family == launch::GameFamily::english ? "FTDL-classic"
                                                                : "BMH-classic";
      request.migrate = false;
      launch::ModLibrary library(root_);
      library.add(request, migrate_);
      collect_mods();
      selected_mod_ = request.name;
      refresh();
    } catch (const std::exception &e) {
      message(e.what());
    }
    return;
  }
  if (action == "setup-step") {
    const auto step = target->GetAttribute<int>("data-step", 0);
    const int limit = id == "duration"     ? 5
                      : id == "difficulty" ? 10
                      : id == "players"
                          ? (start_options_.value("league", true) ? 4 : 1)
                          : 3;
    if (id == "duration" || id == "difficulty" || id == "players" ||
        id == "mode")
      start_options_[id] =
          std::clamp(start_options_.value(id, 1) + step, 1, limit);
    refresh();
    return;
  }
  if (action == "setup-competition") {
    if (id == "league" || id == "world") {
      auto next = start_options_;
      next[id] = !next.value(id, true);
      if ((next.value("league", true) || next.value("world", true)) &&
          (next.value("league", true) || next.value("players", 1) == 1))
        start_options_ = std::move(next);
    }
    refresh();
    return;
  }
  if (action == "setup-next") {
    managers_draft_ = Json::array();
    manager_index_ = 0;
    adding_manager_ = false;
    manager_picker_ = false;
    selections_["portrait"] = "0";
    selections_["manager-name"] = "Alex Berger";
    scene_ = "manager";
    refresh();
    return;
  }
  if (action == "manager-clubs") {
    selections_["manager-name"] = field("manager-name");
    manager_picker_ = true;
    refresh();
    return;
  }
  if (action == "manager-clubs-back") {
    manager_picker_ = false;
    refresh();
    return;
  }
  if (action == "portrait-style") {
    selections_["manager-name"] = field("manager-name");
    selections_["portrait-style"] = id;
    refresh();
    return;
  }
  if (action == "portrait") {
    selections_["manager-name"] = field("manager-name");
    selections_["portrait"] = id;
    refresh();
    return;
  }
  if (action == "manager-club") {
    if (!world_->manageable(id))
      return;
    selected_club_ = id;
    country_ = "nation:de";
    league_ = world_->find("clubs", id)->value("competition", league_);
    manager_picker_ = false;
    refresh();
    return;
  }
  if (action == "manager-back") {
    scene_ = adding_manager_ ? "desktop" : "newgame";
    refresh();
    return;
  }
  if (action == "manager-next") {
    const auto name = field("manager-name");
    if (name.empty() || !world_->manageable(selected_club_)) {
      message("Bitte einen Namen und einen deutschen Verein wählen.");
      return;
    }
    const Json m = {
        {"name", name},
        {"club", selected_club_},
        {"portrait", selections_["portrait"].empty()
                         ? 0
                         : std::stoi(selections_["portrait"])},
        {"portrait_style",
         selections_["portrait-style"] == "painted" ? "painted" : "photo"},
        {"strength_choice", selections_["manager-strength"].empty()
                                ? 1
                                : std::stoi(selections_["manager-strength"])}};
    confirmation("Sind alle Eingaben richtig?", [this, m] {
      auto candidate = managers_draft_;
      candidate.push_back(m);
      if (!adding_manager_ &&
          manager_index_ + 1 < start_options_.value("players", 1)) {
        managers_draft_ = candidate;
        ++manager_index_;
        selections_["manager-name"] = std::array<const char *, 4>{
            "Alex Berger", "Chris Winter", "Robin Meier",
            "Jamie Sommer"}[size_t(manager_index_)];
        for (int p = 0; p < 12; ++p)
          if (std::none_of(
                  candidate.begin(), candidate.end(),
                  [&](const auto &v) { return v.value("portrait", 0) == p; })) {
            selections_["portrait"] = std::to_string(p);
            break;
          }
        refresh();
        return;
      }
      if (!apply("managers", candidate))
        return;
      managers_draft_ = std::move(candidate);
      apply("edit", {{"key", "setup"}, {"value", start_options_}});
      scene_ = adding_manager_ || !start_options_.value("league", true)
                   ? "desktop"
                   : "opening";
      opening_page_ = 0;
      draw_revealed_ = 0;
      refresh();
    });
    return;
  }
  if (action == "opening-play") {
    draw_playing_ = !draw_playing_;
    draw_at_ = SDL_GetTicks64() + 900;
    refresh();
    return;
  }
  if (action == "opening-next") {
    ++opening_page_;
    if (opening_page_ == 3)
      draw_at_ = SDL_GetTicks64() + 900;
    refresh();
    return;
  }
  if (action == "opening-all") {
    draw_revealed_ = 64;
    opening_page_ = 4;
    refresh();
    return;
  }
  if (action == "opening-finish") {
    if (opening_page_ != 4)
      return;
    scene_ = "desktop";
    apply("opening", "complete");
    refresh();
    return;
  }
  if (action == "desktop-context") {
    context_menu({}, 0, 0);
    return;
  }
  if (action.starts_with("open:")) {
    const auto route = action.substr(5);
    if (route == "kickoff")
      return;
    if (route == "newplayer") {
      const auto existing = snap().state.at("managers");
      if (existing.size() >= 4) {
        message("Alle vier Mitspielerplätze sind belegt.");
        return;
      }
      managers_draft_ = existing;
      manager_index_ = int(existing.size());
      adding_manager_ = true;
      manager_picker_ = false;
      selections_["manager-name"] = std::array<const char *, 4>{
          "Alex Berger", "Chris Winter", "Robin Meier",
          "Jamie Sommer"}[existing.size()];
      for (int p = 0; p < 12; ++p)
        if (std::none_of(existing.begin(), existing.end(), [&](const auto &v) {
              return v.value("portrait", 0) == p;
            })) {
          selections_["portrait"] = std::to_string(p);
          break;
        }
      menu_.clear();
      scene_ = "manager";
      refresh();
      return;
    }
    static const std::set<std::string> available = {
        "tables", "strengths", "camp", "settings", "save", "load"};
    if (!available.contains(route)) {
      message("Diese native Ansicht wird im nächsten Menüpaket übertragen.");
      return;
    }
    menu_ = route;
    if (route == "settings") {
      settings_category_ = "display";
      draft_preferences_ = nullptr;
    }
    refresh();
    return;
  }
  if (action == "ticket") {
    if (ticket_) {
      ticket_->open(target, device_ratio_);
      dirty_ = true;
    }
    return;
  }
  if (action == "camp-region") {
    selections_["camp-region"] = id;
    refresh();
    return;
  }
  if (action == "camp-select") {
    apply("camp.select", id);
    refresh();
    return;
  }
  if (action == "camp-remember") {
    if (apply("camp.remember", {}))
      message("Trainingslager für die Demo vorgemerkt.");
    return;
  }
  if (action == "settings-category") {
    settings_category_ = id;
    refresh();
    return;
  }
  if (action == "settings-theme") {
    draft_preferences_["theme"] = id;
    refresh();
    return;
  }
  if (action == "settings-apply") {
    if (settings_category_ == "display") {
      for (const auto *key : {"mode", "resolution", "scale"}) {
        const auto value = field(std::string("display-") + key);
        if (!value.empty())
          draft_preferences_[key] = value;
      }
    }
    if (settings_category_ == "sound") {
      draft_preferences_["effects"] =
          element("sound-effects")->HasAttribute("checked");
      draft_preferences_["sound-style"] = field("sound-style");
    }
    if (source_ && !apply("preferences", draft_preferences_))
      return;
    theme_ = draft_preferences_.value("theme", theme_);
    draft_preferences_ = nullptr;
    menu_.clear();
    refresh();
    return;
  }
  if (action == "save-select") {
    selections_["save"] = id;
    refresh();
    return;
  }
  if (action == "save-new") {
    selections_["save"].clear();
    refresh();
    return;
  }
  if (action == "save-confirm") {
    auto name =
        selections_["save"].empty() ? field("save-name") : selections_["save"];
    try {
      launch::ModLibrary::validate_name(name);
      const auto file = user_dir() / "saves" / editor::path(name + ".json");
      fs::create_directories(file.parent_path());
      const auto save = [this, file] {
        init_desktop();
        source_->save(file, desktop_);
        saved_desktop_ = desktop_;
        menu_.clear();
        if (quit_after_save_)
          closed_ = true;
        refresh();
      };
      if (fs::exists(file))
        confirmation("Vorhandenen Demo-Spielstand überschreiben?", save);
      else
        save();
    } catch (const std::exception &e) {
      message(e.what());
    }
    return;
  }
  if (action == "load-confirm") {
    const auto name = selections_["save"];
    if (name.empty())
      return;
    try {
      launch::ModLibrary::validate_name(name);
      desktop_ =
          source_->load(user_dir() / "saves" / editor::path(name + ".json"));
      saved_desktop_ = desktop_;
      theme_ = snap().state.at("preferences").value("theme", theme_);
      menu_.clear();
      scene_ = "desktop";
      refresh();
    } catch (const std::exception &e) {
      message(e.what());
    }
    return;
  }
  if (action == "save-rename") {
    const auto previous = selections_["save"];
    if (previous.empty())
      return;
    text_dialog(
        "Speicherstand umbenennen", previous,
        [this, previous](const std::string &name) {
          launch::ModLibrary::validate_name(name);
          if (name == previous)
            return;
          const auto folder = user_dir() / "saves",
                     old_file = folder / editor::path(previous + ".json"),
                     new_file = folder / editor::path(name + ".json");
          if (fs::exists(new_file))
            throw editor::Error("Diese Bezeichnung ist bereits vorhanden.");
          fs::rename(old_file, new_file);
          selections_["save"] = name;
          refresh();
        });
    return;
  }
  if (action == "save-delete") {
    const auto name = selections_["save"];
    if (name.empty())
      return;
    confirmation("Demo-Spielstand „" + name + "“ löschen?", [this, name] {
      launch::ModLibrary::validate_name(name);
      fs::remove(user_dir() / "saves" / editor::path(name + ".json"));
      selections_["save"].clear();
      refresh();
    });
    return;
  }
  if (action == "player-detail") {
    if (const auto *p = world_->find("players", id))
      message(world_->name(*p) + " · Stärke " +
              std::to_string(
                  p->value("attributes", Json::object()).value("strength", 0)));
    return;
  }
  if (action == "club-detail") {
    if (const auto *c = world_->find("clubs", id))
      message(world_->name(*c) + " · " +
              std::to_string(world_->squad(id).size()) + " Spieler");
    return;
  }
  if (action == "squad-tactics") {
    menu_ = "tactics";
    refresh();
    return;
  }
}
void App::change(Rml::Element *target) {
  const auto kind = target->GetAttribute<Rml::String>("data-change", "");
  auto *control = dynamic_cast<Rml::ElementFormControl *>(target);
  if (!control)
    return;
  const auto value = control->GetValue();
  if (kind == "manager-country" || kind == "manager-league") {
    if (!manager_picker_)
      selections_["manager-name"] = field("manager-name");
    if (kind == "manager-country") {
      country_ = value;
      league_.clear();
    } else
      league_ = value;
    refresh();
  } else if (kind == "manager-strength") {
    selections_["manager-strength"] = value;
  } else if (kind == "table-country") {
    country_ = value;
    league_.clear();
    refresh();
  } else if (kind == "table-league") {
    league_ = value;
    refresh();
  } else if (kind == "table-scope") {
    selections_["table-scope"] = value;
  }
}
void App::input(Rml::Element *target) {
  const auto kind = target->GetAttribute<Rml::String>("data-input", "");
  auto *control = dynamic_cast<Rml::ElementFormControl *>(target);
  if (!control)
    return;
  const auto value = control->GetValue();
  if (kind == "sound-volume") {
    const int n = std::stoi(value);
    draft_preferences_["volume"] = n;
    element("volume-value")->SetInnerRML(std::to_string(n) + " %");
    dirty_ = true;
  }
  if (kind == "camp-focus") {
    const auto i = std::stoi(target->GetAttribute<Rml::String>("data-id", "0"));
    if (i < 0 || i > 2)
      return;
    auto focus = snap().state.at("camp").at("focus");
    focus[size_t(i)] = std::stoi(value);
    if (apply("camp.focus", focus)) {
      element("camp-value-" + std::to_string(i))->SetInnerRML(value + " / 15");
      const int rest =
          15 -
          (focus[0].get<int>() + focus[1].get<int>() + focus[2].get<int>()) / 3;
      element("camp-value-3")->SetInnerRML(std::to_string(rest) + " / 15");
      if (auto *c =
              dynamic_cast<Rml::ElementFormControl *>(element("camp-focus-3")))
        c->SetValue(std::to_string(rest));
      dirty_ = true;
    }
  }
}
void App::script() {
  std::ifstream stream(config_.commands);
  if (!stream)
    throw editor::Error("UI-Skript kann nicht gelesen werden.");
  const auto locate = [&](const Json &command) {
    if (command.contains("selector")) {
      context_->Update();
      auto *node =
          doc_->QuerySelector(command.at("selector").get<std::string>());
      if (!node || !node->IsVisible(true))
        throw editor::Error("UI-Skript: Element nicht sichtbar.");
      const auto at = node->GetAbsoluteOffset(Rml::BoxArea::Border),
                 size = node->GetBox().GetSize(Rml::BoxArea::Border);
      return Rml::Vector2i{int(at.x + size.x * .5f), int(at.y + size.y * .5f)};
    }
    return Rml::Vector2i{command.at("x").get<int>(),
                         command.at("y").get<int>()};
  };
  std::string line;
  size_t index = 0;
  while (std::getline(stream, line)) {
    ++index;
    if (line.empty())
      continue;
    try {
      const auto command = Json::parse(line);
      const auto type = command.value("type", std::string());
      if (type == "click" || type == "right") {
        const auto point = locate(command);
        const int button = type == "right" ? 1 : 0;
        context_->ProcessMouseMove(point.x, point.y, 0);
        context_->ProcessMouseButtonDown(button, 0);
        context_->ProcessMouseButtonUp(button, 0);
      } else if (type == "drag") {
        const auto point = locate(command),
                   end = Rml::Vector2i{command.at("to_x"), command.at("to_y")};
        context_->ProcessMouseMove(point.x, point.y, 0);
        context_->ProcessMouseButtonDown(0, 0);
        for (int i = 1; i <= 12; ++i)
          context_->ProcessMouseMove(point.x + (end.x - point.x) * i / 12,
                                     point.y + (end.y - point.y) * i / 12, 0);
        context_->ProcessMouseButtonUp(0, 0);
      } else if (type == "text")
        context_->ProcessTextInput(command.value("value", std::string()));
      else if (type == "key") {
        const std::map<std::string, Rml::Input::KeyIdentifier> keys = {
            {"ESC", Rml::Input::KI_ESCAPE}, {"ENTER", Rml::Input::KI_RETURN},
            {"TAB", Rml::Input::KI_TAB},    {"SPACE", Rml::Input::KI_SPACE},
            {"HOME", Rml::Input::KI_HOME},  {"END", Rml::Input::KI_END},
            {"LEFT", Rml::Input::KI_LEFT},  {"RIGHT", Rml::Input::KI_RIGHT},
            {"UP", Rml::Input::KI_UP},      {"DOWN", Rml::Input::KI_DOWN},
            {"A", Rml::Input::KI_A},        {"Z", Rml::Input::KI_Z}};
        const auto name = command.at("value").get<std::string>();
        if (!keys.contains(name))
          throw editor::Error("Unbekannte UI-Skripttaste.");
        const int mods =
            (command.value("ctrl", false) ? Rml::Input::KM_CTRL : 0) |
            (command.value("shift", false) ? Rml::Input::KM_SHIFT : 0);
        context_->ProcessKeyDown(keys.at(name), mods);
        context_->ProcessKeyUp(keys.at(name), mods);
      } else if (type == "screenshot")
        screenshot(editor::path(command.at("path").get<std::string>()));
      else if (type == "checkpoint") {
        init_desktop();
        source_->save(editor::path(command.at("path").get<std::string>()),
                      desktop_);
      } else if (type == "snapshot")
        editor::write_atomic(
            editor::path(command.at("path").get<std::string>()),
            Json{{"scene", scene_},
                 {"menu", menu_},
                 {"desktop", desktop_},
                 {"demo", source_ ? snap().state : Json()}}
                .dump(2));
      else if (type == "dom")
        editor::write_atomic(
            editor::path(command.at("path").get<std::string>()),
            doc_->GetInnerRML());
      else if (type == "assert") {
        if (command.contains("scene") &&
            scene_ != command.at("scene").get<std::string>())
          throw editor::Error("UI-Skript: falsche Szene.");
        if (command.contains("menu") &&
            menu_ != command.at("menu").get<std::string>())
          throw editor::Error("UI-Skript: falsches Menü.");
        if (command.contains("selector")) {
          auto *node =
              doc_->QuerySelector(command.at("selector").get<std::string>());
          const bool present = node && node->IsVisible(true);
          if (present != command.value("visible", true))
            throw editor::Error("UI-Skript: falsche Sichtbarkeit.");
          if (command.contains("disabled") &&
              (!node || node->HasAttribute("disabled") !=
                            command.at("disabled").get<bool>()))
            throw editor::Error("UI-Skript: falscher Sperrstatus.");
        }
        if (command.contains("ticket") &&
            snap().state.at("ticket") != command.at("ticket"))
          throw editor::Error("UI-Skript: falscher Eintrittspreis.");
      } else
        throw editor::Error("Unbekannter UI-Skriptbefehl: " + type);
      frame();
    } catch (const std::exception &e) {
      throw editor::Error("UI-Skript Zeile " + std::to_string(index) + ": " +
                          e.what());
    }
  }
}
