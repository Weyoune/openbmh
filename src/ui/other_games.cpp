// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/other_games.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>

#include "ui/manager.hpp"

namespace bmh::ui {
namespace {
void copy(Image& out, const Image& src, int sx, int sy, int x, int y, int w, int h,
          int transparent = -1) {
    if (sx < 0 || sy < 0 || x < 0 || y < 0 || w < 0 || h < 0 ||
        sx + w > src.width || sy + h > src.height || x + w > out.width || y + h > out.height)
        throw FormatError("other games: Original atlas bounds");
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx) {
            const uint8_t color = src.pixels[size_t(sy + yy) * size_t(src.width) + size_t(sx + xx)];
            if (color != transparent) out.at(x + xx, y + yy) = color;
        }
}

void fill(Image& out, int x, int y, int w, int h, uint8_t color) {
    if (w <= 0 || h <= 0) return;
    for (int yy = std::max(0, y); yy < std::min(out.height, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(out.width, x + w); ++xx)
            out.at(xx, yy) = color;
}

std::string source_number(unsigned value, unsigned width = 0) {
    auto text = std::to_string(value);
    if (text.size() < width) text.insert(0, size_t(width - text.size()), '%');
    return text;
}

std::string source_signed_number(int value, unsigned width = 0) {
    auto text = std::to_string(value);
    if (text.size() < width) text.insert(0, size_t(width - text.size()), '%');
    return text;
}

int row_step(const OtherGamesProjection& p) {
    if (!p.knockout) return 16;
    return p.display_mode == OtherGamesDisplayMode::detail ? 16 : 6;
}

int first_fixture_baseline(const OtherGamesProjection& p) {
    if (!p.knockout) {
        const int raw_team_count = p.group_tables.empty() ? int(p.rows.size() * 2)
                                                          : p.group_tables.front().size;
        return raw_team_count == 2 ? 104 : 39;
    }
    if (p.display_mode == OtherGamesDisplayMode::compact)
        return ((64 - p.source_participant_count) / 2) * 3 + 26;
    return p.source_participant_count < 16 ? (16 - p.source_participant_count) * 4 + 48 : 48;
}

int fixture_count(const OtherGamesProjection& p, size_t projected_rows) {
    if (p.unfinished_predecessor || !p.fixtures_available) return 0;
    if (!p.knockout || p.display_mode == OtherGamesDisplayMode::compact) return int(projected_rows);
    return std::min(8, int(projected_rows));
}

int source_slot(const OtherGamesProjection& p, int slot) {
    for (const auto& selector : p.selectors)
        if (selector.slot == slot) return selector.competition;
    return -1;
}
int fixture_baseline(const OtherGamesState& state,size_t row) {
    return state.fixture_baselines.empty()?first_fixture_baseline(state.projection)+int(row)*row_step(state.projection):state.fixture_baselines.at(row);
}

int linked_stage_y(const OtherGamesProjection& p, int ordinal) {
    int stage_y = 178;
    int previous_group = -1;
    int previous_count = 0;
    for (const auto& page : p.linked_pages) {
        if (previous_group >= 0 && previous_group + 1 >= previous_count && page.group_index == 0)
            stage_y += 9;
        if (page.ordinal == ordinal) return stage_y;
        previous_group = page.group_index;
        previous_count = page.group_count;
    }
    return stage_y;
}

int linked_stage_count(const OtherGamesProjection& p) {
    return int(std::count_if(p.linked_pages.begin(), p.linked_pages.end(),
                             [](const auto& page) { return page.group_index == 0; }));
}
} // namespace

OtherGamesScreen::OtherGamesScreen(const std::filesystem::path& data, OtherGamesState state)
    : exe_(load_le(asset_path(data, "BMH.exe"))),
      texts_(load_textd(asset_path(data, "TEXTD.BLK"))),
      frame_(load_vga(asset_path(data / "KB", "129.VGA"))),
      panel_(load_vga(asset_path(data / "KB", "56.VGA"))),
      arrows_(load_vga(asset_path(data / "KB", "1.VGA"))),
      widgets_(load_vga(asset_path(data / "KB", "0.VGA"))),
      flags_(load_vga(asset_path(data / "KB", "24.VGA"))),
      font_(load_font(asset_path(data / "F", "BMH3.FNT"))), sprite_(data), state_(std::move(state)) {
    const auto& id = state_.identity;
    const auto& p = state_.projection;
    const bool typed=state_.world12_identity.has_value();
    if ((typed?(state_.world12_identity->manager_slot>3 || !state_.world12_identity->club.valid() || !state_.world12_identity->scope || p.entry_address!=0):
         (id.manager_slot < 0 || id.manager_slot > 3 || id.runtime_team < 0 || id.club_identity < 0 || !id.scope)) ||
        p.competition < 0 || p.entry_index < 0 || p.round < 0 || p.rounds <= 0 ||
        p.round >= p.rounds || p.selectors.size() != 8 || state_.rows.size() != p.rows.size() ||
        p.participant_offset < 0 || p.participant_offset % 16 != 0 ||
        state_.source_participant_offset < 0 || state_.source_participant_offset % 16 != 0 || p.title.empty() ||
        // Some Original one-round competitions (e.g. entry23/ID8) have an
        // intentionally empty TEXTD round caption; its competition title remains.
        frame_.width < 320 || frame_.height < 240 ||
        widgets_.width < 55 || widgets_.height < 104 || arrows_.width < 214 || arrows_.height < 43 ||
        flags_.width < 96 || flags_.height < 6 ||
        (state_.cached_backdrop && (state_.underlay.width != 320 || state_.underlay.height != 240)) ||
        (!typed && (id.expected_view_byte & 0x80) !=
            (p.display_mode == OtherGamesDisplayMode::detail ? 0x80 : 0)))
        throw FormatError("other games: incomplete or inconsistent Source projection");
    if(!state_.fixture_baselines.empty() && state_.fixture_baselines.size()!=p.rows.size())throw FormatError("other games: incomplete fixture layout");
    if(typed) {
        if(state_.identity!=OtherGamesIdentity{})throw FormatError("other games: mixed legacy/typed identity");
        for(size_t i=0;i<p.rows.size();++i) {
            if(!state_.world12_teams.contains(p.rows[i].home_team)||!state_.world12_teams.contains(p.rows[i].away_team))throw FormatError("other games: missing typed club token");
            state_.rows[i].primary_result=p.rows[i].played?other_games_cup_score_text(p.rows[i].source_result,texts_,exe_):"-";
            if(p.rows[i].second_leg_visible)state_.rows[i].second_leg_result=other_games_cup_score_text(p.rows[i].source_second_leg,texts_,exe_,true);
            state_.rows[i].details[0]=other_games_club_detail_text(p.rows[i].home_metadata,texts_,exe_);
            state_.rows[i].details[1]=other_games_club_detail_text(p.rows[i].away_metadata,texts_,exe_);
        }
    }
    else if(!state_.sections.empty()||!state_.fixture_baselines.empty())throw FormatError("other games: typed layout overrides on a legacy page");
    for(size_t i=0;i<state_.fixture_baselines.size();++i)
        if(state_.fixture_baselines[i]<30 || state_.fixture_baselines[i]>164 || (i && state_.fixture_baselines[i]-state_.fixture_baselines[i-1]<16))
            throw FormatError("other games: overlapping or out-of-panel fixtures");
    for(const auto& section:state_.sections)
        if(section.heading_baseline<30 || section.heading_baseline>=section.fixture_baseline || section.fixture_baseline>164 || section.first_row+section.row_count>p.rows.size())
            throw FormatError("other games: invalid independent competition section");
    for(size_t i=0;i<state_.sections.size();++i) {
        const auto& section=state_.sections[i];
        if(section.row_count>1 || (section.row_count && state_.fixture_baselines.at(section.first_row)!=section.fixture_baseline) ||
            (i && section.heading_baseline-state_.sections[i-1].fixture_baseline<32))throw FormatError("other games: independent finals are not sufficiently separated");
    }
    for (int slot = 0; slot < 8; ++slot)
        if (p.selectors[size_t(slot)].slot != slot)
            throw FormatError("other games: selector slots are incomplete");
    for (const auto& linked : p.linked_pages)
        if (linked.group_count <= 0 || linked.group_index < 0 || linked.group_index >= linked.group_count)
            throw FormatError("other games: linked Source stages without represented groups are unsupported");
    if (p.group_tables.size() && state_.table_rows.empty())
        throw FormatError("other games: group standings are missing from the UI projection");
    (void)render(); // Validate source asset crops before the accepted owner is replaced.
}

OtherGamesRequest OtherGamesScreen::request(OtherGamesAction action, int value) const {
    if(state_.world12_identity) {
        OtherGamesRequest result;result.action=action;result.value=value;result.world12_identity=state_.world12_identity;
        if(action==OtherGamesAction::team_info){result.world12_team=state_.world12_teams.at(value);result.value=-1;}
        return result;
    }
    return OtherGamesRequest{state_.identity, action, value};
}

void OtherGamesScreen::draw_text(Image& out, int x, int baseline, std::string_view bytes,
                                 uint8_t color) const {
    const int width = font_.text_width(bytes);
    const int top = baseline - font_.baseline;
    if (x < 0 || top < 0 || baseline >= out.height || x + width > out.width ||
        top + font_.height > out.height) return;
    Image mask(std::max(1, width), font_.height);
    font_.draw(mask, 0, 0, bytes, 1);
    int index = 0;
    while (index < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2 + uint32_t(index)) != color) ++index;
    for (int yy = 0; yy < font_.height; ++yy)
        for (int xx = 0; xx < width; ++xx)
            if (mask.at(xx, yy))
                out.at(x + xx, baseline - font_.baseline + yy) =
                    state_.gradient_text && index < 32
                        ? exe_.u8(::bmh::layout::Advertising_Text_RuntimeField + uint32_t(index * 6 + yy)) : color;
}

