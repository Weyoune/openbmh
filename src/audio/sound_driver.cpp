// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "container.hpp"
#include "music_fallback.hpp"
#include "playback.hpp"
#include "formats/le.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <limits>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <charconv>
#include <filesystem>
#include <span>
#include <string_view>

namespace {
bool parse_semantic_cue(const std::string& name, openbmh::audio::SemanticCue& cue) {
    using openbmh::audio::SemanticCue;
    if (name == "kickoff_whistle") cue = SemanticCue::kickoff_whistle;
    else if (name == "goal") cue = SemanticCue::goal;
    else if (name == "full_time_whistle") cue = SemanticCue::full_time_whistle;
    else return false;
    return true;
}

bool write_wav(const openbmh::audio::PcmSample& decoded, const char* path,
               std::string& error) {
    if (decoded.mono.size() > (std::numeric_limits<std::uint32_t>::max() - 36u) / 2u ||
        decoded.sample_rate == 0 || decoded.sample_rate > 192000) {
        error = "WAV output exceeds format bounds"; return false;
    }
    const std::uint32_t data_bytes = static_cast<std::uint32_t>(decoded.mono.size() * 2);
    std::ofstream wav(path, std::ios::binary);
    if (!wav) { error = "cannot create WAV output"; return false; }
    const auto put16 = [&wav](std::uint16_t v) {
        const char b[] = {static_cast<char>(v), static_cast<char>(v >> 8)};
        wav.write(b, 2);
    };
    const auto put32 = [&wav](std::uint32_t v) {
        const char b[] = {static_cast<char>(v), static_cast<char>(v >> 8),
                          static_cast<char>(v >> 16), static_cast<char>(v >> 24)};
        wav.write(b, 4);
    };
    wav.write("RIFF", 4); put32(36 + data_bytes); wav.write("WAVEfmt ", 8);
    put32(16); put16(1); put16(1); put32(decoded.sample_rate);
    put32(decoded.sample_rate * 2); put16(2); put16(16); wav.write("data", 4);
    put32(data_bytes);
    for (const auto value : decoded.mono) put16(static_cast<std::uint16_t>(value));
    if (!wav) { error = "failed while writing WAV output"; return false; }
    error.clear();
    return true;
}

bool bounded_number(const char* value, unsigned minimum, unsigned maximum, unsigned& out) {
    const std::string_view text(value);
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
    return error == std::errc{} && end == text.data() + text.size() && out >= minimum && out <= maximum;
}

openbmh::audio::OriginalAssets load_original_assets(const std::filesystem::path& directory) {
    openbmh::audio::OriginalAssets assets;
    const auto exe = bmh::load_le(directory / "BMH.exe");
    for (unsigned i = 0; i < 4; ++i)
        assets.containers[i] = bmh::read_file(directory / "S" / std::to_string(2000 + i));
    for (unsigned i = 0; i < assets.tables.frequencies.size(); ++i)
        assets.tables.frequencies[i] = exe.u16(::bmh::layout::Audio_OriginalAudioAssets_RuntimeField2 + 2 * i);
    for (unsigned i = 0; i < assets.tables.operators.size(); ++i)
        assets.tables.operators[i] = exe.u8(::bmh::layout::Audio_OriginalAudioAssets_RuntimeField + 16 * i);
    return assets;
}

int original_driver(int argc, char** argv) {
    using namespace openbmh::audio;
    const auto usage = [] {
        std::cerr << "usage: openbmh_sound --original DATA_DIR --export-song 0..2 OUTPUT.wav [--seconds 1..600] [--loops 1..16]\n"
                     "       openbmh_sound --original DATA_DIR --export-id 10..22 OUTPUT.wav [--seconds 1..600]\n"
                     "       openbmh_sound --original DATA_DIR --play-id ID [--seconds 1..600]\n"
                     "       openbmh_sound --original DATA_DIR --play-all [--seconds 1..600]\n";
        return 2;
    };
    if (argc < 4) return usage();
    const std::string action = argv[3];
    const bool song = action == "--export-song";
    const bool export_id = action == "--export-id";
    const bool play = action == "--play-id";
    const bool all = action == "--play-all";
    if (!song && !export_id && !play && !all) return usage();
    const int required = all ? 4 : play ? 5 : 6;
    if (argc < required) return usage();
    unsigned id = 0, seconds = song || all ? 10 : 2, loops = 1;
    if (!all && !bounded_number(argv[4], 0, 255, id)) return usage();
    if ((song && id > 2) || (export_id && (id < 10 || id > 22)) ||
        (play && !(id <= 3 || id == 8 || (id >= 10 && id <= 22)))) return usage();
    for (int at = required; at < argc; at += 2) {
        if (at + 1 >= argc) return usage();
        const std::string option = argv[at];
        if (option == "--seconds") {
            if (!bounded_number(argv[at + 1], 1, 600, seconds)) return usage();
        } else if (song && option == "--loops") {
            if (!bounded_number(argv[at + 1], 1, 16, loops)) return usage();
        } else return usage();
    }
    try {
        auto assets = load_original_assets(argv[2]);
        std::string error;
        if (song || export_id) {
            OriginalSynth synth;
            if (!synth.load(std::move(assets), 22050, error)) {
                std::cerr << "Original audio rejected: " << error << '\n'; return 1;
            }
            if (!(song ? synth.command(std::uint8_t(id)) : synth.effect(std::uint8_t(id)))) {
                std::cerr << "Original command rejected\n"; return 1;
            }
            PcmSample pcm; pcm.sample_rate = 22050;
            const std::size_t limit = std::size_t(seconds) * pcm.sample_rate;
            pcm.mono.reserve(limit);
            std::array<std::int16_t, 128> block{};
            unsigned completed_loops = 0;
            auto previous_bar = synth.stats().bar;
            bool completed = false;
            while (pcm.mono.size() < limit) {
                const auto count = std::min(block.size(), limit - pcm.mono.size());
                synth.render(std::span(block).first(count));
                pcm.mono.insert(pcm.mono.end(), block.begin(), block.begin() + std::ptrdiff_t(count));
                const auto stats = synth.stats();
                if (stats.failed) { std::cerr << "Original decoder failed during export\n"; return 1; }
                if (song) {
                    if (stats.bar < previous_bar) ++completed_loops;
                    previous_bar = stats.bar;
                    if (!stats.playing || completed_loops >= loops) { completed = true; break; }
                }
            }
            if (!write_wav(pcm, argv[5], error)) { std::cerr << error << '\n'; return 1; }
            const auto stats = synth.stats();
            std::cout << "exported Original " << (song ? "song=" : "effect=") << id
                      << " rate=22050 channels=1 bits=16 frames=" << pcm.mono.size()
                      << " ticks=" << stats.ticks << " loops=" << completed_loops
                      << " stop=" << (completed ? "song-or-loop-complete" : "duration-bound") << '\n';
            return 0;
        }
        Playback playback;
        if (!playback.initialize_original(std::move(assets), error)) {
            std::cerr << "Original playback unavailable: " << error << '\n'; return 1;
        }
        const auto audition = [&](unsigned command, unsigned duration) {
            if (!playback.play_original_id(std::uint8_t(command), error)) return false;
            // The actual callback must continue while the main thread is blocked.
            SDL_Delay(duration * 1000);
            const auto stats = playback.original_stats();
            std::cout << "played Original command=" << command << " frames=" << stats.frames
                      << " ticks=" << stats.ticks << " failed=" << stats.failed << '\n';
            playback.stop();
            return !stats.failed;
        };
        if (all) {
            for (unsigned command = 0; command < 3; ++command)
                if (!audition(command, seconds)) { std::cerr << error << '\n'; return 1; }
            for (unsigned command = 10; command <= 22; ++command)
                if (!audition(command, seconds)) { std::cerr << error << '\n'; return 1; }
            return 0;
        }
        if (!audition(id, seconds)) { std::cerr << error << '\n'; return 1; }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Original audio input: " << error.what() << '\n'; return 1;
    }
}
}

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--original") return original_driver(argc, argv);
    if (argc < 2) {
        std::cerr << "usage: openbmh_sound <container-file> [--play-test-tone | --play-sample INDEX | --export-sample INDEX OUTPUT.wav | --play-id ID | --export-id ID OUTPUT.wav]\n"
                     "       openbmh_sound --original DATA_DIR --export-song 0..2 OUTPUT.wav [--seconds N] [--loops N]\n"
                     "       openbmh_sound --original DATA_DIR --export-id 10..22 OUTPUT.wav [--seconds N]\n"
                     "       openbmh_sound --original DATA_DIR --play-id ID | --play-all [--seconds N]\n"
                     "       openbmh_sound --play-cue NAME | --export-cue NAME OUTPUT.wav\n"
                     "       openbmh_sound --export-music-loop OUTPUT.wav\n";
        return 2;
    }
    if (argc == 4 && std::string(argv[1]) == "--export-cue") {
        openbmh::audio::SemanticCue cue;
        const std::string name = argv[2];
        if (!parse_semantic_cue(name, cue)) {
            std::cerr << "unknown semantic cue\n"; return 2;
        }
        openbmh::audio::PcmSample pcm;
        std::string error;
        if (!openbmh::audio::synthesize_semantic_cue(cue, pcm, error) ||
            !write_wav(pcm, argv[3], error)) {
            std::cerr << "semantic cue export failed: " << error << '\n'; return 1;
        }
        std::cout << "exported synthetic semantic cue=" << name << " rate=" << pcm.sample_rate
                  << " Hz channels=1 bits=16 frames=" << pcm.mono.size()
                  << " (not an original Sound ID)\n";
        return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--export-music-loop") {
        openbmh::audio::PcmSample pcm;
        std::string error;
        if (!openbmh::audio::synthesize_matchday_music_loop(pcm, error) ||
            !write_wav(pcm, argv[2], error)) {
            std::cerr << "synthetic music export failed: " << error << '\n'; return 1;
        }
        std::cout << "exported synthetic 8-bar matchday loop rate=" << pcm.sample_rate
                  << " Hz channels=1 bits=16 frames=" << pcm.mono.size()
                  << " (not original music or a Sound ID)\n";
        return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--play-cue") {
        openbmh::audio::SemanticCue cue;
        std::uint32_t wait_ms;
        const std::string name = argv[2];
        if (!parse_semantic_cue(name, cue)) { std::cerr << "unknown semantic cue\n"; return 2; }
        if (cue == openbmh::audio::SemanticCue::kickoff_whistle) wait_ms = 500;
        else if (cue == openbmh::audio::SemanticCue::goal) wait_ms = 750;
        else wait_ms = 1000;
        openbmh::audio::Playback playback;
        std::string cue_error;
        if (!playback.initialize(cue_error)) {
            std::cerr << "audio unavailable: " << cue_error << '\n'; return 0;
        }
        if (!playback.play_semantic_cue(cue, cue_error)) {
            std::cerr << "semantic cue playback failed: " << cue_error << '\n'; return 1;
        }
        SDL_Delay(wait_ms);
        playback.stop();
        std::cout << "played synthetic semantic cue=" << name << " (not an original Sound ID)\n";
        return 0;
    }
    std::vector<std::uint8_t> bytes;
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::cerr << "cannot open input\n"; return 2; }
    if (std::fseek(f, 0, SEEK_END) != 0) { std::fclose(f); return 2; }
    const long n = std::ftell(f);
    if (n < 0 || n > 64L * 1024 * 1024 || std::fseek(f, 0, SEEK_SET) != 0) {
        std::fclose(f); std::cerr << "input size rejected\n"; return 2;
    }
    bytes.resize(static_cast<std::size_t>(n));
    const auto got = std::fread(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    if (got != bytes.size()) { std::cerr << "short read\n"; return 2; }

    openbmh::audio::Container container;
    std::string error;
    if (!openbmh::audio::parse_container(bytes, container, error)) {
        std::cerr << "container rejected: " << error << '\n'; return 1;
    }
    for (const auto& c : container.chunks)
        std::cout << c.id << " offset=" << c.payload_offset << " length=" << c.payload_size << '\n';

    if ((argc >= 4 && std::string(argv[2]) == "--play-id") ||
        (argc >= 5 && std::string(argv[2]) == "--export-id")) {
        const bool do_play = std::string(argv[2]) == "--play-id";
        char* end = nullptr;
        const unsigned long id = std::strtoul(argv[3], &end, 10);
        if (!end || *end != '\0' || id > 255) {
            std::cerr << "invalid original Sound ID\n"; return 2;
        }
        openbmh::audio::OriginalCue cue;
        if (!openbmh::audio::decode_original_cue(bytes, static_cast<std::uint8_t>(id), cue, error)) {
            std::cerr << "original cue rejected: " << error << '\n'; return 1;
        }
        if (do_play) {
            openbmh::audio::Playback playback;
            if (!playback.initialize(error)) {
                std::cerr << "audio unavailable: " << error << '\n'; return 0;
            }
            if (!playback.load_original_bank(bytes, error) ||
                !playback.play_original_id(cue.original_id, error)) {
                std::cerr << "original cue playback rejected: " << error << '\n'; return 1;
            }
            const auto milliseconds = static_cast<std::uint32_t>(
                cue.pcm.mono.size() * 1000 / cue.pcm.sample_rate);
            SDL_Delay(milliseconds + 25);
            playback.stop();
            std::cout << "played original_id=" << static_cast<unsigned>(cue.original_id)
                      << " sefx_index=" << static_cast<unsigned>(cue.sefx_index)
                      << " samp_index=" << static_cast<unsigned>(cue.sample_index)
                      << " frames=" << cue.pcm.mono.size() << " rate="
                      << cue.pcm.sample_rate << " Hz\n";
        } else {
            if (!write_wav(cue.pcm, argv[4], error)) {
                std::cerr << error << '\n'; return 2;
            }
            std::cout << "original_id=" << static_cast<unsigned>(cue.original_id)
                      << " sefx_index=" << static_cast<unsigned>(cue.sefx_index)
                      << " samp_index=" << static_cast<unsigned>(cue.sample_index)
                      << " rate=" << cue.pcm.sample_rate << " Hz channels=1 bits=16 frames="
                      << cue.pcm.mono.size() << '\n';
        }
    }

    if ((argc >= 4 && std::string(argv[2]) == "--play-sample") ||
        (argc >= 5 && std::string(argv[2]) == "--export-sample")) {
        const bool do_play = std::string(argv[2]) == "--play-sample";
        char* end = nullptr;
        const unsigned long index = std::strtoul(argv[3], &end, 10);
        if (!end || *end != '\0' || index > 255) {
            std::cerr << "invalid sample index\n"; return 2;
        }
        const auto samp = std::find_if(container.chunks.begin(), container.chunks.end(),
            [](const auto& c) { return c.id == "SAMP"; });
        if (samp == container.chunks.end()) { std::cerr << "no SAMP chunk\n"; return 1; }
        openbmh::audio::PcmSample decoded;
        if (!openbmh::audio::decode_sample(
                std::span<const std::uint8_t>(bytes).subspan(samp->payload_offset, samp->payload_size),
                index, decoded, error)) {
            std::cerr << "sample rejected: " << error << '\n'; return 1;
        }
        if (do_play) {
            openbmh::audio::Playback playback;
            if (!playback.initialize(error)) {
                std::cerr << "audio unavailable: " << error << '\n'; return 0;
            }
            if (!playback.play_pcm(decoded.mono.data(), decoded.mono.size(), decoded.sample_rate, error)) {
                std::cerr << "sample playback rejected: " << error << '\n'; return 1;
            }
            const auto milliseconds = static_cast<std::uint32_t>(
                decoded.mono.size() * 1000 / decoded.sample_rate);
            SDL_Delay(milliseconds + 25);
            playback.stop();
            std::cout << "played SAMP sample=" << index << " frames=" << decoded.mono.size()
                      << " rate=" << decoded.sample_rate << " Hz\n";
        } else {
        const std::uint32_t data_bytes = static_cast<std::uint32_t>(decoded.mono.size() * 2);
        std::ofstream wav(argv[4], std::ios::binary);
        if (!wav) { std::cerr << "cannot create WAV output\n"; return 2; }
        const auto put16 = [&wav](std::uint16_t v) {
            const char b[] = {static_cast<char>(v), static_cast<char>(v >> 8)};
            wav.write(b, 2);
        };
        const auto put32 = [&wav](std::uint32_t v) {
            const char b[] = {static_cast<char>(v), static_cast<char>(v >> 8),
                              static_cast<char>(v >> 16), static_cast<char>(v >> 24)};
            wav.write(b, 4);
        };
        wav.write("RIFF", 4); put32(36 + data_bytes); wav.write("WAVEfmt ", 8);
        put32(16); put16(1); put16(1); put32(decoded.sample_rate);
        put32(decoded.sample_rate * 2); put16(2); put16(16); wav.write("data", 4);
        put32(data_bytes);
        for (const auto value : decoded.mono) put16(static_cast<std::uint16_t>(value));
        if (!wav) { std::cerr << "failed while writing WAV\n"; return 2; }
        std::cout << "sample=" << index << " rate=" << decoded.sample_rate
                  << " Hz channels=1 bits=16 frames=" << decoded.mono.size() << '\n';
        }
    }

    if (argc >= 3 && std::string(argv[2]) == "--play-test-tone") {
        openbmh::audio::Playback playback;
        if (!playback.initialize(error)) {
            std::cerr << "audio unavailable: " << error << '\n'; return 0;
        }
        constexpr std::uint32_t rate = 22050;
        std::vector<std::int16_t> tone(rate / 5);
        for (std::size_t i = 0; i < tone.size(); ++i)
            tone[i] = static_cast<std::int16_t>(6000.0 * std::sin(2.0 * 3.141592653589793 * 440.0 * i / rate));
        if (!playback.play_pcm(tone.data(), tone.size(), rate, error)) {
            std::cerr << "test tone rejected: " << error << '\n'; return 1;
        }
        SDL_Delay(250);
        playback.stop();
    }
    return 0;
}
