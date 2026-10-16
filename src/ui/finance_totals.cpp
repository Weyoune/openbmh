// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "finance_totals.hpp"

#include <cstdint>
#include <utility>

#include "ui/advertising.hpp"
#include "ui/manager.hpp"
#include "game/messages.hpp"

namespace bmh::ui {
namespace {
constexpr size_t kManagerRecord = 0xb0c;

uint32_t require_memory(const GameState& g, uint32_t address, size_t bytes) {
    if (bytes == 0 || !g.memory.has(address) || !g.memory.has(address + uint32_t(bytes - 1)))
        throw FormatError("statistics finance: save lacks Source asset memory");
    return address;
}
uint16_t read16(const std::vector<uint8_t>& b, size_t at) {
    if (at + 1 >= b.size()) throw FormatError("statistics finance: truncated stock block");
    return uint16_t(b[at]) | uint16_t(b[at + 1]) << 8;
}
uint32_t read32(const std::vector<uint8_t>& b, size_t at) {
    if (at + 3 >= b.size()) throw FormatError("statistics finance: truncated stock block");
    return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 |
           uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
}
int32_t original_mask_1e(const GameState& g, int manager) {
    const size_t rec = size_t(manager) * kManagerRecord;
    if (rec + 0x31b >= g.manager_records.size())
        throw FormatError("statistics finance: manager cash record is truncated");
    const uint32_t values = require_memory(g, ::bmh::layout::FundBlock, 6 * 0x1c);
    const uint32_t quantities = require_memory(g, ::bmh::layout::Advertising_OriginalShareAdjustment_RuntimeField,
        uint32_t(manager) * 2 + 5 * 0x1c + 2);
    const uint32_t loans = require_memory(g, ::bmh::layout::Analytics_SourceAssetMaskE_RuntimeField,
        uint32_t(manager) * 0x78 + 4 * 0x18 + 4);
    if (g.stocks.size() < 12 * 0x5c)
        throw FormatError("statistics finance: stock table is truncated");

    uint32_t total = read32(g.manager_records, rec + 0x318); // HighscoreScore bit 1, Source +b6f30.
    for (uint32_t i = 0; i < 6; ++i) {
        const uint32_t quantity = g.memory.u16(quantities + i * 0x1c + uint32_t(manager) * 2);
        const uint32_t value = g.memory.u32(values + i * 0x1c);
        total += quantity * value; // bit 2: u16 quantity times u32 unit value, wrapping u32.
    }
    for (uint32_t i = 0; i < 12; ++i) {
        const size_t at = size_t(i) * 0x5c;
        const uint32_t quantity = read16(g.stocks, at + 0x50 + size_t(manager) * 2);
        const uint32_t price = read16(g.stocks, at + 0x4e);
        total += quantity * price; // bit 3, Source stock value.
    }
    for (uint32_t i = 0; i < 5; ++i)
        total -= g.memory.u32(loans + uint32_t(manager) * 0x78 + i * 0x18); // bit 4, debt subtracts.
    return int32_t(total);
}
}

FinanceTotals finance_totals_from_game(const GameState& g, int manager) {
    if (manager < 0 || manager > 3 || size_t(manager + 1) * kManagerRecord > g.manager_records.size())
        throw FormatError("statistics finance: invalid manager record");
    if (!g.memory.has(::bmh::layout::ActiveManager) || g.memory.u8(::bmh::layout::ActiveManager) != manager)
        throw FormatError("statistics finance: manager is not the active Original manager");
    const size_t rec = size_t(manager) * kManagerRecord;
    FinanceTotals out;
    out.manager_slot = manager;
    const auto rd32 = [&](size_t offset) {
        const size_t at = rec + offset;
        if (at + 3 >= g.manager_records.size())
            throw FormatError("statistics finance: manager finance field is truncated");
        return int32_t(read32(g.manager_records, at));
    };
    // FinanceTotalsFromGame's source globals relative to manager record base *ManagerRecords.
    out.rows[0] = rd32(0x314); // MonthRuntimeField9
    out.rows[1] = original_mask_1e(g, manager);
    out.rows[2] = int32_t(uint32_t(out.rows[0]) -
                          (uint32_t(rd32(0x324)) + uint32_t(rd32(0x30c)))); // b6f3c + b6f24
    out.rows[3] = rd32(0x320); // b6f38
    out.rows[4] = 0;            // Source group heading only.
    out.rows[5] = rd32(0x338); // b6f50
    out.rows[6] = rd32(0x334); // b6f4c
    out.rows[7] = rd32(0x33c); // b6f54

    // Reuse the established read-only implementation of FinanceTotalsFromGame3/OriginalFinancePreview through
    // its public advertising projection. It uses the same exact Original preview path.
    const AdvertisingFinancePreview monthly = original_finance_preview(g, manager);
    out.rows[8] = monthly.income;
    out.rows[9] = monthly.expenses;
    out.rows[10] = int32_t(uint32_t(out.rows[8]) - uint32_t(out.rows[9]));
    for (size_t i = 0; i < out.rows.size(); ++i)
        if (i != 4) out.amounts[i] = format_number(g, out.rows[i], 0x108);
    return out;
}

FinanceTotalsScreen::FinanceTotalsScreen(const std::filesystem::path& data,
                                         FinanceTotals state, Image underlay)
    : state_(std::move(state)), underlay_(std::move(underlay)),
      texts_(load_textd(asset_path(data, "TEXTD.BLK"))),
      font_(load_font(asset_path(data / "F", "BMH3.FNT"))) {
    if (state_.manager_slot < 0 || state_.manager_slot > 3 ||
        underlay_.width != 320 || underlay_.height != 240)
        throw FormatError("statistics finance: incomplete Source projection");
}

Image FinanceTotalsScreen::render() const {
    Image out = underlay_;
    for (size_t i = 0; i < state_.rows.size(); ++i) {
        const std::string& label = texts_[27 + i]; // FinanceTotalsRuntimeField: TEXTD entries 27..37.
        const int baseline = 79 + int(i) * 8;      // FinanceTotalsFromGame, 0x4f + 8 per row.
        const int label_x = (i >= 4 && i <= 6) ? 80 : 70;
        font_.draw(out, label_x, baseline - font_.baseline, label, 0x6b); // Source literal, not EXE color.
        if (i != 4) font_.draw(out, 190, baseline - font_.baseline, state_.amounts[i], 0x6b);
    }
    return out;
}

} // namespace bmh::ui
