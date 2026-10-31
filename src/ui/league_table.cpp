// SPDX-License-Identifier: GPL-3.0-only
#include "game/state.hpp"
#include "formats/executable_layout.hpp"
#include "ui/league_table.hpp"

#include <algorithm>
#include <sstream>

#include "game/messages.hpp"
#include "game/season.hpp"
#include "game/schedule.hpp"
#include "game/state.hpp"
#include "game/table.hpp"
#include "ui/manager.hpp"
#include "ui/setup.hpp"

namespace bmh::ui {
namespace {
constexpr uint8_t kText = 0x6a;
constexpr uint8_t kHeading = 0x75;
constexpr int kFirstOriginalRowY = 0x23; // FriendlyMatchScreen passes 0x23 to FontBytes.
constexpr int kOriginalRowHeight = 7;   // FontBytes advances by one 7-pixel text row.
constexpr std::array<uint8_t, bmh::kLeagueCount> kEuropeanPlaces{0, 3, 2, 2, 4, 0, 0, 0, 0};
constexpr std::array<uint8_t, bmh::kLeagueCount> kRelegationPlaces{3, 4, 2, 2, 3, 0, 0, 0, 0};

std::string font_bytes(std::string_view value) {
    try { return utf8_to_original(value); }
    catch (const FormatError&) {
        std::string safe;
        for (unsigned char c : value) safe += c < 128 ? char(c) : '?';
        return safe;
    }
}

std::string formatted_field(int value, int width) {
    const bool negative = value < 0;
    std::string digits = std::to_string(negative ? -int64_t(value) : int64_t(value));
    while (int(digits.size()) < width - int(negative)) digits.insert(digits.begin(), '%');
    if (negative) digits.insert(digits.begin(), '-');
    return digits;
}

std::string original_text(const LeImage& exe, uint32_t address) {
    return original_to_utf8(exe.cstring(address));
}

uint8_t original_name_color(const bmh::GameState& game, int runtime_team) {
    const size_t count = std::min<size_t>(game.manager_teams.size(), 4);
    const int active = game.memory.has(::bmh::layout::ActiveManager) ? game.memory.u8(::bmh::layout::ActiveManager) : -1;
    const int manager_count = game.memory.has(::bmh::layout::ManagerCount) ? game.memory.u8(::bmh::layout::ManagerCount) : int(count);
    const bool current_human_context = game.memory.has(::bmh::layout::DeskPosition) &&
        game.memory.u8(::bmh::layout::DeskPosition) < manager_count;
    for (size_t slot = 0; slot < count; ++slot) {
        const size_t record = slot * 0xb0c;
        if (record + 0xa8 >= game.manager_records.size() || game.manager_records[record + 0xa8] == 0)
            continue;
        // manager_teams is the authoritative typed counterpart of record +0x290.
        const int team = game.manager_teams[slot];
        if (team == runtime_team)
            return int(slot) == active && current_human_context ? 0x7a : 0x75;
    }
    if (game.memory.has(::bmh::layout::AlltimeTable_TeamColor_RuntimeField) && game.memory.u16(::bmh::layout::AlltimeTable_TeamColor_RuntimeField) == runtime_team) return 0xf7;
    return 0x5f;
}

void blit_table_region(Image& out, const Image& source, int source_y, int dest_y, int height) {
    if (source.width < 318 || source_y < 0 || height < 0 || source_y + height > source.height)
        throw FormatError("league table: original KB/56 crop outside image");
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < 318; ++x)
            out.at(x + 1, dest_y + y) = source.pixels[size_t(source_y + y) * source.width + x];
}

uint8_t original_rank_color(int league, int zero_based_rank) {
    uint8_t color = 0x37;
    if (zero_based_rank < kEuropeanPlaces[size_t(league)]) color = 0x51;
    else if (int(kLeagueSizes[league]) - zero_based_rank <= kRelegationPlaces[size_t(league)]) color = 0xf7;
    if (league == 0) {
        if (zero_based_rank == 0) color = 0x27;
        else if (zero_based_rank < 5) color = 0x7a;
    }
    if ((league == 2 || league == 3) && (zero_based_rank == 1 || zero_based_rank == 2)) color = 0x27;
    return color;
}

void copy_region(Image& out, const Image& source, int sx, int sy, int x, int y,
                 int width, int height, int transparent = -1) {
    if (sx < 0 || sy < 0 || x < 0 || y < 0 || width < 0 || height < 0 ||
        sx + width > source.width || sy + height > source.height ||
        x + width > out.width || y + height > out.height)
        throw FormatError("league table: original atlas bounds");
    for (int dy = 0; dy < height; ++dy) for (int dx = 0; dx < width; ++dx) {
        const auto pixel = source.pixels[size_t(sy + dy) * size_t(source.width) + size_t(sx + dx)];
        if (pixel != transparent) out.at(x + dx, y + dy) = pixel;
    }
}

void fill_region(Image& out, int x, int y, int width, int height, uint8_t color) {
    for (int dy = y; dy < y + height; ++dy)
        for (int dx = x; dx < x + width; ++dx) out.at(dx, dy) = color;
}
}

