// SPDX-License-Identifier: GPL-3.0-only
#include <algorithm>
#include <cstdio>
#include <stdexcept>

#include "app/career_session.hpp"
#include "formats/le.hpp"
#include "ui/flow.hpp"
#include "ui/training.hpp"

using namespace bmh;
using namespace bmh::ui;

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

TrainingRequest request_for(const TrainingState& state, TrainingRequestKind kind) {
    TrainingRequest request;
    request.kind = kind;
    request.manager_slot = state.manager_slot;
    request.runtime_club = state.runtime_club;
    request.revision = state.revision;
    request.desired_types = state.types;
    request.desired_areas = state.areas;
    request.desired_automation = state.automation;
    return request;
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("training_dispatch_app_tests requires original data directory");
        const std::filesystem::path data = argv[1];
        StartupFlow flow(data);
        flow.click(150, 215); // ordinary domestic career
        flow.click(5, 225, true);
        flow.click(5, 225, true);
        const auto confirmed = flow.click(90, 169);
        check(confirmed.request == FlowRequest::new_career && flow.pending_career(), "one manager selected");

        app::CareerSession session(data, 0x12345678);
        std::string error;
        check(session.confirm_manager(flow.pending_career()->options, flow.pending_career()->managers[0], error),
              "controller accepts manager selection");
        CareerInitializationResult initialized;
        check(session.finish(initialized, error), "career reaches first desk");
        uint64_t revision = initialized.desktop.revision;
        LeImage exe = load_le(asset_path(data, "BMH.exe"));

        auto rng_unchanged = [&](int32_t game_seed, uint64_t game_calls, uint32_t watcom_seed, uint64_t watcom_calls) {
            return session.game().seed() == game_seed && session.game().calls() == game_calls &&
                   session.rand().seed() == watcom_seed && session.rand().calls() == watcom_calls;
        };
        auto game_seed = session.game().seed();
        auto game_calls = session.game().calls();
        auto watcom_seed = session.rand().seed();
        auto watcom_calls = session.rand().calls();

        TrainingState state = training_from_game(session.state(), exe, revision);
        if (state.needs_initialization()) {
            auto first_open = request_for(state, TrainingRequestKind::initialize);
            first_open.desired_automation = 0x9f;
            check(session.dispatch_training(first_open, revision, error), "first-open request initializes training");
            check(revision == state.revision + 1 &&
                  session.state().manager_options[size_t(state.manager_slot) * 5 + 4] == 0x9f &&
                  rng_unchanged(game_seed, game_calls, watcom_seed, watcom_calls),
                  "first-open initialization applies the original deterministic routine without draws");
            state = training_from_game(session.state(), exe, revision);
        }
        check(!state.needs_initialization(), "training is initialized before edits");
        const auto records_before_invalid = session.state().manager_records;
        auto stale = request_for(state, TrainingRequestKind::set_type);
        stale.index = 0;
        stale.value = state.types[0] == 1 ? 2 : 1;
        stale.desired_types[0] = uint8_t(stale.value);
        stale.desired_automation &= uint8_t(~2);
        --stale.revision;
        check(!session.dispatch_training(stale, revision, error) && session.state().manager_records == records_before_invalid &&
              revision == state.revision &&
              rng_unchanged(game_seed, game_calls, watcom_seed, watcom_calls), "stale revision has no effect");

        auto wrong_manager = stale;
        wrong_manager.revision = revision;
        wrong_manager.manager_slot = 1;
        check(!session.dispatch_training(wrong_manager, revision, error) &&
              session.state().manager_records == records_before_invalid &&
              revision == state.revision &&
              rng_unchanged(game_seed, game_calls, watcom_seed, watcom_calls), "wrong manager has no effect");

        auto invalid_sum = request_for(state, TrainingRequestKind::set_area);
        invalid_sum.index = 2;
        invalid_sum.value = state.areas[2] == 3 ? 2 : 3;
        invalid_sum.desired_areas.fill(3);
        check(!session.dispatch_training(invalid_sum, revision, error) &&
              session.state().manager_records == records_before_invalid &&
              revision == state.revision &&
              rng_unchanged(game_seed, game_calls, watcom_seed, watcom_calls), "invalid ball totals have no effect");

        auto club_request = stale;
        club_request.revision = revision;
        ++club_request.runtime_club;
        check(!session.dispatch_training(club_request, revision, error) &&
              session.state().manager_records == records_before_invalid && revision == state.revision &&
              rng_unchanged(game_seed, game_calls, watcom_seed, watcom_calls), "wrong runtime club is rejected");

        int type_index = -1;
        for (int i = 0; i < int(state.types.size()); ++i) if (state.types[size_t(i)] > 1) { type_index = i; break; }
        check(type_index >= 0, "career has a decrementable training type");
        auto type = request_for(state, TrainingRequestKind::set_type);
        type.index = type_index;
        type.value = int(state.types[size_t(type_index)]) - 1;
        type.desired_types[size_t(type_index)] = uint8_t(type.value);
        type.desired_automation &= uint8_t(~2);
        check(session.dispatch_training(type, revision, error), "valid type request accepted");
        check(revision == state.revision + 1 &&
              session.state().manager_records[size_t(state.manager_slot) * 0xb0c + 0xc5 + size_t(type_index)] == type.value &&
              (session.state().manager_options[size_t(state.manager_slot) * 5 + 4] & 2) == 0,
              "type and its manual-override flag are persisted once");
        check(rng_unchanged(game_seed, game_calls, watcom_seed, watcom_calls), "manual type edit is drawless");

        state = training_from_game(session.state(), exe, revision);
        int area_index = -1;
        for (int i = 0; i < int(state.areas.size()); ++i) if (state.areas[size_t(i)] > 1) { area_index = i; break; }
        check(area_index >= 0, "career has a decrementable training area");
        auto area = request_for(state, TrainingRequestKind::set_area);
        area.index = area_index;
        area.value = int(state.areas[size_t(area_index)]) - 1;
        area.desired_areas[size_t(area_index)] = uint8_t(area.value);
        area.desired_automation &= uint8_t(~4);
        check(session.dispatch_training(area, revision, error), "valid training-area request accepted");
        check(session.state().manager_records[size_t(state.manager_slot) * 0xb0c + 0xc9 + size_t(area_index)] == area.value &&
              (session.state().manager_options[size_t(state.manager_slot) * 5 + 4] & 4) == 0 &&
              revision == state.revision + 1 && rng_unchanged(game_seed, game_calls, watcom_seed, watcom_calls),
              "training area and its override flag are saved once without RNG");

        state = training_from_game(session.state(), exe, revision);
        check(!state.players.empty(), "career has training-board players");
        const auto intensity_player = std::find_if(state.players.begin(), state.players.end(),
            [](const TrainingPlayer& p) { return p.intensity <= 3; });
        check(intensity_player != state.players.end(), "career has a player with valid intensity");
        auto intensity = request_for(state, TrainingRequestKind::set_player_intensity);
        intensity.index = intensity_player->row;
        intensity.player_id = intensity_player->player_id;
        intensity.value = intensity_player->intensity == 3 ? 2 : int(intensity_player->intensity) + 1;
        intensity.desired_automation &= uint8_t(~8);
        check(session.dispatch_training(intensity, revision, error), "valid individual intensity request accepted");
        const size_t intensity_board = size_t(state.manager_slot) * 0x840 + size_t(intensity.index) * 0x58 + 0x0a;
        check((session.state().tactics[intensity_board] & 3) == intensity.value &&
              (session.state().manager_options[size_t(state.manager_slot) * 5 + 4] & 8) == 0 &&
              revision == state.revision + 1 && rng_unchanged(game_seed, game_calls, watcom_seed, watcom_calls),
              "player intensity updates only its identified board row, without RNG");

        state = training_from_game(session.state(), exe, revision);
        const auto player = state.players.front();
        const auto board_before_recycled = session.state().tactics;
        auto recycled = request_for(state, TrainingRequestKind::set_player_focus);
        recycled.index = player.row;
        recycled.value = (player.focus + 1) % 8;
        recycled.player_id = player.player_id == 0 ? 1 : 0;
        check(!session.dispatch_training(recycled, revision, error) &&
              session.state().tactics == board_before_recycled &&
              rng_unchanged(game_seed, game_calls, watcom_seed, watcom_calls), "recycled player id is rejected drawlessly");

        auto focus = request_for(state, TrainingRequestKind::set_player_focus);
        focus.index = player.row;
        focus.value = (player.focus + 1) % 8;
        focus.player_id = player.player_id;
        check(session.dispatch_training(focus, revision, error), "current player focus request accepted");
        const size_t board = size_t(state.manager_slot) * 0x840 + size_t(player.row) * 0x58 + 0x0a;
        check(((session.state().tactics[board] >> 2) & 7) == focus.value &&
              revision == state.revision + 1 && rng_unchanged(game_seed, game_calls, watcom_seed, watcom_calls),
              "focus updates only the matching board row without RNG");

        // Subordinate automation switches are only exposed with the global switch on.
        session.state().manager_options[size_t(state.manager_slot) * 5 + 4] &= uint8_t(~1);
        state = training_from_game(session.state(), exe, revision);
        const auto records_before_hidden = session.state().manager_records;
        const auto options_before_hidden = session.state().manager_options;
        const auto board_before_hidden = session.state().tactics;
        auto hidden_group = request_for(state, TrainingRequestKind::set_automation);
        hidden_group.index = 1;
        hidden_group.value = (state.automation & 2) ? 0 : 1;
        hidden_group.desired_automation ^= 2;
        check(!session.dispatch_training(hidden_group, revision, error) &&
              session.state().manager_records == records_before_hidden &&
              session.state().manager_options == options_before_hidden &&
              session.state().tactics == board_before_hidden && revision == state.revision &&
              rng_unchanged(game_seed, game_calls, watcom_seed, watcom_calls),
              "hidden automation group is rejected without any state, revision or RNG change");

        state = training_from_game(session.state(), exe, revision);
        session.state().manager_options[size_t(state.manager_slot) * 5 + 4] &= 0x7f;
        state = training_from_game(session.state(), exe, revision);
        check(state.needs_initialization(), "fixture can exercise first-open initialization");
        auto initialize = request_for(state, TrainingRequestKind::initialize);
        initialize.desired_automation = 0x9f;
        check(session.dispatch_training(initialize, revision, error), "first-open initialization accepted");
        check(session.state().manager_options[size_t(state.manager_slot) * 5 + 4] == 0x9f &&
              revision == state.revision + 1 && rng_unchanged(game_seed, game_calls, watcom_seed, watcom_calls),
              "original deterministic auto-training initialization is drawless");

        state = training_from_game(session.state(), exe, revision);
        auto turn_auto_off = request_for(state, TrainingRequestKind::set_automation);
        turn_auto_off.index = 0;
        turn_auto_off.value = 0;
        turn_auto_off.desired_automation ^= 1;
        check(session.dispatch_training(turn_auto_off, revision, error), "global automation can be switched off");
        auto turn_auto_on = request_for(training_from_game(session.state(), exe, revision), TrainingRequestKind::set_automation);
        turn_auto_on.index = 0;
        turn_auto_on.value = 1;
        turn_auto_on.desired_automation ^= 1;
        check(session.dispatch_training(turn_auto_on, revision, error), "global automation can be enabled again");
        check(rng_unchanged(game_seed, game_calls, watcom_seed, watcom_calls), "automatic plan calculation is drawless");

        std::puts("training dispatch: identity/revision, budgets, player identity, ACK revision and drawless original auto-training OK");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
