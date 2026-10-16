// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/process_timer.hpp"
#include <algorithm>

namespace bmh::ui {
void ProcessTimer::start(uint8_t minutes, uint64_t now) {
    minutes_ = minutes; anchor_ = now; remaining_ = int64_t(minutes)*60;
    started_ = true; paused_ = false;
}
void ProcessTimer::stop() { started_ = false; paused_ = false; }
std::optional<ProcessTimer::Sample> ProcessTimer::sample(uint64_t now) const {
    if (!started_ || paused_ || minutes_ == 0 || minutes_ >= 200) return {};
    if (remaining_ <= 0) return Sample{remaining_, 0, true};
    // The actual x86 gate is signed jg at46ad2, despite Ghidra's unsigned
    // declaration. Overshooting the deadline expires just like reaching zero.
    const uint64_t elapsed = now > anchor_ ? now-anchor_ : 0;
    const int64_t left = remaining_ - int64_t(std::min<uint64_t>(elapsed, INT64_MAX));
    if (left <= 0) return Sample{left, 0, true};
    return Sample{left, unsigned(std::min<int64_t>(100, left*100/(int64_t(minutes_)*60))), false};
}
void ProcessTimer::pause(uint64_t now) {
    if (!started_ || paused_) return;
    if (const auto value = sample(now)) remaining_ = value->remaining_seconds;
    paused_ = true;
}
void ProcessTimer::resume(uint8_t minutes, uint64_t now) {
    if (!started_) { start(minutes, now); return; }
    if (!paused_) return;
    // ApplySettingsFile preserves the remaining budget across save/join/settings waits.
    // Settings add (new_h8-old_h8)*60, or start fresh when old_h8 was zero.
    remaining_ = minutes_ == 0 ? int64_t(minutes)*60
                              : remaining_ + (int64_t(minutes)-minutes_)*60;
    minutes_ = minutes; anchor_ = now; paused_ = false;
}
void ProcessTimer::paint(Image& image, const Sample& value, const LeImage& exe) {
    if (value.expired) return; //Sample returns event8 before drawing an expired bar.
    if (image.width != 320 || image.height != 240 || value.percent > 100)
        throw FormatError("process timer: invalid Original frame/percentage");
    std::fill_n(image.pixels.begin(), 320, uint8_t(0x44));
    const unsigned last = value.percent*319/100; // Abs3 has inclusive endpoints.
    std::fill_n(image.pixels.begin(), last+1, exe.u8(::bmh::layout::ProcessTimer_FormatError_RuntimeField + value.percent/34));
}
} // namespace bmh::ui
