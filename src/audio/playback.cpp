// SPDX-License-Identifier: GPL-3.0-only
#include "playback.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace openbmh::audio {
namespace {
struct AudioLock {
    SDL_AudioDeviceID device;
    explicit AudioLock(SDL_AudioDeviceID id) : device(id) { if (device) SDL_LockAudioDevice(device); }
    ~AudioLock() { if (device) SDL_UnlockAudioDevice(device); }
};
constexpr std::uint32_t kQueueLimitBytes = 2U * 1024U * 1024U;
constexpr std::uint32_t kOutputRate = 22050;
constexpr std::size_t kPumpFrames = kOutputRate / 50; // 20 ms.
constexpr std::uint32_t kPumpBlockBytes = static_cast<std::uint32_t>(kPumpFrames * 2);
constexpr std::uint32_t kQueueHighWaterBytes = kPumpBlockBytes * 2; // 40 ms.
}

Playback::~Playback() { shutdown(); }

bool Playback::initialize(std::string& error) {
    error.clear();
    if (device_) return true;
    if (!initialized_audio_ && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        error = SDL_GetError();
        return false;
    }
    initialized_audio_ = true;
    SDL_AudioSpec wanted{};
    wanted.freq = static_cast<int>(kOutputRate);
    wanted.format = AUDIO_S16SYS;
    wanted.channels = 1;
    // 1024 frames at 22050 Hz exceed the entire 40 ms music queue. A small
    // device block lets the bounded queue cover 17/33 ms app pump intervals.
    wanted.samples = 256;
    if (original_mode_) {
        wanted.callback = original_callback;
        wanted.userdata = this;
    }
    device_ = SDL_OpenAudioDevice(nullptr, 0, &wanted, &obtained_, 0);
    if (!device_) {
        error = SDL_GetError();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        initialized_audio_ = false;
        return false;
    }
    if (obtained_.format != AUDIO_S16SYS || obtained_.channels != 1) {
        error = "audio device did not provide mono S16 output";
        SDL_CloseAudioDevice(device_);
        device_ = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        initialized_audio_ = false;
        return false;
    }
    SDL_PauseAudioDevice(device_, 0);
    return true;
}

bool Playback::initialize_original(OriginalAssets assets, std::string& error) {
    // Asset parsing/preflight never runs inside a live audio callback.
    if (device_) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
    mixer_.clear();
    if (!original_.load(std::move(assets), kOutputRate, error)) return false;
    original_.set_options(original_options_);
    original_mode_ = true;
    return initialize(error);
}

void Playback::original_callback(void* userdata, Uint8* stream, int length) {
    auto& self = *static_cast<Playback*>(userdata);
    SDL_memset(stream, 0, size_t(length));
    if (length <= 0 || self.muted_) return;
    self.original_.render(std::span<std::int16_t>(reinterpret_cast<std::int16_t*>(stream),
        size_t(length) / sizeof(std::int16_t)), self.volume_);
}

void Playback::set_original_options(OriginalAudioOptions options) noexcept {
    if (!original_mode_) return;
    AudioLock lock(device_);
    original_options_ = options;
    original_.set_options(options);
}

bool Playback::original_ready() const noexcept {
    if (!original_mode_) return false;
    AudioLock lock(device_);
    return original_.ready();
}

OriginalAudioStats Playback::original_stats() const noexcept {
    AudioLock lock(original_mode_ ? device_ : 0);
    return original_.stats();
}

void Playback::shutdown() {
    stop();
    if (device_) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
    if (initialized_audio_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        initialized_audio_ = false;
    }
}

void Playback::set_volume(float volume) noexcept {
    AudioLock lock(original_mode_ ? device_ : 0);
    if (std::isfinite(volume)) volume_ = std::clamp(volume, 0.0F, 1.0F);
}

void Playback::set_muted(bool muted) noexcept {
    if (original_mode_) {
        AudioLock lock(device_);
        muted_ = muted;
        if (muted) original_.stop();
        return;
    }
    muted_ = muted;
    if (muted_) stop();
}

