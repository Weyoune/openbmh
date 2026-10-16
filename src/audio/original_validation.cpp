// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "original.hpp"
#include "playback.hpp"
#include "container.hpp"
#include "formats/le.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace openbmh::audio;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
void require(bool value, const std::string& reason) {
    if (!value) throw std::runtime_error(reason);
}
OriginalAssets assets_from(const std::filesystem::path& data) {
    OriginalAssets assets;
    const auto exe = bmh::load_le(data / "BMH.exe");
    for (unsigned i = 0; i < 4; ++i)
        assets.containers[i] = bmh::read_file(data / "S" / std::to_string(2000 + i));
    for (unsigned i = 0; i < assets.tables.frequencies.size(); ++i)
        assets.tables.frequencies[i] = exe.u16(::bmh::layout::Audio_OriginalAudioAssets_RuntimeField2 + 2 * i);
    for (unsigned i = 0; i < assets.tables.operators.size(); ++i)
        assets.tables.operators[i] = exe.u8(::bmh::layout::Audio_OriginalAudioAssets_RuntimeField + 16 * i);
    return assets;
}
void load(OriginalSynth& synth, const OriginalAssets& assets) {
    std::string error;
    require(synth.load(assets, 22050, error), "load external Original audio: " + error);
}
struct TraceHash {
    std::uint64_t count{}, hash = kFnvOffset;
    void word(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8)
            hash = (hash ^ ((value >> shift) & 255u)) * 1099511628211ull;
    }
    void sample(std::uint32_t tick, std::uint8_t slot) { ++count; word(tick); word(slot); }
    void reg(std::uint32_t tick, std::uint8_t index, std::uint8_t value) {
        ++count; word(tick); word(index); word(value);
    }
};
struct Oracle {
    std::uint8_t id;
    unsigned ticks;
    std::uint64_t writes, register_hash, samples, sample_hash;
};
// Counts and FNV-1a hashes from the executed, unmodified Original x86 driver.
// Canonical hash input: each observed column as little-endian u32, in order.
// No Original audio bytes or register/sample sequences are embedded here.
constexpr std::array<Oracle, 16> kOracle{{
    {0,60000,32348,0xcbca207bfd99aeb0ull,3266,0xadad438156972aa9ull},
    {1,70000,12223,0xa0fe3a8b449c366bull,3163,0x31cd0db01f7a3ed2ull},
    {2,2000,386,0x52ce4acd30052d42ull,21,0xed4a856bd0b5d9bbull},
    {10,100,15,0xa38ea2831340e69eull,0,kFnvOffset},
    {11,100,15,0x6842cc45dcb290c5ull,0,kFnvOffset},
    {12,100,15,0x2ad007b26f4116b8ull,0,kFnvOffset},
    {13,100,15,0xaa5fff83f44303f5ull,0,kFnvOffset},
    {14,100,15,0xea8efbf8811746beull,0,kFnvOffset},
    {15,100,15,0x89a307eb865aa83full,0,kFnvOffset},
    {16,100,15,0x23a80f34bb743d13ull,0,kFnvOffset},
    {17,100,0,kFnvOffset,1,0xa91d37acc4be70b5ull},
    {18,100,0,kFnvOffset,1,0x09228ba46e88b424ull},
    {19,100,0,kFnvOffset,1,0xe9128fbd7129e9d7ull},
    {20,100,0,kFnvOffset,1,0x4917e3b51af42d46ull},
    {21,100,0,kFnvOffset,1,0x2932878b6be77e71ull},
    {22,100,0,kFnvOffset,1,0x8937db8315b1c1e0ull},
}};

void original_oracle(const OriginalAssets& assets) {
    for (const auto& expected : kOracle) {
        OriginalSynth synth; load(synth, assets);
        const bool song = expected.id < 3;
        if (song) require(synth.command(expected.id), "Original song command");
        TraceHash registers, samples;
        unsigned tick = 0, wraps = 0;
        auto previous_bar = synth.stats().bar;
        synth.set_register_observer([&](auto reg, auto value) { registers.reg(tick, reg, value); });
        synth.set_sample_observer([&](auto slot) { samples.sample(tick, slot); });
        if (!song) require(synth.effect(expected.id), "Original effect command");
        for (; tick < expected.ticks; ++tick) {
            synth.tick();
            const auto stats = synth.stats();
            if (stats.bar < previous_bar) ++wraps;
            previous_bar = stats.bar;
        }
        require(!synth.stats().failed && registers.count == expected.writes &&
                registers.hash == expected.register_hash && samples.count == expected.samples &&
                samples.hash == expected.sample_hash,
                "executed Original driver oracle mismatch for ID " + std::to_string(expected.id));
        if (expected.id < 2) require(wraps > 0, "looping Original song must traverse its repeat boundary");
        if (expected.id == 2) require(!synth.stats().playing, "finite intro must stop at its Original end");
        std::cout << (song ? "song " : "effect ") << unsigned(expected.id)
                  << " ticks=" << expected.ticks << " registers=" << registers.count
                  << " fnv=" << std::hex << registers.hash << std::dec
                  << " samples=" << samples.count << " fnv=" << std::hex << samples.hash << std::dec << '\n';
    }
}

