#include "test_inputs.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <vector>
#include "app/controller.hpp"
#include "formats/textd.hpp"
#include "game/save.hpp"
#include "ui/season_entrants.hpp"

using namespace bmh;
namespace {
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
struct Result { Bytes state; uint32_t game_seed, watcom_seed; unsigned game_calls, watcom_calls; };
void require_initial_hooks_cleared(const app::CareerSession& session, const char* message) {
    const auto driver = session.manager_join_checkpoint().driver;
    require(!driver.season_pause && !driver.season_cup_draw, message);
}
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
    std::vector<SeasonCupDrawEvent> events;
    SeasonCupDrawHook hook;
    if (observe) hook = [&](const GameState& state, const SeasonCupDrawEvent& event) {
        for (unsigned offer = 0; offer < 4; ++offer) {
            require(state.memory.u32(::bmh::layout::OfferSlots + offer * 4) == 0xffffffffu,
                    "first-round modal follows Source empty-offer boundary");
        }
        const auto before = save_stream(state);
        const auto game = session->game(); const auto watcom = session->rand();
        require(event.phase == (events.empty() ? SeasonCupDrawPhase::draw : SeasonCupDrawPhase::overview),
                "draw precedes native sorted overview");
        require(event.participants.size() == event.count && event.count == 64,
                "owned first-round field contains all 64 participants");
        events.push_back(event); // owned projection can outlive the callback
        require(before == save_stream(state) && game.seed() == session->game().seed() &&
                game.calls() == session->game().calls() && watcom.seed() == session->rand().seed() &&
                watcom.calls() == session->rand().calls(), "presentation leaves state and both RNGs unchanged");
    };
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio;
    const auto message = app::serve_flow_request(flow, request, session, data, input.parent_path(), 1,
        revision, playback, audio, false, {}, {}, {}, {}, {}, {}, {}, hook);
    require(message.empty() && revision == 2 && flow.desktop() != nullptr, "transition reaches the new-season desk");
    if (observe) require(events.size() == 2, "both cup phases presented once");
    const auto settled = save_stream(session->state());
    const auto game = session->game(); const auto watcom = session->rand();
    (void)app::serve_flow_request(flow, request, session, data, input.parent_path(), 1,
        revision, playback, audio, false, {}, {}, {}, {}, {}, {}, {}, hook);
    require(settled == save_stream(session->state()) && revision == 2 && (!observe || events.size() == 2) &&
            game.seed() == session->game().seed() && game.calls() == session->game().calls() &&
            watcom.seed() == session->rand().seed() && watcom.calls() == session->rand().calls(),
            "replayed stale kickoff has no callback, mutation or random draws");
    return {settled, uint32_t(game.seed()), watcom.seed(), unsigned(game.calls()), unsigned(watcom.calls())};
}
Result start(const std::filesystem::path& data, bool observe) {
    ui::StartupFlow flow(data);
    flow.click(150, 215); flow.click(5, 225, true); flow.click(5, 225, true);
    const auto request = flow.click(90, 169);
    require(request.request == ui::FlowRequest::new_career, "production new-career request");
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio;
    std::vector<SeasonCupDrawEvent> events;
    std::vector<int> entrant_competitions;
    const auto exe = load_le(ui::asset_path(data, "BMH.exe"));
    const auto texts = load_textd(ui::asset_path(data, "TEXTD.BLK"));
    app::SeasonPresenter season_hook;
    int national_cup_pauses = 0;
    if (observe) season_hook = [&](const char* checkpoint, const GameState& state) {
        if (std::string(checkpoint) == "the German entrants (legacy routine)") {
            require(events.empty(), "initial German entrants pause precedes the national cup draw");
            const auto before = save_stream(state);
            const auto game_before = session->game();
            const auto watcom_before = session->rand();
            const auto pages = ui::season_entrants_from_game(state, exe, texts);
            require(pages.size() == 3 && pages[0].footer == bmh::test::expected_text("season_cup_draw_app_tests-external-3").c_str() &&
                    pages[1].footer == bmh::test::expected_text("season_cup_draw_app_tests-external-1").c_str() &&
                    pages[2].footer == bmh::test::expected_text("season_cup_draw_app_tests-external-2").c_str(),
                    "initial career exposes the three original competitions in order");
            entrant_competitions = {pages[0].competition, pages[1].competition, pages[2].competition};
            require(before == save_stream(state) && game_before.seed() == session->game().seed() &&
                    game_before.calls() == session->game().calls() &&
                    watcom_before.seed() == session->rand().seed() &&
                    watcom_before.calls() == session->rand().calls(),
                    "initial entrant projection leaves native state and both RNGs unchanged");
        } else {
            if (std::string(checkpoint) == "the national cup draw (legacy routine)") {
                require(entrant_competitions == std::vector<int>({0, 1, 2}) && events.size() == 1 &&
                        events.front().phase == SeasonCupDrawPhase::draw,
                        "national cup pause follows the original European entrants");
                ++national_cup_pauses;
            }
        }
    };
    SeasonCupDrawHook hook;
    if (observe) hook = [&](const GameState& state, const SeasonCupDrawEvent& event) {
        require(session && event.participants.size() == 64, "initial draw owned field");
        require(event.phase == (events.empty() ? SeasonCupDrawPhase::draw : SeasonCupDrawPhase::overview),
                "initial draw/overview order");
        for (unsigned offer = 0; offer < 4; ++offer) {
            require(state.memory.u32(::bmh::layout::OfferSlots + offer * 4) == 0xffffffffu,
                    "first-round modal follows Source empty-offer boundary");
        }
        const auto before = save_stream(state);
        const auto game = session->game(); const auto watcom = session->rand();
        events.push_back(event);
        require(before == save_stream(state) && game.seed() == session->game().seed() &&
                game.calls() == session->game().calls() && watcom.seed() == session->rand().seed() &&
                watcom.calls() == session->rand().calls(), "initial presentation leaves state/RNG unchanged");
    };
    const auto message = app::serve_flow_request(flow, request, session, data, data, 0x1789abcd,
        revision, playback, audio, false, {}, {}, {}, season_hook, {}, {}, {}, hook);
    require(message.empty() && session && flow.screen_name() == "desktop", "initial draw reaches first desk");
    require_initial_hooks_cleared(*session, "initial presenters are cleared after successful controller start");
    if (observe) require(events.size() == 2, "initial cup phases both presented exactly once");
    require(observe ? entrant_competitions == std::vector<int>({0, 1, 2}) && national_cup_pauses == 1
                    : entrant_competitions.empty() && national_cup_pauses == 0,
            "initial season presenter is scoped to the observed career start");
    const auto game = session->game(); const auto watcom = session->rand();
    return {save_stream(session->state()), uint32_t(game.seed()), watcom.seed(),
            unsigned(game.calls()), unsigned(watcom.calls())};
}
void throwing_initial_presenter(const std::filesystem::path& data) {
    ui::StartupFlow flow(data);
    flow.click(150, 215); flow.click(5, 225, true); flow.click(5, 225, true);
    const auto request = flow.click(90, 169);
    require(request.request == ui::FlowRequest::new_career, "throwing-presentation fixture requests a new career");
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio;
    bool installed_inside_callback = false;
    app::SeasonPresenter season_hook = [&](const char* checkpoint, const GameState&) {
        require(std::string(checkpoint) == "the German entrants (legacy routine)",
                "throwing fixture reaches the original German entrants checkpoint");
        const auto driver = session->manager_join_checkpoint().driver;
        installed_inside_callback = bool(driver.season_pause) && bool(driver.season_cup_draw);
        throw std::runtime_error("initial presenter sentinel");
    };
    SeasonCupDrawHook cup_hook = [](const GameState&, const SeasonCupDrawEvent&) {};
    bool caught = false;
    try {
        (void)app::serve_flow_request(flow, request, session, data, data, 0x1789abcd,
            revision, playback, audio, false, {}, {}, {}, season_hook, {}, {}, {}, cup_hook);
    } catch (const std::runtime_error& e) {
        caught = std::string(e.what()) == "initial presenter sentinel";
    }
    require(caught && installed_inside_callback,
            "initial presenter exception propagates after both hooks were installed");
    require_initial_hooks_cleared(*session, "initial presenters are cleared while unwinding the controller");
}
void same(const Result& a, const Result& b) {
    require(a.state == b.state && a.game_seed == b.game_seed && a.watcom_seed == b.watcom_seed &&
            a.game_calls == b.game_calls && a.watcom_calls == b.watcom_calls,
            "cup presentation preserves full native state and RNG at both entry paths");
}

