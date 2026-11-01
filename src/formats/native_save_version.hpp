// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/fbm.hpp"
#include <string_view>

namespace bmh {
// Verified German V2.24 state layout: the supplied Diamond 3.7 changes its
// container label, while retaining the same block order and database counts.
// Expanded careers have their own layout and must not accept these markers.
inline bool supported_native_save_version(std::string_view version, bool expanded = false) {
    return expanded ? version == "O1.1" : version == "V2.24" || version == "V3.7";
}
inline void require_native_savegame(const Savegame& save, bool expanded = false) {
    if (!save.checksum_ok) throw FormatError("savegame: FBM checksum mismatch");
    if (!supported_native_save_version(save.version, expanded))
        throw FormatError("savegame: unsupported FBM version " + save.version);
    // Callers still parse the complete state using the installed edition's
    // exact DbCounts; a recognized marker never bypasses layout validation.
}
}