void OtherGamesScreen::draw_source_panel(Image& out, int left, int top, int right, int bottom) const {
    if (right < left || bottom < top) return;
    if (state_.original_panel) {
        const int width = std::min({right - left - 1, out.width - left - 1, panel_.width - left});
        const int height = std::min({bottom - top - 1, out.height - top - 1, panel_.height - (top - 19)});
        if (width > 0 && height > 0)
            copy(out, panel_, left, top - 19, left + 1, top + 1, width, height, 0xc0);
    } else {
        fill(out, left, top, right - left + 1, bottom - top + 1, 0x42);
    }
    const int x0 = std::max(0, left), x1 = std::min(out.width - 1, right);
    const int y0 = std::max(0, top), y1 = std::min(out.height - 1, bottom);
    if (top >= 0 && top < out.height)
        for (int x = x0; x <= x1; ++x) out.at(x, top) = 0x46;
    if (left >= 0 && left < out.width)
        for (int y = y0; y <= y1; ++y) out.at(left, y) = 0x46;
    if (bottom >= 0 && bottom < out.height)
        for (int x = x0; x <= x1; ++x) out.at(x, bottom) = 0x40;
    if (right >= 0 && right < out.width)
        for (int y = y0; y <= y1; ++y) out.at(right, y) = 0x40;
}

