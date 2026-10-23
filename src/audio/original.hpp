// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace openbmh::audio {

// Read from the user's executable by the application. Audio does not know the
// game state, filesystem layout, simulation clock, or random generators.
struct OriginalTables {
    std::array<std::uint16_t, 96> frequencies{}; // Audio_OriginalAudioAssets_RuntimeField2, including key-on bits
    std::array<std::uint8_t, 9> operators{};    // Audio_OriginalAudioAssets_RuntimeField, stride16
};
struct OriginalAssets {
    std::array<std::vector<std::uint8_t>, 4> containers; // S/2000..2003
    OriginalTables tables;
};
struct OriginalAudioOptions {
    bool music = true;
    bool effects = true;
    bool clicks = true;
};
struct OriginalAudioStats {
    std::uint64_t ticks{}, register_writes{}, sample_starts{}, frames{};
    std::uint32_t bar{}, step{}, song = 255;
    bool playing{}, ambient{}, failed{};
};
// Timing of one song: one sequencer step every `tempo` IRQ0 ticks, 16 steps per
// bar; after bar `end` playback continues at bar `repeat` (end == repeat: stop).
struct OriginalSongTiming {
    std::uint16_t tempo{}, end{}, repeat{};
};
// One note the music sequencer actually started (offline tools such as the MIDI
// export). `pitch` is the index into the OPL note table with the track transpose
// applied; `duration` counts sequencer steps until key-off (255 = held until the
// channel's next note); `level` is the OPL output level 0..63. Sample notes
// (track 9) carry the sample slot in `pitch` and no channel, duration or level.
struct OriginalNoteEvent {
    std::uint32_t bar{}, step{};
    std::uint8_t track{}, channel{}, instrument{}, pitch{}, duration{}, level{};
    bool sample{};
};

// The Original MIDILINK sequencer and one Sound Blaster PCM channel. IRQ0 uses
// PIT divisor0x199a. SONG/TRCK/PART, OPL instruments, SFX priorities/lifetimes,
// note durations, sample interruption and the looping crowd are Source-derived.
// Loading owns/validates its inputs. render() allocates nothing and never blocks.
class OriginalSynth {
public:
    OriginalSynth();
    ~OriginalSynth();
    OriginalSynth(const OriginalSynth&) = delete;
    OriginalSynth& operator=(const OriginalSynth&) = delete;
    bool load(OriginalAssets assets, std::uint32_t output_rate, std::string& error);
    bool ready() const noexcept;
    void set_options(OriginalAudioOptions options) noexcept;
    // Source dispatch IDs:0 title,1 desk,2 intro,3 crowd,8 stop,10..22 effects.
    bool command(std::uint8_t id) noexcept;
    // Direct SFX entry, also used by the standalone driver/oracle.
    bool effect(std::uint8_t id) noexcept;
    void stop() noexcept;
    void render(std::span<std::int16_t> output, float volume = 1.0F) noexcept;
    OriginalAudioStats stats() const noexcept;

    // Deterministic hardware-independent probe: one Original IRQ0, not one game
    // tick. Optional observers are installed only by offline validation tools.
    void tick() noexcept;
    void set_register_observer(std::function<void(std::uint8_t, std::uint8_t)> observer);
    void set_sample_observer(std::function<void(std::uint8_t)> observer);
    void set_note_observer(std::function<void(const OriginalNoteEvent&)> observer);
    // Timing of song 0..2; false before load() or for another index.
    bool song_timing(unsigned song, OriginalSongTiming& timing) const noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace openbmh::audio
