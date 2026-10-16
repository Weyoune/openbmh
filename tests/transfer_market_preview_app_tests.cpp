// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <variant>

#include "app/career_session.hpp"
#include "app/controller.hpp"
#include "formats/fbm.hpp"
#include "game/save.hpp"
#include "game/month.hpp"

using namespace bmh;
namespace fs = std::filesystem;
namespace {
void require(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}
struct Snapshot {
    Bytes native;
    GameRandom game;
    WatcomRandom rand;
    uint64_t revision;
    Snapshot(app::CareerSession& session, uint64_t rev)
        : native(save_stream(session.state())), game(session.game()), rand(session.rand()), revision(rev) {}
    bool same(app::CareerSession& session, uint64_t rev) const {
        return native == save_stream(session.state()) && revision == rev &&
            game.seed() == session.game().seed() && game.calls() == session.game().calls() &&
            rand.seed() == session.rand().seed() && rand.calls() == session.rand().calls();
    }
};
struct Context {
    fs::path directory, data;
    ui::StartupFlow flow;
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision = 0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    Context(const fs::path& root, const fs::path& genuine, bool low, bool preserve = false)
        : directory(fs::temp_directory_path() / ("openbmh-preview-app-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))),
          data(root), flow(root, directory) {
        fs::create_directory(directory);
        auto input = load_savegame(genuine);
        require(input.checksum_ok, "genuine Native fixture checksum");
        auto control = GameState::from_save(SaveState(input.stream));
        // Declared Native scalar controls retain every pool/seller/roster
        // identity. They force the independently tested Source value vectors.
        for (int entry = 0; !preserve && entry < 40; ++entry) {
            const auto player = control.squad_slots.at(2928 + size_t(entry)) & 0xfff;
            if (player == 0xfff) continue;
            auto& p = control.players.at(player);
            p.w35c = (p.w35c & ~((127u << 7) | (127u << 14))) |
                (low ? 0u : (70u << 7) | (70u << 14));
            p.w364 = (p.w364 & ~((31u << 21) | 3u | (63u << 10) | (63u << 26))) |
                (low ? 0u : (10u << 21)) | 1u;
            p.w368 &= ~(63u << 7);
        }
        WatcomRandom serializer(1);
        write_savegame(directory / "AUTOSAVE.FBM", make_savegame(control, serializer));
        playback.set_muted(true);
        flow.click(214, 215);
        const auto load = flow.send_key("ENTER");
        require(load.request == ui::FlowRequest::file_action, "physical load request");
        require(serve(load).empty() && session && revision == 1 && flow.screen_name() == "desktop",
                "production controller loads the Native scalar control");
        require(save_stream(session->state()) == save_stream(control) &&
                session->game().calls() == 0 && session->rand().calls() == 0,
                "productive load preserves Native and consumes no RNG");
        const auto& items = flow.desktop()->state().items;
        const auto menu = std::find_if(items.begin(), items.end(),
            [](const auto& item) { return item.kind == 17 && !item.disabled; });
        require(menu != items.end(), "real desk exposes ordinary transfer menu");
        const auto opened = flow.click(menu->x + menu->width / 2, menu->y + menu->height / 2);
        require(serve(opened).empty() && flow.screen_name() == "transfer_market",
                "physical menu opens the production Original overview");
    }
    ~Context() { std::error_code ignored; fs::remove_all(directory, ignored); }
    std::string serve(const ui::FlowResult& action) {
        return app::serve_flow_request(flow, action, session, data, directory, 37, revision,
            playback, audio_session, false, {}, {}, {}, {}, {}, {}, {}, {}, {},
            app::PendingOfferEntryPolicy::legacy_unpresented, {},
            app::HumanBuyerMarketEntryPolicy::normal_interactive);
    }
    ui::FlowResult hover() {
        const auto request = flow.move_action(210, 28);
        require(request.request == ui::FlowRequest::submenu_action &&
                std::holds_alternative<ui::TransferMarketRequest>(request.submenu_action) &&
                std::get<ui::TransferMarketRequest>(request.submenu_action).kind ==
                    ui::TransferMarketRequestKind::preview_quote,
                "physical mouse motion emits the Source quote request");
        return request;
    }
};

void natural_snapshot_preview(const fs::path& data, const fs::path& input) {
    Context c(data, input, false, true);
    const Snapshot before(*c.session, c.revision);
    const auto identity = transfer_market_snapshot_preview(c.session->state(), 0, 1, 0);
    require(identity && identity->player == 118 && identity->source_slot >= 0,
            "normal loan identifies BURKARDT in the real borrower squad");
    const auto action = c.flow.move_action(25, 207);
    require(action.request == ui::FlowRequest::submenu_action, "physical lender snapshot hover requests preview");
    const auto request = std::get<ui::TransferMarketRequest>(action.submenu_action);
    require(request.kind == ui::TransferMarketRequestKind::preview_quote &&
            request.preview_snapshot_index == 0 && request.expected_player == 118 &&
            request.preview_source_slot == identity->source_slot && request.squad_row == identity->displayed_row,
            "snapshot preview retains owner/borrower identity separately from listing rows");
    auto expected_rand = c.session->rand(); uint32_t fee = 0, salary = 0;
    market_value(c.session->state(), identity->source_slot, 0, 100, fee, salary, 1, 0, expected_rand);
    std::printf("Natural snapshot: player=%d displayed_row=%d borrower_source_slot=%d fee=%u W=%llu\n",
        identity->player, identity->displayed_row, identity->source_slot, fee,
        static_cast<unsigned long long>(expected_rand.calls() - before.rand.calls()));
    require(c.serve(action).empty() && !c.flow.pending_transfer_market(), "real controller acknowledges snapshot hover");
    const auto json = c.flow.state_json();
    require(json.find("\"delta\":" + std::to_string(fee / 10000 * 10000)) != std::string::npos &&
            json.find("\"selected_squad\":null") != std::string::npos,
            "actual player valuation is displayed without a selectable sale row");
    require(save_stream(c.session->state()) == before.native &&
            c.session->game().calls() == before.game.calls() && c.session->rand().calls() == expected_rand.calls(),
            "snapshot preview changes no Native bytes or G and uses only normal valuation W");
    const Snapshot quoted(*c.session, c.revision);
    c.serve(c.flow.move_action(25, 207));
    c.serve(c.flow.send_key("V")); c.serve(c.flow.send_key("ENTER"));
    c.serve(c.flow.click(25, 207));
    require(quoted.same(*c.session, c.revision) && c.flow.screen_name() == "transfer_market",
            "repeat hover and snapshot sale inputs neither submit nor mutate");
    for (int failure = 0; failure < 4; ++failure) {
        auto invalid = request;
        if (failure == 0) invalid.squad_row = 0;
        if (failure == 1) invalid.preview_source_slot = int(squad_range(1).first) + identity->displayed_row;
        if (failure == 2) invalid.preview_snapshot_index = 1;
        if (failure == 3) invalid.preview_is_purchase = true;
        invalid.revision = c.revision;
        std::string error;
        require(!c.session->preview_transfer_market(invalid, c.revision, error) &&
                !error.empty() && quoted.same(*c.session, c.revision), "forged snapshot preview rolls back all state/RNG");
    }
    // Broken/ambiguous relations remain noninteractive; no guessed player quote.
    for (int failure = 0; failure < 7; ++failure) {
        auto invalid = c.session->state();
        if (failure == 0) invalid.memory.set8(::bmh::layout::ClubInfo_FormatError_RuntimeField2, 1);
        if (failure == 1) invalid.memory.set32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField, 0);
        if (failure == 2) invalid.memory.set32(::bmh::layout::FixtureTransferSecondIdentityValue, invalid.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField));
        if (failure == 3) invalid.squad_slots[size_t(identity->source_slot)] = 0x0fff;
        if (failure == 4) invalid.memory.set32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField, invalid.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField) ^ (1u << 12));
        if (failure == 5) {
            const auto range = squad_range(54);
            const size_t duplicate = range.first == size_t(identity->source_slot) ? range.first + 1 : range.first;
            invalid.squad_slots[duplicate] = 118;
        }
        if (failure == 6) {
            invalid.memory.set16(::bmh::layout::FixtureTransferSecondPlayerValue, 118); invalid.memory.set8(::bmh::layout::FixtureTransferCapacityFlag, 0);
        }
        const auto original = save_stream(invalid);
        WatcomRandom rand(37);
        const auto result = quote_transfer_market_preview_source(invalid,
            {0, 1, identity->source_slot, 118, 1, false, false, 0}, rand);
        require(!result.supported && !transfer_market_snapshot_preview(invalid, 0, 1, 0) &&
                rand.calls() == 0 && save_stream(invalid) == original,
                "unreliable snapshot relation rejects without Native/RNG writes");
        ui::TransferMarketScreen screen(data, ui::transfer_market_from_game(invalid, 1));
        require(screen.move(25, 207).kind == ui::TransferMarketRequestKind::none,
                "unreliable nonregular row has no fabricated UI quote");
    }
    c.serve(c.flow.click(310, 230, true));
    std::string error;
    require(c.session->save_game(c.directory / "SNAPSHOT.FBM", 0, false, error), "snapshot preview leaves normal saving available");
    app::CareerSession reloaded(data, 37, c.directory / "SNAPSHOT.FBM");
    require(save_stream(reloaded.state()) == before.native, "fresh save reload preserves the entire normal Native state");
}

