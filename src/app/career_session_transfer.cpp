// SPDX-License-Identifier: GPL-3.0-only
// CareerSession: transfers: offer editor, poaching, transfer market.
// (moved out of career_session.cpp unchanged; the class is declared in app/career_session.hpp)
#include "app/career_session_internal.hpp"

namespace bmh::app {
namespace {

// Transfer editor / market group (helpers first). Projects the engine's transfer offer editor
// owner into the UI state: the quote fields, calculator profiles and, unless the editor is
// closed, the incoming-offer decision panel read from the four offer rows of the market list.
ui::OutgoingTransferEditorState transfer_offer_projection(const GameState& game,
    const TransferOfferEditorState& owner, uint64_t revision, bool gradient) {
    ui::OutgoingTransferEditorState out;
    const auto& request = owner.request;
    out.manager_slot = request.manager; out.runtime_club = request.runtime_club;
    out.revision = revision; out.list_entry = request.list_entry; out.source_slot = request.source_slot;
    out.player = request.expected_player;
    out.seller = request.expected_seller_word == 0xffff ? 0x0fff : request.expected_seller_word & 0x7fff;
    out.bid_slot = owner.bid_slot; out.source_mode = owner.mode;
    out.editable = owner.editable; out.closed = owner.closed; out.quote_loan = owner.quote_loan;
    out.initial_action_loan = request.loan;
    out.gradient_text = gradient;
    out.fee = owner.fee; out.monthly_salary = owner.salary; out.duration_years = owner.years;
    out.bonuses = owner.bonuses;
    const auto copy_profile = [](const TransferOfferInputProfile& p) {
        return ui::OutgoingTransferCalculatorProfile{p.supported, p.currency, p.minimum, p.maximum,
            p.initial, p.source_text_address, p.presets};
    };
    for (size_t n = 0; n < 2; ++n) out.money_calculators[n] = copy_profile(owner.money_profiles[n]);
    for (size_t n = 0; n < 8; ++n) out.mode3_calculators[n] = copy_profile(owner.mode3_profiles[n]);
    if (owner.closed) return out; // No modal is constructed after Source cleanup.
    ui::IncomingTransferDecisionView view;
    view.source_outgoing_editor_context = true;
    view.valid = true; view.list_entry = request.list_entry; view.player = out.player;
    view.seller = out.seller; view.seller_manager = out.seller == 0x0fff ? -1 : game.manager_of(out.seller);
    view.viewer_manager = request.manager;
    view.asking_fee = owner.asking_fee; view.asking_monthly_salary = owner.asking_salary;
    view.quoted_fee = owner.quoted_fee; view.quoted_monthly_salary = owner.quoted_salary;
    view.quoted_years = owner.years;
    const uint32_t list = ::bmh::layout::EditorList + uint32_t(request.list_entry) * 0x58;
    for (uint32_t n = 0; n < 4; ++n) {
        auto& row = view.offers[n];
        const auto buyer = game.memory.u16(list + n * 2);
        row.bid_index = int(n); row.present = buyer != 0xffff;
        row.buyer = row.present ? int(buyer) : -1;
        row.buyer_manager = row.present ? game.manager_of(row.buyer) : -1;
        row.fee = game.memory.u32(list + 0x1c + n * 4);
        row.monthly_salary = game.memory.u32(list + 0x0c + n * 4);
        const auto raw_term = game.memory.u8(list + 0x34 + n);
        row.loan = (raw_term & 0x80) != 0; row.contract_months = raw_term & 0x7f;
        row.eligible = row.present;
        for (uint32_t k = 0; k < 7; ++k) row.bonuses[k] = game.memory.u8(list + 0x38 + n * 7 + k);
    }
    out.panel = ui::incoming_transfer_decision_from_game(game, view);
    return out;
}

}  // namespace

// Public wrapper so other code (loan return offers) can reuse the editor projection.
ui::OutgoingTransferEditorState loan_return_offer_editor_projection(
    const GameState& game, const TransferOfferEditorState& owner, uint64_t revision, bool gradient) {
    return transfer_offer_projection(game, owner, revision, gradient);
}

// Maps a UI field edit onto the engine's transfer offer editor edit and returns the new owner.
TransferOfferEditorState edit_transfer_offer_from_ui(GameState& game,
    const TransferOfferEditorState& owner, const ui::OutgoingTransferRequest& request,
    GameRandom& game_random, WatcomRandom& watcom_random) {
    return edit_transfer_offer_editor_source(game, owner,
        {TransferOfferEditorField(uint8_t(request.field)), request.value, request.accepted},
        game_random, watcom_random);
}

// Transfer market request family (open offer, poach, editor events, quote preview, market
// actions): each entry validates the request revision, the active manager slot and the manager's
// club, refuses nested modals (market_dispatch_active_ and the other owners), works on copies of
// the state and both random generators and commits them only on success. The revision is
// advanced only where something observable changed. Failures return an empty optional and an
// error text.
// Opens the buyer's offer editor for a marked market row after checking the row identity
// against the market list in memory; the editor owner and its revision are stored on success.
std::optional<CareerSession::TransferOfferEditorDispatch> CareerSession::open_transfer_offer(
    const ui::TransferMarketRequest& request, uint64_t& revision, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return std::nullopt; }
    error.clear();
    if (request.kind != ui::TransferMarketRequestKind::open_offer || transfer_offer_editor_ ||
        market_dispatch_active_ || pending_offer_dispatch_active_ || calendar_advance_active_ ||
        request.revision != revision || revision == std::numeric_limits<uint64_t>::max() ||
        request.manager_slot < 0 || request.manager_slot >= 4 ||
        size_t(request.manager_slot) >= g_.manager_teams.size() ||
        !g_.memory.has(::bmh::layout::ActiveManager) || g_.memory.u8(::bmh::layout::ActiveManager) != request.manager_slot ||
        request.runtime_club != g_.manager_teams[size_t(request.manager_slot)] ||
        request.bid.manager != request.manager_slot || request.bid.list_entry < 0 || request.bid.list_entry >= 40 ||
        request.bid.source_slot != 2928 + request.bid.list_entry || request.selected_players.size() > 1) {
        error = "transfer offer: stale, nested or unsupported marked opening";
        return std::nullopt;
    }
    const uint32_t list = ::bmh::layout::EditorList + uint32_t(request.bid.list_entry) * 0x58;
    if (!g_.memory.has(list + 0x57)) { error = "transfer offer: missing Native list row"; return std::nullopt; }
    const uint16_t seller_word = g_.memory.u16(list + 8);
    if (!request.selected_players.empty()) {
        const auto& selected = request.selected_players.front();
        const int seller = seller_word == 0xffff ? -1 : int(seller_word & 0x7fff);
        if (selected.squad_row != -1 || selected.source_slot != request.bid.source_slot ||
            selected.list_entry != request.bid.list_entry || selected.player != request.bid.player ||
            selected.seller != seller) {
            error = "transfer offer: selected row does not match the Native market identity";
            return std::nullopt;
        }
    }
    int bid_slot = -1;
    for (int n = 0; n < 4; ++n) {
        const auto buyer = g_.memory.u16(list + uint32_t(n) * 2);
        if (buyer == 0xffff || buyer == request.runtime_club) { bid_slot = n; break; }
    }
    const TransferOfferEditorRequest input{request.manager_slot, request.runtime_club, request.bid.list_entry,
        request.bid.source_slot, request.bid.player, seller_word, bid_slot, request.offer_is_loan};
    auto next_state = g_; auto next_game = game_; auto next_rand = rand_;
    market_dispatch_active_ = true;
    struct Reset { bool& active; ~Reset() { active = false; } } reset{market_dispatch_active_};
    try {
        auto owner = open_transfer_offer_editor_source(next_state, input, next_game, next_rand);
        if (!owner.supported) { error = owner.reason; return std::nullopt; }
        // Opening has Source G-seeding/G92..108 even for a full read-only list.
        const auto next_revision = revision + 1;
        TransferOfferEditorDispatch result;
        result.preparation = preparation_projection(next_state, next_revision);
        result.editor = transfer_offer_projection(next_state, owner, next_revision, result.preparation.desktop.gradient_text);
        ui::OutgoingTransferScreen validate(data_directory_, result.editor);
        g_ = std::move(next_state); game_ = next_game; rand_ = next_rand;
        transfer_offer_editor_ = std::move(owner); transfer_offer_revision_ = next_revision; revision = next_revision;
        return result;
    } catch (const std::exception& e) { error = e.what(); return std::nullopt; }
}