void OtherGamesScreen::draw_arrow_frame(Image& out, int x, int y, int style) const {
    const int shadow_height = exe_.u8(::bmh::layout::LeaguePairings_Selector_RuntimeField + 1);
    copy(out, arrows_, 198, 0, x, y, 0x37, 0x26, 0xc0);
    copy(out, arrows_, 198, 0x4c - shadow_height, x, y + 0x25,
         0x37, shadow_height - 0x26, 0xc0);
    if (style < 3)
        copy(out, arrows_, exe_.u8(::bmh::layout::LeaguePairings_Selector_RuntimeField3 + uint32_t(style)), 0, x + 1,
             exe_.u8(::bmh::layout::LeaguePairings_Selector_RuntimeField2 + 1) + y, exe_.u8(::bmh::layout::LeaguePairings_Selector_RuntimeField4 + uint32_t(style)), 0x26, 0xc0);
}

void OtherGamesScreen::draw_sprite_text(Image& out, int x, int baseline,
                                        std::string_view bytes) const {
    sprite_.draw(out, x, baseline, original_to_utf8(std::string(bytes)));
}

void OtherGamesScreen::draw_button(Image& out, int x, int y, int width,
                                   std::string_view raw_label, bool pressed, bool selected) const {
    const bool down = pressed || selected;
    // FormatError3 shifts both a selected competition and a held button by (+2,+2).
    // Keep this Original inset; selection is not an unshifted alternate skin.
    const int dx = x + (down ? 2 : 0), dy = y + (down ? 2 : 0);
    if (down) {
        const uint8_t background = out.at(x, y + 10);
        fill(out, x, y, width + 1, 2, background);
        fill(out, x, y, 2, 11, background);
    } else {
        fill(out, x + 2, y + 10, width, 2, 0x40);
        fill(out, x + width, y + 2, 2, 10, 0x40);
    }
    const int sy = down ? 44 : 74;
    copy(out, widgets_, 24, sy, dx, dy, 31, 10, 0xc0);
    int used = 31;
    while (width - used > 20) {
        const int count = std::min(8, width - used - 20);
        copy(out, widgets_, 24, sy + 10, dx + used, dy, count, 10, 0xc0);
        used += count;
    }
    const int tail = width - used;
    copy(out, widgets_, 45 - tail, sy + 20, dx + used, dy, tail, 10, 0xc0);
    const auto label = original_to_utf8(std::string(raw_label));
    sprite_.draw(out, dx + width / 2 - sprite_.text_width(label) / 2, dy + 7, label,
                 exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + (down ? 1u : 0u)));
}

