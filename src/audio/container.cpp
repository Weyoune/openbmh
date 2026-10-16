// SPDX-License-Identifier: GPL-3.0-only
#include "container.hpp"

#include <algorithm>
#include <limits>

namespace openbmh::audio {
namespace {
std::uint32_t read_le32(std::span<const std::uint8_t> bytes, std::size_t at) {
    return static_cast<std::uint32_t>(bytes[at]) |
           (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
}
}

bool parse_container(std::span<const std::uint8_t> bytes, Container& out,
                     std::string& error) {
    out.chunks.clear();
    error.clear();
    if (bytes.size() < 16) {
        error = "truncated FORM/ALPTRAUM header";
        return false;
    }
    if (bytes[0] != 'F' || bytes[1] != 'O' || bytes[2] != 'R' || bytes[3] != 'M') {
        error = "missing FORM signature";
        return false;
    }
    if (std::string(bytes.begin() + 8, bytes.begin() + 16) != "ALPTRAUM") {
        error = "unsupported FORM type (expected ALPTRAUM)";
        return false;
    }
    const auto declared = read_le32(bytes, 4);
    if (declared != bytes.size() - 8 && declared != bytes.size()) {
        error = "FORM length does not match either observed ALPTRAUM convention";
        return false;
    }
    // The only observed full-file-size header is S/2003's SEFX variant. Keep
    // this exception tied to that exact, byte-verified layout; do not silently
    // accept it for the SONG/TRCK/PART music containers.
    const bool sefx_variant = declared == bytes.size() && bytes.size() == 65380 &&
        bytes[16] == 'H' && bytes[17] == 'E' && bytes[18] == 'D' && bytes[19] == 'R' &&
        bytes[32] == 'S' && bytes[33] == 'E' && bytes[34] == 'F' && bytes[35] == 'X';
    if (declared == bytes.size() && !sefx_variant) {
        error = "full-file FORM length is only established for the 2003 SEFX variant";
        return false;
    }

    std::size_t at = 16;
    while (at < bytes.size()) {
        if (bytes.size() - at < 8) {
            error = "truncated chunk header";
            out.chunks.clear();
            return false;
        }
        const std::uint32_t size = read_le32(bytes, at + 4);
        const std::size_t payload = at + 8;
        if (size > bytes.size() - payload) {
            error = "chunk payload exceeds container bounds";
            out.chunks.clear();
            return false;
        }
        out.chunks.push_back({std::string(bytes.begin() + at, bytes.begin() + at + 4),
                              payload, size});
        at = payload + size;
    }
    if (sefx_variant && (out.chunks.size() != 4 || out.chunks[0].id != "HEDR" ||
        out.chunks[1].id != "SEFX" || out.chunks[2].id != "INST" ||
        out.chunks[3].id != "SAMP" || out.chunks[0].payload_size != 8 ||
        out.chunks[1].payload_size != 212 || out.chunks[2].payload_size != 228 ||
        out.chunks[3].payload_size != 64884)) {
        error = "full-file FORM length does not match the observed 2003 SEFX chunk layout";
        out.chunks.clear();
        return false;
    }
    if (sefx_variant) {
        const auto& sefx = out.chunks[1];
        const auto payload = bytes.subspan(sefx.payload_offset, sefx.payload_size);
        const std::size_t records = (payload.size() - 4) / 16;
        if (payload[0] != 0 || payload[1] != 0 || payload[2] != 7 || payload[3] != 0 ||
            (payload.size() - 4) % 16 != 0 || records != 13) {
            error = "2003 SEFX prefix or fixed-record table is malformed";
            out.chunks.clear();
            return false;
        }
    }
    return true;
}

bool decode_sample(std::span<const std::uint8_t> payload, std::size_t index,
                   PcmSample& out, std::string& error) {
    out = {};
    error.clear();
    if (payload.size() < 4) { error = "truncated SAMP header"; return false; }
    const std::size_t count = static_cast<std::size_t>(payload[2]) |
                              (static_cast<std::size_t>(payload[3]) << 8);
    constexpr std::size_t kRecordSize = 16;
    constexpr std::size_t kMaxSamples = 256;
    if (count > kMaxSamples || count > (payload.size() - 4) / kRecordSize) {
        error = "SAMP descriptor table exceeds payload bounds"; return false;
    }
    if (index >= count) { error = "sample index is outside the SAMP table"; return false; }
    const std::size_t data_offset = 4 + count * kRecordSize;
    const std::size_t record = 4 + index * kRecordSize;
    const std::uint32_t start = static_cast<std::uint32_t>(payload[record]) |
                                (static_cast<std::uint32_t>(payload[record + 1]) << 8);
    const std::uint32_t length = static_cast<std::uint32_t>(payload[record + 2]) |
                                 (static_cast<std::uint32_t>(payload[record + 3]) << 8);
    const std::uint32_t rate = static_cast<std::uint32_t>(payload[record + 8]) |
                               (static_cast<std::uint32_t>(payload[record + 9]) << 8);
    const std::size_t data_size = payload.size() - data_offset;
    if (length == 0 || start > data_size || length > data_size - start) {
        error = "SAMP sample span exceeds sample payload"; return false;
    }
    if (rate < 4000 || rate > 48000) {
        error = "unsupported SAMP sample rate"; return false;
    }
    out.sample_rate = rate;
    out.mono.reserve(length);
    const std::size_t first = data_offset + start;
    for (std::size_t i = 0; i < length; ++i) {
        out.mono.push_back(static_cast<std::int16_t>(
            (static_cast<int>(payload[first + i]) - 128) * 256));
    }
    return true;
}

bool decode_original_cue(std::span<const std::uint8_t> bytes,
                         std::uint8_t original_id, OriginalCue& out,
                         std::string& error) {
    out = {};
    error.clear();
    if (original_id < 10 || original_id > 22) {
        error = "original SFX ID is outside the observed 10..22 dispatch range";
        return false;
    }
    Container container;
    if (!parse_container(bytes, container, error)) return false;
    const auto declared_form = read_le32(bytes, 4);
    if (bytes.size() != 65380 || declared_form != bytes.size() || container.chunks.size() != 4 ||
        container.chunks[0].id != "HEDR" || container.chunks[0].payload_size != 8 ||
        container.chunks[1].id != "SEFX" || container.chunks[1].payload_size != 212 ||
        container.chunks[2].id != "INST" || container.chunks[2].payload_size != 228 ||
        container.chunks[3].id != "SAMP" || container.chunks[3].payload_size != 64884) {
        error = "original cue decoding is limited to the exact observed 2003 SEFX bank";
        return false;
    }
    const auto find = [&container](const char* id) -> const Chunk* {
        const auto it = std::find_if(container.chunks.begin(), container.chunks.end(),
            [id](const Chunk& c) { return c.id == id; });
        return it == container.chunks.end() ? nullptr : &*it;
    };
    const Chunk* sefx = find("SEFX");
    const Chunk* samp = find("SAMP");
    if (!sefx || !samp) {
        error = "original cue needs both SEFX and SAMP chunks";
        return false;
    }
    const auto sefx_data = bytes.subspan(sefx->payload_offset, sefx->payload_size);
    constexpr std::size_t kPrefixBytes = 4;
    constexpr std::size_t kRecordBytes = 16;
    constexpr std::size_t kRecordCount = 13;
    if (sefx_data.size() != kPrefixBytes + kRecordBytes * kRecordCount ||
        sefx_data[0] != 0 || sefx_data[1] != 0 || sefx_data[2] != 7 || sefx_data[3] != 0) {
        error = "unsupported SEFX layout for original-cue lookup";
        return false;
    }
    // PauseMusic subtracts 10 before dispatch. Subspan indexes that
    // SEFX record; its type-9 records address the loaded sample slots 16..21.
    const std::size_t sefx_index = static_cast<std::size_t>(original_id - 10);
    const std::size_t record = kPrefixBytes + sefx_index * kRecordBytes;
    const std::uint8_t sample_slot = sefx_data[record];
    if (sefx_data[record + 1] != 9 || sample_slot < 16 || sample_slot > 21) {
        error = "original cue is not a directly decoded type-9 SAMP effect";
        return false;
    }
    const auto sample_index = static_cast<std::size_t>(sample_slot - 16);
    const auto samp_data = bytes.subspan(samp->payload_offset, samp->payload_size);
    if (samp_data[2] != 6 || samp_data[3] != 0) {
        error = "2003 SAMP table does not have the six observed entries";
        return false;
    }
    if (!decode_sample(samp_data, sample_index, out.pcm, error)) return false;
    out.original_id = original_id;
    out.sefx_index = static_cast<std::uint8_t>(sefx_index);
    out.sample_index = static_cast<std::uint8_t>(sample_index);
    return true;
}

} // namespace openbmh::audio