namespace {
LeagueTableState project_league_table(const bmh::GameState& game, int manager_slot, int league_id,
                                      const LeagueTableProjectionContext* source_context) {
    if (manager_slot < 0 || manager_slot > 3 || manager_slot >= int(game.manager_teams.size()))
        throw FormatError("league table: invalid manager slot");
    if (league_id < 0 || league_id >= bmh::kLeagueCount)
        throw FormatError("league table: invalid league");

    const uint32_t family_word_address = ::bmh::layout::LeagueTableDisplayModes + uint32_t(manager_slot) * 2;
    const uint16_t saved_word = game.memory.has(family_word_address + 1)
        ? game.memory.u16(family_word_address) : 0;
    const uint16_t source_word = source_context
        ? source_context->display_word_override.value_or(saved_word) : saved_word;
    if (source_context && (source_context->normal_mode < 0 || source_context->normal_mode > 1 ||
        source_context->first_round < 0 || source_context->end_round < 0 ||
        source_context->first_round > source_context->end_round ||
        source_context->aggregation_flags < 0 || source_context->aggregation_flags > 7 ||
        (source_context->forecast && !source_context->accepted_engine_table) ||
        (source_context->accepted_engine_table && source_context->accepted_engine_league != league_id)))
        throw FormatError("league table: invalid Source projection context");
    bmh::LeagueTable engine_table;
    if (source_context && source_context->accepted_engine_table) {
        if (source_context->accepted_engine_league != league_id ||
            source_context->accepted_engine_table->league != league_id ||
            source_context->accepted_engine_table->size != bmh::kLeagueSizes[league_id])
            throw FormatError("league table: accepted engine projection belongs to another league");
        engine_table = *source_context->accepted_engine_table;
    } else if (source_context) {
        const uint16_t draw_word = uint16_t(source_word & 0x7f);
        const int points_mode = source_context->normal_mode + 0x0e -
            int((draw_word >> 3) & 0x0e);
        engine_table = bmh::compute_league_table(game, league_id, source_context->first_round,
            source_context->end_round, source_context->aggregation_flags, points_mode);
    } else {
        engine_table = bmh::compute_league_table(game, league_id, 0,
            bmh::league_matchdays(league_id), 7, 1);
    }
    LeagueTableState state;
    state.manager_slot = manager_slot;
    state.league_id = league_id;
    // LeagueName uses the shared German TEXTD names (with A/B suffixes) for
    // domestic leagues 0..4. Foreign leagues use a Native nation/team name,
    // not a TEXTD entry at the domestic league-title address.
    state.league_name = league_id < 5
        ? original_to_utf8(bmh::league_name(game, league_id))
        : original_to_utf8(game.club_name(kNoTeam + league_id - 4));
    state.header_text = original_to_utf8(bmh::message_text(game, ::bmh::layout::LeagueTable_OriginalToUtf8_TextPointer2));
    // SelectsOriginalPanelWhenClear selects the original panel when SolidPanelBackground is clear.
    // Older/minimal saves may not serialize this presentation option.
    state.original_table_panel = !game.memory.has(::bmh::layout::SolidPanelBackground) || game.memory.u8(::bmh::layout::SolidPanelBackground) == 0;
    state.decorated_background = !game.memory.has(::bmh::layout::PlainBackground) || game.memory.u8(::bmh::layout::PlainBackground) == 0;
    state.secondary_leagues = !game.memory.has(::bmh::layout::CalendarState_FormatError_RuntimeField2) || game.memory.u8(::bmh::layout::CalendarState_FormatError_RuntimeField2) == 0;
    state.display_word = source_word;
    state.rounds_played = league_id < int(game.league_rounds.size()) ? game.league_rounds[size_t(league_id)] : 0;
    state.raw_round_byte = uint8_t(state.rounds_played);
    state.points_mode = source_context
        ? source_context->normal_mode + 0x0e - int((uint16_t(source_word & 0x7f) >> 3) & 0x0e)
        : 1;
    if (source_context) {
        state.source_controls_projection = true;
        state.source_show_previous_rank = game.memory.has(::bmh::layout::LeagueTable_OriginalToUtf8_RuntimeField) &&
            game.memory.u8(::bmh::layout::LeagueTable_OriginalToUtf8_RuntimeField) != 0 && (state.raw_round_byte & 0x7f) > 1;
        state.source_aggregation_flags = source_context->aggregation_flags;
        state.source_normal_mode = source_context->normal_mode;
        state.retained_range_label_source_bytes = source_context->retained_range_label_source_bytes;
        state.forecast = source_context->forecast;
    }
    if (engine_table.size != bmh::kLeagueSizes[league_id])
        throw FormatError("league table: incomplete engine projection for league " + std::to_string(league_id));

    std::array<int, 20> order{};
    for (int club = 0; club < engine_table.size; ++club) order[size_t(club)] = club;
    std::sort(order.begin(), order.begin() + engine_table.size, [&](int lhs, int rhs) {
        return engine_table.rank(lhs) < engine_table.rank(rhs);
    });
    const int first_team = bmh::league_first_team(league_id);
    state.rows.reserve(size_t(engine_table.size));
    for (int i = 0; i < engine_table.size; ++i) {
        const int club = order[size_t(i)];
        const int runtime_team = first_team + club;
        const int games = engine_table.games(club);
        LeagueTableRow row;
        row.rank = engine_table.rank(club) + 1;
        row.runtime_team_id = runtime_team;
        row.club_identity_key = game.club_ids.at(size_t(runtime_team));
        row.name = original_to_utf8(game.club_name(runtime_team));
        row.name_color = original_name_color(game, runtime_team);
        row.games = games;
        row.wins = engine_table.wins[size_t(club)];
        row.losses = engine_table.defeats[size_t(club)];
        row.draws = games - row.wins - row.losses;
        row.goals_for = engine_table.goals_for[size_t(club)];
        row.goals_against = engine_table.goals_against[size_t(club)];
        row.points = engine_table.points[size_t(club)];
        row.previous_rank = engine_table.previous_rank(club) + 1;
        row.form_bits = engine_table.form[size_t(club)];
        state.rows.push_back(std::move(row));
    }
    return state;
}
} // namespace

