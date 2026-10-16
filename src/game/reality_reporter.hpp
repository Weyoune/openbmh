// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <string>
#include <string_view>
#include <vector>
#include "game/reality_space.hpp"

namespace bmh {
// Runtime locals/globals of InitializeRealityReporter. The scene owner supplies their actual
// values; this module neither advances the ball nor constructs a match result.
struct RealityReporterFrame {
    uint8_t action = 3, opponent = 0, airborne = 0, scene_status = 0;
    uint8_t distance_before = 0, distance_after = 0, referee = 0, player_side = 0;
    bool lost_duel = false, received = false, inaccurate = false;
    bool blocked_attempt = false, intercepted = false, offside = false, leading_run = false;
    int receiver = -1, receiver_player = 0xfff;
    std::array<uint8_t,2> line_selected{};
    uint8_t card_kind = 0;
    // InitializeRealityReporter leaves BH unchanged when dx>=dy. This is the caller's
    // incoming register byte, not max(dx,dy); absence is an explicit boundary.
    std::optional<uint8_t> incoming_bh;
};
struct RealityReporterState {
    int last_side = -1, last_owner = -1, last_player = -1, last_action = -1;
    int current_player = -1; // afaee, assigned even if the explicit cue is suppressed
    std::array<uint8_t,13> selectors{}; // RealityReporter_InitializeRealityReporter_RuntimeField..MatchEngine_Minute_ExecutableField; reset keeps these unchanged
};
struct RealityReporterResult {
    std::vector<uint8_t> codes;
    std::vector<std::string> text; // Original single-byte text, ready for later layout
    bool suppressed = false;
};
RealityReporterState initialize_reality_reporter(const LeImage&);
// RealityReporterText2/RealityReporterText: nested Original %xDD alternatives and entity tokens.
// Only chosen branches consume Watcom draws. No Game draw or Native write.
std::string reality_reporter_text(const GameState&,const RealitySetup&,const LeImage&,
    const RealityReporterFrame&,const RealityReporterState&,std::string_view,WatcomRandom&);
// InitializeRealityReporter; figure100 resets the four history words only. code0 derives the
// normal cue sequence; codes1..21 request an explicit Source cue. May update
// action/opponent in the owned frame. Rendering must use the returned text.
RealityReporterResult report_reality(const GameState&,const RealitySetup&,const LeImage&,
    RealityReporterFrame&,RealityReporterState&,int figure,int side,uint8_t code,WatcomRandom&);
} // namespace bmh
