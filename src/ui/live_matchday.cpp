// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/live_matchday.hpp"

#include <algorithm>
#include <array>

#include "game/matchday.hpp"
#include "game/messages.hpp"
#include "game/state.hpp"
#include "ui/manager.hpp"
#include "ui/live_set_piece.hpp"
#include "ui/setup.hpp"

namespace bmh::ui {
namespace {

constexpr std::array<std::pair<uint8_t, uint8_t>, 3> kEventTags{{{0, 1}, {12, 3}, {4, 5}}};

void copy_region(Image& target, const Image& source, int sx, int sy, int dx, int dy,
                 int width, int height, int transparent = -1) {
    if (sx < 0 || sy < 0 || width < 0 || height < 0 || sx + width > source.width || sy + height > source.height)
        throw FormatError("live matchday: original asset crop out of bounds");
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const uint8_t pixel = source.pixels[size_t(sy + y) * size_t(source.width) + size_t(sx + x)];
        const int tx = dx + x, ty = dy + y;
        if (pixel != transparent && tx >= 0 && ty >= 0 && tx < target.width && ty < target.height)
            target.at(tx, ty) = pixel;
    }
}

void pattern_rect(Image& target, const LeImage& exe, int x, int y, int width, int height) {
    // PatternRect mode 1 passes a 0xff pattern exponent to U82:
    // 16 pixels horizontally, a two-row vertical tile, all bytes opaque.
    for (int yy = 0; yy < height; ++yy) for (int xx = 0; xx < width; ++xx) {
        const uint8_t pixel = exe.u8(::bmh::layout::LiveMatchday_PatternRect_RuntimeFieldUnsigned + uint32_t((yy & 1) * 16 + (xx & 15)));
        if (x + xx >= 0 && y + yy >= 0 && x + xx < target.width && y + yy < target.height)
            target.at(x + xx, y + yy) = pixel;
    }
}

std::string original(std::string_view utf8) {
    try { return utf8_to_original(utf8); }
    catch (const FormatError&) {
        std::string safe;
        for (unsigned char c : utf8) safe += c < 128 ? char(c) : '?';
        return safe;
    }
}

int placed_players(const GameState& state, int team) {
    // SquadRange3 returns 11 for every non-league field team, except the special no-team squad.
    const SquadRange range = squad_range(team);
    if (range.count == 0 && team != kNoTeam) return 11;
    uint8_t count = 0;
    const size_t end = std::min(state.squad_slots.size(), range.first + range.count);
    for (size_t slot = range.first; slot < end; ++slot) {
        const uint16_t value = state.squad_slots[slot];
        if (value != kEmptySlot && (value & kSlotInLineup)) count = uint8_t(count + 1);
    }
    return count;
}

int log_count(const MatchGroup& group, int team, uint8_t first, uint8_t second) {
    // WalksEventLogAsPairsIncrementsUnsigned walks the event log as pairs and increments an unsigned byte.
    uint8_t count = 0;
    for (size_t i = 0; i + 1 < group.log.size(); i += 2) {
        const uint16_t event = group.log[i];
        if ((group.log[i + 1] >> 7) == team &&
            ((event >> 12) == first || (event >> 12) == second)) count = uint8_t(count + 1);
    }
    return count;
}

LiveMatchRow make_row(const MatchGroup& group, int match, int home, int away, const GameState& state) {
    LiveMatchRow row;
    row.home_team = home;
    row.away_team = away;
    row.home = original_to_utf8(state.club_name(home));
    row.away = original_to_utf8(state.club_name(away));
    row.home_stats = {placed_players(state, home), log_count(group, home, kEventTags[0].first, kEventTags[0].second),
                      log_count(group, home, kEventTags[1].first, kEventTags[1].second),
                      log_count(group, home, kEventTags[2].first, kEventTags[2].second)};
    row.away_stats = {placed_players(state, away), log_count(group, away, kEventTags[0].first, kEventTags[0].second),
                      log_count(group, away, kEventTags[1].first, kEventTags[1].second),
                      log_count(group, away, kEventTags[2].first, kEventTags[2].second)};
    row.spectators_known = size_t(match) < group.spectators.size();
    if (row.spectators_known) {
        row.spectators = group.spectators[size_t(match)];
        row.spectators_text = original_to_utf8(format_number(state, int32_t(row.spectators), 0x81));
        const int manager = state.manager_of(home);
        const size_t capacity_at = manager >= 0 ? size_t(manager) * 0xb0c + 0x5c : state.manager_records.size();
        if (capacity_at < state.manager_records.size())
            row.attendance_at_capacity = state.manager_records[capacity_at] == row.spectators / 1000;
    }
    if (group.desc.size() > size_t(match) + 0x50 && group.desc.size() > size_t(match) + 0x30) {
        row.rain = group.desc[size_t(match) + 0x50];
        row.temperature = int8_t(group.desc[size_t(match) + 0x30]);
        row.temperature_text = original_to_utf8(format_number(state, int(row.temperature), 0x81) + message_text(state, ::bmh::layout::LeaguePairings_FormatError_TextPointer14));
    }
    bool recorded = false;
    if (size_t(match) < group.results.size()) recorded = (group.results[size_t(match)] & 0xcc) != 0xcc;
    else if (group.cup_results.size() == size_t(group.matches()))
        recorded = group.cup_results[size_t(match)] != 0xcccc;
    row.score_known = recorded && match_score(group, match, row.home_goals, row.away_goals);
    return row;
}

} // namespace

