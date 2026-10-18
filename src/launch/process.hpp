// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
namespace bmh::launch {
int run_child(const std::filesystem::path &, const std::vector<std::string> &,
              const std::function<void()> &pump);
} // namespace bmh::launch
