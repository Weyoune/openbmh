// SPDX-License-Identifier: GPL-3.0-only
#include "container.hpp"
#include "music_fallback.hpp"
#include "playback.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << "check failed at line " << __LINE__ << ": " << #expr << '\n'; \
    return 1; \
} } while (false)
std::vector<std::uint8_t> fixture() {
    // Authored format header and payload, not bytes copied from a game asset.
    return {'F','O','R','M', 17,0,0,0, 'A','L','P','T','R','A','U','M',
            'H','E','D','R', 1,0,0,0, 42};
}

std::vector<std::uint8_t> sfx_fixture() {
    std::vector<std::uint8_t> bytes(65380, 0);
    std::copy_n("FORM", 4, bytes.begin());
    const auto put32 = [&bytes](std::size_t at, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (i * 8));
    };
    const auto put16 = [&bytes](std::size_t at, std::uint16_t value) {
        bytes[at] = static_cast<std::uint8_t>(value);
        bytes[at + 1] = static_cast<std::uint8_t>(value >> 8);
    };
    put32(4, static_cast<std::uint32_t>(bytes.size()));
    std::copy_n("ALPTRAUM", 8, bytes.begin() + 8);
    const auto chunk = [&bytes, &put32](std::size_t at, const char* id, std::uint32_t size) {
        std::copy_n(id, 4, bytes.begin() + static_cast<std::ptrdiff_t>(at));
        put32(at + 4, size);
    };
    chunk(16, "HEDR", 8);
    chunk(32, "SEFX", 212);
    bytes[42] = 7;
    for (std::size_t i = 7; i < 13; ++i) {
        const std::size_t row = 44 + i * 16;
        bytes[row] = static_cast<std::uint8_t>(16 + i - 7);
        bytes[row + 1] = 9;
    }
    chunk(252, "INST", 228);
    chunk(488, "SAMP", 64884);
    const std::size_t samp = 496;
    put16(samp + 2, 6);
    const std::size_t data_at = samp + 4 + 6 * 16;
    for (std::size_t i = 0; i < 6; ++i) {
        const std::size_t row = samp + 4 + i * 16;
        put16(row, static_cast<std::uint16_t>(i * 2048));
        put16(row + 2, 2048);
        put16(row + 8, 11025);
        std::fill(bytes.begin() + static_cast<std::ptrdiff_t>(data_at + i * 2048),
                  bytes.begin() + static_cast<std::ptrdiff_t>(data_at + (i + 1) * 2048),
                  static_cast<std::uint8_t>(128 + i));
    }
    return bytes;
}
}