LiveMatchdayFrame live_matchday_frame(int minute, const std::vector<MatchGroup>& groups,
                                      const GameState& state) {
    LiveMatchdayFrame frame;
    frame.minute = std::max(minute, 0);
    frame.gradient_text = !state.memory.has(::bmh::layout::TextGradientDisabled) || state.memory.u8(::bmh::layout::TextGradientDisabled) == 0;
    frame.minute_text = original_to_utf8(format_number(state, frame.minute, 0x81) + message_text(state, ::bmh::layout::LiveMatchday_LiveMatchdayFrame_TextPointer));
    std::vector<LiveMatchRow> candidates;
    for (const auto& group : groups) {
        for (int i = 0; i < group.matches(); ++i) {
            const size_t f = size_t(i) * 2;
            if (f + 1 >= group.fixtures.size()) continue;
            const int home = group.fixtures[f], away = group.fixtures[f + 1];
            if (home < 0 || away < 0 || home == 0xFFF || away == 0xFFF) continue;
            if (home >= int(state.club_ids.size()) || away >= int(state.club_ids.size())) continue;
            candidates.push_back(make_row(group, i, home, away, state));
        }
    }

    // Source board slots are selected by AutoLineup in home-team order and capped at four.
    // Keep that order when the owned live-match selection is available.
    for (int selected_home : state.live_matches) {
        const auto match = std::find_if(candidates.begin(), candidates.end(), [selected_home](const auto& row) {
            return row.home_team == selected_home;
        });
        if (match != candidates.end() && frame.matches.size() < 4) frame.matches.push_back(*match);
    }
    return frame;
}

LiveChanceProjection live_chance_projection(const LiveChanceEvent& event,
        const std::vector<MatchGroup>& groups, const GameState& state, const LeImage& exe) {
    if (event.group < 0 || size_t(event.group) >= groups.size() || event.match < 0 ||
        event.side < 0 || event.side > 1 || event.kind < 0 || event.kind > 3 || event.distance < 0)
        throw FormatError("live chance: invalid Source event");
    const auto& group = groups[size_t(event.group)];
    const size_t fixture = size_t(event.match) * 2;
    if (fixture + 1 >= group.fixtures.size() || group.desc.size() < 2)
        throw FormatError("live chance: missing Source fixture");
    const int home = group.fixtures[fixture], away = group.fixtures[fixture + 1];
    if (home < 0 || away < 0 || size_t(home) >= state.club_ids.size() || size_t(away) >= state.club_ids.size())
        throw FormatError("live chance: invalid Source club");
    const auto longest_word = [&](int team) {
        // ClubName3: the last longest token wins; the ER/HER suffix is shortened.
        const std::string name = state.club_name(team);
        std::string best;
        size_t start = 0;
        for (size_t i = 0; i <= name.size(); ++i) {
            if (i < name.size() && std::string_view(" %.1-").find(name[i]) == std::string_view::npos) continue;
            if (i - start >= best.size()) best = name.substr(start, i - start);
            start = i + 1;
        }
        if (best.size() >= 3 && best.ends_with("ER"))
            best.resize(best.size() - (best[best.size() - 3] == 'H' ? 1 : 2));
        return best;
    };
    LiveChanceProjection out;
    out.stage = event.stage; out.minute = event.minute;
    out.group = event.group; out.match = event.match; out.side = event.side; out.ticker = event.ticker;
    out.home = original_to_utf8(state.club_name(home));
    out.away = original_to_utf8(state.club_name(away));
    std::string stadium;
    if (state.memory.has(::bmh::layout::LiveMatchday_FormatError_RuntimeField15) && state.memory.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField15) != 0) {
        stadium = message_text(state, ::bmh::layout::LiveMatchday_FormatError_TextPointer7);
    } else if ((group.flags() & 2) || state.indoor) {
        const size_t at = size_t(home) * 2;
        if (!state.indoor && home < exe.u16(::bmh::layout::CareerSession_DataDirectory_RuntimeField2) && at + 1 < state.club_heap_bc.size()) {
            const size_t offset = le16(state.club_heap_bc, at);
            if (offset < state.stadium_names.size())
                for (size_t i = offset; i < state.stadium_names.size() && state.stadium_names[i]; ++i)
                    stadium += state.stadium_names[i] == ':' ? message_text(state, ::bmh::layout::LiveMatchday_FormatError_TextPointer8) :
                        std::string(1, char(state.stadium_names[i]));
        }
        if (stadium.empty()) {
            int host = home;
            if (state.indoor) {
                const uint32_t tournament = state.memory.has(::bmh::layout::Cup_AdvanceCup_RuntimeField4) ? state.memory.u8(::bmh::layout::Cup_AdvanceCup_RuntimeField4) : 0;
                const uint32_t address = ::bmh::layout::LiveMatchday_FormatError_RuntimeField14 + tournament;
                if (!state.memory.has(address)) throw FormatError("live chance: indoor host missing");
                host = state.memory.u8(address);
                if (size_t(host) >= state.club_ids.size()) throw FormatError("live chance: indoor host invalid");
            }
            stadium = message_text(state, state.indoor ? ::bmh::layout::LiveMatchday_FormatError_TextPointer10 : ::bmh::layout::LiveMatchday_FormatError_TextPointer9) + longest_word(host);
        }
    }
    out.stadium = original_to_utf8(stadium);
    const auto name = [&](int player) {
        if (player == 0xfff) return std::string();
        if (player < 0 || size_t(player) >= state.players.size())
            throw FormatError("live chance: invalid Source player");
        return state.player_name(player);
    };
    std::string description;
    if (event.kind != 0) description = message_text(state, ::bmh::layout::LiveMatchday_FormatError_TextPointer3 + uint32_t(event.kind) * 4);
    if (state.indoor && event.kind == 2) description = message_text(state, ::bmh::layout::LiveMatchday_FormatError_TextPointer11);
    if (event.kind == 1) description += format_number(state, event.distance, 1) + message_text(state, ::bmh::layout::LiveMatchday_FormatError_TextPointer12);
    out.description = original_to_utf8(description);
    std::string shooter = name(event.shooter);
    if (event.kind == 0) {
        if (!shooter.empty()) shooter += exe.cstring(::bmh::layout::LiveMatchday_FormatError_ExecutableField3);
        shooter += format_number(state, event.distance, 1) + message_text(state, ::bmh::layout::LiveMatchday_FormatError_TextPointer12);
        if (event.deflected) shooter += message_text(state, ::bmh::layout::LiveMatchday_FormatError_TextPointer13);
    }
    out.shooter_detail = original_to_utf8(shooter);
    out.outcome = original_to_utf8(message_text(state, event.goal ? ::bmh::layout::LiveMatchday_FormatError_TextPointer14 : ::bmh::layout::LiveMatchday_FormatError_TextPointer15));
    const bool league_stats = event.goal && (group.desc[0] == 0 || group.desc[0] == 10);
    const auto count = [&](unsigned value) {
        return exe.cstring(::bmh::layout::CupDraw_FormatError_TextPointer) + format_number(state, int(value), 1) + exe.cstring(::bmh::layout::CupDraw_FormatError_ExecutableField);
    };
    std::string participants = name(event.shooter);
    if (!participants.empty() && league_stats)
        participants += count((state.players[size_t(event.shooter)].w364 >> 2) & 0x3f);
    if ((event.kind == 0 || event.kind == 3) && event.assistant != 0xfff) {
        participants += exe.cstring(::bmh::layout::LiveMatchday_FormatError_ExecutableField) + name(event.assistant);
        if (league_stats) participants += count((state.players[size_t(event.assistant)].w368 >> 18) & 0x3f);
        participants += exe.cstring(::bmh::layout::LiveMatchday_FormatError_ExecutableField2);
    }
    out.participants = original_to_utf8(participants);
    int home_goals = 0, away_goals = 0;
    if (event.ticker && match_score(group, event.match, home_goals, away_goals))
        out.ticker_score = original_to_utf8(format_number(state, home_goals, 0x81) + exe.cstring(::bmh::layout::LiveMatchday_FormatError_ExecutableField4) +
            format_number(state, away_goals, 1) + exe.cstring(::bmh::layout::LiveMatchday_FormatError_ExecutableField5));
    return out;
}

