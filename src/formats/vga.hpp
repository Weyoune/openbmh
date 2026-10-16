// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// .VGA images: u16 width, u16 height, u32 length, RLE data (docs/formats/VGA.md)
#include <cstdint>
#include <filesystem>
#include <vector>

#include "formats/binary.hpp"

namespace bmh {

// 8-bit indexed image, row-major
struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;

    Image() = default;
    Image(int w, int h, uint8_t fill = 0) : width(w), height(h), pixels(size_t(w) * h, fill) {}
    uint8_t& at(int x, int y) { return pixels[size_t(y) * width + x]; }
    void blit(const Image& src, int x, int y);
};

Image decode_vga(const Bytes& data);
Image load_vga(const std::filesystem::path& path);

}  // namespace bmh