LeagueTableState league_table_from_game(const bmh::GameState& game, int manager_slot, int league_id) {
    return project_league_table(game, manager_slot, league_id, nullptr);
}

LeagueTableState league_table_from_game(const bmh::GameState& game, int manager_slot, int league_id,
                                       const LeagueTableProjectionContext& context) {
    return project_league_table(game, manager_slot, league_id, &context);
}

std::array<LeagueTableState, 9> league_tables_from_game(const bmh::GameState& game, int manager_slot) {
    std::array<LeagueTableState, 9> tables;
    for (int league = 0; league < bmh::kLeagueCount; ++league)
        tables[size_t(league)] = league_table_from_game(game, manager_slot, league);
    return tables;
}

std::string league_table_json(const LeagueTableState& state) {
    std::ostringstream out;
    out << "{\"manager_slot\":" << state.manager_slot << ",\"league_id\":" << state.league_id
        << ",\"league_name\":" << json_quote(state.league_name)
        << ",\"header_text\":" << json_quote(state.header_text)
        << ",\"original_table_panel\":" << (state.original_table_panel ? "true" : "false")
        << ",\"decorated_background\":" << (state.decorated_background ? "true" : "false")
        << ",\"rounds_played\":" << state.rounds_played
        << ",\"points_mode\":" << state.points_mode << ",\"rows\":[";
    for (size_t i = 0; i < state.rows.size(); ++i) {
        const auto& row = state.rows[i];
        if (i) out << ',';
        out << "{\"rank\":" << row.rank << ",\"runtime_team_id\":" << row.runtime_team_id
            << ",\"club_identity_key\":" << row.club_identity_key
            << ",\"name\":" << json_quote(row.name) << ",\"games\":" << row.games
            << ",\"wins\":" << row.wins << ",\"draws\":" << row.draws
            << ",\"losses\":" << row.losses << ",\"goals_for\":" << row.goals_for
            << ",\"goals_against\":" << row.goals_against << ",\"points\":" << row.points
            << ",\"previous_rank\":" << row.previous_rank << '}';
    }
    out << "]}";
    return out.str();
}

LeagueTableScreen::LeagueTableScreen(const std::filesystem::path& data,
                                     std::array<LeagueTableState, 9> tables,
                                     int initial_league)
    : tables_(std::move(tables)), font_(load_font(asset_path(data / "F", "BMH3.FNT"))),
      desktop_background_(load_vga(asset_path(data / "KB", "129.VGA"))),
      background_(load_vga(asset_path(data / "KB", "56.VGA"))),
      widget_atlas_(load_vga(asset_path(data / "KB", "0.VGA"))),
      selector_atlas_(load_vga(asset_path(data / "KB", "1.VGA"))),
      exe_(load_le(asset_path(data, "BMH.exe"))),
      texts_(load_textd(asset_path(data, "TEXTD.BLK"))),
      sprite_font_(data),
      selected_league_(initial_league) {
    if (initial_league < 0 || initial_league >= bmh::kLeagueCount ||
        desktop_background_.width != 320 || desktop_background_.height != 240 ||
        background_.width != 318 || background_.height != 207)
        throw FormatError("league table screen: invalid initial league");
    const int manager = tables_[0].manager_slot;
    if (manager < 0 || manager > 3) throw FormatError("league table screen: invalid manager");
    for (int league = 0; league < bmh::kLeagueCount; ++league) {
        const auto& table = tables_[size_t(league)];
        if (table.manager_slot != manager || table.league_id != league || table.header_text.empty() ||
            table.rows.size() != size_t(bmh::kLeagueSizes[league]) ||
            table.original_table_panel != tables_[0].original_table_panel ||
            table.decorated_background != tables_[0].decorated_background ||
            table.secondary_leagues != tables_[0].secondary_leagues)
            throw FormatError("league table screen: inconsistent owned projections");
        for (size_t i = 0; i < table.rows.size(); ++i) {
            const auto& row = table.rows[i];
            if (row.rank != int(i) + 1 || row.name.empty() || row.runtime_team_id < 0 ||
                row.club_identity_key < 0 || row.games < 0 || row.wins < 0 || row.draws < 0 ||
                row.losses < 0 || row.wins + row.draws + row.losses != row.games)
                throw FormatError("league table screen: invalid ranked row");
        }
    }
}

LeagueTableScreen::LeagueTableScreen(const std::filesystem::path& data,
                                     std::array<LeagueTableState, 9> tables,
                                     int initial_league, OpponentMode)
    : LeagueTableScreen(data, std::move(tables), initial_league) {
    opponent_mode_ = true;
    opponent_default_league_ = initial_league;
    league_group_ = initial_league >= 5;
}

