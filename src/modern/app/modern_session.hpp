// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#pragma once
#include "modern/content/projections.hpp"
#include "modern/content/statistics.hpp"
#include "modern/content/world_expand.hpp"
// The engine side of the UI's career handoff (src/ui/flow.hpp, ENGINE_BINDING.md).
//
// The original starts a career at program start (game/career.hpp): the data files are
// loaded, every player gets new values and InitializeCareer runs up to the options dialog.
// The UI then shows the options and one dialog per manager; after each confirmed
// manager ConfirmManager sets him up, which may move clubs to other places - the UI gets
// the new club order (SelectionContext) for the next dialog. After the last manager the
// first season is prepared and the desk shown.
//
// ModernSession owns the engine state of that sequence. It never draws numbers for
// rendering; every random draw belongs to one of the steps above.

#include <cstdint>
#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "formats/le.hpp"
#include "formats/m3d.hpp"
#include "formats/textd.hpp"
#include "game/random.hpp"
#include "modern/game/desk_actions.hpp"
#include "modern/game/stadium_blocks.hpp"
#include "modern/game/modern_state.hpp"
#include "modern/game/turn_driver.hpp"
#include "modern/game/club_info.hpp"
#include "modern/game/club_player_detail.hpp"
#include "game/pending_offer.hpp"
#include "modern/game/poach.hpp"
#include "game/human_buyer_sale.hpp"
#include "modern/game/weekly.hpp"
#include "ui/flow.hpp"
#include "ui/desk_state.hpp"
#include "ui/league_table.hpp"
#include "ui/league_table_range_dialog.hpp"
#include "ui/league_pairings.hpp"
#include "ui/balance_plan.hpp"
#include "ui/top_scorers_state.hpp"
#include "ui/tactics.hpp"
#include "ui/opportunities.hpp"
#include "ui/other_games_state.hpp"

namespace bmh::modern::app {

ui::OutgoingTransferEditorState loan_return_offer_editor_projection(
    const ModernState&, const TransferOfferEditorState&, uint64_t revision, bool gradient);

// KB6 commits retain their concrete field even though they also refresh the quote.
TransferOfferEditorState edit_transfer_offer_from_ui(ModernState&, const TransferOfferEditorState&,
    const ui::OutgoingTransferRequest&, GameRandom&, WatcomRandom&);

// The original interactive post-desk path needs its own answer producer.
// Existing headless callers retain their prior unpresented driver contract.
enum class PendingOfferEntryPolicy { legacy_unpresented, normal_interactive };

// Source SubmitTransferBidSourceHumanBuyer direct purchases have a separate owner and cleanup contract
// from both special pending offers and automatic weekly auctions.
enum class HumanBuyerMarketEntryPolicy { legacy_unpresented, normal_interactive };

enum class LiveSubstitutionStatus { rejected, applied, source_notice };
struct LiveSubstitutionResult {
    LiveSubstitutionStatus status = LiveSubstitutionStatus::rejected;
    uint32_t source_text_address = 0;
    int manager_slot = -1;
    std::string error;
};

// Synchronous capability facade shared by live club modals and a staged KB31
// TeamInfo transaction. It exposes no calendar/save/outer revision operations.
struct ClubModalServices {
    bool transactional = true;
    std::function<ModernState&()> state;
    std::function<ClubInfoEntry(int)> enter_club;
    std::function<uint8_t(int,int)> change_category;
    std::function<ClubPlayerDetailRedraw(const ClubPlayerDetailSelection&)> redraw_player;
    std::function<bool(const ClubPlayerDetailSelection&)> national_action;
    std::function<PendingOfferResult(const ClubPlayerDetailSelection&,const PendingOfferHooks&)> pending_offers;
    std::function<uint8_t(uint32_t)> runtime_byte;
    std::function<void(uint32_t,uint8_t)> set_runtime_byte;
};
using TransferOfferTeamInfoPresenter = std::function<bool(ClubModalServices&,int,const Image&)>;
using TransferOfferPlayerInfoPresenter = std::function<bool(ClubModalServices&,const ClubPlayerDetailSelection&,const Image&)>;

// Semantic desk settings for frontends without MEN drawer/layout controls.
struct DeskSettingIdentity {
    int manager_slot = -1, runtime_club = -1;
    std::optional<int> stable_club_identity;
    uint64_t revision = 0;
};
struct TicketPriceRequest {
    DeskSettingIdentity identity;
    int price = 0; // Original admission range8..25.
};
enum class DeskAutomation { Formation, Substitution, Tactics, Marking, Training };
struct AutomationRequest {
    DeskSettingIdentity identity;
    DeskAutomation automation = DeskAutomation::Substitution;
    bool enabled = false;
    // Formation on selects an explicit available saved slot1..8; off has no slot.
    // Other automation settings must omit this field.
    std::optional<int> formation;
};

class ModernSession {
    friend class TransferActions;
public:
    struct LineupCheckpoint {
        ModernState state;
        int manager = -1;
        int club = -1;
    };
    // Loads the data files and runs the program start up to the options dialog. The Watcom
    // generator starts at `watcom_seed` (the original: srand() with the clock).
    ModernSession(const std::filesystem::path& data, uint32_t watcom_seed);

    // The order of the clubs as the next manager dialog shows them: runtime club id ->
    // master database index (ui::SelectionContext::club_order).
    ui::SelectionContext selection_context(uint64_t revision) const;
    std::vector<ui::ManagerChoice> manager_choices(uint64_t revision) const;
    bool begin_manager_join(const ui::DeskRequest&, const ui::SetupOptions&, uint64_t revision, std::string& error);
    bool confirm_manager_join(const ui::ManagerChoice&, std::string& error);
    ui::SelectionContext manager_join_context() const;
    bool manager_join_complete() const;
    void cancel_manager_join() { manager_join_.reset(); }
    struct ManagerJoinCheckpoint {
        ModernState state;
        WatcomRandom rand;
        GameRandom game;
        TurnDriver driver;
        std::vector<ui::InitializedManager> managers;
        std::vector<ui::DeskMatchCallargs> desk_callargs;
    };
    ManagerJoinCheckpoint manager_join_checkpoint() const {
        return {g_, rand_, game_, turn_driver_, managers_, source_desk_callargs_};
    }
    void restore_manager_join_checkpoint(ManagerJoinCheckpoint checkpoint) {
        g_ = std::move(checkpoint.state); rand_ = checkpoint.rand; game_ = checkpoint.game;
        turn_driver_ = std::move(checkpoint.driver); managers_ = std::move(checkpoint.managers);
        source_desk_callargs_ = std::move(checkpoint.desk_callargs);
        manager_join_.reset();
    }
    bool commit_manager_join(ui::CareerInitializationResult&, uint64_t& revision, std::string& error);

    // A confirmed manager (in confirmation order). The options are taken over before the
    // first one. Returns false with `error` set if the engine cannot do it.
    bool confirm_manager(const ui::SetupOptions& options, const ui::ManagerChoice& choice, std::string& error);
    int confirmed() const { return int(managers_.size()); }

    // After the last manager: the first season up to the first desk.
    bool finish(ui::CareerInitializationResult& result, std::string& error);
    std::optional<ClubInfoEntry> enter_club_information(int team, std::string& error);
    std::optional<uint8_t> change_club_information_category(int team, int requested,
                                                           std::string& error);
    std::optional<ClubPlayerDetailRedraw> redraw_club_player_detail(
        const ClubPlayerDetailSelection&, std::string& error);
    std::optional<bool> dispatch_club_player_national_action(
        const ClubPlayerDetailSelection&, std::string& error);
    // Synchronous manual DispatchClubPlayerPendingOffers batch. Hooks present the staged state;
    // unsupported/throwing decisions discard the state and both RNGs together.
    std::optional<PendingOfferResult> dispatch_club_player_pending_offers(
        const ClubPlayerDetailSelection&, uint64_t expected_revision,
        const PendingOfferHooks&, uint64_t& revision, std::string& error);

    // Ordinary market actions run on a full Native/RNG copy. Only a supported
    // result with valid dependent projections becomes the live state.
    std::optional<TransferActionResult> dispatch_transfer_market(
        const ui::TransferMarketRequest&, const HumanBuyerSaleHooks&,
        uint64_t& revision, std::string& error);

