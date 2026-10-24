// SPDX-License-Identifier: GPL-3.0-only
void App::close_contracts(bool focus) {
  auto restore = contracts_return_focus_;
  if (focus && !restore.empty()) {
    const auto snapshot = snap();
    const auto club = contracts_text(contracts_field(contract_draft_, "club"), "");
    if ((contracts_world_ && snapshot.world != contracts_world_) ||
        (!club.empty() && contracts_managed(snapshot) != club) ||
        (restore.starts_with("contract-player-") &&
         !contracts_row(contracts_projection(snapshot), contracts_player_).is_object()))
      restore = "contracts-tab-" + std::to_string(contracts_tab_);
    focus_after_render_ = restore;
  }
  contract_draft_ = Json();
  contracts_dialog_.clear();
  contracts_notice_.clear();
  contracts_return_focus_.clear();
  contracts_world_.reset();
  contracts_revision_ = 0;
  refresh();
}

bool App::contracts_action(const std::string &action, Rml::Element *target) {
  if (menu_ != "contracts") return false;
  constexpr std::array<std::string_view, 10> actions = {"contracts-tab", "contract-player", "contract-close",
      "contract-term", "contract-salary", "contract-submit", "contract-insurance",
      "contract-insure", "contracts-bonuses", "contract-bonus"};
  if (std::find(actions.begin(), actions.end(), action) == actions.end()) return false;
  const auto id = target ? target->GetAttribute<Rml::String>("data-id", "") : std::string();
  const auto integer_id = [&]() -> std::optional<int> {
    if (id.empty() || id.size() > 3 ||
        std::any_of(id.begin(), id.end(), [](char c) { return c < '0' || c > '9'; })) return {};
    return std::stoi(id);
  };
  const auto reject = [&](const std::string &text) {
    contract_draft_ = {{"notice_title", "Nicht übernommen"},
                       {"club", contracts_text(contracts_field(contract_draft_, "club"), "")}};
    contracts_notice_ = text;
    contracts_dialog_ = "notice";
    focus_after_render_ = "contract-close";
    refresh();
  };
  const auto bound = [&](bool player) {
    return contracts_bound(snap(), contracts_revision_, contracts_world_,
        contracts_text(contracts_field(contract_draft_, "club"), ""), player ? contracts_player_ : "");
  };
  if (action == "contract-close") {
    close_contracts(true);
    return true;
  }
  if (action == "contracts-tab") {
    const auto tab = integer_id();
    if (!contracts_dialog_.empty() || !tab || *tab > 2) return true;
    contracts_tab_ = *tab;
    focus_after_render_ = "contracts-tab-" + std::to_string(*tab);
    refresh();
    return true;
  }
  if (action == "contract-player" || action == "contracts-bonuses") {
    if (!contracts_dialog_.empty()) return true;
    const auto snapshot = snap();
    const auto projection = contracts_projection(snapshot);
    if (!snapshot.world || !projection.value("ok", false)) return true;
    const auto row = action == "contract-player" ? contracts_row(projection, id) : Json();
    if (action == "contract-player" && !row.is_object()) return true;
    contracts_world_ = snapshot.world;
    contracts_revision_ = snapshot.revision;
    contracts_return_focus_ = target ? target->GetId() : "contracts-bonuses";
    contracts_notice_.clear();
    contract_draft_ = {{"club", projection.at("club")}};
    if (action == "contracts-bonuses") {
      contracts_player_.clear();
      contract_draft_["bonuses"] = projection.at("bonuses");
      contracts_dialog_ = "bonuses";
      focus_after_render_ = "contract-bonus-0";
    } else {
      contracts_player_ = id;
      const auto factor = finance_currency_factor(*snapshot.world);
      const auto &salary = contracts_field(row, "salary");
      contract_draft_["row"] = row;
      // The editor and the finance action both use whole display units. The
      // source alone multiplies the accepted value by its currency factor.
      contract_draft_["salary"] = salary.is_number_integer()
          ? Json(salary.get<int64_t>() / factor) : Json();
      contract_draft_["months"] = contracts_field(row, "months");
      contracts_dialog_ = "offer";
      focus_after_render_ = "contract-term-12";
    }
    refresh();
    return true;
  }
  if (action == "contract-term") {
    const auto term = integer_id();
    if (contracts_dialog_ != "offer" || !term ||
        !(*term == 12 || *term == 24 || *term == 36 || *term == 48)) return true;
    contract_draft_["months"] = *term;
    focus_after_render_ = "contract-term-" + std::to_string(*term);
    refresh();
    return true;
  }
  if (action == "contract-salary") {
    if (contracts_dialog_ != "offer" || !contracts_world_) return true;
    const auto player = contracts_player_;
    const auto world = contracts_world_;
    const auto revision = contracts_revision_;
    const auto &value = contracts_field(contract_draft_, "salary");
    AmountRequest request;
    const auto unit = money_unit();
    request.title = "Angebotenes Monatsgehalt in " + (unit == "€" ? std::string("Euro") : unit);
    request.unit = unit;
    request.return_focus = "contract-salary";
    request.value = value.is_number_integer() ? value.get<int64_t>() : 0;
    request.min = 0;
    request.max = 200000;
    request.step = 1;
    request.suggestion_step = 5000;
    request.review = true;
    request.accept = [this, player, world, revision](int64_t amount) {
      if (menu_ != "contracts" || contracts_dialog_ != "offer" ||
          contracts_player_ != player || contracts_world_ != world ||
          contracts_revision_ != revision || amount < 0 || amount > 200000) return false;
      contract_draft_["salary"] = amount;
      refresh();
      return true;
    };
    open_amount(std::move(request));
    return true;
  }
  if (action == "contract-insurance") {
    if (contracts_dialog_ != "offer") return true;
    if (!bound(true)) {
      reject("Der Vertragsstand hat sich geändert. Bitte den Spieler erneut auswählen.");
      return true;
    }
    // Opening insurance discards the proposal. Cancelling it returns to the
    // roster, never to the previous salary or term draft.
    const auto row = contract_draft_.at("row"), club = contract_draft_.at("club");
    contract_draft_ = {{"row", row}, {"club", club}};
    contracts_dialog_ = "insurance";
    focus_after_render_ = "contract-insure-0";
    refresh();
    return true;
  }
  if (action == "contract-submit" || action == "contract-insure") {
    const bool offer = action == "contract-submit";
    if (contracts_dialog_ != (offer ? "offer" : "insurance")) return true;
    if (!bound(true)) {
      reject("Der Vertragsstand hat sich geändert. Bitte den Spieler erneut auswählen.");
      return true;
    }
    Json payload;
    if (offer) {
      const auto &salary = contracts_field(contract_draft_, "salary");
      const auto &months = contracts_field(contract_draft_, "months");
      if (!salary.is_number_integer() || salary < 0 || salary > 200000 ||
          !months.is_number_integer() || months < 1 || months > 48) return true;
      payload = {{"kind", "contract-offer"}, {"player", contracts_player_},
                 {"salary", salary}, {"months", months}};
    } else {
      const auto duration = integer_id();
      if (!duration || !(*duration == 0 || *duration == 12 || *duration == 24)) return true;
      payload = {{"kind", "insurance"}, {"player", contracts_player_}, {"value", *duration}};
    }
    const auto result = source_->apply({contracts_revision_, "finance", payload});
    if (!result.ok) {
      reject("Die Änderung konnte nicht übernommen werden. Bitte die Vertragsdaten erneut öffnen.");
      return true;
    }
    if (!offer) {
      close_contracts(true);
      return true;
    }
    const auto row = contract_draft_.at("row");
    const auto salary = contract_draft_.at("salary").get<int64_t>();
    const auto months = contract_draft_.at("months").get<int>();
    const auto club = contract_draft_.at("club");
    const auto factor = finance_currency_factor(*contracts_world_);
    contracts_notice_ = contracts_text(contracts_field(row, "name")) + " · " +
        contracts_money(Json(salary * factor), *contracts_world_) + " · " + std::to_string(months) + " Monate";
    contract_draft_ = {{"notice_title", "Vertrag vorgemerkt"}, {"club", club}};
    contracts_revision_ = result.revision;
    contracts_dialog_ = "notice";
    focus_after_render_ = "contract-close";
    refresh();
    return true;
  }
  if (action == "contract-bonus") {
    const auto index = integer_id();
    if (contracts_dialog_ != "bonuses" || !index || *index > 6 || !contracts_world_) return true;
    const auto &bonuses = contracts_field(contract_draft_, "bonuses");
    if (!bonuses.is_array() || size_t(*index) >= bonuses.size()) return true;
    const auto bonus = bonuses[size_t(*index)];
    const auto world = contracts_world_;
    const auto revision = contracts_revision_;
    const auto club = contracts_text(contracts_field(contract_draft_, "club"), "");
    AmountRequest request;
    request.title = contracts_text(contracts_field(bonus, "name"));
    request.unit = money_unit();
    request.return_focus = "contract-bonus-" + std::to_string(*index);
    request.value = bonus.at("value").get<int64_t>();
    request.min = 0;
    request.max = 255;
    request.step = 1;
    request.suggestion_step = 1;
    request.review = true;
    request.accept = [this, world, revision, club, index = *index](int64_t amount) {
      if (menu_ != "contracts" || contracts_dialog_ != "bonuses" || contracts_world_ != world ||
          contracts_revision_ != revision || amount < 0 || amount > 255 ||
          !contracts_bound(snap(), revision, world, club)) return false;
      const auto result = source_->apply({revision, "finance",
          {{"kind", "bonus"}, {"index", index}, {"value", amount}}});
      if (!result.ok) return false;
      contract_draft_["bonuses"][size_t(index)]["value"] = amount;
      contracts_revision_ = result.revision;
      refresh();
      return true;
    };
    open_amount(std::move(request));
    return true;
  }
  return true;
}

