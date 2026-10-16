// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <optional>
#include "formats/font.hpp"
#include "formats/le.hpp"
#include "game/league_pairings.hpp"
#include "ui/sprite_font.hpp"
#include "ui/league_family_request.hpp"

namespace bmh::ui {
struct LeaguePairingsIdentity {
    int manager=-1,runtime_club=-1,club_identity=-1,league=-1,round=-1;
    uint64_t revision=0,scope=0,sequence=0;
    bool operator==(const LeaguePairingsIdentity&) const=default;
};
struct LeaguePairingsRequest {
    LeaguePairingsIdentity identity;
    LeaguePairingsEvent event;
    bool operator==(const LeaguePairingsRequest& b) const {
        return identity==b.identity&&event.kind==b.event.kind&&event.value==b.event.value&&
            event.expected_club_identity==b.event.expected_club_identity;
    }
};
struct LeaguePairingsDisplayRow {
    std::array<std::string,2> names,details;
    std::array<uint8_t,2> name_colors{};
    std::string result,first_leg,scheduled_ordinal,scheduled_date;
    int result_x=265;
    std::optional<LeaguePairingsActiveRow> active;
    std::string temperature; // Original bytes, formatted while GameState exists.
};
struct LeaguePairingsState {
    LeaguePairingsIdentity identity;
    LeaguePairingsView source;
    std::string title,subtitle,date;
    std::array<std::string,8> buttons;
    std::vector<LeaguePairingsDisplayRow> rows;
    bool decorated=true,original_panel=true,gradient=true,secondary_leagues=true;
    std::optional<LeaguePairingsActivePacket> active_packet;
};
LeaguePairingsConstants league_pairings_constants(const LeImage&);
LeaguePairingsState league_pairings_from_game(const GameState&,const LeaguePairingsOwner&,
    const LeImage&,uint64_t revision,uint64_t scope,uint64_t sequence,
    const std::optional<LeaguePairingsActivePacket>& active_packet={});
uint64_t league_pairings_source_seconds(uint64_t milliseconds);

class LeaguePairingsScreen {
public:
    struct FamilyMode { StrengthsIdentity identity; };
    LeaguePairingsScreen(const std::filesystem::path&,LeaguePairingsState);
    LeaguePairingsScreen(const std::filesystem::path&,LeaguePairingsState,FamilyMode);
    Image render() const;
    std::string state_json() const;
    std::optional<LeaguePairingsEvent> selector_event(int x,int y) const;
    bool pointer_down(int x,int y,bool right=false);
    bool release(int x,int y);
    bool move(int x,int y);
    bool tick_source_seconds(uint64_t now);
    void mark_presented_source_seconds(uint64_t now);
    const std::optional<LeaguePairingsRequest>& pending() const {return pending_;}
    const LeaguePairingsState& state() const {return state_;}
    void reject_action(std::string reason);
    const std::optional<LeagueFamilyRequest>& pending_family_action() const { return pending_family_action_; }
    void reject_family_action(std::string reason);
private:
    int button_at(int x,int y) const;
    int arrow_at(int x,int y) const;
    bool in_arrow(int arrow,int x,int y) const;
    void post(LeaguePairingsEvent);
    void post_family(LeagueFamilyRequest);
    void selector(Image&,int x,int y,int style) const;
    void text(Image&,int x,int baseline,std::string_view,uint8_t color) const;
    LeImage exe_;
    Image frame_,panel_,arrows_,widgets_,weather_;
    Font font_;
    SpriteFont sprite_;
    LeaguePairingsState state_;
    std::optional<LeaguePairingsRequest> pending_;
    std::optional<FamilyMode> family_;
    std::optional<LeagueFamilyRequest> pending_family_action_;
    int pressed_button_=-1,pressed_arrow_=-1;
    std::string rejection_;
    std::optional<uint64_t> last_presented_source_seconds_;
};
} // namespace bmh::ui
