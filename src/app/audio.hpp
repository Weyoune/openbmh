// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#pragma once

#include "audio/playback.hpp"
#include "formats/le.hpp"
#include "game/state.hpp"
#include "ui/setup.hpp"

namespace bmh::app {

inline openbmh::audio::OriginalAssets original_audio_assets(
    const std::filesystem::path& directory, const LeImage& exe) {
    openbmh::audio::OriginalAssets assets;
    for (unsigned i = 0; i < 4; ++i)
        assets.containers[i] = read_file(ui::asset_path(directory / "S", std::to_string(2000 + i)));
    for (unsigned i = 0; i < assets.tables.frequencies.size(); ++i)
        assets.tables.frequencies[i] = exe.u16(::bmh::layout::Audio_OriginalAudioAssets_RuntimeField2 + 2 * i);
    for (unsigned i = 0; i < assets.tables.operators.size(); ++i)
        assets.tables.operators[i] = exe.u8(::bmh::layout::Audio_OriginalAudioAssets_RuntimeField + 16 * i);
    return assets;
}

inline openbmh::audio::OriginalAudioOptions original_audio_options(const GameState& state, bool music) {
    const auto on = [&](std::uint32_t address) {
        return !state.memory.has(address) || (state.memory.u8(address) & 0x7f) == 0;
    };
    // A5 governs match presentation and is checked by the match observer. It
    // must not mute menu clicks. A6/A7 are independent saved music/click switches.
    return {music && on(::bmh::layout::Audio_OriginalAudioOptions_ExecutableField), true, on(::bmh::layout::Audio_OriginalAudioOptions_ExecutableField2)};
}
inline bool match_sound_enabled(const GameState& state) {
    return !state.memory.has(::bmh::layout::Audio_MatchSoundEnabled_RuntimeField) || (state.memory.u8(::bmh::layout::Audio_MatchSoundEnabled_RuntimeField) & 0x7f) == 0;
}
inline void original_audio_command(openbmh::audio::Playback& playback, std::uint8_t id) {
    if (!playback.original_ready() || !playback.available()) return;
    std::string error;
    playback.play_original_id(id, error);
}
inline void original_match_audio_command(openbmh::audio::Playback& playback,
                                         const GameState& state, std::uint8_t id) {
    if (match_sound_enabled(state)) original_audio_command(playback, id);
    else if (id == 3) original_audio_command(playback, 8); // Match entry still stops desk music.
}

// One callback per actual SDL mouse event, including nested modal loops. No
// rendering/hover polling, no scene inference, and no application/GameState access
// from a possible SDL event thread. Source OriginalMouseSound maps left2/right8 to10/11.
class OriginalMouseSound {
public:
    explicit OriginalMouseSound(openbmh::audio::Playback& p) : playback_(p) {
        SDL_AddEventWatch(watch, this);
    }
    ~OriginalMouseSound() { detach(); }
    void detach() noexcept {
        if (!attached_) return;
        SDL_DelEventWatch(watch, this);
        attached_ = false;
    }
    OriginalMouseSound(const OriginalMouseSound&) = delete;
    OriginalMouseSound& operator=(const OriginalMouseSound&) = delete;
private:
    static int watch(void* data, SDL_Event* event) {
        if (event->type == SDL_MOUSEBUTTONDOWN) {
            auto& self = *static_cast<OriginalMouseSound*>(data);
            if (event->button.button == SDL_BUTTON_LEFT) original_audio_command(self.playback_, 10);
            if (event->button.button == SDL_BUTTON_RIGHT) original_audio_command(self.playback_, 11);
        }
        return 1;
    }
    openbmh::audio::Playback& playback_;
    bool attached_ = true;
};

} // namespace bmh::app
