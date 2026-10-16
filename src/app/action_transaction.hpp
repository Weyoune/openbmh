// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "app/career_session.hpp"

namespace bmh::app::detail {

template<class Checkpoint, class Restore>
bool apply_window(CareerSession& session, uint64_t& revision, Checkpoint before,
    Restore restore_checkpoint, const std::function<bool()>& action) {
    const auto game = session.game();
    const auto rand = session.rand();
    const auto old_revision = revision;
    const auto restore = [&] {
        session.game() = game;
        session.rand() = rand;
        // Some owners reconstruct their saved RNG identity during restore.
        restore_checkpoint(std::move(before));
        revision = old_revision;
    };
    try {
        if (action()) return true;
    } catch (...) {
        restore();
        throw;
    }
    restore();
    return false;
}

inline bool apply_native(CareerSession& session, uint64_t& revision, const std::function<bool()>& action) {
    return apply_window(session, revision, session.state(),
        [&](GameState before) { session.state() = std::move(before); }, action);
}

} // namespace bmh::app::detail
