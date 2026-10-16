// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "app/desktop_actions.hpp"
#include "game/save.hpp"
#include "game/messages.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>

using namespace bmh;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Snapshot {
    Bytes native;
    GameRandom game;
    WatcomRandom rand;
    uint64_t revision;
    std::map<uint32_t,uint8_t> runtime;
    Snapshot(app::CareerSession& session, uint64_t rev)
        : native(save_stream(session.state())), game(session.game()), rand(session.rand()),
          revision(rev), runtime(session.club_modal_runtime_checkpoint()) {}
    void unchanged(app::CareerSession& session, uint64_t rev) const {
        check(save_stream(session.state()) == native && rev == revision &&
            session.game().seed() == game.seed() && session.game().calls() == game.calls() &&
            session.rand().seed() == rand.seed() && session.rand().calls() == rand.calls() &&
            session.club_modal_runtime_checkpoint() == runtime,
            "shared action rejection preserves complete Native/G/W/revision/runtime");
    }
};

void reject_ack(app::CareerSession& session) {
    ++session.state().date;
    session.game().range(0, 10);
    session.rand().rand();
    throw std::runtime_error("controlled frontend ACK rejection");
}

void settings(const std::filesystem::path& data, const std::filesystem::path& save) {
    app::CareerSession session(data, 0x10351a7u, save);
    uint64_t revision = 1;
    app::DeskSettingsActions actions(session);
    std::string error;
    const auto desk = session.desktop_projection(revision);
    const auto request = app::DesktopActions::menu(desk, 19);
    const Snapshot before(session, revision);
    const auto old_scope = session.desk_settings_checkpoint(revision).next_scope;
    auto stale = request;
    ++stale.revision;
    check(!actions.open(stale, revision, error), "shared settings rejects a stale observed desktop entry");
    before.unchanged(session, revision);
    bool acknowledged = false;
    try {
        actions.open(request, revision, error, [&](app::CareerSession::DeskSettingsProjection&) {
            acknowledged = session.desk_settings_active();
            reject_ack(session);
        });
    } catch (const std::runtime_error&) {}
    check(acknowledged && !session.desk_settings_active() &&
        session.desk_settings_checkpoint(revision).next_scope == old_scope,
        "failed settings open ACK restores owner and next scope");
    before.unchanged(session, revision);

    const auto opened = actions.open(request, revision, error);
    check(bool(opened), error.c_str());
    const Snapshot owned(session, revision);
    acknowledged = false;
    try {
        actions.close(opened->owner.sequence, opened->owner.revision, request, revision, error,
            [&](app::CareerSession::DeskSettingsDispatch& result) {
                acknowledged = result.closed && !session.desk_settings_active();
                reject_ack(session);
            });
    } catch (const std::runtime_error&) {}
    check(acknowledged && session.desk_settings_active(), "failed settings close ACK restores its real owner");
    owned.unchanged(session, revision);
    const auto closed = actions.close(opened->owner.sequence, opened->owner.revision, request, revision, error);
    check(closed && closed->closed && !session.desk_settings_active(),
        "settings retry accepts the restored owner and its original RNG identity");
}

void balance(const std::filesystem::path& data, const std::filesystem::path& save) {
    app::CareerSession session(data, 0x103ba1u, save);
    uint64_t revision = 1;
    app::BalanceActions actions(session);
    std::string error;
    const auto desk = session.desktop_projection(revision);
    const auto request = app::DesktopActions::menu(desk, 28);
    const Snapshot before(session, revision);
    const auto next_scope = session.balance_checkpoint().next_scope;
    auto stale = request;
    ++stale.revision;
    check(!actions.open(stale, revision, Image(320,240,0), error), "shared balance rejects stale observed entry");
    before.unchanged(session, revision);
    bool acknowledged = false;
    try {
        actions.open(request, revision, Image(320,240,0), error, [&](ui::BalanceState&) {
            acknowledged = session.balance_active();
            reject_ack(session);
        });
    } catch (const std::runtime_error&) {}
    check(acknowledged && !session.balance_active() && session.balance_checkpoint().next_scope == next_scope,
        "failed balance open ACK restores its draft owner and scope");
    before.unchanged(session, revision);

    auto draft = actions.open_draft(desk.manager_slot);
    while (draft.view != BalanceView::future) actions.cycle_draft(draft);
    check(actions.edit_draft(draft, 0, 100000) == BalanceEditStatus::changed,
        "shared console balance draft retains its existing edit operation");
    before.unchanged(session, revision);
    bool stored = false;
    try {
        actions.apply(revision, [&] {
            (void)actions.close_draft(draft, revision);
            stored = revision == before.revision + 1;
            reject_ack(session);
            return true;
        });
    } catch (const std::runtime_error&) {}
    check(stored, "console balance close still stores once and consumes exactly one revision");
    before.unchanged(session, revision);
}