LiveBookingProjection live_booking_projection(const LiveBookingEvent& event,
    const std::vector<MatchGroup>& groups, const GameState& state, const LeImage& exe) {
    if (event.kind < 0 || event.kind > 3 || event.player < 0 ||
        size_t(event.player) >= state.players.size() || event.season_yellows < 0 || event.season_yellows > 15)
        throw FormatError("live booking: invalid Source event");
    LiveChanceEvent identity;
    identity.minute=event.minute;identity.group=event.group;identity.match=event.match;identity.side=event.side;
    LiveBookingProjection out;
    out.header=live_chance_projection(identity,groups,state,exe);out.kind=event.kind;
    const auto& group=groups.at(size_t(event.group));
    if (group.fixtures.at(size_t(event.match*2+event.side)) != event.team)
        throw FormatError("live booking: player event belongs to a different Source side");
    out.score_known=match_score(group,event.match,out.home_goals,out.away_goals);
    std::string message=message_text(state,::bmh::layout::LiveMatchday_FormatError_TextPointer2Unsigned+uint32_t(event.kind*4))+state.player_name(event.player);
    // Cstring: only a first yellow adds the Source season count in parentheses.
    if (event.kind==0)
        message+=exe.cstring(::bmh::layout::CupDraw_FormatError_TextPointer)+format_number(state,event.season_yellows,1)+exe.cstring(::bmh::layout::CupDraw_FormatError_ExecutableField);
    out.message=original_to_utf8(message);
    return out;
}

LiveShootoutProjection live_shootout_projection(const LiveShootoutEvent& event,
    const std::vector<MatchGroup>& groups, const GameState& state, const LeImage& exe) {
    if (event.group<0 || size_t(event.group)>=groups.size() || event.match<0 ||
        event.match>=groups[size_t(event.group)].matches() || event.side<0 || event.side>3 ||
        event.home_goals<0 || event.away_goals<0 || event.home_goals>31 || event.away_goals>31 ||
        (event.stage!=LiveShootoutStage::start && event.side>1))
        throw FormatError("live shoot-out: invalid Source fixture/header");
    LiveChanceEvent identity;
    identity.minute=event.minute;identity.group=event.group;identity.match=event.match;
    identity.side=event.side<2?event.side:0;
    LiveShootoutProjection out;
    out.header=live_chance_projection(identity,groups,state,exe);out.header.side=event.side;
    out.home_goals=event.home_goals;out.away_goals=event.away_goals;
    if (event.stage==LiveShootoutStage::start)
        out.message=original_to_utf8(message_text(state,::bmh::layout::LiveMatchday_FormatError_TextPointerUnsigned+uint32_t(state.indoor)*4));
    else if (event.stage==LiveShootoutStage::end)
        out.message=original_to_utf8(message_text(state,::bmh::layout::LiveMatchday_FormatError_TextPointer6));
    else if (event.stage==LiveShootoutStage::preview) {
        if (event.of!=1 && event.of!=5) throw FormatError("live shoot-out: invalid Source round count");
        if (event.round<1 || event.round>event.of) throw FormatError("live shoot-out: invalid Source kick number");
        if (event.player!=0xfff) {
            if (event.player<0 || size_t(event.player)>=state.players.size())
                throw FormatError("live shoot-out: invalid Source taker");
            out.message=original_to_utf8(format_number(state,event.round,0x81)+message_text(state,::bmh::layout::LiveMatchday_FormatError_TextPointer4)+
                format_number(state,event.of,1)+message_text(state,::bmh::layout::LiveMatchday_FormatError_TextPointer5)+state.player_name(event.player));
        }
    } else throw FormatError("live shoot-out: invalid Source stage");
    return out;
}

