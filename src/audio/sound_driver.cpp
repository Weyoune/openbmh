// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "container.hpp"
#include "music_fallback.hpp"
#include "original.hpp"
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
#include <sstream>
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

// ---------------------------------------------------------------------------
// MIDI export of the Original music (for remastering with other instruments).
//
// The sequencer runs offline exactly as in the game; the note observer records
// every note it starts. Each of the 16 song tracks becomes one MIDI track on the
// MIDI channel of the same number, so track 9, the sample (drum) track, lands on
// General MIDI channel 10. One sequencer step is a sixteenth note (16 steps per
// bar, 96 ticks per quarter); the tempo follows from the song's IRQ0 divisor.
// OPL pitches are converted through the note table of the user's executable.
// An optional map file assigns General MIDI programs and drum notes:
//   inst <instrument> <program 0..127>
//   sample <slot> <drum note 0..127>
// A template listing the instruments and samples a song uses is written next to
// the MIDI file (OUTPUT.map.txt) so the assignment can be edited and re-run.
struct MidiNote {
    std::uint32_t start{}, end{};
    std::uint8_t note{}, velocity{}, instrument{};
    bool sample{};
};
// Program per instrument and drum note per sample slot; -1 = not assigned.
struct MidiMap {
    std::array<int, 256> program;
    std::array<int, 256> drum;
    MidiMap() { program.fill(-1); drum.fill(-1); }
};
// Default General MIDI program from how an instrument is played: low lines become
// a bass, instruments that often sound in chords strings, high lines a trumpet and
// the remaining lines a brass section. Only a starting point for the editable map.
int guess_program(double mean_note, double chord_share) {
    if (mean_note < 50.0) return 33;        // Electric Bass (finger)
    if (chord_share > 0.25) return 48;      // String Ensemble 1
    return mean_note >= 70.0 ? 56 : 61;     // Trumpet for high lines, else Brass Section
}
// Default drum note from the sample itself: zero crossings per second separate
// dark (kick), middle (snare, tom) and bright (hi-hat, cymbal) sounds; the
// length separates closed from open or crash sounds.
int guess_drum(const openbmh::audio::PcmSample& pcm) {
    if (pcm.mono.empty() || !pcm.sample_rate) return 38;
    std::size_t crossings = 0;
    for (std::size_t i = 1; i < pcm.mono.size(); ++i)
        crossings += (pcm.mono[i - 1] < 0) != (pcm.mono[i] < 0);
    const double seconds = double(pcm.mono.size()) / pcm.sample_rate;
    const double rate = crossings / seconds;
    if (rate < 1200.0) return seconds < 0.6 ? 36 : 41;   // Bass Drum 1 / Low Floor Tom
    if (rate < 4000.0) return seconds < 0.4 ? 38 : 45;   // Acoustic Snare / Low Tom
    if (seconds < 0.15) return 42;                       // Closed Hi-Hat
    return seconds < 0.5 ? 46 : 49;                      // Open Hi-Hat / Crash Cymbal 1
}
bool read_midi_map(const std::filesystem::path& path, MidiMap& map, std::string& error) {
    std::ifstream in(path);
    if (!in) { error = "cannot read MIDI map"; return false; }
    std::string kind;
    unsigned line = 0;
    for (std::string text; std::getline(in, text);) {
        ++line;
        if (const auto hash = text.find('#'); hash != std::string::npos) text.resize(hash);
        std::istringstream fields(text);
        int index = -1, value = -1;
        if (!(fields >> kind)) continue;
        if (!(fields >> index >> value) || index < 0 || index > 255 || value < 0 || value > 127 ||
            (kind != "inst" && kind != "sample")) {
            error = "invalid MIDI map line " + std::to_string(line); return false;
        }
        (kind == "inst" ? map.program : map.drum)[std::size_t(index)] = value;
    }
    return true;
}
bool write_midi(const std::filesystem::path& path, const std::array<std::vector<MidiNote>, 16>& tracks,
                const MidiMap& map, std::uint32_t usec_per_quarter, std::uint32_t loop_start,
                std::uint32_t loop_end, std::string& error) {
    using Track = std::vector<std::uint8_t>;
    const auto vlq = [](Track& t, std::uint32_t value) {
        std::uint8_t bytes[5]; int n = 0;
        do { bytes[n++] = std::uint8_t(value & 0x7f); value >>= 7; } while (value);
        while (n--) t.push_back(std::uint8_t(bytes[n] | (n ? 0x80 : 0)));
    };
    const auto meta_text = [&vlq](Track& t, std::uint32_t delta, std::uint8_t type, const std::string& text) {
        vlq(t, delta); t.push_back(0xff); t.push_back(type); vlq(t, std::uint32_t(text.size()));
        t.insert(t.end(), text.begin(), text.end());
    };
    std::vector<Track> chunks;
    {   // Conductor track: tempo, 4/4, loop markers.
        Track t;
        t.insert(t.end(), {0x00, 0xff, 0x51, 0x03, std::uint8_t(usec_per_quarter >> 16),
                           std::uint8_t(usec_per_quarter >> 8), std::uint8_t(usec_per_quarter)});
        t.insert(t.end(), {0x00, 0xff, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08});
        std::uint32_t now = 0;
        if (loop_start != loop_end) {
            meta_text(t, loop_start, 0x06, "loop start"); now = loop_start;
            meta_text(t, loop_end - now, 0x06, "loop end"); now = loop_end;
        }
        t.insert(t.end(), {0x00, 0xff, 0x2f, 0x00});
        chunks.push_back(std::move(t));
    }
    for (unsigned index = 0; index < tracks.size(); ++index) {
        const auto& notes = tracks[index];
        if (notes.empty()) continue;
        const std::uint8_t channel = std::uint8_t(index);
        struct Event { std::uint32_t time; int order; std::uint8_t status, a, b; bool two; };
        std::vector<Event> events;
        int last_program = -1;
        for (const auto& n : notes) {
            if (!n.sample) {
                const int program = map.program[n.instrument];
                if (program != last_program) {
                    events.push_back({n.start, 1, std::uint8_t(0xc0 | channel), std::uint8_t(program), 0, false});
                    last_program = program;
                }
            }
            // Sample notes carry their slot in `instrument`; the map names the drum.
            const auto key = n.sample ? std::uint8_t(map.drum[n.instrument]) : n.note;
            events.push_back({n.start, 2, std::uint8_t(0x90 | channel), key, n.velocity, true});
            events.push_back({n.end, 0, std::uint8_t(0x80 | channel), key, 0, true});
        }
        // Note-offs before program changes before note-ons at the same time.
        std::stable_sort(events.begin(), events.end(), [](const Event& l, const Event& r) {
            return l.time != r.time ? l.time < r.time : l.order < r.order;
        });
        Track t;
        meta_text(t, 0, 0x03, index == 9 ? "track 9 (samples)" : "track " + std::to_string(index));
        std::uint32_t now = 0;
        for (const auto& e : events) {
            vlq(t, e.time - now); now = e.time;
            t.push_back(e.status); t.push_back(e.a);
            if (e.two) t.push_back(e.b);
        }
        t.insert(t.end(), {0x00, 0xff, 0x2f, 0x00});
        chunks.push_back(std::move(t));
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) { error = "cannot create MIDI output"; return false; }
    const auto put16 = [&out](unsigned v) { const char b[] = {char(v >> 8), char(v)}; out.write(b, 2); };
    const auto put32 = [&out](std::uint32_t v) {
        const char b[] = {char(v >> 24), char(v >> 16), char(v >> 8), char(v)}; out.write(b, 4);
    };
    out.write("MThd", 4); put32(6); put16(1); put16(unsigned(chunks.size())); put16(96);
    for (const auto& t : chunks) {
        out.write("MTrk", 4); put32(std::uint32_t(t.size()));
        out.write(reinterpret_cast<const char*>(t.data()), std::streamsize(t.size()));
    }
    if (!out) { error = "failed while writing MIDI output"; return false; }
    return true;
}
int export_midi(openbmh::audio::OriginalAssets assets, unsigned song, const std::filesystem::path& output,
                unsigned loops, const std::filesystem::path& map_path) {
    using namespace openbmh::audio;
    MidiMap map;
    std::string error;
    if (!map_path.empty() && !read_midi_map(map_path, map, error)) { std::cerr << error << '\n'; return 1; }
    // MIDI note of every entry of the OPL note table: F-number and block give the
    // frequency (49716 Hz OPL clock), rounded to the nearest equal-tempered note.
    std::array<std::uint8_t, 96> midi_note{};
    for (unsigned i = 0; i < midi_note.size(); ++i) {
        const auto f = assets.tables.frequencies[i];
        const double hz = double(f & 0x3ff) * 49716.0 / std::pow(2.0, 20 - ((f >> 10) & 7));
        midi_note[i] = std::uint8_t(std::clamp(std::lround(69.0 + 12.0 * std::log2(hz / 440.0)), 0L, 127L));
    }
    // The song's own bank, kept for analysing its drum samples after the run.
    const std::vector<std::uint8_t> bank = assets.containers.at(song);
    OriginalSynth synth;
    if (!synth.load(std::move(assets), 22050, error)) {
        std::cerr << "Original audio rejected: " << error << '\n'; return 1;
    }
    OriginalSongTiming timing;
    if (!synth.song_timing(song, timing)) { std::cerr << "unknown song\n"; return 1; }
    constexpr std::uint32_t kTicksPerStep = 24;  // 96 per quarter, a step is a sixteenth
    std::array<std::vector<MidiNote>, 16> tracks;
    // Last note per OPL channel: a new note on the channel ends the previous one.
    std::array<std::pair<int, std::size_t>, 9> sounding;
    sounding.fill({-1, 0});
    std::array<bool, 256> used_instrument{}, used_sample{};
    // The export ends after the first pass plus (loops - 1) passes of the loop
    // section; a song without a loop (end == repeat) ends after its last bar.
    const bool looping = timing.end != timing.repeat;
    const std::uint64_t last_step = std::uint64_t(timing.end) * 16 +
        (looping ? std::uint64_t(loops - 1) * (timing.end - timing.repeat) * 16 : 0);
    std::uint64_t tick = 0;
    synth.set_note_observer([&](const OriginalNoteEvent& e) {
        if (tick / timing.tempo >= last_step) return;
        const auto start = std::uint32_t(tick / timing.tempo) * kTicksPerStep;
        if (e.sample) {
            used_sample[e.pitch] = true;
            tracks[e.track].push_back({start, start + kTicksPerStep / 2, 0, 100, e.pitch, true});
            return;
        }
        used_instrument[e.instrument] = true;
        if (auto [track, index] = sounding[e.channel]; track >= 0) {
            auto& previous = tracks[std::size_t(track)][index];
            previous.end = std::min(previous.end, start);
            if (previous.end <= previous.start) previous.end = previous.start + 1;
        }
        // duration 0 releases in the step it starts; 255 holds until the next note.
        const std::uint32_t length = e.duration == 255 ? std::numeric_limits<std::uint32_t>::max() / 2
                                   : std::max<std::uint32_t>(e.duration * kTicksPerStep, kTicksPerStep / 4);
        tracks[e.track].push_back({start, start + length, midi_note[e.pitch],
                                   std::uint8_t(std::min(127, 1 + e.level * 2)), e.instrument, false});
        sounding[e.channel] = {int(e.track), tracks[e.track].size() - 1};
    });
    if (!synth.command(std::uint8_t(song))) { std::cerr << "Original command rejected\n"; return 1; }
    std::uint64_t steps = 0;
    for (; tick / timing.tempo < last_step; ++tick) {
        synth.tick();
        const auto stats = synth.stats();
        if (stats.failed) { std::cerr << "Original decoder failed during export\n"; return 1; }
        steps = tick / timing.tempo + 1;
        if (!stats.playing) break;
    }
    const auto song_end = std::uint32_t(std::min(steps, last_step)) * kTicksPerStep;
    const unsigned completed_loops = looping ? loops : 1;
    for (auto& notes : tracks)
        for (auto& n : notes) n.end = std::min(n.end, song_end);
    // One step lasts tempo * 6554 / 1193180 s; a quarter is four steps.
    const auto usec = std::uint32_t(std::lround(4.0 * timing.tempo * 6554.0 / 1193180.0 * 1e6));
    const std::uint32_t loop_start = timing.repeat * 16u * kTicksPerStep;
    const std::uint32_t loop_end = timing.end == timing.repeat ? loop_start : timing.end * 16u * kTicksPerStep;
    // Defaults for everything the map file leaves open (§ guess_program/guess_drum):
    // per instrument the mean note and the share of notes that start together with
    // another note of the same instrument (chords); per sample its sound.
    struct Usage { std::size_t notes{}, chord{}; double sum{}; int low{127}, high{0}; };
    std::array<Usage, 256> usage{};
    for (const auto& notes : tracks)
        for (std::size_t i = 0; i < notes.size(); ++i) {
            const auto& n = notes[i];
            if (n.sample) continue;
            auto& u = usage[n.instrument];
            ++u.notes; u.sum += n.note; u.low = std::min<int>(u.low, n.note); u.high = std::max<int>(u.high, n.note);
        }
    for (const auto& a : tracks) for (const auto& n : a) {
        if (n.sample) continue;
        bool together = false;
        for (const auto& b : tracks) for (const auto& m : b)
            if (&m != &n && !m.sample && m.instrument == n.instrument && m.start == n.start) { together = true; break; }
        if (together) ++usage[n.instrument].chord;
    }
    Container container;
    std::span<const std::uint8_t> samp;
    if (parse_container(bank, container, error))
        for (const auto& c : container.chunks)
            if (c.id == "SAMP") samp = std::span<const std::uint8_t>(bank).subspan(c.payload_offset, c.payload_size);
    for (unsigned i = 0; i < 256; ++i) {
        if (used_instrument[i] && map.program[i] < 0) {
            const auto& u = usage[i];
            map.program[i] = guess_program(u.notes ? u.sum / u.notes : 60.0, u.notes ? double(u.chord) / u.notes : 0.0);
        }
        if (used_sample[i] && map.drum[i] < 0) {
            PcmSample pcm;
            map.drum[i] = !samp.empty() && decode_sample(samp, i, pcm, error) ? guess_drum(pcm) : 38;
        }
    }
    if (!write_midi(output, tracks, map, usec, loop_start, loop_end, error)) { std::cerr << error << '\n'; return 1; }
    auto template_path = output;
    template_path += ".map.txt";
    std::ofstream list(template_path);
    list << "# OpenBMH MIDI map for song " << song << ": edit and pass with --map FILE\n"
         << "# inst <instrument> <General MIDI program 0..127>, sample <slot> <drum note>\n";
    for (unsigned i = 0; i < 256; ++i) if (used_instrument[i]) {
        const auto& u = usage[i];
        list << "inst " << i << ' ' << map.program[i] << "    # " << u.notes << " notes, range " << u.low << '-'
             << u.high << ", chords " << (u.notes ? 100 * u.chord / u.notes : 0) << "%\n";
    }
    for (unsigned i = 0; i < 256; ++i) if (used_sample[i]) list << "sample " << i << ' ' << map.drum[i] << '\n';
    std::size_t notes = 0;
    for (const auto& t : tracks) notes += t.size();
    std::cout << "exported Original song=" << song << " midi notes=" << notes << " tempo_us=" << usec
              << " loops=" << completed_loops << " map=" << template_path.string() << '\n';
    return 0;
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
    if (action == "--export-midi") {
        unsigned song = 0, loops = 1;
        std::filesystem::path map;
        if (argc < 6 || !bounded_number(argv[4], 0, 2, song)) return usage();
        for (int at = 6; at < argc; at += 2) {
            if (at + 1 >= argc) return usage();
            const std::string option = argv[at];
            if (option == "--loops") {
                if (!bounded_number(argv[at + 1], 1, 16, loops)) return usage();
            } else if (option == "--map") map = argv[at + 1];
            else return usage();
        }
        try {
            return export_midi(load_original_assets(argv[2]), song, argv[5], loops, map);
        } catch (const std::exception& error) {
            std::cerr << "Original audio input: " << error.what() << '\n'; return 1;
        }
    }
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