Image OtherGamesScreen::render() const {
    const auto& p = state_.projection;
    Image out = state_.cached_backdrop ? state_.underlay :
                state_.decorated_background ? frame_ : Image(320, 240);
    if (!state_.cached_backdrop && !state_.decorated_background) {
        fill(out, 0, 4, 320, 13, 0x44); fill(out, 1, 5, 318, 11, 0x39);
        fill(out, 0, 228, 320, 10, 0x44); fill(out, 1, 229, 318, 8, 0x39);
    }
    const bool blocked = !p.fixtures_available || p.unfinished_predecessor;
    const int count = fixture_count(p, p.rows.size());
    const int first = first_fixture_baseline(p);
    if (blocked) {
        draw_source_panel(out, 0, 112, 319, 122);
    } else if (!state_.sections.empty()) {
        for(const auto& section:state_.sections)draw_source_panel(out,0,section.fixture_baseline-8,319,section.fixture_baseline+9);
    } else if (!p.knockout) {
        const int raw_team_count = p.group_tables.empty() ? int(p.rows.size() * 2)
                                                          : p.group_tables.front().size;
        if (state_.original_panel && raw_team_count > 0) {
            const int fixture_top = first - 8;
            const int fixture_bottom = first + ((raw_team_count / 2) << 4) - 6;
            draw_source_panel(out, 0, fixture_top, 319, fixture_bottom);
        }
        if (state_.original_panel && raw_team_count > 2)
            draw_source_panel(out, 0, 125, 319, raw_team_count * 7 + 126);
    } else if (state_.original_panel && p.source_participant_count > 0) {
        const int mode = p.display_mode == OtherGamesDisplayMode::detail ? 1 : 0;
        int displayed = p.source_participant_count - (mode ? p.participant_offset : 0);
        if (displayed > 16 && mode) displayed = 16;
        const int top = (first - 7) - mode;
        const int bottom = ((mode * 10 + 6) * (displayed >> 1) + first - 5) - mode;
        draw_source_panel(out, 0, top, 319, bottom);
    }

    const auto title = original_to_utf8(p.title);
    sprite_.draw(out, SpriteFont::centered_title_x(sprite_.text_width(title)), 13, title);
    if (p.display_mode == OtherGamesDisplayMode::detail && p.descriptor_competition != 12 &&
        !state_.date_heading.empty())
        draw_text(out, 319 - font_.text_width(state_.date_heading), 22, state_.date_heading, 0x5f);
    if (!state_.matchday_heading.empty()) draw_text(out, 2, 22, state_.matchday_heading, 0x5f);
    for(const auto& section:state_.sections) {
        draw_text(out,std::max(0,(320-font_.text_width(section.title))/2),section.heading_baseline,section.title,0x6a);
        if(!section.message.empty())draw_text(out,std::max(0,(320-font_.text_width(section.message))/2),section.fixture_baseline,section.message,0x6a);
    }

    if (blocked) {
        const std::string_view warning = state_.unavailable_message.empty()?std::string_view(texts_.strings.at(1186)):std::string_view(state_.unavailable_message);
        draw_text(out, (320 - font_.text_width(warning)) / 2, 120, warning, 0x6a);
    } else {
        for (int visible = 0; visible < count; ++visible) {
            const size_t index = size_t(visible);
            if (index >= p.rows.size() || index >= state_.rows.size()) break;
            const auto& source = p.rows[index];
            const auto& display = state_.rows[index];
            const int baseline = fixture_baseline(state_,index);
            const bool compact_knockout = p.knockout && p.display_mode == OtherGamesDisplayMode::compact;
            const bool group = !p.knockout;
            const int home_x = compact_knockout ? 2 : group ? 5 : 5;
            const int away_x = compact_knockout ? 110 : group ? 132 : 132;
            const int separator_x = compact_knockout ? 100 : group ? 125 : 125;
            const uint8_t name_color_home = display.name_colors[0] ? display.name_colors[0] : 0x5f;
            const uint8_t name_color_away = display.name_colors[1] ? display.name_colors[1] : 0x5f;
            {
                const auto separator = exe_.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer);
                if (!separator.empty()) draw_text(out, separator_x, baseline, separator, 0x68);
                draw_text(out, home_x, baseline, source.home_name, name_color_home);
                draw_text(out, away_x, baseline, source.away_name, name_color_away);
                const int primary_x = compact_knockout
                    ? (display.second_leg_result.empty() ? 232 : 217)
                    : group ? 255 : 260;
                draw_text(out, primary_x, baseline, display.primary_result, 0x5f);
                if (!display.second_leg_result.empty())
                    draw_text(out, compact_knockout ? 247 : 260,
                              baseline + (compact_knockout ? 0 : 7), display.second_leg_result, 0x69);
                if (!compact_knockout) {
                    draw_text(out, compact_knockout ? home_x : group ? 3 : 5,
                              baseline + 7, display.details[0], 0x68);
                    draw_text(out, group ? 133 : 132, baseline + 7, display.details[1], 0x68);
                }

                const int badge_home_x = compact_knockout ? 90 : 115;
                const int badge_away_x = compact_knockout ? 200 : 242;
                const std::array<int, 2> badge_x{badge_home_x, badge_away_x};
                const std::array<int, 2> tiers{display.league_tiers[0], display.league_tiers[1]};
                const std::array<int, 2> countries{display.countries[0], display.countries[1]};
                for (int side = 0; side < 2; ++side) {
                    const int code = tiers[size_t(side)];
                    if (code >= 0 && code < 5) {
        const int atlas_x = exe_.u8(::bmh::layout::CupDraw_GroupMarker_RuntimeField + uint32_t(code));
                        // Copy -> PathTreatsActive -> UsesForTheseSprites uses DesktopRuntimeField (0)
                        // as the transparent color. Its final 0xff is a VGA
                        // write mask, not the sprite's transparent palette index.
                        copy(out, arrows_, atlas_x, 0x26, badge_x[size_t(side)], baseline - 5, 16, 5, 0);
                    } else if (!compact_knockout && countries[size_t(side)] != 0xff) {
                        const int country = countries[size_t(side)];
                        copy(out, flags_, 9 * (country % 12), 6 * (country / 12),
                             badge_x[size_t(side)], baseline - 5, 9, 6, 0xff);
                    }
                }
            }
        }
    }

    if (!state_.table_rows.empty()) {
        for (const auto& row : state_.table_rows) {
            const int baseline = 132 + std::max(0, row.rank - 1) * 7;
            const std::string rank = source_number(unsigned(std::max(0, row.rank)), 2);
            const uint8_t rank_color = row.rank_color ? row.rank_color : 0x7a;
            if (row.rank < 10)
                draw_text(out, 1, baseline, rank + exe_.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer2), rank_color);
            else {
                draw_text(out, 2, baseline, rank.substr(0, 1), rank_color);
                draw_text(out, 6, baseline, rank.substr(1, 1) + exe_.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer2), rank_color);
            }
            if (state_.previous_rank_visible) {
                const uint8_t movement = row.rank < row.previous_rank ? 0x75 :
                                         row.previous_rank < row.rank ? 0xf7 : 0x6a;
                draw_text(out, 12, baseline, "(" + source_number(unsigned(row.previous_rank), 2) +
                          exe_.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer3),
                          movement);
            }
            draw_text(out, 30, baseline, row.name, row.name_color ? row.name_color : 0x6a);
            const std::string record = source_number(unsigned(row.games), 2) + exe_.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer4) +
                source_number(unsigned(row.wins), 2) + exe_.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer5) +
                source_number(unsigned(row.draws), 2) + exe_.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer5) +
                source_number(unsigned(row.defeats), 2) + exe_.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer4);
            draw_text(out, 114, baseline, record, 0x6a);
            for (int form = 0; form < 8; ++form) {
                const unsigned bits = (row.form >> (3 * form)) & 7u;
                const unsigned glyph = (bits & 4u) ? (bits & 3u) : (bits & 3u) + 4u;
                const auto& source_glyphs = texts_.strings.at(72);
                const std::string glyph_text = glyph < source_glyphs.size()
                    ? source_glyphs.substr(glyph, 1) : std::string("-");
                draw_text(out, 207 - form * 6, baseline, glyph_text, 0x6a);
            }
            std::string points = exe_.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer6);
            if (p.flags & 0x80) points += exe_.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer7);
            points += source_signed_number(row.points, 2) + exe_.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer8);
            if (!(p.flags >> 7)) {
                points += exe_.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer9) + source_signed_number(2 * row.games - row.points, 2) +
                          exe_.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer8);
            }
            draw_text(out, 206, baseline, points, 0x6a);
            const int difference = row.goals_for - row.goals_against;
            const uint32_t sign_at = difference < 0 ? ::bmh::layout::Bank_OriginalToUtf8_ExecutableField5 : difference > 0 ? ::bmh::layout::Bank_OriginalToUtf8_ExecutableField6 : ::bmh::layout::Bank_OriginalToUtf8_ExecutableField7;
            const std::string goal_record = source_number(unsigned(row.goals_for), 3) +
                exe_.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer9) + source_number(unsigned(row.goals_against), 3) +
                exe_.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer10) + exe_.cstring(sign_at) +
                source_number(unsigned(std::abs(difference)), 3) + exe_.cstring(::bmh::layout::CupDraw_DrawGroupOverview_TextPointer11);
            draw_text(out, 255, baseline, goal_record, 0x6a);
        }
    }
    draw_sprite_text(out, 5, 235, p.round_heading);

    if (p.display_mode == OtherGamesDisplayMode::detail) {
        for (int slot = 0; slot < 8; ++slot) {
            const int x = 1 + (slot / 4) * 90, y = 176 + (slot % 4) * 13;
            const auto& selector = p.selectors[size_t(slot)];
            draw_button(out, x, y, 86, state_.selector_labels[size_t(slot)], pressed_button_ == slot,
                        selector.competition == p.competition);
        }
    }
    if (p.display_mode_toggle_available && !blocked) {
        const auto label = p.display_mode == OtherGamesDisplayMode::detail ? texts_.strings.at(162)
                                                                            : texts_.strings.at(161);
        draw_button(out, 202, 215, 114, label, pressed_button_ == 8);
    }
    if (p.display_mode == OtherGamesDisplayMode::detail) {
        if (p.round_arrows_visible) {
            draw_arrow_frame(out, 262, 176, 2);
            if (pressed_arrow_ >= 0) copy(out, arrows_, 136 + pressed_arrow_ * 26, 0,
                                          263 + pressed_arrow_ * 26, 177, 26, 38);
        }
        if (linked_stage_count(p) > 1)
            draw_arrow_frame(out, 205, 176, 3);
        for (const auto& page : p.linked_pages) {
            const int stage_y = linked_stage_y(p, page.ordinal);
            const int x = 233 - page.group_count * 4 + page.group_index * 8;
            copy(out, arrows_, 188, page.entry_index == p.entry_index ? 8 : 0, x, stage_y, 8, 8, 0xc0);
        }
    }
    if (rejection_.size()) {
        const std::string text = rejection_;
        draw_text(out, std::max(0, (320 - font_.text_width(text)) / 2), 224, text, 0x6a);
    }
    return out;
}

