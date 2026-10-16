// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "app/career_session.hpp"
#include "audio/playback.hpp"
#include <functional>

namespace bmh::app {

using SeasonPresenter = std::function<void(const char*, const GameState&)>;

using TransferOfferPresenter = std::function<int(const GameState&, const TransferOfferListView&)>;
using TransferDecisionPresenter = std::function<void(const GameState&, const TransferDecisionEvent&)>;
using PoachNoticePresenter = std::function<void(const GameState&,const PoachStart&,const Image&)>;
using AdvertisingRenewalPresenter = std::function<CareerSession::AdvertisingRenewalPresentation(
    const GameState&, const AdvertisingRenewal&, const Image&)>;
using NationalFriendlyQuestionPresenter = std::function<std::optional<bool>(
    const GameState&, ui::TaxDialogProjection, const Image&)>;
using ContractExpiryPresenter = CareerSession::ContractExpiryPresenter;
using LoanReturnPresenter = CareerSession::LoanReturnPresenter;
using LoanReturnOfferPresenter = CareerSession::LoanReturnOfferPresenter;
using LiveLineupDispatcher = std::function<LiveSubstitutionResult(const ui::LineupRequest&)>;
using LiveTacticsDispatcher = std::function<bool(const ui::TacticsRequest&, std::string&)>;
using LivePitchDispatcher = std::function<LiveSubstitutionResult(const ui::PitchRequest&)>;
enum class LiveInterventionBoundary { begin, finish, abort };
using LiveInterventionScopeDispatcher = std::function<bool(LiveInterventionBoundary, int, int, int, std::string&)>;
using LiveInterventionPresenter = std::function<void(int, MatchEngine&, std::vector<MatchGroup>&,
    GameState&, uint64_t&, const LiveLineupDispatcher&, const LiveTacticsDispatcher&,
    const LivePitchDispatcher&, const LiveInterventionScopeDispatcher&)>;
using LiveMatchEventPresenter = std::function<void(const LiveMatchEvent&, MatchEngine&,
    std::vector<MatchGroup>&, GameState&, uint64_t&, const LiveLineupDispatcher&,
    const LiveTacticsDispatcher&, const LivePitchDispatcher&,
    const LiveInterventionScopeDispatcher&)>;
using EnvironmentNoticePresenter = std::function<bool(const GameState&, ui::TaxDialogProjection, const Image&)>;
using LiveRealityPresenter = decltype(TurnDriver::live_reality);
using PhaseResultPresenter = decltype(TurnDriver::phase_results);
using LiveBallPresenter = std::function<void(const LiveBallEvent&,
    const std::vector<MatchGroup>&, const GameState&)>;
using LiveChancePresenter = std::function<void(const LiveChanceEvent&,
    const std::vector<MatchGroup>&, const GameState&)>;
using LiveShotPresenter = std::function<void(const LiveShotEvent&,
    const std::vector<MatchGroup>&, const GameState&)>;
using LiveSetPiecePresenter = std::function<void(const LiveSetPieceEvent&,
    const std::vector<MatchGroup>&, const GameState&)>;
using LiveBookingPresenter = std::function<void(const LiveBookingEvent&,
    const std::vector<MatchGroup>&, const GameState&)>;
using LiveShootoutPresenter = std::function<void(const LiveShootoutEvent&,
    const std::vector<MatchGroup>&, const GameState&)>;

// Shared, synchronous request boundary of the SDL app. Tests drive the same
// controller without a window; rendering and navigation never advance the engine.
std::string serve_flow_request(ui::StartupFlow&, const ui::FlowResult&,
    std::unique_ptr<CareerSession>&, const std::filesystem::path& data,
    const std::filesystem::path& save_directory, uint32_t seed, uint64_t& revision,
    openbmh::audio::Playback&, std::optional<uint64_t>& audio_session, bool music_enabled,
    std::function<void(int, const std::vector<MatchGroup>&, const GameState&)> live_presenter = {},
    TransferOfferPresenter transfer_presenter = {},
    TransferDecisionPresenter transfer_decision_presenter = {},
    SeasonPresenter season_presenter = {},
    SeasonTaxHooks tax_presenter = {},
    SeasonHighscoreHook highscore_presenter = {},
    SeasonPromotionHook promotion_presenter = {},
    SeasonCupDrawHook cup_draw_presenter = {},
    PendingOfferHooks pending_offer_presenter = {},
    PendingOfferEntryPolicy pending_entry_policy = PendingOfferEntryPolicy::legacy_unpresented,
    HumanBuyerSaleHooks market_sale_presenter = {},
    HumanBuyerMarketEntryPolicy market_entry_policy = HumanBuyerMarketEntryPolicy::legacy_unpresented,
    TransferOfferTeamInfoPresenter team_info_presenter = {},
    TransferOfferPlayerInfoPresenter player_info_presenter = {},
    ConstructionNoticeHook construction_notice_presenter = {},
    SeasonTicketCompensationNoticeHook compensation_notice_presenter = {},
    SportschauPresentationHooks sportschau_presenter = {},
    AdvertisingRenewalPresenter advertising_renewal_presenter = {},
    CareerSession::PoachPlayerSelector poach_player_selector = {},
    PoachNoticePresenter poach_notice_presenter = {},
    ContractExpiryPresenter contract_expiry_presenter = {},
    NationalFriendlyQuestionPresenter national_friendly_question_presenter = {},
    LoanReturnPresenter loan_return_presenter = {},
    LoanReturnOfferPresenter loan_return_offer_presenter = {},
    LiveInterventionPresenter live_intervention_presenter = {},
    EnvironmentNoticePresenter environment_notice_presenter = {},
    LiveMatchEventPresenter live_match_event_presenter = {},
    LiveBallPresenter live_ball_presenter = {},
    LiveChancePresenter live_chance_presenter = {},
    LiveShotPresenter live_shot_presenter = {},
    LiveSetPiecePresenter live_set_piece_presenter = {},
    LiveBookingPresenter live_booking_presenter = {},
    LiveShootoutPresenter live_shootout_presenter = {},
    LiveRealityPresenter live_reality_presenter = {},
    PhaseResultPresenter phase_result_presenter = {}, bool enable_autosave = false);

// Focused controller entry for callers that only need to inject the original
// yes/no question presenter; all unrelated synchronous hooks keep defaults.
std::string serve_flow_request(ui::StartupFlow&, const ui::FlowResult&,
    std::unique_ptr<CareerSession>&, const std::filesystem::path& data,
    const std::filesystem::path& save_directory, uint32_t seed, uint64_t& revision,
    openbmh::audio::Playback&, std::optional<uint64_t>& audio_session, bool music_enabled,
    NationalFriendlyQuestionPresenter national_friendly_question_presenter);

} // namespace bmh::app