LeagueTableScreen::LeagueTableScreen(const std::filesystem::path& data,
                                     std::array<LeagueTableState, 9> tables,
                                     int initial_league, FamilyMode family)
    : LeagueTableScreen(data, std::move(tables), initial_league) {
    const auto& id = family.identity;
    if (id.manager < 0 || id.manager > 3 || id.league < 0 || id.league >= bmh::kLeagueCount ||
        id.league_group > 1 || id.manager_default_league < 0 || id.manager_default_league >= bmh::kLeagueCount ||
        id.revision == UINT64_MAX || id.sequence == UINT64_MAX ||
        id.manager != tables_[0].manager_slot || id.league != initial_league || family.source_selection_mode > 2)
        throw FormatError("league table screen: invalid family identity");
    family_ = std::move(family);
    league_group_ = family_->identity.league_group;
    for (auto& table : tables_) {
        if (!table.source_controls_projection) table.display_word = id.expected_family_word;
    }
}

void LeagueTableScreen::text(Image& image, int x, int y, std::string_view value,
                             uint8_t color, int max_width) const {
    std::string bytes = font_bytes(value);
    while (!bytes.empty() && font_.text_width(bytes) > max_width) bytes.pop_back();
    const int top = y - font_.baseline;
    if (x >= 0 && top >= 0 && top + font_.height <= image.height)
        font_.draw(image, x, top, bytes, color);
}