LiveMatchdayScreen::LiveMatchdayScreen(const std::filesystem::path& data)
    : exe_(load_le(asset_path(data, "BMH.exe"))),
      background_(load_vga(asset_path(data / "KB", "133.VGA"))),
      board_(load_vga(asset_path(data / "KB", "21.VGA"))),
      score_digits_(load_vga(asset_path(data / "KB", "13.VGA"))),
      weather_(load_vga(asset_path(data / "KB", "22.VGA"))),
      goal_background_(load_vga(asset_path(data / "KB", "62.VGA"))),
      goal_sprites_(load_vga(asset_path(data / "KB", "63.VGA"))),
      set_piece_background_(load_vga(asset_path(data / "KB", "66.VGA"))),
      set_piece_sprites_(load_vga(asset_path(data / "KB", "68.VGA"))),
      font_(load_font(asset_path(data / "F", "BMH3.FNT"))),
      heading_(load_font(asset_path(data / "F", "BMH2.FNT"))) {
    if (background_.width != 320 || background_.height != 240 || board_.width != 207 || board_.height != 32 ||
        score_digits_.width != 189 || score_digits_.height != 42 || weather_.width < 25 || weather_.height < 11 ||
        goal_background_.width != 192 || goal_background_.height != 95 ||
        goal_sprites_.width != 141 || goal_sprites_.height != 101 ||
        set_piece_background_.width != 320 || set_piece_background_.height != 152 ||
        set_piece_sprites_.width != 256 || set_piece_sprites_.height != 199)
        throw FormatError("live matchday: original scoreboard assets have unexpected dimensions");
}

void LiveMatchdayScreen::text(Image& image, int x, int baseline, std::string_view value,
                              uint8_t color, int max_width, bool centered, bool gradient) const {
    std::string bytes = original(value);
    const int width = font_.text_width(bytes);
    int left = x;
    if (centered) left = x + (max_width / 2) - width / 2;
    const int top = baseline - font_.baseline;
    if (left < 0 || left + width > image.width || top < 0 || top + font_.height > image.height) return;
    if (!gradient || exe_.u8(::bmh::layout::TextGradientMode) != 2) {
        font_.draw(image, left, top, bytes, color);
        return;
    }
    int ramp = 0;
    while (ramp < 32 && exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2Unsigned + uint32_t(ramp)) != color) ++ramp;
    if (ramp == 32) {
        font_.draw(image, left, top, bytes, color);
        return;
    }
    Image mask(std::max(1, width), font_.height);
    font_.draw(mask, 0, 0, bytes, 1);
    for (int yy = 0; yy < font_.height; ++yy) for (int xx = 0; xx < width; ++xx)
        if (mask.at(xx, yy) && left + xx >= 0 && left + xx < image.width)
            image.at(left + xx, top + yy) = exe_.u8(::bmh::layout::Advertising_Text_RuntimeFieldUnsigned + uint32_t(ramp * 6 + std::min(yy, 5)));
}