void other_games(const std::filesystem::path& data, const std::filesystem::path& save) {
    app::CareerSession session(data, 0x10305u, save);
    uint64_t revision = 1;
    app::OtherGamesActions actions(session);
    std::string error;
    const auto request = app::DesktopActions::menu(session.desktop_projection(revision), 5);
    const Snapshot before(session, revision);
    const auto next_scope = session.other_games_checkpoint().next_scope;
    auto stale = request;
    ++stale.revision;
    check(!actions.open(stale, revision, Image(320,240,0), error), "shared Other Games rejects stale entry");
    before.unchanged(session, revision);
    bool acknowledged = false;
    try {
        actions.open(request, revision, Image(320,240,0), error, [&](ui::OtherGamesState&) {
            acknowledged = session.other_games_active();
            reject_ack(session);
        });
    } catch (const std::runtime_error&) {}
    check(acknowledged && !session.other_games_active() && session.other_games_checkpoint().next_scope == next_scope,
        "failed Other Games open ACK restores owner and scope");
    before.unchanged(session, revision);

    const auto page = actions.open(request, revision, Image(320,240,0), error);
    check(bool(page), error.c_str());
    const Snapshot owned(session, revision);
    const ui::OtherGamesRequest close{page->identity, ui::OtherGamesAction::close, -1};
    acknowledged = false;
    try {
        actions.dispatch(close, revision, error, {}, [&](app::CareerSession::OtherGamesDispatch& result) {
            acknowledged = result.closed;
            reject_ack(session);
        });
    } catch (const std::runtime_error&) {}
    check(acknowledged && session.other_games_active(), "Other Games close ACK failure restores its owner");
    owned.unchanged(session, revision);
    const auto closed = actions.dispatch(close, revision, error);
    check(closed && closed->closed && !session.other_games_active(), "Other Games restored close can be retried once");
}
void top_scorers(const std::filesystem::path& data, const std::filesystem::path& save) {
    app::CareerSession session(data, 0x10320u, save);
    uint64_t revision = 1;
    app::TopScorersActions actions(session);
    std::string error;
    const auto request = app::DesktopActions::menu(session.desktop_projection(revision), 20);
    const Snapshot before(session, revision);
    const auto next_scope = session.top_scorers_checkpoint().next_scope;
    auto stale = request;
    ++stale.revision;
    check(!actions.open(stale, revision, Image(320,240,0), error), "shared Top Scorers rejects stale entry");
    before.unchanged(session, revision);
    bool acknowledged = false;
    try {
        actions.open(request, revision, Image(320,240,0), error, [&](ui::TopScorersState&) {
            acknowledged = session.top_scorers_active();
            reject_ack(session);
        });
    } catch (const std::runtime_error&) {}
    check(acknowledged && !session.top_scorers_active() && session.top_scorers_checkpoint().next_scope == next_scope,
        "failed Top Scorers open ACK restores owner and scope");
    before.unchanged(session, revision);

    const auto page = actions.open(request, revision, Image(320,240,0), error);
    check(page && revision == before.revision + 1, "shared Top Scorers opening advances revision exactly once");
    const Snapshot owned(session, revision);
    const ui::TopScorersRequest close{page->identity, ui::TopScorersAction::close, page->metric, page->scope};
    acknowledged = false;
    try {
        actions.dispatch(close, revision, error, [&](app::CareerSession::TopScorersDispatch& result) {
            acknowledged = result.closed;
            reject_ack(session);
        });
    } catch (const std::runtime_error&) {}
    check(acknowledged && session.top_scorers_active(), "Top Scorers close ACK failure restores its owner");
    owned.unchanged(session, revision);
    const auto closed = actions.dispatch(close, revision, error);
    check(closed && closed->closed && !session.top_scorers_active(), "Top Scorers restored close can be retried once");
}
void opportunities(const std::filesystem::path& data, const std::filesystem::path& save) {
    app::CareerSession session(data, 0x103adu, save);
    uint64_t revision = 1;
    app::OpportunitiesActions actions(session);
    std::string error;
    const auto request = app::DesktopActions::menu(session.desktop_projection(revision), 23);
    const Snapshot before(session, revision);
    const auto next_scope = session.opportunities_checkpoint().next_scope;
    ui::AnalyticsRequest preference;
    preference.owner = request;
    preference.manager_slot = request.manager_slot;
    preference.metric = 0;
    preference.expected_flags = session.state().memory.u8(::bmh::layout::Flags + uint32_t(request.manager_slot) * 3);
    preference.target_flags = uint8_t(preference.expected_flags ^ 1);
    bool preference_ack = false;
    try {
        actions.preference(preference, revision, error, [&] {
            preference_ack = revision == before.revision + 1;
            reject_ack(session);
        });
    } catch (const std::runtime_error&) {}
    check(preference_ack, "shared analytics preference preserves its existing single revision");
    before.unchanged(session, revision);
    auto stale = request;
    ++stale.revision;
    check(!actions.open(stale, revision, Image(320,240,0), error), "shared Opportunities rejects stale entry");
    before.unchanged(session, revision);
    bool acknowledged = false;
    try {
        actions.open(request, revision, Image(320,240,0), error, false, -1, [&](ui::OpportunitiesState&) {
            acknowledged = session.opportunities_active();
            reject_ack(session);
        });
    } catch (const std::runtime_error&) {}
    check(acknowledged && !session.opportunities_active() && session.opportunities_checkpoint().next_scope == next_scope,
        "failed Opportunities open ACK restores owner and scope");
    before.unchanged(session, revision);

    const auto page = actions.open(request, revision, Image(320,240,0), error);
    check(page && revision == before.revision + 1, "shared Opportunities opening advances revision exactly once");
    const Snapshot owned(session, revision);
    const auto close = app::OpportunitiesActions::close_request(*page);
    acknowledged = false;
    try {
        actions.dispatch(close, revision, error, [&](app::CareerSession::OpportunitiesDispatch& result) {
            acknowledged = result.closed;
            reject_ack(session);
        });
    } catch (const std::runtime_error&) {}
    check(acknowledged && session.opportunities_active(), "Opportunities close ACK failure restores its owner");
    owned.unchanged(session, revision);
    const auto closed = actions.dispatch(close, revision, error);
    check(closed && closed->closed && !session.opportunities_active(), "Opportunities restored close can be retried once");
}
void manager_join(const std::filesystem::path& data, const std::filesystem::path& save) {
    app::CareerSession session(data, 0x10329u, save);
    uint64_t revision = 1;
    app::ManagerJoinActions actions(session);
    const Snapshot before(session, revision);
    std::string error;
    bool acknowledged = false;
    check(!actions.commit(revision, error, [&](ui::CareerInitializationResult&) { acknowledged = true; }) && !acknowledged,
        "shared manager join rejects a commit without completed selection before any ACK");
    before.unchanged(session, revision);
    check(!actions.apply(revision, [&] {
        ++session.state().date;
        session.game().range(0,10);
        session.rand().rand();
        ++revision;
        return false;
    }), "shared manager join rolls back a rejected partial operation");
    before.unchanged(session, revision);
}

