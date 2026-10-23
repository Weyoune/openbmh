// SPDX-License-Identifier: GPL-3.0-only
#include "original.hpp"
#include "container.hpp"
#include "third_party/nuked-opl3/opl3.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace openbmh::audio {
namespace {
using Bytes = std::span<const std::uint8_t>;
std::uint16_t word(Bytes b, std::size_t at) {
    if (at > b.size() || b.size() - at < 2) throw std::runtime_error("truncated audio word");
    return std::uint16_t(b[at] | (unsigned(b[at + 1]) << 8));
}
std::uint32_t dword(Bytes b, std::size_t at) {
    return std::uint32_t(word(b, at)) | (std::uint32_t(word(b, at + 2)) << 16);
}
Bytes chunk(Bytes bytes, const Container& c, const char* name) {
    const Chunk* found = nullptr;
    for (const auto& p : c.chunks) if (p.id == name) {
        if (found) throw std::runtime_error("duplicate Original audio chunk");
        found = &p;
    }
    if (!found) throw std::runtime_error(std::string("missing Original audio chunk ") + name);
    return bytes.subspan(found->payload_offset, found->payload_size);
}
bool room(Bytes b, std::size_t at, std::size_t n) noexcept {
    return at <= b.size() && n <= b.size() - at;
}
}

struct OriginalSynth::State {
    struct Sample { Bytes data, loop; std::uint8_t time_constant{}; };
    struct Bank {
        std::vector<std::array<std::uint8_t, 32>> instruments;
        std::vector<Sample> samples;
    };
    struct Song : Bank {
        Bytes tracks, parts;
        std::array<std::size_t, 16> start{}, loop{};
        std::vector<std::size_t> patterns;
        std::uint16_t tempo{}, end{}, repeat{};
    };
    struct Track {
        std::size_t next{}, loop{}, cursor{};
        std::uint8_t tick{}, instrument{}, volume{}, transpose{}, first{}, end{}, channel{};
        bool active{};
    };
    struct Channel {
        std::uint8_t duration{}, instrument{255}, level{255}, high{}, reserved{}, release{255}, priority{};
        std::uint8_t modulator{}, carrier{};
    };
    OriginalAssets assets;
    std::array<Song, 3> songs;
    Bank effects;
    std::array<std::array<std::uint8_t, 16>, 13> sfx{};
    std::array<Track, 16> tracks{};
    std::array<Channel, 9> channels{};
    opl3_chip chip{};
    OriginalAudioOptions options;
    OriginalAudioStats stats;
    std::uint32_t rate{};
    std::uint64_t pit_phase{}, dma_phase{};
    Bytes dma;
    std::size_t dma_cursor{};
    std::uint8_t dma_constant{}, dma_priority{}, dma_slot{}, ambient_slot{255};
    bool dma_loop{}, dma_effect{}, dry{};
    unsigned tempo_tick{};
    std::function<void(std::uint8_t, std::uint8_t)> register_observer;
    std::function<void(std::uint8_t)> sample_observer;
    std::function<void(const OriginalNoteEvent&)> note_observer;

