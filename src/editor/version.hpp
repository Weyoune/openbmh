// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>
#include <string_view>

namespace bmh::editor {
inline constexpr std::string_view kEditorVersion = OPENBMH_EDITOR_VERSION;
inline std::string editor_product() { return "OpenBMH Editor " + std::string(kEditorVersion); }
} // namespace bmh::editor
