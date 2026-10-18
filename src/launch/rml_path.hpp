// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <string>

namespace bmh::rml {
// RmlUI parses document locations as URLs. Native Windows backslashes make
// URL::GetPath() empty, so relative image sources lose their asset directory.
inline std::string asset_path(const std::filesystem::path &path) {
    const auto utf8 = path.generic_u8string();
    return {utf8.begin(), utf8.end()};
}
} // namespace bmh::rml