int OtherGamesScreen::button_at(int x, int y) const {
    if (state_.projection.display_mode_toggle_available && !state_.projection.unfinished_predecessor &&
        state_.projection.fixtures_available && x >= 202 && x <= 316 && y >= 215 && y <= 225) return 8;
    if (state_.projection.display_mode != OtherGamesDisplayMode::detail) return -1;
    for (int slot = 0; slot < 8; ++slot) {
        const int bx = 1 + (slot / 4) * 90, by = 176 + (slot % 4) * 13;
        if (x >= bx && x <= bx + 86 && y >= by && y <= by + 10) return slot;
    }
    return -1;
}

int OtherGamesScreen::arrow_at(int x, int y) const {
    if (!state_.projection.round_arrows_visible || x <= 261 || x >= 317 || y <= 175 || y >= 2213)
        return -1;
    if (x >= 263 && x <= 289 && y >= 177 && y <= 215) return 0;
    if (x >= 289 && x <= 315 && y >= 177 && y <= 215) return 1;
    return -1;
}

bool OtherGamesScreen::in_arrow(int arrow, int x, int y) const {
    return arrow >= 0 && arrow < 2 && x >= 263 + arrow * 26 && x <= 263 + arrow * 26 + 26 &&
           y >= 177 && y <= 177 + 38;
}