    struct TransferMarketPreviewDispatch {
        ui::PreparationState preparation;
        ui::TransferMarketPreview quote;
    };
    // TransferMarketScreen belongs to pointer input, never to rendering. Stage the optional
    // W fallback and validate the owned projection before publishing it.
    std::optional<TransferMarketPreviewDispatch> preview_transfer_market(
        const ui::TransferMarketRequest&, uint64_t& revision, std::string& error);

    struct TransferOfferEditorDispatch {
        ui::PreparationState preparation;
        ui::OutgoingTransferEditorState editor;
        TransferActionResult action;
    };
    std::optional<TransferOfferEditorDispatch> open_transfer_offer(
        const ui::TransferMarketRequest&, uint64_t& revision, std::string& error);
    std::optional<TransferOfferEditorDispatch> dispatch_transfer_offer(
        const ui::OutgoingTransferRequest&, const HumanBuyerSaleHooks&,
        uint64_t& revision, std::string& error,
        const TransferOfferTeamInfoPresenter& team_info = {},
        const TransferOfferPlayerInfoPresenter& player_info = {});
    // Poaching (Abwerben, transfer screen button 4): the checks and the special list row of
    // src/game/poach.hpp, then the ordinary offer editor on that row (decided at once, see
    // dispatch_transfer_offer). Without `editor` the attempt ended with the original's
    // messages in `start` (some still count as an attempt and are committed).
    struct PoachDispatch {
        PoachStart start;
        std::optional<TransferOfferEditorDispatch> editor;
    };
    std::optional<PoachDispatch> open_poach(int manager_slot, int squad_slot, uint64_t& revision, std::string& error);
    // FriendlyMatchScreen(param1=1) owns a separate staged selection, not the normal
    // LeagueScreenShown Table family. Return the actual squad selection delivered by
    // ClubPlayerDetailFromGame's >25-tick long press, or nullopt when Table is cancelled.
    using PoachPlayerSelector = std::function<std::optional<ClubPlayerDetailSelection>(
        ModernSession& selection_session, ClubModalServices&, int manager_slot,
        uint64_t& selection_revision, const Image& transfer_backdrop)>;
    using PoachUiAccept = std::function<void(const PoachDispatch&, ui::PreparationState)>;
    bool close_poach_table_selection(const ui::LeagueFamilyRequest&, std::string& error);
    bool dispatch_poach_ui(const ui::TransferMarketRequest&, const Image&,
        const PoachPlayerSelector&, const PoachUiAccept&, uint64_t& revision, std::string& error);
    ClubModalServices live_club_modal_services(uint64_t& revision);
    struct LeaguePairingsDraft {
        ModernState state;
        GameRandom game;
        WatcomRandom rand;
        std::map<uint32_t,uint8_t> runtime,original_runtime;
        LeaguePairingsOwner owner;
        ui::LeaguePairingsIdentity identity;
        Bytes original_stream;
        uint32_t original_rand_seed=0;
        int32_t original_game_seed=0;
        uint64_t original_rand_calls=0,original_game_calls=0;
        std::optional<LeaguePairingsActivePacket> active_packet;
        // Only the active presentation context is borrowed from the driver;
        // retain its original descriptors/fixtures/profiles to reject a stale
        // redraw or nested return instead of changing packet ownership silently.
        std::vector<std::pair<Bytes,std::vector<int>>> original_active_groups;
        std::vector<uint8_t> original_referee_profiles;
    };
    struct LeagueFamilyOwner {
        enum class TableDispatchPhase : uint8_t { ready, range_start, range_end, forecast_pending_present };
        ui::StrengthsIdentity identity;
        uint8_t child = 0; // Source selector: 1 table, 2 pairings, 3 strengths.
        bool friendly_opponent = false; // Direct FriendlyMatchScreen(2,0), without outer LeagueScreenShown close.
        Bytes native;
        uint32_t rand_seed = 0;
        int32_t game_seed = 0;
        uint64_t rand_calls = 0, game_calls = 0;
        std::map<uint32_t,uint8_t> runtime;
        struct TableOwner {
            int first_round = 0, end_round = 0;
            uint8_t aggregation_flags = 4;
            int normal_mode = 0;
            TableDispatchPhase phase = TableDispatchPhase::ready;
            int pending_start_round = 0;
            int previous_start_round = 0, previous_end_round = 0;
            uint16_t display_word_override = 0;
            uint8_t runtime_font_id = 0;
            std::string retained_range_label_source_bytes;
            int dialog_x = 140, dialog_y = 80;
            ui::LeagueTableState page;
            std::optional<ui::LeagueTableState> forecast_page;
            std::optional<GameRandom> forecast_game_after;
            bool forecast_present_pending = false;
            bool operator==(const TableOwner& other) const {
                const bool same_rng = forecast_game_after.has_value() == other.forecast_game_after.has_value() &&
                    (!forecast_game_after || (forecast_game_after->seed() == other.forecast_game_after->seed() &&
                                               forecast_game_after->calls() == other.forecast_game_after->calls()));
                return first_round == other.first_round && end_round == other.end_round &&
                    aggregation_flags == other.aggregation_flags && normal_mode == other.normal_mode &&
                    phase == other.phase && pending_start_round == other.pending_start_round &&
                    previous_start_round == other.previous_start_round && previous_end_round == other.previous_end_round &&
                    display_word_override == other.display_word_override && runtime_font_id == other.runtime_font_id &&
                    retained_range_label_source_bytes == other.retained_range_label_source_bytes &&
                    dialog_x == other.dialog_x && dialog_y == other.dialog_y && page == other.page &&
                    forecast_page == other.forecast_page && same_rng &&
                    forecast_present_pending == other.forecast_present_pending;
            }
        };
        std::optional<TableOwner> table;
        bool operator==(const LeagueFamilyOwner&) const = default;
    };
    using TableDispatchPhase = LeagueFamilyOwner::TableDispatchPhase;
    struct LeagueTableDispatch {
        ui::LeagueTableState page;
        ui::StrengthsIdentity identity;
        TableDispatchPhase phase = TableDispatchPhase::ready;
        std::optional<ui::TableRangeDialogProfile> dialog;
    };
    struct LeaguePairingsCheckpoint {
        ModernState state;
        GameRandom game;
        WatcomRandom rand;
        std::map<uint32_t,uint8_t> runtime;
        std::optional<LeaguePairingsDraft> draft;
        std::optional<LeagueFamilyOwner> family;
        uint64_t next_family_id = 1;
    };
    struct LeaguePairingsDispatch {
        bool closed=false;
        ui::LeaguePairingsState page;
        ui::PreparationState preparation;
    };
    std::optional<ui::LeaguePairingsState> open_league_pairings(const ui::DeskRequest&,uint64_t revision,std::string& error);
    std::optional<LeaguePairingsDispatch> dispatch_league_pairings(const ui::LeaguePairingsRequest&,
        uint64_t& revision,std::string& error,const TransferOfferTeamInfoPresenter& team_info={});
    LeaguePairingsCheckpoint league_pairings_checkpoint() const {
        return {g_,game_,rand_,club_modal_runtime_,league_pairings_draft_,league_family_owner_,next_league_family_id_};
    }
    void restore_league_pairings_checkpoint(LeaguePairingsCheckpoint checkpoint) {
        g_=std::move(checkpoint.state);game_=checkpoint.game;rand_=checkpoint.rand;
        club_modal_runtime_=std::move(checkpoint.runtime);league_pairings_draft_=std::move(checkpoint.draft);
        league_family_owner_=std::move(checkpoint.family);next_league_family_id_=checkpoint.next_family_id;
    }
    const std::optional<LeaguePairingsDraft>& league_pairings_owner() const {return league_pairings_draft_;}
    struct BalanceDraft {
        struct NoticeGate { uint8_t compact_dialog_frame=0,notice_gate_disabled=0,presentation_blocked=0; bool enabled() const { return notice_gate_disabled==0&&(compact_dialog_frame!=0||presentation_blocked==0); } };
        ModernState state;
        GameRandom game;
        WatcomRandom rand;
        std::map<uint32_t,uint8_t> runtime, original_runtime;
        ui::BalanceState page;
        Bytes original_stream;
        uint32_t original_rand_seed=0;
        int32_t original_game_seed=0;
        uint64_t original_rand_calls=0, original_game_calls=0;
        bool warning=false;
        NoticeGate notice_gate;
    };
    struct BalanceCheckpoint {
        ModernState state; GameRandom game; WatcomRandom rand;
        std::map<uint32_t,uint8_t> runtime;
        std::optional<BalanceDraft> draft;
        uint64_t next_scope=0;
    };
    struct BalanceDispatch { bool closed=false, warning=false; ui::BalanceState page; ui::PreparationState preparation; };
    std::optional<ui::BalanceState> open_balance_plan(const ui::DeskRequest&, uint64_t revision,
        const Image& underlay, std::string& error);
    std::optional<BalanceDispatch> dispatch_balance_plan(const ui::BalanceRequest&, uint64_t& revision,
        std::string& error);
    BalanceCheckpoint balance_checkpoint() const { return {g_,game_,rand_,club_modal_runtime_,balance_draft_,next_balance_scope_}; }
    void restore_balance_checkpoint(BalanceCheckpoint c) { g_=std::move(c.state);game_=c.game;rand_=c.rand;club_modal_runtime_=std::move(c.runtime);balance_draft_=std::move(c.draft);next_balance_scope_=c.next_scope; }
    bool balance_active() const { return bool(balance_draft_)||balance_dispatch_active_; }
    std::map<uint32_t,uint8_t> club_modal_runtime_checkpoint() const { return club_modal_runtime_; }
    void restore_club_modal_runtime(std::map<uint32_t,uint8_t> runtime) { club_modal_runtime_=std::move(runtime); }
    const std::optional<TransferOfferEditorState>& transfer_offer_owner() const { return transfer_offer_editor_; }
    void restore_transfer_offer_owner(std::optional<TransferOfferEditorState> owner, uint64_t revision) {
        transfer_offer_editor_ = std::move(owner); transfer_offer_revision_ = revision;
    }