void transition_oracle(const OriginalAssets& assets) {
    // Executed Original x86: warm up 1000 IRQs, start direct effect16, switch
    // through legacy routine/legacy routine (different song), or legacy routine/721d9/72199
    // (same song); observe the following5000 IRQs. Only digests are retained.
    constexpr std::array<std::uint64_t, 9> writes{2469,364,386,1922,446,386,1922,364,0};
    constexpr std::array<std::uint64_t, 9> hashes{
        0xe0e4d6a8ce302b09ull,0xa7c86105f1e09e74ull,0x52ce4acd30052d42ull,
        0x2157ea8cf3b1f61eull,0x4d37e6aa0de33d57ull,0x52ce4acd30052d42ull,
        0x22ad1211c726b767ull,0x5ebdbbd8e9af9298ull,kFnvOffset};
    constexpr std::array<std::uint64_t, 9> starts{276,218,21,277,231,21,277,218,0};
    constexpr std::array<std::uint64_t, 9> sample_hashes{
        0x781b9195a1b85333ull,0x24d2a97d1a8511f9ull,0xed4a856bd0b5d9bbull,
        0x23b9f58978f6f974ull,0x7d674e527408da4dull,0xed4a856bd0b5d9bbull,
        0x23b9f58978f6f974ull,0x24d2a97d1a8511f9ull,kFnvOffset};
    for (unsigned previous = 0; previous < 3; ++previous) for (unsigned target = 0; target < 3; ++target) {
        OriginalSynth synth; load(synth, assets);
        require(synth.command(std::uint8_t(previous)), "transition warmup song");
        for (unsigned n = 0; n < 1000; ++n) synth.tick();
        require(synth.effect(16) && synth.command(std::uint8_t(target)), "transition effect and target");
        TraceHash registers, samples; unsigned tick = 0;
        synth.set_register_observer([&](auto reg, auto value) { registers.reg(tick, reg, value); });
        synth.set_sample_observer([&](auto slot) { samples.sample(tick, slot); });
        for (; tick < 5000; ++tick) synth.tick();
        const auto index = previous * 3 + target;
        require(!synth.stats().failed && registers.count == writes[index] && registers.hash == hashes[index] &&
                samples.count == starts[index] && samples.hash == sample_hashes[index],
                "Original song transition mismatch " + std::to_string(previous) + "->" + std::to_string(target));
    }
    std::cout << "transitions: all9 cross/same-song executed Original digests, including finite intro resume\n";
}

