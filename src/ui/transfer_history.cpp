// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/transfer_history.hpp"

#include <algorithm>
#include <limits>
#include <utility>

#include "game/messages.hpp"
#include "ui/desk_state.hpp"
#include "ui/manager.hpp"
#include "ui/setup.hpp"

namespace bmh::ui {
namespace {
constexpr size_t kManagerStride = 0xb0c;
constexpr size_t kHistoryCount = 0x21d;
constexpr size_t kEventWords = 0x760;
constexpr size_t kNameWords = 0x7d8;
constexpr size_t kRecordCapacity = 30;
constexpr size_t kSourceCountLimit = 25; // PoolString stops incrementing at 0x19.
constexpr size_t kTypeTextBase = 23;     // TEXTD labels at TransferHistoryRuntimeField, four words.
constexpr size_t kEmptyText = 912;       // TransferHistoryRuntimeField2.
constexpr uint8_t kSourceCloseEvent = 8;

std::string pool_string(const std::vector<uint8_t>& pool, size_t offset, const char* what) {
    if (offset >= pool.size()) throw FormatError(std::string("transfer history: invalid ") + what + " pool offset");
    const auto end = std::find(pool.begin() + std::ptrdiff_t(offset), pool.end(), uint8_t(0));
    if (end == pool.end()) throw FormatError(std::string("transfer history: unterminated ") + what + " pool string");
    return std::string(pool.begin() + std::ptrdiff_t(offset), end);
}

uint32_t read_u32(const std::vector<uint8_t>& bytes, size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4)
        throw FormatError("transfer history: truncated record word");
    return uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1]) << 8) |
        (uint32_t(bytes[offset + 2]) << 16) | (uint32_t(bytes[offset + 3]) << 24);
}

std::string replace_source_padding(std::string value) {
    // DeskDate2 rewrites every formatter '%' placeholder to a leading zero.
    std::replace(value.begin(), value.end(), '%', '0');
    return value;
}
} // namespace

TransferHistoryProjection transfer_history_from_game(const GameState& game, int manager_slot) {
    if (manager_slot < 0 || manager_slot >= 4)
        throw FormatError("transfer history: manager slot outside Original four records");
    const size_t base = size_t(manager_slot) * kManagerStride;
    if (base > game.manager_records.size() ||
        kHistoryCount >= game.manager_records.size() - base ||
        kEventWords + kRecordCapacity * 4 > game.manager_records.size() - base ||
        kNameWords + kRecordCapacity * 4 > game.manager_records.size() - base)
        throw FormatError("transfer history: truncated manager-owned transfer-history blocks");

    const uint8_t count = game.manager_records[base + kHistoryCount];
    if (count > kSourceCountLimit)
        throw FormatError("transfer history: count exceeds PoolString's 25-entry limit");

    TransferHistoryProjection result;
    result.manager_slot = manager_slot;
    result.rows.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        const uint32_t event = read_u32(game.manager_records, base + kEventWords + index * 4);
        const uint32_t name_and_club = read_u32(game.manager_records, base + kNameWords + index * 4);
        TransferHistoryRow row;
        row.source_date_counter = uint16_t((event >> 2) & 0xffff);
        row.source_kind = uint8_t(event & 3);
        row.source_fee = ((event >> 19) * 10000u);
        row.saved_player_name_offset = uint16_t(name_and_club & 0xffff);
        row.counterparty_club_offset = uint16_t(name_and_club >> 16);
        row.saved_player_name = pool_string(game.player_names, row.saved_player_name_offset, "saved player-name");
        row.special_counterparty = row.counterparty_club_offset == 0xffff;
        if (!row.special_counterparty)
            row.counterparty_club_name = pool_string(game.club_names, row.counterparty_club_offset, "counterparty club-name");
        result.rows.push_back(std::move(row));
    }
    return result;
}

TransferHistoryScreen::TransferHistoryScreen(const std::filesystem::path& data_directory,
                                             const GameState& formatting_state,
                                             TransferHistoryProjection projection)
    : exe_(load_le(asset_path(data_directory, "BMH.exe"))),
      texts_(load_textd(asset_path(data_directory, "TEXTD.BLK"))),
      font_(load_font(asset_path(data_directory / "F", "BMH3.FNT"))),
      formatting_state_(formatting_state), projection_(std::move(projection)) {
    if (projection_.manager_slot < 0 || projection_.manager_slot >= 4 ||
        projection_.rows.size() > kSourceCountLimit || texts_.strings.size() <= kEmptyText)
        throw FormatError("transfer history: incomplete Original row projection or TEXTD resources");
    for (size_t i = 0; i < 4; ++i)
        if (texts_[kTypeTextBase + i].empty())
            throw FormatError("transfer history: missing Original TEXTD operation label");
}

void TransferHistoryScreen::draw_text(Image& image, int x, int baseline,
                                      std::string_view value, uint8_t color) const {
    const int top = baseline - font_.baseline;
    if (x >= 0 && top >= 0 && top + font_.height <= image.height &&
        x + font_.text_width(value) <= image.width)
        font_.draw(image, x, top, value, color);
}

std::string TransferHistoryScreen::date_text(uint16_t source_counter) const {
    const DeskDate date = desk_date(source_counter, exe_);
    std::string out = format_number(formatting_state_, date.day, 2) + exe_.cstring(::bmh::layout::OtherGamesState_ResultText_ExecutableField) +
        format_number(formatting_state_, date.month, 2) + exe_.cstring(::bmh::layout::OtherGamesState_ResultText_ExecutableField) +
        format_number(formatting_state_, date.year, 0x44) + exe_.cstring(::bmh::layout::SeriesRecords_Render_ExecutableField2);
    return replace_source_padding(std::move(out));
}

Image TransferHistoryScreen::render(const Image& accepted_underlay, int source_origin_y) const {
    if (accepted_underlay.width != 320 || accepted_underlay.height != 240)
        throw FormatError("transfer history: expected 320x240 accepted statistics-page underlay");
    if (source_origin_y < 0 || source_origin_y > 255)
        throw FormatError("transfer history: source origin outside byte-sized parent value");
    Image out = accepted_underlay;
    if (projection_.rows.empty()) {
        const std::string empty = texts_[kEmptyText];
        draw_text(out, std::max(0, (320 - font_.text_width(empty)) / 2),
                  source_origin_y + 19, empty, 0x6b);
        return out;
    }

    for (size_t i = 0; i < projection_.rows.size(); ++i) {
        const auto& row = projection_.rows[i];
        const int baseline = source_origin_y + 19 + int(i) * 6;
        const uint8_t color = exe_.u8(::bmh::layout::TransferHistory_FormatError_RuntimeField + row.source_kind);
        const std::string date_and_player = date_text(row.source_date_counter) +
            exe_.cstring(::bmh::layout::AlltimeTable_Render_ExecutableField3) + row.saved_player_name;
        draw_text(out, 5, baseline, date_and_player, color);
        draw_text(out, 0x71, baseline, texts_[kTypeTextBase + row.source_kind], color);
        if (row.special_counterparty)
            draw_text(out, 0xa6, baseline, exe_.cstring(::bmh::layout::TransferHistory_FormatError_TextPointer), color);
        else
            draw_text(out, 0xa6, baseline, row.counterparty_club_name, color);
        draw_text(out, 0x103, baseline, format_number(formatting_state_, int32_t(row.source_fee), 0x107), color);
    }
    return out;
}

bool TransferHistoryScreen::handle_source_event(uint8_t event_code) {
    if (closed_ || event_code != kSourceCloseEvent) return false;
    closed_ = true;
    return true;
}

} // namespace bmh::ui