    struct StrengthsOwner {
        ui::StrengthsState page;
        Bytes native;
        uint32_t rand_seed = 0;
        int32_t game_seed = 0;
        uint64_t rand_calls = 0, game_calls = 0;
        std::map<uint32_t,uint8_t> runtime;
    };
    struct LeagueFamilyCheckpoint {
        ModernState state;
        GameRandom game;
        WatcomRandom rand;
        std::map<uint32_t,uint8_t> runtime;
        std::optional<LeagueFamilyOwner> family;
        std::optional<StrengthsOwner> strengths;
        std::optional<LeaguePairingsDraft> pairings;
        uint64_t next_strengths_scope=1,next_family_id=1;
    };
    struct StrengthsCheckpoint {
        ModernState state;
        GameRandom game;
        WatcomRandom rand;
        std::map<uint32_t,uint8_t> runtime;
        std::optional<StrengthsOwner> owner;
        uint64_t next_scope = 1;
        std::optional<LeagueFamilyOwner> family;
        uint64_t next_family_id = 1;
    };
    struct StrengthsDispatch {
        bool closed = false;
        ui::StrengthsState page;
        ui::PreparationState preparation;
    };
    // Semantic entry has no MEN/open-widget requirement. Every submitted identity
    // and both expected saved words are validated again against the live session.
    std::optional<ui::StrengthsRequest> strengths_open_request(uint64_t revision) const;
    std::optional<ui::StrengthsIdentity> begin_league_family(ui::StrengthsIdentity,uint8_t child,
        uint64_t revision,std::string& error);
    std::optional<ui::StrengthsIdentity> begin_friendly_opponent_table(uint64_t revision,std::string& error);
    // Resolves the clicked physical row through normal working ranks after KI.
    std::optional<int> resolve_friendly_table_opponent(const ui::LeagueFamilyRequest&,uint64_t revision,
        std::string& error) const;
    std::optional<ui::StrengthsIdentity> acknowledge_friendly_own_club(
        const ui::LeagueFamilyRequest&,uint64_t revision,std::string& error);
    std::optional<ui::PreparationState> finish_friendly_opponent_table(const ui::LeagueFamilyRequest&,
        uint64_t& revision,std::string& error);
    bool advance_league_family(const ui::LeagueFamilyRequest&,ui::StrengthsIdentity,uint8_t target,
        uint64_t revision,std::string& error);
    std::optional<LeagueFamilyOwner> league_family_owner() const { return league_family_owner_; }
    LeagueFamilyCheckpoint league_family_checkpoint() const {
        return {g_,game_,rand_,club_modal_runtime_,league_family_owner_,strengths_owner_,league_pairings_draft_,
            next_strengths_scope_,next_league_family_id_};
    }
    void restore_league_family_checkpoint(LeagueFamilyCheckpoint c) {
        g_=std::move(c.state);game_=c.game;rand_=c.rand;club_modal_runtime_=std::move(c.runtime);
        league_family_owner_=std::move(c.family);strengths_owner_=std::move(c.strengths);
        league_pairings_draft_=std::move(c.pairings);next_strengths_scope_=c.next_strengths_scope;
        next_league_family_id_=c.next_family_id;
    }
    std::optional<ui::PreparationState> close_league_family(const ui::LeagueFamilyRequest&,
        uint64_t& revision,std::string& error);
    bool suspend_strengths_for_family(const ui::LeagueFamilyRequest&,uint64_t revision,std::string& error);
    bool suspend_pairings_for_family(const ui::LeagueFamilyRequest&,uint64_t revision,std::string& error);
    bool select_league_family_page(const ui::LeagueFamilyRequest&,uint64_t revision,std::string& error);
    std::optional<LeagueTableDispatch> current_league_table_dispatch(std::string& error) const;
    std::optional<LeagueTableDispatch> dispatch_league_table_control(
        const ui::LeagueFamilyRequest&,uint64_t& revision,std::string& error);
    std::optional<LeagueTableDispatch> dispatch_league_table_range_reply(
        const ui::LeagueFamilyRequest&,uint64_t& revision,std::string& error);
    std::optional<LeagueTableDispatch> acknowledge_league_forecast_present(
        const ui::LeagueFamilyRequest&,uint64_t& revision,std::string& error);
    std::optional<ui::StrengthsIdentity> dispatch_family_table_team_info(
        const ui::LeagueFamilyRequest&,uint64_t& revision,std::string& error,
        const TransferOfferTeamInfoPresenter&,
        const std::optional<ClubPlayerDetailSelection>* poach_selection = nullptr);
    bool refresh_family_after_child(const ui::StrengthsIdentity& prior,uint8_t child,
        ui::StrengthsIdentity next,uint64_t revision,std::string& error);
    bool prepare_league_family_child(const ui::StrengthsIdentity&,uint8_t child,
        uint64_t revision,std::string& error);
    std::optional<StrengthsDispatch> dispatch_strengths(const ui::StrengthsRequest&,
        uint64_t& revision, std::string& error, const TransferOfferTeamInfoPresenter& team_info = {},
        bool family_transition = false);
    StrengthsCheckpoint strengths_checkpoint() const {
        return {g_,game_,rand_,club_modal_runtime_,strengths_owner_,next_strengths_scope_,league_family_owner_,next_league_family_id_};
    }
    void restore_strengths_checkpoint(StrengthsCheckpoint checkpoint) {
        g_=std::move(checkpoint.state); game_=checkpoint.game; rand_=checkpoint.rand;
        club_modal_runtime_=std::move(checkpoint.runtime); strengths_owner_=std::move(checkpoint.owner);
        next_strengths_scope_=checkpoint.next_scope;league_family_owner_=std::move(checkpoint.family);
        next_league_family_id_=checkpoint.next_family_id;
    }
    const std::optional<StrengthsOwner>& strengths_owner() const { return strengths_owner_; }

