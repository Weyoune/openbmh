// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Only the product entry points include the generated version, so changing a
// release number does not rebuild the game, test suites or third-party code.
#ifndef OPENBMH_VERSION
#if __has_include("openbmh_build_version.hpp")
#include "openbmh_build_version.hpp"
#else
#define OPENBMH_VERSION "development"
#endif
#endif

namespace bmh {
inline constexpr const char* kOpenBmhVersion = OPENBMH_VERSION;
}  // namespace bmh
