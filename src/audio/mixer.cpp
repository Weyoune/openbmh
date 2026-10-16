// SPDX-License-Identifier: GPL-3.0-only
#include "mixer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace openbmh::audio {
namespace {
constexpr std::size_t kMaxLoopBytes = 2U * 1024U * 1024U;
constexpr std::size_t kMaxVoiceBytes = 2U * 1024U * 1024U;
constexpr std::size_t kMaxPerVoiceBytes = kMaxVoiceBytes / 8;

bool convert_rate(std::span<const std::int16_t> input, std::uint32_t source_rate,
                  std::uint32_t output_rate, std::size_t byte_limit,
                  std::vector<std::int16_t>& output, std::string& error) {
    output.clear();
    if (input.empty() || source_rate < 4000 || source_rate > 192000 ||
        output_rate < 4000 || output_rate > 192000) {
        error = "invalid PCM rate or empty input";
        return false;
    }
    if (input.size() > (std::numeric_limits<std::uint64_t>::max() - source_rate / 2) /
                           output_rate) {
        error = "PCM input size overflows sample-rate conversion";
        return false;
    }
    const auto count64 = (static_cast<std::uint64_t>(input.size()) * output_rate + source_rate / 2) /
                         source_rate;
    if (!count64 || count64 > byte_limit / sizeof(std::int16_t) ||
        count64 > std::numeric_limits<std::size_t>::max()) {
        error = "converted PCM exceeds mixer buffer limit";
        return false;
    }
    const auto count = static_cast<std::size_t>(count64);
    output.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto source_index = std::min(input.size() - 1,
            static_cast<std::size_t>((static_cast<std::uint64_t>(i) * source_rate + output_rate / 2) /
                                     output_rate));
        output[i] = input[source_index];
    }
    error.clear();
    return true;
}
}

bool Mixer::start_music_loop(const PcmSample& loop, std::uint32_t output_rate,
                             std::string& error) {
    std::vector<std::int16_t> converted;
    if (!convert_rate(loop.mono, loop.sample_rate, output_rate, kMaxLoopBytes, converted, error))
        return false;
    music_ = std::move(converted);
    music_cursor_ = 0;
    error.clear();
    return true;
}

void Mixer::stop_music() noexcept {
    music_.clear();
    music_cursor_ = 0;
}

bool Mixer::add_voice(std::span<const std::int16_t> samples, std::uint32_t sample_rate,
                      std::uint32_t output_rate, std::string& error) {
    std::vector<std::int16_t> converted;
    if (!convert_rate(samples, sample_rate, output_rate, kMaxPerVoiceBytes, converted, error))
        return false;
    if (converted.size() * sizeof(std::int16_t) > kMaxVoiceBytes - voice_bytes_) {
        error = "combined active cue voices exceed mixer buffer limit";
        return false;
    }
    const auto free_voice = std::find_if(voices_.begin(), voices_.end(),
        [](const Voice& voice) { return voice.cursor >= voice.samples.size(); });
    if (free_voice == voices_.end()) {
        error = "all fixed mixer cue voices are active";
        return false;
    }
    voice_bytes_ += converted.size() * sizeof(std::int16_t);
    free_voice->samples = std::move(converted);
    free_voice->cursor = 0;
    error.clear();
    return true;
}

void Mixer::clear() noexcept {
    stop_music();
    for (auto& voice : voices_) {
        voice.samples.clear();
        voice.cursor = 0;
    }
    voice_bytes_ = 0;
}

bool Mixer::active() const noexcept {
    return !music_.empty() || active_voices() != 0;
}

std::size_t Mixer::active_voices() const noexcept {
    return static_cast<std::size_t>(std::count_if(voices_.begin(), voices_.end(),
        [](const Voice& voice) { return voice.cursor < voice.samples.size(); }));
}

bool Mixer::mix_block(std::span<std::int16_t> output, float volume) noexcept {
    if (!active() || output.empty() || !std::isfinite(volume)) return false;
    const float bounded_volume = std::clamp(volume, 0.0F, 1.0F);
    for (auto& dst : output) {
        std::int32_t mixed = 0;
        if (!music_.empty()) {
            mixed += music_[music_cursor_++];
            if (music_cursor_ == music_.size()) music_cursor_ = 0;
        }
        for (auto& voice : voices_) {
            if (voice.cursor < voice.samples.size()) {
                mixed += voice.samples[voice.cursor++];
                if (voice.cursor == voice.samples.size()) {
                    voice_bytes_ -= voice.samples.size() * sizeof(std::int16_t);
                    voice.samples.clear();
                    voice.cursor = 0;
                }
            }
        }
        const auto scaled = std::lround(static_cast<float>(mixed) * bounded_volume);
        dst = static_cast<std::int16_t>(std::clamp<long>(scaled, -32768, 32767));
    }
    return true;
}

} // namespace openbmh::audio