Image LiveMatchdayScreen::render(const LiveMatchdayFrame& frame) const {
    Image image = background_;
    pattern_rect(image, exe_, 0x40, 2, 0xc0, 0x5f);
    text(image, 0x45, 8, frame.minute_text.empty() ? std::to_string(frame.minute) : frame.minute_text,
         0x27, 0xfb - 0x45, true);

    constexpr std::array<uint8_t, 4> fallback_y{138, 172, 104, 206};
    for (size_t i = 0; i < std::min<size_t>(4, frame.matches.size()); ++i) {
        const auto& match = frame.matches[i];
        const int y = exe_.contains(::bmh::layout::LiveMatchday_Render_ExecutableFieldUnsigned + uint32_t(i)) ? exe_.u8(::bmh::layout::LiveMatchday_Render_ExecutableFieldUnsigned + uint32_t(i)) : fallback_y[i];
        // DcThat assembles a 320px row from the left crop (src 3..115) and full right crop.
        copy_region(image, board_, 3, 0, 0, y, 113, 32);
        copy_region(image, board_, 0, 0, 113, y, 207, 32);

        text(image, 3, y + 8, match.home, 0x37, 105, true, frame.gradient_text);
        text(image, 119, y + 8, match.away, 0x37, 105, true, frame.gradient_text);
        for (size_t stat = 0; stat < 4; ++stat) {
            const uint8_t x_offset = exe_.u8(::bmh::layout::LiveMatchday_Render_RuntimeField2Unsigned + uint32_t(stat));
            const uint8_t y_offset = exe_.u8(::bmh::layout::LiveMatchday_Render_RuntimeField3Unsigned + uint32_t(stat));
            const uint8_t color = exe_.u8(::bmh::layout::LiveMatchday_Render_RuntimeFieldUnsigned + uint32_t(stat));
            const auto stat_text = [](int value) { return value >= 0 && value < 10 ? "%" + std::to_string(value) : std::to_string(value); };
            text(image, int(x_offset) + 3, y + int(y_offset), stat_text(match.home_stats[stat]), color, 24, false, frame.gradient_text);
            text(image, int(x_offset) + 119, y + int(y_offset), stat_text(match.away_stats[stat]), color, 24, false, frame.gradient_text);
        }
        const int rain_x = int(match.rain) * 25;
        if (!match.temperature_text.empty() && rain_x + 25 <= weather_.width)
            copy_region(image, weather_, rain_x, 0, 0x124, y + 0x11, 25, 11);
        if (match.temperature_text.size()) text(image, 0x122, y + 0x1c, match.temperature_text, 0x62, 29, true, frame.gradient_text);
        pattern_rect(image, exe_, 0xe6, y + 3, 0x39, 0x19);
        copy_region(image, score_digits_, 0x95, 0, 0xff, y + 9, 7, 15, 0xff);
        if (match.spectators_known) text(image, 0x123, y + 8, match.spectators_text,
                                      match.attendance_at_capacity ? 0xfb : 0x6b, 29, true, frame.gradient_text);
        if (!match.score_known) continue;
        const auto digit = [&](int value, int x) {
            const int sx = (std::min(value, 9) % 10) * 19;
            const int width = std::min(19, score_digits_.width - sx);
            copy_region(image, score_digits_, sx, 22, x + 1, y + 6, width, 20, 0xff);
        };
        // SelectsTransparentIndex right-aligns one decimal digit; its destination is one pixel right of param_8.
        digit(match.home_goals, 0xea);
        digit(match.away_goals, 0x106);
    }
    return image;
}

int LiveMatchdayScreen::ball_frame_count(int displacement) {
    if (displacement < -52 || displacement > 52)
        throw FormatError("live ball: position outside the Source chance range");
    return std::max(1, std::abs(displacement / 2));
}

Image LiveMatchdayScreen::render_ball(const LiveMatchdayFrame& frame, int displacement,
                                     int frame_index, const LiveChanceProjection* chance) const {
    const int count = ball_frame_count(displacement);
    if (frame_index < 0 || frame_index >= count)
        throw FormatError("live ball: animation frame out of range");
    Image image = chance ? render_chance(frame, *chance) : render(frame);
    // KB/13.VGA contains both the score digits and the rolling-ball atlas.
    // Source uses an opaque 129x13 strip, then three sprites transparent at 255.
    copy_region(image, score_digits_, 0, 0, 0x5f, 0x4b, 0x81, 0x0d);
    const int direction = displacement < 0 ? -1 : 1;
    const int rotation = ((direction * frame_index % 4) + 4) % 4;
    copy_region(image, score_digits_, rotation * 16, 13,
                0x5f + 0x3d + direction * frame_index * 2, 0x4b + 3, 16, 7, 255);
    for (uint32_t side = 0; side < 2; ++side)
        copy_region(image, score_digits_, exe_.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField12Unsigned + side), 13,
                    0x5f + exe_.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField13Unsigned + side), 0x4b + 2, 16, 9, 255);
    return image;
}

std::vector<Image> LiveMatchdayScreen::render_set_piece(const LiveMatchdayFrame& frame,
    const LiveSetPieceEvent& event, const Image* source_backdrop) const {
    const auto scene = live_set_piece_frames(event,exe_);
    Image pitch = set_piece_background_, atlas = set_piece_sprites_;
    if (event.source_side == 0) {
        // LiveSetPieceFrames/MirrorsSelectedCropsInsideKBNotScene mirrors KB66 and only the eight extra poses in KB68.
        for (int y=0;y<152;++y) for (int x=0;x<320;++x)
            pitch.at(319-x,y)=set_piece_background_.pixels[size_t(y)*320+size_t(x)];
        for (uint32_t pose=0;pose<8;++pose) {
            const int sx=exe_.u8(::bmh::layout::LiveMatchday_RenderSetPiece_RuntimeField2+pose), height=exe_.u8(::bmh::layout::LiveMatchday_RenderSetPiece_RuntimeField+pose);
            for (int y=0;y<height;++y) for (int x=0;x<32;++x)
                atlas.at(sx+31-x,158+y)=set_piece_sprites_.pixels[size_t(158+y)*256+size_t(sx+x)];
        }
    }
    const Image board = source_backdrop ? *source_backdrop : render(frame);
    if (board.width != 320 || board.height != 240) throw FormatError("set piece: invalid Source backdrop");
    std::vector<Image> images;images.reserve(scene.size());
    for (const auto& step:scene) {
        Image image=board;
        copy_region(image,pitch,step.camera_x,step.camera_y,64,2,192,95);
        for (const auto& sprite:step.sprites) {
            const int sx=(sprite.code&7)*32,sy=(sprite.code&0x38)*4;
            const int left=sprite.x-step.camera_x+64,top=sprite.y-step.camera_y+2;
            for (int y=0;y<32;++y) for (int x=0;x<32;++x) {
                const int dx=left+x,dy=top+y;
                if (sx+x>=256 || sy+y>=199 || dx<64 || dx>=256 || dy<2 || dy>=97) continue;
                uint8_t color=atlas.pixels[size_t(sy+y)*256+size_t(sx+x)];
                if (color==0) continue;
                if ((sprite.code&128) && color<16) color+=16;
                image.at(dx,dy)=color;
            }
        }
        images.push_back(std::move(image));
    }
    return images;
}

