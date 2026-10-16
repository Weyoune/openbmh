// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace openbmh::audio {

struct Chunk {
    std::string id;
    std::size_t payload_offset{};
    std::size_t payload_size{};
};

struct Container { std::vector<Chunk> chunks; };

struct PcmSample {
    std::uint32_t sample_rate{};
    std::vector<std::int16_t> mono;
};

struct OriginalCue {
    std::uint8_t original_id{};
    std::uint8_t sefx_index{};
    std::uint8_t sample_index{};
    PcmSample pcm;
};

// Parses the observed FORM/ALPTRAUM wrapper. Chunk lengths are little endian;
// chunks are adjacent with no alignment byte. Payloads remain in the caller's buffer.
bool parse_container(std::span<const std::uint8_t> bytes, Container& out,
                     std::string& error);

// Converts one bounded SAMP sample span from unsigned 8-bit DMA data to signed
// 16-bit mono PCM, using the rate stored in its 16-byte descriptor.
bool decode_sample(std::span<const std::uint8_t> samp_payload, std::size_t index,
                   PcmSample& out, std::string& error);

// Resolves only the observed PCM-backed SEFX effects (original IDs 17..22).
// IDs 10..16 use the separate instrument path and remain unsupported here.
bool decode_original_cue(std::span<const std::uint8_t> container_bytes,
                         std::uint8_t original_id, OriginalCue& out,
                         std::string& error);

} // namespace openbmh::audio