Image LeagueTableScreen::render() const {
    const auto& table = state();
    Image image = table.decorated_background ? desktop_background_ : Image(320, 240);
    if (!table.decorated_background) {
        // PlainWindowFrameUsedBy's undecorated desktop frame.
        for (int y = 4; y <= 16; ++y) for (int x = 0; x < 320; ++x) image.at(x, y) = 0x44;
        for (int y = 5; y <= 15; ++y) for (int x = 1; x < 319; ++x) image.at(x, y) = 0x39;
        for (int y = 228; y <= 237; ++y) for (int x = 0; x < 320; ++x) image.at(x, y) = 0x44;
        for (int y = 229; y <= 236; ++y) for (int x = 1; x < 319; ++x) image.at(x, y) = 0x39;
    }
    const int bottom = int(table.rows.size()) * 7 + 30;
    if (table.original_table_panel) {
        // FriendlyMatchScreen calls LoadGameEndTaxAnimationAssets with y=20 and bottom=league_size*7+30.
        // LoadGameEndTaxAnimationAssets copies source y=1 to destination (1,21), 318 x (league_size*7+9).
        blit_table_region(image, background_, 1, 21, int(table.rows.size()) * 7 + 9);
    } else {
        // Alternate LoadGameEndTaxAnimationAssets path fills the same panel with 0x42.
        for (int y = 20; y <= bottom; ++y)
            for (int x = 0; x < 320; ++x) image.at(x, y) = 0x42;
    }
    // PaintsFourBorderEdges applies the border in both presentation modes.
    for (int y = 20; y <= bottom; ++y) image.at(0, y) = 0x46;
    for (int x = 0; x < 320; ++x) image.at(x, 20) = 0x46;
    for (int x = 0; x < 320; ++x) image.at(x, bottom) = 0x40;
    for (int y = 20; y <= bottom; ++y) image.at(319, y) = 0x40;

    // The shared PlainWindowFrameUsedBy title is drawn in mode 5; its text call restores mode 3.
    const auto league_title = table.league_name.empty()
        ? "Liga " + std::to_string(table.league_id + 1) : table.league_name;
    std::string title = league_title;
    std::string subtitle;
    if (family_ || opponent_mode_) {
        const auto textd = [&](uint32_t address) {
            if (address < ::bmh::layout::TextPointerTable || ((address - ::bmh::layout::TextPointerTable) & 3) != 0)
                throw FormatError("league table: invalid Source TEXTD pointer");
            const size_t index = size_t((address - ::bmh::layout::TextPointerTable) / 4);
            if (index >= texts_.strings.size()) throw FormatError("league table: missing Source TEXTD string");
            return original_to_utf8(texts_[index]);
        };
        const auto selection_mode = family_ ? family_->source_selection_mode : 2;
        title = textd(::bmh::layout::LeagueTable_OriginalToUtf8_TextPointer + uint32_t(selection_mode)*4) + league_title;
        const int raw_round = table.source_controls_projection ? table.raw_round_byte : table.rounds_played;
        const int round = (raw_round & 0x7f) + 1;
        if (round > league_matchdays(table.league_id)) {
            subtitle = textd(::bmh::layout::CareerSession_FormatError_TextPointer3);
        } else {
        const uint32_t round_prefix = ((raw_round & 0x80) || (family_ && family_->identity.source_param2))
            ? ::bmh::layout::LeaguePairings_FormatError_TextPointer4 : ::bmh::layout::CareerSession_FormatError_TextPointer2;
            subtitle = textd(round_prefix) + std::to_string(round) +
                original_text(exe_, ::bmh::layout::CareerSession_FormatError_TextPointer) + textd(::bmh::layout::CareerSession_FormatError_TextPointer4);
        }
    }
    const int title_width = sprite_font_.text_width(title);
    sprite_font_.draw(image, SpriteFont::centered_title_x(title_width), 13, title);
    if (!subtitle.empty()) sprite_font_.draw(image, 5, 235, subtitle);

    // FriendlyMatchScreen draws LeagueTable_OriginalToUtf8_TextPointer2 at x=4, baseline=0x1b, color=0x75.
    text(image, 4, 0x1b, table.header_text, kHeading, 312);
    // DrawnFirstBy restores mode 3 before TextWidth3 draws the header/rows,
    // which use BMH3.FNT. Their column starts are x=0x1e,0x72,0xce,0xff.
    for (const auto& row : table.rows) {
        const int y = kFirstOriginalRowY + (row.rank - 1) * kOriginalRowHeight;
        const auto rank_color = original_rank_color(table.league_id, row.rank - 1);
        if (row.rank < 10) {
            text(image, 1, y, formatted_field(row.rank, 2) + original_text(exe_, ::bmh::layout::CupDraw_DrawGroupOverview_TextPointer2), rank_color, 24);
        } else {
            text(image, 2, y, std::to_string(row.rank / 10), rank_color, 8);
            text(image, 6, y, formatted_field(row.rank % 10, 1) + original_text(exe_, ::bmh::layout::CupDraw_DrawGroupOverview_TextPointer2), rank_color, 8);
        }
        if (table.source_controls_projection && table.source_show_previous_rank) {
            const uint8_t previous_color = row.rank < row.previous_rank ? 0x75
                : row.previous_rank < row.rank ? 0xf7 : kText;
            text(image, 12, y, "(" + formatted_field(row.previous_rank, 2) +
                original_text(exe_, ::bmh::layout::CupDraw_DrawGroupOverview_TextPointer3), previous_color, 24);
        }
        text(image, 30, y, row.name, row.name_color, 78);
        const std::string matches = formatted_field(row.games, 2) + original_text(exe_, ::bmh::layout::CupDraw_DrawGroupOverview_TextPointer4) +
            formatted_field(row.wins, 2) + original_text(exe_, ::bmh::layout::CupDraw_DrawGroupOverview_TextPointer5) +
            formatted_field(row.draws, 2) + original_text(exe_, ::bmh::layout::CupDraw_DrawGroupOverview_TextPointer5) +
            formatted_field(row.losses, 2) + original_text(exe_, ::bmh::layout::CupDraw_DrawGroupOverview_TextPointer4);
        text(image, 0x72, y, matches, kText, 90);
        if (table.source_controls_projection) {
            // LeagueTableRuntimeField is runtime TEXTD slot 72, loaded from "-NUS-nus"
            // by the TEXTD loader; it is not an executable-file address.
            const auto& glyphs = texts_[72];
            if (glyphs.size() < 8)
                throw FormatError("league table: Source form TEXTD record is incomplete");
            // Uint8T draws eight packed three-bit results right-to-left from x=0xcf.
            for (int form_index = 0; form_index < 8; ++form_index) {
                const uint8_t value = uint8_t((row.form_bits >> (form_index * 3)) & 7);
                std::string glyph(1, '-');
                if ((table.source_aggregation_flags & 1) != 0 && (value & 4) == 0) {
                    glyph[0] = glyphs[size_t(4 + (value & 3))];
                } else if ((table.source_aggregation_flags & 2) != 0 && (value & 4) != 0) {
                    glyph[0] = glyphs[size_t(value & 3)];
                }
                text(image, 207 - form_index * 6, y, original_to_utf8(glyph), kText, 8);
            }
        }
        std::string points = original_text(exe_, ::bmh::layout::CupDraw_DrawGroupOverview_TextPointer6);
        if (table.source_controls_projection) {
            if (table.source_normal_mode != 0) points += original_text(exe_, ::bmh::layout::CupDraw_DrawGroupOverview_TextPointer7);
            points += formatted_field(row.points, 2) + original_text(exe_, ::bmh::layout::CupDraw_DrawGroupOverview_TextPointer8);
            if (table.source_normal_mode != 0) {
                points += original_text(exe_, ::bmh::layout::LeagueTable_Glyph_TextPointer);
            } else {
                points += original_text(exe_, ::bmh::layout::CupDraw_DrawGroupOverview_TextPointer9);
                points += formatted_field(row.games * 2 - row.points, 2) + original_text(exe_, ::bmh::layout::CupDraw_DrawGroupOverview_TextPointer8);
            }
        } else {
            points += original_text(exe_, ::bmh::layout::CupDraw_DrawGroupOverview_TextPointer7) + formatted_field(row.points, 2) +
                original_text(exe_, ::bmh::layout::CupDraw_DrawGroupOverview_TextPointer8) + original_text(exe_, ::bmh::layout::LeagueTable_Glyph_TextPointer);
        }
        text(image, 0xce, y, points, kText, 46);
        const int difference = row.goals_for - row.goals_against;
        const auto sign = original_text(exe_, difference < 0 ? ::bmh::layout::Bank_OriginalToUtf8_ExecutableField5 : difference > 0 ? ::bmh::layout::Bank_OriginalToUtf8_ExecutableField6 : ::bmh::layout::Bank_OriginalToUtf8_ExecutableField7);
        const std::string goals = formatted_field(row.goals_for, 3) + original_text(exe_, ::bmh::layout::CupDraw_DrawGroupOverview_TextPointer9) +
            formatted_field(row.goals_against, 3) + original_text(exe_, ::bmh::layout::CupDraw_DrawGroupOverview_TextPointer10) +
            sign + formatted_field(difference < 0 ? -difference : difference, 3) + original_text(exe_, ::bmh::layout::CupDraw_DrawGroupOverview_TextPointer11);
        text(image, 0xff, y, goals, kText, 64);
    }

    // FriendlyMatchScreen's twelve original control buttons (FormatError3).
    const uint16_t word = uint16_t(table.display_word & ~uint16_t(0x80));
    for (int index = 0; index < 12; ++index) {
        const int x = 1 + (index / 4) * 65;
        const int y = 176 + (index % 4) * 13;
        std::string label = texts_[size_t(73 + index)];
        if(family_ && family_->source_selection_mode!=0 && index>=10)
            label=texts_[size_t((::bmh::layout::LeagueTable_Glyph_TextPointer2-::bmh::layout::TextPointerTable)/4 + (index==10 ? 0 : family_->source_selection_mode))];
        if(opponent_mode_ && index>=10)
            label=texts_[size_t((::bmh::layout::LeagueTable_Glyph_TextPointer2-::bmh::layout::TextPointerTable)/4 + (index==10 ? 0 : 2))];
        if (index == 9 && table.source_controls_projection) {
            if (!table.retained_range_label_source_bytes.empty())
                label = original_to_utf8(table.retained_range_label_source_bytes);
        } else if (index == 9 && label.size() >= 9) {
            constexpr int first_round = 1; // FriendlyMatchScreen local_14 starts at zero.
            int last_round = std::min<int>(table.rounds_played & 0x7f,
                                           league_matchdays(table.league_id));
            if (last_round < first_round) last_round = first_round;
            const size_t start=kEnglishCompetitions ? 0 : 4;
            const size_t finish=kEnglishCompetitions ? 3 : 7;
            label[start] = char('0' + first_round / 10);
            label[start+1] = char('0' + first_round % 10);
            label[finish] = char('0' + last_round / 10);
            label[finish+1] = char('0' + last_round % 10);
        }
        button(image, index, x, y, 63, label,
               pressed_hit_ == index ||
               (pending_family_action_&&pending_family_action_->kind==LeagueFamilyRequestKind::control&&
                pending_family_action_->control_index==index) ||
               (word & (uint16_t(1) << index)) != 0);
    }

    // Selector/Selector2: Source's league map and league-group selectors.
    selector(image, 205, 176, 3);
    selector(image, 262, 176, pressed_hit_ == 200 ? (league_group_ ^ 1) : league_group_);
    int league = league_group_ * 5;
    for (int row = 0; row < 4; ++row) {
        const int count = exe_.u8(::bmh::layout::CalendarLeagueCounts + uint32_t(league_group_ * 4 + row));
        int x = 233 - count * 4;
        for (int cell = 0; cell < count; ++cell, ++league, x += 8) {
            const int marker_y = exe_.u8(::bmh::layout::LeaguePairings_Fill_RuntimeField) + exe_.u8(::bmh::layout::LeaguePairings_Fill_RuntimeField2) * row;
            copy_region(image, selector_atlas_, 188,
                        (selected_league_ == league || pressed_hit_ == 300 + league) ? 8 : 0,
                        x, marker_y, 8, 8, 0xc0);
        }
    }
    return image;
}