    struct TopScorersOwner {
        ui::TopScorersState page;
        Bytes native;
        uint32_t rand_seed = 0;
        int32_t game_seed = 0;
        uint64_t rand_calls = 0, game_calls = 0;
    };
    struct TopScorersCheckpoint {
        ModernState state;
        GameRandom game;
        WatcomRandom rand;
        std::optional<TopScorersOwner> owner;
        uint64_t next_scope = 1;
    };
    struct TopScorersDispatch {
        bool closed = false;
        ui::TopScorersState page;
        ui::PreparationState preparation;
    };
    std::optional<ui::TopScorersState> open_top_scorers(const ui::DeskRequest&, uint64_t revision,
                                                        const Image& underlay, std::string& error);
    std::optional<TopScorersDispatch> dispatch_top_scorers(const ui::TopScorersRequest&,
        uint64_t& revision, std::string& error);
    TopScorersCheckpoint top_scorers_checkpoint() const {
        return {g_,game_,rand_,top_scorers_owner_,next_top_scorers_scope_};
    }
    void restore_top_scorers_checkpoint(TopScorersCheckpoint c) {
        g_=std::move(c.state);game_=c.game;rand_=c.rand;
        top_scorers_owner_=std::move(c.owner);next_top_scorers_scope_=c.next_scope;
    }
    bool top_scorers_active() const { return bool(top_scorers_owner_) || top_scorers_dispatch_active_; }
    const std::optional<TopScorersOwner>& top_scorers_owner() const { return top_scorers_owner_; }

    struct OpportunitiesOwner { ui::OpportunitiesState page; Bytes native; GameRandom game; WatcomRandom rand; bool national_source_case=false; };
    struct OpportunitiesCheckpoint { ModernState state; GameRandom game; WatcomRandom rand; std::optional<OpportunitiesOwner> owner; uint64_t next_scope=1; };
    struct OpportunitiesDispatch { bool closed=false; ui::OpportunitiesState page; ui::PreparationState preparation; };
    std::optional<ui::OpportunitiesState> open_opportunities(const ui::DeskRequest&,uint64_t,const Image&,std::string&,
                                                              bool national_source_case=false,int stable_identity=-1);
    std::optional<OpportunitiesDispatch> dispatch_opportunities(const ui::OpportunitiesRequest&,uint64_t&,std::string&);
    OpportunitiesCheckpoint opportunities_checkpoint() const { return {g_,game_,rand_,opportunities_owner_,next_opportunities_scope_}; }
    void restore_opportunities_checkpoint(OpportunitiesCheckpoint c) { g_=std::move(c.state);game_=c.game;rand_=c.rand;opportunities_owner_=std::move(c.owner);next_opportunities_scope_=c.next_scope; }
    bool opportunities_active() const { return bool(opportunities_owner_)||opportunities_dispatch_active_; }
    bool apply_statistics_preference(const ui::DeskRequest&, int manager_slot, uint8_t expected_flags,
                                     uint8_t target_flags, uint64_t& revision, std::string& error);

    struct OtherGamesSelection {
        // Process-global Source selectors: the ordinary desk entry does not clear
        // Season_SeasonTransition_RuntimeField/CareerSession_FormatError_RuntimeField8 when the active manager changes.
        int competition = 0, linked_page = 0;
    };
    struct OtherGamesOwner {
        ui::OtherGamesState page;
        ui::DeskRequest desk_owner;
        int round = -1, participant_offset = 0;
        OtherGamesDisplayMode display_mode = OtherGamesDisplayMode::detail;
        Bytes native;
        std::map<uint32_t,uint8_t> runtime;
        uint32_t rand_seed = 0;
        int32_t game_seed = 0;
        uint64_t rand_calls = 0, game_calls = 0;
    };
    struct OtherGamesCheckpoint {
        ModernState state;
        GameRandom game;
        WatcomRandom rand;
        std::map<uint32_t,uint8_t> runtime;
        std::optional<OtherGamesOwner> owner;
        OtherGamesSelection selection;
        uint64_t next_scope = 1;
    };
    struct OtherGamesDispatch {
        bool closed = false;
        ui::OtherGamesState page;
        ui::PreparationState preparation;
    };
    std::optional<ui::OtherGamesState> open_other_games(
        const ui::DeskRequest&, uint64_t& revision, const Image& underlay, std::string& error);
    std::optional<OtherGamesDispatch> dispatch_other_games(
        const ui::OtherGamesRequest&, uint64_t& revision, std::string& error,
        const TransferOfferTeamInfoPresenter& team_info = {});
    OtherGamesCheckpoint other_games_checkpoint() const {
        return {g_,game_,rand_,club_modal_runtime_,other_games_owner_,other_games_selection_,next_other_games_scope_};
    }
    void restore_other_games_checkpoint(OtherGamesCheckpoint c) {
        g_=std::move(c.state);game_=c.game;rand_=c.rand;club_modal_runtime_=std::move(c.runtime);
        other_games_owner_=std::move(c.owner);other_games_selection_=c.selection;
        next_other_games_scope_=c.next_scope;
    }
    bool other_games_active() const { return bool(other_games_owner_) || other_games_dispatch_active_; }
    const std::optional<OtherGamesOwner>& other_games_owner() const { return other_games_owner_; }

    // Accept the desk's kickoff request and advance the shared engine turn cursor.
    TurnAdvanceResult dispatch_kickoff(const ui::DeskRequest& request, uint64_t& revision,
                                       std::string& error);

    // Fresh coherent preparation views for one controller revision, after a desk action.
    ui::PreparationState preparation_projection(uint64_t revision) const;