bool OtherGamesScreen::fixture_hit(int x, int y, int& team) const {
    const auto& p = state_.projection;
    if (p.unfinished_predecessor || !p.fixtures_available) return false;
    const int count = fixture_count(p, p.rows.size());
    for (int visible = 0; visible < count; ++visible) {
        const int top = fixture_baseline(state_,size_t(visible)) - 6;
        const int bottom = top + (p.display_mode == OtherGamesDisplayMode::detail ? 12 : 6);
        if (!(y > top && y < bottom)) continue;
        const auto& row = p.rows[size_t(visible)];
        if (p.knockout && p.display_mode == OtherGamesDisplayMode::compact) {
            if (x > 0 && x < 102) { team = row.home_team; return true; }
            if (x > 103 && x < 319) { team = row.away_team; return true; }
        } else if (!p.knockout) {
            if (x > 0 && x < 127) { team = row.home_team; return true; }
            if (x > 125 && x < 319) { team = row.away_team; return true; }
        } else {
            if (x > 0 && x < 127) { team = row.home_team; return true; }
            if (x > 128 && x < 319) { team = row.away_team; return true; }
        }
    }
    return false;
}

bool OtherGamesScreen::pointer_down(int x, int y, bool right) {
    if (pending_) return false;
    if (right) {
        pressed_button_ = pressed_arrow_ = -1;
        pending_ = request(OtherGamesAction::close);
        rejection_.clear();
        return true;
    }
    int team = -1;
    if (fixture_hit(x, y, team)) {
        pending_ = request(OtherGamesAction::team_info, team);
        rejection_.clear();
        return true;
    }
    const auto& p = state_.projection;
    if (p.display_mode == OtherGamesDisplayMode::detail) {
        for (const auto& page : p.linked_pages) {
            const int y_cell = linked_stage_y(p, page.ordinal);
            const int left = 233 - page.group_count * 4 + page.group_index * 8;
            if (x >= left && x <= left + 8 && y >= y_cell && y <= y_cell + 8) {
                pending_ = request(OtherGamesAction::select_linked_page, page.ordinal);
                rejection_.clear();
                return true;
            }
        }
    }
    pressed_button_ = button_at(x, y);
    if (pressed_button_ >= 0) return true;
    pressed_arrow_ = arrow_at(x, y);
    return pressed_arrow_ >= 0;
}

