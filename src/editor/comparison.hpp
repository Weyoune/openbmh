// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "editor/types.hpp"

namespace bmh::editor {
class World;
// Read-only projections. Session caches the unfiltered data at its revision;
// searching, sorting and paging always operate on the complete projection.
Json comparison_data(const World &, const Json &args);
Json query_comparison(const Json &data, const Json &args);
} // namespace bmh::editor
