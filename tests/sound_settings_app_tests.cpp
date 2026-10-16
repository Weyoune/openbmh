// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

#include "app/career_session.hpp"
#include "game/save.hpp"

using namespace bmh;
namespace {
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }

struct Snapshot {
    Bytes native;
    uint32_t game_seed, rand_seed;
    uint64_t game_calls, rand_calls, revision;
    Snapshot(app::CareerSession& session, uint64_t rev)
        : native(save_stream(session.state())), game_seed(session.game().seed()),
          rand_seed(session.rand().seed()), game_calls(session.game().calls()),
          rand_calls(session.rand().calls()), revision(rev) {}
    bool random_unchanged(app::CareerSession& session) const {
        return game_seed == session.game().seed() && rand_seed == session.rand().seed() &&
            game_calls == session.game().calls() && rand_calls == session.rand().calls();
    }
    bool unchanged(app::CareerSession& session, uint64_t rev) const {
        return native == save_stream(session.state()) && random_unchanged(session) && revision == rev;
    }
};

ui::SettingsWriteRequest write_for(const ui::SettingsState& state, uint8_t column) {
    return {state.manager_slot, state.revision, state.cells, 0, column, ui::SettingsEdit::toggle_binary};
}

void rejected(app::CareerSession& session, ui::SettingsWriteRequest request, uint64_t& revision) {
    const Snapshot before(session, revision);
    std::string error;
    check(!session.dispatch_settings(request, revision, error) && !error.empty(),
          "unavailable or stale sound action is rejected with a reason");
    check(before.unchanged(session, revision), "rejected sound action preserves Native, both RNGs and revision");
}

void toggle(app::CareerSession& session, const std::filesystem::path& data,
            uint8_t column, uint64_t& revision) {
    const auto state = session.settings_projection(int(session.state().memory.u8(::bmh::layout::ActiveManager)), revision);
    const Snapshot before(session, revision);
    ui::SettingsScreen screen(data, state);
    check(screen.click(70 + column * 24, 27) && screen.pending(), "real A6/A7 UI click creates a request");
    check(before.unchanged(session, revision), "projection, rendering and input do not write Native");
    auto expected = session.state();
    const uint32_t address = ::bmh::layout::LeaguePresentationOptionsUnsigned + column;
    expected.memory.set8(address, uint8_t((expected.memory.u8(address) & 0x7f) ^ 1));
    std::string error;
    check(session.dispatch_settings(*screen.pending(), revision, error), "supported sound UI request toggles");
    check(save_stream(session.state()) == save_stream(expected) && revision == before.revision + 1 &&
          before.random_unchanged(session), "deliberate sound toggle writes only the addressed Native byte and one revision");
    screen.acknowledge(session.settings_projection(state.manager_slot, revision));
    check(!screen.pending() && screen.state().cells[size_t(column)] == expected.memory.u8(address),
          "UI acknowledgement displays the actual committed sound value");
}
}

