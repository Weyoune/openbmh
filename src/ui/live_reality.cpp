// SPDX-License-Identifier: GPL-3.0-only
#include "ui/live_reality.hpp"

#include "ui/setup.hpp"

namespace bmh::ui {
namespace {
void blit(Image& target, const Image& source, int sx, int sy, int x, int y, int width, int height, bool away) {
    for (int row = 0; row < height; ++row) for (int column = 0; column < width; ++column) {
        const int xx = sx + column, yy = sy + row, dx = x + column, dy = y + row;
        if (xx < 0 || xx >= source.width || yy < 0 || yy >= source.height ||
            dx < 0 || dx >= 320 || dy < 0 || dy >= 240) continue;
        uint8_t pixel = source.pixels[size_t(yy) * size_t(source.width) + size_t(xx)];
        // KB50 figures and KB13's small number strip both use index zero.
        if (pixel == 0) continue;
        // PathTreatsActive sets bdb8e for away players only (not ball, shadow or numbers).
        if (away && pixel < 16) pixel += 16;
        target.at(dx, dy) = pixel;
    }
}
} // namespace

RealityFieldScreen::RealityFieldScreen(const std::filesystem::path& data, uint8_t ball_style)
    : atlas_(load_vga(asset_path(data / "KB", "50.VGA"))),
      digits_(load_vga(asset_path(data / "KB", "13.VGA"))) {
    if (atlas_.width != 251 || atlas_.height != 27 || digits_.width < 159 || digits_.height < 21)
        throw FormatError("Reality: unexpected Original sprite assets");
    const int strip = int(ball_style & 3) - 1; // saved Source Main_FormatError_RuntimeField3
    if (strip >= 0) for (int y = 0; y < 3; ++y) for (int x = 0; x < 51; ++x)
        atlas_.at(135 + x, 24 + y) = atlas_.pixels[size_t(15 + strip * 3 + y) * 251 + size_t(135 + x)];
    for (int y = 15; y < 24; ++y) for (int x = 135; x < 186; ++x) atlas_.at(x, y) = 0;
}

Image RealityFieldScreen::render(const RealityMotionFrame& frame, const Image& backdrop) const {
    if (backdrop.width != 320 || backdrop.height != 240 || frame.count > frame.sprites.size())
        throw FormatError("Reality: invalid backdrop/frame");
    Image image = backdrop;
    for (size_t index = 0; index < frame.count; ++index) {
        const auto& sprite = frame.sprites[index];
        if (sprite.figure < 0 || sprite.figure > 23 || sprite.cell < 0 || sprite.cell > 30 ||
            sprite.number < 0 || sprite.number > 11 || (sprite.number && sprite.figure >= 22))
            throw FormatError("Reality: invalid projected sprite");
        blit(image, atlas_, (sprite.cell & 15) * 16, sprite.cell & 0xf0,
             sprite.x, sprite.y + 129, 16, 11, sprite.away_palette);
        if (!sprite.number) continue;
        // SelectsTransparentIndex creates the centered shirt number in its 16x6 label cell.
        int number = sprite.number, digits = number >= 10 ? 2 : 1;
        int x = sprite.x + 8 - 3 + digits * 4 - digits * 2;
        do {
            blit(image, digits_, 129 + (number % 10) * 3, 16, x, sprite.y + 137, 3, 5, false);
            number /= 10; x -= 4;
        } while (number);
    }
    return image;
}

} // namespace bmh::ui