// Starts a poaching attempt for a squad slot: the attempt draws random numbers, so the revision
// advances whenever the result is not invalid; a ready attempt also opens the offer editor on
// the special poach row and remembers the attempt in poach_.
std::optional<CareerSession::PoachDispatch> CareerSession::open_poach(
    int manager_slot, int squad_slot, uint64_t& revision, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return std::nullopt; }
    error.clear();
    if (transfer_offer_editor_ || market_dispatch_active_ || pending_offer_dispatch_active_ || calendar_advance_active_ ||
        revision == std::numeric_limits<uint64_t>::max() || manager_slot < 0 || manager_slot >= 4 ||
        size_t(manager_slot) >= g_.manager_teams.size() || !g_.memory.has(::bmh::layout::ActiveManager) ||
        g_.memory.u8(::bmh::layout::ActiveManager) != manager_slot) {
        error = "poach: stale, nested or unsupported opening";
        return std::nullopt;
    }
    auto next_state = g_; auto next_game = game_; auto next_rand = rand_;
    market_dispatch_active_ = true;
    struct Reset { bool& active; ~Reset() { active = false; } } reset{market_dispatch_active_};
    try {
        PoachDispatch result;
        result.start = start_poach(next_state, manager_slot, squad_slot, next_rand);
        if (result.start.status == PoachStatus::ready) {
            const int row = 0x28 + result.start.offer_row;
            const TransferOfferEditorRequest input{manager_slot, g_.manager_teams[size_t(manager_slot)], row, 2928 + row,
                result.start.player, next_state.memory.u16(::bmh::layout::EditorList + uint32_t(row) * 0x58 + 8), 0, false, true};
            auto owner = open_transfer_offer_editor_source(next_state, input, next_game, next_rand);
            if (!owner.supported) { error = owner.reason; return std::nullopt; }
            const auto next_revision = revision + 1;
            TransferOfferEditorDispatch editor;
            editor.preparation = preparation_projection(next_state, next_revision);
            editor.editor = transfer_offer_projection(next_state, owner, next_revision, editor.preparation.desktop.gradient_text);
            result.editor = std::move(editor);
            transfer_offer_editor_ = std::move(owner);
            transfer_offer_revision_ = next_revision;
            poach_ = result.start;
            revision = next_revision;
        } else if (result.start.status != PoachStatus::invalid) {
            ++revision;  // draws, and "Transfermarkt überlastet" counts the attempt
        }
        g_ = std::move(next_state); game_ = next_game; rand_ = next_rand;
        return result;
    } catch (const std::exception& e) { error = e.what(); return std::nullopt; }
}

