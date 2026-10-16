// SPDX-License-Identifier: GPL-3.0-only
#include "music_fallback.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace openbmh::audio {
namespace {
constexpr std::uint32_t kRate = 22050;
constexpr std::uint32_t kBeatsPerMinute = 108;
constexpr std::uint32_t kBars = 8;
constexpr std::uint32_t kBeatsPerBar = 4;
constexpr double kPi = 3.14159265358979323846;
constexpr std::size_t kLoopFrames =
    static_cast<std::size_t>(kRate) * 60 * kBars * kBeatsPerBar / kBeatsPerMinute;

constexpr std::array<int, 4> kBassMidi{43, 38, 40, 36}; // G2, D2, E2, C2
constexpr std::array<std::array<int, 3>, 4> kChordMidi{{
    {{55, 59, 62}}, // G major
    {{50, 54, 57}}, // D major
    {{52, 55, 59}}, // E minor
    {{48, 52, 55}}, // C major
}};
constexpr std::array<int, 16> kLeadMidi{
    67, 71, 74, 71, 69, 74, 78, 74,
    71, 76, 79, 76, 67, 72, 76, 72,
};

double midi_hz(int midi) {
    return 440.0 * std::pow(2.0, static_cast<double>(midi - 69) / 12.0);
}

std::int16_t clamp_sample(double value) {
    const auto rounded = static_cast<long>(std::lround(value));
    return static_cast<std::int16_t>(std::clamp(rounded, -32768L, 32767L));
}
}

bool synthesize_matchday_music_loop(PcmSample& out, std::string& error) {
    out = {};
    error.clear();
    out.sample_rate = kRate;
    out.mono.resize(kLoopFrames);

    const double samples_per_beat = static_cast<double>(kRate) * 60.0 / kBeatsPerMinute;
    const std::size_t lead_gate = static_cast<std::size_t>(samples_per_beat * 0.72);
    const std::size_t fade_frames = kRate / 200; // 5 ms endpoint ramp for a clean loop seam.

    for (std::size_t frame = 0; frame < kLoopFrames; ++frame) {
        const double beat_position = static_cast<double>(frame) / samples_per_beat;
        const auto bar = static_cast<std::size_t>(beat_position / kBeatsPerBar) % kBars;
        const double beat_fraction = std::fmod(beat_position, 1.0);
        const double bar_fraction = std::fmod(beat_position / kBeatsPerBar, 1.0);

        const double bass_phase = 2.0 * kPi * midi_hz(kBassMidi[bar % 4]) * frame / kRate;
        const double bass_envelope = 0.78 + 0.22 * std::cos(kPi * beat_fraction);
        double mixed = 2600.0 * bass_envelope *
            (std::sin(bass_phase) + 0.18 * std::sin(2.0 * bass_phase));

        const auto& chord = kChordMidi[bar % 4];
        const double pad_envelope = 0.58 + 0.16 * std::cos(2.0 * kPi * bar_fraction);
        for (std::size_t note = 0; note < chord.size(); ++note) {
            const double phase = 2.0 * kPi * midi_hz(chord[note]) * frame / kRate;
            mixed += 1350.0 * pad_envelope *
                (std::sin(phase) + 0.12 * std::sin(2.0 * phase));
        }

        // A short eighth-note lead gives the loop a recognizable repeated motif.
        const auto step = static_cast<std::size_t>(beat_position * 2.0) % kLeadMidi.size();
        const double step_fraction = std::fmod(beat_position * 2.0, 1.0);
        const auto lead_elapsed = static_cast<std::size_t>(step_fraction * samples_per_beat / 2.0);
        if (lead_elapsed < lead_gate) {
            const double phase = 2.0 * kPi * midi_hz(kLeadMidi[step]) * lead_elapsed / kRate;
            const double attack = std::min(1.0, static_cast<double>(lead_elapsed) / (kRate / 160));
            const double release = std::min(1.0,
                static_cast<double>(lead_gate - lead_elapsed) / (kRate / 35));
            const double envelope = std::min(attack, release);
            mixed += 3600.0 * envelope *
                (std::sin(phase) + 0.22 * std::sin(2.0 * phase));
        }

        const auto edge = std::min(frame, kLoopFrames - 1 - frame);
        const double seam_envelope = std::min(1.0, static_cast<double>(edge) / fade_frames);
        out.mono[frame] = clamp_sample(mixed * seam_envelope);
    }
    return true;
}

} // namespace openbmh::audio
