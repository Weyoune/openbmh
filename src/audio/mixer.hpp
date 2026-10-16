// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "container.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace openbmh::audio {

// Small allocation-bounded PCM mixer. Setup/add_voice may allocate; mix_block
// only reads its prepared buffers and writes into caller-owned storage.
class Mixer {
public:
    bool start_music_loop(const PcmSample& loop, std::uint32_t output_rate,
                          std::string& error);
    void stop_music() noexcept;
    bool add_voice(std::span<const std::int16_t> samples, std::uint32_t sample_rate,
                   std::uint32_t output_rate, std::string& error);
    void clear() noexcept;
    bool active() const noexcept;
    bool has_music() const noexcept { return !music_.empty(); }
    std::size_t active_voices() const noexcept;
    bool mix_block(std::span<std::int16_t> output, float volume) noexcept;

private:
    struct Voice { std::vector<std::int16_t> samples; std::size_t cursor{}; };
    std::vector<std::int16_t> music_;
    std::size_t music_cursor_{};
    std::array<Voice, 8> voices_{};
    std::size_t voice_bytes_{};
};

} // namespace openbmh::audio