    // Validates camp request context before any action; advances revision only on change.
    bool dispatch_training(const ui::TrainingRequest&, uint64_t& revision, std::string& error);
    bool dispatch_training_camp(const ui::TrainingCampRequest&, uint64_t& revision, std::string& error);
    bool dispatch_bank(const ui::BankRequest&, uint64_t& revision, std::string& error);
    struct BankCheckpoint {
        ModernState state;
        WatcomRandom rand;
        GameRandom game;
        std::optional<int> last_tip;
        std::optional<ui::BankState::PropertyNotice> property_notice;
        std::optional<ui::BankState::CreditNotice> credit_notice;
    };
    BankCheckpoint bank_checkpoint() const { return {g_, rand_, game_, last_stock_tip_, last_property_notice_, last_credit_notice_}; }
    void restore_bank_checkpoint(BankCheckpoint checkpoint) {
        g_ = std::move(checkpoint.state);
        rand_ = checkpoint.rand; game_ = checkpoint.game;
        last_stock_tip_ = checkpoint.last_tip;
        last_property_notice_ = std::move(checkpoint.property_notice);
        last_credit_notice_ = std::move(checkpoint.credit_notice);
    }
    bool dispatch_stadium(const ui::StadiumRequest&, uint64_t& revision, std::string& error,
                          const ConstructionNoticeHook& notice_presenter = {},
                          const SeasonTicketCompensationNoticeHook& compensation_presenter = {});
    struct StadiumRuntime {
        struct BlockExpansion {
            StadiumBlockExpansionQuote quote;
            uint32_t total_target_places = 0;
            uint8_t desired_features = 0;
            int header_text_variant = -1;
            std::optional<StandBlockOffer> tip;
        };
        bool opened = false;
        bool data_open = false;
        uint8_t data_flags = 0x40;
        int manager = -1, club = -1, stable_club = -1, mode = -1;
        uint64_t scope = 0, sequence = 0, revision = 0;
        int price_percent = 100;
        std::optional<std::pair<int, int32_t>> periphery_quote;
        std::optional<StadiumInvestment> investment;
        std::optional<uint8_t> selection_status;
        int option_index = -1, text_variant = -1;
        std::optional<StadiumInvestmentOption> option;
        std::optional<StandBlockOffer> tip;
        std::optional<BlockExpansion> block_expansion;
        std::string feedback;
    };
    struct StadiumCheckpoint {
        ModernState state;
        WatcomRandom rand;
        GameRandom game;
        StadiumRuntime stadium;
        uint64_t next_scope;
    };
    StadiumCheckpoint stadium_checkpoint() const { return {g_, rand_, game_, stadium_, next_stadium_scope_}; }
    void restore_stadium_checkpoint(StadiumCheckpoint checkpoint) {
        g_ = std::move(checkpoint.state); rand_ = checkpoint.rand; game_ = checkpoint.game;
        stadium_ = std::move(checkpoint.stadium); next_stadium_scope_ = checkpoint.next_scope;
    }
    bool stadium_tip_deadline_expired() const;
    bool dispatch_toto(const ui::TotoRequest&, uint64_t& revision, std::string& error);
    // Modern Toto with up to six columns for the manager at the desk (docs/new_engine/toto.md).
    // Errors: too_many_columns, stake_out_of_range, total_stake_too_high, duplicate_column,
    // toto_coupon_requires_eleven_matches, invalid_toto_coupon, stale_manager_or_revision.
    bool dispatch_pools_columns(int manager, uint32_t stake, const std::vector<std::array<uint8_t, 11>>& columns,
                                uint64_t& revision, std::string& error);
    bool dispatch_tactics(const ui::TacticsRequest&, uint64_t& revision, std::string& error);
    bool dispatch_lineup(const ui::LineupRequest&, uint64_t& revision, std::string& error);
    // Source PitchScreen live-match path: apply one user-picked field/bench
    // exchange and re-rate the side while TurnDriver is paused after a minute.
    bool dispatch_live_substitution(const ui::LineupRequest&, int minute, int source_mode,
                                    MatchEngine&, uint64_t& revision, std::string& error);
    LiveSubstitutionResult dispatch_live_substitution_result(const ui::LineupRequest&, int minute,
        int source_mode, MatchEngine&, uint64_t& revision);
    bool dispatch_live_tactic(const ui::TacticsRequest&, int minute, int source_mode,
                              MatchEngine&, uint64_t& revision, std::string& error);
    bool dispatch_live_pitch(const ui::PitchRequest&, int minute, int source_mode,
        MatchEngine&, uint64_t& revision, std::string& error);
    LiveSubstitutionResult dispatch_live_pitch_result(const ui::PitchRequest&, int minute,
        int source_mode, MatchEngine&, uint64_t& revision);
    // Source EndPhase's configured keyboard slots 0/1 (G/T) suspend the current
    // match-minute loop and open LeagueScreenShown or CupGroupOverviewEffects. The caller must only create
    // this request from that synchronous observer; group_index is fixed to 0,
    // the first Native descriptor selected by CareerSessionRuntimeField5 (not the current DeskPosition tick).
    enum class LiveOverviewKind : uint8_t { league_table, league_pairings, league_strengths, cup_other_games };
    struct LiveOverviewRequest {
        int key_slot = -1; // Source Main_FileScreen_RuntimeField slot: 0=G/table, 1=T/pairings.
        int minute = -1;
        int group_index = -1;
        int manager_slot = -1;
        int runtime_team = -1;
        uint64_t revision = 0;
        uint64_t sequence = 0;
        bool operator==(const LiveOverviewRequest&) const = default;
    };
    struct LiveOverviewProjection {
        LiveOverviewKind kind = LiveOverviewKind::league_table;
        LiveOverviewRequest owner;
        ui::StrengthsIdentity family_identity; // G uses the existing full Table-family controls.
        std::optional<ui::LeagueTableState> table;
        std::optional<ui::LeaguePairingsState> pairings;
        std::optional<ui::StrengthsState> strengths;
        std::optional<ui::OtherGamesState> cup;
    };
    using LiveOverviewAction = std::variant<ui::LeagueFamilyRequest,
        ui::LeaguePairingsRequest, ui::StrengthsRequest, ui::OtherGamesRequest>;
    struct LiveOverviewDispatch {
        LiveOverviewKind kind = LiveOverviewKind::league_table;
        bool closed = false;
        ui::StrengthsIdentity family_identity;
        TableDispatchPhase table_phase = TableDispatchPhase::ready;
        std::optional<ui::TableRangeDialogProfile> table_dialog;
        std::optional<ui::LeagueTableState> table;
        std::optional<ui::LeaguePairingsState> pairings;
        std::optional<ui::StrengthsState> strengths;
        std::optional<ui::OtherGamesState> cup;
        std::optional<ui::PreparationState> preparation;
    };
    std::optional<LiveOverviewProjection> open_live_overview(const LiveOverviewRequest&,
        MatchEngine&, uint64_t& revision, const Image& underlay, std::string& error);
    std::optional<LiveOverviewDispatch> dispatch_live_overview(const LiveOverviewAction&,
        MatchEngine&, uint64_t& revision, std::string& error,
        const TransferOfferTeamInfoPresenter& team_info = {});
    bool live_overview_active() const { return bool(live_overview_owner_); }
    // Only available inside the installed phase presenter. Uses the same Source
    // viewer/action owners as keyboard overviews, before the engine's single ACK.
    std::optional<LiveOverviewProjection> open_phase_results(uint64_t& revision,
        const Image& underlay, std::string& error);
    std::optional<LiveOverviewDispatch> dispatch_phase_results(const LiveOverviewAction&,
        uint64_t& revision, std::string& error,
        const TransferOfferTeamInfoPresenter& team_info = {});
    // Source EndPhase key slot 2 suspends the live-minute observer in SettingsSourceRefusal.
    // The request is accepted only during that synchronous observer; Session allocates
    // the returned sequence, so callers must retain the returned owner verbatim.
    struct LiveSettingsRequest {
        int minute = -1;
        int manager_slot = -1;
        int runtime_team = -1;
        uint64_t revision = 0;
        uint64_t sequence = 0;
        bool operator==(const LiveSettingsRequest&) const = default;
    };
    struct LiveSettingsWrite {
        uint64_t owner_sequence = 0;
        ui::SettingsWriteRequest write;
    };
    struct LiveSettingsFile { uint64_t owner_sequence = 0; ui::SaveLoadRequest request; };
    struct LiveSettingsCloseRequest { LiveSettingsRequest owner; };
    using LiveSettingsAction = std::variant<LiveSettingsWrite,LiveSettingsFile,LiveSettingsCloseRequest>;
    struct LiveSettingsProjection { LiveSettingsRequest owner; ui::SettingsState state; };
    struct LiveSettingsDispatch { LiveSettingsRequest owner; bool closed = false; ui::SettingsState state; };
    std::optional<LiveSettingsProjection> open_live_settings(const LiveSettingsRequest&,
        MatchEngine&, uint64_t& revision, std::string& error);
    std::optional<LiveSettingsDispatch> dispatch_live_settings(const LiveSettingsAction&,
        MatchEngine&, uint64_t& revision, std::string& error);
    bool live_settings_active() const { return bool(live_settings_owner_); }
    struct DeskSettingsOwner {
        uint64_t sequence = 0, revision = 0;
        int manager_slot = -1, source_item_id = -1;
        bool operator==(const DeskSettingsOwner&) const = default;
    };
    struct DeskSettingsWrite { uint64_t owner_sequence = 0; ui::SettingsWriteRequest write; };
    struct DeskSettingsFile { uint64_t owner_sequence = 0; ui::SaveLoadRequest request; };
    struct DeskSettingsClose { DeskSettingsOwner owner; };
    using DeskSettingsAction = std::variant<DeskSettingsWrite,DeskSettingsFile,DeskSettingsClose>;
    struct DeskSettingsProjection { DeskSettingsOwner owner; ui::SettingsState state; };
    struct DeskSettingsDispatch { DeskSettingsOwner owner; bool closed = false; ui::SettingsState state; };
    struct DeskSettingsCheckpoint {
        ModernState state;
        std::map<uint32_t,uint8_t> runtime;
        std::optional<DeskSettingsOwner> owner;
        uint8_t initial_h8 = 0;
        uint64_t next_scope = 1, revision = 0;
    };
    std::optional<DeskSettingsProjection> open_desk_settings(const ui::DeskRequest&,
        uint64_t& revision, std::string& error);
    std::optional<DeskSettingsDispatch> dispatch_desk_settings(const DeskSettingsAction&,
        uint64_t& revision, std::string& error);
    DeskSettingsCheckpoint desk_settings_checkpoint(uint64_t revision) const;
    void restore_desk_settings_checkpoint(DeskSettingsCheckpoint, uint64_t& revision);
    bool desk_settings_active() const { return bool(desk_settings_owner_); }
    bool begin_live_intervention(int minute, int source_mode, int manager, int team,
        MatchEngine&, uint64_t& revision, std::string& error);
    bool begin_live_event_intervention(const LiveMatchEvent&, MatchEngine&,
        uint64_t& revision, std::string& error);
    bool finish_live_intervention(int minute, int source_mode, int manager, int team,
        MatchEngine&, uint64_t& revision, std::string& error);
    bool finish_live_event_intervention(const LiveMatchEvent&, MatchEngine&,
        uint64_t& revision, std::string& error);
    void abort_live_intervention(int minute, int source_mode, int manager, int team,
        MatchEngine&, uint64_t& revision);
    // Semantic Pitch actions bind the live manager/club, prepared entry and revision.
    // The four Source helpers are deterministic; rejected actions do not commit.
    bool dispatch_pitch(const ui::PitchRequest&, uint64_t& revision, std::string& error);
    bool dispatch_pitch_swap(const ui::PitchRequest&, uint64_t& revision, std::string& error);
    // The Source desk callargs are the only authority for an ordinary upcoming
    // opponent. The revision binds the returned display projection; write requests
    // are independently checked against the controller's current revision.
    std::optional<int> prepared_lineup_opponent(int manager_slot, int runtime_club,
                                                uint64_t revision) const;
    LineupCheckpoint lineup_checkpoint() const { return {g_, lineup_entry_manager_, lineup_entry_club_}; }
    void restore_lineup_checkpoint(LineupCheckpoint checkpoint) {
        g_ = std::move(checkpoint.state);
        lineup_entry_manager_ = checkpoint.manager;
        lineup_entry_club_ = checkpoint.club;
    }
    bool dispatch_mailbox(const ui::MailboxActionRequest&, uint64_t& revision, std::string& error);
    std::optional<ui::MailboxState> pending_mailbox_notification(uint64_t revision) const;
    bool acknowledge_mailbox_close(const ui::MailboxCloseRequest&, uint64_t revision,
                                  std::string& error);
    uint32_t mailbox_notification_checkpoint() const {
        return g_.memory.has(::bmh::layout::MailboxRecipientMask) ? g_.memory.u32(::bmh::layout::MailboxRecipientMask) : 0;
    }
    void restore_mailbox_notification_checkpoint(uint32_t mask) {
        if (g_.memory.has(::bmh::layout::MailboxRecipientMask)) g_.memory.set32(::bmh::layout::MailboxRecipientMask, mask);
    }
    bool dispatch_settings(const ui::SettingsWriteRequest&, uint64_t& revision, std::string& error);
    bool dispatch_contract(const ui::ContractRequest&, uint64_t& revision,
                           ui::ContractActionResult&, std::string& error);
    bool dispatch_advertising(const ui::AdvertisingRequest&, uint64_t& revision, std::string& error,
                              AdvertisingSpendingResult* spending_result = nullptr);
    // CheckAdvertisingRenewal: renewing a contract whose months ran out (game/desk_actions.hpp). The
    // check draws the sponsor's random number and returns the question or settles the
    // refusal; the answer applies the manager's choice. Both bind the active manager and
    // the revision (and advance it).
    std::optional<AdvertisingRenewal> check_advertising_renewal(int manager_slot, int display_slot, uint64_t& revision,
                                                                std::string& error);
    bool answer_advertising_renewal(int manager_slot, int contract_slot, bool renew, uint64_t& revision, std::string& error);
    enum class AdvertisingRenewalPresentation { abort, renew, decline, acknowledged };
    using AdvertisingRenewalPresenter = std::function<AdvertisingRenewalPresentation(
        const ModernState&, const AdvertisingRenewal&)>;
    std::optional<AdvertisingRenewal> dispatch_advertising_renewal(
        const ui::AdvertisingRequest&, uint64_t& revision, std::string& error,
        const AdvertisingRenewalPresenter& presenter);
    struct AdvertisingActionResult {
        AdvertisingSpendingResult spending;
        std::optional<AdvertisingRenewal> renewal;
    };
    using AdvertisingAcknowledge = std::function<void(const AdvertisingActionResult&)>;
    // Frontends supply an observed intent, never a live-state refresh of an old
    // offer. Acknowledgement is inside the transaction (SDL projection can fail).
    bool apply_advertising(const ui::AdvertisingRequest&, uint64_t& revision,
        std::string& error, const AdvertisingAcknowledge& acknowledge = {},
        const AdvertisingRenewalPresenter& presenter = {});
    bool accept_advertising_selection(const ui::AdvertisingState& observed, int display_slot,
        int raw_offer, uint64_t request_id, uint64_t& revision, std::string& error);
    bool set_advertising_budget(const ui::AdvertisingState& observed, int target_slot,
        uint32_t amount, uint64_t request_id, uint64_t& revision, std::string& error,
        AdvertisingSpendingResult& result);
    struct AdvertisingCheckpoint {
        ModernState state;
        uint64_t last_request_id = 0;
        WatcomRandom rand;
        GameRandom game;
    };
    AdvertisingCheckpoint advertising_checkpoint() const { return {g_, last_advertising_request_id_, rand_, game_}; }
    void restore_advertising_checkpoint(AdvertisingCheckpoint checkpoint) {
        g_ = std::move(checkpoint.state);
        last_advertising_request_id_ = checkpoint.last_request_id;
        rand_ = checkpoint.rand;
        game_ = checkpoint.game;
    }
    void clear_contract_quote() { contract_quote_.reset(); }
    bool dispatch_desk_widget(const ui::DeskRequest&, uint64_t& revision, std::string& error);
    bool dispatch_desktop_layout(const ui::DeskRequest&, uint64_t& revision, std::string& error);
    bool dispatch_ticket_price(const TicketPriceRequest&, uint64_t& revision, std::string& error);
    bool dispatch_automation(const AutomationRequest&, uint64_t& revision, std::string& error);
    ui::DeskState desktop_projection(uint64_t revision) const;
    bool save_game(const std::filesystem::path& path, int manager_slot, bool overwrite, std::string& error);
    void configure_autosave_directory(const std::filesystem::path& directory);
    std::string take_autosave_error();
    // Source DispatchMenuLayout: read/write the active manager's raw 0x360-byte .MEN layout.
    // This operation never serializes a ModernSession or consumes either RNG.
    bool dispatch_menu_layout(const ui::SaveLoadRequest&, uint64_t& revision, std::string& error);

