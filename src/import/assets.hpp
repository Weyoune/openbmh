// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "import/classic.hpp"

namespace bmh::obw {
cli::Json portrait_recipe(const std::string& legacy_name, const LeImage& exe);
// PNGs retain original resolution. Portrait PARTS are exported, never composed faces.
void import_assets(Documents& out, const std::filesystem::path& root,
                   const Database& db, const LeImage& exe);
void load_assets(const Documents& out, ClassicData& data);
} // namespace bmh::obw