void mailbox(const std::filesystem::path& data, const std::filesystem::path& save) {
    app::CareerSession session(data, 0x10344u, save);
    uint64_t revision = 1;
    app::MailboxActions actions(session);
    const int manager = session.desktop_projection(revision).manager_slot;
    // Controlled in-memory fixture using the real Core writer; the captured FBM
    // stays unchanged and is not claimed to contain this test message.
    post_composed(session.state(), manager, "COMMON ROLLBACK");
    const auto observed = ui::mailbox_state_from_game(session.state(), manager, revision);
    const auto message = std::find_if(observed.messages.begin(), observed.messages.end(),
        [](const ui::MailboxMessage& m) { return m.text == "COMMON ROLLBACK"; });
    check(message != observed.messages.end(), "Core-posted controlled mailbox message is visible");
    const auto request = app::MailboxActions::request(manager, revision, ui::MailboxActionKind::delete_for_manager, *message);
    const Snapshot before(session, revision);
    std::string error;
    auto stale = request;
    ++stale.revision;
    check(!actions.dispatch(stale, revision, error), "shared mailbox rejects a stale observed message action");
    before.unchanged(session, revision);
    bool acknowledged = false;
    try {
        actions.dispatch(request, revision, error, [&] { acknowledged = true; reject_ack(session); });
    } catch (const std::runtime_error&) {}
    check(acknowledged, "real mailbox deletion reaches the shared acknowledgement boundary");
    before.unchanged(session, revision);
    acknowledged = false;
    try {
        actions.close({manager,revision}, revision, error, [&] { acknowledged = true; reject_ack(session); });
    } catch (const std::runtime_error&) {}
    check(acknowledged, "real mailbox close reaches its shared acknowledgement boundary");
    before.unchanged(session, revision);
    check(actions.dispatch(request, revision, error), "restored mailbox message can be deleted on retry");
}

