// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#include "app/controller.hpp"
#include "game/save.hpp"

using namespace bmh;
namespace {
constexpr uint32_t kOffers = ::bmh::layout::OfferSlots, kSpecial = ::bmh::layout::EditorList + 40 * 0x58;
constexpr size_t kPool = 0xb70 + 40;
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
struct Snapshot {
    Bytes native;
    GameRandom game;
    WatcomRandom rand;
};
Snapshot snapshot(app::CareerSession& session) {
    return {save_stream(session.state()), session.game(), session.rand()};
}
void unchanged(app::CareerSession& session, const Snapshot& before) {
    require(save_stream(session.state()) == before.native &&
            session.game().seed() == before.game.seed() && session.game().calls() == before.game.calls() &&
            session.rand().seed() == before.rand.seed() && session.rand().calls() == before.rand.calls(),
            "nested unsupported batch retains all live native fields and both RNGs");
}

// These are controlled injections at a real native modal boundary, not evidence
// that this first-round fixture naturally contains pending offers. Keep its real
// owner/squad/board and borrow only the checksum-validated special-entry shape.
ClubPlayerDetailSelection inject(GameState& state, const std::array<uint8_t, 0x58>& offer,
                                 bool two) {
    const int manager = state.memory.u8(::bmh::layout::ActiveManager);
    require(manager >= 0 && manager < 4 && size_t(manager) < state.manager_teams.size(),
            "real cup callback has an active human owner");
    const int club = state.manager_teams.at(size_t(manager));
    require(club >= 0 && club < 138, "nested pending fixture uses an ordinary human club");
    const auto range = squad_range(club);
    std::array<int, 2> rows{-1, -1};
    for (size_t row = 0; row < range.count && rows[1] < 0; ++row) {
        const int player = state.squad_slots.at(range.first + row) & 0xfff;
        if (player == 0xfff) continue;
        if (rows[0] < 0) rows[0] = int(row);
        else if (player != (state.squad_slots.at(range.first + size_t(rows[0])) & 0xfff)) rows[1] = int(row);
    }
    require(rows[0] >= 0 && (!two || rows[1] >= 0), "real modal squad has the requested distinct players");
    // The later standalone check can run after the new desk generated its own
    // offers. Isolate this explicitly injected batch rather than answering those.
    for (uint32_t raw = 0; raw < 4; ++raw) {
        state.memory.set32(kOffers + raw * 4, 0xffffffffu);
        state.squad_slots.at(kPool + raw) = 0x0fff;
    }
    for (int raw = 0; raw < (two ? 2 : 1); ++raw) {
        const uint32_t list = kSpecial + uint32_t(raw) * 0x58;
        for (uint32_t i = 0; i < offer.size(); ++i) state.memory.set8(list + i, offer.at(i));
        state.memory.set16(list + 8, uint16_t(club));
        const int player = state.squad_slots.at(range.first + size_t(rows[size_t(raw)])) & 0xfff;
        state.squad_slots.at(kPool + size_t(raw)) = uint16_t(player);
        state.memory.set32(kOffers + uint32_t(raw) * 4,
                           0x80000000u | uint32_t(player) * 1024u | uint32_t(manager));
    }
    const int first = state.squad_slots.at(range.first + size_t(rows[0])) & 0xfff;
    return {club, first, rows[0], rows[0], -1};
}

void nested(const std::filesystem::path& data, const std::filesystem::path& season_input,
            const std::filesystem::path& offer_input) {
    app::CareerSession genuine(data, 1, offer_input);
    require(genuine.state().memory.u32(kOffers) == 0x80003800u &&
            genuine.state().memory.u32(kSpecial + 0x0c) == 52300 &&
            genuine.state().memory.u32(kSpecial + 0x1c) == 2470000,
            "injection donor is the genuine checksum-validated N.BROWN salary offer");
    std::array<uint8_t, 0x58> offer{};
    for (uint32_t i = 0; i < offer.size(); ++i) offer.at(i) = genuine.state().memory.u8(kSpecial + i);

    ui::StartupFlow flow(data, season_input.parent_path());
    auto session = std::make_unique<app::CareerSession>(data, 1, season_input);
    uint64_t revision = 1;
    flow.click(214, 215);
    require(flow.send_key("ENTER").request == ui::FlowRequest::file_action, "native load screen");
    flow.attach_loaded_game(session->desktop_projection(revision), session->preparation_projection(revision));
    const auto& items = flow.desktop()->state().items;
    const auto item = std::find_if(items.begin(), items.end(), [](const auto& i) {
        return !i.disabled && (i.kind == 12 || (i.kind == 58 && i.open));
    });
    require(item != items.end(), "native season-end Weiter is reachable");
    const auto request = flow.click(item->x + item->width / 2, item->y + item->height / 2);
    require(request.request == ui::FlowRequest::kickoff, "production season advance is pending");

    int callbacks = 0, resumed_after_commit = 0;
    bool injected = false;
    SeasonCupDrawHook hook = [&](const GameState& state, const SeasonCupDrawEvent& event) {
        ++callbacks;
        require(&state == &session->state(), "cup hook borrows the actual session GameState");
        require(event.phase == (callbacks == 1 ? SeasonCupDrawPhase::draw : SeasonCupDrawPhase::overview),
                "real draw is followed by the native overview callback");
        require(event.participants.size() == 64, "native annual modal retains its real 64-team field");
        if (event.phase != SeasonCupDrawPhase::overview) return;
        require(!injected && revision == 1, "outer calendar revision stays unpublished in the modal");
        for (unsigned raw = 0; raw < 4; ++raw)
            require(state.memory.u32(kOffers + raw * 4) == 0xffffffffu,
                    "the unmodified native first-round boundary has no natural pending offers");
        const auto selection = inject(session->state(), offer, true);
        injected = true;
        const auto before = snapshot(*session);
        const StaticMemory& borrowed_memory = state.memory;
        const auto* memory_address = &borrowed_memory;
        const auto* squad_container_address = &state.squad_slots;
        const auto field = event.participants;
        const uint8_t competition = borrowed_memory.u8(event.entry_address + 0x18);
        std::string error;

        int answers = 0;
        PendingOfferHooks unavailable;
        unavailable.answer = [&](const GameState& staged, const PendingOfferQuestion& question) {
            unchanged(*session, before);
            require(&staged != &state && revision == 1, "questions use a staged copy without publishing the outer revision");
            ++answers;
            if (question.raw_offer_slot == 0) return PendingOfferAnswer::yes;
            require(question.raw_offer_slot == 1 && staged.memory.u32(kOffers) == 0xffffffffu &&
                    save_stream(staged) != before.native,
                    "second question follows an actual earlier staged JA and cleanup");
            return PendingOfferAnswer::unsupported;
        };
        require(!session->dispatch_club_player_pending_offers(selection, 1, unavailable, revision, error) &&
                answers == 2 && revision == 1 && !error.empty(), "nested partial batch rolls back");
        unchanged(*session, before);

        GameState expected = session->state();
        auto expected_game = session->game(); auto expected_rand = session->rand();
        PendingOfferHooks accept;
        accept.answer = [](const auto&, const auto&) { return PendingOfferAnswer::yes; };
        MinuteReport report;
        const auto oracle = process_player_card_pending_offers(expected, selection,
            expected_game, expected_rand, accept, report);
        require(oracle.completed && oracle.processed == 2 && !report.unsupported, "controlled batch is supported by Core");
        const auto result = session->dispatch_club_player_pending_offers(selection, 1, accept, revision, error);
        require(result && result->completed && result->salary_accepted == 2 && result->processed == 2 &&
                error.empty() && revision == 1, "nested successful batch defers its revision to the outer calendar");
        require(save_stream(state) == save_stream(expected) &&
                session->game().seed() == expected_game.seed() && session->game().calls() == expected_game.calls() &&
                session->rand().seed() == expected_rand.seed() && session->rand().calls() == expected_rand.calls(),
                "callback's borrowed state sees the complete committed Core result and both RNGs");
        require(&state.memory == memory_address && &state.squad_slots == squad_container_address &&
                borrowed_memory.u8(event.entry_address + 0x18) == competition && event.participants == field,
                "move commit preserves member-object references and the owned cup event");
        // This statement must execute in the same still-running callback after
        // rollback and commit; no UI input or invented modal adapter is involved.
        ++resumed_after_commit;
    };
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio;
    const auto message = app::serve_flow_request(flow, request, session, data, season_input.parent_path(), 1,
        revision, playback, audio, false, {}, {}, {}, {}, {}, {}, {}, hook);
    require(message.empty() && injected && callbacks == 2 && resumed_after_commit == 1 &&
            revision == 2 && flow.desktop() != nullptr, "outer driver resumes and publishes exactly revision2");

    const auto settled = snapshot(*session);
    (void)app::serve_flow_request(flow, request, session, data, season_input.parent_path(), 1,
        revision, playback, audio, false, {}, {}, {}, {}, {}, {}, {}, hook);
    require(revision == 2 && callbacks == 2, "replayed kickoff cannot reenter its modal");
    unchanged(*session, settled);

    const auto selection = inject(session->state(), offer, false);
    PendingOfferHooks accept;
    accept.answer = [](const auto&, const auto&) { return PendingOfferAnswer::yes; };
    std::string error;
    const auto result = session->dispatch_club_player_pending_offers(selection, 2, accept, revision, error);
    require(result && result->completed && result->processed == 1 && error.empty() && revision == 3,
            "calendar RAII resets: a later standalone transaction publishes its own revision once");
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    if (!*argv[1]) { std::puts("Pending nested app tests skipped: Original data not configured"); return 77; }
    try {
        nested(argv[1], argv[2], argv[3]);
        std::puts("Nested native cup callback: partial rollback, commit, borrowed references, outer revision and RAII reset OK");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