// Closes the table opened only to choose a poach target: needs an unchanged table owner, then
// restores the display word and drops the family owner without a family close. False with
// 'error' if stale.
bool CareerSession::close_poach_table_selection(const ui::LeagueFamilyRequest& request,std::string& error) {
    error.clear();
    if(!poach_table_selection_ || request.kind!=ui::LeagueFamilyRequestKind::close || request.from_child!=1 ||
       !canonical_table_envelope(request) || !league_family_owner_ || league_family_owner_->child!=1 ||
       league_family_owner_->identity!=request.identity || !league_family_owner_->table ||
       league_family_owner_->table->phase!=TableDispatchPhase::ready ||
       save_stream(g_)!=league_family_owner_->native || club_modal_runtime_!=league_family_owner_->runtime ||
       game_.seed()!=league_family_owner_->game_seed || game_.calls()!=league_family_owner_->game_calls ||
       rand_.seed()!=league_family_owner_->rand_seed || rand_.calls()!=league_family_owner_->rand_calls) {
        error="poach: stale Table selection close";return false;
    }
    const uint32_t at=::bmh::layout::LeagueTableDisplayModes+uint32_t(request.identity.manager)*2;
    uint16_t word=uint16_t(g_.memory.u16(at)&0x0f7f);
    if(source_runtime_byte(g_,club_modal_runtime_,exe_,::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField)!=0)word|=0x80;
    g_.memory.set16(at,word);
    if(g_.memory.has(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2))g_.memory.set8(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2,0);else club_modal_runtime_[::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2]=0;
    // FriendlyMatchScreen returns directly to19c1c. No21213 family close.
    league_family_owner_.reset();return true;
}