void natural_return_editor(const fs::path& data, const fs::path& input) {
    const auto file = load_savegame(input);
    require(file.checksum_ok, "natural return input checksum");
    auto state = GameState::from_save(SaveState(file.stream));
    const auto texts = load_textd(data / "TEXTD.BLK");
    state.texts = &texts;
    require(state.date == 395 && state.memory.u8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField) == 1 &&
            state.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField) == 118 && state.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField) == 1,
            "unmodified naturally played human loan before expiry");
    bool edited = false;
    std::exception_ptr editor_failure;
    state.loan_return_screen = [](GameState&, int, const std::string&) { return 1; };
    state.loan_return_offer_screen = [&](GameState& staged, GameRandom& game, WatcomRandom& rand,
            int manager, int source_slot, int list_entry, const HumanBuyerSaleHooks&) {
        try {
        require(staged.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField) == 0 && staged.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField) == 0xfff &&
                staged.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField) == 0,
                "return removes old ledger and snapshot before the actual editor");
        const uint32_t row = ::bmh::layout::EditorList + uint32_t(list_entry) * 0x58;
        TransferOfferEditorRequest request{manager, staged.manager_teams.at(size_t(manager)),
            list_entry, source_slot, 118, staged.memory.u16(row + 8), 0};
        request.loan_return_entry = true;
        auto owner = open_transfer_offer_editor_source(staged, request, game, rand);
        require(owner.supported && owner.editable, "natural return opens the real editor");
        ui::OutgoingTransferScreen screen(data,
            app::loan_return_offer_editor_projection(staged, owner, 1, false));
        const auto edit = [&](int x, int y, const char* digits) {
            (void)screen.click(x, y, false);
            require(screen.text(digits), "return KB6 accepts physical numeric input");
            const auto action = screen.key("ENTER");
            require(action.kind == ui::OutgoingTransferRequestKind::quote_change,
                    "KB6 commit also requests a quote refresh");
            owner = app::edit_transfer_offer_from_ui(staged, owner, action, game, rand);
            require(owner.supported, "return presenter applies concrete KB6 field");
            screen.acknowledge(app::loan_return_offer_editor_projection(staged, owner, 1, false));
        };
        const uint32_t fee = owner.money_profiles[0].maximum;
        require(fee > 0 && fee < 5000000, "natural return fee calculator has its Source upper bound");
        edit(80, 124, "5000000");
        require(owner.fee == fee && owner.salary == 0 && !owner.quote_loan,
                "fee commit changes Native fee without toggling loan quote");
        edit(220, 124, "250000");
        require(owner.fee == fee && owner.salary == 200000 && !owner.quote_loan,
                "salary commit retains fee and purchase quote");
        edit(280, 140, "1");
        require(owner.years == 1 && owner.term == 12 && !owner.quote_loan,
                "term calculator remains a term event in the return presenter");
        const auto toggle = screen.click(160, 25, false, true);
        owner = app::edit_transfer_offer_from_ui(staged, owner, toggle, game, rand);
        require(owner.quote_loan && owner.fee == fee && owner.salary == 200000 && owner.term == 12,
                "only the header action toggles the quote loan class");
        screen.acknowledge(app::loan_return_offer_editor_projection(staged, owner, 1, false));
        (void)screen.key("ESC");
        require(cancel_transfer_offer_editor_source(staged, owner).supported,
                "return editor closes normally after concrete field commits");
        edited = true;
        return 0;
        } catch (...) { editor_failure = std::current_exception(); return -1; }
    };
    GameRandom game(73); WatcomRandom rand(1); MinuteReport report;
    month_change(state, rand, report, &game);
    if (editor_failure) std::rethrow_exception(editor_failure);
    if (report.unsupported) throw std::runtime_error("natural return: " + report.reason);
    require(edited && !report.unsupported && state.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField) == 0 &&
            state.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField) == 0xfff && state.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField) == 0,
            "natural return completes after the real calculator event bridge");
}
}

