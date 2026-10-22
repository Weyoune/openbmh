// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "editor/types.hpp"
#include <cstdint>

namespace bmh::editor::images {
struct Raster {
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;
};
struct Result {
    std::string bytes, extension;
    int width = 0, height = 0;
    Json recipe;
};
Raster decode(std::string_view bytes, const std::string &extension, int vector_size = 1024);
std::string encode_png(const Raster &);
Result transform(std::string_view original, const std::string &extension, const Json &recipe);
Raster preview(std::string_view original, const std::string &extension, const Json &recipe, int maximum = 1024);
std::string base64(std::string_view);
std::string silhouette();
} // namespace bmh::editor::images
