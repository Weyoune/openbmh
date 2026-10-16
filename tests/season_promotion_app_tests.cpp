// SPDX-License-Identifier: GPL-3.0-only
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <vector>
#include "app/controller.hpp"
#include "game/save.hpp"

using namespace bmh;
namespace {
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
struct Result { Bytes state; uint32_t game_seed, watcom_seed; unsigned game_calls, watcom_calls; };
Result advance(const std::filesystem::path& data, const std::filesystem::path& input, bool observe) {
    ui::StartupFlow flow(data, input.parent_path());
    auto session = std::make_unique<app::CareerSession>(data, 1, input);
    uint64_t revision = 1;
    flow.click(214, 215);
    require(flow.send_key("ENTER").request == ui::FlowRequest::file_action, "native load screen");
    flow.attach_loaded_game(session->desktop_projection(revision), session->preparation_projection(revision));
    const auto& items = flow.desktop()->state().items;
    const auto item = std::find_if(items.begin(), items.end(), [](const auto& i) {
        return !i.disabled && (i.kind == 12 || (i.kind == 58 && i.open));
    });
    require(item != items.end(), "season-end Weiter is reachable");
    const auto request = flow.click(item->x + item->width / 2, item->y + item->height / 2);
    require(request.request == ui::FlowRequest::kickoff, "pending production season advance");
    std::vector<SeasonPromotionEvent> events;
    SeasonPromotionHook hook;
    if (observe) hook = [&](const GameState& state, const SeasonPromotionEvent& event) {
        const auto before = save_stream(state);
        const auto game = session->game(); const auto watcom = session->rand();
        require(event.group == int(events.size()), "Original four-group order");
        require(event.count <= event.relegated.size() && event.count <= event.promoted.size(),
                "owned lists contain all displayed Original participants");
        events.push_back(event); // owned projection can outlive the callback
        require(before == save_stream(state) && game.seed() == session->game().seed() &&
                game.calls() == session->game().calls() && watcom.seed() == session->rand().seed() &&
                watcom.calls() == session->rand().calls(), "presentation leaves state and both RNGs unchanged");
    };
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio;
    const auto message = app::serve_flow_request(flow, request, session, data, input.parent_path(), 1,
        revision, playback, audio, false, {}, {}, {}, {}, {}, {}, hook);
    require(message.empty() && revision == 2 && flow.desktop() != nullptr, "transition reaches the new-season desk");
    if (observe) require(events.size() == 4, "all promotion groups presented once");
    const auto settled = save_stream(session->state());
    const auto game = session->game(); const auto watcom = session->rand();
    (void)app::serve_flow_request(flow, request, session, data, input.parent_path(), 1,
        revision, playback, audio, false, {}, {}, {}, {}, {}, {}, hook);
    require(settled == save_stream(session->state()) && revision == 2 && (!observe || events.size() == 4) &&
            game.seed() == session->game().seed() && game.calls() == session->game().calls() &&
            watcom.seed() == session->rand().seed() && watcom.calls() == session->rand().calls(),
            "replayed stale kickoff has no callback, mutation or random draws");
    return {settled, uint32_t(game.seed()), watcom.seed(), unsigned(game.calls()), unsigned(watcom.calls())};
}
}
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    if (!*argv[1]) { std::puts("Promotion app tests skipped: Original data not configured"); return 77; }
    try {
        const auto plain = advance(argv[1], argv[2], false);
        const auto observed = advance(argv[1], argv[2], true);
        require(plain.state == observed.state && plain.game_seed == observed.game_seed &&
                plain.watcom_seed == observed.watcom_seed && plain.game_calls == observed.game_calls &&
                plain.watcom_calls == observed.watcom_calls, "observer preserves the full transition and RNG order");
        std::puts("Production promotion bridge, full-state/RNG neutrality and replay OK");
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
    // SDL2main renames main to SDL_main, which has no implicit "return 0"
    return 0;
}