Palette LiveMatchdayScreen::shot_palette(const Palette& base, const LiveShotEvent& shot,
    const std::vector<MatchGroup>& groups, const GameState& state) const {
    const auto& group = groups.at(size_t(shot.group));
    Palette out = base;
    int home_shirt = 255, home_shorts = 255;
    const auto color = [&](int slot, uint32_t table, int index) {
        const auto channel = [&](int component) {
            return uint32_t(vga_dac_rgb8(exe_.u8(table + uint32_t(index * 3 + component))));
        };
        out.argb[size_t(slot)] = 0xff000000u | channel(0) << 16 | channel(1) << 8 | channel(2);
    };
    // TakesSecondHiddenBankArgumentItsMachine takes a second, hidden bank argument. Its machine code also
    // resolves clashing away kits; the decompiler drops those comparisons.
    for (int side = 0; side < 2; ++side) {
        const int team = group.fixtures.at(size_t(shot.match * 2 + side));
        if (team == -1) continue;
        if (team < 0 || size_t(team) >= state.club_ids.size())
            throw FormatError("live shot: original club colours missing");
        const int kit = state.club_kit(team);
        int shirt = kit >> 4, shorts = kit & 15;
        if (side == 0) { home_shirt = shirt; home_shorts = shorts; }
        else if (shorts == home_shorts || (shirt == home_shorts && shorts == home_shirt)) {
            if (shorts != home_shorts) shirt = shorts;
            else if (shorts != shirt) shorts = shirt;
            else shorts = shorts ? 0 : 1;
            if (shorts > 12 && shirt > 12) shorts = home_shorts != 10 ? 10 : 11;
        }
        const int bank = side * 16;
        color(bank + 13, ::bmh::layout::LiveMatchday_FormatError_ExecutableField12, shorts);
        color(bank + 14, ::bmh::layout::LiveMatchday_FormatError_ExecutableField13, shorts);
        color(bank + 15, ::bmh::layout::LiveMatchday_FormatError_ExecutableField14, shorts);
        color(bank + 9, ::bmh::layout::LiveMatchday_FormatError_ExecutableField13, shirt);
        color(bank + 8, ::bmh::layout::LiveMatchday_FormatError_ExecutableField14, shirt);
    }
    return out;
}

