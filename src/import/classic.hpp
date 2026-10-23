// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "import/obw.hpp"
#include "formats/content.hpp"
#include "formats/palette.hpp"

namespace bmh::obw {
// Installation content only. No manager choices, career RNG or save state.
using ClassicData = content::Classic;

void complete_classic(Documents& out, const std::filesystem::path& root,
                      const Database& database, const LeImage& executable,
                      const LeImage& source_tables, bool english, const Options& options);
void validate_classic(const Documents& out);
// For an editor after deliberate changes. CRC32 detects damage, not authenticity.
void refresh_classic_inventory(Documents& out);
ClassicData load_classic(const Documents& out);
// The Classic installation payload (x-classic, x-legacy, assets) of a world that is not a
// sealed Classic package, such as a Modern world built on an imported installation. Only the
// payload is checked; the world tables are the caller's business (docs/new_engine).
ClassicData load_classic_payload(const Documents& out);
} // namespace bmh::obw
