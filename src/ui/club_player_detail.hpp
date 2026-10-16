// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "game/club_player_detail.hpp"
#include "formats/font.hpp"
#include "formats/textd.hpp"
#include "formats/vga.hpp"

namespace bmh::ui {

struct ClubPlayerDetailClub {
    int runtime_team = -1;
    uint16_t badge_selector = 0;
    std::string original_full_name;
    std::string original_short_name;
};

// Owned ClubPlayerDetailFromGame inputs. The redraw is produced by Core once per actual
// card rebuild; render/move/press/release never touch state or RNG.
struct ClubPlayerDetailProjection {
    ClubPlayerDetailRedraw redraw;
    std::array<std::optional<ClubPlayerDetailClub>, 2> clubs;
    std::string original_nationality_name;
    int currency_numerator = 1, currency_denominator = 1;
    bool gradient_text = true;
    bool injury_as_text = false;
    uint8_t statistics_months = 0, statistics_flags = 0;
    std::optional<uint32_t> displayed_offer_amount;
};
ClubPlayerDetailProjection club_player_detail_from_game(const GameState&,
    ClubPlayerDetailRedraw redraw, const LeImage&);

enum class ClubPlayerDetailRequestKind : uint8_t {
    none, close, redraw_card, national_action, offer_action
};
struct ClubPlayerDetailRequest {
    ClubPlayerDetailRequestKind kind = ClubPlayerDetailRequestKind::none;
    ClubPlayerDetailSelection selection;
    uint8_t statistics_flags = 0;
    int source_return = 0; // 0 rebuilds ClubInfo; nonzero closes its parent.
    bool close_parent() const { return source_return != 0; }
};

class ClubPlayerDetailScreen {
public:
    ClubPlayerDetailScreen(const std::filesystem::path&, ClubPlayerDetailProjection,
                          Image parent_frame);
    Image render() const;
    bool move(int x, int y);
    bool press(int x, int y, bool right = false);
    bool release(int x, int y, bool right = false);
    bool key(std::string_view) { return false; } // OriginalMouseSound ignores keys.
    // Source CareerSession_InitializeClubModalRuntime_ExecutableField becomes true after more than25 held-left BIOS ticks.
    // Caller consumes the opening DOWN in bbcde!=0, including its optional
    // wait-for-prior-hold-release boundary; only selection mode retains it.
    bool held_left_ticks(uint32_t ticks);
    // OriginalMouseSound event8 follows the normal statistics/card return, after
    // ExpireQuestion has released its pressed control. No player action is selected.
    bool process_timer_expired();
    bool source_mouse_wait_release_pending() const { return pressed_.has_value(); }
    ClubPlayerDetailRequest take_request();
    // After statistics returns, the caller persists statistics_flags and runs
    // Core redraw once, then supplies the new projection before presenting.
    void replace_redraw(ClubPlayerDetailProjection);
    bool in_statistics() const { return statistics_open_; }
    bool awaiting_redraw() const { return awaiting_redraw_; }
    const ClubPlayerDetailProjection& projection() const { return projection_; }
private:
    ClubPlayerDetailProjection projection_;
    LeImage exe_;
    Font small_, heading_;
    TextDb texts_;
    Image widgets_, atlas_, flags_, portrait_, letters_, parent_;
    std::array<std::optional<Image>, 2> badges_;
    std::optional<Image> statistics_frame_, restored_card_frame_;
    bool statistics_open_ = false, awaiting_redraw_ = false, closed_ = false;
    std::optional<int> pressed_;
    ClubPlayerDetailRequest request_;
    const std::string& label(uint32_t) const;
    void text(Image&, int x, int baseline, std::string_view, uint8_t color,
              int right = -1, bool heading = false) const;
    void frame(Image&, int x, int y, int width, int height) const;
    void button(Image&, int x, int y, int width, std::string_view, bool active = false) const;
    void draw_portrait(Image&) const;
    Image render_card() const;
    Image render_statistics() const;
    std::optional<int> button_at(int x, int y) const;
    bool activate(int);
    bool finish_statistics();
    void request(ClubPlayerDetailRequestKind, int source_return = 0);
};
} // namespace bmh::ui