bool OtherGamesScreen::move(int x, int y) {
    bool changed = false;
    if (pressed_button_ >= 0 && button_at(x, y) != pressed_button_) {
        pressed_button_ = -1;
        changed = true;
    }
    if (pressed_arrow_ >= 0 && !in_arrow(pressed_arrow_, x, y)) {
        pressed_arrow_ = -1;
        changed = true;
    }
    return changed;
}

bool OtherGamesScreen::release(int x, int y) {
    if (pending_) return false;
    const int button = pressed_button_, arrow = pressed_arrow_;
    pressed_button_ = pressed_arrow_ = -1;
    if (arrow >= 0) {
        if (!in_arrow(arrow, x, y)) return true;
        pending_ = request(arrow == 0 ? OtherGamesAction::previous_round : OtherGamesAction::next_round);
        rejection_.clear();
        return true;
    }
    if (button < 0 || button_at(x, y) != button) return button >= 0;
    if (button < 8) {
        const int competition = source_slot(state_.projection, button);
        if (competition < 0 || competition == 20) return true;
        pending_ = request(OtherGamesAction::select_competition, button);
    } else if (button == 8 && state_.projection.display_mode_toggle_available) {
        pending_ = request(OtherGamesAction::toggle_display_mode);
    }
    rejection_.clear();
    return true;
}