// Poach flow driven by UI callbacks: checks the request is a pristine open_poach request of the
// current revision (320x240 backdrop, nothing else open), then reports attempts-used or
// transfer-ban notices, or lets the player pick a squad player in a table on a cloned session
// and opens the poach from that choice. The UI acknowledges through 'accept'; any failure,
// including a session changed by the acknowledgement, restores the checkpoint and revision.
bool CareerSession::dispatch_poach_ui(const ui::TransferMarketRequest& request,
    const Image& backdrop, const PoachPlayerSelector& select, const PoachUiAccept& accept,
    uint64_t& revision, std::string& error) {
    error.clear();
    if (!accept || request.kind!=ui::TransferMarketRequestKind::open_poach ||
        !g_.memory.has(::bmh::layout::GameMode) || g_.memory.u8(::bmh::layout::GameMode)!=3 ||
        request.revision!=revision || revision==UINT64_MAX || request.manager_slot<0 || request.manager_slot>3 ||
        !g_.memory.has(::bmh::layout::ActiveManager) || g_.memory.u8(::bmh::layout::ActiveManager)!=request.manager_slot ||
        size_t(request.manager_slot)>=g_.manager_teams.size() ||
        request.runtime_club!=g_.manager_teams[size_t(request.manager_slot)] ||
        request.bid.manager!=request.manager_slot || request.bid.list_entry!=-1 || request.bid.player!=-1 ||
        request.bid.source_slot!=-1 || request.bid.fee!=0 || request.bid.annual_salary!=0 ||
        request.bid.contract_years!=1 || request.bid.poach || std::any_of(request.bid.bonuses.begin(),request.bid.bonuses.end(),
            [](uint32_t n){return n!=0;}) || request.squad_row!=-1 || request.expected_player!=-1 ||
        request.preview_source_slot!=-1 || request.preview_seller!=-1 || request.preview_is_purchase ||
        request.preview_is_loan || !request.selected_players.empty() ||
        backdrop.width!=320 || backdrop.height!=240 || advertising_renewal_dispatch_active_ || pending_offer_dispatch_active_ || market_dispatch_active_ ||
        calendar_advance_active_ || transfer_offer_editor_ || poach_ || manager_join_ || contract_quote_ ||
        league_family_owner_ || league_pairings_draft_ || strengths_owner_ || top_scorers_owner_ ||
        other_games_owner_ || balance_draft_ || stadium_.opened ||
        (turn_driver_.stage!=TurnDriver::Stage::MatchdayDesks && turn_driver_.stage!=TurnDriver::Stage::SeasonEndDesk)) {
        error="poach: stale or competing selection";return false;
    }
    const auto checkpoint=advertising_session_checkpoint(revision);
    try {
        const int manager=request.manager_slot;
        const size_t record=size_t(manager)*0xb0c;
        PoachDispatch result;
        result.start.manager=manager;
        // These three notices precede the Table modal in Reset.
        if (record+0x8c0>g_.manager_records.size()) throw FormatError("poach: missing manager record");
        const bool attempts_used=(g_.manager_records[record+0x8be] |
            uint16_t(g_.manager_records[record+0x8bf])<<8)!=0xffff;
        if(attempts_used) {
            result.start.status=PoachStatus::attempts_used;
            result.start.messages.push_back(message_text(g_,::bmh::layout::CareerSession_DispatchPoachUi_TextPointer2));
        } else if(g_.manager_records[record+0x220]>>5==1) {
            result.start.status=PoachStatus::transfer_ban;
            result.start.messages.push_back(message_text(g_,::bmh::layout::CareerSession_DispatchPoachUi_TextPointer));
        } else if(request.offer_is_loan) {
            auto services=staged_club_modal_services(g_,game_,rand_,club_modal_runtime_);
            services.set_runtime_byte(::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField,0);
            services.set_runtime_byte(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField2,g_.manager_records[record+0x210]);
            services.set_runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField,2);
            result.start.messages.push_back(message_text(g_,::bmh::layout::CareerSession_DispatchPoachUi_TextPointer3));
        } else {
            if(!select) throw FormatError("poach: Source player selection presenter unavailable");
            // The cloned Session owns the existing50775 controls implementation.
            // begin_league_family(child1) itself does not call53435; only the
            // host Table owner is reused, never prepare/close-family effects.
            CareerSession staged=*this;
            staged.poach_table_selection_=true;
            uint64_t selection_revision=revision;
            auto services=staged.staged_club_modal_services(staged.g_,staged.game_,staged.rand_,staged.club_modal_runtime_);
            services.set_runtime_byte(::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField,0);
            services.set_runtime_byte(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField2,g_.manager_records[record+0x210]);
            services.set_runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField,2);
            services.set_runtime_byte(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField,0);services.set_runtime_byte(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2,0);
            //50775 control7 toggles this existing BSS byte. Materialize its
            // actual current value in the staged runtime, including when it
            // was read from the executable rather than persisted in Native.
            services.set_runtime_byte(::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField,services.runtime_byte(::bmh::layout::CareerSession_AdvanceLeagueFamily_ExecutableField));
            auto entry=staged.strengths_open_request(selection_revision);
            if(!entry)throw FormatError("poach: missing Table selection identity");
            entry->identity.league=g_.manager_records[record+0x210];entry->identity.league_group=0;
            if(!staged.begin_league_family(entry->identity,1,selection_revision,error))throw FormatError(error);
            market_dispatch_active_=true;
            const auto live_baseline=advertising_session_checkpoint(revision);
            const auto selection=select(staged,services,manager,selection_revision,backdrop);
            if(!advertising_session_unchanged(live_baseline,revision))
                throw FormatError("poach: selection changed the live session");
            auto& next=staged.g_;
            if(next.date!=g_.date || next.manager_teams!=g_.manager_teams ||
               !next.memory.has(::bmh::layout::ActiveManager) || next.memory.u8(::bmh::layout::ActiveManager)!=manager)
                throw FormatError("poach: selection changed its manager/calendar context");
            market_dispatch_active_=false;
            int slot=-1;
            if(selection) {
                if(selection->origin!=ClubPlayerDetailOrigin::squad || selection->human_loan_index!=-1)
                    throw FormatError("poach: selection is not an ordinary squad row");
                const auto player=club_player_detail_context(next,*selection);
                slot=player.source_slot;
                if(slot<0 || slot>=0xb70 || slot_team(slot)!=selection->runtime_team ||
                   (next.squad_slots.at(size_t(slot))&0xfff)!=selection->player_id)
                    throw FormatError("poach: stale player selection");
            }
            // Preserve actual Table/ClubInfo/card effects, including a cancelled
            // selection. No LeagueScreenShown entry and no family-close RNG stir.
            g_=std::move(next);game_=staged.game_;rand_=staged.rand_;club_modal_runtime_=std::move(staged.club_modal_runtime_);
            if(selection) {
                auto opened=open_poach(manager,slot,revision,error);
                if(!opened) throw FormatError(error);
                result=std::move(*opened);
            }
            if(revision==checkpoint.revision) ++revision;
        }
        const auto accepted_baseline=advertising_session_checkpoint(revision);
        accept(result,preparation_projection(revision));
        if(!advertising_session_unchanged(accepted_baseline,revision))
            throw FormatError("poach: UI acknowledgement changed the session");
        return true;
    } catch(const std::exception& e) {
        restore_advertising_session_checkpoint(checkpoint);revision=checkpoint.revision;
        error=e.what();return false;
    }
}