    // Load an existing V2.24 save without applying new-career options or startup RNG.
    ModernSession(const std::filesystem::path &data, uint32_t watcom_seed,
                  const std::filesystem::path &save_path, bool importing_legacy = false);

    const ModernState& state() const { return g_; }
    // The game statistics menu (docs/new_engine/statistik.md): winners by season, player
    // statistics per competition. Read-only; answers {ok, ...} or {ok: false, error}.
    cli::Json read_statistics(const cli::Json& request) const { return query_statistics(g_, request); }
    // The read queries of the new interface (docs/new_engine/ui-abfragen.md): status, leagues,
    // table, fixtures, squad and the statistics, for the manager at the desk. Read-only.
    cli::Json read(const cli::Json& request) const {
        return query_projection(g_, int(g_.memory.has(::bmh::layout::ActiveManager) ? g_.memory.u8(::bmh::layout::ActiveManager) : 0), request);
    }
    ModernState& state() { return g_; }
    const TurnResultSnapshot& last_turn_results() const { return turn_driver_.last_results; }
    // Owned presentation for the event completed by the last accepted kickoff.
    // Empty on a manager handoff or a calendar event without fixtures.
    const std::optional<ui::MatchdayResults>& completed_matchday_results() const { return completed_results_; }
    // The manager's intervention during a matchday (see TurnDriver::live_minute).
    void set_live_minute(decltype(TurnDriver::live_minute) hook) { turn_driver_.live_minute = std::move(hook); }
    void set_original_sound(decltype(TurnDriver::original_sound) hook) { turn_driver_.original_sound = std::move(hook); }
    void set_live_event(decltype(TurnDriver::live_event) hook) { turn_driver_.live_event = std::move(hook); }
    void set_live_reality(decltype(TurnDriver::live_reality) hook) { turn_driver_.live_reality = std::move(hook); }
    void set_phase_results(decltype(TurnDriver::phase_results) hook);
    void set_live_ball(decltype(TurnDriver::live_ball) hook) { turn_driver_.live_ball = std::move(hook); }
    void set_live_chance(decltype(TurnDriver::live_chance) hook) { turn_driver_.live_chance = std::move(hook); }
    void set_live_shot(decltype(TurnDriver::live_shot) hook) { turn_driver_.live_shot = std::move(hook); }
    void set_live_set_piece(decltype(TurnDriver::live_set_piece) hook) { turn_driver_.live_set_piece = std::move(hook); }
    void set_live_booking(decltype(TurnDriver::live_booking) hook) { turn_driver_.live_booking = std::move(hook); }
    void set_live_shootout(decltype(TurnDriver::live_shootout) hook) { turn_driver_.live_shootout = std::move(hook); }
    // Temporary synchronous presenter for InstallDefaultHooks, invoked by the monthly
    // board walk. The supplied random stream is the session's live Watcom stream.
    // Frontends install this only while an interactive kickoff is running.
    using ContractExpiryPresenter = std::function<int(ModernState&, WatcomRandom&, int, int)>;
    void set_contract_expiry_presenter(ContractExpiryPresenter presenter);
    using LoanReturnPresenter = std::function<int(ModernState&, WatcomRandom&, int, const std::string&)>;
    void set_loan_return_presenter(LoanReturnPresenter presenter);
    using LoanReturnOfferPresenter = std::function<int(ModernState&, GameRandom&, WatcomRandom&,
        ClubModalServices&, int manager, int source_slot, int list_entry, const HumanBuyerSaleHooks&)>;
    void set_loan_return_offer_presenter(LoanReturnOfferPresenter presenter,
                                         HumanBuyerSaleHooks sale_hooks = {});
    SportschauSourceOptions sportschau_source_options() const;
    void set_sportschau_presentation_hooks(SportschauPresentationHooks hooks) {
        turn_driver_.sportschau = std::move(hooks);
    }
    const SportschauPresentationHooks& sportschau_presentation_hooks() const {
        return turn_driver_.sportschau;
    }
    void set_season_pause(decltype(TurnDriver::season_pause) hook) { turn_driver_.season_pause = std::move(hook); }
    void set_season_promotion(SeasonPromotionHook hook) { turn_driver_.season_promotion = std::move(hook); }
    void set_season_cup_draw(SeasonCupDrawHook hook) { turn_driver_.season_cup_draw = std::move(hook); }
    void set_season_tax(SeasonTaxHooks hooks) { turn_driver_.season_tax = std::move(hooks); }
    void set_season_highscore(decltype(TurnDriver::season_highscore) hook) { turn_driver_.season_highscore = std::move(hook); }
    // Borrowed only during the synchronous post-desk transaction. Its modal
    // presenters receive the staged state rather than the session's live state.
    void set_pending_offer_hooks(PendingOfferHooks hooks) { pending_offer_hooks_ = std::move(hooks); }
    const PendingOfferHooks& pending_offer_hooks() const { return pending_offer_hooks_; }
    void set_pending_offer_entry_policy(PendingOfferEntryPolicy policy) { pending_offer_entry_policy_ = policy; }
    PendingOfferEntryPolicy pending_offer_entry_policy() const { return pending_offer_entry_policy_; }
    // Synchronous original offer chooser. Installed only while the app advances;
    // the modal presenter cannot dispatch another desk action or save a partial turn.
    void set_transfer_offer_selector(std::function<int(const ModernState&, const TransferOfferListView&)> hook) {
        g_.transfer_offer_selector = std::move(hook);
    }
    void set_transfer_decision_observer(std::function<void(const TransferDecisionEvent&)> hook) {
        g_.transfer_decision_observer = std::move(hook);
    }
    GameRandom& game() { return game_; }
    WatcomRandom& rand() { return rand_; }
    uint64_t session_id() const { return session_id_; }
    bool career_complete() const { return turn_driver_.stage == TurnDriver::Stage::CareerComplete; }

