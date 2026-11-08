// SPDX-License-Identifier: GPL-3.0-only
// Transfer market screen with three views: market listing, own-squad selling and offer review. TransferMarketState
// is an owned projection (items, squad, previews, club roster) so no GameState pointer is kept. Actions (bid,
// list player, open offer, quote preview, poach, review offers) become TransferMarketRequests that the caller
// applies through the desk actions, then it attaches a new projection or a rejection. Implemented in transfer_market.cpp.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/textd.hpp"
#include "ui/club_info.hpp"
#include "ui/game_end.hpp"
#include "game/poach.hpp"
#include "ui/sprite_font.hpp"
#include "game/desk_actions.hpp"
#include "game/search_profile.hpp"
#include "ui/manager.hpp"
#include "ui/setup.hpp"

namespace bmh { struct GameState; }
namespace bmh::ui {

struct TransferMarketItem {
    int list_entry = -1, player = -1, seller = -1;
    int age = 0, position = 0, strength = 0;
    std::string name, seller_name;
    int32_t asking_fee = 0;
    uint32_t salary_reference = 0;
    uint8_t salary_demand_percent = 0;
    int offers = 0;
    bool own_listing = false, own_bid_pending = false;
    int source_slot = -1; // Absolute SquadSlots identity supplied by the projection.
    std::optional<TransferBidRequest> own_bid;
    Player attributes;
    uint8_t nationality = 0;
    uint16_t source_word = 0x0fff;
};

struct TransferMarketSquadItem {
    int row = -1, player = -1;
    uint16_t slot = 0;
    std::string name;
    int age = 0, position = 0, strength = 0;
    bool already_listed = false;
};

struct TransferMarketState {
    int manager_slot = -1, runtime_club = -1;
    uint64_t revision = 0;
    std::string manager_name, club_name, source;
    int32_t balance = 0, credit_line = 0;
    std::vector<TransferMarketItem> items;
    std::vector<TransferMarketSquadItem> squad;
    std::vector<TransferMarketSnapshotPreview> snapshot_previews;
    // Owned Native inputs for LineupScreen; no transient GameState pointer.
    ClubInfoProjection roster;
    uint16_t source_view_flags = 0x1f0;
    bool gradient_text = true, decorated_background = true, textured_panes = true;
    uint8_t source_market_mode = 0; // Unknown is not an inferred Mode III.
    std::optional<std::pair<int32_t,int32_t>> currency;
    std::array<uint16_t,5> active_strengths{},team_strengths{};
    // Search profile of the manager (buttons Suchen / Vorschlag): the stored profile and the two suggestions the squad
    // asks for (index 0 = first eleven, 1 = whole squad, chosen by the Komplett-style view flag 0x400).
    SearchProfile search_profile{};
    std::array<SearchProfile,2> search_suggestion{};
};

TransferMarketState transfer_market_from_game(const GameState&, uint64_t revision);
std::string transfer_market_state_json(const TransferMarketState&);
TaxDialogProjection poach_notice_from_game(const GameState&,const PoachStart&,std::string original_message);

enum class TransferMarketRequestKind { none, submit_bid, list_player, open_offer, preview_quote, open_poach, review_offers,
                                       store_search_profile };
struct TransferMarketSelection {
    int list_entry=-1,source_slot=-1,player=-1,seller=-1,squad_row=-1;
};
struct TransferMarketRequest {
    TransferMarketRequestKind kind = TransferMarketRequestKind::none;
    int manager_slot = -1, runtime_club = -1;
    uint64_t revision = 0;
    TransferBidRequest bid{};
    int squad_row = -1, expected_player = -1;
    int preview_source_slot = -1;
    int preview_seller = -1;
    bool preview_is_purchase = false;
    bool preview_is_loan = false;
    int preview_snapshot_index = -1;
    bool offer_is_loan = false;
    std::vector<TransferMarketSelection> selected_players;
    // store_search_profile: the edited profile when the search dialog is closed; `search_cancelled` = left by right click
    // (the profile is kept but switched off), `search_apply` = closed with the "Suchen" button (list the matches).
    SearchProfile search_profile{};
    bool search_cancelled = false, search_apply = false;
    // review_offers started by VERKAUFEN on an already listed own player: the original first asks
    // "Wollen Sie <name> vom Transfermarkt holen?" (yes = off the list, no = look at the offers).
    bool ask_withdraw = false;
};
std::string transfer_market_request_json(const TransferMarketRequest&);

// TransferMarketScreen quote supplied by the App owner after42307(...,loan,100,1,0).
// These are Source signed32 rounded delta and wrapped balance, never a UI RNG
// estimate. The quote identity must still match the outstanding selected row.
struct TransferMarketPreview {
    int manager_slot=-1,runtime_club=-1,source_slot=-1,player=-1,seller=-1;
    uint64_t revision=0;
    int32_t delta=0,projected_balance=0;
    bool purchase=false,loan=false;
    int snapshot_index=-1;
};

// UI-only desk screen. Requests carry the authoritative identity and offer fields;
// callers apply them through desk_actions and attach a new projection or rejection.
class TransferMarketScreen {
public:
    TransferMarketScreen(const std::filesystem::path&, TransferMarketState);
    Image render() const;
    TransferMarketRequest click(int x, int y, bool right = false);
    // Mouse DOWN on a button: the button is drawn pressed until the release (click). Returns whether the picture changed.
    bool press(int x, int y, bool right = false);
    // TransferMarketScreen hover preview; host acknowledges the quote before more input.
    TransferMarketRequest move(int x, int y);
    TransferMarketRequest key(std::string_view);
    bool text(std::string_view);
    bool close_requested() const { return close_requested_; }
    // The search dialog (buttons Suchen / Vorschlag) is open: it owns every click until it is closed.
    bool search_open() const { return search_open_; }
    const SearchProfile& search_edit() const { return search_edit_; }
    const TransferMarketState& state() const { return state_; }
    const std::optional<TransferMarketRequest>& pending() const { return pending_; }
    void acknowledge(TransferMarketState, std::string notice = {});
    // A presented Source refusal may consume the request without changing
    // Native state, RNG or revision. Keep the existing projection in that case.
    void acknowledge_unchanged();
    void acknowledge_preview(TransferMarketState, TransferMarketPreview);
    void refresh_after_offer(TransferMarketState);
    void reject_action(std::string reason);
    std::string state_json() const;
private:
    enum class View { market, sell_squad, offer };
    TransferMarketState state_;
    Font font_;
    LeImage exe_;
    TextDb texts_;
    SpriteFont sprite_font_;
    Image background_, frame_, widgets_, atlas_, flags_, market_widgets_, search_panel_;
    uint16_t view_flags_ = 0x1f0;
    bool data_b_ = false;
    size_t market_page_ = 0;
    std::vector<int> market_order() const;
    std::vector<std::pair<int,int>> roster_rows() const;
    void original_text(Image&,int x,int baseline,std::string_view,uint8_t color,int right=-1) const;
    void button(Image&,int x,int y,int width,std::string_view,bool active) const;
    void draw_roster(Image&,bool full) const;
    void draw_market_overview(Image&) const;
    std::string original_money(int32_t) const;
    View view_ = View::market;
    std::optional<int> selected_item_, selected_squad_, selected_snapshot_;
    std::vector<int> marked_items_,marked_squad_;
    std::optional<TransferMarketRequest> pending_;
    std::optional<TransferMarketPreview> preview_;
    std::array<uint32_t, 10> offer_values_{}; // fee, salary, years, seven bonuses
    std::string numeric_input_, feedback_; // feedback is UTF-8, like host notices.
    int active_field_ = 0;
    bool close_requested_ = false;
    bool search_open_ = false;
    SearchProfile search_edit_{};
    // The button held by the mouse (draw id: 1..12 market buttons, 100 Data, 101 Komplett, 200 search confirm), -1 none.
    int pressed_button_ = -1;
    int button_at(int x, int y) const;
    void open_search(SearchProfile profile);
    void draw_search(Image&) const;
    TransferMarketRequest click_search(int x, int y, bool right);
    TransferMarketRequest close_search(bool cancelled);
    void draw_text(Image&, int x, int y, std::string_view, uint8_t color) const;
    void commit_numeric_field();
    TransferMarketRequest make_review(const TransferMarketItem&, bool ask_withdraw = false);
    TransferMarketRequest make_bid();
    TransferMarketRequest make_listing(bool loan=false);
    TransferMarketRequest make_open_offer(bool loan=false);
    TransferMarketRequest make_preview(bool purchase);
};

} // namespace bmh::ui
