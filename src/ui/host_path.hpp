// SPDX-License-Identifier: GPL-3.0-only
// Tiny helper for showing host directory paths in the UI. The legacy font has no usable backslash glyph,
// so save_directory_label() always formats paths with portable forward separators.
#pragma once

#include <filesystem>
#include <string>

namespace bmh::ui {
// Host paths use portable separators: the legacy font's backslash glyph is Ö.
inline std::string save_directory_label(const std::filesystem::path& directory) {
    const auto value = directory.generic_u8string();
    return std::string(value.begin(), value.end());
}
} // namespace bmh::ui
