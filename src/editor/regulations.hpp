// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "editor/types.hpp"

namespace bmh::editor {
class World;
// Presentation of declared rules and versioned Classic policy, never a season
// simulation or an editable replacement for world/rules.json.
Json regulation_view(const World &, const Json &args);
} // namespace bmh::editor