void OtherGamesScreen::reject_action(std::string reason) {
    if (!pending_ || reason.empty()) throw FormatError("other games: rejection requires pending action and reason");
    pending_.reset();
    pressed_button_ = pressed_arrow_ = -1;
    rejection_ = std::move(reason);
}

std::string OtherGamesScreen::state_json() const {
    std::ostringstream out;
    const auto& id = state_.identity;
    const auto& p = state_.projection;
    out << "{\"screen\":\"other_games\",\"manager\":" << id.manager_slot
        << ",\"runtime_team\":" << id.runtime_team << ",\"competition\":" << p.competition
        << ",\"linked_page\":" << p.linked_page << ",\"round\":" << p.round
        << ",\"mode\":" << int(p.display_mode) << ",\"participant_offset\":" << state_.source_participant_offset
        << ",\"view_byte\":" << unsigned(id.expected_view_byte) << ",\"revision\":" << id.revision
        << ",\"scope\":" << id.scope << ",\"sequence\":" << id.sequence
        << ",\"pressed_button\":" << pressed_button_ << ",\"pressed_arrow\":" << pressed_arrow_
        << ",\"pending\":" << (pending_ ? "true" : "false")
        << ",\"last_rejection\":" << json_quote(rejection_) << ",\"rows\":[";
    for (size_t i = 0; i < p.rows.size(); ++i) {
        if (i) out << ',';
        out << "{\"home\":" << p.rows[i].home_team << ",\"away\":" << p.rows[i].away_team
            << ",\"result\":" << p.rows[i].source_result << ",\"second_leg\":"
            << p.rows[i].source_second_leg << '}';
    }
    out << ']';
    if(state_.world12_identity) {
        const auto& owner=*state_.world12_identity;
        out<<",\"world12\":true,\"world_manager\":"<<int(owner.manager_slot)<<",\"world_club\":"<<owner.club.value<<",\"world_revision\":"<<owner.revision<<",\"world_scope\":"<<owner.scope<<",\"world_sequence\":"<<owner.sequence<<",\"world_teams\":[";
        bool first=true;for(const auto& [token,club]:state_.world12_teams){if(!first)out<<',';first=false;out<<"{\"token\":"<<token<<",\"club\":"<<club.value<<'}';}out<<']';
    }
    out << '}';
    return out.str();
}

} // namespace bmh::ui
