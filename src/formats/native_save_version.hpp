// SPDX-License-Identifier: GPL-3.0-only
// Accepted savegame version markers for the native (original-layout) save format. The FBM
// container (fbm.hpp) carries a version string; V2.24 and V3.7 share the same block order,
// while expanded careers use their own marker (O1.1) and layout. require_native_savegame()
// throws FormatError on a bad checksum or an unsupported marker; callers still validate the
// full state layout afterwards (savestate.hpp). Header-only.
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
