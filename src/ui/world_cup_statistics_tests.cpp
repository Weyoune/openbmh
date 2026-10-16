// SPDX-License-Identifier: GPL-3.0-only
#include "world_cup_statistics.hpp"

#include <filesystem>
#include <stdexcept>
#include <utility>

namespace {
void check(bool condition) {
    if (!condition) throw std::runtime_error("World Cup statistics focused check failed");
}
}

int main(int argc, char** argv) {
    using namespace bmh::ui;
    std::array<uint8_t, 72> editions{};
    // Bit 7 gates an edition result; the low seven bits identify the nation.
    editions[0] = 0x80 | 7;       // first place
    editions[1] = 0x80 | 7;       // second place in another slot
    editions[2] = 0x80 | 3;       // third place
    editions[3] = 0x80 | 0;       // fourth place, Germany
    editions[4] = 7;               // bit 7 clear: ignored
    editions[5] = 0x80 | 3;
    check(world_cup_medal_key(editions, 7) == 0x01010000u);
    check(world_cup_medal_key(editions, 3) == 0x00010100u);
    check(world_cup_medal_key(editions, 0) == 0x00000001u);
    std::array<WorldCupNation, 62> ranking{};
    ranking[0] = {0, "zero", 0x00000001u};
    ranking[1] = {1, "one", 0x01000000u};
    ranking[2] = {2, "two", 0x00010000u};
    order_world_cup_nations(ranking);
    check(ranking[0].id == 1 && ranking[0].medals == 0x01000000u);
    check(ranking[1].id == 2 && ranking[1].medals == 0x00010000u);
    check(ranking[2].id == 0 && ranking[2].medals == 1u);

    WorldCupStatisticsProjection desk, picker;
    desk.can_select_opponent = false;
    picker.can_select_opponent = true;
    const auto germany = world_cup_selection_request(picker, uint8_t{0});
    check(germany.kind == WorldCupStatisticsRequestKind::none); // visible and hoverable, never committed
    check(world_cup_selection_request(desk, uint8_t{7}).kind == WorldCupStatisticsRequestKind::none);
    const auto opponent = world_cup_selection_request(picker, uint8_t{7});
    check(opponent.kind == WorldCupStatisticsRequestKind::select_opponent);
    check(opponent.runtime_team == 0x16c);
    check(world_cup_selection_request(picker, {}).kind == WorldCupStatisticsRequestKind::none);

    picker.flag_cells[0] = {35, 34};
    picker.editions[0] = 0x80 | 0;
    check(world_cup_nation_at(picker, 45, 41) == 0); // bank edge is inclusive
    check(world_cup_nation_at(picker, 52, 46) == 0); // edition cell is inclusive
    check(!world_cup_nation_at(picker, 267, 46));   // outside the 18-edition grid

    if (argc > 1) {
        WorldCupStatisticsProjection smoke;
        smoke.flag_cells[0] = {35, 34};
        smoke.country_names[0] = "Deutschland";
        smoke.ranking[0] = {0, "Deutschland", 0x01000000};
        bmh::Image underlay(320, 240);
        WorldCupStatisticsScreen screen(std::filesystem::path(argv[1]), std::move(smoke),
                                        std::move(underlay));
        check(screen.render().width == 320);
        check(screen.move(40, 37));
        check(screen.render().height == 240);
    }
}
