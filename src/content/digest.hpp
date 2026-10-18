// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>
#include <string_view>
namespace bmh::content {
std::string sha256(std::string_view data);
}