std::string LeagueTableScreen::state_json() const {
    std::ostringstream out;
    out << "{\"screen\":\"league_table\",\"selected_league\":" << selected_league_
        << ",\"league_group\":" << league_group_
        << ",\"close_requested\":" << (close_requested_ ? "true" : "false")
        << ",\"family_mode\":" << (family_ ? "true" : "false")
        << ",\"family_pending\":" << (pending_family_action_ ? "true" : "false")
        << ",\"pressed_hit\":" << pressed_hit_
        << ",\"last_rejection\":" << json_quote(rejection_)
        << ",\"state\":" << league_table_json(state()) << '}';
    return out.str();
}

void LeagueTableScreen::button(Image& out, int, int x, int y, int width,
                               std::string_view label, bool selected) const {
    int dx = x, dy = y;
    if (selected) {
        const auto bg = out.at(x, y + 10);
        fill_region(out, x, y, width + 1, 2, bg);
        fill_region(out, x, y, 2, 11, bg);
        dx += 2; dy += 2;
    } else {
        fill_region(out, x + 2, y + 10, width + 1, 2, 0x40);
        fill_region(out, x + width, y + 2, 2, 10, 0x40);
    }
    const int atlas_y = selected ? 44 : 74;
    copy_region(out, widget_atlas_, 24, atlas_y, dx, dy, 31, 10, 0xc0);
    int used = 31;
    while (width - used > 20) {
        const int chunk = std::min(8, width - used - 20);
        copy_region(out, widget_atlas_, 24, atlas_y + 10, dx + used, dy, chunk, 10, 0xc0);
        used += chunk;
    }
    const int tail = width - used;
    copy_region(out, widget_atlas_, 45 - tail, atlas_y + 20, dx + used, dy, tail, 10, 0xc0);
    const std::string utf8 = original_to_utf8(std::string(label));
    const int text_x = dx + width / 2 - sprite_font_.text_width(utf8) / 2;
    (void)sprite_font_.draw(out, text_x, dy + 7, utf8, exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + uint32_t(selected)));
}

void LeagueTableScreen::selector(Image& out, int x, int y, int group) const {
    const int height = exe_.u8(::bmh::layout::LeaguePairings_Selector_RuntimeField);
    copy_region(out, selector_atlas_, 198, 0, x, y, 55, 38, 0xc0);
    copy_region(out, selector_atlas_, 198, 76 - height, x, y + 37, 55, height - 38, 0xc0);
    if (group < 3)
        copy_region(out, selector_atlas_, exe_.u8(::bmh::layout::LeaguePairings_Selector_RuntimeField3 + uint32_t(group)), 0,
                    x + 1, y + exe_.u8(::bmh::layout::LeaguePairings_Selector_RuntimeField2), exe_.u8(::bmh::layout::LeaguePairings_Selector_RuntimeField4 + uint32_t(group)), 38, 0xc0);
}

