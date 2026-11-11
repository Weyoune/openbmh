// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The check of the test programs: a failed expectation ends the test with its message (the main
// function of a test prints the message and returns 1). Until 1.0.12 every test file had its own copy.
#include <stdexcept>

namespace bmh::test_support {

inline void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

}  // namespace bmh::test_support