// Applies one editor event (field quote change, submit, cancel, team info, player info) to the
// open offer editor: identity and quote must match the owner and the posted row must equal the
// single field change. Runs on copies; the revision rises only if state, runtime bytes or a
// random generator actually changed. A closing editor ends a poach attempt and clears the owner.
std::optional<CareerSession::TransferOfferEditorDispatch> CareerSession::dispatch_transfer_offer(
    const ui::OutgoingTransferRequest& request, const HumanBuyerSaleHooks& presenters,
    uint64_t& revision, std::string& error,
    const TransferOfferTeamInfoPresenter& team_info,
    const TransferOfferPlayerInfoPresenter& player_info) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return std::nullopt; }
    error.clear();
    if (!transfer_offer_editor_ || market_dispatch_active_ || pending_offer_dispatch_active_ || calendar_advance_active_ ||
        request.identity.revision != revision || transfer_offer_revision_ != revision ||
        revision == std::numeric_limits<uint64_t>::max()) {
        error = "transfer offer: stale or nested editor event"; return std::nullopt;
    }
    const auto& current = *transfer_offer_editor_;
    const auto& id = request.identity;
    const int seller = current.request.expected_seller_word == 0xffff ? 0x0fff : current.request.expected_seller_word & 0x7fff;
    if (id.manager_slot != current.request.manager || id.runtime_club != current.request.runtime_club ||
        id.list_entry != current.request.list_entry || id.source_slot != current.request.source_slot ||
        id.player != current.request.expected_player || id.seller != seller || id.bid_slot != current.bid_slot ||
        request.quoted_fee != current.quoted_fee || request.quoted_monthly_salary != current.quoted_salary ||
        request.quoted_years != current.years) {
        error = "transfer offer: mismatched editor identity or quote"; return std::nullopt;
    }
    uint32_t fee = current.fee, salary = current.salary;
    uint8_t years = current.years; auto bonuses = current.bonuses;
    bool quote_loan = current.quote_loan;
    const unsigned field = unsigned(request.field);
    if (request.kind == ui::OutgoingTransferRequestKind::quote_change) {
        if (field > unsigned(ui::OutgoingTransferField::quote_loan)) {
            error = "transfer offer: invalid editor field"; return std::nullopt;
        }
        if (request.accepted) {
            if (field == 0) fee = request.value;
            else if (field == 1) salary = request.value;
            else if (field == 2) years = uint8_t(request.value);
            else if (field < 10) bonuses[field - 3] = uint8_t(field < 6 ? request.value : request.value / 5);
            else quote_loan = !quote_loan;
        }
    } else if (request.kind != ui::OutgoingTransferRequestKind::submit && request.kind != ui::OutgoingTransferRequestKind::cancel &&
               request.kind != ui::OutgoingTransferRequestKind::team_info &&
               request.kind != ui::OutgoingTransferRequestKind::player_info) {
        error = "transfer offer: unsupported editor event"; return std::nullopt;
    }
    if (request.fee != fee || request.monthly_salary != salary || request.duration_years != years ||
        request.bonuses != bonuses || request.quote_loan != quote_loan) {
        error = "transfer offer: posted row differs from the single Source field event"; return std::nullopt;
    }
    if(request.kind==ui::OutgoingTransferRequestKind::team_info&&!team_info) {
        error="transfer offer: TeamInfo presenter unavailable"; return std::nullopt;
    }
    if(request.kind==ui::OutgoingTransferRequestKind::player_info&&!player_info) {
        error="transfer offer: PlayerInfo presenter unavailable"; return std::nullopt;
    }
    auto next_state = g_; auto next_game = game_; auto next_rand = rand_;
    auto next_runtime=club_modal_runtime_;
    auto owner = current; TransferActionResult action;
    auto hooks = presenters;
    switch (turn_driver_.transfer_allocator_context) {
    case PendingOfferAllocatorGate::zero: hooks.allocator_gate = HumanBuyerSaleAllocatorGate::zero; break;
    case PendingOfferAllocatorGate::blocked: hooks.allocator_gate = HumanBuyerSaleAllocatorGate::blocked; break;
    default: hooks.allocator_gate = HumanBuyerSaleAllocatorGate::unsupported; break;
    }
    hooks.direct_mode = HumanBuyerSaleDirectMode::ordinary_ai_purchase;
    hooks.before_accounts = {}; hooks.after_paid_accounts = {};
    market_dispatch_active_ = true;
    struct Reset { bool& active; ~Reset() { active = false; } } reset{market_dispatch_active_};
    try {
        if (request.kind == ui::OutgoingTransferRequestKind::quote_change) {
            owner = edit_transfer_offer_from_ui(next_state, current, request, next_game, next_rand);
        } else if (request.kind == ui::OutgoingTransferRequestKind::cancel) {
            owner = cancel_transfer_offer_editor_source(next_state, current);
        } else if(request.kind==ui::OutgoingTransferRequestKind::team_info) {
            // Validate the complete old Native row before any modal effect.
            const auto validation=cancel_transfer_offer_editor_source(next_state,current);
            if(!validation.supported) {error=validation.reason;return std::nullopt;}
            auto services=staged_club_modal_services(next_state,next_game,next_rand,next_runtime);
            const auto parent=transfer_offer_projection(next_state,current,revision,
                next_state.memory.u8(::bmh::layout::TextGradientDisabled)==0);
            const Image backdrop=ui::OutgoingTransferScreen(data_directory_,parent).render();
            if(!team_info(services,current.request.runtime_club,backdrop)) {
                error="transfer offer: TeamInfo presenter stopped";return std::nullopt;
            }
            owner=resume_transfer_offer_team_info_source(next_state,current,next_rand);
        } else if(request.kind==ui::OutgoingTransferRequestKind::player_info) {
            const auto selection=transfer_offer_player_info_selection_source(next_state,current);
            auto services=staged_club_modal_services(next_state,next_game,next_rand,next_runtime);
            const auto parent=transfer_offer_projection(next_state,current,revision,
                next_state.memory.u8(::bmh::layout::TextGradientDisabled)==0);
            const Image backdrop=ui::OutgoingTransferScreen(data_directory_,parent).render();
            services.set_runtime_byte(::bmh::layout::CareerSession_DispatchPoachUi_ExecutableField,1); // Source direct entry persists, unlike4f548.
            if(!player_info(services,selection,backdrop)) {
                error="transfer offer: PlayerInfo presenter stopped";return std::nullopt;
            }
            owner=resume_transfer_offer_player_info_source(next_state,current,
                services.runtime_byte(::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableField),services.runtime_byte(::bmh::layout::CareerSession_DispatchTransferOffer_ExecutableField2),next_rand);
        } else {
            MinuteReport report;
            auto submitted = submit_transfer_offer_editor_source(next_state, current, next_rand, hooks, report);
            owner = std::move(submitted.editor); action = submitted.action;
            if (report.unsupported) { error = report.reason; return std::nullopt; }
        }
        if (!owner.supported) { error = owner.reason; return std::nullopt; }
        // a poaching editor that closes frees its special row and counts the attempt (Reset)
        if (owner.closed && poach_) finish_poach(next_state, *poach_);
        const bool changed = save_stream(next_state) != save_stream(g_) || next_runtime != club_modal_runtime_ ||
            next_game.seed() != game_.seed() || next_game.calls() != game_.calls() ||
            next_rand.seed() != rand_.seed() || next_rand.calls() != rand_.calls();
        const auto next_revision = revision + uint64_t(changed);
        TransferOfferEditorDispatch result;
        result.preparation = preparation_projection(next_state, next_revision);
        result.editor = transfer_offer_projection(next_state, owner, next_revision, result.preparation.desktop.gradient_text);
        result.action = action;
        if (!owner.closed) { ui::OutgoingTransferScreen validate(data_directory_, result.editor); }
        g_ = std::move(next_state); game_ = next_game; rand_ = next_rand;
        club_modal_runtime_=std::move(next_runtime);
        if (owner.closed) { transfer_offer_editor_.reset(); poach_.reset(); } else transfer_offer_editor_ = std::move(owner);
        transfer_offer_revision_ = next_revision; revision = next_revision;
        return result;
    } catch (const std::exception& e) { error = e.what(); return std::nullopt; }
}

