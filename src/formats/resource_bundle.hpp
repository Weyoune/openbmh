// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/binary.hpp"
#include <optional>
namespace bmh {
inline constexpr const char* kCommunityBundle = "COMMUNITY.OBD";
// Read-only package lookup. Loose user saves always remain outside the bundle.
std::optional<Bytes> bundled_resource(const std::filesystem::path& path);
bool resource_exists(const std::filesystem::path& path);
}