std::uint32_t Playback::queued_bytes() const noexcept {
    if (original_mode_) return 0;
    return device_ ? SDL_GetQueuedAudioSize(device_) : 0;
}

void Playback::stop() {
    if (original_mode_) {
        AudioLock lock(device_);
        original_.stop();
        return;
    }
    if (device_) SDL_ClearQueuedAudio(device_);
    mixer_.clear();
}

bool Playback::start_music_loop(const PcmSample& loop, std::string& error) {
    error.clear();
    if (original_mode_) { error = "Original driver owns music playback"; return false; }
    if (!device_) { error = "audio device is unavailable"; return false; }
    if (obtained_.freq != static_cast<int>(kOutputRate) || obtained_.samples > 256) {
        error = "audio device block does not support the bounded music pump cadence";
        return false;
    }
    if (SDL_GetQueuedAudioSize(device_) > kQueueHighWaterBytes) {
        error = "cannot start music while the existing cue queue exceeds 40 ms";
        return false;
    }
    return mixer_.start_music_loop(loop, static_cast<std::uint32_t>(obtained_.freq), error);
}

void Playback::stop_music() noexcept {
    if (original_mode_) {
        AudioLock lock(device_);
        original_options_.music = false;
        original_.set_options(original_options_);
        return;
    }
    mixer_.stop_music();
}

bool Playback::pump(std::string& error) {
    error.clear();
    if (original_mode_) return available();
    if (!device_) { error = "audio device is unavailable"; return false; }
    if (muted_ || !mixer_.active()) return false;

    std::array<std::int16_t, kPumpFrames> block{};
    auto enqueue_up_to_20ms = [&]() {
        const auto queued = SDL_GetQueuedAudioSize(device_);
        if (queued >= kQueueHighWaterBytes) return false;
        const auto room_frames = (kQueueHighWaterBytes - queued) / sizeof(std::int16_t);
        const auto frames = std::min<std::size_t>(kPumpFrames, room_frames);
        if (!frames || !mixer_.mix_block(std::span<std::int16_t>(block.data(), frames), volume_))
            return false;
        const auto bytes = static_cast<std::uint32_t>(frames * sizeof(std::int16_t));
        if (SDL_QueueAudio(device_, block.data(), bytes) != 0) {
            error = SDL_GetError();
            mixer_.clear();
            return false;
        }
        return true;
    };

    // Fill toward the 40 ms high-water mark. Two fixed, bounded calls let a
    // 30 Hz main loop catch up without busy waiting or allocating in pump().
    const bool first = enqueue_up_to_20ms();
    if (!first || !mixer_.active()) return first;
    enqueue_up_to_20ms();
    return error.empty();
}

void Playback::reset_session(std::uint64_t session_id) noexcept {
    if (session_id == session_id_) return;
    stop();
    session_id_ = session_id;
    last_event_id_ = 0;
}

CueStatus Playback::consume_event(std::uint64_t session_id, std::uint64_t event_id,
                                  std::string& detail) {
    detail.clear();
    if (!session_id_ || session_id != session_id_ || event_id == 0) {
        detail = "invalid or stale audio event envelope";
        return CueStatus::invalid;
    }
    if (event_id <= last_event_id_) return CueStatus::duplicate;
    last_event_id_ = event_id;
    if (muted_) return CueStatus::muted;
    if (!device_) return CueStatus::disabled;
    return CueStatus::queued;
}

CueStatus Playback::submit(CueRequest request, std::string& detail) {
    const auto gate = consume_event(request.session_id, request.event_id, detail);
    if (gate != CueStatus::queued) return gate;
    if (!play_original_id(request.original_id, detail)) return CueStatus::unsupported;
    return CueStatus::queued;
}

CueStatus Playback::submit(SemanticCueRequest request, std::string& detail) {
    const auto gate = consume_event(request.session_id, request.event_id, detail);
    if (gate != CueStatus::queued) return gate;
    if (!play_semantic_cue(request.cue, detail)) return CueStatus::unsupported;
    return CueStatus::queued;
}

