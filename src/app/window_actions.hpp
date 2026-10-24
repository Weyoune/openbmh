// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "app/session.hpp"

namespace bmh::app {

// Shared application boundary for Table, Pairings and Strengths. The callback
// includes the frontend's acknowledgement; false or an exception rolls back.
class LeagueFamilyActions {
public:
    explicit LeagueFamilyActions(CareerSession& session) : session_(session) {}
    bool apply(uint64_t& revision, const std::function<bool()>& action);
    std::optional<ui::LeaguePairingsState> open_pairings(const ui::DeskState& observed,
        uint64_t& revision, std::string& error);
    std::optional<CareerSession::LeaguePairingsDispatch> pairings(
        const ui::LeaguePairingsIdentity& observed, LeaguePairingsAction, int value,
        uint64_t& revision, std::string& error);
    static ui::DeskRequest pairings_entry(const ui::StrengthsIdentity&, uint64_t revision);
    static ui::LeaguePairingsRequest pairings_selection(const ui::LeaguePairingsIdentity&, int league);
    static ui::StrengthsRequest strengths_entry(const ui::StrengthsRequest&, const ui::StrengthsIdentity&);
private:
    CareerSession& session_;
};

// Market, quote and outgoing editor share this boundary, including frontend
// acknowledgement and the suspended poaching attempt behind an offer owner.
class TransferActions {
public:
    explicit TransferActions(CareerSession& session) : session_(session) {}
    bool apply(uint64_t& revision, const std::function<bool()>& action);
    std::string rejection(const ui::TransferMarketRequest&, uint64_t revision,
        HumanBuyerMarketEntryPolicy) const;
    struct MarketDispatch { TransferActionResult action; bool changed = false; };
    using MarketAccept = std::function<void(const MarketDispatch&)>;
    using OfferAccept = std::function<void(CareerSession::TransferOfferEditorDispatch&)>;
    using PreviewAccept = std::function<void(CareerSession::TransferMarketPreviewDispatch&)>;
    std::optional<MarketDispatch> market(const ui::TransferMarketRequest&, const HumanBuyerSaleHooks&,
        uint64_t& revision, std::string& error,
        HumanBuyerMarketEntryPolicy = HumanBuyerMarketEntryPolicy::normal_interactive,
        const MarketAccept& = {});
    std::optional<CareerSession::TransferOfferEditorDispatch> open_offer(const ui::TransferMarketRequest&,
        uint64_t& revision, std::string& error, const OfferAccept& = {});
    std::optional<CareerSession::TransferMarketPreviewDispatch> preview(const ui::TransferMarketRequest&,
        uint64_t& revision, std::string& error, const PreviewAccept& = {});
    std::optional<CareerSession::TransferOfferEditorDispatch> offer(const ui::OutgoingTransferRequest&,
        const HumanBuyerSaleHooks&, uint64_t& revision, std::string& error,
        const TransferOfferTeamInfoPresenter& = {}, const TransferOfferPlayerInfoPresenter& = {},
        const OfferAccept& = {});
    std::optional<CareerSession::PoachDispatch> poach(int manager, int squad_slot,
        uint64_t& revision, std::string& error);
    static ui::TransferMarketRequest listing(const ui::TransferMarketState&,
        const ui::TransferMarketSquadItem&, bool loan);
    static ui::TransferMarketRequest opening(const ui::TransferMarketState&,
        const ui::TransferMarketItem&, bool loan);
    static ui::OutgoingTransferRequest editor(const ui::OutgoingTransferEditorState&,
        ui::OutgoingTransferRequestKind, ui::OutgoingTransferField, uint32_t value);
private:
    CareerSession& session_;
};

class BankActions {
public:
    explicit BankActions(CareerSession& session) : session_(session) {}
    bool apply(uint64_t& revision, const std::function<bool()>& action);
    bool dispatch(const ui::BankRequest&, uint64_t& revision, std::string& error,
        const std::function<void()>& acknowledge = {});
    static ui::BankRequest savings(const ui::BankState&, int32_t amount);
    static ui::BankRequest investment(const ui::BankState&, ui::BankRequestKind, int asset, int count = 0);
    static ui::BankRequest credit(const ui::BankState&, const BankCreditContext&,
        const BankCreditDraft&, bool accept_fees, int counterparty);
private:
    static ui::BankRequest bind(const ui::BankState&, ui::BankRequestKind);
    CareerSession& session_;
};

class StadiumActions {
public:
    explicit StadiumActions(CareerSession& session) : session_(session) {}
    bool apply(uint64_t& revision, const std::function<bool()>& action);
    bool dispatch(const ui::StadiumRequest&, uint64_t& revision, std::string& error,
        const ConstructionNoticeHook& = {}, const SeasonTicketCompensationNoticeHook& = {},
        const std::function<void()>& acknowledge = {});
    static ui::StadiumRequest request(const ui::StadiumState&, ui::StadiumRequestKind,
        int object = -1, int option = -1, int32_t price = 0, bool accept_tip = false);
private:
    CareerSession& session_;
};

class DeskActions {
public:
    explicit DeskActions(CareerSession& session) : session_(session) {}
    bool ticket_price(const ui::DeskState& observed, int price, uint64_t& revision, std::string& error);
private:
    CareerSession& session_;
};

class LineupActions {
public:
    explicit LineupActions(CareerSession& session) : session_(session) {}
    bool apply(uint64_t& revision, const std::function<bool()>& action);
    bool lineup(const ui::LineupRequest&, uint64_t& revision, std::string& error,
        const std::function<void()>& acknowledge = {});
    bool pitch(const ui::PitchRequest&, uint64_t& revision, std::string& error,
        const std::function<void()>& acknowledge = {});
    bool pitch_swap(const ui::PitchRequest&, uint64_t& revision, std::string& error);
    bool order(const ui::LineupState&, const ui::LineupPlayer&, int aspect, int value, uint64_t& revision);
    enum class PairTarget { invalid, other_pair, requested };
    PairTarget pair_target(const ui::PitchRequest&, int second_row) const;
    static ui::LineupRequest prepare(const ui::LineupState&);
    static ui::LineupRequest swap(const ui::LineupState&, const ui::LineupPlayer&, const ui::LineupPlayer&);
    static ui::PitchRequest swap_pitch(const ui::PreparationState&, const ui::LineupPlayer&, const ui::LineupPlayer&);
    static ui::PitchRequest page(const ui::PreparationState&);
    static ui::PitchRequest formation(const ui::PreparationState&, ui::PitchRequestKind, int slot);
    static ui::PitchRequest move(const ui::PreparationState&, const ui::LineupPlayer&, int depth, int lane);
    static ui::PitchRequest weight(const ui::PreparationState&, int index);
private:
    static ui::PitchRequest bind_pitch(const ui::PreparationState&, ui::PitchRequestKind);
    CareerSession& session_;
};

} // namespace bmh::app