int main(int argc, char** argv) try {
    require(argc == 5, "usage: transfer_market_preview_app_tests DATA genuine.FBM natural-return.FBM natural-borrow.FBM");
    natural_return_editor(argv[1], argv[3]);
    natural_snapshot_preview(argv[1], argv[4]);
    for (const bool low : {false, true}) {
        Context c(argv[1], argv[2], low);
        const Snapshot before(*c.session, c.revision);
        const auto action = c.hover();
        require(before.same(*c.session, c.revision), "UI motion alone never advances Native or RNG");
        require(c.serve(action).empty() && !c.flow.pending_transfer_market(),
                "real controller acknowledges the hover quote");
        require(save_stream(c.session->state()) == before.native &&
                c.session->game().seed() == before.game.seed() &&
                c.session->game().calls() == before.game.calls(), "preview preserves all Native and G state");
        require(c.session->rand().calls() == before.rand.calls() + uint64_t(low) &&
                c.revision == before.revision + uint64_t(low) &&
                c.flow.desktop()->state().revision == c.revision,
                "optional one-W fallback publishes exactly one coherent revision");
        const auto request = std::get<ui::TransferMarketRequest>(action.submenu_action);
        require(c.flow.state_json().find(low ? "-50000" : "-1020000") != std::string::npos,
                "Source scalar rounded purchase delta reaches the actual visible quote");
        const Snapshot quoted(*c.session, c.revision);
        for (int i = 0; i < 3; ++i) {
            (void)c.flow.render();
            const auto repeated = c.flow.move_action(210, 28);
            require(repeated.request == ui::FlowRequest::none && quoted.same(*c.session, c.revision),
                    "repeated hover and rendering never reroll the selected quote");
        }
        require(c.serve(action).empty() && quoted.same(*c.session, c.revision),
                "replayed already-consumed controller request cannot reroll");
        const auto marked = c.flow.click(210, 28);
        require(marked.request == ui::FlowRequest::none && quoted.same(*c.session, c.revision),
                "marking the already hovered Source row does not trigger another quote");
        c.flow.move_action(160, 180);
        const auto second = c.hover();
        auto bad = std::get<ui::TransferMarketRequest>(second.submenu_action);
        bad.preview_seller ^= 1;
        std::string error;
        require(!c.session->preview_transfer_market(bad, c.revision, error) &&
                !error.empty() && quoted.same(*c.session, c.revision),
                "stale seller rejects before any live Native/W/G/revision change");
        bad = std::get<ui::TransferMarketRequest>(second.submenu_action);
        --bad.revision;
        require(!c.session->preview_transfer_market(bad, c.revision, error) &&
                quoted.same(*c.session, c.revision), "stale revision rejects before valuation");
        require(request.preview_source_slot >= 2928 && request.preview_source_slot < 2968,
                "real quote uses the projected ordinary source slot");
    }
    {
        Context c(argv[1], argv[2], false);
        const auto hovered = c.hover();
        c.serve(hovered);
        c.flow.click(210, 28);
        const Snapshot before(*c.session, c.revision);
        const auto opened = c.flow.send_key("B");
        require(opened.request == ui::FlowRequest::submenu_action,
                "marked ordinary player requests the Original outgoing editor");
        c.serve(opened);
        require(c.flow.screen_name() == "outgoing_transfer" && c.session->transfer_offer_owner() &&
                c.session->game().calls() == before.game.calls() + 1 && c.revision == before.revision + 1,
                "real controller opens KB31 with the owned Source G draw and coherent revision");
        const auto owner = *c.session->transfer_offer_owner();
        require(owner.editable && owner.bid_slot >= 0, "Source picks the real editable raw row");
        const uint32_t list = ::bmh::layout::EditorList + uint32_t(owner.request.list_entry) * 0x58;
        const uint32_t bid = uint32_t(owner.bid_slot);
        const auto buyer = c.session->state().memory.u16(list + bid * 2);
        const auto term = c.session->state().memory.u8(list + 0x34 + bid);
        const auto g_calls = c.session->game().calls();
        const auto focused = c.flow.click(40, 123);
        require(focused.request == ui::FlowRequest::none && c.flow.text("10000"),
                "physical fee rectangle opens the Source KB6 calculator locally");
        const auto committed = c.flow.send_key("ENTER");
        require(committed.request == ui::FlowRequest::submenu_action && c.flow.pending_transfer_offer(),
                "KB6 commit produces the typed Source field event");
        c.serve(committed);
        require(!c.flow.pending_transfer_offer() && c.flow.screen_name() == "outgoing_transfer" &&
                c.session->state().memory.u32(list + 0x1c + bid * 4) == 10000 &&
                c.session->state().memory.u16(list + bid * 2) == buyer &&
                c.session->state().memory.u8(list + 0x34 + bid) == term &&
                c.session->game().calls() == g_calls,
                "Source field event immediately writes the Native fee without buyer/term/G changes");
        const Snapshot edited(*c.session, c.revision);
        c.serve(committed);
        require(edited.same(*c.session, c.revision), "consumed editor commit cannot be replayed");
        std::string error;
        require(!c.session->save_game(c.directory / "BLOCKED.FBM", 0, false, error) &&
                !fs::exists(c.directory / "BLOCKED.FBM") && edited.same(*c.session, c.revision),
                "saving cannot escape the active Source editor modal");
        const auto canceled = c.flow.send_key("ESC");
        c.serve(canceled);
        require(c.flow.screen_name() == "transfer_market" && !c.session->transfer_offer_owner() &&
                edited.same(*c.session, c.revision),
                "Source cancel returns to the overview while retaining every committed Native edit");
        require(c.session->save_game(c.directory / "EDITED.FBM", 0, false, error),
                "completed Source cancel makes the ordinary Native state saveable again");
        app::CareerSession reloaded(c.data, 37, c.directory / "EDITED.FBM");
        require(save_stream(reloaded.state()) == save_stream(c.session->state()) &&
                reloaded.state().memory.u32(list + 0x1c + bid * 4) == 10000,
                "fresh productive reload retains the Source empty/own-row fee edit");
    }
    {
        Context c(argv[1], argv[2], true);
        const Snapshot before(*c.session, c.revision);
        const auto action = c.hover();
        require(draw_hook == nullptr, "exclusive draw observer ownership");
        draw_hook = [](int) { throw std::runtime_error("controlled Source fallback interruption"); };
        c.serve(action);
        draw_hook = nullptr;
        require(before.same(*c.session, c.revision) && !c.flow.pending_transfer_market(),
                "controller rolls back the interrupted staged W fallback and consumes rejection once");
    }
    std::puts("PASS Original market hover input/Source quote/revision/RNG/rollback controller integration");
    return 0;
} catch (const std::exception& e) {
    draw_hook = nullptr;
    std::fprintf(stderr, "transfer_market_preview_app_tests: %s\n", e.what());
    return 1;
}