int main(int argc, char** argv) try {
    check(argc == 3, "usage: sound_settings_app_tests <data> <genuine two-manager FBM>");
    const std::filesystem::path data(argv[1]);
    app::CareerSession session(data, 8128, argv[2]);
    check(session.state().manager_teams == std::vector<int>({1, 54}), "unchanged genuine two-manager Native fixture");
    uint64_t revision = 17;
    const int manager = session.state().memory.u8(::bmh::layout::ActiveManager);
    const Snapshot native_input(session, revision);
    const auto headless = session.settings_projection(manager, revision);
    check(!headless.audio_config_known && headless.display_cells, "headless Session retains unknown capability state");
    for (uint8_t column : {uint8_t(5), uint8_t(6)}) {
        check(headless.cells[column] == session.state().memory.u8(::bmh::layout::LeaguePresentationOptionsUnsigned + column) &&
              (*headless.display_cells)[column] == headless.cells[column],
              "headless projection preserves saved A6/A7 value and lock bits exactly");
        rejected(session, write_for(headless, column), revision);
    }
    check(native_input.unchanged(session, revision), "unknown sound projection and rejected toggles preserve entire genuine Native");
    for (uint8_t value : {uint8_t(0), uint8_t(1), uint8_t(0x80), uint8_t(0x81)}) {
        session.state().memory.set8(::bmh::layout::Audio_OriginalAudioOptions_ExecutableField, value);
        session.state().memory.set8(::bmh::layout::Audio_OriginalAudioOptions_ExecutableField2, value);
        const Snapshot before(session, revision);
        const auto unknown = session.settings_projection(manager, revision);
        check(!unknown.audio_config_known && (*unknown.display_cells)[5] == value &&
              (*unknown.display_cells)[6] == value, "unknown host preserves both locked and unlocked saved sound bytes");
        rejected(session, write_for(unknown, 5), revision);
        rejected(session, write_for(unknown, 6), revision);
        check(before.unchanged(session, revision), "unknown host cannot toggle even an unlocked zero/one sound cell");
    }

    // Explicit controlled byte variants exercise portable capabilities over old
    // SOUND.CFG locks. These are regression controls, not natural career saves.
    for (bool music : {false, true}) for (bool effects : {false, true}) {
        session.state().memory.set8(::bmh::layout::Audio_OriginalAudioOptions_ExecutableField, 0x80);
        session.state().memory.set8(::bmh::layout::Audio_OriginalAudioOptions_ExecutableField2, 0x81);
        const Snapshot before(session, revision);
        session.configure_sound_capabilities(music, effects);
        const auto state = session.settings_projection(manager, revision);
        ui::SettingsScreen screen(data, state);
        (void)screen.render();
        check(state.audio_config_known && state.display_cells && state.cells[5] == 0x80 && state.cells[6] == 0x81,
              "known capability projection retains raw old lock bytes");
        check((*state.display_cells)[5] == (music ? 0 : 0x80) &&
              (*state.display_cells)[6] == (effects ? 1 : 0x81),
              "independent runtime music/effects capabilities determine A6/A7 display locks");
        check(before.unchanged(session, revision), "configuring capabilities and projecting/rendering writes no Native or RNG state");
        for (uint8_t column : {uint8_t(5), uint8_t(6)}) {
            if (column == 5 ? music : effects) {
                toggle(session, data, column, revision);
                toggle(session, data, column, revision);
                check(session.state().memory.u8(::bmh::layout::LeaguePresentationOptionsUnsigned + column) == (column == 5 ? 0 : 1),
                      "two deliberate toggles restore logical value and clear only stale sound lock");
            } else {
                rejected(session, write_for(session.settings_projection(manager, revision), column), revision);
                check(session.state().memory.u8(::bmh::layout::LeaguePresentationOptionsUnsigned + column) == (column == 5 ? 0x80 : 0x81),
                      "unsupported sound channel preserves saved lock and logical value");
            }
        }
    }

    session.configure_sound_capabilities(true, true);
    for (uint8_t invalid : {uint8_t(2), uint8_t(0x82), uint8_t(0x40), uint8_t(0xc1)}) {
        for (uint8_t column : {uint8_t(5), uint8_t(6)}) {
            session.state().memory.set8(::bmh::layout::LeaguePresentationOptionsUnsigned + column, invalid);
            rejected(session, write_for(session.settings_projection(manager, revision), column), revision);
        }
    }
    session.state().memory.set8(::bmh::layout::Audio_OriginalAudioOptions_ExecutableField, 0);
    session.state().memory.set8(::bmh::layout::Audio_OriginalAudioOptions_ExecutableField2, 1);
    const auto state = session.settings_projection(manager, revision);
    auto bad = write_for(state, 5); --bad.revision; rejected(session, bad, revision);
    bad = write_for(state, 5); bad.manager_slot = (manager + 1) % 2; rejected(session, bad, revision);
    bad = write_for(state, 5); bad.expected_raw[6] ^= 1; rejected(session, bad, revision);
    bad = write_for(state, 5); bad.operation = ui::SettingsEdit::increment; rejected(session, bad, revision);
    const auto stale = write_for(state, 6);
    toggle(session, data, 5, revision);
    rejected(session, stale, revision);

    // Check the normal owned desktop menu, whose write/ACK projection must also
    // carry the configured host capabilities through both sound toggles.
    const auto desk = session.desktop_projection(revision);
    const auto item = std::find_if(desk.items.begin(), desk.items.end(),
        [](const auto& i) { return i.kind == 19 && !i.disabled; });
    check(item != desk.items.end(), "genuine manager desk exposes the settings menu");
    std::string error;
    const Snapshot before_open(session, revision);
    const std::array<uint8_t, 2> saved_sound{session.state().memory.u8(::bmh::layout::Audio_OriginalAudioOptions_ExecutableField), session.state().memory.u8(::bmh::layout::Audio_OriginalAudioOptions_ExecutableField2)};
    auto opened = session.open_desk_settings(ui::DeskRequest{ui::DeskRequestKind::open_menu,
        desk.manager_slot, item->id, 19, -1, revision}, revision, error);
    check(opened && opened->state.audio_config_known &&
          session.state().memory.u8(::bmh::layout::Audio_OriginalAudioOptions_ExecutableField) == saved_sound[0] && session.state().memory.u8(::bmh::layout::Audio_OriginalAudioOptions_ExecutableField2) == saved_sound[1] &&
          before_open.random_unchanged(session), "desktop settings entry retains capabilities without rewriting sound bytes or drawing RNG");
    for (uint8_t column : {uint8_t(4), uint8_t(5), uint8_t(6)}) {
        const Snapshot before(session, revision);
        ui::SettingsScreen screen(data, opened->state);
        check(screen.click(70 + column * 24, 27) && screen.pending(), "owned menu sound click creates request");
        auto expected = session.state();
        expected.memory.set8(::bmh::layout::LeaguePresentationOptionsUnsigned + column, uint8_t((expected.memory.u8(::bmh::layout::LeaguePresentationOptionsUnsigned + column) & 0x7f) ^ 1));
        auto changed = session.dispatch_desk_settings(app::CareerSession::DeskSettingsAction{
            app::CareerSession::DeskSettingsWrite{opened->owner.sequence, *screen.pending()}}, revision, error);
        check(changed && !changed->closed && changed->state.audio_config_known &&
              save_stream(session.state()) == save_stream(expected) && revision == before.revision + 1 &&
              before.random_unchanged(session), "owned desktop sound toggle changes only its Native byte and revision");
        screen.acknowledge(changed->state);
        opened->owner = changed->owner;
        opened->state = changed->state;
    }
    const Snapshot before_close(session, revision);
    const std::array<uint8_t, 2> changed_sound{session.state().memory.u8(::bmh::layout::Audio_OriginalAudioOptions_ExecutableField), session.state().memory.u8(::bmh::layout::Audio_OriginalAudioOptions_ExecutableField2)};
    auto closed = session.dispatch_desk_settings(app::CareerSession::DeskSettingsAction{
        app::CareerSession::DeskSettingsClose{opened->owner}}, revision, error);
    check(closed && closed->closed && closed->state.audio_config_known &&
          session.state().memory.u8(::bmh::layout::Audio_OriginalAudioOptions_ExecutableField) == changed_sound[0] && session.state().memory.u8(::bmh::layout::Audio_OriginalAudioOptions_ExecutableField2) == changed_sound[1] &&
          before_close.random_unchanged(session), "normal settings close retains capabilities without additional sound writes or RNG draws");
    std::cout << "Sound settings Session/UI tests passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "sound_settings_app_tests: " << error.what() << '\n';
    return 1;
}