std::size_t payload(const OriginalAssets& assets, unsigned bank, const char* name) {
    Container container; std::string error;
    require(parse_container(assets.containers[bank], container, error), "parse external chunk: " + error);
    for (const auto& chunk : container.chunks) if (chunk.id == name) return chunk.payload_offset;
    throw std::runtime_error("missing external chunk");
}
void put16(std::vector<std::uint8_t>& bytes, std::size_t at, unsigned value) {
    bytes.at(at) = std::uint8_t(value); bytes.at(at + 1) = std::uint8_t(value >> 8);
}
void malformed_inputs(const OriginalAssets& assets) {
    unsigned cases = 0;
    const auto rejected = [&](const std::function<void(OriginalAssets&)>& mutation) {
        auto malformed = assets; mutation(malformed);
        OriginalSynth synth; std::string error;
        require(!synth.load(std::move(malformed), 22050, error) && !error.empty() && !synth.ready(),
                "malformed Original asset must fail before callback exposure: case " + std::to_string(cases));
        ++cases;
    };
    rejected([](auto& a) { a.containers[0].clear(); });
    rejected([](auto& a) { a.containers[1].pop_back(); });
    rejected([](auto& a) { a.tables.operators[0] = 255; });
    rejected([](auto& a) { a.tables.operators[1] = a.tables.operators[0]; });
    rejected([](auto& a) { a.tables.frequencies[0] = 0; });
    rejected([](auto& a) { a.tables.frequencies[1] &= ~0x2000u; });
    const auto song = payload(assets, 0, "SONG"), track = payload(assets, 0, "TRCK");
    const auto part = payload(assets, 0, "PART"), sample = payload(assets, 3, "SAMP");
    const auto effect = payload(assets, 3, "SEFX");
    rejected([&](auto& a) { put16(a.containers[0], song + 4, 0); });
    rejected([&](auto& a) { put16(a.containers[0], song + 8, 0xffff); });
    rejected([&](auto& a) { std::fill_n(a.containers[0].begin() + std::ptrdiff_t(track + 4), 4, 255); });
    rejected([&](auto& a) { std::fill_n(a.containers[0].begin() + std::ptrdiff_t(part + 4), 4, 255); });
    rejected([&](auto& a) {
        a.containers[3].at(sample + 4 + 10) = 1;
        put16(a.containers[3], sample + 4 + 4, 0xffff);
        put16(a.containers[3], sample + 4 + 6, 0xffff);
    });
    rejected([&](auto& a) { a.containers[3].at(effect + 4 + 2) = 255; });
    rejected([&](auto& a) { a.containers[3].at(effect + 4 + 1) = 10; });
    OriginalSynth synth; std::string error;
    require(!synth.load(assets, 7999, error) && !synth.load(assets, 192001, error), "output rate bounds");
    load(synth, assets);
    require(synth.command(1), "valid song after malformed cases");
    const auto before = synth.stats();
    auto malformed = assets; malformed.containers[0].clear();
    require(!synth.load(std::move(malformed), 22050, error) && synth.ready() &&
            synth.stats().register_writes == before.register_writes && synth.stats().song == before.song,
            "failed reload must retain the previous validated synthesizer");
    std::cout << "malformed: " << cases << " asset/table/span/loop cases plus rate and failed-reload bounds\n";
}

bool nonzero(std::span<const std::int16_t> samples) {
    return std::any_of(samples.begin(), samples.end(), [](auto value) { return value != 0; });
}
void render_and_options(const OriginalAssets& assets) {
    for (unsigned id = 0; id <= 22; ++id) {
        if (id > 2 && id < 10) continue;
        OriginalSynth synth; load(synth, assets);
        require(id < 3 ? synth.command(std::uint8_t(id)) : synth.effect(std::uint8_t(id)), "render command");
        std::array<std::int16_t, 8192> pcm{}; synth.render(pcm);
        require(!synth.stats().failed && synth.stats().frames == pcm.size() && nonzero(pcm),
                "all Original songs/effects must produce bounded audible PCM: " + std::to_string(id));
    }
    OriginalSynth first, second, silent;
    load(first, assets); load(second, assets); load(silent, assets);
    require(first.command(1) && second.command(1) && silent.command(1), "streaming song setup");
    std::array<std::int16_t, 16384> a{}, b{}, zero{};
    first.render(a);
    for (std::size_t at = 0; at < b.size(); at += 97)
        second.render(std::span(b).subspan(at, std::min<std::size_t>(97, b.size() - at)));
    silent.render(zero, 0);
    require(a == b && nonzero(a) && !nonzero(zero) &&
            silent.stats().ticks == first.stats().ticks && silent.stats().frames == a.size(),
            "PCM continuation is callback-block independent; zero volume preserves sequencing");
    require(!first.command(255) && !first.effect(9) && !first.effect(23), "invalid command/effect bounds");
    require(first.command(0) && !first.command(255), "title suppression cannot accept invalid IDs");
    auto writes = first.stats().register_writes;
    require(first.command(16) && first.stats().register_writes == writes, "Source title suppresses game effects");
    first.stop();
    first.set_options({false, true, false});
    require(first.command(1) && !first.stats().playing, "music disabled independently");
    writes = first.stats().register_writes;
    require(!first.effect(10) && first.stats().register_writes == writes && first.effect(16),
            "click switch disables click ID10 while retaining non-click ID16");
    first.set_options({false, false, false});
    require(!first.effect(17), "effect option independently disabled");
    first.set_options({false, true, true});
    require(first.command(3) && first.stats().ambient, "Original crowd command starts loop");
    const auto starts = first.stats().sample_starts;
    first.set_options({true, true, true}); first.set_options({false, true, true});
    require(first.stats().ambient && first.stats().sample_starts == starts,
            "music option toggles cannot erase the Original crowd");
    first.render(a); require(nonzero(a) && first.stats().ambient, "bounded crowd render continues");
    first.stop(); require(!first.stats().playing && !first.stats().ambient, "stop clears all playback owners");
    std::cout << "render: 3 songs/13 effects, chunk continuity, volume0, switches, title gate and crowd loop\n";
}