    void observe_note(unsigned track, std::uint8_t channel, std::uint8_t instrument, std::uint8_t pitch,
                      std::uint8_t duration, std::uint8_t volume, bool sample) {
        if (dry || !note_observer) return;
        note_observer(OriginalNoteEvent{stats.bar, stats.step, std::uint8_t(track), channel, instrument, pitch,
                                        duration, volume, sample});
    }
    void write(std::uint8_t reg, std::uint8_t value) noexcept {
        if (dry) return;
        ++stats.register_writes;
        OPL3_WriteRegBuffered(&chip, reg, value);
        if (register_observer) register_observer(reg, value);
    }
    void reset_chip() noexcept {
        OPL3_Reset(&chip, rate);
        for (const auto [r, v] : std::array<std::pair<std::uint8_t, std::uint8_t>, 7>{{
                 {1, 0x20}, {8, 0}, {2, 0}, {3, 0}, {4, 0x60}, {4, 0x80}, {0xbd, 0}}}) write(r, v);
    }
    static Bank read_bank(Bytes raw, const Container& container) {
        Bank bank;
        const auto inst = chunk(raw, container, "INST");
        const auto count = word(inst, 2);
        if (word(inst, 0) != 0 || count > 128 || inst.size() != 4u + 32u * count)
            throw std::runtime_error("invalid Original INST table");
        bank.instruments.resize(count);
        for (unsigned i = 0; i < count; ++i)
            std::copy_n(inst.begin() + 4 + i * 32, 32, bank.instruments[i].begin());
        const auto samp = chunk(raw, container, "SAMP");
        const auto n = word(samp, 2);
        if (word(samp, 0) != 0 || n > 16 || !room(samp, 4, std::size_t(n) * 16))
            throw std::runtime_error("invalid Original SAMP table");
        const auto data = samp.subspan(4 + n * 16);
        for (unsigned i = 0; i < n; ++i) {
            const auto desc = samp.subspan(4 + i * 16, 16);
            const auto begin = word(desc, 0), length = word(desc, 2);
            if (!length || !room(data, begin, length)) throw std::runtime_error("invalid Original sample span");
            Sample s{data.subspan(begin, length), {}, desc[11]};
            if (desc[10]) {
                const auto start = word(desc, 4), size = word(desc, 6);
                if (!size || !room(data, start, size)) throw std::runtime_error("invalid Original sample loop");
                s.loop = data.subspan(start, size);
            }
            bank.samples.push_back(s);
        }
        return bank;
    }
    bool parse(std::string& error) {
        for (unsigned i = 0; i < 4; ++i) {
            const Bytes raw = assets.containers[i];
            Container container;
            if (!parse_container(raw, container, error)) return false;
            const auto header = chunk(raw, container, "HEDR");
            if (header.size() != 8 || word(header, 0) != 1 || word(header, 2) != 0 ||
                word(header, 4) != 1 || word(header, 6) != 1)
                throw std::runtime_error("unsupported Original sound driver variant");
            auto bank = read_bank(raw, container);
            if (i == 3) {
                effects = std::move(bank);
                const auto fx = chunk(raw, container, "SEFX");
                if (fx.size() != 4 + sfx.size() * 16) throw std::runtime_error("invalid Original SEFX table");
                for (unsigned n = 0; n < sfx.size(); ++n) {
                    std::copy_n(fx.begin() + 4 + n * 16, 16, sfx[n].begin());
                    const auto& f = sfx[n];
                    if (f[1] == 9) {
                        if (f[0] < 16 || f[0] - 16 >= effects.samples.size())
                            throw std::runtime_error("SEFX sample outside bank");
                    } else if (f[1] >= 9 || !(f[0] & 128) || (f[0] & 127) >= effects.instruments.size() ||
                               f[2] >= assets.tables.frequencies.size() || f[4] > 63)
                        throw std::runtime_error("SEFX instrument, channel, note or level outside bank");
                }
                continue;
            }
            Song& song = songs[i];
            static_cast<Bank&>(song) = std::move(bank);
            const auto info = chunk(raw, container, "SONG");
            if (info.size() != 10 || word(info, 0) != 0) throw std::runtime_error("invalid Original SONG header");
            song.tempo = word(info, 4); song.end = word(info, 6); song.repeat = word(info, 8);
            if (!song.tempo || song.tempo > 1024 || !song.end || song.end > 8192 || song.repeat > song.end)
                throw std::runtime_error("invalid Original song timing");
            const auto tr = chunk(raw, container, "TRCK");
            if (word(tr, 0) != 0 || word(tr, 2) != 16 || tr.size() < 132)
                throw std::runtime_error("invalid Original TRCK table");
            song.tracks = tr.subspan(132);
            for (unsigned t = 0; t < 16; ++t) {
                song.start[t] = dword(tr, 4 + 4 * t); song.loop[t] = dword(tr, 68 + 4 * t);
                if (!room(song.tracks, song.start[t], 8) || !room(song.tracks, song.loop[t], 8))
                    throw std::runtime_error("Original track pointer outside table");
            }
            const auto part = chunk(raw, container, "PART");
            const auto patterns = word(part, 2);
            if (word(part, 0) != 0 || !patterns || patterns > 256 || !room(part, 4, patterns * 4))
                throw std::runtime_error("invalid Original PART table");
            song.parts = part.subspan(4 + patterns * 4);
            for (unsigned p = 0; p < patterns; ++p) {
                const auto start = dword(part, 4 + p * 4);
                if (start >= song.parts.size()) throw std::runtime_error("Original pattern pointer outside table");
                song.patterns.push_back(start);
            }
        }
        return true;
    }
    void fail() noexcept { stats.failed = true; stats.playing = false; dma = {}; }
    void level(unsigned c, std::uint8_t value) noexcept {
        if (c >= 9 || value > 63) { fail(); return; }
        auto& ch = channels[c];
        if (ch.level == value) return;
        ch.level = value;
        // Uint8T5's 64x64 table, generated there at runtime.
        const auto scale = [value](std::uint8_t tl) {
            return std::uint8_t((tl & 0xc0) | (63 - (unsigned(value) * (64 - (tl & 63)) >> 6)));
        };
        write(assets.tables.operators[c] + 0x20, scale(ch.modulator));
        write(assets.tables.operators[c] + 0x23, scale(ch.carrier));
    }
    void instrument(unsigned c, std::uint8_t id) noexcept {
        if (c >= 9) { fail(); return; }
        const Bank& bank = id & 128 ? effects : songs[stats.song];
        if ((id & 127) >= bank.instruments.size()) { fail(); return; }
        auto& ch = channels[c]; ch.instrument = id;
        const auto& in = bank.instruments[id & 127];
        ch.modulator = in[2]; ch.carrier = in[3]; ch.level = 255;
        const unsigned op = assets.tables.operators[c];
        write(op, in[0]); write(op + 3, in[1]);
        write(op + 0x40, in[4]); write(op + 0x43, in[5]);
        write(op + 0x60, in[6]); write(op + 0x63, in[7]);
        write(op + 0xc0, in[8]); write(op + 0xc3, in[9]);
        write(0xc0 + c, in[10]);
    }
    void note(unsigned c, unsigned pitch, std::uint8_t volume) noexcept {
        if (c >= 9 || pitch >= assets.tables.frequencies.size()) { fail(); return; }
        write(0xb0 + c, 0);
        level(c, volume);
        const auto frequency = assets.tables.frequencies[pitch];
        channels[c].high = std::uint8_t(frequency >> 8);
        write(0xa0 + c, std::uint8_t(frequency)); write(0xb0 + c, std::uint8_t(frequency >> 8));
    }
    void start_sample(std::uint8_t slot, bool is_effect, std::uint8_t priority = 0) noexcept {
        const Bank* bank = nullptr;
        unsigned index = slot;
        if (slot >= 16) { bank = &effects; index -= 16; }
        else if (stats.song < 3) bank = &songs[stats.song];
        if (!bank || index >= bank->samples.size()) { fail(); return; }
        const auto& s = bank->samples[index];
        dma = s.loop.empty() ? s.data : s.loop;
        dma_loop = !s.loop.empty(); dma_cursor = 0; dma_phase = 0;
        dma_constant = s.time_constant; dma_effect = is_effect; dma_priority = priority; dma_slot = slot;
        if (dma_loop && is_effect) ambient_slot = slot;
        if (!dry) { ++stats.sample_starts; if (sample_observer) sample_observer(slot); }
    }
    void stop_effects() noexcept {
        for (unsigned c = 0; c < 9; ++c) if (channels[c].reserved) {
            level(c, 0); channels[c].reserved = 0;
        }
        dma = {}; dma_effect = false; ambient_slot = 255; stats.ambient = false;
    }
    void pause_music() noexcept {
        for (unsigned c = 0; c < 9; ++c) level(c, 0);
        stats.playing = false;
        dma = {};
    }
    void start_song(unsigned i, bool restart = false) noexcept {
        if (i >= 3) return;
        if (stats.song == i && !restart) { stats.playing = options.music; return; }
        pause_music(); stop_effects();
        // Effect resets only OriginalRuntimeField(duration)=0, a5540(instrument)=FF,
        // a5544(level)=FF and a5546(reserved)=0 at stride16. The cached high
        // frequency byte, release/priority and instrument levels survive.
        // Clearing high changes the first pending key-off after a song switch.
        for (auto& c : channels) {
            c.duration = 0; c.instrument = 255; c.level = 255; c.reserved = 0;
        }
        stats.song = i; stats.bar = 0; stats.step = 0; tempo_tick = 0;
        for (unsigned t = 0; t < 16; ++t) {
            tracks[t].active = false; tracks[t].next = songs[i].start[t]; tracks[t].loop = songs[i].loop[t];
        }
        stats.playing = options.music;
    }
    bool effect(std::uint8_t id) noexcept {
        if (id < 10 || id > 22 || !options.effects || (id <= 15 && !options.clicks)) return false;
        const auto& f = sfx[id - 10];
        if (f[1] == 9) {
            if (dma_effect && !dma.empty() && f[3] < dma_priority) return true;
            start_sample(f[0], true, f[3]);
        } else {
            auto& c = channels[f[1]];
            if (c.reserved && f[3] < c.priority) return true;
            c.reserved = f[5]; c.release = f[6]; c.priority = f[3];
            instrument(f[1], f[0]); note(f[1], f[2], f[4]);
        }
        return !stats.failed;
    }
    std::uint8_t song_level(std::uint8_t value) noexcept {
        // Source OriginalRuntimeField2=0x3f80 selects master127 in the 128x128 table.
        if (value > 127) { fail(); return 0; }
        return std::uint8_t((127u * (unsigned(value) + 1) >> 7) >> 1);
    }
    void music_step() noexcept {
        if (!stats.playing || stats.failed) return;
        const auto& song = songs[stats.song];
        if (stats.step == 0) {
            if (stats.bar == song.end) {
                if (song.end == song.repeat) { stats.playing = false; return; }
                for (auto& t : tracks) t.next = t.loop;
                stats.bar = song.repeat;
            }
            for (auto& t : tracks) {
                if (!room(song.tracks, t.next, 8)) { fail(); return; }
                const auto row = song.tracks.subspan(t.next, 8);
                const auto bar = unsigned(row[0]) | (unsigned(row[1]) << 8);
                if (bar != stats.bar) continue;
                t.active = true; t.tick = 0;
                if (row[2] != 255) t.instrument = row[2];
                if (row[3] != 255) t.volume = row[3];
                t.transpose = row[4]; t.first = row[6]; t.channel = t.first;
                t.end = std::uint8_t(t.first + row[7]);
                if (row[5] >= song.patterns.size() || (unsigned(&t - tracks.data()) != 9 &&
                    (t.first >= 9 || t.end > 9 || t.end <= t.first))) { fail(); return; }
                t.cursor = song.patterns[row[5]]; t.next += 8;
            }
        }
        for (unsigned index = 0; index < tracks.size(); ++index) {
            auto& t = tracks[index];
            if (!t.active) continue;
            if (!room(song.parts, t.cursor, 1)) { fail(); return; }
            const auto time = song.parts[t.cursor];
            if (time == 255) { t.active = false; continue; }
            if (time == t.tick) {
                if (!room(song.parts, t.cursor, 2)) { fail(); return; }
                auto cursor = t.cursor + 2;
                const auto code = song.parts[t.cursor + 1];
                unsigned count = 1;
                if (code >= 128) {
                    if (code & 0x40) {
                        if (!room(song.parts, cursor, 1)) { fail(); return; }
                        t.instrument = song.parts[cursor++];
                    }
                    if (code & 0x20) {
                        if (!room(song.parts, cursor, 1)) { fail(); return; }
                        t.volume = song.parts[cursor++];
                        const auto value = song_level(t.volume);
                        for (unsigned c = t.first; c < t.end; ++c) level(c, value);
                    }
                    count = code & 7;
                }
                for (unsigned n = 0; n < count; ++n) {
                    if (!room(song.parts, cursor, code < 128 ? 1 : 2)) { fail(); return; }
                    const auto pitch = code < 128 ? code : song.parts[cursor++];
                    const auto duration = song.parts[cursor++];
                    if (index == 9 && code < 128) {
                        if (!dma_effect) {
                            observe_note(index, 0, 0, pitch, 0, 0, true);
                            start_sample(pitch, false);
                        }
                    } else if (t.channel < 9) {
                        auto& c = channels[t.channel];
                        if (!c.reserved) {
                            if (c.instrument != t.instrument) instrument(t.channel, t.instrument);
                            c.duration = duration;
                            const auto played = std::uint8_t(pitch + t.transpose);
                            const auto volume = song_level(t.volume);
                            observe_note(index, t.channel, t.instrument, played, duration, volume, false);
                            note(t.channel, played, volume);
                        }
                        if (++t.channel == t.end) t.channel = t.first;
                    } else { fail(); return; }
                }
                t.cursor = cursor;
            }
            ++t.tick;
        }
        if (++stats.step == 16) { stats.step = 0; ++stats.bar; }
    }
    void tick() noexcept {
        ++stats.ticks;
        if (stats.failed) return;
        // IRQ0 calls Write2 before Write/Write3.
        for (unsigned n = 0; n < 9; ++n) {
            auto& c = channels[n];
            if (c.reserved) {
                --c.reserved;
                if (c.release != 255 && --c.release == 255) write(0xb0 + n, c.high & 31);
            }
        }
        if (!stats.playing) return;
        if (tempo_tick == 0) {
            music_step();
            for (unsigned n = 0; n < 9; ++n) {
                auto& c = channels[n];
                if (c.duration != 255 && --c.duration == 255) write(0xb0 + n, c.high & 31);
            }
        }
        if (++tempo_tick == songs[stats.song].tempo) tempo_tick = 0;
    }
};

