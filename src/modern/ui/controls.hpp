// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "modern/ui/catalog.hpp"
#include <RmlUi/Core.h>
#include <cmath>

namespace bmh::modern_ui {
inline Rml::Element *select_part(Rml::Element &select, const std::string &tag) {
  // WidgetDropDown appends these as non-DOM children. QuerySelector excludes
  // them.
  for (int i = 0; i < select.GetNumChildren(true); ++i)
    if (auto *child = select.GetChild(i); child->GetTagName() == tag)
      return child;
  return nullptr;
}
inline std::string button_markup(const std::string &action,
                                 const std::string &label,
                                 const std::string &css = {},
                                 const std::string &attributes = {}) {
  // RmlUI needs an explicit flex item for text; anonymous text disappears.
  const bool plain = label.find('<') == std::string::npos;
  return "<button data-action=\"" + editor::escaped(action) + "\" class=\"" +
         css + (plain ? " control-button" : "") + "\" " + attributes + ">" +
         (plain ? "<span class=\"control-label\">" + label + "</span>"
                : label) +
         "</button>";
}
inline bool center_selects(Rml::ElementDocument &document) {
  bool changed = false;
  Rml::ElementList controls;
  document.QuerySelectorAll(controls, "select");
  const auto set_pixels = [&](Rml::Element *element, Rml::PropertyId name,
                              float value) {
    const auto *old = element->GetLocalProperty(name);
    if (old && old->unit == Rml::Unit::PX &&
        std::abs(old->Get<float>() - value) < .02f)
      return;
    element->SetProperty(name, Rml::Property(value, Rml::Unit::PX));
    changed = true;
  };
  for (auto *select : controls) {
    if (!select->IsVisible(true))
      continue;
    auto *caption = select_part(*select, "selectvalue"),
         *arrow = select_part(*select, "selectarrow");
    const float height = select->GetBox().GetSize(Rml::BoxArea::Content).y;
    if (height <= 0 || !caption || !arrow)
      continue;
    // WidgetDropDown fixes both offsets at the content-box top. A flex
    // selectvalue loses its text in pinned RmlUI; center its line instead.
    set_pixels(caption, Rml::PropertyId::LineHeight, height);
    set_pixels(arrow, Rml::PropertyId::MarginTop,
               std::max(0.f, (height -
                              arrow->GetBox().GetSize(Rml::BoxArea::Border).y) /
                                 2));
  }
  return changed;
}
} // namespace bmh::modern_ui