void pause_and_crowd_transitions(const OriginalAssets& assets) {
    OriginalSynth reference, paused; load(reference, assets); load(paused, assets);
    require(reference.command(1) && paused.command(1), "pause/resume desk setup");
    for (unsigned n = 0; n < 233; ++n) { reference.tick(); paused.tick(); }
    const auto cursor = paused.stats();
    paused.set_options({false, true, true});
    require(!paused.stats().playing, "music option pauses current song");
    for (unsigned n = 0; n < 71; ++n) paused.tick();
    require(paused.stats().bar == cursor.bar && paused.stats().step == cursor.step,
            "disabled music retains its bar/step cursor");
    paused.set_options({true, true, true});
    require(paused.stats().playing, "music option resumes current song");
    for (unsigned n = 0; n < 257; ++n) { reference.tick(); paused.tick(); }
    require(paused.stats().bar == reference.stats().bar && paused.stats().step == reference.stats().step,
            "music option resumes retained tempo phase and cursors");

    OriginalSynth desk, crowd; load(desk, assets); load(crowd, assets);
    require(desk.command(1) && crowd.command(1), "crowd/desk setup");
    for (unsigned n = 0; n < 233; ++n) { desk.tick(); crowd.tick(); }
    require(crowd.command(3) && crowd.stats().ambient && !crowd.stats().playing, "crowd pauses desk");
    const auto before = crowd.stats();
    require(crowd.command(3) && crowd.stats().sample_starts == before.sample_starts,
            "same crowd command retains existing owner without restarting sample");
    for (unsigned n = 0; n < 71; ++n) crowd.tick();
    require(crowd.stats().bar == before.bar && crowd.stats().step == before.step,
            "crowd retains the paused song cursor");
    require(crowd.command(1) && !crowd.stats().ambient && crowd.stats().playing,
            "return to same desk song stops crowd and resumes music");
    for (unsigned n = 0; n < 257; ++n) { desk.tick(); crowd.tick(); }
    require(crowd.stats().bar == desk.stats().bar && crowd.stats().step == desk.stats().step &&
            crowd.stats().song == 1 && !crowd.stats().failed,
            "crowd to desk retains song/tempo cursor rather than restarting");
    std::cout << "pause/crowd: music off/on and crowd-to-desk retain song and tempo cursors\n";
}

void effect_priorities(const OriginalAssets& assets) {
    const auto at = payload(assets, 3, "SEFX") + 4;
    const auto& bytes = assets.containers[3];
    unsigned pairs = 0;
    for (unsigned high = 0; high < 13; ++high) for (unsigned low = 0; low < 13; ++low) {
        const auto h = at + high * 16, l = at + low * 16;
        if (bytes[h + 1] != bytes[l + 1] || bytes[h + 3] <= bytes[l + 3] ||
            (bytes[h + 1] != 9 && bytes[h + 5] == 0)) continue;
        OriginalSynth synth; load(synth, assets);
        require(synth.effect(std::uint8_t(high + 10)), "start higher-priority Original effect");
        const auto before = synth.stats();
        require(synth.effect(std::uint8_t(low + 10)) &&
                synth.stats().register_writes == before.register_writes &&
                synth.stats().sample_starts == before.sample_starts,
                "lower-priority effect must not interrupt the active Original channel");
        OriginalSynth replacement; load(replacement, assets);
        require(replacement.effect(std::uint8_t(low + 10)), "start lower-priority Original effect");
        const auto old = replacement.stats();
        require(replacement.effect(std::uint8_t(high + 10)) &&
                (replacement.stats().register_writes > old.register_writes ||
                 replacement.stats().sample_starts > old.sample_starts),
                "higher-priority effect must replace the active lower-priority channel");
        ++pairs;
    }
    require(pairs != 0, "external bank must exercise actual priority pairs");
    std::cout << "priority: " << pairs << " actual competing high/low pairs retained correctly\n";
}