void detail_actions(const std::filesystem::path& data, const std::filesystem::path& input) {
    app::CareerSession session(data, 1, input);
    const auto exe = load_le(data / "BMH.exe");
    const auto range = squad_range(4); // the real runtime club, not the active manager
    size_t row = 0;
    while (row < range.count && (session.state().squad_slots.at(range.first + row) & 0x0fff) == 0x0fff) ++row;
    require(row < range.count, "native detail fixture has a real player");
    const int player = session.state().squad_slots[range.first + row] & 0x0fff;
    const ClubPlayerDetailSelection selection{4, player, int(row), int(row), -1};
    const auto native = save_stream(session.state());
    auto expected_game = session.game(); auto expected_rand = session.rand();
    const auto expected = club_player_detail_redraw(session.state(), selection, exe, expected_game, expected_rand);
    std::string error;
    const auto actual = session.redraw_club_player_detail(selection, error);
    require(actual.has_value() && error.empty() && actual->context.selection.player_id == player &&
            actual->portrait_base == expected.portrait_base && actual->portrait_features == expected.portrait_features &&
            actual->market_value == expected.market_value && native == save_stream(session.state()),
            "session action uses the requested runtime player and leaves native tables unchanged");
    require(session.game().seed() == expected_game.seed() && session.game().calls() == expected_game.calls() &&
            session.rand().seed() == expected_rand.seed() && session.rand().calls() == expected_rand.calls(),
            "one real card redraw commits exactly the Source Core RNG action");
    auto stale = selection; stale.player_id = -1;
    require(!session.redraw_club_player_detail(stale, error) && !error.empty() &&
            native == save_stream(session.state()) && session.game().seed() == expected_game.seed() &&
            session.game().calls() == expected_game.calls() && session.rand().seed() == expected_rand.seed() &&
            session.rand().calls() == expected_rand.calls(), "invalid card identity leaves live native state and RNG intact");
    require(!session.dispatch_club_player_national_action(selection, error) && !error.empty() &&
            native == save_stream(session.state()) && session.rand().seed() == expected_rand.seed() &&
            session.rand().calls() == expected_rand.calls(), "unavailable national action rejects without native writes or RNG");
}

}
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    if (!*argv[1]) { std::puts("Cup app tests skipped: Original data not configured"); return 77; }
    try {
        const auto plain = advance(argv[1], argv[2], false);
        const auto observed = advance(argv[1], argv[2], true);
        require(plain.state == observed.state && plain.game_seed == observed.game_seed &&
                plain.watcom_seed == observed.watcom_seed && plain.game_calls == observed.game_calls &&
                plain.watcom_calls == observed.watcom_calls, "observer preserves the full transition and RNG order");
        same(start(argv[1], false), start(argv[1], true));
        throwing_initial_presenter(argv[1]);
        detail_actions(argv[1], argv[2]);
        std::puts("Production cup bridge, full-state/RNG neutrality and replay OK");
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
    // SDL2main renames main to SDL_main, which has no implicit "return 0"
    return 0;
}
