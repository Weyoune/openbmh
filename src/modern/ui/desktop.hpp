// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "modern/ui/catalog.hpp"

namespace bmh::modern_ui {
struct WidgetDefinition {
  std::string id, name;
  std::vector<std::pair<int, int>> sizes;
};
const std::vector<WidgetDefinition> &widget_definitions();
const std::vector<std::string> &desktop_routes();
// These operations stage the whole layout. Failed placement leaves it
// untouched.
Json default_desktop();
bool valid_desktop(const Json &);
bool place_desktop_object(Json &, const std::string &, int x, int y,
                          bool nearest = true);
bool resize_desktop_widget(Json &, const std::string &, int columns, int rows);
bool arrange_desktop(Json &);
} // namespace bmh::modern_ui