std::array<Image, 18> LiveMatchdayScreen::render_shot(const LiveMatchdayFrame& frame,
                                                   const LiveShotEvent& shot, const Image* source_backdrop) const {
    if (shot.acting_side < 0 || shot.acting_side > 1 || shot.lane < 83 || shot.lane > 234 ||
        shot.height < 23 || shot.height > 62 || shot.end_lane < 83 || shot.end_lane > 234 ||
        shot.end_height < 23 || shot.end_height > 70 ||
        (shot.miss ? shot.keeper_delay != 0 : shot.keeper_delay < 2 || shot.keeper_delay > 4))
        throw FormatError("live shot: invalid Source observation");

    Image atlas = goal_sprites_;
    // MirrorsSelectedCropsInsideKBNotScene mirrors selected crops inside KB63, not the scene or coordinates.
    if (shot.mirror_sprites) for (uint32_t sprite = 0; sprite < 20; ++sprite) {
        if (sprite >= 5 && sprite <= 16) continue;
        const int w = exe_.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField8 + sprite), h = exe_.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField9 + sprite);
        const int sx = exe_.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField10 + sprite), sy = exe_.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField11 + sprite);
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
            atlas.at(sx + w - 1 - x, sy + y) = goal_sprites_.pixels[size_t(sy + y) * 141 + size_t(sx + x)];
    }
    Image background = source_backdrop ? *source_backdrop : render(frame);
    if (background.width != 320 || background.height != 240) throw FormatError("shot: invalid Source backdrop");
    copy_region(background, goal_background_, 0, 0, 64, 2, 192, 95);
    const uint32_t x_table = shot.end_lane < 161 ? ::bmh::layout::LiveMatchday_FormatError_ExecutableField8 : ::bmh::layout::LiveMatchday_FormatError_ExecutableField9;
    const uint32_t hand_table = shot.end_lane < 161 ? ::bmh::layout::LiveMatchday_FormatError_ExecutableField6 : ::bmh::layout::LiveMatchday_FormatError_ExecutableField7;
    const auto signed_byte = [&](uint32_t address) { return int(int8_t(exe_.u8(address))); };
    const int reaction_band = (shot.end_height < 47) + (shot.end_height < 34);
    int band = reaction_band, step = 0, reached = 0, delay = 0;
    int keeper_x = 162, keeper_y = 70, keeper_progress = 0;
    int keeper_dx = shot.end_lane + signed_byte(x_table + 3 - band) -
        signed_byte(hand_table + 3 - band) - 162;
    int keeper_dy = shot.end_height + exe_.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField2 - band) - exe_.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField4 - band) - 70;
    int ball_x = 162, ball_y = 84, ball_dx = shot.lane - 162;
    int ball_height = shot.height, ball_progress = 0, ball_duration = 11, ball_sprite = 5;
    int shadow_y = 113;
    const int shadow_end = shot.miss ? 72 : 69;
    int net_sprite = 0;
    struct Entry { uint8_t x = 0, y = 0, sx = 0, sy = 0, w = 0, h = 0; };
    const auto pack = [&](int x, int y, int sprite, int dx, int dy) {
        // Int16T3 stores six bytes: X saturates at 255, Y wraps as a byte.
        const int left = int16_t(x + dx - signed_byte(x_table + uint32_t(sprite)));
        return Entry{uint8_t(std::min(left, 255)),
            uint8_t(y + dy - exe_.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField + uint32_t(sprite))),
            exe_.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField10 + uint32_t(sprite)), exe_.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField11 + uint32_t(sprite)),
            exe_.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField8 + uint32_t(sprite)), exe_.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField9 + uint32_t(sprite))};
    };
    const auto draw = [&](Image& image, const Entry& entry) {
        if (!entry.w) return;
        for (int y = 0; y < entry.h; ++y) for (int x = 0; x < entry.w; ++x) {
            // SizeT also clips the source crop: the second net ripple at
            // X128 requests 16 columns from the 141-column atlas (13 survive).
            if (entry.sx + x >= atlas.width || entry.sy + y >= atlas.height) continue;
            const int dx = entry.x + x, dy = entry.y + y;
            if (dx < 64 || dx >= 256 || dy < 2 || dy >= 97) continue;
            uint8_t color = atlas.pixels[size_t(entry.sy + y) * 141 + size_t(entry.sx + x)];
            if (color == 0) continue; // UsesForTheseSprites uses DesktopRuntimeField=0 for these sprites.
            // PathTreatsActive/UsesForTheseSprites bit 8 selects the opposing keeper's colour bank.
            // It does not mirror the picture. LimitsWritesToViewport limits writes to the viewport.
            if (shot.acting_side == 0 && color < 16) color += 16;
            image.at(dx, dy) = color;
        }
    };
    std::array<Image, 18> frames;
    for (int index = 0; index < 18; ++index) {
        if (index == 12) {
            keeper_x += keeper_dx * 11 / exe_.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField7 + uint32_t(band));
            keeper_y += keeper_dy * 11 / exe_.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField7 + uint32_t(band));
            keeper_dy = (71 - keeper_y) * 2;
            ball_y = shot.height; ball_height = 62; shadow_y = shadow_end;
            if (!shot.miss) {
                // Watcom SAR by 5 rounds negative values down, unlike C++ /32.
                ball_x = shot.lane + (ball_dx >= 0 ? ball_dx / 32 : -((-ball_dx + 31) / 32));
            } else {
                ball_x = keeper_x - signed_byte(x_table + step) + signed_byte(hand_table + step);
                ball_y = keeper_y - exe_.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField + uint32_t(step)) + exe_.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField3 + uint32_t(step));
            }
            keeper_progress = 0; ball_progress = 1;
            keeper_dx /= 8; ball_dx /= 8; ball_duration = 6; band = 3;
        }
        if (index == exe_.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField6 + uint32_t(step * 4 + reaction_band)) && reached < 3) {
            ++reached;
            delay = !shot.miss && step == 0 ? shot.keeper_delay : 1;
        }
        if (delay) { if (delay == 1) step = reached; --delay; }
        const uint32_t order = index < 10 || shot.miss ? ::bmh::layout::LiveMatchday_FormatError_ExecutableField10 : ::bmh::layout::LiveMatchday_FormatError_ExecutableField11;
        if (index == 17) { keeper_dy = 0; keeper_y = 70; }
        int dx = keeper_dx * keeper_progress / exe_.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField7 + uint32_t(band));
        int dy = keeper_dy * keeper_progress / exe_.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField7 + uint32_t(band));
        std::array<Entry, 4> entries{};
        if (step) {
            ++keeper_progress;
            entries[0] = pack(keeper_x, 70, step + 16, dx, 0);
        }
        entries[exe_.u8(order + 1)] = pack(keeper_x, keeper_y, step, dx, dy);
        if (!shot.miss || index < 12) {
            dx = ball_dx * ball_progress / 11;
            dy = (ball_height - ball_y) * ball_progress / ball_duration;
        } else {
            ball_x = keeper_x - signed_byte(x_table + step) + signed_byte(hand_table + step);
            ball_y = keeper_y - exe_.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField + uint32_t(step)) + exe_.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField3 + uint32_t(step));
        }
        if (index < 12 && index && index % 2 == 0 && (index < 10 || !shot.miss)) ++ball_sprite;
        if (ball_sprite >= 8)
            entries[exe_.u8(order + 2)] = pack(ball_x, shadow_y, ball_sprite + 3,
                dx, ball_progress * (shadow_end - shadow_y) / 11);
        entries[exe_.u8(order + 3)] = pack(ball_x, ball_y, ball_sprite, dx, dy);
        ++ball_progress;
        frames[size_t(index)] = background;
        for (int slot = 0; slot < 4; ++slot) {
            const auto& entry = entries[size_t(slot)];
            const int absolute = index * 4 + slot;
            if (entry.w && entry.h == 9 && (absolute == 46 || absolute == 50 || absolute == 54 || absolute == 58)) {
                draw(frames[size_t(index)], Entry{uint8_t(entry.x - 3 - net_sprite),
                    uint8_t(entry.y - 5 - net_sprite), uint8_t(64 << net_sprite),
                    exe_.u8(::bmh::layout::LiveMatchday_FormatError_RuntimeField5 + uint32_t(net_sprite)), 16, 15});
                if (absolute == 50) net_sprite = 1;
            }
            draw(frames[size_t(index)], entry);
        }
    }
    return frames;
}

