// SPDX-License-Identifier: GPL-3.0-only
bool App::bank_action(const std::string &action, Rml::Element *target) {
  if (menu_ != "bank" || !source_ || !world_) return false;
  if (action == "close") {
    if (bank_view_ == "entrance") return false;
    bank_view_ = "entrance";
    bank_hint_.clear();
    focus_after_render_ = "window-close";
    refresh();
    return true;
  }
  if (action == "bank-view" && target) {
    const auto view = target->GetAttribute<Rml::String>("data-view", "");
    if (view != "properties" && view != "stocks" && view != "overview" && view != "savings" && view != "credit") return true;
    bank_view_ = view;
    bank_hint_.clear();
    focus_after_render_ = "window-close";
    refresh();
    return true;
  }
  if (action == "property-next") {
    if (bank_view_ != "properties") return true;
    bank_property_ = (bank_property_ + 1) % 6;
    focus_after_render_ = "property-next";
    refresh();
    return true;
  }
  if (action == "property-owned-next") {
    if (bank_view_ != "properties") return true;
    const auto state = snap().state;
    for (int step = 1; step <= 6; ++step) {
      const int next = (bank_owned_ + step) % 6;
      if (state.at("finance").at("properties")[size_t(next)] != 0) { bank_owned_ = next; break; }
    }
    focus_after_render_ = "property-owned-next";
    refresh();
    return true;
  }
  if (action == "bank-stock" && target) {
    const auto index = target->GetAttribute<int>("data-id", -1);
    if (bank_view_ != "stocks" || index < 0 || index > 11) return true;
    bank_stock_ = index;
    focus_after_render_ = "bank-stock-" + std::to_string(index);
    refresh();
    return true;
  }
  if (action != "bank-calc" || !target) return false;
  const auto kind = target->GetAttribute<Rml::String>("data-kind", "");
  const bool property = kind == "property-buy" || kind == "property-sell";
  const bool stock = kind == "stock-buy" || kind == "stock-sell";
  if ((property && bank_view_ != "properties") || (stock && bank_view_ != "stocks") ||
      ((kind == "deposit" || kind == "withdraw") && bank_view_ != "savings") ||
      ((kind == "loan" || kind == "repay") && bank_view_ != "credit")) return true;
  const auto snapshot = snap();
  Json quote_request = {{"kind", kind}};
  if (property) quote_request["index"] = kind == "property-buy" ? bank_property_ : bank_owned_;
  if (stock) quote_request["index"] = bank_stock_;
  const auto quote = demo_bank_quote(snapshot.state, quote_request, *world_);
  if (!quote.value("ok", false)) return true;
  const auto world = world_;
  const auto *source = source_.get();
  const auto view = bank_view_;
  const auto revision = snapshot.revision;
  const auto unit_price = quote.at("unit_price").get<int64_t>();
  AmountRequest request;
  const std::map<std::string, std::string> titles{{"deposit", "Einzahlen"}, {"withdraw", "Abheben"},
      {"loan", "Kredit aufnehmen"}, {"repay", "Kredit tilgen"}, {"property-buy", "Immobilien kaufen"},
      {"property-sell", "Immobilien verkaufen"}, {"stock-buy", "Aktien kaufen"}, {"stock-sell", "Aktien verkaufen"}};
  request.title = titles.at(kind);
  request.unit = property || stock ? "Stück" : money_unit();
  request.return_focus = target->GetId();
  request.value = 0;
  request.min = quote.at("min");
  request.max = quote.at("max");
  request.step = 1;
  request.suggestion_step = quote.at("step");
  request.review = true;
  if (property || stock) request.review_text = [world, unit_price](int64_t count) {
    return "Gesamtbetrag: " + money(count * unit_price, world->manifest().value("currency", Json::object()));
  };
  request.accept = [this, world, source, view, revision, quote_request, kind](int64_t value) {
    if (world != world_ || source != source_.get() || menu_ != "bank" || bank_view_ != view) return false;
    auto action = quote_request;
    action["value"] = value;
    const bool accepted = commit_finance(action, revision);
    if (accepted && kind == "property-buy") {
      bank_owned_ = quote_request.at("index").get<int>();
      refresh();
    }
    return accepted;
  };
  open_amount(std::move(request));
  return true;
}

bool App::bank_event(Rml::Event &event) {
  if (menu_ != "bank" || !source_ || amount_ || !popup_.empty()) return false;
  const auto type = event.GetType();
  auto *target = event.GetTargetElement();
  const bool keyboard = type == "keydown";
  const auto key = keyboard ? Rml::Input::KeyIdentifier(event.GetParameter<int>("key_identifier", 0)) : Rml::Input::KI_UNKNOWN;
  if (bank_view_ != "entrance" && ((keyboard && key == Rml::Input::KI_ESCAPE) ||
      (type == "mouseup" && event.GetParameter<int>("button", 0) == 1))) {
    if (keyboard)
      if (auto *select = dynamic_cast<Rml::ElementFormControlSelect *>(element("bank-term"));
          select && select->IsSelectBoxVisible()) {
        select->CancelSelectBox();
        select->Focus(true);
        dirty_ = true;
        event.StopImmediatePropagation();
        return true;
      }
    bank_action("close", nullptr);
    event.StopImmediatePropagation();
    return true;
  }
  if (type == "contextmenu" && bank_view_ != "entrance") { event.StopImmediatePropagation(); return true; }
  if (type == "click" && bank_view_ != "entrance")
    for (auto *node = target; node && node != doc_; node = node->GetParentNode())
      if (node->GetAttribute<Rml::String>("data-action", "") == "close") {
        bank_action("close", node);
        event.StopImmediatePropagation();
        return true;
      }
  if (type == "change" && target->GetId() == "bank-term") {
    auto *select = dynamic_cast<Rml::ElementFormControlSelect *>(target);
    if (!select || bank_view_ != "credit" || snap().state.at("debt") != 0) return true;
    const auto raw = select->GetValue();
    if (raw.empty() || raw.size() > 2 || raw.find_first_not_of("0123456789") != raw.npos) return true;
    const int value = std::stoi(raw);
    if (value < 6 || value > 60 || value % 6) return true;
    // Initializing a native select may emit change; opening a view is read-only.
    if (snap().state.at("finance").at("term") == value) {
      event.StopImmediatePropagation();
      return true;
    }
    focus_after_render_ = "bank-term";
    commit_finance({{"kind", "term"}, {"value", value}}, snap().revision);
    event.StopImmediatePropagation();
    return true;
  }
  if (bank_view_ == "entrance" && (type == "mousemove" || type == "focus")) {
    const auto hint = [&](Rml::Element *from) {
      for (auto *node = from; node && node != doc_; node = node->GetParentNode())
        if (node->HasAttribute("data-bank-hint")) return node->GetAttribute<Rml::String>("data-bank-hint", "");
      return Rml::String();
    };
    auto text = hint(target);
    // RmlUI dispatches focus to the whole new ancestor chain before updating
    // Context's focus pointer. Ancestor notifications must not erase a hint
    // just supplied by the actual hotspot. The close button resets it.
    if (type == "focus" && text.empty() && target->GetId() != "window-close") return false;
    if (type == "mousemove" && text.empty()) text = hint(context_->GetFocusElement());
    if (bank_hint_ != text) {
      bank_hint_ = text;
      if (auto *label = element("bank-interaction-hint"))
        label->SetInnerRML("<span>" + esc(text.empty() ? "Ein Element in der Bankhalle auswählen." : text) + "</span>");
      dirty_ = true;
    }
  }
  return false;
}
