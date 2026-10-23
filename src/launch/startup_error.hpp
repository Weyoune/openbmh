// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>

namespace bmh::rml {
// Safe before video initialization, including when no graphics driver is available.
// No preferences or authentication tokens are logged.
void report_start_error(const char *product, const std::string &message, bool dialog = true) noexcept;
void attach_parent_console() noexcept;
} // namespace bmh::rml