// Quotes a purchase, lender snapshot or squad row preview (the listing identity is re-checked)
// using a copy of the W random stream; the stream and revision are updated only if the quote
// and its projection both succeed.
std::optional<CareerSession::TransferMarketPreviewDispatch> CareerSession::preview_transfer_market(
    const ui::TransferMarketRequest& request, uint64_t& revision, std::string& error) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return std::nullopt; }
    error.clear();
    if (request.kind != ui::TransferMarketRequestKind::preview_quote ||
        transfer_offer_editor_ || market_dispatch_active_ || pending_offer_dispatch_active_ || calendar_advance_active_ ||
        request.revision != revision || revision == std::numeric_limits<uint64_t>::max() ||
        request.manager_slot < 0 || request.manager_slot >= 4 ||
        size_t(request.manager_slot) >= g_.manager_teams.size() ||
        !g_.memory.has(::bmh::layout::ActiveManager) || g_.memory.u8(::bmh::layout::ActiveManager) != request.manager_slot ||
        request.runtime_club != g_.manager_teams[size_t(request.manager_slot)]) {
        error = "transfer market: stale or nested preview";
        return std::nullopt;
    }
    if (request.preview_is_purchase) {
        if (request.preview_snapshot_index != -1 || request.squad_row != -1 || request.bid.manager != request.manager_slot ||
            request.bid.list_entry < 0 || request.bid.list_entry >= 40 ||
            request.bid.source_slot != request.preview_source_slot ||
            request.preview_source_slot != 0xb70 + request.bid.list_entry ||
            request.bid.player != request.expected_player) {
            error = "transfer market: mismatched preview listing identity";
            return std::nullopt;
        }
    } else if (request.preview_snapshot_index >= 0) {
        const auto identity = transfer_market_snapshot_preview(g_, request.manager_slot,
            request.runtime_club, request.preview_snapshot_index);
        if (!identity || identity->displayed_row != request.squad_row || identity->player != request.expected_player ||
            identity->source_slot != request.preview_source_slot || request.preview_seller != request.runtime_club) {
            error = "transfer market: mismatched lender snapshot identity";
            return std::nullopt;
        }
    } else {
        const auto range = squad_range(request.runtime_club);
        if (request.preview_snapshot_index != -1 || request.squad_row < 0 || size_t(request.squad_row) >= range.count ||
            request.preview_source_slot != int(range.first + size_t(request.squad_row)) ||
            request.preview_seller != request.runtime_club) {
            error = "transfer market: mismatched preview roster identity";
            return std::nullopt;
        }
    }
    auto next_rand = rand_;
    market_dispatch_active_ = true;
    struct Reset { bool& active; ~Reset() { active = false; } } reset{market_dispatch_active_};
    try {
        const TransferMarketPreviewRequest input{request.manager_slot, request.runtime_club,
            request.preview_source_slot, request.expected_player, request.preview_seller,
            request.preview_is_purchase, request.preview_is_loan, request.preview_snapshot_index};
        const auto result = quote_transfer_market_preview_source(g_, input, next_rand);
        if (!result.supported) {
            error = result.reason.empty() ? "transfer market: unsupported Source preview" : result.reason;
            return std::nullopt;
        }
        const bool changed = next_rand.seed() != rand_.seed() || next_rand.calls() != rand_.calls();
        const auto next_revision = revision + uint64_t(changed);
        TransferMarketPreviewDispatch dispatch;
        dispatch.preparation = preparation_projection(next_revision);
        if (!dispatch.preparation.transfer_market)
            throw FormatError("transfer market: preview has no ordinary projection");
        dispatch.quote = {request.manager_slot, request.runtime_club, request.preview_source_slot,
            request.expected_player, request.preview_seller, next_revision,
            result.delta, result.projected_balance, request.preview_is_purchase, request.preview_is_loan, request.preview_snapshot_index};
        // Projection construction can throw. Keep the live W stream and revision
        // unchanged until both the Source result and the projection are valid.
        rand_ = next_rand;
        revision = next_revision;
        return dispatch;
    } catch (const std::exception& e) {
        error = e.what();
        return std::nullopt;
    }
}