    // Portable host driver capabilities replace SOUND.CFG runtime latches; they
    // are never inferred from a saved switch or written into an FBM on entry.
    void configure_sound_capabilities(bool music, bool effects) {
        sound_capabilities_ = ui::SettingsSoundCapabilities{music, effects};
    }
    ui::SettingsState settings_projection(int manager, uint64_t revision) const {
        return ui::settings_from_game(state(), manager, revision, exe_, sound_capabilities_);
    }
    // What expanding the Classic start into the Modern world did (empty for Classic data).
    const ExpandReport& world_report() const { return world_report_; }

private:
    ExpandReport world_report_;
    std::optional<ui::SettingsSoundCapabilities> sound_capabilities_;
    std::optional<std::filesystem::path> autosave_directory_;
    std::string autosave_error_;
    void write_desk_autosave(const ModernState&, WatcomRandom&);
    bool native_autosave_pending_ = false;
    void flush_native_autosave();
    cli::Json native_session_context() const;
    void restore_native_session_context(const cli::Json &);
    bool live_source_identity(const ui::StrengthsIdentity&) const;
    bool validate_desk_setting_owner(const DeskSettingIdentity&, uint64_t revision, std::string& error,
                                    bool require_desk_phase = true) const;
    bool apply_desk_setting(int manager, bool price, int index, int value, std::string& error);
    ui::PreparationState preparation_projection(const ModernState&, uint64_t revision) const;
    ui::DeskState desktop_projection(const ModernState&, uint64_t revision) const;
    bool pending_offer_dispatch_active_ = false;
    bool market_dispatch_active_ = false;
    bool league_pairings_dispatch_active_=false;
    bool league_family_teaminfo_active_=false;
    bool poach_table_selection_=false; // Only the isolated50775(param1=1) selection Session.
    bool strengths_dispatch_active_=false;
    bool top_scorers_dispatch_active_=false;
    bool opportunities_dispatch_active_=false;
    bool other_games_dispatch_active_=false;
    uint64_t next_other_games_scope_=1;
    std::optional<OtherGamesOwner> other_games_owner_;
    OtherGamesSelection other_games_selection_;
    bool balance_dispatch_active_=false;
    uint64_t next_balance_scope_=1;
    std::optional<BalanceDraft> balance_draft_;
    uint64_t next_strengths_scope_=1;
    std::optional<StrengthsOwner> strengths_owner_;
    std::optional<LeagueFamilyOwner> league_family_owner_;
    uint64_t next_league_family_id_=1;
    uint64_t next_top_scorers_scope_=1;
    std::optional<TopScorersOwner> top_scorers_owner_;
    uint64_t next_opportunities_scope_=1;
    std::optional<OpportunitiesOwner> opportunities_owner_;
    uint64_t league_pairings_scope_=0;
    std::optional<LeaguePairingsDraft> league_pairings_draft_;
    std::optional<LeaguePairingsActivePacket> validate_normal_pairings_group(
        const ModernState&,LeaguePairingsOwner&,const std::map<uint32_t,uint8_t>&) const;
    std::optional<TransferOfferEditorState> transfer_offer_editor_;
    std::optional<PoachStart> poach_;  // the open poaching attempt (its editor is transfer_offer_editor_)
    uint64_t transfer_offer_revision_ = 0;
    bool calendar_advance_active_ = false;
    PendingOfferHooks pending_offer_hooks_;
    PendingOfferEntryPolicy pending_offer_entry_policy_ = PendingOfferEntryPolicy::legacy_unpresented;
    std::map<uint32_t,uint8_t> club_modal_runtime_;
    void initialize_club_modal_runtime();
    ClubModalServices staged_club_modal_services(ModernState&,GameRandom&,WatcomRandom&,
        std::map<uint32_t,uint8_t>&);
    PendingOfferHooks pending_hooks_for_dispatch(PendingOfferHooks) const;
    void capture_source_desk_callargs();
    uint64_t last_advertising_request_id_ = 0;
    bool advertising_renewal_dispatch_active_ = false;
    // Engine hooks for screens the app does not have yet. The contracts screen
    // (InstallDefaultHooks, desk menu 27): until it exists, the assistant renews an ended
    // contract for two years at the market salary (see the definition).
    void install_default_hooks();
    void apply_options(const ui::SetupOptions& options);
    int runtime_club(int master) const;
    int runtime_club(const ModernState&, int master) const;
    ui::SelectionContext selection_context_for(const ModernState&, uint64_t revision) const;
    struct ManagerJoinDraft {
        ModernState state;
        WatcomRandom rand;
        GameRandom game;
        ui::SetupOptions options;
        ui::DeskRequest origin;
        std::vector<ui::ManagerChoice> choices;
        Bytes original_stream;
        uint32_t original_rand_seed = 0;
        int32_t original_game_seed = 0;
        uint64_t original_rand_calls = 0, original_game_calls = 0, context_revision = 0;
    };
    std::optional<ManagerJoinDraft> manager_join_;