bool Playback::load_original_bank(std::span<const std::uint8_t> bytes,
                                  std::string& error) {
    std::array<std::optional<PcmSample>, 13> decoded{};
    for (std::uint8_t id = 17; id <= 22; ++id) {
        OriginalCue cue;
        if (!decode_original_cue(bytes, id, cue, error)) return false;
        decoded[static_cast<std::size_t>(id - 10)] = std::move(cue.pcm);
    }
    stop();
    original_cues_ = std::move(decoded);
    error.clear();
    return true;
}

bool Playback::play_pcm(const std::int16_t* samples, std::size_t count,
                        std::uint32_t sample_rate, std::string& error) {
    error.clear();
    if (original_mode_) { error = "Original driver does not queue synthetic PCM"; return false; }
    if (!device_) { error = "audio device is unavailable"; return false; }
    if (muted_ || volume_ == 0.0F) return true;
    if (!samples || !count || sample_rate < 4000 || sample_rate > 192000) {
        error = "invalid PCM input"; return false;
    }
    if (mixer_.active()) {
        return mixer_.add_voice(std::span<const std::int16_t>(samples, count), sample_rate,
                                static_cast<std::uint32_t>(obtained_.freq), error);
    }
    if (count > std::numeric_limits<std::uint32_t>::max() / sizeof(std::int16_t)) {
        error = "PCM input is too large"; return false;
    }
    const std::uint64_t output_count64 =
        (static_cast<std::uint64_t>(count) * obtained_.freq + sample_rate / 2) / sample_rate;
    if (output_count64 == 0 || output_count64 > kQueueLimitBytes / sizeof(std::int16_t)) {
        error = "PCM input exceeds the bounded playback buffer"; return false;
    }
    const auto output_count = static_cast<std::size_t>(output_count64);
    const auto queued = SDL_GetQueuedAudioSize(device_);
    if (queued > kQueueLimitBytes - output_count * sizeof(std::int16_t)) {
        error = "playback queue is full"; return false;
    }
    std::vector<std::int16_t> converted(output_count);
    for (std::size_t i = 0; i < output_count; ++i) {
        const std::uint64_t numerator = static_cast<std::uint64_t>(i) * sample_rate;
        const auto source_index = std::min<std::size_t>(count - 1,
            static_cast<std::size_t>((numerator + obtained_.freq / 2) / obtained_.freq));
        const auto scaled = std::lround(static_cast<float>(samples[source_index]) * volume_);
        converted[i] = static_cast<std::int16_t>(std::clamp<long>(scaled, -32768, 32767));
    }
    const auto bytes = static_cast<std::uint32_t>(converted.size() * sizeof(std::int16_t));
    if (SDL_QueueAudio(device_, converted.data(), bytes) != 0) {
        error = SDL_GetError(); return false;
    }
    return true;
}

bool Playback::play_original_id(std::uint8_t id, std::string& error) {
    if (original_mode_) {
        error.clear();
        if (!device_) { error = "audio device is unavailable"; return false; }
        AudioLock lock(device_);
        if (muted_) return true;
        if (original_.command(id)) return true;
        // Disabled Original options are normal refusals, not decoder errors.
        if (id >= 10 && id <= 22 && original_.ready()) return true;
        error = "invalid Original sound command or failed sound driver";
        return false;
    }
    error.clear();
    if (id < 10 || id > 22 || !original_cues_[static_cast<std::size_t>(id - 10)]) {
        error = "original Sound ID has no decoded PCM cue (IDs 10..16 use unsupported instruments)";
        return false;
    }
    const auto& pcm = *original_cues_[static_cast<std::size_t>(id - 10)];
    return play_pcm(pcm.mono.data(), pcm.mono.size(), pcm.sample_rate, error);
}

bool Playback::play_semantic_cue(SemanticCue cue, std::string& error) {
    PcmSample pcm;
    if (!synthesize_semantic_cue(cue, pcm, error)) return false;
    return play_pcm(pcm.mono.data(), pcm.mono.size(), pcm.sample_rate, error);
}

} // namespace openbmh::audio
