// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/reality_motion.hpp"

#include <algorithm>
#include <cstdlib>
#include <numeric>

namespace bmh {

int reality_direction(int x, int y) {
    if (x < -42 || x > 42 || y < -24 || y > 24)
        throw FormatError("Reality: direction outside the Source grid");
    int ax = std::abs(x), ay = std::abs(y);
    if (!(ay > 1 && ax > 1 && ax != ay)) {
        if (ay < ax) ay *= 2;
        else ax *= 2;
    }
    int direction = y == 0 ? 3 : std::abs(ax / ay);
    if (direction > 2) direction = 3;
    if (direction > 1) --direction;
    if (y > 0 && direction == 0) direction = 4;
    if (x < 0 && direction > 0 && direction != 4) direction += 4;
    if (y > 0 && (direction == 1 || direction == 5)) direction += 2;
    return direction;
}

std::pair<int, int> reality_project(const LeImage& exe, int x, int y) {
    if (x < 0 || x > 42 || y < 0 || y > 24)
        throw FormatError("Reality: position outside the Source grid");
    // Int7: signed division truncates towards zero, including the horizon.
    const int distance = (int(int16_t(exe.u16(::bmh::layout::RealityMotion_FormatError_RuntimeField4Unsigned + uint32_t(y * 2)))) - 2) * std::abs(21 - x) / 21;
    return {153 + (x > 21 ? distance : -distance), exe.u8(::bmh::layout::RealityMotion_FormatError_RuntimeField5Unsigned + uint32_t(y))};
}

RealityMotionResult reality_motion(const RealityMotionInput& input, const LeImage& exe) {
    RealityMotionResult result;
    auto figures = input.figures;
    // RealityFieldScreen copies the ground trajectory (22) to the elevated ball (23).
    // Only its animation phase remains independent.
    const auto ball_phase = figures[23].phase;
    figures[23] = figures[22]; figures[23].phase = ball_phase;
    std::array<int, 24> x{}, y{}, dx{}, dy{}, next_x{}, next_y{}, pose{}, phase{}, order{};
    std::iota(order.begin(), order.end(), 0);
    for (int i = 0; i < 24; ++i) {
        const auto& figure = figures[size_t(i)];
        // Source iterates all slots, but absent figures are not projected/drawn.
        if (!figure.active) { result.phases_after[size_t(i)] = figure.phase; continue; }
        const auto old = reality_project(exe, figure.old_x, figure.old_y);
        const auto next = reality_project(exe, figure.next_x, figure.next_y);
        x[size_t(i)] = old.first * 128; y[size_t(i)] = old.second * 128;
        next_x[size_t(i)] = next.first; next_y[size_t(i)] = next.second;
        dx[size_t(i)] = (next.first - old.first) * 128 / 10;
        dy[size_t(i)] = (next.second - old.second) * 128 / 10;
        pose[size_t(i)] = i < 22 ? exe.u8(::bmh::layout::RealityMotion_RealityMotion_RuntimeFieldUnsigned + uint32_t(reality_direction(
            figure.next_x - figure.old_x, figure.next_y - figure.old_y) + (i > 10 ? 8 : 0))) : i - 14;
        phase[size_t(i)] = figure.phase;
        const int poses = exe.u8(::bmh::layout::RealityMotion_FormatError_RuntimeField7Unsigned + uint32_t(pose[size_t(i)]));
        if (!poses || phase[size_t(i)] >= poses)
            throw FormatError("Reality: animation phase outside the Source pose");
    }
    for (int tick = 0; tick < 11; ++tick) {
        // Source's insertion sort uses final depth; equal depths keep slot order.
        for (int group = 0; group < 8; ++group) {
            const int start = exe.u8(::bmh::layout::RealityMotion_FormatError_RuntimeFieldUnsigned + uint32_t(group));
            const int end = exe.u8(::bmh::layout::RealityMotion_FormatError_RuntimeField2Unsigned + uint32_t(group));
            if (start > end || end > 24) throw FormatError("Reality: invalid Source depth-sort interval");
            for (int index = start; index < end; ++index) {
                int at = index;
                while (at > 0 && next_y[size_t(order[size_t(at)])] < next_y[size_t(order[size_t(at - 1)])]) {
                    std::swap(order[size_t(at)], order[size_t(at - 1)]); --at;
                }
            }
        }
        auto& frame = result.frames[size_t(tick)];
        for (int i : order) {
            if (!figures[size_t(i)].active) continue;
            const bool player = i < 22, away = i > 10;
            int px = tick < 9 ? x[size_t(i)] / 128 : next_x[size_t(i)];
            int py = tick < 9 ? y[size_t(i)] / 128 : next_y[size_t(i)];
            int code = pose[size_t(i)], animation = phase[size_t(i)];
            if (player) px += away ? 1 : -1;
            if (!dx[size_t(i)] && !dy[size_t(i)]) {
                animation = 0;
                if (player) code = away ? 11 : 10;
            } else if (i == 23 && input.ball_airborne) py -= exe.u8(::bmh::layout::RealityMotion_FormatError_RuntimeField3Unsigned + uint32_t(tick));
            const int count = exe.u8(::bmh::layout::RealityMotion_FormatError_RuntimeField7Unsigned + uint32_t(code));
            const int cell = exe.u8(::bmh::layout::RealityMotion_FormatError_RuntimeField6Unsigned + uint32_t(code)) + animation;
            if (!count || cell < 0 || cell > 30) throw FormatError("Reality: invalid Source sprite cell");
            frame.sprites[frame.count++] = {i, px, py, cell,
                input.numbers && player ? i % 11 + 1 : 0, player && away};
            if (tick < 9) phase[size_t(i)] = (animation + 1) % count;
            x[size_t(i)] += dx[size_t(i)]; y[size_t(i)] += dy[size_t(i)];
        }
    }
    for (int i = 0; i < 24; ++i) if (figures[size_t(i)].active)
        result.phases_after[size_t(i)] = uint8_t(phase[size_t(i)]);
    return result;
}

RealityPath reality_path(int x, int y, int target_x, int target_y) {
    const auto valid = [](int xx, int yy) { return xx >= 0 && xx <= 42 && yy >= 0 && yy <= 24; };
    if (!valid(x,y) || !valid(target_x,target_y)) throw FormatError("Reality: invalid path endpoint");
    RealityPath path;
    const int distance_x = std::abs(target_x-x), distance_y = std::abs(target_y-y);
    const int length = std::max(distance_x,distance_y);
    const int step_x = length ? distance_x*100/length : 0;
    const int step_y = length ? distance_y*100/length : 0;
    const int direction_x = target_x < x ? -1 : 1, direction_y = target_y < y ? -1 : 1;
    int error_x = 0, error_y = 0;
    while (x != target_x || y != target_y) {
        path.points[path.count++] = {x,y};
        // Preserve Source's finite array guard and duplicate terminal write.
        if (path.count > 49) break;
        if (x != target_x && (error_x += step_x) >= 100-step_x) { error_x -= 100;x += direction_x; }
        if (y != target_y && (error_y += step_y) >= 100-step_y) { error_y -= 100;y += direction_y; }
    }
    path.points[path.count++] = {x,y};
    return path;
}

RealityPath reality_goal_path(int x, int y, int goal_x) {
    return reality_path(x,y,goal_x,y < 8 ? 11 : y > 16 ? 13 : 12);
}

bool RealityGrid::place(int figure, int x, int y, int side) {
    if (figure < 0 || figure >= 24 || side < 0 || side > 2)
        throw FormatError("Reality: invalid figure/occupancy side");
    if (x < 0 || x > 42 || y < 0 || y > 24) return false;
    if (side < 2) {
        auto& cell = occupied_[size_t(side)][size_t(x*26+y)];
        if (cell) return false;
        cell = uint8_t(figure+1);
    } else {
        previous_ball_ = ball_; ball_ = {x,y};
    }
    positions_[size_t(figure)] = {x,y};
    return true;
}

uint8_t RealityGrid::occupant(int x, int y, int side) const {
    if (x < 0 || x > 42 || y < 0 || y > 24 || side < 0 || side > 1)
        throw FormatError("Reality: invalid occupancy lookup");
    return occupied_[size_t(side)][size_t(x*26+y)];
}

void RealityGrid::clear(int x, int y, int side) {
    if (x < 0 || x > 42 || y < 0 || y > 24 || side < 0 || side > 1)
        throw FormatError("Reality: invalid occupancy clear");
    occupied_[size_t(side)][size_t(x*26+y)] = 0;
}

void RealityGrid::reset_occupancy() {
    for (auto& side : occupied_) side.fill(0);
}

void RealityGrid::stage_position(int figure, int x, int y) {
    if (figure < 0 || figure >= 24 || x < 0 || x > 42 || y < 0 || y > 24)
        throw FormatError("Reality: invalid staged position");
    positions_[size_t(figure)] = {x,y};
}

std::pair<int,int> RealityGrid::position(int figure) const {
    if (figure < 0 || figure >= 24) throw FormatError("Reality: invalid figure lookup");
    return positions_[size_t(figure)];
}

} // namespace bmh

