// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The step of the turn the engine is in: the turn driver names it when an exception (such as
// std::out_of_range, which carries no location) ends a turn, and a watchdog can read it while
// a turn runs (tests/cli_soak.cpp). Set at the coarse steps only.

#include <atomic>

namespace bmh {

inline std::atomic<const char*> turn_step{"start"};
// The club the current step works on (diagnostics only; -1 = none).
inline std::atomic<int> turn_team{-1};

}  // namespace bmh