void callback_and_lifecycle(const OriginalAssets& assets) {
    std::string error;
    {
        Playback playback;
        require(playback.initialize_original(assets, error), "SDL Original initialization: " + error);
        require(playback.available() && playback.original_ready() && !playback.needs_pump(), "callback mode owns audio");
        playback.reset_session(42);
        require(playback.play_original_id(1, error), "callback desk song");
        const auto initial = playback.original_stats();
        SDL_Delay(120); // Deliberately no pump, render, GUI step or simulation callback.
        const auto advanced = playback.original_stats();
        require(advanced.frames > initial.frames + 512 && advanced.ticks > initial.ticks &&
                advanced.playing && !advanced.failed && playback.queued_bytes() == 0,
                "SDL callback must continue throughout a blocked main thread");
        playback.set_volume(0);
        const auto quiet = playback.original_stats().frames; SDL_Delay(40);
        require(playback.original_stats().frames > quiet && playback.volume() == 0, "zero volume retains callback clock");
        playback.set_volume(1);
        playback.set_original_options({false, false, false});
        require(!playback.original_stats().playing, "saved music option reaches callback");
        const auto stopped = playback.original_stats().sample_starts;
        require(playback.play_original_id(17, error) && playback.original_stats().sample_starts == stopped,
                "disabled effects are a normal consumed refusal");
        playback.set_original_options({true, true, true});
        playback.set_muted(true);
        const auto muted = playback.original_stats().frames; SDL_Delay(40);
        require(playback.original_stats().frames == muted && !playback.original_stats().playing,
                "mute stops and freezes Original sequencing");
        require(playback.submit(CueRequest{42, 1, 17}, error) == CueStatus::muted, "muted event consumed");
        playback.set_muted(false);
        require(playback.submit(CueRequest{42, 1, 17}, error) == CueStatus::duplicate,
                "unmute cannot replay a consumed event");
        require(playback.submit(CueRequest{41, 2, 17}, error) == CueStatus::invalid, "stale session refused");
        require(playback.submit(CueRequest{42, 2, 17}, error) == CueStatus::queued, "real effect submitted");
        require(playback.submit(CueRequest{42, 3, 255}, error) == CueStatus::unsupported &&
                playback.submit(CueRequest{42, 3, 255}, error) == CueStatus::duplicate,
                "invalid command is rejected and its envelope remains consumed");
        playback.stop(); playback.reset_session(43);
        require(playback.submit(CueRequest{43, 1, 16}, error) == CueStatus::queued, "new session starts independent event sequence");
        playback.shutdown();
        require(!playback.available() && playback.submit(CueRequest{43, 2, 16}, error) == CueStatus::disabled,
                "shutdown closes device and consumes no-device events safely");
        require(playback.initialize_original(assets, error), "reinitialize after shutdown: " + error);
        require(playback.play_original_id(2, error), "reinitialized finite song");
    }
    const char* old = SDL_getenv("SDL_AUDIODRIVER");
    const std::string previous = old ? old : "dummy";
    SDL_setenv("SDL_AUDIODRIVER", "openbmh_intentionally_missing_driver", 1);
    Playback unavailable;
    const bool initialized = unavailable.initialize_original(assets, error);
    SDL_setenv("SDL_AUDIODRIVER", previous.c_str(), 1);
    require(!initialized && !unavailable.available() && !error.empty(), "unavailable device fails without crashing");
    require(unavailable.initialize_original(assets, error), "retry after no-device failure: " + error);
    unavailable.shutdown();
    std::cout << "callback: blocked-main continuity, volume0/mute/options, dedup, no-device, reinitialize/lifecycle\n";
}
}

int main(int argc, char** argv) try {
    if (argc != 2) return 2;
    if (!*argv[1]) {
        std::cout << "SKIP: external game audio not configured\n";
        return 77;
    }
    if (!*argv[1]) { std::cout << "SKIP: Original BMH_DATA not configured\n"; return 77; }
    const auto assets = assets_from(argv[1]);
    original_oracle(assets);
    transition_oracle(assets);
    malformed_inputs(assets);
    render_and_options(assets);
    pause_and_crowd_transitions(assets);
    effect_priorities(assets);
    callback_and_lifecycle(assets);
    return 0;
} catch (const std::exception& error) {
    std::cerr << "Original audio validation: " << error.what() << '\n';
    return 1;
}
