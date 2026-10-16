// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <numeric>
#include "game/schedule.hpp"

namespace bmh::test {
// Authored circle-method rounds for isolated tests, never a production fallback.
inline FixtureTableCache authored_fixture_tables() {
    FixtureTableCache tables;
    for (const int teams : {4, 6, 12, 16, 18, 20}) {
        std::vector<int> rotation(size_t(teams), 0);
        std::iota(rotation.begin(), rotation.end(), 0);
        for (int round = 0; round < teams - 1; ++round) {
            std::string row;
            for (int match = 0; match < teams / 2; ++match) {
                row += char('a' + rotation[size_t(match)]);
                row += char('a' + rotation[size_t(teams - 1 - match)]);
            }
            tables[teams].push_back(std::move(row));
            std::rotate(rotation.begin() + 1, rotation.end() - 1, rotation.end());
        }
    }
    return tables;
}
}