std::optional<int> LeagueTableScreen::league_at(int x, int y) const {
    if (x < 205 || x > 260 || y < 176 || y > 216) return {};
    const auto rendered = render();
    const uint8_t pixel = rendered.pixels[size_t(y) * size_t(rendered.width) + size_t(x)];
    if (pixel == 0x19 || pixel == 0x42) return {};
    const int row_step = exe_.u8(::bmh::layout::LeaguePairings_SelectorEvent_RuntimeField3);
    const int row = row_step ? (y - exe_.u8(::bmh::layout::LeaguePairings_SelectorEvent_RuntimeField2)) / row_step : -1;
    if (row < 0 || row >= 4) return {};
    if (league_group_ == 0) {
        const int count = exe_.u8(::bmh::layout::CalendarLeagueCounts + uint32_t(row));
        const int left = 233 - count * 4;
        int column = 0;
        while (column < count && (x < left + column * 8 || x > left + column * 8 + 8)) ++column;
        if (column >= count) return {};
        const int selected = exe_.u8(::bmh::layout::CalendarFirstLeague + uint32_t(row)) + column;
        if (selected >= 0 && selected < bmh::kLeagueCount) return selected;
    } else {
        const int count = exe_.u8(::bmh::layout::LeaguePairings_SelectorEvent_RuntimeField + uint32_t(row));
        const int column = (count * 4 + x - 233) / 8;
        const int selected = exe_.u8(::bmh::layout::CareerSession_FormatError_RuntimeField4) + row + column;
        if (column >= 0 && column < count && selected >= 0 && selected < bmh::kLeagueCount)
            return selected;
    }
    return {};
}

void LeagueTableScreen::post(LeagueFamilyRequest request) {
    if (!family_ || pending_family_action_) return;
    request.identity = family_->identity;
    request.from_child = 1;
    pending_family_action_ = std::move(request);
    rejection_.clear();
}

int LeagueTableScreen::hit_at(int x, int y, bool right) const {
    if (right) return 400;
    for (int index = 0; index < 12; ++index) {
        const int bx = 1 + (index / 4) * 65;
        const int by = 176 + (index % 4) * 13;
        if (x >= bx && x <= bx + 63 && y >= by && y <= by + 10) return index;
    }
    if (x >= 262 && x <= 317 && y >= 176 && y <= 216 && state().secondary_leagues) {
        const auto rendered = render();
        const uint8_t pixel = rendered.pixels[size_t(y) * size_t(rendered.width) + size_t(x)];
        if (pixel != 0x19 && pixel != 0x42) return 200;
    }
    if (const auto target = league_at(x, y)) return 300 + *target;
    const int rows = int(state().rows.size());
    if (y > 0x1c && y < rows * 7 + 0x1c) {
        const int row = (y - 0x1d) / 7;
        if (row >= 0 && row < rows) return 100 + row;
    }
    return -1;
}

bool LeagueTableScreen::pointer_down(int x, int y, bool right) {
    if (opponent_mode_) {
        if (close_requested_ || right) return false;
        const int hit = hit_at(x,y);
        if (hit==10 || hit==11) { pressed_hit_=hit; return true; }
        if (hit>=300 && hit<300+bmh::kLeagueCount) {
            const int league=hit-300;
            if (league==selected_league_) return false;
            selected_league_=league; return true;
        }
        if (hit==200) {
            league_group_^=1;
            selected_league_=league_group_ ? int(exe_.u8(::bmh::layout::CareerSession_FormatError_RuntimeField4)) : opponent_default_league_;
            return true;
        }
        return false;
    }
    if (!family_ || pending_family_action_ || close_requested_) return false;
    const int hit = hit_at(x, y, right);
    if (hit < 0) return false;
    if (hit < 12) {
        pressed_hit_ = hit;
        return true;
    }
    // FriendlyMatchScreen's row/map branches and the family close dispatch on the
    // physical DOWN event; only FormatError3 footer widgets await release.
    return activate_hit(hit);
}

bool LeagueTableScreen::move(int x, int y) {
    if (pressed_hit_ < 0) return false;
    if (hit_at(x, y, pressed_hit_ == 400) != pressed_hit_) {
        pressed_hit_ = -1;
        return true;
    }
    return false;
}

bool LeagueTableScreen::activate_hit(int hit) {
    if (hit == 400) {
        post({{}, LeagueFamilyRequestKind::close, 1, 0});
        return true;
    }
    if (hit >= 0 && hit < 12) {
        if ((hit == 10 || hit == 11) && family_->source_selection_mode!=0)
            post({{}, LeagueFamilyRequestKind::close, 1, 0});
        else if (hit == 10 || hit == 11)
            post({{}, LeagueFamilyRequestKind::navigate, 1, uint8_t(hit - 8)});
        else {
            LeagueFamilyRequest request;
            request.kind = LeagueFamilyRequestKind::control;
            request.control_index = hit;
            post(std::move(request));
        }
        return true;
    }
    if (hit == 200) {
        const int target_group = league_group_ ^ 1;
        const int target = target_group == 1 ? int(exe_.u8(::bmh::layout::CareerSession_FormatError_RuntimeField4))
            : family_->identity.manager_default_league;
        if (target >= 0 && target < bmh::kLeagueCount) {
            LeagueFamilyRequest request;
            request.kind = LeagueFamilyRequestKind::toggle_league_group;
            request.league_id = target; // Expected target; Session verifies against the old owner.
            post(std::move(request));
        }
        return true;
    }
    if (hit >= 300 && hit < 300 + bmh::kLeagueCount) {
        LeagueFamilyRequest request;
        request.kind = LeagueFamilyRequestKind::select_league;
        request.league_id = hit - 300;
        post(std::move(request));
        return true;
    }
    if (hit >= 100 && hit < 100 + int(state().rows.size())) {
        const auto& selected = state().rows[size_t(hit - 100)];
        LeagueFamilyRequest request;
        request.kind = family_->source_selection_mode==2
            ? LeagueFamilyRequestKind::select_opponent : LeagueFamilyRequestKind::team_info;
        request.table_row_index = hit - 100;
        request.runtime_team = selected.runtime_team_id;
        request.expected_club_identity = selected.club_identity_key;
        post(std::move(request));
        return true;
    }
    return false;
}

