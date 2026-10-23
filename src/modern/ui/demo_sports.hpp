// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "modern/ui/catalog.hpp"

namespace bmh::modern_ui {
// UI fixtures from the HTML reference; these never run a training/match rule.
Json demo_training(const Catalog &, const std::string &club);
bool valid_demo_training(const Json &, const Catalog &,
                         const std::string &club);
std::string edit_demo_training(Json &, const Json &action);
Json demo_tactics();
bool valid_demo_tactics(const Json &);
std::string edit_demo_tactics(Json &, const Json &action);
std::string tactics_diagram(const Json &, const std::string &theme);
} // namespace bmh::modern_ui
