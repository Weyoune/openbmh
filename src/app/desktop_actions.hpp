// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "app/career_session.hpp"

namespace bmh::app {

class DesktopActions {
public:
    static ui::DeskRequest menu(const ui::DeskState& observed, int kind);
};

class DeskSettingsActions {
public:
    explicit DeskSettingsActions(CareerSession& session) : session_(session) {}
    bool apply(uint64_t& revision, const std::function<bool()>& action);
    using OpenAccept = std::function<void(CareerSession::DeskSettingsProjection&)>;
    using Accept = std::function<void(CareerSession::DeskSettingsDispatch&)>;
    std::optional<CareerSession::DeskSettingsProjection> open(const ui::DeskRequest&,
        uint64_t& revision, std::string& error, const OpenAccept& = {});
    std::optional<CareerSession::DeskSettingsDispatch> write(uint64_t owner_sequence,
        const ui::SettingsWriteRequest&, uint64_t& revision, std::string& error, const Accept& = {});
    std::optional<CareerSession::DeskSettingsDispatch> file(uint64_t owner_sequence,
        const ui::SaveLoadRequest&, uint64_t& revision, std::string& error, const Accept& = {});
    std::optional<CareerSession::DeskSettingsDispatch> close(uint64_t owner_sequence,
        uint64_t owner_revision, const ui::DeskRequest& observed,
        uint64_t& revision, std::string& error, const Accept& = {});
    bool edit(const ui::SettingsState& observed, int row, int column, ui::SettingsEdit,
        uint64_t& revision, std::string& error);
private:
    std::optional<CareerSession::DeskSettingsDispatch> dispatch(const CareerSession::DeskSettingsAction&,
        uint64_t& revision, std::string& error, const Accept&);
    CareerSession& session_;
};

class BalanceActions {
public:
    explicit BalanceActions(CareerSession& session) : session_(session) {}
    bool apply(uint64_t& revision, const std::function<bool()>& action);
    using OpenAccept = std::function<void(ui::BalanceState&)>;
    using Accept = std::function<void(CareerSession::BalanceDispatch&)>;
    std::optional<ui::BalanceState> open(const ui::DeskRequest&, uint64_t& revision,
        const Image& underlay, std::string& error, const OpenAccept& = {});
    std::optional<CareerSession::BalanceDispatch> dispatch(const ui::BalanceRequest&,
        uint64_t& revision, std::string& error, const Accept& = {});
    BalancePlanDraft open_draft(int manager) const;
    void cycle_draft(BalancePlanDraft&) const;
    void reset_draft(BalancePlanDraft&) const;
    BalanceEditStatus edit_draft(BalancePlanDraft&, int row, int32_t value) const;
    bool close_draft(const BalancePlanDraft&, uint64_t& revision);
private:
    CareerSession& session_;
};

class OtherGamesActions {
public:
    explicit OtherGamesActions(CareerSession& session) : session_(session) {}
    bool apply(uint64_t& revision, const std::function<bool()>& action);
    using OpenAccept = std::function<void(ui::OtherGamesState&)>;
    using Accept = std::function<void(CareerSession::OtherGamesDispatch&)>;
    std::optional<ui::OtherGamesState> open(const ui::DeskRequest&, uint64_t& revision,
        const Image& underlay, std::string& error, const OpenAccept& = {});
    std::optional<CareerSession::OtherGamesDispatch> dispatch(const ui::OtherGamesRequest&,
        uint64_t& revision, std::string& error, const TransferOfferTeamInfoPresenter& = {}, const Accept& = {});
private:
    CareerSession& session_;
};

class TopScorersActions {
public:
    explicit TopScorersActions(CareerSession& session) : session_(session) {}
    bool apply(uint64_t& revision, const std::function<bool()>& action);
    using OpenAccept = std::function<void(ui::TopScorersState&)>;
    using Accept = std::function<void(CareerSession::TopScorersDispatch&)>;
    std::optional<ui::TopScorersState> open(const ui::DeskRequest&, uint64_t& revision,
        const Image& underlay, std::string& error, const OpenAccept& = {});
    std::optional<CareerSession::TopScorersDispatch> dispatch(const ui::TopScorersRequest&,
        uint64_t& revision, std::string& error, const Accept& = {});
private:
    CareerSession& session_;
};

class OpportunitiesActions {
public:
    explicit OpportunitiesActions(CareerSession& session) : session_(session) {}
    bool apply(uint64_t& revision, const std::function<bool()>& action);
    using OpenAccept = std::function<void(ui::OpportunitiesState&)>;
    using Accept = std::function<void(CareerSession::OpportunitiesDispatch&)>;
    std::optional<ui::OpportunitiesState> open(const ui::DeskRequest&, uint64_t& revision,
        const Image& underlay, std::string& error, bool national_source_case = false,
        int stable_identity = -1, const OpenAccept& = {});
    std::optional<CareerSession::OpportunitiesDispatch> dispatch(const ui::OpportunitiesRequest&,
        uint64_t& revision, std::string& error, const Accept& = {});
    bool preference(const ui::AnalyticsRequest&, uint64_t& revision, std::string& error,
        const std::function<void()>& acknowledge = {});
    static ui::OpportunitiesRequest close_request(const ui::OpportunitiesState& observed);
private:
    CareerSession& session_;
};

class ManagerJoinActions {
public:
    explicit ManagerJoinActions(CareerSession& session) : session_(session) {}
    bool apply(uint64_t& revision, const std::function<bool()>& action);
    bool commit(uint64_t& revision, std::string& error,
        const std::function<void(ui::CareerInitializationResult&)>& acknowledge = {});
private:
    CareerSession& session_;
};

class MailboxActions {
public:
    explicit MailboxActions(CareerSession& session) : session_(session) {}
    bool dispatch(const ui::MailboxActionRequest&, uint64_t& revision, std::string& error,
        const std::function<void()>& acknowledge = {});
    bool close(const ui::MailboxCloseRequest&, uint64_t& revision, std::string& error,
        const std::function<void()>& acknowledge = {});
    static ui::MailboxActionRequest request(int manager, uint64_t revision,
        ui::MailboxActionKind, const ui::MailboxMessage& observed);
private:
    CareerSession& session_;
};

class MenuLayoutActions {
public:
    explicit MenuLayoutActions(CareerSession& session) : session_(session) {}
    bool dispatch(const ui::SaveLoadRequest&, uint64_t& revision, std::string& error,
        const std::function<void()>& acknowledge = {});
private:
    CareerSession& session_;
};

class EnvironmentActions {
public:
    explicit EnvironmentActions(CareerSession& session) : session_(session) {}
    using Notice = std::function<bool(const GameState&, const std::string&)>;
    std::optional<EnvironmentResult> set(const ui::EnvironmentRequest&, uint64_t& revision, std::string& error,
        const Notice& = {}, const std::function<void(bool changed)>& acknowledge = {});
    std::optional<EnvironmentResult> set_console(const ui::EnvironmentView&, int item, int level,
        uint64_t& revision, std::string& error, const Notice& = {});
    static ui::EnvironmentRequest request(const ui::EnvironmentView& observed, int item, int level);
private:
    std::optional<EnvironmentResult> set_impl(const ui::EnvironmentRequest&, uint64_t&, std::string&,
        const Notice&, const std::function<void(bool)>&, bool publish_console_result);
    CareerSession& session_;
};

class RescheduleActions {
public:
    explicit RescheduleActions(CareerSession& session) : session_(session) {}
    bool apply(uint64_t& revision, const std::function<bool()>& action);
    RescheduleStatus dispatch(const ui::RescheduleAction&, uint64_t& revision,
        const std::function<void()>& acknowledge = {});
    RescheduleStatus move(int manager, const LeagueMatchRef&, int day, uint64_t& revision);
    void undo_console(int manager, int day, uint64_t& revision);
private:
    static RescheduleStatus change(GameState&, int manager, const LeagueMatchRef&, int day, ui::RescheduleActionKind);
    CareerSession& session_;
};

class ContractActions {
public:
    explicit ContractActions(CareerSession& session) : session_(session) {}
    bool dispatch(const ui::ContractRequest&, uint64_t& revision, ui::ContractActionResult&, std::string& error,
        const std::function<void()>& acknowledge = {});
    static ui::ContractRequest demands(const ui::ContractsState&, const ui::ContractPlayer&, uint64_t request_id);
    static ui::ContractRequest team_bonus(const ui::ContractsState&, int index, uint8_t percent, uint64_t request_id);
    static ui::ContractRequest insurance(const ui::ContractsState&, int row, int player, uint64_t quote,
        uint8_t form, uint8_t months, uint64_t request_id);
    static ui::ContractRequest offer(const ui::ContractsState&, int row, int player, uint64_t quote,
        const std::array<uint8_t,7>& bonuses, int years, uint32_t salary,
        const std::array<uint32_t,4>& demands, uint64_t request_id);
private:
    static ui::ContractRequest bind(const ui::ContractsState&, ui::ContractActionKind, uint64_t request_id);
    CareerSession& session_;
};

class FriendlyActions {
public:
    explicit FriendlyActions(CareerSession& session) : session_(session) {}
    struct Booking { FriendlyBookingResult result; bool applied = false, planner_mismatch = false; };
    using Accept = std::function<void(const ui::FriendlyBookingAck&)>;
    bool apply(uint64_t& revision, const std::function<bool()>& action);
    Booking book_club(const ui::FriendlyMatchRequest&, uint64_t& revision, const Accept& = {});
    Booking book_national(const ui::FriendlyMatchRequest&, uint64_t& revision, const Accept& = {});
    FriendlyBookingResult book_console(const FriendlyBookingQuote&, uint16_t day, bool accept_opponent,
        int consenting_manager, bool accept_cost, int approving_manager, uint64_t& revision);
    FriendlyBookingQuote quote(int manager, int opponent, bool home) const;
    NationalFriendlyQuote national_quote(const NationalFriendlyOwner&, int opponent, bool home);
    FriendlyDateStatus date_status(const FriendlyBookingQuote&, uint16_t day) const;
    FriendlyDateStatus national_date_status(const NationalFriendlyQuote&, const NationalFriendlyOwner&, uint16_t day) const;
    static NationalFriendlyOwner national_owner(const ui::FriendlyMatchRequest&);
private:
    Booking book(const ui::FriendlyMatchRequest&, uint64_t& revision, bool national, const Accept&);
    static FriendlyBookingRequest booking_request(const ui::FriendlyMatchRequest&);
    ui::FriendlyBookingAck acknowledgement(const ui::FriendlyMatchRequest&, const FriendlyBookingResult&, uint64_t revision) const;
    CareerSession& session_;
};

} // namespace bmh::app