void LeagueTableScreen::reject_family_action(std::string reason) {
    if (!family_ || !pending_family_action_ || reason.empty())
        throw FormatError("league table: no pending family rejection");
    pending_family_action_.reset();
    pressed_hit_ = -1;
    rejection_ = std::move(reason);
}

void LeagueTableScreen::acknowledge_same_league_selector(const LeagueFamilyRequest& old_request,
    const StrengthsIdentity& accepted) {
    if (!family_ || !pending_family_action_ ||
        (pending_family_action_->kind != LeagueFamilyRequestKind::select_league &&
         pending_family_action_->kind != LeagueFamilyRequestKind::toggle_league_group) ||
        pending_family_action_->identity != family_->identity || *pending_family_action_ != old_request ||
        old_request.from_child != 1 || old_request.target_child != 0 || old_request.runtime_team != -1 ||
        old_request.expected_club_identity != -1 || old_request.table_row_index != -1 ||
        old_request.control_index != -1 || old_request.table_dialog_reply ||
        accepted.manager != family_->identity.manager ||
        accepted.runtime_club != family_->identity.runtime_club ||
        accepted.club_identity != family_->identity.club_identity ||
        accepted.day_counter != family_->identity.day_counter ||
        accepted.family_id != family_->identity.family_id ||
        accepted.manager_default_league != family_->identity.manager_default_league ||
        accepted.source_param2 != family_->identity.source_param2 ||
        accepted.expected_settings_word != family_->identity.expected_settings_word ||
        accepted.scope != family_->identity.scope || accepted.sequence != family_->identity.sequence ||
        accepted.revision != family_->identity.revision || family_->identity.family_sequence == UINT64_MAX ||
        accepted.league != selected_league_ || accepted.family_sequence != family_->identity.family_sequence + 1 ||
        accepted.league_group > 1)
        throw FormatError("league table: stale same-league selector acknowledgement");
    const auto kind = pending_family_action_->kind;
    const uint8_t expected_group = kind == LeagueFamilyRequestKind::toggle_league_group
        ? uint8_t(family_->identity.league_group ^ 1u) : family_->identity.league_group;
    if (accepted.league_group != expected_group || pending_family_action_->league_id != accepted.league)
        throw FormatError("league table: selector acknowledgement does not match operation");
    family_->identity = accepted;
    league_group_ = accepted.league_group;
    pending_family_action_.reset();
    pressed_hit_ = -1;
    rejection_.clear();
}

void LeagueTableScreen::acknowledge_friendly_own_club(const StrengthsIdentity& accepted) {
    if(!family_||family_->source_selection_mode!=2||!pending_family_action_||
       pending_family_action_->kind!=LeagueFamilyRequestKind::select_opponent||
       pending_family_action_->identity!=family_->identity||family_->identity.family_sequence==UINT64_MAX)
        throw FormatError("friendly table: no owned own-club acknowledgement");
    auto expected=family_->identity;++expected.family_sequence;
    expected.expected_family_word=uint16_t((expected.expected_family_word&0x0f7f)|
        (state().source_normal_mode?0x80:0));
    if(accepted!=expected)throw FormatError("friendly table: own-club acknowledgement changed identity");
    family_->identity=accepted;pending_family_action_.reset();pressed_hit_=-1;rejection_.clear();
}

bool LeagueTableScreen::click(int x, int y, bool right) {
    if (close_requested_) return false;
    if (opponent_mode_) {
        const int hit=pressed_hit_;
        pressed_hit_=-1;
        if ((hit==10 || hit==11) && !right && hit_at(x,y)==hit) {
            close_requested_=true;
            return true;
        }
        return hit>=0; // Redraw a released/cancelled held footer too.
    }
    if (family_) {
        if (pending_family_action_ || pressed_hit_ < 0) return false;
        const int hit = pressed_hit_;
        pressed_hit_ = -1;
        if (hit_at(x, y, right) != hit) return false;
        return activate_hit(hit);
    }
    (void)x; (void)y; (void)right;
    return false;
}

bool LeagueTableScreen::key(std::string_view value) {
    if (family_) {
        if (pending_family_action_) return false;
        // The Original table dispatch's key-to-close mapping is not established;
        // family close is requested only by a physical Source input path.
        return false;
    }
    if (close_requested_) return false;
    if (value == "ESC") { close_requested_ = true; return true; }
    if (value == "LEFT") {
        selected_league_ = (selected_league_ + bmh::kLeagueCount - 1) % bmh::kLeagueCount;
        if (opponent_mode_) league_group_=selected_league_>=5;
        return true;
    }
    if (value == "RIGHT") {
        selected_league_ = (selected_league_ + 1) % bmh::kLeagueCount;
        if (opponent_mode_) league_group_=selected_league_>=5;
        return true;
    }
    return false;
}

} // namespace bmh::ui