OriginalSynth::OriginalSynth() = default;
OriginalSynth::~OriginalSynth() = default;
bool OriginalSynth::load(OriginalAssets assets, std::uint32_t rate, std::string& error) {
    error.clear();
    if (rate < 8000 || rate > 192000) { error = "invalid Original output rate"; return false; }
    std::array<bool, 256> used{};
    for (auto op : assets.tables.operators) {
        const bool valid = (op >= 0x20 && op <= 0x22) || (op >= 0x28 && op <= 0x2a) ||
                           (op >= 0x30 && op <= 0x32);
        if (!valid || used[op]) { error = "invalid Original OPL operator table"; return false; }
        used[op] = true;
    }
    for (auto frequency : assets.tables.frequencies)
        if (!(frequency & 0x2000) || (frequency & 0xc000) || !(frequency & 0x3ff)) {
            error = "invalid Original OPL note table"; return false;
        }
    try {
        auto candidate = std::make_unique<State>();
        candidate->assets = std::move(assets); candidate->rate = rate;
        if (!candidate->parse(error)) return false;
        // Interpret two full passes before making foreign data visible to the
        // real-time callback. All runtime table accesses remain checked as well.
        candidate->dry = true;
        for (unsigned i = 0; i < 3; ++i) {
            candidate->start_song(i, true);
            const auto steps = unsigned(candidate->songs[i].end) * 32 + 1;
            for (unsigned n = 0; n < steps && candidate->stats.playing; ++n) candidate->music_step();
            if (candidate->stats.failed) throw std::runtime_error("invalid Original track/pattern event");
        }
        candidate->dry = false; candidate->stop_effects(); candidate->stats = {};
        candidate->tracks = {}; candidate->channels = {}; candidate->tempo_tick = 0;
        candidate->reset_chip();
        state_ = std::move(candidate);
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}
bool OriginalSynth::ready() const noexcept { return state_ && !state_->stats.failed; }
void OriginalSynth::set_options(OriginalAudioOptions options) noexcept {
    if (!state_) return;
    auto& s = *state_;
    if (s.options.music && !options.music && s.stats.playing) s.pause_music();
    const bool resume = !s.options.music && options.music && s.stats.song < 3 && !s.stats.ambient;
    if (s.options.effects && !options.effects) s.stop_effects();
    s.options = options;
    if (resume) s.stats.playing = true;
}
bool OriginalSynth::command(std::uint8_t id) noexcept {
    if (!ready()) return false;
    auto& s = *state_;
    if (id < 3) {
        // PauseMusic -> PauseMusic2 pauses music/effects before either loading a
        // different song or PauseMusic3's same-song resume. Track cursors and
        // retained instrument/volume stay intact on the latter path.
        s.pause_music(); s.stop_effects(); s.start_song(id); return true;
    }
    if (id == 3) {
        if (s.stats.ambient) return true;
        s.pause_music(); s.stop_effects(); s.stats.ambient = true;
        if (s.options.effects) s.effect(22);
        return true;
    }
    if (id == 8) { stop(); return true; }
    if (id < 10 || id > 22) return false;
    // Source suppresses effects over the title song. The standalone direct
    // effect entry remains available for isolated decoding and verification.
    if (s.stats.song == 0) return true;
    return s.effect(id);
}
bool OriginalSynth::effect(std::uint8_t id) noexcept { return ready() && state_->effect(id); }
void OriginalSynth::stop() noexcept {
    if (!state_) return;
    state_->pause_music(); state_->stop_effects();
}
void OriginalSynth::tick() noexcept { if (state_) state_->tick(); }
void OriginalSynth::render(std::span<std::int16_t> output, float volume) noexcept {
    std::fill(output.begin(), output.end(), 0);
    if (!state_ || state_->stats.failed) return;
    auto& s = *state_;
    const auto gain = std::isfinite(volume) ? std::clamp(volume, 0.0F, 1.0F) : 0.0F;
    for (auto& value : output) {
        if (s.stats.failed) break;
        s.pit_phase += 1193180;
        const std::uint64_t period = std::uint64_t(s.rate) * 6554;
        if (s.pit_phase >= period) { s.pit_phase -= period; s.tick(); }
        std::int16_t pair[2]{};
        OPL3_GenerateResampled(&s.chip, pair);
        std::int32_t mixed = (std::int32_t(pair[0]) + pair[1]) / 2;
        if (!s.dma.empty()) {
            mixed += (int(s.dma[s.dma_cursor]) - 128) * 256;
            s.dma_phase += 1000000;
            const auto divisor = std::uint64_t(s.rate) * (256 - unsigned(s.dma_constant));
            while (s.dma_phase >= divisor && !s.dma.empty()) {
                s.dma_phase -= divisor;
                if (++s.dma_cursor == s.dma.size()) {
                    if (s.dma_loop) s.dma_cursor = 0;
                    else if (s.ambient_slot != 255) s.start_sample(s.ambient_slot, true, 64);
                    else { s.dma = {}; s.dma_effect = false; }
                }
            }
        }
        value = std::int16_t(std::clamp<long>(std::lround(mixed * gain), -32768, 32767));
        ++s.stats.frames;
    }
}
OriginalAudioStats OriginalSynth::stats() const noexcept { return state_ ? state_->stats : OriginalAudioStats{}; }
void OriginalSynth::set_register_observer(std::function<void(std::uint8_t, std::uint8_t)> f) {
    if (state_) state_->register_observer = std::move(f);
}
void OriginalSynth::set_sample_observer(std::function<void(std::uint8_t)> f) {
    if (state_) state_->sample_observer = std::move(f);
}
void OriginalSynth::set_note_observer(std::function<void(const OriginalNoteEvent&)> f) {
    if (state_) state_->note_observer = std::move(f);
}
bool OriginalSynth::song_timing(unsigned song, OriginalSongTiming& timing) const noexcept {
    if (!state_ || song >= state_->songs.size()) return false;
    const auto& s = state_->songs[song];
    timing = {s.tempo, s.end, s.repeat};
    return true;
}

} // namespace openbmh::audio
