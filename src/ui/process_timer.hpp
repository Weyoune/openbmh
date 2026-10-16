// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <optional>
#include "formats/le.hpp"
#include "formats/vga.hpp"

namespace bmh::ui {
// DeskDate owns this process clock, not a saved FBM or a result-viewer minute.
// Host timestamps are monotonic whole seconds. No GameState/RNG access.
class ProcessTimer {
public:
    struct Sample {
        int64_t remaining_seconds = 0;
        unsigned percent = 0;
        bool expired = false;
        bool operator==(const Sample&) const = default;
    };
    void start(uint8_t minutes, uint64_t now);
    void stop();
    void pause(uint64_t now);
    void resume(uint8_t minutes, uint64_t now);
    bool paused() const { return paused_; }
    std::optional<Sample> sample(uint64_t now) const;
    static void paint(Image&, const Sample&, const LeImage&);
private:
    uint8_t minutes_ = 0;
    bool started_ = false, paused_ = false;
    uint64_t anchor_ = 0;
    int64_t remaining_ = 0;
};
} // namespace bmh::ui
