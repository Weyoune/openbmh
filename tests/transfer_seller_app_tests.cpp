// SPDX-License-Identifier: GPL-3.0-only
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include "app/career_session.hpp"
#include "app/window_actions.hpp"
#include "game/save.hpp"
#include "ui/incoming_transfer.hpp"

using namespace bmh;
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Snapshot {
    Bytes native; GameRandom game; WatcomRandom rand; uint64_t revision;
    Snapshot(app::CareerSession& s, uint64_t r) : native(save_stream(s.state())), game(s.game()), rand(s.rand()), revision(r) {}
    bool same(app::CareerSession& s, uint64_t r) const {
        return native == save_stream(s.state()) && revision == r && game.seed() == s.game().seed() &&
            game.calls() == s.game().calls() && rand.seed() == s.rand().seed() && rand.calls() == s.rand().calls();
    }
};
ui::IncomingTransferDecisionState panel(const GameState& state, const TransferOfferListView& offers) {
    ui::IncomingTransferDecisionView view;
    view.valid = offers.valid; view.list_entry = offers.list_entry; view.player = offers.player;
    view.seller = offers.seller; view.seller_manager = offers.seller_manager; view.viewer_manager = offers.seller_manager;
    view.asking_fee = offers.asking_fee; view.asking_monthly_salary = offers.asking_monthly_salary;
    for (size_t n = 0; n < 4; ++n) {
        const auto& source = offers.offers[n]; auto& row = view.offers[n];
        row.bid_index = source.bid_index; row.buyer = source.buyer; row.buyer_manager = source.buyer_manager;
        row.present = source.present; row.eligible = source.eligible; row.transaction_supported = source.transaction_supported;
        row.fee = source.fee; row.monthly_salary = source.monthly_salary; row.contract_months = source.contract_months;
        row.fee_reference = source.fee_reference; row.salary_reference = source.salary_reference;
        row.loan = source.loan; row.bonuses = source.bonuses;
    }
    return ui::incoming_transfer_decision_from_game(state, view);
}
}
int main(int argc, char** argv) try {
    if (argc != 3 && argc != 4) return 2;
    const std::filesystem::path data = argv[1];
    app::CareerSession session(data, 37, argv[2]); uint64_t revision = 1;
    const auto base = session.state(); const auto manager = base.memory.u8(layout::ActiveManager);
    const auto range = squad_range(base.manager_teams.at(manager));
    int entry = -1, player = -1, chosen = -1;
    // Produce offers through real listing/bid routines on the supplied career.
    // No bidder/amount/term/table bytes are fabricated for this integration test.
    for (size_t row = 0; row < range.count && chosen < 0; ++row) {
        if ((base.squad_slots[range.first + row] & 0xfff) == 0xfff) continue;
        auto listed = base; auto w = session.rand(); auto game = session.game(); MinuteReport listing_report;
        const auto listing = list_manager_player(listed, manager, int(row), w, listing_report);
        if (listing.status != TransferActionStatus::accepted || listing_report.unsupported) continue;
        for (int attempt = 0; attempt < 4 && chosen < 0; ++attempt) {
            MinuteReport report;
            transfer_bid(listed, 0xb70 + listing.list_entry,
                layout::EditorList + uint32_t(listing.list_entry) * 0x58, false, -1, game, w, report);
            if (report.unsupported) break;
            const auto offers = project_transfer_offer_list(listed, listing.list_entry);
            for (const auto& offer : offers.offers) {
                if (!offer.present || offer.loan || !offer.eligible) continue;
                auto expected = listed; auto expected_rand = w; MinuteReport expected_report;
                const auto result = accept_manager_transfer_offer(expected, manager, listing.list_entry,
                    listing.player, offer.bid_index, expected_rand, expected_report);
                if (result.status != TransferActionStatus::accepted || expected_report.unsupported) continue;
                session.state() = listed; session.game() = game; session.rand() = w;
                chosen = offer.bid_index; entry = listing.list_entry; player = listing.player; break;
            }
        }
    }
    require(chosen >= 0, "supplied career produces a natural eligible seller offer");
    if (argc == 4) {
        std::string save_error;
        require(session.save_game(argv[3], 0, false, save_error), "natural offer evidence saves through the session writer");
    }
    const auto before = Snapshot(session, revision);
    ui::TransferMarketRequest request;
    request.kind = ui::TransferMarketRequestKind::review_offers;
    request.manager_slot = manager; request.runtime_club = session.state().manager_teams[manager];
    request.revision = revision; request.bid.manager = manager; request.bid.list_entry = entry;
    request.bid.source_slot = 0xb70 + entry; request.bid.player = player;
    std::string error; int presentations = 0;
    auto choose = [&](const GameState& state, const TransferOfferListView& offers) {
        ++presentations; ui::IncomingTransferScreen screen(data, panel(state, offers));
        require(screen.render().width == 320, "actual incoming modal renders natural offer rows");
        const auto selected = screen.click(10, 150 + chosen * 17);
        require(selected && *selected == chosen, "actual incoming row input returns its raw bidder identity");
        return *selected;
    };
    auto cancel = [&](const GameState& state, const TransferOfferListView& offers) {
        ui::IncomingTransferScreen screen(data, panel(state, offers));
        const auto answer = screen.click(200, 100, true);
        require(answer && *answer == -1, "actual incoming modal right click cancels early review"); return *answer;
    };
    const auto canceled = app::TransferActions(session).market(request, {}, revision, error,
        app::HumanBuyerMarketEntryPolicy::normal_interactive, {}, cancel);
    if (!canceled) std::fprintf(stderr, "early cancel rejected: %s\n", error.c_str());
    require(canceled && !canceled->changed && before.same(session, revision), "early cancel preserves complete state and both RNGs");
    for (int malformed = 0; malformed < 3; ++malformed) {
        auto stale = request;
        if (malformed == 0) ++stale.bid.player;
        if (malformed == 1) --stale.revision;
        if (malformed == 2) ++stale.bid.source_slot;
        require(!app::TransferActions(session).market(stale, {}, revision, error,
                    app::HumanBuyerMarketEntryPolicy::normal_interactive, {}, choose) &&
                before.same(session, revision) && presentations == 0, "stale review rejects before presenter and mutation");
    }
    bool rolled_back = false;
    try {
        (void)app::TransferActions(session).market(request, {}, revision, error,
            app::HumanBuyerMarketEntryPolicy::normal_interactive,
            [](const app::TransferActions::MarketDispatch&) { throw std::runtime_error("interrupted acknowledgement"); }, choose);
    } catch (const std::runtime_error&) { rolled_back = true; }
    require(rolled_back && before.same(session, revision), "interrupted acknowledgement rolls back seller transfer and RNG");
    const auto accepted = app::TransferActions(session).market(request, {}, revision, error,
        app::HumanBuyerMarketEntryPolicy::normal_interactive, {}, choose);
    require(accepted && accepted->changed && accepted->action.status == TransferActionStatus::accepted &&
            revision == before.revision + 1 && (session.state().squad_slots.at(size_t(0xb70 + entry)) & 0xfff) == 0xfff,
            "qualified natural offer settles exactly once through shared session transaction");
    const Snapshot settled(session, revision);
    require(!app::TransferActions(session).market(request, {}, revision, error,
                app::HumanBuyerMarketEntryPolicy::normal_interactive, {}, choose) && settled.same(session, revision),
            "replaying accepted seller request is inert");
    std::printf("PASS natural seller offer: player=%d entry=%d raw_bid=%d; real modal cancel/accept, stale guards, acknowledgement rollback\n", player, entry, chosen);
    return 0;
} catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
