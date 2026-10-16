// SPDX-License-Identifier: GPL-3.0-only
#include "ui/transfer_history.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void write_u32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes.at(at + size_t(i)) = uint8_t(value >> (i * 8));
}
}

int main() {
    using namespace bmh;
    using namespace bmh::ui;

    GameState game;
    game.manager_records.assign(4 * 0xb0c, 0);
    game.player_names = {'A','n','n','a',0,'M','a','x',0};
    game.club_names = {'O','l','d',' ','F','C',0,'N','e','w',' ','F','C',0};
    constexpr size_t record = 0xb0c;
    game.manager_records[record + 0x21d] = 2;
    const uint32_t sell_event = (12u << 19) | (400u << 2) | 0u;
    const uint32_t buy_event = (3u << 19) | (401u << 2) | 1u;
    write_u32(game.manager_records, record + 0x760, sell_event);
    write_u32(game.manager_records, record + 0x7d8, (7u << 16) | 0u);
    write_u32(game.manager_records, record + 0x764, buy_event);
    write_u32(game.manager_records, record + 0x7dc, (0xffffu << 16) | 5u);

    const auto projection = transfer_history_from_game(game, 1);
    require(projection.manager_slot == 1 && projection.rows.size() == 2,
            "count and manager owner are read from the active record");
    const auto& sold = projection.rows[0];
    require(sold.source_date_counter == 400 && sold.source_kind == 0 && sold.source_fee == 120000,
            "first row's packed date/type/fee fields follow Source bit positions");
    require(sold.saved_player_name_offset == 0 && sold.saved_player_name == "Anna" &&
            sold.counterparty_club_offset == 7 && sold.counterparty_club_name == "New FC" &&
            !sold.special_counterparty,
            "history uses the saved name-pool and stable club-pool offsets");
    const auto& bought = projection.rows[1];
    require(bought.source_date_counter == 401 && bought.source_kind == 1 &&
            bought.source_fee == 30000 && bought.saved_player_name_offset == 5 &&
            bought.saved_player_name == "Max" && bought.special_counterparty,
            "buyer row and 0xffff special counterparty remain source-decoded");

    bool rejected = false;
    try { (void)transfer_history_from_game(game, 4); }
    catch (const FormatError&) { rejected = true; }
    require(rejected, "manager outside Source slots is rejected");

    game.manager_records[record + 0x21d] = 26;
    rejected = false;
    try { (void)transfer_history_from_game(game, 1); }
    catch (const FormatError&) { rejected = true; }
    require(rejected, "impossible history count above Source limit is rejected");

    std::cout << "Transfer-history projection: Source fields, stable pools, and bounds passed\n";
    return 0;
}
