// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/le.hpp"
#include "game/messages.hpp"
#include "game/schedule.hpp"

namespace bmh::test {
// Standalone engine tests explicitly install the supplied edition's scheduling
// content and the message texts. CareerSession performs the same installation for
// normal app callers.
inline LeImage load_executable(const std::filesystem::path& path) {
    auto executable = bmh::load_le(path);
    install_fixture_tables(executable);
    install_message_texts(executable);
    return executable;
}
}