bool App::contracts_event(Rml::Event &event) {
  if (menu_ != "contracts" || contracts_dialog_.empty() || amount_) return false;
  const auto type = event.GetType();
  const bool keyboard = type == "keydown";
  const auto key = keyboard ? Rml::Input::KeyIdentifier(event.GetParameter<int>("key_identifier", 0))
                            : Rml::Input::KI_UNKNOWN;
  if ((keyboard && key == Rml::Input::KI_ESCAPE) || type == "contextmenu" ||
      (type == "mouseup" && event.GetParameter<int>("button", 0) == 1)) {
    close_contracts(true);
    event.StopImmediatePropagation();
    return true;
  }
  if (keyboard && key == Rml::Input::KI_TAB) {
    Rml::ElementList buttons;
    if (auto *dialog = element("contracts-dialog")) dialog->QuerySelectorAll(buttons, "button");
    buttons.erase(std::remove_if(buttons.begin(), buttons.end(), [](auto *button) {
      return button->HasAttribute("disabled");
    }), buttons.end());
    if (!buttons.empty()) {
      const auto current = std::find(buttons.begin(), buttons.end(), context_->GetFocusElement());
      const int count = int(buttons.size());
      const int old = current == buttons.end() ? -1 : int(current - buttons.begin());
      const bool backwards = event.GetParameter<int>("shift_key", 0) != 0;
      const int next = old < 0 ? backwards ? count - 1 : 0 : (old + (backwards ? -1 : 1) + count) % count;
      buttons[size_t(next)]->Focus(true);
    }
    event.StopImmediatePropagation();
    return true;
  }
  bool inside = false;
  for (auto *node = event.GetTargetElement(); node && node != doc_; node = node->GetParentNode())
    if (node->GetId() == "contracts-dialog") inside = true;
  if (!inside && (type == "click" || type == "dblclick" || type == "mousedown" ||
                  type == "mouseup" || keyboard)) {
    event.StopImmediatePropagation();
    return true;
  }
  return false;
}