void menu_layout(const std::filesystem::path& data, const std::filesystem::path& save) {
    app::CareerSession session(data, 0x10372u, save);
    uint64_t revision = 1;
    app::MenuLayoutActions actions(session);
    const int manager = session.desktop_projection(revision).manager_slot;
    const auto directory = std::filesystem::temp_directory_path() /
        ("openbmh-shared-layout-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(std::filesystem::create_directory(directory), "isolated shared layout test directory");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } cleanup{directory};
    const ui::SaveLoadRequest request{ui::SaveLoadAction::save, manager, revision, directory/"TEST.MEN", false,
        ui::SaveLoadFileKind::menu_layout, 53};
    const Snapshot before(session, revision);
    std::string error;
    auto stale = request;
    ++stale.revision;
    check(!actions.dispatch(stale, revision, error), "shared layout rejects a stale file request");
    before.unchanged(session, revision);
    bool acknowledged = false;
    try {
        actions.dispatch(request, revision, error, [&] {
            acknowledged = std::filesystem::file_size(request.path) == 0x360;
            reject_ack(session);
        });
    } catch (const std::runtime_error&) {}
    check(acknowledged, error.empty() ? "real layout save reaches its shared ACK" : error.c_str());
    before.unchanged(session, revision);
    // As before, native rollback does not undo the already written .MEN file.
    auto retry = request;
    retry.overwrite = true;
    check(actions.dispatch(retry, revision, error) && revision == before.revision + 1,
        "layout retry uses the existing overwrite confirmation and advances once");
}

void environment(const std::filesystem::path& data, const std::filesystem::path& save) {
    app::CareerSession session(data, 0x10324u, save);
    uint64_t revision = 1;
    app::EnvironmentActions actions(session);
    const auto observed = ui::environment_from_game(session.state(), revision);
    const int level = (observed.values.level[kCoTrainer] + 1) % 7;
    const auto request = app::EnvironmentActions::request(observed, kCoTrainer, level);
    const Snapshot before(session, revision);
    std::string error;
    auto stale = request;
    ++stale.revision;
    check(!actions.set(stale, revision, error), "shared environment rejects a stale observed staff change");
    before.unchanged(session, revision);
    bool acknowledged = false;
    try {
        actions.set(request, revision, error, {}, [&](bool changed) {
            acknowledged = changed && revision == before.revision + 1;
            reject_ack(session);
        });
    } catch (const std::runtime_error&) {}
    check(acknowledged, "real staff change reaches the shared acknowledgement with one revision");
    before.unchanged(session, revision);
    const auto applied = actions.set_console(observed, kCoTrainer, level, revision, error);
    check(applied == EnvironmentResult::changed && revision == before.revision + 1,
        "shared console environment retains its actual staff write and one revision");
}

void reschedule(const std::filesystem::path& data, const std::filesystem::path& save) {
    app::CareerSession session(data, 0x10307u, save);
    uint64_t revision = 1;
    app::RescheduleActions actions(session);
    const Snapshot before(session, revision);
    ui::RescheduleAction request;
    request.owner = app::DesktopActions::menu(session.desktop_projection(revision), 7);
    ++request.owner.revision;
    check(actions.dispatch(request, revision) == RescheduleStatus::invalid_match,
        "shared reschedule rejects a stale observed calendar owner");
    before.unchanged(session, revision);
    check(actions.move(-1, {}, -1, revision) == RescheduleStatus::invalid_manager,
        "shared console reschedule rejects an invalid manager without a revision");
    before.unchanged(session, revision);
    check(!actions.apply(revision, [&] {
        ++session.state().date;
        session.game().range(0,10);
        session.rand().rand();
        ++revision;
        return false;
    }), "shared reschedule rejects a partial operation");
    before.unchanged(session, revision);
}

void contracts(const std::filesystem::path& data, const std::filesystem::path& save) {
    app::CareerSession session(data, 0x10327u, save);
    uint64_t revision = 1;
    app::ContractActions actions(session);
    const auto observed = ui::contracts_from_game(session.state(), session.desktop_projection(revision).manager_slot, revision);
    const auto player = std::find_if(observed.players.begin(), observed.players.end(),
        [](const ui::ContractPlayer& p) { return !p.offer_blocked; });
    check(player != observed.players.end(), "captured squad has a player for an actual demands query");
    const auto request = app::ContractActions::demands(observed, *player, 1);
    const Snapshot before(session, revision);
    std::string error;
    ui::ContractActionResult result;
    auto stale = request;
    ++stale.revision;
    check(!actions.dispatch(stale, revision, result, error), "shared contracts reject a stale observed demands query");
    before.unchanged(session, revision);
    bool acknowledged = false;
    try {
        actions.dispatch(request, revision, result, error, [&] {
            acknowledged = result.quote_id != 0;
            reject_ack(session);
        });
    } catch (const std::runtime_error&) {}
    check(acknowledged, "actual contract quote reaches its shared acknowledgement boundary");
    before.unchanged(session, revision);
    const auto retry = app::ContractActions::demands(observed, *player, 2);
    check(actions.dispatch(retry, revision, result, error) && result.quote_id != 0,
        "contracts preserve the existing new-request-ID retry and quote cleanup policy");
}

void friendly(const std::filesystem::path& data, const std::filesystem::path& save) {
    app::CareerSession session(data, 0x10306u, save);
    uint64_t revision = 1;
    app::FriendlyActions actions(session);
    const int manager = session.desktop_projection(revision).manager_slot;
    FriendlyBookingQuote quote;
    int selected_day = -1;
    for (int opponent = 0; opponent < 138 && selected_day < 0; ++opponent) {
        const auto candidate = actions.quote(manager, opponent, false);
        if (candidate.status != FriendlyQuoteStatus::quoted || candidate.opponent_manager >= 0) continue;
        for (int day = 0; day <= 365; ++day) {
            const auto status = actions.date_status(candidate, uint16_t(day));
            if (status == FriendlyDateStatus::available || status == FriendlyDateStatus::available_with_calendar_event) {
                quote = candidate;
                selected_day = day;
                break;
            }
        }
    }
    check(selected_day >= 0, "captured career offers an actual AI friendly and free date");
    ui::FriendlyMatchRequest request;
    request.kind = ui::FriendlyRequestKind::book;
    request.request_id = 1; request.quote_id = 1; request.revision = revision;
    request.manager_slot = quote.manager; request.runtime_team = quote.manager_team;
    request.opponent = quote.opponent; request.manager_home = quote.manager_home;
    request.home_team = quote.home_team; request.away_team = quote.away_team;
    request.payer_manager = quote.payer_manager; request.opponent_manager = quote.opponent_manager;
    request.cost = quote.cost; request.planner_count = quote.planner_count; request.season_day = selected_day;
    request.accept_cost = true; request.approving_manager = quote.payer_manager;
    request.accept_opponent = false; request.consenting_manager = quote.opponent_manager;
    const Snapshot before(session, revision);
    auto stale = request;
    ++stale.revision;
    check(!actions.book_club(stale, revision).applied, "shared friendly booking rejects a stale observed request");
    before.unchanged(session, revision);
    bool acknowledged = false;
    try {
        actions.book_club(request, revision, [&](const ui::FriendlyBookingAck& ack) {
            acknowledged = ack.revision == before.revision + 1 && revision == before.revision &&
                ack.planner_count == request.planner_count + 2;
            reject_ack(session);
        });
    } catch (const std::runtime_error&) {}
    check(acknowledged, "real friendly booking keeps the old revision during ACK and supplies the two new plan rows");
    before.unchanged(session, revision);
    const auto retry = actions.book_console(quote, uint16_t(selected_day), false, quote.opponent_manager,
        true, quote.payer_manager, revision);
    check(retry.status == FriendlyBookingStatus::booked && revision == before.revision + 1,
        "shared console friendly retry books the restored quote and consumes one revision");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("usage: shared_desktop_actions_tests DATA SAVE");
        settings(argv[1], argv[2]);
        balance(argv[1], argv[2]);
        other_games(argv[1], argv[2]);
        top_scorers(argv[1], argv[2]);
        opportunities(argv[1], argv[2]);
        manager_join(argv[1], argv[2]);
        mailbox(argv[1], argv[2]);
        menu_layout(argv[1], argv[2]);
        environment(argv[1], argv[2]);
        reschedule(argv[1], argv[2]);
        contracts(argv[1], argv[2]);
        friendly(argv[1], argv[2]);
        std::puts("shared desktop application boundaries OK");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
    return 0;
}