    std::filesystem::path data_directory_;
    LeImage exe_;
    // Source MatchEventsDisabled belongs to the loaded program image, not the Native FBM.
    // Both session constructors explicitly initialize this owned runtime input.
    Database db_;
    TextDb texts_;
    ModernState g_;
    GameRandom game_{0};
    WatcomRandom rand_;
    TurnDriver turn_driver_;
    // DayCompetitions2 callargs are event-local. A join retains the old slots and
    // gets Source's default arrays for its new slot until the next event.
    std::vector<ui::DeskMatchCallargs> source_desk_callargs_;
    std::string saved_turn_error_;
    std::optional<ui::MatchdayResults> completed_results_;
    int lineup_entry_manager_ = -1, lineup_entry_club_ = -1;
    struct LiveInterventionCheckpoint {
        ModernState state;
        std::vector<MatchGroup> groups;
        GameRandom game{0};
        WatcomRandom rand;
        int minute = 0, source_mode = 0, manager = -1, team = -1;
        bool source_event = false;
        LiveMatchEvent event;
        uint64_t initial_revision = 0, current_revision = 0;
    };
    std::optional<LiveInterventionCheckpoint> live_intervention_;
    struct LiveOverviewOwner {
        bool phase_result = false;
        LiveOverviewRequest request;
        LiveOverviewKind kind = LiveOverviewKind::league_table;
        ui::StrengthsIdentity family_identity;
        int source_competition = -1;
        Bytes original_native;
        uint32_t original_rand_seed = 0;
        int32_t original_game_seed = 0;
        uint64_t original_rand_calls = 0, original_game_calls = 0;
        std::vector<std::pair<Bytes,std::vector<int>>> active_groups;
        std::vector<uint8_t> referees;
    };
    std::optional<LiveOverviewOwner> live_overview_owner_;
    const PhaseResultRequest* phase_result_request_ = nullptr; // borrowed synchronous boundary only
    std::optional<LiveOverviewProjection> open_running_overview(const LiveOverviewRequest&,
        MatchEngine&, uint64_t& revision, const Image& underlay, std::string& error,
        bool phase_result);
    uint64_t next_live_overview_scope_ = 1;
    struct LiveSettingsOwner {
        LiveSettingsRequest request;
        Bytes native;
        std::vector<uint8_t> expected_buttons;
        bool expected_live_desk_disabled = false, expected_live_scene_visible = false;
        std::map<uint32_t,uint8_t> expected_runtime;
        uint32_t rand_seed = 0;
        int32_t game_seed = 0;
        uint64_t rand_calls = 0, game_calls = 0;
        std::vector<std::pair<Bytes,std::vector<int>>> active_groups;
        std::vector<uint8_t> referees;
    };
    std::optional<LiveSettingsOwner> live_settings_owner_;
    uint64_t next_live_settings_scope_ = 1;
    bool live_settings_dispatch_active_ = false;
    struct DeskSettingsOwnerState {
        DeskSettingsOwner owner;
        uint8_t initial_h8 = 0; // ApplySettingsFile captures CareerSession_DispatchStrengths_ExecutableField before SettingsSourceRefusal.
        Bytes native;
        std::map<uint32_t,uint8_t> expected_runtime;
        uint32_t rand_seed = 0;
        int32_t game_seed = 0;
        uint64_t rand_calls = 0, game_calls = 0;
    };
    std::optional<DeskSettingsOwnerState> desk_settings_owner_;
    uint64_t next_desk_settings_scope_ = 1;
    bool desk_settings_dispatch_active_ = false;
    bool apply_settings_file(const ui::SaveLoadRequest&, int manager_slot, int origin_item_id,
                            ModernState& staged, bool& loaded, std::string& error) const;
    uint64_t session_id_ = 0;
    std::vector<ui::InitializedManager> managers_;
    std::optional<int> last_stock_tip_;
    std::optional<ui::BankState::PropertyNotice> last_property_notice_;
    std::optional<ui::BankState::CreditNotice> last_credit_notice_;
    StadiumRuntime stadium_;
    uint64_t next_stadium_scope_ = 1;
    struct ContractQuoteRecord {
        uint64_t quote_id = 0, request_id = 0, revision = 0;
        int manager_slot = -1, runtime_club = -1, row = -1, player_id = -1;
        std::array<uint32_t, 4> demands{};
    };
    // Snapshot every mutable Session owner around the synchronous modal. The callback
    // may hold a reference to this session; a changed fingerprint rejects the answer,
    // and the full copy is restored before the staged Core result can be committed.
    struct AdvertisingSessionCheckpoint {
        ModernState state; GameRandom game; WatcomRandom rand;
        TurnDriver driver; std::vector<ui::DeskMatchCallargs> desk_callargs;
        std::string saved_turn_error; std::optional<ui::MatchdayResults> completed_results;
        int lineup_manager=-1, lineup_club=-1; std::vector<ui::InitializedManager> managers;
        std::optional<int> stock_tip;
        std::optional<ui::BankState::PropertyNotice> property_notice;
        std::optional<ui::BankState::CreditNotice> credit_notice;
        StadiumRuntime stadium; uint64_t next_stadium_scope=1;
        std::optional<ContractQuoteRecord> contract_quote; uint64_t next_contract_quote_id=1;
        uint64_t last_contract_request_id=0; bool options_applied=false;
        bool pending_offer_active=false, market_active=false, pairings_active=false,
             family_teaminfo_active=false, strengths_active=false, top_scorers_active=false,
             other_games_active=false, balance_active=false, calendar_active=false;
        uint64_t next_other_games_scope=1; std::optional<OtherGamesOwner> other_games_owner;
        OtherGamesSelection other_games_selection;
        uint64_t next_balance_scope=1; std::optional<BalanceDraft> balance;
        uint64_t next_strengths_scope=1; std::optional<StrengthsOwner> strengths;
        std::optional<LeagueFamilyOwner> family; uint64_t next_family_id=1;
        uint64_t next_top_scorers_scope=1; std::optional<TopScorersOwner> top_scorers;
        uint64_t pairings_scope=0; std::optional<LeaguePairingsDraft> pairings;
        std::optional<TransferOfferEditorState> transfer_editor; std::optional<PoachStart> poach;
        uint64_t transfer_offer_revision=0; PendingOfferHooks pending_hooks;
        PendingOfferEntryPolicy pending_policy=PendingOfferEntryPolicy::legacy_unpresented;
        std::map<uint32_t,uint8_t> runtime; uint64_t last_request_id=0;
        std::optional<ManagerJoinDraft> manager_join;
        uint64_t revision=0, session_id=0;
    };
    AdvertisingSessionCheckpoint advertising_session_checkpoint(uint64_t revision) const;
    void restore_advertising_session_checkpoint(AdvertisingSessionCheckpoint);
    bool advertising_session_unchanged(const AdvertisingSessionCheckpoint&, uint64_t revision) const;
    std::optional<ContractQuoteRecord> contract_quote_;
    uint64_t next_contract_quote_id_ = 1;
    uint64_t last_contract_request_id_ = 0;
    bool contract_expiry_presenter_installed_ = false;
    bool loan_return_presenter_installed_ = false;
    bool loan_return_offer_presenter_installed_ = false;
    bool options_applied_ = false;
};

}  // namespace bmh::modern::app
