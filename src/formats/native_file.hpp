// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <string_view>
namespace bmh::content {
void write_atomic(const std::filesystem::path &, std::string_view, bool overwrite = false);
}