Image LiveMatchdayScreen::render_booking(const LiveMatchdayFrame& frame,
                                       const LiveBookingProjection& booking) const {
    Image image=render_header(frame,booking.header,booking.home_goals,booking.away_goals,
                              booking.score_known,247);
    // Source flips TextGradientMode for the message too, bypassing the gradient font mode.
    text(image,64,95,booking.message,39,192,true,false);
    return image;
}

Image LiveMatchdayScreen::render_shootout(const LiveMatchdayFrame& frame,
                                        const LiveShootoutProjection& shootout) const {
    Image image=render_header(frame,shootout.header,shootout.home_goals,shootout.away_goals,true,95);
    text(image,64,73,shootout.message,95,192,true,frame.gradient_text);
    return image;
}

Image LiveMatchdayScreen::render_sportschau_header(const LiveChanceProjection& header,
    int home_before, int away_before, bool gradient_text) const {
    LiveMatchdayFrame frame;
    frame.gradient_text = gradient_text;
    return render_header(frame, header, home_before, away_before, true, 0x5f);
}

Image LiveMatchdayScreen::render_header(const LiveMatchdayFrame& frame,
    const LiveChanceProjection& header,int home_goals,int away_goals,bool score_known,uint8_t actor_color) const {
    Image image=render(frame);
    pattern_rect(image,exe_,64,2,192,95);
    text(image,64,8,header.stadium,39,192,true,false);
    copy_region(image,score_digits_,149,0,158,40,7,15,255);
    for (int side=0;side<2;++side) {
        const auto& name=side==0?header.home:header.away;
        if (side==header.side) {
            const auto bytes=original(name);
            const int left=160-heading_.text_width(bytes)/2;
            const int top=18+side*12-heading_.baseline;
            if (left>=0 && left+heading_.text_width(bytes)<=320 && top>=0 && top+heading_.height<=240)
                heading_.draw(image,left,top,bytes,actor_color);
        } else text(image,64,18+side*12,name,39,192,true,false);
        if (!score_known) continue;
        int score=side==0?home_goals:away_goals;
        if (score<0) throw FormatError("live booking: negative Source score");
        int digits=1;for (int n=score;n>=10;n/=10) ++digits;
        // SelectsTransparentIndex: 19-pixel atlas digits with a 20-pixel advance, centered.
        int x=exe_.u8(::bmh::layout::LiveMatchday_RenderHeader_RuntimeFieldUnsigned+uint32_t(side))-19+20*digits-10*digits;
        do {
            const int sx=(score%10)*19;
            // KB13 is 189px wide: digit nine ends at the atlas boundary.
            copy_region(image,score_digits_,sx,22,x,37,std::min(19,score_digits_.width-sx),20,255);
            score/=10;x-=20;
        } while (score);
    }
    text(image,158,24,original_to_utf8(exe_.cstring(::bmh::layout::LiveMatchday_RenderHeader_TextPointer)),39,0,false,false);
    return image;
}

Image LiveMatchdayScreen::render_reality(const LiveMatchdayFrame& frame,
                                        const LiveChanceProjection& header) const {
    auto title = header; title.description.clear(); title.shooter_detail.clear();
    LiveMatchdayFrame field_frame;field_frame.gradient_text=frame.gradient_text;
    Image image = render_chance(field_frame,title); // KB133 field; no lower match-table rows
    // RealityReporterPanel saves/restores the lower 320x77 pitch independently.
    // PatternRect receives corner coordinates: x64,y32 through x255,y96.
    pattern_rect(image,exe_,64,32,192,65);
    return image;
}

Image LiveMatchdayScreen::render_chance(const LiveMatchdayFrame& frame,
                                      const LiveChanceProjection& chance) const {
    Image image = render(frame);
    // Button(side|0x80) suppresses the large score, uses BMH2 for the
    // acting club, then returns to BMH3. Header/player text bypasses gradients.
    pattern_rect(image, exe_, 64, 2, 192, 95);
    const auto heading = [&](std::string_view value, int baseline, uint8_t color, int left, int right) {
        const std::string bytes = original(value);
        const int x = left + (right - left) / 2 - heading_.text_width(bytes) / 2;
        if (x >= 0 && x + heading_.text_width(bytes) <= image.width && baseline >= heading_.baseline &&
            baseline - heading_.baseline + heading_.height <= image.height)
            heading_.draw(image, x, baseline - heading_.baseline, bytes, color);
    };
    text(image, 64, 8, chance.stadium, 0x27, 192, true, false);
    if (chance.side == 0) heading(chance.home, 18, 0x5f, 64, 256);
    else text(image, 64, 18, chance.home, 0x27, 192, true, false);
    text(image, 158, 24, original_to_utf8(exe_.cstring(::bmh::layout::LiveMatchday_RenderHeader_TextPointer)), 0x27, 0, false, false);
    if (chance.side == 1) heading(chance.away, 30, 0x5f, 64, 256);
    else text(image, 64, 30, chance.away, 0x27, 192, true, false);
    if (chance.stage == LiveChanceStage::preview) {
        text(image, 64, 43, chance.description, 0x5f, 192, true, frame.gradient_text);
        text(image, 64, 95, chance.shooter_detail, 0x5f, 192, true, frame.gradient_text);
    } else {
        heading(chance.outcome, 65, 0x27, 69, 251);
        if (chance.ticker) text(image, 69, 83, chance.ticker_score, 0x5f, 182, true, frame.gradient_text);
        text(image, 64, 95, chance.participants, 0x27, 192, true, false);
    }
    return image;
}

} // namespace bmh::ui
