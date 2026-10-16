// SPDX-License-Identifier: GPL-3.0-only
#include "semantic_cues.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace openbmh::audio {
namespace {
constexpr std::uint32_t kSampleRate = 22050;
constexpr double kPi = 3.14159265358979323846;

std::int16_t clamp_pcm(double value) {
    const auto rounded = static_cast<long>(std::lround(value));
    return static_cast<std::int16_t>(std::clamp(rounded, -32768L, 32767L));
}

void append_whistle(std::vector<std::int16_t>& pcm, std::size_t frames,
                    double start_hz, double end_hz) {
    double phase = 0.0;
    const std::size_t attack = kSampleRate / 80;
    const std::size_t release = kSampleRate / 14;
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(frames);
        const double frequency = start_hz + (end_hz - start_hz) * t;
        phase += 2.0 * kPi * frequency / kSampleRate;
        const double in = std::min(1.0, static_cast<double>(i) / attack);
        const double out = std::min(1.0, static_cast<double>(frames - 1 - i) / release);
        const double envelope = std::min(in, out);
        const double tone = std::sin(phase) + 0.16 * std::sin(phase * 2.01);
        pcm.push_back(clamp_pcm(14500.0 * envelope * tone));
    }
}

void append_fanfare_note(std::vector<std::int16_t>& pcm, std::size_t frames,
                         double frequency) {
    double phase = 0.0;
    const std::size_t attack = kSampleRate / 90;
    const std::size_t release = kSampleRate / 9;
    for (std::size_t i = 0; i < frames; ++i) {
        phase += 2.0 * kPi * frequency / kSampleRate;
        const double in = std::min(1.0, static_cast<double>(i) / attack);
        const double out = std::min(1.0, static_cast<double>(frames - 1 - i) / release);
        const double envelope = std::min(in, out);
        const double brass = std::sin(phase) + 0.38 * std::sin(phase * 2.0) +
                             0.14 * std::sin(phase * 3.0);
        pcm.push_back(clamp_pcm(10500.0 * envelope * brass));
    }
}
}

bool synthesize_semantic_cue(SemanticCue cue, PcmSample& out, std::string& error) {
    out = {};
    error.clear();
    out.sample_rate = kSampleRate;
    switch (cue) {
    case SemanticCue::kickoff_whistle:
        append_whistle(out.mono, kSampleRate * 42 / 100, 2050.0, 1880.0);
        break;
    case SemanticCue::goal:
        append_fanfare_note(out.mono, kSampleRate * 15 / 100, 659.25);
        append_fanfare_note(out.mono, kSampleRate * 15 / 100, 783.99);
        append_fanfare_note(out.mono, kSampleRate * 34 / 100, 987.77);
        break;
    case SemanticCue::full_time_whistle:
        append_whistle(out.mono, kSampleRate * 32 / 100, 1940.0, 1840.0);
        out.mono.insert(out.mono.end(), kSampleRate * 9 / 100, 0);
        append_whistle(out.mono, kSampleRate * 42 / 100, 2110.0, 1910.0);
        break;
    default:
        out = {};
        error = "unknown semantic audio cue";
        return false;
    }
    return true;
}

} // namespace openbmh::audio
