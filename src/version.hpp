// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The OpenBMH version. CMake sets OPENBMH_VERSION from project(... VERSION ...) in the top
// CMakeLists.txt - the one place to change it; this fallback only serves other builds.
#ifndef OPENBMH_VERSION
#define OPENBMH_VERSION "0.1.0-dev"
#endif

namespace bmh {
inline constexpr const char* kOpenBmhVersion = OPENBMH_VERSION;
}  // namespace bmh