int main(int, char**) {  // (the SDL_main signature: SDL.h renames main on Windows)
    using namespace openbmh::audio;
    std::string error;
    Container c;
    auto good = fixture();
    CHECK(parse_container(good, c, error));
    CHECK(c.chunks.size() == 1 && c.chunks[0].id == "HEDR");
    CHECK(c.chunks[0].payload_size == 1 && good[c.chunks[0].payload_offset] == 42);

    auto broken = good;
    broken[20] = 0xff;
    broken[21] = 0xff;
    broken[22] = 0xff;
    broken[23] = 0x7f;
    CHECK(!parse_container(broken, c, error));
    CHECK(c.chunks.empty());
    CHECK(!parse_container(std::span<const std::uint8_t>(good.data(), 15), c, error));

    std::vector<std::uint8_t> sefx(65380, 0);
    const char form[] = "FORM";
    std::copy(form, form + 4, sefx.begin());
    sefx[4] = static_cast<std::uint8_t>(sefx.size());
    sefx[5] = static_cast<std::uint8_t>(sefx.size() >> 8);
    const char type[] = "ALPTRAUM";
    std::copy(type, type + 8, sefx.begin() + 8);
    std::size_t at = 16;
    const auto put_chunk = [&](const char* id, std::uint32_t size, std::size_t& cursor) {
        std::copy(id, id + 4, sefx.begin() + static_cast<std::ptrdiff_t>(cursor));
        sefx[cursor + 4] = static_cast<std::uint8_t>(size);
        sefx[cursor + 5] = static_cast<std::uint8_t>(size >> 8);
        cursor += 8 + size;
    };
    put_chunk("HEDR", 8, at);
    put_chunk("SEFX", 212, at);
    sefx[42] = 7; // observed 2003 SEFX record-count field
    put_chunk("INST", 228, at);
    put_chunk("SAMP", 64884, at);
    CHECK(at == sefx.size());
    CHECK(parse_container(sefx, c, error));
    CHECK(c.chunks.size() == 4 && c.chunks[1].id == "SEFX");
    auto wrong_exception = sefx;
    wrong_exception[32] = 'S'; wrong_exception[33] = 'O';
    wrong_exception[34] = 'N'; wrong_exception[35] = 'G';
    CHECK(!parse_container(wrong_exception, c, error));
    auto wrong_type = good;
    wrong_type[8] = 'X';
    CHECK(!parse_container(wrong_type, c, error));

    std::vector<std::uint8_t> samp(24, 0);
    samp[2] = 1;
    samp[6] = 4;
    samp[12] = 0x22; samp[13] = 0x56; // 22050 Hz
    samp[20] = 0; samp[21] = 128; samp[22] = 255; samp[23] = 128;
    PcmSample pcm;
    CHECK(decode_sample(samp, 0, pcm, error));
    CHECK(pcm.sample_rate == 22050 && pcm.mono.size() == 4);
    CHECK(pcm.mono[0] == -32768 && pcm.mono[1] == 0 && pcm.mono[2] == 32512);
    samp[6] = 0xff; samp[7] = 0xff;
    CHECK(!decode_sample(samp, 0, pcm, error));
    CHECK(!decode_sample(samp, 1, pcm, error));

    auto bank = sfx_fixture();
    OriginalCue original;
    CHECK(decode_original_cue(bank, 17, original, error));
    CHECK(original.original_id == 17 && original.sefx_index == 7 && original.sample_index == 0);
    CHECK(original.pcm.sample_rate == 11025 && original.pcm.mono.size() == 2048);
    CHECK(original.pcm.mono.front() == 0);
    CHECK(decode_original_cue(bank, 22, original, error));
    CHECK(original.sefx_index == 12 && original.sample_index == 5);
    CHECK(!decode_original_cue(bank, 16, original, error));

    CHECK(SDL_InitSubSystem(SDL_INIT_VIDEO) == 0);
    Playback playback;
    CHECK(playback.initialize(error));
    CHECK(playback.available());
    CHECK(!playback.music_active() && !playback.needs_pump());
    CHECK(!playback.pump(error)); // The optional music path is inactive by default.
    playback.set_volume(2.0F);
    CHECK(playback.volume() == 1.0F);
    playback.set_volume(-1.0F);
    CHECK(playback.volume() == 0.0F);
    playback.set_volume(1.0F);
    playback.set_volume(std::numeric_limits<float>::quiet_NaN());
    CHECK(playback.volume() == 1.0F);
    playback.set_volume(std::numeric_limits<float>::infinity());
    CHECK(playback.volume() == 1.0F);
    playback.set_muted(true);
    CHECK(playback.muted());
    CHECK(playback.play_pcm(nullptr, 0, 22050, error)); // mute drops without queueing
    playback.set_muted(false);
    const std::int16_t sample[] = {0, 1000, -1000, 0};
    CHECK(playback.play_pcm(sample, 4, 22050, error));
    std::vector<std::int16_t> long_sample(22050, 1000);
    CHECK(playback.play_pcm(long_sample.data(), long_sample.size(), 22050, error));
    CHECK(playback.queued_bytes() > 0);
    playback.set_muted(true);
    CHECK(playback.queued_bytes() == 0);
    playback.set_muted(false);
    CHECK(playback.play_pcm(long_sample.data(), long_sample.size(), 22050, error));
    CHECK(playback.queued_bytes() > 0);

    playback.reset_session(7);
    CHECK(playback.queued_bytes() == 0);
    playback.reset_session(7); // same session preserves the event watermark
    CHECK(playback.load_original_bank(bank, error));

    const CueRequest cue_a{7, 1, 17};
    CHECK(playback.submit(cue_a, error) == CueStatus::queued);
    CHECK(playback.queued_bytes() > 0);
    CHECK(playback.submit(cue_a, error) == CueStatus::duplicate);
    CHECK(playback.submit(CueRequest{7, 2, 17}, error) == CueStatus::queued);
    playback.set_muted(true);
    const CueRequest muted{7, 3, 17};
    CHECK(playback.submit(muted, error) == CueStatus::muted);
    playback.set_muted(false);
    CHECK(playback.submit(muted, error) == CueStatus::duplicate);
    playback.stop();
    CHECK(playback.submit(cue_a, error) == CueStatus::duplicate);
    playback.reset_session(8);
    CHECK(playback.submit(CueRequest{8, 1, 17}, error) == CueStatus::queued);
    CHECK(playback.submit(CueRequest{7, 4, 17}, error) == CueStatus::invalid);
    CHECK(playback.submit(CueRequest{8, 1, 17}, error) == CueStatus::duplicate);

    PcmSample kickoff_pcm, goal_pcm, final_pcm;
    CHECK(synthesize_semantic_cue(SemanticCue::kickoff_whistle, kickoff_pcm, error));
    CHECK(synthesize_semantic_cue(SemanticCue::goal, goal_pcm, error));
    CHECK(synthesize_semantic_cue(SemanticCue::full_time_whistle, final_pcm, error));
    CHECK(kickoff_pcm.sample_rate == 22050 && goal_pcm.sample_rate == 22050 &&
          final_pcm.sample_rate == 22050);
    CHECK(!kickoff_pcm.mono.empty() && !goal_pcm.mono.empty() && !final_pcm.mono.empty());
    CHECK(kickoff_pcm.mono != goal_pcm.mono && goal_pcm.mono != final_pcm.mono &&
          kickoff_pcm.mono != final_pcm.mono);
    PcmSample matchday_loop, repeated_loop;
    CHECK(synthesize_matchday_music_loop(matchday_loop, error));
    CHECK(synthesize_matchday_music_loop(repeated_loop, error));
    CHECK(matchday_loop.sample_rate == 22050);
    CHECK(matchday_loop.mono.size() == 392000); // 8 bars, 4 beats, 108 BPM.
    CHECK(matchday_loop.mono == repeated_loop.mono); // deterministic, no RNG.
    CHECK(matchday_loop.mono.front() == 0 && matchday_loop.mono.back() == 0);
    CHECK(std::any_of(matchday_loop.mono.begin(), matchday_loop.mono.end(),
                      [](std::int16_t value) { return value != 0; }));

    Mixer mixer;
    PcmSample constant_music{22050, std::vector<std::int16_t>(8, 1000)};
    const std::int16_t voice_samples[] = {2000, 2000, 2000, 2000};
    CHECK(mixer.start_music_loop(constant_music, 22050, error));
    CHECK(mixer.add_voice(voice_samples, 22050, 22050, error));
    CHECK(mixer.active_voices() == 1);
    std::int16_t mixed_block[2]{};
    CHECK(mixer.mix_block(mixed_block, 1.0F));
    CHECK(mixed_block[0] == 3000 && mixed_block[1] == 3000); // Cue overlays the loop.
    mixer.stop_music();
    CHECK(!mixer.has_music() && mixer.active_voices() == 1);
    std::int16_t voice_tail[4]{};
    CHECK(mixer.mix_block(voice_tail, 1.0F)); // stop_music leaves the cue voice alive.
    CHECK(voice_tail[0] == 2000 && voice_tail[1] == 2000 &&
          voice_tail[2] == 0 && voice_tail[3] == 0);
    CHECK(!mixer.active());
    mixer.clear();
    CHECK(mixer.start_music_loop(constant_music, 22050, error));
    for (int i = 0; i < 8; ++i) CHECK(mixer.add_voice(voice_samples, 22050, 22050, error));
    CHECK(mixer.active_voices() == 8);
    CHECK(!mixer.add_voice(voice_samples, 22050, 22050, error));
    mixer.clear();
    static_assert(static_cast<std::uint8_t>(SemanticCue::kickoff_whistle) == 1);
    static_assert(static_cast<std::uint8_t>(SemanticCue::goal) == 2);
    static_assert(static_cast<std::uint8_t>(SemanticCue::full_time_whistle) == 3);
    playback.reset_session(9);
    const SemanticCueRequest kickoff{9, 1, SemanticCue::kickoff_whistle};
    CHECK(playback.submit(kickoff, error) == CueStatus::queued);
    CHECK(playback.queued_bytes() > 0);
    CHECK(playback.submit(kickoff, error) == CueStatus::duplicate);
    CHECK(playback.submit(SemanticCueRequest{9, 2, SemanticCue::kickoff_whistle}, error) == CueStatus::queued);
    playback.set_muted(true);
    const SemanticCueRequest muted_goal{9, 3, SemanticCue::goal};
    CHECK(playback.submit(muted_goal, error) == CueStatus::muted);
    playback.set_muted(false);
    CHECK(playback.submit(muted_goal, error) == CueStatus::duplicate);
    CHECK(playback.submit(SemanticCueRequest{9, 4, SemanticCue::goal}, error) == CueStatus::queued);
    playback.reset_session(10);
    CHECK(playback.queued_bytes() == 0);
    CHECK(playback.submit(SemanticCueRequest{10, 1, SemanticCue::full_time_whistle}, error) == CueStatus::queued);
    CHECK(playback.queued_bytes() > 0);

    playback.reset_session(11);
    CHECK(playback.submit(CueRequest{11, 1, 17}, error) == CueStatus::queued);
    CHECK(playback.submit(SemanticCueRequest{11, 1, SemanticCue::goal}, error) == CueStatus::duplicate);
    CHECK(playback.submit(SemanticCueRequest{11, 2, SemanticCue::goal}, error) == CueStatus::queued);
    CHECK(playback.submit(CueRequest{11, 2, 17}, error) == CueStatus::duplicate);
    CHECK(playback.submit(SemanticCueRequest{10, 3, SemanticCue::goal}, error) == CueStatus::invalid);
    CHECK(playback.submit(CueRequest{11, 0, 17}, error) == CueStatus::invalid);
    CHECK(playback.submit(SemanticCueRequest{11, 3, SemanticCue::full_time_whistle}, error) == CueStatus::queued);

    // Optional music is explicit. Cues submitted while it is active enter the
    // mixer immediately; pump emits short mixed blocks, never a whole loop.
    playback.reset_session(12);
    CHECK(playback.play_pcm(long_sample.data(), long_sample.size(), 22050, error));
    CHECK(playback.queued_bytes() > 0);
    CHECK(!playback.start_music_loop(matchday_loop, error)); // Reject >40ms legacy cue backlog.
    CHECK(!playback.music_active());
    CHECK(playback.queued_bytes() > 0); // Existing queued cue was not discarded.
    playback.stop();
    CHECK(!playback.needs_pump());
    CHECK(playback.start_music_loop(matchday_loop, error));
    CHECK(playback.pump(error));
    CHECK(playback.queued_bytes() <= 40U * 22050U * 2U / 1000U);
    // Refill after typical 60/30 Hz app ticks. This checks the bounded refill,
    // not continuous audible output: the dummy backend is not a listening test.
    for (const unsigned delay : {17U, 33U}) {
        for (unsigned tick = 0; tick < 12; ++tick) {
            SDL_Delay(delay);
            CHECK(playback.pump(error));
            CHECK(error.empty());
            CHECK(playback.queued_bytes() > 0);
            CHECK(playback.queued_bytes() <= 40U * 22050U * 2U / 1000U);
        }
    }
    CHECK(playback.submit(CueRequest{12, 1, 17}, error) == CueStatus::queued);
    bool cue_pumped = false;
    for (unsigned attempt = 0; attempt < 5 && !cue_pumped; ++attempt) {
        cue_pumped = playback.pump(error);
        if (cue_pumped)
            CHECK(playback.queued_bytes() <= 40U * 22050U * 2U / 1000U);
        if (!cue_pumped) SDL_Delay(25);
    }
    CHECK(cue_pumped);
    const SemanticCueRequest music_muted_event{12, 2, SemanticCue::goal};
    playback.set_muted(true); // Clears music and both queued and active voices.
    CHECK(!playback.music_active());
    CHECK(!playback.pump(error));
    CHECK(playback.submit(music_muted_event, error) == CueStatus::muted);
    playback.set_muted(false);
    CHECK(playback.submit(music_muted_event, error) == CueStatus::duplicate);
    CHECK(!playback.pump(error));

    // A zero master volume still advances an already active voice silently.
    playback.reset_session(13);
    CHECK(playback.start_music_loop(matchday_loop, error));
    const std::int16_t short_voice[] = {1000, 1000, 1000, 1000};
    CHECK(playback.play_pcm(short_voice, 4, 22050, error));
    playback.set_volume(0.0F);
    playback.stop_music();
    CHECK(playback.needs_pump());
    CHECK(playback.pump(error));
    CHECK(!playback.needs_pump());
    playback.set_volume(1.0F);
    CHECK(!playback.pump(error)); // No delayed replay after restoring volume.

    playback.reset_session(13);
    CHECK(!playback.music_active() && !playback.pump(error));
    CHECK(playback.start_music_loop(matchday_loop, error));
    CHECK(playback.needs_pump());
    playback.reset_session(14); // A real session change clears music and queue.
    CHECK(!playback.music_active() && !playback.needs_pump() && playback.queued_bytes() == 0);
    CHECK(playback.start_music_loop(matchday_loop, error));
    CHECK(playback.submit(CueRequest{14, 1, 17}, error) == CueStatus::queued);
    playback.stop_music();
    CHECK(!playback.music_active());
    CHECK(playback.needs_pump());
    CHECK(playback.pump(error)); // Stopping music alone preserves an active cue.
    playback.stop();
    CHECK(!playback.needs_pump());
    CHECK(!playback.pump(error));

    Playback unavailable;
    unavailable.reset_session(20);
    const CueRequest unavailable_event{20, 1, 17};
    CHECK(!unavailable.available());
    CHECK(!unavailable.start_music_loop(matchday_loop, error));
    CHECK(unavailable.submit(unavailable_event, error) == CueStatus::disabled);
    playback.shutdown();
    CHECK(unavailable.initialize(error));
    CHECK(unavailable.submit(unavailable_event, error) == CueStatus::duplicate);
    CHECK(unavailable.queued_bytes() == 0); // A disabled event is not replayed after device recovery.
    unavailable.shutdown();
    CHECK(playback.initialize(error));
    playback.reset_session(15);
    CHECK(playback.start_music_loop(matchday_loop, error));
    CHECK(playback.submit(CueRequest{15, 1, 17}, error) == CueStatus::queued);
    playback.shutdown(); // Device shutdown clears the optional track and active cue.
    CHECK(!playback.available() && !playback.music_active());
    playback.shutdown();
    CHECK(!playback.available());
    CHECK((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) != 0);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    std::cout << "checks passed: original/semantic shared event ledger; stale and zero IDs; unavailable device and no replay; deterministic eight-bar loop; PCM overlay and stop-music cue tail; fixed voice cap; mute/reset/shutdown clear music; SDL dummy lifecycle; no listening test\n";
    return 0;  // (explicit: main is SDL_main here, no implicit return 0)
}
