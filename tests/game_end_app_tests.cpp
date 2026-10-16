// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include "app/controller.hpp"
#include "game/save.hpp"

using namespace bmh;
namespace {
void require(bool b, const char* message) { if (!b) throw std::runtime_error(message); }
void run(const std::filesystem::path& data, const std::filesystem::path& input) {
    ui::StartupFlow flow(data, input.parent_path());
    auto session = std::make_unique<app::CareerSession>(data, 589388519u, input);
    session->game().set_seed(665367);
    uint64_t revision = 1;
    flow.click(214, 215);
    require(flow.send_key("ENTER").request == ui::FlowRequest::file_action, "native fixture load menu");
    flow.attach_loaded_game(session->desktop_projection(revision), session->preparation_projection(revision));
    auto& items = flow.desktop()->state().items;
    auto next = std::find_if(items.begin(), items.end(), [](const auto& i) {
        return !i.disabled && (i.kind == 12 || (i.kind == 58 && i.open));
    });
    require(next != items.end(), "native final-season Weiter");
    auto request = flow.click(next->x + next->width / 2, next->y + next->height / 2);
    require(request.request == ui::FlowRequest::kickoff, "production pending advance");
    const auto original_desk = flow.state_json();
    std::vector<bool> highscore_calls;
    std::vector<SeasonTaxEventKind> tax_calls;
    SeasonTaxHooks tax;
    tax.observe = [&](const GameState& state, const SeasonTaxEvent& event) {
        auto before = save_stream(state);
        tax_calls.push_back(event.kind);
        require(event.kind == SeasonTaxEventKind::notice, "NO has only notice observer");
        require(before == save_stream(state), "owned tax presentation does not mutate state");
    };
    tax.choose = [&](const GameState&, const SeasonTaxEvent& event) {
        require(event.kind == SeasonTaxEventKind::question, "Source actual question");
        tax_calls.push_back(event.kind);
        return false;
    };
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio;
    auto highscore = [&](const GameState& state, bool display) {
        require(state.date == 365 && state.season == 1, "highscore before first new-season Wednesday");
        highscore_calls.push_back(display);
    };
    const auto message = app::serve_flow_request(flow, request, session, data, input.parent_path(), 1,
        revision, playback, audio, false, {}, {}, {}, {}, tax, highscore);
    require(message == "career_complete" && revision == 2 && session->state().memory.u8(::bmh::layout::CareerDuration) == 0,
        "terminal completion is normal production result");
    require(highscore_calls == std::vector<bool>({false, true}) &&
        tax_calls == std::vector<SeasonTaxEventKind>({SeasonTaxEventKind::notice, SeasonTaxEventKind::question}),
        "Source modal order/one NO decision, two highscore calls");
    require(flow.state_json() == original_desk, "completed career bypasses desktop reprojection");
    const auto settled = save_stream(session->state());
    const auto g = session->game(); const auto w = session->rand();
    (void)app::serve_flow_request(flow, request, session, data, input.parent_path(), 1,
        revision, playback, audio, false, {}, {}, {}, {}, tax, highscore);
    require(settled == save_stream(session->state()) && revision == 2 && highscore_calls.size() == 2 &&
        tax_calls.size() == 2 && g.seed() == session->game().seed() && g.calls() == session->game().calls() &&
        w.seed() == session->rand().seed() && w.calls() == session->rand().calls(),
        "terminal request replay is inert with no duplicate callbacks or RNG");
}
}
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    if (!*argv[1]) { std::puts("GameEnd app tests skipped: Original data not configured"); return 77; }
    try { run(argv[1], argv[2]); std::puts("GameEnd production Controller: completion, callback order and replay OK"); }
    catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