namespace bmh {
cli::Json RealityGrid::checkpoint() const {
    cli::Json::Array planes, positions;
    for (const auto &plane : occupied_) {
        cli::Json::Array cells;
        for (auto b : plane)
            cells.emplace_back(int(b));
        planes.emplace_back(std::move(cells));
    }
    for (const auto &[x, y] : positions_)
        positions.emplace_back(cli::Json::Array{x, y});
    return cli::Json::Object{
        {"planes", std::move(planes)},
        {"positions", std::move(positions)},
        {"ball", cli::Json::Array{ball_.first, ball_.second}},
        {"previous_ball", cli::Json::Array{previous_ball_.first, previous_ball_.second}}};
}
void RealityGrid::restore_checkpoint(const cli::Json &value) {
    const auto field = [&](const char *name) -> const cli::Json & {
        if (!value.object() || !value.object()->contains(name))
            throw FormatError("native Reality grid field missing");
        return value.object()->at(name);
    };
    const auto array = [](const cli::Json &j, size_t size) -> const cli::Json::Array & {
        if (!j.array() || j.array()->size() != size)
            throw FormatError("invalid native Reality grid dimensions");
        return *j.array();
    };
    const auto integer = [](const cli::Json &j) -> int {
        if (!j.integer() || *j.integer() < INT32_MIN || *j.integer() > INT32_MAX)
            throw FormatError("invalid native Reality coordinate");
        return int(*j.integer());
    };
    RealityGrid restored;
    const auto &planes = array(field("planes"), 2);
    for (size_t p = 0; p < 2; ++p) {
        const auto &cells = array(planes[p], 0x478);
        for (size_t i = 0; i < cells.size(); ++i) {
            const int n = integer(cells[i]);
            if (n < 0 || n > 255)
                throw FormatError("invalid native Reality cell");
            restored.occupied_[p][i] = uint8_t(n);
        }
    }
    const auto pair = [&](const cli::Json &j) {
        const auto &xy = array(j, 2);
        return std::pair<int, int>{integer(xy[0]), integer(xy[1])};
    };
    const auto &positions = array(field("positions"), 24);
    for (size_t i = 0; i < 24; ++i)
        restored.positions_[i] = pair(positions[i]);
    restored.ball_ = pair(field("ball"));
    restored.previous_ball_ = pair(field("previous_ball"));
    *this = std::move(restored);
}
} // namespace bmh