// Direct market actions of a human manager: review offers, submit a bid, list a player. Needs
// a request of the current revision for the active manager's club; loans and marked batches are
// not accepted here. The allocator gate comes from the turn driver, never from the presenter.
// The staged copy is committed (revision plus one) only if something changed.
std::optional<TransferActionResult> CareerSession::dispatch_transfer_market(
    const ui::TransferMarketRequest& request, const HumanBuyerSaleHooks& presenters,
    uint64_t& revision, std::string& error,
    const std::function<int(const GameState&, const TransferOfferListView&)>& review) {
    if (balance_draft_ || balance_dispatch_active_ || strengths_owner_ || strengths_dispatch_active_ ||
        top_scorers_owner_ || top_scorers_dispatch_active_ || stadium_.block_expansion || other_games_owner_ || other_games_dispatch_active_) { error = "strengths_active"; return std::nullopt; }
    error.clear();
    if (transfer_offer_editor_ || market_dispatch_active_ || pending_offer_dispatch_active_ || calendar_advance_active_ ||
        request.revision != revision || revision == std::numeric_limits<uint64_t>::max() ||
        request.manager_slot < 0 || request.manager_slot >= 4 ||
        size_t(request.manager_slot) >= g_.manager_teams.size() ||
        !g_.memory.has(::bmh::layout::ActiveManager) || g_.memory.u8(::bmh::layout::ActiveManager) != request.manager_slot ||
        request.runtime_club != g_.manager_teams[size_t(request.manager_slot)]) {
        error = "transfer market: stale or nested request";
        return std::nullopt;
    }
    if (request.kind == ui::TransferMarketRequestKind::review_offers) {
        if (!review || request.bid.manager != request.manager_slot || request.bid.list_entry < 0 ||
            request.bid.list_entry >= 40 || request.bid.source_slot != 0xb70 + request.bid.list_entry ||
            request.squad_row != -1 || !request.selected_players.empty()) {
            error = "transfer market: invalid offer review"; return std::nullopt;
        }
        const auto view = project_transfer_offer_list(g_, request.bid.list_entry);
        if (!view.valid || view.player != request.bid.player || view.seller != request.runtime_club ||
            view.seller_manager != request.manager_slot) {
            error = "transfer market: stale seller listing"; return std::nullopt;
        }
    } else if (request.kind == ui::TransferMarketRequestKind::submit_bid) {
        if (request.bid.manager != request.manager_slot || request.squad_row != -1 ||
            request.expected_player != -1 ||
            std::any_of(request.bid.bonuses.begin(), request.bid.bonuses.end(),
                        [](uint32_t value) { return value > 255; })) {
            error = "transfer market: invalid buyer request";
            return std::nullopt;
        }
    } else if (request.kind == ui::TransferMarketRequestKind::list_player) {
        const auto range = squad_range(request.runtime_club);
        if (request.squad_row < 0 || size_t(request.squad_row) >= range.count ||
            range.first + size_t(request.squad_row) >= g_.squad_slots.size() ||
            request.expected_player < 0 || request.expected_player >= 0xfff ||
            (g_.squad_slots[range.first + size_t(request.squad_row)] & 0xfff) != request.expected_player) {
            error = "transfer market: stale seller row";
            return std::nullopt;
        }
    } else {
        error = "transfer market: unsupported action";
        return std::nullopt;
    }
    // Reset sends an owned marked player through the same Size2
    // listing for both VER-KAUFEN and VER-LEIHEN. Only a buyer's OpenTransferEditor
    // offer owns the actual loan term; listing does not create a loan record.
    if ((request.offer_is_loan && request.kind != ui::TransferMarketRequestKind::list_player) ||
        request.selected_players.size() > 1) {
        error = "transfer market: this caller does not own buyer loan or marked batch processing";
        return std::nullopt;
    }
    if (!request.selected_players.empty()) {
        const auto& selected = request.selected_players.front();
        const bool matches = request.kind == ui::TransferMarketRequestKind::list_player
            ? selected.squad_row == request.squad_row && selected.player == request.expected_player &&
              selected.seller == request.runtime_club &&
              selected.source_slot == int(squad_range(request.runtime_club).first + size_t(request.squad_row))
            : selected.list_entry == request.bid.list_entry && selected.source_slot == request.bid.source_slot &&
              selected.player == request.bid.player && selected.squad_row == -1;
        if (!matches) {
            error = "transfer market: mismatched marked selection";
            return std::nullopt;
        }
    }

    auto next_state = g_;
    auto next_game = game_;
    auto next_rand = rand_;
    auto hooks = presenters;
    // A presenter cannot supply the allocator gate or insert the special
    // pending caller's severance/ACK/news sequence into a direct purchase.
    switch (turn_driver_.transfer_allocator_context) {
    case PendingOfferAllocatorGate::zero: hooks.allocator_gate = HumanBuyerSaleAllocatorGate::zero; break;
    case PendingOfferAllocatorGate::blocked: hooks.allocator_gate = HumanBuyerSaleAllocatorGate::blocked; break;
    default: hooks.allocator_gate = HumanBuyerSaleAllocatorGate::unsupported; break;
    }
    // Reset -> OpenTransferEditor establishes DeskActions_EditorFail_RuntimeField2=0 at LAB1b67e before
    // resolving the AI seller. This explicitly selected App entry supplies
    // that transient ownership; a missing FBM byte is never the observation.
    hooks.direct_mode = HumanBuyerSaleDirectMode::ordinary_ai_purchase;
    hooks.before_accounts = {};
    hooks.after_paid_accounts = {};
    market_dispatch_active_ = true;
    struct Reset { bool& active; ~Reset() { active = false; } } reset{market_dispatch_active_};
    MinuteReport report;
    try {
        const auto before = save_stream(g_);
        TransferActionResult result;
        if (request.kind == ui::TransferMarketRequestKind::review_offers) {
            const auto view = project_transfer_offer_list(next_state, request.bid.list_entry);
            result.player = view.player; result.list_entry = view.list_entry;
            result.status = TransferActionStatus::pending;
            // VERKAUFEN on a listed player: "Wollen Sie <name> vom Transfermarkt holen?". Yes frees the list
            // slot (the player stays in the squad), no goes on to the offers.
            bool withdrawn = false;
            if (request.ask_withdraw && hooks.confirm) {
                const std::string question = message_text(next_state, ::bmh::layout::TransferMarket_WithdrawQuestion_TextPointer) +
                    next_state.player_name(view.player) +
                    message_text(next_state, ::bmh::layout::TransferMarket_WithdrawQuestion_TextPointer2);
                if (hooks.confirm(next_state, request.manager_slot, question)) {
                    next_state.squad_slots.at(0xb70u + size_t(view.list_entry)) = 0xfff;
                    withdrawn = true;
                }
            }
            const int selected = !withdrawn && view.offer_count ? review(next_state, view) : -1;
            if (selected != -1) {
                auto allocator = TransferAllocatorContext::unsupported;
                if (turn_driver_.transfer_allocator_context == PendingOfferAllocatorGate::zero)
                    allocator = TransferAllocatorContext::zero;
                else if (turn_driver_.transfer_allocator_context == PendingOfferAllocatorGate::blocked)
                    allocator = TransferAllocatorContext::blocked;
                result = accept_manager_transfer_offer(next_state, request.manager_slot,
                    view.list_entry, view.player, selected, next_rand, report, allocator);
            }
        } else if (request.kind == ui::TransferMarketRequestKind::list_player)
            result = list_manager_player(next_state, request.manager_slot, request.squad_row, next_rand, report);
        else
            result = submit_transfer_bid_source_human_buyer(next_state, request.bid, next_rand, hooks, report);
        if (report.unsupported || result.status == TransferActionStatus::unsupported) {
            error = report.reason.empty() ? "transfer market: unsupported Source sale" : report.reason;
            return std::nullopt;
        }
        const bool changed = save_stream(next_state) != before ||
            next_rand.seed() != rand_.seed() || next_rand.calls() != rand_.calls() ||
            next_game.seed() != game_.seed() || next_game.calls() != game_.calls();
        if (changed) {
            // Pure projections reject inconsistent squad/board/training state
            // before any staged notice effects become live.
            (void)ui::preparation_from_game(next_state, exe_, revision + 1);
            g_ = std::move(next_state);
            game_ = next_game;
            rand_ = next_rand;
            ++revision;
        }
        return result;
    } catch (const std::exception& e) {
        error = e.what();
        return std::nullopt;
    }
}

}  // namespace bmh::app
